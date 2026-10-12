/*
 * ps3recomp - host-side movie HLE
 *
 * TWO INDEPENDENT HALVES live in this file, with different portability. Keep
 * the split in mind before adding anything:
 *
 *  1. movie_hle_*  -- the VIDEO OVERLAY + audio (PS3_MOVIE_HLE=1).
 *     Windows: ffmpeg.exe → RGBA → D3D12 + winmm PlaySound.
 *     macOS: AVAssetReader (BGRA) → Metal overlay + AVAudioPlayer (.wav);
 *            see movie_vt_metal.m (M0, no ffmpeg). Requires PS3_RSX_BACKEND=metal
 *            for visible frames (SDL null has no movie blit).
 *     Other POSIX: stubs (guest path only).
 *     Opt-in via PS3_MOVIE_HLE=1.
 *
 *  2. movie_io_*   -- the host-backed DATA PATH (PS3_MOVIE_IO=1). PORTABLE:
 *     plain stdio + a mutex + one background trace thread, no video or audio
 *     backend involved. It runs on Windows and on POSIX (macOS/Linux) alike.
 *     Without it the recompiled game gets nothing from the movie cache, so the
 *     WAD stream never completes and g_ps3_rperma_full never opens the
 *     post-WAD ICGLdr gate. See the section header below.
 */

#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>   /* PlaySound (link: -lwinmm) */
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>   /* PS3_STAT64 (movie_io_stat) */

#ifndef _WIN32
#include <pthread.h>    /* movie io lock + trace thread */
#include <time.h>       /* nanosleep */
#include <unistd.h>
#include <dirent.h>     /* case-insensitive cache rescan (movie_io_resolve) */
#include <strings.h>    /* strcasecmp */
#endif

/* _fseeki64/_ftelli64 -> fseeko/ftello and _stat64 -> stat on POSIX. */
#include "ps3emu/host_compat.h"
#include "ps3emu/ps3_mods.h"
#include "ps3_checkpoint.h"
#include "ps3_trace.h"   /* Task 5 (bringup-instrumentacao-fase0): subsistema FIOS */
#include "movie_clock.h"
#include "movie_io_status.h" /* lock-free WAD snapshot for the runtime overlay */
#include "rsx_overlay_log.h"  /* WAD/movie events also reach the overlay Logs tab */

/* winport: mingw */
extern unsigned char* vm_base;
extern uint32_t       ppu_vm_size;
extern void ps3_watch_store_bulk(uint32_t ea, uint32_t len, const char* who);

/* Host cache path of the movie the game most recently opened. Written by
 * movie_hle_from_guest (Windows overlay) and by movie_io_open on every host;
 * cellVdec's HLE decoder reads the same file to produce real frames. Empty
 * until a movie is opened. Shared by both halves, hence defined out here. */
static char s_movie_cache_path[1024];
const char* movie_hle_cache_path(void) { return s_movie_cache_path[0] ? s_movie_cache_path : 0; }

void movie_hle_set_cache_path(const char* host)
{
    if (!host || !host[0]) {
        s_movie_cache_path[0] = 0;
        return;
    }
    strncpy(s_movie_cache_path, host, sizeof s_movie_cache_path - 1);
    s_movie_cache_path[sizeof s_movie_cache_path - 1] = 0;
}

/* Open sequence the skip and the logo queue consult. Unknown until the
 * sampler or an in-game StartSeq says otherwise: a playing host video is
 * then the current sequence, which is how a skip completes the intro. */
static int s_clock_known;
static int s_clock_open;
static int s_clock_index;
static int s_clock_stale;

void movie_clock_note(int sequence_open, int ingame_index)
{
    if (ingame_index < 0) ingame_index = 0;
    s_clock_known = 1;
    s_clock_open = sequence_open ? 1 : 0;
    s_clock_index = ingame_index;
}

void movie_clock_note_stale(int on)
{
    s_clock_stale = on ? 1 : 0;
}

int movie_clock_sequence_open(void)
{
    if (!s_clock_known) return 1;
    return s_clock_open;
}

int movie_clock_ingame_index(void)
{
    if (!s_clock_known) return 0;
    return s_clock_index;
}

int movie_clock_arms_logos(void)
{
    /* Index 0 is the Santa Monica intro. introhud is index 1 and must not
     * start the company logos, neither when it ends nor when it is skipped. */
    return movie_end_arms_boot_logos(s_clock_index != 0, s_clock_stale);
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak)) void movie_done_timebased_reset(void) {}
__attribute__((weak)) void pad_autostart_note_cutscene(int on) { (void)on; }
#else
static void movie_done_timebased_reset(void) {}
static void pad_autostart_note_cutscene(int on) { (void)on; }
#endif

static int movie_host_is_intro_logo(const char* host)
{
    const char* b;
    if (!host || !host[0]) return 0;
    b = strrchr(host, '/');
    b = b ? b + 1 : host;
#ifdef _WIN32
    {
        const char* b2 = strrchr(b, '\\');
        if (b2) b = b2 + 1;
    }
#endif
    return (strncmp(b, "SmLogo", 6) == 0 || strncmp(b, "smlogo", 6) == 0
            || strncmp(b, "SMLOGO", 6) == 0);
}

/* winport v30-bench: skip request for the Windows overlay player (see engine_fixes_v30_bench.py) */
static volatile long s_wp_skip_req = 0;
static volatile long s_wp_pause_req = 0;   /* winport v47-cutscene-ui: SELECT pause (cellPad.c) */
static void movie_on_new_m2v(const char* host)
{
    movie_hle_set_cache_path(host);
    movie_done_timebased_reset();
    if (!movie_host_is_intro_logo(host))
        pad_autostart_note_cutscene(1);
}

void movie_hle_from_guest(const char* path_or_guest);
void movie_hle_stop(void);
#ifdef __APPLE__
void movie_vt_clear_overlay_done(void);
#endif

/* StartSeq >= 3 (post Santa Monica intro). The first in-game sequence may
 * take introhud from the cache when the latched file is still the logo.
 * A later sequence keeps the file the guest opened and does not put the
 * logo or introhud back up as the picture. */
static char s_pending_ingame[1024];

static int movie_hle_start_at_seq(void)
{
    static int v = -1;
    /* Default: hold the picture for Play (st620 11). PS3_MOVIE_START_AT=seq
     * goes back to starting it at StartSeq. */
    if (v < 0) { const char* e = getenv("PS3_MOVIE_START_AT"); v = (e && e[0] == 's') ? 1 : 0; }
    return v;
}

/* ---- winport v48-movies (engine_fixes_v48_movies.py): WINPORT_ALL_MOVIES=1 -- which movie does the game stream? ----
 * movie_cache/_psarc_movies.txt (winport/tools/extract_movies_v48.py) = "<name> <lo> <hi>" per /_movies/*.m2v member
 * of gow2.psarc (compressed byte range). ppu_fs.cpp reports every cellFsRead on the psarc; the bytes are counted per
 * member since the in-game StartSeq, and at Play the most-read member is the overlay picture. */
#define WP48_PENDING "\001wp48-pick-at-play"
static char s_wp71_fallback[1024];   /* winport v71-moviepick: opener used at Play if the game read no known movie */
static int wp71_on(void)
{ static int v = -1; if (v < 0) { const char* e = getenv("WINPORT_MOVIE_PICK_FIRST"); v = (e && e[0] == '1') ? 1 : 0;
    if (v) { fprintf(stderr, "[MOVIE71] first in-game movie also chosen from the psarc reads at Play\n"); fflush(stderr); } }
  return v; }
