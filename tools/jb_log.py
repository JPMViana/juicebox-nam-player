#!/usr/bin/env python3
"""Read the Juice-Box boot log over USB-MIDI (SysEx F0 7D 'J' 'B' 'L' F7).

Each boot writes one 32-byte record (fw/src/juicebox.c boot_rec): what the
previous boot reached before this one started, the reset reason and the
boot guard counters. Use it after a boot that went wrong (e.g. on battery).

  jb_log.py [--port NAME] [--last N]
"""
import argparse
import struct
import sys
import time

sys.path.insert(0, __import__("os").path.dirname(__file__))
from bb_install import PORT_RE, unpack7   # noqa: E402

STAGES = {0: "-", 2: "cstart", 3: "usb_start", 4: "flash id", 5: "log", 6: "lcd init", 7: "splash",
          8: "rescue check", 9: "main loop"}
P3_RST = ["power-on", "VDDIO low", "WDT", "VCM", "long press", "1.2 V", "soft (P33)"]


def p3_text(v):
    return ",".join(n for i, n in enumerate(P3_RST) if v >> i & 1) or "0"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port")
    ap.add_argument("--last", type=int, default=20)
    ap.add_argument("--raw", action="store_true", help="also print each record as hex")
    a = ap.parse_args()
    import mido
    names = [n for n in mido.get_input_names() if (a.port.lower() in n.lower() if a.port else PORT_RE.search(n))]
    if not names:
        raise SystemExit(f"no Juice-Box MIDI port (inputs: {mido.get_input_names()})")
    got, done = {}, []

    def rx(msg):
        b = bytes(msg.bytes())
        if b[:5] != b"\xF0\x7DJBL":
            return
        if b[5] == 0x7F:
            done.append(1)
            return
        got[b[5]] = unpack7(b[6:-1])[:32]

    inp = mido.open_input(names[0], callback=rx)
    out = mido.open_output([n for n in mido.get_output_names() if n.startswith(names[0][:12])][0])
    out.send(mido.Message("sysex", data=b"\x7DJBL"))
    t0 = time.time()
    while not done and time.time() - t0 < 5:
        time.sleep(0.05)
    inp.close()
    out.close()
    if not done:
        raise SystemExit("no answer (is it running Juice-Box v0.2 or later?)")
    print(f"{len(got)} records (oldest first; 'prev' = where the boot before this one got to)")
    print(" #  boots warm  prev stage       prev ms  prev loops  p3_rst          wdt  failed pend resc")
    for i in sorted(got)[-a.last:]:
        r = got[i]
        if a.raw:
            print(f"    raw {r.hex(' ')}")
        magic, boots, pst, pms, plo = struct.unpack_from("<IIIII", r, 0)
        p3, wdt, failed, pend, resc, warm, vers, _ = r[20:28]
        if magic != 0x474C424A:
            print(f"{i:2d}  (bad record {r.hex()})")
            continue
        print(f"{i:2d}  {boots:5d} {warm:4d}  {STAGES.get(pst, pst):<14} {pms:8d} {plo:10d}  "
              f"{p3_text(p3):<15} {wdt:#04x} {failed:6d} {pend:4d} {resc:4d}")


if __name__ == "__main__":
    main()
