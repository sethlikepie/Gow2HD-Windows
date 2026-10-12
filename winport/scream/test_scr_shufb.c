/* test_scr_shufb.c -- scr_shufb (SSE) == spu_shufb_ref on random + edge selectors. */
#include "scr_native.h"
#include "spu_helpers.h"
#include <stdio.h>
#include <stdlib.h>
int main(void)
{
    unsigned long long s = 0x9E3779B97F4A7C15ull, bad = 0;
    for (int it = 0; it < 2000000; it++) {
        u128 a, b, c;
        for (int i = 0; i < 4; i++) {
            s ^= s << 13; s ^= s >> 7; s ^= s << 17; a._u32[i] = (uint32_t)s;
            s ^= s << 13; s ^= s >> 7; s ^= s << 17; b._u32[i] = (uint32_t)s;
            s ^= s << 13; s ^= s >> 7; s ^= s << 17; c._u32[i] = (uint32_t)s;
        }
        if (it & 1) for (int i = 0; i < 4; i++) c._u32[i] &= 0x1F1F1F1Fu;   /* mostly plain selects */
        u128 r1 = spu_shufb_ref(a, b, c), r2 = scr_shufb(a, b, c);
        if (memcmp(&r1, &r2, 16)) { if (bad++ < 5) printf("MISMATCH it=%d\n", it); }
    }
    printf("scr_shufb vs spu_shufb_ref: %llu mismatches\n", bad);
    return bad != 0;
}
