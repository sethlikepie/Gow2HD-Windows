/*
 * ps3recomp - RSX Fragment Program (NV40 ISA) → HLSL decompiler
 *
 * See rsx_fp_decompiler.h and docs/RSX_FRAGMENT_PROGRAM.md.
 *
 * The encoding (4 words / 16 bytes per instruction; CONST sources carry an
 * inline 16-byte constant in the following slot) is taken verbatim from
 * Mesa/nouveau nvfx_shader.h. Bit positions are spelled out as named macros
 * below so this file is self-documenting.
 */

#include "rsx_fp_decompiler.h"
#include "rsx_glsl_dialect.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- DWORD 0 (OPDEST) --------------------------------------------------- */
#define FP_END              (1u << 0)
#define FP_OUT_REG_SHIFT    1
#define FP_OUT_REG_MASK     (63u << 1)
#define FP_OUT_HALF         (1u << 7)
#define FP_COND_WRITE       (1u << 8)
#define FP_OUT_MASK_SHIFT   9          /* X@9 Y@10 Z@11 W@12 */
#define FP_INPUT_SRC_SHIFT  13
#define FP_INPUT_SRC_MASK   (15u << 13)
#define FP_TEX_UNIT_SHIFT   17
#define FP_TEX_UNIT_MASK    (15u << 17)
#define FP_OPCODE_SHIFT     24
#define FP_OPCODE_MASK      (0x3Fu << 24)
#define FP_OUT_NONE         (1u << 30)
#define FP_OUT_SAT          (1u << 31)

/* ---- DWORD 1/2/3 (SRC0/1/2) --------------------------------------------- */
#define FP_REG_TYPE_SHIFT   0
#define FP_REG_TYPE_MASK    (3u << 0)
#define   FP_REG_TYPE_TEMP  0
#define   FP_REG_TYPE_INPUT 1
#define   FP_REG_TYPE_CONST 2
#define FP_SRC_REG_SHIFT    2
#define FP_SRC_REG_MASK     (63u << 2)
#define FP_SRC_HALF         (1u << 8)
#define FP_SRC_SWZ_SHIFT    9          /* X@9 Y@11 Z@13 W@15, 2 bits each */
#define FP_SRC_NEGATE       (1u << 17)
#define FP_SRC0_ABS         (1u << 29) /* in DWORD1 */
#define FP_SRC1_ABS         (1u << 18) /* in DWORD2 */
#define FP_BRANCH           (1u << 31) /* DWORD2 bit31: instruction is a branch */

/* ---- Opcodes ------------------------------------------------------------ */
enum {
    OP_NOP=0x00, OP_MOV=0x01, OP_MUL=0x02, OP_ADD=0x03, OP_MAD=0x04,
    OP_DP3=0x05, OP_DP4=0x06, OP_DST=0x07, OP_MIN=0x08, OP_MAX=0x09,
    OP_SLT=0x0A, OP_SGE=0x0B, OP_SLE=0x0C, OP_SGT=0x0D, OP_SNE=0x0E,
    OP_SEQ=0x0F, OP_FRC=0x10, OP_FLR=0x11, OP_KIL=0x12, OP_PK4B=0x13,
    OP_UP4B=0x14, OP_DDX=0x15, OP_DDY=0x16, OP_TEX=0x17, OP_TXP=0x18,
    OP_TXD=0x19, OP_RCP=0x1A, OP_RSQ=0x1B, OP_EX2=0x1C, OP_LG2=0x1D,
    OP_LIT=0x1E, OP_LRP=0x1F, OP_STR=0x20, OP_SFL=0x21, OP_COS=0x22,
    OP_SIN=0x23, OP_PK2H=0x24, OP_UP2H=0x25, OP_POW=0x26, OP_PK4UB=0x27,
    OP_UP4UB=0x28, OP_PK2US=0x29, OP_UP2US=0x2A, OP_DP2A=0x2E, OP_TXL=0x2F,
    OP_TXB=0x31, OP_RFL=0x36, OP_DP2=0x38, OP_NRM=0x39, OP_DIV=0x3A,
    OP_DIVSQ=0x3B, OP_LIF=0x3C, OP_FENCT=0x3D, OP_FENCB=0x3E
};

const char* rsx_fp_opcode_name(u32 op)
{
    switch (op) {
    case OP_NOP: return "NOP"; case OP_MOV: return "MOV"; case OP_MUL: return "MUL";
    case OP_ADD: return "ADD"; case OP_MAD: return "MAD"; case OP_DP3: return "DP3";
    case OP_DP4: return "DP4"; case OP_DST: return "DST"; case OP_MIN: return "MIN";
    case OP_MAX: return "MAX"; case OP_SLT: return "SLT"; case OP_SGE: return "SGE";
    case OP_SLE: return "SLE"; case OP_SGT: return "SGT"; case OP_SNE: return "SNE";
    case OP_SEQ: return "SEQ"; case OP_FRC: return "FRC"; case OP_FLR: return "FLR";
    case OP_KIL: return "KIL"; case OP_DDX: return "DDX"; case OP_DDY: return "DDY";
    case OP_TEX: return "TEX"; case OP_TXP: return "TXP"; case OP_TXD: return "TXD";
    case OP_RCP: return "RCP"; case OP_RSQ: return "RSQ"; case OP_EX2: return "EX2";
    case OP_LG2: return "LG2"; case OP_LIT: return "LIT"; case OP_LRP: return "LRP";
    case OP_STR: return "STR"; case OP_SFL: return "SFL"; case OP_COS: return "COS";
    case OP_SIN: return "SIN"; case OP_POW: return "POW"; case OP_DP2A: return "DP2A";
    case OP_TXL: return "TXL"; case OP_TXB: return "TXB"; case OP_RFL: return "RFL";
    case OP_DIV: return "DIV"; case OP_DP2: return "DP2"; case OP_NRM: return "NRM";
    case OP_DIVSQ: return "DIVSQ"; case OP_LIF: return "LIF";
    case OP_PK2H: return "PK2H"; case OP_UP2H: return "UP2H";
    case OP_PK4B: return "PK4B"; case OP_UP4B: return "UP4B";
    case OP_PK4UB: return "PK4UB"; case OP_UP4UB: return "UP4UB";
    case OP_PK2US: return "PK2US"; case OP_UP2US: return "UP2US";
    case OP_FENCT: return "FENCT"; case OP_FENCB: return "FENCB";
    default:     return "?";
    }
}

/* ------------------------------------------------------------------------- */

u32 rsx_fp_read_word(const u8* p)
{
    u32 be = ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | (u32)p[3];
    return (be << 16) | (be >> 16);   /* 16-bit half-word swap */
}

u32 rsx_fp_program_size(const u8* ucode, u32 max_bytes)
{
    if (!ucode) return 0;
    u32 off = 0;
    while (off + 16 <= max_bytes) {
        u32 w0 = rsx_fp_read_word(ucode + off + 0);
        u32 w1 = rsx_fp_read_word(ucode + off + 4);
        u32 w2 = rsx_fp_read_word(ucode + off + 8);
        u32 w3 = rsx_fp_read_word(ucode + off + 12);
        off += 16;
        /* A CONST source pulls an inline 16-byte constant slot. */
        if (((w1 & FP_REG_TYPE_MASK) >> FP_REG_TYPE_SHIFT) == FP_REG_TYPE_CONST ||
            ((w2 & FP_REG_TYPE_MASK) >> FP_REG_TYPE_SHIFT) == FP_REG_TYPE_CONST ||
            ((w3 & FP_REG_TYPE_MASK) >> FP_REG_TYPE_SHIFT) == FP_REG_TYPE_CONST) {
            if (off + 16 <= max_bytes) off += 16;
        }
        if (w0 & FP_END) return off;
    }
    return 0;
}

