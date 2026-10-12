/*
 * Unit test for the movie_io_* host data path in libs/video/movie_hle.c.
 *
 * movie_io_* is the host-backed file layer the recompiled game uses to stream
 * its own assets (movies and the pre-extracted resource WADs) out of the movie
 * cache instead of going through the guest FIOS scheduler + psarc dearchiver.
 * It was written Win32-only; this test exercises the REAL functions (the
 * module is linked in directly, nothing is re-implemented here) so the POSIX
 * port has to behave the same.
 *
 * Coverage:
 *   1. open of a cached file: FD token, reported size, movie_io_is
 *   2. stat of the same path without opening it
 *   3. pread at an offset returns the exact bytes
 *   4. seek + read_cur agree with pread (shared cursor)
 *   5. seek(END) reports the size; seek(CUR) is relative
 *   6. extensionless guest name resolves through the .m2v / .wad_ps3 fallback
 *   7. a path with no cache file returns 0 (caller falls back to the guest io)
 *   8. bogus / closed FDs are rejected instead of touching a stale slot
 *   9. the 4 io slots are finite and recycled by close
 *  10. g_ps3_rperma_full: a >=20 MB "R_PermA" read to completion flags the
 *      post-WAD gate FROM pread, before close. This is the whole reason the
 *      port matters -- on the unported tree the flag can never be set on a
 *      POSIX host, so runtime/ppu/ppu_loader.cpp's ps3_force_post_wad_icgldr
 *      is unreachable.
 *  11. (Task 5, bringup-instrumentacao-fase0) FIOS canonical trace: OP_BEGIN
 *      carries wad=1 for a .wad_ps3 open and wad=0 for a plain file; READ/
 *      PARTIAL/OP_END carry the fields the brief asks for (op=/off=/len=/
 *      buf=/state=); and the case-insensitive suffix fix (has_suffix_ci in
 *      movie_hle.c) still marks wad=1 for a real ".WAD_PS3" (uppercase)
 *      suffix -- the Task 2 review finding this task decided to fix.
 *
 * Build (see scripts/smoke_macos.sh):
 *   cc -std=c11 -O1 -I include libs/video/tests/test_movie_io.c \
 *      libs/video/movie_hle.c runtime/trace/ps3_trace.c \
 *      runtime/trace/ps3_checkpoint.c -o test_movie_io
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/stat.h>
#include "ps3_checkpoint.h"
#include "../movie_clock.h"

/* Regression fix (bringup-instrumentacao-fase0, Tasks 1-2 vs.
 * scripts/smoke_macos.sh): movie_hle.c's movie_io_open() now calls
 * ps3_checkpoint_fire() (FIRST_WAD_OPEN), so this harness links
 * runtime/trace/ps3_trace.c + runtime/trace/ps3_checkpoint.c. Post-review fix
 * (Task 4 Minor #3): the four stubs ps3_checkpoint.c needs now live in a
 * shared header -- see runtime/trace/tests/trace_link_stubs.h. */
#include "trace_link_stubs.h"
#include "stderr_capture.h"

/* movie_hle.c exports no header: the recompiled guest code declares these at
 * its hook sites. Mirror those declarations exactly. */
extern int                movie_io_is(unsigned fd);
extern unsigned           movie_io_open(const char* guest_path, unsigned* out_size);
extern unsigned long long movie_io_stat(const char* guest_path);
extern const char*        movie_hle_cache_path(void);
extern void               movie_hle_begin_ingame_sequence(void);
extern void               movie_hle_set_cache_path(const char* host);
extern unsigned           movie_io_pread(unsigned fd, void* dst, unsigned n, unsigned pos);
extern unsigned           movie_io_read_cur(unsigned fd, void* dst, unsigned n);
extern long long          movie_io_seek(unsigned fd, long long off, int whence);
extern void               movie_io_close(unsigned fd);

/* On POSIX runtime/ppu/ppu_loader.cpp owns the definition of this flag and
 * movie_hle.c only declares it (see the #if defined(_WIN32) there). The loader
 * is a per-game object, not part of the runtime library, so stand in for it. */
