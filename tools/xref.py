#!/usr/bin/env python3
"""Cross-references in a JieLi-objdump listing of app.bin (base 0x02000120).

  xref.py DIS APP.bin --string screen_task      who loads the address of a string
  xref.py DIS APP.bin --addr 0x2012345          who loads / calls an address
  xref.py DIS APP.bin --func 0x2012345 [-n 80]  print the listing from an address
  xref.py DIS APP.bin --callers 0x2012345       functions that call an address
  xref.py DIS APP.bin --strings-in 0x2012345    strings referenced by the function at an address

The listing comes from analysis/disasm (see docs/06-analise-firmware.md).
Functions are approximated as ranges between call targets.
"""
import argparse
import bisect
import re
from pathlib import Path

BASE = 0x02000120   # app.bin offset 0 (the SPL maps it here; Felucca app.ld)
LINE = re.compile(r"^\s*([0-9a-f]{7,8}):\s+((?:[0-9a-f]{2} )+)\s*(.*)$")
TARGET = re.compile(r": ([0-9a-f]+) >")


def load(dis):
    rows = []
    for ln in Path(dis).read_text(errors="replace").splitlines():
        m = LINE.match(ln)
        if m:
            rows.append((int(m[1], 16), m[3].strip()))
    return rows


def targets(text):
    return [int(t, 16) for t in TARGET.findall(text)]


def func_starts(rows):
    starts = {BASE}
    for _, txt in rows:
        if txt.startswith("call "):
            starts.update(t for t in targets(txt) if t >= BASE)
    return sorted(starts)


def func_of(starts, addr):
    i = bisect.bisect_right(starts, addr) - 1
    return starts[max(i, 0)]


def cstring(app, addr):
    off = addr - BASE
    if not 0 <= off < len(app):
        return None
    end = app.find(b"\0", off)
    s = app[off:end if end >= 0 else off + 64]
    return s.decode() if 3 <= len(s) <= 120 and all(32 <= c < 127 for c in s) else None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dis")
    ap.add_argument("app")
    ap.add_argument("--string")
    ap.add_argument("--addr", type=lambda s: int(s, 0))
    ap.add_argument("--func", type=lambda s: int(s, 0))
    ap.add_argument("--callers", type=lambda s: int(s, 0))
    ap.add_argument("--strings-in", type=lambda s: int(s, 0))
    ap.add_argument("-n", type=int, default=60)
    a = ap.parse_args()
    rows = load(a.dis)
    app = Path(a.app).read_bytes()
    starts = func_starts(rows)

    want = []
    if a.string:
        i = -1
        while (i := app.find(a.string.encode() + b"\0", i + 1)) >= 0:
            # the string may be a suffix of a longer one; report its own start
            want.append(BASE + i)
        print(f"{a.string!r} at", ", ".join(hex(w) for w in want) or "(not found)")
    if a.addr is not None:
        want.append(a.addr)
    for w in want:
        for addr, txt in rows:
            if w in targets(txt):
                print(f"  {addr:#x} (func {func_of(starts, addr):#x}): {txt}")
    if a.callers is not None:
        seen = sorted({func_of(starts, addr) for addr, txt in rows
                       if txt.startswith("call ") and a.callers in targets(txt)})
        print(f"callers of {a.callers:#x}:", ", ".join(hex(s) for s in seen) or "(none)")
    if a.func is not None:
        i = bisect.bisect_left([r[0] for r in rows], a.func)
        for addr, txt in rows[i:i + a.n]:
            note = " ".join(f'"{s}"' for t in targets(txt) if (s := cstring(app, t)))
            print(f"{addr:#x}: {txt}" + (f"    ; {note}" if note else ""))
    if a.strings_in is not None:
        f0 = func_of(starts, a.strings_in)
        f1 = starts[bisect.bisect_right(starts, f0)] if bisect.bisect_right(starts, f0) < len(starts) else 1 << 32
        for addr, txt in rows:
            if f0 <= addr < f1:
                for t in targets(txt):
                    if (s := cstring(app, t)):
                        print(f"  {addr:#x}: \"{s}\"")


if __name__ == "__main__":
    main()
