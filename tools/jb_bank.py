#!/usr/bin/env python3
"""JBY X Y SECONDS: core 1 reads 4 KB at X, core 0 reads 4 KB at Y, both back to back; misreads and locks."""
import struct, sys, time
sys.path.insert(0, __import__("os").path.dirname(__file__))
from bb_install import unpack7
import mido
x, y, secs = int(sys.argv[1], 0), int(sys.argv[2], 0), int(sys.argv[3]) if len(sys.argv) > 3 else 20
def enc(v): return bytes((v >> (7 * i)) & 0x7F for i in range(5))
n = [a for a in mido.get_input_names() if "armaria" in a or "Juice" in a][0]
got = []
inp = mido.open_input(n, callback=lambda m: got.append(bytes(m.bytes()))); out = mido.open_output(n)
time.sleep(0.4); out.send(mido.Message("sysex", data=b"\x7DJBY" + enc(x) + enc(y) + bytes([secs]))); t = time.time()
while not [g for g in got if g[:5] == b"\xF0\x7DJBY"] and time.time() - t < secs + 12:
    time.sleep(0.1)
inp.close(); out.close()
g = [g for g in got if g[:5] == b"\xF0\x7DJBY"]
if not g:
    raise SystemExit(f"núcleo 1 em {x:#010x}, núcleo 0 em {y:#010x} ({abs(x - y) // 1024} KB): sem resposta (travou)")
r1, b1, r0, b0, done = struct.unpack("<5I", unpack7(g[0][5:-1])[:20])
print(f"núcleo 1 em {x:#010x}, núcleo 0 em {y:#010x} ({abs(x - y) // 1024} KB), {secs} s: núcleo 1 {r1 * 1024} leituras, {b1} erradas; "
      f"núcleo 0 {r0 * 1024} leituras, {b0} erradas; terminou {bool(done)}")
