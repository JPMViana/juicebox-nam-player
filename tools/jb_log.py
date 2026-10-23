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
          8: "rescue check", 9: "main loop", 40: "JB2 start", 41: "JB2 bloco", 42: "JB2 run c1",
          43: "JB2 front ok", 44: "JB2 wait ok", 45: "JB2 fim", 49: "JB3 c0 ok", 51: "JB3 f1+f0", 52: "JB3 f1+i0", 53: "JB3 i1+f0", 54: "JB3 i1+i0", 59: "JB4 c0 ok", 61: "JB4 c1xip+c0ram", 62: "JB4 c1ram+c0xip", 63: "JB4 c1xip+c0xip", 69: "JB5 c0 ok", 71: "JB5 i1+f0", 72: "JB5 f1+i0", 73: "JB5 f1+f0", 80: "NAM antes run", 81: "NAM run ok", 82: "NAM front ok", 83: "NAM wait ok", 90: "loop: topo", 91: "loop: poll ok", 92: "loop: debug ok", 93: "loop: ota ok", 94: "loop: antes cpu", 95: "loop: antes medidores", 96: "poll: usb ok", 97: "poll: antes usb", 98: "poll: audio ok", 99: "poll: led ok"}
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
        if magic == 0x52434A42:
            vec, core = boots & 0xFFFF, boots >> 16
            emu = struct.unpack_from("<I", r, 28)[0]
            print(f"{i:2d}  FALHA núcleo {core}: vetor {vec}  PC {pst:#010x}  RETS {pms:#010x}  SP {plo:#010x}  "
                  f"EMU {emu:#010x}  DBG {wdt:#04x}  #{p3}")
            continue
        if magic != 0x474C424A:
            print(f"{i:2d}  (bad record {r.hex()})")
            continue
        print(f"{i:2d}  {boots:5d} {warm:4d}  {STAGES.get(pst, pst):<14} {pms:8d} {plo:10d}  "
              f"{p3_text(p3):<15} {wdt:#04x} {failed:6d} {pend:4d} {resc:4d}")


if __name__ == "__main__":
    main()
