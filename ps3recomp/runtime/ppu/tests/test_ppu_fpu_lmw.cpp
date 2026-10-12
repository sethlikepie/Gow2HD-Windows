/*
 * ps3recomp - PPU FMA family + lmw/stmw runtime-helper unit test (self-contained).
 *
 * Hand-written (NOT derived from the lifter). Exercises the runtime helpers in
 * runtime/ppu/ppu_ops.h directly, in isolation -- no ELF, no lifted game code,
 * no boot required. This proves the REFERENCE helpers are correct; the
 * companion golden test runtime/ppu/tests/test_ppu_lifter_emit.py then proves
 * the lifter's own emitted C matches these helpers bit-for-bit (so a green run
 * here can't mask a drifted lifter emission). Covers two of the six areas from
 * the PPU lifter audit:
 *
 *   1. lmw/stmw (ppu_lmw/ppu_stmw): asserts the big-endian word order and the
 *      "always runs through r31" register range (Book I 3.3.11/3.3.13).
 *   2. fmadd/fmsub/fnmadd/fnmsub family (ppu_fmadd..ppu_fnmsubs): asserts true
 *      IEEE-754 single-rounding fused semantics, distinguishing it from the
 *      naive two-op `a*c+b` (which double-rounds). Uses the classic Dekker
 *      splitting constant 2^27+1, whose square is not exactly representable
 *      as a double, so `a*c` alone already loses the low bit -- adding back
 *      the negated (already-rounded) product then differs from what a truly
 *      fused multiply-add would produce. The naive arm uses `volatile`
 *      intermediates to defeat compiler FP-contraction (clang/gcc may
 *      auto-fuse a bare `a*c+b` at -O2, which would otherwise hide the very
 *      divergence this test exists to demonstrate); MSVC's default
 *      `/fp:precise` never contracts, so this divergence is the real,
 *      observable behavior of the naive formula the lifter used to emit.
 *
 * Build (from this directory; -ffp-contract=off is required -- see below):
 *   c++ -std=c++17 -O0 -ffp-contract=off test_ppu_fpu_lmw.cpp -o test_ppu_fpu_lmw -lm
 * Run:
 *   ./test_ppu_fpu_lmw
 */

/* Forces the "naive" a*c+b arm below to actually perform two separate
 * roundings. Without this, clang/gcc are standards-permitted to silently
 * contract a bare `a*c+b` into one hardware fused-multiply-add at -O1+ (and
 * even at -O0 for a fresh, non-volatile expression) -- which would make the
 * naive arm produce the SAME answer as ppu_fmadd() and hide the very
 * divergence this test exists to demonstrate. The pragma alone is not
 * honored by every toolchain, so the build line above also passes
 * -ffp-contract=off as a belt-and-suspenders match. */
#pragma STDC FP_CONTRACT OFF

#include "../ppu_context.h"
#include "../ppu_memory.h"
#include "../ppu_ops.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

/* ppu_memory.h declares `extern uint8_t* vm_base` -- this test provides the
 * backing storage (a small flat buffer standing in for the guest address
 * space) instead of a real VM manager. */
static uint8_t g_guest_mem[256];
extern "C" uint8_t* vm_base = g_guest_mem;

/* ppu_memory.h guards every vm_read/write with a commit check; this flat
 * backing buffer is fully addressable, so the whole range reads as committed. */
extern "C" int ppu_guest_range_committed(uint32_t /*addr*/, uint32_t /*n*/) { return 1; }

static int fails = 0;
#define CHECK(c, msg) do { if (c) printf("[PASS] %s\n", msg); \
    else { printf("[FAIL] %s\n", msg); fails++; } } while (0)

/* ------------------------------------------------------------------------
 * lmw / stmw
 * --------------------------------------------------------------------- */
