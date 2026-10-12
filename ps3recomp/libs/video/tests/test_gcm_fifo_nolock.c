/* Drives ps3_gcm_fifo_nolock_parse used by the RSX wake thread. */
#include "gcm_fifo_nolock.h"
#include <stdio.h>

#define CHECK(cond) do { \
    if (!(cond)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); return 1; } \
} while (0)

int main(void)
{
    CHECK(ps3_gcm_fifo_nolock_parse(NULL) == 0);
    CHECK(ps3_gcm_fifo_nolock_parse("") == 0);
    CHECK(ps3_gcm_fifo_nolock_parse("nope") == 0);
    CHECK(ps3_gcm_fifo_nolock_parse("0") == 0);
    CHECK(ps3_gcm_fifo_nolock_parse("1") == 1);
    printf("PASS\n");
    return 0;
}
