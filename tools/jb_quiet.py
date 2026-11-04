#!/usr/bin/env python3
"""Noise hunt (JBE): what the pedal leaves out.  jb_quiet.py MASK
  0 normal, 1 LEDs off, 2 no screen updates, 3 both, 4 LEDs on and static (no PWM), 6 static LEDs + no screen"""
import sys, time
import mido
m = int(sys.argv[1], 0)
n = [x for x in mido.get_input_names() if "armaria" in x or "Juice" in x][0]
got = []
inp = mido.open_input(n, callback=lambda x: got.append(bytes(x.bytes()))); out = mido.open_output(n)
time.sleep(0.4)
for _ in range(4):
    out.send(mido.Message("sysex", data=bytes([0x7D, ord("J"), ord("B"), ord("E"), m & 0x7F]))); t = time.time()
    while time.time() - t < 1.5 and not [g for g in got if g[:5] == b"\xF0\x7DJBE"]:
        time.sleep(0.02)
    r = [g for g in got if g[:5] == b"\xF0\x7DJBE"]
    if r:
        print("modo", r[0][5]); break
else:
    print("no answer")
inp.close(); out.close()
