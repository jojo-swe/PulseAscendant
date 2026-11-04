"""Decode an 8-bit RGB PNG (filter type 0 scanlines) and print WIDTHxHEIGHT plus the first and last pixel."""

import struct
import sys
import zlib


def main(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        sys.exit("not a PNG signature")

    pos = 8
    width = height = None
    idat = b""
    while pos < len(data):
        (length,) = struct.unpack(">I", data[pos : pos + 4])
        kind = data[pos + 4 : pos + 8]
        body = data[pos + 8 : pos + 8 + length]
        (crc,) = struct.unpack(">I", data[pos + 8 + length : pos + 12 + length])
        if zlib.crc32(kind + body) != crc:
            sys.exit("bad CRC in " + kind.decode())
        if kind == b"IHDR":
            width, height, depth, colour, _, _, _ = struct.unpack(">IIBBBBB", body)
            if depth != 8 or colour != 2:
                sys.exit("expected 8-bit RGB")
        elif kind == b"IDAT":
            idat += body
        elif kind == b"IEND":
            break
        pos += 12 + length

    raw = zlib.decompress(idat)
    stride = 1 + width * 3
    if len(raw) != stride * height:
        sys.exit("unexpected decompressed size")
    for row in range(height):
        if raw[row * stride] != 0:
            sys.exit("unsupported filter type")

    first = raw[1:4]
    last = raw[stride * height - 3 :]
    print(f"{width}x{height} {first.hex()} {last.hex()}")


if __name__ == "__main__":
    main(sys.argv[1])
