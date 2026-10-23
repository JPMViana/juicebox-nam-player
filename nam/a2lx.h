/* SPDX-License-Identifier: GPL-3.0-only
 * Juice-Box (João Viana): the mixed A2-Lite (float front on core 0, fixed-point back on core 1). See a2lx.c. */
#pragma once
#include "a2lite.h"
#include "a2lq.h"

void a2lx_load(struct a2lq *q, const struct a2l *m);     /* after a2l_load: the back half's weights to fixed point */
void a2lx_reset(struct a2l *m, struct a2lq *q);          /* zero all history, prewarm through the mixed path */
void a2lx_process(struct a2l *m, struct a2lq *q, const float *in, float *out, uint32_t frames);   /* one core */
void a2lx_to_q(const float *x, a2lq_t *q, uint32_t n);
void a2lx_from_q(const a2lq_t *q, float *x, uint32_t n);
