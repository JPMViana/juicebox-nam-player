#!/usr/bin/env python3
"""JB9 MODE SECONDS: core 1 loops the fixed-point back half; core 0: 0 = float front, 1 = integer, 2 = only poll."""
import struct, sys, time
sys.path.insert(0, __import__("os").path.dirname(__file__))
from bb_install import unpack7
import mido
NAMES = {0: "core 0 on the float front", 1: "core 0 on integers", 2: "core 0 only in the service loop"}
m, secs = int(sys.argv[1]), int(sys.argv[2]) if len(sys.argv) > 2 else 20
n = [x for x in mido.get_input_names() if "armaria" in x or "Juice" in x][0]
got = []
inp = mido.open_input(n, callback=lambda msg: got.append(bytes(msg.bytes()))); out = mido.open_output(n)
time.sleep(0.4); out.send(mido.Message("sysex", data=b"\x7DJB9" + bytes([m, secs]))); t = time.time()
while not [g for g in got if g[:5] == b"\xF0\x7DJB9"] and time.time() - t < secs + 12:
    time.sleep(0.1)
inp.close(); out.close()
g = [g for g in got if g[:5] == b"\xF0\x7DJB9"]
if not g:
    raise SystemExit(f"mode {m} ({NAMES[m]}): no answer (hung?)")
r1, r0, done = struct.unpack("<3I", unpack7(g[0][5:-1])[:12])
print(f"mode {m} ({NAMES[m]}), {secs} s: core 1 {r1} back blocks ({r1 / secs:.0f}/s, {1e6 * secs / max(r1, 1):.0f} us each); "
      f"core 0 {r0} runs; finished {bool(done)}")
