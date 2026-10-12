/* test_spu_simd.c -- winport v34: x86 SIMD SPU helpers vs the scalar *_ref, bit for bit.
 *
 * Build against a ps3recomp tree patched by engine_fixes_v34_spu_simd.py (see run_tests.sh):
 *   clang -O2 -march=x86-64-v2 -I <ps3recomp>/runtime/spu -I <ps3recomp>/include test_spu_simd.c
 * Usage: test_spu_simd [N=1000000]      -> randomized + edge-case equivalence, exit 1 on any mismatch
 *        test_spu_simd bench [iters]    -> ns/call scalar ref vs SIMD for the hottest helpers
 * Run once more with PS3_SPU_FMA_ISA=1 to cover the (scalar-delegating) RTZ/FTZ float model.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <float.h>
#include <math.h>
#include <time.h>
#include "spu_context.h"
#include "spu_helpers.h"

#if !SPU_HELPERS_X86
#error "SPU_HELPERS_X86 is off: nothing to test (need x86-64 + SSE4.1 and WINPORT_SPU_SIMD != 0)"
#endif

static uint64_t g_s = 0x9E3779B97F4A7C15ull;
static inline uint64_t rnd64(void) { g_s ^= g_s >> 12; g_s ^= g_s << 25; g_s ^= g_s >> 27; return g_s * 0x2545F4914F6CDD1Dull; }
static inline uint32_t rnd32(void) { return (uint32_t)(rnd64() >> 32); }

static const uint32_t k_fspecial[] = {
    0x00000000u, 0x80000000u, 0x00000001u, 0x80000001u, 0x007FFFFFu, 0x807FFFFFu, 0x00400000u, /* +-0, denormals */
    0x00800000u, 0x80800000u, 0x7F7FFFFFu, 0xFF7FFFFFu, 0x7F800000u, 0xFF800000u,              /* min/max, inf */
    0x7FC00000u, 0xFFC00000u, 0x7F800001u, 0x7FA00000u, 0xFFFFFFFFu, 0x7FC12345u, 0xFF812345u, /* NaNs (q, s) */
    0x3F800000u, 0xBF800000u, 0x3F7FFFFFu, 0x3F800001u, 0x34000000u, 0x4B000000u, 0x4B800000u,
    0x7F000000u, 0x00FFFFFFu, 0x01000000u, 0x0C000000u, 0xB3800000u };
#define NFS (sizeof k_fspecial / sizeof k_fspecial[0])
static const uint32_t k_ispecial[] = { 0, 1, 2, 0x7F, 0x80, 0xFF, 0x7FFF, 0x8000, 0xFFFF, 0x10000, 0x7FFFFFFF,
                                       0x80000000u, 0x80000001u, 0xFFFFFFFFu, 0xFFFF8000u, 0x00008000u, 0x8000FFFFu, 0x7FFF8000u };
#define NIS (sizeof k_ispecial / sizeof k_ispecial[0])

static uint32_t rnd_word_int(void) {
    switch (rnd32() & 7) {
    case 0: return k_ispecial[rnd32() % NIS];
    case 1: return rnd32() & 0xFF;                  /* small */
    case 2: return (uint32_t)-(int32_t)(rnd32() & 0xFF);
    case 3: { uint32_t x = rnd32(); return x & 0x80808080u; }
    default: return rnd32();
    }
}
static uint32_t rnd_word_float(void) {
    uint32_t r = rnd32();
    switch (r & 7) {
    case 0: case 1: return k_fspecial[rnd32() % NFS];
    case 2: return rnd32() & 0x807FFFFFu;            /* denormal or zero */
    case 3: { uint32_t e = 0x7F + (int)(rnd32() % 20) - 10; return (rnd32() & 0x807FFFFFu) | (e << 23); } /* near 1 */
    default: return rnd32();
    }
}
static u128 rnd_int(void)   { u128 r; for (int i = 0; i < 4; i++) r._u32[i] = rnd_word_int(); return r; }
static u128 rnd_float(void) { u128 r; for (int i = 0; i < 4; i++) r._u32[i] = rnd_word_float(); return r; }
/* shufb selectors: plenty of 0x80/0xC0/0xE0-class bytes (every byte value occurs) */
static u128 rnd_sel(void) {
    u128 r;
    for (int i = 0; i < 16; i++) {
        uint8_t v = (uint8_t)rnd32();
        switch (rnd32() & 3) { case 0: v &= 0x1F; break; case 1: v = (uint8_t)(0x80 | (v & 0x7F)); break; default: break; }
        r._u8[i] = v;
    }
    return r;
}
static int32_t rnd_imm(void) {
    switch (rnd32() & 3) {
    case 0: return (int32_t)k_ispecial[rnd32() % NIS];
    case 1: return (int32_t)(rnd32() % 129) - 64;
    case 2: return (int32_t)((int16_t)rnd32());   /* sign-extended 16 like i10/i16 fields */
    default: return (int32_t)rnd32();
    }
}

