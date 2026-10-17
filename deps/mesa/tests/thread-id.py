#!/usr/bin/env python3
"""Build/run host or build a no-GPU PS5 probe of Mesa's actual vn_gettid body."""
import argparse
import ast
import configparser
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[3]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('target', choices=['host', 'ps5'])
a = parser.parse_args()
if os.environ.get('PS5WL_BUILD_ENV') != '1':
    parser.error('run through env/run')
source = ROOT/'build/mesa/sources/mesa-26.2.0-venus/src'
header = (source/'virtio/vulkan/vn_common.h').read_text()
match = re.search(r'static inline pid_t\s+vn_gettid\(void\)\s*\{.*?\n\}', header, re.S)
if not match:
    raise SystemExit('Cannot locate vn_gettid in the prepared Venus source; build Venus first')
out = ROOT/'build'/a.target/'venus-thread-id'
out.mkdir(parents=True, exist_ok=True)
(out/'venus-thread-id-function.h').write_text(match.group() + '\n')
flags = ['-O2', '-g', '-pthread', '-I'+str(out), '-I'+str(source)]
extra = []
if a.target == 'ps5':
    cross = configparser.ConfigParser()
    cross.read(ROOT/'build/ps5/cross.ini')
    cc = ast.literal_eval(cross['binaries']['c'])
    flags += ['-DPS5WL_PS5=1', '-I'+str(ROOT/'common/ps5log'),
              '-I'+str(ROOT/'compositor/src/backend/ps5')]
    flags += ast.literal_eval(cross['built-in options']['c_args'])
    extra = [str(ROOT/'common/ps5log/ps5log.c'),
             str(ROOT/'compositor/src/backend/ps5/power.c'), '-lSceSystemService']
    extra += ast.literal_eval(cross['built-in options']['c_link_args'])
    binary = out/'ps5wl-venus-thread-id.elf'
else:
    cc = 'clang'
    binary = out/'venus-thread-id'
subprocess.run([cc, *flags, str(ROOT/'deps/mesa/tests/thread-id.c'), *extra, '-o', str(binary)], check=True)
if a.target == 'host':
    subprocess.run([str(binary)], check=True, timeout=10)
else:
    print(f'Built {binary}; no Vulkan, Wayland, process creation or budget changes; logs9595')
