/* rsx_replay -- feeds an RSX frame capture (PS3_RSX_CAPTURE, rsx_capture.h)
 * into the PRODUCTION Metal backend, offline, and dumps its surfaces.
 *
 *   rsx_replay <capture.rsxc> <out_prefix> [--census] [--backend metal]
 *
 * Mirrors the GCM walker (cellGcmSys.c):
 *   FRAME    restore the recorded rsx_state into a replay-owned static, then
 *            b->begin_frame (the recorder emits FRAME right after the walker's
 *            begin_frame, so the snapshot is the state the walker had there);
 *   METHODS  subch 0 -> rsx_process_method(&state, m, v); subch != 0 is the 2D
 *            engine, which this host cannot execute: counted (2d_words), skipped;
 *   PRESENT  b->end_frame, b->present(id), pump messages (and the id becomes
 *            cellGcmGetCurrentDisplayBufferId);
 *   END      rsx_metal_dump_surfaces(out_prefix).
 * EATABLE/DISPLAY feed the shim (replay_shim.c); PAGE is copied into vm_base.
 * RANGE (v2) is copied into vm_base AS SOON AS IT IS READ: the recorder writes a
 * backend callback's RANGEs before the method during whose dispatch that
 * callback fired, so applying them in file order gives every set_shader /
 * bind_texture / draw the guest bytes it read live -- including FP inline
 * constants rewritten between the frame start and a draw. PAGE and RANGE both
 * mark their 1 MiB page(s) committed in the shim.
 *
 * --census prints, per frame, at its PRESENT (or at the next FRAME / END when
 * it has none):
 *   frame=<i> methods=<n> draws=<n> clears=<n> color_targets={..}
 *   tex_formats={..} blend_on=<n> stencil_on=<n> 2d_words=<n>
 * methods counts every recorded method (all subchannels); draws counts
 * SET_BEGIN_END with a non-zero primitive; blend_on / stencil_on count
 * SET_BLEND_ENABLE / SET_STENCIL_TEST_ENABLE writes with a non-zero value;
 * the sets hold the distinct SET_SURFACE_COLOR_TARGET values and the distinct
 * SET_TEXTURE_FORMAT(0..15) values, in first-seen order. */
#include "ps3emu/ps3types.h"
#include "rsx_capture.h"
#include "rsx_commands.h"
#include "rsx_metal_backend.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* replay_shim.c */
extern uint8_t* vm_base;
int  replay_shim_vm_init(void);
void replay_shim_set_windows(u32 io_address, u32 io_size, u32 local_address, u32 local_size);
void replay_shim_set_eatable(const u16* io_to_ea);
int  replay_shim_set_display(u32 id, u32 offset, u32 pitch, u32 width, u32 height);
void replay_shim_set_present_id(u32 id);
void replay_shim_mark_committed(u32 ea, u32 len);

static rsx_state s_state;   /* replay-owned: rsx_process_method's state, the backend keeps a pointer */

#define CENSUS_SET_MAX 64
typedef struct { u32 v[CENSUS_SET_MAX]; u32 n; int overflow; } census_set;

typedef struct {
    int open;
    u32 frame, methods, draws, clears, blend_on, stencil_on, words_2d;
    census_set color_targets, tex_formats;
} census;

static void set_add(census_set* s, u32 v)
{
    for (u32 i = 0; i < s->n; i++) if (s->v[i] == v) return;
    if (s->n < CENSUS_SET_MAX) s->v[s->n++] = v; else s->overflow = 1;
}

static void set_print(const census_set* s)
{
    putchar('{');
    for (u32 i = 0; i < s->n; i++) printf(i ? ",0x%X" : "0x%X", s->v[i]);
    if (s->overflow) printf(s->n ? ",..." : "...");
    putchar('}');
}

static void census_flush(census* c, int enabled)
{
    if (!c->open) return;
    if (enabled) {
        printf("frame=%u methods=%u draws=%u clears=%u color_targets=",
               c->frame, c->methods, c->draws, c->clears);
        set_print(&c->color_targets);
        printf(" tex_formats=");
        set_print(&c->tex_formats);
        printf(" blend_on=%u stencil_on=%u 2d_words=%u\n", c->blend_on, c->stencil_on, c->words_2d);
        fflush(stdout);
    }
    c->open = 0;
}

static void census_method(census* c, u32 subch, u32 m, u32 v)
{
    c->methods++;
    if (subch != 0) { c->words_2d++; return; }
    if (m == NV4097_SET_BEGIN_END && v != 0) c->draws++;
    else if (m == NV4097_CLEAR_SURFACE) c->clears++;
    else if (m == NV4097_SET_SURFACE_COLOR_TARGET) set_add(&c->color_targets, v);
    else if (m == NV4097_SET_BLEND_ENABLE && v != 0) c->blend_on++;
    else if (m == NV4097_SET_STENCIL_TEST_ENABLE && v != 0) c->stencil_on++;
    else if (m >= NV4097_SET_TEXTURE_FORMAT && m < NV4097_SET_TEXTURE_FORMAT + 16u * 0x20u &&
             ((m - NV4097_SET_TEXTURE_FORMAT) & 0x1Fu) == 0)
        set_add(&c->tex_formats, v);
}