static long g_total, g_bad;
typedef struct { const char* name; long n, bad, nanpick; } stat_t;
static stat_t g_st[128]; static int g_nst;
static stat_t* st_get(const char* nm) {
    for (int i = 0; i < g_nst; i++) if (!strcmp(g_st[i].name, nm)) return &g_st[i];
    g_st[g_nst].name = nm; return &g_st[g_nst++];
}
static void dump(const char* t, u128 x) { fprintf(stderr, " %s=%08X_%08X_%08X_%08X", t, x._u32[0], x._u32[1], x._u32[2], x._u32[3]); }
/* Float ops only: the one tolerated difference is WHICH NaN comes out when two or more operands of a lane are
 * NaN. C leaves that unspecified, x86 returns the first source operand's NaN, and clang commutes fadd/fmul
 * operands freely -- the scalar reference itself returns a's NaN at one -O level and b's at another (see
 * H4_spu_simd.md). Any other lane difference (one NaN input, or a non-NaN result) is a hard mismatch. */
static long g_nanpick;
static int g_float_op;   /* 1 = fa/fs/fm, 2 = fma/fms/fnms */
static int is_nan32(uint32_t w) { return (w & 0x7F800000u) == 0x7F800000u && (w & 0x007FFFFFu); }
static int nan_choice_only(u128 got, u128 ref, u128 a, u128 b, u128 c) {
    for (int l = 0; l < 4; l++) {
        if (got._u32[l] == ref._u32[l]) continue;
        int nn = is_nan32(a._u32[l]) + is_nan32(b._u32[l]) + is_nan32(c._u32[l]);
        /* fma/fms/fnms: an invalid product (0 * inf) is itself a NaN source competing with a NaN c */
        if (g_float_op == 2 && !is_nan32(a._u32[l]) && !is_nan32(b._u32[l]) && isnan((double)a._f32[l] * (double)b._f32[l])) nn++;
        if (!(is_nan32(got._u32[l]) && is_nan32(ref._u32[l]) && nn >= 2)) return 0;
    }
    return 1;
}
static void check(const char* nm, u128 got, u128 ref, u128 a, u128 b, u128 c, long imm) {
    stat_t* s = st_get(nm); s->n++; g_total++;
    if (memcmp(&got, &ref, 16) && g_float_op && nan_choice_only(got, ref, a, b, c)) { g_nanpick++; if (s->nanpick++ < 2 && getenv("SHOWNAN")) { fprintf(stderr, "NANPICK %s", nm); dump("a", a); dump("b", b); dump("c", c); dump("simd", got); dump("ref", ref); fputc(10, stderr); } return; }
    if (memcmp(&got, &ref, 16)) {
        if (s->bad++ < 3) {
            fprintf(stderr, "MISMATCH %s imm=%ld", nm, imm); dump("a", a); dump("b", b); dump("c", c);
            dump("simd", got); dump("ref", ref); fputc('\n', stderr);
        }
        g_bad++;
    }
}
/* Float helpers are compared through noinline wrappers (helper body inlined into each wrapper with the same
 * flags). Without this, clang optimises ACROSS the back-to-back fms/fnms calls of the test loop (e.g. reuses
 * one product, or folds c - p into -(p - c)), which can flip the sign of a propagated NaN in either the SIMD or
 * the scalar copy -- an artefact of the test harness, not of the helper. In isolation both compile to the
 * same mulps/subps (or vfmadd at -v3). */
