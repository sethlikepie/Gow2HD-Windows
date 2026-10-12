/*
 * ps3recomp - unit test for ps3_movie_eos_media_trace() (runtime/ppu/
 * ppu_movie_eos_trace.c), the MEDIA EOS trace hook vm_read8() (runtime/ppu/
 * ppu_loader.cpp) calls when the guest reads the armed EOS byte.
 *
 * Task 5 review fix (bringup-instrumentacao-fase0, Important #1): the EOS
 * trace line used to hardcode "src=natural" -- a claim the hook cannot back
 * up, since it has no visibility into WHY g_movie_eos_ea got armed (the real
 * armer is the sibling repo ../gow2-recomp/movie_eos_arm.c, entirely out of
 * this engine's view). ppu_movie_eos_trace.c's header comment explains the
 * fix in full: report the honest src=unknown unconditionally instead of
 * asserting a natural/forced split this repo cannot verify end-to-end; the
 * genuine natural|forced discriminator for MEDIA already lives on the
 * SEQDONE path (libs/codec/cellVdec.c, see
 * libs/codec/tests/test_cellvdec_force_gate.c), which is unaffected.
 *
 * This links the REAL ps3_movie_eos_media_trace() -- not a reimplementation
 * -- as its own tiny translation unit (it depends on nothing but
 * ps3_trace.h), which is exactly the point of factoring it out of the
 * multi-thousand-line ppu_loader.cpp: no fixture, no lifted function table,
 * no giant-lock/HLE scaffolding needed to exercise the real branch.
 *
 * Two invocations, not one process: ps3_trace_on() reads its env var ONCE and
 * caches the result for the rest of the process (by design -- no getenv() on
 * the hot path), so the "gate off" and "gate on" behaviours cannot both be
 * observed in a single run. `--baseline` runs ONLY the gate-off check (no
 * PS3_TRACE_* set at all); the default mode sets PS3_TRACE_MEDIA=1 before the
 * first call and runs the gate-on cases. scripts/smoke_macos.sh runs both.
 *
 * Build (see scripts/smoke_macos.sh):
 *   cc -std=c11 -O1 -I include -I runtime/trace/tests \
 *      runtime/ppu/tests/test_movie_eos_trace.c runtime/ppu/ppu_movie_eos_trace.c \
 *      runtime/trace/ps3_trace.c \
 *      -o test_movie_eos_trace
 * Run:
 *   ./test_movie_eos_trace             # gate-on cases
 *   ./test_movie_eos_trace --baseline  # gate-off / no-op baseline
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "stderr_capture.h"

extern int ps3_movie_eos_media_trace(uint32_t a, uint32_t eos_ea);

/* ps3_trace.c declares this extern and calls it unconditionally inside
 * ps3_trace_emit(); the real definition (runtime/syscalls/sys_timer.c) is not
 * linked into this standalone harness. Same stub idiom as
 * runtime/trace/tests/trace_link_stubs.h / libs/codec/tests/
 * test_cellvdec_force_gate.c. */
uint64_t ps3_timebase_now(void) { return 1; }

static int g_fail = 0;
#define CHECK(cond) do {                                                     \
    if (cond) { printf("  [PASS] %s\n", #cond); }                            \
    else      { printf("  [FAIL] line %d: %s\n", __LINE__, #cond); g_fail++; } \
} while (0)

/* ftell_stderr()/stderr_range_contains(): runtime/trace/tests/
 * stderr_capture.h (review fix, follow-up pos-Task 5, item 6 -- was pasted
 * locally here before). */

static void freopen_stderr_to_tmp(void)
{
    const char* tmp = getenv("TMPDIR"); if (!tmp || !*tmp) tmp = "/tmp";
    char dir[512]; snprintf(dir, sizeof dir, "%s/ps3recomp_eos_trace_XXXXXX", tmp);
    if (!mkdtemp(dir)) { printf("[FAIL] mkdtemp: %s\n", dir); exit(1); }
    char tracelog[600]; snprintf(tracelog, sizeof tracelog, "%s/trace.log", dir);
    if (!freopen(tracelog, "w+", stderr)) {
        printf("[FAIL] freopen stderr -> %s\n", tracelog); exit(1);
    }
}

int main(int argc, char** argv)
{
    const uint32_t EA = 0x00884744u;
    int baseline = (argc > 1 && strcmp(argv[1], "--baseline") == 0);

    freopen_stderr_to_tmp();

    if (baseline) {
        /* ---- no-op baseline: PS3_TRACE_MEDIA (and every other PS3_TRACE_*)
         * unset -> the matched call still returns 1 (the read-hook's return
         * value is NOT gated by tracing), but emits ZERO [PS3T] bytes (G2:
         * the trace layer is default-OFF and must not change observable
         * behaviour when tracing is off). ------------------------------- */
        long mark = ftell_stderr();
        int r = ps3_movie_eos_media_trace(EA, EA);
        CHECK(r == 1);
        CHECK(!stderr_range_contains(mark, "[PS3T]"));
    } else {
        setenv("PS3_TRACE_MEDIA", "1", 1);   /* before the first call: ps3_trace_on() caches on first use */

        /* ---- unarmed (eos_ea==0): no match, no trace, returns 0 -------- */
        long mark1 = ftell_stderr();
        int r1 = ps3_movie_eos_media_trace(EA, 0);
        CHECK(r1 == 0);
        CHECK(!stderr_range_contains(mark1, "[PS3T]"));

        /* ---- armed but a different address: no match ------------------- */
        long mark2 = ftell_stderr();
        int r2 = ps3_movie_eos_media_trace(EA, EA + 4);
        CHECK(r2 == 0);
        CHECK(!stderr_range_contains(mark2, "[PS3T]"));

        /* ---- armed and matched: returns 1, emits the canonical line with
         * the honest src=unknown (this is the whole point of the Task 5
         * review fix -- never assert natural/forced from this site). ----- */
        long mark3 = ftell_stderr();
        int r3 = ps3_movie_eos_media_trace(EA, EA);
        CHECK(r3 == 1);
        { char needle[64]; snprintf(needle, sizeof needle, "| MEDIA | EOS | ea=0x%08X", EA);
          CHECK(stderr_range_contains(mark3, needle)); }
        CHECK(stderr_range_contains(mark3, "src=unknown"));
        CHECK(!stderr_range_contains(mark3, "src=natural"));
        CHECK(!stderr_range_contains(mark3, "src=forced"));
    }

    printf("\n===========================================\n");
    printf("Results: %s (%d failure(s))\n", g_fail ? "FAIL" : "OK", g_fail);
    return g_fail ? 1 : 0;
}
