#!/usr/bin/env python3
"""Real xdg-shell clients, composited screenshots, and virtual-seat input."""
import os
from pathlib import Path
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / 'build/host'
if os.environ.get('PS5WL_BUILD_ENV') != '1':
    raise SystemExit('Run env/run python3 clients/tests/smoke.py')


def stop(process):
    if process.poll() is None:
        process.terminate()
    try:
        return process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()
        raise


artifacts = BUILD / 'clients/evidence'
artifacts.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='client-smoke-', dir=BUILD) as runtime:
    env = dict(os.environ, XDG_RUNTIME_DIR=runtime, WAYLAND_DISPLAY='ps5wl-client-test',
               WLR_RENDERER='pixman', LD_LIBRARY_PATH=str(BUILD / 'sysroot/lib'),
               XKB_CONFIG_ROOT=str(BUILD / 'sysroot/share/xkeyboard-config-2'))
    with (artifacts / 'compositor.log').open('w+') as server_log:
        server = subprocess.Popen([str(BUILD / 'compositor/ps5wl'), '-b', 'headless', '-d',
                                   env['WAYLAND_DISPLAY']], env=env, stdout=server_log, stderr=server_log)
        try:
            deadline = time.monotonic() + 15
            while not (Path(runtime) / env['WAYLAND_DISPLAY']).exists():
                if server.poll() is not None or time.monotonic() > deadline:
                    raise RuntimeError('Compositor failed to open socket')
                time.sleep(0.05)
            time.sleep(1)
            for name, mode in [('simple-shm', 'input'), ('demo', 'demo')]:
                log_path = artifacts / f'{name}.log'
                with log_path.open('w+') as log:
                    client = subprocess.Popen([str(BUILD / f'clients/ps5wl-{name}')], env=env,
                                              stdout=log, stderr=log)
                    try:
                        deadline = time.monotonic() + 10
                        while 'frame-done committed=1' not in log_path.read_text():
                            if client.poll() is not None or time.monotonic() > deadline:
                                raise RuntimeError(f'{name}: no completed frame: {log_path.read_text()}')
                            time.sleep(0.05)
                        subprocess.run([str(BUILD / 'clients/tests/client-exercise'), mode,
                                        str(artifacts / name)], env=env, check=True, timeout=20)
                        time.sleep(0.2)
                    finally:
                        status = stop(client)
                text = log_path.read_text()
                print(text, end='')
                assert status == 0, (name, status)
                assert 'buffer-commit frame=1' in text
                assert 'frame-done committed=1' in text
                if name == 'demo':
                    for expected in ['keyboard-keymap bytes=', 'pointer-enter',
                                     'pointer-button button=272 state=1', 'demo-drag',
                                     'keyboard-key key=30 state=1', 'demo-text bytes=1 text=a',
                                     'fullscreen=1', 'fullscreen=0']:
                        assert expected in text, (expected, text)
                for expected in ['keyboard-keymap bytes=', 'pointer-enter',
                                 'pointer-button button=272 state=1',
                                 'keyboard-key key=30 state=1']:
                    assert expected in text, (expected, text)
        finally:
            status = stop(server)
            server_log.seek(0)
            print(server_log.read(), end='')
        assert status == 0, status
print(f'Client headless frames + keyboard/mouse/fullscreen passed; evidence: {artifacts}')
