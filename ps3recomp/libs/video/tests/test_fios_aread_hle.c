/*
 * Unit test for ps3_fios_aread_hle() -- libs/video/fios_aread_hle.c.
 *
 * The helper is the host side of the FIOS async-read hook: given the guest's
 * op container, a destination EA and a byte count, it serves the bytes through
 * the REAL movie_io path (libs/video/movie_hle.c is linked in, nothing is
 * re-implemented here) and completes the op. This test drives it against a
 * temporary movie cache -- no game data, no boot.
 *
 * What it pins:
 *   1. happy path -- exact bytes land at dst, cursor advances, status goes
 *      done, the in-flight bit is cleared and nothing else in the op moves
 *   2. big-endian -- every op field is read and written BE. The fixture values
 *      are chosen so a host-endian access lands somewhere impossible.
 *   3. the op's limit clamps the request (partial tail of the window)
 *   4. an exhausted op (cursor == limit) is declined untouched
 *   5. SHORT READ IS A DECLINE -- the request runs past EOF, so nothing is
 *      written and the op is NOT marked done. A synchronous waiter released
 *      over a half-filled ring is the hazard here; the sibling
 *      lfq_host_inflate test guards the same class.
 *   6. failed read (well-formed movie_io token, no file behind it) declines
 *   7. non-movie FDs and fd == 0 decline (caller falls through to the guest)
 *   8. null / absurd arguments decline
 *   9. the guest-VM range guard (ppu_vm_size != 0) refuses op and dst past the
 *      end of the address space
 *  10. the product gate: with PS3_MOVIE_IO off the helper never fires
 *
 * Every decline is checked the same way: return 0, all eight op words byte-
 * identical, and the destination window still holding its sentinel.
 *
 * Build (see scripts/smoke_macos.sh):
 *   cc -std=c11 -O1 -I include libs/video/tests/test_fios_aread_hle.c \
 *      libs/video/movie_hle.c libs/video/fios_aread_hle.c \
 *      runtime/trace/ps3_trace.c runtime/trace/ps3_checkpoint.c \
 *      -o test_fios_aread_hle
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/wait.h>

#include "../fios_aread_hle.h"
#include "ps3_checkpoint.h"

/* Regression fix (bringup-instrumentacao-fase0, Tasks 1-2 vs.
 * scripts/smoke_macos.sh): movie_hle.c's movie_io_open() now calls
 * ps3_checkpoint_fire() (FIRST_WAD_OPEN), so this harness links
 * runtime/trace/ps3_trace.c + runtime/trace/ps3_checkpoint.c. Post-review fix
 * (Task 4 Minor #3): the four stubs ps3_checkpoint.c needs now live in a
 * shared header -- see runtime/trace/tests/trace_link_stubs.h. */
#include "trace_link_stubs.h"
#include "stderr_capture.h"

/* LAYOUT LOCK. Mirrors the constants in libs/video/fios_aread_hle.h, which are
 * derived from the static lift plus the Windows hook notes and are NOT yet
 * confirmed by the live PS3_TRACE_AREAD probe (plan Task 2). The test pokes
 * the op through the literal offsets below, so the header alone cannot move a
 * field without this block failing the build and forcing the update. When the
 * probe measurement lands: fix the header, then these five lines. */
_Static_assert(PS3_AREAD_OP_STATUS     == 0x08u,  "op status offset moved");
_Static_assert(PS3_AREAD_OP_FLAGS      == 0x0Cu,  "op flags offset moved");
_Static_assert(PS3_AREAD_OP_LIMIT      == 0x10u,  "op limit offset moved");
_Static_assert(PS3_AREAD_OP_CURSOR     == 0x14u,  "op cursor offset moved");
_Static_assert(PS3_AREAD_FLAG_INFLIGHT == 0x4000u, "in-flight bit moved");

/* movie_hle.c exports no header (the recompiled guest declares these at its
 * hook sites); mirror those declarations exactly, as test_movie_io.c does. */
extern int      movie_io_is(unsigned fd);
extern unsigned movie_io_open(const char* guest_path, unsigned* out_size);
extern void     movie_io_close(unsigned fd);

