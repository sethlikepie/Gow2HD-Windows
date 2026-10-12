#include "rsx_depth_debug.h"
#include <stdio.h>
#include <stdlib.h>

static int g_fail;

#define CHECK(cond) do { \
    if (!(cond)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); g_fail = 1; } \
} while (0)

int main(void)
{
    CHECK(rsx_depth_normalize_func(0x0201u) == 0x0201u);
    CHECK(rsx_depth_normalize_func(1u) == 0x0201u);       /* compact LESS */
    CHECK(rsx_depth_normalize_func(3u) == 0x0203u);
    CHECK(rsx_depth_normalize_func(0x0206u) == 0x0206u);
    CHECK(rsx_depth_normalize_func(0xDEADu) == 0x0203u);

    CHECK(rsx_depth_invert_func(0x0201u) == 0x0204u);
    CHECK(rsx_depth_invert_func(0x0203u) == 0x0206u);
    CHECK(rsx_depth_invert_func(0x0204u) == 0x0201u);
    CHECK(rsx_depth_invert_func(0x0206u) == 0x0203u);
    CHECK(rsx_depth_invert_func(0x0207u) == 0x0207u);
    CHECK(rsx_depth_invert_func(1u) == 0x0204u);          /* compact LESS */

    /* B0: test off → Always, write follows mask */
    CHECK(rsx_depth_effective_func(0x0201u, 0, RSX_DEPTH_DBG_RSX) == 0x0207u);
    CHECK(rsx_depth_effective_func(0x0201u, 1, RSX_DEPTH_DBG_RSX) == 0x0201u);
    CHECK(rsx_depth_effective_func(1u, 1, RSX_DEPTH_DBG_RSX) == 0x0201u);
    CHECK(rsx_depth_effective_write(0, 1, RSX_DEPTH_DBG_RSX) == 0);
    CHECK(rsx_depth_effective_write(1, 1, RSX_DEPTH_DBG_RSX) == 1);

    /* B1 Always + write ON even if RSX had test off / mask 0 */
    CHECK(rsx_depth_effective_func(0x0201u, 0, RSX_DEPTH_DBG_ALWAYS) == 0x0207u);
    CHECK(rsx_depth_effective_write(0, 0, RSX_DEPTH_DBG_ALWAYS) == 1);

    /* B2 LEQUAL + write ON */
    CHECK(rsx_depth_effective_func(0x0206u, 1, RSX_DEPTH_DBG_LEQUAL) == 0x0203u);
    CHECK(rsx_depth_effective_write(0, 1, RSX_DEPTH_DBG_LEQUAL) == 1);

    CHECK(rsx_depth_effective_func(0x0203u, 1, RSX_DEPTH_DBG_GEQUAL) == 0x0206u);
    CHECK(rsx_depth_effective_write(0, 1, RSX_DEPTH_DBG_GEQUAL) == 1);

    /* invert does not force write; test-off stays Always */
    CHECK(rsx_depth_effective_func(0x0203u, 1, RSX_DEPTH_DBG_INVERT) == 0x0206u);
    CHECK(rsx_depth_effective_func(0x0206u, 1, RSX_DEPTH_DBG_INVERT) == 0x0203u);
    CHECK(rsx_depth_effective_func(0x0201u, 0, RSX_DEPTH_DBG_INVERT) == 0x0207u);
    CHECK(rsx_depth_effective_write(0, 1, RSX_DEPTH_DBG_INVERT) == 0);

    CHECK(rsx_depth_debug_mode() == RSX_DEPTH_DBG_RSX);

    {
        int done = 0;
        CHECK(rsx_debug_clear_frame_take(1, 3, &done) == 0 && done == 0);
        CHECK(rsx_debug_clear_frame_take(3, 3, &done) == 1 && done == 1);
        CHECK(rsx_debug_clear_frame_take(3, 3, &done) == 0 && done == 1);
        done = 0;
        CHECK(rsx_debug_clear_frame_take(2, -1, &done) == 0 && done == 0);
        CHECK(rsx_debug_clear_frame_take(-1, 0, &done) == 0);
        CHECK(rsx_debug_clear_frame_on() == 0);
    }

    CHECK(rsx_depth_fallback_should_clear(1, 0) == 1);
    CHECK(rsx_depth_fallback_should_clear(1, 1) == 1);
    CHECK(rsx_depth_fallback_should_clear(0, 0) == 0);
    CHECK(rsx_depth_fallback_should_clear(0, 1) == 1);

    /* Cull: off / FRONT / BACK / compact BACK / FRONT_AND_BACK */
    CHECK(rsx_cull_mode(0, 0x0405u) == RSX_CULL_NONE);
    CHECK(rsx_cull_mode(1, 0x0404u) == RSX_CULL_FRONT);
    CHECK(rsx_cull_mode(1, 0x0405u) == RSX_CULL_BACK);
    CHECK(rsx_cull_mode(1, 1u) == RSX_CULL_BACK);
    CHECK(rsx_cull_mode(1, 0x0408u) == RSX_CULL_NONE);

    /* Guest winding as-is: CW(0/0x0900) → Metal CW, CCW(0x0901/1) → CCW */
    CHECK(rsx_metal_winding_is_ccw(0u) == 0);
    CHECK(rsx_metal_winding_is_ccw(0x0900u) == 0);
    CHECK(rsx_metal_winding_is_ccw(0x0901u) == 1);
    CHECK(rsx_metal_winding_is_ccw(1u) == 1);

    if (g_fail) {
        printf("test_rsx_depth_debug: FAIL\n");
        return 1;
    }
    printf("test_rsx_depth_debug: PASS\n");
    return 0;
}
