#!/usr/bin/env python3
"""JBY X Y SECONDS [MODE]: core 1 reads 4 KB at X, core 0 reads 4 KB at Y, both back to back; misreads and locks.
MODE bits (v0.13.12): 1 = no profiler, 2 = core 0 only polls, 4 = core 1 not started;
(v0.13.13) 8 = core 1 writes + reads back, 16 = core 0 runs the NAM front (float)."""
import struct, sys, time
sys.path.insert(0, __import__("os").path.dirname(__file__))
from bb_install import unpack7
import mido
x, y, secs = int(sys.argv[1], 0), int(sys.argv[2], 0), int(sys.argv[3]) if len(sys.argv) > 3 else 20
mode = int(sys.argv[4], 0) if len(sys.argv) > 4 else None
def enc(v): return bytes((v >> (7 * i)) & 0x7F for i in range(5))
n = [a for a in mido.get_input_names() if "armaria" in a or "Juice" in a][0]
got = []
inp = mido.open_input(n, callback=lambda m: got.append(bytes(m.bytes()))); out = mido.open_output(n)
time.sleep(0.4); out.send(mido.Message("sysex", data=b"\x7DJBY" + enc(x) + enc(y) + bytes([secs] + ([mode] if mode is not None else [])))); t = time.time()
while not [g for g in got if g[:5] == b"\xF0\x7DJBY"] and time.time() - t < secs + 12:
    time.sleep(0.1)
inp.close(); out.close()
g = [g for g in got if g[:5] == b"\xF0\x7DJBY"]
if not g:
    raise SystemExit(f"core 1 at {x:#010x}, core 0 at {y:#010x} ({abs(x - y) // 1024} KB): no answer (hung)")
d = unpack7(g[0][5:-1])
r1, b1, r0, b0, done = struct.unpack("<5I", d[:20])
first = struct.unpack("<6I", d[20:44]) if len(d) >= 44 else None
print(f"core 1 at {x:#010x}, core 0 at {y:#010x} ({abs(x - y) // 1024} KB), {secs} s: core 1 {r1} passes of 4 KB, {b1} wrong; "
      f"core 0 {r0} passes, {b0} wrong; finished {bool(done)}")
if first:
    for who, (a, v, e) in (("core 1", first[:3]), ("core 0", first[3:])):
        if v != e:
            print(f"  first error {who}: [{a:#010x}] = {v:#010x}, expected {e:#010x} (xor {v ^ e:#010x})")
