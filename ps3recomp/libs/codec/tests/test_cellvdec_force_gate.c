/*
 * Unit test for the cellVdec FORCE-SEQDONE watchdog -- libs/codec/cellVdec.c.
 *
 * GoW2's logo FSM cannot leave its "playing" state until the movie player
 * receives SEQDONE on the callback it registered with cellVdecOpen. When the
 * elementary stream never reaches the decoder (the AU path is not wired), the
 * only honest way to unblock bring-up is the opt-in watchdog: after
 * PS3_VDEC_FORCE_SEQDONE_MS the HLE decides the sequence is over and delivers
 * SEQDONE THROUGH THE GUEST'S OWN CALLBACK, exactly as a natural EOF would.
 *
 * That is a replay, not a forge, and the difference is entirely in the gating.
 * This test links the REAL cellVdec.c and pins both halves:
 *
 *   1. Open's minimal contract: the callback OPD/arg are read big-endian from
 *      the guest cb struct and the handle is written back big-endian.
 *   2. GATE OFF (env unset)   -> no SEQDONE, ever.        <- falsification
 *   3. GATE ZERO (env == "0") -> no SEQDONE, ever.        <- falsification
 *   4. GATE ON  -> exactly one SEQDONE, on the registered OPD, carrying
 *      {handle, CELL_VDEC_MSG_TYPE_SEQDONE, 0, cbArg}, delivered with the PPU
 *      giant lock held, and NOT before the configured delay (nothing may be
 *      armed at t=0 / on StartSeq itself).
 *   5. still exactly one after the deadline passes again -- single-shot, so a
 *      natural EOF and the watchdog cannot both fire for one sequence.
 *   6. a NEW sequence re-arms (one SEQDONE per StartSeq, not one per process).
 *   7. EndSeq cancels a pending watchdog: the guest already got its SEQDONE
 *      from EndSeq, and the stale watchdog must not deliver a second one that
 *      no producer stands behind.
 *   8. a slot opened without a callback never reaches the guest caller -- the
 *      FORCE path must have a real registered OPD to talk to, never a
 *      fabricated one.
 *   9. a NATURAL end of stream spends the same single-shot token, so an armed
 *      watchdog cannot add a second SEQDONE on top of it. Two producers, one
 *      sequence, one event.
 *
 * Hermetic: movie_hle_cache_path() returns NULL here, so no ffmpeg process is
 * ever spawned and no game data is touched.
 *
 * Task 5 (bringup-instrumentacao-fase0): extended (not duplicated) with the
 * MEDIA SEQDONE src=natural|forced assertion the brief asks for -- cases 1-9
 * above already drive exactly the two producers (watchdog FORCE vs.
 * EndSeq/natural-EOF), so this only has to check the canonical event's
 * <data>. stderr is redirected to a temp file opened "w+" (read AND write)
 * so the test can read back the [PS3T] lines ps3_trace_emit() already writes
 * there; ftell_stderr()/stderr_range_contains() isolate the text emitted
 * after a given mark, same idiom as libs/video/tests/test_movie_io.c.
 *
 * Build (see scripts/smoke_macos.sh):
 *   cc -std=c11 -O1 -I include -I libs/codec -I runtime/trace/tests \
 *      libs/codec/tests/test_cellvdec_force_gate.c libs/codec/cellVdec.c \
 *      runtime/trace/ps3_trace.c \
 *      -o test_cellvdec_force_gate
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>

#include "cellVdec.h"
#include "ps3emu/guest_call.h"
#include "stderr_capture.h"

/* ps3_trace.c declares this extern and calls it unconditionally inside
 * ps3_trace_emit(); the real definition (runtime/syscalls/sys_timer.c) is not
 * linked into this standalone harness. Same stub idiom as
 * runtime/trace/tests/trace_link_stubs.h, kept local here because this file
 * does not link ps3_checkpoint.c (no need for the other three accessors). */
uint64_t ps3_timebase_now(void) { return 1; }

/* Pre-existing link gap on Darwin, unrelated to Task 5 (confirmed by
 * inspecting cellVdec.c at the branch's base commit 2142a8f: vdec_try_metal_
 * present's #if defined(__APPLE__) branch already called these two functions
 * from libs/video/rsx_metal_backend.m, which this standalone harness -- like
 * scripts/smoke_macos.sh section 3d -- never linked). Stubbed locally, same
 * scope/rationale as the movie_vt_ and cellGcmMainloopNote stubs added to
 * libs/video/tests/test_movie_io.c and test_fios_aread_hle.c in this task. */
