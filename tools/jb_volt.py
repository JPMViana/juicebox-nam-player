#!/usr/bin/env python3
"""Read the P33 analog registers (JBV) and optionally set SYSVDD (core voltage) level 1..15
and, from v0.13.15, VDC14 (the SYSVDD LDO's input) level 0..7 = 1.25..1.60 V.
  jb_volt.py [--set LEVEL] [--vdc14 LEVEL]"""
import argparse, sys, time
sys.path.insert(0, __import__("os").path.dirname(__file__))
from bb_install import unpack7
import mido

ap = argparse.ArgumentParser(); ap.add_argument("--set", type=int); ap.add_argument("--vdc14", type=int); a = ap.parse_args()
n = [x for x in mido.get_input_names() if "armaria" in x or "Juice" in x][0]
for attempt in range(4):
    got = []
    inp = mido.open_input(n, callback=lambda m: got.append(bytes(m.bytes()))); out = mido.open_output(n)
    out.send(mido.Message("sysex", data=b"\x7DJBV" + bytes([a.set if a.set is not None else 0x7F] + ([a.vdc14] if a.vdc14 is not None else []))))
    t = time.time()
    while not [g for g in got if g[:5] == b"\xF0\x7DJBV"] and time.time() - t < 3:
        time.sleep(0.05)
    inp.close(); out.close()
    r = [g for g in got if g[:5] == b"\xF0\x7DJBV"]
    if r:
        break
else:
    raise SystemExit("no answer")
v = unpack7(r[0][5:-1])[:32]
print(" ".join(f"{i:02x}:{x:02x}" for i, x in enumerate(v)))
print(f"P3_ANA_CON9 = {v[9]:#04x} -> SYSVDD nivel {v[9] & 15} = {0.93 + 0.03 * (v[9] & 15):.2f} V")
print(f"P3_ANA_CON6 = {v[6]:#04x} -> VDC14 nivel {v[6] & 7} = {1.25 + 0.05 * (v[6] & 7):.2f} V")
