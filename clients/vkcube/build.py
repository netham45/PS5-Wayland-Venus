#!/usr/bin/env python3
"""Build pinned Vulkan-Tools cube and GPU smoke against this tree's Mesa."""
import argparse
import hashlib
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('mesa_build', ROOT / 'deps/mesa/build.py')
mesa = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mesa)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('target', choices=['host', 'ps5'])
    args = parser.parse_args()
    if os.environ.get('PS5WL_BUILD_ENV') != '1':
        raise SystemExit('Use env/run clients/vkcube/build.sh host|ps5')
    target = args.target
    prefix = ROOT / 'build' / target / 'sysroot'
    binary = ROOT / 'build' / target / 'vkcube'
    binary.mkdir(parents=True, exist_ok=True)
    patches = sorted((ROOT / 'clients/vkcube/patches').glob('*.patch'))
    digest = hashlib.sha256(mesa.fetch('vulkan-tools').read_bytes() + b''.join(p.read_bytes() for p in patches)).hexdigest()
    src = mesa.BASE / 'sources' / mesa.PINS['vulkan-tools']['source_dir']
    stamp = src / '.ps5wl-cube-source'
    if not stamp.exists() or stamp.read_text() != digest:
        if src.exists():
            shutil.rmtree(src)
        src = mesa.source('vulkan-tools')
        for patch in patches:
            mesa.run(['patch', '--batch', '-p1', '-i', patch], cwd=src)
        stamp.write_text(digest)
    cube = src / 'cube'
    scanner = ROOT / 'build/host/sysroot/bin/wayland-scanner'
    protocols = prefix / 'share/wayland-protocols'
    generated = []
    for name, xml in [('xdg-shell', protocols / 'stable/xdg-shell/xdg-shell.xml'),
                      ('xdg-decoration', protocols / 'unstable/xdg-decoration/xdg-decoration-unstable-v1.xml')]:
        header = binary / (name + '-client-header.h')
        code = binary / (name + '-code.c')
        mesa.run([scanner, 'client-header', xml, header])
        mesa.run([scanner, 'private-code', xml, code])
        generated.append(code)
    env = os.environ.copy()
    env.update(PKG_CONFIG_PATH='', PKG_CONFIG_DIR='', PKG_CONFIG_LIBDIR=str(prefix / 'lib/pkgconfig'))
    pkg = ROOT / ('cross/pkg-config' if target == 'ps5' else 'cross/host-pkg-config')
    flags = subprocess.check_output([str(pkg), '--cflags', '--libs', '--static', 'wayland-client'], env=env, text=True).split()
    compiler = 'prospero-clang' if target == 'ps5' else 'clang'
    common = ['-O2', '-g', '-I' + str(cube), '-I' + str(binary),
              '-I' + str(ROOT / 'build/mesa/sources' / mesa.PINS['mesa']['source_dir'] / 'include'),
              '-I' + str(ROOT / 'common/ps5log'), '-I' + str(ROOT / 'common/ps5wl-client')]
    libs = ['-lm', '-pthread']
    if target == 'ps5':
        common += ['-DPS5WL_PS5=1', '-I' + str(prefix / 'include')]
        library = prefix / 'lib/libvulkan.so.1'
        shutil.copyfile(library, binary / 'radv.so')
        mesa.run(['objcopy', '-I', 'binary', '-O', 'elf64-x86-64', '-B', 'i386:x86-64',
                  'radv.so', 'radv.o'], cwd=binary)
        libs += [binary / 'radv.o', '-lSceAgc', '-lSceAgcDriver']
    else:
        libs += ['-ldl']
    runtime = [ROOT / 'clients/vkcube/runtime.c', ROOT / 'common/ps5log/ps5log.c']
    mesa.run([compiler] + common + [ROOT / 'clients/vkcube/vktest.c'] + runtime + libs +
             ['-o', binary / ('ps5wl-vktest.elf' if target == 'ps5' else 'ps5wl-vktest')], env=env)
    mesa.run([compiler] + common + ['-DVK_USE_PLATFORM_WAYLAND_KHR', cube / 'cube.c'] + generated + runtime +
             [ROOT / 'common/ps5wl-client/ps5wl-client.c'] + flags + libs +
             ['-o', binary / ('ps5wl-vkcube.elf' if target == 'ps5' else 'ps5wl-vkcube')], env=env)


if __name__ == '__main__':
    main()
