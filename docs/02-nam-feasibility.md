# 02 – Feasibility of NAM A2 on the AC791N

> Written before the port (2026-10-22). Outcome: A2-Lite runs split across both cores, ~2700 µs of a 2902 µs
> block — see [07](07-firmware-history.md).

## What A2 is

NAM Architecture 2 (TONE3000 + Steven Atkinson, 2026) ships as a `SlimmableContainer`: a single `.nam` with
sub-models picked according to the available CPU. In the official example
(`NeuralAmpModelerCore/example_models/A2.nam`):

| Sub-model | `max_value` | Channels | Layers | Weights | MMAC/s @ 48 kHz |
|---|---|---|---|---|---|
| **A2-Lite** | 0.5 | 3 | 23 (kernels 6/15, dilation up to 239) | 1 871 | **~90** |
| **A2-Full** | 1.0 | 8 | 23 | 12 146 | **~583** |
| A1-Standard (reference) | – | 16/8 | 20 | 13 802 | ~663 |

Activation: `LeakyReLU(0.01)` — cheap (no `tanh`), great for embedded.
TONE3000's reference: A2-Lite uses ~50 % of a 600 MHz Cortex-M7
(≈300 Mcycles/s → ~3.3 cycles/MAC including overhead).

Generated with `tools/nam_budget.py`.

## Budget on the BlackBox

CPU: pi32v2 @ 360 MHz, possibly 2 cores (stock uses 1).

| Scenario | Cycles/MAC | Cores | A2-Lite load | A2-Full load |
|---|---|---|---|---|
| Pessimistic (same as TONE3000's M7) | 3.3 | 1 | ~82 % of 1 core, **no headroom** | impossible |
| Realistic, optimized | 2.0 | 1 | ~50 % | ~320 % |
| Optimistic (asm/SIMD, 2 cores) | 1.0 | 2 | ~12 % | ~81 % |

## Conclusions

1. **A2-Lite is the realistic target.** It fits, but needs a hand-optimized kernel (no Eigen/NEON on the pi32v2:
   specialized dilated-convolution loops, buffers in internal SRAM, possibly Q15/Q31 fixed point).
2. **A2-Full would only be possible** if (a) the second core is usable **and** (b) we reach ≈1 cycle/MAC.
   Unlikely; a stretch goal.
3. Running at 44.1 kHz cuts the load by ~8 %, but the model was trained at 48 kHz (subtle tone shift) — consider
   clocking the codec at 48 kHz.
4. Memory is not a problem: A2-Lite has ~7 KB of fp32 weights; the largest history buffer
   (dilation 239 × kernel 6 × 3 channels) is a few KB.

**Update (static benchmark, [05](05-static-benchmark.md)):** the pi32v2 FPU has no FMA (~4.2 instr./MAC in float),
but there is a **native integer MAC with a 64-bit accumulator** (~3.3 instr./MAC, target ~2).

**Update:** the open Felucca firmware already runs code on the AC791N's **second core** (`FELUCCA_CPU2`), so the
two-core scenario is realistic.

## Plan B (if the pi32v2 can't deliver)

Brain swap: keep the BlackBox's enclosure, footswitches, jacks and power, and fit a 600 MHz Cortex-M7 board
(e.g. Teensy 4.1 / i.MX RT1062, or Daisy Seed / STM32H750 at 480 MHz) with its own codec. Not needed in the end.