#define FW2(op) __attribute__((noinline)) static u128 w_##op(u128 a, u128 b) { return spu_##op(a, b); } \
                __attribute__((noinline)) static u128 w_##op##_ref(u128 a, u128 b) { return spu_##op##_ref(a, b); }
#define FW3(op) __attribute__((noinline)) static u128 w_##op(u128 a, u128 b, u128 c) { return spu_##op(a, b, c); } \
                __attribute__((noinline)) static u128 w_##op##_ref(u128 a, u128 b, u128 c) { return spu_##op##_ref(a, b, c); }
FW2(fa) FW2(fs) FW2(fm) FW3(fma) FW3(fms) FW3(fnms)
#define F2(op, A, B)     do { u128 a_ = (A), b_ = (B); check(#op, w_##op(a_, b_), w_##op##_ref(a_, b_), a_, b_, spu_zero(), 0); } while (0)
#define F3(op, A, B, C)  do { u128 a_ = (A), b_ = (B), c_ = (C); check(#op, w_##op(a_, b_, c_), w_##op##_ref(a_, b_, c_), a_, b_, c_, 0); } while (0)
#define T2(op, A, B)     do { u128 a_ = (A), b_ = (B); check(#op, spu_##op(a_, b_), spu_##op##_ref(a_, b_), a_, b_, spu_zero(), 0); } while (0)
#define T3(op, A, B, C)  do { u128 a_ = (A), b_ = (B), c_ = (C); check(#op, spu_##op(a_, b_, c_), spu_##op##_ref(a_, b_, c_), a_, b_, c_, 0); } while (0)
#define T1(op, A)        do { u128 a_ = (A); check(#op, spu_##op(a_), spu_##op##_ref(a_), a_, spu_zero(), spu_zero(), 0); } while (0)
#define TI(op, A, I)     do { u128 a_ = (A); long i_ = (I); check(#op, spu_##op(a_, (int32_t)i_), spu_##op##_ref(a_, (int32_t)i_), a_, spu_zero(), spu_zero(), i_); } while (0)
#define TIONLY(op, I)    do { long i_ = (I); check(#op, spu_##op((int32_t)i_), spu_##op##_ref((int32_t)i_), spu_zero(), spu_zero(), spu_zero(), i_); } while (0)

/* scalar LS reference (the pre-v34 byte loops of spu_context.h) */
static u128 ls_read_ref(const uint8_t* p) {
    u128 v; for (int i = 0; i < 4; i++)
        v._u32[i] = ((uint32_t)p[i*4] << 24) | ((uint32_t)p[i*4+1] << 16) | ((uint32_t)p[i*4+2] << 8) | (uint32_t)p[i*4+3];
    return v;
}

static spu_context* g_ctx;

