#include "ps3_timebase_scale.h"

/* Overflow-safe ns → 79.8 MHz. Same split the Windows/QPC path uses.
 * Never returns 0: guest mftb sites spin `beq` until nonzero. */
uint64_t ps3_timebase_from_delta_ns(uint64_t d)
{
    uint64_t t = (d / 1000000000ULL) * PS3_TIMEBASE_FREQ
               + ((d % 1000000000ULL) * PS3_TIMEBASE_FREQ) / 1000000000ULL;
    return t ? t : 1ull;
}
