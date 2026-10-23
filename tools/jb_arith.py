#!/usr/bin/env python3
"""JBX MODE SECONDS: core 1 dense arithmetic (0 = 32-bit multiply-add, 1 = 64-bit MAC) vs core 0 float front."""
import struct, sys, time
sys.path.insert(0, __import__("os").path.dirname(__file__))
from bb_install import unpack7
import mido
NAMES = {0: "multiplicação de 32 bits", 1: "multiplicação-acumulação de 64 bits"}
m, secs = int(sys.argv[1]), int(sys.argv[2]) if len(sys.argv) > 2 else 20
n = [x for x in mido.get_input_names() if "armaria" in x or "Juice" in x][0]
got = []
inp = mido.open_input(n, callback=lambda msg: got.append(bytes(msg.bytes()))); out = mido.open_output(n)
time.sleep(0.4); out.send(mido.Message("sysex", data=b"\x7DJBX" + bytes([m, secs]))); t = time.time()
while not [g for g in got if g[:5] == b"\xF0\x7DJBX"] and time.time() - t < secs + 12:
    time.sleep(0.1)
inp.close(); out.close()
g = [g for g in got if g[:5] == b"\xF0\x7DJBX"]
if not g:
    raise SystemExit(f"núcleo 1 em {NAMES[m]}: sem resposta (travou?)")
runs, bad, ref, first, r0, done = struct.unpack("<6I", unpack7(g[0][5:-1])[:24])
print(f"núcleo 1 em {NAMES[m]}, {secs} s: {runs} rodadas, {bad} erradas" + (f" (1ª {first:#010x}, certo {ref:#010x})" if bad else "")
      + f"; núcleo 0 {r0} fronts em float; terminou {bool(done)}")