#if defined(__APPLE__)
void rsx_metal_movie_frame_stage(const void* bgra, unsigned w, unsigned h)
{ (void)bgra; (void)w; (void)h; }
int rsx_metal_movie_overlay_active(void) { return 0; }
#endif

/* ftell_stderr()/stderr_range_contains(): runtime/trace/tests/
 * stderr_capture.h (review fix, follow-up pos-Task 5, item 6 -- was pasted
 * locally here before). */

/* ---- fixture knobs ------------------------------------------------------
 * FORCE_MS is the watchdog delay under test; FORCE_FLOOR_MS is the earliest
 * arrival the test tolerates (timer slop), and proves the callback is not
 * armed at StartSeq time. SETTLE_MS is how long we keep watching after a
 * deadline to catch a second, duplicate delivery. */
#define FORCE_MS        200
#define FORCE_FLOOR_MS  150
#define SETTLE_MS       600
#define POLL_LIMIT_MS   5000

/* ---- guest memory double ------------------------------------------------
 * cellVdec.c reads/writes guest structs directly through vm_base with its own
 * big-endian helpers, so a flat arena is all it needs. */
#define ARENA_SIZE (1u << 16)
static unsigned char g_arena[ARENA_SIZE];
unsigned char* vm_base = g_arena;

/* ---- PPU giant lock double ----------------------------------------------
 * The real one lives in runtime/ppu/ppu_loader.cpp (a per-game object, not in
 * the runtime library). Model the contract the HLE dispatcher gives handlers:
 * one non-recursive mutex, held across a guest callback. */
static pthread_mutex_t g_giant = PTHREAD_MUTEX_INITIALIZER;
static volatile int    g_giant_held = 0;
void ppu_giant_lock_acquire(void) { pthread_mutex_lock(&g_giant); g_giant_held = 1; }
void ppu_giant_lock_release(void) { g_giant_held = 0; pthread_mutex_unlock(&g_giant); }

/* Host movie cache. NULL for most cases, so vdec_open_decoder bails before
 * popen() and nothing external runs. Case 9 points it at a path that does not
 * exist: popen() still succeeds (it spawns a shell), the decode command writes
 * nothing, and the first fread comes up short -- i.e. the decoder reaches its
 * NATURAL end of stream, deterministically, whether or not ffmpeg is even
 * installed on this host. */
static const char* g_cache_path = NULL;
const char* movie_hle_cache_path(void) { return g_cache_path; }

/* ---- guest callback recorder --------------------------------------------
 * Stands in for the recompiled dispatcher (boot_macos.cpp / boot_main.cpp).
 * Defined by libs/system/cellSysutil.c in the real build. */
ps3_guest_caller_fn g_ps3_guest_caller = NULL;

typedef struct { uint32_t opd; uint64_t a0, a1, a2, a3; long ms; int held; } CbRec;
#define CB_MAX 32
static CbRec  g_cb[CB_MAX];
static volatile int g_cbn = 0;
static pthread_mutex_t g_cbmx = PTHREAD_MUTEX_INITIALIZER;
static long   g_t0_ms;

