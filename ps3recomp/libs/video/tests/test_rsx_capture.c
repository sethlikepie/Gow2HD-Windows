#include "rsx_capture.h"
#include "rsx_commands.h"
#include "rsx_texture_formats.h"
#include "rsx_vertex_formats.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* --- recorder providers (Task 2): the test links rsx_capture.c only, so the
 * providers cellGcmSys.c / rsx_commands.c implement in the runtime are stubbed
 * here with call counters. The guest address space is a small malloc'd arena:
 * vm_base points at it and the stubs place RSX local at 0 and the IO window
 * right after it. */
uint8_t* vm_base = NULL;
#define T_LOCAL_SIZE (4u * RSXC_PAGE)
#define T_IO_ADDR    T_LOCAL_SIZE
#define T_IO_SIZE    (2u * RSXC_PAGE)
static int n_state, n_io, n_local, n_disp, n_pend, n_logical, n_dump;
static rsx_state t_state;
static u16 t_eat[65536];
static u32 t_logical;
static char t_dump_prefix[512];
const rsx_state* gcm_capture_rsx_state(void) { n_state++; return &t_state; }
void gcm_capture_io_info(u32* io_address, u32* io_size, const u16** io_to_ea)
{ n_io++; *io_address = T_IO_ADDR; *io_size = T_IO_SIZE; *io_to_ea = t_eat; }
void gcm_capture_local_info(u32* addr, u32* size) { n_local++; *addr = 0u; *size = T_LOCAL_SIZE; }
void gcm_capture_display_table(void (*emit)(u32 id, u32 off, u32 pitch, u32 w, u32 h))
{ n_disp++; emit(0u, 0x10000u, 5120u, 1280u, 720u); }
int rsx_commands_pending_batch(void) { n_pend++; return 0; }
u32 rsx_stats_logical(void) { n_logical++; return t_logical; }
static int t_dump(const char* prefix) { n_dump++; snprintf(t_dump_prefix, sizeof t_dump_prefix, "%s", prefix); return 0; }

/* ppu_guest_range_committed stub (review finding 1): rc_snapshot_range must
 * consult this before touching vm_base. Default (t_reject_len == 0) reports
 * everything committed, matching the assumption the rest of this file's
 * tests already make. A test can name one rejected [ea, ea+len) window and
 * count how many times it was asked about a page inside it. */
static u32 t_reject_ea = 0xFFFFFFFFu, t_reject_len, t_reject_hits;
int ppu_guest_range_committed(uint32_t addr, uint32_t n)
{
    /* Overlap test: a query touching any byte of the rejected window is
     * rejected (page-aligned page queries behave exactly as before). */
    if (t_reject_len && (u64)addr < (u64)t_reject_ea + t_reject_len && (u64)addr + n > t_reject_ea) { t_reject_hits++; return 0; }
    return 1;
}

/* Task 3 (V2.0b): rsx_commands.c / cellGcmSys.c are not linked, so the
 * backend registry and the IO-offset translation are stubbed here. The IO
 * map is the identity (IO offset == EA inside the test arena). */
static rsx_backend* t_backend;
void rsx_set_backend(rsx_backend* b) { t_backend = b; }
rsx_backend* rsx_get_backend(void) { return t_backend; }
int gcm_io_offset_to_ea(u32 io_off, u32* ea) { *ea = io_off; return 1; }

/* Fake backend: logs every callback in call order; a forwarded call must
 * arrive with the real userdata. */
static int t_fake_ud_tag;
static char t_calls[64][32];
static int t_ncalls, t_bad_ud;
static void t_log(void* ud, const char* what)
{
    if (ud != &t_fake_ud_tag) t_bad_ud++;
    if (t_ncalls < 64) snprintf(t_calls[t_ncalls++], sizeof t_calls[0], "%s", what);
}
static void f_begin(void* ud) { t_log(ud, "begin"); }
static void f_present(void* ud, u32 id) { (void)id; t_log(ud, "present"); }
static void f_set_shader(void* ud, const rsx_state* st) { (void)st; t_log(ud, "set_shader"); }
static void f_draw_arrays(void* ud, u32 p, u32 f, u32 c) { (void)p; (void)f; (void)c; t_log(ud, "draw_arrays"); }
static void f_draw_indexed(void* ud, u32 p, u32 o, u32 c) { (void)p; (void)o; (void)c; t_log(ud, "draw_indexed"); }
static void f_bind_texture(void* ud, u32 u, const rsx_texture_state* t) { (void)u; (void)t; t_log(ud, "bind_texture"); }
static rsx_backend t_fake;
static void t_fake_init(void)
{
    memset(&t_fake, 0, sizeof t_fake);
    t_fake.userdata = &t_fake_ud_tag;
    t_fake.begin_frame = f_begin;
    t_fake.present = f_present;
    t_fake.set_shader = f_set_shader;
    t_fake.draw_arrays = f_draw_arrays;
    t_fake.draw_indexed = f_draw_indexed;
    t_fake.bind_texture = f_bind_texture;
    /* clear, set_viewport, ... stay NULL: the interposer must keep them NULL */
    t_ncalls = t_bad_ud = 0;
}

/* Flattened capture: one event per RANGE record, per method entry, per
 * FRAME and per PRESENT, in file order. */
