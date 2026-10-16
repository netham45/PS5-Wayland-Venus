#!/usr/bin/env python3
"""Generate the launcher's original 512x512 RGB PNG without external tools."""
import struct
import sys
import zlib
from pathlib import Path

size = 512
rows = bytearray()
for y in range(size):
    rows.append(0)
    for x in range(size):
        # Three joined, thick strokes form a W on a dark blue field.
        px, py = x / size, y / size
        strokes = ((0.17, 0.25, 0.32, 0.73), (0.32, 0.73, 0.5, 0.4),
                   (0.5, 0.4, 0.68, 0.73), (0.68, 0.73, 0.83, 0.25))
        ink = False
        for ax, ay, bx, by in strokes:
            dx, dy = bx-ax, by-ay
            t = max(0, min(1, ((px-ax)*dx+(py-ay)*dy)/(dx*dx+dy*dy)))
            if (px-ax-t*dx)**2+(py-ay-t*dy)**2 < 0.0011:
                ink = True
                break
        rows.extend((240, 192, 56) if ink else (20, 34, 56))

def chunk(kind, data):
    return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind+data))

png = b'\x89PNG\r\n\x1a\n'
png += chunk(b'IHDR', struct.pack('>IIBBBBB', size, size, 8, 2, 0, 0, 0))
png += chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b'')
Path(sys.argv[1]).write_bytes(png)
