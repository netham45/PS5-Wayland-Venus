#!/usr/bin/env python3
"""Real Venus client EOF with idle work versus an unsatisfied timeline wait."""
import argparse
import os
from pathlib import Path
import socket
import struct
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]


def until(predicate, processes, seconds, description):
    end = time.monotonic() + seconds
    while not predicate():
        if any(p.poll() is not None for p in processes) or time.monotonic() >= end:
            raise RuntimeError(description)
        time.sleep(.02)


def mappings(pid):
    return {line for line in (Path('/proc') / str(pid) / 'maps').read_text().splitlines()
            if 'vkr' in line or 'virgl' in line}


def refused(path):
    with socket.socket(socket.AF_UNIX) as peer:
        peer.settimeout(2)
        peer.connect(str(path))
        name = b'after-wedge\0'
        peer.sendall(struct.pack('=II', len(name), 8) + name)
        peer.sendall(struct.pack('=III', 1, 11, 4))
        data = b''
        while len(data) < 12:
            part = peer.recv(12 - len(data))
            assert part, 'wedged server disconnected before context initialization'
            data += part
        assert struct.unpack('=III', data) == (1, 11, 4)
        peer.sendall(struct.pack('=III', 1, 17, 4))
        assert peer.recv(1) == b'', 'wedged server accepted a new context'


def run(build, mode, logs):
    with tempfile.TemporaryDirectory(prefix='gpud-teardown-', dir=ROOT / 'build') as temp:
        runtime = Path(temp)
        env = dict(os.environ, VTEST_SOCKET_NAME=str(runtime / 'vtest'), VN_DEBUG='vtest',
                   LD_LIBRARY_PATH=str(ROOT / 'build/host/sysroot/lib'),
                   PS5WL_NO_EXTERNAL_FD='true',
                   ASAN_OPTIONS='abort_on_error=1:detect_leaks=1:fast_unwind_on_malloc=0',
                   UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
        server_log, client_log = logs / (mode + '-server.log'), logs / (mode + '-client.log')
        server_env = dict(env, PS5WL_VULKAN_LIBRARY=str(ROOT / 'build/host/venus/lib/libvulkan_lvp.so'))
        client_env = dict(env, PS5WL_VULKAN_LIBRARY=str(ROOT / 'build/host/venus/lib/libvulkan_virtio.so'))
        with server_log.open('w') as sl, client_log.open('w') as cl:
            server = subprocess.Popen([str(build / 'gpud-host'), str(runtime / 'vtest'), str(runtime / 'render')],
                                      env=server_env, stdout=sl, stderr=sl)
            client = None
            try:
                until(lambda: (runtime / 'vtest').exists(), [server], 10, 'server startup failed')
                client = subprocess.Popen([str(build / 'pending-client'), mode], env=client_env, stdout=cl, stderr=cl)
                marker = 'idle submission completed' if mode == 'idle' else 'pending submission waits'
                until(lambda: marker in client_log.read_text(), [server, client], 30, 'client did not submit real work')
                before = mappings(server.pid)
                assert before, 'no imported SHM mapping to protect'
                client.kill()
                client.wait()
                started = time.monotonic()
                if mode == 'pending':
                    until(lambda: 'gpud wedged:' in server_log.read_text(), [server], 4, 'pending-work teardown was not bounded')
                    after = mappings(server.pid)
                    assert before <= after, 'GPU-referenced SHM was unmapped before idle'
                    text = server_log.read_text()
                    assert 'teardown retained' in text and 'idle verified' not in text
                    refused(runtime / 'vtest')
                    expected = 1
                else:
                    until(lambda: 'context destroyed' in server_log.read_text(), [server], 5, 'idle context was not destroyed')
                    assert not mappings(server.pid), 'idle context retained SHM'
                    text = server_log.read_text()
                    assert 'DeviceWaitIdle result=0' in text and 'idle verified' in text
                    assert 'gpud wedged:' not in text
                    expected = 0
                elapsed = time.monotonic() - started
                server.terminate()
                assert server.wait(timeout=8) == expected, 'unexpected stop result'
                text = server_log.read_text()
                for bad in ['ERROR: AddressSanitizer', 'LeakSanitizer', 'runtime error:']:
                    assert bad not in text, bad
                print(f'{mode}: teardown={elapsed:.3f}s mappings-before={len(before)} '
                      f'preserved={mode == "pending"} exit={expected} PASS')
            finally:
                if client is not None and client.poll() is None:
                    client.kill()
                    client.wait()
                if server.poll() is None:
                    server.kill()
                    server.wait()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--pending-only', action='store_true')
    args = parser.parse_args()
    if os.environ.get('PS5WL_BUILD_ENV') != '1':
        parser.error('run inside env/run')
    build = ROOT / 'build/gpud' / ('host-sanitize' if args.sanitize else 'host')
    logs = ROOT / 'build/gpud/logs' / ('safe-teardown-sanitize' if args.sanitize else 'safe-teardown')
    logs.mkdir(parents=True, exist_ok=True)
    for mode in (['pending'] if args.pending_only else ['idle', 'pending']):
        run(build, mode, logs)


if __name__ == '__main__':
    main()
