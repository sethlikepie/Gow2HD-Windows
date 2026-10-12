/*
 * Unit test for the cellFs* <-> movie_io routing in runtime/ppu/ppu_fs.cpp.
 *
 * Two things are covered, both discovered as real gaps while bringing the GoW2
 * boot up on macOS:
 *
 *  A. movie_io routing. libs/video/movie_hle.c exposes a complete host-backed
 *     data path (movie_io_open/pread/seek/read_cur/stat/close) that serves
 *     pre-extracted psarc members out of PS3_MOVIE_CACHE. It had ZERO callers:
 *     nothing ever offered a guest path to it, so PS3_MOVIE_IO=1 was inert and
 *     the WADs could never be read from the cache. cellFsOpen must consult it
 *     first and hand back its tagged FD; Read/Lseek/Fstat/Close must recognise
 *     that tag and dispatch to the movie_io variant instead of the FILE* table.
 *
 *  B. Guest->host path mapping. The guest asks for
 *         /dev_hdd0/game/NPUA80491/USRDIR/gow2.psarc
 *     and PS3_VFS_ROOT points at the directory that HOLDS USRDIR's contents.
 *     Stripping only the "/dev_hdd0/" mount left "game/NPUA80491/USRDIR/..."
 *     appended to a root that already ended in USRDIR, so every open resolved
 *     to a doubled path that does not exist and the archive never opened.
 *
 * The test drives the REAL static handlers by including the translation unit,
 * so nothing is re-implemented here. Guest memory is a plain buffer and the
 * out-params are read back big-endian, exactly as the guest would see them.
 *
 * Build (see scripts/smoke_macos.sh):
 *   c++ -std=c++20 -I runtime/ppu/tests/fixture \
 *       runtime/ppu/tests/test_fs_movie_io.cpp libs/video/movie_hle.c \
 *       runtime/trace/ps3_trace.c runtime/trace/ps3_checkpoint.c \
 *       -o test_fs_movie_io
 *   (ps3_trace.c/ps3_checkpoint.c are C, not C++ -- precompile them with the C
 *   driver to .o first, same as movie_hle.c above, or clang++ mangles them.)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/stat.h>

#include "ppu_recomp.h"         /* fixture: ppu_context (see tests/fixture/) */
#include "ps3_checkpoint.h"

/* ---- stubs for the engine symbols ppu_fs.cpp imports -------------------
 * Defined BEFORE including the TU so the definitions satisfy its externs. */

static uint8_t  g_mem[1 << 16];
extern "C" uint8_t* vm_base       = g_mem;
extern "C" uint32_t ppu_vm_size   = (uint32_t)sizeof g_mem;

/* Guest memory is big-endian; ppu_fs.cpp writes every out-param through these. */
extern "C" void vm_write32(uint64_t a, uint32_t v)
{
    if (a + 4 > sizeof g_mem) return;
    g_mem[a + 0] = (uint8_t)(v >> 24); g_mem[a + 1] = (uint8_t)(v >> 16);
    g_mem[a + 2] = (uint8_t)(v >>  8); g_mem[a + 3] = (uint8_t)(v);
}
extern "C" void vm_write64(uint64_t a, uint64_t v)
{
    if (a + 8 > sizeof g_mem) return;
    for (int i = 0; i < 8; i++) g_mem[a + i] = (uint8_t)(v >> (56 - 8 * i));
}
static uint32_t rd32(uint32_t a)
{
    return ((uint32_t)g_mem[a] << 24) | ((uint32_t)g_mem[a+1] << 16)
         | ((uint32_t)g_mem[a+2] <<  8) |  (uint32_t)g_mem[a+3];
}
static uint64_t rd64(uint32_t a)
{
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v = (v << 8) | g_mem[a + i];
    return v;
}

/* The HLE registry is not under test. movie_hle_start/_once/_stop are NOT
 * stubbed here -- movie_hle.c already defines them (as no-ops on POSIX) and
 * that module is linked in, so defining them again would collide. */
extern "C" void ps3_hle_register_ctx(uint32_t, const char*, void (*)(ppu_context*)) {}
extern "C" unsigned long long ps3_ms(void) { return 0; }

/* On POSIX runtime/ppu/ppu_loader.cpp owns this flag and movie_hle.c only
 * declares it (see the #if defined(_WIN32) there). The loader is a per-game
 * object, not part of the runtime library, so stand in for it. */
#if !defined(_WIN32)
extern "C" { volatile int g_ps3_rperma_full = 0; }
#endif

