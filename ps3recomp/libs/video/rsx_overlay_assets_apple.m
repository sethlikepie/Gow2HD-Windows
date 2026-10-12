/* CoreText SF Pro + SF Symbols atlas for the console shell (macOS). Falls back
 * to the embedded Inter builder when the system font is unavailable; icons
 * fall back to geometric drawing (system_icons = 0). */
#import <AppKit/AppKit.h>
#import <CoreText/CoreText.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rsx_overlay_assets.h"
#include "rsx_overlay_log.h"

static NSFontWeight apple_weight(int style)
{
    switch (style) {
    case RSX_OVERLAY_TEXT_TITLE: return NSFontWeightSemibold;
    case RSX_OVERLAY_TEXT_LABEL: return NSFontWeightMedium;
    default: return NSFontWeightRegular;
    }
}

static int apple_bake_style(rsx_overlay_assets* a, rsx_overlay_packer* p, int style, CTFontRef font)
{
    a->ascent[style] = (float)CTFontGetAscent(font);
    a->descent[style] = (float)CTFontGetDescent(font);
    a->line_height[style] = (float)(CTFontGetAscent(font) + CTFontGetDescent(font) + CTFontGetLeading(font));
    for (int i = 0; i < RSX_OVERLAY_GLYPH_COUNT; ++i) {
        rsx_overlay_glyph* out = &a->glyphs[style][i];
        UniChar ch = (UniChar)rsx_overlay_glyph_codepoint(i);
        CGGlyph g;
        CGSize adv;
        memset(out, 0, sizeof(*out));
        if (!CTFontGetGlyphsForCharacters(font, &ch, &g, 1))
            continue;
        CTFontGetAdvancesForGlyphs(font, kCTFontOrientationHorizontal, &g, &adv, 1);
        out->advance = (float)adv.width;
        out->present = 1;
        const CGRect bb = CTFontGetBoundingRectsForGlyphs(font, kCTFontOrientationHorizontal, &g, NULL, 1);
        if (bb.size.width <= 0.0 || bb.size.height <= 0.0)
            continue;
        const int x0 = (int)floor(bb.origin.x) - 1, y0 = (int)floor(bb.origin.y) - 1;
        const int x1 = (int)ceil(bb.origin.x + bb.size.width) + 1;
        const int y1 = (int)ceil(bb.origin.y + bb.size.height) + 1;
        const int w = x1 - x0, h = y1 - y0;
        uint16_t ox, oy;
        uint8_t* buf = (uint8_t*)calloc((size_t)w * (size_t)h, 1);
        if (buf == NULL)
            return 0;
        CGContextRef ctx = CGBitmapContextCreate(buf, (size_t)w, (size_t)h, 8, (size_t)w, NULL,
                                                 (CGBitmapInfo)kCGImageAlphaOnly);
        if (ctx == NULL) { free(buf); return 0; }
        CGContextSetAllowsAntialiasing(ctx, true);
        CGContextSetShouldAntialias(ctx, true);
        CGContextSetAllowsFontSmoothing(ctx, false);
        const CGPoint pos = CGPointMake(-x0, -y0);
        CTFontDrawGlyphs(font, &g, &pos, 1, ctx);
        CGContextRelease(ctx);
        /* CGBitmapContext memory row 0 is the top row: same as the atlas. */
        if (!rsx_overlay_packer_add(p, (uint32_t)w, (uint32_t)h, &ox, &oy)) { free(buf); return 0; }
        rsx_overlay_assets_put_alpha(a, ox, oy, (uint32_t)w, (uint32_t)h, buf, (uint32_t)w);
        free(buf);
        out->x = ox; out->y = oy; out->w = (uint16_t)w; out->h = (uint16_t)h;
        out->left = (int16_t)x0; out->top = (int16_t)y1;
    }
    return 1;
}

static const char* const k_icon_symbols[RSX_OVERLAY_ICON_COUNT] = {
    "play.fill", "display", "gamecontroller", "speaker.wave.2", "hammer", "power"
};

#ifdef RSX_OVERLAY_TESTING
static const char* s_test_symbol_override[RSX_OVERLAY_ICON_COUNT];

void rsx_overlay_assets_apple_test_set_symbol(int icon, const char* name)
{
    if (icon >= 0 && icon < RSX_OVERLAY_ICON_COUNT)
        s_test_symbol_override[icon] = name;
}
#endif

static const char* apple_icon_symbol(int icon)
{
#ifdef RSX_OVERLAY_TESTING
    if (s_test_symbol_override[icon] != NULL)
        return s_test_symbol_override[icon];
#endif
    return k_icon_symbols[icon];
}

enum { APPLE_ICONS_OK = 1, APPLE_ICONS_UNAVAILABLE = 0, APPLE_ICONS_ATLAS_FULL = -1 };

/* APPLE_ICONS_UNAVAILABLE: a symbol is missing or could not be rendered
 * (the shell draws geometric icons); *missing names it when known.
 * APPLE_ICONS_ATLAS_FULL: the packer overflowed, so the caller retries with
 * the next atlas size instead of dropping the icons. */
