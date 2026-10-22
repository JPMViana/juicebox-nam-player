#!/usr/bin/env python3
"""Log the Juice-Box input test stream (v0.3+: SysEx F0 7D 'J' 'B' 'I' ... F7) with timestamps.

  jb_inputs.py [--port NAME] [--seconds N] [--out FILE]

Each line: time, the pressed digital pins (pull-up inputs read low), the three SARADC values.
"""
import argparse
import sys
import time

sys.path.insert(0, __import__("os").path.dirname(__file__))
from bb_install import PORT_RE   # noqa: E402

DIN = ["PH6", "PA1", "PA0", "PH9", "PH8", "PA3", "PA2", "PC1", "PC0"]
AIN = ["PA10", "PC10", "PC9"]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port")
    ap.add_argument("--seconds", type=float, default=120)
    ap.add_argument("--out")
    a = ap.parse_args()
    import mido
    names = [n for n in mido.get_input_names() if (a.port.lower() in n.lower() if a.port else PORT_RE.search(n))]
    if not names:
        raise SystemExit(f"no Juice-Box MIDI port (inputs: {mido.get_input_names()})")
    out = open(a.out, "w") if a.out else None
    t0 = time.time()

    def rx(msg):
        b = bytes(msg.bytes())
        if b[:5] != b"\xF0\x7DJBI" or len(b) < 14:
            return
        din = b[5] | b[6] << 7
        adc = [b[7 + 2 * i] | b[8 + 2 * i] << 7 for i in range(3)]
        pressed = " ".join(n for i, n in enumerate(DIN) if din >> i & 1) or "-"
        line = f"{time.time() - t0:8.3f}  pressed: {pressed:<28} " + "  ".join(f"{n}={v:4d}" for n, v in zip(AIN, adc))
        print(line, flush=True)
        if out:
            out.write(line + "\n")
            out.flush()

    inp = mido.open_input(names[0], callback=rx)
    try:
        time.sleep(a.seconds)
    except KeyboardInterrupt:
        pass
    inp.close()


if __name__ == "__main__":
    main()
