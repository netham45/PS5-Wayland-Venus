#!/usr/bin/env python3
"""Present Venus vkcube over vtest to ps5wl, with host Vulkan fd extensions disabled."""
import argparse
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / 'build/gpu-server/host'


def stop(process):
    if process.poll() is None:
        os.killpg(process.pid, signal.SIGTERM)
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        os.killpg(process.pid, signal.SIGKILL)
        process.wait()


def socket_ready(path, process):
    deadline = time.monotonic() + 15
    while not path.exists():
        if process.poll() is not None or time.monotonic() > deadline:
            raise RuntimeError(f'{path}: server failed to create socket')
        time.sleep(0.05)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', action='store_true', help='expect upstream failure, not a passing smoke')
    args = parser.parse_args()
    if os.environ.get('PS5WL_BUILD_ENV') != '1':
        raise SystemExit('Run env/run python3 gpu-server/smoke.py')
    logs = ROOT / 'build/gpu-server/logs' / ('baseline' if args.baseline else 'host-pointer')
    logs.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='venus-', dir=ROOT / 'build') as temp:
        runtime = Path(temp)
        env = dict(os.environ, XDG_RUNTIME_DIR=temp, WAYLAND_DISPLAY='ps5wl-venus',
                   WLR_RENDERER='pixman', VN_DEBUG='vtest',
                   VTEST_SOCKET_NAME=str(runtime / 'vtest'),
                   XKB_CONFIG_ROOT=str(ROOT / 'build/host/sysroot/share/xkeyboard-config-2'),
                   LD_LIBRARY_PATH=os.pathsep.join([str(BUILD / 'sysroot/lib'),
                                                   str(ROOT / 'build/host/sysroot/lib')]),
                   PS5WL_NO_EXTERNAL_FD='true',
                   RENDER_SERVER_EXEC_PATH=str(BUILD / 'virgl/server/virgl_render_server'))
        with (logs / 'compositor.log').open('w') as compositor_log, (logs / 'vtest.log').open('w') as vtest_log:
            compositor = subprocess.Popen([str(ROOT / 'build/host/compositor/ps5wl'),
                '-b', 'headless', '-d', env['WAYLAND_DISPLAY']], env=env,
                stdout=compositor_log, stderr=compositor_log, start_new_session=True)
            server = None
            try:
                socket_ready(runtime / env['WAYLAND_DISPLAY'], compositor)
                host_env = dict(env, PS5WL_VULKAN_LIBRARY=str(BUILD / 'sysroot/lib/libvulkan_lvp.so'))
                server = subprocess.Popen([str(BUILD / 'virgl/vtest/virgl_test_server'),
                    '--venus', '--no-virgl', '--no-fork', '--socket-path', env['VTEST_SOCKET_NAME']],
                    env=host_env, stdout=vtest_log, stderr=vtest_log, start_new_session=True)
                socket_ready(Path(env['VTEST_SOCKET_NAME']), server)
                client_env = dict(env, PS5WL_VULKAN_LIBRARY=str(BUILD / 'sysroot/lib/libvulkan_virtio.so'))
                client = subprocess.run([str(ROOT / 'build/host/vkcube/ps5wl-vkcube'),
                    '--wsi', 'wayland', '--c', '120'], env=client_env, timeout=120,
                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
                (logs / 'client.log').write_text(client.stdout)
                print(client.stdout, end='')
            finally:
                if server:
                    stop(server)
                stop(compositor)
        server_text = (logs / 'vtest.log').read_text()
        compositor_text = (logs / 'compositor.log').read_text()
        print(server_text, end='')
        print(compositor_text, end='')
        if args.baseline:
            if client.returncode == 0:
                raise RuntimeError('Upstream unexpectedly passed; baseline is not reproduced')
            print(f'EXPECTED BASELINE FAILURE client={client.returncode}; logs={logs}')
            return
        client.check_returncode()
        for token in ['vkcube presented frame=120', 'vkcube exit frames=120 validation_error=0']:
            if token not in client.stdout:
                raise RuntimeError(f'Missing client evidence: {token}')
        for token in ['extensions disabled', 'host-pointer blob:']:
            if token not in server_text:
                raise RuntimeError(f'Missing no-fd memory evidence: {token}')
        if 'count=120' not in compositor_text or 'Created shm allocator' not in compositor_text:
            raise RuntimeError('Missing 120 Wayland surface commits via wl_shm')
        if compositor.returncode != 0:
            raise RuntimeError(f'Compositor exit={compositor.returncode}')
        print(f'PASS: Venus vtest / host-pointer SHM / no Vulkan external fd extensions / 120 Wayland presents; logs={logs}')


if __name__ == '__main__':
    main()
