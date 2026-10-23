/* Host test: run nam/a2lite.c over a raw float32 file in blocks of 128 frames.
 *   a2lite_host WEIGHTS.f32 IN.f32 OUT.f32 */
#include <stdio.h>
#include <stdlib.h>
#include "../a2lite.h"

static struct a2l model;
static a2l_t ring[A2L_RING_FRAMES * A2L_C];

int main(int argc, char **argv)
{
    static float w[A2L_NWEIGHTS + 1];
    float buf_in[A2L_BLOCK_MAX], buf_out[A2L_BLOCK_MAX];
    FILE *fw, *fi, *fo;
    size_t n;
    if (argc != 4) {
        fprintf(stderr, "usage: a2lite_host WEIGHTS.f32 IN.f32 OUT.f32\n");
        return 2;
    }
    fw = fopen(argv[1], "rb");
    n = fread(w, sizeof(float), A2L_NWEIGHTS + 1, fw);
    if (a2l_load(&model, w, (uint32_t)n, ring)) {
        fprintf(stderr, "load failed (%zu weights)\n", n);
        return 1;
    }
    fprintf(stderr, "prewarm %u samples\n", model.prewarm);
    fi = fopen(argv[2], "rb");
    fo = fopen(argv[3], "wb");
    while ((n = fread(buf_in, sizeof(float), A2L_BLOCK_MAX, fi)) > 0) {
        a2l_process(&model, buf_in, buf_out, (uint32_t)n);
        fwrite(buf_out, sizeof(float), n, fo);
    }
    fclose(fo);
    return 0;
}
