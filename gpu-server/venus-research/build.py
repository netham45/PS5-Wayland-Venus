#!/usr/bin/env python3
"""Build pinned Venus/vtest in isolated host or PS5 prefixes (inside env/run)."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / 'build/gpu-server'
PINS = {p['name']: p for p in json.loads((ROOT / 'deps/versions.lock.json').read_text())['dependencies']}
JOBS = os.environ.get('JOBS', str(min(os.cpu_count() or 1, 12)))


def run(args, env=None, cwd=None):
    print('+', ' '.join(map(str, args)), flush=True)
    subprocess.run(list(map(str, args)), check=True, env=env, cwd=cwd)


def source(name):
    pin = PINS[name]
    cache = ROOT / '.cache/deps' / pin['archive']
    cache.parent.mkdir(parents=True, exist_ok=True)
    if not cache.exists():
        with urllib.request.urlopen(pin['url'], timeout=120) as response, cache.open('wb') as output:
            shutil.copyfileobj(response, output)
    if hashlib.sha256(cache.read_bytes()).hexdigest() != pin['sha256']:
        raise RuntimeError(f'{cache}: checksum mismatch')
    dest = BASE / 'sources' / pin['source_dir']
    if not dest.exists():
        dest.parent.mkdir(parents=True, exist_ok=True)
        with tarfile.open(cache) as archive:
            archive.extractall(dest.parent, filter='data')
    return dest


def configure(name, src, options, env):
    dest = BASE / name
    args = ['meson', 'setup', dest, src, '--libdir=lib', '--buildtype=release',
            '--wrap-mode=nofallback']
    if (dest / 'meson-private/coredata.dat').exists():
        args.append('--reconfigure')
    run(args + options, env)
    run(['meson', 'compile', '-C', dest, '-j', JOBS], env)
    run(['meson', 'install', '-C', dest, '--no-rebuild'], env)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('target', choices=['host', 'ps5'])
    parser.add_argument('--upstream', action='store_true', help='omit renderer host-pointer patch')
    args = parser.parse_args()
    if os.environ.get('PS5WL_BUILD_ENV') != '1':
        raise SystemExit('Run env/run python3 gpu-server/build.py TARGET')
    target = args.target
    prefix = BASE / target / 'sysroot'
    deps = ROOT / 'build' / target / 'sysroot'
    env = os.environ.copy()
    env.update(CC='clang', CXX='clang++', PKG_CONFIG_PATH='', PKG_CONFIG_DIR='',
               PKG_CONFIG_LIBDIR=os.pathsep.join(str(p / d) for p in [prefix, deps]
                                               for d in ['lib/pkgconfig', 'share/pkgconfig']))
    env.pop('PKG_CONFIG_SYSROOT_DIR', None)
    env['PATH'] = str(ROOT / 'build/host/sysroot/bin') + os.pathsep + env['PATH']
    cross = [] if target == 'host' else ['--cross-file', ROOT / 'build/ps5/cross.ini',
                                       '--native-file', ROOT / 'build/ps5/native.ini']
    common = ['--prefix=' + str(prefix), '--default-library=shared'] + cross
    configure(target + '/epoxy', source('libepoxy'), common +
              ['-Dglx=no', '-Degl=no', '-Dx11=false', '-Dtests=false', '-Ddocs=false'], env)
    virgl = source('virglrenderer')
    patches = sorted((ROOT / 'gpu-server/patches').glob('*.patch'))
    if args.upstream:
        patches = patches[:1]
    stamp = virgl / '.ps5wl-patches'
    digest = hashlib.sha256(b''.join(p.read_bytes() for p in patches)).hexdigest()
    if not stamp.exists() or stamp.read_text() != digest:
        shutil.rmtree(virgl)
        virgl = source('virglrenderer')
        for patch in patches:
            run(['patch', '--batch', '-p1', '-i', patch], cwd=virgl)
        stamp.write_text(digest)
    configure(target + '/virgl', virgl, common + ['-Dplatforms=', '-Dvenus=true',
              '-Dvulkan-dload=true', '-Drender-server-worker=thread', '-Dtests=false'], env)
    spec = importlib.util.spec_from_file_location('mesa_recipe', ROOT / 'deps/mesa/build.py')
    mesa = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mesa)
    mesa.BASE = BASE / 'mesa'
    src, _ = mesa.prepare()
    mesa_patches = sorted((ROOT / 'gpu-server/mesa-patches').glob('*.patch'))
    mesa_stamp = src / '.ps5wl-gpu-server'
    mesa_digest = hashlib.sha256(b''.join(p.read_bytes() for p in mesa_patches)).hexdigest()
    if not mesa_stamp.exists() or mesa_stamp.read_text() != mesa_digest:
        if mesa_stamp.exists():
            shutil.rmtree(src)
            src, _ = mesa.prepare()
        for patch in mesa_patches:
            run(['patch', '--batch', '-p1', '-i', patch], cwd=src)
        mesa_stamp.write_text(mesa_digest)
    options = ['-Dgallium-drivers=', '-Dvulkan-drivers=virtio', '-Dplatforms=wayland',
               '-Dglx=disabled', '-Degl=disabled', '-Dgbm=disabled', '-Dopengl=false',
               '-Dgles1=disabled', '-Dgles2=disabled', '-Dllvm=disabled',
               '-Dvalgrind=disabled', '-Dlibunwind=disabled', '-Dzstd=disabled',
               '-Dexpat=disabled', '-Dxmlconfig=disabled', '-Dbuild-tests=false',
               '-Dvulkan-layers=', '-Dvideo-codecs=', '-Dshader-cache=disabled',
               '-Dzlib=disabled', '-Dmesa-clc=system']
    if target == 'host':
        options = [x for x in options if x not in
                   ['-Dvulkan-drivers=virtio', '-Dllvm=disabled']]
        options += ['-Dvulkan-drivers=virtio,swrast', '-Dllvm=enabled', '-Dshared-llvm=enabled']
    env['PATH'] = os.pathsep.join([str(ROOT / 'build/mesa/tools/src/compiler/clc'),
                                 str(ROOT / 'build/mesa/tools/src/compiler/spirv'), env['PATH']])
    configure(target + '/venus', src, common + options, env)


if __name__ == '__main__':
    main()
