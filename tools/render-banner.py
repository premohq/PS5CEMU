#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Draw the README's banner, with nothing but the standard library.

    render-banner.py OUTPUT_SVG

writes docs/banner.svg (1280x320): PS5CEMU-HAR's two sides, split down the middle as its start
screen is. On the left Cemu's: the Wii U GamePad (tools/render-icons.py's shapes) on the Wii U
Homebrew Launcher's background as the launcher draws it (tools/render-background.py: its gradient
and the discs of the top 320 rows of its 1280x720 screen). On the right Azahar's: the 3DS on the 3DS
Homebrew Launcher's waves made yellow (port/frontend/wave.cpp's colours). The app's name runs across
the middle.
"""

import importlib.util
import math
import os
import sys

W, H = 1280, 320
HALF = W // 2
T = 272  # a device's unit square, in banner pixels
OVERLAY, WAVE_OVERLAY = 0.35, 0.22
BODY = '#f4f8fc'
BLUE = {'top': '#1e4f7a', 'bottom': '#0b1e33', 'accent': '#9fd6ff', 'detail': '#6a7e93'}
GOLD = {'top': '#7c5610', 'bottom': '#332206', 'accent': '#ffd25a', 'detail': '#8f826c'}
WAVE_TOP, WAVE_BOTTOM = (255, 204, 64), (236, 158, 22)
# top, wavelength, amplitude, colour, alpha: port/frontend/wave.cpp's layers, at the banner's size
WAVES = [(196, 430, 12, '#ffe48c', 0.26), (228, 320, 9, '#ffeca6', 0.32), (262, 240, 7, '#fff4c4', 0.40)]
FONT = "'Segoe UI', 'Helvetica Neue', Helvetica, Arial, sans-serif"


def background():
    spec = importlib.util.spec_from_file_location(
        'render_background', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'render-background.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class Device:
    """SVG shapes in a device's unit square, T pixels wide, its corner at (ox, oy)."""

    def __init__(self, ox, oy, palette, gradient):
        self.ox, self.oy, self.palette, self.gradient = ox, oy, palette, gradient

    def x(self, u):
        return round(self.ox + u * T, 2)

    def y(self, v):
        return round(self.oy + v * T, 2)

    def box(self, cx, cy, hw, hh, r, fill, extra=''):
        return (f'<rect x="{self.x(cx - hw)}" y="{self.y(cy - hh)}" width="{round(2 * hw * T, 2)}" '
                f'height="{round(2 * hh * T, 2)}" rx="{round(r * T, 2)}" fill="{fill}"{extra}/>')

    def dot(self, cx, cy, r, fill):
        return f'<circle cx="{self.x(cx)}" cy="{self.y(cy)}" r="{round(r * T, 2)}" fill="{fill}"/>'

    def play(self, cx, cy, size):
        # render-icons.py's play(): from cx - 0.45 size to its tip at cx + 0.55 size
        half = size * 0.62 / 1.15
        return (f'<polygon points="{self.x(cx - 0.45 * size)},{self.y(cy - half)} {self.x(cx - 0.45 * size)},'
                f'{self.y(cy + half)} {self.x(cx + 0.55 * size)},{self.y(cy)}" fill="{self.palette["accent"]}"/>')


def gamepad(d):
    accent, detail = d.palette['accent'], d.palette['detail']
    shapes = [
        d.box(0.5, 0.53, 0.40, 0.205, 0.085, BODY, ' filter="url(#shadow)"'),
        d.box(0.5, 0.52, 0.205, 0.145, 0.02, accent),
        d.box(0.5, 0.52, 0.19, 0.13, 0.014, f'url(#{d.gradient})'),
        d.play(0.515, 0.52, 0.09),
    ]
    for cx in (0.175, 0.825):  # sticks
        shapes += [d.dot(cx, 0.43, 0.04, detail), d.dot(cx, 0.43, 0.024, BODY)]
    shapes += [d.box(0.175, 0.585, 0.045, 0.014, 0.005, detail), d.box(0.175, 0.585, 0.014, 0.045, 0.005, detail)]
    for cx, cy, c in ((0.825, 0.545, accent), (0.86, 0.585, detail), (0.79, 0.585, detail), (0.825, 0.625, detail)):
        shapes.append(d.dot(cx, cy, 0.017, c))
    return shapes


def n3ds(d):
    accent, detail = d.palette['accent'], d.palette['detail']
    shapes = [
        f'<g filter="url(#shadow)">{d.box(0.5, 0.4925, 0.285, 0.03, 0.02, detail)}'
        f'{d.box(0.5, 0.30, 0.31, 0.175, 0.05, BODY)}{d.box(0.5, 0.685, 0.31, 0.175, 0.05, BODY)}</g>',
        d.box(0.5, 0.30, 0.215, 0.135, 0.016, accent),
        d.box(0.5, 0.30, 0.2, 0.12, 0.01, f'url(#{d.gradient})'),
        d.play(0.51, 0.30, 0.085),
        d.box(0.5, 0.665, 0.13, 0.1, 0.012, detail),
        d.box(0.5, 0.665, 0.12, 0.09, 0.008, f'url(#{d.gradient})'),
        d.dot(0.27, 0.6, 0.042, detail), d.dot(0.27, 0.6, 0.028, BODY),
        d.box(0.27, 0.735, 0.038, 0.012, 0.004, detail), d.box(0.27, 0.735, 0.012, 0.038, 0.004, detail),
    ]
    for cx, cy, c in ((0.73, 0.6, accent), (0.765, 0.635, detail), (0.695, 0.635, detail), (0.73, 0.67, detail)):
        shapes.append(d.dot(cx, cy, 0.0155, c))
    for cx in (0.43, 0.5, 0.57):  # Select, Home, Start
        shapes.append(d.box(cx, 0.8, 0.022, 0.007, 0.007, detail))
    return shapes