#ifdef _WIN32
typedef struct { char name[64]; unsigned long long lo, hi; volatile unsigned long long bytes; } wp48_member;
static wp48_member s_wp48_m[128];
static int s_wp48_n = -1;   /* -1 = index not loaded yet */
static int wp48_on(void)
{
    static int on = -1;
    if (on < 0) {
        const char* e = getenv("WINPORT_ALL_MOVIES");
        on = (e && e[0] == '1') ? 1 : 0;
        fprintf(stderr, "[MOVIE48] every in-game movie on the host overlay: %s\n", on ? "ON" : "off");
        fflush(stderr);
    }
    return on;
}
static void wp48_load_index(void)
{
    char p[1100], line[256];
    const char* cache;
    FILE* f;
    if (s_wp48_n >= 0) return;
    s_wp48_n = 0;
    cache = getenv("PS3_MOVIE_CACHE");
    if (!cache || !*cache) cache = "../movie_cache";
    snprintf(p, sizeof p, "%s/_psarc_movies.txt", cache);
    f = fopen(p, "r");
    if (!f) {
        fprintf(stderr, "[MOVIE48] no index '%s' (run winport/tools/extract_movies_v48.py) -> nothing to do\n", p);
        fflush(stderr);
        return;
    }
    while (s_wp48_n < 128 && fgets(line, sizeof line, f)) {
        char nm[64]; unsigned long long lo = 0, hi = 0;
        if (line[0] == '#') continue;
        if (sscanf(line, "%63s %llu %llu", nm, &lo, &hi) == 3 && hi > lo) {
            snprintf(s_wp48_m[s_wp48_n].name, sizeof s_wp48_m[0].name, "%s", nm);
            s_wp48_m[s_wp48_n].lo = lo; s_wp48_m[s_wp48_n].hi = hi; s_wp48_m[s_wp48_n].bytes = 0;
            s_wp48_n++;
        }
    }
    fclose(f);
    fprintf(stderr, "[MOVIE48] index: %d movies (%s)\n", s_wp48_n, p);
    fflush(stderr);
}
/* ---- winport v82b-strm-diag (engine_fixes_v82b_strm_diag.py): WINPORT_STRM82=<psarc index> ---- */
typedef struct { unsigned long long lo, hi; char name[56]; unsigned long long sec_bytes, tot; ULONGLONG last; } wp82_mem;
static wp82_mem* s82_m; static int s82_n = -1; static SRWLOCK s82_lk = SRWLOCK_INIT;
static ULONGLONG s82_t0, s82_tl; static unsigned long long s82_reads, s82_bytes;
static int wp82_snd(const char* nm)
{
    return strstr(nm, "_sound") || strstr(nm, ".vpk") || strstr(nm, ".vag");
}
static void wp82_load(void)
{
    const char* p = getenv("WINPORT_STRM82");
    char line[512]; FILE* f; int cap = 0;
    s82_n = 0;
    if (!p || !*p || p[0] == '0') return;
    f = fopen(p, "r");
    if (!f) { fprintf(stderr, "[STRM82] cannot open index '%s'\n", p); fflush(stderr); return; }
    while (fgets(line, sizeof line, f)) {
        char nm[400]; unsigned long long lo = 0, hi = 0;
        if (line[0] == '#' || sscanf(line, "%399s %llu %llu", nm, &lo, &hi) != 3 || hi <= lo) continue;
        if (s82_n == cap) { cap = cap ? cap * 2 : 1024; s82_m = (wp82_mem*)realloc(s82_m, (size_t)cap * sizeof *s82_m); }
        {   const char* b = strrchr(nm, '/'); b = b ? b + 1 : nm;
            wp82_mem* m = &s82_m[s82_n++]; memset(m, 0, sizeof *m); m->lo = lo; m->hi = hi;
            snprintf(m->name, sizeof m->name, "%s%s", strstr(nm, "_sound") ? "" : "~", b); }
    }
    fclose(f);
    /* index is in psarc order = sorted by lo (insertion sort just in case) */
    for (int i = 1; i < s82_n; i++) { wp82_mem t = s82_m[i]; int j = i - 1;
        while (j >= 0 && s82_m[j].lo > t.lo) { s82_m[j + 1] = s82_m[j]; j--; } s82_m[j + 1] = t; }
    fprintf(stderr, "[STRM82] psarc stream-read diagnostics ON: %d members (%s)\n", s82_n, p); fflush(stderr);
}
static void wp82_strm_read(unsigned long long pos, unsigned n)
{
    static volatile LONG once = 0;
    const unsigned long long end = pos + n;
    ULONGLONG now;
    if (s82_n < 0) { if (InterlockedCompareExchange(&once, 1, 0) == 0) wp82_load(); else while (s82_n < 0) Sleep(0); }
    if (s82_n <= 0 || !n) return;
    now = GetTickCount64();
    AcquireSRWLockExclusive(&s82_lk);
    if (!s82_t0) { s82_t0 = now; s82_tl = now; }
    s82_reads++; s82_bytes += n;
    {   int lo = 0, hi = s82_n - 1, i = -1;              /* last member with m.lo <= pos */
        while (lo <= hi) { int mid = (lo + hi) / 2; if (s82_m[mid].lo <= pos) { i = mid; lo = mid + 1; } else hi = mid - 1; }
        for (int k = (i < 0 ? 0 : i); k < s82_n && s82_m[k].lo < end; k++) {
            wp82_mem* m = &s82_m[k];
            if (m->hi <= pos) continue;
            {   const unsigned long long a = pos > m->lo ? pos : m->lo, b = end < m->hi ? end : m->hi;
                if (wp82_snd(m->name) && (!m->last || now - m->last > 10000)) {
                    fprintf(stderr, "[STRM82] t=%llu open %s (off 0x%llX of 0x%llX)%s\n", (unsigned long long)(now - s82_t0),
                            m->name, a - m->lo, m->hi - m->lo, m->last ? " again after >10 s" : "");
                    fflush(stderr);
                }
                m->sec_bytes += b - a; m->tot += b - a; m->last = now; }
        }
    }
    if (now - s82_tl >= 1000) {
        char buf[900]; int L = 0;
        L += snprintf(buf + L, sizeof buf - L, "[STRM82] t=%llu reads=%llu KB=%llu |", (unsigned long long)(now - s82_t0),
                      s82_reads, s82_bytes / 1024ull);
        for (int shown = 0; shown < 8; shown++) {                      /* top 8 sound members of this second */
            int best = -1;
            for (int k = 0; k < s82_n; k++)
                if (s82_m[k].sec_bytes && wp82_snd(s82_m[k].name) && (best < 0 || s82_m[k].sec_bytes > s82_m[best].sec_bytes)) best = k;
            if (best < 0) break;
            if (L < (int)sizeof buf - 80)
                L += snprintf(buf + L, sizeof buf - L, " %s:%llu", s82_m[best].name, (s82_m[best].sec_bytes + 1023) / 1024ull);
            s82_m[best].sec_bytes = 0;
        }
        for (int k = 0; k < s82_n; k++) s82_m[k].sec_bytes = 0;
        fprintf(stderr, "%s\n", buf); fflush(stderr);
        s82_reads = 0; s82_bytes = 0; s82_tl = now;
    }
    ReleaseSRWLockExclusive(&s82_lk);
}
/* ---- end winport v82b-strm-diag ---- */
/* ---- winport v82b-snd-diag (engine_fixes_v82b_snd_diag.py): WINPORT_SND82=1 snd_stream channel states ---- */
static unsigned wp82s_rd(unsigned a) { const unsigned char* p = vm_base + a; return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) | ((unsigned)p[2] << 8) | p[3]; }
static DWORD WINAPI wp82s_thread(LPVOID arg)
{
    static const unsigned off[] = { 0x134, 0x1A0, 0x1AC, 0x1B0, 0x1B4, 0x1B8, 0x1BC, 0x1C0, 0x1C4, 0x1F0 };
    enum { NF = sizeof off / sizeof off[0] };
    unsigned prev[8][NF]; int have = 0, nmoff[8]; ULONGLONG st1[8] = {0}; int stuck_logged[8] = {0};
    const ULONGLONG t0 = GetTickCount64();
    (void)arg;
    for (int i = 0; i < 8; i++) nmoff[i] = -1;
    for (;;) {
        Sleep(20);
        unsigned p1 = wp82s_rd(0x541178u + 0x7ACu), base;
        if (!p1 || p1 >= 0xF0000000u) continue;
        {   MEMORY_BASIC_INFORMATION m1;   /* v82b2 guard: p1 must be committed */
            if (!VirtualQuery(vm_base + p1, &m1, sizeof m1) || m1.State != MEM_COMMIT) continue; }
        base = wp82s_rd(p1);
        if (!base || base >= 0xF0000000u) continue;
        {   /* the guest address space is reserved, not all committed: never touch an uncommitted page (v82b2 guard) */
            MEMORY_BASIC_INFORMATION mbi; unsigned a = base; int ok = 1;
            while (a < base + 8u * 0x35A8u) {
                if (!VirtualQuery(vm_base + a, &mbi, sizeof mbi) || mbi.State != MEM_COMMIT) { ok = 0; break; }
                a = (unsigned)((unsigned char*)mbi.BaseAddress + mbi.RegionSize - vm_base);
            }
            if (!ok) continue;
        }
        for (int i = 0; i < 8; i++) {
            const unsigned s = base + (unsigned)i * 0x35A8u;
            unsigned cur[NF]; char line[700]; int L = 0, ch = 0;
            for (int k = 0; k < NF; k++) cur[k] = wp82s_rd(s + off[k]);
            if (nmoff[i] < 0 || (cur[3] != (have ? prev[i][3] : ~0u))) {      /* (re)locate the stream path in the struct */
                nmoff[i] = -1;
                for (unsigned o = 0; o + 24 < 0x35A8u; o++)
                    if (!memcmp(vm_base + s + o, "/wad/_", 6) && !_strnicmp((const char*)vm_base + s + o + 6, "Sound/dvd", 9)) { nmoff[i] = (int)o; break; }
            }
            L += snprintf(line + L, sizeof line - L, "[SND82] t=%llu ch%d", (unsigned long long)(GetTickCount64() - t0), i);
            for (int k = 0; k < NF; k++)
                if (!have || cur[k] != prev[i][k]) { ch = 1; L += snprintf(line + L, sizeof line - L, " %X=%X->%X", off[k], have ? prev[i][k] : 0u, cur[k]); }
            if (ch) {
                char nm[64] = "";
                if (nmoff[i] >= 0) { const char* q = (const char*)vm_base + s + nmoff[i] + 24; int n = 0;
                    while (n < 60 && q[n] >= 32 && q[n] < 127) { nm[n] = q[n]; n++; } nm[n] = 0; }
                snprintf(line + L, sizeof line - L, " | %s", nm);
                fprintf(stderr, "%s\n", line); fflush(stderr);
            }
            if (cur[3] == 1 && 0) {   /* v82b3: +0x1B0 == 1 is the normal idle (stopped) state -> no 'stuck' report */
                if (!st1[i]) st1[i] = GetTickCount64();
                else if (GetTickCount64() - st1[i] > 3000 && !stuck_logged[i]) {
                    stuck_logged[i] = 1;
                    fprintf(stderr, "[SND82] t=%llu ch%d STUCK in stop > 3 s (134=%X 1B4=%X 1B8=%X 1BC=%X 1C0=%X 1C4=%X)\n",
                            (unsigned long long)(GetTickCount64() - t0), i, cur[0], cur[4], cur[5], cur[6], cur[7], cur[8]);
                    fflush(stderr);
                }
            } else { st1[i] = 0; stuck_logged[i] = 0; }
            memcpy(prev[i], cur, sizeof cur);
        }
        have = 1;
    }
    return 0;
}
static void wp82s_start(void)
{
    static volatile LONG once = 0;
    if (InterlockedCompareExchange(&once, 1, 0) != 0) return;
    {   const char* e = getenv("WINPORT_SND82");
        if (!(e && e[0] == '1')) return; }
    fprintf(stderr, "[SND82] snd_stream channel state diagnostics ON\n"); fflush(stderr);
    CloseHandle(CreateThread(NULL, 0, wp82s_thread, NULL, 0, NULL));
}
/* ---- end winport v82b-snd-diag ---- */
/* ppu_fs.cpp cellFsRead on a *.psarc fd: pos = file position before the read, n = bytes asked */
void movie_wp48_psarc_read(unsigned long long pos, unsigned n)
{
    static int nlog = 0;
    const unsigned long long end = pos + n;
    { extern void wp50_boot_diag_x(unsigned); wp50_boot_diag_x(n); }   /* winport v50-movie-sync */
    wp82_strm_read(pos, n);   /* winport v82b-strm-diag */
    wp82s_start();   /* winport v82b-snd-diag */
    { extern void wp93_start(void); wp93_start(); }   /* winport v93-movie */
    if (!n || !wp48_on()) return;
    wp48_load_index();
    for (int i = 0; i < s_wp48_n; i++) {
        wp48_member* m = &s_wp48_m[i];
        if (end <= m->lo || pos >= m->hi) continue;
        if (!m->bytes && nlog < 300) {
            nlog++;
            fprintf(stderr, "[MOVIE48] the game reads movie '%s' from gow2.psarc (off 0x%llX)\n", m->name, pos);
            fflush(stderr);
        }
        m->bytes += (end < m->hi ? end : m->hi) - (pos > m->lo ? pos : m->lo);
    }
}
static void wp48_seq_begin(void)
{
    wp48_load_index();
    for (int i = 0; i < s_wp48_n; i++) s_wp48_m[i].bytes = 0;
}
/* the movie the game streamed most since the StartSeq -> "/_movies/<name>.m2v" in out; NULL if none */
static const char* wp48_pick(char* out, size_t cap)
{
    int best = -1;
    for (int i = 0; i < s_wp48_n; i++)
        if (s_wp48_m[i].bytes && (best < 0 || s_wp48_m[i].bytes > s_wp48_m[best].bytes)) best = i;
    if (best < 0) return NULL;
    snprintf(out, cap, "/_movies/%s.m2v", s_wp48_m[best].name);
    fprintf(stderr, "[MOVIE48] Play: picture = '%s' (%llu KB of it read by the game since StartSeq)\n",
            out, s_wp48_m[best].bytes / 1024ull);
    fflush(stderr);
    return out;
}
#else
static int wp48_on(void) { return 0; }
static void wp48_seq_begin(void) {}
static const char* wp48_pick(char* out, size_t cap) { (void)out; (void)cap; return NULL; }
#endif
/* ---- end winport v48-movies ---- */

void movie_hle_begin_ingame_sequence(void)
{
    static int s_ingame_calls;
    const int later = s_ingame_calls > 0;
    const int index = s_ingame_calls + 1;
    const char* cur = s_movie_cache_path[0] ? s_movie_cache_path : NULL;
    const int logo = movie_host_is_intro_logo(cur);
    const int earlier = logo || (later && movie_clock_is_introhud(cur));
    char opener_buf[1024];
    const char* opener = NULL;
    const char* pick;

    s_ingame_calls++;
#ifdef _WIN32
    {   extern volatile long g_movie_overlay_done; extern uint32_t g_movie_eos_ea;   /* winport v93c-eos-reset */
        if (g_movie_overlay_done || g_movie_eos_ea) { fprintf(stderr, "[MV93] new in-game sequence: stale EOS state of the previous movie cleared (overlay_done=%ld eos_ea=0x%08X)\n", g_movie_overlay_done, g_movie_eos_ea); fflush(stderr); }
        g_movie_overlay_done = 0; g_movie_eos_ea = 0; }
#endif
    movie_clock_note(1, index);
    pad_autostart_note_cutscene(1);
    movie_done_timebased_reset();

    if (!later) {
        const char* cache = getenv("PS3_MOVIE_CACHE");
        struct stat st;
        if (!cache || !*cache) cache = "movie_cache";
        snprintf(opener_buf, sizeof opener_buf, "%s/introhud.m2v", cache);
        if (stat(opener_buf, &st) != 0 || !S_ISREG(st.st_mode))
            snprintf(opener_buf, sizeof opener_buf, "%s/IntroHud.m2v", cache);
        if (stat(opener_buf, &st) == 0 && S_ISREG(st.st_mode))
            opener = opener_buf;
    }

    pick = movie_ingame_picture(cur, earlier, opener, later);
    if (earlier) {
        fprintf(stderr, "[movie] drop earlier cache '%s' for in-game StartSeq\n",
                cur ? cur : "");
        fflush(stderr);
        if (later) movie_clock_note_stale(1);
        movie_hle_stop();
        s_movie_cache_path[0] = 0;
#ifdef __APPLE__
        movie_vt_clear_overlay_done();
#endif
        cur = NULL;
    }
    {   /* winport v74-movie-pick-always: a later movie never inherits the previous movie's file */
        static int on = -1; if (on < 0) { const char* e = getenv("WINPORT_MOVIE_PICK_ALWAYS"); on = (e && e[0] == '1') ? 1 : 0; }
        if (on && later && wp48_on()) {
            fprintf(stderr, "[MOVIE74] later sequence #%d: ignore current '%s' / rule pick '%s' -> pick at Play from the psarc reads\n",
                    index, cur ? cur : "", pick ? pick : "");
            fflush(stderr);
            if (cur) { movie_hle_stop(); s_movie_cache_path[0] = 0; cur = NULL; }
            pick = NULL;
        }
    }
    int wp71_defer = 0;   /* winport v71-moviepick */
    s_wp71_fallback[0] = 0;
    if (!later && wp48_on() && wp71_on()) {   /* winport v71-moviepick: first sequence: decide at Play too, opener = fallback */
        snprintf(s_wp71_fallback, sizeof s_wp71_fallback, "%s", pick ? pick : "");
        fprintf(stderr, "[MOVIE71] first in-game sequence: picture chosen at Play (fallback '%s')\n", s_wp71_fallback);
        fflush(stderr);
        pick = NULL; wp71_defer = 1;
    }
    if (!pick && (later || wp71_defer) && wp48_on()) {   /* winport v48-movies: decide at Play from the psarc reads */
        wp48_seq_begin();
        snprintf(s_pending_ingame, sizeof s_pending_ingame, "%s", WP48_PENDING);
        fprintf(stderr, "[MOVIE48] later in-game sequence: picture chosen at Play from the movie the game streams\n");
        fflush(stderr);
        return;
    }
    if (!pick) return;
    if (cur && strcmp(pick, cur) == 0) return;
    fprintf(stderr, "[movie] in-game picture -> '%s'\n", pick);
    fflush(stderr);
    movie_on_new_m2v(pick);
    /* StartSeq is where the game starts DECODING, while the in-engine scene
     * that leads into the video is still running (st620 3 -> 10, prebuffer).
     * The picture belongs on screen when the player reaches Play (st620 11),
     * which is also when its audio stream starts. Starting the overlay here
     * put the video ~15 s early over the model cutscene and ended it ~15 s
     * early. Held until movie_hle_start_pending_ingame() (PS3_MOVIE_START_AT=seq
     * restores the old start). */
    /* winport: in-game picture waits for Play */ /* boot_main.cpp calls movie_hle_start_pending_ingame() at st620 11 */
    if (movie_hle_start_at_seq()) {
        movie_hle_from_guest(pick);
        return;
    }
    snprintf(s_pending_ingame, sizeof s_pending_ingame, "%s", pick);
    fprintf(stderr, "[movie] in-game picture held until Play (st620 11)\n");
    fflush(stderr);
}

/* Called by the player-state sampler when st620 enters Play (11). */
int movie_hle_start_pending_ingame(void)
{
    char path[sizeof s_pending_ingame];
    if (!s_pending_ingame[0]) return 0;
    snprintf(path, sizeof path, "%s", s_pending_ingame);
    s_pending_ingame[0] = 0;
    if (strcmp(path, WP48_PENDING) == 0 && s_wp71_fallback[0] && !wp48_pick(path, sizeof path)) {   /* winport v71-moviepick */
        snprintf(path, sizeof path, "%s", s_wp71_fallback);
        fprintf(stderr, "[MOVIE71] Play: no known movie read since StartSeq -> opener fallback '%s'\n", path);
        fflush(stderr);
    }
    if (strcmp(path, WP48_PENDING) == 0 && !wp48_pick(path, sizeof path)) {   /* winport v48-movies */
        fprintf(stderr, "[MOVIE48] Play reached but the game read no known movie since StartSeq -> its own pictures\n");
        fflush(stderr);
        return 0;
    }
    fprintf(stderr, "[movie] Play reached: in-game picture starts now\n");
    fflush(stderr);
    movie_hle_from_guest(path);
    return 1;
}

