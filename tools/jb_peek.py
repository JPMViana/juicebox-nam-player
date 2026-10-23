#!/usr/bin/env python3
"""Read RAM from the pedal (JBM).  jb_peek.py ADDR LEN [--words]"""
import argparse, struct, sys, time
sys.path.insert(0, __import__("os").path.dirname(__file__))
from bb_install import unpack7
import mido
ap = argparse.ArgumentParser(); ap.add_argument("addr", type=lambda x: int(x, 0)); ap.add_argument("len", type=lambda x: int(x, 0))
a = ap.parse_args()
n = [x for x in mido.get_input_names() if "armaria" in x or "Juice" in x][0]
def enc(v): return bytes((v >> (7 * i)) & 0x7F for i in range(5))
for _ in range(4):
    got = []
    inp = mido.open_input(n, callback=lambda m: got.append(bytes(m.bytes()))); out = mido.open_output(n)
    out.send(mido.Message("sysex", data=b"\x7DJBM" + enc(a.addr) + enc(a.len))); t = time.time()
    while not [g for g in got if g[:6] == b"\xF0\x7DJBM\x7F"] and time.time() - t < 4:
        time.sleep(0.05)
    inp.close(); out.close()
    if [g for g in got if g[:6] == b"\xF0\x7DJBM\x7F"]:
        break
chunks = {g[5]: unpack7(g[6:-1]) for g in got if g[:5] == b"\xF0\x7DJBM" and g[5] != 0x7F}
data = b"".join(chunks[i][:min(48, a.len - 48 * i)] for i in sorted(chunks))
for i in range(0, len(data), 16):
    w = struct.unpack_from(f"<{min(4, (len(data) - i) // 4)}I", data, i)
    print(f"{a.addr + i:#010x}: " + " ".join(f"{x:08x}" for x in w))
