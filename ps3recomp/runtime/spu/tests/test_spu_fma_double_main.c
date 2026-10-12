/*
 * TDD: SPU double-precision FMA family (dfma/dfms/dfnms/dfnma) e
 * multiply-high-high-accumulate (mpyhha/mpyhhau). O lifter (tools/spu_lifter.py,
 * caso dfma/dfms/dfnms/dfnma + mpyhha/mpyhhau) EMITE chamadas para estes
 * helpers -> qualquer job SPU do GoW2 que use FMA de dupla precisao (fisica)
 * ou mpyhha depende deles. Este teste exercita SO os helpers de spu_helpers.h
 * -- puro C, sem programa SPU liftado nenhum. Alem da corretude basica, os
 * vetores fundidos abaixo discriminam o fix de precisao (finding #5): as ops
 * DP do Cell sao genuinamente FUSED (uma so' arredondagem), logo os helpers
 * usam a C99 fma() e NAO um mul-then-add que double-round-a. Vetores derivados
 * da SPU ISA (IBM Cell BE).
 *
 * Semantica (RT e' o acumulador c):
 *   dfma  rt = ra*rb + rt
 *   dfms  rt = ra*rb - rt
 *   dfnms rt = rt - ra*rb        (negative multiply-subtract)
 *   dfnma rt = -(ra*rb + rt)     (negative multiply-add)
 *   mpyhha  rt.wi += s16(ra.wi>>16) * s16(rb.wi>>16)
 *   mpyhhau rt.wi += u16(ra.wi>>16) * u16(rb.wi>>16)
 *
 * Build: gcc -std=c11 -I../.. test_spu_fma_double_main.c -o test_spu_fma_double
 */
#include "../spu_helpers.h"
#include <stdio.h>
#include <string.h>

/* Suppress compiler FP contraction in THIS test so the fused-single-rounding
 * assertions below are a genuine discriminator: without this, -O2 on arm64
 * would auto-contract a plain `x*y - c` into a hardware fma and mask the very
 * double-rounding that finding #5 is about. With contraction off, only the
 * helpers' explicit C99 fma() keeps the product residual -- a plain mul-then-sub
 * would double-round and fail. (fma() is a library call, unaffected.) */
#pragma STDC FP_CONTRACT OFF

static int g_pass = 0, g_fail = 0;
static const char* g_current = "(none)";
#define TEST(name) g_current = name

static u128 dpair(double a, double b) { u128 r; r._u64[0]=r._u64[1]=0; spu_set_df(&r,0,a); spu_set_df(&r,1,b); return r; }

static void expect_df(u128 got, double e0, double e1) {
    double g0 = spu_get_df(got, 0), g1 = spu_get_df(got, 1);
    if (g0 == e0 && g1 == e1) { g_pass++; return; }
    g_fail++;
    fprintf(stderr, "FAIL: %s  got {%g,%g} expected {%g,%g}\n", g_current, g0, g1, e0, e1);
}

static u128 words(uint32_t w0, uint32_t w1, uint32_t w2, uint32_t w3) {
    u128 r; r._u32[0]=w0; r._u32[1]=w1; r._u32[2]=w2; r._u32[3]=w3; return r;
}
static void expect_words(u128 got, uint32_t e0, uint32_t e1, uint32_t e2, uint32_t e3) {
    if (got._u32[0]==e0 && got._u32[1]==e1 && got._u32[2]==e2 && got._u32[3]==e3) { g_pass++; return; }
    g_fail++;
    fprintf(stderr, "FAIL: %s  got {%08X,%08X,%08X,%08X} expected {%08X,%08X,%08X,%08X}\n",
            g_current, got._u32[0],got._u32[1],got._u32[2],got._u32[3], e0,e1,e2,e3);
}

int main(void) {
    u128 a = dpair(2.0, 3.0), b = dpair(5.0, 7.0), c = dpair(1.0, 10.0);

    TEST("dfma  = ra*rb + rt");   expect_df(spu_dfma(a, b, c), 11.0, 31.0);
    TEST("dfms  = ra*rb - rt");   expect_df(spu_dfms(a, b, c),  9.0, 11.0);
    TEST("dfnms = rt - ra*rb");   expect_df(spu_dfnms(a, b, c), -9.0, -11.0);
    TEST("dfnma = -(ra*rb + rt)");expect_df(spu_dfnma(a, b, c), -11.0, -31.0);

    /* Genuine FUSED single-rounding (finding #5): the SPU DP ops keep the full
     * product before rounding once. With x=1+2^-27, y=1-2^-27 the exact product
     * is 1 - 2^-54, which does NOT fit in a 53-bit double: a plain `x*y` rounds
     * it to 1.0 first, so a naive `x*y - 1.0` would double-round to 0.0. The C99
     * fma() the helpers use preserves the residual: (1-2^-54) - 1 = -2^-54.
     * This vector FAILS the old plain-`a*b-c` implementation and PASSES fma(). */
    {
        double x = 1.0 + 0x1p-27, y = 1.0 - 0x1p-27;
        u128 fx = dpair(x, 2.0), fy = dpair(y, 4.0), one = dpair(1.0, 0.0);
        TEST("dfms fused: keeps product residual (single rounding)");
        expect_df(spu_dfms(fx, fy, one), -0x1p-54, 8.0);   /* slot1: 2*4-0=8 */
        /* dfnms = rt - ra*rb = 1 - (1-2^-54) = 2^-54 (fused); naive -> 0.0 */
        TEST("dfnms fused: keeps product residual (single rounding)");
        expect_df(spu_dfnms(fx, fy, one), 0x1p-54, -8.0);  /* slot1: 0 - 2*4 = -8 */
    }

    /* hi16 por word: a={3,-2,7,1} b={4,5,-1,2} rt={100,0,1000,0} */
    u128 ia = words(0x00030000, 0xFFFE0000, 0x00070000, 0x00010000);
    u128 ib = words(0x00040000, 0x00050000, 0xFFFF0000, 0x00020000);
    u128 ic = words(100, 0, 1000, 0);
    /* mpyhha (signed): {100+12, 0-10, 1000-7, 0+2} */
    TEST("mpyhha signed hi*hi + rt");
    expect_words(spu_mpyhha(ia, ib, ic), 112u, (uint32_t)-10, 993u, 2u);

    /* mpyhhau (unsigned): a_hi={3,65534,7,1} b_hi={4,5,65535,2} rt={100,0,0,0} */
    u128 ua = words(0x00030000, 0xFFFE0000, 0x00070000, 0x00010000);
    u128 ub = words(0x00040000, 0x00050000, 0xFFFF0000, 0x00020000);
    u128 uc = words(100, 0, 0, 0);
    /* {100+12, 65534*5=327670, 7*65535=458745, 1*2=2} */
    TEST("mpyhhau unsigned hi*hi + rt");
    expect_words(spu_mpyhhau(ua, ub, uc), 112u, 327670u, 458745u, 2u);

    fprintf(stderr, "\n%s: %d passed, %d failed\n", g_fail ? "RED" : "GREEN", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
