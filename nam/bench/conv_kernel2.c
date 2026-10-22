/* Variant of conv_kernel.c with the loop order an embedded A2 kernel would use:
 * all CH outputs accumulate together, so each history value is loaded once
 * and the accumulators stay in registers. Also a fused-multiply-add probe. */
#include <stdint.h>

#define CH 3
#define K 6
#define RING 2048

void conv_f32_v2(const float *restrict hist, uint32_t t, uint32_t d,
                 const float *restrict w, const float *restrict bias, float *restrict out)
{
    float a0 = bias[0], a1 = bias[1], a2 = bias[2];
    /* w layout: [k][i][o], so the 3 weights for one input value are adjacent */
    for (int k = 0; k < K; k++) {
        const float *x = hist + ((t - k * d) & (RING - 1)) * CH;
        const float *wk = w + k * CH * CH;
        for (int i = 0; i < CH; i++) {
            float xi = x[i];
            a0 += wk[i * CH + 0] * xi;
            a1 += wk[i * CH + 1] * xi;
            a2 += wk[i * CH + 2] * xi;
        }
    }
    out[0] = a0; out[1] = a1; out[2] = a2;
}

float fma_probe(float acc, float a, float b)
{
    return __builtin_fmaf(a, b, acc);
}

/* 1x1 mix (the A2 layer1x1 / bottleneck shape): y = W x + b, 3x3 */
void mix3_f32(const float *restrict w, const float *restrict b, const float *restrict x, float *restrict y)
{
    for (int o = 0; o < CH; o++)
        y[o] = b[o] + w[o * 3 + 0] * x[0] + w[o * 3 + 1] * x[1] + w[o * 3 + 2] * x[2];
}
