#!/usr/bin/env python3
"""Unpack and rebuild M-VAVE ANN BlackBox update packages (.fwsc).

  bbpkg.py unpack STOCK.fwsc DIR          components + manifest.json into DIR
  bbpkg.py build DIR OUT.fwsc [--product BlackBox_021] [--app APP.bin]
                              [--patch-string OLD=NEW ...] [--bump-cfg]
  bbpkg.py selftest STOCK.fwsc            rebuild without changes; flash.bin must be byte-identical

Package = 20 interleaved identity marker bytes + UFW container (header and file
list ENC-ciphered with 0xFFFF). The UFW we write holds only flash.bin and
ota.bin, as Felucca's packages do (accepted by the stock step-1 verifier).
flash.bin keeps the stock head [0, 0x4000) verbatim (SPL, isd_config, top
JLFS); the app area is rebuilt from its parts with fresh CRCs and SFC-ciphered
with the chip key.

--bump-cfg flips a padding byte in the nested eq_cfg_hw.bin entry name, so the
cfg header differs from the one already on the pedal: the stock verifier
refuses packages whose cfg header matches the installed one (FM-1-RE,
hardware-verified on the FM-1).

Formats follow kagaimiq/jl-misctools and Felucca tools/fm1pkg_make.py.
"""
import argparse
import json
import struct
import sys
from pathlib import Path

BLOCKS, BLK, KEEP, NOMARK = 20, 0x30, 0x2F, 0x7D
APP_AREA = 0x4000


def crc16(data, crc=0):
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) if crc & 0x8000 else (crc << 1)
        crc &= 0xFFFF
    return crc


def enc(buf, off, size, key=0xFFFF):
    """JieLi ENC stream cipher, in place (its own inverse)"""
    for i in range(size):
        buf[off + i] ^= key & 0xFF
        key = ((key << 1) ^ (0x1021 if key & 0x8000 else 0)) & 0xFFFF


def sfc(buf, off, size, key):
    """SFC cipher over buf[off:off+size]: ENC per 32-byte block, key mixed with the block address"""
    for i in range(0, size, 32):
        enc(buf, off + i, min(size - i, 32), key ^ (i >> 2))


def chipkey_decode(d):
    s = sum(d[:16]) & 0xFF
    s = 0xAA if s >= 0xE0 else 0x55 if s <= 0x10 else s
    return sum(1 << i for i in range(16) if (d[16 + i] ^ d[15 - i]) < s)


# ------------------------------------------------------------- JLFS entries ---

def parse_entry(buf, off):
    hcrc, body = struct.unpack_from("<H30s", buf, off)
    if crc16(body) != hcrc:
        raise ValueError(f"JLFS entry header CRC mismatch at {off:#x}")
    dcrc, eoff, size, flags, resvd, index, name = struct.unpack("<HIIBBH16s", body)
    return dict(dcrc=dcrc, offset=eoff, size=size, flags=flags, resvd=resvd, index=index, name=name.hex())


def make_entry(e, dcrc=None):
    body = struct.pack("<HIIBBH16s", e["dcrc"] if dcrc is None else dcrc, e["offset"], e["size"],
                       e["flags"], e["resvd"], e["index"], bytes.fromhex(e["name"]))
    return struct.pack("<H", crc16(body)) + body


def entry_name(e):
    return bytes.fromhex(e["name"]).split(b"\0")[0].decode(errors="replace")


# ---------------------------------------------------------------- container ---

def logical_image(raw):
    out = bytearray()
    for i in range(BLOCKS):
        out += raw[i * BLK:i * BLK + KEEP]
    return bytes(out + raw[BLOCKS * BLK:])


def product_of(raw):
    return "".join(chr((m - i - 1) & 0xFF) for i in range(BLOCKS) if (m := raw[i * BLK + KEEP]) != NOMARK)


def ufw_files(logical):
    hdr = bytearray(logical[:0x40])
    enc(hdr, 0, 0x40)
    hcrc, lcrc, total, n = struct.unpack_from("<HHIH", hdr, 0)
    if crc16(hdr[2:0x40]) != hcrc or hdr[16:22] != b"AC791N":
        raise ValueError("not an AC791N UFW package")
    files = {}
    for i in range(n):
        e = bytearray(logical[0x40 + i * 0x50:0x40 + (i + 1) * 0x50])
        enc(e, 0, 0x50)
        typ, idx, dcrc, _, off, ln, _ = struct.unpack_from("<HHHHIII", e, 0)
        files[e[0x40:].split(b"\0")[0].decode()] = (typ, bytes(logical[off:off + ln]))
    return files


