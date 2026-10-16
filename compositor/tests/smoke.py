#!/usr/bin/env python3
"""Run a headless compositor and validate globals using our pinned-libwayland client."""
import os
from pathlib import Path
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / 'build/host'
if os.environ.get('PS5WL_BUILD_ENV') != '1':
    raise SystemExit('Run compositor/tests/smoke.sh through env/run')
with tempfile.TemporaryDirectory(prefix='smoke-', dir=BUILD) as runtime:
    env = dict(os.environ, XDG_RUNTIME_DIR=runtime, WAYLAND_DISPLAY='ps5wl-smoke',
               WLR_RENDERER='pixman', LD_LIBRARY_PATH=str(BUILD / 'sysroot/lib'),
               XKB_CONFIG_ROOT=str(BUILD / 'sysroot/share/xkeyboard-config-2'))
    with tempfile.TemporaryFile(mode='w+') as log:
        server = subprocess.Popen([str(BUILD / 'compositor/ps5wl'), '-b', 'headless',
                                   '-d', env['WAYLAND_DISPLAY']], env=env,
                                  stdout=log, stderr=log)
        try:
            deadline = time.monotonic() + 10
            while not (Path(runtime) / env['WAYLAND_DISPLAY']).exists():
                if server.poll() is not None or time.monotonic() >= deadline:
                    raise RuntimeError('Compositor exited or failed to create socket')
                time.sleep(0.05)
            time.sleep(3)
            subprocess.run([str(BUILD / 'compositor/registry-dump')], env=env, check=True)
        finally:
            if server.poll() is None:
                server.terminate()
            try:
                status = server.wait(timeout=5)
            except subprocess.TimeoutExpired:
                server.kill()
                server.wait()
                raise
            log.seek(0)
            print(log.read(), end='')
        if status != 0:
            raise SystemExit(f'Compositor exited with status {status}')
print('Headless registry smoke passed')
