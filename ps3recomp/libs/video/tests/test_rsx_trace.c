/*
 * ps3recomp - RSX PS3_TRACE committed test (bringup-instrumentacao-fase0,
 * Task 5 review fix, Minor #3).
 *
 * The review found zero committed test for the RSX subsystem: the diff added
 * no test and no PS3_TRACE_RSX assertion anywhere, the only offline backing
 * was an uncommitted ad-hoc rsx_backend_smoke invocation, and the in-boot log
 * that proved the events had already been deleted by the time of review --
 * exactly the "a verification nobody runs again" pattern that let the SPU
 * suite rot for two tasks (see the Task 5 report's own framing).
 *
 * This drives the REAL production code (libs/video/cellGcmSys.c's
 * rsx_process_fifo/cellGcmGetControlRegister/cellGcmSetFlipCommand, and
 * libs/video/rsx_commands.c's rsx_process_method, reached transitively
 * through the FIFO -- not called directly) through its public guest-facing
 * entry points: cellGcmInit, cellGcmGetControlRegister, cellGcmSetFlipCommand.
 * A hand-built big-endian FIFO command buffer stands in for what the guest
 * would have written via cellGcm*.
 *
 * PS3_RSX_BACKEND=trace selects s_trace_backend, an in-process, no-window
 * backend cellGcmSys.c defines itself (set_shader/clear/draw_arrays/
 * draw_indexed, all one-line stderr prints) purely so this kind of headless
 * exercise is possible -- no GPU, no window server, no SDL dependency, unlike
 * the "null"/"sdl" backend PS3_RSX_BACKEND=sdl selects (that one needs a
 * window server or SDL_VIDEODRIVER=dummy; it is covered separately by
 * libs/video/tests/run_backend_smoke.sh, GUI-gated, scripts/smoke_macos.sh
 * section 4).
 *
 * Reachable and asserted below: FIFO_KICK, FENCE, LABEL, FLIP_REQ, FLIP_DONE,
 * DRAW (both non-indexed NV4097_DRAW_ARRAYS and indexed
 * NV4097_DRAW_INDEX_ARRAY).
 *
 * NOT reachable this way -- stated precisely, not silently skipped: PRESENT
 * only fires when the registered backend defines .present
 * (libs/video/cellGcmSys.c's cellGcmSetFlipCommand: "if (b->present) { ...
 * PS3_TRACE ... PRESENT }"), and s_trace_backend does not define one (it
 * exists purely for state-tracking, not presentation). PRESENT remains
 * in-boot-only: proven real in .superpowers/sdd/task-5-report.md Sec.7/9
 * (in-boot FLIP_REQ/FLIP_DONE/PRESENT triple), and offline-reachable only via
 * the GUI-gated SDL null backend path above.
 *
 * Build (see scripts/smoke_macos.sh, section 2b):
 *   cc -std=c11 -O1 -I include -I libs/video -I runtime/trace/tests \
 *      libs/video/tests/test_rsx_trace.c libs/video/cellGcmSys.c \
 *      libs/video/rsx_commands.c \
 *      runtime/trace/ps3_trace.c runtime/trace/ps3_checkpoint.c \
 *      -o test_rsx_trace
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>

#include "cellGcmSys.h"
#include "rsx_commands.h"
#include "ps3emu/guest_call.h"
#include "stderr_capture.h"

/* NOT runtime/trace/tests/trace_link_stubs.h here: that header defines all
 * four of ps3_checkpoint.c's transitive needs, including ps3_gcm_flip_count
 * -- but libs/video/cellGcmSys.c already defines the REAL one (it owns the
 * SNAPSHOT flip counter), so pulling in the shared header would duplicate
 * that symbol. Stub only the other three by hand. */
uint64_t ps3_timebase_now(void)           { return 1; }
uint32_t ps3_ppu_thread_alive_count(void) { return 0; }
uint32_t ps3_sys_memory_used(void)        { return 0; }

/* ---- link-gap stubs -------------------------------------------------------
 * cellGcmSys.c unconditionally references these (real backends' init/pump,
 * the host mainloop-tracing TLS trio, the VRAM occupancy scanner) even though
 * this harness never takes those runtime branches: PS3_RSX_BACKEND stays
 * "trace" below (never null/d3d12/metal), and PS3_TRACE_MAINLOOP/PS3_TRACE_VRAM/
 * PS3_TRACE_TEX_PIXELS stay unset. Same one-line-stub idiom as the rsx_metal_*
 * stubs in libs/codec/tests/test_cellvdec_force_gate.c. */
