#ifndef PS3RECOMP_GCM_WAIT_NS_H
#define PS3RECOMP_GCM_WAIT_NS_H

#include "ps3emu/ps3types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Overflow/flip-wait sleep. Default 500000 ns (the console-facing 0.5 ms
 * slice). PS3_GCM_WAIT_NS overrides; 0 = no sleep (still kick the FIFO). */
#define PS3_GCM_WAIT_NS_DEFAULT 500000ull

u64 ps3_gcm_wait_ns_parse(const char* e);

/* Mach ticks for `ns` given mach_timebase_info numer/denom.
 * ns = ticks * numer / denom  =>  ticks = ns * denom / numer. */
u64 ps3_gcm_ns_to_mach_delta(u64 ns, u32 numer, u32 denom);

/* Sleep `ns`. 0 is a no-op. Apple uses mach_wait_until (sub-ms);
 * nanosleep on Darwin often overshoots a 500 µs overflow slice. */
void ps3_gcm_sleep_ns(u64 ns);

#ifdef __cplusplus
}
#endif

#endif
