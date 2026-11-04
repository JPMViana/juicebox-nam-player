#!/usr/bin/env python3
"""The installer's icon: the juice jug from the firmware's splash screen (fw/tools/gen_assets.py) on a dark tile.

  installer/scripts/gen_icon.py      -> installer/build/appicon.png (1024, macOS) + build/windows/icon.ico
"""
import math
from pathlib import Path

from PIL import Image, ImageDraw

HERE = Path(__file__).resolve().parent
BUILD = HERE.parent / "build"
N = 1024
SS = 2                                    # supersampling


def tile(size):
    """macOS-style rounded square with the system grid's margin (824 of 1024), transparent outside"""
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    m, r = size * 100 // 1024, size * 185 // 1024
    top, bot = (34, 32, 36), (14, 14, 16)
    mask = Image.new("L", (size, size), 0)
    ImageDraw.Draw(mask).rounded_rectangle((m, m, size - m, size - m), radius=r, fill=255)
    grad = Image.new("RGBA", (size, size))
    gd = ImageDraw.Draw(grad)
    for y in range(size):
        t = y / size
        gd.line((0, y, size, y), fill=tuple(int(a + (b - a) * t) for a, b in zip(top, bot)) + (255,))
    img.paste(grad, (0, 0), mask)
    return img, d


def jug(d, ox, oy, s):
    """the splash's jug (same geometry, splash units), drawn at origin (ox, oy) with s pixels per unit"""
    def P(*v):
        return tuple(ox + v[i] * s if i % 2 == 0 else oy + v[i] * s for i in range(len(v)))

    glass, rim = (130, 150, 160), (200, 218, 228)
    jx0, jy0, jx1, jy1 = 82, 22, 150, 118
    y = 52.0
    while y < jy1 - 6:
        t = min(1.0, max(0.0, (y - 52) / ((jy1 - 6) - 52)))
        d.line(P(jx0 + 5, y, jx1 - 5, y), fill=(255, int(190 - 50 * t), int(50 - 35 * t)), width=max(1, int(s / 4) + 1))
        y += 0.25
    d.rounded_rectangle(P(jx0 + 5, jy1 - 18, jx1 - 5, jy1 - 4), radius=10 * s, fill=(255, 140, 15))
    d.ellipse(P(jx0 + 5, 47, jx1 - 5, 57), fill=(255, 214, 90))
    d.rounded_rectangle(P(jx0, jy0, jx1, jy1), radius=12 * s, outline=glass, width=int(3 * s))
    d.polygon(P(jx0, jy0 + 2, jx0 - 10, jy0 - 6, jx0 + 8, jy0), fill=glass)
    d.arc(P(jx1 - 14, 40, jx1 + 20, 96), 285, 75, fill=glass, width=int(5 * s))
    d.line(P(jx0 + 2, jy0, jx1 - 2, jy0), fill=rim, width=int(3 * s))
    d.line(P(jx0 + 12, 30, jx0 + 12, 104), fill=(230, 240, 245), width=int(2 * s))
    d.line(P(128, 6, 116, 70), fill=(80, 200, 120), width=int(4 * s))
    cx, cy, r = 146, 24, 15
    d.ellipse(P(cx - r, cy - r, cx + r, cy + r), fill=(255, 140, 0))
    d.ellipse(P(cx - r + 3, cy - r + 3, cx + r - 3, cy + r - 3), fill=(255, 200, 80))
    for k in range(6):
        a = k * math.pi / 3
        d.line(P(cx, cy, cx + (r - 4) * math.cos(a), cy + (r - 4) * math.sin(a)), fill=(255, 150, 20), width=max(1, int(s)))


def icon(size):
    big = size * SS
    img, _ = tile(big)
    layer = Image.new("RGBA", (big, big), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    # the drawing spans x 72..170, y 6..118 (splash units): centre it, ~58 % of the tile's height
    s = big * 0.52 / 112
    cx, cy = (72 + 170) / 2, (6 + 118) / 2
    jug(d, big / 2 - cx * s, big / 2 - cy * s, s)
    img = Image.alpha_composite(img, layer)
    return img.resize((size, size), Image.LANCZOS)


def main():
    png = icon(N)
    png.save(BUILD / "appicon.png")
    png.save(BUILD / "windows" / "icon.ico", sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
    print(f"icon -> {BUILD / 'appicon.png'}, {BUILD / 'windows' / 'icon.ico'}")


if __name__ == "__main__":
    main()
