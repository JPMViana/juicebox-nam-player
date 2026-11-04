# 04 – USB-only strategy (no external programmer)

**Project decision (2026-10-22): no SPI programmer, SOIC-8 clip or dongle.** All development, flashing and
recovery happens over the pedal's own USB-C cable.

## Why this works

The BlackBox platform (JieLi AC791N, same `uboot.boot` SPL, same `ota.bin` update loader, same chip-key `0x980F`)
is the same as the M-VAVE FM-1 and SMK-37. On those devices the community already installs **complete, open-source
firmware over USB only**, with no extra hardware:

| Project | Device | Install | Back to stock |
|---|---|---|---|
| [Felucca](https://github.com/hugelton/Felucca) (GPL-3.0) | FM-1 | web installer (WebMIDI) | yes, from the installer or M-UPGRADE |
| [fm1-x0x](https://github.com/charlesvestal/fm1-x0x) | FM-1 | web installer | yes + automatic **safe mode** if the app keeps crashing |
| [Snoop37 / fm1-to-smk37](https://github.com/genkigenki/fm1-to-smk37) | SMK-37 | web installer | yes — **a port of FM-1 firmware to another AC791N board**, exactly our case |
| [annblackbox-custom-firmware-installer](https://github.com/entitymar/annblackbox-custom-firmware-installer) | **BlackBox** | modified M-UPGRADE | restores the stock V20 |

M-VAVE itself published [m-vave/fm1_open](https://github.com/m-vave/fm1_open) with C drivers and USB flashing.

### How USB flashing works (FM-1-RE, confirmed on hardware)

1. The **running app** receives the update command over USB-MIDI.
2. It downloads `ota.bin` (loader, ≤ 19 456 bytes, a multiple of 512) and checks **only a CRC-16** — no
   cryptographic signature.
3. It reboots into the loader (runs from RAM at `0x01C0A800`), which writes the new `app.bin` to flash and confirms
   with `0xE0000000` / `0xF0000000` + `"success\0"`.

### Confirmed on the BlackBox (2026-10-22)

- Official V20 written by `tools/bb_install.py` on macOS (009 → 020).
- A package built by `tools/bbpkg.py` with a modified app was accepted and ran (020 → 019).
- For packages whose `cfg` equals the installed one, use `bbpkg.py build --bump-cfg`.
- Back to stock: `bb_install.py firmware/stock/BlackBox_FACTORY_V20.fwsc` (or the installer's "Restore official firmware").

## The remaining risk (and how we remove it)

The only case with no way out over USB: **our app writes fine, passes the CRC, but hangs before USB starts.** Then
the pedal won't take an update and the ROM won't enter recovery mode (the flash "looks" valid).

Rules so that it never happens:

1. **USB and the update routine are the first thing our firmware starts**, before codec, NAM, display or
   footswitches. The USB/update code is Felucca's, proven in the field.
2. **Rescue mode:** boot counter + watchdog. If the app restarts several times in a row, the official JieLi update
   loader (kept in flash by Juice-Box) takes over and accepts a new package.
3. **Validate hardware before replacing the app**: test code first.
4. **Never touch** `uboot.boot`, `key_mac` (`0xFF000`), `BTIF` or factory calibration. Only `app.bin`
   (and, if needed, `ota.bin`).
5. Every tool has `--dry-run`, checks CRC/size/alignment and reads back.
6. While writing: pedal charged or powered, USB cable direct (no hub).

## Last resort without buying anything (not planned, only documented)

The JieLi ROM enters USB boot mode by itself when it **cannot read the flash**
([kagaimiq/jielie](https://kagaimiq.github.io/jielie/isp/usb/usb-key.html)). In theory, opening the pedal and
blocking flash reads for an instant at power-on (e.g. with tweezers) would put the chip in that mode, and the flash
could then be rewritten from the PC. **Not tested on the AC791N**; only for a real brick.