static void one_round(long it) {
    const u128 a = rnd_int(), b = rnd_int(), c = rnd_int();
    const u128 fa = rnd_float(), fb = rnd_float(), fc = rnd_float();
    const int32_t imm = rnd_imm();
    /* constructors */
    { uint32_t v = rnd_word_int(); check("splat_u32", spu_splat_u32(v), spu_splat_u32_ref(v), a, b, c, v);
      check("splat_u16", spu_splat_u16((uint16_t)v), spu_splat_u16_ref((uint16_t)v), a, b, c, v);
      check("splat_u8", spu_splat_u8((uint8_t)v), spu_splat_u8_ref((uint8_t)v), a, b, c, v);
      check("iohl", spu_iohl(a, (uint16_t)v), spu_iohl_ref(a, (uint16_t)v), a, b, c, v); }
    /* quadword byte movers: shl/shr defined for sh >= 0 (callers pass 0..31), rot for any int */
    { int sh = (int)(it % 48); if (rnd32() & 1) sh = (int)(rnd32() % 32);
      check("qw_shl_bytes", spu_qw_shl_bytes(a, sh), spu_qw_shl_bytes_ref(a, sh), a, b, c, sh);
      check("qw_shr_bytes", spu_qw_shr_bytes(a, sh), spu_qw_shr_bytes_ref(a, sh), a, b, c, sh);
      int shr = (rnd32() & 1) ? sh : (int)rnd32();
      check("qw_rot_bytes", spu_qw_rot_bytes(a, shr), spu_qw_rot_bytes_ref(a, shr), a, b, c, shr); }
    /* public rotqby & co. (route through the movers) vs the movers' refs */
    check("rotqby", spu_rotqby(a, b), spu_qw_rot_bytes_ref(a, b._u32[0] & 0x0F), a, b, c, 0);
    check("shlqby", spu_shlqby(a, b), spu_qw_shl_bytes_ref(a, b._u32[0] & 0x1F), a, b, c, 0);
    check("rotqmby", spu_rotqmby(a, b), spu_qw_shr_bytes_ref(a, (0 - (int)b._u32[0]) & 0x1F), a, b, c, 0);
    check("rotqbybi", spu_rotqbybi(a, b), spu_qw_rot_bytes_ref(a, (b._u32[0] >> 3) & 0x0F), a, b, c, 0);
    check("rotqmbyi", spu_rotqmbyi(a, imm), spu_qw_shr_bytes_ref(a, (0 - imm) & 0x1F), a, b, c, imm);
    check("shlqbyi", spu_shlqbyi(a, imm), spu_qw_shl_bytes_ref(a, imm & 0x1F), a, b, c, imm);
    check("rotqbyi", spu_rotqbyi(a, imm), spu_qw_rot_bytes_ref(a, imm & 0x0F), a, b, c, imm);
    /* integer */
    T2(a, a, b); T2(sf, a, b); T2(ah, a, b); T2(sfh, a, b);
    TI(ai, a, imm); TI(ahi, a, imm); TI(sfi, a, imm); TI(sfhi, a, imm);
    T2(mpy, a, b); T3(mpya, a, b, c); T2(mpyu, a, b); TI(mpyi, a, imm); T2(mpyh, a, b); T2(mpyhh, a, b); TI(mpyui, a, imm);
    T2(and, a, b); T2(or, a, b); T2(xor, a, b); T2(nand, a, b); T2(nor, a, b); T2(andc, a, b); T2(orc, a, b);
    TI(andi, a, imm); TI(ori, a, imm); TI(xori, a, imm);
    T1(cntb, a); T3(selb, a, b, c);
    /* compares: make equality likely in some lanes */
    { u128 b2 = b; for (int i = 0; i < 16; i++) if (rnd32() & 1) b2._u8[i] = a._u8[i];
      T2(ceq, a, b2); T2(ceqh, a, b2); T2(ceqb, a, b2); T2(cgt, a, b2); T2(cgth, a, b2); T2(cgtb, a, b2);
      T2(clgt, a, b2); T2(clgth, a, b2); T2(clgtb, a, b2);
      int32_t im2 = (rnd32() & 1) ? imm : (int32_t)a._u32[rnd32() & 3];
      TI(ceqi, a, im2); TI(cgti, a, im2); TI(clgti, a, im2);
      int32_t imb = (rnd32() & 1) ? imm : (int32_t)a._u8[rnd32() & 15], imh = (rnd32() & 1) ? imm : (int32_t)a._s16[rnd32() & 7];
      TI(ceqbi, a, imb); TI(cgtbi, a, imb); TI(clgtbi, a, imb); TI(ceqhi, a, imh); TI(cgthi, a, imh); TI(clgthi, a, imh); }
    /* shufb with special selectors */
    T3(shufb, a, b, rnd_sel());
    /* immediate shifts: full count range (masked inside) */
    { int sh = (int)(it % 128) - 64; if (rnd32() & 1) sh = imm;
      TI(shli, a, sh); TI(shlhi, a, sh); TI(roti, a, sh); TI(rothi, a, sh); TI(rotmi, a, sh); TI(rotmai, a, sh);
      TI(rotmhi, a, sh); TI(rothmi, a, sh); TI(rotmahi, a, sh); }
    { u128 cnt = b; if (rnd32() & 1) for (int i = 0; i < 4; i++) cnt._u32[i] = (uint32_t)((int)(rnd32() % 128) - 64);
      T2(shl, a, cnt); T2(rot, a, cnt); T2(rotm, a, cnt); T2(rotma, a, cnt); }
    T1(xsbh, a); T1(xshw, a); T1(fsm, a); T1(fsmb, a); TIONLY(fsmbi, imm);
    { int pos = (rnd32() & 1) ? (int)(it % 64) - 32 : (int)rnd32();
      check("cbd_pos", spu_cbd_pos(pos), spu_cbd_pos_ref(pos), a, b, c, pos);
      check("chd_pos", spu_chd_pos(pos), spu_chd_pos_ref(pos), a, b, c, pos);
      check("cwd_pos", spu_cwd_pos(pos), spu_cwd_pos_ref(pos), a, b, c, pos);
      check("cdd_pos", spu_cdd_pos(pos), spu_cdd_pos_ref(pos), a, b, c, pos);
      check("cwd", spu_cwd(a, imm), spu_cwd_pos_ref((int)a._u32[0] + imm), a, b, c, imm); }
    /* floats (special-heavy operands) */
    g_float_op = 1;
    F2(fa, fa, fb); F2(fs, fa, fb); F2(fm, fa, fb); g_float_op = 2; F3(fma, fa, fb, fc); F3(fms, fa, fb, fc); F3(fnms, fa, fb, fc);
    g_float_op = 0;
    { u128 fb2 = fb; for (int i = 0; i < 4; i++) if (rnd32() & 1) fb2._u32[i] = fa._u32[i] ^ ((rnd32() & 3) == 0 ? 0x80000000u : 0);
      T2(fceq, fa, fb2); T2(fcgt, fa, fb2); }
    /* LS quadword load/store vs the byte loops */
    { uint32_t lsa = rnd32() & SPU_LS_MASK; u128 v = a;
      spu_ls_write128(g_ctx, lsa, v);
      const uint8_t* p = &g_ctx->ls[lsa & SPU_LS_MASK & ~0xFu];
      uint8_t want[16]; for (int i = 0; i < 4; i++) { want[4*i] = (uint8_t)(v._u32[i] >> 24); want[4*i+1] = (uint8_t)(v._u32[i] >> 16);
                                                     want[4*i+2] = (uint8_t)(v._u32[i] >> 8); want[4*i+3] = (uint8_t)v._u32[i]; }
      u128 gw, rw; memcpy(&gw, p, 16); memcpy(&rw, want, 16);
      check("ls_write128", gw, rw, a, b, c, lsa);
      memcpy(&g_ctx->ls[lsa & SPU_LS_MASK & ~0xFu], &b, 16);
      check("ls_read128", spu_ls_read128(g_ctx, lsa), ls_read_ref(p), a, b, c, lsa); }
}