/* Regression fix (bringup-instrumentacao-fase0, Tasks 1-2 vs.
 * scripts/smoke_macos.sh): ppu_fs.cpp's cellFsOpen() now calls
 * ps3_checkpoint_fire() (FIRST_FS_OPEN) and movie_hle.c's movie_io_open()
 * does too (FIRST_WAD_OPEN), so this harness links
 * runtime/trace/ps3_trace.c + runtime/trace/ps3_checkpoint.c. Post-review fix
 * (Task 4 Minor #3): the four stubs ps3_checkpoint.c needs now live in a
 * shared header (works from C++ too via extern "C") -- see
 * runtime/trace/tests/trace_link_stubs.h. */
#include "trace_link_stubs.h"
#include "stderr_capture.h"

#include "../ppu_fs.cpp"        /* the unit under test, statics and all */

/* movie_hle.c exports no header: every hook site declares what it uses. Mirror
 * those declarations here too, so the test can set up and tear down cache
 * handles independently of whatever the handlers do. */
extern "C" int      movie_io_is(unsigned fd);
extern "C" unsigned movie_io_open(const char* guest_path, unsigned* out_size);
extern "C" void     movie_io_close(unsigned fd);

/* Link stubs for symbols movie_hle.c calls unconditionally that this harness
 * does not otherwise pull in (movie_hle.o is precompiled separately by
 * scripts/smoke_macos.sh section 3b and linked here). Pre-existing gap
 * (bringup-instrumentacao-fase0, Task 4 report 8.5 #2), same rationale/scope
 * as the identical stubs added to libs/video/tests/test_movie_io.c and
 * test_fios_aread_hle.c in Task 5: this test only exercises the cellFs*->
 * movie_io routing, not the video overlay, so a no-op is correct here. */
extern "C" void cellGcmMainloopNote(const char* why) { (void)why; }
#if defined(__APPLE__)
extern "C" void movie_vt_start(const char* host_m2v_path, int loop) { (void)host_m2v_path; (void)loop; }
extern "C" void movie_vt_stop(void) {}
extern "C" long movie_vt_overlay_done(void) { return 0; }
extern "C" void movie_vt_autostart_from_cache(void) {}
#endif

/* ---- harness ----------------------------------------------------------- */

static int g_fail = 0;

#define CHECK(cond) do {                                                      \
    if (!(cond)) {                                                            \
        printf("  [FAIL] line %d: %s\n", __LINE__, #cond);                    \
        g_fail++;                                                             \
    }                                                                         \
} while (0)

/* Task 5 review fix (Minor #5): stderr capture so the [PS3T] FIOS lines
 * ppu_fs.cpp's cellFsRead() emits can be read back and asserted on -- same
 * idiom as libs/video/tests/test_movie_io.c and
 * libs/codec/tests/test_cellvdec_force_gate.c. ftell_stderr()/
 * stderr_range_contains() now come from runtime/trace/tests/
 * stderr_capture.h (review fix, follow-up pos-Task 5, item 6 -- was pasted
 * locally here before). */

#define CHECK_EQ_U64(got, want) do {                                          \
    unsigned long long _g = (unsigned long long)(got);                        \
    unsigned long long _w = (unsigned long long)(want);                       \
    if (_g != _w) {                                                           \
        printf("  [FAIL] line %d: %s == %llu, expected %llu\n",               \
               __LINE__, #got, _g, _w);                                       \
        g_fail++;                                                             \
    }                                                                         \
} while (0)

/* Deterministic byte pattern: every offset has a distinct-looking value, so a
 * read landing at the wrong position cannot accidentally compare equal. */
static unsigned char pat(size_t i) { return (unsigned char)((i * 11u + 5u) & 0xFFu); }

static void write_pattern_file(const char* path, size_t n)
{
    FILE* f = fopen(path, "wb");
    if (!f) { printf("  [FAIL] cannot create %s\n", path); g_fail++; return; }
    for (size_t i = 0; i < n; i++) fputc(pat(i), f);
    fclose(f);
}

/* Put a NUL-terminated guest string into guest memory and return its address. */
static uint32_t put_str(uint32_t at, const char* s)
{
    size_t n = strlen(s);
    memcpy(g_mem + at, s, n + 1);
    return at;
}

