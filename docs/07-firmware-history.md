# 07 – Juice-Box firmware (formerly armaria-nam): version history

Name: **Juice-Box**, by **João Viana** (until v0.1.2: armaria-nam). Identity in the update protocol: `BlackBox_9xx`
(the model stays `BlackBox` for compatibility with the official updater and `tools/bb_install.py`).

Newest first. "Package" is the identity the pedal reports (`BlackBox_NNN`).

## v0.17.1 (2026-11-05) — the matrix accelerator works on the BlackBox

`BlackBox_981`: command `JBD t` (`tools/jb_accel.py`) runs one job on the matrix × vector unit at `0x01EE0400`
([09](09-onyx-analysis.md)) and compares it with the CPU. Nothing else changes; the test only runs on request.

| Test | Shape | Result (3 runs each, NAM running on both cores) |
|---|---|---|
| 0 | 4 × 3 | exact, 0.4 µs |
| 1 | 64 × 20, 12-byte output records (an A2-Lite k = 6 layer) | exact, 16.7 µs |
| 2 | 64 × 47, 12-byte output records (k = 15) | exact, 35.9 µs |
| 3 | the stock's 9 × 17 job (vector / matrix swapped, stride `0x411000`) | exact, 2.1 µs |

**~12 ns per multiply-add (≈ 84 MMAC/s, ~2.9 cycles at 240 MHz)** with 64-bit accumulation, rounding and saturation as
documented by Onyx, running in parallel with the CPU. No extra clock or power setup was needed. The buffers were in C1RAM
(`0x01C68E00..`), so the unit reaches the high RAM too.

## v0.15 – v0.17 (2026-11-03/04) — on-screen UI, banks, bilingual

| Version | Package | What changed | Verified |
|---|---|---|---|
| v0.15.0 | `_978` | **Live capture switching**: the capture region becomes an unencrypted XIP window (like Felucca's `fl_plain_window_init`, from `0xA9000`); at boot the firmware compares XIP × driver reads (`ms_xip_ok`). Switching reads the weights straight from flash with core 1 running (idle): ~0.2 s of silence. Anti-aliased 4 bpp fonts with accents (`fonts_aa.h`). No built-in capture | `xip_ok = 1`; switching from the footswitches works |
| v0.15.0 | `_978` | Experiment `z`: restart core 1 at runtime. Variants 1 and 2 (write limits off): it does not answer within 20 ms; variant 0 (as at boot): does not answer **and** crashes the pedal (`DBG_MSG 0x800`) | saving is still a reboot (~1.5 s) |
| v0.16.0 | `_979` | **Banks**: FX, AMP, MOD, DLY, REV, CAB = 6 banks of 2 captures (A/B) with IN/OUT per bank (half-dB steps). Right footswitch = A↔B; left = effect on/off (tap) and mute (hold 0.6 s; one press unmutes). PARA: bank menu; hold = save. 64 B record (`"JBK1"`) at `0xBF000`. Commands `t`/`u` (table), `p` (P33/ports), `l` (LEDs) | controls ok by the user |
| v0.17.0 | `_980` | UI **in English (default) and Portuguese** (Language/Idioma menu, saved in the record). Battery from **PC9** (= half the voltage, 3.3 V reference assumed). Fix: before the first ADC read the PC10 ladder read 0 = "CAB pressed" → bank 6 at every boot | boots in bank 1; battery shows 97 % |

### Hardware findings
- The AC791N's **internal** charger is off (`P3_CHG_CON0..3 = 0`; P33 read checked with `0x06 = 0x0D`, `0x09 = 0x1D`):
  the BlackBox charges its battery with a **separate charger chip** (it has its own charge LED). It works with our firmware.
- The SARADC's internal PMU channels (VBG, VBAT/4, LDO5V/4) read 0 / a fixed value on this board: useless here.
- PC9 = 645 (on USB, charged for hours) → 4161 mV by the current formula; the pedal shows 97 %.

### To do
- **Calibrate the battery**: on USB for a long time the user expects 100 % (shows 97 %). Measure PC9 on battery only,
  charging and full, ideally with a multimeter, and tune `BAT_REF_MV` and the `MV[]` curve.
- **Charger status pin** (for a "charging" icon): HOME shows the `A B C H` inputs; compare battery × wall charger ×
  full. Today "+" = USB host connected.
- **LED map**: `BANK_LED[]` is a guess; map it with `l` (bit by bit, the screen shows `LED 0xNN`).
- Restart core 1 without rebooting (would allow saving without the ~1.5 s).
- **Headphone output (PHONES)**: never tested with Juice-Box. The firmware only feeds the external codec (I2S ALNK0,
  same signal on L and R) → main output. If the headphone amp has an enable pin (set by the stock firmware), it stays
  silent: listen on the pedal; if silent, find the pin in V20's `app.bin` (GPIOs set as outputs near the codec init,
  `0x02007490`) and turn it on.

