/* HLSL-dialect -> GLSL rewrite: exact-string cases taken from what the NV40
 * decompilers emit (rsx_fp_decompiler.c / rsx_vp_decompiler.c). */
#include "rsx_glsl_dialect.h"
#include <stdio.h>
#include <string.h>

static int fails = 0;
static void expect_rw(const char* name, const char* in, const char* want)
{
    char out[4096];
    int rc = rsx_glsl_rewrite_body(in, out, sizeof out);
    if (rc != 0 || strcmp(out, want) != 0) {
        printf("FAIL %s\n  in:   %s\n  want: %s\n  got:  %s (rc=%d)\n", name, in, want,
               rc == 0 ? out : "", rc);
        fails++;
    } else {
        printf("[PASS] %s\n", name);
    }
}

int main(void)
{
    expect_rw("rewrite_unroll_and_zero_cast",
        "    [unroll] for (int _i = 0; _i < 48; _i++) { r[_i] = (float4)0; }\n",
        "    for (int _i = 0; _i < 48; _i++) { r[_i] = float4(0); }\n");
    expect_rw("rewrite_compare_fp_spacing",
        "r[0] = (float4)((r[1].xyzw) <  (r[2].xyzw));",
        "r[0] = rsx_slt((r[1].xyzw), (r[2].xyzw));");
    expect_rw("rewrite_compare_vp_nospace",
        "r[3] = (float4)((v[0].xyzw)>=(vp_c[4].xyzw));",
        "r[3] = rsx_sge((v[0].xyzw), (vp_c[4].xyzw));");
    expect_rw("rewrite_compare_all_ops",
        "(float4)((a)<=(b)) (float4)((a)>(b)) (float4)((a)==(b)) (float4)((a)!=(b))",
        "rsx_sle((a), (b)) rsx_sgt((a), (b)) rsx_seq((a), (b)) rsx_sne((a), (b))");
    expect_rw("rewrite_compare_nested",
        "(float4)(((r[0]).x) < (abs(r[1]).x))",
        "rsx_slt(((r[0]).x), (abs(r[1]).x))");
    /* rewrite_compare_input_operand: try_vec_compare used to copy each
     * operand slice raw (g_putn), so a compare between two `input.` sources
     * (SLT/SGE/... against COL0/TC*) skipped the `input.` -> `rsx_in.`
     * rewrite inside the operands -- shaderc then rejected the reserved word
     * `input`. Both operands must go through the full rewriter. */
    expect_rw("rewrite_compare_input_operand",
        "(float4)(((input.col0).xyzw) <  ((input.tc1).xyzw))",
        "rsx_slt(((rsx_in.col0).xyzw), ((rsx_in.tc1).xyzw))");
    /* rewrite_compare_input_operand_paren: same bug, but with the operand
     * additionally wrapped in an abs() call, so the recursive rewrite must
     * still find `input.` past the extra nesting the existing
     * rewrite_compare_nested case already proves match_paren handles. */
    expect_rw("rewrite_compare_input_operand_paren",
        "(float4)(((input.tc2).x) < (abs(input.col1).x))",
        "rsx_slt(((rsx_in.tc2).x), (abs(rsx_in.col1).x))");
    expect_rw("rewrite_tex_sample",
        "rsx_tex[3].Sample(rsx_samp[3], (r[1]).xy)",
        "texture(rsx_tex[3], (r[1]).xy)");
    expect_rw("rewrite_tex_level_bias_grad",
        "rsx_tex[12].SampleLevel(rsx_samp[12], u, l) "
        "rsx_tex[0].SampleBias(rsx_samp[0], u, b) "
        "rsx_tex[1].SampleGrad(rsx_samp[1], u, dx, dy)",
        "textureLod(rsx_tex[12], u, l) texture(rsx_tex[0], u, b) textureGrad(rsx_tex[1], u, dx, dy)");
    expect_rw("rewrite_kil",
        "    if (any((r[0]).xyzw < 0.0)) discard;\n",
        "    if (any(lessThan(r[0], vec4(0.0)))) discard;\n");
    expect_rw("rewrite_input_boundary",
        "r[0] = input.col0 + myinput.x + input.tc3;",
        "r[0] = rsx_in.col0 + myinput.x + rsx_in.tc3;");
    expect_rw("rewrite_passthrough",
        "{ float _s = dot((r[0]).xyz,(r[1]).xyz); r[2].x = _s; }",
        "{ float _s = dot((r[0]).xyz,(r[1]).xyz); r[2].x = _s; }");

    /* rewrite_overflow: output too small -> -1, never a truncated string */
    {
        char tiny[8];
        int rc = rsx_glsl_rewrite_body("r[0] = (float4)0;", tiny, sizeof tiny);
        if (rc != -1) { printf("FAIL rewrite_overflow rc=%d\n", rc); fails++; }
        else printf("[PASS] rewrite_overflow\n");
    }
    /* prelude sanity: the macros and the compare helpers exist */
    if (!strstr(RSX_GLSL_PRELUDE, "#define float4 vec4") ||
        !strstr(RSX_GLSL_PRELUDE, "vec4 rsx_slt(vec4 a, vec4 b)") ||
        !strstr(RSX_GLSL_PRELUDE, "saturate(vec4 x)")) {
        printf("FAIL prelude_shape\n"); fails++;
    } else printf("[PASS] prelude_shape\n");

    /* stage split: dFdx/dFdy-backed ddx/ddy live only in the fragment-only
     * block (Vulkan GLSL rejects them outside the fragment stage even
     * unused), never in the shared prelude every stage includes. */
    if (strstr(RSX_GLSL_PRELUDE, "dFdx") != NULL) {
        printf("FAIL prelude_no_frag_deriv\n"); fails++;
    } else printf("[PASS] prelude_no_frag_deriv\n");
    if (!strstr(RSX_GLSL_PRELUDE_FRAG, "vec4 ddx(vec4 x)")) {
        printf("FAIL prelude_frag_has_deriv\n"); fails++;
    } else printf("[PASS] prelude_frag_has_deriv\n");

    printf(fails ? "%d FAILED\n" : "ALL PASS\n", fails);
    return fails ? 1 : 0;
}
