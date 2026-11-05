# 09 – Prior art: Onyx (M-VAVE / Donner Pocket AMP)

Repository: [28spawn/m-vave-pocket-amp-onyx](https://github.com/28spawn/m-vave-pocket-amp-onyx) (GPL-3.0-or-later,
first public commit 2026-11-04, "Pack 194"). Same SoC (AC791N, pi32v2), same JieLi chip-key family. Read-only clone in
`reference/onyx` (nothing of it was built or run). Its docs say it was written with AI assistance; several docs disagree with
the code (e.g. ROADMAP says core 1 is dormant and nothing was measured, the code has a working two-core pipeline with
measured costs), so the code is the source of truth below.

## The key finding: a hardware matrix / FIR accelerator at `0x01EE0400`

The AC791N has a vector coprocessor (`custom-fw/include/pa_hw_accel.h`, `src/dsp/hw_accel.c`), driven by a 24-byte command
block (`config, src_a, src_b, dst, ctrl, stride`):

| Mode | `config` | What it does |
|---|---|---|
| matrix × vector | `0x0AA1` | `dst[r] = sat((Σ_j A[r·step + j] · B[j] + round) >> shift)`, int32 in, 64-bit accumulate; `ctrl` = shift[31:24], rows[23:12], cols[11:0]; `stride` = dst step bytes[23:20], row step bytes[15:0] |
| block FIR | `0x0A82` / `0x8A82` (accumulate) | partitioned FIR (Onyx found its output layout confusing and does not use it now) |

Start by writing the block and polling bit 7 (READY) of `reg[0]`, acknowledge with bit 6. Onyx bounds the wait (1 ms) and falls
back to a bit-exact software model if the unit ever times out. The accelerator does about **one multiply-add per 4.3 cycles**
and runs **in parallel with the CPU**; it shares the SRAM bus, so Onyx keeps its matrices in the low SRAM (`0x01C02000..`).

According to Onyx, the Pocket AMP stock firmware runs **its own A2-Lite layers on this accelerator**. Our BlackBox V20 loads
`0x01EE0400` in 5 places (`analysis/disasm/app_v20.dis`: `0x2002484`, `0x2043f2a`, `0x2045eba`, `0x20460ae`, `0x2046862`), so
the stock "ANN" engine very likely does the same.

## NAM A2-Lite in Onyx (`src/nam/pa_wavenet.c`)

- 44.1 kHz, **64-sample blocks (1451 µs)**. Only A2-Lite (3 channels, 1871 weights); A2-Full is rejected (RAM and CPU).
- Weights converted at load to **Q24 fixed point**, activations Q21; 1x1 bias as a 64-bit Q45 start value.
- Each dilated layer = an im2col matrix (one row per sample: the `k` taps × 3 channels, plus the input mixin and 1.0 for the
  bias = `3k + 2` columns) × the layer's 3 output vectors → **3 accelerator jobs per layer** (one per output channel).
- The block is split in two chunks A/B and **pipelined**: while the accelerator runs chunk A of layer *l*, the CPU does the
  leaky ReLU + 1x1 mix (assembly, `pa_wavenet_asm.c`) of chunk B of layer *l−1* and gathers chunk B of layer *l*. The head
  (16 taps) is two more jobs.
- Runs on **core 1**, together with any slot in front of the amp that fits a 1300 µs budget. Core 0 does the input
  conditioning and the output stage (filters, soft clip; "software floats", ~40 µs that core 1 cannot spare), then all
  post-amp slots. Core 1 jobs carry a check word, results are verified, and a late core 1 makes the block silent rather than
  hanging. Same voltage note as ours: core 1 misreads at the bootloader's VDC14/SYSVDD levels; the stock raises them.

## Cabinet IR (`src/fx/fx_cab_ir.c`, `src/fx/fft_conv.c`)

- **Float uniform-partitioned FFT convolution on core 0**: 64-sample partitions, 128-point real FFT (as a 64-point complex
  FFT, radix-4), **1024 taps by default, up to 2048**. Bench: 1,153 cycles/sample for 1024 taps (~200 µs a 64-sample block at
  360 MHz; their slot table budgets 340 µs). A load governor drops partitions when the audio interrupt runs long.
- Low-cut / high-cut biquads around it. The accelerator FIR path exists but is disabled (`PA_CAB_FFT 1`).

## Compared with Juice-Box v0.17

| | Juice-Box | Onyx |
|---|---|---|
| Block | 128 samples, 2902 µs | 64 samples, 1451 µs |
| NAM math | CPU only: float front on core 0, Q2.29 / Q8.23 back on core 1 | accelerator (Q24 × Q21) + CPU activation in asm, on core 1 |
| NAM cost | ~2700 of 2902 µs (both cores busy) | fits one core with room for pre-amp slots |
| IR | none | float FFT partitioned, 1024–2048 taps, core 0 |
| Clock | measured 240 MHz | assumes 360 MHz (unverified) |

## Confirmed on the BlackBox (v0.17.1)

`tools/jb_accel.py`: all four test shapes exact; ~12 ns per multiply-add (≈ 84 MMAC/s). The A2-Lite convolutions are
~1.5k multiply-adds per sample (≈ 66 MMAC/s at 44.1 kHz), about 80 % of the unit, in parallel with the CPU, which is left
with the gathers, the activations and the 1x1 mixes. See [07](07-firmware-history.md#v0171-2026-11-05--the-matrix-accelerator-works-on-the-blackbox).

## What it means for us

1. **Using the accelerator for the A2-Lite convolutions** could free most of one core: the per-layer convolutions are the
   bulk of the ~1.9k MACs/sample. That is what makes IR (Phase 7) possible together with NAM, instead of an IR-only mode.
2. The register protocol is small and Onyx has a software model to test against (`accel_software`). A first step on our side:
   a USB-triggered self-test (one 4×3 job, compare with the CPU), like Onyx's `wn_lowmem_selftest`.
3. IR: an FFT partitioned convolver on the core that has the time; 1024 taps at 44.1 kHz is 23 ms, enough for a cabinet.
4. Licence: GPL-3.0-or-later is compatible with our GPL-3.0; reuse needs attribution and the original notices.