## v0.14.0 – v0.14.2 (2026-11-03) — captures over USB, no rebuild

The pedal stores **8 captures** in flash and plays the chosen one at boot. The capture built in
(`fw/build.py --model`) stays as a fallback (slot "0"). Client: `tools/jb_models.py`, the reference for the installer.

```bash
.venv/bin/python -I tools/jb_models.py list
.venv/bin/python -I tools/jb_models.py put "my.nam" 1 --select     # write slot 1 and play it
.venv/bin/python -I tools/jb_models.py select 2                    # 0 = built-in capture
.venv/bin/python -I tools/jb_models.py wipe                        # erase everything (before going back to V20)
```

| Version | Package | What changed | Measured |
|---|---|---|---|
| v0.14.0 | `_975` | Capture store + SysEx commands `d e w c s x r` (lowercase). `nam2c.py --slot` builds the slot image and refuses weights outside core 1's fixed-point range | writing a slot: **0.5 s**; on resume, **restarting core 1 crashes the pedal** (write-limit error inside `fm1_cpu1_start`, `DBG_MSG 0x800`) |
| v0.14.1 | `_976` | Resume = **reboot** the pedal (boot loads the saved capture). Before the intended reboot the `bootguard` is cleared, otherwise two quick switches would lead to rescue | `select` → reboot → right capture; captures kept after a firmware update |
| v0.14.2 | `_977` | Fix: the 20 s idle limit compared `now` (from the top of the loop, older) with `ms_last_ms` unsigned → reboot right after the 1st command (reboot loop during the test). The client resends an unanswered `w` (up to 3×; rewriting the same bytes does not change the flash) | 4 switches in a row < 10 s after boot, 3 writes in a row, abandoned write → reboot in 20 s: all ok, no failure records |

### Map and format

| Region | Use |
|---|---|
| `0xA9000–0xB9000` | 8 slots × 8 KB (free start of the VM) |
| `0xBF000` | configuration: active slot (16 B records, append-only; erases the sector when full) |
| `0xC3000–` (`USR`) | **unused**: the official firmware's file system (user AMP/CAB), kept for going back to V20 |

Slot = 64 B header (`"JBM1"`, version 1, 1871 weights, CRC-32 of the weights, rate, loudness gain, 24 B title,
CRC-32 of the header) + float32 LE weights in `.nam` order. The host writes the weights and **the header last**; `c`
checks both CRCs. An incomplete slot shows as empty.

### Protocol (SysEx `F0 7D 'J' 'B' cmd … F7`, answers `… cmd slot rc`, rc 0 = ok)

| Cmd | Arguments | Effect |
|---|---|---|
| `d` | — | per slot `d slot valid pack7(header)`, then `d 7F active protocol_version` |
| `e` | slot | pauses NAM and erases the slot |
| `w` | slot, offset (3 × 7 bits), pack7(≤ 256 B) | writes; only to a slot erased in this session |
| `c` | slot | checks header and CRC → slot valid |
| `s` | slot (7F = built-in) | saves as the boot capture and **reboots** |
| `x` | — | erases all slots and the configuration |
| `r` | — | reboots (back to playing) |

Error codes: 1 invalid request, 2 read, 3 CRC, 4 model, 5 erase, 6 write, 7 header.
Paused with no commands for 20 s, the pedal reboots by itself.

### To do
- Restart core 1 without rebooting.
- The stock V20 was not tested with captures left in the VM: the installer runs `x` before going back to stock
  (leaves the region erased, like a fresh VM).

## v0.13.18 – v0.13.22 (2026-10-23) — usable sound: audio path fixed, user capture built in

First guitar test (v0.13.17, `BlackBox_965`): low volume, choppy sound and digital hiss. Three audio-path defects plus
a test-setup problem. NAM itself was right.

