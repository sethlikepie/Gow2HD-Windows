#include "rsx_overlay_assets.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
/* Private copy: stb's API we do not call would warn as unused statics. */
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include "imstb_truetype.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

extern const unsigned char rsx_overlay_inter_regular[];
extern const size_t rsx_overlay_inter_regular_size;
extern const unsigned char rsx_overlay_inter_semibold[];
extern const size_t rsx_overlay_inter_semibold_size;

static const float k_point_size[RSX_OVERLAY_TEXT_STYLE_COUNT] = { 17.0f, 15.0f, 11.0f, 13.0f };
static const int k_inter_semibold[RSX_OVERLAY_TEXT_STYLE_COUNT] = { 1, 0, 1, 0 };

float rsx_overlay_text_point_size(int style)
{
    return (style >= 0 && style < RSX_OVERLAY_TEXT_STYLE_COUNT) ? k_point_size[style] : 15.0f;
}

int rsx_overlay_glyph_index(uint32_t cp)
{
    if (cp >= 0x20 && cp <= 0x7E) return (int)(cp - 0x20);
    if (cp >= 0xA0 && cp <= 0xFF) return 95 + (int)(cp - 0xA0);
    if (cp == 0x2026) return 191;
    if (cp == 0x2013) return 192;
    if (cp == 0x2014) return 193;
    return -1;
}

uint32_t rsx_overlay_glyph_codepoint(int index)
{
    if (index >= 0 && index < 95) return 0x20u + (uint32_t)index;
    if (index >= 95 && index < 191) return 0xA0u + (uint32_t)(index - 95);
    if (index == 191) return 0x2026u;
    if (index == 192) return 0x2013u;
    if (index == 193) return 0x2014u;
    return 0;
}

uint32_t rsx_overlay_utf8_next(const char** text)
{
    const unsigned char* s = (const unsigned char*)*text;
    uint32_t cp;
    int n;
    if (s[0] < 0x80) { cp = s[0]; n = 1; }
    else if ((s[0] & 0xE0) == 0xC0 && (s[1] & 0xC0) == 0x80) {
        cp = ((uint32_t)(s[0] & 0x1F) << 6) | (s[1] & 0x3F); n = 2;
    } else if ((s[0] & 0xF0) == 0xE0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80) {
        cp = ((uint32_t)(s[0] & 0x0F) << 12) | ((uint32_t)(s[1] & 0x3F) << 6) | (s[2] & 0x3F); n = 3;
    } else if ((s[0] & 0xF8) == 0xF0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80 &&
               (s[3] & 0xC0) == 0x80) {
        cp = ((uint32_t)(s[0] & 0x07) << 18) | ((uint32_t)(s[1] & 0x3F) << 12) |
             ((uint32_t)(s[2] & 0x3F) << 6) | (s[3] & 0x3F); n = 4;
    } else { cp = 0xFFFDu; n = 1; }
    *text += n;
    return cp;
}

float rsx_overlay_text_width(const rsx_overlay_assets* a, int style, const char* text, float k)
{
    const int q = rsx_overlay_glyph_index('?');
    float w = 0.0f;
    if (a == NULL || text == NULL || style < 0 || style >= RSX_OVERLAY_TEXT_STYLE_COUNT)
        return 0.0f;
    while (*text) {
        int gi = rsx_overlay_glyph_index(rsx_overlay_utf8_next(&text));
        if (gi < 0 || !a->glyphs[style][gi].present)
            gi = q;
        w += a->glyphs[style][gi].advance * k;
    }
    return w;
}

int rsx_overlay_packer_add(rsx_overlay_packer* p, uint32_t w, uint32_t h, uint16_t* ox, uint16_t* oy)
{
    if (w + 2u > p->width)
        return 0;
    if (p->x + w + 1u > p->width) {
        p->x = 1u;
        p->y += p->row_h + 1u;
        p->row_h = 0u;
    }
    if (p->y + h + 1u > p->height)
        return 0;
    *ox = (uint16_t)p->x;
    *oy = (uint16_t)p->y;
    p->x += w + 1u;
    if (h > p->row_h)
        p->row_h = h;
    return 1;
}

void rsx_overlay_assets_put_alpha(rsx_overlay_assets* a, uint32_t x, uint32_t y, uint32_t w,
                                  uint32_t h, const uint8_t* alpha, uint32_t stride)
{
    for (uint32_t row = 0; row < h; ++row)
        for (uint32_t col = 0; col < w; ++col)
            a->rgba[(((size_t)(y + row) * a->width) + x + col) * 4u + 3u] = alpha[(size_t)row * stride + col];
}