/* Constant-buffer mode (see rsx_fp_set_const_buffer_mode) and the ordinal of the
 * inline constant block of the instruction being emitted. */
static int s_fp_const_buf_mode = 0;
static u32 s_fp_cur_k = 0;

void rsx_fp_set_const_buffer_mode(int on) { s_fp_const_buf_mode = on ? 1 : 0; }

/* -1 AUTO, 0 h0, 1 r0. See rsx_fp_set_color_export. */
static int s_fp_color_export = -1;

void rsx_fp_set_color_export(int mode) { s_fp_color_export = mode; }

/* SET_SHADER_CONTROL & 0xE (DEPTH_EXPORT): the fragment depth is r1.z.
 * s_fp_wrote_r1 is set by the last rsx_fp_decompile. */
static int s_fp_depth_export = 0;
static int s_fp_wrote_r1 = 0;

void rsx_fp_set_depth_export(int on) { s_fp_depth_export = on ? 1 : 0; }

/* Numero de alvos de cor ligados (SET_SURFACE_COLOR_TARGET, 1..4). Com mais de
 * um, o MSL devolve uma saida por alvo, dos registos que o hardware exporta
 * (RPCS3 FragmentProgramDecompiler: r0,r2,r3,r4 em fp32; h0,h4,h6,h8 em fp16).
 * Os registos estao zerados no inicio, como o "init_value" do RPCS3. */
static int s_fp_mrt_count = 1;

void rsx_fp_set_mrt_count(int n) { s_fp_mrt_count = (n < 1) ? 1 : (n > 4 ? 4 : n); }

u32 rsx_fp_hash_and_consts(const u8* ucode, u32 max_bytes, float* consts,
                           u32 max_consts, u32* n_consts)
{
    u32 h = 2166136261u, k = 0, off = 0;
    if (n_consts) *n_consts = 0;
    if (!ucode) return 0;
    while (off + 16 <= max_bytes) {
        u32 w0 = rsx_fp_read_word(ucode + off + 0);
        u32 w1 = rsx_fp_read_word(ucode + off + 4);
        u32 w2 = rsx_fp_read_word(ucode + off + 8);
        u32 w3 = rsx_fp_read_word(ucode + off + 12);
        for (u32 i = 0; i < 16; i++) { h ^= ucode[off + i]; h *= 16777619u; }
        off += 16;
        if (((w1 & FP_REG_TYPE_MASK) >> FP_REG_TYPE_SHIFT) == FP_REG_TYPE_CONST ||
            ((w2 & FP_REG_TYPE_MASK) >> FP_REG_TYPE_SHIFT) == FP_REG_TYPE_CONST ||
            ((w3 & FP_REG_TYPE_MASK) >> FP_REG_TYPE_SHIFT) == FP_REG_TYPE_CONST) {
            if (consts && k < max_consts) {
                for (int i = 0; i < 4; i++) {
                    u32 cw = (off + 16 <= max_bytes) ? rsx_fp_read_word(ucode + off + (u32)i * 4) : 0u;
                    memcpy(&consts[k * 4 + (u32)i], &cw, 4);
                }
            }
            k++;
            if (off + 16 <= max_bytes) off += 16;
        }
        if (w0 & FP_END) break;
    }
    if (n_consts) *n_consts = k;
    return h;
}

/* Bounded string appender. */
typedef struct { char* p; u32 cap; u32 len; int ok; } Out;

static void out_puts(Out* o, const char* s)
{
    if (!o->ok) return;
    u32 n = (u32)strlen(s);
    if (o->len + n + 1 > o->cap) { o->ok = 0; return; }
    memcpy(o->p + o->len, s, n);
    o->len += n;
    o->p[o->len] = '\0';
}

/* Decoded view of one source word. */
typedef struct {
    u32 type;       /* FP_REG_TYPE_* */
    u32 index;
    int half;
    int negate;
    int abs;
    char swz[5];    /* e.g. "xyzw" */
} Src;

static void decode_src(u32 w, int abs_bit, Src* s)
{
    static const char comp[4] = {'x','y','z','w'};
    s->type   = (w & FP_REG_TYPE_MASK) >> FP_REG_TYPE_SHIFT;
    s->index  = (w & FP_SRC_REG_MASK)  >> FP_SRC_REG_SHIFT;
    s->half   = (w & FP_SRC_HALF) ? 1 : 0;
    s->negate = (w & FP_SRC_NEGATE) ? 1 : 0;
    s->abs    = abs_bit ? 1 : 0;
    for (int i = 0; i < 4; i++)
        s->swz[i] = comp[(w >> (FP_SRC_SWZ_SHIFT + 2 * i)) & 3];
    s->swz[4] = '\0';
}

/* HLSL expression for the interpolated input selected by OPDEST.INPUT_SRC.
 * The backend's passthrough VS forwards COL0/COL1/FOG/TEXCOORD0..7; WPOS maps
 * to SV_POSITION (screen-space — an approximation of the RSX window coord). */
static const char* input_expr(u32 input_src)
{
    static const char* tc[8] = {
        "input.tc0", "input.tc1", "input.tc2", "input.tc3",
        "input.tc4", "input.tc5", "input.tc6", "input.tc7" };
    switch (input_src) {
    case 0x0: return "input.position"; /* WPOS */
    case 0x1: return "input.col0";     /* COL0 */
    case 0x2: return "input.col1";     /* COL1 */
    case 0x3: return "input.fog";      /* FOGC */
    default:
        if (input_src >= 0x4 && input_src <= 0xB) return tc[input_src - 0x4]; /* TC0..7 */
        return "float4(0,0,0,0)";      /* TC8/TC9/FACING not plumbed */
    }
}

/* %g can emit "nan"/"inf" which Metal rejects as identifiers. Use hex float
 * bit patterns that both HLSL and MSL accept. */
static void fmt_f32_literal(char* buf, u32 bufsz, float f)
{
    u32 bits;
    memcpy(&bits, &f, 4);
    /* finite and not huge denorm notation issues — still use hex for all for
     * bit-exact const bank replay. */
    snprintf(buf, bufsz, "asfloat(0x%08Xu)", bits);
}

/* Build the swizzled/negated/abs'd HLSL for one source into `buf`. */
static void emit_src(const Src* s, u32 input_src, const float* k, int has_k,
                     char* buf, u32 bufsz)
{
    char base[160];
    if (s->type == FP_REG_TYPE_TEMP) {
        snprintf(base, sizeof(base), "%s[%u]", s->half ? "h" : "r", s->index);
    } else if (s->type == FP_REG_TYPE_INPUT) {
        snprintf(base, sizeof(base), "%s", input_expr(input_src));
    } else { /* CONST */
        if (has_k && s_fp_const_buf_mode) {
            snprintf(base, sizeof(base), "rsx_fc[%u]", s_fp_cur_k);
        } else if (has_k) {
            char a[40], b[40], c[40], d[40];
            fmt_f32_literal(a, sizeof a, k[0]);
            fmt_f32_literal(b, sizeof b, k[1]);
            fmt_f32_literal(c, sizeof c, k[2]);
            fmt_f32_literal(d, sizeof d, k[3]);
            snprintf(base, sizeof(base), "float4(%s,%s,%s,%s)", a, b, c, d);
        } else
            snprintf(base, sizeof(base), "float4(0,0,0,0)");
    }

    char swz[160];
    snprintf(swz, sizeof(swz), "(%s).%s", base, s->swz);

    const char* pre = "";
    const char* post = "";
    if (s->abs)    { pre = "abs("; post = ")"; }
    if (s->negate) snprintf(buf, bufsz, "-(%s%s%s)", pre, swz, post);
    else           snprintf(buf, bufsz, "%s%s%s", pre, swz, post);
}