/* exhaustive-ish edges */
static void edges(void) {
    u128 a, b, c;
    for (int i = 0; i < 16; i++) a._u8[i] = (uint8_t)(0x11 * i + 3);
    for (int i = 0; i < 16; i++) b._u8[i] = (uint8_t)(0xA0 + i);
    /* every selector byte value in every position */
    for (int v = 0; v < 256; v++) for (int pos = 0; pos < 16; pos++) {
        for (int i = 0; i < 16; i++) c._u8[i] = (uint8_t)(i * 7);
        c._u8[pos] = (uint8_t)v;
        T3(shufb, a, b, c);
        for (int i = 0; i < 16; i++) c._u8[i] = (uint8_t)v;
        T3(shufb, a, b, c);
    }
    for (int sh = -64; sh < 64; sh++) {
        if (sh >= 0) { check("qw_shl_bytes", spu_qw_shl_bytes(a, sh), spu_qw_shl_bytes_ref(a, sh), a, b, b, sh);
                       check("qw_shr_bytes", spu_qw_shr_bytes(a, sh), spu_qw_shr_bytes_ref(a, sh), a, b, b, sh); }
        check("qw_rot_bytes", spu_qw_rot_bytes(a, sh), spu_qw_rot_bytes_ref(a, sh), a, b, b, sh);
        TI(shli, a, sh); TI(shlhi, a, sh); TI(roti, a, sh); TI(rothi, a, sh); TI(rotmi, a, sh); TI(rotmai, b, sh);
        TI(rotmhi, a, sh); TI(rothmi, a, sh); TI(rotmahi, b, sh);
        check("cbd_pos", spu_cbd_pos(sh), spu_cbd_pos_ref(sh), a, b, b, sh);
        check("chd_pos", spu_chd_pos(sh), spu_chd_pos_ref(sh), a, b, b, sh);
        check("cwd_pos", spu_cwd_pos(sh), spu_cwd_pos_ref(sh), a, b, b, sh);
        check("cdd_pos", spu_cdd_pos(sh), spu_cdd_pos_ref(sh), a, b, b, sh);
    }
    for (int v = 0; v < 0x10000; v++) TIONLY(fsmbi, v);
    /* all pairs of special floats in every lane */
    for (unsigned i = 0; i < NFS; i++) for (unsigned j = 0; j < NFS; j++) for (unsigned k = 0; k < NFS; k += 3) {
        u128 x, y, z;
        for (int l = 0; l < 4; l++) { x._u32[l] = k_fspecial[(i + l) % NFS]; y._u32[l] = k_fspecial[(j + 3*l) % NFS]; z._u32[l] = k_fspecial[(k + l) % NFS]; }
        g_float_op = 1;
        F2(fa, x, y); F2(fs, x, y); F2(fm, x, y); g_float_op = 2; F3(fma, x, y, z); F3(fms, x, y, z); F3(fnms, x, y, z);
        g_float_op = 0;
        T2(fceq, x, y); T2(fcgt, x, y);
    }
    /* special ints, every pair */
    for (unsigned i = 0; i < NIS; i++) for (unsigned j = 0; j < NIS; j++) {
        u128 x, y; for (int l = 0; l < 4; l++) { x._u32[l] = k_ispecial[(i + l) % NIS]; y._u32[l] = k_ispecial[(j + 5*l) % NIS]; }
        T2(mpy, x, y); T2(mpyu, x, y); T2(mpyh, x, y); T2(mpyhh, x, y); T3(mpya, x, y, x);
        T2(clgt, x, y); T2(cgt, x, y); T2(clgth, x, y); T2(clgtb, x, y); T2(cgth, x, y); T2(cgtb, x, y);
        TI(mpyi, x, (int32_t)k_ispecial[j]); TI(mpyui, x, (int32_t)k_ispecial[j]); TI(clgti, x, (int32_t)k_ispecial[j]);
        TI(cgti, x, (int32_t)k_ispecial[j]); TI(clgtbi, x, (int32_t)k_ispecial[j]); TI(clgthi, x, (int32_t)k_ispecial[j]);
        T2(shl, x, y); T2(rot, x, y); T2(rotm, x, y); T2(rotma, x, y);
    }
}