/* Call a handler with the usual (r3..r6) argument convention. */
static uint64_t call4(void (*fn)(ppu_context*), uint64_t r3, uint64_t r4,
                      uint64_t r5, uint64_t r6)
{
    ppu_context ctx;
    memset(&ctx, 0, sizeof ctx);
    ctx.gpr[3] = r3; ctx.gpr[4] = r4; ctx.gpr[5] = r5; ctx.gpr[6] = r6;
    fn(&ctx);
    return ctx.gpr[3];
}

/* ---- fixtures ---------------------------------------------------------- */

#define PATH_STR   0x1000u      /* guest addr of the path string  */
#define FD_OUT     0x2000u      /* guest addr of the out fd       */
#define BUF        0x3000u      /* guest addr of the read buffer  */
#define NREAD_OUT  0x5000u      /* guest addr of the out byte count */
#define STAT_OUT   0x5100u      /* guest addr of the CellFsStat   */
#define POS_OUT    0x5200u      /* guest addr of the out position */

static char g_cache[512];       /* stand-in PS3_MOVIE_CACHE */
static char g_root[512];        /* stand-in PS3_VFS_ROOT    */

/* The real R_PermA is 20169344 bytes; movie_io only flags the post-WAD gate
 * above a 20 MB floor, so use a size that clears it. */
#define RPERMA_SIZE  (20u * 1024u * 1024u + 64u)

static void make_fixtures(const char* tmp)
{
    snprintf(g_cache, sizeof g_cache, "%s/movie_cache", tmp);
    snprintf(g_root,  sizeof g_root,  "%s/USRDIR", tmp);
    mkdir(g_cache, 0777);
    mkdir(g_root, 0777);

    char p[1024];
    /* Mixed-case in the cache, exactly as the members are extracted. */
    snprintf(p, sizeof p, "%s/R_PermA.wad_ps3", g_cache);
    write_pattern_file(p, RPERMA_SIZE);
    snprintf(p, sizeof p, "%s/SmLogo_v2.wav", g_cache);
    write_pattern_file(p, 4096);

    /* A plain host file that is NOT in the cache: must fall through. */
    snprintf(p, sizeof p, "%s/gow2.psarc", g_root);
    write_pattern_file(p, 2048);

    setenv("PS3_MOVIE_CACHE", g_cache, 1);
    setenv("PS3_MOVIE_IO", "1", 1);
    ppu_vfs_root = g_root;
}

/* ---- tests ------------------------------------------------------------- */

/* 1. cellFsOpen must offer the guest path to movie_io FIRST and, when the
 *    cache claims it, return movie_io's tagged FD. */
static void test_open_routes_to_movie_io(void)
{
    printf("test: cellFsOpen routes a cached name to movie_io\n");
    put_str(PATH_STR, "R_PermA");
    uint64_t rc = call4(cellFsOpen, PATH_STR, CELL_FS_O_RDONLY, FD_OUT, 0);
    CHECK_EQ_U64(rc, (uint64_t)CELL_OK);

    uint32_t fd = rd32(FD_OUT);
    CHECK(movie_io_is(fd));                 /* tagged, not a FILE* table slot */
    if (movie_io_is(fd)) movie_io_close(fd);
}

/* 2. The tagged FD must survive Read/Lseek/Fstat/Close, which otherwise reject
 *    it as out of range of the FILE* table. */
