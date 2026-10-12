/*
 * ps3recomp - RSX Vertex Program (NV40 ISA) -> HLSL decompiler
 * See rsx_vp_decompiler.h. Encoding from RPCS3 RSXVertexProgram.h (D0..D3).
 */
#include "rsx_vp_decompiler.h"
#include "rsx_glsl_dialect.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ---- little-endian word read (RPCS3 native order) ---------------------- */
static u32 rd_le(const u8* p)
{ return (u32)p[0] | ((u32)p[1]<<8) | ((u32)p[2]<<16) | ((u32)p[3]<<24); }

/* ---- opcode names ------------------------------------------------------ */
const char* rsx_vp_vec_name(u32 op)
{
    static const char* n[] = {"NOP","MOV","MUL","ADD","MAD","DP3","DPH","DP4",
        "DST","MIN","MAX","SLT","SGE","ARL","FRC","FLR","SEQ","SFL","SGT","SLE",
        "SNE","STR","SSG","?","?","TXL"};
    return op < 26 ? n[op] : "?";
}
const char* rsx_vp_sca_name(u32 op)
{
    static const char* n[] = {"NOP","MOV","RCP","RCC","RSQ","EXP","LOG","LIT",
        "BRA","BRI","CAL","CLI","RET","LG2","EX2","SIN","COS","BRB","CLB","PSH","POP"};
    return op < 21 ? n[op] : "?";
}

u32 rsx_vp_program_size_instrs(const u8* ucode, u32 max_bytes)
{
    if (!ucode) return 0;
    u32 off = 0, instrs = 0;
    while (off + 16 <= max_bytes) {
        u32 d3 = rd_le(ucode + off + 12);
        instrs++;
        off += 16;
        if (d3 & 1u) return instrs;   /* D3.end */
    }
    return 0;
}

/* ---- output appender --------------------------------------------------- */
typedef struct { char* p; u32 cap; u32 len; int overflow; } Out;
static void emit(Out* o, const char* s)
{
    u32 n = (u32)strlen(s);
    if (o->len + n + 1 > o->cap) { o->overflow = 1; return; }
    memcpy(o->p + o->len, s, n); o->len += n; o->p[o->len] = '\0';
}

/* ---- source operand ---------------------------------------------------- */
static const char SWZ[4] = {'x','y','z','w'};

/* Build the HLSL expression for one 17-bit source field. */
static void emit_src(u32 src, int is_abs, u32 input_src, u32 const_src,
                     int index_const, u32 addr_sel, u32 addr_swz,
                     char* buf, u32 bufsz)
{
    u32 reg_type = src & 3;
    u32 tmp_src  = (src >> 2) & 0x3F;
    u32 sx = (src >> 14) & 3, sy = (src >> 12) & 3;
    u32 sz = (src >> 10) & 3, sw = (src >> 8) & 3;
    int neg = (src >> 16) & 1;

    char base[64];
    /* reg_type: 1 = temp, 2 = input, 3 = constant (upstream sp00nznet
     * rsx_vp_decompiler.c, verified against the oracle). This table had them
     * shifted by one -- input decoded as constant, constant as temp, temp as
     * input -- so a matrix-vector MUL/MAD chain computed 0 and every 3D position
     * collapsed (E449/E451: with a magenta FS and no depth test nothing but NDC
     * quads rasterized). D3.index_const adds the address register component
     * a<D0.addr_reg_sel>.<D0.addr_swz> to the constant index (skinning's bone
     * palette; same rule as upstream HEAD). */
    switch (reg_type) {
    case 2:  snprintf(base, sizeof(base), "v[%u]", input_src & 15u); break;  /* input attr */
    case 3:                                                                   /* constant */
        if (index_const)
            snprintf(base, sizeof(base), "vp_c[(%uu + uint(a%u.%c)) & 511u]",
                     const_src & 511u, addr_sel & 1u, SWZ[addr_swz & 3u]);
        else
            snprintf(base, sizeof(base), "vp_c[%u]", const_src & 511u);
        break;
    default: snprintf(base, sizeof(base), "r[%u]", tmp_src); break;          /* temp */
    }

    char swz[8];
    swz[0]='.'; swz[1]=SWZ[sx]; swz[2]=SWZ[sy]; swz[3]=SWZ[sz]; swz[4]=SWZ[sw]; swz[5]='\0';

    const char* pre = neg ? "-" : "";
    if (is_abs) snprintf(buf, bufsz, "%sabs(%s)%s", pre, base, swz);
    else        snprintf(buf, bufsz, "%s(%s)%s", pre, base, swz);
}

/* writemask string from 4 bits (x,y,z,w order) -> ".xyzw" subset */
static void mask_str(int mx,int my,int mz,int mw, char* m)
{
    int i=0;
    m[i++]='.';
    if (mx) m[i++]='x';
    if (my) m[i++]='y';
    if (mz) m[i++]='z';
    if (mw) m[i++]='w';
    if (i==1) m[0]='\0'; else m[i]='\0';   /* empty if no mask */
}