/* Dest write-mask letters from OPDEST bits 9..12 (X..W). */
static void dest_mask(u32 op0, char* m)
{
    int n = 0;
    if (op0 & (1u << (FP_OUT_MASK_SHIFT + 0))) m[n++] = 'x';
    if (op0 & (1u << (FP_OUT_MASK_SHIFT + 1))) m[n++] = 'y';
    if (op0 & (1u << (FP_OUT_MASK_SHIFT + 2))) m[n++] = 'z';
    if (op0 & (1u << (FP_OUT_MASK_SHIFT + 3))) m[n++] = 'w';
    if (n == 0) { m[0] = 'x'; m[1] = 'y'; m[2] = 'z'; m[3] = 'w'; n = 4; }
    m[n] = '\0';
}

int rsx_fp_decompile(const u8* ucode, u32 max_bytes, char* out, u32 out_size)
{
    if (!ucode || !out || out_size == 0) return -1;

    Out o = { out, out_size, 0, 1 };

    /* Preamble: PSInput matches the backend's placeholder layout; temp/half
     * register files; texture+sampler banks for TEX. */
    out_puts(&o,
        "struct PSInput {\n"
        "    float4 position : SV_POSITION; float4 col0 : COLOR0; float4 col1 : COLOR1;\n"
        "    float4 fog : FOG;\n"
        "    float4 tc0:TEXCOORD0; float4 tc1:TEXCOORD1; float4 tc2:TEXCOORD2; float4 tc3:TEXCOORD3;\n"
        "    float4 tc4:TEXCOORD4; float4 tc5:TEXCOORD5; float4 tc6:TEXCOORD6; float4 tc7:TEXCOORD7;\n"
        "};\n"
        "Texture2D    rsx_tex[16] : register(t0);\n"
        "SamplerState rsx_samp[16] : register(s0);\n"
        "float4 main(PSInput input) : SV_TARGET {\n"
        "    float4 r[48]; float4 h[48];\n"
        /* Fully initialise both register files: RSX programs routinely read a
         * register lane before writing it (the hardware reads undefined), but
         * HLSL rejects use-before-init (error X4000). Zero everything. */
        "    [unroll] for (int _i = 0; _i < 48; _i++) { r[_i] = (float4)0; h[_i] = (float4)0; }\n");

    int wrote_r0 = 0, wrote_h0 = 0;
    s_fp_wrote_r1 = 0;
    int count = 0;
    u32 off = 0;
    u32 kidx = 0;   /* ordinal of the next inline constant block */

    while (off + 16 <= max_bytes) {
        u32 w0 = rsx_fp_read_word(ucode + off + 0);
        u32 w1 = rsx_fp_read_word(ucode + off + 4);
        u32 w2 = rsx_fp_read_word(ucode + off + 8);
        u32 w3 = rsx_fp_read_word(ucode + off + 12);
        off += 16;
        count++;

        u32 opcode    = (w0 & FP_OPCODE_MASK) >> FP_OPCODE_SHIFT;
        u32 input_src = (w0 & FP_INPUT_SRC_MASK) >> FP_INPUT_SRC_SHIFT;
        u32 tex_unit  = (w0 & FP_TEX_UNIT_MASK) >> FP_TEX_UNIT_SHIFT;
        int is_branch = (w2 & FP_BRANCH) ? 1 : 0;

        Src s0, s1, s2;
        decode_src(w1, w1 & FP_SRC0_ABS, &s0);
        decode_src(w2, w2 & FP_SRC1_ABS, &s1);
        decode_src(w3, 0,                &s2);

        /* Inline constant: any CONST source pulls the next 16 bytes as a
         * float4 literal and advances past it. */
        float k[4] = {0,0,0,0};
        int has_k = 0;
        if (s0.type == FP_REG_TYPE_CONST || s1.type == FP_REG_TYPE_CONST ||
            s2.type == FP_REG_TYPE_CONST) {
            if (off + 16 <= max_bytes) {
                for (int i = 0; i < 4; i++) {
                    u32 cw = rsx_fp_read_word(ucode + off + i * 4);
                    memcpy(&k[i], &cw, 4);
                }
                off += 16;
            }
            has_k = 1;
            s_fp_cur_k = kidx++;   /* same order as rsx_fp_hash_and_consts */
        }

        char a[200], b[200], c[200];
        emit_src(&s0, input_src, k, has_k, a, sizeof(a));
        emit_src(&s1, input_src, k, has_k, b, sizeof(b));
        emit_src(&s2, input_src, k, has_k, c, sizeof(c));

        if (is_branch) {
            out_puts(&o, "    /* TODO: branch/flow-control op skipped */\n");
            if (w0 & FP_END) break;
            continue;
        }

        /* Build the RHS expression for this opcode. */
        char rhs[640];
        int handled = 1;
        switch (opcode) {
        case OP_NOP: rhs[0] = '\0'; handled = 0; break;
        case OP_MOV: snprintf(rhs, sizeof(rhs), "%s", a); break;
        case OP_MUL: snprintf(rhs, sizeof(rhs), "(%s) * (%s)", a, b); break;
        case OP_ADD: snprintf(rhs, sizeof(rhs), "(%s) + (%s)", a, b); break;
        case OP_MAD: snprintf(rhs, sizeof(rhs), "(%s) * (%s) + (%s)", a, b, c); break;
        case OP_DP3: snprintf(rhs, sizeof(rhs), "dot((%s).xyz, (%s).xyz)", a, b); break;
        case OP_DP4: snprintf(rhs, sizeof(rhs), "dot((%s), (%s))", a, b); break;
        /* DST: distance vector {1, a.y*b.y, a.z, b.w}. */
        case OP_DST: snprintf(rhs, sizeof(rhs),
            "float4(1.0, (%s).y*(%s).y, (%s).z, (%s).w)", a, b, a, b); break;
        /* DP2A: 2-component dot plus scalar. */
        case OP_DP2A: snprintf(rhs, sizeof(rhs),
            "dot((%s).xy, (%s).xy) + (%s).x", a, b, c); break;
        case OP_MIN: snprintf(rhs, sizeof(rhs), "min((%s), (%s))", a, b); break;
        case OP_MAX: snprintf(rhs, sizeof(rhs), "max((%s), (%s))", a, b); break;
        case OP_FRC: snprintf(rhs, sizeof(rhs), "frac(%s)", a); break;
        case OP_FLR: snprintf(rhs, sizeof(rhs), "floor(%s)", a); break;
        case OP_RCP: snprintf(rhs, sizeof(rhs), "(1.0 / (%s).x)", a); break;
        case OP_RSQ: snprintf(rhs, sizeof(rhs), "rsqrt((%s).x)", a); break;
        case OP_EX2: snprintf(rhs, sizeof(rhs), "exp2((%s).x)", a); break;
        case OP_LG2: snprintf(rhs, sizeof(rhs), "log2((%s).x)", a); break;
        case OP_COS: snprintf(rhs, sizeof(rhs), "cos((%s).x)", a); break;
        case OP_SIN: snprintf(rhs, sizeof(rhs), "sin((%s).x)", a); break;
        case OP_POW: snprintf(rhs, sizeof(rhs), "pow((%s).x, (%s).x)", a, b); break;
        case OP_DIV: snprintf(rhs, sizeof(rhs), "(%s) / (%s).x", a, b); break;
        case OP_DIVSQ: snprintf(rhs, sizeof(rhs), "(%s) / sqrt((%s).x)", a, b); break;
        case OP_DP2: snprintf(rhs, sizeof(rhs), "dot((%s).xy, (%s).xy)", a, b); break;
        case OP_NRM: snprintf(rhs, sizeof(rhs), "float4(normalize((%s).xyz), 1.0)", a); break;
        case OP_LRP: snprintf(rhs, sizeof(rhs), "lerp((%s), (%s), (%s))", c, b, a); break;
        /* LIT: lighting coefficients {1, diffuse, specular, 1}. HLSL lit()
         * matches NV40: x=1, y=max(ndl,0), z=(ndl>0)?pow(max(nds,0),m):0, w=1. */
        case OP_LIT: snprintf(rhs, sizeof(rhs), "lit((%s).x, (%s).y, (%s).w)", a, a, a); break;
        /* Screen-space derivatives. */
        case OP_DDX: snprintf(rhs, sizeof(rhs), "ddx(%s)", a); break;
        case OP_DDY: snprintf(rhs, sizeof(rhs), "ddy(%s)", a); break;
        /* Set-true / set-false constants. */
        case OP_STR: snprintf(rhs, sizeof(rhs), "float4(1,1,1,1)"); break;
        case OP_SFL: snprintf(rhs, sizeof(rhs), "float4(0,0,0,0)"); break;
        /* RFL: reflection vector 2*(A.B)/(A.A)*A - B (xyz), w=1. */
        case OP_RFL: snprintf(rhs, sizeof(rhs),
            "float4((2.0*dot((%s).xyz,(%s).xyz)/dot((%s).xyz,(%s).xyz))*(%s).xyz - (%s).xyz, 1.0)",
            a, b, a, a, a, b); break;
        /* LIF: obscure NV40 lighting helper; defined as identity here (never
         * emitted by GoW2 shaders — kept handled so no TODO leaks through). */
        case OP_LIF: snprintf(rhs, sizeof(rhs), "%s", a); break;
        /* Pack/unpack: NV40 packs multiple channels into one 32-bit lane and
         * reads them back. Modeled with HLSL bit ops so results round-trip. */
        case OP_PK2H: snprintf(rhs, sizeof(rhs),
            "asfloat(f32tof16((%s).x) | (f32tof16((%s).y) << 16))", a, a); break;
        case OP_UP2H: snprintf(rhs, sizeof(rhs),
            "float4(f16tof32(asuint((%s).x) & 0xFFFF), f16tof32(asuint((%s).x) >> 16), 0, 0)", a, a); break;
        case OP_PK4UB: snprintf(rhs, sizeof(rhs),
            "asfloat((uint(saturate((%s).x)*255.0)) | (uint(saturate((%s).y)*255.0)<<8) | "
            "(uint(saturate((%s).z)*255.0)<<16) | (uint(saturate((%s).w)*255.0)<<24))", a, a, a, a); break;
        case OP_UP4UB: snprintf(rhs, sizeof(rhs),
            "float4(asuint((%s).x) & 0xFF, (asuint((%s).x)>>8)&0xFF, "
            "(asuint((%s).x)>>16)&0xFF, (asuint((%s).x)>>24)&0xFF) / 255.0", a, a, a, a); break;
        case OP_PK4B: snprintf(rhs, sizeof(rhs),
            "asfloat((uint(clamp((%s).x,-1.0,1.0)*127.0)&0xFF) | ((uint(clamp((%s).y,-1.0,1.0)*127.0)&0xFF)<<8) | "
            "((uint(clamp((%s).z,-1.0,1.0)*127.0)&0xFF)<<16) | ((uint(clamp((%s).w,-1.0,1.0)*127.0)&0xFF)<<24))", a, a, a, a); break;
        case OP_UP4B: snprintf(rhs, sizeof(rhs),
            "(float4(int4(asint((%s).x)<<24, asint((%s).x)<<16, asint((%s).x)<<8, asint((%s).x)) >> 24)) / 127.0", a, a, a, a); break;
        case OP_PK2US: snprintf(rhs, sizeof(rhs),
            "asfloat((uint(saturate((%s).x)*65535.0)) | (uint(saturate((%s).y)*65535.0)<<16))", a, a); break;
        case OP_UP2US: snprintf(rhs, sizeof(rhs),
            "float4(asuint((%s).x) & 0xFFFF, (asuint((%s).x)>>16)&0xFFFF, 0, 0) / 65535.0", a, a); break;
        /* Texture/branch fences: ordering hints with no result -- no-op. */
        case OP_FENCT: case OP_FENCB: rhs[0] = '\0'; handled = 0; break;
        case OP_SLT: snprintf(rhs, sizeof(rhs), "(float4)((%s) <  (%s))", a, b); break;
        case OP_SGE: snprintf(rhs, sizeof(rhs), "(float4)((%s) >= (%s))", a, b); break;
        case OP_SLE: snprintf(rhs, sizeof(rhs), "(float4)((%s) <= (%s))", a, b); break;
        case OP_SGT: snprintf(rhs, sizeof(rhs), "(float4)((%s) >  (%s))", a, b); break;
        case OP_SNE: snprintf(rhs, sizeof(rhs), "(float4)((%s) != (%s))", a, b); break;
        case OP_SEQ: snprintf(rhs, sizeof(rhs), "(float4)((%s) == (%s))", a, b); break;
        case OP_TEX:
            snprintf(rhs, sizeof(rhs),
                     "rsx_tex[%u].Sample(rsx_samp[%u], (%s).xy)", tex_unit, tex_unit, a);
            break;
        case OP_TXP:
            snprintf(rhs, sizeof(rhs),
                     "rsx_tex[%u].Sample(rsx_samp[%u], (%s).xy / (%s).w)",
                     tex_unit, tex_unit, a, a);
            break;
        /* TXL: sample at explicit LOD (src0.w). */
        case OP_TXL:
            snprintf(rhs, sizeof(rhs),
                     "rsx_tex[%u].SampleLevel(rsx_samp[%u], (%s).xy, (%s).w)",
                     tex_unit, tex_unit, a, a);
            break;
        /* TXB: sample with LOD bias (src0.w). */
        case OP_TXB:
            snprintf(rhs, sizeof(rhs),
                     "rsx_tex[%u].SampleBias(rsx_samp[%u], (%s).xy, (%s).w)",
                     tex_unit, tex_unit, a, a);
            break;
        /* TXD: sample with explicit gradients (src1=ddx, src2=ddy). */
        case OP_TXD:
            snprintf(rhs, sizeof(rhs),
                     "rsx_tex[%u].SampleGrad(rsx_samp[%u], (%s).xy, (%s).xy, (%s).xy)",
                     tex_unit, tex_unit, a, b, c);
            break;
        case OP_KIL:
            /* Discard the fragment when the (modeled) condition fails. NV40 KIL
             * tests the condition-code register; we approximate the common
             * CC "less-than zero" case on src0. */
            out_puts(&o, "    if (any((r[0]).xyzw < 0.0)) discard;\n");
            handled = 0;
            break;
        default:
            out_puts(&o, "    /* TODO: unhandled FP opcode ");
            out_puts(&o, rsx_fp_opcode_name(opcode));
            out_puts(&o, " */\n");
            handled = 0;
            break;
        }

        if (handled && !(w0 & FP_OUT_NONE)) {
            u32 dst_idx = (w0 & FP_OUT_REG_MASK) >> FP_OUT_REG_SHIFT;
            int dst_half = (w0 & FP_OUT_HALF) ? 1 : 0;
            char m[5];
            dest_mask(w0, m);

            char line[900];
            /* NV40 per-instruction result modifiers, applied to the result
             * before the saturate (RPCS3 FragmentProgramDecompiler SetDst):
             *  - scale, SRC1 word bits 28-30: 1/2/3 = *2/*4/*8, 5/6/7 = /2 /4 /8.
             *    Dropped until now: GoW2's bloom bright-pass (FP 654864DA) sums
             *    four taps and its /4 was lost, so the 304x171 chain saturated
             *    white and the composite came out dark and blotched (2026-09-16;
             *    upstream fixed the same "whiteout" class in its s32 round).
             *  - precision, OPDEST bits 22-23: 0 fp32, 1 fp16, 2 fx12 (fixed point,
             *    [-2, 2]). Clamped as RPCS3 does (ON; PS3_FP_PRECISION_CLAMP=0
             *    disables). Measured 2026-09-16: Kratos went from a solid white
             *    silhouette to partly textured, soldiers/scene unchanged. */
            u32 res_scale = is_branch ? 0u : ((w2 >> 28) & 0x7u);
            static const char* scale_txt[8] = { "", " * 2.0", " * 4.0", " * 8.0",
                                                "", " / 2.0", " / 4.0", " / 8.0" };
            static int prec_clamp = -1;
            if (prec_clamp < 0) {
                const char* pe = getenv("PS3_FP_PRECISION_CLAMP");
                prec_clamp = (pe && pe[0] == '0') ? 0 : 1;
            }
            const u32 prec = (w0 >> 22) & 0x3u;
            char rhs_eff[720];
            if (prec_clamp && !(w0 & FP_OUT_SAT) && prec == 2u)
                snprintf(rhs_eff, sizeof(rhs_eff), "clamp((%s)%s, -2.0, 2.0)", rhs, scale_txt[res_scale]);
            else if (prec_clamp && !(w0 & FP_OUT_SAT) && prec == 1u)
                snprintf(rhs_eff, sizeof(rhs_eff), "clamp((%s)%s, -65504.0, 65504.0)", rhs, scale_txt[res_scale]);
            else if (res_scale)
                snprintf(rhs_eff, sizeof(rhs_eff), "((%s)%s)", rhs, scale_txt[res_scale]);
            else
                snprintf(rhs_eff, sizeof(rhs_eff), "%s", rhs);
            const char* sat_pre = (w0 & FP_OUT_SAT) ? "saturate(" : "";
            const char* sat_post = (w0 & FP_OUT_SAT) ? ")" : "";
            const char* bank = dst_half ? "h" : "r";
            int ncomp = (int)strlen(m);
            if (ncomp <= 0) ncomp = 4;

            /* Scalar ALU / dots yield float; wrap so Metal accepts partial masks.
             * Vector RHS is forced through float4(...).firstN so
             * `r[i].yzw = float4` (illegal in Metal) becomes
             * `r[i].yzw = (float4(rhs)).xyz`. */
            int rhs_scalar =
                (opcode == OP_DP3 || opcode == OP_DP4 || opcode == OP_DP2 ||
                 opcode == OP_DP2A || opcode == OP_RCP || opcode == OP_RSQ ||
                 opcode == OP_EX2 || opcode == OP_LG2 || opcode == OP_COS ||
                 opcode == OP_SIN || opcode == OP_POW ||
                 opcode == OP_PK2H || opcode == OP_PK4UB || opcode == OP_PK4B ||
                 opcode == OP_PK2US);


            if (rhs_scalar) {
                /* Temp avoids 4× explosion of huge const expressions (was
                 * truncating line[] and leaving unbalanced parens → Metal
                 * "expected ')'" at return). */
                snprintf(line, sizeof(line),
                         "    { float _s = %s%s%s; ",
                         sat_pre, rhs_eff, sat_post);
                out_puts(&o, line);
                if (ncomp == 1)
                    snprintf(line, sizeof(line), "%s[%u].%s = _s; }\n",
                             bank, dst_idx, m);
                else if (ncomp == 2)
                    snprintf(line, sizeof(line),
                             "%s[%u].%s = float2(_s,_s); }\n",
                             bank, dst_idx, m);
                else if (ncomp == 3)
                    snprintf(line, sizeof(line),
                             "%s[%u].%s = float3(_s,_s,_s); }\n",
                             bank, dst_idx, m);
                else
                    snprintf(line, sizeof(line),
                             "%s[%u] = float4(_s,_s,_s,_s); }\n",
                             bank, dst_idx);
                out_puts(&o, line);
            } else if (ncomp == 4 && strcmp(m, "xyzw") == 0) {
                snprintf(line, sizeof(line), "    %s[%u] = %s%s%s;\n",
                         bank, dst_idx, sat_pre, rhs_eff, sat_post);
                out_puts(&o, line);
            } else {
                /* Partial mask + vector: NV40 writes component-wise, dest.m =
                 * result.m (RPCS3 masks both sides; upstream's emit_store does
                 * `_v.m`). Taking the FIRST n components instead turned every
                 * non-prefix write wrong -- `MOV h0.w, h0` became h0.w = h0.x, so
                 * the character FP (CEFEF8CE) output alpha = red channel and
                 * models went translucent under SRC_ALPHA blending (2026-09-16). */
                snprintf(line, sizeof(line),
                         "    %s[%u].%s = (float4(%s%s%s)).%s;\n",
                         bank, dst_idx, m, sat_pre, rhs_eff, sat_post, m);
                out_puts(&o, line);
            }

            if (!dst_half && dst_idx == 0) wrote_r0 = 1;
            if (!dst_half && dst_idx == 1) s_fp_wrote_r1 = 1;
            if (dst_half  && dst_idx == 0) wrote_h0 = 1;
        }

        if (w0 & FP_END) break;
    }

    /* Fragment color output. Hardware uses SET_SHADER_CONTROL bit 0x40
     * (32_BITS_EXPORTS) to pick r0 vs h0. AUTO (no register yet): if the
     * program wrote h0, that is the colour (fp6B2E71EA writes colour to h0
     * then clobbers r0 with scalars — returning r0 is the yellow cast). */
    int use_r0;
    if (s_fp_color_export == 1)      use_r0 = 1;
    else if (s_fp_color_export == 0) use_r0 = 0;
    else                             use_r0 = wrote_h0 ? 0 : 1;
    if (use_r0)
        out_puts(&o, "    return r[0];\n}\n");
    else
        out_puts(&o, "    return h[0];\n}\n");

    if (!o.ok) return -1;
    return count;
}

