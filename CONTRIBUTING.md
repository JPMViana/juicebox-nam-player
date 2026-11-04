# Contributing to Juice-Box

Thanks for helping! Issues and pull requests are welcome in English or Portuguese.

## Testing and bug reports

What helps most right now is testing on real pedals, on macOS and Windows.

1. Follow the [user guide](docs/user-guide.md).
2. If something goes wrong, [open a bug report](../../issues/new/choose) and attach the installer log:
   - macOS: `~/Library/Caches/Juice-Box/installer.log`
   - Windows: `%LOCALAPPDATA%\Juice-Box\installer.log`
3. Include your OS version, the app version, what the pedal's screen showed and the identity the app showed
   (e.g. `BlackBox_020`, `BlackBox_980`).

Reports that it **works** are useful too, especially on Windows and on pedals bought at different times.

## Code

- The firmware (`fw/`) builds with the JieLi pi32v2 toolchain in Docker. See the build section of the
  [README](README.md#building-developers-macos) and the version history in [docs/07](docs/07-firmware-history.md).
- The installer (`installer/`) is Go + Wails. Run `go test ./...` in `installer/` before sending changes.
- Keep [docs/04-safety.md](docs/04-safety.md) in mind: USB and the update path must come up first in the firmware,
  and nothing may write outside the app, capture and settings regions of the flash.
- Never commit vendor binaries (stock firmware, SDK objects, `.fwsc` packages) or third-party `.nam` captures.
- Code, comments and docs are in English. The README and the user guide also have Portuguese versions: please keep
  both in sync when you change one.

By contributing you agree that your contribution is licensed under GPL-3.0.
