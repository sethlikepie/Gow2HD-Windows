/*
 * ps3recomp - HLSL dialect -> Vulkan GLSL 450
 *
 * The NV40 decompilers emit one HLSL dialect (D3D12 consumes it as is, Metal
 * through a string rewrite). For Vulkan the dialect is kept and made legal
 * GLSL by a compat prelude (type macros + intrinsic overloads) plus three
 * textual rewrites the prelude cannot express: `(float4)0` casts, vector
 * compares `(float4)((A) op (B))`, and `rsx_tex[N].Sample*(rsx_samp[N], ...)`.
 * `input.` becomes `rsx_in.` because `input` is a reserved word in GLSL.
 */
#ifndef PS3RECOMP_RSX_GLSL_DIALECT_H
#define PS3RECOMP_RSX_GLSL_DIALECT_H

#include "ps3emu/ps3types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Place right after "#version 450\n". */
extern const char RSX_GLSL_PRELUDE[];

/* Place right after RSX_GLSL_PRELUDE, fragment shaders only: `ddx`/`ddy`
 * (dFdx/dFdy-backed). Vulkan GLSL 450 rejects dFdx/dFdy outside the fragment
 * stage even when the declaring function is never called, so a vertex (or
 * other non-fragment) shader must not include this block. */
extern const char RSX_GLSL_PRELUDE_FRAG[];

/* Rewrite a decompiler body. 0 on success, -1 on null args or overflow
 * (never leaves a truncated string that looks valid: out[0] = '\0'). */
int rsx_glsl_rewrite_body(const char* in, char* out, u32 out_size);

#ifdef __cplusplus
}
#endif
#endif /* PS3RECOMP_RSX_GLSL_DIALECT_H */