/* On POSIX runtime/ppu/ppu_loader.cpp owns this flag and movie_hle.c only
 * declares it. The loader is a per-game object, so stand in for it. */
#if !defined(_WIN32)
volatile int g_ps3_rperma_full = 0;
#endif

/* Link stubs, same rationale/scope as libs/video/tests/test_movie_io.c (Task 5,
 * bringup-instrumentacao-fase0): movie_io_open() calls these unconditionally
 * and this harness does not otherwise link movie_vt_metal.m/cellGcmSys.c. */
void cellGcmMainloopNote(const char* why) { (void)why; }
#if defined(__APPLE__)
void movie_vt_start(const char* host_m2v_path, int loop) { (void)host_m2v_path; (void)loop; }
void movie_vt_stop(void) {}
long movie_vt_overlay_done(void) { return 0; }
void movie_vt_autostart_from_cache(void) {}
void movie_vt_clear_overlay_done(void) {}
#endif

/* ---- guest memory double ------------------------------------------------
 * Signatures match the real accessors in runtime/ppu/ppu_loader.cpp
 * (uint64_t address, big-endian value), so the helper links unchanged against
 * either. The bounds check here is not the code under test: it exists so a
 * helper that walks off the arena fails loudly instead of corrupting the
 * harness. */
#define ARENA_SIZE (1u << 20)
static unsigned char g_arena[ARENA_SIZE];
unsigned char* vm_base = g_arena;
uint32_t       ppu_vm_size = 0;   /* harness convention: range guard off */

static int g_vm_oob = 0;

uint32_t vm_read32(uint64_t a)
{
    if (a + 4 > ARENA_SIZE) { g_vm_oob++; return 0; }
    const unsigned char* p = g_arena + a;
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  | (uint32_t)p[3];
}

void vm_write32(uint64_t a, uint32_t v)
{
    if (a + 4 > ARENA_SIZE) { g_vm_oob++; return; }
    unsigned char* p = g_arena + a;
    p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);  p[3] = (unsigned char)v;
}

/* ---- harness ------------------------------------------------------------ */

static int g_fail = 0;

#define CHECK(cond) do {                                                      \
    if (!(cond)) {                                                            \
        printf("  [FAIL] line %d: %s\n", __LINE__, #cond);                    \
        g_fail++;                                                             \
    }                                                                         \
} while (0)

#define CHECK_EQ_U32(got, want) do {                                          \
    uint32_t _g = (uint32_t)(got);                                            \
    uint32_t _w = (uint32_t)(want);                                           \
    if (_g != _w) {                                                           \
        printf("  [FAIL] line %d: %s == 0x%08X (%u), expected 0x%08X (%u)\n", \
               __LINE__, #got, _g, _g, _w, _w);                               \
        g_fail++;                                                             \
    }                                                                         \
} while (0)

/* Task 5 review fix (Minor #4): stderr capture so the FIOS/CALLBACK [PS3T]
 * line ps3_fios_aread_hle() emits on the fulfil path can be read back and
 * asserted on -- same idiom as libs/video/tests/test_movie_io.c and
 * libs/codec/tests/test_cellvdec_force_gate.c. ftell_stderr()/
 * stderr_range_contains() now come from runtime/trace/tests/
 * stderr_capture.h (review fix, follow-up pos-Task 5, item 6 -- was pasted
 * locally here before). */

/* ---- fixture ------------------------------------------------------------ */

/* Distinct-looking byte per offset: a read landing at the wrong file position
 * cannot compare equal by accident. Same generator as test_movie_io.c. */
static unsigned char pat(size_t i) { return (unsigned char)((i * 7u + 3u) & 0xFFu); }

#define FIXTURE_SIZE 0x2000u          /* 8192 B of pattern in the cache */

static int write_pattern_file(const char* path, size_t n)
{
    unsigned char* buf = (unsigned char*)malloc(n);
    if (!buf) return 0;
    for (size_t i = 0; i < n; i++) buf[i] = pat(i);
    FILE* f = fopen(path, "wb");
    if (!f) { free(buf); return 0; }
    size_t w = fwrite(buf, 1, n, f);
    fclose(f);
    free(buf);
    return w == n;
}

