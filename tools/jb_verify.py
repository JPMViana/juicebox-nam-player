#!/usr/bin/env python3
"""Read the accelerator self-check log (SRAM 0x01C7D200; tools/jb_onyx.py --verify), Juice-Box v0.18.0-dev."""
import os, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__))
out = subprocess.run([sys.executable, "-I", os.path.join(HERE, "jb_peek.py"), "0x01C7D200", "64"], capture_output=True, text=True).stdout
w = []
for line in out.splitlines():
    w += [int(x, 16) for x in line.split(":")[1].split()]
if len(w) < 14 or w[0] != 0x4656424A:
    raise SystemExit("no verify log")
s = lambda v: v - (1 << 32) if v >> 31 else v
print(f"jobs checked {w[1]}, wrong {w[2]}")
if w[2]:
    c = w[7]
    print(f"first wrong: job #{w[13]}  a {w[4]:#010x}  b {w[5]:#010x}  dst {w[6]:#010x}  rows {c >> 12 & 0xFFF} cols {c & 0xFFF} "
          f"stride {w[8]:#010x}: {w[12]} rows wrong, first row {w[9]}: got {s(w[10])}, want {s(w[11])}")
