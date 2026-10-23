/* SPDX-License-Identifier: GPL-3.0-only
 * Juice-Box (João Viana): NAM Architecture 2, "Lite" width (3 channels), in plain C.
 *
 * The A2 shape (NeuralAmpModelerCore NAM/wavenet/a2_fast.h): one layer array, 23 layers,
 * kernel sizes 6 (15 for layers 14 and 15), dilations 1 3 7 17 41 101 239 1 3 7 17 41 101 239 1 13
 * 1 3 7 17 41 101 239, LeakyReLU(0.01), bottleneck = channels, layer1x1 active, no gating / FiLM /
 * head1x1, head rechannel conv k = 16 with bias, head_scale = the last weight.
 *
 * Weights are taken in the .nam stream order (tools/nam2c.py writes them unchanged), so a model can
 * later be loaded as raw floats. No allocation: everything is in struct a2l.
 */
#pragma once
#include <stdint.h>

#define A2L_C 3
#define A2L_LAYERS 23
#define A2L_HEAD_K 16
#define A2L_BLOCK_MAX 128                    /* frames per a2l_process call */
#define A2L_NWEIGHTS 1871                    /* A2-Lite weight count (with head_scale) */
#define A2L_HEAD_RING 256u                   /* power of two >= 15 + A2L_BLOCK_MAX */

typedef float a2l_t;

/* The firmware puts the hot path in RAM (A2L_HOT = section(".ram_text")); the host test leaves it empty.
 * A caller in XIP flash must then call a2l_process through a pointer (the call range does not reach). */
#ifndef A2L_HOT
#define A2L_HOT
#endif

struct a2l_layer {
    uint16_t k, d;                           /* kernel size, dilation */
    uint32_t mask;                           /* ring size - 1 (power of two) */
    a2l_t *ring;                             /* (size + A2L_BLOCK_MAX) frames x A2L_C: tail mirrors [0, BLOCK) */
    a2l_t conv_w[15][A2L_C][A2L_C];          /* [tap][out][in] */
    a2l_t conv_b[A2L_C], mixin[A2L_C];
    a2l_t l1_w[A2L_C][A2L_C], l1_b[A2L_C];   /* [out][in] */
};

struct a2l {
    struct a2l_layer L[A2L_LAYERS];
    a2l_t rechannel[A2L_C];
    a2l_t head_w[A2L_HEAD_K][A2L_C], head_b, head_scale;
    uint32_t pos, pos_b;                     /* frames done by a2l_front / a2l_back (ring write positions) */
    uint32_t prewarm;                        /* samples for a2l_reset() */
    a2l_t head_ring[(A2L_HEAD_RING + A2L_BLOCK_MAX) * A2L_C];   /* the head's history (k = 16, d = 1), mirrored tail */
};

/* Ring storage the caller provides: for each layer the power of two >= (k - 1) * d + A2L_BLOCK_MAX
 * frames plus A2L_BLOCK_MAX (the mirrored tail), A2L_C values each (checked by a2l_load). */
#define A2L_RING_FRAMES (14592u + 23u * A2L_BLOCK_MAX)

/* 0 = ok, -1 = wrong weight count */
int a2l_load(struct a2l *m, const float *w, uint32_t n, a2l_t *ring_storage);
void a2l_reset(struct a2l *m);                            /* zero the history, then prewarm on silence */
void a2l_process(struct a2l *m, const a2l_t *in, a2l_t *out, uint32_t frames);   /* frames <= A2L_BLOCK_MAX */

/* The same in two halves for two cores (balanced: ~861 / ~870 MACs per sample). a2l_back of a block must
 * run after a2l_front of the same block, with the same in, lin and hsum (frames x A2L_C each); a2l_front of the
 * next block may run alongside it (separate layers, rings and scratch). */
#define A2L_SPLIT 13
extern a2l_t a2l_back_z[A2L_BLOCK_MAX * A2L_C];
void a2l_front(struct a2l *m, const a2l_t *in, a2l_t *lin, a2l_t *hsum, uint32_t frames);
void a2l_back(struct a2l *m, const a2l_t *in, a2l_t *lin, a2l_t *hsum, a2l_t *out, uint32_t frames);
