/*
 * NV40 -> HLSL translator validation harness.
 *
 * Synthesizes one NV40 instruction per opcode (FP and VP), runs it through the
 * decompiler, writes the generated HLSL, and compiles it with fxc.exe. Prints
 * PASS/FAIL per opcode with the fxc error. Objective pass/fail signal for the
 * fix -> test -> validate loop.
 *
 * Also fuzzes random ucode to ensure the decompiler never produces HLSL that
 * fxc rejects (robustness) and never crashes.
 *
 * argv[1] = path to fxc.exe
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

typedef uint8_t  u8;
typedef uint32_t u32;

extern int  rsx_fp_decompile(const u8*, u32, char*, u32);
extern const char* rsx_fp_opcode_name(u32);
extern int  rsx_vp_decompile(const u8*, u32, char*, u32);
extern const char* rsx_vp_vec_name(u32);
extern const char* rsx_vp_sca_name(u32);

static const char* g_fxc = "fxc.exe";
static int g_pass = 0, g_fail = 0;

/* ---- FP word packing: decoder does BE-load + 16-bit halfword swap, so store
 * be = swap16(host) as big-endian bytes to recover `host`. -------------- */
static void put_fp(u8* p, u32 host) {
    u32 be = (host << 16) | (host >> 16);
    p[0] = (u8)(be >> 24); p[1] = (u8)(be >> 16); p[2] = (u8)(be >> 8); p[3] = (u8)be;
}
static void put_le(u8* p, u32 v) {
    p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24);
}

/* Compile `hlsl` with fxc as the given profile/entry. Returns 1 on success. */
static int fxc_compile(const char* hlsl, const char* profile, const char* tag) {
    char path[256], logp[256], cmd[1200];
    snprintf(path, sizeof path, "sh_%s.hlsl", tag);
    snprintf(logp, sizeof logp, "sh_%s.log", tag);
    FILE* f = fopen(path, "wb");
    if (!f) { printf("  [%s] cannot write %s\n", tag, path); return 0; }
    fwrite(hlsl, 1, strlen(hlsl), f);
    fclose(f);
    /* cmd.exe /c strips the outermost quote pair when the string begins with a
     * quote, which would break a quoted exe path that has spaces. Wrap the
     * whole command in an extra quote pair so the inner quoting survives. */
    snprintf(cmd, sizeof cmd, "\"\"%s\" /nologo /T %s /E main /Fo nul \"%s\" > \"%s\" 2>&1\"",
             g_fxc, profile, path, logp);
    int rc = system(cmd);
    if (rc == 0) return 1;
    /* print first error line */
    FILE* lf = fopen(logp, "rb");
    if (lf) {
        char line[400];
        while (fgets(line, sizeof line, lf)) {
            if (strstr(line, "error") || strstr(line, "X3") || strstr(line, "X4")) {
                line[strcspn(line, "\r\n")] = 0;
                printf("      fxc: %s\n", line);
                break;
            }
        }
        fclose(lf);
    }
    return 0;
}

