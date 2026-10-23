#!/usr/bin/env python3
"""Core 1 and debug-unit state, live (JBQ): registers, PC, EMU / ETM, DBG_MSG, write windows, core 1's fault record."""
import struct, sys, time
sys.path.insert(0, __import__("os").path.dirname(__file__))
from bb_install import unpack7
import mido
n = [x for x in mido.get_input_names() if "armaria" in x or "Juice" in x][0]
for _ in range(4):
    got = []
    inp = mido.open_input(n, callback=lambda m: got.append(bytes(m.bytes()))); out = mido.open_output(n)
    out.send(mido.Message("sysex", data=b"\x7DJBQ")); t = time.time()
    while not [g for g in got if g[:6] == b"\xF0\x7DJBQ\x7F"] and time.time() - t < 3:
        time.sleep(0.05)
    inp.close(); out.close()
    if [g for g in got if g[:6] == b"\xF0\x7DJBQ\x7F"]:
        break
else:
    raise SystemExit("no answer")
chunks = {g[5]: unpack7(g[6:-1]) for g in got if g[:5] == b"\xF0\x7DJBQ" and g[5] != 0x7F}
data = b"".join(chunks[i][:48] for i in sorted(chunks))
w = struct.unpack(f"<{len(data) // 4}I", data[: len(data) // 4 * 4])
r = w[:32]
print("núcleo 1:", " ".join(f"r{i}={r[i]:08x}" for i in range(16)))
print(f"  RETI {r[16]:08x}  RETS {r[19]:08x}  PSR {r[21]:08x}  ICFG {r[27]:08x}  SP {r[30]:08x}  PC {r[31]:08x}")
e = w[32:36]; print(f"  EMU_CON {e[0]:08x}  EMU_MSG {e[1]:08x}   ETM", " ".join(f"{x:08x}" for x in w[37:41]))
d = w[41:52]
print(f"DBG_MSG {d[0]:08x}  DBG_EN {d[1]:08x}  DBG_CON {d[2]:08x}  C0_LIM {d[3]:02x}  C1_LIM {d[4]:02x}  erros {d[5]:06x}")
print(f"c1 trace {d[6]:08x}  job {d[7]}  done {d[8]}  bad {d[9]}")
nw = d[10]
print("janelas proibidas ao núcleo 1:", " ".join(f"[{w[52 + i]:08x}..{w[60 + i]:08x}]" for i in range(nw)))
c = w[68:68 + 18]
names = "magic count vec pc rets emu dbg sp psr icfg uptime_ms etm0 etm1 etm2 etm3 early core wrerr".split()
if c and c[0] == 0x43525348:
    print("FALHA NO NÚCLEO 1:", " ".join(f"{k}={x:#x}" for k, x in zip(names, c)))
else:
    print("núcleo 1 sem falha registrada")
if c and c[0] == 0x43525348 and len(w) >= 86 + 48:
    fr = w[86:86 + 48]
    print("  registradores na falha:", " ".join(f"r{i}={fr[i]:08x}" for i in range(16)))
    print("  pilha a partir do SP:")
    for i in range(0, 32, 8):
        print(f"    sp+{4 * i:3d}: " + " ".join(f"{x:08x}" for x in fr[16 + i:24 + i]))
