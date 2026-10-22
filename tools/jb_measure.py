#!/usr/bin/env python3
"""Measure the Juice-Box output through an audio interface on the Mac.

Sets the pedal over USB (SysEx JBC: DAC word format, 440 Hz test tone, output gain), records the
interface input and reports per setting: level, the strongest frequency, THD (harmonics 2..10 of
the strongest), and the noise floor. Each take is also saved as WAV under analysis/logs/measure/.

  jb_measure.py --list                                  audio input devices
  jb_measure.py --device "STUDIO S+" [--channel 0] [--fmts 0,1,2,3,4] [--gains 0.25] [--tone 1]
  jb_measure.py --device "STUDIO S+" --tone 0 --gains master --seconds 5    (record the guitar as played)
"""
import argparse
import math
import os
import sys
import time
import wave

import numpy as np

sys.path.insert(0, os.path.dirname(__file__))
from bb_install import PORT_RE   # noqa: E402

FMT = ["<<8 LJ24", "<<7", "<<9", "<<16", "RJ24"]


def send_jbc(fmt, tone, gain):
    import mido
    names = [n for n in mido.get_output_names() if PORT_RE.search(n)]
    if not names:
        raise SystemExit("no Juice-Box MIDI port")
    gq = 0x3FFF if gain is None else max(0, min(0x3FFE, int(round(gain * 16384))))
    with mido.open_output(names[0]) as out:
        for _ in range(2):                         # (a frame can be dropped while another is pending)
            out.send(mido.Message("sysex", data=bytes([0x7D, ord("J"), ord("B"), ord("C"), fmt, tone,
                                                       gq >> 7 & 0x7F, gq & 0x7F])))
            time.sleep(0.05)


def record(device, seconds, sr, channels):
    import sounddevice as sd
    x = sd.rec(int(seconds * sr), samplerate=sr, channels=channels, device=device, dtype="float32")
    sd.wait()
    return x


def analyse(x, sr):
    x = x - np.mean(x)
    n = len(x)
    rms = math.sqrt(float(np.mean(x ** 2))) + 1e-12
    pk = float(np.max(np.abs(x))) + 1e-12
    win = np.hanning(n)
    spec = np.abs(np.fft.rfft(x * win))
    freqs = np.fft.rfftfreq(n, 1 / sr)
    spec[freqs < 30] = 0
    k0 = int(np.argmax(spec))
    f0 = float(freqs[k0])
    p = spec ** 2

    def band(f, w=6):
        k = int(round(f * n / sr))
        return float(np.sum(p[max(k - w, 0):k + w + 1]))

    fund = band(f0)
    harm = sum(band(f0 * h) for h in range(2, 11) if f0 * h < sr / 2 - 100)
    total = float(np.sum(p))
    thd = math.sqrt(harm / fund) if fund else float("nan")
    thdn = math.sqrt(max(total - fund, 0) / fund) if fund else float("nan")
    return dict(rms_db=20 * math.log10(rms), peak_db=20 * math.log10(pk), f0=f0, thd=thd, thdn=thdn)


def save_wav(path, x, sr):
    y = np.clip(x, -1, 1)
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(sr)
        w.writeframes((y * 32767).astype("<i2").tobytes())


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--device")
    ap.add_argument("--channel", type=int, default=0, help="interface input channel (0 = first)")
    ap.add_argument("--fmts", default="0")
    ap.add_argument("--gains", default="0.25", help="comma list of linear gains, or 'master'")
    ap.add_argument("--tone", type=int, default=1)
    ap.add_argument("--seconds", type=float, default=1.0)
    ap.add_argument("--sr", type=int, default=48000)
    a = ap.parse_args()
    import sounddevice as sd
    if a.list:
        print(sd.query_devices())
        return
    dev = next((i for i, d in enumerate(sd.query_devices()) if a.device.lower() in d["name"].lower()
                and d["max_input_channels"] > 0), None)
    if dev is None:
        raise SystemExit(f"no input device matching {a.device!r}")
    nch = sd.query_devices(dev)["max_input_channels"]
    os.makedirs("analysis/logs/measure", exist_ok=True)
    gains = [None] if a.gains == "master" else [float(g) for g in a.gains.split(",")]
    print(f"device {sd.query_devices(dev)['name']}, channel {a.channel}, {a.sr} Hz")
    print(" fmt          gain    rms dBFS  peak dBFS   f0 Hz      THD     THD+N")
    for fmt in [int(f) for f in a.fmts.split(",")]:
        for g in gains:
            send_jbc(fmt, a.tone, g)
            time.sleep(1.0)
            x = record(dev, a.seconds, a.sr, nch)[:, a.channel]
            r = analyse(x, a.sr)
            gname = "MASTER" if g is None else f"{g:.3f}"
            print(f" {fmt} {FMT[fmt]:<9} {gname:>7} {r['rms_db']:9.1f} {r['peak_db']:9.1f} {r['f0']:8.1f} "
                  f"{r['thd'] * 100:7.2f} % {r['thdn'] * 100:7.2f} %")
            save_wav(f"analysis/logs/measure/fmt{fmt}_g{gname}_tone{a.tone}.wav", x, a.sr)


if __name__ == "__main__":
    main()