| Version | Package | What changed | Measured |
|---|---|---|---|
| v0.13.18 | `_966`–`_969` | (1) ALNK pending flags cleared **on detecting** the half: cleared at the end of the block, they erased the next one, and 14 % of halves were lost. (2) Core 1 starts the back of block n at the end of the call, and core 0 no longer waits for it. (3) Pot ADC **non-blocking**, one channel per 5 ms scan: the 3 blocking reads held audio for ~670 µs every 15 ms. (4) `text_at`/`status` call `poll()` per line. | 295 → **344.7 blocks/s**; late outputs ~40/s → **0**; NAM 2710 → **2455 µs/block**; longest stretch without poll 670 → 146 µs |
| v0.13.19 | `_970` | `JBW` + `tools/jb_capture.py` + `nam/test/a2l_cap_host.c`: 48 blocks of raw input and core 1 output, compared on the Mac with the same model from the same state (reset on the 1st loud block) | 125 jumps in 6144 input samples, **all in the first ~10 samples of a block** |
| v0.13.20 | `_971` | **DMA half resynchronized**: `CON0` bit 15 does not toggle, so the ready half was only counted in software. A half lost once (boot, model reset, and before 1 in 7) left `au_h` on the half in progress forever: a click every 2.9 ms, with or without NAM. Now the last word of each half read gets `0x5A5A5A5A` (RX words have a 0 top byte): the half in progress still has the mark, and `au_h` is corrected. | 0 jumps; pedal output **equal to the host** (diff 1e-7); 1 resync, at boot |
| v0.13.21 | `_972` | `JBE` / `tools/jb_quiet.py`: turns off LEDs, PWM or display to hunt noise | the remaining 3–4 s cycle hiss (even with MASTER at 0) is the **USB cable ground** (pedal on the Mac + output into the same Mac's interface): gone without the cable, back when the cable touches the enclosure |
| v0.13.22 | `_973`–`_974` | `fw/build.py --model X.nam` (A2 / A2-Lite); `nam2c` emits `_NORM` (loudness → -18 dB, like the plugin) and `_TITLE` (display). Output = normalization × 2.5 (+8 dB) with a **soft limiter** above 0.7 of full scale. `_974` = Fender Super Reverb 1965 capture (amp+cab, TONE3000) | fixed point with this capture: SNR 125–130 dB vs float; NAM 2460 µs/block |

The user's verdict with `_974`: "not excellent yet, but already usable".

To test without the ground hiss: write and measure over USB, then unplug the cable and run the pedal on battery or a
charger to listen through the same Mac's interface.

Diagnostics over USB:
- `tools/jb_nam_on.py --no-enable --watch N`: blocks/s, resyncs, late outputs and longest stretch without poll per loop section (`JBS`, `JBG`);
- `tools/jb_capture.py DIR`: capture and comparison with the host;
- `tools/jb_quiet.py M`: turns off LEDs, PWM or display.

### To do
- Refine the sound: the pedal runs at ~44.1 kHz and captures are 48 kHz (change the rate or resample); input gain
  (the guitar reaches -1.4 dBFS); review the output gain and limiter by ear; optional IR.
- The profiler stops after 12 samples; the boot log keeps 1 record.

## v0.13.15 – v0.13.16 (2026-10-23) — **solved: VDC14 at 1.50 V; NAM stable on both cores**

- **Cause**: at 240 MHz, with both cores active and dense RAM writes, core 0 read **flipped bits** (e.g.
  `[0x01C70504] = 0xad8ffe61`, expected `0x8d8ffe61`, only bit 29). In a return address or an instruction, that sent
  execution into data (`au_rx`) → exception → reset. It was neither a bank conflict nor a logic bug.
- **VDC14** (`P3_ANA_CON6` bits 2–0, 1.25 V + 0.05 V × level) is the input of the SYSVDD LDO. Default 1.40 V (level 3).
  Raising only SYSVDD did not help; raising VDC14 does:

  | VDC14 | SYSVDD | JBY mode 9 (core 1 writes, core 0 reads) |
  |---|---|---|
  | 1.40 V | 1.32 V | reset in ~12 ms |
  | 1.40 V | 1.38 V | 15 539 wrong bits in 46 M reads (core 0) |
  | **1.50 V** | 1.38 V | **0 errors** (5 s ×2, 20 s, 20 s with float = mode 25) |
  | **1.50 V** | 1.32 V | **0 errors** (10 s) |

- **NAM on two cores with VDC14 = 1.50 V and SYSVDD = 1.32 V: 120 s without failure, 2700 µs/block (max 2706; limit
  2902), core 1 never late.**
- **v0.13.16 (`BlackBox_964`)**: the firmware sets VDC14 to 1.50 V at boot (`VDC14_LEVEL`, right after `usb_start`).
- New tools: `tools/jb_volt.py --vdc14 N`, `tools/jb_reboot.py` (JBB), `tools/jb_hsb.py` (JBH: the HSB divider also
  divides the CPU, 240 → 120 MHz; at 120 MHz everything passed, which pointed at electrical margin), JBY with the
  first error of each core.
- **v0.13.17 (`BlackBox_965`)**: `nam_start()` (boot and JBN) turns NAM on **at boot** and updates the display.
- Built-in model then: `reference/NeuralAmpModelerCore/example_models/A2.nam` (NAM Core example; 48 kHz,
  loudness -26.4 dB, input 14.76 dBu). The pedal runs at ~44.1 kHz, so the model is slightly shifted in frequency.

## v0.13.12 – v0.13.13 (2026-10-23, morning) — not a bank conflict

- **JBY on 958/959 crashed the pedal itself** about 12 ms after starting: uptime (`crumb.ms`, 0x01C7C0B8) went back to
  zero and no answer came. The 14–17 byte USB frames arrive intact (`sx_frame` read with JBM).
- **v0.13.12 (`BlackBox_960`)**: JBY with a mode byte (1 = no profiler, 2 = core 0 only serves USB, 4 = core 1
  stopped) and `csync` every turn of core 0's loop. With that, **all modes pass**, including 0.
- **Banks (JBY, core 1 reading 0x01C60000, 20 s each)**: core 0 at 64 KB, 32 KB, 4 KB, **same address**, pool
  (0x01C30000) and `.bss` (0x01C0BA2C) → **0 errors in ~360 M + 377 M reads, no reboot**, same throughput everywhere.
  Dense reads from both cores do not conflict anywhere.
- JB6 on 960 (core 0 running the float front): region 1 (`.bss`, crashed on 958) and region 2 → 0 errors in 20 s.
- **NAM on two cores (`jb_nam_on.py`) still crashes in 5–10 s** (reboot; the pedal comes back in passthrough).
- **The profiler stops after 12 samples** (TIMER5 stops firing ~12 ms after `prof_start`).
- `tools/jb_log.py`: was missing the 0.4 s pause after opening the port (the first SysEx was lost → "no answer").
- **New hypothesis**: what the pipeline has and the tests don't is core 1 **writing** to RAM while core 0 runs float.
  **v0.13.13 (`BlackBox_961`)**: JBY mode 8 = core 1 writes and rereads the pattern (`mw_job`), mode 16 = core 0 runs
  the float front.
- v0.13.13 also lowers NAM's output by **-12 dB** (`NAM_OUT_TRIM` 0.25, MASTER at max), a first value.

### v0.13.13 results (`BlackBox_961`) — the failure is core 1 writes + dense core 0 access

| JBY (mode) | Core 1 | Core 0 | Result |
|---|---|---|---|
| 1 / 0 | reads | reads | ok (20 s, 0 errors) |
| 17 | reads | float front | ok |
| 11 | **writes** + rereads | USB only | ok (10.6k turns of 4 KB/s, 0 errors) |
| 9 | **writes** + rereads | reads | **reset** in ~12 ms (turn 129) or ~3 s |
| 25 | **writes** + rereads | float front | **reset** |

- Independent of region: core 1 writing to the pool (0x01C30000), high RAM (0x01C60000) or RAMTEXT (0x01C02000) →
  reset. After several resets in a row the pedal entered the **rescue loader** (`ota-?`), as designed.
- The reset is a **core 0 exception** (`fm1_crash`: vec 1, `p3_rst` = 0, i.e. neither watchdog nor brown-out):
  PC = 0x01C09818, **inside the audio buffer `au_rx`**; ETM only shows jumps inside it; RETS = 0x02001742 (`poll`,
  right after `call led_write`). Core 0 started executing data: an instruction or return address read wrong, the same
  kind of defect as bit 21 in core 1's RETS (v0.12.3).
- The SDK's data cache only covers addresses ≥ 0x04000000 (SDRAM), so cache coherency does not explain the failure.
- Next suspect: **bus clock**. `SYS_DIV` (0x10008) = 0x00010200, i.e. HSB divider = 0 (bus at system clock).
  **v0.13.14 (`BlackBox_962`)**: command `JBH d` (`tools/jb_hsb.py D`) changes that divider at runtime from RAM and
  measures the CPU clock.

### v0.13.14 results (`BlackBox_962`) — it is timing margin at 240 MHz

- `JBH 1` (HSB divider = 1) **also divides the CPU**: `clk_loop` 8333 µs → 16667 µs (240 → 120 MHz).
- **At 120 MHz everything passes**: JBY mode 9 and **mode 25 (core 1 writing + float front, the NAM case)** → 0
  errors, no reset. The failure is the chip's timing margin with both cores at 240 MHz.
- Voltages (P33): `P3_ANA_CON6` = 0x0b → **VDC14 = 1.40 V** (input of the SYSVDD LDO; the SDK uses 1.40 V and SYSVDD
  1.26 V by default). With SYSVDD at 1.38 V the LDO headroom is only 20 mV.
- 240 MHz + SYSVDD 1.38 V, mode 9 (5 s): **no reset**, but 4.3 M wrong reads on core 1 and 19 M (56 %) on core 0, and
  **core 1 took an exception**.
- The SDK (`cpu/wl82/debug.c`, `exception_auto_fix_config`) has an "automatic exception fix" that raises VDDIOM and
  SYSVDD and, last, lowers the SDRAM clock: JieLi treats these exceptions as a voltage/clock problem.

## v0.13.x (2026-10-23) — NAM on two cores: fixed point on core 1, crashes

**State when stopping (v0.13.11, `BlackBox_958`)**: the pedal boots with NAM off (passthrough, safe). NAM on two cores
is turned on only over USB (`tools/jb_nam_on.py`) and still crashes in ~6 s.

### Done
- `nam/a2lq.c`: second half of A2-Lite (layers 13..22 + head) in **fixed point** (Q2.29 weights, Q8.23 activations,
  64-bit accumulator with the `r1_r0 += r2 * r3 (s)` instruction via inline asm); no float instruction at all (the
  build checks). `nam/a2lx.c`: conversions and prewarm (core 0). Host (`nam/test/a2lx_host.c`): **SNR 121 dB** and max
  diff 2.8e-7 against the float model.
- Pipeline: core 0 runs the float front and converts to Q8.23; core 1 runs the fixed-point back.
  - v0.13.0: 60 s without failure, but 2940 µs/block (> 2902);
  - v0.13.1 (explicit MAC): **2674 µs/block** (wait for core 1 ~200 µs), but crashes in ~6–15 s.
- Everything core 1 uses lives in `.c1_bss` (0x01C50000–0x01C5DBD0).
- Diagnostic tools: **profiler** on TIMER5 at 1 kHz (both cores' PCs saved in `.noinit` and logged at boot as
  "PROFILER" in `tools/jb_log.py`); flash operations run with interrupts off; the wait for core 1 became "calm" (no
  hammering of the mailbox).
- New tests, all over USB: JB9 (`tools/jb_qback.py`), JBX (`tools/jb_arith.py`), JB6 with duration
  (`tools/jb_misread.py SEC REGIONS`), JBY (`tools/jb_bank.py X Y SEC`); JBV also sets the test voltage (`nam_vlevel`).

### What the tests showed

| Core 1 | Core 0 | Result |
|---|---|---|
| fixed-point back (JB9) | service / integers only | ok, 20 s |
| fixed-point back (JB9) | float front | core 0 reads an invalid address |
| dense 32-bit mul loop over a table at 0x01C5C9E4 (JBX) | float front | crash (even at 1.38 V) |
| dense reads at **0x01C60000** (JB6, 30 s) | float front | **0 errors in 538 M reads** |
| dense reads at **0x01C0BA2C** (.bss) (JB6) | float front | **crash** |
| recursion (stack) at 0x01C50000 (JB8, 120 s) | float front | ok |

- Voltage alone did not fix it (1.38 V crashes too). The SDK starts core 1 the same way as Felucca (`EnableOtherCpu`
  and `cpu1_start` disassembled from `system.a` / `cpu.a`); clock dividers `0x10008 = 0x00010200`.
- Hypothesis then: SRAM **bank conflict** (disproved in v0.13.12; the real cause was voltage margin, v0.13.15).

## v0.11.x – v0.12.x (2026-10-23) — second core: what fails and what works

- **Core voltage**: SYSVDD default 1.26 V (`P3_ANA_CON9` level 11); we use 1.32 V (level 13) when core 1 works.
  Core 1's wait loop in flash (RAM + RAM hangs the chip).
- **Diagnostic tools** (all over USB):
  - the chip's debug unit (`corex2`, 0x1EEE000, from the AC79 SDK's `cpu/wl82/debug.c`): **per-core write
    protection windows** (`WR_LIMITn`, `C1_WR_LIMIT_EN`) and invalid bus access exceptions (`DBG_EN`);
  - **core 1 registers readable by core 0** (`q32DSP(1)` at 0x1EEF200: r0..r15, RETS, SP, PC);
  - exception enabled on core 1 with its own record (registers + stack at the time of the fault);
  - `tools/jb_c1.py` (JBQ), `tools/jb_crash.py` (JBF), `tools/jb_peek.py` (JBM), `tools/jb_volt.py` (JBV).