/* ======================================================================== */
/* Half 1: video overlay + audio -- Windows only (see the file header).     */
/* ======================================================================== */
#ifdef _WIN32

/* Provided by rsx_d3d12_backend.c: hand over one RGBA frame (copied). */
extern void rsx_d3d12_movie_frame(const void* rgba, unsigned w, unsigned h);
extern void rsx_d3d12_movie_stop(void);
extern void rsx_d3d12_movie_playback_begin(void);

static volatile LONG s_movie_running = 0;
static HANDLE s_movie_thread = NULL;
static char   s_movie_path[1024];
static int    s_movie_loop = 1;      /* loop passes until movie_hle_stop */

/* Set to 1 once the overlay has rendered at least one full pass of the current
 * movie (i.e. the video reached its natural end). The boot-side tracer reads
 * this to know when to inject the movie player's EOS signal. */
volatile long g_movie_overlay_done = 0;
long movie_hle_overlay_done(void) { return g_movie_overlay_done; }

typedef struct { unsigned w, h; double fps; } movie_info;

/* The movie path is embedded into an ffmpeg/ffprobe command line run through
 * cmd.exe by _popen(), so a path containing a double-quote or a cmd
 * metacharacter could break out of the quoted argument and inject commands.
 * The path comes from the guest's cellFsOpen under our VFS root, but treat it
 * as untrusted: refuse to spawn a shell for any path with shell-significant
 * characters (a legitimate movie path never has them). */
static int path_is_shell_safe(const char* p)
{
    if (!p || !*p) return 0;
    for (const unsigned char* c = (const unsigned char*)p; *c; c++) {
        if (*c < 0x20) return 0;                 /* control chars / newlines */
        if (strchr("\"&|<>^%!`$;", (char)*c)) return 0;  /* cmd/shell metachars */
    }
    return 1;
}

/* ffprobe the stream geometry; fall back to 640x448@29.97 (GoW2 m2v) on error. */
static void probe_movie(const char* path, movie_info* mi)
{
    mi->w = 640; mi->h = 448; mi->fps = 29.97;
    if (!path_is_shell_safe(path)) return;      /* keep the fallback geometry */
    char cmd[1400];
    snprintf(cmd, sizeof cmd,
        "ffprobe -v error -select_streams v:0 -show_entries stream=width,height,r_frame_rate "
        "-of csv=p=0 \"%s\"", path);
    FILE* p = _popen(cmd, "r");
    if (!p) return;
    char line[256];
    if (fgets(line, sizeof line, p)) {
        unsigned w = 0, h = 0, num = 0, den = 1;
        if (sscanf(line, "%u,%u,%u/%u", &w, &h, &num, &den) >= 3 && w && h) {
            mi->w = w; mi->h = h;
            if (num && den) mi->fps = (double)num / (double)den;
        }
    }
    _pclose(p);
}

/* Companion audio: GoW2 ships each movie as .m2v + sibling .wav (plain PCM).
 * winmm PlaySound handles the format directly, asynchronously. */
static void movie_play_audio(const char* m2v_path)
{
    char wav[1024];
    size_t L = strlen(m2v_path);
    if (L < 4 || L >= sizeof wav) return;
    memcpy(wav, m2v_path, L + 1);
    memcpy(wav + L - 4, ".wav", 4);
    if (GetFileAttributesA(wav) == INVALID_FILE_ATTRIBUTES) return;
    PlaySoundA(wav, NULL, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
}

static void movie_stop_audio(void)
{
    PlaySoundA(NULL, NULL, 0);
}

/* winport v47-cutscene-ui: PlaySound cannot pause, so a resumed overlay movie restarts its companion .wav at the
 * resume point: the file is read into memory and a trimmed copy (same fmt chunk, data from the offset) is played
 * with SND_MEMORY. No .wav (introhud: its sound is the game's own) -> nothing to do. */
static unsigned char* s_wp47_wav = NULL;
static unsigned wp47_le32(const unsigned char* p)
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}
static void wp47_wav_resume(const char* m2v_path, double sec)
{
    char wav[1024];
    size_t L = strlen(m2v_path);
    if (L < 4 || L >= sizeof wav) return;
    memcpy(wav, m2v_path, L + 1);
    memcpy(wav + L - 4, ".wav", 4);
    FILE* f = fopen(wav, "rb");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char* src = (sz > 44 && sz < 512L * 1024L * 1024L) ? (unsigned char*)malloc((size_t)sz) : NULL;
    if (!src || fread(src, 1, (size_t)sz, f) != (size_t)sz) { fclose(f); free(src); return; }
    fclose(f);
    const unsigned char* fmt = NULL;
    unsigned fmt_len = 0;
    size_t data_off = 0, data_len = 0;
    if (memcmp(src, "RIFF", 4) == 0 && memcmp(src + 8, "WAVE", 4) == 0) {
        size_t p = 12;
        while (p + 8 <= (size_t)sz) {
            const unsigned cl = wp47_le32(src + p + 4);
            if (memcmp(src + p, "fmt ", 4) == 0 && p + 8 + (size_t)cl <= (size_t)sz) { fmt = src + p + 8; fmt_len = cl; }
            else if (memcmp(src + p, "data", 4) == 0) {
                data_off = p + 8; data_len = cl;
                if (data_off + data_len > (size_t)sz) data_len = (size_t)sz - data_off;
                break;
            }
            p += 8 + (size_t)cl + (cl & 1u);
        }
    }
    if (!fmt || fmt_len < 16 || !data_off) { free(src); return; }
    const unsigned rate  = wp47_le32(fmt + 8);                           /* nAvgBytesPerSec */
    const unsigned align = (unsigned)fmt[12] | ((unsigned)fmt[13] << 8);  /* nBlockAlign */
    size_t off = (size_t)(sec * (double)rate);
    if (align) off -= off % align;
    if (off >= data_len) { free(src); return; }                          /* the sound had already ended */
    const size_t rest = data_len - off, fl = fmt_len + (fmt_len & 1u);
    unsigned char* w = (unsigned char*)malloc(12 + 8 + fl + 8 + rest);
    if (!w) { free(src); return; }
    unsigned char* q = w;
#define WP47_PUT32(v) do { unsigned v_ = (unsigned)(v); q[0] = (unsigned char)v_; q[1] = (unsigned char)(v_ >> 8); \
                           q[2] = (unsigned char)(v_ >> 16); q[3] = (unsigned char)(v_ >> 24); q += 4; } while (0)
    memcpy(q, "RIFF", 4); q += 4; WP47_PUT32(4 + 8 + fl + 8 + rest); memcpy(q, "WAVE", 4); q += 4;
    memcpy(q, "fmt ", 4); q += 4; WP47_PUT32(fmt_len); memcpy(q, fmt, fmt_len); q += fmt_len;
    if (fmt_len & 1u) *q++ = 0;
    memcpy(q, "data", 4); q += 4; WP47_PUT32(rest); memcpy(q, src + data_off + off, rest);
#undef WP47_PUT32
    free(src);
    PlaySoundA(NULL, NULL, 0);               /* stop whatever plays before the old buffer is freed */
    free(s_wp47_wav);
    s_wp47_wav = w;
    PlaySoundA((LPCSTR)w, NULL, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
    fprintf(stderr, "[CSUI] movie .wav resumed at %.2f s\n", sec);
    fflush(stderr);
}

/* ---- winport v50-movie-sync (engine_fixes_v50_movie_sync.py): slave the overlay picture to the game's movie
 * audio clock (WINPORT_MOVIE_SYNC=1). See the script header for the guest addresses (from movie_eos_arm.c). ---- */
extern int ppu_guest_range_committed(uint32_t addr, uint32_t n);
extern int wp50_vdec_pictures(unsigned* consumed);   /* cellVdec.c (engine_fixes_v50_movie_sync.py) */
static int wp50_peek32(uint32_t ea, uint32_t* out)
{
    const unsigned char* p;
    if (!vm_base || !out || ea < 0x10000u || ea >= 0x4F000000u) return 0;
    if (!ppu_guest_range_committed(ea, 4)) return 0;
    p = vm_base + ea;
    *out = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
    return 1;
}
/* game's movie audio clock in samples (48 kHz): 1 = read; *vpos = SCREAM voice position (diag), 0xFFFFFFFF if none */
static int wp50_game_clock(uint32_t* samples, uint32_t* vpos)
{
    static uint32_t c_h = 0, c_sess = 0;
    uint32_t obj = 0, h = 0, table = 0, v = 0, voice = 0xFFFFFFFFu;
    *vpos = 0xFFFFFFFFu;
    if (!wp50_peek32(0x540054u, &obj) || !obj || obj >= 0x4F000000u) return 0;
    if (!wp50_peek32(obj + 0x720u, &h) || !h || h == 0xFFFFFFFFu) return 0;
    if (h != c_h || !c_sess) {
        c_h = h; c_sess = 0;
        if (!wp50_peek32(0x541178u - 0x394u, &table) || !table || table >= 0x4F000000u) return 0;
        for (uint32_t i = 0; i < 256u; i++) {
            uint32_t slot = table + i * 0x1E4u, w = 0;
            if (slot + 0x1E4u >= 0x4F000000u) break;
            if (wp50_peek32(slot, &w) && w == h) { c_sess = slot; break; }
        }
        if (!c_sess) return 0;
    }
    if (!wp50_peek32(c_sess + 0x154u, &v)) return 0;
    if (wp50_peek32(c_sess + 0x1C4u, &voice) && voice < 64u) {
        uint32_t vp = 0;
        if (wp50_peek32(0x008AF900u + voice * 0x300u + 0x14u, &vp)) *vpos = vp;
    }
    *samples = v;
    return 1;
}
static struct {
    int env, on, log, drop, maxhold, offset_ms;   /* env: -1 = not read */
    int used;                                     /* this frame was paced by the game clock */
    unsigned dropped, held, stalls, nodata;
    double held_ms;
    uint32_t last_s; ULONGLONG t_last_change, t_last_present, t_log;
    unsigned n_last_log;
} s50 = { -1 };
static void wp50_env(void)
{
    const char* e;
    if (s50.env >= 0) return;
    e = getenv("WINPORT_MOVIE_SYNC");          s50.env = (e && e[0] == '1') ? 1 : 0;
    e = getenv("WINPORT_MOVIE_SYNC_LOG");      s50.log = (e && e[0] == '1') ? 1 : 0;
    e = getenv("WINPORT_MOVIE_SYNC_DROP");     s50.drop = e ? atoi(e) : 2;   if (s50.drop < 1) s50.drop = 1;
    e = getenv("WINPORT_MOVIE_SYNC_MAXHOLD_MS"); s50.maxhold = e ? atoi(e) : 5000; if (s50.maxhold < 100) s50.maxhold = 100;
    e = getenv("WINPORT_MOVIE_SYNC_OFFSET_MS"); s50.offset_ms = e ? atoi(e) : 0;
    fprintf(stderr, "[SYNC50] movie picture follows the game's audio clock: %s (drop>%d frames, max hold %d ms, offset %d ms, log=%d)\n",
            s50.env ? "ON" : "off (wall clock)", s50.drop, s50.maxhold, s50.offset_ms, s50.log);
    fflush(stderr);
}
/* start of a playback pass: the clock applies only when the sound is the game's (no host companion .wav) */
static void wp50_begin(const char* m2v)
{
    char wav[1024];
    size_t L = strlen(m2v);
    int host_wav = 0;
    wp50_env();
    if (L >= 4 && L < sizeof wav) {
        memcpy(wav, m2v, L + 1); memcpy(wav + L - 4, ".wav", 4);
        host_wav = GetFileAttributesA(wav) != INVALID_FILE_ATTRIBUTES;
    }
    s50.on = s50.env && !host_wav;
    { extern volatile int g_wp73_movie_wallclock; g_wp73_movie_wallclock = !s50.on; }   /* winport v73-moviesolo */
    s50.used = 0; s50.dropped = s50.held = s50.stalls = s50.nodata = 0; s50.held_ms = 0.0;
    s50.last_s = 0xFFFFFFFFu; s50.t_last_change = s50.t_last_present = s50.t_log = GetTickCount64(); s50.n_last_log = 0;
    if (s50.env || s50.log) {
        fprintf(stderr, "[SYNC50] pass start: %s -> %s\n", host_wav ? "host .wav (wall clock, the .wav is the sound)" : "game sound (.vpk/SCREAM)",
                s50.on ? "picture slaved to the game's audio clock" : "wall clock");
        fflush(stderr);
    }
}
volatile int g_wp73_movie_wallclock = 0;   /* winport v73-moviesolo: 1 = current overlay pass paced by wall clock (boot logo) */
static void wp50_logline(unsigned n, double fps, const char* why)
{
    uint32_t smp = 0, vp = 0; unsigned c = 0;
    const int okv = wp50_vdec_pictures(&c), oks = wp50_game_clock(&smp, &vp);
    fprintf(stderr, "[SYNC50] %s frame=%u (t=%.2f s) game pictures=%s%u (drift %+.0f ms) session samples=%s%u voice_pos=%d | "
            "dropped=%u held=%u (%.0f ms) stalls=%u noclock=%u\n",
            why, n, (double)n / fps, okv ? "" : "n/a ", c, okv ? ((double)n - (double)c) * 1000.0 / fps : 0.0,
            oks ? "" : "n/a ", smp, (int)vp, s50.dropped, s50.held, s50.held_ms, s50.stalls, s50.nodata);
    fflush(stderr);
}
static void wp50_note_pause(unsigned n, double fps, int resumed)
{
    if (s50.env || s50.log) wp50_logline(n, fps, resumed ? "RESUME" : "PAUSE ");
    if (resumed) s50.t_last_change = GetTickCount64();   /* the clock was frozen on purpose: not a stall */
}
/* The game's movie clock in FRAMES (pictures the game's own player has taken from cellVdec since the StartSeq; the
 * player takes picture k when its audio timebase reaches k's pts) -- or, if that is not available, the session sample
 * count converted to frames. src: 1 = vdec pictures, 2 = session samples. */
