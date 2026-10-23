/* SPDX-License-Identifier: GPL-3.0-only
 * Juice-Box (João Viana): a2lq_back, the A2-Lite back half in fixed point (see a2lq.h). Integer only: this file
 * must never contain a float type (it runs on core 1). Same data flow as a2lite.c's layer_run / a2l_back. */
#include "a2lq.h"

#define WF A2LQ_WF

int64_t a2lq_z[A2L_BLOCK_MAX * A2L_C] A2L_C1_DATA;

static void ring_write_q(a2lq_t *ring, uint32_t mask, uint32_t pos, const a2lq_t *src, uint32_t frames)
{
    uint32_t f, at = pos & mask;
    for (f = 0; f < frames; f++) {
        a2lq_t *r = ring + at * A2L_C;
        r[0] = src[f * A2L_C + 0];
        r[1] = src[f * A2L_C + 1];
        r[2] = src[f * A2L_C + 2];
        if (at < A2L_BLOCK_MAX) {
            a2lq_t *t = r + (mask + 1u) * A2L_C;
            t[0] = r[0];
            t[1] = r[1];
            t[2] = r[2];
        }
        at = (at + 1u) & mask;
    }
}

/* LeakyReLU(0.01) on Q8.23: x * round(0.01 * 2^32) >> 32 */
static inline int32_t leaky(int32_t a)
{
    return a < 0 ? (int32_t)(((int64_t)a * 42949673) >> 32) : a;
}

static void layer_q(struct a2lq_layer *L, uint32_t pos, const a2lq_t *in, a2lq_t *lin, a2lq_t *hsum, int64_t *z,
                    uint32_t frames)
{
    const uint32_t mask = L->mask, K = L->k, D = L->d;
    uint32_t f, k;
    ring_write_q(L->ring, mask, pos, lin, frames);
    for (k = 0; k + 1u < K; k++) {
        const a2lq_t *s = L->ring + ((pos - (K - 1u - k) * D) & mask) * A2L_C;
        const int32_t w00 = L->conv_w[k][0][0], w01 = L->conv_w[k][0][1], w02 = L->conv_w[k][0][2];
        const int32_t w10 = L->conv_w[k][1][0], w11 = L->conv_w[k][1][1], w12 = L->conv_w[k][1][2];
        const int32_t w20 = L->conv_w[k][2][0], w21 = L->conv_w[k][2][1], w22 = L->conv_w[k][2][2];
        int64_t *zf = z;
        if (k == 0) {
            const int64_t b0 = L->conv_b[0], b1 = L->conv_b[1], b2 = L->conv_b[2];
            for (f = 0; f < frames; f++, s += 3, zf += 3) {
                const int32_t s0 = s[0], s1 = s[1], s2 = s[2];
                zf[0] = b0 + (int64_t)w00 * s0 + (int64_t)w01 * s1 + (int64_t)w02 * s2;
                zf[1] = b1 + (int64_t)w10 * s0 + (int64_t)w11 * s1 + (int64_t)w12 * s2;
                zf[2] = b2 + (int64_t)w20 * s0 + (int64_t)w21 * s1 + (int64_t)w22 * s2;
            }
        } else {
            for (f = 0; f < frames; f++, s += 3, zf += 3) {
                const int32_t s0 = s[0], s1 = s[1], s2 = s[2];
                zf[0] += (int64_t)w00 * s0 + (int64_t)w01 * s1 + (int64_t)w02 * s2;
                zf[1] += (int64_t)w10 * s0 + (int64_t)w11 * s1 + (int64_t)w12 * s2;
                zf[2] += (int64_t)w20 * s0 + (int64_t)w21 * s1 + (int64_t)w22 * s2;
            }
        }
    }
    {   /* last tap (offset 0), mixin, LeakyReLU, head sum, 1x1 residual */
        const a2lq_t *s = L->ring + (pos & mask) * A2L_C;
        const int32_t w00 = L->conv_w[K - 1u][0][0], w01 = L->conv_w[K - 1u][0][1], w02 = L->conv_w[K - 1u][0][2];
        const int32_t w10 = L->conv_w[K - 1u][1][0], w11 = L->conv_w[K - 1u][1][1], w12 = L->conv_w[K - 1u][1][2];
        const int32_t w20 = L->conv_w[K - 1u][2][0], w21 = L->conv_w[K - 1u][2][1], w22 = L->conv_w[K - 1u][2][2];
        const int32_t m0 = L->mixin[0], m1 = L->mixin[1], m2 = L->mixin[2];
        const int32_t (*lw)[A2L_C] = L->l1_w;
        const int64_t lb0 = L->l1_b[0], lb1 = L->l1_b[1], lb2 = L->l1_b[2];
        int64_t *zf = z;
        a2lq_t *hs = hsum, *ln = lin;
        for (f = 0; f < frames; f++, s += 3, zf += 3, hs += 3, ln += 3) {
            const int32_t s0 = s[0], s1 = s[1], s2 = s[2], x = in[f];
            int32_t a0 = (int32_t)((zf[0] + (int64_t)w00 * s0 + (int64_t)w01 * s1 + (int64_t)w02 * s2 + (int64_t)m0 * x) >> WF);
            int32_t a1 = (int32_t)((zf[1] + (int64_t)w10 * s0 + (int64_t)w11 * s1 + (int64_t)w12 * s2 + (int64_t)m1 * x) >> WF);
            int32_t a2 = (int32_t)((zf[2] + (int64_t)w20 * s0 + (int64_t)w21 * s1 + (int64_t)w22 * s2 + (int64_t)m2 * x) >> WF);
            a0 = leaky(a0);
            a1 = leaky(a1);
            a2 = leaky(a2);
            hs[0] += a0;
            hs[1] += a1;
            hs[2] += a2;
            ln[0] += (int32_t)((lb0 + (int64_t)lw[0][0] * a0 + (int64_t)lw[0][1] * a1 + (int64_t)lw[0][2] * a2) >> WF);
            ln[1] += (int32_t)((lb1 + (int64_t)lw[1][0] * a0 + (int64_t)lw[1][1] * a1 + (int64_t)lw[1][2] * a2) >> WF);
            ln[2] += (int32_t)((lb2 + (int64_t)lw[2][0] * a0 + (int64_t)lw[2][1] * a1 + (int64_t)lw[2][2] * a2) >> WF);
        }
    }
}

