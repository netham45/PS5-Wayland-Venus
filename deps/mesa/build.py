#!/usr/bin/env python3
"""Build pinned pacbrew RADV, Venus payloads and host Vulkan stand-ins."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
BASE = ROOT / 'build/mesa'
CACHE = ROOT / '.cache/deps'
PINS = {p['name']: p for p in json.loads((ROOT / 'deps/versions.lock.json').read_text())['dependencies']}
JOBS = os.environ.get('JOBS', str(min(os.cpu_count() or 1, 12)))


def run(args, env=None, cwd=None):
    print('+', ' '.join(map(str, args)), flush=True)
    subprocess.run(list(map(str, args)), check=True, env=env, cwd=cwd)


def fetch(name):
    pin = PINS[name]
    CACHE.mkdir(parents=True, exist_ok=True)
    archive = CACHE / pin['archive']
    if not archive.exists():
        temporary = archive.with_suffix(archive.suffix + '.partial')
        with urllib.request.urlopen(pin['url'], timeout=120) as response, temporary.open('wb') as output:
            shutil.copyfileobj(response, output)
        temporary.replace(archive)
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    if digest != pin['sha256']:
        raise RuntimeError(f'{archive}: SHA256 {digest}, expected {pin["sha256"]}')
    return archive


def source(name):
    pin = PINS[name]
    archive = fetch(name)
    destination = BASE / 'sources' / pin['source_dir']
    if not destination.exists():
        destination.parent.mkdir(parents=True, exist_ok=True)
        with tarfile.open(archive) as tar:
            tar.extractall(destination.parent, filter='data')
    return destination


def prepare(driver='radv'):
    recipe = source('pacbrew-mesa') / 'mesa'
    patches = sorted(recipe.glob('*.patch')) + sorted((ROOT / 'deps/mesa/patches').glob('*.patch'))
    if driver == 'venus':
        patches += sorted((ROOT / 'deps/mesa/venus-patches').glob('*.patch'))
    digest = hashlib.sha256(fetch('mesa').read_bytes() + b''.join(p.read_bytes() for p in patches)).hexdigest()
    suffix = '-venus' if driver == 'venus' else ''
    destination = BASE / 'sources' / (PINS['mesa']['source_dir'] + suffix)
    stamp = destination / '.ps5wl-mesa-source'
    if not stamp.exists() or stamp.read_text() != digest:
        if destination.exists():
            shutil.rmtree(destination)
        destination.parent.mkdir(parents=True, exist_ok=True)
        if suffix:
            with tarfile.open(fetch('mesa')) as tar:
                members = tar.getmembers()
                for member in members:
                    member.name = member.name.replace(PINS['mesa']['source_dir'], destination.name, 1)
                    if member.issym() or member.islnk():
                        member.linkname = member.linkname.replace(PINS['mesa']['source_dir'], destination.name, 1)
                tar.extractall(destination.parent, members=members, filter='data')
        else:
            destination = source('mesa')
        for patch in patches:
            run(['patch', '--batch', '-p1', '-i', patch], cwd=destination)
        stamp.write_text(digest)
    return destination, digest


def configure(name, src, options, env):
    binary = BASE / name
    cross_signature = None
    stamp = binary / '.ps5wl-toolchain'
    if name == 'ps5' or name.startswith('ps5-'):
        run([ROOT / 'cross/bootstrap-compat.sh'], env=env)
        inputs = [ROOT / 'build/ps5/cross.ini', ROOT / 'build/ps5/native.ini',
                  ROOT / 'cross/compiler.sh', ROOT / 'cross/public-loader.dyn',
                  ROOT / 'build/ps5/runtime/libps5wl-fd-compat.a']
        cross_signature = hashlib.sha256(b''.join(p.read_bytes() for p in inputs)).hexdigest()
    args = ['meson', 'setup', binary, src, '--libdir=lib', '--buildtype=release',
            '--wrap-mode=nofallback', '-Dauto_features=disabled']
    if (binary / 'meson-private/coredata.dat').exists():
        # Compiler-proxy arguments and injected archives are invisible to Ninja.
        # Match the compositor's clean compiler migration on signature changes.
        if cross_signature is not None and (
                not stamp.exists() or stamp.read_text() != cross_signature):
            args.append('--wipe')
        else:
            args.append('--reconfigure')
    run(args + options, env=env)
    if cross_signature is not None:
        stamp.write_text(cross_signature)
    return binary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('target', choices=['tools', 'host', 'ps5', 'prepare'])
    parser.add_argument('--driver', choices=['radv', 'venus', 'lavapipe'], default='radv')
    args = parser.parse_args()
    if os.environ.get('PS5WL_BUILD_ENV') != '1':
        raise SystemExit('Run through env/run deps/mesa/build.sh TARGET')
    if args.driver == 'lavapipe':
        run(['python3', ROOT / 'deps/mesa/lavapipe.py', args.target])
        return
    src, digest = prepare(args.driver)
    if args.target == 'prepare':
        return
    env = os.environ.copy()
    env.setdefault('CC', 'clang')
    env.setdefault('CXX', 'clang++')
    common = ['-Dgallium-drivers=', '-Dplatforms=', '-Dglx=disabled', '-Degl=disabled',
              '-Dgbm=disabled', '-Dopengl=false', '-Dgles1=disabled', '-Dgles2=disabled',
              '-Dvalgrind=disabled', '-Dlibunwind=disabled', '-Dzstd=disabled',
              '-Dexpat=disabled', '-Dxmlconfig=disabled', '-Dbuild-tests=false',
              '-Dvulkan-layers=', '-Dvideo-codecs=']
    if args.target == 'tools':
        binary = configure('tools', src, common + ['-Dvulkan-drivers=', '-Dmesa-clc=enabled',
                            '-Dinstall-mesa-clc=true', '-Dllvm=enabled', '-Dshared-llvm=enabled',
                            '-Dspirv-tools=enabled'], env)
        run(['meson', 'compile', '-C', binary, '-j', JOBS,
             'src/compiler/clc/mesa_clc', 'src/compiler/spirv/vtn_bindgen2'], env=env)
        return
    target = args.target
    dependencies = ROOT / 'build' / target / 'sysroot'
    prefix = ROOT / 'build' / target / 'venus' if args.driver == 'venus' else dependencies
    env.update(PKG_CONFIG_PATH='', PKG_CONFIG_DIR='', PKG_CONFIG_LIBDIR=os.pathsep.join(
        [str(dependencies / 'lib/pkgconfig'), str(dependencies / 'share/pkgconfig')]))
    env.pop('PKG_CONFIG_SYSROOT_DIR', None)
    env['PATH'] = os.pathsep.join([str(BASE / 'tools/src/compiler/clc'),
                                 str(BASE / 'tools/src/compiler/spirv'),
                                 str(ROOT / 'build/host/sysroot/bin'), env['PATH']])
    options = [x for x in common if x != '-Dplatforms='] + ['-Dplatforms=wayland',
               '--prefix=' + str(prefix), '--default-library=shared', '-Dmesa-clc=system',
               '-Dshader-cache=disabled', '-Dzlib=disabled']
    if target == 'ps5':
        options += ['--cross-file', ROOT / 'build/ps5/cross.ini', '--native-file',
                    ROOT / 'build/ps5/native.ini', '-Dllvm=disabled',
                    '-Dcpp_args=-I' + str(dependencies / 'include')]
        if args.driver == 'venus':
            options += ['-Dvulkan-drivers=virtio', '-Dvenus-ps5=true']
        else:
            options += ['-Dvulkan-drivers=amd', '-Dradv-winsys=ps5',
                        '-Dradv-build-id=' + digest[:40]]
    else:
        drivers = 'virtio,swrast' if args.driver == 'venus' else 'swrast'
        options += ['-Dvulkan-drivers=' + drivers, '-Dllvm=enabled', '-Dshared-llvm=enabled']
    name = target + '-venus' if args.driver == 'venus' else target
    binary = configure(name, src, options, env)
    run(['meson', 'compile', '-C', binary, '-j', JOBS], env=env)
    run(['meson', 'install', '-C', binary, '--no-rebuild'], env=env)
    if target == 'ps5':
        metadata = prefix / 'libdata/pkgconfig/vulkan.pc'
        if metadata.exists():
            metadata.replace(prefix / 'lib/pkgconfig/vulkan.pc')


if __name__ == '__main__':
    main()