static int wp50_clock_frames(double fps, double* frames, int* src)
{
    unsigned c = 0; uint32_t smp = 0, vp = 0;
    if (wp50_vdec_pictures(&c)) { *frames = (double)c; *src = 1; return 1; }
    if (wp50_game_clock(&smp, &vp)) { *frames = (double)smp / 48000.0 * fps; *src = 2; return 1; }
    return 0;
}
/* Before showing frame index n: 1 = show it, 0 = drop it (decoded, not shown). Frame index n = the game's picture n+1. */
static int wp50_gate(unsigned n, double fps)
{
    s50.used = 0;
    if (s50.log || s50.on) {
        const ULONGLONG now = GetTickCount64();
        if (now - s50.t_log >= 1000) { s50.t_log = now; wp50_logline(n, fps, "tick  "); }
    }
    if (!s50.on) return 1;
    for (int spins = 0;; spins++) {
        const ULONGLONG now = GetTickCount64();
        double g; int src = 0;
        if (!wp50_clock_frames(fps, &g, &src)) { s50.nodata++; return 1; }   /* no clock: wall pacing for this frame */
        if ((uint32_t)g != s50.last_s) { s50.last_s = (uint32_t)g; s50.t_last_change = now; }
        g = g - 1.0 + (double)s50.offset_ms * fps / 1000.0;               /* index of the picture the game shows */
        s50.used = 1;
        if (g >= (double)n - 0.5) {                                       /* the game has reached this frame */
            if (g > (double)n + (double)s50.drop && now - s50.t_last_present < 250) { s50.dropped++; return 0; }
            s50.t_last_present = now;
            return 1;
        }
        if (!s_movie_running || s_wp_skip_req || s_wp_pause_req) { s50.t_last_present = now; return 1; }
        if (now - s50.t_last_change > (ULONGLONG)s50.maxhold) {          /* clock frozen: never freeze the movie */
            s50.stalls++; s50.used = 0; s50.t_last_present = now;
            return 1;
        }
        if (spins == 0) s50.held++;
        Sleep(1);
        s50.held_ms += 1.0;
    }
}
#ifdef _WIN32
/* WINPORT_BOOT_DIAG=1: psarc bytes read per second (called from the v48 psarc read hook) */
static void wp50_boot_diag(unsigned n)
{
    static int on = -1;
    static unsigned long long bytes = 0, reads = 0;
    static ULONGLONG t0 = 0;
    ULONGLONG now;
    if (on < 0) { const char* e = getenv("WINPORT_BOOT_DIAG"); on = (e && e[0] == '1') ? 1 : 0; }
    if (!on) return;
    bytes += n; reads++;
    now = GetTickCount64();
    if (!t0) t0 = now;
    if (now - t0 >= 1000) {
        fprintf(stderr, "[BOOT50] psarc reads: %llu KB in %llu reads over %llu ms\n", bytes / 1024ull, reads,
                (unsigned long long)(now - t0));
        fflush(stderr);
        bytes = 0; reads = 0; t0 = now;
    }
}
#endif
void wp50_boot_diag_x(unsigned n) { wp50_boot_diag(n); }
/* ---- end winport v50-movie-sync ---- */

/* ---- winport v93-movie (engine_fixes_v93_movie.py): movie watch, safety net, in-game test driver ----
 * Guest objects (lifted EBOOT, see the script header): P = movie player [0x540054] (+0x620 state, +0x624 bytes read,
 * +0x714 play command, +0x744 EOS), S = movie request pump [0x539330] (+0x170 play requested, +0x172 name,
 * +0x4C20 preload requested, +0x4C24 stage, +0x4C10 other mode), game state at 0x57EB18. */
