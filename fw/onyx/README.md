# fw/onyx — code from Onyx

Files copied unmodified (unless a change is listed below) from **Onyx**, the open-source firmware for the M-VAVE / Donner
Pocket AMP by @28spawn: <https://github.com/28spawn/m-vave-pocket-amp-onyx>, commit
`06d3f02b8177176162a134b61b13c7761230089f` (2026-11-04), licensed **GPL-3.0-or-later** (see that repository's `LICENSE`
and `NOTICE.md`). Used here under the GPL-3.0, the licence of this project. Thank you!

| File | Onyx path | What it is |
|---|---|---|
| `src/nam/pa_wavenet.c` | `custom-fw/src/nam/pa_wavenet.c` | NAM A2-Lite WaveNet: float reference and the block engine on the matrix accelerator |
| `src/nam/pa_wavenet_asm.c` | `custom-fw/src/nam/pa_wavenet_asm.c` | the block engine's CPU stage (leaky ReLU, 1x1 mix, head sum) in pi32v2 assembly |
| `src/dsp/hw_accel.c` | `custom-fw/src/dsp/hw_accel.c` | driver and bit-exact software model of the accelerator at `0x01EE0400` |
| `include/*.h` | `custom-fw/include/` | their headers (`pa_wavenet.h`, `pa_hw_accel.h`, `pa_hw.h`, `pa_plat.h`, `pa_config.h`) |

Analysis: [docs/09-onyx-analysis.md](../../docs/09-onyx-analysis.md). Local changes: none yet.