/* ---------------- micro-benchmark ---------------- */
#define NB 4096
static u128 g_in[NB + 4];
static double now_ns(void) { struct timespec ts; timespec_get(&ts, TIME_UTC); return ts.tv_sec * 1e9 + ts.tv_nsec; }
#define BENCH3(nm, simd_expr, ref_expr) do { \
    double best[2] = {1e30, 1e30}; volatile uint32_t sink = 0; \
    for (int rep = 0; rep < 5; rep++) for (int w = 0; w < 2; w++) { \
        u128 acc = spu_zero(); double t0 = now_ns(); \
        for (long k = 0; k < iters; k++) { const u128 x = g_in[k & (NB-1)], y = g_in[(k+1) & (NB-1)], z = g_in[(k+2) & (NB-1)]; \
            u128 r = w ? (simd_expr) : (ref_expr); acc._u64[0] ^= r._u64[0]; acc._u64[1] += r._u64[1]; } \
        double t = (now_ns() - t0) / iters; if (t < best[w]) best[w] = t; sink ^= (uint32_t)acc._u64[0] ^ (uint32_t)acc._u64[1]; } \
    printf("  %-14s ref %6.2f ns  simd %6.2f ns  x%.1f\n", nm, best[0], best[1], best[0] / best[1]); (void)sink; } while (0)

