#!/usr/bin/env python3
"""JBD: test the chip's matrix x vector accelerator (0x01EE0400) on the pedal (Juice-Box v0.17.1+, docs/09-onyx-analysis.md).

  tools/jb_accel.py [TEST ...] [--repeat N]      tests: 0 = 4x3, 1 = 64x20 (A2-Lite k=6 layer), 2 = 64x47 (k=15), 3 = stock 9x17 job
Read-only for the flash; it only runs jobs in RAM and compares them with the result computed on the pedal's CPU.
"""
import argparse, struct, sys, time
sys.path.insert(0, __import__("os").path.dirname(__file__))
from bb_install import unpack7
import mido

STATUS = {0: "ok", 1: "WRONG RESULT", 2: "TIMEOUT (no answer from the unit)", 3: "ok, but wrote past its rows", 0xFF: "bad test"}
NAMES = {0: "4 x 3", 1: "64 x 20, 12-byte output", 2: "64 x 47, 12-byte output", 3: "stock 9 x 17 (mode G)"}

ap = argparse.ArgumentParser()
ap.add_argument("tests", nargs="*", type=int, default=[0, 1, 2, 3])
ap.add_argument("--repeat", type=int, default=3)
a = ap.parse_args()
names = [x for x in mido.get_input_names() if "armaria" in x or "Juice" in x]
if not names:
    raise SystemExit("Juice-Box not found (MIDI inputs: %s)" % mido.get_input_names())
got = []
inp = mido.open_input(names[0], callback=lambda m: got.append(bytes(m.bytes())))
out = mido.open_output(names[0])
time.sleep(0.4)
fails = 0
for t in a.tests:
    for k in range(a.repeat):
        got.clear()
        out.send(mido.Message("sysex", data=b"\x7DJBD" + bytes([t])))
        t0 = time.time()
        while not [g for g in got if g[:5] == b"\xF0\x7DJBD"] and time.time() - t0 < 3:
            time.sleep(0.02)
        g = [g for g in got if g[:5] == b"\xF0\x7DJBD"]
        if not g:
            print(f"test {t}: no answer (is the pedal running Juice-Box v0.17.1 or later?)"); fails += 1; break
        st, rows, cols, ticks, bad, first, v, e = struct.unpack("<8I", unpack7(g[0][5:-1])[:32])
        us = ticks / 24 if ticks != 0xFFFFFFFF else float("nan")
        line = f"test {t} ({NAMES.get(t, '?')}): {STATUS.get(st, st)}  {us:.1f} us"
        if rows and cols and ticks not in (0, 0xFFFFFFFF):
            line += f"  ({ticks / (rows * cols):.2f} ticks of 24 MHz per multiply-add)"
        if st == 1:
            line += f"  {bad} of {rows} rows wrong, first row {first}: got {v - (1 << 32) if v >> 31 else v}, expected {e - (1 << 32) if e >> 31 else e}"
        print(line)
        fails += st not in (0, 3)
inp.close(); out.close()
sys.exit(1 if fails else 0)
