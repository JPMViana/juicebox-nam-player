#!/usr/bin/env python3
"""Capture raw audio input words from Juice-Box (v0.5+, SysEx F0 7D 'J' 'B' 'A' F7) and analyse them.

Each snapshot is one DMA half: 256 int32 words, L/R interleaved, as the I2S RX channel wrote them.

  jb_audio.py [--port NAME] [--snaps N] [--out PREFIX]
  jb_audio.py --stats                 halves, late polls, input peak, clipped samples, processing time
  jb_audio.py --trigger [--wait S]    arm the capture of the first half above -18 dBFS, then read it

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


def request(inp_name, out_name, cmd, tag, wait=2.0, tries=3):
    """send F0 7D 'JB'+cmd F7, collect the replies tagged F0 7D 'JB'+tag"""
    import mido
    got = []
    inp = mido.open_input(inp_name, callback=lambda m: got.append(bytes(m.bytes())))
    out = mido.open_output(out_name)
    for _ in range(tries):
        out.send(mido.Message("sysex", data=b"\x7DJB" + cmd))
        t0 = time.time()
        while not [g for g in got if g[:5] == b"\xF0\x7DJB" + tag] and time.time() - t0 < wait:
            time.sleep(0.01)
        if [g for g in got if g[:5] == b"\xF0\x7DJB" + tag]:
            break
    time.sleep(0.2)
    inp.close()
    out.close()
    return [g for g in got if g[:5] == b"\xF0\x7DJB" + tag]


def capture(inp_name, out_name, snaps, trigger=False, wait=2.0):
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
            out.send(mido.Message("sysex", data=b"\x7DJBT" if trigger else b"\x7DJBA"))
            t0 = time.time()
            while not done and time.time() - t0 < wait:
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
    ap.add_argument("--stats", action="store_true")
    ap.add_argument("--trigger", action="store_true")
    ap.add_argument("--wait", type=float, default=60.0)
    a = ap.parse_args()
    import mido
    names = [n for n in mido.get_input_names() if (a.port.lower() in n.lower() if a.port else PORT_RE.search(n))]
    if not names:
        raise SystemExit("no Juice-Box MIDI port")
    outs = [n for n in mido.get_output_names() if n.startswith(names[0][:12])]
    if a.stats:
        r = request(names[0], outs[0], b"S", b"S")
        if not r:
            raise SystemExit("no answer")
        st = struct.unpack("<8I", unpack7(r[0][5:-1])[:32])
        fs = 1 << 23
        print(f"halves {st[0]}  late-polls {st[1]}  input peak {st[2]} ({20 * math.log10(max(st[2], 1) / fs):.1f} dBFS)  "
              f"clipped in {st[3]} out {st[4]}  max processing {st[5]} us of 2900  NAM {st[6]} us (max {st[7]}) of 2902")
        return
    w = capture(names[0], outs[0], 1 if a.trigger else a.snaps, a.trigger, a.wait if a.trigger else 2.0)
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