int rsx_overlay_assets_begin(rsx_overlay_assets* a, uint32_t w, uint32_t h, float scale,
                             rsx_overlay_packer* p)
{
    static const uint8_t opaque[16] = { 255, 255, 255, 255, 255, 255, 255, 255,
                                        255, 255, 255, 255, 255, 255, 255, 255 };
    uint16_t x, y;
    memset(a, 0, sizeof(*a));
    a->rgba = (uint8_t*)malloc((size_t)w * h * 4u);
    if (a->rgba == NULL)
        return 0;
    for (size_t i = 0; i < (size_t)w * h; ++i) {
        a->rgba[i * 4u + 0u] = 255;
        a->rgba[i * 4u + 1u] = 255;
        a->rgba[i * 4u + 2u] = 255;
        a->rgba[i * 4u + 3u] = 0;
    }
    a->width = w;
    a->height = h;
    a->scale = scale;
    p->width = w; p->height = h; p->x = 1u; p->y = 1u; p->row_h = 0u;
    if (!rsx_overlay_packer_add(p, 4u, 4u, &x, &y)) {
        rsx_overlay_assets_free(a);
        return 0;
    }
    rsx_overlay_assets_put_alpha(a, x, y, 4u, 4u, opaque, 4u);
    a->white_x = x;
    a->white_y = y;
    return 1;
}

void rsx_overlay_assets_free(rsx_overlay_assets* a)
{
    if (a == NULL)
        return;
    free(a->rgba);
    a->rgba = NULL;
    a->width = a->height = 0;
}

static int bake_style(rsx_overlay_assets* a, rsx_overlay_packer* p, int style,
                      const stbtt_fontinfo* font, float scale)
{
    const float sc = stbtt_ScaleForMappingEmToPixels(font, rsx_overlay_text_point_size(style) * scale);
    int ascent, descent, gap;
    stbtt_GetFontVMetrics(font, &ascent, &descent, &gap);
    a->ascent[style] = (float)ascent * sc;
    a->descent[style] = (float)-descent * sc;
    a->line_height[style] = (float)(ascent - descent + gap) * sc;
    for (int i = 0; i < RSX_OVERLAY_GLYPH_COUNT; ++i) {
        rsx_overlay_glyph* out = &a->glyphs[style][i];
        const int g = stbtt_FindGlyphIndex(font, (int)rsx_overlay_glyph_codepoint(i));
        int adv, lsb, x0, y0, x1, y1;
        uint16_t ox, oy;
        uint8_t* tmp;
        memset(out, 0, sizeof(*out));
        if (g == 0)
            continue;
        stbtt_GetGlyphHMetrics(font, g, &adv, &lsb);
        out->advance = (float)adv * sc;
        out->present = 1;
        stbtt_GetGlyphBitmapBox(font, g, sc, sc, &x0, &y0, &x1, &y1);
        if (x1 <= x0 || y1 <= y0)
            continue;
        if (!rsx_overlay_packer_add(p, (uint32_t)(x1 - x0), (uint32_t)(y1 - y0), &ox, &oy))
            return 0;
        tmp = (uint8_t*)malloc((size_t)(x1 - x0) * (size_t)(y1 - y0));
        if (tmp == NULL)
            return 0;
        stbtt_MakeGlyphBitmap(font, tmp, x1 - x0, y1 - y0, x1 - x0, sc, sc, g);
        rsx_overlay_assets_put_alpha(a, ox, oy, (uint32_t)(x1 - x0), (uint32_t)(y1 - y0), tmp,
                                     (uint32_t)(x1 - x0));
        free(tmp);
        out->x = ox; out->y = oy;
        out->w = (uint16_t)(x1 - x0); out->h = (uint16_t)(y1 - y0);
        out->left = (int16_t)x0; out->top = (int16_t)-y0;
    }
    return 1;
}

int rsx_overlay_assets_build_embedded(rsx_overlay_assets* out, float scale)
{
    static const uint32_t sizes[] = { 1024u, 2048u, 4096u };
    stbtt_fontinfo regular, semibold;
    if (out == NULL || !(scale >= 0.5f && scale <= 4.0f))
        return 0;
    if (!stbtt_InitFont(&regular, rsx_overlay_inter_regular,
                        stbtt_GetFontOffsetForIndex(rsx_overlay_inter_regular, 0)) ||
        !stbtt_InitFont(&semibold, rsx_overlay_inter_semibold,
                        stbtt_GetFontOffsetForIndex(rsx_overlay_inter_semibold, 0)))
        return 0;
    for (size_t attempt = 0; attempt < sizeof sizes / sizeof sizes[0]; ++attempt) {
        rsx_overlay_packer p;
        int ok = 1;
        if (!rsx_overlay_assets_begin(out, sizes[attempt], sizes[attempt], scale, &p))
            return 0;
        for (int st = 0; st < RSX_OVERLAY_TEXT_STYLE_COUNT && ok; ++st)
            ok = bake_style(out, &p, st, k_inter_semibold[st] ? &semibold : &regular, scale);
        if (ok) {
            out->system_font = 0;
            out->system_icons = 0;
            return 1;
        }
        rsx_overlay_assets_free(out);
    }
    return 0;
}
