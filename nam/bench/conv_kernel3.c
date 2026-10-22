/* Fixed-point variant using the pi32v2 64-bit multiply-accumulate builtin. */
#include <stdint.h>

#define CH 3
#define K 6
#define RING 2048

void conv_q31_mac(const int32_t *restrict hist, uint32_t t, uint32_t d,
                  const int32_t *restrict w, const int64_t *restrict bias, int32_t *restrict out)
{
    long long a0 = bias[0], a1 = bias[1], a2 = bias[2];
    for (int k = 0; k < K; k++) {
        const int32_t *x = hist + ((t - k * d) & (RING - 1)) * CH;
        const int32_t *wk = w + k * CH * CH;
        for (int i = 0; i < CH; i++) {
            int32_t xi = x[i];
            a0 += (int64_t)wk[i * CH + 0] * xi;
            a1 += (int64_t)wk[i * CH + 1] * xi;
            a2 += (int64_t)wk[i * CH + 2] * xi;
        }
    }
    out[0] = (int32_t)(a0 >> 31); out[1] = (int32_t)(a1 >> 31); out[2] = (int32_t)(a2 >> 31);
}