extern void ppu_giant_lock_acquire(void);
extern void ppu_giant_lock_release(void);
extern void (*g_ps3_guest_caller)(uint32_t, uint64_t, uint64_t, uint64_t, uint64_t);
extern int  wp93_vdec_info(unsigned* made, unsigned* taken, int* ready, int* auq);
extern void wp93_vdec_force_end(void);
extern volatile unsigned g_wp93_endseq_pics, g_wp93_endseq_n, g_wp93_seqdone_n;
int movie_wp47_request_skip(void);
volatile unsigned g_wp93_ovl_frames = 0, g_wp93_ovl_passes = 0;
#define WP93_P_SLOT        0x00540054u
#define WP93_S_SLOT        0x00539330u
#define WP93_GATE          0x00539594u
#define WP93_GSTATE        0x0057EB18u
#define WP93_OPD_PLAYCMD   0x0051A028u   /* func_0001EFE4(name, idx, flag) */
#define WP93_OPD_PRELOAD   0x0051A020u   /* func_0001EE40(name, idx) */
#define WP93_OPD_SET628    0x00530198u   /* func_002BFA6C(b) */
#define WP93_OPD_PLAY      0x005301D8u   /* func_002BFF00(b) */
#define WP93_OPD_STOP      0x005301E8u   /* func_002BFF88() MovieStop */
#define WP93_OPD_STOPRESET 0x00519C30u   /* func_00018030() stop if active + clear the request state */
#define WP93_SCRATCH       0x0CFFF000u   /* above the HLE callback stacks (boot_main CB_STACK_TOP 0x0D000000, 1st frame 0x0CFF0000) */
static int wp93_ok(uint32_t ea, uint32_t n) { return vm_base && ea >= 0x10000u && ea + n < 0x4F000000u && ppu_guest_range_committed(ea, n); }
static uint32_t wp93_r32(uint32_t ea) { const unsigned char* p; if (!wp93_ok(ea, 4)) return 0xFFFFFFFFu; p = vm_base + ea;
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static unsigned wp93_r8(uint32_t ea) { return wp93_ok(ea, 1) ? vm_base[ea] : 0xFFu; }
static void wp93_w32(uint32_t ea, uint32_t v) { if (!wp93_ok(ea, 4)) return; vm_base[ea] = (unsigned char)(v >> 24);
    vm_base[ea + 1] = (unsigned char)(v >> 16); vm_base[ea + 2] = (unsigned char)(v >> 8); vm_base[ea + 3] = (unsigned char)v; }
static void wp93_w8(uint32_t ea, unsigned v) { if (wp93_ok(ea, 1)) vm_base[ea] = (unsigned char)v; }
typedef struct {
    uint32_t P, S, st, rd, f714, gs, gate, pre, stage, b4c10;
    unsigned f744, play, made, taken; int ready, auq, vd; char name[20];
} wp93_snap;
static void wp93_read(wp93_snap* s)
{
    uint32_t g;
    memset(s, 0, sizeof *s);
    s->st = s->pre = s->stage = s->gs = s->gate = 0xFFFFFFFFu;
    s->gs = wp93_r32(WP93_GSTATE);
    s->P = wp93_r32(WP93_P_SLOT);
    if (s->P && s->P != 0xFFFFFFFFu && wp93_ok(s->P, 0x790)) {
        s->st = wp93_r32(s->P + 0x620); s->rd = wp93_r32(s->P + 0x624); s->f714 = wp93_r32(s->P + 0x714);
        s->f744 = wp93_r8(s->P + 0x744);
    } else s->P = 0;
    s->S = wp93_r32(WP93_S_SLOT);
    if (s->S && s->S != 0xFFFFFFFFu && wp93_ok(s->S, 0x4D40)) {
        s->play = wp93_r8(s->S + 0x170);
        for (int i = 0; i < 19; i++) { unsigned c = vm_base[s->S + 0x172 + i]; if (c < 32 || c > 126) break; s->name[i] = (char)c; }
        s->pre = wp93_r32(s->S + 0x4C20); s->stage = wp93_r32(s->S + 0x4C24); s->b4c10 = wp93_r32(s->S + 0x4C10);
    } else s->S = 0;
    g = wp93_r32(WP93_GATE);
    s->gate = (g && g != 0xFFFFFFFFu) ? wp93_r32(g) : 0xFFFFFFFFu;
    s->vd = wp93_vdec_info(&s->made, &s->taken, &s->ready, &s->auq);
}
static ULONGLONG s93_t0;
static void wp93_log(const char* why, const wp93_snap* s)
{
    fprintf(stderr, "[MV93] t=%llu %s gs=%d st620=%d read=%u f714=%u eos=%u | req play=%u pre=%d stage=%d 4C10=%d gate=%d name='%s' | vdec %s made=%u taken=%u ready=%d auq=%d | endseq=%u(%u pics) seqdone=%u ovl=%u(%u)\n",
            (unsigned long long)(GetTickCount64() - s93_t0), why, (int)s->gs, (int)s->st, s->rd, s->f714, s->f744, s->play, (int)s->pre,
            (int)s->stage, (int)s->b4c10, (int)s->gate, s->name, s->vd ? "open" : "none", s->made, s->taken, s->ready, s->auq,
            g_wp93_endseq_n, g_wp93_endseq_pics, g_wp93_seqdone_n, g_wp93_ovl_passes, g_wp93_ovl_frames);
    fflush(stderr);
}
/* guest call under the giant lock; returns 0 if no caller */
static int wp93_call(uint32_t opd, uint64_t a0, uint64_t a1, uint64_t a2)
{
    if (!g_ps3_guest_caller) return 0;
    g_ps3_guest_caller(opd, a0, a1, a2, 0);
    return 1;
}
static int wp93_paused(const wp93_snap* s) { return s->gs == 12u || s->gs == 13u || s_wp_pause_req; }
/* ---- safety net ---- */
static int s93_safe = -1;
static unsigned s93_r1, s93_r2, s93_r3;
static void wp93_safety(const wp93_snap* s, ULONGLONG now, ULONGLONG dt)
{
    static ULONGLONG r1_t, r2_t, r3_t, r4_t; static unsigned r1_n, r3_step, last_taken; static uint32_t r4_st;
    const int act = s93_safe;
    /* R1: play requested, preload state lost */
    if (s93_safe != 2 && s->S && s->P && s->play == 1 && s->name[0] && s->pre == 0 && s->st == 10u && s->rd > 0 && s->rd < 0x80000000u) {   /* =2: test R2 alone */
        if (!wp93_paused(s)) r1_t += dt;
        if (r1_t >= 400 && r1_n < 3) {
            const uint32_t stage = 1u;   /* prebuffered: the pump's stage 1 sees 'ready' and goes on to the play */
            fprintf(stderr, "[MOVIESAFE] R1 '%s': the game asked to play it but its request pump had lost the preload (st620=%u pre=0 stage=%d)%s\n",
                    s->name, s->st, (int)s->stage, act ? " -> preload flag + stage restored, the game's pump plays it" : " (log only)");
            fflush(stderr);
            if (act) {
                ppu_giant_lock_acquire();
                if (wp93_r8(s->S + 0x170) == 1 && wp93_r32(s->S + 0x4C20) == 0) { wp93_w32(s->S + 0x4C24, stage); wp93_w32(s->S + 0x4C20, 1); }
                ppu_giant_lock_release();
            }
            r1_t = 0; r1_n++; s93_r1++;
        }
    } else { r1_t = 0; if (s->play != 1) r1_n = 0; }
    /* R2: play requested, Play never reached */
    if (s->S && s->P && s->play == 1 && s->st != 11u) {
        if (!wp93_paused(s)) r2_t += dt;
        if (r2_t >= (s->st == 10u ? 12000u : 30000u)) {
            fprintf(stderr, "[MOVIESAFE] R2 '%s': play requested %u s ago, player still in state %u%s\n", s->name, s->st == 10u ? 12u : 30u, s->st,
                    !act ? " (log only)" : s->st == 10u ? " -> start it like the game's pump does" : " -> cancel the request (stop + clear), the game goes on");
            fflush(stderr);
            if (act) {
                ppu_giant_lock_acquire();
                if (wp93_r8(s->S + 0x170) == 1 && wp93_r32(s->P + 0x620) == 10u) {
                    wp93_call(WP93_OPD_SET628, 1, 0, 0);
                    wp93_call(WP93_OPD_PLAY, 0, 0, 0);
                    wp93_w8(s->S + 0x172, 0); wp93_w32(s->S + 0x4C24, 0); wp93_w8(s->S + 0x170, 0); wp93_w8(s->S + 0x171, 0);
                    wp93_w32(s->S + 0x4C34, 0); wp93_w32(s->S + 0x4C20, 0);
                } else if (wp93_r8(s->S + 0x170) == 1) wp93_call(WP93_OPD_STOPRESET, 0, 0, 0);
                ppu_giant_lock_release();
            }
            r2_t = 0; s93_r2++;
        }
    } else r2_t = 0;
    /* R3: Play reached, the game's picture clock frozen */
    if (s->P && s->st == 11u && s->vd) {
        if (s->taken != last_taken) { last_taken = s->taken; r3_t = 0; r3_step = 0; }
        else if (!wp93_paused(s)) r3_t += dt;
        if (r3_t >= 8000) {
            r3_step++; s93_r3++;
            fprintf(stderr, "[MOVIESAFE] R3 step %u: movie in Play but the game took no picture for 8 s (taken=%u made=%u ready=%d auq=%d)%s\n",
                    r3_step, s->taken, s->made, s->ready, s->auq,
                    !act ? " (log only)" : r3_step == 1 ? " -> end it like a skip (EOS)" : r3_step == 2 ? " -> SEQDONE + EOS byte" : " -> the game's MovieStop");
            fflush(stderr);
            if (act) {
                if (r3_step == 1) movie_wp47_request_skip();
                else {
                    ppu_giant_lock_acquire();
                    if (r3_step == 2) { wp93_vdec_force_end(); wp93_w8(s->P + 0x744, 1); }
                    else wp93_call(WP93_OPD_STOP, 0, 0, 0);
                    ppu_giant_lock_release();
                }
            }
            r3_t = 2000;   /* next step 6 s later */
        }
    } else { r3_t = 0; r3_step = 0; last_taken = 0xFFFFFFFFu; }
    /* R4: stuck in an opening state */
    if (s->P && s->st >= 1u && s->st <= 9u) {
        if (s->st != r4_st) { r4_st = s->st; r4_t = 0; } else r4_t += dt;
        if (r4_t >= 20000) { wp93_log("R4 player stuck in an opening state for 20 s (log only)", s); r4_t = 0; }
    } else { r4_t = 0; r4_st = 0; }
    (void)now;
}
/* ---- test driver ---- */
static const char* k93_all[] = { "ColossusEye", "ColossusHand", "BladeOffer", "DeathColossus", "ZeusIntoEagle", "DeathKratos",
    "IntroPegasus", "IntroPrometheus", "ItemTitanRage", "PegaRevealIsle", "IsleKickDoor", "IntroTheseus", "ItemLightning",
    "IsleHorsePull", "IsleLahkesisStatue", "Dock1EyeExplode", "BKDragIntro", "BKArenaFight", "Bog20TempleSlide",
    "IntroPerseus", "Dock2EyeExplode", "BonusCredits", "gow2credits_scea", "EndMovie", "DeathAthena", "RhodesReturn",
    "MirrorProtect", "MirrorDestroy", "IntroAtropos", "IntroLahkesis", "PhoenixFlight", "DeathKraken", "GaiaPepTalk",
    "DeathLastSpartan", "SeparatePhoenix", "SummonLavaGeyser", "Translator2Death", "Translator1Death", "ItemEarthquake",
    "AtlasTitanWar", "AtlasHand", "DeathIcarus", "IntroIcarus", "IntroHUD", NULL };
static char s93_list[64][24]; static int s93_n = -1, s93_i, s93_step, s93_ok;
static ULONGLONG s93_ts, s93_play_t, s93_start_t; static unsigned s93_r1b, s93_r2b, s93_r3b, s93_eseq, s93_sdone, s93_ovl;
static int s93_delay = 8000, s93_gap = 4000, s93_pre = 1500, s93_wipe, s93_skipat, s93_maxplay = 400;
static void wp93_test_init(void)
{
    const char* e = getenv("WINPORT_MOVIETEST93");
    s93_n = 0;
    if (!e || !*e || e[0] == '0') return;
    if (!strcmp(e, "all")) { for (int i = 0; k93_all[i] && s93_n < 64; i++) snprintf(s93_list[s93_n++], 24, "%s", k93_all[i]); }
    else { char buf[2048]; char* tok; snprintf(buf, sizeof buf, "%s", e);
        for (tok = strtok(buf, ",;"); tok && s93_n < 64; tok = strtok(NULL, ",;")) snprintf(s93_list[s93_n++], 24, "%s", tok); }
    if ((e = getenv("WINPORT_MOVIETEST93_DELAY_MS"))) s93_delay = atoi(e);
    if ((e = getenv("WINPORT_MOVIETEST93_GAP_MS"))) s93_gap = atoi(e);
    if ((e = getenv("WINPORT_MOVIETEST93_PRE_MS"))) s93_pre = atoi(e);
    if ((e = getenv("WINPORT_MOVIETEST93_WIPE"))) s93_wipe = atoi(e);
    if ((e = getenv("WINPORT_MOVIETEST93_SKIPAT"))) s93_skipat = atoi(e);
    if ((e = getenv("WINPORT_MOVIETEST93_MAXPLAY_S"))) s93_maxplay = atoi(e);
    fprintf(stderr, "[MT93] test driver: %d movies, first after %d ms of gameplay, gap %d, pre %d, wipe %d, skipat %d, maxplay %d s\n",
            s93_n, s93_delay, s93_gap, s93_pre, s93_wipe, s93_skipat, s93_maxplay);
    fflush(stderr);
}
static void wp93_result(const char* res, ULONGLONG now)
{
    fprintf(stderr, "[MT93] RESULT %d %s result=%s start_ms=%llu play_ms=%llu endseq_pics=%u endseq=%u seqdone=%u ovl_frames=%u safety_r1=%u r2=%u r3=%u\n",
            s93_i + 1, s93_list[s93_i], res, s93_start_t ? (unsigned long long)(s93_start_t - s93_play_t) : 0ull,
            s93_start_t ? (unsigned long long)(now - s93_start_t) : 0ull, g_wp93_endseq_pics, g_wp93_endseq_n - s93_eseq,
            g_wp93_seqdone_n - s93_sdone, g_wp93_ovl_passes != s93_ovl ? g_wp93_ovl_frames : 0u,
            s93_r1 - s93_r1b, s93_r2 - s93_r2b, s93_r3 - s93_r3b);
    if (!strcmp(res, "OK")) s93_ok++;
    fflush(stderr);
    s93_i++; s93_step = 0; s93_ts = now;
    if (s93_i >= s93_n) { fprintf(stderr, "[MT93] DONE %d movies, %d OK\n", s93_n, s93_ok); fflush(stderr); }
}
static void wp93_test(const wp93_snap* s, ULONGLONG now)
{
    if (s93_n < 0) wp93_test_init();
    if (s93_i >= s93_n || !s->S || !s->P) return;
    switch (s93_step) {
    case 0:   /* gameplay, player idle, no request */
        if (s->gs != 11u || s->st != 0u || s->play || s->pre) { s93_ts = now; return; }
        if (now - s93_ts < (ULONGLONG)(s93_i == 0 ? s93_delay : s93_gap)) return;
        ppu_giant_lock_acquire();
        if (wp93_ok(WP93_SCRATCH, 64)) { memset(vm_base + WP93_SCRATCH, 0, 64); memcpy(vm_base + WP93_SCRATCH, s93_list[s93_i], strlen(s93_list[s93_i])); }
        wp93_call(WP93_OPD_PRELOAD, WP93_SCRATCH, 0, 0);
        ppu_giant_lock_release();
        s93_eseq = g_wp93_endseq_n; s93_sdone = g_wp93_seqdone_n; s93_ovl = g_wp93_ovl_passes;
        s93_r1b = s93_r1; s93_r2b = s93_r2; s93_r3b = s93_r3; s93_start_t = 0;
        fprintf(stderr, "[MT93] #%d '%s': preload (game's own script entry func_0001EE40)\n", s93_i + 1, s93_list[s93_i]); fflush(stderr);
        s93_step = 1; s93_ts = now; return;
    case 1:   /* until prebuffered and the pump waits for the play request */
        if (!(s->st == 10u && s->stage == 2u)) {
            if (now - s93_ts > 25000) { wp93_log("MT93 preload timeout", s); s93_step = 2; s93_ts = now; }
            return;
        }
        if (now - s93_ts < (ULONGLONG)s93_pre) return;
        if (s93_wipe) {
            ppu_giant_lock_acquire();
            for (uint32_t o = 0x4C0C; o <= 0x4C30; o += 4) wp93_w32(s->S + o, 0);   /* = func_00017C44 */
            ppu_giant_lock_release();
            fprintf(stderr, "[MT93] #%d: WIPE test -- request fields cleared like an unrelated script request would\n", s93_i + 1); fflush(stderr);
        }
        s93_step = 2; s93_ts = now; return;
    case 2:
        ppu_giant_lock_acquire();
        wp93_call(WP93_OPD_PLAYCMD, WP93_SCRATCH, 0, 1);
        ppu_giant_lock_release();
        fprintf(stderr, "[MT93] #%d '%s': play (func_0001EFE4)\n", s93_i + 1, s93_list[s93_i]); fflush(stderr);
        s93_play_t = now; s93_step = 3; return;
    case 3:   /* until Play */
        if (s->st == 11u) { s93_start_t = now; s93_step = 4; wp93_log("MT93 started", s); return; }
        if (now - s93_play_t > 30000) {
            wp93_log("MT93 NOSTART", s);
            ppu_giant_lock_acquire(); wp93_call(WP93_OPD_STOPRESET, 0, 0, 0); ppu_giant_lock_release();
            wp93_result("NOSTART", now);
        }
        return;
    case 4:   /* until the player is idle again */
        if (s93_skipat > 0 && now - s93_start_t >= (ULONGLONG)s93_skipat && s93_skipat != -1) {
            fprintf(stderr, "[MT93] #%d: hold-START skip now (%d ms after Play) -> %d\n", s93_i + 1, s93_skipat, movie_wp47_request_skip());
            fflush(stderr); s93_skipat = -s93_skipat;   /* once per movie; restored below */
        }
        if (s->st == 0u) { s93_step = 5; s93_ts = now; wp93_log("MT93 ended", s); if (s93_skipat < -1) s93_skipat = -s93_skipat; return; }
        if (now - s93_start_t > (ULONGLONG)s93_maxplay * 1000ull) {
            wp93_log("MT93 STALL", s);
            ppu_giant_lock_acquire(); wp93_call(WP93_OPD_STOP, 0, 0, 0); ppu_giant_lock_release();
            if (s93_skipat < -1) s93_skipat = -s93_skipat;
            wp93_result("STALL", now);
        }
        return;
    case 5:   /* the game is back in gameplay */
        if (s->gs == 11u && now - s93_ts >= 3000) { ULONGLONG t = s93_ts; s93_ts = now; (void)t; wp93_result("OK", now); return; }
        if (now - s93_ts > 20000) wp93_result("NOGAME", now);
        return;
    }
}
static DWORD WINAPI wp93_thread(LPVOID arg)
{
    wp93_snap prev, cur; ULONGLONG last = GetTickCount64(), last_line = 0;
    const char* e = getenv("WINPORT_MOVIEWATCH93"); const int watch = !(e && e[0] == '0');
    (void)arg;
    memset(&prev, 0xFF, sizeof prev); prev.name[0] = 0;
    for (;;) {
        ULONGLONG now;
        Sleep(50);
        now = GetTickCount64();
        wp93_read(&cur);
        if (watch && (cur.gs != prev.gs || cur.st != prev.st || cur.play != prev.play || cur.pre != prev.pre || cur.stage != prev.stage ||
                      cur.b4c10 != prev.b4c10 || cur.gate != prev.gate || cur.f714 != prev.f714 || cur.f744 != prev.f744 ||
                      strcmp(cur.name, prev.name) || cur.vd != prev.vd)) {
            if (prev.pre == 1u && cur.pre == 0u && cur.play == 0u && cur.name[0] && cur.st != 0u && cur.st != 0xFFFFFFFFu)
                wp93_log("PRELOAD-LOST (the request pump cleared +0x4C20 while the movie is loaded)", &cur);
            else wp93_log("change", &cur);
            last_line = now;
        } else if (watch && cur.st != 0u && cur.st != 0xFFFFFFFFu && now - last_line >= 1000) { wp93_log("tick", &cur); last_line = now; }
        wp93_safety(&cur, now, now - last);
        wp93_test(&cur, now);
        prev = cur; last = now;
    }
    return 0;
}
void wp93_start(void)
{
    static volatile LONG once = 0;
    const char* e;
    if (InterlockedCompareExchange(&once, 1, 0) != 0) return;
    e = getenv("WINPORT_MOVIESAFE93"); s93_safe = !(e && e[0] == '0'); if (e && e[0] == '2') s93_safe = 2;   /* 2 = test: R1 off */
    s93_t0 = GetTickCount64();
    fprintf(stderr, "[MV93] movie watch + safety net %s (WINPORT_MOVIESAFE93=0 = log only)\n", s93_safe ? "ON" : "log-only");
    fflush(stderr);
    CloseHandle(CreateThread(NULL, 0, wp93_thread, NULL, 0, NULL));
}
/* ---- end winport v93-movie ---- */

static DWORD WINAPI movie_thread_main(LPVOID arg)
{
    (void)arg;
    if (!path_is_shell_safe(s_movie_path)) {
        fprintf(stderr, "[movie] refusing unsafe movie path (shell metachars): '%s'\n",
                s_movie_path);
        s_movie_running = 0; return 0;
    }
    movie_info mi;
    probe_movie(s_movie_path, &mi);
    fprintf(stderr, "[movie] HLE playback '%s' %ux%u @%.2ffps\n",
            s_movie_path, mi.w, mi.h, mi.fps);
    fflush(stderr);

    /* Suspend sticky content-hold so legal screen does not freeze over the logo. */
    rsx_d3d12_movie_playback_begin();

    const size_t frame_bytes = (size_t)mi.w * mi.h * 4;
    uint8_t* frame = (uint8_t*)malloc(frame_bytes);
    if (!frame) { s_movie_running = 0; return 0; }

    /* -nostdin: prevent ffmpeg from stalling on stdin under _popen.
     * -fflags +genpts: more reliable frame pacing on MPEG-1 .m2v. */
    char cmd[1400];
    snprintf(cmd, sizeof cmd,
        "ffmpeg -nostdin -hide_banner -loglevel error "
        "-fflags +genpts -i \"%s\" -f rawvideo -pix_fmt rgba -", s_movie_path);

    unsigned pass = 0;
    while (s_movie_running) {                               /* loop while the game
                                                             * holds the movie open */
        FILE* p = _popen(cmd, "rb");
        if (!p) { fprintf(stderr, "[movie] ffmpeg spawn failed\n"); break; }
        /* Binary pipe, fully buffered for large frames (3.6 MB @ 720p). */
        setvbuf(p, NULL, _IOFBF, 4 * 1024 * 1024);

        movie_play_audio(s_movie_path);

        LARGE_INTEGER freq, t0;
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&t0);
        unsigned n = 0;
        wp50_begin(s_movie_path);   /* winport v50-movie-sync */

        while (s_movie_running) {
            if (s_wp_pause_req && n > 0 && !s_wp_skip_req) {   /* winport v47-cutscene-ui: SELECT pause */
                LARGE_INTEGER p0, p1;
                ULONGLONG last = 0;
                QueryPerformanceCounter(&p0);
                movie_stop_audio();
                fprintf(stderr, "[CSUI] overlay movie paused at frame %u\n", n);
                wp50_note_pause(n, mi.fps > 1.0 ? mi.fps : 30.0, 0);   /* winport v50-movie-sync */
                fflush(stderr);
                while (s_wp_pause_req && s_movie_running && !s_wp_skip_req) {
                    const ULONGLONG now = GetTickCount64();
                    /* re-hand the frame on screen: keeps picture + UI presented even when the game is not flipping */
                    if (now - last >= 50) { rsx_d3d12_movie_frame(frame, mi.w, mi.h); last = now; }
                    Sleep(5);
                }
                QueryPerformanceCounter(&p1);
                t0.QuadPart += p1.QuadPart - p0.QuadPart;   /* the movie clock did not run while paused */
                fprintf(stderr, "[CSUI] overlay movie resumed after %.1f s%s\n",
                        (double)(p1.QuadPart - p0.QuadPart) / (double)freq.QuadPart, s_wp_skip_req ? " (skip)" : "");
                fflush(stderr);
                wp50_note_pause(n, mi.fps > 1.0 ? mi.fps : 30.0, 1);   /* winport v50-movie-sync */
                if (s_movie_running && !s_wp_skip_req)
                    wp47_wav_resume(s_movie_path, (double)n / (mi.fps > 1.0 ? mi.fps : 30.0));
            }
            size_t got = 0;
            /* Read full frame even if pipe returns short chunks. */
            while (got < frame_bytes && s_movie_running) {
                size_t r = fread(frame + got, 1, frame_bytes - got, p);
                if (r == 0) break;
                got += r;
            }
            if (got < frame_bytes) break;                   /* EOF / error */
            if (s_wp_skip_req && n > 0) { s_wp_skip_req = 2; break; }   /* winport v30-bench: skip -> EOS */
            if (!wp50_gate(n, mi.fps > 1.0 ? mi.fps : 30.0)) { n++; continue; }   /* winport v50-movie-sync: dropped */
            rsx_d3d12_movie_frame(frame, mi.w, mi.h);
            n++;
            if ((n % 30u) == 1u) {
                fprintf(stderr, "[movie] frame %u T=%llu\n", n, (unsigned long long)GetTickCount64());   /* winport v20 */
                fflush(stderr);
            }

            /* Pace to the stream fps against wall clock. */
            LARGE_INTEGER now;
            QueryPerformanceCounter(&now);
            double elapsed = (double)(now.QuadPart - t0.QuadPart) / (double)freq.QuadPart;
            double due = (double)n / (mi.fps > 1.0 ? mi.fps : 30.0);
            if (!s50.used && due > elapsed) {   /* winport v50-movie-sync */
                DWORD ms = (DWORD)((due - elapsed) * 1000.0);
                if (ms > 0 && ms < 200) Sleep(ms);
            }
        }

        _pclose(p);
        pass++;
        g_wp93_ovl_frames = n; g_wp93_ovl_passes++;   /* winport v93-movie */
        fprintf(stderr, "[movie] HLE playback pass %u done (%u frames)\n", pass, n);
        fflush(stderr);
        if (n > 0) { g_movie_overlay_done = 1;              /* video reached its end */
            fprintf(stderr, "[movie] EOS flag set: g_movie_overlay_done=1 (&=%p val=%ld)\n",
                    (void*)&g_movie_overlay_done, g_movie_overlay_done); fflush(stderr); }
        pad_autostart_note_cutscene(0);   /* winport v30-bench: the cutscene latch was never cleared */
        if (s_wp_skip_req == 2) { s_wp_skip_req = 0; fprintf(stderr, "[movie] skipped at frame %u\n", n); fflush(stderr); break; }
        if (n == 0 || !s_movie_loop) break;                 /* single-pass or failure */
    }

    free(frame);
    movie_stop_audio();
    if (s_wp47_wav) { free(s_wp47_wav); s_wp47_wav = NULL; }   /* winport v47-cutscene-ui */
    s_wp_pause_req = 0;   /* winport v47-cutscene-ui */
    rsx_d3d12_movie_stop();
    s_movie_running = 0;
    return 0;
}

