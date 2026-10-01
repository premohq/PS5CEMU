#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Draw PS5Cemu's icon, with nothing but the standard library.

    render-icons.py OUTPUT_DIR

writes sce_sys/icon0.png (512x512, opaque: the console's tile) and the launcher's
ui/icons/ps5cemu.tga (336x336) and ui/icons/ps5cemu-72.tga, in ProsperoEden's
palette: a Wii U GamePad on a dark green tile. Shapes are signed distance
functions, so every size is drawn sharp, and edges are anti-aliased over a pixel.
"""

import math
import os
import struct
import sys
import zlib

BACKGROUND_TOP = (0x17, 0x3a, 0x31)
BACKGROUND_BOTTOM = (0x05, 0x0d, 0x0b)
BODY = (0xf0, 0xf5, 0xf2)
SCREEN_TOP = (0x1d, 0x4a, 0x3c)
SCREEN_BOTTOM = (0x0b, 0x1c, 0x17)
ACCENT = (0xa9, 0xdb, 0x63)
DETAIL = (0x5f, 0x6e, 0x66)


def rounded_box(x, y, cx, cy, half_w, half_h, radius):
    """Signed distance to a rounded rectangle (negative inside)."""
    qx = abs(x - cx) - half_w + radius
    qy = abs(y - cy) - half_h + radius
    outside = math.hypot(max(qx, 0.0), max(qy, 0.0))
    return outside + min(max(qx, qy), 0.0) - radius


def circle(x, y, cx, cy, r):
    return math.hypot(x - cx, y - cy) - r


def coverage(distance, pixel):
    """Area of the pixel inside a shape at that signed distance (in pixels of the unit square)."""
    return min(max(0.5 - distance / pixel, 0.0), 1.0)


def mix(a, b, t):
    return tuple(a[i] + (b[i] - a[i]) * t for i in range(3))


def shade(u, v, pixel, rounded):
    """Colour and alpha at (u, v) in the unit square."""
    # the tile: a vertical gradient with a faint accent glow behind the GamePad, its corners
    # rounded in the launcher (the console rounds icon0.png's itself)
    alpha = coverage(rounded_box(u, v, 0.5, 0.5, 0.5, 0.5, 0.11), pixel) if rounded else 1.0
    if alpha <= 0.0:
        return (0, 0, 0), 0.0
    colour = mix(BACKGROUND_TOP, BACKGROUND_BOTTOM, v)
    glow = max(0.0, 1.0 - math.hypot(u - 0.5, (v - 0.52) * 1.6) / 0.48)
    colour = mix(colour, ACCENT, 0.16 * glow * glow)

    # the GamePad's body
    body = rounded_box(u, v, 0.5, 0.53, 0.40, 0.205, 0.085)
    colour = mix(colour, (0, 0, 0), 0.35 * coverage(body - 0.02, pixel * 6) * (1 - coverage(body, pixel)))  # shadow
    colour = mix(colour, BODY, coverage(body, pixel))
    # its screen, with an accent edge
    screen_edge = rounded_box(u, v, 0.5, 0.52, 0.205, 0.145, 0.02)
    colour = mix(colour, ACCENT, coverage(screen_edge, pixel))
    screen = rounded_box(u, v, 0.5, 0.52, 0.19, 0.13, 0.014)
    colour = mix(colour, mix(SCREEN_TOP, SCREEN_BOTTOM, (v - 0.39) / 0.26), coverage(screen, pixel))
    # a play triangle on the screen
    tri = max(0.47 - u, abs(v - 0.52) * 1.15 - (0.56 - u) * 0.62)
    colour = mix(colour, ACCENT, coverage(tri * 1.0, pixel))
    # sticks
    for cx in (0.175, 0.825):
        ring = circle(u, v, cx, 0.43, 0.04)
        colour = mix(colour, DETAIL, coverage(ring, pixel))
        colour = mix(colour, BODY, coverage(circle(u, v, cx, 0.43, 0.024), pixel))
    # d-pad
    dpad = min(rounded_box(u, v, 0.175, 0.585, 0.045, 0.014, 0.005), rounded_box(u, v, 0.175, 0.585, 0.014, 0.045, 0.005))
    colour = mix(colour, DETAIL, coverage(dpad, pixel))
    # face buttons, the top one in the accent colour
    for (cx, cy, c) in ((0.825, 0.545, ACCENT), (0.86, 0.585, DETAIL), (0.79, 0.585, DETAIL), (0.825, 0.625, DETAIL)):
        colour = mix(colour, c, coverage(circle(u, v, cx, cy, 0.017), pixel))
    return colour, alpha


def render(size, rounded=True):
    pixel = 1.0 / size
    rows = []
    for y in range(size):
        row = []
        v = (y + 0.5) / size
        for x in range(size):
            colour, alpha = shade((x + 0.5) / size, v, pixel, rounded)
            row.append(tuple(int(round(max(0, min(255, c)))) for c in colour) + (int(round(alpha * 255)),))
        rows.append(row)
    return rows


def write_png(path, rows):
    size = len(rows)
    raw = b"".join(b"\x00" + bytes(channel for p in row for channel in p) for row in rows)

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    with open(path, "wb") as out:
        out.write(png)


def write_tga(path, rows):
    size = len(rows)
    # uncompressed true colour, 32 bits, 8 of alpha, top-down: what RmlUi's Vulkan renderer reads
    header = struct.pack("<BBBHHBHHHHBB", 0, 0, 2, 0, 0, 0, 0, 0, size, size, 32, 0x28)
    with open(path, "wb") as out:
        out.write(header + b"".join(bytes((p[2], p[1], p[0], p[3])) for row in rows for p in row))


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    out = sys.argv[1]
    os.makedirs(os.path.join(out, "sce_sys"), exist_ok=True)
    os.makedirs(os.path.join(out, "ui", "icons"), exist_ok=True)
    write_png(os.path.join(out, "sce_sys", "icon0.png"), render(512, rounded=False))
    write_tga(os.path.join(out, "ui", "icons", "ps5cemu.tga"), render(336))
    write_tga(os.path.join(out, "ui", "icons", "ps5cemu-72.tga"), render(72))


if __name__ == "__main__":
    main()
