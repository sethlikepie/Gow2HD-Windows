#include "rsx_depth_debug.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

u32 rsx_depth_normalize_func(u32 f)
{
    if (f >= 0x0200u && f <= 0x0207u)
        return f;
    if (f <= 7u)
        return 0x0200u + f;
    return 0x0203u; /* LEQUAL: same default metal_map_cmp used for junk */
}

u32 rsx_depth_invert_func(u32 normalized)
{
    switch (rsx_depth_normalize_func(normalized)) {
    case 0x0201u: return 0x0204u; /* LESS -> GREATER */
    case 0x0203u: return 0x0206u; /* LEQUAL -> GEQUAL */
    case 0x0204u: return 0x0201u;
    case 0x0206u: return 0x0203u;
    default:      return rsx_depth_normalize_func(normalized);
    }
}

u32 rsx_depth_effective_func(u32 rsx_func, int test_enable, int mode)
{
    switch (mode) {
    case RSX_DEPTH_DBG_ALWAYS: return 0x0207u;
    case RSX_DEPTH_DBG_LEQUAL: return 0x0203u;
    case RSX_DEPTH_DBG_GEQUAL: return 0x0206u;
    case RSX_DEPTH_DBG_INVERT:
        if (!test_enable)
            return 0x0207u;
        return rsx_depth_invert_func(rsx_depth_normalize_func(rsx_func));
    default:
        if (!test_enable)
            return 0x0207u;
        return rsx_depth_normalize_func(rsx_func);
    }
}

int rsx_depth_effective_write(int rsx_mask, int test_enable, int mode)
{
    if (mode == RSX_DEPTH_DBG_ALWAYS ||
        mode == RSX_DEPTH_DBG_LEQUAL ||
        mode == RSX_DEPTH_DBG_GEQUAL)
        return 1;
    (void)test_enable;
    return rsx_mask ? 1 : 0;
}

int rsx_depth_debug_mode(void)
{
    static int mode = -1;
    if (mode >= 0)
        return mode;
    const char *e = getenv("PS3_METAL_DEBUG_DEPTH");
    mode = RSX_DEPTH_DBG_RSX;
    if (!e || !e[0] || e[0] == '0')
        return mode;
    if (!strcmp(e, "always") || !strcmp(e, "ALWAYS") || !strcmp(e, "1"))
        mode = RSX_DEPTH_DBG_ALWAYS;
    else if (!strcmp(e, "lequal") || !strcmp(e, "less_equal") ||
             !strcmp(e, "LESS_EQUAL") || !strcmp(e, "2"))
        mode = RSX_DEPTH_DBG_LEQUAL;
    else if (!strcmp(e, "gequal") || !strcmp(e, "greater_equal") ||
             !strcmp(e, "GREATER_EQUAL") || !strcmp(e, "3"))
        mode = RSX_DEPTH_DBG_GEQUAL;
    else if (!strcmp(e, "invert") || !strcmp(e, "INVERT") || !strcmp(e, "4"))
        mode = RSX_DEPTH_DBG_INVERT;
    else
        mode = RSX_DEPTH_DBG_RSX;
    return mode;
}

const char *rsx_depth_func_name(u32 f)
{
    switch (rsx_depth_normalize_func(f)) {
    case 0x0200u: return "Never";
    case 0x0201u: return "Less";
    case 0x0202u: return "Equal";
    case 0x0203u: return "LessEqual";
    case 0x0204u: return "Greater";
    case 0x0205u: return "NotEqual";
    case 0x0206u: return "GreaterEqual";
    case 0x0207u: return "Always";
    default:      return "?";
    }
}

const char *rsx_depth_debug_mode_name(int mode)
{
    switch (mode) {
    case RSX_DEPTH_DBG_ALWAYS: return "always";
    case RSX_DEPTH_DBG_LEQUAL: return "lequal";
    case RSX_DEPTH_DBG_GEQUAL: return "gequal";
    case RSX_DEPTH_DBG_INVERT: return "invert";
    default:                   return "rsx";
    }
}

int rsx_debug_clear_frame_on(void)
{
    static int on = -1;
    if (on < 0) {
        const char *e = getenv("PS3_METAL_DEBUG_CLEAR_FRAME");
        on = (e && e[0] && e[0] != '0') ? 1 : 0;
    }
    return on;
}

int rsx_depth_fallback_should_clear(int first_this_frame, int guest_clear_depth)
{
    if (first_this_frame)
        return 1;
    return guest_clear_depth ? 1 : 0;
}

int rsx_cull_mode(int enable, u32 face)
{
    if (!enable)
        return RSX_CULL_NONE;
    if (face == 0x0404u)
        return RSX_CULL_FRONT;
    if (face == 0x0405u || face == 1u)
        return RSX_CULL_BACK;
    return RSX_CULL_NONE;
}

int rsx_metal_winding_is_ccw(u32 front_face)
{
    /* Guest CW (0 / 0x0900) → Metal CW. The Y-down invert culled GoW2
     * title/UI quads in-boot (st620 stuck at 0, no menu). Flip via env. */
    return (front_face == 0x0901u || front_face == 1u) ? 1 : 0;
}

int rsx_debug_clear_frame_take(int color_idx, int main_idx, int *done)
{
    if (!done || *done)
        return 0;
    if (color_idx < 0 || main_idx < 0)
        return 0;
    if (color_idx != main_idx)
        return 0;
    *done = 1;
    return 1;
}

void rsx_depth_dump_begin(u32 begin_id, int rsx_en, u32 rsx_func, int rsx_write,
                          float clip_min, float clip_max, float clear_depth)
{
    static int on = -1;
    static int n;
    if (on < 0) {
        const char *e = getenv("PS3_TRACE_DEPTH");
        on = (e && e[0] && e[0] != '0') ? 1 : 0;
    }
    if (!on && rsx_depth_debug_mode() == RSX_DEPTH_DBG_RSX)
        return;
    n++;
    if (n > 48 && (n % 32) != 0)
        return;
    int mode = rsx_depth_debug_mode();
    u32 mf = rsx_depth_effective_func(rsx_func, rsx_en, mode);
    int mw = rsx_depth_effective_write(rsx_write, rsx_en, mode);
    fprintf(stderr,
            "[DEPTH BEGIN] id=%u rsxEn=%d rsxFunc=0x%X (%s) rsxWrite=%d "
            "metalCmp=%s metalWrite=%d clearDepth=%g znear=%g zfar=%g "
            "mode=%s metalVpZ=0..1\n",
            begin_id, rsx_en, rsx_func, rsx_depth_func_name(rsx_func),
            rsx_write, rsx_depth_func_name(mf), mw, clear_depth,
            clip_min, clip_max, rsx_depth_debug_mode_name(mode));
    fflush(stderr);
}
