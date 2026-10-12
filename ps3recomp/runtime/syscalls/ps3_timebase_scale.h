#ifndef PS3_TIMEBASE_SCALE_H
#define PS3_TIMEBASE_SCALE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PS3_TIMEBASE_FREQ  79800000ULL

uint64_t ps3_timebase_from_delta_ns(uint64_t d_ns);

#ifdef __cplusplus
}
#endif

#endif
