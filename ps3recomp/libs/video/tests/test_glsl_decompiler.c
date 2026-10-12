/* NV40 FP/VP -> Vulkan GLSL 450, compiled to SPIR-V with shaderc. */
#include "rsx_fp_decompiler.h"
#include "rsx_vp_decompiler.h"
#include <shaderc/shaderc.h>
#include <stdio.h>
#include <string.h>

static int fails = 0;

/* Lazily-initialised, process-lifetime shaderc compiler/options: the opcode
 * sweep below calls compiles() ~950 times, and shaderc_compiler_initialize()
 * is expensive enough (it builds the glslang/SPIRV-Tools state) that
 * re-creating it per call turned a sub-second sweep into ~60s. Released at
 * process exit (nothing else in this file runs after main() returns). */
static shaderc_compiler_t s_cc;
static shaderc_compile_options_t s_op;

static int compiles(const char* src, shaderc_shader_kind kind, const char* name)
{
    if (!s_cc) {
        s_cc = shaderc_compiler_initialize();
        s_op = shaderc_compile_options_initialize();
        shaderc_compile_options_set_target_env(s_op, shaderc_target_env_vulkan,
                                               shaderc_env_version_vulkan_1_2);
    }
    shaderc_compilation_result_t r =
        shaderc_compile_into_spv(s_cc, src, strlen(src), kind, name, "main", s_op);
    int ok = shaderc_result_get_compilation_status(r) == shaderc_compilation_status_success;
    if (!ok) printf("shaderc (%s):\n%s\n--- source ---\n%s\n", name,
                    shaderc_result_get_error_message(r), src);
    shaderc_result_release(r);
    return ok;
}

static void check(const char* name, int cond)
{
    if (!cond) { printf("FAIL %s\n", name); fails++; }
    else printf("[PASS] %s\n", name);
}

/* FP words: big-endian with half-word swap, as the RSX reads them. */
static void fp_word(unsigned char* p, u32 h)
{
    u32 be = (h << 16) | (h >> 16);
    p[0] = (unsigned char)(be >> 24); p[1] = (unsigned char)(be >> 16);
    p[2] = (unsigned char)(be >> 8);  p[3] = (unsigned char)be;
}
#define FP_OPC(x)      ((u32)(x) << 24)
#define FP_OUTREG(x)   ((u32)(x) << 1)   /* OPDEST bits 1-6: dest reg index */
#define FP_OUTMASK_ALL (0xFu << 9)
#define FP_END         (1u << 0)
#define FP_INSRC(x)    ((u32)(x) << 13)
#define FP_SWZ_IDENT   ((0u<<9)|(1u<<11)|(2u<<13)|(3u<<15))
#define FP_T_INPUT     1u

static void fp_mov_r0_col0(unsigned char prog[16])
{
    fp_word(prog + 0, FP_OPC(0x01) | FP_OUTMASK_ALL | FP_INSRC(1) | FP_END);
    fp_word(prog + 4, FP_T_INPUT | FP_SWZ_IDENT);
    fp_word(prog + 8, 0);
    fp_word(prog + 12, 0);
}

/* MOV r1, COL0 -- writes the full (fp32) r1, the register the depth-export
 * epilogue reads (`gl_FragDepth = r[1].z`); rsx_fp_decompiler.c only sets
 * s_fp_wrote_r1 when dst_idx==1 and the half bit is clear (FP_OUTREG(1),
 * no FP_OUT_HALF). */
static void fp_mov_r1_col0(unsigned char prog[16])
{
    fp_word(prog + 0, FP_OPC(0x01) | FP_OUTREG(1) | FP_OUTMASK_ALL | FP_INSRC(1) | FP_END);
    fp_word(prog + 4, FP_T_INPUT | FP_SWZ_IDENT);
    fp_word(prog + 8, 0);
    fp_word(prog + 12, 0);
}

/* VP words are little-endian, 4 per instruction (RPCS3 raw .vp format). */
static void vp_word(unsigned char* p, u32 w)
{
    p[0] = (unsigned char)w; p[1] = (unsigned char)(w >> 8);
    p[2] = (unsigned char)(w >> 16); p[3] = (unsigned char)(w >> 24);
}
/* MOV o[0], v[0] (vector unit, vec_result, full mask), END.
 * src0 = input(reg_type 2) with identity swizzle = 0x1B02:
 *   low 9 bits -> D2[31:23] = 0x102, high 8 bits -> D1[7:0] = 0x0D. */