/* ---- MSL emission (HLSL dialect → Metal, no SPIRV-Cross) -----------------
 * Policy M2/A: convert the existing decompiler dialect mechanically so HLSL
 * and MSL stay in lock-step. Rare pack/bit ops may still fail Metal compile;
 * the backend falls back to the fixed M1 PSO in that case.
 * -----------------------------------------------------------------------*/

static int msl_append(char* out, u32 out_size, u32* pos, const char* s)
{
    u32 n = (u32)strlen(s);
    if (*pos + n + 1u > out_size) return -1;
    memcpy(out + *pos, s, n + 1u);
    *pos += n;
    return 0;
}

/* Replace every occurrence of `from` with `to` into `out`. Returns 0/-1. */
static int msl_replace_all(const char* in, const char* from, const char* to,
                           char* out, u32 out_size)
{
    u32 pos = 0;
    size_t flen = strlen(from);
    size_t tlen = strlen(to);
    const char* p = in;
    if (flen == 0) return msl_append(out, out_size, &pos, in);

    while (*p) {
        const char* hit = strstr(p, from);
        if (!hit) {
            if (msl_append(out, out_size, &pos, p) != 0) return -1;
            return 0;
        }
        /* copy [p, hit) */
        while (p < hit) {
            if (pos + 2u > out_size) return -1;
            out[pos++] = *p++;
            out[pos] = '\0';
        }
        if (pos + (u32)tlen + 1u > out_size) return -1;
        memcpy(out + pos, to, tlen + 1u);
        pos += (u32)tlen;
        p = hit + flen;
    }
    return 0;
}

