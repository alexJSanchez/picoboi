#!/usr/bin/env python3
"""Convert the 24-bit BMP screenshots written by tests/play_rom.c into PNG files (standard library
only). Scaling: an integer (nearest neighbour, crisp) or "fit" for the exact 1.5x picture the
PicoSystem shows (240x216).

    frames_to_png.py shot.bmp shot.png --scale 3
    frames_to_png.py shot.bmp shot.png --scale fit
"""
import argparse
import struct
import sys
import zlib


def read_bmp(path):
    d = open(path, "rb").read()
    w, h = struct.unpack("<ii", d[18:26])
    off = struct.unpack("<I", d[10:14])[0]
    row = (w * 3 + 3) & ~3
    rows = []
    for y in range(h - 1, -1, -1):  # BMP rows are bottom-up
        r = d[off + y * row: off + y * row + w * 3]
        rows.append([(r[i + 2], r[i + 1], r[i]) for i in range(0, w * 3, 3)])
    return w, h, rows


def scale_fit(w, h, rows):
    """1.5x nearest neighbour: every 2 source pixels become 3 (s0, s0, s1), same for rows."""
    out_w, out_h = w * 3 // 2, h * 3 // 2
    return out_w, out_h, [[rows[y * 2 // 3][x * 2 // 3] for x in range(out_w)] for y in range(out_h)]


def scale_int(w, h, rows, k):
    return w * k, h * k, [[px for px in row for _ in range(k)] for row in rows for _ in range(k)]


def write_png(path, w, h, rows):
    raw = b"".join(b"\x00" + bytes(c for px in row for c in px) for row in rows)

    def chunk(t, c):
        return struct.pack(">I", len(c)) + t + c + struct.pack(">I", zlib.crc32(t + c) & 0xFFFFFFFF)

    png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) +
           chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))
    open(path, "wb").write(png)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("bmp")
    ap.add_argument("png")
    ap.add_argument("--scale", default="3", help='integer factor or "fit" (1.5x)')
    a = ap.parse_args()
    w, h, rows = read_bmp(a.bmp)
    if a.scale == "fit":
        w, h, rows = scale_fit(w, h, rows)
    else:
        w, h, rows = scale_int(w, h, rows, int(a.scale))
    write_png(a.png, w, h, rows)
    print(f"{a.png}: {w}x{h}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