/* Op container. The four words the helper is allowed to touch get values that
 * are unambiguous under byte swapping; the four it must NOT touch get
 * sentinels, so a mis-aimed store shows up as a corrupted neighbour. */
#define OP_EA            0x1000u
#define DST_EA           0x8000u
#define DST_WINDOW       256u
#define DST_SENTINEL     0xEEu

#define OP_W00_SENTINEL  0xA0A0A0A0u
#define OP_W04_SENTINEL  0xB1B1B1B1u
#define OP_STATUS_BUSY   0x11111111u   /* != DONE, and != any offset's value */
#define OP_FLAGS_INIT    0x00034000u   /* other bits + the in-flight bit      */
#define OP_FLAGS_CLEARED 0x00030000u   /* the in-flight bit masked off        */
#define OP_W18_SENTINEL  0xC2C2C2C2u
#define OP_W1C_SENTINEL  0xD3D3D3D3u

/* Byte comparisons over a whole window report the FIRST divergence and stop:
 * one line naming the offset and the file position it should have come from is
 * what diagnoses a wrong seek, where 64 identical lines are just noise. */
static void check_bytes_from(const char* what, uint32_t dst, uint32_t file_pos,
                             uint32_t len)
{
    for (uint32_t i = 0; i < len; i++) {
        if (g_arena[dst + i] != pat(file_pos + i)) {
            printf("  [FAIL] %s: byte +%u = 0x%02X, expected 0x%02X "
                   "(file pos %u)\n", what, i, g_arena[dst + i],
                   pat(file_pos + i), file_pos + i);
            g_fail++;
            return;
        }
    }
}

static void check_untouched_from(const char* what, uint32_t dst, uint32_t from,
                                 uint32_t to)
{
    for (uint32_t i = from; i < to; i++) {
        if (g_arena[dst + i] != DST_SENTINEL) {
            printf("  [FAIL] %s: wrote past the request at +%u (0x%02X)\n",
                   what, i, g_arena[dst + i]);
            g_fail++;
            return;
        }
    }
}

static uint32_t g_cursor0, g_limit0;

static void op_reset(uint32_t cursor, uint32_t limit)
{
    g_cursor0 = cursor; g_limit0 = limit;
    vm_write32(OP_EA + 0x00, OP_W00_SENTINEL);
    vm_write32(OP_EA + 0x04, OP_W04_SENTINEL);
    vm_write32(OP_EA + 0x08, OP_STATUS_BUSY);
    vm_write32(OP_EA + 0x0C, OP_FLAGS_INIT);
    vm_write32(OP_EA + 0x10, limit);
    vm_write32(OP_EA + 0x14, cursor);
    vm_write32(OP_EA + 0x18, OP_W18_SENTINEL);
    vm_write32(OP_EA + 0x1C, OP_W1C_SENTINEL);
    memset(g_arena + DST_EA, DST_SENTINEL, DST_WINDOW);
}

/* The four words outside the documented layout must survive every call. */
static void check_op_neighbours(void)
{
    CHECK_EQ_U32(vm_read32(OP_EA + 0x00), OP_W00_SENTINEL);
    CHECK_EQ_U32(vm_read32(OP_EA + 0x04), OP_W04_SENTINEL);
    CHECK_EQ_U32(vm_read32(OP_EA + 0x18), OP_W18_SENTINEL);
    CHECK_EQ_U32(vm_read32(OP_EA + 0x1C), OP_W1C_SENTINEL);
}

/* Nothing at all moved: the op is bit-identical to op_reset() and the
 * destination window still holds its sentinel. */
static int op_and_dst_pristine(void)
{
    int ok = 1;
    if (vm_read32(OP_EA + 0x08) != OP_STATUS_BUSY) { printf("    status moved: 0x%08X\n", vm_read32(OP_EA + 0x08)); ok = 0; }
    if (vm_read32(OP_EA + 0x0C) != OP_FLAGS_INIT)  { printf("    flags moved: 0x%08X\n",  vm_read32(OP_EA + 0x0C)); ok = 0; }
    if (vm_read32(OP_EA + 0x10) != g_limit0)       { printf("    limit moved: 0x%08X\n",  vm_read32(OP_EA + 0x10)); ok = 0; }
    if (vm_read32(OP_EA + 0x14) != g_cursor0)      { printf("    cursor moved: 0x%08X\n", vm_read32(OP_EA + 0x14)); ok = 0; }
    for (unsigned i = 0; i < DST_WINDOW; i++)
        if (g_arena[DST_EA + i] != DST_SENTINEL) {
            printf("    dst written at +%u: 0x%02X\n", i, g_arena[DST_EA + i]);
            ok = 0; break;
        }
    return ok;
}

