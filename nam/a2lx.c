/* SPDX-License-Identifier: GPL-3.0-only
 * Juice-Box (João Viana): the mixed A2-Lite, float front (a2lite.c, core 0) + fixed-point back (a2lq.c, core 1).
 * This file runs on core 0 only: the conversions at the boundary, the weight conversion, reset and prewarm. */
#include "a2lx.h"

#define AS ((float)(1u << A2LQ_AF))              /* 2^23 */
#define WS ((float)(1u << A2LQ_WF))              /* 2^29 */

static int32_t q_round(float x)
{
    return (int32_t)(x >= 0.0f ? x + 0.5f : x - 0.5f);
}

void a2lx_to_q(const float *x, a2lq_t *q, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        q[i] = q_round(x[i] * AS);
}

void a2lx_from_q(const a2lq_t *q, float *x, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        x[i] = (float)q[i] * (1.0f / AS);
}

static int64_t bias_q(float b)                   /* Q.52: the activation's Q23 times the weight's 2^29 */
{
    return (int64_t)q_round(b * AS) << A2LQ_WF;
}

void a2lx_load(struct a2lq *q, const struct a2l *m)
{
    uint32_t li, i, j, k;
    for (li = 0; li < A2LQ_LAYERS; li++) {
        const struct a2l_layer *L = &m->L[A2L_SPLIT + li];
        struct a2lq_layer *Q = &q->L[li];
        Q->k = L->k;
        Q->d = L->d;
        Q->mask = L->mask;
        Q->ring = (a2lq_t *)L->ring;             /* the float back layers' rings are not used any more */
        for (k = 0; k < L->k; k++)
            for (i = 0; i < A2L_C; i++)
                for (j = 0; j < A2L_C; j++)
                    Q->conv_w[k][i][j] = q_round(L->conv_w[k][i][j] * WS);
        for (i = 0; i < A2L_C; i++) {
            Q->conv_b[i] = bias_q(L->conv_b[i]);
            Q->mixin[i] = q_round(L->mixin[i] * WS);
            Q->l1_b[i] = bias_q(L->l1_b[i]);
            for (j = 0; j < A2L_C; j++)
                Q->l1_w[i][j] = q_round(L->l1_w[i][j] * WS);
        }
    }
    for (k = 0; k < A2L_HEAD_K; k++)
        for (j = 0; j < A2L_C; j++)
            q->head_w[k][j] = q_round(m->head_w[k][j] * WS);
    q->head_b = bias_q(m->head_b);
    q->head_scale = q_round(m->head_scale * WS);
}

void a2lx_process(struct a2l *m, struct a2lq *q, const float *in, float *out, uint32_t frames)
{
    static float lin[A2L_BLOCK_MAX * A2L_C], hsum[A2L_BLOCK_MAX * A2L_C];
    static a2lq_t qin[A2L_BLOCK_MAX], qlin[A2L_BLOCK_MAX * A2L_C], qhs[A2L_BLOCK_MAX * A2L_C], qout[A2L_BLOCK_MAX];
    a2l_front(m, in, lin, hsum, frames);
    a2lx_to_q(in, qin, frames);
    a2lx_to_q(lin, qlin, frames * A2L_C);
    a2lx_to_q(hsum, qhs, frames * A2L_C);
    a2lq_back(q, qin, qlin, qhs, qout, frames);
    a2lx_from_q(qout, out, frames);
}

void a2lx_reset(struct a2l *m, struct a2lq *q)
{
    static const float zeros[A2L_BLOCK_MAX];
    static float sink[A2L_BLOCK_MAX];
    uint32_t li, i, left;
    for (li = 0; li < A2L_LAYERS; li++)          /* all rings: float 0 and Q 0 are the same bits */
        for (i = 0; i < (m->L[li].mask + 1u + A2L_BLOCK_MAX) * A2L_C; i++)
            m->L[li].ring[i] = 0;
    for (i = 0; i < (A2L_HEAD_RING + A2L_BLOCK_MAX) * A2L_C; i++)
        q->head_ring[i] = 0;
    m->pos = m->pos_b = 0;
    q->pos = 0;
    for (left = m->prewarm; left;) {
        uint32_t n = left > A2L_BLOCK_MAX ? A2L_BLOCK_MAX : left;
        a2lx_process(m, q, zeros, sink, n);
        left -= n;
    }
}