def ufw_pack(files, product):
    """files: [(type, name, data)] -> .fwsc bytes (Felucca fm1pkg_make.ufw layout)"""
    off, ents = 0x400, []
    for i, (typ, name, d) in enumerate(files):
        off = (off + 0xFF) & ~0xFF
        e = bytearray(0x50)
        struct.pack_into("<HHHHIII", e, 0, typ, i, crc16(d), 0, off, len(d), (len(d) + 31) & ~31)
        e[0x40:0x40 + len(name)] = name.encode()
        ents.append((e, off, d))
        off += len(d)
    total = off
    hdr = bytearray(0x40)
    struct.pack_into("<IH", hdr, 4, total, len(files))
    struct.pack_into("<HHH", hdr, 10, 0x0004, 0x0200, 0)
    hdr[16:22] = b"AC791N"
    lst = bytearray()
    for e, _, _ in ents:
        x = bytearray(e)
        enc(x, 0, 0x50)
        lst += x
    struct.pack_into("<H", hdr, 2, crc16(lst))
    struct.pack_into("<H", hdr, 0, crc16(hdr[2:0x40]))
    enc(hdr, 0, 0x40)
    logical = bytearray(b"\xFF" * total)
    logical[0:0x40] = hdr
    logical[0x40:0x40 + len(lst)] = lst
    for _, o, d in ents:
        logical[o:o + len(d)] = d
    if len(product) > BLOCKS or any((ord(c) + i + 1) & 0xFF == NOMARK for i, c in enumerate(product)):
        raise SystemExit(f"product {product!r}: at most {BLOCKS} characters, none encoding to the unused marker")
    raw = bytearray()
    for i in range(BLOCKS):
        m = (ord(product[i]) + i + 1) & 0xFF if i < len(product) else NOMARK
        raw += logical[i * KEEP:(i + 1) * KEEP] + bytes([m])
    return bytes(raw + logical[BLOCKS * KEEP:])


# -------------------------------------------------------------- flash image ---

def unpack(stock_path, outdir):
    raw = Path(stock_path).read_bytes()
    files = ufw_files(logical_image(raw))
    flash = bytearray(files["flash.bin"][1])
    head0 = bytearray(flash[:0xA0])                 # flash header + top entries: ENC'd per 32-byte slot
    for o in range(0, len(head0), 32):
        enc(head0, o, 32)
    top = [parse_entry(head0, 0x20 + 0x20 * i) for i in range(4)]
    isd_e = next(e for e in top if entry_name(e) == "isd_config.ini")
    key = chipkey_decode(flash[isd_e["offset"]:isd_e["offset"] + 32])
    area = bytearray(flash[APP_AREA:])
    sfc(area, 0, len(area), key)
    head = parse_entry(area, 0)
    ents, o = [], 0x20
    while o < 0x120:
        if area[o:o + 32] == b"\xFF" * 32:
            break
        ents.append(parse_entry(area, o))
        o += 0x20
    blk = head["size"]
    cfg = parse_entry(area, blk)
    cfg_len = cfg["offset"] + cfg["size"] - 0x20 + 0x20     # cfg header + body
    cfg_raw = bytes(area[blk:blk + 0x20 + cfg["size"] - 0x20])
    out = Path(outdir)
    out.mkdir(parents=True, exist_ok=True)
    manifest = dict(product=product_of(raw), chip_key=key, flash_len=len(flash), app_area_head=head,
                    entries=ents, cfg_offset=blk, cfg_len=len(cfg_raw),
                    other_files={k: v[0] for k, v in files.items()})
    for e in ents:
        if e["flags"] == 0x82:   # data files in the block
            (out / entry_name(e)).write_bytes(area[e["offset"]:e["offset"] + e["size"]])
    (out / "cfg.raw").write_bytes(cfg_raw)
    (out / "flash_head.bin").write_bytes(flash[:APP_AREA])
    (out / "ota.bin").write_bytes(files["ota.bin"][1])
    (out / "flash.stock.bin").write_bytes(flash)
    (out / "manifest.json").write_text(json.dumps(manifest, indent=1))
    return manifest