static void test_lmw_stmw()
{
    printf("-- lmw/stmw --\n");
    memset(g_guest_mem, 0, sizeof(g_guest_mem));

    /* stmw r29,0x10(r1): registers r29,r30,r31 (3 words) store to EA=0x10.. */
    ppu_context ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.gpr[29] = 0x1122334455667788ULL;
    ctx.gpr[30] = 0x00000000CAFEBABEULL;
    ctx.gpr[31] = 0x0000000000000001ULL;
    /* Sentinel in a register BELOW the stored range, to prove stmw doesn't
     * touch anything outside [rs..31]. */
    ctx.gpr[28] = 0xDEADDEADDEADDEADULL;

    ppu_stmw(&ctx, 29, 0x10);

    /* Big-endian word order: word[0] (lowest address) = GPR[rs] = r29 (its
     * low 32 bits, since stmw stores the 32-bit word, not the full 64-bit
     * GPR), word[1] = r30, word[2] = r31 -- ASCENDING register at ASCENDING
     * address, not reversed. */
    uint32_t w0 = vm_read32(0x10);
    uint32_t w1 = vm_read32(0x14);
    uint32_t w2 = vm_read32(0x18);
    CHECK(w0 == (uint32_t)ctx.gpr[29], "stmw: word[0] == r29 (ascending reg/addr order)");
    CHECK(w1 == (uint32_t)ctx.gpr[30], "stmw: word[1] == r30");
    CHECK(w2 == (uint32_t)ctx.gpr[31], "stmw: word[2] == r31 (range runs through r31)");

    /* Verify the actual memory bytes are big-endian (MSB first), not a raw
     * host-order copy: r31's low word is 0x00000001 -> bytes 00 00 00 01. */
    CHECK(g_guest_mem[0x18] == 0x00 && g_guest_mem[0x19] == 0x00 &&
          g_guest_mem[0x1A] == 0x00 && g_guest_mem[0x1B] == 0x01,
          "stmw: word[2] bytes are big-endian (00 00 00 01)");

    /* lmw r29,0x10(r1): reload into a fresh context, must reproduce the low
     * 32 bits of each register (lmw zero-extends into the 64-bit GPR --
     * Book I: "GPR(r) <- 0x0 || MEM(EA,4)", no sign extension). */
    ppu_context ctx2;
    memset(&ctx2, 0, sizeof(ctx2));
    ctx2.gpr[28] = 0xABCDABCDABCDABCDULL;  /* sentinel: must survive untouched */

    ppu_lmw(&ctx2, 29, 0x10);

    CHECK(ctx2.gpr[29] == (uint32_t)ctx.gpr[29], "lmw: r29 round-trips through stmw/lmw");
    CHECK(ctx2.gpr[30] == (uint32_t)ctx.gpr[30], "lmw: r30 round-trips");
    CHECK(ctx2.gpr[31] == (uint32_t)ctx.gpr[31], "lmw: r31 round-trips (range runs through r31)");
    CHECK(ctx2.gpr[28] == 0xABCDABCDABCDABCDULL,
          "lmw: register below the loaded range (r28) is untouched");
    CHECK((ctx2.gpr[29] >> 32) == 0, "lmw: zero-extends into the upper 32 bits (no sign extension)");
}

/* ------------------------------------------------------------------------
 * fmadd family: true single-rounding fused semantics
 * --------------------------------------------------------------------- */
static void test_fma_family()
{
    printf("-- fmadd/fmsub/fnmadd/fnmsub family --\n");

    /* a = c = 2^27 + 1 (exactly representable). a*c = 2^54 + 2^28 + 1, which
     * needs 55 significant bits -- one more than a double's 53-bit mantissa
     * can hold in this exponent range (ULP = 4 for magnitudes in
     * [2^54, 2^55)) -- so the bare multiply must round, losing the low bit. */
    double a = 134217729.0; /* 2^27 + 1 */
    double c = 134217729.0;

    volatile double naive_mul = a * c;     /* forces real materialization: defeats
                                             * compiler FP-contraction so this test
                                             * measures the *naive formula's* actual
                                             * behavior, not an auto-fused one. */
    double b = -naive_mul;                 /* exact: negating a double is exact */

    volatile double naive_expr = a * c + b; /* naive two-op: multiply rounds once,
                                              * then the add rounds again */
    double fused = ppu_fmadd(a, c, b);      /* runtime helper under test */

    printf("   naive a*c+b = %.17g, ppu_fmadd(a,c,b) = %.17g\n",
           (double)naive_expr, fused);

    CHECK(naive_expr == 0.0,
          "sanity: naive a*c+b collapses to 0 (double-rounded, loses the exact remainder)");
    CHECK(fused == 1.0,
          "ppu_fmadd: recovers the exact rounding remainder (true single rounding, Book I 4.6.6)");
    CHECK(fused != (double)naive_expr,
          "ppu_fmadd differs from the naive two-op formula on this input (proves fusion, not luck)");

    /* fmsub: a*c - b  ==  fma(a,c,-b). With b as above, -b == naive_mul, so
     * this recombines to (true product) - (rounded product) == +1 as well
     * (same magnitude, mirrored sign convention through the identity). */
    double fmsub_result = ppu_fmsub(a, c, -b);
    CHECK(fmsub_result == 1.0, "ppu_fmsub(a,c,-b) matches ppu_fmadd(a,c,b) via -(-b)==b");

    /* fnmadd/fnmsub are simply the negation of fmadd/fmsub -- verify the sign
     * flip is applied to the already-correctly-rounded fused result. */
    CHECK(ppu_fnmadd(a, c, b) == -ppu_fmadd(a, c, b), "ppu_fnmadd == -ppu_fmadd(same operands)");
    CHECK(ppu_fnmsub(a, c, -b) == -ppu_fmsub(a, c, -b), "ppu_fnmsub == -ppu_fmsub(same operands)");

    /* Single-precision ("s") forms additionally round to float precision.
     * Use operands whose double-precision fused result is NOT exactly
     * representable as a float, so the extra rounding step is observable. */
    double da = 1.0 + 0x1p-30;   /* exactly representable in double, not in float */
    double dc = 1.0;
    double db = 0x1p-40;         /* far below float precision (24-bit mantissa) at this scale */
    double full_double = ppu_fmadd(da, dc, db);
    double rounded_single = ppu_fmadds(da, dc, db);
    CHECK(rounded_single == (double)(float)full_double,
          "ppu_fmadds rounds the fused result to float precision (not just double)");
    CHECK(rounded_single != full_double,
          "ppu_fmadds's extra float rounding is actually observable for this input");
}

int main()
{
    test_lmw_stmw();
    test_fma_family();

    printf("\nResults: %s (%d failure(s))\n", fails ? "FAIL" : "OK", fails);
    return fails ? 1 : 0;
}