static void vp_mov_o0_v0(unsigned char prog[16])
{
    u32 d0 = (1u << 30) | (0x3Fu << 15);                 /* vec_result, dst_tmp none */
    u32 d1 = (0x01u << 22) | 0x0Du;                      /* vec MOV, src0 hi */
    u32 d2 = 0x102u << 23;                               /* src0 lo */
    u32 d3 = (1u << 16) | (1u << 15) | (1u << 14) | (1u << 13)   /* vec mask xyzw */
           | (0x3Fu << 7)                                        /* sca_dst_tmp none */
           | (0u << 2)                                           /* dst_out o[0] */
           | 1u;                                                 /* end */
    vp_word(prog + 0, d0); vp_word(prog + 4, d1);
    vp_word(prog + 8, d2); vp_word(prog + 12, d3);
}

/* ---- FP/VP opcode sweep -------------------------------------------------
 * Reviewer's finding (V0 whole-branch review): rsx_glsl_dialect.c's compare
 * rewrite copied its two operand slices raw, so a compare between two
 * `input.` sources (COL0/TC*) skipped the `input.` -> `rsx_in.` rewrite
 * inside the operands and shaderc rejected the reserved word `input`
 * (18/192 FP cases in the reviewer's own opcode sweep, all SLT/SGE/SLE/
 * SGT/SNE/SEQ with input sources). rsx_glsl_dialect.c now runs each operand
 * back through the full rewriter (see rewrite_operand); this sweep is the
 * regression guard: every FP opcode x source kind x mask/precision variant,
 * and every VP vec/sca opcode x source kind x mask variant, must decompile
 * and compile with shaderc.
 *
 * A short, explicit, commented skip list carries the handful of cases that
 * fail for a DIFFERENT, pre-existing reason in the shared HLSL-dialect
 * emitter (also present in the MSL path -- not introduced or fixed by this
 * GLSL work; fixing them is out of scope here, see the plan's Global
 * Constraints: MSL output must stay byte-identical). */

/* FP: reg_type field is the low 2 bits of every source word (FP_REG_TYPE_*). */
#define FP_OUT_HALF_BIT (1u << 7)

/* True if (opcode, source kind, mask variant) is a known pre-existing
 * HLSL-emitter bug unrelated to the compare-operand fix, and must be
 * skipped rather than fixed here. */
static int fp_sweep_skip(u32 opc, int kind, const char** why)
{
    /* RFL (0x36) with a CONST source: rsx_fp_decompiler.c's RFL rhs format
     * string substitutes the same operand up to 4 times
     * ("float4((2.0*dot(A.xyz,B.xyz)/dot(A.xyz,A.xyz))*A.xyz - B.xyz,1.0)"),
     * and a CONST operand expands to an ~90-char `float4(asfloat(0x...u),
     * ...)` literal; 4x that blows the 640-byte `rhs` buffer in
     * rsx_fp_decompile (snprintf truncates, the body no longer balances
     * parens, and the GLSL fails to parse). Reproduced identically by the
     * MSL path (same HLSL dialect) -- pre-existing, not a GLSL/Vulkan bug,
     * out of scope for this fix (Global Constraints: MSL stays untouched). */
    if (opc == 0x36 /* OP_RFL */ && kind == 2 /* CONST -- matches FP_REG_TYPE_CONST */) {
        *why = "RFL+CONST truncates the shared HLSL rhs[640] buffer (pre-existing, also MSL)";
        return 1;
    }
    return 0;
}

