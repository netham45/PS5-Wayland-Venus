#!/usr/bin/env python3
"""Build source-pinned static libraries using only the container and PS5 SDK."""
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
BUILD = ROOT / 'build/ps5'
PREFIX = BUILD / 'sysroot'
CACHE = ROOT / '.cache/deps'
LOCK = json.loads((ROOT / 'deps/versions.lock.json').read_text())
PINS = {item['name']: item for item in LOCK['dependencies']}
JOBS = os.environ.get('JOBS', str(min(os.cpu_count() or 1, 12)))
ENV = os.environ.copy()
ENV.update(PKG_CONFIG_PATH='', PKG_CONFIG_DIR='',
           PKG_CONFIG_LIBDIR=str(PREFIX / 'lib/pkgconfig'))
ENV.pop('PKG_CONFIG_SYSROOT_DIR', None)


def run(args, cwd=None):
    print('+', ' '.join(map(str, args)), flush=True)
    subprocess.run(list(map(str, args)), cwd=cwd, env=ENV, check=True)


def download(name):
    pin = PINS[name]
    CACHE.mkdir(parents=True, exist_ok=True)
    archive = CACHE / pin['archive']
    if not archive.exists():
        temporary = archive.with_suffix(archive.suffix + '.partial')
        with urllib.request.urlopen(pin['url'], timeout=120) as response:
            with temporary.open('wb') as output:
                shutil.copyfileobj(response, output)
        temporary.replace(archive)
    actual = hashlib.sha256(archive.read_bytes()).hexdigest()
    if actual != pin['sha256']:
        raise RuntimeError(f'{archive}: SHA256 {actual}, expected {pin["sha256"]}')
    print(f'{name}: verified SHA256 {actual}', flush=True)
    return archive


def source(name):
    pin = PINS[name]
    archive = download(name)
    patches = sorted((ROOT / 'deps/ps5/patches' / name).glob('*.patch'))
    identity = hashlib.sha256(archive.read_bytes() + b''.join(p.read_bytes() for p in patches)).hexdigest()
    destination = BUILD / 'sources' / name
    stamp = destination / '.ps5wl-source'
    if not stamp.exists() or stamp.read_text() != identity:
        if destination.exists():
            shutil.rmtree(destination)
        work = BUILD / 'sources' / (name + '.extract')
        work.mkdir(parents=True, exist_ok=True)
        with tarfile.open(archive) as tar:
            tar.extractall(work, filter='data')
        (work / pin['source_dir']).rename(destination)
        shutil.rmtree(work)
        for patch in patches:
            run(['patch', '-p1', '-i', patch], destination)
        stamp.write_text(identity)
        binary = BUILD / 'deps' / name
        if binary.exists():
            shutil.rmtree(binary)
    return destination


def meson(name, options):
    src = source(name)
    binary = BUILD / 'deps' / name
    identity = hashlib.sha256((BUILD / 'cross.ini').read_bytes()).hexdigest()
    stamp = binary / '.ps5wl-cross-config'
    # Meson caches the compiler and cannot migrate it with --reconfigure.
    if binary.exists() and (not stamp.exists() or stamp.read_text() != identity):
        shutil.rmtree(binary)
    args = ['meson', 'setup', binary, src, '--cross-file', BUILD / 'cross.ini',
            '--native-file', BUILD / 'native.ini',
            '--prefix', PREFIX, '--libdir=lib', '--default-library=static',
            '--wrap-mode=nofallback', '-Db_staticpic=true', '-Dauto_features=disabled']
    if (binary / 'meson-private/coredata.dat').exists():
        args.append('--reconfigure')
    run(args + options)
    stamp.write_text(identity)
    # xkbcommon 1.11 has no tests-off option; build its sole enabled installable library.
    targets = ['xkbcommon'] if name == 'libxkbcommon' else []
    run(['meson', 'compile', '-C', binary, '-j', JOBS] + targets)
    run(['meson', 'install', '-C', binary, '--no-rebuild'])
    # FreeBSD libraries default to libdata; data-only projects use share.
    # Our contract is lib/pkgconfig for all metadata, not extra search paths.
    canonical_pc = PREFIX / 'lib/pkgconfig'
    canonical_pc.mkdir(parents=True, exist_ok=True)
    for location in ('libdata/pkgconfig', 'share/pkgconfig'):
        installed_pc = PREFIX / location
        if installed_pc.exists():
            for pc in installed_pc.glob('*.pc'):
                pc.replace(canonical_pc / pc.name)
            installed_pc.rmdir()


