/* rsx_core_pipe: bodies moved verbatim from rsx_metal_backend.m
 * (metal_pipe_blend_key, metal_pipe_ds_desc, metal_pipe_ds_key,
 * metal_pso_state_key, metal_map_cmp, metal_map_blend_factor,
 * metal_map_blend_op, metal_map_stencil_op, metal_map_color_mask, the blend
 * split in metal_apply_blend_to_attachment, metal_wrap_from_gcm and the
 * sampler key / linear-filter predicates of metal_sampler_for). See
 * .superpowers/sdd/2026-09-23-vulkan-v1a-rsx-core-pure/task-3-brief.md for
 * the anchors and the oracle test that proves bit-for-bit equivalence. */
#include "rsx_core_pipe.h"

#include <string.h>

u32 rsx_core_blend_key(const rsx_core_pipe* p, int disable_blend)
{
    if (disable_blend)
        return 0u;
    u32 k = p->blend_enable ? 1u : 0u;
    k ^= p->blend_sfactor * 0x9E3779B1u;
    k ^= p->blend_dfactor * 0x85EBCA6Bu;
    k ^= p->blend_equation * 0xC2B2AE35u;
    k ^= p->color_mask;
    return k;
}

/* RSX stencil: with TWO_SIDED_STENCIL_TEST_ENABLE off both faces use the front
 * parameters; on, back faces use the BACK_STENCIL_* set (z-fail shadow volumes
 * increment on one face and decrement on the other). */
void rsx_core_pipe_ds_desc(const rsx_core_pipe* p, int force_write, u32 dmode, rsx_core_ds_desc* o)
{
    memset(o, 0, sizeof(*o));
    o->depth_test = p->depth_test_enable ? 1u : 0u;
    o->depth_write_mask = p->depth_mask ? 1u : 0u;
    o->depth_func = p->depth_func;
    o->force_write = force_write ? 1u : 0u;
    o->dmode = dmode;
    if (!p->stencil_test_enable) return;
    o->stencil_en = 1u;
    o->f_func = p->stencil_func;
    o->f_fail = p->stencil_op_fail;
    o->f_zfail = p->stencil_op_zfail;
    o->f_zpass = p->stencil_op_zpass;
    o->f_rmask = p->stencil_mask & 0xFFu;
    o->f_wmask = p->stencil_write_mask & 0xFFu;
    if (p->two_sided_stencil) {
        o->b_func = p->back_stencil_func;
        o->b_fail = p->back_stencil_op_fail;
        o->b_zfail = p->back_stencil_op_zfail;
        o->b_zpass = p->back_stencil_op_zpass;
        o->b_rmask = p->back_stencil_mask & 0xFFu;
        o->b_wmask = p->back_stencil_write_mask & 0xFFu;
    } else {
        o->b_func = o->f_func;   o->b_fail = o->f_fail;
        o->b_zfail = o->f_zfail; o->b_zpass = o->f_zpass;
        o->b_rmask = o->f_rmask; o->b_wmask = o->f_wmask;
    }
}

u32 rsx_core_ds_key(const rsx_core_ds_desc* d)
{
    u32 h = 2166136261u;
    const unsigned char* b = (const unsigned char*)d;
    for (size_t i = 0; i < sizeof(*d); i++) { h ^= b[i]; h *= 16777619u; }
    return h;
}

u32 rsx_core_pso_state_key(u32 blend_key, const rsx_core_pipe* p, int mrt_n, int atest_on)
{
    return ((blend_key * 0x9E3779B1u) ^ p->color_mask) ^
           ((u32)(mrt_n - 1) * 0x51ED270Bu) ^
           ((p->alpha_test_enable && atest_on) ? 0xA17E57C5u : 0u);
}

/* GL/NV compare enums used by GCM (0x0200..). */
rsx_core_cmp rsx_core_map_cmp(u32 f)
{
    switch (f) {
    case 0x0200: return RSX_CMP_NEVER;
    case 0x0201: return RSX_CMP_LESS;
    case 0x0202: return RSX_CMP_EQUAL;
    case 0x0203: return RSX_CMP_LEQUAL;
    case 0x0204: return RSX_CMP_GREATER;
    case 0x0205: return RSX_CMP_NOTEQUAL;
    case 0x0206: return RSX_CMP_GEQUAL;
    case 0x0207: return RSX_CMP_ALWAYS;
    default:     return RSX_CMP_LEQUAL;
    }
}

rsx_core_blend_factor rsx_core_map_blend_factor(u32 f, int alpha)
{
    switch (f & 0xFFFF) {
    case 0x0000: return RSX_BF_ZERO;
    case 0x0001: return RSX_BF_ONE;
    case 0x0300: return alpha ? RSX_BF_SRC_ALPHA : RSX_BF_SRC_COLOR;
    case 0x0301: return alpha ? RSX_BF_ONE_MINUS_SRC_ALPHA
                              : RSX_BF_ONE_MINUS_SRC_COLOR;
    case 0x0302: return RSX_BF_SRC_ALPHA;
    case 0x0303: return RSX_BF_ONE_MINUS_SRC_ALPHA;
    case 0x0304: return RSX_BF_DST_ALPHA;
    case 0x0305: return RSX_BF_ONE_MINUS_DST_ALPHA;
    case 0x0306: return alpha ? RSX_BF_DST_ALPHA
                              : RSX_BF_DST_COLOR;
    case 0x0307: return alpha ? RSX_BF_ONE_MINUS_DST_ALPHA
                              : RSX_BF_ONE_MINUS_DST_COLOR;
    case 0x0308: return RSX_BF_SRC_ALPHA_SAT;
    case 0x8001: return RSX_BF_CONST_COLOR;
    case 0x8002: return RSX_BF_ONE_MINUS_CONST_COLOR;
    case 0x8003: return RSX_BF_CONST_COLOR;
    case 0x8004: return RSX_BF_ONE_MINUS_CONST_COLOR;
    default:     return RSX_BF_ONE;
    }
}