/* Called from the fs HLE when the game opens a movie file. */
static void movie_hle_start_mode(const char* host_path, int loop)
{
    static int on = -1;
    if (on < 0) on = getenv("PS3_MOVIE_HLE") ? 1 : 0;
    if (!on) return;
    if (InterlockedCompareExchange(&s_movie_running, 1, 0) != 0)
        return;                                             /* already playing */
    /* NB: g_movie_overlay_done is intentionally NOT reset here. The game
     * re-opens the same logo movie every ~11s while it waits for the decode
     * to "finish"; resetting on each open made the EOS flag oscillate 0/1 so
     * the consumer almost always saw 0. Sticky-once-played is what we want:
     * after the video has played through even once, EOS holds. */
    s_movie_loop = loop;
    strncpy(s_movie_path, host_path, sizeof s_movie_path - 1);
    s_movie_path[sizeof s_movie_path - 1] = 0;
    s_movie_thread = CreateThread(NULL, 0, movie_thread_main, NULL, 0, NULL);
    if (!s_movie_thread) s_movie_running = 0;
}

void movie_hle_start(const char* host_path)      { movie_hle_start_mode(host_path, 1); }
void movie_hle_start_once(const char* host_path) { movie_hle_start_mode(host_path, 0); }

/* Play a movie the game opened from INSIDE the psarc (via FIOS, not cellFsOpen)
 * as a host overlay. GoW2's movies live as psarc members ("/_movies/NAME.m2v");
 * the game never streams the video to a host file, so ffmpeg can't read the
 * member directly. We pre-extract the members to a cache dir (tools/psarc_list
 * --dump-entry) and map the guest path to the cached host file here. Videos
 * only (.m2v); loop=0 (single pass) so the overlay ends near the logo's real
 * length. Opt-in via PS3_MOVIE_HLE (checked in movie_hle_start_mode). */
void movie_hle_from_guest(const char* guest_path)
{
    if (!guest_path) return;
    size_t L = strlen(guest_path);
    if (L < 4 || strcmp(guest_path + L - 4, ".m2v") != 0) return;   /* videos only */
    const char* base = strrchr(guest_path, '/');
    base = base ? base + 1 : guest_path;
    const char* cache = getenv("PS3_MOVIE_CACHE");
    if (!cache || !*cache) cache = "../movie_cache";                /* cwd = recomp_mid_v2 */
    char host[1024];
    snprintf(host, sizeof host, "%s/%s", cache, base);
    if (GetFileAttributesA(host) == INVALID_FILE_ATTRIBUTES) {
        fprintf(stderr, "[movie] guest opened '%s' but no cache file '%s'\n", guest_path, host);
        return;
    }
    fprintf(stderr, "[movie] guest movie '%s' -> host overlay '%s'\n", guest_path, host);
    movie_on_new_m2v(host);
    movie_hle_start_mode(host, 0);
}

void movie_hle_stop(void)
{
    s_movie_running = 0;
}

#elif defined(__APPLE__)

#include "movie_vt_metal.h"

/* PS3_WATCH_STORE: watch de escritas HOST em bloco (ppu_loader.cpp). */
extern unsigned char* vm_base;
extern uint32_t       ppu_vm_size;
extern void ps3_watch_store_bulk(uint32_t ea, uint32_t len, const char* who);

long movie_hle_overlay_done(void) { return movie_vt_overlay_done(); }

void movie_hle_start(const char* host_path)
{
    movie_vt_start(host_path, /*loop=*/1);
}

void movie_hle_start_once(const char* host_path)
{
    movie_vt_start(host_path, /*loop=*/0);
}

void movie_hle_stop(void)
{
    movie_vt_stop();
}

/* movie_io_open often passes a resolved HOST path (.../SmLogo_v2.m2v). Also
 * accept guest-style "/_movies/X.m2v" and rebuild via PS3_MOVIE_CACHE. */
void movie_hle_from_guest(const char* path_or_guest)
{
    if (!path_or_guest) return;
    size_t L = strlen(path_or_guest);
    if (L < 4 || strcmp(path_or_guest + L - 4, ".m2v") != 0) return;

    char host[1024];
    host[0] = 0;
    struct stat st;
    if (stat(path_or_guest, &st) == 0 && S_ISREG(st.st_mode)) {
        strncpy(host, path_or_guest, sizeof host - 1);
        host[sizeof host - 1] = 0;
    } else {
        const char* base = strrchr(path_or_guest, '/');
        base = base ? base + 1 : path_or_guest;
        const char* cache = getenv("PS3_MOVIE_CACHE");
        if (!cache || !*cache) cache = "movie_cache";
        snprintf(host, sizeof host, "%s/%s", cache, base);
        if (stat(host, &st) != 0 || !S_ISREG(st.st_mode)) {
            fprintf(stderr, "[movie] guest/host '%s' sem cache '%s'\n",
                    path_or_guest, host);
            fflush(stderr);
            return;
        }
    }
    movie_on_new_m2v(host);
    fprintf(stderr, "[movie] guest movie -> host VT overlay '%s'\n", host);
    fflush(stderr);
    movie_vt_start(host, /*loop=*/0);
}

/* Called from the Mac movie FSM sampler when st620 becomes active: on this
 * port the intro .m2v often never hits movie_io_open (only gow2.psarc does). */
void movie_hle_autostart_cache_if_needed(void)
{
    movie_vt_autostart_from_cache();
}

#else  /* other POSIX -- no host overlay */

void movie_hle_start(const char* host_path) { (void)host_path; }
void movie_hle_start_once(const char* host_path) { (void)host_path; }
void movie_hle_from_guest(const char* guest_path) { (void)guest_path; }
void movie_hle_stop(void) {}
long movie_hle_overlay_done(void) { return 0; }

#endif /* _WIN32 / __APPLE__ / else */

/* ======================================================================== */
/* Half 2: movie io (PS3_MOVIE_IO=1) -- PORTABLE (Windows + POSIX).         */
/*                                                                          */
/* Host-backed file ops for the game's own movie                            */
/* streaming layer. The movie player's io ops (open/read/close on an op     */
/* container) normally go through the guest FIOS scheduler + psarc          */
/* dearchiver + SPU inflate. Instead, the recomp hooks the three op         */
/* functions and serves the bytes HERE, synchronously, from the same        */
/* pre-extracted cache the overlay uses. The guest demuxer then runs        */
/* unmodified on real data and feeds cellVdecDecodeAu naturally.            */
/* FD token: 0x4D56xxxx ('MV'<<16 | slot+1) so hooks can recognize ours.    */
/* ======================================================================== */

#define MOVIE_IO_MAGIC  0x4D560000u
#define MOVIE_IO_SLOTS  4

/* Case-insensitive suffix match, portable (no strcasecmp/_stricmp dependency,
 * so it is usable verbatim on both Windows and POSIX builds of this file).
 *
 * Task 5 (bringup-instrumentacao-fase0) review finding: the WAD detection
 * below was a plain strcmp(host+hl-8, ".wad_ps3") -- case-SENSITIVE -- as
 * first written by Task 2 of this same plan (commit 9e61658). That plain
 * strcmp never reached a state outside this branch: it lived only in that
 * one intermediate commit and is replaced here, by Task 5, before merge --
 * it was not inherited from an older baseline. While it existed, a resolved
 * host path ending in ".WAD_PS3" (or any mixed case) would have silently
 * missed both PS3_CP_FIRST_WAD_OPEN and the FIOS wad= discriminator this
 * task adds. Every psarc member observed so far is lowercase (see the case
 * note on movie_io_resolve below), so this has not manifested in practice,
 * but "WADs abertos" is a Fase 0 baseline metric and must not depend on
 * getting lucky about case. Fixed here, once, and reused by every wad=
 * decision below so the checkpoint and the new FIOS event cannot drift
 * apart. */