ps3_guest_caller_fn g_ps3_guest_caller = NULL;
__thread unsigned long long g_last_hle_caller_lr;
__thread unsigned int       g_last_hle_caller_guest[4];
__thread unsigned int       g_last_hle_nid;
void ps3_scan_vram_occupancy(int phase) { (void)phase; }
int  rsx_null_backend_init(u32 w, u32 h, const char* t)  { (void)w; (void)h; (void)t; return -1; }
int  rsx_null_backend_pump_messages(void)                { return 0; }
int  rsx_d3d12_backend_init(u32 w, u32 h, const char* t) { (void)w; (void)h; (void)t; return -1; }
int  rsx_d3d12_backend_pump_messages(void)               { return 0; }
#if defined(_WIN32)
int  rsx_overlay_init(const char* path)                  { (void)path; return 1; }
void rsx_overlay_shutdown(void)                          {}
const char* rsx_overlay_settings_path_from_env(void)     { return NULL; }
#endif
#if defined(__APPLE__)
int  rsx_metal_backend_init(u32 w, u32 h, const char* t) { (void)w; (void)h; (void)t; return -1; }
int  rsx_metal_backend_pump_messages(void)               { return 0; }
#endif

/* ---- guest memory fixture --------------------------------------------------
 * LAYOUT LOCK: GCM_CTRL_EA/GCM_LABEL_EA mirror the #define's local to
 * libs/video/cellGcmSys.c (not exported by any header) -- if those move, the
 * addresses here need updating too, same caveat as the op-field offsets in
 * libs/video/tests/test_fios_aread_hle.c. Our own FIFO command buffer lives at
 * a low, disjoint address (IO_BASE) well under GCM_CTRL_EA. */
#define GCM_CTRL_EA   0x0A010000u
#define GCM_LABEL_EA  0x0A020000u
#define VM_SIZE       (GCM_LABEL_EA + 0x10000u)
#define IO_BASE       0x00200000u

static uint8_t* g_vm;

unsigned int vm_read32(unsigned long long a)
{
    uint32_t o = (uint32_t)a;
    if ((unsigned long long)o + 4ull > VM_SIZE) {
        printf("  [FAIL] vm_read32 OOB 0x%llX\n", a);
        return 0;
    }
    return ((unsigned int)g_vm[o]     << 24) | ((unsigned int)g_vm[o + 1] << 16) |
           ((unsigned int)g_vm[o + 2] <<  8) |  (unsigned int)g_vm[o + 3];
}
void vm_write32(unsigned long long a, unsigned int v)
{
    uint32_t o = (uint32_t)a;
    if ((unsigned long long)o + 4ull > VM_SIZE) {
        printf("  [FAIL] vm_write32 OOB 0x%llX\n", a);
        return;
    }
    g_vm[o]     = (uint8_t)(v >> 24); g_vm[o + 1] = (uint8_t)(v >> 16);
    g_vm[o + 2] = (uint8_t)(v >> 8);  g_vm[o + 3] = (uint8_t)v;
}

/* ---- FIFO command encoding (same header layout/idiom as
 * libs/video/tests/test_rsx_fifo.c's emit_method -- [28:18] count,
 * [12:2] method, type-0/increasing, count=1 per command here). ----------- */
static uint32_t g_io_put; /* offset from IO_BASE, mirrors GCM_CTRL_EA's put= */

static void fifo_put_be32(uint32_t v)
{
    vm_write32(IO_BASE + g_io_put, v);
    g_io_put += 4;
}
static void fifo_emit(uint32_t method, uint32_t data)
{
    uint32_t header = (1u << 18) | (((method >> 2) & 0x7FFu) << 2); /* type 0, count 1 */
    fifo_put_be32(header);
    fifo_put_be32(data);
}

/* ftell_stderr()/stderr_range_contains(): runtime/trace/tests/
 * stderr_capture.h (review fix, follow-up pos-Task 5, item 6 -- was pasted
 * locally here before). */

