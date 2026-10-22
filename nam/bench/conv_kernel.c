/* Inner loops of a NAM A2-Lite layer (channels = 3, kernel = 6, dilated), in the
 * two number formats we are weighing for the pi32v2: fp32 and Q15 with a 32-bit
 * accumulator. Compiled to assembly only, to count instructions per MAC before
 * we can time it on the pedal.
 *
 * Layout: history is a ring of frames, CH values each; the caller passes the
 * index of the newest frame and the dilation. */
#include <stdint.h>

#define CH 3
#define K 6
#define RING 2048 /* > (K - 1) * max dilation (239) * ... power of two for masking */

/* out[o] = bias[o] + sum_k sum_i w[o][k][i] * hist[t - k*d][i] */
void conv_f32(const float *restrict hist, uint32_t t, uint32_t d,
              const float *restrict w, const float *restrict bias, float *restrict out)
{
    for (int o = 0; o < CH; o++) {
        float acc = bias[o];
        for (int k = 0; k < K; k++) {
            const float *x = hist + ((t - k * d) & (RING - 1)) * CH;
            const float *wk = w + (o * K + k) * CH;
            acc += wk[0] * x[0] + wk[1] * x[1] + wk[2] * x[2];
        }
        out[o] = acc;
    }
}

void conv_q15(const int16_t *restrict hist, uint32_t t, uint32_t d,
              const int16_t *restrict w, const int32_t *restrict bias, int16_t *restrict out)
{
    for (int o = 0; o < CH; o++) {
        int32_t acc = bias[o];
        for (int k = 0; k < K; k++) {
            const int16_t *x = hist + ((t - k * d) & (RING - 1)) * CH;
            const int16_t *wk = w + (o * K + k) * CH;
            acc += wk[0] * x[0] + wk[1] * x[1] + wk[2] * x[2];
        }
        acc >>= 15;
        out[o] = acc > 32767 ? 32767 : acc < -32768 ? -32768 : (int16_t)acc;
    }
}

/* LeakyReLU(0.01), the A2 activation */
void leaky_f32(float *x, int n)
{
    for (int i = 0; i < n; i++)
        if (x[i] < 0.0f)
            x[i] *= 0.01f;
}
