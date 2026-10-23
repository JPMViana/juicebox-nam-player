/* SPDX-License-Identifier: GPL-3.0-only
 * Juice-Box (João Viana): the back half of the A2-Lite (layers A2L_SPLIT..22 + the head) in fixed point, for the
 * second core. On this unit core 1 runs integer code reliably and float code does not (docs/07, v0.12.x), so
 * a2lq_back has no float instruction at all: weights Q2.29, activations Q8.23, 64-bit accumulators (the
 * pi32v2 "r1_r0 += r2 * r3 (s)" multiply-accumulate). Ranges measured on the A2 model with a guitar
 * (nam/test/a2lite_ranges.c): weights within +-1.4, activations within +-21, head sum within +-41.
 *
 * Core 0 converts at the boundary (a2lx.c): the front's lin / hsum and the input to Q8.23, the output back. */
#pragma once
#include <stdint.h>
#include "a2lite.h"

#define A2LQ_AF 23                               /* activation fraction bits (Q8.23) */
#define A2LQ_WF 29                               /* weight fraction bits (Q2.29) */
#define A2LQ_LAYERS (A2L_LAYERS - A2L_SPLIT)

typedef int32_t a2lq_t;

struct a2lq_layer {
    uint16_t k, d;
    uint32_t mask;
    a2lq_t *ring;                                /* the float layer's ring storage, reused (same size) */
    int32_t conv_w[15][A2L_C][A2L_C];            /* [tap][out][in], Q2.29 */
    int64_t conv_b[A2L_C];                       /* Q.52 (weight x activation scale) */
    int32_t mixin[A2L_C];                        /* Q2.29 */
    int32_t l1_w[A2L_C][A2L_C];                  /* [out][in], Q2.29 */
    int64_t l1_b[A2L_C];                         /* Q.52 */
};

struct a2lq {
    struct a2lq_layer L[A2LQ_LAYERS];
    int32_t head_w[A2L_HEAD_K][A2L_C];           /* Q2.29 */
    int64_t head_b;                              /* Q.52 */
    int32_t head_scale;                          /* Q2.29 */
    uint32_t pos;                                /* frames done (ring write position) */
    a2lq_t head_ring[(A2L_HEAD_RING + A2L_BLOCK_MAX) * A2L_C];
};

/* integer only (core 1): in, lin, hsum Q8.23 (frames, frames x C, frames x C; lin and hsum are updated in place),
 * out Q8.23 (frames) */
extern int64_t a2lq_z[A2L_BLOCK_MAX * A2L_C];    /* its scratch (core 1's write windows need the address) */
void a2lq_back(struct a2lq *q, const a2lq_t *in, a2lq_t *lin, a2lq_t *hsum, a2lq_t *out, uint32_t frames);
