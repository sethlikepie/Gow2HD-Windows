/*
 * Oracle test: proves the thin Metal switches Task 3 puts in
 * rsx_metal_backend.m (metal_map_cmp/metal_map_blend_factor/
 * metal_map_blend_op/metal_map_stencil_op/metal_map_color_mask/
 * metal_wrap_from_gcm, each now `switch (rsx_core_map_*(x)) { case RSX_...:
 * return MTL...; }`) return BIT-IDENTICAL MTL* values to the old bodies they
 * replaced, over every input the old code could see. This is the proof that
 * Metal pipelines are unchanged by the rsx_core_pipe extraction. See
 * .superpowers/sdd/2026-09-23-vulkan-v1a-rsx-core-pure/task-3-brief.md.
 *
 * old_metal_*: verbatim copies of rsx_metal_backend.m's metal_map_cmp,
 * metal_map_blend_factor, metal_map_blend_op, metal_map_stencil_op,
 * metal_map_color_mask, metal_wrap_from_gcm (renamed only).
 * new_metal_*: the exact thin-switch bodies Step 3 placed in the backend,
 * copied here verbatim (kept in lockstep by hand -- any future edit to one
 * must be mirrored in the other).
 */
#import <Metal/Metal.h>
#include "rsx_core_pipe.h"

#include <stdio.h>
#include <stdlib.h>

/* --------------------------------------------------------------------- *
 * old_metal_*: verbatim from rsx_metal_backend.m pre-Task-3.
 * --------------------------------------------------------------------- */

static MTLCompareFunction old_metal_map_cmp(u32 f)
{
    switch (f) {
    case 0x0200: return MTLCompareFunctionNever;
    case 0x0201: return MTLCompareFunctionLess;
    case 0x0202: return MTLCompareFunctionEqual;
    case 0x0203: return MTLCompareFunctionLessEqual;
    case 0x0204: return MTLCompareFunctionGreater;
    case 0x0205: return MTLCompareFunctionNotEqual;
    case 0x0206: return MTLCompareFunctionGreaterEqual;
    case 0x0207: return MTLCompareFunctionAlways;
    default:     return MTLCompareFunctionLessEqual;
    }
}

static MTLBlendFactor old_metal_map_blend_factor(u32 f, int alpha)
{
    switch (f & 0xFFFF) {
    case 0x0000: return MTLBlendFactorZero;
    case 0x0001: return MTLBlendFactorOne;
    case 0x0300: return alpha ? MTLBlendFactorSourceAlpha : MTLBlendFactorSourceColor;
    case 0x0301: return alpha ? MTLBlendFactorOneMinusSourceAlpha
                              : MTLBlendFactorOneMinusSourceColor;
    case 0x0302: return MTLBlendFactorSourceAlpha;
    case 0x0303: return MTLBlendFactorOneMinusSourceAlpha;
    case 0x0304: return MTLBlendFactorDestinationAlpha;
    case 0x0305: return MTLBlendFactorOneMinusDestinationAlpha;
    case 0x0306: return alpha ? MTLBlendFactorDestinationAlpha
                              : MTLBlendFactorDestinationColor;
    case 0x0307: return alpha ? MTLBlendFactorOneMinusDestinationAlpha
                              : MTLBlendFactorOneMinusDestinationColor;
    case 0x0308: return MTLBlendFactorSourceAlphaSaturated;
    case 0x8001: return MTLBlendFactorBlendColor;
    case 0x8002: return MTLBlendFactorOneMinusBlendColor;
    case 0x8003: return MTLBlendFactorBlendColor;
    case 0x8004: return MTLBlendFactorOneMinusBlendColor;
    default:     return MTLBlendFactorOne;
    }
}

static MTLBlendOperation old_metal_map_blend_op(u32 e)
{
    switch (e & 0xFFFF) {
    case 0x8006: return MTLBlendOperationAdd;
    case 0x8007: return MTLBlendOperationMin;
    case 0x8008: return MTLBlendOperationMax;
    case 0x800A: return MTLBlendOperationSubtract;
    case 0x800B: return MTLBlendOperationReverseSubtract;
    default:     return MTLBlendOperationAdd;
    }
}

