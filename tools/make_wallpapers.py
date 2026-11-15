#!/usr/bin/env python3
"""Generate the avocadOS wallpapers as plain 24-bit BMP files (standard library only)."""
import math
import os
import struct
import sys

W, H = 1024, 768

# name: (dark corner colour, light corner colour, glow centre x, glow centre y)
THEMES = {
    "guacamole": ((18, 44, 14), (126, 176, 44), 0.78, 0.30),
    "pit":       ((38, 22, 12), (156, 104, 52), 0.25, 0.70),
    "dusk":      ((12, 18, 32), (72, 124, 92), 0.70, 0.25),
    "lime":      ((28, 66, 18), (206, 230, 114), 0.30, 0.35),
}


def pixel(theme, x, y):
    dark, light, gx, gy = theme
    t = (x / W * 0.45) + (y / H * 0.55)
    d = math.hypot(x / W - gx, (y / H - gy) * 0.75)
    glow = max(0.0, 1.0 - d * 1.7) ** 2 * 0.55
    k = min(1.0, t * 0.8 + glow)
    return tuple(int(dark[i] + (light[i] - dark[i]) * k) for i in range(3))


def write_bmp(path, theme):
    rows = []
    for y in range(H - 1, -1, -1):  # BMP rows are stored bottom-up
        row = bytearray()
        for x in range(W):
            r, g, b = pixel(theme, x, y)
            row += bytes((b, g, r))
        rows.append(bytes(row))
    data = b"".join(rows)  # width*3 = 3072, already a multiple of 4
    header = b"BM" + struct.pack("<IHHI", 54 + len(data), 0, 0, 54)
    header += struct.pack("<IiiHHIIiiII", 40, W, H, 1, 24, 0, len(data), 2835, 2835, 0, 0)
    with open(path, "wb") as f:
        f.write(header + data)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "build/wallpapers"
    os.makedirs(out, exist_ok=True)
    for name, theme in THEMES.items():
        write_bmp(os.path.join(out, name + ".bmp"), theme)
        print("wrote", os.path.join(out, name + ".bmp"))


if __name__ == "__main__":
    main()
