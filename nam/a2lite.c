/* SPDX-License-Identifier: GPL-3.0-only
 * Juice-Box (João Viana): NAM A2-Lite forward pass in plain C (see a2lite.h).
 * Mirrors NeuralAmpModelerCore's A2FastModel<3> (NAM/wavenet/a2_fast.cpp): same weight order,
 * same per-layer data flow, same prewarm length (verified sample for sample on the host,
 * nam/test/a2lite_host.c).
 *
 * Speed (v0.8.2): tap-major over the whole block (independent frames: no FPU dependency stalls),
 * rings with a mirrored tail so every tap reads one contiguous run (no index masking per access),
 * last tap fused with mixin, LeakyReLU, head sum and the 1x1 residual. */
#include "a2lite.h"

static const uint8_t K_SIZES[A2L_LAYERS] = {6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 15, 15, 6, 6, 6, 6, 6, 6, 6};
static const uint8_t DILATIONS[A2L_LAYERS] = {1, 3, 7, 17, 41, 101, 239, 1, 3, 7, 17, 41, 101, 239,
                                              1, 13, 1, 3, 7, 17, 41, 101, 239};

int a2l_load(struct a2l *m, const float *w, uint32_t n, a2l_t *ring)
{
    uint32_t li, i, j, k, used = 0, p = 0;
    if (n != A2L_NWEIGHTS)
        return -1;
    for (i = 0; i < A2L_C; i++)                   /* rechannel 1 -> C, no bias */
        m->rechannel[i] = w[p++];
    m->prewarm = 1u + (A2L_HEAD_K - 1u);
    for (li = 0; li < A2L_LAYERS; li++) {
        struct a2l_layer *L = &m->L[li];
        uint32_t size = 1;
        L->k = K_SIZES[li];
        L->d = DILATIONS[li];
        while (size < (uint32_t)(L->k - 1u) * L->d + A2L_BLOCK_MAX)
            size <<= 1;
        L->mask = size - 1u;
        L->ring = ring + used * A2L_C;
        used += size + A2L_BLOCK_MAX;             /* + the mirrored tail */
        m->prewarm += (uint32_t)(L->k - 1u) * L->d;
        for (i = 0; i < A2L_C; i++)               /* conv: for out, for in, for tap */
            for (j = 0; j < A2L_C; j++)
                for (k = 0; k < L->k; k++)
                    L->conv_w[k][i][j] = w[p++];
        for (i = 0; i < A2L_C; i++)
            L->conv_b[i] = w[p++];
        for (i = 0; i < A2L_C; i++)               /* input mixin 1 -> C, no bias */
            L->mixin[i] = w[p++];
        for (i = 0; i < A2L_C; i++)               /* layer1x1: for out, for in */
            for (j = 0; j < A2L_C; j++)
                L->l1_w[i][j] = w[p++];
        for (i = 0; i < A2L_C; i++)
            L->l1_b[i] = w[p++];
    }
    for (j = 0; j < A2L_C; j++)                   /* head rechannel: for in, for tap */
        for (k = 0; k < A2L_HEAD_K; k++)
            m->head_w[k][j] = w[p++];
    m->head_b = w[p++];
    m->head_scale = w[p++];
    if (p != n || used != A2L_RING_FRAMES)
        return -1;
    a2l_reset(m);
    return 0;
}

void a2l_reset(struct a2l *m)
{
    static const a2l_t zeros[A2L_BLOCK_MAX];
    static a2l_t sink[A2L_BLOCK_MAX];
    uint32_t li, i, left;
    for (li = 0; li < A2L_LAYERS; li++)
        for (i = 0; i < (m->L[li].mask + 1u + A2L_BLOCK_MAX) * A2L_C; i++)
            m->L[li].ring[i] = 0;
    for (i = 0; i < (A2L_HEAD_RING + A2L_BLOCK_MAX) * A2L_C; i++)
        m->head_ring[i] = 0;
    m->pos = m->pos_b = 0;
    for (left = m->prewarm; left;) {              /* settle the biases into the history, as NAM's prewarm */
        uint32_t n = left > A2L_BLOCK_MAX ? A2L_BLOCK_MAX : left;
        void (*volatile run)(struct a2l *, const a2l_t *, a2l_t *, uint32_t) = a2l_process;   /* (RAM: far) */
        run(m, zeros, sink, n);
        left -= n;
    }
}

