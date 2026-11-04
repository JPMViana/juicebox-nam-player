# 05 – Static benchmark of the A2-Lite kernel on the pi32v2

Date: 2026-10-22. Compiler: JieLi clang 4.0.1 (`pi32v2`, toolchain `jieli-linux-toolchains-20260730.1`) through
`scripts/pi32cc.sh` (Docker). Sources in `nam/bench/`. "Static" = instruction count in the generated assembly; real
cycles only on the pedal (24 MHz timer, `TIMER4`). Measured results on the pedal are in [07](07-firmware-history.md)
(v0.8–v0.10).

## Flags

The official SDK flags are required: **`-mcpu=r3 -mfprev1`**. Without them float becomes software calls
(`__mulsf3`/`__addsf3`), dozens of cycles per operation.

## Results (A2-Lite layer: 3 channels, kernel 6, dilated)

| Variant | File | MACs/iter. | Instr./iter. | **Instr./MAC** | Notes |
|---|---|---|---|---|---|
| float, naive order | `conv_kernel.c` | 18 | ~87 | ~4.8 | many stack spills |
| float, outputs together | `conv_kernel2.c` | 9 | ~38 | **~4.2** | separate `fmul` + `fadd`; **no FMA** (`fmaf` becomes a libm call) |
| **Q31 + 64-bit accumulator** | `conv_kernel3.c` | 9 | ~30 | **~3.3** | native MAC: `r5_r4 += r14 * r7 (s)` — 1 instruction per MAC |

- `__builtin_pi32v2_smuladd64` exists but the backend fails with `-mcpu=r3` ("Cannot select"); plain C
  `acc += (int64_t)a * b` emits the same instruction.
- `-mllvm -pi32v2-enable-simd=true` changed nothing in these kernels.
- There are still ~4 stack reloads per iteration in Q31; with a tuned weight layout / manual unrolling the target is
  **~2 instr./MAC**.

## What this means for A2-Lite (~90 MMAC/s @ 48 kHz)

Assuming ~1 instruction/cycle (to be confirmed on hardware):

| Kernel | Mcycles/s | 1 core @ 360 MHz | 2 cores |
|---|---|---|---|
| float today (4.2) | ~378 | 105 % ❌ | ~53 % each |
| Q31 today (3.3) | ~297 | 83 % ⚠️ | ~41 % each |
| Q31 optimized (target 2.0) | ~180 | **50 % ✅** | ~25 % each |

**Provisional decision (later revised, see 07):** fixed-point Q31 kernel with a 64-bit accumulator, hot code in RAM
(`.ram_text`, not flash XIP), second core as reserve.

## Unknowns only the hardware answers

- Real latency of the 64-bit MAC and of SRAM loads (stalls?)
- Cache/XIP cost if the code does not fit in `.ram_text`
- Clock: the BlackBox config says 360 MHz; the FM-1/Felucca runs at 240 MHz (set by the SPL)