#if !defined(_WIN32)
volatile int g_ps3_rperma_full = 0;
#endif
extern volatile int g_ps3_rperma_full;

/* Link stubs for symbols movie_hle.c calls unconditionally that this test
 * does not otherwise pull in. Pre-existing gap (bringup-instrumentacao-fase0,
 * Task 4 report 8.5 #2): scripts/smoke_macos.sh's sections 3/3b/3c already
 * fail to link this same file on Darwin for this exact reason, documented
 * there as out of scope for that task ("wire-up do modulo VideoToolbox/Metal
 * do plano M0"). Stubbed HERE, scoped to this standalone test binary, so
 * Task 5's own new assertions (wad=1, READ/PARTIAL/OP_END format) can
 * actually link and run -- not a fix of the shared script gap, which stays
 * as Task 4 left it (movie_hle_start/movie_hle_from_guest still do nothing
 * useful here, which is fine: this test only exercises movie_io_*, not the
 * video overlay). cellGcmMainloopNote (cellGcmSys.c) is needed on every
 * platform; movie_vt_* (movie_vt_metal.m) only inside the __APPLE__ branch
 * of movie_hle.c. */
void cellGcmMainloopNote(const char* why) { (void)why; }
#if defined(__APPLE__)
void movie_vt_start(const char* host_m2v_path, int loop) { (void)host_m2v_path; (void)loop; }
void movie_vt_stop(void) {}
long movie_vt_overlay_done(void) { return 0; }
void movie_vt_autostart_from_cache(void) {}
void movie_vt_clear_overlay_done(void) {}
unsigned char* vm_base = NULL;
uint32_t ppu_vm_size = 0;
void ps3_watch_store_bulk(uint32_t ea, uint32_t len, const char* who)
{ (void)ea; (void)len; (void)who; }
#endif

/* ---- harness ----------------------------------------------------------- */

static int g_fail = 0;

#define CHECK(cond) do {                                                      \
    if (!(cond)) {                                                            \
        printf("  [FAIL] line %d: %s\n", __LINE__, #cond);                    \
        g_fail++;                                                             \
    }                                                                         \
} while (0)

#define CHECK_EQ_U64(got, want) do {                                          \
    unsigned long long _g = (unsigned long long)(got);                        \
    unsigned long long _w = (unsigned long long)(want);                       \
    if (_g != _w) {                                                           \
        printf("  [FAIL] line %d: %s == %llu, expected %llu\n",               \
               __LINE__, #got, _g, _w);                                       \
        g_fail++;                                                             \
    }                                                                         \
} while (0)

/* ---- Task 5: capture the [PS3T] canonical trace off redirected stderr ----
 * stderr is freopen'd (below, in main) to a plain file opened "w+" (read AND
 * write), so it can be used both as the sink ps3_trace_emit()/fprintf()
 * already write to and as the source these helpers read back from -- no
 * separate pipe or thread needed. ftell_stderr()/stderr_range_contains() now
 * come from runtime/trace/tests/stderr_capture.h (review fix, follow-up
 * pos-Task 5, item 6 -- was pasted locally here before). */

/* Deterministic byte pattern: every offset has a distinct-looking value, so a
 * read landing at the wrong position cannot accidentally compare equal. */
static unsigned char pat(size_t i) { return (unsigned char)((i * 7u + 3u) & 0xFFu); }

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

/* A 20 MB file of real content would make the smoke slow for no gain: the gate
 * only looks at st_size and the running bytes_read total, so a sparse file
 * (instant to create, zero-filled on read) exercises exactly the same path. */
static int write_sparse_file(const char* path, unsigned long long size)
{
    FILE* f = fopen(path, "wb");
    if (!f) return 0;
    int ok = (ftruncate(fileno(f), (off_t)size) == 0);
    fclose(f);
    return ok;
}