/* write frames x C values at ring position pos (masked), keeping [size, size + BLOCK) a copy of [0, BLOCK) */
A2L_HOT static void ring_write(a2l_t *ring, uint32_t mask, uint32_t pos, const a2l_t *src, uint32_t frames)
{
    uint32_t f, at = pos & mask;
    for (f = 0; f < frames; f++) {
        a2l_t *r = ring + at * A2L_C;
        r[0] = src[f * A2L_C + 0];
        r[1] = src[f * A2L_C + 1];
        r[2] = src[f * A2L_C + 2];
        if (at < A2L_BLOCK_MAX) {                 /* the mirror */
            a2l_t *t = r + (mask + 1u) * A2L_C;
            t[0] = r[0];
            t[1] = r[1];
            t[2] = r[2];
        }
        at = (at + 1u) & mask;
    }
}

/* one layer over the block: history write, taps, mixin, LeakyReLU, head sum, 1x1 residual */
A2L_HOT static void layer_run(struct a2l_layer *L, uint32_t pos, const a2l_t *in, a2l_t *lin, a2l_t *hsum, a2l_t *z,
                              uint32_t frames)
{
    const uint32_t mask = L->mask, K = L->k, D = L->d;
    uint32_t f, k;
    ring_write(L->ring, mask, pos, lin, frames);
    /* tap 0 (the oldest) seeds z with the bias; taps 1..K-2 accumulate */
    for (k = 0; k + 1u < K; k++) {
        const a2l_t *s = L->ring + ((pos - (K - 1u - k) * D) & mask) * A2L_C;
        const a2l_t w00 = L->conv_w[k][0][0], w01 = L->conv_w[k][0][1], w02 = L->conv_w[k][0][2];
        const a2l_t w10 = L->conv_w[k][1][0], w11 = L->conv_w[k][1][1], w12 = L->conv_w[k][1][2];
        const a2l_t w20 = L->conv_w[k][2][0], w21 = L->conv_w[k][2][1], w22 = L->conv_w[k][2][2];
        a2l_t *zf = z;
        if (k == 0) {
            const a2l_t b0 = L->conv_b[0], b1 = L->conv_b[1], b2 = L->conv_b[2];
            for (f = 0; f < frames; f++, s += 3, zf += 3) {
                const a2l_t s0 = s[0], s1 = s[1], s2 = s[2];
                zf[0] = b0 + w00 * s0 + w01 * s1 + w02 * s2;
                zf[1] = b1 + w10 * s0 + w11 * s1 + w12 * s2;
                zf[2] = b2 + w20 * s0 + w21 * s1 + w22 * s2;
            }
        } else {
            for (f = 0; f < frames; f++, s += 3, zf += 3) {
                const a2l_t s0 = s[0], s1 = s[1], s2 = s[2];
                zf[0] += w00 * s0 + w01 * s1 + w02 * s2;
                zf[1] += w10 * s0 + w11 * s1 + w12 * s2;
                zf[2] += w20 * s0 + w21 * s1 + w22 * s2;
            }
        }
    }
    {   /* last tap (offset 0), mixin, LeakyReLU, head sum, 1x1 residual */
        const a2l_t *s = L->ring + (pos & mask) * A2L_C;
        const a2l_t w00 = L->conv_w[K - 1u][0][0], w01 = L->conv_w[K - 1u][0][1], w02 = L->conv_w[K - 1u][0][2];
        const a2l_t w10 = L->conv_w[K - 1u][1][0], w11 = L->conv_w[K - 1u][1][1], w12 = L->conv_w[K - 1u][1][2];
        const a2l_t w20 = L->conv_w[K - 1u][2][0], w21 = L->conv_w[K - 1u][2][1], w22 = L->conv_w[K - 1u][2][2];
        const a2l_t m0 = L->mixin[0], m1 = L->mixin[1], m2 = L->mixin[2];
        const a2l_t (*lw)[A2L_C] = L->l1_w;
        const a2l_t lb0 = L->l1_b[0], lb1 = L->l1_b[1], lb2 = L->l1_b[2];
        a2l_t *zf = z, *hs = hsum, *ln = lin;
        for (f = 0; f < frames; f++, s += 3, zf += 3, hs += 3, ln += 3) {
            const a2l_t s0 = s[0], s1 = s[1], s2 = s[2], x = in[f];
            a2l_t a0 = zf[0] + w00 * s0 + w01 * s1 + w02 * s2 + m0 * x;
            a2l_t a1 = zf[1] + w10 * s0 + w11 * s1 + w12 * s2 + m1 * x;
            a2l_t a2 = zf[2] + w20 * s0 + w21 * s1 + w22 * s2 + m2 * x;
            if (a0 < 0)
                a0 *= (a2l_t)0.01f;
            if (a1 < 0)
                a1 *= (a2l_t)0.01f;
            if (a2 < 0)
                a2 *= (a2l_t)0.01f;
            hs[0] += a0;
            hs[1] += a1;
            hs[2] += a2;
            ln[0] += lb0 + lw[0][0] * a0 + lw[0][1] * a1 + lw[0][2] * a2;
            ln[1] += lb1 + lw[1][0] * a0 + lw[1][1] * a1 + lw[1][2] * a2;
            ln[2] += lb2 + lw[2][0] * a0 + lw[2][1] * a1 + lw[2][2] * a2;
        }
    }
}