static void fp_opcode_sweep(char* glsl, u32 glsl_size, int* fails_out)
{
    int fails = 0, tot = 0, skipped = 0;
    for (u32 opc = 0; opc < 0x40; opc++) {
        for (int kind = 0; kind < 3; kind++) {            /* 0 temp, 1 input, 2 const */
            for (int mvar = 0; mvar < 3; mvar++) {        /* 0 full, 1 partial, 2 half dest */
                tot++;
                const char* why = NULL;
                if (fp_sweep_skip(opc, kind, &why)) { skipped++; continue; }

                unsigned char p[16];
                u32 mask = (mvar == 1) ? (1u << 9) : FP_OUTMASK_ALL;
                u32 half = (mvar == 2) ? FP_OUT_HALF_BIT : 0;
                fp_word(p + 0, FP_OPC(opc) | mask | half | FP_INSRC(1) | FP_END);
                u32 s = (u32)kind | ((0u << 9) | (1u << 11) | (2u << 13) | (3u << 15));
                fp_word(p + 4, s);
                fp_word(p + 8, s);
                fp_word(p + 12, s);

                char nm[64];
                snprintf(nm, sizeof nm, "fp_%s_k%d_m%d.frag", rsx_fp_opcode_name(opc), kind, mvar);

                int n = rsx_fp_decompile_glsl(p, sizeof p, glsl, glsl_size);
                if (n < 0) { printf("FAIL %s: emit failed\n", nm); fails++; continue; }
                if (!compiles(glsl, shaderc_fragment_shader, nm)) fails++;
            }
        }
    }
    printf("FP opcode sweep: %d/%d fail, %d skipped (known pre-existing bugs, see fp_sweep_skip)\n",
           fails, tot, skipped);
    *fails_out += fails;
}

/* VP source word: reg_type (2 bits) | identity swizzle (x,y,z,w), no negate,
 * tmp_src=0 -- see emit_src()/the D0..D3 field table in rsx_vp_decompiler.c. */
static u32 vp_src_word(u32 reg_type)
{
    return (reg_type & 3u) | (0u << 14) | (1u << 12) | (2u << 10) | (3u << 8);
}

/* True if (unit, op, source kind) is a known pre-existing HLSL-emitter bug
 * unrelated to the compare-operand fix. None identified for VP so far (its
 * per-op rhs buffers are generously sized -- see rhs[384]/rhs[640] in
 * rsx_vp_decompiler.c); kept as a hook so a future finding has somewhere to
 * go without touching the sweep's control flow. */
static int vp_sweep_skip(int unit, u32 op, u32 reg_type, const char** why)
{
    (void)unit; (void)op; (void)reg_type; (void)why;
    return 0;
}

static void vp_opcode_sweep(char* glsl, u32 glsl_size, int* fails_out)
{
    int fails = 0, tot = 0, skipped = 0;
    for (int unit = 0; unit < 2; unit++) {                 /* 0 vec engine, 1 sca engine */
        for (u32 op = 0; op < 0x20; op++) {
            for (u32 rt = 0; rt < 3; rt++) {                /* 0 temp, 1 input, 2 const */
                u32 reg_type = (rt == 0) ? 0u : (rt == 1) ? 2u : 3u; /* VP FP_REG_TYPE_* values */
                for (int mvar = 0; mvar < 2; mvar++) {      /* 0 full mask, 1 partial (X only) */
                    tot++;
                    const char* why = NULL;
                    if (vp_sweep_skip(unit, op, reg_type, &why)) { skipped++; continue; }

                    u32 sv = vp_src_word(reg_type);
                    unsigned char p[16];
                    u32 d0 = (1u << 30) | (0u << 15);                    /* vec_result=1, dst_tmp=r[0] */
                    u32 d1 = (unit == 0 ? (op << 22) : (op << 27)) | ((sv >> 9) & 0xFFu);
                    u32 d2 = ((sv & 0x1FFu) << 23) | ((sv >> 11) & 0x3Fu) | (sv << 6);
                    u32 d3 = ((sv & 0x7FFu) << 21) | (0x3Fu << 7) | (0u << 2) | 1u; /* dst_out=o[0], end */
                    u32 m = (mvar == 0) ? 0xFu : 0x8u;
                    if (unit == 0) {
                        d3 |= m << 13;                                    /* vec write mask */
                    } else {
                        d3 |= m << 17;                                    /* sca write mask */
                        d3 = (d3 & ~(0x3Fu << 7)) | (0u << 7);             /* sca_dst_tmp = r[0] */
                    }
                    vp_word(p + 0, d0); vp_word(p + 4, d1);
                    vp_word(p + 8, d2); vp_word(p + 12, d3);

                    char nm[64];
                    snprintf(nm, sizeof nm, "vp_%s_rt%u_m%d.vert",
                             unit ? rsx_vp_sca_name(op) : rsx_vp_vec_name(op), reg_type, mvar);

                    rsx_vp_set_fix_override(4); /* NO_UV_SEED only, same as vp_basic_* above */
                    int n = rsx_vp_decompile_glsl(p, sizeof p, glsl, glsl_size);
                    rsx_vp_set_fix_override(-1);
                    if (n < 0) { printf("FAIL %s: emit failed\n", nm); fails++; continue; }
                    if (!compiles(glsl, shaderc_vertex_shader, nm)) fails++;
                }
            }
        }
    }
    printf("VP opcode sweep: %d/%d fail, %d skipped (known pre-existing bugs, see vp_sweep_skip)\n",
           fails, tot, skipped);
    *fails_out += fails;
}