def compat():
    binary = BUILD / 'deps/ps5compat'
    binary.mkdir(parents=True, exist_ok=True)
    library = PREFIX / 'lib/libps5compat.a'
    library.parent.mkdir(parents=True, exist_ok=True)
    run(['prospero-clang', '-std=c11', '-O2', '-fPIC', '-c',
         ROOT / 'deps/ps5/shim/getline.c', '-o', binary / 'getline.o'])
    run(['prospero-ar', 'rcs', library, binary / 'getline.o'])
    pc = PREFIX / 'lib/pkgconfig/ps5compat.pc'
    pc.parent.mkdir(parents=True, exist_ok=True)
    pc.write_text(f'prefix={PREFIX}\nName: ps5compat\nDescription: PS5 POSIX getline compatibility\nVersion: 1\nLibs: -L${{prefix}}/lib -lps5compat\n')


def input_codes():
    header = download('linux-input-event-codes')
    target = PREFIX / 'include/linux'
    target.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(header, target / 'input-event-codes.h')


def ffi():
    src = source('libffi')
    binary = BUILD / 'deps/libffi'
    binary.mkdir(parents=True, exist_ok=True)
    if not (binary / 'Makefile').exists():
        run(['env', 'LIBFFI_PS5=yes', src / 'configure', '--host=x86_64-unknown-freebsd', f'--prefix={PREFIX}',
             '--libdir=' + str(PREFIX / 'lib'), '--disable-shared', '--enable-static',
             '--with-pic', '--disable-docs', '--disable-multi-os-directory',
             'CC=prospero-clang', 'CXX=prospero-clang++', 'AR=prospero-ar', 'CFLAGS=-O2 -DLIBFFI_PS5',
             'RANLIB=prospero-ranlib', 'NM=prospero-nm'], binary)
    run(['make', '-j', JOBS], binary)
    run(['make', 'install'], binary)


def epoll():
    src = source('epoll-shim')
    binary = BUILD / 'deps/epoll-shim'
    run(['cmake', '-S', src, '-B', binary, '-G', 'Ninja',
         '-DCMAKE_TOOLCHAIN_FILE=' + str(ROOT / 'cross/prospero.cmake'),
         '-DCMAKE_INSTALL_PREFIX=' + str(PREFIX), '-DCMAKE_INSTALL_LIBDIR=lib',
         '-DCMAKE_INSTALL_PKGCONFIGDIR=' + str(PREFIX / 'lib/pkgconfig'), '-DBUILD_SHARED_LIBS=OFF',
         '-DBUILD_TESTING=OFF', '-DALLOWS_ONESHOT_TIMERS_WITH_TIMEOUT_ZERO=OFF'])
    run(['cmake', '--build', binary, '-j', JOBS])
    run(['cmake', '--install', binary])


def main():
    tasks = [
        ('ps5compat', compat),
        ('linux-input-event-codes', input_codes),
        ('libffi', ffi),
        ('epoll-shim', epoll),
        ('wayland', lambda: meson('wayland', ['-Dscanner=false', '-Dtests=false', '-Ddocumentation=false', '-Dlibraries=true'])),
        ('wayland-protocols', lambda: meson('wayland-protocols', ['-Dtests=false'])),
        ('pixman', lambda: meson('pixman', ['-Dtests=disabled', '-Ddemos=disabled', '-Dgtk=disabled'])),
        ('xkeyboard-config', lambda: meson('xkeyboard-config', [])),
        ('libxkbcommon', lambda: meson('libxkbcommon', ['-Denable-x11=false', '-Denable-tools=false', '-Denable-docs=false', '-Denable-xkbregistry=false', '-Denable-wayland=false', '-Denable-bash-completion=false', '-Dxkb-config-root=/system_tmp/ps5wl/share/X11/xkb'])),
        ('libdrm', lambda: meson('libdrm', ['-Dtests=false', '-Dudev=false', '-Dinstall-test-programs=false'])),
        # Pixman, headless/Wayland backends and shm allocator are unconditional in 0.20.
        ('wlroots', lambda: meson('wlroots', ['-Dbackends=[]', '-Drenderers=[]', '-Dallocators=[]', '-Dsession=disabled', '-Dxwayland=disabled', '-Dexamples=false'])),
    ]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--only', choices=[name for name, _ in tasks],
                        help='Rebuild one target dependency after its prerequisites')
    args = parser.parse_args()
    if os.environ.get('PS5WL_BUILD_ENV') != '1':
        raise SystemExit('Use deps/ps5/build.sh inside env/run')
    PREFIX.mkdir(parents=True, exist_ok=True)
    shutil.copytree(ROOT / 'deps/ps5/shim/include', PREFIX / 'include', dirs_exist_ok=True)
    scanner = ROOT / 'build/host/sysroot/bin/wayland-scanner'
    if not scanner.is_file() and args.only not in ('libffi', 'epoll-shim'):
        raise SystemExit('Build pinned host dependencies first: make deps-host (native wayland-scanner required)')
    ENV['PATH'] = str(scanner.parent) + os.pathsep + ENV['PATH']
    for name, build in tasks:
        if args.only and args.only != name:
            continue
        build()
        print(f'{name}: installed successfully', flush=True)


if __name__ == '__main__':
    main()
