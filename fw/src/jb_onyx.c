/* jb_onyx.c - Juice-Box's glue to Onyx's NAM A2-Lite block engine (fw/onyx, GPL-3.0-or-later, see fw/onyx/README.md).
 *
 * v0.18.0-dev: a TEST only (JBO over USB, tools/jb_onyx.py). The current two-core NAM is stopped, the Onyx model is placed in
 * its memory (the pool's nam_ring), loaded from the active capture, and run on a synthetic input from the main loop while the
 * audio keeps passing the dry signal. The pedal is rebooted afterwards to get the normal NAM back.
 *
 * Onyx's engine (pa_wavenet.c, wn_block_run): 64-sample blocks, Q24 weights / Q21 activations, every dilated convolution as
 * three matrix x vector jobs on the accelerator at 0x01EE0400, the CPU stage (leaky ReLU, 1x1 mix, head sum) in assembly
 * (pa_wavenet_asm.c) overlapped with the jobs. Its matrices live in the low SRAM 0x01C02000..0x01C05D00 (free in our map:
 * .ram_text ends at 0x01C00BA0). */
#include <stdint.h>
#include "pa_wavenet.h"
#include "pa_hw_accel.h"

/* what pa_wavenet.c / hw_accel.c need from a board (Onyx's main.c provides them) */
uint32_t plat_ticks24(void) { return *(volatile uint32_t *)0x00010804u; }   /* TIMER4, 24 MHz */

/* the engine's block runner is static in pa_wavenet.c: this unit includes it so the test can call it directly (the raw head
 * output before Onyx's conditioning and output stage, the same thing tools/... compare on the host) */
#include "../onyx/src/nam/pa_wavenet.c"

static pa_wavenet_model_t *om;

uint32_t onyx_model_size(void) { return (uint32_t)sizeof(pa_wavenet_model_t); }

/* place the model at mem (size bytes), load n weights; r[0..6] = rc, blk_ok, blk_low, asm stage ok, model size, accel broken,
 * the address of the model's float weights (tools/jb_onyx.py reads them back to run the same model on the Mac) */
void onyx_prepare(void *mem, uint32_t size, const float *w, uint32_t n, uint32_t *r)
{
    r[4] = (uint32_t)sizeof(pa_wavenet_model_t);
    if (size < sizeof(pa_wavenet_model_t)) {
        r[0] = 0xFFFFFFF0u; r[1] = r[2] = r[3] = r[5] = r[6] = 0;
        om = 0;
        return;
    }
    om = (pa_wavenet_model_t *)mem;
    memset(om, 0, sizeof *om);
    pa_hw_accel_init();
    r[0] = (uint32_t)pa_wavenet_load_weights(om, w, n, 1.0f);
    r[1] = om->blk_ok; r[2] = om->blk_low; r[3] = s_wn_asm; r[5] = pa_hw_accel_broken(); r[6] = (uint32_t)om->weights;
}

#include "jb_onyx_input.h"

/* run blocks 64 samples at a time; r[0..5] = blocks, min, mean, max ticks per block (TIMER4), accel broken, accel errors.
 * last: the last block's 64 raw outputs (float, Onyx's head output before head_scale). between() runs after every block, outside
 * the timing: Juice-Box's poll() (USB, the audio halves, the watchdog), so the dry signal keeps playing and the bus is shared as in use */
void onyx_run(uint32_t blocks, uint32_t *r, float *last, void (*between)(void))
{
    float x[PA_WN_BLK_N];
    uint32_t mn = 0xFFFFFFFFu, mx = 0, sum = 0;
    if (!om || !om->blk_ok) {
        r[0] = 0; r[1] = r[2] = r[3] = 0; r[4] = pa_hw_accel_broken(); r[5] = pa_hw_accel_errors();
        return;
    }
    pa_wavenet_reset(om);
    in_t = 0; in_seed = 12345u;
    for (uint32_t b = 0; b < blocks; b++) {
        make_input(x, PA_WN_BLK_N);
        const uint32_t t0 = plat_ticks24();
        wn_block_run(om, x, last, PA_WN_BLK_N);
        const uint32_t t = plat_ticks24() - t0;
        if (t < mn) mn = t;
        if (t > mx) mx = t;
        sum += t;
        if (between)
            between();
    }
    r[0] = blocks; r[1] = mn; r[2] = blocks ? sum / blocks : 0; r[3] = mx;
    r[4] = pa_hw_accel_broken(); r[5] = pa_hw_accel_errors();
}