def build_flash(d, app=None, bump_cfg=False):
    d = Path(d)
    m = json.loads((d / "manifest.json").read_text())
    key, blk = m["chip_key"], m["cfg_offset"]
    area = bytearray(b"\xFF" * (m["flash_len"] - APP_AREA))
    for e in m["entries"]:
        name = entry_name(e)
        if e["flags"] == 0x82:
            data = app if (name == "app.bin" and app is not None) else (d / name).read_bytes()
            if len(data) != e["size"]:
                raise SystemExit(f"{name}: {len(data)} B, the stock slot is {e['size']} B (sizes must match)")
            area[e["offset"]:e["offset"] + len(data)] = data
            e = dict(e, dcrc=crc16(data))
        idx = m["entries"].index(next(x for x in m["entries"] if x["name"] == e["name"]))
        area[0x20 + 0x20 * idx:0x40 + 0x20 * idx] = make_entry(e)
    area[0:0x20] = make_entry(m["app_area_head"], crc16(area[0x20:blk]))
    cfg = bytearray((d / "cfg.raw").read_bytes())
    if bump_cfg:
        inner = parse_entry(cfg, 0x20)              # nested eq_cfg_hw.bin entry
        name = bytearray.fromhex(inner["name"])
        name[15] ^= 0x01                            # last padding byte of the 16-byte name field
        inner["name"] = name.hex()
        cfg[0x20:0x40] = make_entry(inner)
        outer = parse_entry(cfg, 0)
        cfg[0:0x20] = make_entry(outer, crc16(cfg[0x20:]))
    area[blk:blk + len(cfg)] = cfg
    sfc(area, 0, blk + len(cfg), key)               # the rest of the image stays plain 0xFF, as in stock
    return (d / "flash_head.bin").read_bytes() + bytes(area)


def selftest(stock_path, workdir):
    unpack(stock_path, workdir)
    stock = (Path(workdir) / "flash.stock.bin").read_bytes()
    rebuilt = build_flash(workdir)
    if rebuilt != stock:
        diff = next(i for i in range(len(stock)) if stock[i] != rebuilt[i])
        raise SystemExit(f"selftest FAILED: rebuilt flash.bin differs from stock at {diff:#x}")
    print(f"selftest ok: rebuilt flash.bin is byte-identical to the stock one ({len(stock)} B)")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    u = sub.add_parser("unpack"); u.add_argument("stock"); u.add_argument("dir")
    s = sub.add_parser("selftest"); s.add_argument("stock"); s.add_argument("--dir", default="analysis/v20_pkg")
    b = sub.add_parser("build"); b.add_argument("dir"); b.add_argument("out")
    b.add_argument("--product"); b.add_argument("--app")
    b.add_argument("--patch-string", action="append", default=[], metavar="OLD=NEW",
                   help="replace a same-length byte string in app.bin (every occurrence)")
    b.add_argument("--bump-cfg", action="store_true")
    a = ap.parse_args()
    if a.cmd == "unpack":
        m = unpack(a.stock, a.dir)
        print(f"{m['product']}: chip key {m['chip_key']:#06x}, {len(m['entries'])} app-area entries -> {a.dir}")
    elif a.cmd == "selftest":
        selftest(a.stock, a.dir)
    else:
        d = Path(a.dir)
        m = json.loads((d / "manifest.json").read_text())
        app = Path(a.app).read_bytes() if a.app else (d / "app.bin").read_bytes()
        for p in a.patch_string:
            old, new = (x.encode() for x in p.split("=", 1))
            if len(old) != len(new):
                raise SystemExit(f"--patch-string {p!r}: OLD and NEW must have the same length")
            n = app.count(old)
            if not n:
                raise SystemExit(f"--patch-string: {old!r} not found in app.bin")
            app = app.replace(old, new)
            print(f"patched {n}x {old!r} -> {new!r}")
        flash = build_flash(d, app, a.bump_cfg)
        product = a.product or m["product"]
        pkg = ufw_pack([(0, "flash.bin", flash), (100, "ota.bin", (d / "ota.bin").read_bytes())], product)
        Path(a.out).write_bytes(pkg)
        print(f"package {a.out}: {len(pkg)} B, identity {product}")


if __name__ == "__main__":
    sys.exit(main())
