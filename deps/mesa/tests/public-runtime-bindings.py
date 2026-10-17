#!/usr/bin/env python3
"""Check the final PS5 ICD, including its version script, not a model DSO."""
import argparse
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[3]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('driver', choices=['radv', 'venus', 'lavapipe'])
    args = parser.parse_args()
    if os.environ.get('PS5WL_BUILD_ENV') != '1':
        parser.error('run through env/run')
    libraries = {
        'radv': ROOT / 'build/ps5/sysroot/lib/libvulkan.so.1',
        'venus': ROOT / 'build/ps5/venus/lib/libvulkan-venus.so.1',
        'lavapipe': ROOT / 'build/mesa/ps5-lavapipe/sysroot/lib/libvulkan_lvp.so',
    }
    library = libraries[args.driver]
    relocations = subprocess.check_output(['llvm-readelf', '-rW', str(library)], text=True)
    required = ['dlopen', 'dlsym']
    if args.driver == 'lavapipe':
        required += ['dlclose', 'dlerror', 'mmap', 'mprotect']
    for symbol in required:
        if not re.search(r'R_X86_64_(?:GLOB_DAT|JUMP_SLOT)\s+\S+\s+' + symbol + r'(?:\s|$)', relocations):
            raise RuntimeError(f'{library}: public {symbol} has no interposable GOT/PLT relocation')
    resolver = subprocess.check_output(
        ['llvm-objdump', '-d', '--disassemble-symbols=resolve_poll', str(library)], text=True)
    if '<resolve_poll>:' not in resolver:
        raise RuntimeError(f'{library}: missing central native-poll resolver')
    if re.search(r'callq?\s+(?:0x)?[0-9a-f]+\s+<(?:dlopen|dlsym)>', resolver):
        raise RuntimeError(f'{library}: native-poll resolver directly calls SDK-local loader wrappers')
    print(f'PASS {args.driver}: actual versioned ICD public runtime GOT/PLT bindings; resolver has no direct dlopen/dlsym calls')


if __name__ == '__main__':
    main()