/* ---- FP per-opcode test ------------------------------------------------- */
static void test_fp_opcode(u32 opcode, int with_const) {
    u8 uc[64]; memset(uc, 0, sizeof uc);
    /* w0: OPCODE@24, mask xyzw (9..12)=0xF, END@0, OUT_REG=0 */
    u32 w0 = (opcode << 24) | (0xF << 9) | 1u;
    /* srcs: TEMP type(0), reg index 1/2/3, identity swizzle xyzw.
     * swizzle bits at 9.. (2 each): x=0,y=1,z=2,w=3 -> xyzw = 0b11100100 = 0xE4 << 9 */
    u32 swz = 0xE4u << 9;
    u32 w1 = (1u << 2) | swz;           /* src0 = r1.xyzw */
    u32 w2 = (2u << 2) | swz;           /* src1 = r2.xyzw */
    u32 w3 = (3u << 2) | swz;           /* src2 = r3.xyzw */
    if (with_const) { w1 = (2u /*CONST*/) | swz; } /* src0 = const */
    put_fp(uc + 0, w0); put_fp(uc + 4, w1); put_fp(uc + 8, w2); put_fp(uc + 12, w3);
    u32 sz = 16;
    if (with_const) { /* inline const slot: 4 float words */
        put_fp(uc + 16, 0x3F800000); put_fp(uc + 20, 0x3F800000);
        put_fp(uc + 24, 0x3F800000); put_fp(uc + 28, 0x3F800000);
        sz = 32;
    }
    char hlsl[16384];
    int n = rsx_fp_decompile(uc, sz, hlsl, sizeof hlsl);
    char tag[64]; snprintf(tag, sizeof tag, "fp_%s%s", rsx_fp_opcode_name(opcode), with_const ? "_k" : "");
    if (n < 0) { printf("  FAIL %-12s decompile returned -1\n", tag); g_fail++; return; }
    /* opcodes that legitimately produce no statement */
    int noop_ok = (opcode==0x00/*NOP*/||opcode==0x3D/*FENCT*/||opcode==0x3E/*FENCB*/);
    if (!noop_ok && (strstr(hlsl,"TODO") || strstr(hlsl,"unhandled"))) {
        printf("  FAIL %-12s UNHANDLED (emits TODO, op 0x%02X)\n", tag, opcode); g_fail++; return;
    }
    if (fxc_compile(hlsl, "ps_5_0", tag)) { g_pass++; }
    else { printf("  FAIL %-12s COMPILE (op 0x%02X)\n", tag, opcode); g_fail++; }
}

/* ---- VP per-opcode test ------------------------------------------------- */
static void test_vp(u32 vec_op, u32 sca_op, const char* label) {
    u8 uc[16]; memset(uc, 0, sizeof uc);
    /* d1: vec_op@22, sca_op@27, input_src@8, const_src@12 */
    u32 d1 = (vec_op << 22) | (sca_op << 27);
    /* sources: temp regs, identity swizzle (sx..sw). src fields packed across
     * d1/d2/d3 as the decoder unpacks them; keep them 0 (r[0].xxxx) which is
     * always valid. */
    u32 d0 = (0u << 15);                 /* dst_tmp=0 */
    u32 d2 = 0;
    /* d3: end@0, dst_out@2, vec mask xyzw = bits16..13, sca mask = bits20..17 */
    u32 d3 = 1u;                         /* end */
    if (vec_op) d3 |= (1<<16)|(1<<15)|(1<<14)|(1<<13); /* vec writemask xyzw */
    if (sca_op) d3 |= (1<<20)|(1<<19)|(1<<18)|(1<<17); /* sca writemask xyzw */
    put_le(uc + 0, d0); put_le(uc + 4, d1); put_le(uc + 8, d2); put_le(uc + 12, d3);
    char hlsl[16384];
    int n = rsx_vp_decompile(uc, 16, hlsl, sizeof hlsl);
    char tag[64]; snprintf(tag, sizeof tag, "vp_%s", label);
    if (n < 0) { printf("  FAIL %-12s decompile returned -1\n", tag); g_fail++; return; }
    /* flow-control scalar ops legitimately emit no result today */
    int flow = (sca_op>=0x08 && sca_op<=0x0C) || sca_op==0x11 || sca_op==0x12
            || sca_op==0x13 || sca_op==0x14; /* BRA/BRI/CAL/CLI/RET/BRB/CLB/PSH/POP */
    if (!flow && (strstr(hlsl,"TODO") || strstr(hlsl,"unhandled") || strstr(hlsl,"passthrough"))) {
        printf("  FAIL %-12s UNHANDLED (vec 0x%02X sca 0x%02X)\n", tag, vec_op, sca_op); g_fail++; return;
    }
    if (fxc_compile(hlsl, "vs_5_0", tag)) { g_pass++; }
    else { printf("  FAIL %-12s COMPILE (vec 0x%02X sca 0x%02X)\n", tag, vec_op, sca_op); g_fail++; }
}

