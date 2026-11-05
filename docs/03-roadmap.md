# 03 – Roadmap

Strategy: **USB only, no programmers** — see [04-safety.md](04-safety.md).
Code base: [Felucca](https://github.com/hugelton/Felucca) (FM-1, GPL-3.0), ported to the BlackBox hardware.

## Firmware scope (decision 2026-10-22)

Lean firmware, built for fast response. **In:** NAM A2 (Lite) player, optional IR loader, USB (update + loading
captures/IRs), footswitches, our own screen UI. **Out:** Bluetooth, USB Audio, effects (mod/delay/reverb/comp), the
vendor's ANN engine, the official editor. Audio chain:
`input → gate → NAM A2 → [IR] → volume → output` (power amp mode = IR off).

## Phase 0 – Reconnaissance
- [x] Get the stock V20 firmware and unpack it (`scripts/unpack_stock.sh`)
- [x] Identify SoC, clock, flash map, chip-key
- [x] Estimate the cost of NAM A2 Lite/Full (`tools/nam_budget.py`)
- [ ] Board photos (optional, screwdriver only): codec, footswitch/LED/display pins

## Phase 1 – USB path proven on the BlackBox
- [x] JieLi toolchain (clang pi32v2) + AC79 SDK via Docker on macOS — Felucca builds completely
- [x] BlackBox update protocol = the FM-1's (same `F0 22 24 35 7F F7` command, same M-UPGRADE worker)
- [x] macOS update client: `tools/bb_install.py` (derived from Felucca, GPL-3.0)
- [x] Stock V20 written by our client (2026-10-22): `BlackBox_009` → `BlackBox_020`
- [x] Own packer `tools/bbpkg.py`: rebuilds V20's `flash.bin` **byte for byte** (`selftest`)
- [x] Modified app accepted and running (`BlackBox_020` → `BlackBox_019`): no signature, the pedal runs our code

## Phase 2 – Firmware disassembly
- [x] Display, audio (external I²S codec), panel controls mapped from V20 ([06](06-stock-firmware-analysis.md))
- [ ] LEDs, charger status, PHONES amplifier enable (if any)

## Phase 3 – NAM proof of concept
- [x] Static benchmark fp32 vs Q31 ([05](05-static-benchmark.md)) and measured on the pedal ([07](07-firmware-history.md))
- [x] A2-Lite in plain C, identical to NeuralAmpModelerCore (max diff 5e-7)
- [x] Loop test: input → A2-Lite → output on the pedal

## Phase 4 – Own firmware (Felucca port)
- [x] v0.1: USB + update + rescue + splash screen
- [x] Audio passthrough, footswitches, buttons, knobs, display
- [x] A2-Lite on both cores (float front on core 0, fixed-point back on core 1): stable with VDC14 = 1.50 V,
  2700 µs/block (v0.13.16)
- [x] NAM on at boot + real guitar test (v0.13.17–22): audio path fixed; "usable" sound
- [ ] Refine the sound: 48 kHz rate (or resampling), input gain, output gain/limiter
- [ ] Noise gate before NAM

## Phase 5 – User tools
- [x] `.nam` (A2) → A2-Lite weights converter with loudness normalization (`tools/nam2c.py`)
- [x] Load `.nam` captures over USB without rebuilding (`tools/jb_models.py`, installer)
- [x] Capture selection on the pedal (v0.15–v0.17): FX..CAB banks with A/B, PARA menu, live switching
- [x] On-screen UI (v0.17), English and Portuguese

## Phase 6 – Distribution and open source (decision 2026-11-03)
- [x] Repository cleaned for publication (vendor SDK objects and WAVs purged from history), GPL-3.0
- [x] Captures stored outside the app (v0.14.2): 8 slots in free VM (not in `USR`, which belongs to the official firmware)
- [x] Installer for non-technical users (Go, Windows and macOS): install Juice-Box, restore official V20, load
  captures. Restore + install from stock tested on macOS ([08](08-installer.md))
- [x] English documentation; README and user guide also in Portuguese
- [x] Release builds by GitHub Actions (macOS universal + Windows), SHA-256 checksums
- [ ] **First public test**: macOS and Windows testers
- [x] Window staying at 99 %: the progress summed the bytes served, but the loader reads backwards (fixed, to confirm in the app)
- [ ] Own VID:PID at pid.codes (today `1209:0001`, a test PID)
- [x] Own installer icon (the splash screen's juice jug, `installer/scripts/gen_icon.py`)
- [ ] Battery calibration, charger status pin, LED map ([07](07-firmware-history.md))
- [ ] Test the PHONES output

## Phase 7 – IR (after the first distributable version)
- [x] Matrix accelerator works on the BlackBox: exact, ~84 MMAC/s (v0.17.1, [07](07-firmware-history.md))
- [ ] Run the A2-Lite convolutions on the chip's matrix accelerator (`0x01EE0400`, as Onyx and the stock firmware do,
  [09](09-onyx-analysis.md)) to free CPU for an IR
- [ ] Load IRs from the installer. May run **without NAM at the same time** (IR-only mode), since NAM already uses
  ~2700 of 2902 µs per block