static long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)(ts.tv_sec * 1000L + ts.tv_nsec / 1000000L);
}
static void sleep_ms(long ms)
{
    struct timespec ts;
    ts.tv_sec  = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

static void record_cb(uint32_t opd, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3)
{
    pthread_mutex_lock(&g_cbmx);
    if (g_cbn < CB_MAX) {
        g_cb[g_cbn].opd = opd; g_cb[g_cbn].a0 = a0; g_cb[g_cbn].a1 = a1;
        g_cb[g_cbn].a2 = a2;   g_cb[g_cbn].a3 = a3;
        g_cb[g_cbn].ms  = now_ms() - g_t0_ms;
        g_cb[g_cbn].held = g_giant_held;
        g_cbn++;
    }
    pthread_mutex_unlock(&g_cbmx);
}
static int cb_count(void)
{
    int n;
    pthread_mutex_lock(&g_cbmx); n = g_cbn; pthread_mutex_unlock(&g_cbmx);
    return n;
}
/* Callbacks of one message type only (case 9 also sees AUDONE). */
static int cb_count_type(uint64_t msg)
{
    int n = 0;
    pthread_mutex_lock(&g_cbmx);
    for (int i = 0; i < g_cbn; i++) if (g_cb[i].a1 == msg) n++;
    pthread_mutex_unlock(&g_cbmx);
    return n;
}
/* Wait until the callback count reaches `want`, or the budget runs out. */
static int wait_cb(int want, long budget_ms)
{
    long t = 0;
    while (cb_count() < want && t < budget_ms) { sleep_ms(5); t += 5; }
    return cb_count();
}

/* ---- HLE entry points ---------------------------------------------------
 * cellVdec.h deliberately does not declare these (they use the dispatcher ABI,
 * not the SDK prototypes); mirror the definitions in cellVdec.c. */
#define HLE8 uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t
extern uint64_t cellVdecOpen(HLE8);
extern uint64_t cellVdecStartSeq(HLE8);
extern uint64_t cellVdecEndSeq(HLE8);
extern uint64_t cellVdecClose(HLE8);
extern uint64_t cellVdecDecodeAu(HLE8);

/* ---- big-endian pokes into the arena ------------------------------------ */
static void be32(uint32_t ea, uint32_t v)
{
    g_arena[ea+0] = (uint8_t)(v >> 24); g_arena[ea+1] = (uint8_t)(v >> 16);
    g_arena[ea+2] = (uint8_t)(v >> 8);  g_arena[ea+3] = (uint8_t)v;
}
static uint32_t rd_be32(uint32_t ea)
{
    return ((uint32_t)g_arena[ea+0] << 24) | ((uint32_t)g_arena[ea+1] << 16) |
           ((uint32_t)g_arena[ea+2] << 8)  |  (uint32_t)g_arena[ea+3];
}

/* Handlers run with the giant lock held in the real dispatcher; StartSeq/
 * EndSeq/Close rely on that (they release it to join the decoder thread). */
#define HLE_CALL(expr) ({ ppu_giant_lock_acquire(); uint64_t r_ = (expr); \
                          ppu_giant_lock_release(); r_; })

static int g_fail = 0;
#define CHECK(cond, ...) do { if (!(cond)) { \
        fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
        fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); g_fail++; } } while (0)

/* ---- guest struct layout used by the cases ------------------------------ */
#define EA_TYPE   0x1000u    /* CellVdecType   {codecType, profileLevel} */
#define EA_CB     0x1100u    /* {u32 cbFuncOPD, u32 cbArg}               */
#define EA_HANDLE 0x1200u    /* OUT: handle                              */
#define EA_CB2    0x1300u
#define EA_HAND2  0x1400u

#define CB_OPD  0x00ABCDEFu
#define CB_ARG  0x11223344u

