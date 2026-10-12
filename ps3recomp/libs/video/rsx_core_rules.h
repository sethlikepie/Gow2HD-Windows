/* rsx_core_rules: portable C rules shared by every RSX backend (Metal today,
 * Vulkan later) -- EA resolution, viewport, per-draw scissor, alpha ref,
 * render-target dims and MRT target selection. Moved verbatim out of
 * rsx_metal_backend.m -- see
 * .superpowers/sdd/2026-09-23-vulkan-v1a-rsx-core-pure/task-2-brief.md.
 *
 * These run once per draw / once per render-target change, not per vertex or
 * per texel, so they stay out-of-line (unlike rsx_core_decode.h's static
 * inline hot-path helpers). No ObjC, no Metal types, no mutable global
 * state: env gates and lookup tables stay in the backend and are passed in
 * as plain parameters (`io2ea`, `mrt_on`, `so_on`, `scissor_fix`). */
#ifndef PS3RECOMP_RSX_CORE_RULES_H
#define PS3RECOMP_RSX_CORE_RULES_H

#include "ps3emu/ps3types.h"
#include "rsx_commands.h"

#ifdef __cplusplus
extern "C" {
#endif

/* gcm_io_offset_to_ea is passed in so core does not link the GCM HLE. */
typedef int (*rsx_core_io2ea_fn)(u32 io_offset, u32* ea);

/* *unresolved=1 when it fell to the flat offset (caller logs); 0 otherwise.
 * unresolved may be NULL. */
u32 rsx_core_resolve_fp_ea(const rsx_state* s, rsx_core_io2ea_fn io2ea, int* unresolved);
u32 rsx_core_resolve_tex_ea(const rsx_texture_state* t, rsx_core_io2ea_fn io2ea);

/* Viewport: rect (has_vp, vp[4]) or full target, then VIEWPORT_SCALE/OFFSET
 * when so_on && seen==3 && sx>0 && sy<0. out = x,y,w,h */
void rsx_core_viewport(int has_vp, const float vp[4], u32 tw, u32 th,
                       const float vso[8], u32 vso_seen, int so_on, double out[4]);

/* Per-draw scissor: returns 0 draw with out[4]; 1 skip AND count a scissor
 * reject; 2 skip without counting. */
int rsx_core_scissor_per_draw(const u32 sc[4], u32 tw, u32 th, int scissor_fix, u32 out[4]);

float rsx_core_alpha_ref(u32 surface_format, u32 alpha_ref);

u32 rsx_core_clamp_dim(u32 v, u32 def);
void rsx_core_rt_dims(const rsx_state* s, u32 def_w, u32 def_h, u32* w, u32* h);

/* rtt[4] target order (A=0..D=3), *nrtt, cloc[4] 0 local / 1 main, from
 * SET_SURFACE_COLOR_TARGET + DMA. */
void rsx_core_mrt_targets(const rsx_state* s, int mrt_on, int rtt[4], int* nrtt, u32 cloc[4]);

#ifdef __cplusplus
}
#endif

#endif /* PS3RECOMP_RSX_CORE_RULES_H */
