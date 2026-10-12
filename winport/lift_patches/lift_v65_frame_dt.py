#!/usr/bin/env python3
"""engine_fixes_v65_frame_dt.py -- v65: high-frame-rate game clock (WINPORT_DT_MIN_US=N) + per-second clock diagnostic
(WINPORT_TIMEDIAG=1).  Research: research/T1_frame_timing.md.

WHY: GoW2's one game clock is func_002B7188 (end of every frame step, from func_002B2DD0 / func_002B2E74). It measures
the real frame time with mftb (func_0025C0D4: mftb*294912000/freq, >>9 = 1/576000 s units; delta at 0x8694D4) and
then, in func_002B72C8, clamps it to [1, 4] frames of 59.94 Hz:
    f = delta/96.1;  ratio = f<=100 ? 1.0 : f<400 ? f/100 : 4.0;  ratio = max(ratio, 60/T.fps(=60))
    T+8 (0x57284C) = ratio/60 (dt_real),  T+0xC (0x572850) = dt_real * T+0x10 * T+0x14 (dt_game; 211 readers).
The console never exceeded 60 fps, so the 1-frame MINIMUM never mattered. At 120 fps every frame still advances
1/60 s -> the whole game runs fps/60 times too fast while SCREAM audio stays real-time. 15-60 fps is already correct.

WHAT (ppu_recomp_001.cpp only; the lift must be rebuilt: FORCE_REBUILD_LIFT=1):
 - WINPORT_DT_MIN_US=N (100..16666): func_002B72C8 computes ratio = clamp(delta/9600, N/16666.7, 4.0) itself (exact
   real time: the game's /9610 runs game time 0.1% slow, i.e. ~190 ms per 3 min, whereas a console locked at 60 Hz
   hits the 1.0 clamp and is exact; same 4-frame maximum as the game) and jumps to the game's own tail func_002B7278 (/60, time scale, stores).
   Unset / out of range = original code path (min 1 frame). Suggested: 2000 (allows up to 500 fps).
 - WINPORT_TIMEDIAG=1: one [TIMEDIAG65] line per second from func_002B7188's entry: clock calls/s, raw frame delta
   ms avg/min/max, frames that hit the 1-frame minimum / 4-frame maximum, sum of dt_game per wall second
   (speed: 1.00 = real time; ~2.00 at 120 fps without the fix), last dt_real/dt_game, game-flow state (G+0x1B8).
UNDO: unset the env vars (or restore ppu_recomp_001.cpp.pre_v65 and rebuild the lift).
Apply:  python engine_fixes_v65_frame_dt.py <kit>/ps3recomp   (idempotent)."""
import pathlib, shutil, sys

TAG = "winport v65-frame-dt"


def rep1(s, old, new, what):
    if s.count(old) != 1:
        sys.exit(f"v65-frame-dt: anchor for '{what}' found {s.count(old)}x (want 1)")
    return s.replace(old, new, 1)


