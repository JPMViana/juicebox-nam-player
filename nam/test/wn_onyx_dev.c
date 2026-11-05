/* Host twin of the pedal's JBO test: the same weights and input through Onyx's block engine (software accelerator).
 *   wn_onyx_dev WEIGHTS.f32 BLOCKS OUT.f32      -> OUT = the last block's 64 raw outputs */
#define PA_HOST_BUILD 1
#include <stdio.h>
#include <stdlib.h>
#include "../../fw/onyx/src/nam/pa_wavenet.c"
#include "../../fw/onyx/src/dsp/hw_accel.c"
#include "../../fw/src/jb_onyx_input.h"

static pa_wavenet_model_t m;

int main(int argc, char **argv)
{
    static float w[PA_WN_LITE_WEIGHTS + 1];
    float x[PA_WN_BLK_N], y[PA_WN_BLK_N];
    if (argc != 4) { fprintf(stderr, "usage: wn_onyx_dev WEIGHTS.f32 BLOCKS OUT.f32\n"); return 2; }
    FILE *fw = fopen(argv[1], "rb");
    size_t n = fread(w, sizeof(float), PA_WN_LITE_WEIGHTS + 1, fw);
    pa_hw_accel_init();
    if (pa_wavenet_load_weights(&m, w, (uint32_t)n, 1.0f) || !m.blk_ok) { fprintf(stderr, "load failed\n"); return 1; }
    pa_wavenet_reset(&m);
    in_t = 0; in_seed = 12345u;
    for (long b = atol(argv[2]); b > 0; b--) {
        make_input(x, PA_WN_BLK_N);
        wn_block_run(&m, x, y, PA_WN_BLK_N);
    }
    FILE *fo = fopen(argv[3], "wb");
    fwrite(y, sizeof(float), PA_WN_BLK_N, fo);
    fclose(fo);
    return 0;
}
