/*
 * Diagnostic mapping of RSX/GCM depth compare → the compare Metal should use.
 * Overrides are env-gated (PS3_METAL_DEBUG_DEPTH); default is the RSX value.
 */
#ifndef RSX_DEPTH_DEBUG_H
#define RSX_DEPTH_DEBUG_H

#include "ps3emu/ps3types.h"

enum {
    RSX_DEPTH_DBG_RSX    = 0, /* B0: translate the guest func, no override */
    RSX_DEPTH_DBG_ALWAYS = 1, /* B1: Always, depthWrite ON */
    RSX_DEPTH_DBG_LEQUAL = 2, /* B2: LessEqual, depthWrite ON */
    RSX_DEPTH_DBG_GEQUAL = 3, /* GreaterEqual, depthWrite ON */
    RSX_DEPTH_DBG_INVERT = 4  /* flip Less↔Greater / LEqual↔GEqual only */
};

/* GCM 0x0200..0x0207, or the compact 0..7 form, → 0x0200..0x0207. */
u32 rsx_depth_normalize_func(u32 f);
u32 rsx_depth_invert_func(u32 normalized);
u32 rsx_depth_effective_func(u32 rsx_func, int test_enable, int mode);
int rsx_depth_effective_write(int rsx_mask, int test_enable, int mode);

/* PS3_METAL_DEBUG_DEPTH: unset/rsx=0, always=1, lequal=2, gequal=3, invert=4. */
int         rsx_depth_debug_mode(void);
const char *rsx_depth_func_name(u32 f);
const char *rsx_depth_debug_mode_name(int mode);

void rsx_depth_dump_begin(u32 begin_id, int rsx_en, u32 rsx_func, int rsx_write,
                          float clip_min, float clip_max, float clear_depth);

/* PS3_METAL_DEBUG_CLEAR_FRAME: one black clear of the main color target
 * on the first 3D pass of the frame. Intermediates stay Load. */
int rsx_debug_clear_frame_on(void);
/* Returns 1 once when color_idx is the known main target. main_idx < 0
 * never fires (do not guess an intermediate). *done is the per-frame latch. */
int rsx_debug_clear_frame_take(int color_idx, int main_idx, int *done);

/* Fallback depth (no guest zeta): Clear only on first use this frame, or
 * when the guest CLEAR_SURFACE included depth. Later passes Load. */
int rsx_depth_fallback_should_clear(int first_this_frame, int guest_clear_depth);

/* GCM cull: CELL_GCM_FRONT=0x0404 BACK=0x0405 FRONT_AND_BACK=0x0408.
 * Compact 1 = BACK (rsx_state_init). FRONT_AND_BACK and unknown → none. */
enum {
    RSX_CULL_NONE  = 0,
    RSX_CULL_FRONT = 1,
    RSX_CULL_BACK  = 2
};
int rsx_cull_mode(int enable, u32 face);

/* Guest FRONT_FACE: 0x0900/0 = CW, 0x0901/1 = CCW. Returns 1 for
 * MTLWindingCounterClockwise. In-boot (GoW2 title/UI) matches the guest
 * value; PS3_METAL_CULL_FLIP inverts if a title is inside-out. */
int rsx_metal_winding_is_ccw(u32 front_face);

#endif