/* Rewrite Sample/SampleLevel/SampleBias/SampleGrad and HLSL-only intrinsics. */
static int msl_rewrite_fp_body(const char* body, char* out, u32 out_size)
{
    char a[96 * 1024], b[96 * 1024];
    if (msl_replace_all(body, "[unroll] ", "", a, sizeof(a)) != 0) return -1;
    if (msl_replace_all(a, "(float4)0", "float4(0)", b, sizeof(b)) != 0) return -1;
    if (msl_replace_all(b, "frac(", "fract(", a, sizeof(a)) != 0) return -1;
    if (msl_replace_all(a, "lerp(", "mix(", b, sizeof(b)) != 0) return -1;
    if (msl_replace_all(b, "ddx(", "dfdx(", a, sizeof(a)) != 0) return -1;
    if (msl_replace_all(a, "ddy(", "dfdy(", b, sizeof(b)) != 0) return -1;
    if (msl_replace_all(b, "discard;", "discard_fragment();", a, sizeof(a)) != 0) return -1;
    if (msl_replace_all(a, "lit(", "rsx_lit(", b, sizeof(b)) != 0) return -1;
    if (msl_replace_all(b, "asfloat(", "as_type<float>(", a, sizeof(a)) != 0) return -1;
    if (msl_replace_all(a, "asuint(", "as_type<uint>(", b, sizeof(b)) != 0) return -1;
    if (msl_replace_all(b, "asint(", "as_type<int>(", a, sizeof(a)) != 0) return -1;
    /* Texture sampling: HLSL Texture2D.Sample* → Metal texture2d.sample*.
     * SampleGrad(s, uv, ddx, ddy) → sample(s, uv, gradient2d(ddx, ddy)). */
    {
        const char* p = a;
        u32 pos = 0;
        const char* key = ".SampleGrad(rsx_samp[";
        b[0] = '\0';
        while (*p) {
            const char* hit = strstr(p, key);
            if (!hit) {
                if (msl_append(b, sizeof(b), &pos, p) != 0) return -1;
                break;
            }
            while (p < hit) {
                if (pos + 2u > sizeof(b)) return -1;
                b[pos++] = *p++;
                b[pos] = '\0';
            }
            /* Parse SampleGrad(rsx_samp[N], UV, DDX, DDY) with paren depth. */
            /* find '(' of SampleGrad */
            const char* open = strchr(hit, '(');
            if (!open) return -1;
            int depth = 1;
            const char* r = open + 1;
            const char* commas[3] = {0, 0, 0};
            int nc = 0;
            while (*r && depth > 0) {
                if (*r == '(') depth++;
                else if (*r == ')') {
                    depth--;
                    if (depth == 0) break;
                } else if (*r == ',' && depth == 1 && nc < 3)
                    commas[nc++] = r;
                r++;
            }
            if (depth != 0 || nc != 3) {
                /* fallback: copy as-is */
                size_t n = (size_t)(r - hit + 1);
                if (pos + n + 1u > sizeof(b)) return -1;
                memcpy(b + pos, hit, n);
                pos += (u32)n;
                b[pos] = '\0';
                p = r + 1;
                continue;
            }
            /* hit..open = ".SampleGrad"
             * open+1 .. commas[0] = rsx_samp[N]
             * commas[0]+1 .. commas[1] = UV
             * commas[1]+1 .. commas[2] = DDX
             * commas[2]+1 .. r = DDY
             */
            if (msl_append(b, sizeof(b), &pos, ".sample(") != 0) return -1;
            /* samp */
            {
                size_t n = (size_t)(commas[0] - (open + 1));
                if (pos + n + 1u > sizeof(b)) return -1;
                memcpy(b + pos, open + 1, n);
                pos += (u32)n;
                b[pos] = '\0';
            }
            if (msl_append(b, sizeof(b), &pos, ",") != 0) return -1;
            /* UV */
            {
                size_t n = (size_t)(commas[1] - (commas[0] + 1));
                if (pos + n + 1u > sizeof(b)) return -1;
                memcpy(b + pos, commas[0] + 1, n);
                pos += (u32)n;
                b[pos] = '\0';
            }
            if (msl_append(b, sizeof(b), &pos, ", gradient2d(") != 0) return -1;
            /* DDX */
            {
                size_t n = (size_t)(commas[2] - (commas[1] + 1));
                if (pos + n + 1u > sizeof(b)) return -1;
                memcpy(b + pos, commas[1] + 1, n);
                pos += (u32)n;
                b[pos] = '\0';
            }
            if (msl_append(b, sizeof(b), &pos, ",") != 0) return -1;
            /* DDY */
            {
                size_t n = (size_t)(r - (commas[2] + 1));
                if (pos + n + 1u > sizeof(b)) return -1;
                memcpy(b + pos, commas[2] + 1, n);
                pos += (u32)n;
                b[pos] = '\0';
            }
            if (msl_append(b, sizeof(b), &pos, "))") != 0) return -1;
            p = r + 1;
        }
        memcpy(a, b, strlen(b) + 1u);
    }
    if (msl_replace_all(a, ".SampleLevel(rsx_samp[",
                        ".sample(rsx_samp[", b, sizeof(b)) != 0) return -1;
    if (msl_replace_all(b, ".SampleBias(rsx_samp[",
                        ".sample(rsx_samp[", a, sizeof(a)) != 0) return -1;
    if (msl_replace_all(a, ".Sample(rsx_samp[",
                        ".sample(rsx_samp[", b, sizeof(b)) != 0) return -1;
    memcpy(a, b, strlen(b) + 1u);

    /* Expand SampleLevel leftover: sample(rsx_samp[U], UV, LOD)
     * → sample(rsx_samp[U], UV, level(LOD)). LOD is typically (expr).w */
    {
        /* Best-effort: ".sample(rsx_samp[N], X, Y)" where Y is LOD — if the
         * original was SampleLevel the third arg is scalar LOD. Without full
         * parse we leave 3-arg sample as-is; Metal rejects it and the PSO
         * cache falls back. Common GoW2 path is TEX (2-arg). */
    }

    /* HLSL bool→float4 casts: (float4)(a < b) → float4(select(0,1,a<b)) approx
     * as float4((a)<(b)) which Metal accepts for float via component ctor on
     * bool4 in recent MSL — use select for portability. */
    /* (float4)((a)<(b)) → select(float4(0),float4(1),(a)<(b)) */
    if (msl_replace_all(a, "(float4)((", "select(float4(0),float4(1),(", b, sizeof(b)) != 0)
        return -1;

    if (strlen(b) + 1u > out_size) return -1;
    memcpy(out, b, strlen(b) + 1u);
    return 0;
}

