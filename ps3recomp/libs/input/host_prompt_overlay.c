#include "host_prompt_overlay.h"
#include "host_prompts.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OVERLAY_W 1280u
#define OVERLAY_H 720u
#define MENU_GLYPH_X 40u
#define MENU_GLYPH_Y 634u
#define MENU_GLYPH_CX 64u
#define MENU_GLYPH_CY 648u

static uint8_t s_frame[OVERLAY_W * OVERLAY_H * 4u];
static host_prompt_set s_rendered = (host_prompt_set)-1;

static void pixel(unsigned x, unsigned y, unsigned r, unsigned g, unsigned b, unsigned a)
{
    if (x >= OVERLAY_W || y >= OVERLAY_H) return;
    uint8_t* p = &s_frame[(y * OVERLAY_W + x) * 4u];
    p[0] = (uint8_t)b; p[1] = (uint8_t)g; p[2] = (uint8_t)r; p[3] = (uint8_t)a;
}

static void rect(unsigned x, unsigned y, unsigned w, unsigned h,
                 unsigned r, unsigned g, unsigned b, unsigned a)
{
    if (x >= OVERLAY_W || y >= OVERLAY_H) return;
    if (w > OVERLAY_W - x) w = OVERLAY_W - x;
    if (h > OVERLAY_H - y) h = OVERLAY_H - y;
    for (unsigned yy = y; yy < y + h; yy++)
        for (unsigned xx = x; xx < x + w; xx++) pixel(xx, yy, r, g, b, a);
}

static const uint8_t* glyph(char c)
{
    static const uint8_t blank[7] = {0,0,0,0,0,0,0};
    static const uint8_t glyphs[][7] = {
        /* A */ {14,17,17,31,17,17,17}, /* B */ {30,17,17,30,17,17,30},
        /* C */ {15,16,16,16,16,16,15},
        /* D */ {30,17,17,17,17,17,30}, /* E */ {31,16,16,30,16,16,31},
        /* F */ {31,16,16,30,16,16,16}, /* G */ {14,17,16,23,17,17,15},
        /* I */ {31,4,4,4,4,4,31},     /* J */ {1,1,1,1,17,17,14},
        /* K */ {17,18,20,24,20,18,17}, /* L */ {16,16,16,16,16,16,31},
        /*   */ {0,0,0,0,0,0,0},
        /* N */ {17,25,21,19,17,17,17}, /* O */ {14,17,17,17,17,17,14},
        /* P */ {30,17,17,30,16,16,16}, /* Q */ {14,17,17,17,21,18,13},
        /* R */ {30,17,17,30,20,18,17}, /* S */ {15,16,16,14,1,1,30},
        /* T */ {31,4,4,4,4,4,4},       /* U */ {17,17,17,17,17,17,14},
        /* W */ {17,17,17,21,21,21,10}, /* X */ {17,17,10,4,10,17,17},
        /* Y */ {17,17,10,4,4,4,4},
        /* 0 */ {14,17,19,21,25,17,14}, /* 1 */ {4,12,4,4,4,4,14},
        /* 2 */ {14,17,1,2,4,8,31},     /* 3 */ {30,1,1,14,1,1,30},
        /* 4 */ {2,6,10,18,31,2,2},     /* 5 */ {31,16,16,30,1,1,30}
    };
    static const char alphabet[] = "ABCDEFGIJKL NOPQRSTUWXY012345";
    for (unsigned i = 0; i < sizeof(glyphs) / sizeof(glyphs[0]); i++)
        if (alphabet[i] == c) return glyphs[i];
    return blank;
}

static void text(unsigned x, unsigned y, const char* s, unsigned scale,
                 unsigned r, unsigned g, unsigned b, unsigned a)
{
    for (; s && *s; s++, x += 6u * scale) {
        char c = *s;
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        const uint8_t* rows = glyph(c);
        for (unsigned yy = 0; yy < 7; yy++)
            for (unsigned xx = 0; xx < 5; xx++)
                if (rows[yy] & (1u << (4u - xx)))
                    rect(x + xx * scale, y + yy * scale, scale, scale, r, g, b, a);
    }
}

static void disc(unsigned cx, unsigned cy, unsigned radius,
                 unsigned r, unsigned g, unsigned b, unsigned a)
{
    for (int y = -(int)radius; y <= (int)radius; y++)
        for (int x = -(int)radius; x <= (int)radius; x++)
            if (x * x + y * y <= (int)(radius * radius))
                pixel((unsigned)((int)cx + x), (unsigned)((int)cy + y), r, g, b, a);
}

static void keycap(unsigned x, unsigned y, unsigned w, const char* label,
                   unsigned scale, unsigned r, unsigned g, unsigned b)
{
    rect(x + 2, y + 3, w, 25, 0, 0, 0, 190);
    rect(x, y, w, 23, r, g, b, 235);
    rect(x + 2, y + 2, w - 4, 19, 12, 16, 26, 245);
    text(x + (w - 6u * (unsigned)strlen(label) * scale) / 2u,
         y + 7, label, scale, 240, 245, 255, 255);
}

int host_prompt_overlay_is_menu_draw_count(unsigned draws)
{
    return draws >= 180u && draws <= 300u;
}

static void render(host_prompt_set set)
{
    memset(s_frame, 0, sizeof(s_frame));
    unsigned r = 40, g = 105, b = 220;
    if (set == HOST_PROMPT_SET_SWITCH) { r = 220; g = 50; b = 70; }
    else if (set == HOST_PROMPT_SET_XBOX) { r = 35; g = 180; b = 105; }
    else if (set == HOST_PROMPT_SET_KB) { r = 155; g = 85; b = 220; }

    /* Exact menu patch: only the original X glyph in "X = SELECT" is
     * covered. The title's equals sign, text and scene remain guest pixels. */
    if (set == HOST_PROMPT_SET_KB) {
        keycap(MENU_GLYPH_X, MENU_GLYPH_Y, 48, "SPACE", 1, r, g, b);
        return;
    }

    disc(MENU_GLYPH_CX, MENU_GLYPH_CY, 19, r, g, b, 245);
    disc(MENU_GLYPH_CX, MENU_GLYPH_CY, 16, 12, 16, 26, 245);
    {
        const host_prompt_view* view = host_prompts_view_for(set);
        const char* label = view ? view->cross : "X";
        text(MENU_GLYPH_CX - 5u, MENU_GLYPH_CY - 7u, label, 2, 240, 245, 255, 255);
    }
}

const uint8_t* host_prompt_overlay_frame(unsigned* width, unsigned* height)
{
    host_prompt_set set = host_prompts_current();
    if (s_rendered != set) { render(set); s_rendered = set; }
    if (width) *width = OVERLAY_W;
    if (height) *height = OVERLAY_H;
    return s_frame;
}
