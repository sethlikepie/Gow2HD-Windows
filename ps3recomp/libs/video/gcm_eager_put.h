#ifndef PS3RECOMP_GCM_EAGER_PUT_H
#define PS3RECOMP_GCM_EAGER_PUT_H

/*
 * Publish CellGcmControl.put when guest current has advanced past the last
 * put, so the FIFO walker can drain while the title is still filling the
 * 2 MB command buffer. Overflow then waits on a nearly-empty ring instead
 * of 2 MB of unsubmitted commands.
 *
 * current/begin/end are guest EAs. io is the IO base. put is the published
 * offset (Control.put). On 1, *out_put is the new offset to store.
 */
#include <stdint.h>

static inline int ps3_gcm_eager_put(uint32_t current, uint32_t io, uint32_t put,
                                    uint32_t begin, uint32_t end, uint32_t *out_put)
{
    uint32_t cur_off, begin_off, end_off;
    if (!out_put || io == 0)
        return 0;
    if (begin < io || end <= begin)
        return 0;
    if (current < begin || current > end)
        return 0;
    cur_off = current - io;
    begin_off = begin - io;
    end_off = end - io;
    if (put < begin_off || put > end_off)
        return 0;
    if (cur_off <= put)
        return 0;
    *out_put = cur_off;
    return 1;
}

#endif