typedef struct { char k; u32 a, b; } t_ev;
static t_ev t_evs[4096];
static int t_nev;
static int t_load_events(const char* path)
{
    char err[256]; rsxc_header rh; rsxc_record rec; int st;
    t_nev = 0;
    rsxc_reader* rr = rsxc_open_read(path, (u32)sizeof(rsx_state), &rh, err, sizeof err);
    if (!rr) { printf("  open: %s\n", err); return 0; }
    while ((st = rsxc_next(rr, &rec)) == 1 && t_nev < 4096) {
        if (rec.tag == RSXC_TAG_RANGE) {
            u32 ea, len; memcpy(&ea, rec.data, 4); memcpy(&len, rec.data + 4, 4);
            t_evs[t_nev].k = 'R'; t_evs[t_nev].a = ea; t_evs[t_nev].b = len; t_nev++;
        } else if (rec.tag == RSXC_TAG_METHODS) {
            for (u32 k = 0; k < rec.len / 12u && t_nev < 4096; k++) {
                u32 t3[3]; memcpy(t3, rec.data + k * 12u, 12);
                t_evs[t_nev].k = 'M'; t_evs[t_nev].a = t3[1]; t_evs[t_nev].b = t3[2]; t_nev++;
            }
        } else if (rec.tag == RSXC_TAG_PAGE) {
            u32 ea; memcpy(&ea, rec.data, 4);
            t_evs[t_nev].k = 'G'; t_evs[t_nev].a = ea; t_evs[t_nev].b = 0; t_nev++;
        } else if (rec.tag == RSXC_TAG_FRAME) {
            t_evs[t_nev].k = 'F'; t_evs[t_nev].a = t_evs[t_nev].b = 0; t_nev++;
        } else if (rec.tag == RSXC_TAG_PRESENT) {
            t_evs[t_nev].k = 'P'; t_evs[t_nev].a = t_evs[t_nev].b = 0; t_nev++;
        }
    }
    rsxc_close_read(rr);
    return st == 0;
}
/* Index of the first event of kind k matching a (and b when b != ~0u) at or after from; -1 if none. */
static int t_find(int from, char k, u32 a, u32 b)
{
    for (int i = from; i < t_nev; i++)
        if (t_evs[i].k == k && t_evs[i].a == a && (b == 0xFFFFFFFFu || t_evs[i].b == b)) return i;
    return -1;
}
static int t_count(char k, u32 a)
{
    int n = 0;
    for (int i = 0; i < t_nev; i++) if (t_evs[i].k == k && t_evs[i].a == a) n++;
    return n;
}
static void t_setenv_capture(char* buf, size_t bufsz, const char* path, const char* spec)
{
    snprintf(buf, bufsz, "PS3_RSX_CAPTURE=%s:%s", path, spec);
#ifdef _WIN32
    _putenv(buf);
#else
    putenv(buf);
#endif
}
/* Stores logical FP word W at b (rsx_fp_read_word swaps the 16-bit halves). */
static void t_store_fp_word(u8* b, u32 w)
{
    u32 be = (w << 16) | (w >> 16);
    b[0] = (u8)(be >> 24); b[1] = (u8)(be >> 16); b[2] = (u8)(be >> 8); b[3] = (u8)be;
}
static int fails;
#define CHECK(n, c) do { if (!(c)) { printf("FAIL %s\n", n); fails++; } else printf("[PASS] %s\n", n); } while (0)
static const char* tmp_path(const char* name) { static char p[512]; snprintf(p, sizeof p, "%s/%s", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp", name); return p; }
int main(void)
{
    rsxc_header h = { RSXC_MAGIC, RSXC_VERSION, 1234u, 0x47E00000u, 0x3000000u, 0xC0000000u, 0x10000000u, RSXC_PAGE };
    static u16 eat[65536]; for (u32 i = 0; i < 65536u; i++) eat[i] = (u16)(i < 48u ? 0x47Eu + i : 0xFFFFu);
    static u8 page[RSXC_PAGE]; for (u32 i = 0; i < RSXC_PAGE; i++) page[i] = (u8)(i * 7u);
    static u8 state[1234]; for (u32 i = 0; i < sizeof state; i++) state[i] = (u8)(i ^ 0x5Au);
    char path[512]; snprintf(path, sizeof path, "%s", tmp_path("rsxc_roundtrip.rsxc"));
    rsxc_writer* w = rsxc_open_write(path, &h);
    CHECK("open_write", w != NULL);
    rsxc_write_eatable(w, eat);
    rsxc_write_display(w, 0, 0x400000u, 5120u, 1280u, 720u);
    rsxc_write_frame(w, 7u, 0u, state, sizeof state);
    rsxc_write_page(w, 0xC0010000u, page);
    for (u32 i = 0; i < 100000u; i++) rsxc_write_method(w, i & 1u, 0x1800u + (i & 0xFFu) * 4u, i);
    rsxc_write_present(w, 2u);
    CHECK("close_write", rsxc_close_write(w, 1u) == 0);

    char err[256]; rsxc_header rh;
    rsxc_reader* r = rsxc_open_read(path, 1234u, &rh, err, sizeof err);
    CHECK("open_read", r != NULL && memcmp(&rh, &h, sizeof h) == 0);
    rsxc_record rec; u32 methods = 0, sum_ok = 1, seen = 0; int st;
    while ((st = rsxc_next(r, &rec)) == 1) {
        seen |= 1u << rec.tag;
        if (rec.tag == RSXC_TAG_EATABLE) sum_ok &= memcmp(rec.data, eat, sizeof eat) == 0;
        if (rec.tag == RSXC_TAG_FRAME) sum_ok &= rec.len == 8u + sizeof state && memcmp(rec.data + 8, state, sizeof state) == 0;
        if (rec.tag == RSXC_TAG_PAGE) sum_ok &= memcmp(rec.data + 4, page, RSXC_PAGE) == 0;
        if (rec.tag == RSXC_TAG_METHODS) {
            const u32 n = rec.len / 12u;
            for (u32 k = 0; k < n; k++) {
                u32 t[3]; memcpy(t, rec.data + k * 12u, 12);
                sum_ok &= t[0] == (methods & 1u) && t[1] == 0x1800u + (methods & 0xFFu) * 4u && t[2] == methods;
                methods++;
            }
        }
    }
    CHECK("roundtrip_records", st == 0 && sum_ok && methods == 100000u &&
          (seen & ((1u<<2)|(1u<<3)|(1u<<4)|(1u<<5)|(1u<<6)|(1u<<7))) == ((1u<<2)|(1u<<3)|(1u<<4)|(1u<<5)|(1u<<6)|(1u<<7)));
    rsxc_close_read(r);

    /* reader_rejects_bad_abi */
    CHECK("reader_rejects_bad_abi", rsxc_open_read(path, 999u, &rh, err, sizeof err) == NULL && strstr(err, "state_size"));
    /* reader_rejects_truncated: drop the END record */
    {
        FILE* f = fopen(path, "rb"); fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
        u8* b = malloc((size_t)n); fread(b, 1, (size_t)n, f); fclose(f);
        char tpath[512]; snprintf(tpath, sizeof tpath, "%s", tmp_path("rsxc_trunc.rsxc"));
        f = fopen(tpath, "wb"); fwrite(b, 1, (size_t)n - 12, f); fclose(f); free(b);
        CHECK("reader_rejects_truncated", rsxc_open_read(tpath, 1234u, &rh, err, sizeof err) == NULL);
        remove(tpath);
    }
    /* hash sanity: equal pages equal hash, one flipped byte changes it */
    { u64 a = rsxc_hash_page(page); page[RSXC_PAGE - 1] ^= 1u; CHECK("hash_changes", a != rsxc_hash_page(page)); }
    remove(path);

    /* capture_off_is_noop: env unset -> every hook returns before touching a
     * provider (the OFF path must cost one cached branch, no work). */
    {
#ifdef _WIN32
        _putenv("PS3_RSX_CAPTURE=");
#else
        unsetenv("PS3_RSX_CAPTURE");
#endif
        const int en = rsx_capture_enabled();
        rsx_capture_set_dump_hook(t_dump);
        rsx_capture_on_begin_frame(); rsx_capture_on_method(0, 0x1808, 1);
        rsx_capture_on_present(0); rsx_capture_on_display_buffer(0, 0, 5120, 1280, 720);
        CHECK("capture_off_is_noop", en == 0 && n_state + n_io + n_local + n_disp + n_pend + n_logical + n_dump == 0);
    }

    /* capture_records_frames: PS3_RSX_CAPTURE=<path>:<min_draws>:<frames>
     * arms after a frame with >= min_draws, records K frames (FRAME + changed
     * pages + methods + PRESENT), closes and calls the dump hook with the
     * "<path-without-ext>_live" prefix. */
    {
        static u8 arena[T_IO_ADDR + T_IO_SIZE];
        vm_base = arena;
        memset(arena, 0, sizeof arena);
        arena[1u * RSXC_PAGE + 5u] = 0xAB;              /* local page 1 non-zero */
        arena[T_IO_ADDR + 3u] = 0xCD;                   /* io page 0 non-zero */
        for (u32 i = 0; i < 65536u; i++) t_eat[i] = 0xFFFFu;
        char cpath[512], env[600];
        snprintf(cpath, sizeof cpath, "%s", tmp_path("rsxc_rec.rsxc"));
        remove(cpath);
        snprintf(env, sizeof env, "PS3_RSX_CAPTURE=%s:10:2", cpath);
        static char envbuf[600]; memcpy(envbuf, env, sizeof envbuf);
#ifdef _WIN32
        _putenv(envbuf);
#else
        putenv(envbuf);
#endif
        rsx_capture_reset_for_tests();
        CHECK("capture_enabled", rsx_capture_enabled() == 1);
        /* frame with too few draws: stays armed */
        t_logical = 3; rsx_capture_on_begin_frame(); rsx_capture_on_method(0, 0x1808, 1); rsx_capture_on_present(0);
        rsx_capture_on_begin_frame();                  /* prev logical 3 < 10 -> not armed */
        FILE* probe = fopen(cpath, "rb");
        CHECK("capture_waits_min_draws", probe == NULL);
        if (probe) fclose(probe);
        t_logical = 12; rsx_capture_on_method(0, 0x1808, 2); rsx_capture_on_present(1);
        rsx_capture_on_begin_frame();                  /* arms: frame 0 */
        rsx_capture_on_method(0, 0x1814, 0x11); rsx_capture_on_method(1, 0x0300, 0x22);
        rsx_capture_on_display_buffer(1, 0x20000u, 5120u, 1280u, 720u);
        rsx_capture_on_present(0);
        arena[1u * RSXC_PAGE + 6u] = 0xEF;              /* change local page 1 only */
        rsx_capture_on_begin_frame();                  /* frame 1 */
        rsx_capture_on_method(0, 0x1814, 0x33);
        rsx_capture_on_present(1);                     /* last frame: close + dump */
        rsx_capture_on_begin_frame(); rsx_capture_on_method(0, 0x1814, 0x44); rsx_capture_on_present(0); /* done: ignored */

        char err[256]; rsxc_header rh; rsxc_record rec; int st;
        rsxc_reader* rr = rsxc_open_read(cpath, (u32)sizeof(rsx_state), &rh, err, sizeof err);
        CHECK("capture_file_valid", rr != NULL && rh.local_size == T_LOCAL_SIZE && rh.io_address == T_IO_ADDR &&
              rh.io_size == T_IO_SIZE && rh.page_size == RSXC_PAGE);
        u32 frames = 0, pages = 0, meths = 0, presents = 0, disp = 0, eat = 0, order_ok = 1;
        u32 page_ea[8]; u32 frame_of_page[8]; u32 last_m = 0;
        while (rr && (st = rsxc_next(rr, &rec)) == 1) {
            if (rec.tag == RSXC_TAG_EATABLE) eat++;
            if (rec.tag == RSXC_TAG_DISPLAY) disp++;
            if (rec.tag == RSXC_TAG_FRAME) frames++;
            if (rec.tag == RSXC_TAG_PAGE && pages < 8) { memcpy(&page_ea[pages], rec.data, 4); frame_of_page[pages] = frames; pages++; }
            if (rec.tag == RSXC_TAG_METHODS) { meths += rec.len / 12u; memcpy(&last_m, rec.data + rec.len - 4u, 4); }
            if (rec.tag == RSXC_TAG_PRESENT) { presents++; if (frames != presents) order_ok = 0; }
        }
        if (rr) rsxc_close_read(rr);
        /* frame 0: local page 1 + io page 0 (zero pages skipped); frame 1: local page 1 again */
        CHECK("capture_records_frames", frames == 2 && presents == 2 && order_ok && eat == 1 && disp == 2 &&
              meths == 3 && last_m == 0x33 && pages == 3 &&
              page_ea[0] == 1u * RSXC_PAGE && frame_of_page[0] == 1 &&
              page_ea[1] == T_IO_ADDR && frame_of_page[1] == 1 &&
              page_ea[2] == 1u * RSXC_PAGE && frame_of_page[2] == 2);
        char want[512]; snprintf(want, sizeof want, "%s", cpath); *strrchr(want, '.') = 0; strcat(want, "_live");
        CHECK("capture_dump_hook", n_dump == 1 && strcmp(t_dump_prefix, want) == 0);
        remove(cpath);
    }
    /* capture_skips_uncommitted_pages (review finding 1, V2.0): s_config.localSize
     * is hardcoded to 256 MB in cellGcmSys.c, but the host boot only commits
     * PS3_VM_RSX_MB of RSX local memory -- the tail (and, if not fully committed,
     * the IO window) is PROT_NONE, and hashing an uncommitted page would SIGBUS
     * the game. rc_snapshot_range must check ppu_guest_range_committed() per
     * RSXC_PAGE and skip pages it rejects. This test malloc's an arena sized to
     * cover only the pages the stub reports as committed -- one page short of the
     * full IO window -- so that a read of the rejected page is, under ASan, a
     * detectable heap-buffer-overflow (the malloc'd block ends exactly at the
     * rejected page's guest address); the stub also counts every check it is
     * asked for that page as a portable (non-ASan) signal. */
    {
        const u32 committed_size = T_IO_ADDR + RSXC_PAGE;   /* local (4 pages) + io page 0 only */
        const u32 reject_ea = T_IO_ADDR + RSXC_PAGE;        /* io page 1: reported NOT committed */
        u8* small_arena = (u8*)malloc(committed_size);
        CHECK("uncommitted_test_alloc", small_arena != NULL);
        if (small_arena) {
            memset(small_arena, 0, committed_size);
            small_arena[1u * RSXC_PAGE + 5u] = 0xAB;        /* local page 1 non-zero -> gets written */
            small_arena[T_IO_ADDR + 3u]      = 0xCD;        /* io page 0 non-zero -> gets written */
            vm_base = small_arena;
            for (u32 i = 0; i < 65536u; i++) t_eat[i] = 0xFFFFu;
            t_reject_ea = reject_ea;
            t_reject_len = RSXC_PAGE;
            t_reject_hits = 0;

            char cpath[512], env[600];
            snprintf(cpath, sizeof cpath, "%s", tmp_path("rsxc_uncommitted.rsxc"));
            remove(cpath);
            snprintf(env, sizeof env, "PS3_RSX_CAPTURE=%s:0:1", cpath);
            static char envbuf2[600]; memcpy(envbuf2, env, sizeof envbuf2);
#ifdef _WIN32
            _putenv(envbuf2);
#else
            putenv(envbuf2);
#endif
            rsx_capture_reset_for_tests();
            CHECK("uncommitted_capture_enabled", rsx_capture_enabled() == 1);
            t_logical = 1;
            rsx_capture_on_begin_frame();          /* min_draws=0 -> arms and starts recording here */
            rsx_capture_on_method(0, 0x1814, 0x11);
            rsx_capture_on_present(0);              /* frames=1 -> closes + dumps */

            CHECK("uncommitted_page_checked", t_reject_hits > 0);

            char err[256]; rsxc_header rh; rsxc_record rec; int st;
            rsxc_reader* rr = rsxc_open_read(cpath, (u32)sizeof(rsx_state), &rh, err, sizeof err);
            CHECK("uncommitted_capture_valid", rr != NULL);
            int found_rejected_page = 0, found_committed_pages = 0;
            while (rr && (st = rsxc_next(rr, &rec)) == 1) {
                if (rec.tag == RSXC_TAG_PAGE) {
                    u32 ea; memcpy(&ea, rec.data, 4);
                    if (ea == reject_ea) found_rejected_page = 1;
                    if (ea == 1u * RSXC_PAGE || ea == T_IO_ADDR) found_committed_pages++;
                }
            }
            if (rr) rsxc_close_read(rr);
            CHECK("uncommitted_page_not_captured", !found_rejected_page);
            CHECK("committed_pages_still_captured", found_committed_pages == 2);

            t_reject_ea = 0xFFFFFFFFu;
            t_reject_len = 0;
            remove(cpath);
            free(small_arena);
            vm_base = NULL;
        }
    }

    /* range_roundtrip (Task 1, format v2): 3 RANGE records incl. one with
     * len < RSXC_CHUNK, read back byte-equal; header version 2. */
    {
        rsxc_header h2 = { RSXC_MAGIC, RSXC_VERSION, 1234u, 0x47E00000u, 0x3000000u, 0xC0000000u, 0x10000000u, RSXC_PAGE };
        CHECK("range_version_is_2", RSXC_VERSION == 2u);
        static u8 r0[RSXC_CHUNK]; for (u32 i = 0; i < RSXC_CHUNK; i++) r0[i] = (u8)(i * 3u);
        static u8 r1[123];        for (u32 i = 0; i < sizeof r1; i++) r1[i] = (u8)(i ^ 0x77u);
        static u8 r2[RSXC_CHUNK]; for (u32 i = 0; i < RSXC_CHUNK; i++) r2[i] = (u8)(i * 5u + 1u);
        char rpath[512]; snprintf(rpath, sizeof rpath, "%s", tmp_path("rsxc_range.rsxc"));
        rsxc_writer* rw = rsxc_open_write(rpath, &h2);
        CHECK("range_open_write", rw != NULL);
        CHECK("range_write_full", rsxc_write_range(rw, 0x10000u, r0, RSXC_CHUNK) == 0);
        CHECK("range_write_partial", rsxc_write_range(rw, 0x20000u, r1, (u32)sizeof r1) == 0);
        CHECK("range_write_full2", rsxc_write_range(rw, 0x30000u, r2, RSXC_CHUNK) == 0);
        CHECK("range_write_rejects_zero_len", rsxc_write_range(rw, 0x40000u, r1, 0u) == -1);
        CHECK("range_write_rejects_oversize", rsxc_write_range(rw, 0x40000u, r0, RSXC_CHUNK + 1u) == -1);
        CHECK("range_close_write", rsxc_close_write(rw, 0u) == 0);

        char rerr[256]; rsxc_header rrh;
        rsxc_reader* rr2 = rsxc_open_read(rpath, 1234u, &rrh, rerr, sizeof rerr);
        CHECK("range_open_read", rr2 != NULL && rrh.version == 2u);
        u32 ranges = 0, range_ok = 1;
        u32 exp_ea[3] = { 0x10000u, 0x20000u, 0x30000u };
        u32 exp_len[3] = { RSXC_CHUNK, (u32)sizeof r1, RSXC_CHUNK };
        const u8* exp_data[3] = { r0, r1, r2 };
        rsxc_record rrec; int rst;
        while (rr2 && (rst = rsxc_next(rr2, &rrec)) == 1) {
            if (rrec.tag == RSXC_TAG_RANGE) {
                u32 ea, len; memcpy(&ea, rrec.data, 4); memcpy(&len, rrec.data + 4, 4);
                if (ranges < 3) {
                    range_ok &= ea == exp_ea[ranges] && len == exp_len[ranges] &&
                                rrec.len == 8u + exp_len[ranges] &&
                                memcmp(rrec.data + 8, exp_data[ranges], exp_len[ranges]) == 0;
                }
                ranges++;
            }
        }
        CHECK("range_roundtrip", rst == 0 && ranges == 3 && range_ok);
        if (rr2) rsxc_close_read(rr2);

        /* reader corruption checks: hand-corrupt the RANGE payload's inner
         * len field so it disagrees with the record length / exceeds
         * RSXC_CHUNK -- rsxc_next must report -1, never read past it. */
        {
            FILE* f = fopen(rpath, "rb"); fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
            u8* buf = (u8*)malloc((size_t)n); fread(buf, 1, (size_t)n, f); fclose(f);
            /* locate the first RANGE record (tag 9) by scanning from after the header */
            size_t pos = 8u + sizeof(rsxc_header);
            size_t range_pos = 0;
            while (pos + 8u <= (size_t)n) {
                u32 tag, len; memcpy(&tag, buf + pos, 4); memcpy(&len, buf + pos + 4, 4);
                if (tag == RSXC_TAG_RANGE) { range_pos = pos; break; }
                pos += 8u + len;
            }
            CHECK("range_corruption_setup", range_pos != 0);
            if (range_pos != 0) {
                /* mismatched inner len (record says len X, inner len field says X+1) */
                char bpath[512]; snprintf(bpath, sizeof bpath, "%s", tmp_path("rsxc_range_bad_len.rsxc"));
                u8* cbuf = (u8*)malloc((size_t)n); memcpy(cbuf, buf, (size_t)n);
                u32 bad_inner = RSXC_CHUNK - 1u; /* record len still says RSXC_CHUNK (8+RSXC_CHUNK) */
                memcpy(cbuf + range_pos + 8u + 4u, &bad_inner, 4);
                FILE* bf = fopen(bpath, "wb"); fwrite(cbuf, 1, (size_t)n, bf); fclose(bf); free(cbuf);
                rsxc_header bh; char berr[256];
                rsxc_reader* br = rsxc_open_read(bpath, 1234u, &bh, berr, sizeof berr);
                int bad_seen = -2;
                if (br) {
                    rsxc_record brec;
                    while (1) {
                        int s = rsxc_next(br, &brec);
                        if (s != 1) { bad_seen = s; break; }
                        if (brec.tag == RSXC_TAG_RANGE) { bad_seen = -3; /* should not have accepted it */ break; }
                    }
                    rsxc_close_read(br);
                }
                CHECK("range_reader_rejects_len_mismatch", bad_seen == -1);
                remove(bpath);

                /* inner len == 0: record len still says 8 (0 + 0 data bytes)
                 * so the outer/inner lengths agree, but len 0 violates the
                 * documented [1, RSXC_CHUNK] range and must be rejected too. */
                char zpath[512]; snprintf(zpath, sizeof zpath, "%s", tmp_path("rsxc_range_zero_len.rsxc"));
                u8* zbuf = (u8*)malloc(range_pos + 16u);
                memcpy(zbuf, buf, range_pos + 16u);
                u32 zero_rec_len = 8u, zero_inner_len = 0u;
                memcpy(zbuf + range_pos + 4u, &zero_rec_len, 4);       /* outer len = 8 */
                memcpy(zbuf + range_pos + 8u + 4u, &zero_inner_len, 4); /* inner len = 0 */
                FILE* zf = fopen(zpath, "wb"); fwrite(zbuf, 1, range_pos + 16u, zf); fclose(zf); free(zbuf);
                /* append END so the header/END scan in rsxc_open_read still succeeds */
                {
                    FILE* zf2 = fopen(zpath, "ab");
                    u32 endtag = RSXC_TAG_END, endlen = 4u, frames0 = 0u;
                    fwrite(&endtag, 4, 1, zf2); fwrite(&endlen, 4, 1, zf2); fwrite(&frames0, 4, 1, zf2);
                    fclose(zf2);
                }
                rsxc_header zh; char zerr[256];
                rsxc_reader* zr = rsxc_open_read(zpath, 1234u, &zh, zerr, sizeof zerr);
                int zero_seen = -2;
                if (zr) {
                    rsxc_record zrec;
                    while (1) {
                        int s = rsxc_next(zr, &zrec);
                        if (s != 1) { zero_seen = s; break; }
                        if (zrec.tag == RSXC_TAG_RANGE) { zero_seen = -3; /* should not have accepted it */ break; }
                    }
                    rsxc_close_read(zr);
                }
                CHECK("range_reader_rejects_len_zero", zero_seen == -1);
                remove(zpath);
            }
            free(buf);
        }
        remove(rpath);
    }

    /* reader_accepts_v1 (format v2): a hand-written v1 header + END must
     * still open (old captures keep replaying). */
    {
        rsxc_header v1h = { RSXC_MAGIC, 1u, 999u, 0x47E00000u, 0x3000000u, 0xC0000000u, 0x10000000u, RSXC_PAGE };
        char v1path[512]; snprintf(v1path, sizeof v1path, "%s", tmp_path("rsxc_v1.rsxc"));
        FILE* f = fopen(v1path, "wb");
        u32 tag = RSXC_TAG_HEADER, len = (u32)sizeof v1h;
        fwrite(&tag, 4, 1, f); fwrite(&len, 4, 1, f); fwrite(&v1h, sizeof v1h, 1, f);
        u32 endtag = RSXC_TAG_END, endlen = 4u, frames = 0u;
        fwrite(&endtag, 4, 1, f); fwrite(&endlen, 4, 1, f); fwrite(&frames, 4, 1, f);
        fclose(f);
        char v1err[256]; rsxc_header v1rh;
        rsxc_reader* v1r = rsxc_open_read(v1path, 999u, &v1rh, v1err, sizeof v1err);
        CHECK("reader_accepts_v1", v1r != NULL && v1rh.version == 1u);
        if (v1r) rsxc_close_read(v1r);
        remove(v1path);
    }

    /* range_dedupe_overlap: chunk table, open addressing, growth. */
    {
        rsxc_chunk_table* t = rsxc_chunks_new();
        CHECK("chunks_new", t != NULL);
        const u32 ea = 0x77000u;
        CHECK("dedupe_first_seen", rsxc_chunks_changed(t, ea, 0xABCDEF01u) == 1);
        CHECK("dedupe_same_hash_unchanged", rsxc_chunks_changed(t, ea, 0xABCDEF01u) == 0);
        CHECK("dedupe_diff_hash_changed", rsxc_chunks_changed(t, ea, 0xABCDEF02u) == 1);
        CHECK("dedupe_new_hash_now_stable", rsxc_chunks_changed(t, ea, 0xABCDEF02u) == 0);

        /* 10000 distinct chunks force growth (init cap well under that);
         * all still found afterwards. */
        enum { N = 10000 };
        int grow_ok = 1, refind_ok = 1;
        for (u32 i = 0; i < (u32)N; i++) {
            const u32 gea = (i + 1u) * RSXC_CHUNK; /* distinct from `ea` above */
            const u64 h = (u64)i * 0x9E3779B97F4A7C15ull + 1u;
            if (rsxc_chunks_changed(t, gea, h) != 1) grow_ok = 0;
        }
        CHECK("dedupe_growth_all_first_seen", grow_ok);
        for (u32 i = 0; i < (u32)N; i++) {
            const u32 gea = (i + 1u) * RSXC_CHUNK;
            const u64 h = (u64)i * 0x9E3779B97F4A7C15ull + 1u;
            if (rsxc_chunks_changed(t, gea, h) != 0) refind_ok = 0;
        }
        CHECK("dedupe_growth_all_refound_unchanged", refind_ok);
        /* one of the grown entries, hash changed -> 1 */
        CHECK("dedupe_growth_one_changed", rsxc_chunks_changed(t, 5000u * RSXC_CHUNK, 0x1u) == 1);
        rsxc_chunks_free(t);

        /* t == NULL: always reports changed (never silently drops data). */
        CHECK("dedupe_null_table_reports_changed", rsxc_chunks_changed(NULL, ea, 1u) == 1);
    }

    /* hash_bytes matches hash_page for a full RSXC_PAGE, and hashes a
     * non-8-aligned length without reading past it (ASan-visible if it did). */
    {
        static u8 hp[RSXC_PAGE]; for (u32 i = 0; i < RSXC_PAGE; i++) hp[i] = (u8)(i * 11u + 3u);
        CHECK("hash_bytes_matches_hash_page", rsxc_hash_bytes(hp, RSXC_PAGE) == rsxc_hash_page(hp));
        u8* odd = (u8*)malloc(13u);
        for (u32 i = 0; i < 13u; i++) odd[i] = (u8)(i * 17u);
        u64 ha = rsxc_hash_bytes(odd, 13u);
        u64 hb = rsxc_hash_bytes(odd, 13u);
        CHECK("hash_bytes_odd_len_deterministic", ha == hb);
        odd[12] ^= 1u;
        CHECK("hash_bytes_odd_len_changes", rsxc_hash_bytes(odd, 13u) != ha);
        free(odd);
    }

    /* ---- Task 3 (V2.0b): backend interposer ------------------------------
     * Guest layout in a malloc'd arena (identity IO map): local 0..0x40000,
     * IO window 0x40000..0x60000; FP ucode at 0x50000 (main memory,
     * shader_program loc 2), vertex attribute 0 at 0x48000 (main memory,
     * float4, stride 16), texture at 0x44000 (A8R8G8B8 linear 16x16, main). */
    {
        const u32 ARENA = T_IO_ADDR + T_IO_SIZE;
        const u32 FP_EA = 0x50000u, VTX_EA = 0x48000u, TEX_EA = 0x44000u;
        u8* ar = (u8*)calloc(1, ARENA);
        vm_base = ar;
        for (u32 i = 0; i < 65536u; i++) t_eat[i] = 0xFFFFu;
        memset(&t_state, 0, sizeof t_state);
        t_state.shader_program = 2u;                 /* loc 2 = main memory */
        t_state.fragment_program_addr = FP_EA;
        t_state.vertex_attribs[0].enabled = 1;
        t_state.vertex_attribs[0].type = RSX_VERTEX_TYPE_F;
        t_state.vertex_attribs[0].size = 4u;
        t_state.vertex_attribs[0].stride = 16u;
        t_state.vertex_attribs[0].offset = 0x80000000u | VTX_EA;   /* bit 31: main memory */

        char cpath[512]; static char envb[700];
        snprintf(cpath, sizeof cpath, "%s", tmp_path("rsxc_interposer.rsxc"));
        remove(cpath);
        t_setenv_capture(envb, sizeof envb, cpath, "0:2");
        rsx_capture_reset_for_tests();
        t_fake_init();
        rsx_set_backend(&t_fake);
        t_logical = 1;
        CHECK("interposer_enabled", rsx_capture_enabled() == 1);
        CHECK("interposer_absent_while_armed", rsx_get_backend() == &t_fake);

        rsx_capture_on_begin_frame();                /* min_draws 0: starts recording (frame 0) */
        /* Guest data written AFTER the frame-start PAGE snapshot (the pages
         * were all-zero there, so no PAGE covers them): only a RANGE can
         * carry these bytes to the replay. */
        t_store_fp_word(ar + FP_EA, 0x00000001u);    /* instr 0: FP_END -> 16-byte program, span 512 */
        for (u32 i = 0; i < 64u; i++) ar[VTX_EA + i] = (u8)(i + 1u);
        rsx_backend* b = rsx_get_backend();
        CHECK("interposer_installed_while_recording",
              b != NULL && b != &t_fake && b->userdata == &t_fake_ud_tag &&
              b->begin_frame == t_fake.begin_frame && b->present == t_fake.present &&
              b->clear == NULL && b->set_viewport == NULL && b->blit_scaled == NULL &&
              b->draw_arrays != NULL && b->draw_arrays != t_fake.draw_arrays);

        /* range_precedes_method: method 0x1814 (DRAW_ARRAYS) dispatch calls
         * draw_arrays; the walker records the method AFTER dispatch. */
        if (b && b->draw_arrays) b->draw_arrays(b->userdata, 5u, 0u, 3u);
        rsx_capture_on_method(0u, 0x1814u, 0xA1u);
        /* fp_reemitted_per_draw: patch one inline-constant word of the FP
         * bytes (inside the 512-byte span) between two draws. */
        ar[FP_EA + 64u] ^= 0x5Au;
        if (b && b->draw_arrays) b->draw_arrays(b->userdata, 5u, 0u, 3u);
        rsx_capture_on_method(0u, 0x1814u, 0xA2u);
        /* texture: no per-frame "already emitted this key" filter any more
         * (removed: the CPU/SPU can rewrite a texture between two binds in
         * the same frame and the Metal backend can re-read on a cache miss,
         * so skipping the second bind risked stale bytes). Every bind still
         * goes through rc_emit_span's 4 KiB chunk-hash dedupe: same bytes,
         * same key, twice in the same frame -> one RANGE; bytes changed
         * between binds -> a second RANGE. */
        rsx_texture_state tx; memset(&tx, 0, sizeof tx);
        tx.offset = TEX_EA;
        tx.format = 2u | ((RSX_TEXTURE_A8R8G8B8 | RSX_TEXTURE_LN) << 8);
        tx.image_rect = (16u << 16) | 16u;
        ar[TEX_EA] = 0x11u;
        if (b && b->bind_texture) b->bind_texture(b->userdata, 0u, &tx);
        rsx_capture_on_method(0u, 0x1B00u, 0xB1u);
        if (b && b->bind_texture) b->bind_texture(b->userdata, 0u, &tx);   /* same bytes: deduped */
        rsx_capture_on_method(0u, 0x1B00u, 0xB15u);
        ar[TEX_EA] = 0x22u;                          /* changed, same key, same frame */
        if (b && b->bind_texture) b->bind_texture(b->userdata, 0u, &tx);
        rsx_capture_on_method(0u, 0x1B00u, 0xB2u);
        if (b && b->set_shader) b->set_shader(b->userdata, &t_state);
        rsx_capture_on_method(0u, 0x08E4u, 0xC1u);
        if (b && b->present) b->present(b->userdata, 0u);
        rsx_capture_on_present(0u);
        CHECK("interposer_still_installed_mid_capture", rsx_get_backend() != &t_fake);

        /* range_after_page_snapshot: the vertex chunk changes before frame
         * 1's PAGE snapshot (so the PAGE carries V2) and changes back to the
         * bytes of the last RANGE (V1) before the draw: the chunk table must
         * track what the replay holds (V2 from the PAGE), so the draw emits
         * V1 again instead of deduping against the stale RANGE hash. */
        ar[VTX_EA] ^= 0xFFu;
        rsx_capture_on_begin_frame();                /* frame 1 */
        ar[VTX_EA] ^= 0xFFu;
        ar[TEX_EA] = 0x33u;                          /* changed after the snapshot */
        b = rsx_get_backend();
        if (b && b->draw_arrays) b->draw_arrays(b->userdata, 5u, 0u, 3u);
        rsx_capture_on_method(0u, 0x1814u, 0xA3u);
        if (b && b->bind_texture) b->bind_texture(b->userdata, 0u, &tx);
        rsx_capture_on_method(0u, 0x1B00u, 0xB3u);
        rsx_capture_on_present(1u);                  /* last frame: close */
        CHECK("interposer_removed_on_close", rsx_get_backend() == &t_fake);
        CHECK("interposer_forwarded_with_real_userdata", t_bad_ud == 0 && t_ncalls == 9 &&
              !strcmp(t_calls[0], "draw_arrays") && !strcmp(t_calls[2], "bind_texture") &&
              !strcmp(t_calls[3], "bind_texture") && !strcmp(t_calls[4], "bind_texture") &&
              !strcmp(t_calls[5], "set_shader") && !strcmp(t_calls[6], "present") &&
              !strcmp(t_calls[7], "draw_arrays") && !strcmp(t_calls[8], "bind_texture"));

        CHECK("interposer_capture_valid", t_load_events(cpath));
        const int m1 = t_find(0, 'M', 0x1814u, 0xA1u), m2 = t_find(0, 'M', 0x1814u, 0xA2u);
        const int r_fp = t_find(0, 'R', FP_EA, 0xFFFFFFFFu), r_vtx = t_find(0, 'R', VTX_EA, 0xFFFFFFFFu);
        CHECK("range_precedes_method", m1 > 0 && r_fp >= 0 && r_vtx >= 0 && r_fp < m1 && r_vtx < m1 &&
              t_evs[r_fp].b == RSXC_CHUNK && t_evs[r_vtx].b == RSXC_CHUNK);
        const int r_fp2 = t_find(m1, 'R', FP_EA, 0xFFFFFFFFu);
        CHECK("fp_reemitted_per_draw", m2 > m1 && r_fp2 > m1 && r_fp2 < m2 &&
              (t_find(m1, 'R', VTX_EA, 0xFFFFFFFFu) < 0 || t_find(m1, 'R', VTX_EA, 0xFFFFFFFFu) > m2));
        /* ^ vertices unchanged between the two draws: deduped */
        const int mb1 = t_find(0, 'M', 0x1B00u, 0xB1u), mb1b = t_find(0, 'M', 0x1B00u, 0xB15u);
        const int mb2 = t_find(0, 'M', 0x1B00u, 0xB2u);
        const int mb3 = t_find(0, 'M', 0x1B00u, 0xB3u), f1 = t_find(1, 'F', 0u, 0xFFFFFFFFu);
        const int r_tx1 = t_find(0, 'R', TEX_EA, 0xFFFFFFFFu);
        const int r_tx2 = t_find(r_tx1 + 1, 'R', TEX_EA, 0xFFFFFFFFu);
        const int r_tx3 = f1 >= 0 ? t_find(f1, 'R', TEX_EA, 0xFFFFFFFFu) : -1;
        /* same texture bound twice with unchanged bytes (B1 -> B1b): no
         * second RANGE in between -- rc_emit_span's chunk-hash dedupe holds. */
        CHECK("texture_bind_unchanged_deduped", r_tx1 > m2 && r_tx1 < mb1 && r_tx2 > mb1b);
        /* bytes changed between binds (B1b -> B2, and again across the frame
         * boundary into B3): every changed bind emits its own RANGE -- no
         * per-frame "already emitted this key" filter suppresses it. */
        CHECK("texture_bind_changed_emits", t_count('R', TEX_EA) == 3 &&
              r_tx2 > mb1b && r_tx2 < mb2 &&
              f1 > mb2 && r_tx3 > f1 && r_tx3 < mb3);
        /* set_shader with the FP unchanged since the last draw: deduped;
         * frame 1's PAGE carries the same FP bytes, so the frame-1 draw
         * emits no FP RANGE either. */
        CHECK("set_shader_fp_deduped", t_count('R', FP_EA) == 2);
        const int ma3 = t_find(0, 'M', 0x1814u, 0xA3u);
        const int r_v3 = f1 >= 0 ? t_find(f1, 'R', VTX_EA, 0xFFFFFFFFu) : -1;
        CHECK("range_after_page_snapshot", ma3 > f1 && r_v3 > f1 && r_v3 < ma3);
        remove(cpath);

        /* ranges_skip_uncommitted: a vertex span straddling the end of
         * committed memory. The arena is cut at 0x49000 (a malloc ending
         * there, so an uncommitted read is an ASan heap overflow) and the
         * stub rejects everything from 0x49000 on; the span
         * [0x48F00, 0x48F00 + 32*16 + 16) covers chunks 0x48000 and 0x49000. */
        {
            const u32 CUT = 0x49000u;
            u8* sa = (u8*)calloc(1, CUT);
            vm_base = sa;
            for (u32 i = 0; i < 0x100u; i++) sa[0x48F00u + i] = (u8)(i | 1u);
            t_reject_ea = CUT; t_reject_len = ARENA - CUT; t_reject_hits = 0;
            t_state.shader_program = 0u;             /* no FP (0x50000 is past the cut) */
            t_state.vertex_attribs[0].offset = 0x80000000u | 0x48F00u;
            remove(cpath);
            t_setenv_capture(envb, sizeof envb, cpath, "0:1");
            rsx_capture_reset_for_tests();
            t_fake_init();
            rsx_set_backend(&t_fake);
            (void)rsx_capture_enabled();
            rsx_capture_on_begin_frame();
            const u32 hits_after_frame = t_reject_hits;
            b = rsx_get_backend();
            if (b && b->draw_arrays) b->draw_arrays(b->userdata, 5u, 0u, 32u);
            rsx_capture_on_method(0u, 0x1814u, 0xD1u);
            const u32 hits_draw = t_reject_hits - hits_after_frame;
            rsx_capture_on_present(0u);
            CHECK("ranges_skip_uncommitted_file", t_load_events(cpath));
            const int md = t_find(0, 'M', 0x1814u, 0xD1u);
            const int rc0 = t_find(0, 'R', 0x48000u, 0xFFFFFFFFu);
            CHECK("ranges_skip_uncommitted", hits_draw >= 1u && md > 0 && rc0 >= 0 && rc0 < md &&
                  t_count('R', 0x49000u) == 0 && rsx_get_backend() == &t_fake);
            t_reject_ea = 0xFFFFFFFFu; t_reject_len = 0;
            remove(cpath);
            free(sa);
        }
        /* ranges_partial_chunk_committed (review fix): a chunk that straddles
         * the end of the ONE committed range the caller actually read from
         * must still emit those bytes, even though ppu_guest_range_committed
         * only reports true for a whole-chunk query. CUT is deliberately NOT
         * chunk-aligned: the vertex span [0x49000, 0x49010) is fully inside
         * the committed region [0, CUT), but the whole-chunk query for
         * 0x49000..0x4A000 reaches past CUT and is reported uncommitted. */
        {
            const u32 CUT = 0x49010u;
            u8* sa = (u8*)calloc(1, CUT);
            vm_base = sa;
            for (u32 i = 0; i < 0x10u; i++) sa[0x49000u + i] = (u8)(0xC0u + i);
            t_reject_ea = CUT; t_reject_len = 0x10000000u - CUT; t_reject_hits = 0;
            t_state.shader_program = 0u;             /* no FP */
            t_state.vertex_attribs[0].offset = 0x80000000u | 0x49000u;
            remove(cpath);
            t_setenv_capture(envb, sizeof envb, cpath, "0:1");
            rsx_capture_reset_for_tests();
            t_fake_init();
            rsx_set_backend(&t_fake);
            (void)rsx_capture_enabled();
            rsx_capture_on_begin_frame();
            b = rsx_get_backend();
            /* count=0: span64 = count*stride + 16 == 16 bytes, entirely below CUT. */
            if (b && b->draw_arrays) b->draw_arrays(b->userdata, 5u, 0u, 0u);
            rsx_capture_on_method(0u, 0x1814u, 0xF1u);
            rsx_capture_on_present(0u);
            CHECK("ranges_partial_chunk_committed_file", t_load_events(cpath));
            /* Exactly one RANGE, covering the sub-span only (not the full
             * RSXC_CHUNK) -- no dedupe against the full-chunk hash key. */
            const int rp = t_find(0, 'R', 0x49000u, 0x10u);
            CHECK("ranges_partial_chunk_committed", rp >= 0 && t_count('R', 0x49000u) == 1);
            /* Replay-side content check: the RANGE payload must match the
             * bytes the live backend read (not zeros, not truncated). */
            {
                char rerr[256]; rsxc_header rrh; rsxc_record rrec; int rst; int content_ok = 0;
                rsxc_reader* rr2 = rsxc_open_read(cpath, (u32)sizeof(rsx_state), &rrh, rerr, sizeof rerr);
                if (rr2) {
                    while ((rst = rsxc_next(rr2, &rrec)) == 1) {
                        if (rrec.tag == RSXC_TAG_RANGE) {
                            u32 rea, rlen; memcpy(&rea, rrec.data, 4); memcpy(&rlen, rrec.data + 4, 4);
                            if (rea == 0x49000u)
                                content_ok = rlen == 0x10u && memcmp(rrec.data + 8, sa + 0x49000u, 0x10u) == 0;
                        }
                    }
                    rsxc_close_read(rr2);
                }
                CHECK("ranges_partial_chunk_committed_content", content_ok);
            }
            t_reject_ea = 0xFFFFFFFFu; t_reject_len = 0;
            remove(cpath);
            free(sa);
        }
        /* page_rewritten_after_range (mirror of range_after_page_snapshot):
         * frame 0's PAGE of IO page 0 carries S0; a draw then emits a RANGE
         * with S1 (the replay now holds S1 there); the guest resets the chunk
         * to S0 before frame 1. The page hash equals the last PAGE's, but the
         * replay's copy differs (S1), so frame 1 must rewrite the PAGE --
         * readers the interposer does not see (blit_scaled, 2D) would
         * otherwise start frame 1 from stale bytes. */
        {
            vm_base = ar;
            memset(ar, 0, ARENA);
            t_state.shader_program = 0u;
            t_state.vertex_attribs[0].offset = 0x80000000u | VTX_EA;
            for (u32 i = 0; i < 64u; i++) ar[VTX_EA + i] = (u8)(i + 7u);   /* S0, in the frame-0 PAGE */
            remove(cpath);
            t_setenv_capture(envb, sizeof envb, cpath, "0:2");
            rsx_capture_reset_for_tests();
            t_fake_init();
            rsx_set_backend(&t_fake);
            (void)rsx_capture_enabled();
            rsx_capture_on_begin_frame();            /* frame 0: PAGE(0x40000) = S0 */
            ar[VTX_EA] ^= 0xFFu;                     /* S1 */
            b = rsx_get_backend();
            if (b && b->draw_arrays) b->draw_arrays(b->userdata, 5u, 0u, 3u);
            rsx_capture_on_method(0u, 0x1814u, 0xE1u);
            rsx_capture_on_present(0u);
            ar[VTX_EA] ^= 0xFFu;                     /* back to S0 before frame 1 */
            rsx_capture_on_begin_frame();            /* frame 1: must rewrite PAGE(0x40000) */
            rsx_capture_on_present(1u);
            CHECK("page_rewritten_after_range_file", t_load_events(cpath));
            const int g0 = t_find(0, 'G', T_IO_ADDR, 0xFFFFFFFFu);
            const int me = t_find(0, 'M', 0x1814u, 0xE1u);
            const int rv = t_find(0, 'R', VTX_EA, 0xFFFFFFFFu);
            const int fr1 = t_find(1, 'F', 0u, 0xFFFFFFFFu);
            const int g1 = fr1 >= 0 ? t_find(fr1, 'G', T_IO_ADDR, 0xFFFFFFFFu) : -1;
            const int p1 = fr1 >= 0 ? t_find(fr1, 'P', 0u, 0xFFFFFFFFu) : -1;
            CHECK("page_rewritten_after_range", g0 >= 0 && rv > g0 && me > rv && fr1 > me &&
                  g1 > fr1 && p1 > g1);
            remove(cpath);
        }
        vm_base = NULL;
        free(ar);
        rsx_set_backend(NULL);
    }

    printf(fails ? "%d FAILED\n" : "ALL PASS\n", fails);
    return fails ? 1 : 0;
}