rsx_core_blend_op rsx_core_map_blend_op(u32 e)
{
    switch (e & 0xFFFF) {
    case 0x8006: return RSX_BOP_ADD;
    case 0x8007: return RSX_BOP_MIN;
    case 0x8008: return RSX_BOP_MAX;
    case 0x800A: return RSX_BOP_SUB;
    case 0x800B: return RSX_BOP_REV_SUB;
    default:     return RSX_BOP_ADD;
    }
}

rsx_core_stencil_op rsx_core_map_stencil_op(u32 op)
{
    /* CELL_GCM_* values (RPCS3 gcm_enums.h). The old table was shifted by one
     * (REPLACE->Zero, INCR->Replace, DECR->IncrementClamp, INVERT at 0x1505). */
    switch (op) {
    case 0x0000: return RSX_SOP_ZERO;       /* CELL_GCM_ZERO */
    case 0x1E00: return RSX_SOP_KEEP;
    case 0x1E01: return RSX_SOP_REPLACE;
    case 0x1E02: return RSX_SOP_INCR_CLAMP;
    case 0x1E03: return RSX_SOP_DECR_CLAMP;
    case 0x150A: return RSX_SOP_INVERT;
    case 0x8507: return RSX_SOP_INCR_WRAP;
    case 0x8508: return RSX_SOP_DECR_WRAP;
    default:     return RSX_SOP_KEEP;
    }
}

/* color_mask: bit0 B, bit8 G, bit16 R, bit24 A */
u32 rsx_core_map_color_mask(u32 m)
{
    u32 w = 0;
    if (m & 0x00000001u) w |= RSX_CM_B;
    if (m & 0x00000100u) w |= RSX_CM_G;
    if (m & 0x00010000u) w |= RSX_CM_R;
    if (m & 0x01000000u) w |= RSX_CM_A;
    if (w == 0 && m == 0) return 0;
    /* Default all channels if mask looks uninitialised / full. */
    if (m == 0x01010101u || m == 0xFFFFFFFFu) return RSX_CM_ALL;
    return w ? w : RSX_CM_ALL;
}

/* filter>>16 & 7 min, filter>>24 & 7 mag. GCM address modes:
 *   1 WRAP  2 MIRROR  3 CLAMP_TO_EDGE  4 BORDER  5 CLAMP
 *   6/7/8 MIRROR_ONCE (variants) */
rsx_core_wrap rsx_core_wrap_from_gcm(u32 w)
{
    switch (w & 0xFu) {
    case 1:  return RSX_WRAP_REPEAT;
    case 2:  return RSX_WRAP_MIRROR_REPEAT;
    case 3:  return RSX_WRAP_CLAMP_EDGE;
    case 4:  return RSX_WRAP_CLAMP_BORDER;
    case 5:  return RSX_WRAP_CLAMP_EDGE;
    case 6: case 7: case 8: return RSX_WRAP_MIRROR_CLAMP_EDGE;
    default: return RSX_WRAP_CLAMP_EDGE;
    }
}

/* NV30_3D_BLEND_FUNC_{SRC,DST}: RGB in bits 0..15, ALPHA in 16..31
 * (cellGcmSetBlendFunc packs (alpha << 16) | colour). CELL_GCM_ZERO is 0, so
 * an empty high half is a REAL "alpha factor = ZERO", not an unset field --
 * see metal_apply_blend_to_attachment for the GoW2 E note on why this must
 * not fall back to the RGB factor. */
void rsx_core_split_blend(u32 sf, u32 df, u32 eq, rsx_core_blend_split* o)
{
    o->sf_rgb = sf & 0xFFFFu; o->sf_a = sf >> 16;
    o->df_rgb = df & 0xFFFFu; o->df_a = df >> 16;
    o->eq_rgb = eq & 0xFFFFu; o->eq_a = eq >> 16;
}

/* So' os campos que usamos entram na chave -- assim o cache nao explode com
 * bits que nao mudam o sampler (bias, gamma, signed remap). */
u32 rsx_core_sampler_key(u32 addr, u32 filt)
{
    return ((addr & 0xFu)) | (((addr >> 8) & 0xFu) << 4) | (((addr >> 16) & 0xFu) << 8) |
           ((((filt >> 16) & 0x7u)) << 12) | ((((filt >> 24) & 0x7u)) << 15);
}

/* GCM min: 1 NEAREST, 2 LINEAR, 3 NEAREST_MIPMAP_NEAREST, 4 LINEAR_MIPMAP_NEAREST,
 *          5 NEAREST_MIPMAP_LINEAR, 6 LINEAR_MIPMAP_LINEAR, 7 CONVOLUTION_MIN. */
int rsx_core_min_filter_linear(u32 filt)
{
    u32 minf = (filt >> 16) & 0x7u;
    return (minf == 2u || minf == 4u || minf == 6u || minf == 7u) ? 1 : 0;
}

/* GCM mag: 1 NEAREST, 2 LINEAR, 4 CONVOLUTION_MAG. */
int rsx_core_mag_filter_linear(u32 filt)
{
    u32 magf = (filt >> 24) & 0x7u;
    return (magf == 2u || magf == 4u) ? 1 : 0;
}
