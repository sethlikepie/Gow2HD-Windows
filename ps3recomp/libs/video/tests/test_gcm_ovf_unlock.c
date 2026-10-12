/* Drives ps3_gcm_overflow_fifo_unlocked_parse used by overflow wait. */
#include "gcm_ovf_unlock.h"
#include <stdio.h>

#define CHECK(cond) do { \
    if (!(cond)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); return 1; } \
} while (0)

int main(void)
{
    CHECK(ps3_gcm_overflow_fifo_unlocked_parse(NULL) == 0);
    CHECK(ps3_gcm_overflow_fifo_unlocked_parse("") == 0);
    CHECK(ps3_gcm_overflow_fifo_unlocked_parse("nope") == 0);
    CHECK(ps3_gcm_overflow_fifo_unlocked_parse("0") == 0);
    CHECK(ps3_gcm_overflow_fifo_unlocked_parse("1") == 1);
    printf("PASS\n");
    return 0;
}
