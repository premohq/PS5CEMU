#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Draw PS5CEMU-HAR's home-screen background, sce_sys/pic0.dds.

    render-presentation.py OUTPUT_DDS [PREVIEW_PNG]

The PS5 shows pic0.dds behind the app while it is selected on the home screen, and pic1.dds while
it starts; tools/package.sh installs this one image as both. It is the app's two sides, split down
the middle as its start screen is: on the left Cemu's, the Wii U GamePad on the Wii U Homebrew
Launcher's blue with its bubbles (tools/render-background.py); on the right Azahar's, the 3DS on the
3DS Homebrew Launcher's waves made yellow (as port/frontend/wave.cpp draws them). The devices are
tools/render-icons.py's, drawn at this size, each with its emulator's name under it, and the app's
name runs across the two halves below them. The shell draws its own title, a Play button and its
shading over the lower left.

The console takes a single 3840x2160 BC7_UNORM DX10 DDS without mipmaps
(ps5-native-app-boilerplate's tools/validate-assets.sh), which this encodes itself in BC7's mode 6:
per 4x4 block, a line between two colours, along the block's principal axis, and a 4-bit position
on it for each pixel.

This needs Pillow and numpy, and a bold sans-serif TrueType font (Segoe UI or DejaVu Sans); its
output is committed (sce_sys/pic0.dds), so the build needs none of them. The devices take a minute
or so: render-icons.py draws them a pixel at a time.
"""

import importlib.util
import os
import struct
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

W, H = 3840, 2160
HALF = W // 2
# each device: its unit square's width in pixels, and where its centre goes
GAMEPAD = (1150, (HALF // 2, int(H * 0.33)))
N3DS = (1000, (HALF + HALF // 2, int(H * 0.34)))
FONTS = ["C:/Windows/Fonts/segoeuib.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"]
SUBTITLE_FONTS = ["C:/Windows/Fonts/seguisb.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"]


def load(name, file):
    spec = importlib.util.spec_from_file_location(name, os.path.join(os.path.dirname(os.path.abspath(__file__)), file))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


ICONS = load("render_icons", "render-icons.py")
HBL = load("render_background", "render-background.py")


def bubbles(width, height, region):
    """The Homebrew Launcher's background: region (x, y, w, h) of its 1280x720 screen at this size,
    under the banner's overlay (render-icons.py's)."""
    rx, ry, rw, rh = region
    sx, sy = width / rw, height / rh
    t = (ry + (np.arange(height, dtype=np.float64)[:, None] + 0.5) / sy) / 720.0
    image = np.empty((height, width, 3))
    for c in range(3):
        image[:, :, c] = HBL.TOP[c] + (HBL.BOTTOM[c] - HBL.TOP[c]) * t
    for x, y, radius, alpha in HBL.particles():
        cx, cy, r = (x * 1280 - rx) * sx, (y * 720 - ry) * sy, radius * 1280 * sx
        if r < 0.5 or cx + r < 0 or cx - r > width or cy + r < 0 or cy - r > height:
            continue
        x0, x1 = max(0, int(cx - r - 1)), min(width, int(cx + r + 2))
        y0, y1 = max(0, int(cy - r - 1)), min(height, int(cy + r + 2))
        px = np.arange(x0, x1) + 0.5 - cx
        py = np.arange(y0, y1)[:, None] + 0.5 - cy
        a = alpha * np.clip(r - np.hypot(px, py) + 0.5, 0.0, 1.0)
        region_pixels = image[y0:y1, x0:x1]
        region_pixels += (255.0 - region_pixels) * a[:, :, None]
    return image * (1.0 - ICONS.OVERLAY)


def waves(width, height, region):
    """Azahar's background: region (x, y, w, h) of its 1920x1080 screen at this size, its waves
    where they start (render-icons.py's waves(), a row at a time)."""
    rx, ry, rw, rh = region
    sx, sy = rw / width, rh / height
    fy = ry + (np.arange(height, dtype=np.float64)[:, None] + 0.5) * sy
    fx = rx + (np.arange(width, dtype=np.float64)[None, :] + 0.5) * sx
    t = np.clip(fy / 1080.0, 0.0, 1.0)
    image = np.empty((height, width, 3))
    for c in range(3):
        image[:, :, c] = ICONS.WAVE_TOP[c] + (ICONS.WAVE_BOTTOM[c] - ICONS.WAVE_TOP[c]) * t
    for top, length, amplitude, colour, alpha in ICONS.WAVES:
        surface = top + amplitude * (1.0 - np.cos(2.0 * np.pi * fx / length))
        depth = (fy - surface) / sy
        body = np.clip(depth + 0.5, 0.0, 1.0)
        crest = np.clip(1.0 - np.abs(depth - 1.5 / sy) / (2.5 / sy), 0.0, 1.0)
        a = np.minimum(1.0, body * alpha + crest * 0.35)
        image += (np.array(colour, dtype=np.float64) - image) * a[:, :, None]
    return image * (1.0 - ICONS.WAVE_OVERLAY)


def background():
    left = bubbles(HALF, H, (373, 0, 640, 720))
    right = waves(W - HALF, H, (480, -260, 960, 1080))  # the waves below the label
    image = np.concatenate([left, right], axis=1)
    # the line between the two halves, as on the start screen and the icon
    image[:, HALF - 2:HALF + 2] += (255.0 - image[:, HALF - 2:HALF + 2]) * 0.35
    return Image.fromarray(np.clip(np.rint(image), 0, 255).astype(np.uint8), "RGB").convert("RGBA")


def device(kind, scale, centre):
    """A device as an RGBA layer over the whole picture: its colours where its silhouette is, and
    its shadow, drawn by render-icons.py in the box it fills."""
    draw = ICONS.DEVICES[kind]
    ox, oy = centre
    # its unit square's part that has it, with a margin
    x0, x1 = int(ox - 0.45 * scale), int(ox + 0.45 * scale)
    y0, y1 = int(oy - 0.42 * scale), int(oy + 0.42 * scale)
    pixel = 1.0 / scale
    rgba = np.zeros((y1 - y0, x1 - x0, 4), dtype=np.uint8)
    for y in range(y0, y1):
        v = (y + 0.5 - oy) / scale + 0.5
        row = rgba[y - y0]
        for x in range(x0, x1):
            u = (x + 0.5 - ox) / scale + 0.5
            colour, silhouette = draw(u, v, pixel, ICONS.BODY)
            if silhouette > 0.0:
                row[x - x0] = (int(round(colour[0])), int(round(colour[1])), int(round(colour[2])), int(round(silhouette * 255)))
    layer = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    layer.paste(Image.fromarray(rgba, "RGBA"), (x0, y0))
    shadow = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    shadow.putalpha(layer.getchannel("A").point(lambda a: a * 0.45))
    shadow = shadow.transform(shadow.size, Image.AFFINE, (1, 0, 0, 0, 1, -int(0.02 * scale)))  # a little lower
    return Image.alpha_composite(shadow.filter(ImageFilter.GaussianBlur(0.02 * scale)), layer)


def font(candidates, size):
    for path in candidates:
        if os.path.isfile(path):
            return ImageFont.truetype(path, size)
    sys.exit("no font found: " + ", ".join(candidates))


def labels():
    """Each side's emulator under its device, and the app's name across the two halves, on a soft
    dark band that keeps it legible over the blue and the yellow alike; all with a soft shadow."""
    layer = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    draw = ImageDraw.Draw(layer)
    side, what = font(FONTS, 104), font(SUBTITLE_FONTS, 64)
    for (scale, (cx, cy)), name, system, accent in (
            (GAMEPAD, "Cemu", "Wii U", ICONS.BLUE["accent"]),
            (N3DS, "Azahar", "Nintendo 3DS", (0xff, 0xf1, 0xc8))):
        top = cy + int(0.40 * scale) + 24
        draw.text((cx, top), name, font=side, fill=ICONS.BODY + (255,), anchor="mt")
        draw.text((cx, top + 132), system, font=what, fill=accent + (255,), anchor="mt")

    # the wordmark, centred on the line between the halves
    mark, tagline = font(FONTS, 220), font(SUBTITLE_FONTS, 78)
    top = int(H * 0.715)
    line = "Wii U and Nintendo 3DS games on PlayStation 5"
    left, _, right, bottom = draw.textbbox((HALF, top), "PS5CEMU-HAR", font=mark, anchor="mt")
    line_left, _, line_right, line_bottom = draw.textbbox((HALF, bottom + 40), line, font=tagline, anchor="mt")
    band = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    ImageDraw.Draw(band).rounded_rectangle([min(left, line_left) - 90, top - 50, max(right, line_right) + 90, line_bottom + 60],
                                           radius=70, fill=(4, 8, 16, 120))
    band = band.filter(ImageFilter.GaussianBlur(24))
    draw.text((HALF, top), "PS5CEMU-HAR", font=mark, fill=ICONS.BODY + (255,), anchor="mt")
    draw.text((HALF, bottom + 40), line, font=tagline, fill=(0xdd, 0xe6, 0xf0, 255), anchor="mt")

    shadow = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    shadow.putalpha(layer.getchannel("A").point(lambda a: a * 0.5))
    return Image.alpha_composite(Image.alpha_composite(band, shadow.filter(ImageFilter.GaussianBlur(10))), layer)


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
    picture = background()
    for kind, (scale, centre) in (("wiiu", GAMEPAD), ("3ds", N3DS)):
        picture = Image.alpha_composite(picture, device(kind, scale, centre))
    picture = Image.alpha_composite(picture, labels()).convert("RGB")
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
