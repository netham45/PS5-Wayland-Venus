#!/usr/bin/env python3
"""Render the upstream Vulkan cube through real wl_shm on headless ps5wl."""
import os
from pathlib import Path
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / 'build/host'
if os.environ.get('PS5WL_BUILD_ENV') != '1':
    raise SystemExit('Run env/run python3 clients/vkcube/smoke.py')
with tempfile.TemporaryDirectory(prefix='vkcube-', dir=BUILD) as runtime:
    env = dict(os.environ, XDG_RUNTIME_DIR=runtime, WAYLAND_DISPLAY='ps5wl-vkcube',
               WLR_RENDERER='pixman', LD_LIBRARY_PATH=str(BUILD / 'sysroot/lib'),
               PS5WL_VULKAN_LIBRARY=str(BUILD / 'sysroot/lib/libvulkan_lvp.so'),
               XKB_CONFIG_ROOT=str(BUILD / 'sysroot/share/xkeyboard-config-2'))
    with tempfile.TemporaryFile(mode='w+') as server_log:
        server = subprocess.Popen([str(BUILD / 'compositor/ps5wl'), '-b', 'headless',
                                   '-d', env['WAYLAND_DISPLAY']], env=env,
                                  stdout=server_log, stderr=server_log)
        try:
            deadline = time.monotonic() + 10
            while not (Path(runtime) / env['WAYLAND_DISPLAY']).exists():
                if server.poll() is not None or time.monotonic() >= deadline:
                    raise RuntimeError('Compositor failed to create socket')
                time.sleep(0.05)
            result = subprocess.run([str(BUILD / 'vkcube/ps5wl-vkcube'), '--wsi', 'wayland',
                                     '--c', '120'], env=env, timeout=90,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            print(result.stdout, end='')
            result.check_returncode()
            if 'vkcube presented frame=120' not in result.stdout:
                raise RuntimeError('Missing completed Vulkan presents')
        finally:
            server.terminate()
            try:
                status = server.wait(timeout=5)
            except subprocess.TimeoutExpired:
                server.kill()
                server.wait()
                raise
            server_log.seek(0)
            server_output = server_log.read()
            print(server_output, end='')
        if status:
            raise RuntimeError(f'Compositor exit status {status}')
        if 'Created shm allocator' not in server_output or 'count=120' not in server_output:
            raise RuntimeError('Missing compositor shm allocation or 120 surface commits')
print('Vulkan Wayland shm smoke passed: 120 presents')
