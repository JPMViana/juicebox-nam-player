#!/usr/bin/env python3
"""JBK: read or set the bypass level (left footswitch, dry signal) on the pedal, Juice-Box v0.17.2+.

  tools/jb_bypass.py            print the current bypass gain
  tools/jb_bypass.py +3.5       set it in dB (0 dB = the input word straight to the output; -inf..+12 dB)
Not saved: it returns to 0 dB at the next boot. Use it to find the value that matches the guitar straight into the amp.
"""
import math, struct, sys, time
sys.path.insert(0, __import__("os").path.dirname(__file__))
from bb_install import unpack7
import mido

names = [x for x in mido.get_input_names() if "armaria" in x or "Juice" in x]
if not names:
    raise SystemExit("Juice-Box not found")
data = b"\x7DJBK"
if len(sys.argv) > 1:
    db = float(sys.argv[1])
    v = max(0, min(4 * 65536, round(65536 * 10 ** (db / 20))))
    data += bytes([v & 0x7F, v >> 7 & 0x7F, v >> 14 & 0x7F])
got = []
inp = mido.open_input(names[0], callback=lambda m: got.append(bytes(m.bytes())))
out = mido.open_output(names[0])
time.sleep(0.4)
out.send(mido.Message("sysex", data=data))
t0 = time.time()
while not [g for g in got if g[:5] == b"\xF0\x7DJBK"] and time.time() - t0 < 2:
    time.sleep(0.02)
inp.close(); out.close()
g = [g for g in got if g[:5] == b"\xF0\x7DJBK"]
if not g:
    raise SystemExit("no answer (is the pedal running Juice-Box v0.17.2 or later?)")
v = struct.unpack("<I", unpack7(g[0][5:-1])[:4])[0]
print(f"bypass gain: {v / 65536:.3f} ({20 * math.log10(v / 65536):+.1f} dB)" if v else "bypass gain: 0 (muted)")
