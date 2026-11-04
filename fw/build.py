#!/usr/bin/env python3
"""Build Juice-Box: app.bin and an installable BlackBox package (.fwsc).

  fw/build.py [--product BlackBox_901] [--bump-cfg]

Needs: Docker, the JieLi toolchain in .toolchain/toolchain (scripts/pi32cc.sh),
the stock package unpacked in analysis/v20_pkg (tools/bbpkg.py unpack: its head,
cfg and the official update loader go into our package and our rescue copy).
Outputs in build/fw/: juicebox.elf / .dis / .map-like symbol list, app.bin, juicebox-<product>.fwsc.
"""
import argparse
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FW = ROOT / "fw"
OUT = ROOT / "build" / "fw"
PKG = ROOT / "analysis" / "v20_pkg"
CC = ROOT / "scripts" / "pi32cc.sh"
APP_XIP = 0x02000120
APP_SLOT = 0xA0160                       # stock app.bin size: bbpkg keeps the slot sizes
CFLAGS = ["-Os", "-ffunction-sections", "-fno-builtin", "-Wall", "-Wno-unused-function", "-mcpu=r3", "-mfprev1"]
NAMFLAGS = ["-O2", "-fno-builtin", "-Wall", "-mcpu=r3", "-mfprev1"]   # (A2L_HOT empty: the kernel runs from XIP; v0.9 found
# that both cores executing RAM code at once lock the chip up, while XIP + XIP is fine)   # the NAM kernel: speed, hardware float


def tc(*args, capture=False):
    r = subprocess.run([str(CC), *map(str, args)], cwd=ROOT, capture_output=True, text=True)
    if r.returncode:
        sys.stderr.write(r.stdout + r.stderr)
        raise SystemExit(f"build: {args[0]} failed")
    if r.stderr.strip():
        sys.stderr.write(r.stderr)
    return r.stdout


