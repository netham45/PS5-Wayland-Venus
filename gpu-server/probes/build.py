#!/usr/bin/env python3
"""Build GPU console probes inside env/run using the shim-linking compiler."""
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'build/ps5/gpu-probes'


def run(command, **kwargs):
    print('+', ' '.join(map(str, command)), flush=True)
    subprocess.run(list(map(str, command)), check=True, **kwargs)


def main():
    if os.environ.get('PS5WL_BUILD_ENV') != '1':
        raise SystemExit('Use env/run python3 gpu-server/probes/build.py')
    OUT.mkdir(parents=True, exist_ok=True)
    mesa = ROOT / 'build/mesa/sources/mesa-26.2.0/include'
    driver = ROOT / 'build/ps5/sysroot/lib/libvulkan.so.1'
    shutil.copyfile(driver, OUT / 'radv.so')
    run(['objcopy', '-I', 'binary', '-O', 'elf64-x86-64', '-B', 'i386:x86-64',
         'radv.so', 'radv.o'], cwd=OUT)
    common = [ROOT / 'cross/bin/prospero-clang', '-O2', '-g', '-Wall', '-Wextra',
              '-I' + str(mesa), '-I' + str(ROOT / 'common/ps5log'),
              '-I' + str(ROOT / 'launcher'),
              ROOT / 'common/ps5log/ps5log.c', '-pthread', '-lSceSystemService']
    c1 = [ROOT / 'gpu-server/probes/c1.c']
    run(common + c1 + ['-o', OUT / 'ps5wl-gpu-c1-plain.elf'])
    radv = [OUT / 'radv.o', '-lSceAgc', '-lSceAgcDriver']
    run(common + c1 + ['-DPROBE_APP'] + radv +
        ['-o', OUT / 'ps5wl-gpu-c1-app.elf'])
    run(common + c1 + ['-DPROBE_APP', '-DPROBE_C1_REMAINING'] + radv +
        ['-o', OUT / 'ps5wl-gpu-c1-remaining.elf'])
    gpu = [ROOT / 'gpu-server/probes/gpu.c']
    run(common + gpu + ['-DPROBE_VERIFY', '-o', OUT / 'ps5wl-gpu-c2-verify.elf'])
    for size in (16384, 8 << 20, 64 << 20):
        run(common + gpu + [f'-DPROBE_BYTES={size}'] + radv +
            ['-o', OUT / f'ps5wl-gpu-c2a-{size}.elf'])
    run(common + gpu + ['-DPROBE_APP'] + radv +
        ['-o', OUT / 'ps5wl-gpu-c2b.elf'])


if __name__ == '__main__':
    main()
