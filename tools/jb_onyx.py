#!/usr/bin/env python3
"""JBO: run Onyx's NAM A2-Lite block engine on the pedal's matrix accelerator (Juice-Box v0.18.0-dev) and check it against the Mac.

  tools/jb_onyx.py [--blocks N]
1. stops the normal NAM (the dry signal keeps playing) and loads the active capture into the Onyx engine;
2. reads those weights back and times N blocks of 64 samples of a test input on the pedal;
3. runs the same weights and input on the Mac (build/onyxtest/wn_onyx_dev, built here if missing) and compares the outputs.
The flash is not written. Reboot the pedal afterwards (tools/jb_reboot.py) to get the normal NAM back.
"""
import argparse, os, struct, subprocess, sys, tempfile, time
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
from bb_install import unpack7
import mido

ap = argparse.ArgumentParser()
ap.add_argument("--blocks", type=int, default=200)
a = ap.parse_args()
names = [x for x in mido.get_input_names() if "armaria" in x or "Juice" in x]
if not names:
    raise SystemExit("Juice-Box not found")
got = []
inp = mido.open_input(names[0], callback=lambda m: got.append(bytes(m.bytes())))
out = mido.open_output(names[0])
time.sleep(0.4)


def ask(data, pred, timeout=5.0, count=1):
    got.clear()
    out.send(mido.Message("sysex", data=data))
    t0 = time.time()
    while len([g for g in got if pred(g)]) < count and time.time() - t0 < timeout:
        time.sleep(0.02)
    return [g for g in got if pred(g)]


def enc(v):
    return bytes((v >> (7 * i)) & 0x7F for i in range(5))


r = ask(b"\x7DJBO\x00", lambda g: g[:6] == b"\xF0\x7DJBO\x00")
if not r:
    raise SystemExit("no answer (is the pedal running Juice-Box v0.18.0-dev?)")
rc, ok, low, asm, size, broken, waddr = struct.unpack("<7I", unpack7(r[0][6:-1])[:28])
print(f"load: rc {rc:#x}, block engine ready {ok}, low SRAM self-test {low}, assembly stage {asm}, "
      f"model {size} B, accelerator broken {broken}")
if rc or not ok:
    raise SystemExit("the engine did not load (rc 0xfffffff1 = no capture slot active)")

wbytes = b""
for off in range(0, 1871 * 4, 4032):
    ln = min(4032, 1871 * 4 - off)
    rr = ask(b"\x7DJBM" + enc(waddr + off) + enc(ln), lambda g: g[:5] == b"\xF0\x7DJBM", count=(ln + 47) // 48 + 1)
    chunks = {g[5]: unpack7(g[6:-1]) for g in rr if g[5] != 0x7F}
    wbytes += b"".join(chunks[i][:min(48, ln - 48 * i)] for i in sorted(chunks))
if len(wbytes) != 1871 * 4:
    raise SystemExit(f"read {len(wbytes)} B of the weights, expected {1871 * 4}")

nb = a.blocks
rr = ask(b"\x7DJBO\x01" + bytes([nb & 0x7F, nb >> 7 & 0x7F]), lambda g: g[:6] in (b"\xF0\x7DJBO\x01", b"\xF0\x7DJBO\x02"),
         timeout=10 + nb * 0.01, count=5)
inp.close(); out.close()
st = [g for g in rr if g[5] == 1]
if not st:
    raise SystemExit("no answer to the run")
blocks, mn, mean, mx, broken, errs = struct.unpack("<6I", unpack7(st[0][6:-1])[:24])
print(f"pedal: {blocks} blocks of 64 samples: {mn / 24:.0f} / {mean / 24:.0f} / {mx / 24:.0f} us min / mean / max "
      f"(real time: 1451 us), accelerator broken {broken}, errors {errs}")
parts = {g[6]: unpack7(g[7:-1])[:64] for g in rr if g[5] == 2}
dev = struct.unpack("<64f", b"".join(parts[i] for i in sorted(parts))) if len(parts) == 4 else None

tool = os.path.join(ROOT, "build", "onyxtest", "wn_onyx_dev")
if not os.path.exists(tool):
    os.makedirs(os.path.dirname(tool), exist_ok=True)
    subprocess.run(["cc", "-O2", "-std=gnu11", "-I" + os.path.join(ROOT, "fw", "onyx", "include"), "-o", tool,
                    os.path.join(ROOT, "nam", "test", "wn_onyx_dev.c"), "-lm"], check=True)
with tempfile.TemporaryDirectory() as d:
    wf, of = os.path.join(d, "w.f32"), os.path.join(d, "y.f32")
    open(wf, "wb").write(wbytes)
    subprocess.run([tool, wf, str(nb), of], check=True)
    host = struct.unpack("<64f", open(of, "rb").read())
if dev is None:
    raise SystemExit("the pedal's output did not arrive")
diff = max(abs(x - y) for x, y in zip(dev, host))
peak = max(abs(y) for y in host)
print(f"output, last block: max |pedal - Mac| = {diff:.3g} (signal peak {peak:.3g}): "
      + ("IDENTICAL" if diff == 0 else "match" if diff < 1e-3 * max(peak, 1e-9) else "MISMATCH"))