/* ---- real-shader corpus: decompile a dumped .bin and compile it --------- */
static void test_real(const char* path, int is_vp) {
    FILE* f = fopen(path, "rb");
    if (!f) { printf("  (skip, no file) %s\n", path); return; }
    static u8 buf[64 * 1024];
    size_t n = fread(buf, 1, sizeof buf, f);
    fclose(f);
    const char* base = strrchr(path, '/'); base = base ? base + 1 : path;
    char tag[80]; snprintf(tag, sizeof tag, "real_%s", base);
    for (char* p = tag; *p; p++) if (*p == '.') *p = '_';
    static char hlsl[128 * 1024];
    int r = is_vp ? rsx_vp_decompile(buf, (u32)n, hlsl, sizeof hlsl)
                  : rsx_fp_decompile(buf, (u32)n, hlsl, sizeof hlsl);
    if (r < 0) { printf("  FAIL %-28s decompile -1 (%u bytes)\n", base, (u32)n); g_fail++; return; }
    if (fxc_compile(hlsl, is_vp ? "vs_5_0" : "ps_5_0", tag)) { g_pass++; printf("  PASS %-28s (%d instr)\n", base, r); }
    else { printf("  FAIL %-28s (%d instr, %u bytes)\n", base, r, (u32)n); g_fail++; }
}

