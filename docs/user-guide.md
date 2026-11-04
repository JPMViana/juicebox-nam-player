# Juice-Box user guide

[Português](user-guide.pt-BR.md)

Juice-Box is free, open-source firmware that turns the **M-VAVE ANN BlackBox** into a player for
**NAM (Neural Amp Modeler) A2** captures. You install it, and go back to the official firmware, with the Juice-Box
app on macOS or Windows, over the pedal's USB cable. You don't need any extra hardware.

> ⚠️ **Unofficial project.** It is not made or supported by M-VAVE. Installing firmware is at your own risk and may void
> your warranty. The official firmware can be restored at any time from the app.

## What you need

- An M-VAVE ANN BlackBox, charged.
- A **USB data cable**. Some USB-C cables only charge: if the app never finds the pedal, try another cable.
- A computer with **macOS 11 or newer** (Apple Silicon or Intel) or **Windows 10/11 64-bit**, with internet access.
  The app downloads the official firmware from the internet to build the install package.
- NAM **A2** captures (`.nam` files). See [Captures](#captures).

## 1. Download

Go to the [latest release](https://github.com/OWNER/REPO/releases/latest) and download:

| System | File |
|---|---|
| macOS | `Juice-Box-<version>-macOS.zip` |
| Windows | `Juice-Box-<version>-Windows.exe` |

**Optional: check the download.** Compare its SHA-256 with the line in `SHA256SUMS.txt` on the same page:

- macOS (Terminal): `shasum -a 256 ~/Downloads/Juice-Box-*-macOS.zip`
- Windows (PowerShell): `Get-FileHash $HOME\Downloads\Juice-Box-*-Windows.exe`

## 2. Open the app (it is not signed)

The app is not signed with a paid Apple or Microsoft certificate, so the first time your system will warn you that
it doesn't know the developer. That is expected. The source code is all in this repository.

### macOS

1. Double-click the `.zip` to extract `Juice-Box.app` and drag it to **Applications**.
2. Open it. If macOS says it "cannot be opened" or "cannot verify the developer":
   - **macOS 15 (Sequoia) and newer:** click **Done**, open **System Settings → Privacy & Security**, scroll down to
     *"Juice-Box was blocked…"* and click **Open Anyway**, then confirm with your password.
   - **macOS 14 and older:** right-click (or Control-click) the app → **Open** → **Open**.
3. If macOS says the app "is damaged", the download quarantine is blocking it. Run this in Terminal once:
   `xattr -dr com.apple.quarantine /Applications/Juice-Box.app`

You only need to do this once.

### Windows

1. Double-click `Juice-Box-<version>-Windows.exe`.
2. If **"Windows protected your PC"** (SmartScreen) appears, click **More info → Run anyway**.
3. The app uses Microsoft Edge WebView2, which comes with Windows 10/11. If it is missing, the app offers to install it.

## 3. Install Juice-Box

1. Turn the pedal on and connect it **directly** to the computer (no hub).
2. **Close other apps that use the pedal or MIDI**, such as the M-VAVE editor or a DAW. On Windows only one app at a
   time can open a MIDI device.
3. Open Juice-Box. The first time, it asks for the language (English or Português). You can change it later at the
   top right.
4. The app shows **"BlackBox with the official firmware"**. Click **Install Juice-Box**, read the warning and confirm.
5. Wait about a minute. The pedal restarts a few times. **Do not unplug it or close the window while it writes.**
   - The progress bar can **stay at 99 % for a while**. That is a known bug. Give it a couple of minutes. If the
     pedal restarts and shows Juice-Box, the installation is done and you can close the app.
6. When the pedal shows the Juice-Box screen, it's ready.

Your captures and banks are kept when you update Juice-Box later with a newer app.

## 4. Captures

Juice-Box plays **NAM A2** captures (the full "A2" file or "A2-Lite"). It runs the A2-Lite part of the model, the
part that fits in the pedal's chip. **Standard / older NAM captures (A1, "WaveNet standard", "lite", "feather")
don't work**, and the app tells you when a file is not A2.

Where to get them: [TONE3000](https://www.tone3000.com) (filter by the **A2** architecture), or train your own
with NAM.

Which ones to choose:
- **Into a power amp** (FX return of an amp, or a power amp + a real cabinet): use **amp-only** captures (no cab).
  Juice-Box was made for this.
- **Into headphones, an audio interface or a full-range speaker**: use captures **with the cabinet** ("amp + cab",
  "full rig"). Juice-Box doesn't load IRs yet.

In the app:
- **Captures** tab → **Add captures…**: pick up to 8 `.nam` files. Writing restarts the pedal (about 2 s).
  Volume is normalized like in the NAM plugin.
- **Banks** tab: each bank button on the pedal (**FX, AMP, MOD, DLY, REV, CAB**) holds two captures, **A** and **B**,
  with their own **IN** (input) and **OUT** (output) levels. Click **Save to pedal**.

## 5. Using the pedal

| Control | What it does |
|---|---|
| **FX AMP MOD DLY REV CAB** | choose bank 1–6 |
| **Right footswitch** | switch between capture A and B in the bank |
| **Left footswitch**, tap | effect on / off (off = dry signal) |
| **Left footswitch**, hold | mute (press again to unmute) |
| **MASTER** (left knob) | output volume |
| **PARA** (right knob), click | bank menu: bank, captures A / B, IN, OUT, language, save. Turn to move or change |
| **PARA**, hold 1 s | save (banks and the bank to start on). The pedal restarts quickly |
| **HOME** | information screen (version, captures, battery). Press again to go back |

## 6. Back to the official firmware

In the app: **Firmware** tab → **Restore official firmware…**. The app downloads the official M-VAVE V20 firmware,
checks it, erases Juice-Box's captures and installs V20. After that the pedal works with the M-VAVE editor again.

## Troubleshooting

| Problem | What to do |
|---|---|
| "Connect your BlackBox" never changes | Use another USB cable (a data cable), connect it directly without a hub, close the M-VAVE editor and DAWs, turn the pedal off and on. |
| macOS: "damaged" or "can't be opened" | See [Open the app](#2-open-the-app-it-is-not-signed). |
| Stuck at 99 % | Wait a couple of minutes. If the pedal shows Juice-Box, close the app. If not, see the next row. |
| "The pedal is in update mode" | An install was interrupted. Click **Finish installation**. In this mode the pedal is safe and waits for a package. |
| "Could not get the official firmware" | The app needs internet the first time (it keeps a copy afterwards). |
| Hiss or hum only while the USB cable is plugged in | That's a ground loop: the pedal is on the same computer as your audio interface. Unplug USB after you set things up, or run the pedal on battery. |
| A capture is "not a NAM A2 / A2-Lite capture" | Use A2 captures (see [Captures](#4-captures)). |
| A capture is "too loud for the pedal's math" | Very rare. Try another capture. |

## Reporting a problem

Please [open an issue](https://github.com/OWNER/REPO/issues/new/choose) and attach the app's log:

- macOS: `~/Library/Caches/Juice-Box/installer.log` (Finder → Go → Go to Folder…)
- Windows: `%LOCALAPPDATA%\Juice-Box\installer.log` (paste that into the Explorer address bar)

Tell us your system, the app version and what the pedal's screen showed.

## Known limitations

- The pedal runs at 44.1 kHz and captures are made at 48 kHz, so the tone may be slightly off from the plugin.
- No IR / cabinet simulation yet.
- The PHONES output hasn't been tested yet.
- The battery reading may show 97 % when the battery is full.
- Saving and writing captures restart the pedal (1–2 s).
