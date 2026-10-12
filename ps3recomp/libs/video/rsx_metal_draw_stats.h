#ifndef PS3RECOMP_RSX_METAL_DRAW_STATS_H
#define PS3RECOMP_RSX_METAL_DRAW_STATS_H

/* Guest-draw counting for the Metal backend's overlay metrics. Pure (no Metal
 * types) so it is unit-tested headless.
 *
 * With PS3_METAL_PER_DRAW_RT=1 the per-frame record list also carries clears
 * and surface blits, ordered with the draws; they -- and draws whose RSX
 * primitive has no Metal mapping -- are marked with prim = 0xFFFFFFFF and are
 * never drawn as geometry. Only records with a real primitive are guest
 * draws. `prim` is an MTLPrimitiveType (NSUInteger). */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define RSX_METAL_REC_NO_GEOMETRY 0xFFFFFFFFul

static inline uint32_t rsx_metal_count_geometry_records(const void* first_prim,
                                                        size_t stride, uint32_t count)
{
    const unsigned char* p = (const unsigned char*)first_prim;
    uint32_t n = 0;

    if (p == NULL)
        return 0;
    for (uint32_t i = 0; i < count; ++i, p += stride) {
        unsigned long prim;
        memcpy(&prim, p, sizeof(prim));
        n += prim != RSX_METAL_REC_NO_GEOMETRY;
    }
    return n;
}

#endif
