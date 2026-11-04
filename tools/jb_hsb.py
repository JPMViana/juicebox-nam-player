#!/usr/bin/env python3
"""JBH D: set the HSB divider field (SYS_DIV bits 0..7, 0..3) until the next reset; prints SYS_DIV and the CPU clock."""
import struct, sys, time
sys.path.insert(0, __import__("os").path.dirname(__file__))
from bb_install import unpack7
import mido
d = int(sys.argv[1]) if len(sys.argv) > 1 else 0x7F       # 0x7F: only read
n = [a for a in mido.get_input_names() if "armaria" in a or "Juice" in a][0]
got = []
inp = mido.open_input(n, callback=lambda m: got.append(bytes(m.bytes()))); out = mido.open_output(n)
time.sleep(0.4); out.send(mido.Message("sysex", data=b"\x7DJBH" + bytes([d]))); t = time.time()
while not [g for g in got if g[:5] == b"\xF0\x7DJBH"] and time.time() - t < 5:
    time.sleep(0.05)
inp.close(); out.close()
g = [g for g in got if g[:5] == b"\xF0\x7DJBH"]
if not g:
    raise SystemExit("no answer")
a, b, ticks = struct.unpack("<3I", unpack7(g[0][5:-1])[:12])
print(f"SYS_DIV {a:#010x} -> {b:#010x}; clk_loop(200000) = {ticks} ticks de 24 MHz ({ticks / 24:.0f} us)")