void a2lq_back(struct a2lq *q, const a2lq_t *in, a2lq_t *lin, a2lq_t *hsum, a2lq_t *out, uint32_t frames)
{
    const uint32_t pos = q->pos;
    uint32_t f, li, k;
    int64_t *const z = a2lq_z;
    for (li = 0; li < A2LQ_LAYERS; li++)
        layer_q(&q->L[li], pos, in, lin, hsum, z, frames);
    /* the head: k = 16, d = 1 conv over the summed activations, tap-major into z (one value per frame) */
    ring_write_q(q->head_ring, A2L_HEAD_RING - 1u, pos, hsum, frames);
    for (f = 0; f < frames; f++)
        z[f] = q->head_b;
    for (k = 0; k < A2L_HEAD_K; k++) {
        const a2lq_t *s = q->head_ring + ((pos - (A2L_HEAD_K - 1u - k)) & (A2L_HEAD_RING - 1u)) * A2L_C;
        const int32_t w0 = q->head_w[k][0], w1 = q->head_w[k][1], w2 = q->head_w[k][2];
        for (f = 0; f < frames; f++, s += 3)
            z[f] += (int64_t)w0 * s[0] + (int64_t)w1 * s[1] + (int64_t)w2 * s[2];
    }
    {
        const int32_t sc = q->head_scale;
        for (f = 0; f < frames; f++)
            out[f] = (int32_t)(((int64_t)(int32_t)(z[f] >> WF) * sc) >> WF);
    }
    q->pos = pos + frames;
}