static void bench(long iters) {
    for (int i = 0; i < NB + 4; i++) g_in[i] = (i & 1) ? rnd_float() : rnd_int();
    for (int i = 0; i < NB; i += 3) g_in[i] = rnd_sel();
    printf("micro-bench, %ld calls each, best of 5 (ns/call incl. loop overhead):\n", iters);
    BENCH3("shufb", spu_shufb(x, y, z), spu_shufb_ref(x, y, z));
    BENCH3("rotqby", spu_rotqby(x, y), spu_qw_rot_bytes_ref(x, y._u32[0] & 15));
    BENCH3("shlqbyi", spu_qw_shl_bytes(x, (int)(z._u32[0] & 31)), spu_qw_shl_bytes_ref(x, (int)(z._u32[0] & 31)));
    BENCH3("rotqmby", spu_qw_shr_bytes(x, (int)(z._u32[0] & 31)), spu_qw_shr_bytes_ref(x, (int)(z._u32[0] & 31)));
    BENCH3("ls_read128", spu_ls_read128(g_ctx, (uint32_t)(k * 16)), ls_read_ref(&g_ctx->ls[(k * 16) & SPU_LS_MASK]));
    BENCH3("ai", spu_ai(x, (int32_t)k), spu_ai_ref(x, (int32_t)k));
    BENCH3("selb", spu_selb(x, y, z), spu_selb_ref(x, y, z));
    BENCH3("cwd_pos", spu_cwd_pos((int)k), spu_cwd_pos_ref((int)k));
    BENCH3("fma", spu_fma(x, y, z), spu_fma_ref(x, y, z));
    BENCH3("fm", spu_fm(x, y), spu_fm_ref(x, y));
    BENCH3("clgt", spu_clgt(x, y), spu_clgt_ref(x, y));
    BENCH3("cntb", spu_cntb(x), spu_cntb_ref(x));
    BENCH3("mpy", spu_mpy(x, y), spu_mpy_ref(x, y));
    BENCH3("fsmbi", spu_fsmbi((int32_t)k), spu_fsmbi_ref((int32_t)k));
}

int main(int argc, char** argv) {
    g_ctx = (spu_context*)calloc(1, sizeof(spu_context));
    if (argc > 1 && !strcmp(argv[1], "bench")) { bench(argc > 2 ? atol(argv[2]) : 20000000L); return 0; }
    long n = argc > 1 ? atol(argv[1]) : 1000000L;
    const char* isa = getenv("PS3_SPU_FMA_ISA");
    printf("test_spu_simd: N=%ld rounds, AVX2=%d, FMA=%d, PS3_SPU_FMA_ISA=%s\n", n,
#ifdef __AVX2__
           1,
#else
           0,
#endif
#ifdef __FMA__
           1,
#else
           0,
#endif
           isa ? isa : "(unset)");
    edges();
    for (long it = 0; it < n; it++) one_round(it);
    for (int i = 0; i < g_nst; i++)
        printf("  %-13s %9ld checks  %s%s\n", g_st[i].name, g_st[i].n, g_st[i].bad ? "MISMATCH" : "ok",
               g_st[i].nanpick ? "  (+ multi-NaN-input lanes that picked the other NaN, tolerated)" : "");
    printf("TOTAL %ld checks, %ld mismatches across %d helpers (%ld multi-NaN pick differences tolerated) -> %s\n",
           g_total, g_bad, g_nst, g_nanpick, g_bad ? "FAIL" : "ALL MATCH");
    return g_bad ? 1 : 0;
}