- **v0.12.0**: with the windows, core 1 was caught writing **outside the buffers** inside `layer_run` (block 214, ~0.6 s
  after NAM on); the pedal stayed alive (core 1 safely stopped).
- **v0.12.3**: core 1 snapshot after a float failure: `RETS = 0x02200a32` (correct: `0x02000a32`) — the return address
  came back from the stack **with an extra bit 21**.
- **Tests (core 0 running NAM's float front at the same time):**

  | Test | Core 1 | Result |
  |---|---|---|
  | JB6: reads fixed patterns in 4 RAM regions | integer | 0 errors in ~140 M reads |
  | JB8: verified deep recursion (stack) | integer | 0 errors in 474k recursions / 120 s |
  | JB7: verified float chain | **float** | the pedal falls (watchdog) |
  | NAM pipeline (v0.10–v0.12.2) | **float** | fails in 0.6–60 s |

- **Conclusion**: on this unit **core 1 is reliable only with integers**. Plan: core 0 runs the first half of A2-Lite in
  float; core 1 runs the second half in **fixed point**; core 0 converts at the input and output.
- Everything core 1 uses (stacks, mailbox, `nam`, `pipe`) went to high RAM (`.c1_bss` section, 0x01C50000).
- One core is not enough: the best possible loop (~3 cycles/MAC) would give ~3050 µs per block (> 2902).

## v0.8 – v0.10 (2026-10-22/23) — NAM A2-Lite on the pedal

- `nam/a2lite.c`: A2-Lite in C, **identical to the official NeuralAmpModelerCore** (max diff 5e-7,
  `nam/test/a2lite_host.c` against `render --slim 0.2`; note: `--slim 0.5` selects Full).
- Performance measured on the pedal (240 MHz — the same clock as the stock firmware):

  | Version | Time per 128-sample block (limit 2902 µs) |
  |---|---|
  | v0.8 (frame-major, mask per access) | 5792 µs |
  | v0.8.2 (tap-major, mirrored ring) | 4736 µs |
  | v0.8.3 (kernel in RAM) | 4888 µs (no gain) |

- Microbenchmarks (`JBP`): float mul+add 4.75 cycles/MAC in the real loop; Q31 with 64-bit accumulator 5.3–5.6
  (slower); the FPU **has** a MAC (`r0 += r1 * r2 (f)`, the compiler does not emit it), but it is not pipelined:
  3.2 cycles; separate mul and add pipeline (1.9 cycles/pair).
- **Second core (Felucca `fm1_cpu1.h`):** starts and runs isolated integer and float tasks (`JB1`), but:
  - both cores executing **code from RAM** at the same time **hang the chip** (`JB2`, `JB3`, all modes);
  - both executing from **flash** with integer loops: ok (`JB4` mode 3);
  - v0.10: NAM pipelined across both cores in flash (float on both) → core 1 fails, the fallback catches it, **and USB
    and audio DMA stop** (core 0 still alive: display, LEDs, ADC). Recovered by power-cycling and writing v0.10.1 the
    moment USB answered.
- **v0.10.1**: second core and NAM off (passthrough), to move on safely.
- A2-Lite internal ranges (`nam/test/a2lite_ranges.c`, real guitar, input peak 0.5): weights ±1.4; layer inputs up to
  ~14; activations up to ~21; head sum ~41. Proposed fixed point: Q8.23 activations (±256), Q2.29 weights (±4), 64-bit
  product shifted by 29 → headroom > 6×.

## v0.2.x – v0.7 (2026-10-22) — Juice-Box, display, controls, audio passthrough

- New name and logo (juice jar), "by João Viana" on the splash screen; USB: product `Juice-Box`, manufacturer
  `João Viana` (macOS may keep the old MIDI port name cached: the VID:PID did not change).
- **Boot log in flash** (`0xB9000`, 32 B records) + read over USB: `.venv/bin/python tools/jb_log.py`. Each boot
  records how far the previous boot got (stage, ms, loops), the reset reason and the bootguard counters.
- v0.3: panel controls mapped; v0.4–v0.7: I²S audio passthrough ([06](06-stock-firmware-analysis.md)).

## v0.1.2 (2026-10-22)

v0.1.1 incident: the "USB OK" indicator did not go away when unplugged and, on battery only, the pedal stayed with a
black screen. Cause: an extra rescue rule "bus reset but never configured within 30 s". Without a cable, the floating
D+/D- lines read as a bus reset → after 30 s the firmware entered the rescue loader (which does not use the display).
The pedal was found in the loader (`ota-?`) and v0.1.2 was installed through it: **rescue worked as designed**.

- Rule removed: rescue only after two watchdog resets in a row.
- USB indicator: `config && !suspended` (bus idle = cable out).
- Diagnostic line for 2 s at boot: `RST` (reset reason, P33 register), `BOOT` (boots since the last cold power-on),
  `RESC` (rescue entries).

## v0.1.1 (2026-10-22)

- Display: visible area on rows 0..239 of the controller (v0.1 used 40..279); clears the whole controller RAM
  (240×320) before the image, so nothing of the stock UI remains.
- Installed from V20 with `--bump-cfg`.

## v0.1 (2026-10-22)

1. **USB-MIDI + update entry first** (device `armaria-nam`, pid.codes test VID:PID `1209:0001`), answers the handshake
   as `BlackBox_901` and accepts packages through the same protocol as M-UPGRADE (Felucca's step 1).
2. **Display**: ST7789 init copied from V20, 240×240 splash image and a status line.
3. **Rescue**: on first boot it writes the official JieLi loader at `0xBA000` (end of the VM region). Two failed boots in
   a row enter that loader through the update record in RAM; it accepts V20 from `bb_install.py`. It never enters
   UBOOT mode (which on the Mac would need extra hardware).

Verified on the pedal:
- Installed from the stock app (`BlackBox_019` → `BlackBox_901`).
- **Update from armaria-nam → V20: works.** Our firmware's step 1 accepted the package (75 reads), wrote the record in
  RAM and rebooted; the SPL started the official loader (`4D4A:4155`, "Jieli Technology"), which wrote V20 (100 %) and
  came back as `BlackBox_020`.
  - Finding: started only with the RAM record, the official loader answers the handshake with an **empty identity**
    (it would read the identity from a flash record, which we do not write on purpose). `bb_install.py` accepts it as
    `ota-?`.

## Layout

```
fw/
  build.py            builds (Docker), assembles app.bin, packs with tools/bbpkg.py
  app.ld              memory map (from Felucca; app slot = 0xA0160 B)
  src/juicebox.c      single unit: timing, display, flash, update, rescue, audio, NAM, UI, main
  tools/gen_assets.py splash 240x240 RGB565, fonts, official loader (build/ only)
  assets/             Inter Tight (OFL)
  felucca/            Felucca's HAL, USB, OTA and crt0 (GPL-3.0), commit in FELUCCA_COMMIT;
                      changes marked "armaria-nam" (OTA constants and overridable USB strings)
```

To install from the **stock app**, the package's `cfg` must differ from the installed one (`--bump-cfg`). From
Juice-Box there is no such lock.

## Flash map

| Region | Use in Juice-Box |
|---|---|
| `0x00000–0x04000` | V20 header (SPL, isd_config) — never changed |
| `0x04000–0xA9000` | app (ours) + cfg_tool + V20's cfg |
| `0xA9000–0xB9000` | 8 capture slots |
| `0xB9000` | boot log |
| `0xBA000–0xBF000` | official loader staged (rescue / update) |
| `0xBF000` | settings (banks, language, active capture) |
| `0xC2000`, `0xFF000` | BTIF, key_mac — never touched |
| `0xC3000–` (`USR`) | the official firmware's user data — never touched |