HELPER = r'''/* ---- winport v65-frame-dt (engine_fixes_v65_frame_dt.py): game clock HFR minimum + diagnostic ----
 * Game clock T = 0x572844 (+0 fps 60, +4 flags, +8 dt_real, +0xC dt_game, +0x10/+0x14 time scales);
 * raw frame delta (1/576000 s units) at 0x8694D4. See research/T1_frame_timing.md. */
static int wp65_min_on = -1;
static float wp65_min_ratio = 1.0f;
static volatile long long wp65_hit_min = 0, wp65_hit_max = 0;
static int wp65_dt_hook(ppu_context* ctx)
{
    if (wp65_min_on < 0) {
        const char* e = getenv("WINPORT_DT_MIN_US");
        const long us = (e && *e) ? strtol(e, NULL, 10) : -1;
        wp65_min_on = (us >= 100 && us < 16667) ? 1 : 0;
        if (wp65_min_on) {
            wp65_min_ratio = (float)us / 16666.667f;   /* ratio 1.0 = 1/60 s = 9600 units */
            fprintf(stderr, "[TIMEDIAG65] WINPORT_DT_MIN_US=%ld: game frame dt minimum %.3f ms (was 16.684 ms)\n",
                    us, (double)us / 1000.0);
            fflush(stderr);
        }
    }
    if (!wp65_min_on) return 0;
    const uint32_t delta = (uint32_t)vm_read32(ctx->gpr[28] + 0x0);       /* A+0, same load as the game */
    float r = (float)((double)delta / 9600.0);   /* exact real time in 1/60 s frames (game: /9610 = 59.94 Hz, 0.1% slow) */
    if (!(r >= wp65_min_ratio)) { r = wp65_min_ratio; wp65_hit_min++; }
    if (r > 4.0f) { r = 4.0f; wp65_hit_max++; }                              /* keep the game's 4-frame maximum */
    ctx->fpr[0] = r;
    ctx->fpr[11] = r;
    ctx->gpr[0] = (int64_t)(int32_t)ctx->gpr[9];
    return 1;
}
static void wp65_diag(void)
{
    static int on = -1;
    if (on < 0) { const char* e = getenv("WINPORT_TIMEDIAG"); on = (e && *e && *e != '0') ? 1 : 0; }
    if (!on) return;
    static uint64_t t0 = 0, n = 0, dmin = ~0ull, dmax = 0, dsum = 0, below = 0, above = 0;
    static double game_s = 0.0;
    const uint64_t now = ppu_timebase_now();                                 /* 79.8 MHz */
    const uint32_t d = (uint32_t)vm_read32(0x8694D4u);                       /* delta of the PREVIOUS update */
    uint32_t u; float dt_real, dt_game;
    u = (uint32_t)vm_read32(0x57284Cu); memcpy(&dt_real, &u, 4);
    u = (uint32_t)vm_read32(0x572850u); memcpy(&dt_game, &u, 4);
    if (!t0) { t0 = now; return; }
    n++; dsum += d; if (d < dmin) dmin = d; if (d > dmax) dmax = d;
    if (d <= 9610u) below++; if (d >= 38440u) above++;
    game_s += (double)dt_game;
    const double wall = (double)(now - t0) / 79800000.0;
    if (wall < 1.0) return;
    const uint32_t st = (uint32_t)vm_read32(0x0057E960u + 0x1B8u);
    fprintf(stderr, "[TIMEDIAG65] wall=%.2fs calls=%llu (%.1f/s) delta_ms avg=%.2f min=%.2f max=%.2f "
            "at_min1f=%llu at_max4f=%llu game_s/wall_s=%.3f dt_real=%.5f dt_game=%.5f hfr=%s(hit_min=%lld max=%lld) state=%u\n",
            wall, (unsigned long long)n, n / wall, dsum / 576.0 / n, dmin / 576.0, dmax / 576.0,
            (unsigned long long)below, (unsigned long long)above, game_s / wall, (double)dt_real, (double)dt_game,
            wp65_min_on == 1 ? "on" : "off", wp65_hit_min, wp65_hit_max, st);
    fflush(stderr);
    t0 = now; n = 0; dmin = ~0ull; dmax = 0; dsum = 0; below = 0; above = 0; game_s = 0.0;
    wp65_hit_min = wp65_hit_max = 0;
}
/* ---- end winport v65-frame-dt ---- */
'''


def apply_all(engine):
    engine = pathlib.Path(engine)
    p = engine.parent / "gow2-recomp" / "recomp_macos_e435" / "ppu_recomp_001.cpp"
    s = p.read_text(encoding="utf-8", errors="surrogateescape")
    if TAG in s:
        print("v65-frame-dt: ppu_recomp_001.cpp already patched")
        return
    bak = p.with_name(p.name + ".pre_v65")
    if not bak.exists():
        shutil.copy2(p, bak)
    s = rep1(s, "void func_002B7188(ppu_context* PPU_RESTRICT ctx) {\n",
             HELPER + "void func_002B7188(ppu_context* PPU_RESTRICT ctx) {\n"
             "        wp65_diag();   /* " + TAG + " */\n", "clock entry")
    s = rep1(s, "void func_002B72C8(ppu_context* PPU_RESTRICT ctx) {\n",
             "void func_002B72C8(ppu_context* PPU_RESTRICT ctx) {\n"
             "        if (wp65_dt_hook(ctx)) { g_trampoline_fn = (void(*)(void*))func_002B7278; return; }   /* "
             + TAG + " */\n", "variable-dt clamp")
    p.write_text(s, encoding="utf-8", errors="surrogateescape", newline="\n")
    print(f"v65-frame-dt: patched {p} -- rebuild the lift with FORCE_REBUILD_LIFT=1")


if __name__ == "__main__":
    apply_all(sys.argv[1])
