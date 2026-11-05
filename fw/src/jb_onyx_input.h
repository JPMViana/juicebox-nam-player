/* jb_onyx_input.h - the JBO test input, shared by the pedal (fw/src/jb_onyx.c) and the Mac (nam/test/wn_onyx_dev.c) */
#include <stdint.h>
/* the test input, exact in float on any machine (tools/jb_onyx.py makes the same): a 200-sample triangle at 0.375 of full
 * scale plus LCG noise at 1/32, both computed in integers and scaled by powers of two */
static uint32_t in_t, in_seed;
static void make_input(float *x, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++, in_t++) {
        in_seed = in_seed * 1664525u + 1013904223u;
        const int32_t ph = (int32_t)(in_t % 200u), tri = ph < 100 ? ph - 50 : 150 - ph;   /* -50..50 */
        const int32_t v = tri * 3 * 65536 / 50 * 16 + ((int32_t)in_seed >> 13);           /* up to 0.375 of full scale, + noise at 1/32 */
        x[i] = (float)v * (1.0f / 8388608.0f);
    }
}

