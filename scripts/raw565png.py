#!/usr/bin/env python3
"""RGB565 小端裸帧 -> PNG (只用标准库)。 raw565png.py <in.raw> <宽> <高> <out.png>"""
import struct, sys, zlib
src, w, h, out = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4]
d = open(src, "rb").read()
rows = []
for y in range(h):
    px = struct.unpack_from("<%dH" % w, d, y * w * 2)
    row = bytearray(b"\0")
    for p in px:
        r, g, b = (p >> 11) & 31, (p >> 5) & 63, p & 31
        row += bytes(((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)))
    rows.append(bytes(row))
def chunk(t, b): return struct.pack(">I", len(b)) + t + b + struct.pack(">I", zlib.crc32(t + b) & 0xffffffff)
png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) \
      + chunk(b"IDAT", zlib.compress(b"".join(rows), 6)) + chunk(b"IEND", b"")
open(out, "wb").write(png)
