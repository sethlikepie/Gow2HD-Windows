#import <Foundation/Foundation.h>
#include <stdio.h>
#include <string.h>
#include "rsx_overlay_assets.h"
#include "rsx_overlay_log.h"

#define CHECK(expr) do { \
    if (!(expr)) { fprintf(stderr, "[FAIL] %s:%d: %s\n", __func__, __LINE__, #expr); return 0; } \
} while (0)

static int test_sf_pro_and_symbols(void)
{
    rsx_overlay_assets one, two;
    CHECK(rsx_overlay_assets_build_apple(&one, 1.0f) == 1);
    CHECK(rsx_overlay_assets_build_apple(&two, 2.0f) == 1);
    CHECK(one.system_font == 1 && two.system_font == 1);   /* SF Pro via CoreText */
    CHECK(two.system_icons == 1);                          /* SF Symbols rasterized */
    for (int i = 0; i < RSX_OVERLAY_ICON_COUNT; ++i)
        CHECK(two.icons[i].w > 0 && two.icons[i].h > 0 && two.icons[i].present);
    const int H = rsx_overlay_glyph_index('H');
    const float r = (float)two.glyphs[RSX_OVERLAY_TEXT_TITLE][H].h / (float)one.glyphs[RSX_OVERLAY_TEXT_TITLE][H].h;
    CHECK(r > 1.8f && r < 2.2f);
    CHECK(two.ascent[RSX_OVERLAY_TEXT_ROW] > 1.8f * one.ascent[RSX_OVERLAY_TEXT_ROW]);
    CHECK(two.glyphs[RSX_OVERLAY_TEXT_ROW][rsx_overlay_glyph_index(0xE7)].present); /* ç */
    rsx_overlay_assets_free(&one);
    rsx_overlay_assets_free(&two);
    return 1;
}

/* Icons that overflow the atlas retry at the next atlas size instead of
 * silently falling back to geometric icons (3.5 used to keep 1024 w/o icons). */
static int test_icons_present_at_every_scale(void)
{
    static const float scales[] = { 0.5f, 1.0f, 2.0f, 3.0f, 3.5f, 4.0f };
    for (size_t s = 0; s < sizeof scales / sizeof scales[0]; ++s) {
        rsx_overlay_assets a;
        CHECK(rsx_overlay_assets_build_apple(&a, scales[s]) == 1);
        if (!(a.system_font == 1 && a.system_icons == 1))
            fprintf(stderr, "  scale %.1f: atlas %u font=%d icons=%d\n", (double)scales[s],
                    (unsigned)a.width, a.system_font, a.system_icons);
        CHECK(a.system_font == 1 && a.system_icons == 1);
        for (int i = 0; i < RSX_OVERLAY_ICON_COUNT; ++i)
            CHECK(a.icons[i].present && a.icons[i].w > 0 && a.icons[i].h > 0);
        rsx_overlay_assets_free(&a);
    }
    return 1;
}

#ifdef RSX_OVERLAY_TESTING
/* A genuinely missing symbol still falls back to geometric icons, keeping
 * SF Pro text. */
static int test_missing_symbol_falls_back(void)
{
    rsx_overlay_assets a;
    rsx_overlay_assets_apple_test_set_symbol(RSX_OVERLAY_ICON_HAMMER, "ps3recomp.no.such.symbol");
    const int built = rsx_overlay_assets_build_apple(&a, 2.0f);
    rsx_overlay_assets_apple_test_set_symbol(RSX_OVERLAY_ICON_HAMMER, NULL);
    CHECK(built == 1);
    CHECK(a.system_font == 1 && a.system_icons == 0);
    for (int i = 0; i < RSX_OVERLAY_ICON_COUNT; ++i)
        CHECK(!a.icons[i].present);
    rsx_overlay_assets_free(&a);
    /* The warning names the missing symbol (not a generic "unavailable"). */
    static char snap[RSX_OVERLAY_LOG_SNAPSHOT_CAPACITY];
    rsx_overlay_log_snapshot(snap, sizeof snap);
    CHECK(strstr(snap, "SF Symbol 'ps3recomp.no.such.symbol' unavailable") != NULL);
    CHECK(strstr(snap, "do not fit") == NULL);
    return 1;
}
#endif

int main(void)
{
    int ok = 1;
    @autoreleasepool {
        ok &= test_sf_pro_and_symbols();
        ok &= test_icons_present_at_every_scale();
#ifdef RSX_OVERLAY_TESTING
        ok &= test_missing_symbol_falls_back();
#endif
    }
    printf("test_rsx_overlay_assets_apple: %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
