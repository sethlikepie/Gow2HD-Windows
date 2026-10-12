#ifndef PS3RECOMP_RSX_OVERLAY_ASSETS_H
#define PS3RECOMP_RSX_OVERLAY_ASSETS_H

/* Glyph/icon atlas of the console shell: white RGB, coverage in A, rasterized
 * at the drawable's backing scale. It becomes the overlay's texture 0. */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum rsx_overlay_text_style {
    RSX_OVERLAY_TEXT_TITLE = 0,
    RSX_OVERLAY_TEXT_ROW,
    RSX_OVERLAY_TEXT_LABEL,
    RSX_OVERLAY_TEXT_CAPTION,
    RSX_OVERLAY_TEXT_STYLE_COUNT
} rsx_overlay_text_style;

typedef enum rsx_overlay_icon {
    RSX_OVERLAY_ICON_PLAY = 0, RSX_OVERLAY_ICON_DISPLAY, RSX_OVERLAY_ICON_GAMEPAD,
    RSX_OVERLAY_ICON_SPEAKER, RSX_OVERLAY_ICON_HAMMER, RSX_OVERLAY_ICON_POWER,
    RSX_OVERLAY_ICON_COUNT
} rsx_overlay_icon;

#define RSX_OVERLAY_GLYPH_COUNT 194
#define RSX_OVERLAY_ICON_POINT_SIZE 30.0f

/* Atlas rectangle plus pen metrics in atlas pixels: the glyph's top-left is
 * (pen + left, baseline - top). */
typedef struct rsx_overlay_glyph {
    uint16_t x, y, w, h;
    int16_t left, top;
    float advance;
    uint8_t present;
} rsx_overlay_glyph;

typedef struct rsx_overlay_assets {
    uint32_t width, height;
    uint8_t* rgba;
    float scale;             /* atlas pixels per point */
    uint8_t system_font;     /* 1: SF Pro (CoreText); 0: embedded Inter */
    uint8_t system_icons;    /* 1: `icons` holds SF Symbols; 0: draw geometric icons */
    uint16_t white_x, white_y; /* top-left of an opaque 4x4 block */
    float ascent[RSX_OVERLAY_TEXT_STYLE_COUNT];
    float descent[RSX_OVERLAY_TEXT_STYLE_COUNT];  /* positive, below the baseline */
    float line_height[RSX_OVERLAY_TEXT_STYLE_COUNT];
    rsx_overlay_glyph glyphs[RSX_OVERLAY_TEXT_STYLE_COUNT][RSX_OVERLAY_GLYPH_COUNT];
    rsx_overlay_glyph icons[RSX_OVERLAY_ICON_COUNT];
} rsx_overlay_assets;

typedef struct rsx_overlay_packer { uint32_t width, height, x, y, row_h; } rsx_overlay_packer;

int rsx_overlay_glyph_index(uint32_t codepoint);
uint32_t rsx_overlay_glyph_codepoint(int index);
uint32_t rsx_overlay_utf8_next(const char** text);
float rsx_overlay_text_point_size(int style);
float rsx_overlay_text_width(const rsx_overlay_assets* a, int style, const char* utf8, float k);

/* Shared by the builders. */
int rsx_overlay_assets_begin(rsx_overlay_assets* a, uint32_t w, uint32_t h, float scale,
                             rsx_overlay_packer* p);
int rsx_overlay_packer_add(rsx_overlay_packer* p, uint32_t w, uint32_t h, uint16_t* x, uint16_t* y);
void rsx_overlay_assets_put_alpha(rsx_overlay_assets* a, uint32_t x, uint32_t y, uint32_t w,
                                  uint32_t h, const uint8_t* alpha, uint32_t stride);

int rsx_overlay_assets_build_embedded(rsx_overlay_assets* out, float scale);
#if defined(__APPLE__)
int rsx_overlay_assets_build_apple(rsx_overlay_assets* out, float scale);
#ifdef RSX_OVERLAY_TESTING
/* Replaces one icon's SF Symbol name (NULL restores it) to exercise the
 * missing-symbol fallback. */
void rsx_overlay_assets_apple_test_set_symbol(int icon, const char* name);
#endif
#endif
void rsx_overlay_assets_free(rsx_overlay_assets* a);

#ifdef __cplusplus
}
#endif

#endif
