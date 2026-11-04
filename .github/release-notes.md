**Juice-Box** turns the M-VAVE ANN BlackBox into a NAM (Neural Amp Modeler) A2-Lite capture player.
Unofficial, not affiliated with M-VAVE / Sinco / JieLi. Installing firmware is at your own risk.

## Download

| System | File |
|---|---|
| macOS 11+ (Apple Silicon and Intel) | `Juice-Box-<version>-macOS.zip` |
| Windows 10 / 11 (64-bit) | `Juice-Box-<version>-Windows.exe` |
| Command line (advanced) | `jbx-<version>-<system>.zip` |

The apps are **not signed** (no paid certificates), so your system will warn you the first time.
Follow the [user guide](https://github.com/JPMViana/juicebox-nam-player/blob/main/docs/user-guide.md) to open them,
install Juice-Box and load your captures. Check your download against `SHA256SUMS.txt`.

You can go back to the official M-VAVE firmware at any time from the app (Firmware → Restore official firmware).

## What's new

First public test release.

- NAM A2-Lite captures in real time, split across the pedal's two cores
- 8 captures over USB, 6 banks (FX…CAB) with A/B and their own IN/OUT levels
- Footswitches: right = A/B, left = bypass (tap) / mute (hold)
- On-screen UI in English and Portuguese
- Installer app for macOS and Windows: install, captures, banks, restore the official firmware

Known issues: the progress bar can sit at 99 % for a while before finishing; Windows is untested; the PHONES output is untested.
