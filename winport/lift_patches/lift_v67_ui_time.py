#!/usr/bin/env python3
"""engine_fixes_v67_ui_time.py -- v67: Flash UI animation follows real time; the UI still updates/draws every frame
(WINPORT_UI_RT=1) + [UIRT67] diagnostic.  Research: research/T1_frame_timing.md section 7.  Apply AFTER v65.
Does NOT need v66; keep WINPORT_UI_DT unset (v66's 0-increment frames make the player skip the whole update).

WHY: the Flash player's movie update func_00276C8C (movie M = player+0xDC) reads the per-frame tick 0x8694E8
(+1 per frame), elapsed = tick - M+0xC88. If elapsed == M+0xC84 (last elapsed) it RETURNS without doing anything
(no display-list build / AS / render prep) -- this is what v66 broke (flicker, transparent menus). Otherwise
C84 = elapsed, frame = elapsed * rate(u16 at hdr+0x32, 8.8) / 15360 (= elapsed*fps/60): frame > C80 -> new frame
(C80 = frame, C78 = 0); else C78 = 1 (no new frame, normal processing). Both paths then run func_00269718,
0026D3CC, 00270640 (x13), 0026CA0C and the AS interpreter 0026C458. So the tick both gates the update and is the
animation clock; above 60 fps the timeline runs fps/60 too fast.

WHAT (WINPORT_UI_RT=1; default = original): the tick (0x8694E8) is untouched, so the update runs every frame exactly as
before. Only the frame number is changed: per movie a real-time tick R (60/s of ppu_timebase_now, max +4 per call)
replaces elapsed in the frame formula. Discontinuities in elapsed (reset to 0, gotoFrame/C88 moves, a movie not
updated for some frames) are applied to R as-is (R += d - 1), so only the per-frame +1 becomes real time. The result
is capped at C80+1 (never skips SWF frames, like the original at 60 Hz). Patched at all 4 copies of the frame formula:
func_00276C8C (ppu_recomp_000.cpp) and the entry copies func_00276CD0/00276CE4/00276D0C (ppu_recomp_002.cpp).
 - WINPORT_TIMEDIAG=1: one [UIRT67] line/s: game frames/s (tick), movie updates/s that ran (a: should be ~ frames/s
   x active movies, i.e. every frame), new SWF frames/s and fps of the busiest movie (b: ~ its own fps whatever the
   game fps), resyncs, movies tracked.
UNDO: unset WINPORT_UI_RT (or restore the .pre_v67 backups and rebuild the lift with FORCE_REBUILD_LIFT=1).
Apply:  python engine_fixes_v67_ui_time.py <kit>/ps3recomp   (idempotent)."""
import pathlib, shutil, sys

TAG = "winport v67-ui-time"

ANCHOR = ("        ctx->gpr[0] = ctx->gpr[0] - ctx->gpr[10];\n"
          "        if (((ctx->cr >> 0) & 8)) goto loc_00276D68;\n")
HOOKED = ("        ctx->gpr[0] = ctx->gpr[0] - ctx->gpr[10];\n"
          "        ctx->gpr[0] = (uint64_t)wp67_frame((uint32_t)ctx->gpr[8], (int64_t)ctx->gpr[0], (int32_t)ctx->gpr[7], "
          "(uint32_t)ctx->gpr[11]);   /* " + TAG + " */\n"
          "        if (((ctx->cr >> 0) & 8)) goto loc_00276D68;\n")

DECL = 'extern "C" int64_t wp67_frame(uint32_t m, int64_t frame, int32_t cur, uint32_t rate);   /* ' + TAG + ' */\n'

