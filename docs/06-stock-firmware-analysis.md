# 06 – Stock firmware analysis (V20)

## Method

1. `bbpkg.py unpack` → `analysis/v20_pkg/app.bin` (copied to `analysis/disasm/app_v20.bin`).
2. The binary is wrapped in an object (`.incbin`), linked at `0x02000120` (offset 0 of `app.bin` is mapped there by
   the SPL — same as Felucca's `app.ld`) (`analysis/disasm/link.ld`) and disassembled with JieLi's own `objdump`
   (`scripts/pi32cc.sh common/bin/objdump -d`) → `analysis/disasm/app_v20.dis` (~245k lines). Same method as
   FM-1-RE; JieLi's objdump is the reference (Ghidra's pi32v2 SLEIGH has unresolved instructions).
3. `scripts/xr` (`tools/xref.py`) finds who loads an address, lists functions and shows referenced strings.
4. Peripheral names: the SDK's official map (`WL82.h`), extracted to `analysis/notes/wl82_sfr_map.txt`
   (`lsfr_base = 0x10000`, `hsfr_base = 0x40000`, `map_adr(g, a) = (64 g + a) * 4`).

Strings live in a merged pool (the compiler loads base + offset), so searching for a string's exact address rarely
works. The productive path is to start from the **peripheral registers**.

## App memory map

| Region | Address | Size |
|---|---|---|
| Code/rodata (XIP) | `0x02000120` – `0x020A0280` | 640 KB |
| `.data` (LMA in flash) | `0x02091720` → RAM `0x01C00000` | `0xEB40` (59 KB) |
| `.bss` | `0x01C0EB40` | `0x1A5E0` (108 KB) |
| Stacks | `sp = 0x01C1CC3C`, `ssp = 0x01C1DC3C` | — |

## Peripherals accessed (reference count)

| Peripheral | Base | Refs | Likely use |
|---|---|---|---|
| `JL_ANA` | `0x11900` | 34 | analog config (audio ADC/DAC, LDOs) |
| `JL_SFC` | `0x40200` | 27 | flash |
| `JL_AUDIO` | `0x12F00` | 17 | internal codec (ADC/DAC) — not the audio path, see below |
| `JL_POWER` | `0x10000` | 16 | clock/power |
| `JL_P33` | `0x13E00` | 14 | low-power domain (battery/charging?) |
| `JL_WL` | `0x14000` | 11 | Wi-Fi/BT (dropped) |
| `JL_SPI1` | `0x11D00` | 5 | **display** |
| `JL_ALNK0/1` | `0x12E00` / `0x15100` | 3 / 1 | I²S (external codec) |
| `JL_SRC`, `JL_EQ` | `0x14300` / `0x1E000` | 3 / 1 | hardware sample-rate converter and EQ |
| `JL_PSRAM` | `0x40500` | 2 | PSRAM (presence to confirm) |
| `JL_ADC` | `0x13100` | 4 | SARADC (battery, pots) |

## Display — **solved**

**ST7789** controller (MIPI-DCS commands), 4-wire SPI, write-only.

| Signal | Pin | Source |
|---|---|---|
| SCK | **PB6** | SDK SPI1 "port A" (`IOMAP_CON1.bit4 = 0`) |
| MOSI | **PB7** | SPI1 "port A" |
| D/C | **PB5** | written directly to `PORTB_OUT` (`0x50040`, bit 5) |
| CS | **PB1** | `PORTB_OUT` bit 1 |
| RESET or backlight | PC7 (gpio 39)? | driven low before init — to confirm |

- `JL_SPI1->CON = 0x4020`, `BAUD = 1` (SCK = LSB/2), data sent by DMA (`ADR`/`CNT`), wait on `CON` bit 15.
- **Visible area 240×240 = rows 0..239** of a 240×320 controller's RAM (confirmed on the pedal: drawing from row 40
  cut the image at the bottom and the top showed leftovers of the stock UI). The `RASET 40..279` in the init table is
  only an initial window. The clear-screen routine (`0x2028FF0`) sweeps the whole 320×240 and then sends
  `DISPON (0x29)`.
- **RGB565** color (`COLMOD 0x55`), `MADCTL 0x00`, inversion on.

Init table at `0x0205CE60` (23 entries of 18 bytes `[cmd][len][16 data]`, read by `0x0202A4BE`; entries 1 and 3 are
delays, time in ms in byte 1):

