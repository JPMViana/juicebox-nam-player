#!/usr/bin/env python3
"""Juice-Box captures over USB, without rebuilding the firmware (fw v0.14+, the capture store).

  jb_models.py list                                  the 8 slots and the running capture
  jb_models.py put FILE.nam SLOT [--title T] [--select]   write a capture to slot 1..8
  jb_models.py select SLOT                           put slot 1..8 on the running bank position, saved
  jb_models.py wipe                                  erase every slot (before going back to the stock firmware)
  jb_models.py resume                                back to playing after put without --select

The NAM pauses (no sound) during put / wipe. select and resume reboot the pedal (~2 s); with no command for
20 s it reboots on its own. Reference client for the installer: the
protocol is in fw/src/juicebox.c ("capture store").
"""
import argparse
import os
import queue
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from bb_install import PORT_RE, pack7, unpack7   # noqa: E402
from nam2c import read_model, slot_image          # noqa: E402

SLOTS = 8
BUILTIN = 0x7F
CHUNK = 256
ERR = {1: "invalid request", 2: "flash read error", 3: "weights CRC mismatch", 4: "model rejected",
       5: "flash erase error", 6: "flash write error", 7: "invalid header"}


class Pedal:
    def __init__(self, port=None):
        import mido
        self.mido = mido
        ins = [n for n in mido.get_input_names() if (port.lower() in n.lower() if port else PORT_RE.search(n))]
        if not ins:
            raise SystemExit(f"Juice-Box not found (MIDI ports: {mido.get_input_names()})")
        self.q = queue.Queue()
        self.inp = mido.open_input(ins[0], callback=self._rx)
        self.out = mido.open_output([n for n in mido.get_output_names() if n.startswith(ins[0][:12])][0])
        time.sleep(0.4)                           # a SysEx sent right after opening the port is lost

    def _rx(self, msg):
        b = bytes(msg.bytes())
        if b[:4] == b"\xF0\x7DJB" and len(b) > 5 and 0x61 <= b[4] <= 0x7A:   # the store's replies: lowercase
            self.q.put(b[4:-1])

    def close(self):
        self.inp.close()
        self.out.close()

    def send(self, body):
        self.out.send(self.mido.Message("sysex", data=b"\x7DJB" + bytes(body)))

    def reply(self, cmd, timeout=5.0):
        t_end = time.time() + timeout
        while True:
            try:
                r = self.q.get(timeout=max(0.01, t_end - time.time()))
            except queue.Empty:
                raise SystemExit(f"no answer to '{chr(cmd)}' (is the pedal running Juice-Box v0.14 or later?)")
            if r[0] == cmd:
                return r[1:]

    def call(self, cmd, *args, timeout=5.0, tries=1):
        for k in range(tries):                    # only for idempotent commands (w: the same bytes again)
            while not self.q.empty():             # a late reply to an earlier try
                self.q.get_nowait()
            self.send(bytes([cmd, *args]))
            try:
                r = self.reply(cmd, timeout)
                break
            except SystemExit:
                if k == tries - 1:
                    raise
        if len(r) >= 2 and r[1]:
            raise SystemExit(f"'{chr(cmd)}': {ERR.get(r[1], r[1])} (code {r[1]})")
        return r

    def directory(self):
        self.send(b"d")
        slots, end = {}, None
        while end is None:
            r = self.reply(ord("d"))
            if r[0] == 0x7F:
                end = (r[1], r[2])
            else:
                slots[r[0]] = (r[1], unpack7(r[2:])[:64])
        return slots, end[0], end[1]

    def put(self, slot, image):
        self.call(ord("e"), slot, timeout=10)
        body, head = image[64:], image[:64]
        parts = [(64 + i, body[i:i + CHUNK]) for i in range(0, len(body), CHUNK)] + [(0, head)]   # the header last
        for k, (off, data) in enumerate(parts):
            self.call(ord("w"), slot, off & 0x7F, off >> 7 & 0x7F, off >> 14 & 0x7F, *pack7(data), timeout=2.0, tries=3)
            print(f"\r  writing {100 * (k + 1) // len(parts):3d} %", end="", flush=True)
        print()
        self.call(ord("c"), slot, timeout=10)


def show(slots, active):
    for i in range(SLOTS):
        valid, h = slots.get(i, (0, b""))
        mark = "▶" if active == i else " "
        if valid:
            _, _, _, nw, _, rate, norm, title = struct.unpack("<IHHIIIf24s", h[:48])
            print(f" {mark} {i + 1}: {title.rstrip(bytes(1)).decode('ascii', 'replace'):<23} {rate} Hz, gain {norm:.2f}")
        else:
            print(f" {mark} {i + 1}: (empty)")
    if active == BUILTIN:
        print("   (no capture playing: dry signal)")


def slot_arg(s, builtin_ok=False):
    v = int(s)
    if builtin_ok and v == 0:
        return BUILTIN
    if not 1 <= v <= SLOTS:
        raise SystemExit(f"slot {s}: use 1..{SLOTS}")
    return v - 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port")
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("list")
    p = sub.add_parser("put")
    p.add_argument("model")
    p.add_argument("slot")
    p.add_argument("--title")
    p.add_argument("--select", action="store_true")
    p = sub.add_parser("select")
    p.add_argument("slot")
    sub.add_parser("wipe")
    sub.add_parser("resume")
    a = ap.parse_args()

    image = None
    if a.cmd == "put":                            # convert before touching the pedal
        slot = slot_arg(a.slot)
        w, sr, norm, title, _ = read_model(a.model, a.title)
        image = slot_image(w, sr, norm, title)
        print(f"{os.path.basename(a.model)} -> slot {slot + 1} as \"{title}\"")
    ped = Pedal(a.port)
    try:
        if a.cmd == "list":
            slots, active, proto = ped.directory()
            show(slots, active)
        elif a.cmd == "put":
            ped.put(slot, image)
            if a.select:
                ped.call(ord("s"), slot, timeout=15)
                print(f"slot {slot + 1} set on the running bank position; the pedal restarts")
            else:
                ped.call(ord("r"))
                print("written; the pedal restarts (assign it from the bank menu, or with 'select')")
        elif a.cmd == "select":
            s = slot_arg(a.slot)
            ped.call(ord("s"), s, timeout=15)
            print("ok; the pedal restarts")
        elif a.cmd == "wipe":
            ped.call(ord("x"), timeout=30)
            ped.call(ord("r"))
            print("captures erased; the pedal restarts with none (dry signal)")
        elif a.cmd == "resume":
            ped.call(ord("r"))
    finally:
        ped.close()


if __name__ == "__main__":
    main()
