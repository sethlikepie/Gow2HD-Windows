/* Drives the shipped parser used by cellGcm overflow/flip waits. */
#include "gcm_wait_ns.h"
#include <stdio.h>

#define CHECK(cond) do { \
    if (!(cond)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); return 1; } \
} while (0)

int main(void)
{
    CHECK(ps3_gcm_wait_ns_parse(NULL) == 500000ull);
    CHECK(ps3_gcm_wait_ns_parse("") == 500000ull);
    CHECK(ps3_gcm_wait_ns_parse("nope") == 500000ull);
    CHECK(ps3_gcm_wait_ns_parse("0") == 0ull);
    CHECK(ps3_gcm_wait_ns_parse("50000") == 50000ull);
    CHECK(ps3_gcm_wait_ns_parse("500000") == 500000ull);
    CHECK(ps3_gcm_ns_to_mach_delta(0, 1, 1) == 0);
    CHECK(ps3_gcm_ns_to_mach_delta(1000, 0, 1) == 0);
    CHECK(ps3_gcm_ns_to_mach_delta(1000, 1, 0) == 0);
    CHECK(ps3_gcm_ns_to_mach_delta(1000, 1, 1) == 1000);
    CHECK(ps3_gcm_ns_to_mach_delta(500000, 1, 1) == 500000);
    /* Common 125/3 timebase: ticks = ns * 3 / 125. */
    CHECK(ps3_gcm_ns_to_mach_delta(125, 125, 3) == 3);
    CHECK(ps3_gcm_ns_to_mach_delta(500000, 125, 3) == 12000);
    ps3_gcm_sleep_ns(0); /* no-op, must not hang */
    return 0;
}
