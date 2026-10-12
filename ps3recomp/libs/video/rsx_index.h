/*
 * Guest RSX index → host uint32. PS3 stores indices big-endian; 0xFFFF in a
 * 16-bit buffer is primitive restart, not vertex 65535. Metal's 32-bit IB
 * restart sentinel is 0xFFFFFFFF (Apple: 16-bit restart is 0xFFFF, 32-bit
 * is 0xFFFFFFFF). Expanding u16→u32 without this mapping draws a spike to
 * vertex 65535.
 */
#ifndef PS3RECOMP_RSX_INDEX_H
#define PS3RECOMP_RSX_INDEX_H

#include "ps3emu/ps3types.h"
#include <string.h>

#define RSX_INDEX_RESTART 0xFFFFFFFFu

static inline u32 rsx_index_from_guest(const u8* src, int is_16bit)
{
    if (is_16bit) {
        u16 v;
        memcpy(&v, src, 2);
        u32 idx = (u32)((v >> 8) | ((v & 0xFFu) << 8));
        return (idx == 0xFFFFu) ? RSX_INDEX_RESTART : idx;
    }
    u32 v;
    memcpy(&v, src, 4);
    v = ((v >> 24) & 0xFFu) | ((v >> 8) & 0xFF00u) |
        ((v << 8) & 0xFF0000u) | ((v << 24) & 0xFF000000u);
    return v;
}

static inline int rsx_index_is_restart(u32 idx)
{
    return idx == RSX_INDEX_RESTART;
}

/* NV4097 DRAW_ARRAYS / DRAW_INDEX_ARRAY pack count-1 in 8 bits, so one FIFO
 * word is at most 256 vertices/indices. Consecutive packets with
 * first == prev_first+prev_count are the same primitive-assembler run (BEGIN
 * still open), not new draws. Applies to every topology: a triangle list
 * restarted every 256 verts (256 % 3 == 1) joins vertices of different
 * primitives into screen-sized planes; strips/fans/loops continue across
 * packets even when count divides the group size. */
static inline int rsx_draw_batch_continues(u32 prim, u32 first, u32 count,
                                           u32 prev_prim, u32 prev_first,
                                           u32 prev_count)
{
    (void)count;
    if (prev_count == 0 || prim != prev_prim)
        return 0;
    return first == prev_first + prev_count;
}

static inline int rsx_index_batch_continues(u32 prim, u32 first, u32 count,
                                            u32 prev_prim, u32 prev_first,
                                            u32 prev_count)
{
    return rsx_draw_batch_continues(prim, first, count,
                                    prev_prim, prev_first, prev_count);
}

#endif
