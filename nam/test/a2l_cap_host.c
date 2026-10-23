/* Host side of the pedal capture (tools/jb_capture.py, JBW): runs the captured input blocks through the mixed
 * A2-Lite (float front + fixed-point back, as on the pedal, from a2lx_reset) and the all-float one.
 *   a2l_cap_host WEIGHTS.f32 IN.f32 OUT_MIXED.f32 OUT_FLOAT.f32 */
#include <stdio.h>
#include "../a2lx.h"

static struct a2l mf, mx;
static struct a2lq q;
static a2l_t ring_f[A2L_RING_FRAMES * A2L_C], ring_x[A2L_RING_FRAMES * A2L_C];

int main(int argc, char **argv)
{
    static float w[A2L_NWEIGHTS + 1];
    float bi[A2L_BLOCK_MAX], bf[A2L_BLOCK_MAX], bx[A2L_BLOCK_MAX];
    size_t n;
    FILE *fw, *fi, *fx, *ff;
    if (argc < 5)
        return fprintf(stderr, "usage: a2l_cap_host WEIGHTS.f32 IN.f32 OUT_MIXED.f32 OUT_FLOAT.f32\n"), 2;
    fw = fopen(argv[1], "rb");
    n = fread(w, sizeof(float), A2L_NWEIGHTS + 1, fw);
    if (a2l_load(&mf, w, (uint32_t)n, ring_f) || a2l_load(&mx, w, (uint32_t)n, ring_x))
        return fprintf(stderr, "load failed\n"), 1;
    a2lx_load(&q, &mx);
    a2lx_reset(&mx, &q);
    fi = fopen(argv[2], "rb");
    fx = fopen(argv[3], "wb");
    ff = fopen(argv[4], "wb");
    while ((n = fread(bi, sizeof(float), A2L_BLOCK_MAX, fi)) > 0) {
        a2lx_process(&mx, &q, bi, bx, (uint32_t)n);
        a2l_process(&mf, bi, bf, (uint32_t)n);
        fwrite(bx, sizeof(float), n, fx);
        fwrite(bf, sizeof(float), n, ff);
    }
    return 0;
}