/* Cut the HLSL function body out of rsx_fp_decompile's output (between the
 * entry signature and the final closing brace). Shared by the MSL and GLSL
 * emitters so both see exactly the same instructions. 0 ok / -1. */
static int fp_hlsl_body(const char* hlsl, char* dst, u32 dst_size)
{
    const char* sig = "float4 main(PSInput input) : SV_TARGET {";
    const char* body = strstr(hlsl, sig);
    if (!body) return -1;
    body += strlen(sig);
    size_t blen = strlen(body);
    while (blen > 0 && (body[blen - 1] == '\n' || body[blen - 1] == '\r' ||
                        body[blen - 1] == ' '))
        blen--;
    if (blen > 0 && body[blen - 1] == '}') blen--;
    while (blen > 0 && (body[blen - 1] == '\n' || body[blen - 1] == ' '))
        blen--;
    if (blen + 1u > dst_size) return -1;
    memcpy(dst, body, blen);
    dst[blen] = '\0';
    return 0;
}

int rsx_fp_decompile_msl(const u8* ucode, u32 max_bytes, char* out, u32 out_size)
{
    static char hlsl[96 * 1024];
    static char body_msl[96 * 1024];
    if (!ucode || !out || out_size < 256) return -1;

    int n = rsx_fp_decompile(ucode, max_bytes, hlsl, sizeof(hlsl));
    if (n < 0) return -1;

    static char body_cut[96 * 1024];
    if (fp_hlsl_body(hlsl, body_cut, sizeof(body_cut)) != 0) return -1;

    if (msl_rewrite_fp_body(body_cut, body_msl, sizeof(body_msl)) != 0)
        return -1;

    /* Depth export (RPCS3 GLFragmentProgram: `gl_FragDepth = r1.z` when
     * SHADER_CONTROL & DEPTH_EXPORT and r1 is used; depth always comes from the
     * fp32 register). GoW2's scene copy 6B2E71EA decodes the scene depth from
     * the colour target into r1.z; without it the shadow volumes that follow
     * test against the quad's own depth. The final `return X;` becomes FSOut. */
    const int depth_out = s_fp_depth_export && s_fp_wrote_r1;
    const int mrt = s_fp_mrt_count;
    const int struct_out = depth_out || mrt > 1;
    char* last_ret = NULL;
    for (char* q = strstr(body_msl, "return "); q; q = strstr(q + 1, "return "))
        last_ret = q;
    if (struct_out && !last_ret) return -1;

    u32 pos = 0;
    if (msl_append(out, out_size, &pos,
        "#include <metal_stdlib>\n"
        "using namespace metal;\n"
        "struct PSIn {\n"
        "  float4 position [[position]];\n"
        "  float4 col0;\n"
        "  float4 col1;\n"
        "  float4 fog;\n"
        "  float4 tc0; float4 tc1; float4 tc2; float4 tc3;\n"
        "  float4 tc4; float4 tc5; float4 tc6; float4 tc7;\n"
        "};\n"
        "static inline float4 rsx_lit(float ndl, float nds, float m) {\n"
        "  float d = max(ndl, 0.0f);\n"
        "  float s = (ndl > 0.0f) ? pow(max(nds, 0.0f), m) : 0.0f;\n"
        "  return float4(1.0f, d, s, 1.0f);\n"
        "}\n"
        /* RSX alpha test (RPCS3 RSXROPEpilogue.glsl): the colour-0 alpha against
         * ALPHA_REF with ALPHA_FUNC, discard on fail. Function constant 0 selects
         * the variant: undefined/false = no test, the shader is today's. */
        "constant bool rsx_at_def [[function_constant(0)]];\n"
        "constant bool rsx_at_on = is_function_constant_defined(rsx_at_def) && rsx_at_def;\n"
        "static inline void rsx_atest(float4 c, uint4 at) {\n"
        "  if (!rsx_at_on) return;\n"
        "  float a = c.w, b = as_type<float>(at.z); bool p;\n"
        "  switch (at.y & 7u) {\n"
        "  case 0u: p = false; break;\n"
        "  case 1u: p = a < b; break;\n"
        "  case 2u: p = a == b; break;\n"
        "  case 3u: p = a <= b; break;\n"
        "  case 4u: p = a > b; break;\n"
        "  case 5u: p = a != b; break;\n"
        "  case 6u: p = a >= b; break;\n"
        "  default: p = true; break;\n"
        "  }\n"
        "  if (!p) discard_fragment();\n"
        "}\n") != 0)
        return -1;
    if (struct_out) {
        char decl[320];
        int dp = snprintf(decl, sizeof decl, "struct FSOut {");
        for (int c = 0; c < mrt; c++)
            dp += snprintf(decl + dp, sizeof decl - (size_t)dp, " float4 c%d [[color(%d)]];", c, c);
        if (depth_out)
            dp += snprintf(decl + dp, sizeof decl - (size_t)dp, " float depth [[depth(any)]];");
        snprintf(decl + dp, sizeof decl - (size_t)dp,
                 " };\nfragment FSOut fs_main(PSIn input [[stage_in]],\n");
        if (msl_append(out, out_size, &pos, decl) != 0) return -1;
    } else if (msl_append(out, out_size, &pos, "fragment float4 fs_main(PSIn input [[stage_in]],\n") != 0)
        return -1;
    if (msl_append(out, out_size, &pos,
        "    array<texture2d<float>, 16> rsx_tex [[texture(0)]],\n"
        "    array<sampler, 16> rsx_samp [[sampler(0)]]") != 0)
        return -1;
    if (s_fp_const_buf_mode &&
        msl_append(out, out_size, &pos, ",\n    constant float4* rsx_fc [[buffer(0)]]") != 0)
        return -1;
    if (msl_append(out, out_size, &pos, ",\n    constant uint4& rsx_at [[buffer(1)]]") != 0)
        return -1;
    if (msl_append(out, out_size, &pos, ") {\n") != 0)
        return -1;
    if (!struct_out) {
        /* alpha test on the value the program returns, right before it */
        const char* semi0 = last_ret ? strchr(last_ret + 7, ';') : NULL;
        if (!semi0) {
            if (msl_append(out, out_size, &pos, body_msl) != 0) return -1;
        } else {
            char at[300];
            const size_t el = (size_t)(semi0 - (last_ret + 7));
            if (el + 40u > sizeof(at)) return -1;
            snprintf(at, sizeof at, "rsx_atest(%.*s, rsx_at);\n    ", (int)el, last_ret + 7);
            const char saved = *last_ret;
            *last_ret = '\0';
            const int bad = msl_append(out, out_size, &pos, body_msl) != 0 ||
                            msl_append(out, out_size, &pos, at) != 0;
            *last_ret = saved;
            if (bad || msl_append(out, out_size, &pos, last_ret) != 0) return -1;
        }
    } else {
        /* body up to the final return, then `return X;` -> FSOut{X, r1.z}. */
        const char* expr = last_ret + 7;
        const char* semi = strchr(expr, ';');
        if (!semi) return -1;
        char tail[256];
        const size_t elen = (size_t)(semi - expr);
        if (elen + 1u > sizeof(tail)) return -1;
        memcpy(tail, expr, elen);
        tail[elen] = '\0';
        *last_ret = '\0';
        if (msl_append(out, out_size, &pos, body_msl) != 0) return -1;
        /* c0 e' exactamente o que o programa devolvia; c1..c3 saem dos
         * registos exportados do MESMO banco (r0 -> r2,r3,r4; h0 -> h4,h6,h8). */
        const int bank_h = strstr(tail, "h[0]") != NULL;
        static const int idx_r[4] = { 0, 2, 3, 4 }, idx_h[4] = { 0, 4, 6, 8 };
        char ret[768];
        int rp = snprintf(ret, sizeof(ret), "{ rsx_atest(%s, rsx_at); FSOut _fo; _fo.c0 = %s;", tail, tail);
        for (int c = 1; c < mrt; c++)
            rp += snprintf(ret + rp, sizeof(ret) - (size_t)rp, " _fo.c%d = %s[%d];", c,
                           bank_h ? "h" : "r", bank_h ? idx_h[c] : idx_r[c]);
        if (depth_out)
            rp += snprintf(ret + rp, sizeof(ret) - (size_t)rp, " _fo.depth = r[1].z;");
        snprintf(ret + rp, sizeof(ret) - (size_t)rp, " return _fo; }%s", semi + 1);
        if (msl_append(out, out_size, &pos, ret) != 0) return -1;
    }
    if (msl_append(out, out_size, &pos, "\n}\n") != 0) return -1;
    return n;
}

