#!/usr/bin/env python3
"""Convert a binary P6 PPM into an RGB PNG using only the standard library."""

import struct
import sys
import zlib


def read_ppm(path):
    with open(path, "rb") as handle:
        data = handle.read()
    if not data.startswith(b"P6"):
        raise SystemExit(f"{path}: not a binary P6 PPM")
    fields = []
    offset = 2
    while len(fields) < 3:
        while offset < len(data) and data[offset : offset + 1].isspace():
            offset += 1
        if data[offset : offset + 1] == b"#":
            while offset < len(data) and data[offset : offset + 1] != b"\n":
                offset += 1
            continue
        start = offset
        while offset < len(data) and not data[offset : offset + 1].isspace():
            offset += 1
        fields.append(int(data[start:offset]))
    offset += 1
    width, height, _maxval = fields
    return width, height, data[offset : offset + width * height * 3]


def chunk(tag, payload):
    body = tag + payload
    return struct.pack(">I", len(payload)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)


def write_png(path, width, height, rgb):
    stride = width * 3
    raw = bytearray()
    for row in range(height):
        raw.append(0)
        raw += rgb[row * stride : (row + 1) * stride]
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as handle:
        handle.write(png)


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: ppm2png.py <in.ppm> <out.png>")
    width, height, rgb = read_ppm(sys.argv[1])
    write_png(sys.argv[2], width, height, rgb)
    print(f"{sys.argv[2]}: {width}x{height}")


if __name__ == "__main__":
    main()
