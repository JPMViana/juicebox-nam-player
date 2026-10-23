/* Host: run the A2-Lite reference over a file and print the largest |value| per layer of the
 * layer input (lin), the activation (z after LeakyReLU) and the head sum: the fixed-point budget. */
#include <stdio.h>
#include <math.h>
#include "../a2lite.h"
#include "../a2lite.c"

int main(int argc, char **argv)
{
    static struct a2l m; static a2l_t ring[A2L_RING_FRAMES * A2L_C];
    static float w[A2L_NWEIGHTS + 1], in[A2L_BLOCK_MAX], out[A2L_BLOCK_MAX];
    static a2l_t lin[A2L_BLOCK_MAX * 3], hsum[A2L_BLOCK_MAX * 3], z[A2L_BLOCK_MAX * 3];
    float mlin[A2L_LAYERS + 1] = {0}, mz[A2L_LAYERS] = {0}, mh = 0, mo = 0, mw = 0;
    FILE *f = fopen(argv[1], "rb"); size_t n = fread(w, 4, A2L_NWEIGHTS + 1, f), i, li, k;
    a2l_load(&m, w, (uint32_t)n, ring);
    for (i = 0; i < A2L_NWEIGHTS; i++) if (fabsf(w[i]) > mw) mw = fabsf(w[i]);
    f = fopen(argv[2], "rb");
    while ((n = fread(in, 4, A2L_BLOCK_MAX, f)) > 0) {
        /* front by hand with per-layer probes */
        for (i = 0; i < n; i++) { lin[i*3]=m.rechannel[0]*in[i]; lin[i*3+1]=m.rechannel[1]*in[i]; lin[i*3+2]=m.rechannel[2]*in[i];
                                  hsum[i*3]=hsum[i*3+1]=hsum[i*3+2]=0; }
        for (li = 0; li < A2L_LAYERS; li++) {
            for (k = 0; k < n * 3; k++) if (fabsf(lin[k]) > mlin[li]) mlin[li] = fabsf(lin[k]);
            float before[A2L_BLOCK_MAX * 3]; for (k = 0; k < n * 3; k++) before[k] = hsum[k];
            layer_run(&m.L[li], li < A2L_SPLIT ? m.pos : m.pos_b, in, lin, hsum, z, (uint32_t)n);
            for (k = 0; k < n * 3; k++) { float a = fabsf(hsum[k] - before[k]); if (a > mz[li]) mz[li] = a; }
        }
        for (k = 0; k < n * 3; k++) { if (fabsf(lin[k]) > mlin[A2L_LAYERS]) mlin[A2L_LAYERS] = fabsf(lin[k]);
                                      if (fabsf(hsum[k]) > mh) mh = fabsf(hsum[k]); }
        m.pos += (uint32_t)n; m.pos_b += (uint32_t)n;
        (void)out;
    }
    printf("max |peso| %.3f\n", mw);
    for (li = 0; li < A2L_LAYERS; li++) printf("camada %2zu: |entrada| %8.3f  |ativacao| %8.3f\n", li, mlin[li], mz[li]);
    printf("saida residual final %.3f, |soma head| %.3f\n", mlin[A2L_LAYERS], mh);
    return 0;
}
