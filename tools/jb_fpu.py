#!/usr/bin/env python3
"""JB7: float on both cores at once. Modes: 0 = core 0 integer, 1 = core 0 float chain, 2 = core 0 NAM front."""
import struct, sys, time
sys.path.insert(0, __import__("os").path.dirname(__file__))
from bb_install import unpack7
import mido
NAMES = {0: "core 0 on integers", 1: "core 0 on float (same chain)", 2: "core 0 running the NAM front"}
n = [x for x in mido.get_input_names() if "armaria" in x or "Juice" in x][0]
for m in [int(a) for a in sys.argv[1:]] or [0, 1, 2]:
    got = []
    inp = mido.open_input(n, callback=lambda msg: got.append(bytes(msg.bytes()))); out = mido.open_output(n)
    time.sleep(0.4); out.send(mido.Message("sysex", data=b"\x7DJB7" + bytes([m]))); t = time.time()
    while not [g for g in got if g[:5] == b"\xF0\x7DJB7"] and time.time() - t < 7:
        time.sleep(0.05)
    inp.close(); out.close()
    g = [g for g in got if g[:5] == b"\xF0\x7DJB7"]
    if not g:
        print(f"mode {m}: no answer (the pedal may have rebooted)", flush=True); break
    r1, b1, f1, ref, kb1, r0, b0, done = struct.unpack("<8I", unpack7(g[0][5:-1])[:32])
    print(f"mode {m} ({NAMES[m]}): core 1 {r1} chains, {b1} wrong in float, {kb1} wrong in integer"
          + (f" (1st value {f1:#010x}, right {ref:#010x})" if b1 else "")
          + f"; core 0 {r0} runs, {b0} wrong; finished {bool(done)}", flush=True)
    time.sleep(0.5)
