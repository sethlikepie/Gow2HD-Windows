/* rsx_core_pipe: portable C pipeline state, keys and neutral enum maps
 * shared by every RSX backend (Metal today, Vulkan later). Moved verbatim
 * out of rsx_metal_backend.m -- see
 * .superpowers/sdd/2026-09-23-vulkan-v1a-rsx-core-pure/task-3-brief.md.
 *
 * rsx_core_pipe / rsx_core_ds_desc keep the SAME field order and types as
 * the old MetalPipeState / MetalDsDesc: the DS key hashes the raw bytes of
 * the desc (FNV over sizeof) and the desc is compared with memcmp elsewhere,
 * so any layout change would change keys. The Metal file now does
 * `typedef rsx_core_pipe MetalPipeState;` / `typedef rsx_core_ds_desc
 * MetalDsDesc;` instead of redefining the structs.
 *
 * No ObjC, no Metal types, no mutable global state: the sampler cache,
 * s_samp_cache and every MTL object stay in the Metal file. Env/debug gates
 * (PS3_METAL_DEBUG_DISABLE_BLEND, PS3_METAL_DEBUG_FORCE_DEPTH_WRITE,
 * PS3_METAL_ALPHA_TEST, rsx_depth_debug_mode, s_mrt_n) are read once in the
 * backend and passed in as plain parameters. */
#ifndef PS3RECOMP_RSX_CORE_PIPE_H
#define PS3RECOMP_RSX_CORE_PIPE_H

#include "ps3emu/ps3types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* M5 -- RSX pipeline state snapshot applied on present/draw. Field order and
 * types match the old MetalPipeState exactly. */
typedef struct {
    int  blend_enable;
    u32  blend_sfactor, blend_dfactor, blend_equation;
    u32  blend_color;
    int  depth_test_enable;
    u32  depth_func;
    int  depth_mask;
    int  stencil_test_enable;
    u32  stencil_func, stencil_ref, stencil_mask;
    u32  stencil_op_fail, stencil_op_zfail, stencil_op_zpass;
    u32  stencil_write_mask;
    int  two_sided_stencil;
    u32  back_stencil_func, back_stencil_ref, back_stencil_mask, back_stencil_write_mask;
    u32  back_stencil_op_fail, back_stencil_op_zfail, back_stencil_op_zpass;
    u32  color_mask; /* bits 0=B, 8=G, 16=R, 24=A */
    int  alpha_test_enable;
    u32  alpha_func, alpha_ref;
    u32  scissor_x, scissor_y, scissor_w, scissor_h;
    int  has_scissor;
    /* Key used to rebuild fixed PSO when blend/mask changes. */
    u32  blend_key;
    u32  ds_key;
} rsx_core_pipe;

/* Everything a depth/stencil state is built from. Field order and types
 * match the old MetalDsDesc exactly -- compared whole (memcmp) and hashed
 * whole (FNV) elsewhere. */
typedef struct {
    u32 depth_test, depth_write_mask, depth_func, force_write, dmode;
    u32 stencil_en;
    u32 f_func, f_fail, f_zfail, f_zpass, f_rmask, f_wmask;
    u32 b_func, b_fail, b_zfail, b_zpass, b_rmask, b_wmask;
} rsx_core_ds_desc;

u32  rsx_core_blend_key(const rsx_core_pipe* p, int disable_blend);
void rsx_core_pipe_ds_desc(const rsx_core_pipe* p, int force_write, u32 dmode, rsx_core_ds_desc* o);
u32  rsx_core_ds_key(const rsx_core_ds_desc* d);
u32  rsx_core_pso_state_key(u32 blend_key, const rsx_core_pipe* p, int mrt_n, int atest_on);

/* Neutral enums for the GL/NV-style constants GCM uses -- every backend maps
 * these to its own API types. */
typedef enum {
    RSX_CMP_NEVER, RSX_CMP_LESS, RSX_CMP_EQUAL, RSX_CMP_LEQUAL, RSX_CMP_GREATER,
    RSX_CMP_NOTEQUAL, RSX_CMP_GEQUAL, RSX_CMP_ALWAYS
} rsx_core_cmp;

typedef enum {
    RSX_BF_ZERO, RSX_BF_ONE, RSX_BF_SRC_COLOR, RSX_BF_ONE_MINUS_SRC_COLOR, RSX_BF_SRC_ALPHA,
    RSX_BF_ONE_MINUS_SRC_ALPHA, RSX_BF_DST_ALPHA, RSX_BF_ONE_MINUS_DST_ALPHA, RSX_BF_DST_COLOR,
    RSX_BF_ONE_MINUS_DST_COLOR, RSX_BF_SRC_ALPHA_SAT, RSX_BF_CONST_COLOR,
    RSX_BF_ONE_MINUS_CONST_COLOR
} rsx_core_blend_factor;

typedef enum { RSX_BOP_ADD, RSX_BOP_MIN, RSX_BOP_MAX, RSX_BOP_SUB, RSX_BOP_REV_SUB } rsx_core_blend_op;

typedef enum {
    RSX_SOP_ZERO, RSX_SOP_KEEP, RSX_SOP_REPLACE, RSX_SOP_INCR_CLAMP, RSX_SOP_DECR_CLAMP,
    RSX_SOP_INVERT, RSX_SOP_INCR_WRAP, RSX_SOP_DECR_WRAP
} rsx_core_stencil_op;

typedef enum {
    RSX_WRAP_REPEAT, RSX_WRAP_MIRROR_REPEAT, RSX_WRAP_CLAMP_EDGE, RSX_WRAP_CLAMP_BORDER,
    RSX_WRAP_MIRROR_CLAMP_EDGE
} rsx_core_wrap;

enum { RSX_CM_R = 1, RSX_CM_G = 2, RSX_CM_B = 4, RSX_CM_A = 8, RSX_CM_ALL = 15 };

rsx_core_cmp          rsx_core_map_cmp(u32 f);                    /* default LEQUAL */
rsx_core_blend_factor rsx_core_map_blend_factor(u32 f, int alpha); /* default ONE */
rsx_core_blend_op     rsx_core_map_blend_op(u32 e);               /* default ADD */
rsx_core_stencil_op   rsx_core_map_stencil_op(u32 op);            /* default KEEP */
u32                   rsx_core_map_color_mask(u32 m);             /* RSX_CM_* bits */
rsx_core_wrap         rsx_core_wrap_from_gcm(u32 w);              /* default CLAMP_EDGE */

/* NV30_3D_BLEND_FUNC_{SRC,DST}: RGB in bits 0..15, ALPHA in 16..31. A zero
 * alpha half is a real "alpha factor = ZERO" (see
 * metal_apply_blend_to_attachment's E note), not an unset field falling
 * back to the RGB factor. */
typedef struct { u32 sf_rgb, sf_a, df_rgb, df_a, eq_rgb, eq_a; } rsx_core_blend_split;
void rsx_core_split_blend(u32 sf, u32 df, u32 eq, rsx_core_blend_split* o);

u32 rsx_core_sampler_key(u32 addr, u32 filt);
int rsx_core_min_filter_linear(u32 filt); /* GCM min 2,4,6,7 */
int rsx_core_mag_filter_linear(u32 filt); /* GCM mag 2,4 */

#ifdef __cplusplus
}
#endif

#endif /* PS3RECOMP_RSX_CORE_PIPE_H */
