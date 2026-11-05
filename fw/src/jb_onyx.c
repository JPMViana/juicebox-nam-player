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
/* v0.18.0-dev (987): the engine runs on its own stack, in the memory left after the model (the main loop's 8 KB system stack
 * may be too small), with a painted guard above its top: the run reports how much stack it used and whether anything wrote past
 * the top (986 overwrote ~4 KB above the system stack's top with float data) */
#define OSTK_GUARD 1024u
#define OSTK_PAINT 0x5AFEC0DEu
static uint32_t *ostk_lo, *ostk_top, *ostk_end;
extern void jb_on_stack(void (*fn)(void *), void *arg, void *top);
__asm__(".section .text.jb_on_stack,\"ax\",@progbits\n"
        "\t.global jb_on_stack\n"
        "jb_on_stack:\n"
        "\t[--sp] = {rets, r4}\n"
        "\tr4 = sp\n"
        "\tsp = r2\n"
        "\tr3 = r0\n"
        "\tr0 = r1\n"
        "\tcall r3\n"
        "\tsp = r4\n"
        "\t{pc, r4} = [sp++]\n"
        "\t.text\n");
static uint8_t om_irq_off;                       /* 1: interrupts off around every block (diagnostic) */

/* diagnostics: asm bit 0 = 0 forces the C version of the CPU stage, bit 1 = every accelerator job in software; irq_off = 1 masks the interrupts around every block */
extern void pa_hw_accel_force_sw(int on);
extern volatile uint32_t jb_ready_delay, jb_verify;
void onyx_options(uint32_t asm_on, uint32_t irq_off) { jb_ready_delay = (irq_off >> 1) * 16u; irq_off &= 1u; s_wn_asm = (asm_on & 1u) ? s_wn_asm : 0u; om_irq_off = (uint8_t)irq_off; pa_hw_accel_force_sw((asm_on & 2u) != 0u); jb_verify = (asm_on & 4u) ? 1u : 0u; if (jb_verify) for (int i = 0; i < 16; i++) ((volatile uint32_t *)0x01C7D200u)[i] = 0; }

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
    {   const uintptr_t lo = ((uintptr_t)mem + sizeof *om + 63u) & ~(uintptr_t)63u, end = ((uintptr_t)mem + size) & ~(uintptr_t)7u;
        ostk_lo = (uint32_t *)lo; ostk_end = (uint32_t *)end; ostk_top = (uint32_t *)(end - OSTK_GUARD);
        if (end - lo < 16384u + OSTK_GUARD) ostk_lo = 0;      /* not room for 16 KB: run on the caller's stack */
    }
    pa_hw_accel_init();
    r[0] = (uint32_t)pa_wavenet_load_weights(om, w, n, 1.0f);
    r[1] = om->blk_ok; r[2] = om->blk_low; r[3] = s_wn_asm; r[5] = pa_hw_accel_broken(); r[6] = (uint32_t)om->weights;
}

#include "jb_onyx_input.h"

/* run blocks 64 samples at a time; r[0..7] = blocks, min, mean, max ticks per block (TIMER4), accel broken, accel errors,
 * bytes of the private stack used (0: no private stack), guard words above its top that were written (should be 0).
 * last: the last block's 64 raw outputs (float, Onyx's head output before head_scale). between() runs after every block, outside
 * the timing: Juice-Box's poll() (USB, the audio halves, the watchdog), so the dry signal keeps playing and the bus is shared as in use */
struct orun { uint32_t blocks; uint32_t *r; float *last; void (*between)(void); };
static void onyx_body(void *argp)
{
    struct orun *a = (struct orun *)argp;
    float x[PA_WN_BLK_N];
    uint32_t mn = 0xFFFFFFFFu, mx = 0, sum = 0;
    pa_wavenet_reset(om);
    in_t = 0; in_seed = 12345u;
    for (uint32_t b = 0; b < a->blocks; b++) {
        make_input(x, PA_WN_BLK_N);
        if (om_irq_off) __asm__ volatile("cli" ::: "memory");
        const uint32_t t0 = plat_ticks24();
        wn_block_run(om, x, a->last, PA_WN_BLK_N);
        const uint32_t t = plat_ticks24() - t0;
        if (om_irq_off) __asm__ volatile("sti" ::: "memory");
        if (t < mn) mn = t;
        if (t > mx) mx = t;
        sum += t;
        if (a->between)
            a->between();
    }
    a->r[0] = a->blocks; a->r[1] = mn; a->r[2] = a->blocks ? sum / a->blocks : 0; a->r[3] = mx;
}

void onyx_run(uint32_t blocks, uint32_t *r, float *last, void (*between)(void))
{
    struct orun a = { blocks, r, last, between };
    r[0] = r[1] = r[2] = r[3] = r[6] = r[7] = 0;
    if (!om || !om->blk_ok) {
        r[4] = pa_hw_accel_broken(); r[5] = pa_hw_accel_errors();
        return;
    }
    if (ostk_lo) {
        for (uint32_t *p = ostk_lo; p < ostk_end; p++) *p = OSTK_PAINT;
        jb_on_stack(onyx_body, &a, ostk_top);
        uint32_t *p = ostk_lo;
        while (p < ostk_top && *p == OSTK_PAINT) p++;
        r[6] = (uint32_t)((uintptr_t)ostk_top - (uintptr_t)p);
        for (p = ostk_top; p < ostk_end; p++) if (*p != OSTK_PAINT) r[7]++;
    } else
        onyx_body(&a);
    r[4] = pa_hw_accel_broken(); r[5] = pa_hw_accel_errors();
}

/* the state after a run, to find where the pedal and the Mac part: per layer, a checksum of its ring (with the mirror) and its
 * position; then the head history. c[0..22] ring sums, c[23..45] ring_pos, c[46] head_q sum (nam/test/wn_onyx_dev.c: the same) */
void onyx_state(uint32_t *c)
{
    uint32_t col = 0;
    for (int l = 0; l < PA_WN_NUM_LAYERS; l++) {
        const uint32_t w = 3u * (wn_R(l) + wn_MIR(l));
        uint32_t s = 0;
        for (uint32_t i = 0; i < w; i++) s = s * 31u + (uint32_t)om->ring_q[3u * col + i];
        c[l] = s;
        c[23 + l] = om->ring_pos[l];
        col += wn_R(l) + wn_MIR(l);
    }
    uint32_t h = 0;
    for (uint32_t i = 0; i < 3u * PA_WN_BLK_HEAD_STEPS; i++) h = h * 31u + (uint32_t)om->head_q[i];
    c[46] = h;
}

