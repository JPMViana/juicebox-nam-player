#!/usr/bin/env python3
"""Read the log of the last 16 accelerator jobs (SRAM 0x01C7D000, kept across a reset; Juice-Box v0.18.0-dev), newest last."""
import struct, subprocess, sys, os
HERE = os.path.dirname(os.path.abspath(__file__))
out = subprocess.run([sys.executable, "-I", os.path.join(HERE, "jb_peek.py"), "0x01C7D000", "512"], capture_output=True, text=True).stdout
words = []
for line in out.splitlines():
    words += [int(x, 16) for x in line.split(":")[1].split()]
recs = [words[i:i + 8] for i in range(0, len(words), 8) if len(words[i:i + 8]) == 8 and words[i + 7] == 0x4A4F4221]
for r in sorted(recs):
    n, cfg, a, b, d, ctrl, stride, _ = r
    print(f"job {n:6d}: cfg {cfg:#06x}  a {a:#010x}  b {b:#010x}  dst {d:#010x}  ctrl {ctrl:#010x} "
          f"(shift {ctrl >> 24}, rows {ctrl >> 12 & 0xFFF}, cols {ctrl & 0xFFF})  stride {stride:#010x}")
if not recs:
    print("no job records")