static MTLStencilOperation old_metal_map_stencil_op(u32 op)
{
    switch (op) {
    case 0x0000: return MTLStencilOperationZero;       /* CELL_GCM_ZERO */
    case 0x1E00: return MTLStencilOperationKeep;
    case 0x1E01: return MTLStencilOperationReplace;
    case 0x1E02: return MTLStencilOperationIncrementClamp;
    case 0x1E03: return MTLStencilOperationDecrementClamp;
    case 0x150A: return MTLStencilOperationInvert;
    case 0x8507: return MTLStencilOperationIncrementWrap;
    case 0x8508: return MTLStencilOperationDecrementWrap;
    default:     return MTLStencilOperationKeep;
    }
}

/* color_mask: bit0 B, bit8 G, bit16 R, bit24 A */
static MTLColorWriteMask old_metal_map_color_mask(u32 m)
{
    MTLColorWriteMask w = MTLColorWriteMaskNone;
    if (m & 0x00000001u) w |= MTLColorWriteMaskBlue;
    if (m & 0x00000100u) w |= MTLColorWriteMaskGreen;
    if (m & 0x00010000u) w |= MTLColorWriteMaskRed;
    if (m & 0x01000000u) w |= MTLColorWriteMaskAlpha;
    if (w == MTLColorWriteMaskNone && m == 0) return MTLColorWriteMaskNone;
    if (m == 0x01010101u || m == 0xFFFFFFFFu) return MTLColorWriteMaskAll;
    return w ? w : MTLColorWriteMaskAll;
}

static MTLSamplerAddressMode old_metal_wrap_from_gcm(u32 w)
{
    switch (w & 0xFu) {
    case 1:  return MTLSamplerAddressModeRepeat;
    case 2:  return MTLSamplerAddressModeMirrorRepeat;
    case 3:  return MTLSamplerAddressModeClampToEdge;
    case 4:  return MTLSamplerAddressModeClampToBorderColor;
    case 5:  return MTLSamplerAddressModeClampToEdge;
    case 6: case 7: case 8: return MTLSamplerAddressModeMirrorClampToEdge;
    default: return MTLSamplerAddressModeClampToEdge;
    }
}

/* --------------------------------------------------------------------- *
 * new_metal_*: the exact thin-switch bodies Step 3 places in
 * rsx_metal_backend.m (metal_map_* now dispatch on rsx_core_map_*).
 * --------------------------------------------------------------------- */

static MTLCompareFunction new_metal_map_cmp(u32 f)
{
    switch (rsx_core_map_cmp(f)) {
    case RSX_CMP_NEVER:    return MTLCompareFunctionNever;
    case RSX_CMP_LESS:     return MTLCompareFunctionLess;
    case RSX_CMP_EQUAL:    return MTLCompareFunctionEqual;
    case RSX_CMP_LEQUAL:   return MTLCompareFunctionLessEqual;
    case RSX_CMP_GREATER:  return MTLCompareFunctionGreater;
    case RSX_CMP_NOTEQUAL: return MTLCompareFunctionNotEqual;
    case RSX_CMP_GEQUAL:   return MTLCompareFunctionGreaterEqual;
    case RSX_CMP_ALWAYS:   return MTLCompareFunctionAlways;
    default:               return MTLCompareFunctionLessEqual;
    }
}

