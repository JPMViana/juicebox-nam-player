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

- **New NAM engine**: the capture now runs on the chip's matrix accelerator (engine from the open-source
  [Onyx](https://github.com/28spawn/m-vave-pocket-amp-onyx) firmware by @28spawn), on one core instead of two. Same model, same
  sound; it frees a whole core for the cabinet IR that comes next
- **Bypass at the right level**: the left footswitch's dry signal no longer follows the MASTER knob (it was far quieter than the
  guitar plugged straight in)
- **Level meters in dBFS**: silence reads as empty, -60 dBFS to 0 dBFS
- **Battery** shows 100 % when full
- **Installer**: tells you when a newer version is released; the progress bar no longer sits at 99 %

Updating from v0.17: download this installer, open it and click **Update** — your captures and banks are kept.
Installers older than this one cannot tell you about updates: check the [releases page](https://github.com/JPMViana/juicebox-nam-player/releases) now and then.

Known issues: Windows is lightly tested; the PHONES output is untested.
