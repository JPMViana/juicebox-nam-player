#!/usr/bin/env python3
"""Capture 48 halves (128 samples) of the v0.18 NAM path while you play, and check them on the Mac.

Arms the capture (JBW); at the first loud half (> -18 dBFS: play a chord) the pedal resets Onyx's engine and records its input
(already scaled by the bank's IN) and the engine's raw output. Here the same weights and input run through Onyx's engine on the
Mac (software accelerator) and through the float reference; the levels the pedal sends to the DAC are printed too.
  jb_capture_onyx.py [OUTDIR]
"""
import math, os, struct, subprocess, sys, time
HERE = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
from bb_install import unpack7
import mido

outdir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build", "capture_onyx")
os.makedirs(outdir, exist_ok=True)
names = [x for x in mido.get_input_names() if "armaria" in x or "Juice" in x]
if not names:
    raise SystemExit("Juice-Box not found")
got = []
inp = mido.open_input(names[0], callback=lambda m: got.append(bytes(m.bytes()))); out = mido.open_output(names[0])
time.sleep(0.4)
enc = lambda v: bytes((v >> (7 * i)) & 0x7F for i in range(5))


def ask_w(arm):
    for _ in range(4):
        got.clear(); out.send(mido.Message("sysex", data=b"\x7DJBW" + bytes([arm]))); t = time.time()
        while time.time() - t < 2:
            r = [g for g in got if g[:5] == b"\xF0\x7DJBW"]
            if r:
                return struct.unpack("<6I", unpack7(r[0][5:-1])[:24])
            time.sleep(0.02)
    raise SystemExit("no answer to JBW")


def peek(addr, ln):
    data = b""
    while len(data) < ln:
        n = min(4032, ln - len(data))
        for _ in range(4):
            got.clear(); out.send(mido.Message("sysex", data=b"\x7DJBM" + enc(addr + len(data)) + enc(n))); t = time.time()
            while not [g for g in got if g[:6] == b"\xF0\x7DJBM\x7F"] and time.time() - t < 4:
                time.sleep(0.02)
            ch = {g[5]: unpack7(g[6:-1]) for g in got if g[:5] == b"\xF0\x7DJBM" and g[5] != 0x7F}
            part = b"".join(ch[i][:min(48, n - 48 * i)] for i in sorted(ch))
            if len(part) == n:
                break
        else:
            raise SystemExit(f"read failed at {addr + len(data):#x}")
        data += part
    return data


st = ask_w(1)
print("capture armed: play a strong chord...", flush=True)
t0 = time.time()
while st[0] != 3:
    if time.time() - t0 > 90:
        raise SystemExit(f"nothing captured in 90 s (state {st[0]})")
    time.sleep(0.3)
    st = ask_w(0)
state, nblk, a_in, a_out, gain, cap = st
info = struct.unpack("<I3fIIf", peek(0x01C7D300, 28))
if info[0] != 0x4943424A:
    raise SystemExit("no capture info (firmware older than BlackBox_994?)")
_, hs, nam_in, gf, waddr, g, trim = info
w = peek(waddr, 1871 * 4)
raw_in = peek(a_in, cap * 512)
raw_out = peek(a_out, cap * 512)
inp.close(); out.close()
open(os.path.join(outdir, "w.f32"), "wb").write(w)
open(os.path.join(outdir, "in.f32"), "wb").write(raw_in)
x = struct.unpack(f"<{cap * 128}f", raw_in)
dev = [float("nan") if v == 0x7FFFFFFF else struct.unpack("<f", struct.pack("<i", v))[0]
       for v in struct.unpack(f"<{cap * 128}i", raw_out)]

tool = os.path.join(ROOT, "build", "onyxtest", "wn_onyx_host")
os.makedirs(os.path.dirname(tool), exist_ok=True)
subprocess.run(["cc", "-O2", "-std=gnu11", "-I" + os.path.join(ROOT, "fw", "onyx", "include"), "-o", tool,
                os.path.join(ROOT, "nam", "test", "wn_onyx_host.c"), "-lm"], check=True, capture_output=True)
subprocess.run([tool, os.path.join(outdir, "w.f32"), os.path.join(outdir, "in.f32"), os.path.join(outdir, "host.f32"),
                os.path.join(outdir, "float.f32")], check=True, capture_output=True)
rd = lambda f: struct.unpack(f"<{os.path.getsize(f) // 4}f", open(os.path.join(outdir, f), "rb").read())
host, flt = rd("host.f32"), rd("float.f32")
db = lambda v: 20 * math.log10(v) if v > 0 else -999
print(f"scaling: head_scale {hs:.5g}, IN {nam_in:.3f} ({db(nam_in):+.1f} dB), MASTER gain {g / 65536:.3f}, NAM trim {trim:.3f}, "
      f"output factor {gf:.4g}")
pk_x = max(abs(v) for v in x)
print(f"input peak {pk_x:.3f} ({db(pk_x):.1f} dBFS of the model's scale)")
print(" half  in_pk   pedal_out_dBFS  host_out_dBFS  max|pedal-host| (raw)")
bad = 0
for k in range(cap):
    d, h = dev[128 * k:128 * k + 128], host[128 * k:128 * k + 128]
    ip = max(abs(v) for v in x[128 * k:128 * k + 128])
    if any(v != v for v in d):
        print(f"{k:5d}  {ip:5.3f}   (no output: core 1 late)"); bad += 1; continue
    md = max(abs(a - b) for a, b in zip(d, h))
    po = max(abs(v) for v in d) * gf / 8388607
    ho = max(abs(v) for v in h) * gf / 8388607
    flag = md > 1e-3 * max(max(abs(v) for v in h), 1e-6)
    bad += flag
    print(f"{k:5d}  {ip:5.3f}   {db(po):8.1f}        {db(ho):8.1f}       {md:9.3g}{'  <<<' if flag else ''}")
print(f"halves different from the Mac: {bad} of {cap}")