static int g_fail = 0;
#define CHECK(cond) do { \
    if (!(cond)) { printf("  [FAIL] line %d: %s\n", __LINE__, #cond); g_fail++; } \
} while (0)

/* Runs the whole cellGcmInit -> FIFO -> cellGcmGetControlRegister ->
 * cellGcmSetFlipCommand sequence once. Mode-agnostic: whether anything lands
 * on stderr depends entirely on PS3_TRACE_RSX in the CALLER's environment
 * (ps3_trace_on() resolves getenv() once per process -- same reason "off"
 * and "on" run as separate process invocations, see the wrapper script). */
static void run_scenario(void)
{
    g_io_put = 0;

    /* PS3_RSX_BACKEND=trace: see the file header comment -- the one backend
     * mode that needs neither a window nor a real GPU. PS3_RSX_FIFO=1: the
     * real recipe's FIFO parser (vs. the legacy instant-consume path). Both
     * are feature gates, not PS3_TRACE_* -- fine to force from inside the
     * test regardless of the off/on trace scenario. */
    setenv("PS3_RSX_BACKEND", "trace", 1);
    setenv("PS3_RSX_FIFO", "1", 1);

    s32 rc = cellGcmInit(0x10000, 0x100000, IO_BASE);
    CHECK(rc == CELL_OK);

    /* Build a FIFO: FENCE (SET_REFERENCE), LABEL (SEMAPHORE_RELEASE), a
     * non-indexed draw, then an indexed draw. */
    fifo_emit(0x0050u /* NV406E SET_REFERENCE  */, 0xCAFEBABEu);
    fifo_emit(0x0014u /* NV406E SEMAPHORE_RELEASE, offset 0 (s_rsx_sema_off default) */, 0x11223344u);
    fifo_emit(NV4097_SET_BEGIN_END,   RSX_PRIMITIVE_TRIANGLES);
    fifo_emit(NV4097_DRAW_ARRAYS,     (3u << 24) | 0u);       /* count=4, first=0 */
    fifo_emit(NV4097_SET_BEGIN_END,   0);
    fifo_emit(NV4097_SET_INDEX_ARRAY_ADDRESS, 0);
    fifo_emit(NV4097_SET_INDEX_ARRAY_DMA,     0);
    fifo_emit(NV4097_SET_BEGIN_END,   RSX_PRIMITIVE_TRIANGLE_STRIP);
    fifo_emit(NV4097_DRAW_INDEX_ARRAY, (3u << 24) | 0u);      /* count=4, first=0, indexed */
    fifo_emit(NV4097_SET_BEGIN_END,   0);

    vm_write32(GCM_CTRL_EA + 0, g_io_put); /* put= the end of what we just wrote */
    vm_write32(GCM_CTRL_EA + 4, 0);        /* get= start of the buffer           */

    (void)cellGcmGetControlRegister();     /* drains the FIFO above (PS3_RSX_FIFO=1) */
    rc = cellGcmSetFlipCommand(0);
    CHECK(rc == CELL_OK);
}

/* ---- "off" -- PS3_TRACE_RSX unset in the caller's env: zero "[PS3T]" lines,
 * even with a real FIFO_KICK/FENCE/LABEL/DRAW/FLIP sequence driven. G2 is
 * about ps3_trace_emit()'s own lines specifically -- NOT a literal
 * zero-bytes-on-stderr check, because PS3_RSX_BACKEND=trace's own debug
 * prints (e.g. "[rsx-bridge] DRAW_ARRAYS ...", unconditional, pre-existing,
 * unrelated to PS3_TRACE_RSX) are expected on stderr regardless of the trace
 * gate; conflating the two would fail this scenario for a reason that has
 * nothing to do with the instrumentation under test. */
static int test_off(long mark)
{
    int fails = 0;
    run_scenario();
    /* Matched narrowly on "| RSX |" (the <subsys> field, bounded by its own
     * " | " separators -- see include/ps3_trace.h's line format), not a bare
     * "[PS3T]": ps3_checkpoint_fire()'s FIRST_DRAW/FIRST_FLIP calls in the
     * production code this test drives emit their OWN "[PS3T] ... |
     * CHECKPOINTS | ..." lines whenever the CALLER's environment happens to
     * have PS3_TRACE_CHECKPOINTS set (review Minor: this scenario proves the
     * RSX gate specifically silences RSX lines, not that the whole process
     * environment is [PS3T]-free -- that's the runner's job, scrubbing every
     * PS3_TRACE_* var, see run_tests_rsx_trace.sh). */
    if (stderr_range_contains(mark, "| RSX |")) {
        printf("  [FAIL] off: esperava 0 linhas RSX com PS3_TRACE_RSX desligado\n");
        fails++;
    }
    return fails;
}

/* ---- "on" -- PS3_TRACE_RSX=1 in the caller's env: every mandated field on
 * every reachable event (see the file header comment for what is/isn't
 * reachable offline, and why). --------------------------------------------- */
static int test_on(long mark)
{
    int before = g_fail;
    run_scenario();

    /* FIFO_KICK: one per drain call, even before anything is parsed. */
    CHECK(stderr_range_contains(mark, "| rsx_process_fifo | RSX | FIFO_KICK | "));
    { char needle[80];
      snprintf(needle, sizeof needle, "io=0x%08X get=0x00000000 put=0x%08X", IO_BASE, g_io_put);
      CHECK(stderr_range_contains(mark, needle)); }

    /* FENCE: the SET_REFERENCE value published to GCM_CTRL_EA+8 (not
     * re-checked via a direct memory read afterward: cellGcmGetControlRegister
     * does its own unconditional ref++ on GCM_CTRL_EA+8 right after the FIFO
     * drains, so the raw memory value has already moved past what FENCE
     * published by the time control returns here -- the trace line is the
     * honest record of what FENCE actually saw). */
    CHECK(stderr_range_contains(mark, "| rsx_process_fifo | RSX | FENCE | val=0xCAFEBABE"));

    /* LABEL: the SEMAPHORE_RELEASE value published to GCM_LABEL_EA. */
    CHECK(stderr_range_contains(mark, "| rsx_process_fifo | RSX | LABEL | off=0 val=0x11223344"));
    CHECK(vm_read32(GCM_LABEL_EA + 0) == 0x11223344u);

    /* DRAW, non-indexed: dispatched through s_trace_backend's draw_arrays,
     * reached transitively via rsx_process_method. */
    CHECK(stderr_range_contains(mark, "| rsx_process_method | RSX | DRAW | "
                                       "prim=5 first=0 count=4 indexed=0"));

    /* DRAW, indexed. */
    CHECK(stderr_range_contains(mark, "| rsx_process_method | RSX | DRAW | "
                                       "prim=6 first=0 count=4 indexed=1"));

    /* FLIP_REQ / FLIP_DONE: cellGcmSetFlipCommand, unconditional (no backend
     * .present needed for either -- see the file header comment for why
     * PRESENT itself is not asserted here). */
    CHECK(stderr_range_contains(mark, "| cellGcmSetFlipCommand | RSX | FLIP_REQ | buf=0"));
    CHECK(stderr_range_contains(mark, "| cellGcmSetFlipCommand | RSX | FLIP_DONE | buf=0"));
    /* Precise negative: PRESENT must NOT appear -- s_trace_backend has no
     * .present, so this offline harness cannot reach it (documented above). */
    CHECK(!stderr_range_contains(mark, "| cellGcmSetFlipCommand | RSX | PRESENT | "));

    return g_fail - before;
}

int main(int argc, char** argv)
{
    const char* mode = (argc > 1) ? argv[1] : "on";

    g_vm = (uint8_t*)calloc(1, VM_SIZE);
    if (!g_vm) { printf("[FAIL] calloc guest memory (%u bytes)\n", (unsigned)VM_SIZE); return 1; }

    const char* tmp = getenv("TMPDIR");
    if (!tmp || !*tmp) tmp = "/tmp";
    char dir[512];
    snprintf(dir, sizeof dir, "%s/ps3recomp_rsx_trace_%s_XXXXXX", tmp, mode);
    if (!mkdtemp(dir)) { printf("[FAIL] mkdtemp: %s\n", dir); return 1; }
    char tracelog[600];
    snprintf(tracelog, sizeof tracelog, "%s/trace.log", dir);
    if (!freopen(tracelog, "w+", stderr)) {
        printf("[FAIL] freopen stderr -> %s\n", tracelog);
        return 1;
    }

    long mark = ftell_stderr();
    int fails;
    if (strcmp(mode, "off") == 0)      fails = test_off(mark);
    else if (strcmp(mode, "on") == 0)  fails = test_on(mark);
    else { printf("modo desconhecido: %s (usar off|on)\n", mode); return 2; }

    remove(tracelog);
    rmdir(dir);
    free(g_vm);

    printf("[%s] %d failed\n", mode, fails);
    return fails ? 1 : 0;
}