static int apple_bake_icons(rsx_overlay_assets* a, rsx_overlay_packer* p, float scale,
                            const char** missing)
{
    *missing = NULL;
    if (@available(macOS 11.0, *)) {
        NSImageSymbolConfiguration* cfg =
            [NSImageSymbolConfiguration configurationWithPointSize:RSX_OVERLAY_ICON_POINT_SIZE * scale
                                                            weight:NSFontWeightRegular];
        for (int i = 0; i < RSX_OVERLAY_ICON_COUNT; ++i) {
            const char* symbol = apple_icon_symbol(i);
            NSImage* img = [[NSImage imageWithSystemSymbolName:@(symbol) accessibilityDescription:nil]
                               imageWithSymbolConfiguration:cfg];
            if (img == nil) {
                *missing = symbol;
                return APPLE_ICONS_UNAVAILABLE;
            }
            const NSSize sz = img.size;
            const int w = (int)ceil(sz.width) + 2, h = (int)ceil(sz.height) + 2;
            uint8_t* rgba = (uint8_t*)calloc((size_t)w * (size_t)h, 4);
            uint8_t* alpha = (uint8_t*)malloc((size_t)w * (size_t)h);
            unsigned coverage = 0;
            uint16_t ox, oy;
            if (rgba == NULL || alpha == NULL) { free(rgba); free(alpha); return APPLE_ICONS_UNAVAILABLE; }
            CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
            CGContextRef ctx = CGBitmapContextCreate(rgba, (size_t)w, (size_t)h, 8, (size_t)w * 4u, cs,
                                                     (CGBitmapInfo)kCGImageAlphaPremultipliedLast);
            CGColorSpaceRelease(cs);
            if (ctx == NULL) { free(rgba); free(alpha); return APPLE_ICONS_UNAVAILABLE; }
            NSGraphicsContext* gc = [NSGraphicsContext graphicsContextWithCGContext:ctx flipped:NO];
            [NSGraphicsContext saveGraphicsState];
            [NSGraphicsContext setCurrentContext:gc];
            [img drawInRect:NSMakeRect(1, 1, sz.width, sz.height) fromRect:NSZeroRect
                  operation:NSCompositingOperationSourceOver fraction:1.0];
            [NSGraphicsContext restoreGraphicsState];
            CGContextRelease(ctx);
            for (int k = 0; k < w * h; ++k) { alpha[k] = rgba[k * 4 + 3]; coverage += alpha[k] != 0; }
            free(rgba);
            if (coverage == 0) {
                free(alpha);
                *missing = symbol;
                return APPLE_ICONS_UNAVAILABLE;
            }
            if (!rsx_overlay_packer_add(p, (uint32_t)w, (uint32_t)h, &ox, &oy)) {
                free(alpha);
                return APPLE_ICONS_ATLAS_FULL;
            }
            rsx_overlay_assets_put_alpha(a, ox, oy, (uint32_t)w, (uint32_t)h, alpha, (uint32_t)w);
            free(alpha);
            a->icons[i].x = ox; a->icons[i].y = oy;
            a->icons[i].w = (uint16_t)w; a->icons[i].h = (uint16_t)h;
            a->icons[i].present = 1;
        }
        return APPLE_ICONS_OK;
    }
    return APPLE_ICONS_UNAVAILABLE;
}

int rsx_overlay_assets_build_apple(rsx_overlay_assets* out, float scale)
{
    static const uint32_t sizes[] = { 1024u, 2048u, 4096u };
    if (out == NULL || !(scale >= 0.5f && scale <= 4.0f))
        return 0;
    @autoreleasepool {
        for (size_t attempt = 0; attempt < sizeof sizes / sizeof sizes[0]; ++attempt) {
            rsx_overlay_packer p;
            int ok = 1;
            if (!rsx_overlay_assets_begin(out, sizes[attempt], sizes[attempt], scale, &p))
                return 0;
            for (int st = 0; st < RSX_OVERLAY_TEXT_STYLE_COUNT && ok; ++st) {
                NSFont* font = [NSFont systemFontOfSize:rsx_overlay_text_point_size(st) * scale
                                                 weight:apple_weight(st)];
                if (font == nil) {
                    rsx_overlay_assets_free(out);
                    rsx_overlay_log_write(RSX_OVERLAY_LOG_WARN, "overlay: SF Pro unavailable, using Inter");
                    return rsx_overlay_assets_build_embedded(out, scale);
                }
                ok = apple_bake_style(out, &p, st, (__bridge CTFontRef)font);
            }
            if (ok) {
                const char* missing = NULL;
                const int icons = apple_bake_icons(out, &p, scale, &missing);
                const int last = attempt + 1 == sizeof sizes / sizeof sizes[0];
                char msg[160];
                if (icons == APPLE_ICONS_ATLAS_FULL && !last) {
                    /* Text fit but the icons did not: retry larger rather
                     * than lose SF Symbols. */
                    rsx_overlay_assets_free(out);
                    continue;
                }
                out->system_font = 1;
                out->system_icons = icons == APPLE_ICONS_OK;
                if (!out->system_icons) {
                    memset(out->icons, 0, sizeof(out->icons));
                    if (icons == APPLE_ICONS_ATLAS_FULL)
                        snprintf(msg, sizeof msg, "overlay: SF Symbols do not fit a %ux%u atlas, geometric icons",
                                 (unsigned)out->width, (unsigned)out->height);
                    else if (missing != NULL)
                        snprintf(msg, sizeof msg, "overlay: SF Symbol '%s' unavailable, geometric icons", missing);
                    else
                        snprintf(msg, sizeof msg, "overlay: SF Symbols unavailable, geometric icons");
                    rsx_overlay_log_write(RSX_OVERLAY_LOG_WARN, msg);
                }
                return 1;
            }
            rsx_overlay_assets_free(out);
        }
    }
    return rsx_overlay_assets_build_embedded(out, scale);
}