def rel(p):
    return Path(p).resolve().relative_to(ROOT)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--product", default="BlackBox_904")
    ap.add_argument("--bump-cfg", action="store_true", help="see tools/bbpkg.py")
    a = ap.parse_args()
    if not (PKG / "manifest.json").exists():
        raise SystemExit("analysis/v20_pkg missing: python3 -I tools/bbpkg.py unpack firmware/stock/BlackBox_FACTORY_V20.fwsc analysis/v20_pkg")
    OUT.mkdir(parents=True, exist_ok=True)
    gen = OUT / "gen"
    subprocess.run([sys.executable, FW / "tools" / "gen_assets.py", gen, "--loader", PKG / "ota.bin",
                    "--png", OUT / "splash_preview.png"], check=True)

    inc = ["-I", rel(FW / "felucca" / "hal"), "-I", rel(FW / "felucca" / "src"), "-I", rel(gen), "-I", rel(ROOT / "nam")]
    c1data = "-DA2L_C1_DATA=__attribute__((section(\".c1_bss.z\")))"   # core 1's scratch in its RAM
    tc("clang", *NAMFLAGS, c1data, "-c", rel(ROOT / "nam" / "a2lite.c"), "-o", rel(OUT / "a2lite.o"))
    tc("clang", *NAMFLAGS, "-c", rel(ROOT / "nam" / "a2lx.c"), "-o", rel(OUT / "a2lx.o"))
    # core 1's half: integer only (no -mfprev1; checked below: not one float instruction)
    tc("clang", "-O2", "-fno-builtin", "-Wall", "-mcpu=r3", c1data, "-c", rel(ROOT / "nam" / "a2lq.c"), "-o", rel(OUT / "a2lq.o"))
    tc("clang", "-O2", "-fno-builtin", "-mcpu=r3", c1data, "-S", rel(ROOT / "nam" / "a2lq.c"), "-o", rel(OUT / "a2lq.s"))
    if "(f)" in (OUT / "a2lq.s").read_text():
        sys.exit("nam/a2lq.c: float instructions in core 1's code")
    tc("clang", "-O2", "-fno-builtin", "-mcpu=r3", "-mfprev1", "-c", rel(ROOT / "nam" / "bench_dev.c"), "-o", rel(OUT / "bench_dev.o"))
    tc("clang", *CFLAGS, *inc, f'-DFELUCCA_ID="{a.product}"', "-c", rel(FW / "src" / "juicebox.c"),
       "-o", rel(OUT / "juicebox.o"))
    tc("clang", "-c", rel(FW / "felucca" / "crt0.S"), "-o", rel(OUT / "crt0.o"))
    tc("clang", "-c", rel(FW / "felucca" / "hal" / "fm1_cpu1.S"), "-o", rel(OUT / "fm1_cpu1.o"))
    tc("clang", "-c", rel(FW / "felucca" / "hal" / "fm1_vec.S"), "-o", rel(OUT / "fm1_vec.o"))
    tc("clang", "-c", rel(FW / "src" / "jb_isr.S"), "-o", rel(OUT / "jb_isr.o"))
    elf = OUT / "juicebox.elf"
    tc("ld", "-T", rel(FW / "app.ld"), rel(OUT / "crt0.o"), rel(OUT / "fm1_cpu1.o"), rel(OUT / "fm1_vec.o"), rel(OUT / "jb_isr.o"), rel(OUT / "juicebox.o"), rel(OUT / "a2lite.o"), rel(OUT / "a2lx.o"), rel(OUT / "a2lq.o"), rel(OUT / "bench_dev.o"), "-o", rel(elf))
    for sect, name in ((".text", "text.bin"), (".ram_text", "ramtext.bin"), (".c1_text", "c1text.bin"), (".data", "data.bin")):
        (OUT / name).unlink(missing_ok=True)
        tc("common/bin/objcopy", "-O", "binary", "-j", sect, rel(elf), rel(OUT / name))
    syms = tc("common/bin/objdump", "-t", rel(elf))
    (OUT / "juicebox.syms").write_text(syms)
    (OUT / "juicebox.dis").write_text(tc("common/bin/objdump", "-d", rel(elf)))

    def symv(name):
        m = re.search(r"^([0-9a-f]+) .*\s" + re.escape(name) + r"$", syms, re.M)
        if not m:
            raise SystemExit(f"build: symbol {name} not found")
        return int(m.group(1), 16)

    img = bytearray((OUT / "text.bin").read_bytes())
    if img[:4] != bytes.fromhex("0481") and img[:2] != b"\x04\x81":
        print(f"warning: first instruction {img[:4].hex()} (the SPL expects `goto +4; rts`)")
    for name, lname in (("ramtext.bin", "_rt_load"), ("c1text.bin", "_c1_load"), ("data.bin", "_data_load")):
        blob = (OUT / name).read_bytes() if (OUT / name).exists() else b""
        if not blob:
            continue
        load = symv(lname)
        if load % 4 or load - APP_XIP < len(img):
            raise SystemExit(f"build: {lname} {load:#x} misplaced")
        img += b"\xff" * (load - APP_XIP - len(img)) + blob
    used = len(img)
    if used > APP_SLOT:
        raise SystemExit(f"build: app is {used} B, the slot is {APP_SLOT} B")
    img += b"\xff" * (APP_SLOT - used)
    app = OUT / "app.bin"
    app.write_bytes(img)
    rt = (OUT / "ramtext.bin").stat().st_size if (OUT / "ramtext.bin").exists() else 0
    print(f"app: {used} B used of {APP_SLOT} ({used * 100 // APP_SLOT} %), .ram_text {rt} B, "
          f"bss end {symv('_bss_end'):#x}")

    pkg = OUT / f"juicebox-{a.product}.fwsc"
    cmd = [sys.executable, "-I", ROOT / "tools" / "bbpkg.py", "build", PKG, pkg, "--app", app, "--product", a.product]
    if a.bump_cfg:
        cmd.append("--bump-cfg")
    subprocess.run(list(map(str, cmd)), check=True)


if __name__ == "__main__":
    main()
