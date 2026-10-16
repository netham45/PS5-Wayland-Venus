#!/usr/bin/env python3
"""Validate the real capture socket, PNG framing, checksums and RGB pixels."""
import socket
import struct
import subprocess
import sys
import zlib

process = subprocess.Popen([sys.argv[1]], stdout=subprocess.PIPE)
try:
    assert process.stdout.readline() == b"READY\n"
    with socket.create_connection(("127.0.0.1", 9531), timeout=5) as client:
        parts = []
        while part := client.recv(65536):
            parts.append(part)
    png = b"".join(parts)
    assert png[:8] == b"\x89PNG\r\n\x1a\n"
    offset, compressed, kinds = 8, bytearray(), []
    while offset < len(png):
        size = struct.unpack_from(">I", png, offset)[0]
        kind = png[offset + 4:offset + 8]
        data = png[offset + 8:offset + 8 + size]
        crc = struct.unpack_from(">I", png, offset + 8 + size)[0]
        assert zlib.crc32(kind + data) == crc, kind
        kinds.append(kind)
        if kind == b"IHDR":
            assert struct.unpack(">IIBBBBB", data) == (17, 9, 8, 2, 0, 0, 0)
        elif kind == b"IDAT":
            compressed.extend(data)
        offset += 12 + size
    assert kinds[0] == b"IHDR" and kinds[-1] == b"IEND" and offset == len(png)
    decoder = zlib.decompressobj()
    pixels = decoder.decompress(compressed) + decoder.flush()
    assert decoder.eof and not decoder.unused_data
    expected = b"".join(b"\0" + bytes(v for x in range(17) for v in (x * 11, y * 23, x ^ y)) for y in range(9))
    assert pixels == expected, "RGB pixel order/filter/row data mismatch"
    assert process.wait(timeout=5) == 0
    print(f"PASS capture PNG: 17x9 RGB, {len(png)} bytes, all chunk CRCs, DEFLATE/Adler32 and 153 pixels")
finally:
    if process.poll() is None:
        process.kill()
        process.wait()