int main(void)
{
    /* --- a private movie cache for this run ----------------------------- */
    const char* tmp = getenv("TMPDIR");
    if (!tmp || !*tmp) tmp = "/tmp";
    char dir[512];
    snprintf(dir, sizeof dir, "%s/ps3recomp_movieio_XXXXXX", tmp);
    if (!mkdtemp(dir)) { printf("[FAIL] mkdtemp: %s\n", dir); return 1; }
    setenv("PS3_MOVIE_CACHE", dir, 1);
    /* Exercise the periodic-trace thread spawn (pthread_create on POSIX,
     * CreateThread on Win32). It only prints every 5 s, so this run just
     * proves starting it is harmless -- it must not deadlock or crash. */
    setenv("PS3_TRACE_ASSET", "1", 1);

    /* Task 5 (bringup-instrumentacao-fase0): PS3_TRACE_FIOS on for the whole
     * run, set before any movie_io_* call (ps3_trace_on() caches its gate on
     * first use). stderr is freopen'd to a plain file inside the same cache
     * dir so the canonical [PS3T] lines (and the module's own existing
     * fprintf(stderr,...) diagnostics) can be read back and asserted on --
     * see ftell_stderr()/stderr_range_contains() above. */
    setenv("PS3_TRACE_FIOS", "1", 1);
    char tracelog[700];
    snprintf(tracelog, sizeof tracelog, "%s/trace.log", dir);
    if (!freopen(tracelog, "w+", stderr)) {
        printf("[FAIL] freopen stderr -> %s\n", tracelog);
        return 1;
    }

    char p_asset[640], p_lgl[640], p_perm[640], p_movie[640];
    snprintf(p_asset, sizeof p_asset, "%s/Asset01.bin",     dir);
    snprintf(p_lgl,   sizeof p_lgl,   "%s/R_LglScA.wad_ps3", dir);
    snprintf(p_perm,  sizeof p_perm,  "%s/R_PermA.wad_ps3",  dir);
    snprintf(p_movie, sizeof p_movie, "%s/SmLogo_v2.m2v",    dir);

    const size_t ASSET_N = 4096;
    if (!write_pattern_file(p_asset, ASSET_N)) { printf("[FAIL] write %s\n", p_asset); return 1; }
    if (!write_pattern_file(p_lgl, 256))       { printf("[FAIL] write %s\n", p_lgl);   return 1; }
    if (!write_pattern_file(p_movie, 64))      { printf("[FAIL] write %s\n", p_movie); return 1; }

    unsigned char buf[512];

    /* --- 1. open ------------------------------------------------------- */
    long mark_asset = ftell_stderr();
    unsigned size = 0;
    unsigned fd = movie_io_open("/dev_bdvd/PS3_GAME/USRDIR/Asset01.bin", &size);
    CHECK(fd != 0);
    CHECK(movie_io_is(fd) == 1);
    CHECK_EQ_U64(size, ASSET_N);
    /* Task 5: FIOS OP_BEGIN, formato dos campos + wad=0 para um ficheiro
     * comum (nao .wad_ps3). */
    CHECK(stderr_range_contains(mark_asset, "| FIOS | OP_BEGIN | op="));
    CHECK(stderr_range_contains(mark_asset, "path=/dev_bdvd/PS3_GAME/USRDIR/Asset01.bin"));
    CHECK(stderr_range_contains(mark_asset, "state=open wad=0"));

    if (fd == 0) {
        printf("movie_io: open returned 0 -- the host data path is not "
               "implemented on this platform\n");
        printf("Results: %d check(s) failed\n", g_fail ? g_fail : 1);
        return 1;
    }

    /* --- 2. stat without opening --------------------------------------- */
    CHECK_EQ_U64(movie_io_stat("/dev_bdvd/PS3_GAME/USRDIR/Asset01.bin"), ASSET_N);
    CHECK_EQ_U64(movie_io_stat("/dev_bdvd/PS3_GAME/USRDIR/NoSuchAsset.bin"), 0);

    /* --- 3. pread at an offset ----------------------------------------- */
    long mark_read = ftell_stderr();
    memset(buf, 0, sizeof buf);
    CHECK_EQ_U64(movie_io_pread(fd, buf, 100, 1000), 100);
    for (size_t i = 0; i < 100; i++) CHECK(buf[i] == pat(1000 + i));
    /* Task 5: FIOS READ, formato dos campos (off=/len=/buf=/state=). */
    CHECK(stderr_range_contains(mark_read, "| FIOS | READ | op="));
    CHECK(stderr_range_contains(mark_read, "off=1000 len=100"));
    CHECK(stderr_range_contains(mark_read, "state=ok"));

    /* A pread past EOF is short, not an error. */
    long mark_partial = ftell_stderr();
    CHECK_EQ_U64(movie_io_pread(fd, buf, 512, (unsigned)ASSET_N - 10), 10);
    /* Task 5: FIOS PARTIAL -- pedido de 512, satisfeito so' 10 (fim do
     * ficheiro), got=10 no <data>. */
    CHECK(stderr_range_contains(mark_partial, "| FIOS | PARTIAL | op="));
    CHECK(stderr_range_contains(mark_partial, "state=short got=10"));

    /* --- 4/5. seek + read_cur share the cursor -------------------------- */
    CHECK_EQ_U64(movie_io_seek(fd, 2048, 0 /*SET*/), 2048);
    long mark_read_cur = ftell_stderr();
    memset(buf, 0, sizeof buf);
    CHECK_EQ_U64(movie_io_read_cur(fd, buf, 64), 64);
    for (size_t i = 0; i < 64; i++) CHECK(buf[i] == pat(2048 + i));
    /* Task 5: FIOS READ via o outro ponto de entrada (cursor implicito). */
    CHECK(stderr_range_contains(mark_read_cur, "| FIOS | READ | op="));
    CHECK(stderr_range_contains(mark_read_cur, "off=2048 len=64"));
    /* read_cur advanced the cursor by exactly what it returned */
    CHECK_EQ_U64(movie_io_seek(fd, 0, 1 /*CUR*/), 2048 + 64);
    /* and seek(END) reports the size */
    CHECK_EQ_U64(movie_io_seek(fd, 0, 2 /*END*/), ASSET_N);

    /* --- 6. extensionless guest name -> .wad_ps3 fallback --------------- */
    long mark_wad = ftell_stderr();
    unsigned lgl_size = 0;
    unsigned fd_lgl = movie_io_open("/gow2/R_LglScA", &lgl_size);
    CHECK(fd_lgl != 0);
    CHECK_EQ_U64(lgl_size, 256);
    CHECK_EQ_U64(movie_io_stat("/gow2/R_LglScA"), 256);
    /* Task 5: FIOS OP_BEGIN wad=1 -- o discriminador obrigatorio do brief
     * ("WADs abertos" e' metrica do baseline Fase 0). */
    CHECK(stderr_range_contains(mark_wad, "| FIOS | OP_BEGIN | op="));
    CHECK(stderr_range_contains(mark_wad, "path=/gow2/R_LglScA"));
    CHECK(stderr_range_contains(mark_wad, "wad=1"));

    /* Task 5: has_suffix_ci() (movie_hle.c) -- um ficheiro cujo sufixo real e'
     * ".WAD_PS3" (maiusculas) tem de continuar a marcar wad=1. guest_path ja'
     * traz a extensao exacta para o candidato primario (cand[0]) bater sem
     * depender do rescan case-insensitive POSIX -- exercita has_suffix_ci em
     * qualquer filesystem, sensivel a maiusculas ou nao. Achado de review da
     * Task 2, decidido corrigir nesta task (ver relatorio). */
    char p_upper[640];
    snprintf(p_upper, sizeof p_upper, "%s/R_UpperCase.WAD_PS3", dir);
    if (!write_pattern_file(p_upper, 32)) { printf("[FAIL] write %s\n", p_upper); return 1; }
    long mark_upper = ftell_stderr();
    unsigned upper_size = 0;
    unsigned fd_upper = movie_io_open("/gow2/R_UpperCase.WAD_PS3", &upper_size);
    CHECK(fd_upper != 0);
    CHECK(stderr_range_contains(mark_upper, "wad=1"));
    movie_io_close(fd_upper);
    remove(p_upper);
    /* .m2v side of the same fallback */
    unsigned m2v_size = 0;
    unsigned fd_m2v = movie_io_open("/_movies/SmLogo_v2", &m2v_size);
    CHECK(fd_m2v != 0);
    CHECK_EQ_U64(m2v_size, 64);

    /* --- 7. uncached path falls through to the guest io ----------------- */
    unsigned miss_size = 0xDEADu;
    CHECK_EQ_U64(movie_io_open("/gow2/NotInTheCache", &miss_size), 0);

    /* --- 9. the slot table is finite ------------------------------------ */
    unsigned fd4 = movie_io_open("/gow2/Asset01.bin", NULL);   /* 4th slot */
    CHECK(fd4 != 0);
    CHECK_EQ_U64(movie_io_open("/gow2/Asset01.bin", NULL), 0); /* 5th: none left */
    movie_io_close(fd4);
    unsigned fd4b = movie_io_open("/gow2/Asset01.bin", NULL);  /* close recycled it */
    CHECK(fd4b != 0);
    movie_io_close(fd4b);
    movie_io_close(fd_lgl);
    movie_io_close(fd_m2v);

    /* --- 8. bogus and closed FDs ---------------------------------------- */
    CHECK(movie_io_is(0x12340001u) == 0);
    CHECK_EQ_U64(movie_io_pread(0x12340001u, buf, 16, 0), 0);
    CHECK_EQ_U64(movie_io_seek(0x12340001u, 0, 0), (unsigned long long)-1LL);
    CHECK_EQ_U64(movie_io_pread(fd, NULL, 16, 0), 0);

    long mark_close = ftell_stderr();
    movie_io_close(fd);
    /* Task 5: FIOS OP_END, formato dos campos (op=/path=/len=/state=). */
    CHECK(stderr_range_contains(mark_close, "| FIOS | OP_END | op="));
    CHECK(stderr_range_contains(mark_close, "state=closed"));
    CHECK_EQ_U64(movie_io_pread(fd, buf, 16, 0), 0);   /* slot is empty now */
    CHECK_EQ_U64(movie_io_read_cur(fd, buf, 16), 0);
    CHECK_EQ_U64(movie_io_seek(fd, 0, 0), (unsigned long long)-1LL);
    movie_io_close(fd);                                 /* double close: no-op */

    /* --- 12. two distinct .m2v in one process; identity follows the open -- */
    {
        char p_a[640], p_b[640];
        snprintf(p_a, sizeof p_a, "%s/SmLogo_v2.m2v", dir);
        snprintf(p_b, sizeof p_b, "%s/introhud.m2v", dir);
        if (!write_pattern_file(p_b, 96)) { printf("[FAIL] write %s\n", p_b); return 1; }
        unsigned sz_a = 0, sz_b = 0;
        unsigned fd_a = movie_io_open("/_movies/SmLogo_v2", &sz_a);
        CHECK(fd_a != 0);
        CHECK_EQ_U64(sz_a, 64);
        CHECK(movie_hle_cache_path() != NULL);
        CHECK(strstr(movie_hle_cache_path(), "SmLogo_v2.m2v") != NULL);
        unsigned fd_b = movie_io_open("/_movies/introhud", &sz_b);
        CHECK(fd_b != 0);
        CHECK_EQ_U64(sz_b, 96);
        CHECK(movie_hle_cache_path() != NULL);
        CHECK(strstr(movie_hle_cache_path(), "introhud.m2v") != NULL);
        CHECK(strstr(movie_hle_cache_path(), "SmLogo_v2.m2v") == NULL);
        /* After intro latch, in-game StartSeq must drop SmLogo even if that
         * is the current cache path — then pick introhud if it is cached. */
        movie_hle_set_cache_path(p_a);
        CHECK(strstr(movie_hle_cache_path(), "SmLogo_v2.m2v") != NULL);
        movie_hle_begin_ingame_sequence();
        CHECK(movie_hle_cache_path() != NULL);
        CHECK(strstr(movie_hle_cache_path(), "introhud.m2v") != NULL);
        /* A later sequence keeps its own file and does not put introhud
         * or the logo back as the current picture. */
        {
            char p_rhodes[640];
            snprintf(p_rhodes, sizeof p_rhodes, "%s/rhodes_return.m2v", dir);
            movie_hle_set_cache_path(p_rhodes);
            movie_hle_begin_ingame_sequence();
            CHECK(movie_hle_cache_path() != NULL);
            CHECK(strstr(movie_hle_cache_path(), "rhodes_return.m2v") != NULL);
            CHECK(strstr(movie_hle_cache_path(), "introhud.m2v") == NULL);
            movie_hle_set_cache_path(p_a);
            movie_hle_begin_ingame_sequence();
            CHECK(movie_hle_cache_path() == NULL);
        }
        /* Logos arm only for the intro. introhud is index 1. */
        movie_clock_note_stale(0);
        movie_clock_note(1, 0);
        printf("  movie_clock_arms_logos index 0 -> %d\n", movie_clock_arms_logos());
        CHECK(movie_clock_arms_logos() == 1);
        movie_clock_note(1, 1);
        printf("  movie_clock_arms_logos index 1 -> %d\n", movie_clock_arms_logos());
        CHECK(movie_clock_arms_logos() == 0);
        movie_clock_note(1, 2);
        CHECK(movie_clock_arms_logos() == 0);
        movie_clock_note_stale(1);
        movie_clock_note(1, 0);
        CHECK(movie_clock_arms_logos() == 0);
        movie_io_close(fd_a);
        movie_io_close(fd_b);
        unsigned miss_m = 0xDEADu;
        CHECK_EQ_U64(movie_io_open("/_movies/rhodesreturn", &miss_m), 0);
        CHECK(movie_hle_cache_path() == NULL);
        remove(p_b);
        (void)p_a;
    }

    /* --- 10. the post-WAD gate: R_PermA read to completion -------------- */
    const unsigned long long PERM_N = 20000000ull;
    if (!write_sparse_file(p_perm, PERM_N)) { printf("[FAIL] write %s\n", p_perm); return 1; }

    CHECK(g_ps3_rperma_full == 0);
    unsigned perm_size = 0;
    unsigned fd_perm = movie_io_open("/gow2/R_PermA", &perm_size);
    CHECK(fd_perm != 0);
    CHECK_EQ_U64(perm_size, PERM_N);

    if (fd_perm) {
        enum { CHUNK = 1000000 };
        void* chunk = malloc(CHUNK);
        CHECK(chunk != NULL);
        unsigned long long done = 0;
        while (chunk && done < PERM_N) {
            unsigned got = movie_io_pread(fd_perm, chunk, CHUNK, (unsigned)done);
            if (!got) break;
            done += got;
        }
        free(chunk);
        CHECK_EQ_U64(done, PERM_N);
        /* Flagged from pread, while the slot is still open -- the WAD state
         * machine polls it before close. */
        CHECK(g_ps3_rperma_full == 1);
        movie_io_close(fd_perm);
        CHECK(g_ps3_rperma_full == 1);
    }

    /* --- cleanup -------------------------------------------------------- */
    remove(p_asset); remove(p_lgl); remove(p_perm); remove(p_movie);
    remove(tracelog);
    rmdir(dir);

    if (g_fail) {
        printf("Results: %d check(s) failed\n", g_fail);
        return 1;
    }
    printf("movie_io: open/stat/pread/seek/read_cur/close + R_PermA gate + "
           "FIOS trace (wad=1/0, READ/PARTIAL/OP_END) OK\n");
    return 0;
}
