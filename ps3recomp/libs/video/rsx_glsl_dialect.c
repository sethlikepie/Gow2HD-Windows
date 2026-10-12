/*
 * ps3recomp - HLSL dialect -> Vulkan GLSL 450 (see rsx_glsl_dialect.h)
 */
#include "rsx_glsl_dialect.h"

#include <stdio.h>
#include <string.h>

const char RSX_GLSL_PRELUDE[] =
    "#define float2 vec2\n"
    "#define float3 vec3\n"
    "#define float4 vec4\n"
    "#define int2 ivec2\n"
    "#define int3 ivec3\n"
    "#define int4 ivec4\n"
    "#define uint2 uvec2\n"
    "#define uint3 uvec3\n"
    "#define uint4 uvec4\n"
    "float saturate(float x) { return clamp(x, 0.0, 1.0); }\n"
    "vec2 saturate(vec2 x) { return clamp(x, 0.0, 1.0); }\n"
    "vec3 saturate(vec3 x) { return clamp(x, 0.0, 1.0); }\n"
    "vec4 saturate(vec4 x) { return clamp(x, 0.0, 1.0); }\n"
    "float frac(float x) { return fract(x); }\n"
    "vec2 frac(vec2 x) { return fract(x); }\n"
    "vec3 frac(vec3 x) { return fract(x); }\n"
    "vec4 frac(vec4 x) { return fract(x); }\n"
    "float lerp(float a, float b, float t) { return mix(a, b, t); }\n"
    "vec2 lerp(vec2 a, vec2 b, vec2 t) { return mix(a, b, t); }\n"
    "vec3 lerp(vec3 a, vec3 b, vec3 t) { return mix(a, b, t); }\n"
    "vec4 lerp(vec4 a, vec4 b, vec4 t) { return mix(a, b, t); }\n"
    "float rsqrt(float x) { return inversesqrt(x); }\n"
    "vec2 rsqrt(vec2 x) { return inversesqrt(x); }\n"
    "vec3 rsqrt(vec3 x) { return inversesqrt(x); }\n"
    "vec4 rsqrt(vec4 x) { return inversesqrt(x); }\n"
    "float asfloat(uint x) { return uintBitsToFloat(x); }\n"
    "float asfloat(int x) { return intBitsToFloat(x); }\n"
    "float asfloat(float x) { return x; }\n"
    "uint asuint(float x) { return floatBitsToUint(x); }\n"
    "uint asuint(uint x) { return x; }\n"
    "int asint(float x) { return floatBitsToInt(x); }\n"
    "uint f32tof16(float x) { return packHalf2x16(vec2(x, 0.0)) & 0xFFFFu; }\n"
    "float f16tof32(uint x) { return unpackHalf2x16(x & 0xFFFFu).x; }\n"
    "vec4 lit(float ndl, float nds, float m) {\n"
    "  float d = max(ndl, 0.0);\n"
    "  float s = (ndl > 0.0) ? pow(max(nds, 0.0), m) : 0.0;\n"
    "  return vec4(1.0, d, s, 1.0);\n"
    "}\n"
    "vec4 rsx_slt(vec4 a, vec4 b) { return vec4(lessThan(a, b)); }\n"
    "vec4 rsx_sle(vec4 a, vec4 b) { return vec4(lessThanEqual(a, b)); }\n"
    "vec4 rsx_sgt(vec4 a, vec4 b) { return vec4(greaterThan(a, b)); }\n"
    "vec4 rsx_sge(vec4 a, vec4 b) { return vec4(greaterThanEqual(a, b)); }\n"
    "vec4 rsx_seq(vec4 a, vec4 b) { return vec4(equal(a, b)); }\n"
    "vec4 rsx_sne(vec4 a, vec4 b) { return vec4(notEqual(a, b)); }\n"
    "float rsx_slt(float a, float b) { return float(a < b); }\n"
    "float rsx_sle(float a, float b) { return float(a <= b); }\n"
    "float rsx_sgt(float a, float b) { return float(a > b); }\n"
    "float rsx_sge(float a, float b) { return float(a >= b); }\n"
    "float rsx_seq(float a, float b) { return float(a == b); }\n"
    "float rsx_sne(float a, float b) { return float(a != b); }\n";