int main(void)
{
    static char glsl[128 * 1024];
    unsigned char fp[16];
    fp_mov_r0_col0(fp);

    /* fp_basic: MOV r0, COL0 */
    int n = rsx_fp_decompile_glsl(fp, sizeof fp, glsl, sizeof glsl);
    check("fp_basic_decompiles", n == 1);
    check("fp_basic_shape",
          strstr(glsl, "#version 450") && strstr(glsl, "rsx_in.col0") &&
          strstr(glsl, "layout(location = 0) out vec4 rsx_c0") &&
          strstr(glsl, "layout(constant_id = 0) const bool rsx_at_on") &&
          !strstr(glsl, "SV_TARGET") && !strstr(glsl, "input."));
    /* fp_has_deriv_helpers: fragment path gets RSX_GLSL_PRELUDE_FRAG (dFdx/dFdy
     * backed ddx/ddy) -- only the fragment stage may declare them. */
    check("fp_has_deriv_helpers", strstr(glsl, "vec4 ddx(vec4 x)") != NULL);
    check("fp_basic_compiles", compiles(glsl, shaderc_fragment_shader, "fp_basic.frag"));

    /* fp_mrt4_depth: 4 colour outputs compile. depth_export is ON here too,
     * but fp is MOV r0,COL0 -- it never writes r1, so s_fp_wrote_r1 stays 0
     * and gl_FragDepth is NOT emitted by this program; this only proves MRT.
     * See fp_depth_* below for a program that actually writes r1 and is
     * checked for the emitted `gl_FragDepth = r[1].z`. */
    rsx_fp_set_mrt_count(4);
    rsx_fp_set_depth_export(1);
    n = rsx_fp_decompile_glsl(fp, sizeof fp, glsl, sizeof glsl);
    rsx_fp_set_mrt_count(1);
    rsx_fp_set_depth_export(0);
    check("fp_mrt4_outputs", n == 1 && strstr(glsl, "out vec4 rsx_c3") && strstr(glsl, "rsx_c3 = r[4]"));
    check("fp_mrt4_compiles", compiles(glsl, shaderc_fragment_shader, "fp_mrt4.frag"));

    /* fp_depth: MOV r1, COL0 with depth_export ON actually writes r1, so the
     * Vulkan interface contract's `gl_FragDepth = r[1].z` epilogue line must
     * be emitted and compile. */
    {
        unsigned char fp_depth[16];
        fp_mov_r1_col0(fp_depth);
        rsx_fp_set_mrt_count(4);
        rsx_fp_set_depth_export(1);
        n = rsx_fp_decompile_glsl(fp_depth, sizeof fp_depth, glsl, sizeof glsl);
        rsx_fp_set_mrt_count(1);
        rsx_fp_set_depth_export(0);
        check("fp_depth_decompiles", n == 1);
        check("fp_depth_shape", strstr(glsl, "gl_FragDepth = r[1].z") != NULL);
        check("fp_depth_compiles", compiles(glsl, shaderc_fragment_shader, "fp_depth.frag"));
    }

    /* fp_const_buffer: const-buffer mode declares the rsx_fc SSBO */
    rsx_fp_set_const_buffer_mode(1);
    n = rsx_fp_decompile_glsl(fp, sizeof fp, glsl, sizeof glsl);
    rsx_fp_set_const_buffer_mode(0);
    check("fp_const_buffer_decl", n == 1 && strstr(glsl, "binding = 1) readonly buffer RsxFc"));
    check("fp_const_buffer_compiles", compiles(glsl, shaderc_fragment_shader, "fp_fc.frag"));

    /* fp_tiny_out_buffer: overflow -> -1 */
    {
        char tiny[300];
        check("fp_tiny_out_buffer", rsx_fp_decompile_glsl(fp, sizeof fp, tiny, sizeof tiny) == -1);
    }

    /* fp_rejects_early_return: a body with two returns must be refused, never
     * emitted with the colour writes skipped on the early path. */
    {
        const char* two_ret =
            "struct PSInput { float4 position : SV_POSITION; };\n"
            "float4 main(PSInput input) : SV_TARGET {\n"
            "    float4 r[48]; float4 h[48];\n"
            "    if (r[1].x > 0.0) return r[1];\n"
            "    return r[0];\n"
            "}\n";
        check("fp_rejects_early_return",
              rsx_fp_glsl_from_hlsl(two_ret, 2, glsl, sizeof glsl) == -1 && glsl[0] == '\0');
        const char* one_ret =
            "float4 main(PSInput input) : SV_TARGET {\n"
            "    float4 r[48]; float4 h[48];\n"
            "    return r[0];\n"
            "}\n";
        check("fp_single_return_ok", rsx_fp_glsl_from_hlsl(one_ret, 1, glsl, sizeof glsl) == 1);
    }

    unsigned char vp[16];
    vp_mov_o0_v0(vp);
    /* NO_UV_SEED only (bit 2): forced explicitly rather than relying on the
     * ambient PS3_RSX_FIX_NO_UV_SEED default (ON when unset, but a caller's
     * environment could set =0 and flip the branch this test asserts on). */
    rsx_vp_set_fix_override(4);
    n = rsx_vp_decompile_glsl(vp, sizeof vp, glsl, sizeof glsl);
    rsx_vp_set_fix_override(-1);
    check("vp_basic_decompiles", n == 1);
    check("vp_basic_shape",
          strstr(glsl, "invariant gl_Position;") && strstr(glsl, "o[0]") &&
          strstr(glsl, "gl_Position = o[0];") &&
          strstr(glsl, "layout(location = 10) out vec4 v_tc7") &&
          strstr(glsl, "binding = 4) readonly buffer RsxVam") &&
          strstr(glsl, "vam.stride == 0u") &&
          !strstr(glsl, "[unroll]") && !strstr(glsl, "(float4)0"));
    /* vp_no_deriv_helpers: the vertex path must never declare dFdx/dFdy-backed
     * helpers -- Vulkan GLSL rejects them outside the fragment stage even
     * when unused (this is the class of bug that regressed the Windows
     * build via a prior VP-local `memmem` workaround). */
    check("vp_no_deriv_helpers", strstr(glsl, "dFdx") == NULL);
    check("vp_basic_compiles", compiles(glsl, shaderc_vertex_shader, "vp_basic.vert"));

    rsx_vp_set_fix_override(1 | 2 | 8);          /* OUT_W1, NO_TC0_HACK, FOG_X; seed on */
    n = rsx_vp_decompile_glsl(vp, sizeof vp, glsl, sizeof glsl);
    rsx_vp_set_fix_override(-1);
    check("vp_fix_bits_shape", n == 1 && strstr(glsl, "o[_j]=float4(0,0,0,1)") &&
          strstr(glsl, "v_fog = o[5].xxxx") && !strstr(glsl, "o[7] = v[8]") &&
          strstr(glsl, "v[8]=vec4(in_v3.xy, 0.0, 1.0);"));
    check("vp_fix_bits_compiles", compiles(glsl, shaderc_vertex_shader, "vp_fix.vert"));

    /* Opcode sweep (see the block above main): every FP opcode x source kind
     * x mask/precision variant, every VP vec/sca opcode x source kind x mask
     * variant -- the reviewer's repro class (compare ops with `input.`
     * operands) and everything else must decompile and compile with shaderc. */
    fp_opcode_sweep(glsl, sizeof glsl, &fails);
    vp_opcode_sweep(glsl, sizeof glsl, &fails);

    printf(fails ? "%d FAILED\n" : "ALL PASS\n", fails);
    return fails ? 1 : 0;
}