static int has_suffix_ci(const char* s, const char* suffix)
{
    size_t ls = strlen(s), lf = strlen(suffix);
    size_t i;
    const char* p;
    if (ls < lf) return 0;
    p = s + (ls - lf);
    for (i = 0; i < lf; i++) {
        unsigned char a = (unsigned char)p[i], b = (unsigned char)suffix[i];
        if (a >= 'A' && a <= 'Z') a = (unsigned char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (unsigned char)(b - 'A' + 'a');
        if (a != b) return 0;
    }
    return 1;
}

/* Overlay diagnostics: the WAD stream as the runtime overlay shows it. Every
 * writer runs under movie_io_lock; readers never take that lock (it is held
 * across fread). See movie_io_status.h. */
static movie_io_status_store s_wad_status;

int movie_io_wad_status(movie_io_wad_view* out)
{
    if (!out) return 0;
    return movie_io_status_read_view(&s_wad_status, out);
}

/* Movie/WAD open and close events, copied to the overlay Logs tab next to the
 * existing stderr line (bounded, non-blocking ring). */
static void movie_io_overlay_log(int level, const char* what, const char* path,
                                 unsigned long long size, unsigned long long bytes_read,
                                 int with_read)
{
    char line[192];
    const char* base = movie_io_status_basename_(path ? path : "(null)");
    if (with_read)
        snprintf(line, sizeof line, "movieio %s %s read %.1f of %.1f MB", what, base,
                 bytes_read / 1048576.0, size / 1048576.0);
    else if (size)
        snprintf(line, sizeof line, "movieio %s %s (%.1f MB)", what, base, size / 1048576.0);
    else
        snprintf(line, sizeof line, "movieio %s %s", what, base);
    rsx_overlay_log_write(level, line);
}

static struct {
    FILE*              f;
    unsigned long long size;
    char               path[1024];
    unsigned long long bytes_read;   /* total bytes served since open (pread + read_cur) */
    unsigned            pread_count;  /* number of movie_io_pread() calls that returned >0 */
    unsigned            read_cur_count; /* number of movie_io_read_cur() calls that returned >0 */
} s_mio[MOVIE_IO_SLOTS];

/* --- host primitives ---------------------------------------------------
 * Only three things differ between Win32 and POSIX in this half: the mutex,
 * the background trace thread and the millisecond sleep. Everything past them
 * is one shared body of code.
 *
 * usleep() is specified to reject a value >= 1e6 us on some hosts, and the
 * trace period is 5 s, so the POSIX sleep goes through nanosleep. */
#ifdef _WIN32
  #define MIO_SLEEP_MS(ms)  Sleep((DWORD)(ms))
#else
  #define MIO_SLEEP_MS(ms)  do {                                             \
      struct timespec ts_;                                                   \
      ts_.tv_sec  = (time_t)((ms) / 1000u);                                  \
      ts_.tv_nsec = (long)((ms) % 1000u) * 1000000L;                         \
      nanosleep(&ts_, NULL);                                                 \
  } while (0)
#endif

#ifdef _WIN32
static CRITICAL_SECTION s_mio_cs;
static LONG s_mio_cs_state = 0;          /* 0=uninit 1=initializing 2=ready */

static void movie_io_lock(void)
{
    if (s_mio_cs_state != 2) {
        if (InterlockedCompareExchange(&s_mio_cs_state, 1, 0) == 0) {
            InitializeCriticalSection(&s_mio_cs);
            s_mio_cs_state = 2;
        } else {
            while (s_mio_cs_state != 2) Sleep(0);
        }
    }
    EnterCriticalSection(&s_mio_cs);
}
static void movie_io_unlock(void) { LeaveCriticalSection(&s_mio_cs); }
#else
/* PTHREAD_MUTEX_INITIALIZER is a compile-time initialiser, so the lazy-init
 * CAS the Win32 CRITICAL_SECTION needs has no counterpart here: the mutex is
 * usable before main(). It is NON-recursive where Win32 uses a recursive
 * CRITICAL_SECTION, which is safe because no movie_io_* path re-enters the
 * lock -- the two calls that could (movie_io_trace_ensure_started and
 * movie_hle_from_guest, both from movie_io_open) are made after unlocking. */
static pthread_mutex_t s_mio_mtx = PTHREAD_MUTEX_INITIALIZER;

static void movie_io_lock(void)   { pthread_mutex_lock(&s_mio_mtx); }
static void movie_io_unlock(void) { pthread_mutex_unlock(&s_mio_mtx); }
#endif

/* Periodic dump of open slots (PS3_TRACE_ASSET=1), every ~5s, on a real
 * background thread -- NOT gated on pread/read_cur activity. This exists
 * because the game may never call movie_io_close() on a slot, AND may stop
 * calling pread/read_cur entirely after a couple of calls (the "open
 * without consume" hypothesis): a dump that only ticks from inside
 * pread/read_cur would go silent right when it matters most. The thread is
 * started lazily from movie_io_open() the first time a slot is allocated. */
static int          s_mio_trace_flag = -1; /* -1=unchecked 0=off 1=on */
#ifdef _WIN32
static volatile LONG s_mio_trace_started = 0;
#endif

static void movie_io_trace_dump_locked(void)
{
    for (int i = 0; i < MOVIE_IO_SLOTS; i++) {
        if (s_mio[i].f) {
            fprintf(stderr,
                "[movieio] trace stats slot=%d path='%s' size=%llu preads=%u read_cur=%u bytes_read=%llu\n",
                i, s_mio[i].path, s_mio[i].size, s_mio[i].pread_count,
                s_mio[i].read_cur_count, s_mio[i].bytes_read);
        }
    }
    fflush(stderr);
}

#ifdef _WIN32
static DWORD WINAPI movie_io_trace_thread_proc(LPVOID unused)
#else
static void* movie_io_trace_thread_proc(void* unused)
#endif
{
    (void)unused;
    for (;;) {
        MIO_SLEEP_MS(5000);
        movie_io_lock();
        movie_io_trace_dump_locked();
        movie_io_unlock();
    }
    return 0;
}

#ifndef _WIN32
/* Detached: matches the Win32 side closing the handle right away -- nobody
 * ever joins this thread, the process exit takes it down. */
static void movie_io_trace_spawn(void)
{
    pthread_t th;
    if (pthread_create(&th, NULL, movie_io_trace_thread_proc, NULL) == 0)
        pthread_detach(th);
}
#endif

static void movie_io_trace_ensure_started(void)
{
    if (s_mio_trace_flag < 0) {
        const char* e = getenv("PS3_TRACE_ASSET");
        s_mio_trace_flag = (e && *e && *e != '0') ? 1 : 0;
    }
    if (!s_mio_trace_flag) return;
#ifdef _WIN32
    if (InterlockedCompareExchange(&s_mio_trace_started, 1, 0) == 0) {
        HANDLE h = CreateThread(NULL, 0, movie_io_trace_thread_proc, NULL, 0, NULL);
        if (h) CloseHandle(h);
    }
#else
    /* pthread_once is the POSIX spelling of the InterlockedCompareExchange
     * "exactly one winner" above. */
    static pthread_once_t once = PTHREAD_ONCE_INIT;
    pthread_once(&once, movie_io_trace_spawn);
#endif
}

int movie_io_is(unsigned fd) { return (fd & 0xFFFF0000u) == MOVIE_IO_MAGIC; }

/* Resolve a guest path against the movie cache.
 *
 * Only the basename matters: the cache is flat, holding members already pulled
 * out of the psarc. Two shapes have to resolve.
 *
 *  - With an extension ("smlogo_v2.wav"), the name is used as-is.
 *  - Without one, the streaming layer passed the bare asset NAME ("SmLogo_v2",
 *    "R_LglScA") because the extension is appended by a format string further
 *    inside the guest open path, past our hook. Try the asset kinds the cache
 *    serves: movies (.m2v) and resource WADs (.wad_ps3).
 *
 * CASE. Inside the psarc the members are lowercase ("/wad/r_perma.wad_ps3")
 * while the guest asks for "R_PermA"; the movies go the other way, the guest
 * asking for "_movies/smlogo_v2.wav" against an extracted "SmLogo_v2.wav".
 * Windows and the DEFAULT macOS volume are case-insensitive, so the plain
 * fopen already matches on both and the hazard is invisible there. On a
 * case-sensitive volume -- any Linux box, or a case-sensitive APFS volume --
 * it would not match, and the failure would read "no cache file" with the file
 * plainly sitting in the directory. The scan below closes that hole by
 * comparing directory entries case-insensitively.
 *
 * Fills out[] with a path that exists and returns 1, else returns 0 with out[]
 * holding the primary candidate (so callers can name it in a diagnostic).
 */
static int movie_io_resolve(const char* guest_path, char* out, size_t cap)
{
    if (!guest_path || !*guest_path || !out || cap == 0) return 0;
    const char* base = strrchr(guest_path, '/');
    base = base ? base + 1 : guest_path;
    const char* b2 = strrchr(base, '\\');
    if (b2) base = b2 + 1;
    if (!*base) return 0;

    if (ps3_mods_overlay_basename(base, out, cap))
        return 1;

    const char* cache = getenv("PS3_MOVIE_CACHE");
    if (!cache || !*cache) cache = "../movie_cache";

    /* Candidate file names, most specific first. */
    char cand[3][512];
    int  ncand = 0;
    snprintf(cand[ncand++], sizeof cand[0], "%s", base);
    if (!strchr(base, '.')) {
        snprintf(cand[ncand++], sizeof cand[0], "%s.m2v", base);
        snprintf(cand[ncand++], sizeof cand[0], "%s.wad_ps3", base);
    }

    snprintf(out, cap, "%s/%s", cache, cand[0]);   /* primary, for diagnostics */

    PS3_STAT64 st;
    for (int i = 0; i < ncand; i++) {
        char p[1024];
        snprintf(p, sizeof p, "%s/%s", cache, cand[i]);
        if (ps3_stat64(p, &st) == 0) { snprintf(out, cap, "%s", p); return 1; }
    }

#ifndef _WIN32
    /* Case-insensitive rescan. POSIX-only, and that is the one #ifdef this
     * change introduces: Win32 fopen already matched above, so the scan would
     * be dead code there, not different behaviour. */
    {
        DIR* d = opendir(cache);
        if (d) {
            struct dirent* e;
            while ((e = readdir(d)) != NULL) {
                for (int i = 0; i < ncand; i++) {
                    if (strcasecmp(e->d_name, cand[i]) != 0) continue;
                    char p[1024];
                    snprintf(p, sizeof p, "%s/%s", cache, e->d_name);
                    if (ps3_stat64(p, &st) == 0) {
                        snprintf(out, cap, "%s", p);
                        closedir(d);
                        return 1;
                    }
                }
            }
            closedir(d);
        }
    }
#endif
    return 0;
}

/* Resolve a guest path's basename in the movie cache and open it.
 * Returns the FD token, or 0 if the file isn't cached (caller falls back to
 * the original guest io path). out_size receives the file size in bytes. */
unsigned movie_io_open(const char* guest_path, unsigned* out_size)
{
    char host[1024];
    FILE* f = NULL;
    if (movie_io_resolve(guest_path, host, sizeof host)) f = fopen(host, "rb");
    if (!f) {
        /* A later Play of introhud/rhodesreturn must not keep decoding SmLogo.
         * Missing cache is a fallthrough, never an abort. */
        if (guest_path && (strstr(guest_path, "/_movies/") || strstr(guest_path, "\\_movies\\")
                           || has_suffix_ci(guest_path, ".m2v"))) {
            movie_hle_set_cache_path(NULL);
            movie_done_timebased_reset();
            pad_autostart_note_cutscene(1);
        }
        fprintf(stderr, "[movieio] open '%s': no cache file '%s' -> guest path\n",
                guest_path ? guest_path : "(null)", host);
        if (guest_path && (has_suffix_ci(guest_path, ".wad_ps3") ||
                           has_suffix_ci(guest_path, ".m2v")))
            movie_io_overlay_log(RSX_OVERLAY_LOG_WARN, "not cached", guest_path, 0, 0, 0);
        return 0;
    }
    _fseeki64(f, 0, SEEK_END);
    unsigned long long sz = (unsigned long long)_ftelli64(f);
    _fseeki64(f, 0, SEEK_SET);

    movie_io_lock();
    int slot = -1;
    for (int i = 0; i < MOVIE_IO_SLOTS; i++)
        if (!s_mio[i].f) { slot = i; break; }
    if (slot < 0) { movie_io_unlock(); fclose(f);
        fprintf(stderr, "[movieio] open '%s': no free slot\n", guest_path); return 0; }
    s_mio[slot].f = f;
    s_mio[slot].size = sz;
    s_mio[slot].bytes_read = 0;
    s_mio[slot].pread_count = 0;
    s_mio[slot].read_cur_count = 0;
    strncpy(s_mio[slot].path, host, sizeof s_mio[slot].path - 1);
    s_mio[slot].path[sizeof s_mio[slot].path - 1] = 0;
    if (has_suffix_ci(host, ".wad_ps3"))
        movie_io_status_open(&s_wad_status, slot, host, sz);
    movie_io_unlock();

    movie_io_trace_ensure_started();

    /* register the video for cellVdec's host decoder */
    size_t L = strlen(host);
    if (L > 4 && strcmp(host + L - 4, ".m2v") == 0) {
        movie_on_new_m2v(host);
        /* Also play it as a visible fullscreen overlay (PS3_MOVIE_HLE=1), so the
         * intro renders on screen independent of the guest shader path. host is
         * already ".../NAME.m2v"; movie_hle_from_guest re-resolves & threads it.
         * The earlier hook (func_0030D578) only ever saw "/gow2.psarc", never the
         * movie name, so the overlay never fired -- trigger it here at the real
         * .m2v open. */
        { extern void movie_hle_from_guest(const char*); movie_hle_from_guest(host); }
    }

    if (out_size) *out_size = (unsigned)sz;
    fprintf(stderr, "[movieio] open '%s' -> '%s' size=%llu slot=%d\n",
            guest_path, host, sz, slot);
    fflush(stderr);
    if (has_suffix_ci(host, ".wad_ps3") || has_suffix_ci(host, ".m2v"))
        movie_io_overlay_log(RSX_OVERLAY_LOG_INFO, "open", host, sz, 0, 0);
    /* R3: correlate last guest flip issuer with WAD open (gated PS3_TRACE_MAINLOOP). */
    {
        extern void cellGcmMainloopNote(const char*);
        cellGcmMainloopNote(guest_path ? guest_path : "(null)");
    }
    /* Task 5 (bringup-instrumentacao-fase0): FIOS OP_BEGIN. wad= usa
     * has_suffix_ci() (definido acima) em vez do strcmp exacto que aqui
     * estava -- ver o comentario junto a essa funcao. Um so' calculo,
     * reaproveitado pelo checkpoint FIRST_WAD_OPEN logo a seguir. */
    { int is_wad = has_suffix_ci(host, ".wad_ps3");
      PS3_TRACE(PS3_TS_FIOS, "movie_io_open", "OP_BEGIN",
                "op=%u path=%s len=%llu state=open wad=%d",
                MOVIE_IO_MAGIC | (unsigned)(slot + 1),
                guest_path ? guest_path : host, sz, is_wad);
      /* Task 2 (bringup-instrumentacao-fase0): FIRST_WAD_OPEN -- movie_io_open
       * serve movies (.m2v) E WADs de recursos (.wad_ps3, ex. R_LglScA/R_PermA
       * citados no brief); so' os segundos contam para este checkpoint. */
      if (is_wad) {
          ps3_checkpoint_fire(PS3_CP_FIRST_WAD_OPEN, "movie_io_open",
                             "path=%s", guest_path ? guest_path : host);
      } }
    return MOVIE_IO_MAGIC | (unsigned)(slot + 1);
}

