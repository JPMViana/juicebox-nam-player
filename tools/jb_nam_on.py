#!/usr/bin/env python3
"""Turn the two-core NAM on (JBN) and watch the audio statistics.  jb_nam_on.py [--watch SECONDS]"""
import argparse, struct, sys, time
sys.path.insert(0, __import__("os").path.dirname(__file__))
from bb_install import unpack7
import mido

ap = argparse.ArgumentParser(); ap.add_argument("--watch", type=int, default=30); ap.add_argument("--no-enable", action="store_true")
a = ap.parse_args()
n = [x for x in mido.get_input_names() if "armaria" in x or "Juice" in x][0]
got = []
inp = mido.open_input(n, callback=lambda m: got.append(bytes(m.bytes()))); out = mido.open_output(n)

def ask(cmd, tag, tries=4, wait=2.0):
    for _ in range(tries):
        got.clear(); out.send(mido.Message("sysex", data=b"\x7DJB" + cmd)); t = time.time()
        while not [g for g in got if g[:5] == b"\xF0\x7DJB" + tag] and time.time() - t < wait:
            time.sleep(0.05)
        r = [g for g in got if g[:5] == b"\xF0\x7DJB" + tag]
        if r:
            return unpack7(r[0][5:-1])
    return None

if not a.no_enable:
    r = ask(b"N", b"N")
    print("JBN (c1_ok, nam_on):", struct.unpack("<2I", r[:8]) if r else "sem resposta", flush=True)
for k in range(a.watch // 5):
    time.sleep(5)
    r = ask(b"S", b"S")
    if not r:
        print(f"+{5 * (k + 1)}s: sem resposta", flush=True); continue
    st = struct.unpack("<8I", r[:32])
    print(f"+{5 * (k + 1)}s: blocos {st[0]}  c1 atrasado {st[3]} (maior espera {st[4]} us)  NAM {st[6]} us (max {st[7]})", flush=True)
inp.close(); out.close()
