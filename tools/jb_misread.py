#!/usr/bin/env python3
"""JB6: core 1 misread census per RAM region (core 0 runs the NAM front meanwhile)."""
import struct, sys, time
sys.path.insert(0, __import__("os").path.dirname(__file__))
from bb_install import unpack7
import mido
NAMES = ["low RAM 0x01C08000 (old core 1 stack)", "RAM .bss", "free high RAM 0x01C60000", "free RAMTEXT 0x01C02000"]
n = [x for x in mido.get_input_names() if "armaria" in x or "Juice" in x][0]
secs = int(sys.argv[1])
for r in [int(a) for a in sys.argv[2:]] or range(4):
    got = []
    inp = mido.open_input(n, callback=lambda m: got.append(bytes(m.bytes()))); out = mido.open_output(n)
    time.sleep(0.4); out.send(mido.Message("sysex", data=b"\x7DJB6" + bytes([r, secs]))); t = time.time()
    while not [g for g in got if g[:5] == b"\xF0\x7DJB6"] and time.time() - t < secs + 10:
        time.sleep(0.05)
    inp.close(); out.close()
    g = [g for g in got if g[:5] == b"\xF0\x7DJB6"]
    if not g:
        print(f"region {r}: no answer"); break
    rounds, bad, a, v, e, fr = struct.unpack("<6I", unpack7(g[0][5:-1])[:24])
    reads = rounds * 1024
    extra = f"  1st: [{a:#010x}] = {v:#010x} (expected {e:#010x}, xor {v ^ e:#010x})" if bad else ""
    print(f"region {r} {NAMES[r]}, {secs} s: {reads} reads, {bad} wrong ({bad / max(reads, 1):.2e}), core 0 ran {fr} fronts{extra}", flush=True)
    time.sleep(0.5)