int main(void)
{
    g_t0_ms = now_ms();
    g_ps3_guest_caller = record_cb;

    /* Task 5 (bringup-instrumentacao-fase0): PS3_TRACE_MEDIA on for the whole
     * run, set before any cellVdec* call (ps3_trace_on() caches its gate on
     * first use). stderr is freopen'd to a private temp file so the canonical
     * [PS3T] lines (and this module's own existing fprintf(stderr,...)
     * diagnostics) can be read back and asserted on. */
    setenv("PS3_TRACE_MEDIA", "1", 1);
    static char tracelog[600];
    int real_stderr_fd = dup(fileno(stderr));   /* see the failure dump at the end */
    { const char* tmp = getenv("TMPDIR"); if (!tmp || !*tmp) tmp = "/tmp";
      char dir[512]; snprintf(dir, sizeof dir, "%s/ps3recomp_vdec_trace_XXXXXX", tmp);
      if (!mkdtemp(dir)) { printf("[FAIL] mkdtemp: %s\n", dir); return 1; }
      snprintf(tracelog, sizeof tracelog, "%s/trace.log", dir);
      if (!freopen(tracelog, "w+", stderr)) {
          printf("[FAIL] freopen stderr -> %s\n", tracelog); return 1;
      } }

    /* PS3_VDEC_ASYNC mirrors the canonical GoW2 recipe (the FORCE path only
     * runs async on Windows). It also puts the decoder thread and the watchdog
     * in the same race, which is what makes the single-shot check in case 5
     * able to discriminate a missing compare-and-swap. Set before the first
     * StartSeq: cellVdec.c latches the mode in a function-local static. */
    setenv("PS3_VDEC_ASYNC", "1", 1);

    /* ---- 1. Open: BE callback struct in, BE handle out ------------------ */
    be32(EA_TYPE + 0, CELL_VDEC_CODEC_TYPE_MPEG2);
    be32(EA_TYPE + 4, 0);
    be32(EA_CB + 0, CB_OPD);
    be32(EA_CB + 4, CB_ARG);
    be32(EA_HANDLE, 0xDEADBEEFu);

    uint64_t rc = HLE_CALL(cellVdecOpen(EA_TYPE, 0, EA_CB, EA_HANDLE, 0, 0, 0, 0));
    CHECK(rc == CELL_OK, "cellVdecOpen returned 0x%llX, want CELL_OK",
          (unsigned long long)rc);
    uint32_t handle = rd_be32(EA_HANDLE);
    CHECK(handle != 0xDEADBEEFu, "cellVdecOpen did not write the out handle "
          "(still the 0xDEADBEEF sentinel) -- the guest has no handle to drive");
    CHECK(handle < 4, "cellVdecOpen handed back handle %u, out of slot range", handle);

    /* ---- 2. GATE OFF: no env -> nothing is ever delivered --------------- */
    unsetenv("PS3_VDEC_FORCE_SEQDONE_MS");
    rc = HLE_CALL(cellVdecStartSeq(handle, 0, 0, 0, 0, 0, 0, 0));
    CHECK(rc == CELL_OK, "StartSeq (gate off) returned 0x%llX", (unsigned long long)rc);
    sleep_ms(FORCE_MS + SETTLE_MS);
    CHECK(cb_count() == 0, "gate OFF delivered %d callback(s): SEQDONE without a "
          "producer is a forge", cb_count());
    long mark_endseq = ftell_stderr();
    HLE_CALL(cellVdecEndSeq(handle, 0, 0, 0, 0, 0, 0, 0));   /* EndSeq's own SEQDONE */
    int base = cb_count();
    CHECK(base == 1, "EndSeq must deliver exactly one SEQDONE, saw %d total", base);
    /* Task 5: MEDIA SEQDONE src=natural -- cellVdecEndSeq never consults
     * PS3_VDEC_FORCE_SEQDONE_MS, so its SEQDONE is always the guest's own
     * request, never the watchdog. */
    CHECK(stderr_range_contains(mark_endseq, "| MEDIA | SEQDONE | handle="),
          "no canonical MEDIA SEQDONE line after EndSeq");
    CHECK(stderr_range_contains(mark_endseq, "src=natural"),
          "EndSeq's SEQDONE was not tagged src=natural");

    /* ---- 3. GATE ZERO: explicit "0" is off too -------------------------- */
    setenv("PS3_VDEC_FORCE_SEQDONE_MS", "0", 1);
    HLE_CALL(cellVdecStartSeq(handle, 0, 0, 0, 0, 0, 0, 0));
    sleep_ms(FORCE_MS + SETTLE_MS);
    CHECK(cb_count() == base, "gate \"0\" delivered %d extra callback(s)",
          cb_count() - base);
    HLE_CALL(cellVdecEndSeq(handle, 0, 0, 0, 0, 0, 0, 0));
    base = cb_count();

    /* ---- 4. GATE ON: one SEQDONE, on the guest's own OPD, not at t=0 ---- */
    { char ms[16]; snprintf(ms, sizeof ms, "%d", FORCE_MS);
      setenv("PS3_VDEC_FORCE_SEQDONE_MS", ms, 1); }
    long t_start = now_ms();
    long mark_forced = ftell_stderr();
    HLE_CALL(cellVdecStartSeq(handle, 0, 0, 0, 0, 0, 0, 0));
    int n = wait_cb(base + 1, POLL_LIMIT_MS);
    CHECK(n == base + 1, "gate ON delivered %d callback(s) after StartSeq, want 1",
          n - base);
    /* Task 5: MEDIA SEQDONE src=forced -- this is the watchdog path, gated on
     * PS3_VDEC_FORCE_SEQDONE_MS (G8: the env var's semantics are untouched by
     * this task, only reported on). */
    CHECK(stderr_range_contains(mark_forced, "| MEDIA | SEQDONE | handle="),
          "no canonical MEDIA SEQDONE line after the FORCE watchdog fired");
    CHECK(stderr_range_contains(mark_forced, "src=forced"),
          "the FORCE watchdog's SEQDONE was not tagged src=forced");
    if (n == base + 1) {
        CbRec* r = &g_cb[base];
        CHECK(r->opd == CB_OPD, "SEQDONE went to OPD 0x%08X, want the registered "
              "0x%08X", r->opd, CB_OPD);
        CHECK(r->a0 == handle, "SEQDONE arg0 (handle) = %llu, want %u",
              (unsigned long long)r->a0, handle);
        CHECK(r->a1 == CELL_VDEC_MSG_TYPE_SEQDONE, "SEQDONE msgType = %llu, want %d",
              (unsigned long long)r->a1, CELL_VDEC_MSG_TYPE_SEQDONE);
        CHECK(r->a2 == 0, "SEQDONE msgData = %llu, want 0", (unsigned long long)r->a2);
        CHECK(r->a3 == CB_ARG, "SEQDONE cbArg = 0x%llX, want the registered 0x%08X",
              (unsigned long long)r->a3, CB_ARG);
        CHECK(r->held, "SEQDONE was delivered WITHOUT the PPU giant lock: the "
              "guest callback would run concurrently with the game thread");
        long dt = r->ms - (t_start - g_t0_ms);
        CHECK(dt >= FORCE_FLOOR_MS, "SEQDONE arrived %ldms after StartSeq, floor is "
              "%dms -- the watchdog is armed too early to be a timeout", dt,
              FORCE_FLOOR_MS);
    }

    /* ---- 5. single-shot: the deadline passing again changes nothing ----- */
    base = cb_count();
    sleep_ms(FORCE_MS + SETTLE_MS);
    CHECK(cb_count() == base, "watchdog delivered %d duplicate SEQDONE(s) for one "
          "sequence", cb_count() - base);

    /* ---- 6. a new sequence re-arms -------------------------------------- */
    HLE_CALL(cellVdecStartSeq(handle, 0, 0, 0, 0, 0, 0, 0));
    n = wait_cb(base + 1, POLL_LIMIT_MS);
    CHECK(n == base + 1, "second sequence delivered %d SEQDONE(s), want exactly 1",
          n - base);
    CHECK(g_cb[base].a1 == CELL_VDEC_MSG_TYPE_SEQDONE, "second sequence msgType = %llu",
          (unsigned long long)g_cb[base].a1);

    /* ---- 7. EndSeq cancels a pending watchdog --------------------------- */
    base = cb_count();
    { char ms[16]; snprintf(ms, sizeof ms, "%d", FORCE_MS * 4);
      setenv("PS3_VDEC_FORCE_SEQDONE_MS", ms, 1); }
    HLE_CALL(cellVdecStartSeq(handle, 0, 0, 0, 0, 0, 0, 0));
    sleep_ms(FORCE_MS / 2);
    HLE_CALL(cellVdecEndSeq(handle, 0, 0, 0, 0, 0, 0, 0));
    CHECK(cb_count() == base + 1, "EndSeq delivered %d SEQDONE(s), want exactly 1",
          cb_count() - base);
    base = cb_count();
    sleep_ms(FORCE_MS * 4 + SETTLE_MS);
    CHECK(cb_count() == base, "a watchdog left over from an ENDED sequence "
          "delivered %d SEQDONE(s) with nothing behind it", cb_count() - base);
    HLE_CALL(cellVdecClose(handle, 0, 0, 0, 0, 0, 0, 0));

    /* ---- 8. no registered callback -> the guest caller is never used ---- */
    { char ms[16]; snprintf(ms, sizeof ms, "%d", FORCE_MS);
      setenv("PS3_VDEC_FORCE_SEQDONE_MS", ms, 1); }
    be32(EA_CB2 + 0, 0);      /* no OPD */
    be32(EA_CB2 + 4, 0);
    be32(EA_HAND2, 0xDEADBEEFu);
    HLE_CALL(cellVdecOpen(EA_TYPE, 0, EA_CB2, EA_HAND2, 0, 0, 0, 0));
    uint32_t h2 = rd_be32(EA_HAND2);
    CHECK(h2 != 0xDEADBEEFu, "second Open did not write its out handle");
    base = cb_count();
    HLE_CALL(cellVdecStartSeq(h2, 0, 0, 0, 0, 0, 0, 0));
    sleep_ms(FORCE_MS + SETTLE_MS);
    CHECK(cb_count() == base, "a slot with NO registered callback still reached the "
          "guest caller %d time(s)", cb_count() - base);
    HLE_CALL(cellVdecClose(h2, 0, 0, 0, 0, 0, 0, 0));

    /* ---- 9. a NATURAL end of stream also spends the single-shot token ----
     * The decoder reaching EOF and the watchdog firing are two independent
     * producers of the same event. Only one SEQDONE may reach the guest for
     * one sequence: the movie player treats it as "the movie is over" and a
     * second one lands on a player that has already moved on. Drive a real
     * EOF (see g_cache_path) with the watchdog armed for well after it, then
     * let the watchdog's deadline pass. */
    g_cache_path = "/nonexistent/ps3recomp-test-no-such-movie.m2v";
    { char ms[16]; snprintf(ms, sizeof ms, "%d", FORCE_MS * 4);
      setenv("PS3_VDEC_FORCE_SEQDONE_MS", ms, 1); }
    be32(EA_CB + 0, CB_OPD);
    be32(EA_CB + 4, CB_ARG);
    be32(EA_HANDLE, 0xDEADBEEFu);
    HLE_CALL(cellVdecOpen(EA_TYPE, 0, EA_CB, EA_HANDLE, 0, 0, 0, 0));
    uint32_t h3 = rd_be32(EA_HANDLE);
    int seq_before = cb_count_type(CELL_VDEC_MSG_TYPE_SEQDONE);
    HLE_CALL(cellVdecStartSeq(h3, 0, 0, 0, 0, 0, 0, 0));
    /* One solicited AU: the driver only reads the stream when it owes a
     * picture, so without this it never reaches its EOF branch. */
    HLE_CALL(cellVdecDecodeAu(h3, 0, 0, 0, 0, 0, 0, 0));
    long t_eof = now_ms();
    while (cb_count_type(CELL_VDEC_MSG_TYPE_SEQDONE) == seq_before &&
           now_ms() - t_eof < POLL_LIMIT_MS) sleep_ms(5);
    int seq_eof = cb_count_type(CELL_VDEC_MSG_TYPE_SEQDONE);
    CHECK(seq_eof == seq_before + 1, "natural EOF delivered %d SEQDONE(s), want 1",
          seq_eof - seq_before);
    CHECK(now_ms() - t_eof < FORCE_MS * 4, "EOF SEQDONE only arrived after the "
          "watchdog deadline -- case 9 is not testing what it claims");
    sleep_ms(FORCE_MS * 4 + SETTLE_MS);
    CHECK(cb_count_type(CELL_VDEC_MSG_TYPE_SEQDONE) == seq_eof,
          "the watchdog delivered %d MORE SEQDONE(s) after the stream had already "
          "ended naturally -- two producers, one sequence",
          cb_count_type(CELL_VDEC_MSG_TYPE_SEQDONE) - seq_eof);
    HLE_CALL(cellVdecClose(h3, 0, 0, 0, 0, 0, 0, 0));
    g_cache_path = NULL;

    if (g_fail) {
        /* stderr has been redirected to tracelog since the top of main() (see
         * the PS3_TRACE_MEDIA setup above), so every "FAIL ..." line the
         * CHECK() macro already wrote landed there instead of the real
         * stderr. Dump both the summary and the captured log to the REAL
         * stderr (saved before the freopen) so a failure is still visible to
         * a developer running this binary directly -- the smoke script
         * itself already discards stderr (`2>/dev/null`) either way, so this
         * only restores the direct-run experience, it does not change CI. */
        fflush(stderr);
        FILE* rs = fdopen(real_stderr_fd, "w");
        if (rs) {
            fprintf(rs, "test_cellvdec_force_gate: %d check(s) failed -- captured "
                    "trace log ('%s'):\n", g_fail, tracelog);
            FILE* lf = fopen(tracelog, "r");
            if (lf) {
                char line[1024];
                while (fgets(line, sizeof line, lf)) fputs(line, rs);
                fclose(lf);
            }
            fflush(rs);
        }
        return 1;
    }
    printf("cellvdec force gate: 9 cases ok (%d guest callback(s) recorded, %d SEQDONE)\n",
           cb_count(), cb_count_type(CELL_VDEC_MSG_TYPE_SEQDONE));
    return 0;
}
