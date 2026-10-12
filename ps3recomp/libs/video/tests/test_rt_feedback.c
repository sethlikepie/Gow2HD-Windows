/*
 * Drives rsx_rt_is_feedback (shipped). Same offset is a hazard; 0 dest is not.
 */
#include "../rsx_rt_feedback.h"
#include <stdio.h>

int main(void)
{
    int ok = 1;
#define CHECK(cond) do { if (!(cond)) { printf("  [FAIL] %s\n", #cond); ok = 0; } } while (0)

    CHECK(rsx_rt_is_feedback(0x00400000u, 0x00400000u) == 1);
    CHECK(rsx_rt_is_feedback(0x00400000u, 0x007C0000u) == 0);
    CHECK(rsx_rt_is_feedback(0u, 0u) == 0);
    CHECK(rsx_rt_is_feedback(0u, 0x00400000u) == 0);

#undef CHECK
    printf("%s\n", ok ? "test_rt_feedback: PASS" : "test_rt_feedback: FAIL");
    return ok ? 0 : 1;
}
