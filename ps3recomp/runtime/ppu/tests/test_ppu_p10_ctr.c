/* Drives the shipped PS3_TRACE_P10_CTR histogram (func_00293010 bctrl). */
#include "../ppu_p10_ctr.h"
#include <stdio.h>
#include <stdlib.h>

#define CHECK(cond) do { \
    if (!(cond)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); return 1; } \
} while (0)

int main(void)
{
    unsetenv("PS3_TRACE_P10_CTR");
    ps3_p10_ctr_reset_for_test();
    ps3_p10_ctr_note(0, 0x00293010u);
    CHECK(ps3_p10_ctr_nunique(0) == 0);
    CHECK(ps3_p10_ctr_count(0, 0x00293010u) == 0);

    setenv("PS3_TRACE_P10_CTR", "1", 1);
    ps3_p10_ctr_reset_for_test();
    ps3_p10_ctr_note(0, 0x001658A0u);
    ps3_p10_ctr_note(0, 0x001658A0u);
    ps3_p10_ctr_note(1, 0x002F0D78u);
    ps3_p10_ctr_note(0, 0x00156D78u);
    CHECK(ps3_p10_ctr_nunique(0) == 2);
    CHECK(ps3_p10_ctr_count(0, 0x001658A0u) == 2);
    CHECK(ps3_p10_ctr_count(0, 0x00156D78u) == 1);
    CHECK(ps3_p10_ctr_nunique(1) == 1);
    CHECK(ps3_p10_ctr_count(1, 0x002F0D78u) == 1);
    CHECK(ps3_p10_ctr_nunique(2) == 0);
    CHECK(ps3_p10_ctr_count(9, 1) == 0);
    return 0;
}