static MTLBlendFactor new_metal_map_blend_factor(u32 f, int alpha)
{
    switch (rsx_core_map_blend_factor(f, alpha)) {
    case RSX_BF_ZERO:                  return MTLBlendFactorZero;
    case RSX_BF_ONE:                   return MTLBlendFactorOne;
    case RSX_BF_SRC_COLOR:             return MTLBlendFactorSourceColor;
    case RSX_BF_ONE_MINUS_SRC_COLOR:   return MTLBlendFactorOneMinusSourceColor;
    case RSX_BF_SRC_ALPHA:             return MTLBlendFactorSourceAlpha;
    case RSX_BF_ONE_MINUS_SRC_ALPHA:   return MTLBlendFactorOneMinusSourceAlpha;
    case RSX_BF_DST_ALPHA:             return MTLBlendFactorDestinationAlpha;
    case RSX_BF_ONE_MINUS_DST_ALPHA:   return MTLBlendFactorOneMinusDestinationAlpha;
    case RSX_BF_DST_COLOR:             return MTLBlendFactorDestinationColor;
    case RSX_BF_ONE_MINUS_DST_COLOR:   return MTLBlendFactorOneMinusDestinationColor;
    case RSX_BF_SRC_ALPHA_SAT:         return MTLBlendFactorSourceAlphaSaturated;
    case RSX_BF_CONST_COLOR:           return MTLBlendFactorBlendColor;
    case RSX_BF_ONE_MINUS_CONST_COLOR: return MTLBlendFactorOneMinusBlendColor;
    default:                           return MTLBlendFactorOne;
    }
}

static MTLBlendOperation new_metal_map_blend_op(u32 e)
{
    switch (rsx_core_map_blend_op(e)) {
    case RSX_BOP_ADD:     return MTLBlendOperationAdd;
    case RSX_BOP_MIN:     return MTLBlendOperationMin;
    case RSX_BOP_MAX:     return MTLBlendOperationMax;
    case RSX_BOP_SUB:     return MTLBlendOperationSubtract;
    case RSX_BOP_REV_SUB: return MTLBlendOperationReverseSubtract;
    default:              return MTLBlendOperationAdd;
    }
}

static MTLStencilOperation new_metal_map_stencil_op(u32 op)
{
    switch (rsx_core_map_stencil_op(op)) {
    case RSX_SOP_ZERO:       return MTLStencilOperationZero;
    case RSX_SOP_KEEP:       return MTLStencilOperationKeep;
    case RSX_SOP_REPLACE:    return MTLStencilOperationReplace;
    case RSX_SOP_INCR_CLAMP: return MTLStencilOperationIncrementClamp;
    case RSX_SOP_DECR_CLAMP: return MTLStencilOperationDecrementClamp;
    case RSX_SOP_INVERT:     return MTLStencilOperationInvert;
    case RSX_SOP_INCR_WRAP:  return MTLStencilOperationIncrementWrap;
    case RSX_SOP_DECR_WRAP:  return MTLStencilOperationDecrementWrap;
    default:                 return MTLStencilOperationKeep;
    }
}

static MTLColorWriteMask new_metal_map_color_mask(u32 m)
{
    u32 c = rsx_core_map_color_mask(m);
    MTLColorWriteMask w = MTLColorWriteMaskNone;
    if (c & RSX_CM_B) w |= MTLColorWriteMaskBlue;
    if (c & RSX_CM_G) w |= MTLColorWriteMaskGreen;
    if (c & RSX_CM_R) w |= MTLColorWriteMaskRed;
    if (c & RSX_CM_A) w |= MTLColorWriteMaskAlpha;
    return w;
}

static MTLSamplerAddressMode new_metal_wrap_from_gcm(u32 w)
{
    switch (rsx_core_wrap_from_gcm(w)) {
    case RSX_WRAP_REPEAT:             return MTLSamplerAddressModeRepeat;
    case RSX_WRAP_MIRROR_REPEAT:      return MTLSamplerAddressModeMirrorRepeat;
    case RSX_WRAP_CLAMP_EDGE:         return MTLSamplerAddressModeClampToEdge;
    case RSX_WRAP_CLAMP_BORDER:       return MTLSamplerAddressModeClampToBorderColor;
    case RSX_WRAP_MIRROR_CLAMP_EDGE:  return MTLSamplerAddressModeMirrorClampToEdge;
    default:                          return MTLSamplerAddressModeClampToEdge;
    }
}

/* --------------------------------------------------------------------- */

static int fails;
#define CHECK(name, cond) do { if (!(cond)) { printf("FAIL %s\n", name); fails++; } else printf("[PASS] %s\n", name); } while (0)

