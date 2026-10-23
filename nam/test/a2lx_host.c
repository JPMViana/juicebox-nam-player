/* Host test: the mixed A2-Lite (float front + fixed-point back, nam/a2lx.c) against the all-float one
 * (nam/a2lite.c, which matches NeuralAmpModelerCore), over a raw float32 file in blocks of 128 frames.
 *   a2lx_host WEIGHTS.f32 IN.f32 [OUT.f32]   (prints the largest difference and the SNR) */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "../a2lx.h"

static struct a2l mf, mx;
static struct a2lq q;
static a2l_t ring_f[A2L_RING_FRAMES * A2L_C], ring_x[A2L_RING_FRAMES * A2L_C];

int main(int argc, char **argv)
{
    static float w[A2L_NWEIGHTS + 1];
    float bi[A2L_BLOCK_MAX], bf[A2L_BLOCK_MAX], bx[A2L_BLOCK_MAX];
    double maxd = 0, se = 0, ss = 0, peak = 0;
    size_t n, total = 0, i;
    FILE *fw, *fi, *fo = NULL;
    if (argc < 3)
        return fprintf(stderr, "usage: a2lx_host WEIGHTS.f32 IN.f32 [OUT.f32]\n"), 2;
    fw = fopen(argv[1], "rb");
    n = fread(w, sizeof(float), A2L_NWEIGHTS + 1, fw);
    if (a2l_load(&mf, w, (uint32_t)n, ring_f) || a2l_load(&mx, w, (uint32_t)n, ring_x))
        return fprintf(stderr, "load failed\n"), 1;
    a2lx_load(&q, &mx);
    a2lx_reset(&mx, &q);
    fi = fopen(argv[2], "rb");
    if (argc > 3)
        fo = fopen(argv[3], "wb");
    while ((n = fread(bi, sizeof(float), A2L_BLOCK_MAX, fi)) > 0) {
        a2l_process(&mf, bi, bf, (uint32_t)n);
        a2lx_process(&mx, &q, bi, bx, (uint32_t)n);
        for (i = 0; i < n; i++) {
            double d = fabs((double)bf[i] - bx[i]);
            if (d > maxd)
                maxd = d;
            se += d * d;
            ss += (double)bf[i] * bf[i];
            if (fabs(bf[i]) > peak)
                peak = fabs(bf[i]);
        }
        if (fo)
            fwrite(bx, sizeof(float), n, fo);
        total += n;
    }
    printf("%zu samples: output peak %.4f, largest difference %.3g, SNR %.1f dB\n", total, peak, maxd,
           10.0 * log10(ss / (se > 0 ? se : 1e-300)));
    return 0;
}