static void test_read_seek_fstat_close_on_tagged_fd(void)
{
    printf("test: Read/Lseek/Fstat/Close honour the movie_io tag\n");
    put_str(PATH_STR, "R_PermA");
    call4(cellFsOpen, PATH_STR, CELL_FS_O_RDONLY, FD_OUT, 0);
    uint32_t fd = rd32(FD_OUT);
    if (!movie_io_is(fd)) { printf("  [SKIP] open did not route\n"); return; }

    /* Fstat reports the cache file's size, big-endian at +0x24. */
    memset(g_mem + STAT_OUT, 0, 0x40);
    CHECK_EQ_U64(call4(cellFsFstat, fd, STAT_OUT, 0, 0), (uint64_t)CELL_OK);
    CHECK_EQ_U64(rd64(STAT_OUT + 0x24), RPERMA_SIZE);

    /* Sequential read from the current cursor. */
    memset(g_mem + BUF, 0, 256);
    CHECK_EQ_U64(call4(cellFsRead, fd, BUF, 128, NREAD_OUT), (uint64_t)CELL_OK);
    CHECK_EQ_U64(rd64(NREAD_OUT), 128u);
    int ok = 1;
    for (size_t i = 0; i < 128; i++) if (g_mem[BUF + i] != pat(i)) ok = 0;
    CHECK(ok);                              /* bytes came from the cache file */

    /* Lseek moves that cursor and reports the new absolute position. */
    CHECK_EQ_U64(call4(cellFsLseek, fd, 1000, CELL_FS_SEEK_SET, POS_OUT),
                 (uint64_t)CELL_OK);
    CHECK_EQ_U64(rd64(POS_OUT), 1000u);
    memset(g_mem + BUF, 0, 256);
    call4(cellFsRead, fd, BUF, 64, NREAD_OUT);
    CHECK_EQ_U64(rd64(NREAD_OUT), 64u);
    ok = 1;
    for (size_t i = 0; i < 64; i++) if (g_mem[BUF + i] != pat(1000 + i)) ok = 0;
    CHECK(ok);                              /* seek actually took effect */

    /* SEEK_END reports the size. */
    call4(cellFsLseek, fd, 0, CELL_FS_SEEK_END, POS_OUT);
    CHECK_EQ_U64(rd64(POS_OUT), RPERMA_SIZE);

    CHECK_EQ_U64(call4(cellFsClose, fd, 0, 0, 0), (uint64_t)CELL_OK);
    /* After close the slot is free again: a second open must succeed. */
    call4(cellFsOpen, PATH_STR, CELL_FS_O_RDONLY, FD_OUT, 0);
    uint32_t fd2 = rd32(FD_OUT);
    CHECK(movie_io_is(fd2));
    if (movie_io_is(fd2)) movie_io_close(fd2);
}

/* 3. Reading a >=20 MB "R_PermA" to completion through cellFsRead must set
 *    g_ps3_rperma_full. That flag is what makes ps3_force_post_wad_icgldr
 *    reachable, and it is the entire point of the data path. */
static void test_full_read_flags_rperma_gate(void)
{
    printf("test: a complete R_PermA read through cellFsRead opens the gate\n");
    g_ps3_rperma_full = 0;
    put_str(PATH_STR, "R_PermA");
    call4(cellFsOpen, PATH_STR, CELL_FS_O_RDONLY, FD_OUT, 0);
    uint32_t fd = rd32(FD_OUT);
    if (!movie_io_is(fd)) { printf("  [SKIP] open did not route\n"); return; }

    unsigned long long total = 0;
    const uint64_t chunk = 8192;
    for (int guard = 0; guard < 4000 && total < RPERMA_SIZE; guard++) {
        call4(cellFsRead, fd, BUF, chunk, NREAD_OUT);
        uint64_t n = rd64(NREAD_OUT);
        if (!n) break;
        total += n;
    }
    CHECK_EQ_U64(total, RPERMA_SIZE);
    CHECK(g_ps3_rperma_full == 1);
    movie_io_close(fd);
}

/* 4. A path the cache does not hold must fall through to the host file, and
 *    the /dev_hdd0/game/<TITLEID>/USRDIR/ form must resolve against the root.
 *    This is the open that fails in the real boot. */
static void test_hdd_game_path_resolves_to_root(void)
{
    printf("test: /dev_hdd0/game/<ID>/USRDIR/gow2.psarc resolves under the root\n");
    put_str(PATH_STR, "/dev_hdd0/game/NPUA80491/USRDIR/gow2.psarc");
    uint64_t rc = call4(cellFsOpen, PATH_STR, CELL_FS_O_RDONLY, FD_OUT, 0);
    CHECK_EQ_U64(rc, (uint64_t)CELL_OK);
    uint32_t fd = rd32(FD_OUT);
    CHECK(!movie_io_is(fd));                /* not cached -> real host FILE* */

    memset(g_mem + BUF, 0, 32);
    /* Task 5 review fix (Minor #5): the host-direct READ line used to omit
     * off=, the one FIOS field its movie_io siblings (movie_io_pread/
     * movie_io_read_cur) both carry. Two reads at known positions (0, then
     * 16) pin the value is the REAL pre-read file position, not a stale/
     * constant placeholder. */
    long mark_r1 = ftell_stderr();
    call4(cellFsRead, fd, BUF, 16, NREAD_OUT);
    CHECK_EQ_U64(rd64(NREAD_OUT), 16u);
    int ok = 1;
    for (size_t i = 0; i < 16; i++) if (g_mem[BUF + i] != pat(i)) ok = 0;
    CHECK(ok);
    CHECK(stderr_range_contains(mark_r1, "| FIOS | READ | op="));
    CHECK(stderr_range_contains(mark_r1, "off=0 "));   /* first read starts at 0 */

    long mark_r2 = ftell_stderr();
    call4(cellFsRead, fd, BUF, 16, NREAD_OUT);
    CHECK_EQ_U64(rd64(NREAD_OUT), 16u);
    CHECK(stderr_range_contains(mark_r2, "off=16 "));  /* second read starts after the first */

    call4(cellFsClose, fd, 0, 0, 0);

    /* cellFsStat must agree -- FIOS stats the archive before opening it. */
    memset(g_mem + STAT_OUT, 0, 0x40);
    CHECK_EQ_U64(call4(cellFsStat, PATH_STR, STAT_OUT, 0, 0), (uint64_t)CELL_OK);
    CHECK_EQ_U64(rd64(STAT_OUT + 0x24), 2048u);
}

