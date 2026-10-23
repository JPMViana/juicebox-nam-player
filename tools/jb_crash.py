#!/usr/bin/env python3
"""Read the live crash record (JBF): a core-1 fault stays there without a reset."""
import struct, sys, time
sys.path.insert(0, __import__("os").path.dirname(__file__))
from bb_install import unpack7
import mido
n = [x for x in mido.get_input_names() if "armaria" in x or "Juice" in x][0]
for _ in range(4):
    got = []
    inp = mido.open_input(n, callback=lambda m: got.append(bytes(m.bytes()))); out = mido.open_output(n)
    out.send(mido.Message("sysex", data=b"\x7DJBF")); t = time.time()
    while not [g for g in got if g[:5] == b"\xF0\x7DJBF"] and time.time() - t < 2:
        time.sleep(0.05)
    inp.close(); out.close()
    r = [g for g in got if g[:5] == b"\xF0\x7DJBF"]
    if r:
        break
else:
    raise SystemExit("no answer")
v = struct.unpack("<17I", unpack7(r[0][5:-1])[:68])
names = "magic count vec pc rets emu dbg sp psr icfg uptime_ms etm0 etm1 etm2 etm3 early core".split()
print(("registro VALIDO" if v[0] == 0x43525348 else "sem falha registrada"), " ".join(f"{k}={x:#x}" for k, x in zip(names, v)))