/* Fragment-only: `dFdx`/`dFdy` are rejected by Vulkan GLSL 450 outside the
 * fragment stage, even in an unreferenced function body (glslc: "'dFdx' : no
 * matching overloaded function found" from merely declaring the helper in a
 * .vert). NV40 vertex programs have no DDX/DDY opcodes (only the fragment
 * ISA does), so vertex output never needs these; place right after
 * RSX_GLSL_PRELUDE in fragment shaders only. */
const char RSX_GLSL_PRELUDE_FRAG[] =
    "float ddx(float x) { return dFdx(x); }\n"
    "vec2 ddx(vec2 x) { return dFdx(x); }\n"
    "vec3 ddx(vec3 x) { return dFdx(x); }\n"
    "vec4 ddx(vec4 x) { return dFdx(x); }\n"
    "float ddy(float x) { return dFdy(x); }\n"
    "vec2 ddy(vec2 x) { return dFdy(x); }\n"
    "vec3 ddy(vec3 x) { return dFdy(x); }\n"
    "vec4 ddy(vec4 x) { return dFdy(x); }\n";

typedef struct { char* p; u32 cap; u32 len; int bad; } GBuf;

static void g_putn(GBuf* b, const char* s, size_t n)
{
    if (b->bad) return;
    if ((size_t)b->len + n + 1u > b->cap) { b->bad = 1; return; }
    memcpy(b->p + b->len, s, n);
    b->len += (u32)n;
    b->p[b->len] = '\0';
}
static void g_puts(GBuf* b, const char* s) { g_putn(b, s, strlen(s)); }

static int starts(const char* p, const char* lit) { return strncmp(p, lit, strlen(lit)) == 0; }