/* A decline: returns 0 AND leaves guest memory byte-for-byte as it was. */
#define EXPECT_DECLINE(why, op, dst, n, fd) do {                              \
    unsigned _r = ps3_fios_aread_hle((op), (dst), (n), (fd));                 \
    if (_r != 0) {                                                            \
        printf("  [FAIL] line %d: %s -> handled (%u), expected decline\n",    \
               __LINE__, (why), _r);                                          \
        g_fail++;                                                             \
    }                                                                         \
    if (!op_and_dst_pristine()) {                                             \
        printf("  [FAIL] line %d: %s touched guest memory\n", __LINE__, (why));\
        g_fail++;                                                             \
    }                                                                         \
} while (0)

/* ---- the PS3_MOVIE_IO gate, in a child process --------------------------
 * The gate is latched in a process-static the first time the helper runs (repo
 * convention for env gates, cf. lfq_host_inflate.c), so "off" can only be
 * observed by a process that never saw it on. Fork before the parent's first
 * call and let the child do its own open, so what declines is provably the
 * gate and not a bad FD. Returns 1 on success. */
static int check_gate_off_in_child(void)
{
    fflush(stdout);
    pid_t pid = fork();
    if (pid < 0) { printf("  [FAIL] fork: gate-off case not exercised\n"); return 0; }
    if (pid == 0) {
        unsetenv("PS3_MOVIE_IO");
        unsigned fd = movie_io_open("/gow2/R_PermA", NULL);
        int bad = 0;
        if (!fd) { printf("  [FAIL] gate-off child: open failed\n"); bad = 1; }
        op_reset(0, FIXTURE_SIZE);
        unsigned r = ps3_fios_aread_hle(OP_EA, DST_EA, 64, fd);
        if (r != 0) { printf("  [FAIL] gate-off child: handled (%u) with "
                             "PS3_MOVIE_IO unset\n", r); bad = 1; }
        if (!op_and_dst_pristine()) {
            printf("  [FAIL] gate-off child: touched guest memory\n"); bad = 1;
        }
        fflush(stdout);
        _exit(bad);
    }
    int st = 0;
    if (waitpid(pid, &st, 0) < 0) { printf("  [FAIL] waitpid\n"); return 0; }
    if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) {
        printf("  [FAIL] PS3_MOVIE_IO gate: child reported failure\n");
        return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------------ */

int main(void)
{
    /* --- a private movie cache for this run ----------------------------- */
    const char* tmp = getenv("TMPDIR");
    if (!tmp || !*tmp) tmp = "/tmp";
    char dir[512];
    snprintf(dir, sizeof dir, "%s/ps3recomp_aread_XXXXXX", tmp);
    if (!mkdtemp(dir)) { printf("[FAIL] mkdtemp: %s\n", dir); return 1; }
    setenv("PS3_MOVIE_CACHE", dir, 1);
    setenv("PS3_MOVIE_IO", "1", 1);

    /* Task 5 review fix (Minor #4): PS3_TRACE_FIOS on for the whole run, set
     * before any movie_io_open/ps3_fios_aread_hle call (ps3_trace_on() caches
     * its gate on first use, including inside the forked child below -- harmless,
     * it just means the child's own trace lines land in whatever stderr it
     * inherits at fork time). stderr is freopen'd to a plain file in the same
     * cache dir so the CALLBACK line can be read back -- same idiom as
     * libs/video/tests/test_movie_io.c. */
    setenv("PS3_TRACE_FIOS", "1", 1);
    char tracelog[700];
    snprintf(tracelog, sizeof tracelog, "%s/trace.log", dir);
    if (!freopen(tracelog, "w+", stderr)) {
        printf("[FAIL] freopen stderr -> %s\n", tracelog);
        return 1;
    }

    char p_perm[640];
    snprintf(p_perm, sizeof p_perm, "%s/R_PermA.wad_ps3", dir);
    if (!write_pattern_file(p_perm, FIXTURE_SIZE)) {
        printf("[FAIL] write %s\n", p_perm); return 1;
    }

    /* --- 10. product gate, before the parent latches it ------------------ */
    if (!check_gate_off_in_child()) g_fail++;

    /* The guest asks for the bare asset name; movie_io resolves the
     * .wad_ps3 in the cache, exactly as it does in boot. */
    unsigned size = 0;
    unsigned fd = movie_io_open("/gow2/R_PermA", &size);
    CHECK(fd != 0);
    CHECK(movie_io_is(fd) == 1);
    CHECK_EQ_U32(size, FIXTURE_SIZE);
    if (!fd) {
        printf("Results: movie_io_open failed, nothing to fulfil\n");
        remove(p_perm); rmdir(dir);
        return 1;
    }

    /* --- 1/2. happy path, big-endian everywhere -------------------------- */
    /* cursor 0x0123 and count 0x40: the resulting cursor 0x00000163 read
     * host-endian would be 0x63010000, and a host-endian cursor READ would ask
     * the cache for position 0x23010000 -- past EOF, so the read would fail
     * outright. Either way the checks below diverge. */
    const uint32_t POS1 = 0x0123u, N1 = 0x40u;
    op_reset(POS1, FIXTURE_SIZE);
    long mark_cb = ftell_stderr();
    CHECK_EQ_U32(ps3_fios_aread_hle(OP_EA, DST_EA, N1, fd), N1);
    check_bytes_from("happy path", DST_EA, POS1, N1);
    check_untouched_from("happy path", DST_EA, N1, DST_WINDOW);
    CHECK_EQ_U32(vm_read32(OP_EA + 0x14), POS1 + N1);          /* cursor advanced */
    CHECK_EQ_U32(vm_read32(OP_EA + 0x08), PS3_AREAD_STATUS_DONE);
    CHECK_EQ_U32(vm_read32(OP_EA + 0x0C), OP_FLAGS_CLEARED);   /* in-flight off  */
    CHECK_EQ_U32(vm_read32(OP_EA + 0x10), FIXTURE_SIZE);       /* limit untouched */
    check_op_neighbours();
    /* Task 5 review fix (Minor #4): the FIOS CALLBACK line on the fulfil path
     * had no committed assertion -- the file was linked only for its trace
     * source, never checked. Pin the mandated fields on this exact call:
     * op=/buf= are the op/dst EAs, off=/len= are the pre-call cursor and the
     * bytes actually served (POS1/N1 above), state=done on the happy path. */
    CHECK(stderr_range_contains(mark_cb, "| ps3_fios_aread_hle | FIOS | CALLBACK | "));
    { char needle[128];
      snprintf(needle, sizeof needle, "op=0x%08X buf=0x%08X off=%u len=%u state=done",
               OP_EA, DST_EA, POS1, N1);
      CHECK(stderr_range_contains(mark_cb, needle)); }

    /* --- 3. the op's limit clamps the request ---------------------------- */
    /* 16 bytes left in the window, 256 asked for: serve 16 and complete. A
     * host-endian limit (0x00200000) would not clamp at all, the read would
     * come back short and the helper would decline -- caught right here. */
    const uint32_t POS2 = FIXTURE_SIZE - 16u;
    op_reset(POS2, FIXTURE_SIZE);
    CHECK_EQ_U32(ps3_fios_aread_hle(OP_EA, DST_EA, 256u, fd), 16u);
    check_bytes_from("limit clamp", DST_EA, POS2, 16u);
    check_untouched_from("limit clamp", DST_EA, 16u, DST_WINDOW);
    CHECK_EQ_U32(vm_read32(OP_EA + 0x14), FIXTURE_SIZE);
    CHECK_EQ_U32(vm_read32(OP_EA + 0x08), PS3_AREAD_STATUS_DONE);
    CHECK_EQ_U32(vm_read32(OP_EA + 0x0C), OP_FLAGS_CLEARED);
    check_op_neighbours();

    /* --- 4. an exhausted op has nothing to serve ------------------------- */
    op_reset(FIXTURE_SIZE, FIXTURE_SIZE);
    EXPECT_DECLINE("cursor == limit", OP_EA, DST_EA, 64u, fd);
    check_op_neighbours();

    /* --- 5. SHORT READ IS A DECLINE -------------------------------------- */
    /* The op's window claims 16 KB but the cache file holds 8 KB, so the last
     * request runs off the end: 64 asked, 16 available. Nothing may be written
     * and the op must stay busy -- completing here would release a waiter over
     * 48 bytes of stale ring. */
    op_reset(FIXTURE_SIZE - 16u, FIXTURE_SIZE * 2u);
    EXPECT_DECLINE("read past EOF (short)", OP_EA, DST_EA, 64u, fd);
    check_op_neighbours();

    /* --- 6. well-formed movie_io token with no file behind it ------------ */
    op_reset(0, FIXTURE_SIZE);
    EXPECT_DECLINE("movie token, empty slot", OP_EA, DST_EA, 64u, 0x4D560004u);

    /* --- 7. FDs that are not ours ---------------------------------------- */
    EXPECT_DECLINE("fd == 0",       OP_EA, DST_EA, 64u, 0u);
    EXPECT_DECLINE("non-movie fd",  OP_EA, DST_EA, 64u, 0x12340001u);

    /* --- 8. null / absurd arguments -------------------------------------- */
    EXPECT_DECLINE("op_ea == 0",  0u,     DST_EA, 64u, fd);
    EXPECT_DECLINE("dst_ea == 0", OP_EA,  0u,     64u, fd);
    EXPECT_DECLINE("n == 0",      OP_EA,  DST_EA, 0u,  fd);
    EXPECT_DECLINE("n absurd",    OP_EA,  DST_EA, PS3_AREAD_MAX_BYTES + 1u, fd);

    /* --- 9. guest-VM range guard ----------------------------------------- */
    /* ppu_vm_size == 0 disables the check (the convention every harness in
     * this tree uses, cf. spu_args_ea_in_range); turn it on for these two. */
    ppu_vm_size = ARENA_SIZE;
    EXPECT_DECLINE("dst past end of VM", OP_EA, ARENA_SIZE - 8u, 64u, fd);
    EXPECT_DECLINE("dst wraps",          OP_EA, 0xFFFFFFF0u,     64u, fd);
    {   /* an op straddling the end of the VM: its own words are unreachable,
         * so it is declined before any field is read */
        unsigned r = ps3_fios_aread_hle(ARENA_SIZE - 8u, DST_EA, 64u, fd);
        CHECK_EQ_U32(r, 0u);
        if (!op_and_dst_pristine()) {
            printf("  [FAIL] op past end of VM touched guest memory\n");
            g_fail++;
        }
    }
    ppu_vm_size = 0;

    /* and with the guard on, a legal request still goes through */
    ppu_vm_size = ARENA_SIZE;
    op_reset(0, FIXTURE_SIZE);
    CHECK_EQ_U32(ps3_fios_aread_hle(OP_EA, DST_EA, 64u, fd), 64u);
    check_bytes_from("guard on, legal request", DST_EA, 0, 64u);
    CHECK_EQ_U32(vm_read32(OP_EA + 0x14), 64u);
    ppu_vm_size = 0;

    /* --- a closed FD stops being served ---------------------------------- */
    movie_io_close(fd);
    op_reset(0, FIXTURE_SIZE);
    EXPECT_DECLINE("closed fd", OP_EA, DST_EA, 64u, fd);

    /* --- cleanup --------------------------------------------------------- */
    remove(p_perm);
    remove(tracelog);
    rmdir(dir);

    CHECK(g_vm_oob == 0);   /* the helper never walked off the arena */

    if (g_fail) {
        printf("Results: %d check(s) failed\n", g_fail);
        return 1;
    }
    printf("fios_aread_hle: fulfil/clamp/decline + BE op fields + short-read "
           "safety OK\n");
    return 0;
}