/* Emit `tgt[idx]mask = (rhs);` with types that both HLSL and Metal accept.
 *
 * Metal is strict: `float3 x = float4(...)` and `o[i].xyz = vp_c[n]` (float4
 * on the right) are compile errors — the VS library failure seen on Mac
 * (`assigning to 'float3' from 'const constant float4'`). Match the RHS
 * width to the writemask; splat scalars. */
static void emit_masked_assign(Out* b, const char* tgt, u32 idx,
                               const char* mask, const char* rhs, int rhs_scalar)
{
    char line[640];
    int ncomp = (mask[0] == '.') ? (int)strlen(mask) - 1 : 4;
    if (ncomp <= 0) ncomp = 4;

    if (rhs_scalar) {
        if (ncomp == 1 && mask[0] == '.') {
            /* o[i].x = (s); */
            snprintf(line, sizeof line, "    %s[%u]%s = (%s);\n",
                     tgt, idx, mask, rhs);
        } else if (ncomp == 1) {
            snprintf(line, sizeof line, "    %s[%u] = float4((%s),(%s),(%s),(%s));\n",
                     tgt, idx, rhs, rhs, rhs, rhs);
        } else if (ncomp == 2) {
            snprintf(line, sizeof line, "    %s[%u]%s = float2((%s),(%s));\n",
                     tgt, idx, mask[0] ? mask : ".xy", rhs, rhs);
        } else if (ncomp == 3) {
            snprintf(line, sizeof line, "    %s[%u]%s = float3((%s),(%s),(%s));\n",
                     tgt, idx, mask[0] ? mask : ".xyz", rhs, rhs, rhs);
        } else {
            snprintf(line, sizeof line, "    %s[%u] = float4((%s),(%s),(%s),(%s));\n",
                     tgt, idx, rhs, rhs, rhs, rhs);
        }
    } else if (mask[0] == '.' && ncomp < 4) {
        /* Partial vector write: o[i].xyz = ((rhs).xyz); */
        snprintf(line, sizeof line, "    %s[%u]%s = ((%s)%s);\n",
                 tgt, idx, mask, rhs, mask);
    } else {
        snprintf(line, sizeof line, "    %s[%u]%s = (%s);\n",
                 tgt, idx, mask, rhs);
    }
    emit(b, line);
}

