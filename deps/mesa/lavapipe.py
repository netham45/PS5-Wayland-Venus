#!/usr/bin/env python3
"""Build standalone lavapipe with pinned static LLVM and Wayland software WSI."""
import argparse
import hashlib
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import tarfile

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('mesa_build', ROOT / 'deps/mesa/build.py')
mesa = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mesa)


def prepare():
    recipe = mesa.source('pacbrew-mesa') / 'mesa'
    patches = sorted(recipe.glob('*.patch')) + sorted((ROOT / 'deps/mesa/patches').glob('*.patch'))
    patches += sorted((ROOT / 'deps/mesa/lavapipe-patches').glob('*.patch'))
    archive = mesa.fetch('mesa')
    digest = hashlib.sha256(archive.read_bytes() + b''.join(p.read_bytes() for p in patches)).hexdigest()
    src = mesa.BASE / 'lavapipe-source' / mesa.PINS['mesa']['source_dir']
    stamp = src / '.ps5wl-source'
    if not stamp.exists() or stamp.read_text() != digest:
        if src.exists():
            shutil.rmtree(src)
        src.parent.mkdir(parents=True, exist_ok=True)
        with tarfile.open(archive) as tar:
            tar.extractall(src.parent, filter='data')
        for patch in patches:
            mesa.run(['patch', '--batch', '-p1', '-i', patch], cwd=src)
        stamp.write_text(digest)
    return src


def llvm_config(target):
    host = ROOT / 'build/llvm/host'
    if target == 'host':
        return host / 'bin/llvm-config'
    # llvm-config itself runs on the build host. Component closures come from
    # the identical host build; paths and system libraries name the PS5 build.
    wrapper = ROOT / 'build/llvm/ps5/llvm-config-host'
    wrapper.parent.mkdir(parents=True, exist_ok=True)
    prefix = ROOT / 'build/llvm/ps5/sysroot'
    wrapper.write_text('#!/usr/bin/env python3\nimport subprocess, sys\n'
                       'args = sys.argv[1:]\n'
                       'if "--system-libs" in args:\n    print("-lpthread")\n'
                       'elif "--host-target" in args:\n    print("x86_64-sie-ps5")\n'
                       'else:\n'
                       f'    out = subprocess.check_output([{str(host / "bin/llvm-config")!r}] + args, text=True)\n'
                       f'    out = out.replace({str(host / "include")!r}, {str(prefix / "include")!r})\n'
                       f'    out = out.replace({str(host / "lib")!r}, {str(prefix / "lib")!r})\n'
                       f'    out = out.replace({str(ROOT / "build/llvm/sources" / mesa.PINS["llvm"]["source_dir"] / "llvm/include")!r}, {str(prefix / "include")!r})\n'
                       '    print(out, end="")\n')
    wrapper.chmod(0o755)
    return wrapper


def build(target):
    if os.environ.get('PS5WL_BUILD_ENV') != '1':
        raise SystemExit('Run through env/run python3 deps/mesa/lavapipe.py TARGET')
    mesa.run(['python3', ROOT / 'deps/llvm/build.py', target])
    src = prepare()
    prefix = mesa.BASE / (target + '-lavapipe/sysroot')
    dependency_prefix = ROOT / 'build' / target / 'sysroot'
    env = os.environ.copy()
    env.update(CC='clang', CXX='clang++', LLVM_CONFIG=str(llvm_config(target)),
               PKG_CONFIG_PATH='', PKG_CONFIG_DIR='', PKG_CONFIG_LIBDIR=os.pathsep.join(
                   str(dependency_prefix / p) for p in ['lib/pkgconfig', 'share/pkgconfig']))
    env.pop('PKG_CONFIG_SYSROOT_DIR', None)
    env['PATH'] = os.pathsep.join([str(ROOT / 'build/host/sysroot/bin'), env['PATH']])
    options = [
        '--prefix=' + str(prefix), '--default-library=shared', '-Dplatforms=wayland',
        '-Dvulkan-drivers=swrast', '-Dgallium-drivers=', '-Dllvm=enabled',
        '-Dshared-llvm=disabled', '-Dllvm-orcjit=false', '-Ddraw-use-llvm=true',
        '-Dcpp_rtti=false', '-Dglx=disabled', '-Degl=disabled', '-Dgbm=disabled',
        '-Dopengl=false', '-Dgles1=disabled', '-Dgles2=disabled',
        '-Dvalgrind=disabled', '-Dlibunwind=disabled', '-Dzstd=disabled',
        '-Dzlib=disabled', '-Dexpat=disabled', '-Dxmlconfig=disabled',
        '-Dbuild-tests=false', '-Dvulkan-layers=', '-Dvideo-codecs=',
        '-Dshader-cache=disabled', '-Dmesa-clc=disabled',
    ]
    if target == 'ps5':
        cross = mesa.BASE / 'ps5-lavapipe-llvm.ini'
        cross.write_text('[binaries]\nllvm-config = ' + repr(str(llvm_config(target))) + '\n')
        options += ['--cross-file', ROOT / 'build/ps5/cross.ini', '--cross-file', cross,
                    '--native-file', ROOT / 'build/ps5/native.ini',
                    '-Dcpp_args=-I' + str(dependency_prefix / 'include')]
    binary = mesa.configure(target + '-lavapipe', src, options, env)
    mesa.run(['meson', 'compile', '-C', binary, '-j', mesa.JOBS], env=env)
    mesa.run(['meson', 'install', '-C', binary, '--no-rebuild'], env=env)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('target', choices=['host', 'ps5'])
    build(parser.parse_args().target)


if __name__ == '__main__':
    main()
