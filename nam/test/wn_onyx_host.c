/* Host test: Onyx's WaveNet block engine (fw/onyx, accelerator in its software model) against our reference.
 *   wn_onyx_host WEIGHTS.f32 IN.f32 OUT.f32 [FLOAT.f32]   (64-sample blocks; the raw model output, no conditioning) */
#define PA_HOST_BUILD 1
#include <stdio.h>
#include <stdlib.h>
#include "../../fw/onyx/src/nam/pa_wavenet.c"
#include "../../fw/onyx/src/dsp/hw_accel.c"

static pa_wavenet_model_t m;

int main(int argc, char **argv)
{
    static float w[PA_WN_LITE_WEIGHTS + 1];
    float x[PA_WN_BLK_N], y[PA_WN_BLK_N], yf[PA_WN_BLK_N];
    size_t n;
    double maxd = 0;
    if (argc < 4) { fprintf(stderr, "usage: wn_onyx_host WEIGHTS.f32 IN.f32 OUT.f32\n"); return 2; }
    FILE *fw = fopen(argv[1], "rb"), *fi = fopen(argv[2], "rb"), *fo = fopen(argv[3], "wb"), *ff = argc > 4 ? fopen(argv[4], "wb") : 0;
    n = fread(w, sizeof(float), PA_WN_LITE_WEIGHTS + 1, fw);
    pa_hw_accel_init();
    int rc = pa_wavenet_load_weights(&m, w, (uint32_t)n, 1.0f);
    fprintf(stderr, "load %zu weights: rc %d, blk_ok %d\n", n, rc, m.blk_ok);
    if (rc || !m.blk_ok) return 1;
    static pa_wavenet_model_t mf;                         /* the float reference engine, same weights */
    pa_wavenet_load_weights(&mf, w, (uint32_t)n, 1.0f);
    while ((n = fread(x, sizeof(float), PA_WN_BLK_N, fi)) == PA_WN_BLK_N) {
        wn_block_run(&m, x, y, (uint32_t)n);
        for (size_t i = 0; i < n; i++) {
            yf[i] = pa_wavenet_step_sample(&mf, x[i]);
            double d = y[i] - yf[i]; if (d < 0) d = -d; if (d > maxd) maxd = d;
        }
        fwrite(y, sizeof(float), n, fo);
        if (ff) fwrite(yf, sizeof(float), n, ff);
    }
    fclose(fo);
    fprintf(stderr, "max |block - float engine| = %.3g\n", maxd);
    return 0;
}
