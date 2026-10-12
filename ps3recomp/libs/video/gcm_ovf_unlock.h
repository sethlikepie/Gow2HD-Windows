#ifndef PS3RECOMP_GCM_OVF_UNLOCK_H
#define PS3RECOMP_GCM_OVF_UNLOCK_H

/*
 * Overflow wait: drain the FIFO with the giant lock released so other PPU
 * threads run. On the console the RSX consumes the ring concurrently; the
 * walker does not execute guest code (s_walking already serializes drains).
 *
 * PS3_GCM_OVF_UNLOCK_FIFO=1 drains unlocked. Default OFF: e435oul
 * (default ON) measured fps_median=18 vs w64=29 — decode_ms rose to ~12.5.
 */
#include <stdint.h>

static inline int ps3_gcm_overflow_fifo_unlocked_parse(const char *e)
{
    if (e && e[0] == '1')
        return 1;
    return 0;
}

#endif