HELPER = r'''/* ---- winport v67-ui-time (engine_fixes_v67_ui_time.py): Flash timeline advances in real time ----
 * Called at the frame formula of the Flash movie update (func_00276C8C and its entry copies) with M (r8),
 * the original frame number, C80 (current frame, r7) and the 8.8 rate (r11). Returns the frame number to use. */
#define WP67_N 64
static struct { uint32_t m; int32_t last_el; double r; uint64_t t; uint64_t calls, newf; } wp67_tab[WP67_N];
static uint64_t wp67_ucalls = 0, wp67_resync = 0, wp67_t0 = 0;
static uint32_t wp67_tick0 = 0;
static void wp67_diag(uint64_t now)
{
    if (!wp67_t0) { wp67_t0 = now; wp67_tick0 = (uint32_t)vm_read32(0x8694E8u); return; }
    const double wall = (double)(now - wp67_t0) / 79800000.0;
    if (wall < 1.0) return;
    const uint32_t tick = (uint32_t)vm_read32(0x8694E8u);
    int best = -1, n = 0;
    for (int i = 0; i < WP67_N; i++) if (wp67_tab[i].m) { n++; if (best < 0 || wp67_tab[i].calls > wp67_tab[best].calls) best = i; }
    uint32_t rate = 0;
    if (best >= 0) { const uint32_t hdr = (uint32_t)vm_read32(wp67_tab[best].m + 0xC50u); if (hdr) rate = (uint32_t)vm_read16(hdr + 0x32u); }
    fprintf(stderr, "[UIRT67] wall=%.2fs game_frames=%.1f/s movie_updates=%.1f/s busiest: M=0x%08X updates=%.1f/s "
            "new_swf_frames=%.1f/s swf_fps=%.2f resync=%llu movies=%d\n",
            wall, (uint32_t)(tick - wp67_tick0) / wall, wp67_ucalls / wall, best >= 0 ? wp67_tab[best].m : 0u,
            best >= 0 ? wp67_tab[best].calls / wall : 0.0, best >= 0 ? wp67_tab[best].newf / wall : 0.0, rate / 256.0,
            (unsigned long long)wp67_resync, n);
    fflush(stderr);
    for (int i = 0; i < WP67_N; i++) { wp67_tab[i].calls = 0; wp67_tab[i].newf = 0; }
    wp67_ucalls = 0; wp67_resync = 0; wp67_t0 = now; wp67_tick0 = tick;
}
extern "C" int64_t wp67_frame(uint32_t m, int64_t frame, int32_t cur, uint32_t rate)
{
    static int on = -1, diag = -1;
    if (on < 0) {
        const char* e = getenv("WINPORT_UI_RT"); on = (e && *e && *e != '0') ? 1 : 0;
        const char* d = getenv("WINPORT_TIMEDIAG"); diag = (d && *d && *d != '0') ? 1 : 0;
        const char* v = getenv("WINPORT_UI_DT");
        if (on) { fprintf(stderr, "[UIRT67] WINPORT_UI_RT=1: Flash timelines advance in real time%s\n",
                          (v && *v && *v != '0') ? " -- WARNING: WINPORT_UI_DT (v66) is also set, unset it" : ""); fflush(stderr); }
    }
    if (!on && !diag) return frame;
    const uint64_t now = ppu_timebase_now();
    const int32_t el = (int32_t)vm_read32(m + 0xC84u);               /* elapsed ticks, stored just before */
    int i, slot = -1;
    for (i = 0; i < WP67_N; i++) { if (wp67_tab[i].m == m) break; if (slot < 0 && !wp67_tab[i].m) slot = i; }
    if (i == WP67_N) {                                               /* new movie (table full: recycle by address) */
        i = slot >= 0 ? slot : (int)((m >> 4) % WP67_N);
        wp67_tab[i].m = m; wp67_tab[i].last_el = el; wp67_tab[i].r = (double)el; wp67_tab[i].t = now;
        wp67_tab[i].calls = wp67_tab[i].newf = 0;
    } else {
        const int32_t d = el - wp67_tab[i].last_el;
        if (d <= 0 || d > 100000) { wp67_tab[i].r = (double)el; wp67_resync++; }   /* reset / backwards: sync */
        else {
            double add = (double)(now - wp67_tab[i].t) * (60.0 / 79800000.0);
            if (add > 4.0) add = 4.0;
            wp67_tab[i].r += add + (double)(d - 1);                   /* only the per-frame +1 becomes real time */
        }
        wp67_tab[i].last_el = el; wp67_tab[i].t = now;
    }
    int64_t f = frame;
    if (on) {
        const int64_t rt = (int64_t)wp67_tab[i].r;                   /* floor, R >= 0 in practice */
        f = rt >= 0 ? (rt * (int64_t)rate) / 15360 : frame;
        if (cur >= 0 && f > (int64_t)cur + 1) f = (int64_t)cur + 1;  /* never skip SWF frames */
    }
    if (diag) {
        wp67_ucalls++; wp67_tab[i].calls++;
        if (cur < 0 || f > cur) wp67_tab[i].newf++;
        wp67_diag(now);
    }
    return f;
}
/* ---- end winport v67-ui-time ---- */
'''


def patch(p, n_expected, prefix):
    s = p.read_text(encoding="utf-8", errors="surrogateescape")
    if TAG in s:
        print(f"v67-ui-time: {p.name} already patched")
        return
    if s.count(ANCHOR) != n_expected:
        sys.exit(f"v67-ui-time: frame-formula anchor found {s.count(ANCHOR)}x in {p.name} (want {n_expected})")
    bak = p.with_name(p.name + ".pre_v67")
    if not bak.exists():
        shutil.copy2(p, bak)
    s = s.replace(ANCHOR, HOOKED)
    first = s.index("\nvoid func_")
    s = s[:first + 1] + prefix + s[first + 1:]
    p.write_text(s, encoding="utf-8", errors="surrogateescape", newline="\n")
    print(f"v67-ui-time: patched {p} ({n_expected} sites)")


def apply_all(engine):
    lift = pathlib.Path(engine).parent / "gow2-recomp" / "recomp_macos_e435"
    patch(lift / "ppu_recomp_000.cpp", 1, HELPER)
    patch(lift / "ppu_recomp_002.cpp", 3, DECL)
    print("v67-ui-time: rebuild the lift with FORCE_REBUILD_LIFT=1")


if __name__ == "__main__":
    apply_all(sys.argv[1])