int rsx_vp_decompile(const u8* ucode, u32 max_bytes, char* out, u32 out_size)
{
    if (!ucode || !out || out_size < 256) return -1;

    /* Body is built first, preamble/epilogue wrap it. */
    static char body[96 * 1024];
    Out b = { body, sizeof(body), 0, 0 };
    body[0] = '\0';

    u32 off = 0; int instrs = 0;
    while (off + 16 <= max_bytes) {
        u32 d0 = rd_le(ucode+off+0), d1 = rd_le(ucode+off+4);
        u32 d2 = rd_le(ucode+off+8), d3 = rd_le(ucode+off+12);
        off += 16; instrs++;

        u32 vec_op = (d1 >> 22) & 0x1F;
        u32 sca_op = (d1 >> 27) & 0x1F;
        u32 input_src = (d1 >> 8) & 0xF;
        u32 const_src = (d1 >> 12) & 0x3FF;

        /* assemble the three 17-bit source fields */
        u32 src0 = ((d2 >> 23) & 0x1FF) | ((d1 & 0xFF) << 9);
        u32 src1 = (d2 >> 6) & 0x1FFFF;
        u32 src2 = ((d3 >> 21) & 0x7FF) | ((d2 & 0x3F) << 11);
        int a0 = (d0 >> 21) & 1, a1 = (d0 >> 22) & 1, a2 = (d0 >> 23) & 1;
        int index_const = (d3 >> 1) & 1;
        u32 addr_sel = (d0 >> 24) & 1;
        u32 addr_swz = d0 & 3;
        int sat = (d0 >> 26) & 1;   /* D0.saturate: clamp the result to 0..1 */

        char A[96], B[96], C[96];
        emit_src(src0, a0, input_src, const_src, index_const, addr_sel, addr_swz, A, sizeof(A));
        emit_src(src1, a1, input_src, const_src, index_const, addr_sel, addr_swz, B, sizeof(B));
        emit_src(src2, a2, input_src, const_src, index_const, addr_sel, addr_swz, C, sizeof(C));

        u32 dst_tmp     = (d0 >> 15) & 0x3F;
        u32 dst_out     = (d3 >> 2) & 0x1F;
        u32 sca_dst_tmp = (d3 >> 7) & 0x3F;
        int vec_result  = (d0 >> 30) & 1;

        int vmx=(d3>>16)&1, vmy=(d3>>15)&1, vmz=(d3>>14)&1, vmw=(d3>>13)&1;
        int smx=(d3>>20)&1, smy=(d3>>19)&1, smz=(d3>>18)&1, smw=(d3>>17)&1;

        /* ---- vector ALU ---- */
        if ((vmx|vmy|vmz|vmw) && vec_op == 0x0D) {
            /* ARL loads the address register a<addr_sel> (rounded down); it
             * writes no temp or output (it used to put floor(A) into r[dst_tmp]). */
            char m[6]; mask_str(vmx,vmy,vmz,vmw,m);
            char line[256];
            snprintf(line, sizeof line, "    { int4 _a = int4(floor(%s)); a%u%s = _a%s; }\n",
                     A, addr_sel, m, m);
            emit(&b, line);
        } else if ((vmx|vmy|vmz|vmw) && vec_op != 0x00) {
            char rhs[384];
            switch (vec_op) {
            case 0x01: snprintf(rhs,sizeof rhs,"%s",A); break;                 /* MOV */
            case 0x02: snprintf(rhs,sizeof rhs,"(%s)*(%s)",A,B); break;        /* MUL */
            case 0x03: snprintf(rhs,sizeof rhs,"(%s)+(%s)",A,C); break;        /* ADD: src0 + src2 */
            case 0x04: snprintf(rhs,sizeof rhs,"(%s)*(%s)+(%s)",A,B,C); break; /* MAD */
            case 0x05: snprintf(rhs,sizeof rhs,"dot((%s).xyz,(%s).xyz)",A,B); break; /* DP3 */
            case 0x06: snprintf(rhs,sizeof rhs,"(dot((%s).xyz,(%s).xyz)+(%s).w)",A,B,B); break; /* DPH */
            case 0x07: snprintf(rhs,sizeof rhs,"dot((%s),(%s))",A,B); break; /* DP4 */
            case 0x08: snprintf(rhs,sizeof rhs,"float4(1,(%s).y*(%s).y,(%s).z,(%s).w)",A,B,A,B); break; /* DST */
            case 0x09: snprintf(rhs,sizeof rhs,"min((%s),(%s))",A,B); break;   /* MIN */
            case 0x0A: snprintf(rhs,sizeof rhs,"max((%s),(%s))",A,B); break;   /* MAX */
            case 0x0B: snprintf(rhs,sizeof rhs,"(float4)((%s)<(%s))",A,B); break;  /* SLT */
            case 0x0C: snprintf(rhs,sizeof rhs,"(float4)((%s)>=(%s))",A,B); break; /* SGE */
            case 0x0E: snprintf(rhs,sizeof rhs,"frac(%s)",A); break;           /* FRC */
            case 0x0F: snprintf(rhs,sizeof rhs,"floor(%s)",A); break;          /* FLR */
            case 0x10: snprintf(rhs,sizeof rhs,"(float4)((%s)==(%s))",A,B); break; /* SEQ */
            case 0x12: snprintf(rhs,sizeof rhs,"(float4)((%s)>(%s))",A,B); break;  /* SGT */
            case 0x13: snprintf(rhs,sizeof rhs,"(float4)((%s)<=(%s))",A,B); break; /* SLE */
            case 0x14: snprintf(rhs,sizeof rhs,"(float4)((%s)!=(%s))",A,B); break; /* SNE */
            case 0x16: snprintf(rhs,sizeof rhs,"sign(%s)",A); break;           /* SSG */
            case 0x11: snprintf(rhs,sizeof rhs,"float4(0,0,0,0)"); break;      /* SFL: set false */
            case 0x15: snprintf(rhs,sizeof rhs,"float4(1,1,1,1)"); break;      /* STR: set true */
            case 0x19: snprintf(rhs,sizeof rhs,"%s",A); break;                 /* TXL: vertex texture (no VTF bound) ~ passthrough */
            default:   snprintf(rhs,sizeof rhs,"%s",A); break;  /* unknown: passthrough */
            }
            char m[6]; mask_str(vmx,vmy,vmz,vmw,m);
            /* DP3/DPH/DP4 yield a scalar; all other vec ops yield float4-like. */
            int vec_scalar = (vec_op == 0x05 || vec_op == 0x06 || vec_op == 0x07);
            char srhs[400];
            const char* v_rhs = rhs;
            if (sat) { snprintf(srhs, sizeof srhs, "saturate(%s)", rhs); v_rhs = srhs; }
            /* The VEC result goes to o[D3.dst] when D0.vec_result (0x1F = none)
             * AND to r[D0.dst_tmp] (0x3F = none) -- both, not one or the other
             * (upstream's oracle-verified dest rules). */
            if (vec_result && dst_out != 0x1F)
                emit_masked_assign(&b, "o", dst_out, m, v_rhs, vec_scalar);
            if (dst_tmp != 0x3F)
                emit_masked_assign(&b, "r", dst_tmp, m, v_rhs, vec_scalar);
        }

        /* ---- scalar ALU ---- */
        if ((smx|smy|smz|smw) && sca_op != 0x00) {
            char rhs[640];
            int sca_scalar = 1;
            /* The SCA unit reads SRC2 (upstream's oracle-verified rule). RSQ/LOG
             * guard the domain and RCC clamps, as upstream HEAD does; LIT is the
             * real lighting coefficient vector (it was a passthrough). */
            switch (sca_op) {
            case 0x01: snprintf(rhs,sizeof rhs,"%s",C); sca_scalar = 0; break; /* MOV (vec) */
            case 0x02: snprintf(rhs,sizeof rhs,"(1.0/(%s).x)",C); break;  /* RCP */
            case 0x03: snprintf(rhs,sizeof rhs,"clamp(1.0/(%s).x, 5.42101e-20, 1.884467e19)",C); break; /* RCC */
            case 0x04: snprintf(rhs,sizeof rhs,"rsqrt(max(abs((%s).x), 1e-10))",C); break; /* RSQ */
            case 0x05: snprintf(rhs,sizeof rhs,"exp2((%s).x)",C); break;  /* EXP */
            case 0x06: snprintf(rhs,sizeof rhs,"log2(max(abs((%s).x), 1e-10))",C); break; /* LOG */
            case 0x0D: snprintf(rhs,sizeof rhs,"log2(max(abs((%s).x), 1e-10))",C); break; /* LG2 */
            case 0x0E: snprintf(rhs,sizeof rhs,"exp2((%s).x)",C); break;  /* EX2 */
            case 0x0F: snprintf(rhs,sizeof rhs,"sin((%s).x)",C); break;   /* SIN */
            case 0x10: snprintf(rhs,sizeof rhs,"cos((%s).x)",C); break;   /* COS */
            case 0x07: /* LIT: (1, max(s.x,0), s.x>0 ? 2^(s.w*log2(max(s.y,eps))) : 0, 1) */
                snprintf(rhs,sizeof rhs,
                    "float4(1.0, max((%s).x, 0.0), ((%s).x > 0.0) ? "
                    "exp2(clamp((%s).w, -127.996, 127.996) * log2(max((%s).y, 1e-10))) : 0.0, 1.0)",
                    C, C, C, C);
                sca_scalar = 0;
                break;
            default:   rhs[0]='\0'; break; /* BRA/CAL/RET/flow: no result */
            }
            if (rhs[0]) {
                char m[6]; mask_str(smx,smy,smz,smw,m);
                char srhs[660];
                const char* s_rhs = rhs;
                if (sat) { snprintf(srhs, sizeof srhs, "saturate(%s)", rhs); s_rhs = srhs; }
                /* SCA writes the output register when the VEC unit does not own it,
                 * AND its own temp r[D3.sca_dst_tmp] whenever that is valid (upstream
                 * HEAD). The old `else if` dropped the temp write when vec_result=0:
                 * normalize()'s RSQ vanished from the skinned, normal-lit VPs, the
                 * normal was scaled by |N|^2 and Kratos lit white/black (2026-09-16). */
                if (!vec_result && dst_out != 0x1F)
                    emit_masked_assign(&b, "o", dst_out, m, s_rhs, sca_scalar);
                if (sca_dst_tmp != 0x3F)
                    emit_masked_assign(&b, "r", sca_dst_tmp, m, s_rhs, sca_scalar);
            }
        }

        if (d3 & 1u) break; /* end */
    }

    if (b.overflow) return -1;

    /* ---- assemble full shader ---- */
    Out o = { out, out_size, 0, 0 };
    emit(&o,
        /* Input layout is intentionally minimal so it matches the backend's
         * existing vertex feed (POSITION float3 + COLOR0 float4); the other
         * 14 attribute slots are zero locals. Generalising to all 16 RSX
         * vertex attributes is a follow-up (needs a wide vertex + layout). */
        "struct VSInput {\n"
        "    float3 v0 : POSITION;\n"
        "    float4 v3 : COLOR0;\n"
        "};\n"
        "struct VSOutput {\n"
        "    float4 pos:SV_Position; float4 col0:COLOR0; float4 col1:COLOR1;\n"
        "    float4 fog:FOG;\n"
        "    float4 t0:TEXCOORD0; float4 t1:TEXCOORD1; float4 t2:TEXCOORD2; float4 t3:TEXCOORD3;\n"
        "    float4 t4:TEXCOORD4; float4 t5:TEXCOORD5; float4 t6:TEXCOORD6; float4 t7:TEXCOORD7;\n"
        "};\n"
        /* vp_c sized to the full 9-bit-plus const bank; r/o sized to the full
         * temp(6-bit)/output(5-bit) index fields so no constant index can be
         * out of bounds (HLSL error X3504). */
        "cbuffer VPConst : register(b0) { float4 vp_c[1024]; };\n"
        "VSOutput main(VSInput input) {\n"
        "    float4 v[16];\n"
        "    [unroll] for (int _k=0;_k<16;_k++) v[_k]=(float4)0;\n"
        "    v[0]=float4(input.v0,1); v[3]=input.v3;\n"
        "    float4 r[64]; float4 o[32];\n"
        "    int4 a0 = int4(0,0,0,0); int4 a1 = int4(0,0,0,0);\n"
        "    [unroll] for (int _i=0;_i<64;_i++) r[_i]=(float4)0;\n"
        "    [unroll] for (int _j=0;_j<32;_j++) o[_j]=(float4)0;\n");

    emit(&o, body);

    /* Map NV40 VP output registers to the VSOutput varyings.
     * o[0]=HPOS, o[1]=COL0, o[2]=COL1, o[5]=FOG, o[7..14]=TEX0..7 (upstream's oracle-verified map;
     * the old o[8..15] shifted every texcoord by one). */
    emit(&o,
        "    VSOutput Out;\n"
        "    Out.pos = o[0]; Out.col0 = o[1]; Out.col1 = o[2]; Out.fog = o[5];\n"
        "    Out.t0=o[7]; Out.t1=o[8]; Out.t2=o[9]; Out.t3=o[10];\n"
        "    Out.t4=o[11]; Out.t5=o[12]; Out.t6=o[13]; Out.t7=o[14];\n"
        "    return Out;\n"
        "}\n");

    if (o.overflow) return -1;
    return instrs;
}

