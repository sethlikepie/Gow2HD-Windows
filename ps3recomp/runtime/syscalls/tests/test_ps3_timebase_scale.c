/* Drives the shipped ns→79.8 MHz scaler used by ps3_timebase_now / mftb. */
#include "../ps3_timebase_scale.h"
#include <stdio.h>

#define CHECK(cond) do { \
    if (!(cond)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); return 1; } \
} while (0)

int main(void)
{
    CHECK(ps3_timebase_from_delta_ns(0) == 1ull); /* never zero */
    CHECK(ps3_timebase_from_delta_ns(1000000000ull) == PS3_TIMEBASE_FREQ);
    CHECK(ps3_timebase_from_delta_ns(500000000ull) == PS3_TIMEBASE_FREQ / 2ull);
    CHECK(ps3_timebase_from_delta_ns(2000000000ull) == 2ull * PS3_TIMEBASE_FREQ);
    /* 12.5 ms ≈ one 80 Hz slice: 79800000 * 125 / 10000 = 997500 */
    CHECK(ps3_timebase_from_delta_ns(12500000ull) == 997500ull);
    return 0;
}
