#include <math.h>
#include <stdio.h>
#include <string.h>

#include "rsx_overlay_feedback.h"
#include "rsx_overlay_ui_sound.h"

#define CHECK(expr) do { \
    if (!(expr)) { fprintf(stderr, "[FAIL] %s:%d: %s\n", __func__, __LINE__, #expr); return 0; } \
} while (0)

static float buf[RSX_OVERLAY_UI_SOUND_RATE];

static int test_every_event_is_short_quiet_and_click_free(void)
{
    for (int e = 0; e < RSX_OVERLAY_FEEDBACK_COUNT; ++e) {
        const size_t n = rsx_overlay_ui_sound_length(e);
        CHECK(n > 0 && n <= RSX_OVERLAY_UI_SOUND_RATE / 4); /* <= 250 ms */
        CHECK(rsx_overlay_ui_sound_render(e, buf, sizeof buf / sizeof buf[0]) == n);
        float peak = 0.0f;
        for (size_t i = 0; i < n; ++i) peak = fmaxf(peak, fabsf(buf[i]));
        CHECK(peak > 0.05f && peak <= 0.30f);
        CHECK(fabsf(buf[0]) < 1e-3f && fabsf(buf[n - 1]) < 1e-3f);
    }
    CHECK(rsx_overlay_ui_sound_length(RSX_OVERLAY_FEEDBACK_OPEN) == 8640);  /* 180 ms */
    CHECK(rsx_overlay_ui_sound_length(RSX_OVERLAY_FEEDBACK_MOVE) == 1344);  /* 28 ms */
    CHECK(rsx_overlay_ui_sound_length(-1) == 0 && rsx_overlay_ui_sound_length(RSX_OVERLAY_FEEDBACK_COUNT) == 0);
    return 1;
}

static int test_deterministic_distinct_and_bounded(void)
{
    static float a[RSX_OVERLAY_UI_SOUND_RATE], b[RSX_OVERLAY_UI_SOUND_RATE];
    const size_t n = rsx_overlay_ui_sound_render(RSX_OVERLAY_FEEDBACK_OPEN, a, RSX_OVERLAY_UI_SOUND_RATE);
    CHECK(rsx_overlay_ui_sound_render(RSX_OVERLAY_FEEDBACK_OPEN, b, RSX_OVERLAY_UI_SOUND_RATE) == n);
    CHECK(memcmp(a, b, n * sizeof(float)) == 0);
    rsx_overlay_ui_sound_render(RSX_OVERLAY_FEEDBACK_CLOSE, b, RSX_OVERLAY_UI_SOUND_RATE);
    CHECK(memcmp(a, b, 256 * sizeof(float)) != 0);
    CHECK(rsx_overlay_ui_sound_render(RSX_OVERLAY_FEEDBACK_OPEN, a, 100) == 100);
    CHECK(rsx_overlay_ui_sound_render(RSX_OVERLAY_FEEDBACK_OPEN, NULL, 0) == n);
    return 1;
}

int main(void)
{
    int ok = test_every_event_is_short_quiet_and_click_free() & test_deterministic_distinct_and_bounded();
    printf("test_rsx_overlay_ui_sound: %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