int rsx_fp_decompile_glsl(const u8* ucode, u32 max_bytes, char* out, u32 out_size)
{
    static char hlsl[96 * 1024];
    if (!ucode || !out || out_size < 256) return -1;
    out[0] = '\0';
    int n = rsx_fp_decompile(ucode, max_bytes, hlsl, sizeof(hlsl));
    if (n < 0) return -1;
    return rsx_fp_glsl_from_hlsl(hlsl, n, out, out_size);
}

int rsx_fp_glsl_from_hlsl(const char* hlsl, int n, char* out, u32 out_size)
{
    static char body_cut[96 * 1024];
    static char body[96 * 1024];
    if (!hlsl || !out || out_size < 256) return -1;
    out[0] = '\0';
    if (fp_hlsl_body(hlsl, body_cut, sizeof(body_cut)) != 0) return -1;
    if (rsx_glsl_rewrite_body(body_cut, body, sizeof(body)) != 0) return -1;

    /* Exactly one `return X;` (the colour export). An early return would skip
     * the output writes below, so refuse instead of emitting a wrong shader. */
    char* ret = strstr(body, "return ");
    if (!ret || strstr(ret + 1, "return ")) return -1;
    const char* expr = ret + 7;
    const char* semi = strchr(expr, ';');
    if (!semi) return -1;
    char tail[256];
    const size_t elen = (size_t)(semi - expr);
    if (elen + 1u > sizeof(tail)) return -1;
    memcpy(tail, expr, elen);
    tail[elen] = '\0';

    const int depth_out = s_fp_depth_export && s_fp_wrote_r1;
    const int mrt = s_fp_mrt_count < 1 ? 1 : (s_fp_mrt_count > 4 ? 4 : s_fp_mrt_count);

    u32 pos = 0;
    if (msl_append(out, out_size, &pos, "#version 450\n") != 0) goto overflow;
    if (msl_append(out, out_size, &pos, RSX_GLSL_PRELUDE) != 0) goto overflow;
    if (msl_append(out, out_size, &pos, RSX_GLSL_PRELUDE_FRAG) != 0) goto overflow;
    if (msl_append(out, out_size, &pos,
        "struct PSIn { vec4 position; vec4 col0; vec4 col1; vec4 fog;\n"
        "  vec4 tc0; vec4 tc1; vec4 tc2; vec4 tc3; vec4 tc4; vec4 tc5; vec4 tc6; vec4 tc7; };\n"
        "layout(location = 0) in vec4 v_col0;\n"
        "layout(location = 1) in vec4 v_col1;\n"
        "layout(location = 2) in vec4 v_fog;\n"
        "layout(location = 3) in vec4 v_tc0;\n"
        "layout(location = 4) in vec4 v_tc1;\n"
        "layout(location = 5) in vec4 v_tc2;\n"
        "layout(location = 6) in vec4 v_tc3;\n"
        "layout(location = 7) in vec4 v_tc4;\n"
        "layout(location = 8) in vec4 v_tc5;\n"
        "layout(location = 9) in vec4 v_tc6;\n"
        "layout(location = 10) in vec4 v_tc7;\n"
        "layout(set = 0, binding = 0) uniform sampler2D rsx_tex[16];\n"
        "layout(std140, set = 0, binding = 2) uniform RsxAt { uvec4 rsx_at; };\n"
        "layout(constant_id = 0) const bool rsx_at_on = false;\n"
        /* RSX alpha test (RPCS3 RSXROPEpilogue.glsl), same table as the MSL. */
        "void rsx_atest(vec4 c, uvec4 at) {\n"
        "  if (!rsx_at_on) return;\n"
        "  float a = c.w, b = uintBitsToFloat(at.z); bool p;\n"
        "  switch (at.y & 7u) {\n"
        "  case 0u: p = false; break;\n"
        "  case 1u: p = a < b; break;\n"
        "  case 2u: p = a == b; break;\n"
        "  case 3u: p = a <= b; break;\n"
        "  case 4u: p = a > b; break;\n"
        "  case 5u: p = a != b; break;\n"
        "  case 6u: p = a >= b; break;\n"
        "  default: p = true; break;\n"
        "  }\n"
        "  if (!p) discard;\n"
        "}\n") != 0)
        goto overflow;
    if (s_fp_const_buf_mode &&
        msl_append(out, out_size, &pos,
                   "layout(std430, set = 0, binding = 1) readonly buffer RsxFc { vec4 rsx_fc[]; };\n") != 0)
        goto overflow;
    for (int c = 0; c < mrt; c++) {
        char decl[64];
        snprintf(decl, sizeof decl, "layout(location = %d) out vec4 rsx_c%d;\n", c, c);
        if (msl_append(out, out_size, &pos, decl) != 0) goto overflow;
    }
    if (msl_append(out, out_size, &pos,
        "void main() {\n"
        "    PSIn rsx_in;\n"
        "    rsx_in.position = gl_FragCoord; rsx_in.col0 = v_col0; rsx_in.col1 = v_col1;\n"
        "    rsx_in.fog = v_fog; rsx_in.tc0 = v_tc0; rsx_in.tc1 = v_tc1; rsx_in.tc2 = v_tc2;\n"
        "    rsx_in.tc3 = v_tc3; rsx_in.tc4 = v_tc4; rsx_in.tc5 = v_tc5; rsx_in.tc6 = v_tc6;\n"
        "    rsx_in.tc7 = v_tc7;\n") != 0)
        goto overflow;
    *ret = '\0';                                  /* body up to the return */
    {
        const int bad = msl_append(out, out_size, &pos, body) != 0;
        *ret = 'r';
        if (bad) goto overflow;
    }
    {
        /* c0 is what the program returned; c1..c3 come from the SAME bank's
         * export registers (r0 -> r2,r3,r4; h0 -> h4,h6,h8), as in the MSL. */
        const int bank_h = strstr(tail, "h[0]") != NULL;
        static const int idx_r[4] = { 0, 2, 3, 4 }, idx_h[4] = { 0, 4, 6, 8 };
        char epi[768];
        int ep = snprintf(epi, sizeof epi,
                          "{ vec4 _c0 = %s; rsx_atest(_c0, rsx_at); rsx_c0 = _c0;", tail);
        for (int c = 1; c < mrt; c++)
            ep += snprintf(epi + ep, sizeof epi - (size_t)ep, " rsx_c%d = %s[%d];", c,
                           bank_h ? "h" : "r", bank_h ? idx_h[c] : idx_r[c]);
        if (depth_out)
            ep += snprintf(epi + ep, sizeof epi - (size_t)ep, " gl_FragDepth = r[1].z;");
        snprintf(epi + ep, sizeof epi - (size_t)ep, " return; }%s", semi + 1);
        if (msl_append(out, out_size, &pos, epi) != 0) goto overflow;
    }
    if (msl_append(out, out_size, &pos, "\n}\n") != 0) goto overflow;
    return n;

overflow:
    out[0] = '\0';
    return -1;
}
