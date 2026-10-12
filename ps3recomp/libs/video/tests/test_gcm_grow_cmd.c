/* Drives ps3_gcm_grow_cmd_size / parse used by cellGcmSetupContext. */
#include "gcm_grow_cmd.h"
#include <stdio.h>

#define CHECK(cond) do { \
    if (!(cond)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); return 1; } \
} while (0)

int main(void)
{
    /* GoW2: cmd=2 MB, io=48 MB → 4 MB. */
    CHECK(ps3_gcm_grow_cmd_size(0x200000u, 0x3000000u) == 0x400000u);
    /* Below the 64 KB floor, then double. */
    CHECK(ps3_gcm_grow_cmd_size(0x1000u, 0x100000u) == 0x20000u);
    /* Already at the floor, room to double. */
    CHECK(ps3_gcm_grow_cmd_size(0x10000u, 0x100000u) == 0x20000u);
    /* Twice would eat more than 1/4 of IO — leave requested. */
    CHECK(ps3_gcm_grow_cmd_size(0x200000u, 0x300000u) == 0x200000u);
    /* IO smaller than cmd — leave cmd. */
    CHECK(ps3_gcm_grow_cmd_size(0x200000u, 0x10000u) == 0x200000u);
    /* Overflow of cmd*2 — leave cmd. */
    CHECK(ps3_gcm_grow_cmd_size(0x80000000u, 0xFFFFFFFFu) == 0x80000000u);

    CHECK(ps3_gcm_grow_cmd_parse(NULL) == 0);
    CHECK(ps3_gcm_grow_cmd_parse("") == 0);
    CHECK(ps3_gcm_grow_cmd_parse("0") == 0);
    CHECK(ps3_gcm_grow_cmd_parse("1") == 1);

    printf("PASS\n");
    return 0;
}
