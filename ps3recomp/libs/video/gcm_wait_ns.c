#include "gcm_wait_ns.h"
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <mach/mach_time.h>
#else
#include <time.h>
#endif

u64 ps3_gcm_wait_ns_parse(const char* e)
{
    if (!e || !*e)
        return PS3_GCM_WAIT_NS_DEFAULT;
    char* end = NULL;
    unsigned long long v = strtoull(e, &end, 10);
    if (end == e)
        return PS3_GCM_WAIT_NS_DEFAULT;
    return (u64)v;
}

u64 ps3_gcm_ns_to_mach_delta(u64 ns, u32 numer, u32 denom)
{
    if (!ns || !numer || !denom)
        return 0;
    return (ns / numer) * (u64)denom + ((ns % numer) * (u64)denom) / numer;
}

#ifdef _WIN32
#include <windows.h>
#include <stdlib.h>
/* winport v29-gcm: WINPORT_GCM_HRT=1 -> exact sub-ms sleeps via a high-resolution waitable timer
 * (see work/winport/engine_fixes_v29_gcm.py). Returns 0 when off or unavailable (caller falls back to Sleep). */
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif
int wp_gcm_hrt_on(void)
{
    static int on = -1;
    if (on < 0) { const char* e = getenv("WINPORT_GCM_HRT"); on = (e && e[0] == '1') ? 1 : 0; }
    return on;
}
int wp_hrt_sleep_ns(unsigned long long ns)
{
    static __thread HANDLE t = NULL; static __thread int bad = 0;
    if (!wp_gcm_hrt_on() || bad) return 0;
    if (!t) { t = CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
              if (!t) { bad = 1; return 0; } }
    LARGE_INTEGER due; due.QuadPart = -(LONGLONG)(ns / 100ull ? ns / 100ull : 1ull);
    if (!SetWaitableTimer(t, &due, 0, NULL, NULL, FALSE)) { bad = 1; return 0; }
    WaitForSingleObject(t, INFINITE);
    return 1;
}
#endif
void ps3_gcm_sleep_ns(u64 ns)
{
    if (!ns)
        return;
#ifdef _WIN32
    if (wp_hrt_sleep_ns(ns)) return;   /* winport v29-gcm */
    Sleep((DWORD)((ns + 999999ull) / 1000000ull));
#elif defined(__APPLE__)
    static mach_timebase_info_data_t tb;
    static int inited;
    if (!inited) {
        (void)mach_timebase_info(&tb);
        inited = 1;
    }
    const u64 now = mach_absolute_time();
    const u64 dt = ps3_gcm_ns_to_mach_delta(ns, tb.numer, tb.denom);
    (void)mach_wait_until(now + dt);
#else
    struct timespec ts;
    ts.tv_sec = (time_t)(ns / 1000000000ull);
    ts.tv_nsec = (long)(ns % 1000000000ull);
    nanosleep(&ts, NULL);
#endif
}