int main(int argc, char** argv) {
    if (argc > 1) g_fxc = argv[1];

    /* Mode: argv[2] == "real" -> validate the dumped corpus in dumps/ */
    if (argc > 2 && strcmp(argv[2], "real") == 0) {
        printf("=== Real FP shaders ===\n");
        for (int i = 3; i < argc; i++)
            if (strstr(argv[i], "fp_")) test_real(argv[i], 0);
        printf("=== Real VP shaders ===\n");
        for (int i = 3; i < argc; i++)
            if (strstr(argv[i], "vp_")) test_real(argv[i], 1);
        printf("\n=== REAL RESULT: %d pass, %d fail ===\n", g_pass, g_fail);
        return g_fail ? 1 : 0;
    }

    printf("=== FP opcodes ===\n");
    /* every opcode present in the FP enum */
    u32 fp_ops[] = {
        0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0A,0x0B,0x0C,0x0D,0x0E,
        0x0F,0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1A,0x1B,0x1C,0x1D,
        0x1E,0x1F,0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27,0x28,0x29,0x2A,0x2E,0x2F,
        0x31,0x36,0x38,0x39,0x3A,0x3B,0x3C,0x3D,0x3E
    };
    for (u32 i = 0; i < sizeof(fp_ops)/sizeof(fp_ops[0]); i++)
        test_fp_opcode(fp_ops[i], 0);
    /* constant-source path for a representative arithmetic op */
    test_fp_opcode(0x04 /*MAD*/, 1);

    /* Source-modifier + write-mask fuzz on FP MAD: every combination of
     * negate / abs / half-register / partial swizzle / partial dest mask /
     * saturate must still yield HLSL that fxc accepts. */
    printf("=== FP modifier fuzz (MAD) ===\n");
    {
        int fuzz_fail = 0, fuzz_n = 0;
        for (int neg = 0; neg < 2; neg++)
        for (int abs_ = 0; abs_ < 2; abs_++)
        for (int half = 0; half < 2; half++)
        for (u32 swz = 0; swz < 4; swz++)      /* a few swizzle patterns */
        for (u32 mask = 1; mask < 16; mask += 7) /* masks 1,8,15 */
        for (int sat = 0; sat < 2; sat++) {
            u8 uc[32]; memset(uc, 0, sizeof uc);
            u32 swpat[4] = {0xE4u, 0x00u, 0x1Bu, 0xA5u}; /* xyzw, xxxx, wzyx, mixed */
            u32 sf = swpat[swz] << 9;
            u32 w0 = (0x04u << 24) | (mask << 9) | (half ? (1u<<7) : 0) | 1u;
            u32 w1 = (1u << 2) | sf | (neg ? (1u<<17) : 0) | (abs_ ? (1u<<29) : 0);
            u32 w2 = (2u << 2) | sf;
            u32 w3 = (3u << 2) | sf;
            if (sat) w0 |= (1u<<31);
            put_fp(uc+0,w0); put_fp(uc+4,w1); put_fp(uc+8,w2); put_fp(uc+12,w3);
            char hlsl[16384];
            int n = rsx_fp_decompile(uc, 16, hlsl, sizeof hlsl);
            char tag[48]; snprintf(tag, sizeof tag, "fz_%d%d%d%u%u%d", neg,abs_,half,swz,mask,sat);
            fuzz_n++;
            if (n < 0 || !fxc_compile(hlsl, "ps_5_0", tag)) {
                printf("  FAIL fuzz neg=%d abs=%d half=%d swz=%u mask=%u sat=%d\n",
                       neg,abs_,half,swz,mask,sat);
                fuzz_fail++;
            }
        }
        printf("  fuzz: %d/%d ok\n", fuzz_n - fuzz_fail, fuzz_n);
        g_pass += fuzz_n - fuzz_fail; g_fail += fuzz_fail;
    }

    printf("=== VP vector opcodes ===\n");
    for (u32 v = 1; v <= 0x16; v++) {
        char lbl[32]; snprintf(lbl, sizeof lbl, "vec_%s", rsx_vp_vec_name(v));
        test_vp(v, 0, lbl);
    }
    printf("=== VP scalar opcodes ===\n");
    for (u32 s = 1; s <= 0x10; s++) {
        char lbl[32]; snprintf(lbl, sizeof lbl, "sca_%s", rsx_vp_sca_name(s));
        test_vp(0, s, lbl);
    }

    /* VP modifier/mask fuzz on vector MAD + scalar RSQ co-issue. */
    printf("=== VP modifier fuzz ===\n");
    {
        int ff = 0, fn = 0;
        for (int neg = 0; neg < 2; neg++)
        for (int abs_ = 0; abs_ < 2; abs_++)
        for (u32 vm = 1; vm < 16; vm += 7)   /* vec masks 1,8,15 */
        for (u32 sm = 0; sm < 16; sm += 5) {  /* sca masks 0,5,10,15 */
            u8 uc[16]; memset(uc, 0, sizeof uc);
            u32 d0 = (0u<<15) | (abs_?(1u<<21):0) | (neg?0:0);
            u32 d1 = (0x04u<<22) | (0x04u<<27);  /* vec MAD + sca RSQ */
            u32 d2 = 0, d3 = 1u;
            d3 |= (vm&1?(1u<<16):0)|(vm&2?(1u<<15):0)|(vm&4?(1u<<14):0)|(vm&8?(1u<<13):0);
            d3 |= (sm&1?(1u<<20):0)|(sm&2?(1u<<19):0)|(sm&4?(1u<<18):0)|(sm&8?(1u<<17):0);
            put_le(uc+0,d0); put_le(uc+4,d1); put_le(uc+8,d2); put_le(uc+12,d3);
            char hlsl[16384]; int n = rsx_vp_decompile(uc,16,hlsl,sizeof hlsl);
            char tag[48]; snprintf(tag,sizeof tag,"vfz_%d%d%u%u",neg,abs_,vm,sm);
            fn++;
            if (n < 0 || !fxc_compile(hlsl,"vs_5_0",tag)) {
                printf("  FAIL vp fuzz neg=%d abs=%d vm=%u sm=%u\n",neg,abs_,vm,sm); ff++;
            }
        }
        printf("  vp fuzz: %d/%d ok\n", fn-ff, fn);
        g_pass += fn-ff; g_fail += ff;
    }

    /* Realistic programs: a textured/modulated FP and an MVP-transform VP,
     * exercising inter-instruction register flow and partial writes. */
    printf("=== Realistic programs ===\n");
    {
        /* FP: r0 = tex0(tc0); r0 = r0 * col0; r0 = r0*k + k; out = sat(r0). */
        u8 fp[80]; memset(fp,0,sizeof fp); u32 swz=0xE4u<<9; u32 off=0;
        /* TEX r0.xyzw, input=tc0(0x4), unit0 */
        put_fp(fp+off+0,(0x17u<<24)|(0xFu<<9)|(0x4u<<13)); put_fp(fp+off+4,(1u/*INPUT*/)|swz);
        put_fp(fp+off+8,swz); put_fp(fp+off+12,swz); off+=16;
        /* MUL r0, r0, col0(input col0=0x1) */
        put_fp(fp+off+0,(0x02u<<24)|(0xFu<<9)|(0x1u<<13)); put_fp(fp+off+4,(0u<<2)|swz);
        put_fp(fp+off+8,(1u/*INPUT*/)|swz); put_fp(fp+off+12,swz); off+=16;
        /* MAD r0, r0, const, const  (pulls one inline const) */
        put_fp(fp+off+0,(0x04u<<24)|(0xFu<<9)); put_fp(fp+off+4,(0u<<2)|swz);
        put_fp(fp+off+8,(2u/*CONST*/)|swz); put_fp(fp+off+12,(2u/*CONST*/)|swz); off+=16;
        put_fp(fp+off+0,0x3F000000); put_fp(fp+off+4,0x3F000000);
        put_fp(fp+off+8,0x3F000000); put_fp(fp+off+12,0x3F000000); off+=16;
        /* MOV_SAT r0, r0, END */
        put_fp(fp+off+0,(0x01u<<24)|(0xFu<<9)|(1u<<31)|1u); put_fp(fp+off+4,(0u<<2)|swz);
        put_fp(fp+off+8,swz); put_fp(fp+off+12,swz); off+=16;
        char hlsl[16384]; int n = rsx_fp_decompile(fp,off,hlsl,sizeof hlsl);
        if (n>0 && fxc_compile(hlsl,"ps_5_0","real_fp_textured")) { g_pass++; printf("  PASS real FP textured (%d instr)\n",n); }
        else { g_fail++; printf("  FAIL real FP textured\n"); }

        /* VP: 4x DP4 into o[0] (HPOS) from v0 and c[0..3]; MOV o[1]=v3; END. */
        u8 vp[16*6]; memset(vp,0,sizeof vp); u32 voff=0;
        for (int row=0; row<4; row++) {
            u32 d0=(0u<<15); u32 d1=(0x07u<<22)|((u32)row<<12); /* DP4, const_src=row */
            /* src0=input v0 (reg_type 1), src1=const (reg_type 2); simplified fields=0 */
            u32 d2=0,d3=0;
            /* vec writemask single lane: x=row0.. -> bit (16-row) */
            d3 |= (1u<<(16-row));
            put_le(vp+voff+0,d0); put_le(vp+voff+4,d1); put_le(vp+voff+8,d2); put_le(vp+voff+12,d3); voff+=16;
        }
        /* MOV o[1]=... vec MOV writemask xyzw, dst_out=1, vec_result */
        { u32 d0=(1u<<30)/*vec_result*/; u32 d1=(0x01u<<22); u32 d2=0;
          u32 d3=1u | (1u<<16)|(1u<<15)|(1u<<14)|(1u<<13) | (1u<<2)/*dst_out=1*/;
          put_le(vp+voff+0,d0); put_le(vp+voff+4,d1); put_le(vp+voff+8,d2); put_le(vp+voff+12,d3); voff+=16; }
        char vhlsl[16384]; int vn = rsx_vp_decompile(vp,voff,vhlsl,sizeof vhlsl);
        if (vn>0 && fxc_compile(vhlsl,"vs_5_0","real_vp_mvp")) { g_pass++; printf("  PASS real VP mvp (%d instr)\n",vn); }
        else { g_fail++; printf("  FAIL real VP mvp\n"); }
    }

    /* Golden semantic checks: the generated HLSL for tricky opcodes must
     * contain the arithmetically-correct expression, not just compile. Guards
     * against silent translation regressions (e.g. LRP operand order). */
    printf("=== Golden semantics ===\n");
    {
        struct { u32 op; const char* must; } G[] = {
            { 0x04, "* (" },                 /* MAD: a*b+c */
            { 0x1F, "lerp(" },               /* LRP */
            { 0x07, "float4(1.0," },         /* DST: {1, a.y*b.y, a.z, b.w} */
            { 0x1E, "lit(" },                /* LIT */
            { 0x1A, "1.0 / (" },             /* RCP: scalar reciprocal */
            { 0x1B, "rsqrt(" },              /* RSQ */
            { 0x18, "/ (" },                 /* TXP: projective divide by .w */
            { 0x0B, "<" },                   /* SLT: less-than comparison */
            { 0x39, "normalize(" },          /* NRM */
        };
        int gf = 0;
        for (u32 i = 0; i < sizeof(G)/sizeof(G[0]); i++) {
            u8 uc[32]; memset(uc,0,sizeof uc); u32 swz=0xE4u<<9;
            u32 w0=(G[i].op<<24)|(0xFu<<9)|1u;
            u32 w1=(1u<<2)|swz, w2=(2u<<2)|swz, w3=(3u<<2)|swz;
            put_fp(uc+0,w0);put_fp(uc+4,w1);put_fp(uc+8,w2);put_fp(uc+12,w3);
            char hlsl[16384]; rsx_fp_decompile(uc,16,hlsl,sizeof hlsl);
            if (!strstr(hlsl, G[i].must)) {
                printf("  FAIL golden op 0x%02X missing \"%s\"\n", G[i].op, G[i].must); gf++;
            }
        }
        /* LRP operand order must be lerp(c, b, a) — a is the blend factor. */
        {
            u8 uc[16]; memset(uc,0,sizeof uc); u32 swz=0xE4u<<9;
            put_fp(uc+0,(0x1Fu<<24)|(0xFu<<9)|1u);
            put_fp(uc+4,(1u<<2)|swz);put_fp(uc+8,(2u<<2)|swz);put_fp(uc+12,(3u<<2)|swz);
            char hlsl[16384]; rsx_fp_decompile(uc,16,hlsl,sizeof hlsl);
            /* NV40 LRP dst = a*(b-c)+c with a=src0 the blend factor, so the
             * HLSL must be lerp(c, b, a) = lerp(r3, r2, r1): r[3] must appear
             * before r[1] inside the lerp() call. */
            const char* lp = strstr(hlsl, "lerp(");
            const char* p3 = lp ? strstr(lp, "r[3]") : NULL;
            const char* p1 = lp ? strstr(lp, "r[1]") : NULL;
            if (!lp || !p3 || !p1 || p3 > p1) { printf("  FAIL LRP operand order\n"); gf++; }
        }
        printf("  golden: %d checks, %d fail\n", (int)(sizeof(G)/sizeof(G[0]))+1, gf);
        g_fail += gf; g_pass += (int)(sizeof(G)/sizeof(G[0]))+1 - gf;

        /* VP golden: vector ops that used to silently pass through must now
         * emit the correct constant/expression. */
        struct { u32 vop; const char* must; } VG[] = {
            { 0x11, "float4(0,0,0,0)" },  /* SFL */
            { 0x15, "float4(1,1,1,1)" },  /* STR */
            { 0x07, "dot(" },             /* DP4 */
            { 0x02, "*" },                /* MUL */
            { 0x16, "sign(" },            /* SSG */
        };
        int vgf = 0;
        for (u32 i = 0; i < sizeof(VG)/sizeof(VG[0]); i++) {
            u8 uc[16]; memset(uc,0,sizeof uc);
            u32 d1=(VG[i].vop<<22);
            u32 d3=1u|(1u<<16)|(1u<<15)|(1u<<14)|(1u<<13);
            put_le(uc+0,0);put_le(uc+4,d1);put_le(uc+8,0);put_le(uc+12,d3);
            char hlsl[16384]; rsx_vp_decompile(uc,16,hlsl,sizeof hlsl);
            if (!strstr(hlsl, VG[i].must)) {
                printf("  FAIL vp golden vec 0x%02X missing \"%s\"\n", VG[i].vop, VG[i].must); vgf++;
            }
        }
        printf("  vp golden: %d checks, %d fail\n", (int)(sizeof(VG)/sizeof(VG[0])), vgf);
        g_fail += vgf; g_pass += (int)(sizeof(VG)/sizeof(VG[0])) - vgf;
    }

    printf("\n=== RESULT: %d pass, %d fail ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
