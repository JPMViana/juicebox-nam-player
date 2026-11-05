# 08 – Installer (Go) and distribution

State on 2026-11-04. Code in `installer/` (Go module `juicebox/installer`). For end users see the
[user guide](user-guide.md).

## What it is

- **Desktop app** (Wails v2, `installer/main.go` + `app.go` + `frontend/dist/`, plain HTML/CSS/JS, no bundler):
  detects the pedal, installs/updates Juice-Box, manages captures and banks, goes back to the official firmware.
  Bilingual: **English default** + Portuguese (asks on first run; strings in `frontend/dist/i18n.js`).
- **`jbx` CLI** (`installer/cmd/jbx`): the same functions from the command line (`ports`, `info`,
  `install RELEASE.json`, `install-fwsc`, `revert`, `captures`, `put`, `select`, `wipe`, `banks`;
  `--log FILE` writes diagnostics).

| Package | Role | Validation |
|---|---|---|
| `internal/jlpkg` | port of `tools/bbpkg.py` (UFW, ENC/SFC, flash) | packages **byte-identical** to Python (V20, Juice-Box, bump-cfg) |
| `internal/fwbuild` | builds the `.fwsc` from the release + V20 | identical to `fw/build.py` |
| `internal/updater` | port of `tools/bb_install.py` (handshake, steps 1 and 2) | tests with a simulated pedal |
| `internal/nam` | `.nam` → slot (port of `nam2c.py`) | identical to Python |
| `internal/store` | captures and banks (`d e w c s x r t u`) | tested on the pedal |
| `internal/stock` | downloads and checks V20 (cached in the user cache dir, `Juice-Box/`) | SHA-256 |
| `internal/midiio` | MIDI (gomidi v2 + rtmidi, cgo) | — |

## Distribution without vendor bytes

Juice-Box's `app.bin` contains the **JieLi rescue loader** (copied from V20). Therefore:

1. `fw/build.py --product BlackBox_NNN --release` writes `build/fw/release/juicebox-<version>-app.bin` with that
   region **zeroed** + `juicebox-<version>.json` (offset, length, CRC16 of the loader; SHA-256 of the app and of V20).
2. The release is copied to `installer/assets/release/` (committed; embedded with `go:embed`).
3. On the user's machine the app downloads V20 (`internal/stock`, currently from a third-party GitHub repository —
   **pending**: find an official M-VAVE download), checks its SHA-256, puts the loader back and builds the package.
   Always with `bump-cfg` (needed from the stock firmware, harmless otherwise).

No paid code signing (project decision): the user guide explains how to open the unsigned app.

## Releases

```bash
scripts/release.sh BlackBox_981      # build firmware release, copy into the installer, test, commit, tag
git push origin main v0.18.0         # GitHub Actions: macOS universal + Windows builds, SHA256SUMS, draft release
```

`.github/workflows/release.yml` checks that the tag matches the embedded firmware version, builds `Juice-Box.app`
(ad-hoc signed, zipped), `Juice-Box.exe` and `jbx` for both systems, and attaches them with `SHA256SUMS.txt` to a
**draft** release. Edit the notes (`.github/release-notes.md` is the template) and publish by hand.

## Tests on the pedal

- `jbx` (CLI): `info`, `captures`, `banks` ok; **`jbx install` (Juice-Box → Juice-Box) ok** — the loader only
  rewrote what changed (~30 reads).
- App (macOS): detection and listing ok; **Restore official → install Juice-Box from stock: works** (2026-11-04),
  but the window sits at 99 % for a while before finishing (below).
- Windows: not tested yet.

## Bug (fixed 2026-11-05): the window stays at 99 % while writing

**Cause found:** the loader reads the package head first and then **from its end backwards** (the Python client showed the
offset going 0–2 %, then 96 % down to 0 %, on the v0.17.1 install). The app's progress summed the bytes served, which
passed 100 % early and was clamped to 99 % for the rest of the write. The write itself was fine all along. The progress
now follows the backward sweep (`internal/updater` `write`, and `tools/bb_install.py`). To confirm with the next install
from the app.

Earlier notes:

Session of 2026-11-04 00:41 (log in the user cache dir, `Juice-Box/installer.log`):
- Reinstall Juice-Box 980 → 980 from the window. Step 1 ok (75 reads, `finished true`). The loader showed up as port
  **"USB Composite Device"**, identity `ota-?` (Juice-Box's port is "armaria-nam").
- Step 2 began at 00:42:14 and **never finished**; ~5 min later `sample` showed the app sending MIDI
  (`rtmidi_out_send_message`). That build did not log each request.
- The user closed the app: **the pedal rebooted on its own with the firmware written** (BlackBox_980 running).
  Conclusion: the loader finished and was waiting for an answer we did not recognize (likely the end request
  `0xF0000000` not read / a SysEx arriving differently), or it repeated a request whose answer it rejected.

Fixed in commit `4f0e2ae`: the 180 s limit only counts valid requests; the same request repeated 30× is an error; the
log records each request (`serve: req ...`) and unrecognized messages (`serve: not a request ...`).
On 2026-11-04 the user ran Restore + install with this build: it stalled at 99 % but completed.

**Next step:** read a log of a run that stalls (the file must contain `serve:` lines) and compare with the CLI
(`jbx install ... --log build/jbx-install.log`). Hypotheses: (1) the end-request SysEx arrives differently through the
bundled rtmidi (gomidi ships an old RtMidi; large messages may be fragmented / truncated); (2) something specific to
the GUI process (threads / CoreMIDI) — the CLI worked on the same path.

Guaranteed recovery if the pedal stays in update mode: open the app again and use **Finish installation**, or
`.venv/bin/python -I tools/bb_install.py build/fw/juicebox-BlackBox_980.fwsc --force --yes`.

## Commands

```bash
.venv/bin/python fw/build.py --product BlackBox_980 --release   # firmware + release
cd installer && go test ./... && ./scripts/build.sh               # tests + app (build/bin/Juice-Box.app)
go build -o ../build/jbx ./cmd/jbx                                # CLI
```

## Distribution to-do

- Fix the bug above; test on Windows.
- Own USB VID:PID (pid.codes).
- Official source for V20.