def wave(top, length, amplitude):
    points = [f'{x},{top + amplitude * (1 - math.cos(2 * math.pi * (x - HALF) / length)):.1f}' for x in range(HALF, W + 1, 8)]
    return f'M{HALF},{H} L' + ' L'.join(points) + f' L{W},{H} Z'


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    hbl = background()
    top = '#%02x%02x%02x' % hbl.TOP
    # the gradient runs over the launcher's whole 720 rows; the banner shows the top 320
    bottom = '#%02x%02x%02x' % tuple(round(a + (b - a) * H / 720) for a, b in zip(hbl.TOP, hbl.BOTTOM))
    discs = []
    for x, y, radius, alpha in hbl.particles():
        cx, cy, r = x * 1280 - 320, y * 720, radius * 1280
        if r >= 0.5 and cy - r < H and cx - r < HALF and cx + r > 0:
            discs.append(f'<circle cx="{cx:.1f}" cy="{cy:.1f}" r="{r:.1f}" fill="#fff" fill-opacity="{alpha:.2f}"/>')
    waves = [f'<path d="{wave(t, l, a)}" fill="{c}" fill-opacity="{alpha}"/>' for t, l, a, c, alpha in WAVES]
    left = Device(HALF // 2 - T // 2 - 150, 18, BLUE, 'screen-blue')
    right = Device(HALF + HALF // 2 - T // 2 + 150, 30, GOLD, 'screen-gold')
    svg = f'''<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}" role="img" aria-label="PS5CEMU-HAR: Cemu and Azahar, the Wii U and Nintendo 3DS emulators, on PlayStation 5 homebrew">
  <defs>
    <clipPath id="tile"><rect width="{W}" height="{H}" rx="28"/></clipPath>
    <clipPath id="left"><rect width="{HALF}" height="{H}"/></clipPath>
    <linearGradient id="sky" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0" stop-color="{top}"/>
      <stop offset="1" stop-color="{bottom}"/>
    </linearGradient>
    <linearGradient id="sand" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0" stop-color="#{'%02x%02x%02x' % WAVE_TOP}"/>
      <stop offset="1" stop-color="#{'%02x%02x%02x' % WAVE_BOTTOM}"/>
    </linearGradient>
    <linearGradient id="screen-blue" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0" stop-color="{BLUE['top']}"/>
      <stop offset="1" stop-color="{BLUE['bottom']}"/>
    </linearGradient>
    <linearGradient id="screen-gold" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0" stop-color="{GOLD['top']}"/>
      <stop offset="1" stop-color="{GOLD['bottom']}"/>
    </linearGradient>
    <filter id="shadow" x="-20%" y="-30%" width="140%" height="170%">
      <feDropShadow dx="0" dy="{round(0.02 * T, 1)}" stdDeviation="{round(0.02 * T, 1)}" flood-color="#000" flood-opacity="0.45"/>
    </filter>
    <filter id="text-shadow" x="-5%" y="-30%" width="110%" height="160%">
      <feDropShadow dx="0" dy="2" stdDeviation="3" flood-color="#000" flood-opacity="0.45"/>
    </filter>
    <filter id="soft" x="-10%" y="-40%" width="120%" height="180%"><feGaussianBlur stdDeviation="14"/></filter>
  </defs>
  <g clip-path="url(#tile)">
    <g clip-path="url(#left)">
      <rect width="{HALF}" height="{H}" fill="url(#sky)"/>
      {(chr(10) + '      ').join(discs)}
      <rect width="{HALF}" height="{H}" fill="#000" fill-opacity="{OVERLAY}"/>
    </g>
    <rect x="{HALF}" width="{W - HALF}" height="{H}" fill="url(#sand)"/>
    {(chr(10) + '    ').join(waves)}
    <rect x="{HALF}" width="{W - HALF}" height="{H}" fill="#000" fill-opacity="{WAVE_OVERLAY}"/>
    <rect x="{HALF - 1}" width="2" height="{H}" fill="#fff" fill-opacity="0.35"/>
  </g>
  {(chr(10) + '  ').join(gamepad(left))}
  {(chr(10) + '  ').join(n3ds(right))}
  <rect x="{HALF - 330}" y="104" width="660" height="150" rx="40" fill="#040810" fill-opacity="0.45" filter="url(#soft)"/>
  <g font-family="{FONT}" filter="url(#text-shadow)" text-anchor="middle">
    <text x="{HALF}" y="168" font-size="88" font-weight="800" letter-spacing="-1" fill="{BODY}">PS5CEMU-HAR</text>
    <text x="{HALF}" y="208" font-size="26" font-weight="600" fill="#e6eef6">Cemu and Azahar on PlayStation 5 homebrew</text>
    <text x="{HALF}" y="240" font-size="20" fill="#d3e2f0">Wii U and Nintendo 3DS · Vulkan on RADV · the DualSense as both</text>
  </g>
</svg>
'''
    with open(sys.argv[1], 'w', newline='\n', encoding='utf-8') as out:
        out.write(svg)


if __name__ == '__main__':
    main()