static void usage(void)
{
    fprintf(stderr, "usage: rsx_replay <capture.rsxc> <out_prefix> [--census] [--backend metal]\n");
}

static int die(const char* msg)
{
    fprintf(stderr, "[rsx_replay] ERROR: %s\n", msg);
    return 1;
}

int main(int argc, char** argv)
{
    const char* path = NULL;
    const char* prefix = NULL;
    int do_census = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--census")) do_census = 1;
        else if (!strcmp(argv[i], "--backend")) {
            if (i + 1 >= argc || strcmp(argv[i + 1], "metal") != 0) {
                usage();
                return die("only --backend metal is supported");
            }
            i++;
        } else if (argv[i][0] == '-' && argv[i][1] == '-') { usage(); return 2; }
        else if (!path) path = argv[i];
        else if (!prefix) prefix = argv[i];
        else { usage(); return 2; }
    }
    if (!path || !prefix) { usage(); return 2; }

    char err[256];
    rsxc_header hdr;
    rsxc_reader* r = rsxc_open_read(path, (u32)sizeof(rsx_state), &hdr, err, (u32)sizeof err);
    if (!r) {
        fprintf(stderr, "[rsx_replay] ERROR: %s: %s\n", path, err);
        return 1;
    }
    if (hdr.page_size != RSXC_PAGE) { rsxc_close_read(r); return die("unsupported page_size"); }
    if ((u64)hdr.local_address + hdr.local_size > ((u64)1 << 32) ||
        (u64)hdr.io_address + hdr.io_size > ((u64)1 << 32)) {
        rsxc_close_read(r);
        return die("header local/IO window outside the 32-bit guest space");
    }

    /* Before the backend reads any env: hidden window, GoW2's per-draw render
     * targets, and every host-side extra forced off. The recorder must not
     * re-record the replay. Host overlays / movies / boot logos / prompts /
     * pad autoplay are also stubbed out in replay_shim.c. */
    setenv("PS3_WINDOW_HIDDEN", "1", 1);
    setenv("PS3_METAL_PER_DRAW_RT", "1", 1);
    setenv("PS3_FULLSCREEN", "0", 1);
    setenv("PS3_METAL_DEMO_DRAW", "0", 1);
    setenv("PS3_METAL_TEX_DEMO", "0", 1);
    setenv("PS3_METAL_CONTENT_HOLD_DEMO", "0", 1);
    setenv("PS3_CONTENT_HOLD_AFTER_MOVIE", "0", 1);
    setenv("PS3_FRAME_DUMP_GAMEPLAY", "0", 1);
    unsetenv("PS3_METAL_SURF_DUMP");
    unsetenv("PS3_METAL_PICK_AUTO");
    unsetenv("PS3_RSX_CAPTURE");

    if (replay_shim_vm_init() != 0) { rsxc_close_read(r); return die("cannot reserve 4 GiB for vm_base"); }
    replay_shim_set_windows(hdr.io_address, hdr.io_size, hdr.local_address, hdr.local_size);

    if (rsx_metal_backend_init(1280, 720, "rsx_replay") != 0) {
        rsxc_close_read(r);
        return die("rsx_metal_backend_init failed");
    }
    rsx_backend* b = rsx_get_backend();
    if (!b) { rsxc_close_read(r); return die("Metal backend did not register"); }

    census c;
    memset(&c, 0, sizeof c);
    int in_frame = 0, seen_frame = 0, rc = 0;
    u32 frames = 0, presents = 0, pages = 0, ranges = 0;
    u64 range_bytes = 0;
    rsxc_record rec;
    int n;
    while ((n = rsxc_next(r, &rec)) == 1) {
        switch (rec.tag) {
        case RSXC_TAG_HEADER:
            break;
        case RSXC_TAG_EATABLE:
            if (rec.len != 65536u * 2u) { rc = die("EATABLE record has the wrong size"); goto out; }
            replay_shim_set_eatable((const u16*)rec.data);
            break;
        case RSXC_TAG_DISPLAY: {
            if (rec.len != 20u) { rc = die("DISPLAY record has the wrong size"); goto out; }
            u32 d[5];
            memcpy(d, rec.data, sizeof d);
            if (replay_shim_set_display(d[0], d[1], d[2], d[3], d[4]) != 0)
                fprintf(stderr, "[rsx_replay] WARNING: DISPLAY id %u out of range, ignored\n", d[0]);
            break;
        }
        case RSXC_TAG_PAGE: {
            if (rec.len != 4u + RSXC_PAGE) { rc = die("PAGE record has the wrong size"); goto out; }
            u32 ea;
            memcpy(&ea, rec.data, 4);
            if ((u64)ea + RSXC_PAGE > ((u64)1 << 32)) { rc = die("PAGE ea past 4 GiB"); goto out; }
            memcpy(vm_base + ea, rec.data + 4, RSXC_PAGE);
            replay_shim_mark_committed(ea, RSXC_PAGE);
            pages++;
            break;
        }
        case RSXC_TAG_RANGE: {
            if (rec.len < 8u) { rc = die("RANGE record shorter than its header"); goto out; }
            u32 ea, len;
            memcpy(&ea, rec.data, 4);
            memcpy(&len, rec.data + 4, 4);
            if (len == 0u || len > RSXC_CHUNK || rec.len != 8u + len) {
                rc = die("RANGE record has the wrong size");
                goto out;
            }
            if ((u64)ea + len > ((u64)1 << 32)) { rc = die("RANGE ea+len past 4 GiB"); goto out; }
            memcpy(vm_base + ea, rec.data + 8, len);
            replay_shim_mark_committed(ea, len);
            ranges++;
            range_bytes += len;
            break;
        }
        case RSXC_TAG_FRAME: {
            if (rec.len != 8u + (u32)sizeof(rsx_state)) { rc = die("FRAME record has the wrong size"); goto out; }
            u32 idx, pending;
            memcpy(&idx, rec.data, 4);
            memcpy(&pending, rec.data + 4, 4);
            if (pending != 0) {
                fprintf(stderr, "[rsx_replay] ERROR: frame %u was captured with a pending "
                        "draw batch (pending_batch=%u); rsx_commands.c's batch statics are not "
                        "in the capture, so it cannot be replayed faithfully\n", idx, pending);
                rc = 1;
                goto out;
            }
            census_flush(&c, do_census);
            memcpy(&s_state, rec.data + 8, sizeof s_state);
            if (b->begin_frame) b->begin_frame(b->userdata);
            in_frame = 1;
            seen_frame = 1;
            frames++;
            memset(&c, 0, sizeof c);
            c.open = 1;
            c.frame = idx;
            break;
        }
        case RSXC_TAG_METHODS: {
            if (rec.len % 12u) { rc = die("METHODS record length is not a multiple of 12"); goto out; }
            if (!seen_frame) { rc = die("METHODS before the first FRAME"); goto out; }
            for (u32 off = 0; off < rec.len; off += 12u) {
                u32 t[3];
                memcpy(t, rec.data + off, sizeof t);
                census_method(&c, t[0], t[1], t[2]);
                if (t[0] == 0u) rsx_process_method(&s_state, t[1], t[2]);
                /* subch != 0: 2D engine (NV3062/NV3089/...) -- not executable
                 * here; counted in 2d_words, never silently dropped. */
            }
            break;
        }
        case RSXC_TAG_PRESENT: {
            if (rec.len != 4u) { rc = die("PRESENT record has the wrong size"); goto out; }
            u32 id;
            memcpy(&id, rec.data, 4);
            replay_shim_set_present_id(id);
            if (in_frame && b->end_frame) b->end_frame(b->userdata);
            if (b->present) b->present(b->userdata, id);
            in_frame = 0;
            presents++;
            rsx_metal_backend_pump_messages();
            census_flush(&c, do_census);
            break;
        }
        default: {
            /* An unrecognized tag means this replay build is older than the
             * capture: skipping it would silently drop data (state, pages,
             * methods -- anything) that a newer writer considered load-bearing.
             * Fatal instead of a warning, so a stale replay never produces a
             * result that looks complete but is not. */
            char msg[128];
            snprintf(msg, sizeof msg, "unknown record tag %u (len %u) -- this replay build is older than the capture", rec.tag, rec.len);
            rc = die(msg);
            goto out;
        }
        }
    }
    if (n < 0) { rc = die("capture is corrupt (record past the end of the file)"); goto out; }
    census_flush(&c, do_census);

    {
        const int files = rsx_metal_dump_surfaces(prefix);
        fprintf(stderr, "[rsx_replay] frames=%u presents=%u pages=%u ranges=%u (%llu B) dumped=%d prefix=%s\n",
                frames, presents, pages, ranges, (unsigned long long)range_bytes, files, prefix);
        if (files <= 0) rc = die("the Metal backend dumped no surface");
    }
out:
    rsxc_close_read(r);
    fflush(stdout);
    fflush(stderr);
    /* No backend shutdown / munmap: the process exits here, and the SDL/Metal
     * teardown adds nothing to the dump already on disk. */
    return rc;
}
