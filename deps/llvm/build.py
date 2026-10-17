#!/usr/bin/env python3
"""Build static, X86-only LLVM for the host and PS5 lavapipe MCJIT."""
import argparse
import hashlib
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import tarfile

ROOT = Path(__file__).resolve().parents[2]
BASE = ROOT / 'build/llvm'
spec = importlib.util.spec_from_file_location('mesa_build', ROOT / 'deps/mesa/build.py')
mesa = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mesa)


def run(args):
    print('+', ' '.join(map(str, args)), flush=True)
    subprocess.run(list(map(str, args)), check=True)


def prepare():
    archive = mesa.fetch('llvm')
    recipe = mesa.source('pacbrew-mesa') / 'llvm'
    patches = sorted(recipe.glob('*.patch')) + sorted((ROOT / 'deps/llvm/patches').glob('*.patch'))
    digest = hashlib.sha256(archive.read_bytes() + b''.join(p.read_bytes() for p in patches)).hexdigest()
    src = BASE / 'sources' / mesa.PINS['llvm']['source_dir']
    stamp = src / '.ps5wl-source'
    if not stamp.exists() or stamp.read_text() != digest:
        if src.exists():
            shutil.rmtree(src)
        src.parent.mkdir(parents=True, exist_ok=True)
        with tarfile.open(archive) as tar:
            tar.extractall(src.parent, filter='data')
        for patch in patches:
            subprocess.run(['patch', '--batch', '-p1', '-i', str(patch)], cwd=src, check=True)
        stamp.write_text(digest)
    return src


def build(target, src):
    binary = BASE / target
    prefix = binary / 'sysroot'
    options = [
        '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_POSITION_INDEPENDENT_CODE=ON',
        '-DLLVM_TARGETS_TO_BUILD=X86', '-DLLVM_ENABLE_PROJECTS=',
        '-DLLVM_BUILD_LLVM_DYLIB=OFF', '-DLLVM_LINK_LLVM_DYLIB=OFF',
        '-DLLVM_ENABLE_PLUGINS=OFF', '-DLLVM_ENABLE_BACKTRACES=OFF',
        '-DLLVM_ENABLE_LIBXML2=OFF', '-DLLVM_ENABLE_ZLIB=OFF',
        '-DLLVM_ENABLE_TERMINFO=OFF', '-DLLVM_ENABLE_LIBEDIT=OFF',
        '-DLLVM_INCLUDE_TESTS=OFF', '-DLLVM_INCLUDE_EXAMPLES=OFF',
        '-DLLVM_INCLUDE_BENCHMARKS=OFF', '-DLLVM_BUILD_TOOLS=OFF',
        '-DLLVM_ENABLE_RTTI=OFF', '-DLLVM_ENABLE_EH=OFF',
        '-DLLVM_ENABLE_BINDINGS=OFF', '-DLLVM_INCLUDE_UTILS=OFF',
        '-DLLVM_TOOL_LTO_BUILD=OFF',
        '-DCMAKE_C_COMPILER=clang', '-DCMAKE_CXX_COMPILER=clang++',
    ]
    if target == 'ps5':
        options += [
            '-DCMAKE_TOOLCHAIN_FILE=' + str(ROOT / 'cross/prospero.cmake'),
            '-DLLVM_HOST_TRIPLE=x86_64-sie-ps5', '-DLLVM_DEFAULT_TARGET_TRIPLE=x86_64-sie-ps5',
            '-DLLVM_TABLEGEN=' + str(BASE / 'host/bin/llvm-tblgen'),
            '-DLLVM_ENABLE_PIC=ON', '-DLLVM_ENABLE_THREADS=ON',
            '-DCMAKE_C_FLAGS_RELEASE=-O3 -DNDEBUG -fno-omit-frame-pointer',
            '-DCMAKE_CXX_FLAGS_RELEASE=-O3 -DNDEBUG -fno-omit-frame-pointer',
        ]
    fresh = [] if (binary / 'build.ninja').exists() else ['--fresh']
    run(['cmake'] + fresh + ['-G', 'Ninja', '-S', src / 'llvm', '-B', binary] + options)
    if target == 'host':
        run(['cmake', '--build', binary, '--target', 'llvm-tblgen', 'llvm-config', '-j', mesa.JOBS])
    run(['cmake', '--build', binary, '-j', mesa.JOBS])
    # The repository toolchain fixes the generic prefix, so override the install
    # destination here rather than contaminating the shared dependency sysroot.
    run(['cmake', '--install', binary, '--prefix', prefix])
    return prefix


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('target', choices=['host', 'ps5'])
    args = parser.parse_args()
    if os.environ.get('PS5WL_BUILD_ENV') != '1':
        raise SystemExit('Run through env/run python3 deps/llvm/build.py TARGET')
    os.environ.setdefault('PS5_PAYLOAD_SDK', str(ROOT / '.sdk'))
    src = prepare()
    build('host', src)
    if args.target == 'ps5':
        build('ps5', src)


if __name__ == '__main__':
    main()
