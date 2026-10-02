#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Draw PS5Cemu's home-screen background, sce_sys/pic0.dds.

    render-presentation.py OUTPUT_DDS [PREVIEW_PNG]

The PS5 shows pic0.dds behind the app while it is selected on the home screen, and pic1.dds while
it starts; tools/package.sh installs this one image as both. It is the Wii U Homebrew Launcher's
background (tools/render-background.py, under the README banner's lighter overlay) with the app's
logo on the right: its GamePad (tools/render-icons.py's shapes, in the banner's palette) over its
name. The shell draws the app's name, a Play button and its own shading on the left, and a homebrew
title cannot give it a logo of its own there (that is the store's catalog data), so the logo is part
of the picture, where the shell leaves room for one.

The console takes a single 3840x2160 BC7_UNORM DX10 DDS without mipmaps
(ps5-native-app-boilerplate's tools/validate-assets.sh), which this encodes itself in BC7's mode 6:
per 4x4 block, a line between two colours, along the block's principal axis, and a 4-bit position
on it for each pixel. The background's colours lie close to such lines (white over blue).

This needs Pillow and numpy, and a bold sans-serif TrueType font (Segoe UI or DejaVu Sans); its
output is committed (sce_sys/pic0.dds), so the build needs none of them.
"""

import importlib.util
import os
import struct
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

W, H = 3840, 2160
OVERLAY = 0.35
BODY, SCREEN_TOP, SCREEN_BOTTOM = (0xf4, 0xf8, 0xfc), (0x1e, 0x4f, 0x7a), (0x0b, 0x1e, 0x33)
ACCENT, DETAIL = (0x9f, 0xd6, 0xff), (0x6a, 0x7e, 0x93)
# the logo: the icon's unit square, T pixels wide, with the GamePad's centre (0.5, 0.53) at CENTRE
T = 1500
CENTRE = (int(W * 0.70), int(H * 0.40))
FONTS = ["C:/Windows/Fonts/segoeuib.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"]
SUBTITLE_FONTS = ["C:/Windows/Fonts/seguisb.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"]


def load_background():
    spec = importlib.util.spec_from_file_location(
        "render_background", os.path.join(os.path.dirname(os.path.abspath(__file__)), "render-background.py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def background():
    hbl = load_background()
    t = (np.arange(H, dtype=np.float64)[:, None] + 0.5) / H
    image = np.empty((H, W, 3))
    for c in range(3):
        image[:, :, c] = hbl.TOP[c] + (hbl.BOTTOM[c] - hbl.TOP[c]) * t
    for x, y, radius, alpha in hbl.particles():
        cx, cy, r = x * W, y * H, radius * W
        if r < 0.5:
            continue
        x0, x1 = max(0, int(cx - r - 1)), min(W, int(cx + r + 2))
        y0, y1 = max(0, int(cy - r - 1)), min(H, int(cy + r + 2))
        px = np.arange(x0, x1) + 0.5 - cx
        py = np.arange(y0, y1)[:, None] + 0.5 - cy
        a = alpha * np.clip(r - np.hypot(px, py) + 0.5, 0.0, 1.0)
        region = image[y0:y1, x0:x1]
        region += (255.0 - region) * a[:, :, None]
    image *= 1.0 - OVERLAY
    return Image.fromarray(np.clip(np.rint(image), 0, 255).astype(np.uint8), "RGB").convert("RGBA")


def font(candidates, size):
    for path in candidates:
        if os.path.isfile(path):
            return ImageFont.truetype(path, size)
    sys.exit("no font found: " + ", ".join(candidates))


def gamepad():
    """The GamePad, drawn twice the size and scaled down: an RGBA layer over the whole picture."""
    s = 2
    layer = Image.new("RGBA", (W * s, H * s), (0, 0, 0, 0))
    ox, oy = CENTRE[0] - 0.5 * T, CENTRE[1] - 0.53 * T

    def xy(u, v):
        return ((ox + u * T) * s, (oy + v * T) * s)

    def box(draw, cx, cy, hw, hh, r, fill):
        draw.rounded_rectangle([xy(cx - hw, cy - hh), xy(cx + hw, cy + hh)], radius=r * T * s, fill=fill)

    def dot(draw, cx, cy, r, fill):
        draw.ellipse([xy(cx - r, cy - r), xy(cx + r, cy + r)], fill=fill)

    # its shadow, then the body
    shadow = Image.new("RGBA", layer.size, (0, 0, 0, 0))
    box(ImageDraw.Draw(shadow), 0.5, 0.55, 0.40, 0.205, 0.085, (0, 0, 0, 115))
    layer = Image.alpha_composite(layer, shadow.filter(ImageFilter.GaussianBlur(0.02 * T * s)))
    draw = ImageDraw.Draw(layer)
    box(draw, 0.5, 0.53, 0.40, 0.205, 0.085, BODY)
    box(draw, 0.5, 0.52, 0.205, 0.145, 0.02, ACCENT)
    # the screen, a vertical gradient
    (sx0, sy0), (sx1, sy1) = xy(0.5 - 0.19, 0.52 - 0.13), xy(0.5 + 0.19, 0.52 + 0.13)
    size = (int(sx1 - sx0), int(sy1 - sy0))
    t = np.linspace(0.0, 1.0, size[1])[:, None, None]
    gradient = np.array(SCREEN_TOP) + (np.array(SCREEN_BOTTOM) - np.array(SCREEN_TOP)) * t
    screen = Image.fromarray(np.repeat(gradient, size[0], axis=1).astype(np.uint8), "RGB")
    mask = Image.new("L", size, 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, size[0] - 1, size[1] - 1], radius=0.014 * T * s, fill=255)
    layer.paste(screen, (int(sx0), int(sy0)), mask)
    draw = ImageDraw.Draw(layer)
    # the play triangle on the screen (render-icons.py: u >= 0.47, |v - 0.52| * 1.15 <= (0.56 - u) * 0.62)
    half = 0.09 * 0.62 / 1.15
    draw.polygon([xy(0.47, 0.52 - half), xy(0.47, 0.52 + half), xy(0.56, 0.52)], fill=ACCENT)
    for cx in (0.175, 0.825):  # sticks
        dot(draw, cx, 0.43, 0.04, DETAIL)
        dot(draw, cx, 0.43, 0.024, BODY)
    box(draw, 0.175, 0.585, 0.045, 0.014, 0.005, DETAIL)  # d-pad
    box(draw, 0.175, 0.585, 0.014, 0.045, 0.005, DETAIL)
    for cx, cy, c in ((0.825, 0.545, ACCENT), (0.86, 0.585, DETAIL), (0.79, 0.585, DETAIL), (0.825, 0.625, DETAIL)):
        dot(draw, cx, cy, 0.017, c)
    return layer.resize((W, H), Image.LANCZOS)


def name():
    """The app's name and what it is, centred under the GamePad, with a soft shadow."""
    layer = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    draw = ImageDraw.Draw(layer)
    top = CENTRE[1] + int((0.735 - 0.53) * T) + 110
    title, subtitle = font(FONTS, 250), font(SUBTITLE_FONTS, 88)
    draw.text((CENTRE[0], top), "PS5Cemu", font=title, fill=BODY + (255,), anchor="mt")
    draw.text((CENTRE[0], top + 300), "Cemu, the Wii U emulator", font=subtitle, fill=ACCENT + (255,), anchor="mt")
    shadow = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    shadow.putalpha(layer.getchannel("A").point(lambda a: a * 0.45))
    return Image.alpha_composite(shadow.filter(ImageFilter.GaussianBlur(10)), layer)


WEIGHTS = np.array([0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64])


def encode_bc7(rgb):
    """BC7 mode 6 blocks, row by row, for an opaque H x W x 3 image (W and H multiples of 4)."""
    h, w = rgb.shape[:2]
    out = []
    for y in range(0, h, 64):  # 16 rows of blocks at a time
        strip = rgb[y:y + 64].astype(np.float64)
        rows = strip.shape[0] // 4
        px = strip.reshape(rows, 4, w // 4, 4, 3).transpose(0, 2, 1, 3, 4).reshape(-1, 16, 3)
        mean = px.mean(axis=1)
        d = px - mean[:, None, :]
        cov = np.einsum("npi,npj->nij", d, d)
        axis = np.ones((len(px), 3)) / np.sqrt(3.0)
        for _ in range(8):  # the principal axis, by power iteration
            v = np.einsum("nij,nj->ni", cov, axis)
            norm = np.linalg.norm(v, axis=1, keepdims=True)
            axis = np.where(norm > 1e-9, v / np.maximum(norm, 1e-12), axis)
        t = np.einsum("npi,ni->np", d, axis)
        ends = [np.clip(mean + t.min(axis=1)[:, None] * axis, 0, 255), np.clip(mean + t.max(axis=1)[:, None] * axis, 0, 255)]
        # 7 bits per channel and a p-bit of 1 for both ends: odd values, and alpha 255
        q = [np.clip(np.rint((e - 1.0) / 2.0), 0, 127).astype(np.int64) for e in ends]
        r0, r1 = q[0] * 2 + 1, q[1] * 2 + 1
        line = (r1 - r0).astype(np.float64)
        length = np.einsum("ni,ni->n", line, line)
        pos = np.einsum("npi,ni->np", px - r0[:, None, :], line) / np.maximum(length, 1e-9)[:, None]
        index = np.abs(pos[:, :, None] * 64.0 - WEIGHTS[None, None, :]).argmin(axis=2)
        index[length == 0] = 0
        # the first pixel's index must have its top bit clear: else swap the ends
        swap = index[:, 0] >= 8
        q[0][swap], q[1][swap] = q[1][swap].copy(), q[0][swap].copy()
        index[swap] = 15 - index[swap]
        q0, q1 = q[0].astype(np.uint64), q[1].astype(np.uint64)
        lo = np.full(len(px), 1 << 6, dtype=np.uint64)  # mode 6
        for c in range(3):  # R0 R1 G0 G1 B0 B1, then A0 A1 (127), from bit 7
            lo |= q0[:, c] << np.uint64(7 + 14 * c)
            lo |= q1[:, c] << np.uint64(14 + 14 * c)
        lo |= np.uint64(127) << np.uint64(49)
        lo |= np.uint64(127) << np.uint64(56)
        lo |= np.uint64(1) << np.uint64(63)  # P0
        hi = np.ones(len(px), dtype=np.uint64)  # P1
        idx = index.astype(np.uint64)
        hi |= idx[:, 0] << np.uint64(1)  # three bits for the first
        for i in range(1, 16):
            hi |= idx[:, i] << np.uint64(4 * i)
        out.append(np.stack([lo, hi], axis=1).astype("<u8").tobytes())
    return b"".join(out)


def decode_bc7(data, w, h):
    """The image mode 6 blocks hold (encode_bc7's check, and the preview)."""
    words = np.frombuffer(data, dtype="<u8").reshape(-1, 2)
    lo, hi = words[:, 0], words[:, 1]
    ends = []
    for e in range(2):
        channel = [((lo >> np.uint64(7 + 7 * (2 * c + e))) & np.uint64(127)).astype(np.int64) for c in range(3)]
        p = ((lo >> np.uint64(63)) & np.uint64(1)) if e == 0 else (hi & np.uint64(1))
        ends.append(np.stack(channel, axis=1) * 2 + p.astype(np.int64)[:, None])
    index = np.stack([((hi >> np.uint64(1)) & np.uint64(7))] + [((hi >> np.uint64(4 * i)) & np.uint64(15)) for i in range(1, 16)], axis=1)
    weight = WEIGHTS[index.astype(np.int64)][:, :, None]
    px = ((64 - weight) * ends[0][:, None, :] + weight * ends[1][:, None, :] + 32) >> 6
    return px.reshape(h // 4, w // 4, 4, 4, 3).transpose(0, 2, 1, 3, 4).reshape(h, w, 3).astype(np.uint8)


def write_dds(path, blocks):
    linear_size = len(blocks)
    header = struct.pack("<4sIIIIIII44x", b"DDS ", 124, 0xA1007, H, W, linear_size, 0, 1)
    # the pixel format names a DX10 header; caps: a texture
    header += struct.pack("<II4s5I", 32, 0x4, b"DX10", 0, 0, 0, 0, 0)
    header += struct.pack("<5I", 0x1000, 0, 0, 0, 0)
    header += struct.pack("<5I", 98, 3, 0, 1, 0)  # BC7_UNORM, a 2D texture, one of it
    assert len(header) == 148
    with open(path, "wb") as out:
        out.write(header + blocks)


def main():
    if len(sys.argv) not in (2, 3):
        sys.exit(__doc__)
    picture = Image.alpha_composite(Image.alpha_composite(background(), gamepad()), name()).convert("RGB")
    rgb = np.asarray(picture)
    blocks = encode_bc7(rgb)
    decoded = decode_bc7(blocks, W, H)
    error = np.sqrt(np.mean((decoded.astype(np.float64) - rgb) ** 2))
    print(f"BC7: {len(blocks)} bytes, RMS error {error:.2f}")
    write_dds(sys.argv[1], blocks)
    if len(sys.argv) == 3:
        Image.fromarray(decoded, "RGB").save(sys.argv[2])


if __name__ == "__main__":
    main()
