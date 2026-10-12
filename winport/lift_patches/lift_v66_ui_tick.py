#!/usr/bin/env python3
"""engine_fixes_v66_ui_tick.py -- v66: the game's 60 Hz TICK counter follows real time (WINPORT_UI_DT=1) + [UIDT66]
diagnostic.  Research: research/T1_frame_timing.md section 6.  Apply AFTER v65.

WHY: besides the dt clock (v65), GoW2 keeps an integer tick counter at 0x8694E8 (struct 0x8694E4 via TOC-0x12B0).
func_002B73C8 adds 1 to it once per frame (called first thing from the per-frame func_002B25AC, an indirect call).
On the console (locked 60 Hz) that is a 1/60 s clock. ~250 lifted functions read it, including the Flash/AS player:
func_00276C8C (movie advance) reads it through TOC-0x1E28, takes elapsed = tick - movie+0xC88 and converts it to SWF
frames as elapsed * swf_rate(8.8 at header+0x32) / 15360 = elapsed * fps / 60, i.e. it treats the tick as 60 Hz.
So every Flash UI (Power Up menu bar/wheel, pause menu, main menu, HUD tweens) and every tick-based timer runs fps/60
too fast above 60 fps, and slow below 60.

WHAT (ppu_recomp_001.cpp, func_002B73C8; rebuild the lift with FORCE_REBUILD_LIFT=1):
 - WINPORT_UI_DT=1: add round(acc) instead of 1, where acc accumulates real time * 60 (ppu_timebase_now, so the v51
   fast-forward warp applies too). Rounding keeps exactly +1/frame at a jittery 60 Hz; 120 fps gives 1,0,1,0...;
   30 fps gives +2. A single frame adds at most 4 (like the game's 4-frame dt cap; long hitches are dropped).
   The first call adds 1. Unset/0 = original (+1 per frame).
 - WINPORT_TIMEDIAG=1: one [UIDT66] line per second: tick calls/s, ticks added/s (60.0 = real time; = fps without the
   fix), frames that added 0 / >=2, mode.
UNDO: unset WINPORT_UI_DT (or restore ppu_recomp_001.cpp.pre_v66 and rebuild).
Apply:  python engine_fixes_v66_ui_tick.py <kit>/ps3recomp   (idempotent)."""
import pathlib, shutil, sys

TAG = "winport v66-ui-tick"


def rep1(s, old, new, what):
    if s.count(old) != 1:
        sys.exit(f"v66-ui-tick: anchor for '{what}' found {s.count(old)}x (want 1)")
    return s.replace(old, new, 1)


HELPER = r'''/* ---- winport v66-ui-tick (engine_fixes_v66_ui_tick.py): 60 Hz tick counter 0x8694E8 follows real time ---- */
static uint32_t wp66_tick_inc(void)
{
    static int on = -1, diag = -1;
    if (on < 0) {
        const char* e = getenv("WINPORT_UI_DT"); on = (e && *e && *e != '0') ? 1 : 0;
        const char* d = getenv("WINPORT_TIMEDIAG"); diag = (d && *d && *d != '0') ? 1 : 0;
        if (on) { fprintf(stderr, "[UIDT66] WINPORT_UI_DT=1: tick counter 0x8694E8 advances at 60/s of real time\n"); fflush(stderr); }
    }
    static uint64_t last = 0, t0 = 0, calls = 0, added = 0, zero = 0, multi = 0;
    static double acc = 0.0;
    const uint64_t now = ppu_timebase_now();                       /* 79.8 MHz guest timebase */
    uint32_t inc = 1;
    if (on) {
        if (last) {
            acc += (double)(now - last) * (60.0 / 79800000.0);
            if (acc > 4.5) acc = 4.0;                              /* game-style cap: drop long hitches */
            const double r = acc + 0.5;
            inc = r > 0.0 ? (uint32_t)r : 0u;
            acc -= (double)inc;
        }
        last = now;
    }
    if (diag) {
        if (!t0) t0 = now;
        calls++; added += inc; if (inc == 0) zero++; else if (inc > 1) multi++;
        const double wall = (double)(now - t0) / 79800000.0;
        if (wall >= 1.0) {
            fprintf(stderr, "[UIDT66] wall=%.2fs tick_calls=%.1f/s ticks_added=%.1f/s (60=real time) zero=%llu multi=%llu mode=%s\n",
                    wall, calls / wall, added / wall, (unsigned long long)zero, (unsigned long long)multi,
                    on ? "realtime" : "per-frame");
            fflush(stderr);
            t0 = now; calls = added = zero = multi = 0;
        }
    }
    return inc;
}
/* ---- end winport v66-ui-tick ---- */
'''

OLD = ("void func_002B73C8(ppu_context* PPU_RESTRICT ctx) {\n"
       "        ctx->gpr[11] = vm_read32(ctx->gpr[2] + -0x12B0);\n"
       "        ctx->gpr[9] = vm_read32(ctx->gpr[11] + 0x4);\n"
       "        ctx->gpr[9] = ctx->gpr[9] + (int64_t)(1);\n")
NEW = ("void func_002B73C8(ppu_context* PPU_RESTRICT ctx) {\n"
       "        ctx->gpr[11] = vm_read32(ctx->gpr[2] + -0x12B0);\n"
       "        ctx->gpr[9] = vm_read32(ctx->gpr[11] + 0x4);\n"
       "        ctx->gpr[9] = (uint32_t)(ctx->gpr[9] + (uint64_t)wp66_tick_inc());   /* " + TAG + " (was +1) */\n")


def apply_all(engine):
    engine = pathlib.Path(engine)
    p = engine.parent / "gow2-recomp" / "recomp_macos_e435" / "ppu_recomp_001.cpp"
    s = p.read_text(encoding="utf-8", errors="surrogateescape")
    if TAG in s:
        print("v66-ui-tick: ppu_recomp_001.cpp already patched")
        return
    bak = p.with_name(p.name + ".pre_v66")
    if not bak.exists():
        shutil.copy2(p, bak)
    s = rep1(s, OLD, HELPER + NEW, "tick increment func_002B73C8")
    p.write_text(s, encoding="utf-8", errors="surrogateescape", newline="\n")
    print(f"v66-ui-tick: patched {p} -- rebuild the lift with FORCE_REBUILD_LIFT=1")


if __name__ == "__main__":
    apply_all(sys.argv[1])
