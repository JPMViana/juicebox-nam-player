/* On-device microbenchmarks of the NAM inner loop (3x3 tap over 128 frames): float vs Q31 MAC. */
#include <stdint.h>
#define N 128
static float fs[N * 3 + 3], fz[N * 3];
static int32_t qs[N * 3 + 3];
static int64_t qz[N * 3];

uint32_t bench_f32(uint32_t reps, const float *w)
{
    uint32_t r, f, sum = 0;
    for (r = 0; r < reps; r++) {
        const float *s = fs;
        float *z = fz;
        const float w00 = w[0], w01 = w[1], w02 = w[2], w10 = w[3], w11 = w[4], w12 = w[5], w20 = w[6], w21 = w[7], w22 = w[8];
        for (f = 0; f < N; f++, s += 3, z += 3) {
            const float s0 = s[0], s1 = s[1], s2 = s[2];
            z[0] += w00 * s0 + w01 * s1 + w02 * s2;
            z[1] += w10 * s0 + w11 * s1 + w12 * s2;
            z[2] += w20 * s0 + w21 * s1 + w22 * s2;
        }
        sum += (uint32_t)fz[r & 127];
    }
    return sum;
}

uint32_t bench_q31(uint32_t reps, const int32_t *w)
{
    uint32_t r, f, sum = 0;
    for (r = 0; r < reps; r++) {
        const int32_t *s = qs;
        int64_t *z = qz;
        const int32_t w00 = w[0], w01 = w[1], w02 = w[2], w10 = w[3], w11 = w[4], w12 = w[5], w20 = w[6], w21 = w[7], w22 = w[8];
        for (f = 0; f < N; f++, s += 3, z += 3) {
            const int32_t s0 = s[0], s1 = s[1], s2 = s[2];
            z[0] += (int64_t)w00 * s0 + (int64_t)w01 * s1 + (int64_t)w02 * s2;
            z[1] += (int64_t)w10 * s0 + (int64_t)w11 * s1 + (int64_t)w12 * s2;
            z[2] += (int64_t)w20 * s0 + (int64_t)w21 * s1 + (int64_t)w22 * s2;
        }
        sum += (uint32_t)qz[r & 127];
    }
    return sum;
}

/* Q31 variant with 32-bit accumulators (high word of the product: >> 32), per output channel */
uint32_t bench_q31h(uint32_t reps, const int32_t *w)
{
    uint32_t r, f, sum = 0;
    static int32_t z32[N * 3];
    for (r = 0; r < reps; r++) {
        const int32_t *s = qs;
        int32_t *z = z32;
        const int32_t w00 = w[0], w01 = w[1], w02 = w[2], w10 = w[3], w11 = w[4], w12 = w[5], w20 = w[6], w21 = w[7], w22 = w[8];
        for (f = 0; f < N; f++, s += 3, z += 3) {
            const int32_t s0 = s[0], s1 = s[1], s2 = s[2];
            int64_t a0 = (int64_t)w00 * s0 + (int64_t)w01 * s1 + (int64_t)w02 * s2;
            int64_t a1 = (int64_t)w10 * s0 + (int64_t)w11 * s1 + (int64_t)w12 * s2;
            int64_t a2 = (int64_t)w20 * s0 + (int64_t)w21 * s1 + (int64_t)w22 * s2;
            z[0] += (int32_t)(a0 >> 31);
            z[1] += (int32_t)(a1 >> 31);
            z[2] += (int32_t)(a2 >> 31);
        }
        sum += (uint32_t)z32[r & 127];
    }
    return sum;
}

/* float multiply-accumulate instruction (the compiler never emits it): acc += a * b */
#define FMAC(acc, a, b) __asm__("%0 += %1 * %2 (f)" : "+r"(acc) : "r"(a), "r"(b))

uint32_t bench_fmac(uint32_t reps, const float *w)
{
    uint32_t r, f, sum = 0;
    for (r = 0; r < reps; r++) {
        const float *s = fs;
        float *z = fz;
        const float w00 = w[0], w01 = w[1], w02 = w[2], w10 = w[3], w11 = w[4], w12 = w[5], w20 = w[6], w21 = w[7], w22 = w[8];
        for (f = 0; f < N; f++, s += 3, z += 3) {
            const float s0 = s[0], s1 = s[1], s2 = s[2];
            float a0 = z[0], a1 = z[1], a2 = z[2];
            FMAC(a0, w00, s0); FMAC(a1, w10, s0); FMAC(a2, w20, s0);
            FMAC(a0, w01, s1); FMAC(a1, w11, s1); FMAC(a2, w21, s1);
            FMAC(a0, w02, s2); FMAC(a1, w12, s2); FMAC(a2, w22, s2);
            z[0] = a0; z[1] = a1; z[2] = a2;
        }
        sum += (uint32_t)fz[r & 127];
    }
    return sum;
}

/* correctness probe: 1.5 + 2.25 * -3.0 = -5.25 (0xC0A80000) */
uint32_t fmac_check(void)
{
    volatile float va = 1.5f, vb = 2.25f, vc = -3.0f;
    float a = va, b = vb, c = vc;
    union { float f; uint32_t u; } r;
    FMAC(a, b, c);
    r.f = a;
    return r.u;
}

/* MAC latency (1 dependent chain) and throughput (8 independent accumulators), 8000 MACs each */
uint32_t bench_lat(float b, float c)
{
    float a = 0;
    uint32_t i;
    for (i = 0; i < 1000; i++) {
        FMAC(a, b, c); FMAC(a, b, c); FMAC(a, b, c); FMAC(a, b, c);
        FMAC(a, b, c); FMAC(a, b, c); FMAC(a, b, c); FMAC(a, b, c);
    }
    return (uint32_t)a;
}

uint32_t bench_thr(float b, float c)
{
    float a0 = 0, a1 = 0, a2 = 0, a3 = 0, a4 = 0, a5 = 0, a6 = 0, a7 = 0;
    uint32_t i;
    for (i = 0; i < 1000; i++) {
        FMAC(a0, b, c); FMAC(a1, b, c); FMAC(a2, b, c); FMAC(a3, b, c);
        FMAC(a4, b, c); FMAC(a5, b, c); FMAC(a6, b, c); FMAC(a7, b, c);
    }
    return (uint32_t)(a0 + a1 + a2 + a3 + a4 + a5 + a6 + a7);
}

/* the same with plain multiply then add (independent): mul/add throughput */
uint32_t bench_muladd(float b, float c)
{
    float a0 = 0, a1 = 0, a2 = 0, a3 = 0;
    uint32_t i;
    for (i = 0; i < 1000; i++) {
        float p0 = b * c, p1 = b * c, p2 = b * c, p3 = b * c;
        __asm__("" : "+r"(p0), "+r"(p1), "+r"(p2), "+r"(p3));
        a0 += p0; a1 += p1; a2 += p2; a3 += p3;
        p0 = b * c; p1 = b * c; p2 = b * c; p3 = b * c;
        __asm__("" : "+r"(p0), "+r"(p1), "+r"(p2), "+r"(p3));
        a0 += p0; a1 += p1; a2 += p2; a3 += p3;
    }
    return (uint32_t)(a0 + a1 + a2 + a3);
}