static int cmp_oracle(void)
{
    for (u32 f = 0; f <= 0xFFFFu; f++) {
        if (old_metal_map_cmp(f) != new_metal_map_cmp(f)) {
            printf("cmp mismatch f=0x%08X old=%ld new=%ld\n", f,
                   (long)old_metal_map_cmp(f), (long)new_metal_map_cmp(f));
            return 0;
        }
    }
    return 1;
}

static int blend_factor_oracle(void)
{
    for (u32 f = 0; f <= 0xFFFFu; f++) {
        for (int alpha = 0; alpha <= 1; alpha++) {
            if (old_metal_map_blend_factor(f, alpha) != new_metal_map_blend_factor(f, alpha)) {
                printf("blend_factor mismatch f=0x%08X alpha=%d old=%ld new=%ld\n", f, alpha,
                       (long)old_metal_map_blend_factor(f, alpha), (long)new_metal_map_blend_factor(f, alpha));
                return 0;
            }
        }
    }
    return 1;
}

static int blend_op_oracle(void)
{
    for (u32 e = 0; e <= 0xFFFFu; e++) {
        if (old_metal_map_blend_op(e) != new_metal_map_blend_op(e)) {
            printf("blend_op mismatch e=0x%08X old=%ld new=%ld\n", e,
                   (long)old_metal_map_blend_op(e), (long)new_metal_map_blend_op(e));
            return 0;
        }
    }
    return 1;
}

static int stencil_op_oracle(void)
{
    for (u32 op = 0; op <= 0xFFFFu; op++) {
        if (old_metal_map_stencil_op(op) != new_metal_map_stencil_op(op)) {
            printf("stencil_op mismatch op=0x%08X old=%ld new=%ld\n", op,
                   (long)old_metal_map_stencil_op(op), (long)new_metal_map_stencil_op(op));
            return 0;
        }
    }
    return 1;
}

static int wrap_oracle(void)
{
    for (u32 w = 0; w <= 0xFFFFu; w++) {
        if (old_metal_wrap_from_gcm(w) != new_metal_wrap_from_gcm(w)) {
            printf("wrap mismatch w=0x%08X old=%ld new=%ld\n", w,
                   (long)old_metal_wrap_from_gcm(w), (long)new_metal_wrap_from_gcm(w));
            return 0;
        }
    }
    return 1;
}

static int color_mask_oracle(void)
{
    static const u32 specials[] = { 0x00000000u, 0x01010101u, 0xFFFFFFFFu,
                                     0x00000001u, 0x00000100u, 0x00010000u, 0x01000000u };
    for (size_t i = 0; i < sizeof(specials) / sizeof(specials[0]); i++) {
        u32 m = specials[i];
        if (old_metal_map_color_mask(m) != new_metal_map_color_mask(m)) {
            printf("color_mask mismatch (special) m=0x%08X old=%lu new=%lu\n", m,
                   (unsigned long)old_metal_map_color_mask(m), (unsigned long)new_metal_map_color_mask(m));
            return 0;
        }
    }
    srand(222);
    for (int i = 0; i < 1000000; i++) {
        u32 m = ((u32)rand() << 16) ^ (u32)rand();
        if (old_metal_map_color_mask(m) != new_metal_map_color_mask(m)) {
            printf("color_mask mismatch i=%d m=0x%08X old=%lu new=%lu\n", i, m,
                   (unsigned long)old_metal_map_color_mask(m), (unsigned long)new_metal_map_color_mask(m));
            return 0;
        }
    }
    return 1;
}

int main(void)
{
    fails = 0;
    CHECK("cmp_oracle", cmp_oracle());
    CHECK("blend_factor_oracle", blend_factor_oracle());
    CHECK("blend_op_oracle", blend_op_oracle());
    CHECK("stencil_op_oracle", stencil_op_oracle());
    CHECK("wrap_oracle", wrap_oracle());
    CHECK("color_mask_oracle", color_mask_oracle());
    if (fails) { printf("%d FAILURES\n", fails); return 1; }
    printf("ALL PASS\n");
    return 0;
}