```
01 SWRESET          ; 120 ms
11 SLPOUT           ; 120 ms
2A CASET  00 00 00 EF
2B RASET  00 28 01 17
B2 PORCTRL 0C 0C 0C 00 33
20 INVOFF
B7 56   BB 18   C0 2C   C2 01   C3 1F   C4 20   C6 0F
D0 A6 A1
E0 D0 0D 14 0B 0B 07 3A 44 50 08 13 13 2D 32
E1 D0 0D 14 0B 0B 07 3A 44 50 08 13 13 2D 32
36 00   3A 55   E7 00   51 FF
21 INVON
```

Functions: init `0x0202A4BE` (GPIO + SPI1 + table), clear/turn on `0x02028FF0`, window + blit `0x0201E3B2`.

## Audio — **external codec over I²S (ALNK0)** (revised 2026-10-22)

The earlier conclusion ("internal codec") was wrong: the histogram missed `r5 = 0x12E00`, printed in hex. The V20
audio init (`0x02007490`, called with `r14 = 0x0205B9B8`, a constant pool) follows Felucca's FM-1 init
(`hal/fm1_audio.h`) step by step, with other pins:

| Signal | Pin | Source |
|---|---|---|
| MCLK | **PA4** | pin table `r14+1899` = `0x0205C123`: `PA4 PA5 PA6 PA7 PA8 PA9 PA10` |
| SCLK | **PA5** | |
| LRCK | **PA6** | |
| DAT0 → DAC (channel 0, TX) | **PA7** | channel table `r14+4164` = `0x0205C9FC` (16 B/channel: `+4` enable, `+8` 1 = RX, `+12` buffer) |
| DAT1 ← ADC (channel 1, RX) | **PA8** | |
| Codec enable | **PA9** | 0 at start, 1 once the link runs (like the FM-1's PC6) |

- `IOMAP_CON5` bits 7:6 = `01` (the FM-1 uses `00`); `CON3 |= 3`; `CLK_CON2 &= ~0xF00`.
- Rate **44118 Hz** (`0x01C25758 = 44118`). **Input and output: 24-bit RIGHT-justified** (output measured with an
  audio interface, `tools/jb_measure.py`: only RJ24 gives the right tone; the `<< 8` in v0.4–v0.7 gave scrambled
  noise). Guitar on channel **L**. Input: int32 (bits 23..0, bit 23 = sign, top byte 0 — measured with
  `tools/jb_audio.py`), L/R interleaved; 1 KB buffers per channel, `LEN = 128` words per half in V20.
- `CON1`: per channel `ch`, bit `4ch` enable, bit `4ch+2` (always), bit `4ch+3` = RX. `CON2`: bit `4+ch` = half
  done, writing bit `ch` clears. `CON0` bit 15 **does not toggle** in this use (always 0): the half is tracked in
  software.
- 440 Hz −12 dBFS tone measured at the output (v0.7.2): **THD 0.12 %**, residuals < −50 dB.
- No I²C: the codec is configured by pins (model not identified yet; a board photo would confirm). The internal
  `JL_AUDIO` block is touched by the SDK driver but is not the audio path.

Input noise floor measured at rest: **L −91 dBFS rms, R −77 dBFS rms**.
Juice-Box's implementation: `fw/src/juicebox.c` `audio_start`, halves of 256 words (2.9 ms), MASTER = output gain.

## Panel controls — **mapped on the pedal** (Juice-Box v0.3, 2026-10-22)

Configuration read from V20 (`0x020061C0`) and confirmed by pressing each control (`tools/jb_inputs.py`, log in
`analysis/logs/inputs_mapping.log`).

| Control | Input | Reading |
|---|---|---|
| HOME | **PA1** | digital, pull-up, 0 = pressed |
| FX | **PA0** | digital |
| AMP | **PH9** | digital |
| MOD | **PH8** | digital |
| DLY | **PC10** (SARADC ch 9) | resistor ladder: ~706 (rest 1023) |
| REV | **PC10** | ~787 |
| CAB | **PC10** | ~616 |
| Left footswitch (▼) | **PA3** | digital |
| Right footswitch (▲) | **PA2** | digital (both together = tuner on V20) |
| MASTER (left knob) | **PA10** (SARADC ch 2) | potentiometer 0..1023 |
| PARA (right knob), turn | **PC1 / PC0** | quadrature encoder: 4 transitions per detent; **counter-clockwise = PC1 leads, clockwise = PC0 leads** |
| PARA, click | **PH6** | digital |
| Battery | **PC9** (SARADC ch 8) | half the battery voltage (v0.17; see 07) |

Suggested thresholds for the PC10 ladder: < 661 = CAB, < 746 = DLY, < 905 = REV.

## Next targets

- [x] Audio: I²S/external codec mapped; passthrough since v0.4
- [x] Footswitches, buttons and knobs
- [ ] PC7 and backlight; LEDs
- [ ] Charger status; PHONES output amplifier enable (if any)