/* ---- MSL emission (HLSL dialect → Metal) -------------------------------- */

static int vp_msl_append(char* out, u32 out_size, u32* pos, const char* s)
{
    u32 n = (u32)strlen(s);
    if (*pos + n + 1u > out_size) return -1;
    memcpy(out + *pos, s, n + 1u);
    *pos += n;
    return 0;
}

static int vp_msl_replace_all(const char* in, const char* from, const char* to,
                              char* out, u32 out_size)
{
    u32 pos = 0;
    size_t flen = strlen(from);
    size_t tlen = strlen(to);
    const char* p = in;
    if (flen == 0) return vp_msl_append(out, out_size, &pos, in);
    while (*p) {
        const char* hit = strstr(p, from);
        if (!hit) {
            if (vp_msl_append(out, out_size, &pos, p) != 0) return -1;
            return 0;
        }
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

/* -1 = usar as chaves do ambiente; >= 0 = bits (1 OUT_W1, 2 NO_TC0_HACK,
 * 4 NO_UV_SEED, 8 FOG_X) para esta e as proximas chamadas. */
static int s_vp_fix_override = -1;
void rsx_vp_set_fix_override(int bits) { s_vp_fix_override = bits; }
int rsx_vp_fix_env_bits(void)
{
    const char* e;
    int b = 0;
    e = getenv("PS3_RSX_FIX_OUT_W1");      if (e && e[0] == '1') b |= 1;
    e = getenv("PS3_RSX_FIX_NO_TC0_HACK"); if (e && e[0] == '1') b |= 2;
    e = getenv("PS3_RSX_FIX_NO_UV_SEED");  if (!(e && e[0] == '0')) b |= 4;
    e = getenv("PS3_RSX_FIX_FOG_X");       if (e && e[0] == '1') b |= 8;
    return b;
}

/* Instruction body between the o[] zero-init loop and the VSOutput map. */
static int vp_hlsl_body(const char* hlsl, char* dst, u32 dst_size)
{
    const char* start_mark =
        "    [unroll] for (int _j=0;_j<32;_j++) o[_j]=(float4)0;\n";
    const char* end_mark = "    VSOutput Out;\n";
    const char* b0 = strstr(hlsl, start_mark);
    const char* b1 = strstr(hlsl, end_mark);
    if (!b0 || !b1 || b1 <= b0) return -1;
    b0 += strlen(start_mark);
    size_t blen = (size_t)(b1 - b0);
    if (blen + 1u > dst_size) return -1;
    memcpy(dst, b0, blen);
    dst[blen] = '\0';
    return 0;
}

typedef struct { int w1, nohack, noseed, fogx; } VpFix;

/* PS3_RSX_FIX_* A/B switches (read once) with rsx_vp_set_fix_override on top.
 * Chaves de A/B dos desvios contra o RPCS3 (documento do utilizador,
 * 2026-09-21). Todas OFF por default: o jogo so' muda quando uma e' ligada.
 *   PS3_RSX_FIX_OUT_W1=1      §1  o[] comeca em (0,0,0,1), nao (0,0,0,0)
 *   PS3_RSX_FIX_NO_TC0_HACK=1 §2  sem o "if (o[7]==0) o[7]=v[8]" (so' nosso)
 *   PS3_RSX_FIX_NO_UV_SEED    §3  ON por default (=0 desliga): sem semear
 *                                  v[8] com o UV empacotado; sem VAMap, os
 *                                  atributos vem do VERTEX_DATA4F
 *   PS3_RSX_FIX_FOG_X=1       §6  fog = o[5].xxxx (o RPCS3 so' usa o .x) */
static VpFix vp_fix_resolve(void)
{
    static int fx_w1 = -1, fx_nohack = -1, fx_noseed = -1, fx_fogx = -1;
    if (fx_w1 < 0) {
        const char* e;
        e = getenv("PS3_RSX_FIX_OUT_W1");      fx_w1     = (e && e[0] == '1');
        e = getenv("PS3_RSX_FIX_NO_TC0_HACK"); fx_nohack = (e && e[0] == '1');
        /* §3 LIGADA por default (2026-09-21): o utilizador comparou lado a lado
         * FIX 0 x FIX 3 no jogo e ficou com a 3. =0 volta ao comportamento antigo. */
        e = getenv("PS3_RSX_FIX_NO_UV_SEED");  fx_noseed = !(e && e[0] == '0');
        e = getenv("PS3_RSX_FIX_FOG_X");       fx_fogx   = (e && e[0] == '1');
        if (fx_w1 || fx_nohack || fx_noseed || fx_fogx)
            fprintf(stderr, "[RSXFIX] VP: out_w1=%d no_tc0_hack=%d no_uv_seed=%d fog_x=%d\n",
                    fx_w1, fx_nohack, fx_noseed, fx_fogx);
    }
    VpFix f = { fx_w1, fx_nohack, fx_noseed, fx_fogx };
    /* rsx_vp_set_fix_override: o modo de comparacao em metade da tela gera o
     * mesmo VP duas vezes, com e sem uma chave. */
    if (s_vp_fix_override >= 0) {
        f.w1 = (s_vp_fix_override >> 0) & 1; f.nohack = (s_vp_fix_override >> 1) & 1;
        f.noseed = (s_vp_fix_override >> 2) & 1; f.fogx = (s_vp_fix_override >> 3) & 1;
    }
    return f;
}

int rsx_vp_decompile_msl(const u8* ucode, u32 max_bytes, char* out, u32 out_size)
{
    static char hlsl[96 * 1024];
    static char body[96 * 1024];
    static char body2[96 * 1024];
    if (!ucode || !out || out_size < 256) return -1;

    int n = rsx_vp_decompile(ucode, max_bytes, hlsl, sizeof(hlsl));
    if (n < 0) return -1;

    if (vp_hlsl_body(hlsl, body, sizeof(body)) != 0) return -1;

    if (vp_msl_replace_all(body, "[unroll] ", "", body2, sizeof(body2)) != 0) return -1;
    if (vp_msl_replace_all(body2, "(float4)0", "float4(0)", body, sizeof(body)) != 0) return -1;
    if (vp_msl_replace_all(body, "frac(", "fract(", body2, sizeof(body2)) != 0) return -1;
    if (vp_msl_replace_all(body2, "(float4)((", "select(float4(0),float4(1),(",
                           body, sizeof(body)) != 0)
        return -1;

    const VpFix fx = vp_fix_resolve();
    int u_w1 = fx.w1, u_nohack = fx.nohack, u_noseed = fx.noseed, u_fogx = fx.fogx;
    u32 pos = 0;
    if (vp_msl_append(out, out_size, &pos,
        "#include <metal_stdlib>\n"
        "using namespace metal;\n"
        "struct VSIn {\n"
        "  float3 v0 [[attribute(0)]];\n"
        "  float4 v3 [[attribute(1)]];\n"
        "};\n"
        /* Field names match rsx_fp_decompile_msl PSIn for stage linking.
         * `invariant` = RPCS3's `invariant gl_Position` (RSXVertexPrologue.glsl):
         * the GoW2 Colossus is drawn twice over the same mesh by two different
         * VPs (07C9D580 writes depth, E05A608D tests LEQUAL without writing);
         * compiled apart, the same position math rounded differently and the
         * second pass failed on whole triangles (2026-09-21). */
        "struct VSOut {\n"
        "  float4 position [[position, invariant]];\n"
        "  float4 col0; float4 col1; float4 fog;\n"
        "  float4 tc0; float4 tc1; float4 tc2; float4 tc3;\n"
        "  float4 tc4; float4 tc5; float4 tc6; float4 tc7;\n"
        "};\n") != 0)
        return -1;
    /* E458 A/B gate (diagnostic, OFF = M6): PS3_METAL_VS_OLDPRE=1 emits the
     * pre-M6 signature (stage_in + vp_c only). */
    static int oldpre = -1;
    if (oldpre < 0) {
        const char* e = getenv("PS3_METAL_VS_OLDPRE");
        oldpre = (e && e[0] == '1');
    }
    if (oldpre) {
        if (vp_msl_append(out, out_size, &pos,
            "vertex VSOut vs_main(VSIn input [[stage_in]],\n"
            "    constant float4* vp_c [[buffer(1)]]) {\n"
            "    float4 v[16];\n"
            "    for (int _k=0;_k<16;_k++) v[_k]=float4(0);\n"
            "    v[0]=float4(input.v0,1); v[3]=input.v3;\n"
            "    v[8]=float4(input.v3.xy, 0.0, 1.0);\n") != 0)
            return -1;
    } else if (vp_msl_append(out, out_size, &pos,
        /* M6 slot map, matches MetalVAMap in rsx_metal_backend.m. */
        "struct VAMap { uint stride; uint base; uint vid_base; uint pad; int slot[16]; float4 cur[16]; };\n"
        "vertex VSOut vs_main(VSIn input [[stage_in]],\n"
        "    constant float4* vp_c [[buffer(1)]],\n"
        "    constant VAMap& vam [[buffer(2)]],\n"
        "    const device float4* va [[buffer(3)]],\n"
        "    uint vid [[vertex_id]]) {\n"
        "    float4 v[16];\n"
        "    for (int _k=0;_k<16;_k++) v[_k]=float4(0);\n"
        "    v[0]=float4(input.v0,1); v[3]=input.v3;\n") != 0)
        return -1;
    if (!u_noseed) {
        if (vp_msl_append(out, out_size, &pos,
            "    /* Host packs UV into attr1 (v3) when no diffuse: also seed v[8]\n"
            "     * so guest VPs that TEXCOORD from attribute 8 still sample. */\n"
            "    v[8]=float4(input.v3.xy, 0.0, 1.0);\n") != 0)
            return -1;
    } else {
        /* §3 (RPCS3): sem array ligado o atributo vem do registo VERTEX_DATA4F,
         * nunca do UV empacotado; v[3] deixa de ser o UV. */
        if (vp_msl_append(out, out_size, &pos,
            "    if (vam.stride == 0u) { for (int _a=1;_a<16;_a++) v[_a] = vam.cur[_a]; }\n") != 0)
            return -1;
    }
    if (vp_msl_append(out, out_size, &pos,
        "    /* M6: every enabled RSX attribute, decoded per vertex by the host;\n"
        "     * a disabled one reads the VERTEX_DATA4F register (vam.cur). */\n"
        "    if (vam.stride != 0u) {\n"
        "      uint _vb = vam.base + (vid - vam.vid_base) * vam.stride;\n"
        "      for (int _a=0;_a<16;_a++)\n"
        "        v[_a] = (vam.slot[_a] >= 0) ? va[_vb + uint(vam.slot[_a])] : vam.cur[_a];\n"
        "    }\n") != 0)
        return -1;
    if (vp_msl_append(out, out_size, &pos,
        "    float4 r[64]; float4 o[32];\n"
        "    int4 a0 = int4(0,0,0,0); int4 a1 = int4(0,0,0,0);\n"
        "    for (int _i=0;_i<64;_i++) r[_i]=float4(0);\n") != 0)
        return -1;
    /* §1: o RPCS3 inicializa as saidas em (0,0,0,1) -- alfa 1 e W 1. */
    if (vp_msl_append(out, out_size, &pos, u_w1
            ? "    for (int _j=0;_j<32;_j++) o[_j]=float4(0,0,0,1);\n"
            : "    for (int _j=0;_j<32;_j++) o[_j]=float4(0);\n") != 0)
        return -1;
    if (vp_msl_append(out, out_size, &pos, body) != 0) return -1;
    if (!u_nohack && vp_msl_append(out, out_size, &pos,
        "    /* If guest VP never wrote TEXCOORD0, forward packed UV (v[8]). */\n"
        "    if (all(o[7] == float4(0.0))) o[7] = v[8];\n") != 0)
        return -1;
    if (vp_msl_append(out, out_size, &pos, u_fogx
            ? "    VSOut Out;\n"
              "    Out.position = o[0]; Out.col0 = o[1]; Out.col1 = o[2]; Out.fog = o[5].xxxx;\n"
            : "    VSOut Out;\n"
              "    Out.position = o[0]; Out.col0 = o[1]; Out.col1 = o[2]; Out.fog = o[5];\n") != 0)
        return -1;
    if (vp_msl_append(out, out_size, &pos,
        "    Out.tc0=o[7]; Out.tc1=o[8]; Out.tc2=o[9]; Out.tc3=o[10];\n"
        "    Out.tc4=o[11]; Out.tc5=o[12]; Out.tc6=o[13]; Out.tc7=o[14];\n"
        "    return Out;\n"
        "}\n") != 0)
        return -1;
    return n;
}

/* ---- GLSL 450 emission (HLSL dialect -> Vulkan GLSL, via rsx_glsl_dialect) - */

/* Note: PS3_METAL_VS_OLDPRE (rsx_vp_decompile_msl above) is a Metal-only A/B
 * diagnostic for the pre-M6 vertex function signature; it has no GLSL
 * equivalent here -- the Vulkan path only ever emits the M6 (VAMap/ring)
 * binding layout. */
int rsx_vp_decompile_glsl(const u8* ucode, u32 max_bytes, char* out, u32 out_size)
{
    static char hlsl[96 * 1024];
    static char body_cut[96 * 1024];
    static char body[96 * 1024];
    if (!ucode || !out || out_size < 256) return -1;
    out[0] = '\0';

    int n = rsx_vp_decompile(ucode, max_bytes, hlsl, sizeof(hlsl));
    if (n < 0) return -1;
    if (vp_hlsl_body(hlsl, body_cut, sizeof(body_cut)) != 0) return -1;
    if (rsx_glsl_rewrite_body(body_cut, body, sizeof(body)) != 0) return -1;
    const VpFix fx = vp_fix_resolve();

    u32 pos = 0;
#define VP_PUT(s) do { if (vp_msl_append(out, out_size, &pos, (s)) != 0) goto overflow; } while (0)
    VP_PUT("#version 450\n");
    VP_PUT(RSX_GLSL_PRELUDE);
    VP_PUT("layout(location = 0) in vec3 in_v0;\n"
           "layout(location = 1) in vec4 in_v3;\n"
           "layout(location = 0) out vec4 v_col0;\n"
           "layout(location = 1) out vec4 v_col1;\n"
           "layout(location = 2) out vec4 v_fog;\n"
           "layout(location = 3) out vec4 v_tc0;\n"
           "layout(location = 4) out vec4 v_tc1;\n"
           "layout(location = 5) out vec4 v_tc2;\n"
           "layout(location = 6) out vec4 v_tc3;\n"
           "layout(location = 7) out vec4 v_tc4;\n"
           "layout(location = 8) out vec4 v_tc5;\n"
           "layout(location = 9) out vec4 v_tc6;\n"
           "layout(location = 10) out vec4 v_tc7;\n"
           /* RPCS3 `invariant gl_Position`: the Colossus is drawn twice over the
            * same mesh by two VPs; see rsx_vp_decompile_msl. */
           "invariant gl_Position;\n"
           "layout(std430, set = 0, binding = 3) readonly buffer RsxVpc { vec4 vp_c[]; };\n"
           /* std430 gives the byte layout of MetalVAMap (16 + 64 + 256 bytes). */
           "layout(std430, set = 0, binding = 4) readonly buffer RsxVam {\n"
           "  uint stride; uint base; uint vid_base; uint pad; int slot[16]; vec4 cur[16];\n"
           "} vam;\n"
           "layout(std430, set = 0, binding = 5) readonly buffer RsxVa { vec4 va[]; };\n"
           "void main() {\n"
           "    vec4 v[16];\n"
           "    for (int _k=0;_k<16;_k++) v[_k]=vec4(0);\n"
           "    v[0]=vec4(in_v0,1); v[3]=in_v3;\n");
    if (!fx.noseed)
        VP_PUT("    v[8]=vec4(in_v3.xy, 0.0, 1.0);\n");
    else
        VP_PUT("    if (vam.stride == 0u) { for (int _a=1;_a<16;_a++) v[_a] = vam.cur[_a]; }\n");
    VP_PUT("    if (vam.stride != 0u) {\n"
           "      uint _vb = vam.base + (uint(gl_VertexIndex) - vam.vid_base) * vam.stride;\n"
           "      for (int _a=0;_a<16;_a++)\n"
           "        v[_a] = (vam.slot[_a] >= 0) ? va[_vb + uint(vam.slot[_a])] : vam.cur[_a];\n"
           "    }\n"
           "    vec4 r[64]; vec4 o[32];\n"
           "    ivec4 a0 = ivec4(0,0,0,0); ivec4 a1 = ivec4(0,0,0,0);\n"
           "    for (int _i=0;_i<64;_i++) r[_i]=vec4(0);\n");
    VP_PUT(fx.w1 ? "    for (int _j=0;_j<32;_j++) o[_j]=float4(0,0,0,1);\n"
                 : "    for (int _j=0;_j<32;_j++) o[_j]=float4(0);\n");
    VP_PUT(body);
    if (!fx.nohack)
        VP_PUT("    if (all(equal(o[7], vec4(0.0)))) o[7] = v[8];\n");
    VP_PUT("    gl_Position = o[0]; v_col0 = o[1]; v_col1 = o[2];\n");
    VP_PUT(fx.fogx ? "    v_fog = o[5].xxxx;\n" : "    v_fog = o[5];\n");
    VP_PUT("    v_tc0=o[7]; v_tc1=o[8]; v_tc2=o[9]; v_tc3=o[10];\n"
           "    v_tc4=o[11]; v_tc5=o[12]; v_tc6=o[13]; v_tc7=o[14];\n"
           "}\n");
#undef VP_PUT
    return n;

overflow:
    out[0] = '\0';
    return -1;
}
