#ifndef PS3RECOMP_GCM_GROW_CMD_H
#define PS3RECOMP_GCM_GROW_CMD_H

/*
 * Double the guest command buffer when IO has room, so overflow (current==end)
 * fires less often. GoW2 asks for 2 MB in a 48 MB IO window; 4 MB still
 * leaves >= 3/4 of IO for non-FIFO mappings.
 *
 * PS3_GCM_GROW_CMD=1 enables. Default OFF: e435cmd (2 MB→4 MB) measured
 * fps_median=20 vs w64=29 — overflow drained twice the FIFO, decode_ms~11.
 */
#include <stdint.h>

static inline uint32_t ps3_gcm_grow_cmd_size(uint32_t cmdSize, uint32_t ioSize)
{
    uint32_t twice;
    if (cmdSize < 0x10000u)
        cmdSize = 0x10000u;
    if (ioSize < cmdSize)
        return cmdSize;
    twice = cmdSize * 2u;
    if (twice / 2u != cmdSize)
        return cmdSize;
    if (twice > ioSize / 4u)
        return cmdSize;
    return twice;
}

static inline int ps3_gcm_grow_cmd_parse(const char *e)
{
    if (e && e[0] == '1')
        return 1;
    return 0;
}

#endif
