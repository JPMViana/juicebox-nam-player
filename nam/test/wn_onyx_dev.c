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
    {   /* the state, as the pedal's JBO 4 (fw/src/jb_onyx.c onyx_state) */
        uint32_t col = 0;
        for (int l = 0; l < PA_WN_NUM_LAYERS; l++) {
            const uint32_t w = 3u * (wn_R(l) + wn_MIR(l));
            uint32_t sum = 0;
            for (uint32_t i = 0; i < w; i++) sum = sum * 31u + (uint32_t)m.ring_q[3u * col + i];
            printf("%u %u\n", sum, (unsigned)m.ring_pos[l]);
            col += wn_R(l) + wn_MIR(l);
        }
        uint32_t h = 0;
        for (uint32_t i = 0; i < 3u * PA_WN_BLK_HEAD_STEPS; i++) h = h * 31u + (uint32_t)m.head_q[i];
        printf("%u\n", h);
    }
    FILE *fo = fopen(argv[3], "wb");
    fwrite(y, sizeof(float), PA_WN_BLK_N, fo);
    fclose(fo);
    return 0;
}
