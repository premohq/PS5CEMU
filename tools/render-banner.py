#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Draw the README's banner, with nothing but the standard library.

    render-banner.py OUTPUT_SVG

writes docs/banner.svg (1280x320): the app icon's GamePad (tools/render-icons.py's shapes) and the
app's name, on the Wii U Homebrew Launcher's background as the launcher draws it
(tools/render-background.py): its gradient, and the discs of the top 320 rows of its 1280x720
screen, under a lighter dark overlay than the launcher's.
"""

import importlib.util
import os
import sys

W, H = 1280, 320
T = 272          # the icon's unit square, in banner pixels
OX, OY = 56, 24  # where the unit square sits
OVERLAY = 0.35
BODY, SCREEN_TOP, SCREEN_BOTTOM = '#f4f8fc', '#1e4f7a', '#0b1e33'
ACCENT, DETAIL = '#9fd6ff', '#6a7e93'
FONT = "'Segoe UI', 'Helvetica Neue', Helvetica, Arial, sans-serif"


def background():
    spec = importlib.util.spec_from_file_location(
        'render_background', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'render-background.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def px(u):
    return round(OX + u * T, 2)


def py(v):
    return round(OY + v * T, 2)


def box(cx, cy, hw, hh, r, fill, extra=''):
    return (f'<rect x="{px(cx - hw)}" y="{py(cy - hh)}" width="{round(2 * hw * T, 2)}" height="{round(2 * hh * T, 2)}" '
            f'rx="{round(r * T, 2)}" fill="{fill}"{extra}/>')


def dot(cx, cy, r, fill):
    return f'<circle cx="{px(cx)}" cy="{py(cy)}" r="{round(r * T, 2)}" fill="{fill}"/>'


def gamepad():
    pad = [
        box(0.5, 0.53, 0.40, 0.205, 0.085, BODY, ' filter="url(#shadow)"'),
        box(0.5, 0.52, 0.205, 0.145, 0.02, ACCENT),
        box(0.5, 0.52, 0.19, 0.13, 0.014, 'url(#screen)'),
        # the play triangle on the screen (render-icons.py: u >= 0.47, |v - 0.52| * 1.15 <= (0.56 - u) * 0.62)
        f'<polygon points="{px(0.47)},{py(0.52 - 0.09 * 0.62 / 1.15)} {px(0.47)},{py(0.52 + 0.09 * 0.62 / 1.15)} '
        f'{px(0.56)},{py(0.52)}" fill="{ACCENT}"/>',
    ]
    for cx in (0.175, 0.825):  # sticks
        pad += [dot(cx, 0.43, 0.04, DETAIL), dot(cx, 0.43, 0.024, BODY)]
    pad += [box(0.175, 0.585, 0.045, 0.014, 0.005, DETAIL), box(0.175, 0.585, 0.014, 0.045, 0.005, DETAIL)]  # d-pad
    for cx, cy, c in ((0.825, 0.545, ACCENT), (0.86, 0.585, DETAIL), (0.79, 0.585, DETAIL), (0.825, 0.625, DETAIL)):
        pad.append(dot(cx, cy, 0.017, c))
    return pad


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    hbl = background()
    top = '#%02x%02x%02x' % hbl.TOP
    # the gradient runs over the launcher's whole 720 rows; the banner shows the top 320
    bottom = '#%02x%02x%02x' % tuple(round(a + (b - a) * H / 720) for a, b in zip(hbl.TOP, hbl.BOTTOM))
    discs = []
    for x, y, radius, alpha in hbl.particles():
        cx, cy, r = x * 1280, y * 720, radius * 1280
        if r >= 0.5 and cy - r < H:
            discs.append(f'<circle cx="{cx:.1f}" cy="{cy:.1f}" r="{r:.1f}" fill="#fff" fill-opacity="{alpha:.2f}"/>')
    text_x = OX + T + 40
    svg = f'''<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}" role="img" aria-label="PS5Cemu: Cemu, the Wii U emulator, on PlayStation 5 homebrew">
  <defs>
    <clipPath id="tile"><rect width="{W}" height="{H}" rx="28"/></clipPath>
    <linearGradient id="sky" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0" stop-color="{top}"/>
      <stop offset="1" stop-color="{bottom}"/>
    </linearGradient>
    <linearGradient id="screen" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0" stop-color="{SCREEN_TOP}"/>
      <stop offset="1" stop-color="{SCREEN_BOTTOM}"/>
    </linearGradient>
    <filter id="shadow" x="-20%" y="-30%" width="140%" height="170%">
      <feDropShadow dx="0" dy="{round(0.02 * T, 1)}" stdDeviation="{round(0.02 * T, 1)}" flood-color="#000" flood-opacity="0.45"/>
    </filter>
    <filter id="text-shadow" x="-5%" y="-30%" width="110%" height="160%">
      <feDropShadow dx="0" dy="2" stdDeviation="3" flood-color="#000" flood-opacity="0.35"/>
    </filter>
  </defs>
  <g clip-path="url(#tile)">
    <rect width="{W}" height="{H}" fill="url(#sky)"/>
    {(chr(10) + '    ').join(discs)}
    <rect width="{W}" height="{H}" fill="#000" fill-opacity="{OVERLAY}"/>
  </g>
  {(chr(10) + '  ').join(gamepad())}
  <g font-family="{FONT}" filter="url(#text-shadow)">
    <text x="{text_x}" y="152" font-size="104" font-weight="800" letter-spacing="-1" fill="{BODY}">PS5Cemu</text>
    <text x="{text_x + 4}" y="206" font-size="30" font-weight="600" fill="{ACCENT}">Cemu, the Wii U emulator, on PlayStation 5 homebrew</text>
    <text x="{text_x + 4}" y="252" font-size="22" fill="#d3e9fb">Vulkan on RADV · DualSense as the GamePad · community graphic packs</text>
  </g>
</svg>
'''
    with open(sys.argv[1], 'w', newline='\n', encoding='utf-8') as out:
        out.write(svg)


if __name__ == '__main__':
    main()
