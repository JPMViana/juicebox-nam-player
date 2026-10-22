#!/usr/bin/env python3
"""Generate the firmware's build-time assets into OUTDIR:

  splash.h   240x240 RGB565 (big-endian, as the ST7789 takes it) boot image
  font.h     8x16 1-bpp monospaced font, ASCII 32..126 (for status lines)
  loader.h   the official JieLi update loader body (rescue staging), from the
             stock package unpacked by tools/bbpkg.py (vendor bytes: build/ only)

  gen_assets.py OUTDIR --loader analysis/v20_pkg/ota.bin [--png preview.png]
"""
import argparse
import struct
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

HERE = Path(__file__).resolve().parent
FONT = HERE.parent / "assets" / "InterTight.ttf"
W = H = 240
STATUS_Y = 212            # the firmware draws the status line from here (armaria.c)


def font(size, weight):
    f = ImageFont.truetype(str(FONT), size)
    try:
        f.set_variation_by_axes([weight])
    except (OSError, AttributeError):
        pass
    return f


def splash():
    """Juice-Box: a glass jug of orange juice, the name, the author"""
    S = 4                                            # supersampled, then reduced: smooth edges
    big = Image.new("RGB", (W * S, H * S), (14, 14, 16))
    d = ImageDraw.Draw(big)

    def R(*v):
        return tuple(int(x * S) for x in v)

    glass, rim = (120, 140, 150), (190, 210, 220)
    jx0, jy0, jx1, jy1 = 82, 22, 150, 118           # jug body
    # juice: a vertical gradient inside the body, below the surface line
    for y in range(52 * S, jy1 * S - 6 * S):
        t = min(1.0, max(0.0, (y - 52 * S) / ((jy1 - 6) * S - 52 * S)))
        c = (255, int(190 - 50 * t), int(50 - 35 * t))
        d.line((jx0 * S + 5 * S, y, jx1 * S - 5 * S, y), fill=c)
    d.rounded_rectangle(R(jx0 + 5, jy1 - 18, jx1 - 5, jy1 - 4), radius=10 * S, fill=(255, 140, 15))
    d.ellipse(R(jx0 + 5, 47, jx1 - 5, 57), fill=(255, 214, 90))          # the surface
    # glass outline, spout, handle
    d.rounded_rectangle(R(jx0, jy0, jx1, jy1), radius=12 * S, outline=glass, width=3 * S)
    d.polygon(R(jx0, jy0 + 2, jx0 - 10, jy0 - 6, jx0 + 8, jy0), fill=glass)
    d.arc(R(jx1 - 8, 40, jx1 + 26, 96), 280, 80, fill=glass, width=5 * S)
    d.line(R(jx0 + 2, jy0, jx1 - 2, jy0), fill=rim, width=3 * S)
    d.line(R(jx0 + 12, 30, jx0 + 12, 104), fill=(230, 240, 245), width=2 * S)   # highlight
    # an orange slice on the rim and a straw
    d.line(R(128, 6, 116, 70), fill=(80, 200, 120), width=4 * S)
    cx, cy, r = 146, 24, 15
    d.ellipse(R(cx - r, cy - r, cx + r, cy + r), fill=(255, 140, 0))
    d.ellipse(R(cx - r + 3, cy - r + 3, cx + r - 3, cy + r - 3), fill=(255, 200, 80))
    for k in range(6):
        import math
        a = k * math.pi / 3
        d.line(R(cx, cy, cx + (r - 4) * math.cos(a), cy + (r - 4) * math.sin(a)), fill=(255, 150, 20), width=S)
    img = big.resize((W, H), Image.LANCZOS)
    d = ImageDraw.Draw(img)
    t = "Juice-Box"
    f = font(36, 700)
    w = d.textlength(t, font=f)
    d.text(((W - w) / 2, 124), t, font=f, fill=(250, 244, 232))
    t = "N A M   P L A Y E R"
    f = font(12, 550)
    w = d.textlength(t, font=f)
    d.text(((W - w) / 2, 168), t, font=f, fill=(255, 160, 40))
    t = "por João Viana"
    f = font(13, 450)
    w = d.textlength(t, font=f)
    d.text(((W - w) / 2, 186), t, font=f, fill=(150, 146, 140))
    d.line((20, STATUS_Y - 4, W - 20, STATUS_Y - 4), fill=(40, 40, 46), width=1)
    return img


def rgb565_be(img):
    out = bytearray()
    for r, g, b in (img.get_flattened_data() if hasattr(img, 'get_flattened_data') else img.getdata()):
        v = (r >> 3) << 11 | (g >> 2) << 5 | (b >> 3)
        out += struct.pack(">H", v)
    return bytes(out)


def font_bitmap():
    f = font(13, 500)
    glyphs = bytearray()
    for c in range(32, 127):
        im = Image.new("L", (8, 16), 0)
        d = ImageDraw.Draw(im)
        d.text((0, 0), chr(c), font=f, fill=255)
        for y in range(16):
            row = 0
            for x in range(8):
                if im.getpixel((x, y)) >= 110:
                    row |= 0x80 >> x
            glyphs.append(row)
    return bytes(glyphs)


def c_array(name, data, per=16):
    lines = [f"static const uint8_t {name}[{len(data)}] = {{"]
    for i in range(0, len(data), per):
        lines.append("    " + ", ".join(f"0x{b:02X}" for b in data[i:i + per]) + ",")
    lines.append("};")
    return "\n".join(lines) + "\n"


def crc16(data, crc=0):
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) if crc & 0x8000 else crc << 1
        crc &= 0xFFFF
    return crc


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("outdir", type=Path)
    ap.add_argument("--loader", type=Path, required=True)
    ap.add_argument("--png", type=Path)
    a = ap.parse_args()
    a.outdir.mkdir(parents=True, exist_ok=True)

    img = splash()
    if a.png:
        img.save(a.png)
    (a.outdir / "splash.h").write_text(
        f"/* generated by fw/tools/gen_assets.py */\n#define SPLASH_STATUS_Y {STATUS_Y}u\n"
        + c_array("SPLASH565", rgb565_be(img), 24))
    (a.outdir / "font.h").write_text("/* generated by fw/tools/gen_assets.py: Inter Tight (OFL), 8x16, ASCII 32..126 */\n"
                                     + c_array("FONT8X16", font_bitmap()))

    ota = a.loader.read_bytes()
    hcrc, dcrc, doff, dlen, attr = struct.unpack_from("<HHIIB", ota, 0)
    body = ota[0x20:0x20 + dlen]
    if doff != 0x20 or attr != 0x41 or crc16(ota[2:0x20]) != hcrc or crc16(body) != dcrc:
        raise SystemExit(f"{a.loader}: not a JieLi update loader image")
    (a.outdir / "loader.h").write_text(
        "/* generated by fw/tools/gen_assets.py: the official update loader body (vendor binary, never committed) */\n"
        f"#define RESCUE_LDR_LEN 0x{dlen:X}u\n#define RESCUE_LDR_DCRC 0x{dcrc:04X}u\n"
        + c_array("RESCUE_LDR", body))
    print(f"assets -> {a.outdir}: splash {W}x{H}, font 95 glyphs, loader {dlen} B (dcrc {dcrc:#06x})")


if __name__ == "__main__":
    main()
