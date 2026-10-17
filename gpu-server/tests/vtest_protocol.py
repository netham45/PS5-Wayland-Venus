"""Wire-level vtest regressions; no Vulkan driver calls are needed."""
import array
import os
import select
import socket
import struct


def run(path):
    def connect():
        s = socket.socket(socket.AF_UNIX)
        s.settimeout(3)
        s.connect(str(path))
        name = b'protocol-test\0'
        s.sendall(struct.pack('=II', len(name), 8) + name)
        return s

    def send(s, cmd, words=()):
        s.sendall(struct.pack('=II', len(words), cmd) + struct.pack('='+str(len(words))+'I', *words))

    def receive(s, cmd, count):
        data = bytearray()
        while len(data) < (count+2)*4:
            part = s.recv((count+2)*4-len(data))
            if not part:
                raise RuntimeError('unexpected vtest EOF')
            data.extend(part)
        values = struct.unpack('='+str(count+2)+'I', data)
        assert values[:2] == (count, cmd), values
        return values[2:]

    def waitfd(s, points, timeout=1000, any_=False):
        words = [int(any_), timeout]
        for ident, value in points:
            words.extend([ident, value, 0])
        send(s, 23, words)
        receive(s, 23, 0)
        data, ancillary, flags, _ = s.recvmsg(1, socket.CMSG_SPACE(4))
        assert data == b'\0' and not flags
        fds = array.array('i')
        assert len(ancillary) == 1 and ancillary[0][:2] == (socket.SOL_SOCKET, socket.SCM_RIGHTS)
        fds.frombytes(ancillary[0][2])
        assert len(fds) == 1
        return fds[0]

    # A partial header must not block command processing on another connection.
    with connect() as stalled, connect() as s:
        stalled.sendall(b'\x01')
        send(s, 10); receive(s, 10, 0)
        send(s, 11, [4]); assert receive(s, 11, 1) == (4,)
        send(s, 15, [1]); assert receive(s, 15, 2) == (1, 64)
        send(s, 19, [0,0]); first, = receive(s, 19, 1)
        send(s, 19, [0,0]); second, = receive(s, 19, 1)
        fd = waitfd(s, [(first, 3), (second, 5)], any_=True)
        try:
            assert not select.select([fd], [], [], 0)[0]
            send(s, 22, [second,5,0])
            assert select.select([fd], [], [], 3)[0]
            assert os.read(fd, 2) == b'\x01'
        finally:
            os.close(fd)
        fd = waitfd(s, [(first, 3)], timeout=1)
        try:
            assert select.select([fd], [], [], 3)[0]
            assert os.read(fd, 2) == b'\x01'
        finally:
            os.close(fd)
        send(s, 21, [second]); assert receive(s, 21, 2) == (5,0)
        send(s, 20, [first]); send(s, 20, [second])
        send(s, 0xffffffff)
        assert s.recv(1) == b''
    print('vtest-protocol: stalled-peer isolation, pipe ANY/timeout, sync read/write, malformed disconnect PASS')
