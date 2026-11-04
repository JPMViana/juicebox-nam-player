#!/usr/bin/env python3
"""Capture 48 NAM blocks on the pedal (JBW) and compare them with the same model on the host.

Arms the capture; the pedal starts at the first loud block (> -18 dBFS: play a chord), resets the model there
(as after boot) and records the raw input and core 1's raw output. The host then runs the same input through
nam/test/a2l_cap_host (mixed and float) and prints where the pedal's output differs.
  jb_capture.py OUTDIR [--weights build/namtest/a2lite.f32]
"""
import argparse, os, struct, subprocess, sys, time
sys.path.insert(0, os.path.dirname(__file__))
from bb_install import unpack7
import mido

ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
ap.add_argument("outdir"); ap.add_argument("--weights", default="build/namtest/a2lite.f32")
a = ap.parse_args()
os.makedirs(a.outdir, exist_ok=True)
name = [x for x in mido.get_input_names() if "armaria" in x or "Juice" in x][0]
got = []
inp = mido.open_input(name, callback=lambda m: got.append(bytes(m.bytes()))); out = mido.open_output(name)
time.sleep(0.4)

def ask_w(arm):
    for _ in range(4):
        got.clear(); out.send(mido.Message("sysex", data=b"\x7DJBW" + bytes([arm]))); t = time.time()
        while time.time() - t < 2:
            r = [g for g in got if g[:5] == b"\xF0\x7DJBW"]
            if r:
                return struct.unpack("<6I", unpack7(r[0][5:-1])[:24])
            time.sleep(0.02)
    raise SystemExit("no answer to JBW (firmware >= BlackBox_970?)")

def enc(v): return bytes((v >> (7 * i)) & 0x7F for i in range(5))

def peek(addr, ln):
    data = b""
    while len(data) < ln:
        n = min(4096, ln - len(data))
        for _ in range(4):
            got.clear(); out.send(mido.Message("sysex", data=b"\x7DJBM" + enc(addr + len(data)) + enc(n))); t = time.time()
            while not [g for g in got if g[:6] == b"\xF0\x7DJBM\x7F"] and time.time() - t < 4:
                time.sleep(0.02)
            ch = {g[5]: unpack7(g[6:-1]) for g in got if g[:5] == b"\xF0\x7DJBM" and g[5] != 0x7F}
            part = b"".join(ch[i][:min(48, n - 48 * i)] for i in sorted(ch))
            if len(part) == n and sorted(ch) == list(range(len(ch))):
                break
        else:
            raise SystemExit(f"leitura falhou em {addr + len(data):#x}")
        data += part
    return data

st = ask_w(1)
print("captura armada: toque um acorde forte na guitarra...", flush=True)
t0 = time.time()
while st[0] != 3:
    if time.time() - t0 > 90:
        raise SystemExit(f"nada capturado em 90 s (estado {st[0]}, blocos {st[1]})")
    time.sleep(0.3)
    st = ask_w(0)
state, nblk, a_in, a_out, gain, cap = st
print(f"capturados {nblk} blocos; lendo {2 * cap * 512} bytes...", flush=True)
raw_in = struct.unpack(f"<{cap * 128}i", peek(a_in, cap * 512))
raw_out = struct.unpack(f"<{cap * 128}i", peek(a_out, cap * 512))
inp.close(); out.close()
fin = [x / 8388608.0 for x in raw_in]
open(os.path.join(a.outdir, "in.f32"), "wb").write(struct.pack(f"<{len(fin)}f", *fin))
dev = [float("nan") if x == 0x7FFFFFFF else x / 8388608.0 for x in raw_out]
open(os.path.join(a.outdir, "dev.f32"), "wb").write(struct.pack(f"<{len(dev)}f", *dev))
host = os.path.join(a.outdir, "a2l_cap_host")
subprocess.run(["cc", "-O2", "-o", host, "nam/test/a2l_cap_host.c", "nam/a2lx.c", "nam/a2lq.c", "nam/a2lite.c", "-lm"], check=True)
subprocess.run([host, a.weights, os.path.join(a.outdir, "in.f32"), os.path.join(a.outdir, "mix.f32"),
                os.path.join(a.outdir, "flt.f32")], check=True)
rd = lambda f: struct.unpack(f"<{os.path.getsize(f) // 4}f", open(f, "rb").read())
mix, flt = rd(os.path.join(a.outdir, "mix.f32")), rd(os.path.join(a.outdir, "flt.f32"))
pk_in = max(abs(x) for x in fin)
print(f"entrada: pico {pk_in:.3f} FS; ganho MASTER {gain / 65536:.3f}")
print(" bloco  entrada_pico  host_pico  pedal_pico  maior_dif  (pedal bloco k = host bloco k-1)")
bad = 0
for k in range(1, cap):
    d = dev[128 * k:128 * k + 128]
    h = mix[128 * (k - 1):128 * k]
    ip = max(abs(x) for x in fin[128 * (k - 1):128 * k])
    if any(x != x for x in d):
        print(f"{k:5d}  {ip:11.3f}  {max(abs(x) for x in h):9.4f}     (no output)")
        bad += 1
        continue
    md = max(abs(x - y) for x, y in zip(d, h))
    if md > 1e-3:
        bad += 1
    print(f"{k:5d}  {ip:11.3f}  {max(abs(x) for x in h):9.4f}  {max(abs(x) for x in d):10.4f}  {md:9.2e}{'  <<<' if md > 1e-3 else ''}")
print(f"blocos diferentes do host: {bad} de {cap - 1}")