/* 5. Case: inside the psarc the members are lowercase and the guest asks for
 *    mixed case (and vice versa for the movies). macOS's default volume hides
 *    this; a case-sensitive volume would not. */
static void test_case_insensitive_cache_lookup(void)
{
    printf("test: cache lookup is case-insensitive\n");
    put_str(PATH_STR, "r_perma");                 /* on-disk: R_PermA.wad_ps3 */
    call4(cellFsOpen, PATH_STR, CELL_FS_O_RDONLY, FD_OUT, 0);
    uint32_t fd = rd32(FD_OUT);
    CHECK(movie_io_is(fd));
    if (movie_io_is(fd)) movie_io_close(fd);

    /* The guest asks for this one lowercase, with a mount prefix and an
     * extension; the cache holds "SmLogo_v2.wav". */
    put_str(PATH_STR, "/dev_hdd0/game/NPUA80491/USRDIR/_movies/smlogo_v2.wav");
    call4(cellFsOpen, PATH_STR, CELL_FS_O_RDONLY, FD_OUT, 0);
    fd = rd32(FD_OUT);
    CHECK(movie_io_is(fd));
    if (movie_io_is(fd)) movie_io_close(fd);
}

/* 6. PS3_MOVIE_IO unset must leave the cache completely out of the picture. */
static void test_gate_off_never_consults_cache(void)
{
    printf("test: without PS3_MOVIE_IO the cache is not consulted\n");
    unsetenv("PS3_MOVIE_IO");
    g_movie_io_gate = -1;   /* file-scope static of the included TU */
    put_str(PATH_STR, "R_PermA");
    uint64_t rc = call4(cellFsOpen, PATH_STR, CELL_FS_O_RDONLY, FD_OUT, 0);
    /* No host file of that name under the root -> plain ENOENT, no tagged fd. */
    CHECK_EQ_U64(rc, (uint64_t)(uint64_t)(int64_t)CELL_FS_ENOENT);
    setenv("PS3_MOVIE_IO", "1", 1);
    g_movie_io_gate = -1;   /* file-scope static of the included TU */
}

int main(void)
{
    char tmpl[] = "/tmp/ps3recomp_fs_movie_io.XXXXXX";
    const char* tmp = mkdtemp(tmpl);
    if (!tmp) { printf("FAIL: mkdtemp\n"); return 1; }

    /* Task 5 review fix (Minor #5): PS3_TRACE_FIOS on for the whole run, set
     * before any cellFsOpen/movie_io_* call (ps3_trace_on() caches its gate
     * on first use) so the off= assertion in test 4 below observes the real
     * gate, not a permanently-cached OFF. stderr is freopen'd to a plain file
     * in the same tmp dir so the [PS3T] lines can be read back -- same idiom
     * as libs/video/tests/test_movie_io.c. Tests narrate via printf(stdout),
     * untouched by this redirection. */
    setenv("PS3_TRACE_FIOS", "1", 1);
    char tracelog[600];
    snprintf(tracelog, sizeof tracelog, "%s/trace.log", tmp);
    if (!freopen(tracelog, "w+", stderr)) {
        printf("FAIL: freopen stderr -> %s\n", tracelog);
        return 1;
    }

    make_fixtures(tmp);

    test_open_routes_to_movie_io();
    test_read_seek_fstat_close_on_tagged_fd();
    test_full_read_flags_rperma_gate();
    test_hdd_game_path_resolves_to_root();
    test_case_insensitive_cache_lookup();
    test_gate_off_never_consults_cache();

    char cmd[1024];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", tmp);
    if (system(cmd) != 0) printf("  [warn] could not remove %s\n", tmp);

    if (g_fail) { printf("FAILED: %d check(s)\n", g_fail); return 1; }
    printf("PASS: cellFs <-> movie_io routing\n");
    return 0;
}
