#!/usr/bin/env python3
"""JB8 SECONDS: integer recursion on core 1 (checked) while core 0 runs the NAM front."""
import struct, sys, time
sys.path.insert(0, __import__("os").path.dirname(__file__))
from bb_install import unpack7
import mido
secs = int(sys.argv[1]) if len(sys.argv) > 1 else 20
n = [x for x in mido.get_input_names() if "armaria" in x or "Juice" in x][0]
got = []
inp = mido.open_input(n, callback=lambda m: got.append(bytes(m.bytes()))); out = mido.open_output(n)
time.sleep(0.4); out.send(mido.Message("sysex", data=b"\x7DJB8" + bytes([secs]))); t = time.time()
while not [g for g in got if g[:5] == b"\xF0\x7DJB8"] and time.time() - t < secs + 6:
    time.sleep(0.1)
inp.close(); out.close()
g = [g for g in got if g[:5] == b"\xF0\x7DJB8"]
if not g:
    raise SystemExit("sem resposta (o pedal pode ter reiniciado)")
runs, bad, first, ref, fronts, fl = struct.unpack("<6I", unpack7(g[0][5:-1])[:24])
print(f"{secs} s: núcleo 1 {runs} recursões, {bad} erradas" + (f" (1ª {first:#010x}, certo {ref:#010x})" if bad else "")
      + f"; núcleo 0 rodou {fronts} fronts do NAM; terminou {bool(fl & 1)}; falha no núcleo 1 {bool(fl & 2)}")
