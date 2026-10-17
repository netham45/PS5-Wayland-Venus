#!/usr/bin/env python3
"""Require real Venus allocation OOM and recovery under PS5's 128-MiB quota."""
import os
from pathlib import Path
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[3]
if os.environ.get('PS5WL_BUILD_ENV') != '1':
    raise SystemExit('Run through env/run python3 deps/mesa/tests/vtest-quota.py')
logs = ROOT/'build/gpud/logs/quota'
logs.mkdir(parents=True, exist_ok=True)
client = logs/'vtest-quota'
subprocess.run(['clang', '-g', '-I'+str(ROOT/'build/mesa/sources/mesa-26.2.0-venus/include'),
                str(ROOT/'deps/mesa/tests/vtest-quota.c'), '-ldl', '-o', str(client)], check=True)
with tempfile.TemporaryDirectory(prefix='quota-', dir=ROOT/'build') as directory:
    vtest = Path(directory)/'vtest'
    env = dict(os.environ, VTEST_SOCKET_NAME=str(vtest), VN_DEBUG='vtest',
               PS5WL_NO_EXTERNAL_FD='true', MESA_LOG_LEVEL='debug',
               LD_LIBRARY_PATH=str(ROOT/'build/host/sysroot/lib'),
               PS5WL_VULKAN_LIBRARY=str(ROOT/'build/host/venus/lib/libvulkan_lvp.so'),
               ASAN_OPTIONS='detect_leaks=1:abort_on_error=1',
               UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
    with (logs/'server.log').open('w') as output:
        server = subprocess.Popen([str(ROOT/'build/gpud/host-sanitize/gpud-host'),
                                   str(vtest), str(Path(directory)/'render'), '--ps5-limits'],
                                  env=env, stdout=output, stderr=output)
        try:
            deadline = time.monotonic() + 20
            while not vtest.exists():
                if server.poll() is not None or time.monotonic() > deadline:
                    raise RuntimeError('GPU server not ready')
                time.sleep(.02)
            result = subprocess.run([str(client), str(ROOT/'build/host/venus/lib/libvulkan_virtio.so')],
                                    env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30)
            text = result.stdout.decode(errors='replace')
            (logs/'client.log').write_text(text)
            print(text, end='')
            if result.returncode or 'RESULT PASS' not in text:
                raise RuntimeError(f'quota client exit={result.returncode}; logs={logs}')
        finally:
            if server.poll() is None: server.terminate()
            try:
                server.wait(timeout=10)
            except subprocess.TimeoutExpired:
                server.kill(); server.wait(); raise
    if server.returncode:
        raise RuntimeError(f'GPU server exit={server.returncode}; logs={logs}')
    if 'Vulkan fd-export calls=0' not in (logs/'server.log').read_text():
        raise RuntimeError('Missing zero-export shutdown evidence')
    print(f'PASS: 80-MiB allocation, 80-MiB OOM, free, 80-MiB recovery; logs={logs}')