/* rechannel + layers [0, A2L_SPLIT): in -> lin, hsum (the second core's input) */
A2L_HOT void a2l_front(struct a2l *m, const a2l_t *in, a2l_t *lin, a2l_t *hsum, uint32_t frames)
{
    static a2l_t z[A2L_BLOCK_MAX * A2L_C];
    const a2l_t r0 = m->rechannel[0], r1 = m->rechannel[1], r2 = m->rechannel[2];
    uint32_t f, li;
    for (f = 0; f < frames; f++) {
        const a2l_t x = in[f];
        lin[f * 3 + 0] = r0 * x;
        lin[f * 3 + 1] = r1 * x;
        lin[f * 3 + 2] = r2 * x;
        hsum[f * 3 + 0] = hsum[f * 3 + 1] = hsum[f * 3 + 2] = 0;
    }
    for (li = 0; li < A2L_SPLIT; li++)
        layer_run(&m->L[li], m->pos, in, lin, hsum, z, frames);
    m->pos += frames;
}

/* layers [A2L_SPLIT, 23) + the head: lin, hsum -> out (own buffers and position: it may run on core 1) */
a2l_t a2l_back_z[A2L_BLOCK_MAX * A2L_C] A2L_C1_DATA;          /* a2l_back's scratch (exported: core 1 writes only known buffers) */
A2L_HOT void a2l_back(struct a2l *m, const a2l_t *in, a2l_t *lin, a2l_t *hsum, a2l_t *out, uint32_t frames)
{
    a2l_t *const z = a2l_back_z;
    const uint32_t pos = m->pos_b;
    uint32_t f, li, k;
    for (li = A2L_SPLIT; li < A2L_LAYERS; li++)
        layer_run(&m->L[li], pos, in, lin, hsum, z, frames);
    /* the head: k = 16, d = 1 conv over the summed activations, tap-major into out */
    ring_write(m->head_ring, A2L_HEAD_RING - 1u, pos, hsum, frames);
    for (f = 0; f < frames; f++)
        out[f] = m->head_b;
    for (k = 0; k < A2L_HEAD_K; k++) {
        const a2l_t *s = m->head_ring + ((pos - (A2L_HEAD_K - 1u - k)) & (A2L_HEAD_RING - 1u)) * A2L_C;
        const a2l_t w0 = m->head_w[k][0], w1 = m->head_w[k][1], w2 = m->head_w[k][2];
        for (f = 0; f < frames; f++, s += 3)
            out[f] += w0 * s[0] + w1 * s[1] + w2 * s[2];
    }
    {
        const a2l_t sc = m->head_scale;
        for (f = 0; f < frames; f++)
            out[f] *= sc;
    }
    m->pos_b = pos + frames;
}

A2L_HOT void a2l_process(struct a2l *m, const a2l_t *in, a2l_t *out, uint32_t frames)
{
    static a2l_t lin[A2L_BLOCK_MAX * A2L_C], hsum[A2L_BLOCK_MAX * A2L_C];
    a2l_front(m, in, lin, hsum, frames);
    a2l_back(m, in, lin, hsum, out, frames);
}