static int is_ident(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

/* p at '(' -> its matching ')', or NULL. */
static const char* match_paren(const char* p)
{
    int d = 0;
    for (; *p; p++) {
        if (*p == '(') d++;
        else if (*p == ')' && --d == 0) return p;
    }
    return NULL;
}

/* Compare-operand temporaries: the operands the decompilers emit (a src
 * expression like "(r[1]).xyzw" or, for CONST sources, an inlined
 * `float4(asfloat(0x...u), ...)` literal) are well under 1 KiB; 4 KiB leaves
 * generous headroom without risking the host stack. Bigger than that is
 * treated as a hard failure (propagated as -1), never a silent truncation or
 * an un-rewritten operand. */
#define RSX_GLSL_OPERAND_MAX 4096u

/* Copy [start, end_incl] (a parenthesised operand, parens included) into a
 * NUL-terminated temporary, run it back through the full rewriter (so
 * `input.` -> `rsx_in.` and any nested compare also apply inside a compare
 * operand), and append the result to `b`. Sets b->bad and returns 0 on any
 * overflow; never appends a truncated/partial operand. */
static int rewrite_operand(const char* start, const char* end_incl, GBuf* b)
{
    size_t n = (size_t)(end_incl - start + 1);
    if (n >= RSX_GLSL_OPERAND_MAX) { b->bad = 1; return 0; }
    char tmp[RSX_GLSL_OPERAND_MAX];
    memcpy(tmp, start, n);
    tmp[n] = '\0';
    char rewritten[RSX_GLSL_OPERAND_MAX];
    if (rsx_glsl_rewrite_body(tmp, rewritten, sizeof rewritten) != 0) { b->bad = 1; return 0; }
    g_puts(b, rewritten);
    return 1;
}

/* "(float4)(" G1 op G2 ")" with G1/G2 parenthesised -> "rsx_sXX(G1, G2)". */
static const char* try_vec_compare(const char* p, GBuf* b)
{
    if (!starts(p, "(float4)((")) return NULL;
    const char* g1 = p + 9;                 /* '(' of the first operand */
    const char* g1e = match_paren(g1);
    if (!g1e) return NULL;
    const char* q = g1e + 1;
    while (*q == ' ') q++;
    const char* fn;
    size_t oplen = 2;
    if (starts(q, "<=")) fn = "rsx_sle";
    else if (starts(q, ">=")) fn = "rsx_sge";
    else if (starts(q, "==")) fn = "rsx_seq";
    else if (starts(q, "!=")) fn = "rsx_sne";
    else if (*q == '<') { fn = "rsx_slt"; oplen = 1; }
    else if (*q == '>') { fn = "rsx_sgt"; oplen = 1; }
    else return NULL;
    q += oplen;
    while (*q == ' ') q++;
    if (*q != '(') return NULL;
    const char* g2e = match_paren(q);
    if (!g2e || g2e[1] != ')') return NULL;
    g_puts(b, fn);
    g_puts(b, "(");
    if (!rewrite_operand(g1, g1e, b)) return g2e + 2;
    g_puts(b, ", ");
    if (!rewrite_operand(q, g2e, b)) return g2e + 2;
    g_puts(b, ")");
    return g2e + 2;                         /* past the cast's closing ')' */
}

/* "rsx_tex[N].Sample*(rsx_samp[N], " -> "texture*(rsx_tex[N], " */
static const char* try_texture(const char* p, GBuf* b)
{
    static const struct { const char* m; const char* fn; } k[] = {
        { ".SampleLevel(", "textureLod(" },
        { ".SampleBias(",  "texture(" },    /* GLSL 3rd arg of texture() is the bias */
        { ".SampleGrad(",  "textureGrad(" },
        { ".Sample(",      "texture(" },
    };
    if (!starts(p, "rsx_tex[")) return NULL;
    const char* q = p + 8;
    unsigned unit = 0;
    int nd = 0;
    while (*q >= '0' && *q <= '9') { unit = unit * 10u + (unsigned)(*q - '0'); q++; nd++; }
    if (!nd || *q != ']') return NULL;
    q++;
    for (size_t i = 0; i < sizeof k / sizeof k[0]; i++) {
        if (!starts(q, k[i].m)) continue;
        const char* s = q + strlen(k[i].m);
        char samp[32];
        int sl = snprintf(samp, sizeof samp, "rsx_samp[%u], ", unit);
        if (strncmp(s, samp, (size_t)sl) != 0) return NULL;
        char head[48];
        snprintf(head, sizeof head, "%srsx_tex[%u], ", k[i].fn, unit);
        g_puts(b, head);
        return s + sl;
    }
    return NULL;
}

int rsx_glsl_rewrite_body(const char* in, char* out, u32 out_size)
{
    if (!in || !out || out_size == 0) return -1;
    GBuf b = { out, out_size, 0, 0 };
    out[0] = '\0';
    const char* p = in;
    while (*p && !b.bad) {
        const char* n;
        if ((n = try_vec_compare(p, &b)) != NULL) { p = n; continue; }
        if ((n = try_texture(p, &b)) != NULL) { p = n; continue; }
        if (starts(p, "[unroll] ")) { p += strlen("[unroll] "); continue; }
        if (starts(p, "(float4)0")) { g_puts(&b, "float4(0)"); p += strlen("(float4)0"); continue; }
        if (starts(p, "any((r[0]).xyzw < 0.0)")) {
            g_puts(&b, "any(lessThan(r[0], vec4(0.0)))");
            p += strlen("any((r[0]).xyzw < 0.0)");
            continue;
        }
        if (starts(p, "input.") && (p == in || !is_ident(p[-1]))) {
            g_puts(&b, "rsx_in.");
            p += strlen("input.");
            continue;
        }
        g_putn(&b, p, 1);
        p++;
    }
    if (b.bad) { out[0] = '\0'; return -1; }
    return 0;
}