/* stat: resolve the guest path in the cache and return the file size in
 * bytes, or 0 if not cached (caller falls back to the guest path). */
unsigned long long movie_io_stat(const char* guest_path)
{
    char host[1024];
    PS3_STAT64 st;
    if (!movie_io_resolve(guest_path, host, sizeof host)) return 0;
    if (ps3_stat64(host, &st) != 0 || st.st_size <= 0) return 0;
    return (unsigned long long)st.st_size;
}

/* Set when R_PermA.wad is fully consumed (bytes_read == size). Polled by
 * ps3_force_post_wad_icgldr / WAD SM to unlock the type-map → ICGLdr path.
 * Declared early so pread can flag before close (SM still live).
 *
 * OWNERSHIP (do not turn this into two definitions -- it must stay ONE symbol,
 * or the writer here and the reader in ps3_force_post_wad_icgldr drift apart):
 * on Windows this module defines it and runtime/ppu/ppu_loader.cpp declares it
 * extern; on POSIX the loader defines it and this module declares it extern.
 * See the matching #if defined(_WIN32) in ppu_loader.cpp. The asymmetry is
 * deliberate: the loader is a per-game object linked into every boot, while
 * this object is only pulled out of the runtime archive when the game hooks
 * movie_io, so the loader is the safer owner on the newer platform. */
#ifdef _WIN32
volatile int g_ps3_rperma_full = 0;
#else
extern volatile int g_ps3_rperma_full;
#endif

static void movie_io_maybe_flag_rperma_full(int slot)
{
    if (g_ps3_rperma_full) return;
    if (slot < 0 || slot >= MOVIE_IO_SLOTS) return;
    if (s_mio[slot].size < 20000000ull) return;
    if (s_mio[slot].bytes_read < s_mio[slot].size) return;
    if (!s_mio[slot].path[0]) return;
    if (!(strstr(s_mio[slot].path, "R_PermA") || strstr(s_mio[slot].path, "r_perma")
          || strstr(s_mio[slot].path, "R_Perm")))
        return;
    g_ps3_rperma_full = 1;
    fprintf(stderr, "[GATE-FORCE] R_PermA full flagged (bytes_read=%llu size=%llu preads=%u)\n",
            s_mio[slot].bytes_read, s_mio[slot].size, s_mio[slot].pread_count);
    fflush(stderr);
}

/* pread: read n bytes at pos into dst. Returns bytes actually read. */
unsigned movie_io_pread(unsigned fd, void* dst, unsigned n, unsigned pos)
{
    if (!movie_io_is(fd) || !dst) return 0;
    int slot = (int)(fd & 0xFFFFu) - 1;
    if (slot < 0 || slot >= MOVIE_IO_SLOTS) return 0;
    movie_io_lock();
    FILE* f = s_mio[slot].f;
    size_t got = 0;
    if (f && _fseeki64(f, (long long)pos, SEEK_SET) == 0)
        got = fread(dst, 1, n, f);
    if (got) {
        s_mio[slot].bytes_read += got;
        s_mio[slot].pread_count++;
        movie_io_maybe_flag_rperma_full(slot);
        movie_io_status_read(&s_wad_status, slot, s_mio[slot].bytes_read);
    }
    movie_io_unlock();
    /* PS3_WATCH_STORE: quando `dst` cai dentro do mapa guest (caminho
     * ppu_fs.cpp e f2b_stream_fill, que passam vm_base+off), este fread
     * escreveu DIRECTAMENTE em memoria guest sem passar por vm_write* --
     * invisivel aos dois watches de store. Converter o ponteiro host de volta
     * a EA e reportar. No caminho fios_aread_hle.c o `dst` e' um bounce host
     * e cai fora do mapa, logo nao dispara (a copia de la ja e' instrumentada
     * no proprio sitio). Gated, OFF por default. */
    if (got && vm_base && ppu_vm_size
        && (unsigned char*)dst >= vm_base
        && (unsigned char*)dst < vm_base + ppu_vm_size) {
        ps3_watch_store_bulk((uint32_t)((unsigned char*)dst - vm_base),
                             (uint32_t)got, "movie_io_pread fread");
    }
    /* Task 5: FIOS READ/PARTIAL, emitido DEPOIS do unlock (mesma disciplina do
     * Task 4 -- fwrite+fflush de ps3_trace_emit nunca dentro de uma seccao
     * critica evitavel); fd/dst/n/pos/got sao locals, nao tocados pelo
     * unlock. buf= regista o ponteiro passado a esta funcao tal como veio: no
     * caminho ppu_fs.cpp e' uma EA guest (vm_base+off); no caminho
     * fios_aread_hle.c e' um bounce buffer host (nao uma EA) -- ambiguidade
     * inerente da API desta funcao, declarada aqui em vez de escondida. */
    if (got < (size_t)n) {
        PS3_TRACE(PS3_TS_FIOS, "movie_io_pread", "PARTIAL",
                  "op=%u off=%u len=%u buf=0x%08X state=short got=%llu",
                  fd, pos, n, (uint32_t)(uintptr_t)dst, (unsigned long long)got);
    } else {
        PS3_TRACE(PS3_TS_FIOS, "movie_io_pread", "READ",
                  "op=%u off=%u len=%u buf=0x%08X state=ok",
                  fd, pos, n, (uint32_t)(uintptr_t)dst);
    }
    return (unsigned)got;
}

/* seek: whence 0=SET 1=CUR 2=END; returns the new absolute position or -1. */
long long movie_io_seek(unsigned fd, long long off, int whence)
{
    if (!movie_io_is(fd)) return -1;
    int slot = (int)(fd & 0xFFFFu) - 1;
    if (slot < 0 || slot >= MOVIE_IO_SLOTS) return -1;
    movie_io_lock();
    FILE* f = s_mio[slot].f;
    long long r = -1;
    if (f) {
        int w = (whence == 0) ? SEEK_SET : (whence == 1) ? SEEK_CUR : SEEK_END;
        if (_fseeki64(f, off, w) == 0) r = _ftelli64(f);
    }
    movie_io_unlock();
    return r;
}

/* sequential read at the FILE*'s current position (the guest's synchronous
 * read-by-FD keeps an implicit cursor; the host FILE* mirrors it). */
unsigned movie_io_read_cur(unsigned fd, void* dst, unsigned n)
{
    if (!movie_io_is(fd) || !dst) return 0;
    int slot = (int)(fd & 0xFFFFu) - 1;
    if (slot < 0 || slot >= MOVIE_IO_SLOTS) return 0;
    movie_io_lock();
    FILE* f = s_mio[slot].f;
    /* off= for the trace below: the cursor is implicit here (sequential
     * read), so read it BEFORE the fread moves it -- but only when the gate
     * is on, so the extra ftello() never costs anything on the baseline. */
    long long pos_before = -1;
    if (f && ps3_trace_on(PS3_TS_FIOS)) pos_before = _ftelli64(f);
    size_t got = f ? fread(dst, 1, n, f) : 0;
    if (got) {
        s_mio[slot].bytes_read += got;
        s_mio[slot].read_cur_count++;
        /* Same gate check as pread. A consumer that streams the WAD
         * sequentially (cellFsRead) never goes through pread, so without this
         * the flag could only be raised at close -- and the game may never
         * close the slot at all, which is precisely why the trace thread
         * exists. Identical condition, just the other read entry point. */
        movie_io_maybe_flag_rperma_full(slot);
        movie_io_status_read(&s_wad_status, slot, s_mio[slot].bytes_read);
    }
    movie_io_unlock();
    /* Task 5: FIOS READ/PARTIAL, emitido depois do unlock (ver a nota igual
     * em movie_io_pread acima). */
    if (got < (size_t)n) {
        PS3_TRACE(PS3_TS_FIOS, "movie_io_read_cur", "PARTIAL",
                  "op=%u off=%lld len=%u buf=0x%08X state=short got=%llu",
                  fd, pos_before, n, (uint32_t)(uintptr_t)dst, (unsigned long long)got);
    } else {
        PS3_TRACE(PS3_TS_FIOS, "movie_io_read_cur", "READ",
                  "op=%u off=%lld len=%u buf=0x%08X state=ok",
                  fd, pos_before, n, (uint32_t)(uintptr_t)dst);
    }
    return (unsigned)got;
}

void movie_io_close(unsigned fd)
{
    if (!movie_io_is(fd)) return;
    int slot = (int)(fd & 0xFFFFu) - 1;
    if (slot < 0 || slot >= MOVIE_IO_SLOTS) return;
    /* Task 5: locals for the OP_END trace, captured under the lock (below)
     * and emitted AFTER movie_io_unlock() -- the fields are about to be
     * zeroed in the branch that fills them in, so they cannot be read after
     * unlock the way movie_io_pread/read_cur's already-local args can. */
    int had_slot = 0;
    unsigned long long closed_bytes_read = 0, closed_size = 0;
    char closed_path[1024]; closed_path[0] = 0;
    movie_io_lock();
    if (s_mio[slot].f) {
        fprintf(stderr,
            "[movieio] close stats slot=%d path='%s' size=%llu preads=%u read_cur=%u bytes_read=%llu\n",
            slot, s_mio[slot].path, s_mio[slot].size, s_mio[slot].pread_count,
            s_mio[slot].read_cur_count, s_mio[slot].bytes_read);
        /* Gate for post-WAD ICGLdr unlock: full permanent asset WAD. */
        movie_io_maybe_flag_rperma_full(slot);
        if (g_ps3_rperma_full
            && s_mio[slot].size >= 20000000ull
            && (strstr(s_mio[slot].path, "R_PermA") || strstr(s_mio[slot].path, "r_perma")
                || strstr(s_mio[slot].path, "R_Perm"))) {
            fprintf(stderr, "[GATE-FORCE] R_PermA full close confirmed (bytes_read=%llu)\n",
                    s_mio[slot].bytes_read);
            fflush(stderr);
        }
        had_slot = 1;
        if (has_suffix_ci(s_mio[slot].path, ".wad_ps3"))
            movie_io_status_close(&s_wad_status, slot, s_mio[slot].bytes_read);
        closed_bytes_read = s_mio[slot].bytes_read;
        closed_size = s_mio[slot].size;
        strncpy(closed_path, s_mio[slot].path, sizeof closed_path - 1);
        closed_path[sizeof closed_path - 1] = 0;
        fclose(s_mio[slot].f);
        s_mio[slot].f = NULL;
        s_mio[slot].path[0] = 0;
        s_mio[slot].size = 0;
        s_mio[slot].bytes_read = 0;
        s_mio[slot].pread_count = 0;
        s_mio[slot].read_cur_count = 0;
    }
    movie_io_unlock();
    if (had_slot) {
        /* Review fix (bringup-instrumentacao-fase0, follow-up pos-Task 5,
         * item 4): `len=` tem de significar a MESMA coisa em todo o
         * subsistema FIOS -- em OP_BEGIN (:668 acima) e' o tamanho do
         * ficheiro (`sz`). Antes desta correcao, OP_END reutilizava a
         * chave `len=` para `closed_bytes_read` (bytes efectivamente lidos
         * ate ao close), uma colisao de significado por-chave da mesma
         * classe do achado `tag=` do DMA que forcou uma emenda de plano --
         * um parser (Task 6, tools/trace_parse.py) que leia `len=` entre
         * OP_BEGIN e OP_END do mesmo fd apanharia dois significados
         * diferentes sob o mesmo nome. Fix: `len=` fica consistente (tamanho
         * do ficheiro, `closed_size`, ja capturado sob o lock acima -- so'
         * deixou de ser descartado); bytes lidos ganham a sua propria chave,
         * `read=`, sem inventar nenhum valor novo. */
        PS3_TRACE(PS3_TS_FIOS, "movie_io_close", "OP_END",
                  "op=%u path=%s len=%llu read=%llu state=closed",
                  fd, closed_path, closed_size, closed_bytes_read);
        if (has_suffix_ci(closed_path, ".wad_ps3") || has_suffix_ci(closed_path, ".m2v"))
            movie_io_overlay_log(RSX_OVERLAY_LOG_INFO, "close", closed_path,
                                 closed_size, closed_bytes_read, 1);
    }
}

/* winport v30-bench: called by cellPad's pad_movie_skip (START/CROSS) on Windows. 1 = a movie was playing. */
int movie_vt_request_skip(void)
{
    if (!s_movie_running) return 0;
    s_wp_skip_req = 1;
    return 1;
}

#ifdef _WIN32
/* ---- winport v47-cutscene-ui: controls for cellPad's cutscene UI (engine_fixes_v47_cutscene_ui.py) ---------- */
int movie_wp47_overlay_running(void) { return s_movie_running ? 1 : 0; }
void movie_wp47_set_paused(int on) { s_wp_pause_req = on ? 1 : 0; }
/* 1 = the overlay movie ends as a normal EOS (the v30 path). 2 = no overlay (a movie the game decodes itself): raise
 * the same "movie finished" signal the overlay raises at its end, so movie_eos_arm.c arms the game's EOS read-hook
 * ([MOVIEEOS] ... arming EOS read-hook) and the game's player ends the movie its normal way. */
int movie_wp47_request_skip(void)
{
    if (s_movie_running) { s_wp_pause_req = 0; s_wp_skip_req = 1; return 1; }
    g_movie_overlay_done = 1;
    return 2;
}
#endif
