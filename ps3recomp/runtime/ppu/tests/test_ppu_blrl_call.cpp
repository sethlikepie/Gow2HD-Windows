/*
 * ps3recomp - blrl (Branch to Link Register, and Link) code-shape unit test.
 *
 * Hand-written companion to the golden emission test
 * (runtime/ppu/tests/test_ppu_lifter_emit.py, which compiles the REAL lifter
 * output). The `blrl_shape` below is a manual transcription of the C statement
 * tools/ppu_lifter.py emits for `blrl`; the goldens in test_ppu_lifter_emit.py
 * are what actually pin the lifter, while this file documents and asserts the
 * ISA contract in isolation against a mock ps3_indirect_call / g_trampoline_fn.
 *
 * What blrl must do, per the PowerPC ISA (Book I 4.2.2), and what this test
 * asserts:
 *   1. The branch target is read from LR *before* LR is overwritten (unlike
 *      `blr`, blrl LINKS: LR <- CIA+4 happens as part of the same
 *      instruction, but the OLD LR value is what gets branched to).
 *   2. It is a real CALL (dispatches through the same indirect-call mechanism
 *      bctrl uses) rather than a plain "return;", which is what this fork
 *      mistakenly did before this fix.
 *   3. CTR is left UNCHANGED. blrl writes ONLY LR; it never touches CTR. The
 *      lifter has to borrow CTR transiently as the transport for the CTR-keyed
 *      ps3_indirect_call dispatcher, but it saves the caller's CTR first and
 *      restores it after the call, so a legitimate `mtctr rX; ...; blrl`
 *      sequence keeps the rX value it staged. (An earlier version of this fix
 *      left the call target in CTR, which this test now guards against.)
 *   4. Any pending cross-fragment trampoline set by the dispatched call is
 *      drained before the calling function continues (DRAIN_TRAMPOLINE).
 *
 * Build:
 *   c++ -std=c++17 -O0 test_ppu_blrl_call.cpp -o test_ppu_blrl_call
 * Run:
 *   ./test_ppu_blrl_call
 */
#include "../ppu_context.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

/* --- Mocks standing in for the generated header's externs + "the game
 * project"'s real indirect-call dispatcher. --- */
extern "C" __thread void (*g_trampoline_fn)(void*) = nullptr;

static int g_indirect_call_count = 0;
static uint64_t g_indirect_call_saw_ctr = 0;
static int g_trampoline_ran = 0;

extern "C" void ps3_indirect_call(ppu_context* ctx)
{
    g_indirect_call_count++;
    g_indirect_call_saw_ctr = ctx->ctr;   /* what the dispatch was keyed on */
    /* Simulate the dispatched callee itself triggering a cross-fragment
     * trampoline (as a real lifted callee legitimately can) so the drain
     * path is exercised too. */
    g_trampoline_fn = [](void*) { g_trampoline_ran = 1; };
}

/* Mirrors ppu_lifter.py's DRAIN_TRAMPOLINE macro exactly (see HEADER_PREAMBLE
 * in tools/ppu_lifter.py). */
#define DRAIN_TRAMPOLINE(ctx) do { \
    while (g_trampoline_fn) { \
        void(*_tf)(void*) = g_trampoline_fn; \
        g_trampoline_fn = 0; \
        _tf((void*)(ctx)); \
    } \
} while(0)

/* Hand transcription of the C the lifter emits for `blrl` at guest address
 * `addr` (next_insn_addr == addr+4, computed by the lifter at lift time and
 * baked in as a literal -- passed as a parameter here since this test has no
 * fixed guest address). Must stay in lockstep with the `if mn == "blrl":`
 * handler in ppu_lifter.py; test_ppu_lifter_emit.py is the golden that fails
 * if the real emission diverges from this shape. */
static void blrl_shape(ppu_context* ctx, uint64_t next_insn_addr)
{
    uint64_t _blrl_tgt = ctx->lr; uint64_t _blrl_saved_ctr = ctx->ctr;
    ctx->lr = next_insn_addr; ctx->ctr = _blrl_tgt;
    ps3_indirect_call(ctx); DRAIN_TRAMPOLINE(ctx);
    ctx->ctr = _blrl_saved_ctr;
}

static int fails = 0;
#define CHECK(c, msg) do { if (c) printf("[PASS] %s\n", msg); \
    else { printf("[FAIL] %s\n", msg); fails++; } } while (0)

int main()
{
    ppu_context ctx;
    memset(&ctx, 0, sizeof(ctx));

    const uint64_t TARGET  = 0x00501234ULL;          /* what `mtlr rX` put in LR */
    const uint64_t RETADDR = 0x00402008ULL;          /* CIA(blrl) + 4 */
    const uint64_t OWN_CTR = 0x0BADF00DDEADBEEFULL;   /* caller's own live CTR value */
    ctx.lr  = TARGET;
    ctx.ctr = OWN_CTR;

    blrl_shape(&ctx, RETADDR);

    CHECK(g_indirect_call_count == 1, "blrl dispatches through ps3_indirect_call exactly once (a real call, not a no-op return)");
    CHECK(g_indirect_call_saw_ctr == TARGET, "blrl reads the call target from the LR value that was live BEFORE the link (read-before-write order)");
    CHECK(ctx.lr == RETADDR, "blrl sets LR to the return address (CIA+4), so the callee's own blr comes back here");
    CHECK(ctx.ctr == OWN_CTR, "blrl leaves CTR UNCHANGED (Book I 4.2.2: blrl writes only LR; the transient dispatch use of CTR is restored)");
    CHECK(g_trampoline_ran == 1, "DRAIN_TRAMPOLINE runs a trampoline the dispatched call left pending");
    CHECK(g_trampoline_fn == nullptr, "DRAIN_TRAMPOLINE clears the pending trampoline after running it");

    printf("\nResults: %s (%d failure(s))\n", fails ? "FAIL" : "OK", fails);
    return fails ? 1 : 0;
}
