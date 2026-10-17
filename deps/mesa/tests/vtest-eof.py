#!/usr/bin/env python3
"""Require a real Venus client to terminate promptly after transport EOF."""
import os
from pathlib import Path
import resource
import signal
import socket
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]
if os.environ.get('PS5WL_BUILD_ENV') != '1':
    raise SystemExit('Run through env/run python3 deps/mesa/tests/vtest-eof.py')
resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


def receive(connection, size):
    chunks = bytearray()
    while len(chunks) < size:
        data = connection.recv(size - len(chunks))
        if not data:
            raise RuntimeError('Client disconnected before its initialization read')
        chunks.extend(data)
    return chunks


with tempfile.TemporaryDirectory(prefix='venus-eof-') as directory:
    path = str(Path(directory) / 'vtest')
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as listener:
        listener.bind(path)
        listener.listen(1)
        listener.settimeout(5)
        env = dict(os.environ, VTEST_SOCKET_NAME=path, VN_DEBUG='vtest',
                   MESA_LOG='file', MESA_LOG_LEVEL='debug',
                   PS5WL_VULKAN_LIBRARY=str(ROOT / 'build/host/venus/lib/libvulkan_virtio.so'))
        env.pop('MESA_LOG_FILE', None)
        env.pop('MESA_LOG_FILE_AUTO', None)
        executable = ROOT / 'build/host/vkcube-venus/ps5wl-vktest-venus'
        client = subprocess.Popen([str(executable)], env=env, stdout=subprocess.PIPE,
                                  stderr=subprocess.STDOUT, text=True)
        try:
            connection, _ = listener.accept()
            with connection:
                connection.settimeout(5)
                # CREATE_RENDERER is followed by a byte-counted process name.
                length, _ = struct.unpack('=II', receive(connection, 8))
                if not 0 < length <= 4096:
                    raise RuntimeError(f'Invalid client renderer-name length {length}')
                receive(connection, length)
                # Drain PING and its dummy BUSY_WAIT, so the peer is blocked in
                # vtest_read, not a write racing a reset/closed receive half.
                receive(connection, 24)
                connection.shutdown(socket.SHUT_WR)
                output, _ = client.communicate(timeout=5)
            print(output, end='')
            if client.returncode != -signal.SIGABRT:
                raise RuntimeError(f'Expected fatal transport EOF, got {client.returncode}')
            if 'lost connection to rendering server on 8 read 0 0' not in output:
                raise RuntimeError('Missing zero-byte read failure diagnostic')
            print('PASS: Venus terminates on socket EOF without spinning')
        finally:
            if client.poll() is None:
                client.kill()
                client.communicate()
