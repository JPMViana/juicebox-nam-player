# 01 – Hardware and stock firmware

Primary source: `BlackBox_FACTORY_V20.fwsc` (700 436 bytes,
SHA-256 `c0fef191b860d3b20f77fc492a24986eef2d12df44bb5e38355684820e26cb08`),
extracted from the official updater by
[entitymar/annblackbox-custom-firmware-installer](https://github.com/entitymar/annblackbox-custom-firmware-installer).
Unpacked with `kagaimiq/jl-misctools` (`scripts/unpack_stock.sh`).

## Confirmed facts (from the binary)

### UFW container

```
chip name: "AC791N"     PID: "AC791N_STORY"     Flash size: 0x0FF000
chip-key (isd_config.ini): 0x980F          entry point: 0x02000120
```

| Flash offset | Entry | Size | Notes |
|---|---|---|---|
| `0x000A0` | `uboot.boot` (SPL) | 14 384 | vendor blob |
| `0x038D0` | `isd_config.ini` | 699 | boot config (clock, SDRAM, UART) |
| `0x04120` | `app.bin` | 655 712 | pi32v2 application (XIP at `0x02000000`) |
| `0xA4280` | `cfg_tool.bin` | 383 | parameters (contains `JL_AC79XX`, `JL_AC79XX_BLE`) |
| `0xA9000` | `VM` | 100 KB | key-value store (settings) |
| `0xC2000` | `BTIF` | 4 KB | Bluetooth data |
| `0xC3000` | `USR` | 256 KB | **user data – where the stock firmware keeps the AMP/CAB models loaded from the PC** |
| `0xFF000` | `key_mac` | 4 KB | per-device key/MAC — **never overwrite** |

The chip-key `0x980F` is the same as the M-VAVE FM-1 (same platform), so all the work in
[AL-255/FM-1-RE](https://github.com/AL-255/FM-1-RE) applies directly (Ghidra pi32v2, OTA protocol, boot map).

### `isd_config.ini` (decoded values, little-endian u32)

| Key | Value | Meaning |
|---|---|---|
| `OSC_FREQ` | 24 000 000 | 24 MHz crystal |
| `SYS_CLK` | **360 000 000** | CPU at 360 MHz (the FM-1 runs at 240 MHz) |
| `HSB_DIV` / `LSB_DIV` | 1 / 2 | buses |
| `UTBD` | 1 000 000 | **debug UART at 1 Mbaud** |
| `RESET` | `PB01_00_0` | long-reset pin |
| `ENABLE_SDRAM` | 0 | **external SDRAM apparently disabled** (internal SRAM only, ~578 KB?) — to confirm |

### Relevant strings in `app.bin`

- `BlackBox_020` – product/version identifier
- `AMP %d`, `PC AMP`, `CAB %d`, `PC CAB` – factory slots and slots loaded from the PC
- `mnt/sdfile/app/usr` – file system over the `USR` region
- `SYSTEM-*modified #define CPU_CORE_NUM 1*` – the system library was built for **1 core**; the AC791N is
  dual-core → **the second core may be idle**
- `sdram`, `sdram_powerup_ok` – SDRAM code present (may be just the SDK's)
- Update: `usb_update2.bin`, `ble_app_ota.bin`, `LOADER.BIN`, etc.

## USB (observed on the pedal, 2026-10-22)

| Item | Value |
|---|---|
| VID:PID | `4c4a:c755` (19530:51029) — **the same PID as the FM-1 in normal mode** |
| Manufacturer / product | `Jieli Technology` / `USB Composite Device` |
| MIDI port (macOS) | `USB Composite Device` |
| Identity (handshake) | `BlackBox_009` from the factory on this unit; `BlackBox_020` after writing V20 |

The identity number (`_009`, `_020`) is internal and does not necessarily match the package's commercial name.
We do not have the `_009` image: there is no way back to it (V20 is the official firmware we restore).

## Manufacturer's specifications

44.1 kHz / 24 bit · 20 "ANN" AMPs + third-party slots · 1024-sample mono IR ·
USB Audio (UAC) · Bluetooth (audio) · internal battery · USB-C 5 V.

**Note:** NAM is trained at 48 kHz. Either we switch the codec to 48 kHz, resample 44.1↔48 kHz (extra CPU), or
train/convert at 44.1 kHz.

## Open questions (need the hardware in hand)

- [ ] Board photos: SoC marking, flash chip (SOIC-8?), external codec or internal ADC/DAC, UART test points
- [ ] Is there SDRAM? (`ENABLE_SDRAM=0` suggests not)
- [ ] Analog input/output topology (level, impedance) — important for the power amp use
- [ ] Format of the third-party "ANN" file the official editor sends (capture over USB)
- [x] Is the AC791N's second core available? Yes — see [07](07-firmware-history.md)
- [x] Does the pi32v2 have a 1-cycle float MAC? No FMA; see [05](05-static-benchmark.md) and [07](07-firmware-history.md)
