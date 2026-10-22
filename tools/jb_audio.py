#!/usr/bin/env python3
"""Capture raw audio input words from Juice-Box (v0.5+, SysEx F0 7D 'J' 'B' 'A' F7) and analyse them.

Each snapshot is one DMA half: 256 int32 words, L/R interleaved, as the I2S RX channel wrote them.

  jb_audio.py [--port NAME] [--snaps N] [--out PREFIX]

Prints, per channel, statistics under several readings of the word (24-bit left-justified, the
assumption of v0.4; raw 32-bit; 16-bit in the top half; 1-bit I2S offset) and the bit activity
per bit position, which shows where the real sample bits are. --out writes PREFIX.npy-like raw
words (.bin, little-endian int32) for further analysis.
"""
import argparse
import math
import struct
import sys
import time

sys.path.insert(0, __import__("os").path.dirname(__file__))
from bb_install import PORT_RE, unpack7   # noqa: E402


def capture(inp_name, out_name, snaps):
    import mido
    words = []
    for _ in range(snaps):
        got, done = {}, []

        def rx(msg):
            b = bytes(msg.bytes())
            if b[:5] != b"\xF0\x7DJBA":
                return
            if b[5] == 0x7F:
                done.append(1)
            else:
                got[b[5]] = unpack7(b[6:-1])[:32]

        inp = mido.open_input(inp_name, callback=rx)
        out = mido.open_output(out_name)
        for attempt in range(3):
            out.send(mido.Message("sysex", data=b"\x7DJBA"))
            t0 = time.time()
            while not done and time.time() - t0 < 2:
                time.sleep(0.01)
            if done:
                break
        inp.close()
        out.close()
        if not done or len(got) != 32:
            print(f"snapshot incomplete ({len(got)}/32 chunks)", file=sys.stderr)
            continue
        raw = b"".join(got[i] for i in range(32))
        words += list(struct.unpack("<256i", raw))
    return words


def stats(name, xs):
    n = len(xs)
    if not n:
        return
    mean = sum(xs) / n
    rms = math.sqrt(sum((x - mean) ** 2 for x in xs) / n)
    pk = max(abs(x - mean) for x in xs)
    return f"{name:<22} mean {mean:12.1f}  rms {rms:12.1f}  peak {pk:12.0f}"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port")
    ap.add_argument("--snaps", type=int, default=8)
    ap.add_argument("--out")
    a = ap.parse_args()
    import mido
    names = [n for n in mido.get_input_names() if (a.port.lower() in n.lower() if a.port else PORT_RE.search(n))]
    if not names:
        raise SystemExit("no Juice-Box MIDI port")
    outs = [n for n in mido.get_output_names() if n.startswith(names[0][:12])]
    w = capture(names[0], outs[0], a.snaps)
    if not w:
        raise SystemExit("no data")
    if a.out:
        open(a.out + ".bin", "wb").write(struct.pack(f"<{len(w)}i", *w))
    print(f"{len(w)} words ({len(w) // 2} frames)")
    print("first frames (hex L, R):")
    for i in range(0, 16, 2):
        print(f"   {w[i] & 0xFFFFFFFF:08x}  {w[i + 1] & 0xFFFFFFFF:08x}")
    for ch, label in ((0, "L"), (1, "R")):
        xs = w[ch::2]
        print(f"\n== {label}")
        print(stats("raw int32", xs))
        print(stats("24-bit LJ (>>8)", [x >> 8 for x in xs]))
        print(stats("16-bit top (>>16)", [x >> 16 for x in xs]))
        print(stats("I2S +1 bit (<<1 >>8)", [((x << 1) & 0xFFFFFFFF) - (1 << 32) * (((x << 1) >> 31) & 1) >> 8 for x in xs]))
        act = []
        for bit in range(31, -1, -1):
            ones = sum((x >> bit) & 1 for x in xs)
            act.append(ones / len(xs))
        print("bit activity 31..0 (fraction of 1s):")
        print("   " + " ".join(f"{v:.2f}" for v in act[:16]))
        print("   " + " ".join(f"{v:.2f}" for v in act[16:]))


if __name__ == "__main__":
    main()
