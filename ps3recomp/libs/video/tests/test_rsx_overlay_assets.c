#include <math.h>
#include <stdio.h>
#include <string.h>

#include "rsx_overlay_assets.h"

#define CHECK(expr) do { \
    if (!(expr)) { fprintf(stderr, "[FAIL] %s:%d: %s\n", __func__, __LINE__, #expr); return 0; } \
} while (0)

static int test_glyph_set_and_utf8(void)
{
    const char* s = "a\xC3\xA3\xE2\x80\xA6\xE4\xB8\x80\xFF";
    CHECK(rsx_overlay_glyph_index(' ') == 0);
    CHECK(rsx_overlay_glyph_index('~') == 94);
    CHECK(rsx_overlay_glyph_index(0xA0) == 95 && rsx_overlay_glyph_index(0xFF) == 190);
    CHECK(rsx_overlay_glyph_index(0x2026) == 191 && rsx_overlay_glyph_index(0x2014) == 193);
    CHECK(rsx_overlay_glyph_index(0x7F) == -1 && rsx_overlay_glyph_index(0x4E00) == -1);
    for (int i = 0; i < RSX_OVERLAY_GLYPH_COUNT; ++i)
        CHECK(rsx_overlay_glyph_index(rsx_overlay_glyph_codepoint(i)) == i);
    CHECK(rsx_overlay_utf8_next(&s) == 'a');
    CHECK(rsx_overlay_utf8_next(&s) == 0xE3);
    CHECK(rsx_overlay_utf8_next(&s) == 0x2026);
    CHECK(rsx_overlay_utf8_next(&s) == 0x4E00);
    CHECK(rsx_overlay_utf8_next(&s) == 0xFFFD && *s == '\0'); /* invalid byte: one step */
    return 1;
}

static int test_embedded_atlas(float scale, rsx_overlay_assets* out)
{
    CHECK(rsx_overlay_assets_build_embedded(out, scale) == 1);
    CHECK(out->rgba != NULL && out->width >= 1024 && out->width <= 4096 && out->height == out->width);
    CHECK(out->scale == scale && out->system_font == 0 && out->system_icons == 0);
    /* The white block is opaque: the shell's solid shapes sample it. */
    CHECK(out->rgba[((size_t)(out->white_y + 1) * out->width + out->white_x + 1) * 4 + 3] == 255);
    const int A = rsx_overlay_glyph_index('A'), atilde = rsx_overlay_glyph_index(0xE3);
    for (int st = 0; st < RSX_OVERLAY_TEXT_STYLE_COUNT; ++st) {
        CHECK(out->glyphs[st][A].present && out->glyphs[st][A].w > 0 && out->glyphs[st][A].h > 0);
        CHECK(out->glyphs[st][atilde].present);
        CHECK(out->glyphs[st][rsx_overlay_glyph_index(' ')].advance > 0.0f);
        CHECK(out->ascent[st] > 0.0f && out->descent[st] > 0.0f);
    }
    CHECK(out->line_height[RSX_OVERLAY_TEXT_TITLE] > out->line_height[RSX_OVERLAY_TEXT_LABEL]);
    return 1;
}

static int test_embedded_scales_with_backing(void)
{
    rsx_overlay_assets one, two;
    CHECK(test_embedded_atlas(1.0f, &one));
    CHECK(test_embedded_atlas(2.0f, &two));
    const int H = rsx_overlay_glyph_index('H');
    const float r = (float)two.glyphs[RSX_OVERLAY_TEXT_ROW][H].h / (float)one.glyphs[RSX_OVERLAY_TEXT_ROW][H].h;
    CHECK(r > 1.8f && r < 2.2f);
    const float w1 = rsx_overlay_text_width(&one, RSX_OVERLAY_TEXT_ROW, "Tela cheia", 1.0f);
    const float w2 = rsx_overlay_text_width(&two, RSX_OVERLAY_TEXT_ROW, "Tela cheia", 1.0f);
    CHECK(w1 > 0.0f && fabsf(w2 - 2.0f * w1) < 3.0f);
    /* k scales measurement; unknown code points measure as '?'. */
    CHECK(fabsf(rsx_overlay_text_width(&two, RSX_OVERLAY_TEXT_ROW, "Tela cheia", 0.5f) - 0.5f * w2) < 0.01f);
    CHECK(rsx_overlay_text_width(&one, RSX_OVERLAY_TEXT_ROW, "\xE4\xB8\x80", 1.0f) ==
          rsx_overlay_text_width(&one, RSX_OVERLAY_TEXT_ROW, "?", 1.0f));
    rsx_overlay_assets_free(&one);
    rsx_overlay_assets_free(&two);
    CHECK(one.rgba == NULL);
    CHECK(rsx_overlay_assets_build_embedded(&one, 0.1f) == 0); /* out of range */
    return 1;
}

int main(void)
{
    int ok = 1;
    ok &= test_glyph_set_and_utf8();
    ok &= test_embedded_scales_with_backing();
    printf("test_rsx_overlay_assets: %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
