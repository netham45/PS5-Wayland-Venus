#!/usr/bin/env python3
"""Build host dependencies exclusively from the shared source lock."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile

ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / 'build/host'
PREFIX = BUILD / 'sysroot'
CACHE = ROOT / '.cache/deps'
JOBS = os.environ.get('JOBS', str(os.cpu_count() or 1))


def run(*args, cwd=None):
    print('+', ' '.join(map(str, args)), flush=True)
    subprocess.run(list(map(str, args)), cwd=cwd, check=True)


def main():
    if os.environ.get('PS5WL_BUILD_ENV') != '1':
        raise SystemExit('Run deps/host/build.sh through env/run, not build.py directly')
    pins = {p['name']: p for p in json.loads((ROOT / 'deps/versions.lock.json').read_text())['dependencies']}
    PREFIX.mkdir(parents=True, exist_ok=True)
    CACHE.mkdir(parents=True, exist_ok=True)
    os.environ.update({
        'CC': 'clang', 'CXX': 'clang++', 'AR': 'llvm-ar', 'RANLIB': 'llvm-ranlib',
        'PKG_CONFIG': 'pkg-config',
        'PKG_CONFIG_LIBDIR': str(PREFIX / 'lib/pkgconfig') + ':' + str(PREFIX / 'share/pkgconfig'),
        'PKG_CONFIG_PATH': '', 'PKG_CONFIG_SYSROOT_DIR': '',
        'PATH': str(PREFIX / 'bin') + ':' + os.environ['PATH'],
        'CFLAGS': '-O2 -g',
        'LDFLAGS': '-fuse-ld=lld -Wl,-rpath,' + str(PREFIX / 'lib'),
    })

    def source(name):
        pin = pins[name]
        archive = CACHE / pin['archive']
        if not archive.exists():
            temporary = archive.with_suffix(archive.suffix + '.part-host')
            run('curl', '--fail', '--location', '--retry', '3', '--output', temporary, pin['url'])
            temporary.replace(archive)
        actual = hashlib.file_digest(archive.open('rb'), 'sha256').hexdigest()
        if actual != pin['sha256']:
            raise SystemExit(f'SHA256 mismatch for {archive}: expected {pin["sha256"]}, got {actual}')
        dest = BUILD / 'sources' / pin['source_dir']
        marker = dest / '.ps5wl-source-sha256'
        if not marker.exists() or marker.read_text() != actual:
            if dest.exists():
                shutil.rmtree(dest)
            dest.parent.mkdir(parents=True, exist_ok=True)
            with tarfile.open(archive) as tar:
                tar.extractall(dest.parent, filter='data')
            marker.write_text(actual)
        return dest

    def meson(name, options):
        src = source(name)
        out = BUILD / 'deps' / name
        out.parent.mkdir(parents=True, exist_ok=True)
        args = ['meson', 'setup', out, src, '--prefix=' + str(PREFIX), '--libdir=lib',
                '--buildtype=release', '--wrap-mode=nodownload', '-Dauto_features=disabled']
        if (out / 'meson-private/coredata.dat').exists():
            args.append('--reconfigure')
        run(*args, *options)
        run('meson', 'compile', '-C', out, '-j', JOBS)
        run('meson', 'install', '-C', out, '--no-rebuild')

    src = source('libffi')
    out = BUILD / 'deps/libffi'
    out.mkdir(parents=True, exist_ok=True)
    if not (out / 'Makefile').exists():
        run(src / 'configure', '--prefix=' + str(PREFIX), '--libdir=' + str(PREFIX / 'lib'),
            '--disable-docs', '--disable-multi-os-directory', cwd=out)
    run('make', '-j' + JOBS, cwd=out)
    run('make', 'install', cwd=out)

    src = source('expat')
    out = BUILD / 'deps/expat'
    run('cmake', '-S', src, '-B', out, '-G', 'Ninja',
        '-DCMAKE_INSTALL_PREFIX=' + str(PREFIX), '-DCMAKE_INSTALL_LIBDIR=lib',
        '-DCMAKE_BUILD_TYPE=Release', '-DEXPAT_BUILD_TESTS=OFF', '-DEXPAT_BUILD_EXAMPLES=OFF',
        '-DEXPAT_BUILD_TOOLS=OFF', '-DEXPAT_SHARED_LIBS=ON', '-DEXPAT_BUILD_DOCS=OFF')
    run('cmake', '--build', out, '-j', JOBS)
    run('cmake', '--install', out)

    # Linux already has epoll. epoll-shim is only built for the FreeBSD-derived PS5.
    meson('wayland', ['-Dtests=false', '-Ddocumentation=false', '-Ddtd_validation=false', '-Dscanner=true'])
    meson('wayland-protocols', ['-Dtests=false'])
    meson('pixman', ['-Dtests=disabled', '-Ddemos=disabled', '-Dgtk=disabled', '-Dlibpng=disabled'])
    meson('xkeyboard-config', ['-Dnls=false'])
    meson('libxkbcommon', ['-Denable-tools=false', '-Denable-x11=false', '-Denable-docs=false',
                          '-Denable-wayland=false', '-Denable-xkbregistry=false', '-Denable-bash-completion=false',
                          '-Dxkb-config-root=' + str(PREFIX / 'share/X11/xkb'),
                          '-Dxkb-config-extra-path=' + str(PREFIX / 'etc/xkb')])
    meson('libdrm', ['-Dtests=false', '-Dman-pages=disabled'])
    # Headless, Wayland and Pixman are unconditional upstream components; empty
    # arrays disable all optional DRM/libinput/X11 backends and GPU renderers.
    meson('wlroots', ['-Dbackends=[]', '-Drenderers=[]', '-Dallocators=[]',
                     '-Dsession=disabled', '-Dxwayland=disabled', '-Dexamples=false',
                     '-Dcolor-management=disabled', '-Dlibliftoff=disabled', '-Dxcb-errors=disabled'])
    print('Host sysroot:', PREFIX)
    print('Native scanner:', PREFIX / 'bin/wayland-scanner')


if __name__ == '__main__':
    main()
