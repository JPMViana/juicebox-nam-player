/* Juice-Box: the math.h that Onyx's engine needs, without libm: isfinite and lrintf (round half to even, as libm's). */
#ifndef JB_SHIM_MATH_H
#define JB_SHIM_MATH_H
#define isfinite(x) __builtin_isfinite(x)
static inline long lrintf(float x)
{
    const float f = x >= 0.0f ? x + 0.5f : x - 0.5f;
    long r = (long)f;
    if ((float)r == f && (r & 1L))       /* exactly halfway: to even */
        r += x >= 0.0f ? -1L : 1L;
    return r;
}
#endif
