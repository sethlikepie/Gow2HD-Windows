# E412–E415 (2026-09-14) — who fills the file size, the stale sticky "done", and giant-lock starvation

Internal notes in English (CLAUDE.md § Idioma). Continues
[E411](2026-09-14-E411-upstream-ab-and-the-fo-size.md).

## E412 — console: the size is written by FIOS op-completion case 7 (oracle)

`tools/rpcs3_probe/oracle_fosize.py` (Z0 only, no watchpoints in the RPCS3 stub): breakpoints on the 25
`std/stw` with displacement 0x48/0x4C and 13 indexed stores of the FIOS window, armed at func_002BAD10
entry with lr=0x41F6C; stopping thread resolved via qfThreadInfo.

```
ST pc=0x0030D3C4 std  ea=0x33018570 val=0x0     (file_new zero-init, main thread)
ST pc=0x0030CAE8 std  tid=0x100000b ea=0x33018570 val=0xC00  lr=0x0030F074   (FIOS scheduler thread)
TAIL op=0x008695D4 FO=0x33018528 FO+0x48=0xC00 loader-op+0x10=0xC00
ST pc=0x0030CAE8 ... ea=0x330185C8 val=0x133C280   (R_PermA, second open)
```

func_0030C7F8(sched, op, result) switches on op+0xC8 (table @0x30C8F8, 10 cases); case 7 (0x30CA88)
copies the stat into the file object: FO=*(op+0x98); FO+0x48 = *(op+0x80).

Ours (probe `patch_e412_fosize_probe.py`, gated PS3_TRACE_FOSIZE): the jump table is complete (10 cases,
3 fragment copies) and case 7 runs correctly for the intro files (SmLogo_v2.m2v 0x12A9D0D, .wav 0x2B2E68,
gow2.psarc 0x1A0839D60) — but **no dispatch at all** during the R_LglScA sync open; the poll reports done
at once with FIOS op 0x430095A0, the op used earlier by the .wav.

## E413 — cause: our STICKY-RESTORE hands a recycled op the previous owner's "done"

July palliative in func_002B4224: if [io+0x90]==0 but the host sticky table (ppu_loader.cpp
ps3_fios_sticky_*, keyed by op ADDRESS; publish on every completion in func_00306534, consume only in the
loader DONE path func_002B4274) says published, force done=1 and write 1 to guest memory. Proven in-boot
(PS3_TRACE_FIOSSCHED): `STICKY-RESTORE #3 io=0x430094C0` immediately before `[DONE-4274] #3` of the R_LglScA
open. With the restore gated OFF (`patch_e413_sticky_restore_gate.py`):

| arm | runs | sync open | shells | ICALL-BAD | intro |
|---|---|---|---|---|---|
| E413 (restore OFF) | 3 | r3=1 in 2/2 valid | 43 | 0 | 11->1 bounce 3/3, stuck 1/3 |
| E412 (restore ON) | 3 | r3=0 3/3 | 6 | 2 | normal 3/3 |

E413 r1: `CASE7 FO=0x43018528 FO+0x48 <- 0xC00`, `[BAD78] ts+0x74=0x00000C00`, `CASE7 ... <- 0x133C280`,
147 lookups idx=14, 0 at map 4.

## E414 — owner-checked restore (keeps the intro workaround, blocks the inherited one)

`patch_e414_sticky_owner.py` (PS3_FIOS_STICKY_OWNER): record op+0xB8 (submitter's user slot) at publish,
restore only if the polling container is that owner. E414 r1 and E413b r1: r3=1, 43 shells, 0 ICALL-BAD.
The intro still bounces 11->1: 24 `STICKY-OWNER-MISMATCH` during the intro — the movie itself polls an op
published for another container (0x008693C4 vs 0x00869E08). So the E412 intro only worked thanks to the
wrong restore; the real completion does not arrive in time -> E415.

## E415 — giant-lock starvation (host `sample`)

Stuck-intro sample (E414, 8 s): 3 FIOS threads woken from sys_lwcond_wait sit 5836/5837 samples in
`ppu_giant_lock_acquire_prio -> pthread mutexwait`; main thread 100% in func_000CE03C -> func_002C0508 /
func_002B4224 (no giant/preempt frames). Streaming sample (rem=0x115C280): FIOS scheduler thread
(func_0030EDD0) 94% in the same acquire. Two holes in ppu_loader.cpp:

1. the E229 timeslice flag was only polled by ppu_memory.h's inline readers; lifted code calls the
   out-of-line vm_read8/16/32/64 (lift header `uint32_t vm_read32(uint64_t addr);`), which never polled it;
2. on preempt the macOS mutex is not fair — the releasing thread re-acquires before the woken waiter runs.

Fix (commit ad993f3a, gated PS3_GIANT_HANDOFF, default OFF): readers poll the flag; a preempt waits until
g_giant_acq_seq moves (another thread took the lock), bounded, before re-acquiring.

First A/B (same binary boot_gow2_e415, round 1): H (HANDOFF=1): intro complete, r3=1, 43 shells, 0
ICALL-BAD, R_PermA `rem 0x123C280 -> 0xB1C280` in 50 s (~150 KB/s; previous runs ~9 KB/s), ~1675 level
texture records (TXR_/GFX_/PAL_HUDA*). Then a stall at `rem=0xABC280`, state 3, body1D4=0x22180: after it
0 `push2` and 6 LFQ2PRE (513 before), no `push BLOQUEADO` — nobody requests the next compressed block.
H0 round 1 (same binary, HANDOFF=0, i.e. the E414 configuration, 420 s window):

| t | rem | shells |
|---|---|---|
| 130 s | intro ending | 24 |
| 160 s | 0x113C280 (R_PermA body) | 43 |
| 342 s | 0xBC280 | 56 |
| 372 s | **0x342A60** (next container) | **1044** |
| 421 s (deadline) | 0x202A60 | 1063 |

R_PermA fully consumed (16.5 MB in ~182 s, ~90 KB/s in this run — E413 r1 had ~9 KB/s: throughput varies
run to run under the starvation); the driver reached state 13 (`[BAD10] #3 lr=0x00042F0C flags=0x10`),
which answers E403's P1/P2 (P2: it gets there once R_PermA completes); 1063 type-15 shells; 0 ICALL-BAD,
0 FREELIST-TAG-GUARD, 0 abort — the E402 "71st shell exhausts the LglScA arena" wall did not appear.
LDRSH=0. Series stopped after this run (H r2 cut) to give the window to a long visible run.
Reading: the handoff speeds the stream (H r1) but stalled mid-R_PermA in its only run; E414 alone reaches the
third container given time. Handoff stays gated OFF until the H stall is understood.

## E415 stall probe (HANDOFF=1 + traces + `sample` at the stall)

`stall_probe.sh`: stall reproduced at 181 s, `rem=0x91C280`, state 3, `body1D4=0x14A00` (H r1 stalled at
`rem=0xABC280` with `body1D4=0x22180`). `f1C4=0x438E0A00` is NOT one special record: H0 r1 has 350 568 ticks
with that destination — it is the body staging buffer for every record (retracts "deterministic on the
record", said in chat).

Two samples at the stall (8 s each): **no thread in ppu_giant_lock_acquire** (the handoff removes the
starvation); the 4 FIOS threads (0030D8B8, 0030D7D8 x2, scheduler 0030EDD0) and the 8-worker pool
(func_00463598) idle in sys_lwcond_wait; main thread ticking the loader. `[HOSTINFL]` is printed uncapped
(lfq_host_inflate.c:105): last one 10 k lines before the stall, none after — inflates really stop.
`[FIOSSCHED]`/`[FIOSOPEN]`/`[LFQ] push2` stop long before the stall because of their print caps (done90 cap
24, push2 exactly 16): "0 after the stall" for those is a cap artifact, not evidence (retracted in chat).
`[AREAD-HLE]` = 0 lines, BUT the entry probe of func_002B3D1C prints `[AREAD] #n` (cap 256), which fired 86
times: the stream refills DO go through func_002B3D1C (the earlier sentence "does not go through" is
retracted — I had grepped the wrong tag). **The last submit, `[AREAD] #86`, is 5 lines before the stall**:
loader op 0x008695D4, FO 0x43018580 (R_PermA), file position 0xA20000, 128 KB. The read was requested and
never serviced (no HOSTINFL after, FIOS threads idle): a lost wakeup on submit.

Candidate cause (to test, E416): the lwmutex HLE GoW2 uses is a stub without mutual exclusion (CLAUDE.md,
ppu_sysprx.cpp); the giant lock was the de facto exclusion because it only changed hands at syscalls. E415
hands it over inside any vm_read, so the FIOS scheduler's "queue empty -> wait" can interleave with the
submitter's "enqueue -> signal": the scheduler then samples gen0 after the bump and waits for the next
signal. Fix candidate: no giant-lock handoff while the thread holds a guest lwmutex.

## E416 — no handoff while holding a stub lwmutex (commit 22b8cca3): starvation comes back

Premise measured: `lwm_stub_on()` returns 1 unless PS3_LWMUTEX_REAL is set, so GoW2 runs the stub (the
comment in lwm_lock_common naming PS3_LWMUTEX_STUB is stale). Rule: per-thread `g_ppu_lwm_depth` (++ on stub
lock, -- on stub unlock); with PS3_GIANT_HANDOFF=1 a thread with depth>0 is never preempted.

L r1 (E416, HANDOFF=1): intro stuck `0>1>11>1`, no loader. `sample` (6 s) of that state: FIOS scheduler
(0030EDD0), the sys_timer_usleep thread (004553A8) and all 8 workers (00463598) 100% in
`ppu_giant_lock_acquire_prio -> acquire_raw`; main thread running the intro loop. The E415 stall sample had
no thread there. So the rule turns the handoff off exactly in the hot loop: the main thread has depth>0
while it spins. Either the counter leaks (an unlock path not through ppu_sysprx.cpp's stub) or the main
thread really holds a guest lwmutex across the poll. Series stopped after L r1 (H0b r1 cut).
Diagnostic E416b (PS3_TRACE_LWMDEPTH, commit fcaf66cf), one run, HANDOFF=1, stopped in the intro bounce:
26 057 skipped preempts — host tid 767861 (main) depth=1 x26 005, depth=2 x122; tid 768095 depth=7 x6.
The first 80 stub lock/unlock pairs balance back to 0. So not a leak: the main thread really holds one
guest lwmutex across its poll. The E416 rule is too coarse — it protects the FIOS scheduler's short
"check -> wait" window but also pins the main thread's long poll, which brings the starvation back.
Next: real per-lwmutex exclusion, which exists (PS3_LWMUTEX_REAL=1; CLAUDE.md: passes the intro since
E229/E233). With it the depth counter never moves, so boot_gow2_e416b tests REAL+HANDOFF without a rebuild.

## E417 — real lwmutex (PS3_LWMUTEX_REAL=1): the whole front-end load in ~20 s, plateau at 1465 shells

boot_gow2_e416b, ab_rounds3 (DL 480, idle 60), config E414 + PS3_LWMUTEX_REAL=1:

| run | handoff | intro | shells | ticks | end |
|---|---|---|---|---|---|
| R r1 | on | clean `0>1>10>11>0` | 43 | 60 | stalled in R_PermA (state 3, rem 0x12BC280) |
| R0 r1 | off | bounce, recovers | **1465** | 2807 | loader idle, state 2 rem 0 |
| R r2 | on | clean | **1465** | 1407 | idem |
| R0 r2 | off | bounce, recovers | **1465** | 2086 | idem; 1 ICALL-BAD ctr=0xF0 in func_004553A8 -> func_004B9C58 |

R0 r1 progress file: at t=20 s already 1465 shells; the four driver loads (lr 0x41F6C, 0x41FCC, 0x42F0C,
0x42AA4), 455 distinct rem values (several containers) — what E414 did in ~800 s (and did not finish)
happens in ~20 s once guest lwmutexes exclude for real. After it: loader idle (state 2, rem 0), only SPU
workload activity, no BAD10 #5, LDRSH=0. The early R r1 stall (1 of 2 with handoff) is still open.
Next: oracle — does the console run func_0032109C ([LDRSH]) in this front-end phase, or only after the menu?

Oracle (`oracle_ldrsh.py`, skip-intro ELF, attach at 3 s):

| t | console | ours (E417 R0 r1) |
|---|---|---|
| 0.0–0.3 s | SHADERSRC (func_003CC208, lr 0x1621CC) x8, r3 = 0x330A2E64 ... 0x333989C4 | same objects 0x430A2E64 ... 0x433989C4 (#7–#14 of our 18; heap base +0x10000000) |
| 0.4 s | BAD10 #1 lr 0x41F6C, #2 lr 0x41FCC | same |
| 3.8 / 4.3 s | BAD10 #3 lr 0x42F0C, #4 lr 0x42AA4 | same |
| 7.7–12.9 s | **BAD10 #5..#13**: lr 0x439AC, 0x428C8, 0x437AC, 0x43B94 x2, 0x439AC x4 | **none: we stop after #4** |

SHADERSRC in ours: 18 hits with N = 2,1,16,1,12,24,1,2,8,1,1,776,3,16,20,1,3,1 (an earlier count of 0 used
a wrong grep pattern — retracted). LDRSH: **not hit on the console in the whole 312.9 s window** (final counts BAD10=13, SHADERSRC=12, LDRSH=0;
RPCS3 headless, no pad input). So the ICGLdrShader body does not run in the autonomous front-end: LDRSH>0
needs the game to leave the menu into a level. Ours has PS3_PAD_AUTOSTART (START then CROSS every ~200 pad
reads, cellPad.c:455), so it is reachable once our front-end matches the console.

Driver switch (jump table right after the bctr at 0x42348, base 0x4234C, 26 cases; case k -> 0x4234C+entry[k]):
BAD10 call sites map to cases #3 -> 13 (0x42DFC), #4 -> 21 (0x42998), #5 -> **16** (0x43880), #6 -> 23 (0x42818),
#7 -> 3 (0x43684), #8/#9 -> 2 (0x43A60). Console order of cases: 13, 21, 16, 23, 3, 2, 2, 16 x4. Ours: 13, 21,
then nothing — case 21 writes state 22 (E403) and we never reach case 16.

Why (PS3_TRACE_REG run, E416b + LWMUTEX_REAL): [LDRSTATE] alternates `22 -> 0 -> 22 -> 0` forever. Case 22
(0x428D8) is `r3 = func_0003F2B0(); if (r3 != 0) return;` and func_0003F2B0 is only a wrapper around
**func_002BA76C, the loader tick** (returns its r3). So the driver waits for the loader to report "done"
(r3 = 0) and ours keeps answering busy from state 2 with rem = 0 (f1BC = f1C8 = 0): the async container of
BAD10 #4 (desc 0x00543510) is consumed but end-of-stream is never recognised. (The old host F2B path had a
palliative for exactly this, F2B-STREAM-EOF-DONE "... -> state=0".)

The loader tick does have a time budget: it reads the clock through func_0025C0D4 (deadline = t0 + 0x80000 -
32276) and loops over states until it expires. This retracts the in-chat claim "time budget weakened, no
loader function reads the timebase" — the read goes through a function outside the range scanned then.
Next wall: our driver does not reach the case that issues BAD10 #5 (site 0x439A8). Driver state probe is
[LDRSTATE] (E130, gated PS3_TRACE_REG).

## E418 / E418b — the last container is read completely; the EOF-read hypothesis falls

Loader tick (func_002BA76C) static map: one "done" return (li r3,0 @0x2BA898, closes the container via
func_002B3F78, frees f1C4, state 0) reached only when r29 = 1; the only `li r29,1` is @0x2BAA28 in the
state-2 handler, which requires st+0x10 (stream bytes available, st = *(ts+0x1A8)) == 0, rem (ts+0x1AC) == 0,
state != 3, f1C8 == 0. Otherwise the tick returns 1 (@0x2BA918) and driver case 22 keeps waiting.

E418 (`PS3_TRACE_EOF`): its AT-EOF cap (64) was consumed by the R_LglScA sync loop at the start (op 0x008695D4,
size 0xC00 = pos, io 0) — a cap artifact again. E418b (`PS3_TRACE_EOFD`, change-log, cap 4000): the container of
BAD10 #4 (FO 0x43018580, size **0x16D4C40**) is submitted to the very end — submit #432 pos 0x16C0000 n 81920,
**#433 pos 0x16D4000 n 3136** (ends exactly at the size) — and polled to `done=1` with `pos=size`, then the op is
released (`io=0`). All data arrived. Retracted: "the EOF read never completes". The remaining gate is
st+0x10 != 0 (leftover < 32 bytes) -> E418c (`PS3_TRACE_ST2`) measures it.

E418c: its change-log cap (3000) ran out mid-R_PermA (last line rem=0x11C280, line 74 753 of 112 920, no
heartbeat) — a third cap artifact in a row; the plateau is not in it. It does give the stream layout:
st = 0x4007FCD0, +00 ring base 0x40083D40, +04 container total (0xC00 LglScA, 0x20000 per R_PermA window),
+08 read cursor (+0x20 per record header), +0C ring size 0x40000, +10 available. For R_LglScA the done gate
works: with rem 0, +10 goes 0xA0 -> 0x80 -> 0x40 -> 0 and the container closes. Next: E418d, the same dump
restricted to rem == 0 passes (container ends + plateau only).

## E418d — at the plateau every "done" condition is true, yet the loader stays in state 2

> **INFERENCE RETRACTED by E418e (below).** "The loader stays in state 2" was read off the last line of a
> change-log that only prints in state 2; E418e measures the done path running (`set-r29 #4` -> `done` ->
> tick returns r3=0, state 0). The E418d measurements (last pass avail=0 rem=0 state=2 f1C8=0) stand. Same
> for E417's "ours keeps answering busy from state 2 with rem = 0": the tick answers done for container #4.

E418d (`PS3_TRACE_ST2Z`, rem == 0 passes only, 235 lines, cap not reached): the last pass before the plateau is
`total=0x14C40 cursor=0x14C40 avail=0 rem=0 state=2 f1C8=0` (0x14C40 = 0x16D4C40 - 0x16C0000, the last window of
the BAD10 #4 container). So the suspected leftover (avail != 0) is refuted: all state-2 done conditions hold.

Lifted fragments of the tick (func_002BA76C split into ~20): func_002BA9BC sets r29=1 and trampolines to
func_002BA824 with no _cs_ restore; func_002BA824 / func_002BA808 / func_002BA7F0 test r29 at loc_002BA854
before any _cs_ restore, loop to func_002BA808 (not to func_002BA7F0, whose entry does `li r29,0`), and keep
the done path intact (func_002B3F78 close, state 0, return 0). The wrong-fragment-fallthrough hypothesis is
refuted by reading. The only call between setting and testing r29 is func_0025C0D4 (clock). Next: E418e
measures whether `li r29,1` runs, r29 at loc_002BA854, the done write, and the r3 returned to func_0003F2B0.

## E418e — the loader closes; the wall moves to the first menu frame (render stuck in func_00286D44)

Probe `patch_e418e_tick_done_flow_probe.py` (`PS3_TRACE_TICK2`, OFF), lift clone recomp_macos_e418e, env
`PS3_LWMUTEX_REAL=1`, 2 runs (r1 64 s, r2 with `PS3_TRACE_REG` + two `sample` snapshots 10 s apart):
- the done path runs for every container: `set-r29 #1..#4` -> `done fn=func_002BA824` -> the tick returns
  `r3=0 state=0` to func_0003F2B0 (#4 at the plateau). [LDRSTATE] (driver) leaves the `22 <-> 0` alternation
  and stays **0** after done #4.
- right after it: `[CONTENT] boot logo queue DONE — hold cleared (guest/menu)`, one SetFlipCommand, then
  **no more flips** (60 955 flips before the done, 1 after; r1 and r2 alike).
- `sample` (both snapshots, same chain): the main PPU thread is busy in
  `func_002B2660 > func_00424588 > func_00423AB4 > func_0027FDAC > func_00288098 > func_0027D240 >
  func_0028C168 > func_0028AF98 > func_0028A83C > func_00286D44`, spending its time in vm_uncommitted /
  vm_oob / ppu_rsv_on_store: func_00286D44 writes float vertex data at **0x51000000+** (the first uncommitted
  byte: committed range #1 is 0x0..0x51000000; first bad write exactly at 0x51000000 +0) and reads fields of
  an object at **0xE0000000** (+0x14, +0x24, +0x28, +0x90, +0x98, +0xA0; committed range #2 ends there).
  Zero bad accesses before the done; 40 + 40 printed after it (print cap).
So the plateau is not the loader and not the pad: the first menu frame never finishes. Both bad bases sit
exactly at the end of a committed range. Next: where func_00286D44's output pointer and the 0xE0000000 object
come from (static read of func_0028A83C / func_00286D44, then the console value via the oracle).

## E418f / E419 — a lifter defect ran the first menu frame through all of guest memory

E418f (`patch_e418f_menu_loop_bounds_probe.py`, `PS3_TRACE_MLOOP`, OFF): the inner loop of func_00286D44
(PPC 0x286DF0..0x286E88, `for (p = r18; p != end; p += 0x60)`) starts with begin=0x0FEFEB70 (r1+0xC0) and a
lifted end=0x0FEFEB88 (r1+0xD8): (end-begin) mod 0x60 = 0x18, so the loop never ends (one [MLOOP] line in the
whole run) and writes vertex data from the stack upward until 0x51000000 (the first uncommitted byte of E418e).

Cause (PPC vs lift): r27 is loaded from three frame slots in that tail-entry fragment of 0x286BE8 —
`ld r27,0x1B0(r1)` @0x286DCC (array base, r1+0xD8), `ld r27,0x1C8(r1)` @0x286DF4 (the loop end) and
`ld r27,0x250(r1)` @0x2873E0 (the real callee-save restore). tools/ppu_lifter.py keyed the pure-tail-entry
memory snapshot by REGISTER (`_mem_snap.setdefault(reg, off)`), so all three became `_cs_27 = *(r1+0x1B0)`.

Fixes:
- lifter (ps3recomp 28b22bbd): the snapshot covers one (register, offset); a load from any other slot stays a
  plain load. Conformance suite 1323/1323 and the midasm golden (byte-identical) pass. `PS3_LIFT_CS_AUDIT=1`
  lists the sites; a full re-lift of the GoW2 EBOOT finds **3 sites in 2 functions** (func_00286D44 r27 @0x1C8
  and @0x250, func_00290D44 r31 @0x260) — the class is small, not systematic.
- existing lift: `patch_e419_cs_multi_offset_286d44.py` and `patch_e419b_cs_multi_offset_290d44.py` (the
  re-lifted func_00290D44 differs from the old one on exactly that line).

Measured with E419 (1 run, `PS3_LWMUTEX_REAL=1`): [MLOOP] repeats (cap 40 reached, the loop ends), zero
uncommitted/OOB accesses, and after the loader done the game runs its own shader system for the first time:
`time to load/compile shader ($/enginesupport/shaders/PS3/GOWShader.ps3fx [Unknown combination string, disable
USE_PRECALCULATED_SHADER_CRCS in CGOWShader.cpp]) 696 ms`, then Resolve, Fog, SkyboxBloom, Downsample, Blur,
Upsample, Fade .ps3fx. Then SIGSEGV at 23 s (crash report): main thread in our `cellSyncLFQueueSize` HLE,
reading the guest EA 0x47D59A88 as a host pointer (func_0014F810 -> import thunk). The cellSync
GetDirection/Depth/Size HLEs used the host-struct model; they now read the guest CellSyncLFQueue layout
(pop1.h1 @+0, push1.h5 @+8, depth @+0x14, direction @+0x24; RPCS3 semantics). LDRSH still 0 in that run.

Console comparison (RPCS3 TTY.log, headless oracle run): the game's shader-compile lines appear in the same
order in both — FlashUI, Default, GOWShader (the "Unknown combination string" warning is printed on the
console too: normal, not ours), Resolve, Fog, SkyboxBloom, Downsample, Blur, Upsample, Fade — ours = console
#1..#10. Only difference: Default `CONSTCOLOR=1` in ours vs `CONSTCOLOR=0` on the console (unexplained, noted).
The console's #11 is Particle.ps3fx, where our E419 run crashed. None of these go through func_0032109C
(LDRSH = 0 on both sides), consistent with E417: ICGLdrShader is the level-shader path.

## E419b / E420 — past the menu: the CROSS bit was in the wrong place

E419b (cellSync fix, 157 s, idle stop): 11 engine shaders (= console list incl. Particle), 2271 shells, flips
continue to the end (205 in the last 1 % of the log) — alive on the menu, not stalled; LDRSH 0. New signals:
40 uncommitted reads at EA 0x67600000+ from our `rsx_process_fifo` (io 0x47E00000 + get 0x1F800000), and 5887
`[SPU] indirect branch to unknown LS address 0x29A10 (image 0)` (spu0 is lifted only for LS 0x3010..0xF710).

cellPad (read, then measured): our CellPadData writer put the L2..SQUARE half of the combined mask into
digital2 as `buttons & 0xFF00`, i.e. CROSS = 0x4000; libpad/RPCS3 carry it in the low byte (CROSS = 0x40).
Fixed in both GetData paths (+ `PS3_TRACE_PAD` tap trace). E420 (only cellPad changed vs E419b, 1 run,
302 s, stopped by the deadline, not idle): loader containers done **14** (was 4), shells **6460** (was 2271,
still rising at the end), shaders **13** (+ShadowVolume, ApplyShadow), LDRSH still 0; 80 FIFO reads as in
E419b (fix not in this binary). Container names seen in the log (counts of mentions): R_ShellA 92, R_HUDA 64,
R_LglScA 54, R_Perm 51, R_Weapon / R_SubWeapon / R_SavePt / R_Rhsold00 / R_Hero / R_Colsus00 36 each, R_Vis 22,
R_ShellVis 18 — the game left the front-end and is loading the first level (Colossus of Rhodes).

Retracted in the same round (said in chat): "on this Mac the keyboard/mouse marks port 0 connected and turns
the autostart off" — inferred from cellpad_gamecontroller.m, refuted by the trace: all 64 `[PADTAP]` show
`host_on_port0=0` in the headless run. The autostart ran in the disconnected path all along; the change that
mattered is the digital2 byte. The connected-path OR stays (harmless, and right for a window run with a
keyboard) but is not what moved E420.

SPU signal in E420 (measured; the cause is still an inference): the first `[spu-dma] SKIP out-of-range GET
ea=0x00000000 size=4 lsa=0x2FD70` (log line 86 444, right after loader done #5 at 86 122, i.e. the level load)
is followed on the very next line by the first `[SPU] indirect branch to unknown LS address 0x29A10 (image 0)`.
Later a `GET ea=0x00003300 size=13184 lsa=0x0BC00` is refused ~134 M times (power-of-two print cap: #134217728
seen) — LS 0xBC00 is code in spu0 (spu0_spu_func_0000BC00), so this is a code/data upload from a null base +
0x3300. All seven lifted images register without spu_begin_image, i.e. with image_id 0 (the wildcard in
spu_lookup), so "(image 0)" does not name spu0; LS 0x29A10 lies outside every lifted image (highest: spu2,
0x4000..0x11AB0). Inference: a SPU job reads a pointer through a null field (EA 0) of its descriptor; the refused
DMA leaves LS 0x2FD70 stale and the job branches through it. E421 runs with PS3_SPU_BRPROBE=1 to name the call
site (lr) and where the bad word sits in LS.

## E421 — after the level's 14th container the main thread waits for an LFQueue to empty

E421 (FIFO CALL/RETURN build, 600 s, deadline): same end state as E420 — loader done #14 (last asset
MAT_swordtrail), then no new container for the rest of the run (2/2 runs); 13 shaders, 6538 shells, LDRSH 0,
render alive (5048 flips after done #4). `rem=0x2620` in the .prog is a stale print: it is the last WADLD-SM
pass before done #14 (16 lines later), not a stall of that container.

`sample` x2 (10 s apart), main PPU thread, both identical: `func_00424588 > func_00423AB4 > func_00293010 >
func_00249398 > func_0014F810` in `__semwait_signal` / vm_read32. func_0014F810 (36 lifted lines) is
`do { size = cellSyncLFQueueSize(*(r30+0x234)); if (size) sys_timer_usleep(10) /* syscall 0x8D */; } while (size)`.
The queue is `[LFQ] init q=0x47D59A80 buf=0x47CE9A10 size=0xE0 depth=2048 dir=2 (PPU->SPU) taskset=0x47CE7100`
— the E419 crash read 0x47D59A88 = this queue + 8 (push1.h5). Its consumer is the SPURS task at context
0x47CE7100 (`[SPURSTASK] ... *arg=[0 47D59A80 0 0]`). **buf = 0x47CE9A10 is the exact value the failing SPU
code branches to** (`[SPUBRP] pc=0x47CE9A10 target=0x29A10 lr=0xD9E0 site=0xD9DC`, spu0 `r12 = *(u32*)(r3+12);
bisl $0,$12`, the word also sits at LS 0x2FD1C / 0x2FD9C near sp 0x2FCF0). Traces of pushes show pop.h1
following the push position (570/570, 50/50, 612/613), so the consumer does pop at least part of the time.
Our push HLE writes push3.m_h5 at +8 and reads pop.h1 at +0, the layout cellSyncLFQueueSize reads — so a
Size/layout mismatch is not supported by reading; E422 measures Size's return (PS3_TRACE_LFQSIZE).

E422 (1 run, 301 s, deadline; same end state, done 14, LDRSH 0): 449 lines, all for q=0x47D59A80. Size is 0
only early (pop.h1 = push.h5 = 1, 2, 3...); after that the queue moves (heartbeats every 200 000 calls:
pop.h1/push.h5 = 72/75, 308/315, 555/570, 684/685, 868/873, 1006/1020 -> size 3, 7, 15, 1, 5, 14) and the wait
loop called Size ~9.8 M times. Size reads the counters correctly and neither side is frozen: the main thread
waits for an exact 0 while producers keep pushing and the SPU consumer drains slower. Inference (unmeasured):
consumer throughput is lost to the aborting SPU runs (the 0x29A10 branch). E423 dumps the SPU object at the
failure (r3 + LS window) and the pc/lr of the refused EA-0 DMA.

E423 (probes of f16d33f2, PS3_SPU_BRPROBE=1): at the failing call (spu0 0xD9DC) `r3=0x2FD90` and LS 0x2FD90 =
`DC632100 DC632100 DC632100 47CE9A10` = {begin, end, current, callback}: begin == end, so the first append
overflows and calls `callback`. Static read of spu0 0x3AC8..0x3BAC: the object at sp+0x40 is built as
{begin=r9, end=r9+*(r82+8), current=r9} by shufb inserts into the OLD quadword at sp+0x40 — word 3 (callback) is
never written there, so it is whatever the stack held (0x47CE9A10, the queue's buffer EA). With *(r82+8) = 0 the
fill loop is skipped and the flush fires at once. The refused DMA is `[spu-dma] SKIP GET ea=0 size=4 lsa=0x2FD70
pc=0x03750 lr=0x03AD0`: block 0x3AC8 (after the call returning to 0x3AD0) GETs 4 bytes from EA *(r82+28) into
sp+0x20 (0x2FD70, zeros in the dump). So the block at r82 (= r3 on entry to spu0 0x3828) has 0 at +8 (a count)
and +28 (an EA). Cause of those zeros: open (next: what r82 is — likely the element popped from q 0x47D59A80).
Also seen: two unlifted `heqi` asserts (`TODO spu: .word 0x7F000B80` / `0x7F002100`) in this code; harmless.

E424 (brprobe walks the back chain; PS3_TRACE_LFQHDR): at the failure, D930's frame (bc 0x2FD50) holds the
caller's saved r80 = 0x2FD90 (the object, consistent), r81 = 0x10000 and **r82 = 0x00000000**. So inside the body
of spu0 0x3828 (fragments 0x39D0 / 0x3A78 / 0x3AC8 / 0x3BAC) r82 — which 0x3828 sets to its argument P and which is
callee-saved in the SPU ABI — is 0 by the time it reads *(r82+8) and *(r82+28): those are LS 8 and LS 28, zeros.
That explains both the count 0 and the EA-0 DMA. The data is not missing: the PPU pushes real elements (106
[LFQHDR] lines on q 0x47D59A80, e.g. `00000003 489FD480 00BFD480 00000080 | 00000005 41C11EC0 418A1340 000001D0 ...`).
Static read: 0x3828 = `P` in r82; the spu0 callees it makes are 0x3608, 0x36F0 (-> 0x3750/0x3820) and 0x5A18
(from 0x3A78); only 0x5A18 writes r82 (saves it at sp-0x30, sets r82 = r3). Suspect: an exit path of 0x5A18 that
does not restore r82 (SPU-lifter callee-save class). Checked by reading and cleared: 0x5A18 saves r82 at
sp-0x30 before `sp -= 208`, and every exit (via 0x5AE0 / 0x5C00 / 0x5C10) ends in 0x5C4C, which does `sp += 208`
and `r82 = lq(sp-0x30)` before returning. No callee of 0x3828's body leaves r82 changed, so either P is already 0
when 0x3828 is entered or something else enters it (it is in the indirect-branch registry; the only direct call,
spu0 0x3550, passes r10+4). Next: a gated probe at the entry of 0x3828 (r3, lr) — measurement, not reading.

E425 (`patch_e425_spu0_3828_r82_probe.py`, PS3_TRACE_SPU3828, 1 run, 201 s): 40/40 entries of 0x3828 have
`P=0x0003FEC4 lr=0x03560 sp=0x3FE60` (valid descriptor; frame sp should be 0x3FD50), but 40/40 checks right after
its call to 0x5A18 and 40/40 after 0x36F0 show `r82=0x00000000 sp=0x2FD50` — **the stack pointer is exactly
0x10000 lower than it should be**. Callee-save restores (`r82 = lq(sp-0x30)`) then read a zero-filled LS region,
so r82 = 0; the same shift explains the object at 0x2FD90, the EA-0 DMA into 0x2FD70 (= sp+0x20) and E424's
saved r81 = 0x10000. The pushed data was never the problem. Refines E424's "a callee does not restore r82": the
callees restore from the right *offset* of a wrong *sp*. Static reading of 0x3608 / 0x36F0 / 0x3750 / 0x3820
(no nested calls) and of 0x5A18 (sp -208 / +208, calls 0xD300 -> 0xD210, 0xD638, 0x50B0 -> 0x5138) found no
0x10000 adjustment. Next: E426 checks sp balance at every direct call in spu0 and names the callee that
returns with a different sp.

E426 (`patch_e426_spu0_sp_balance_probe.py`, PS3_TRACE_SPUSP, 75 call sites wrapped, 1 run, 193 s): only two
callees ever return with a different sp — `spu0_spu_func_000050B0 returned to 0x05C40 with sp 0x3FC80 -> 0x2FC80
(delta -65536)` (20x) and, inheriting it, `spu0_spu_func_00005A18 returned to 0x03A90 with sp 0x3FD50 -> 0x2FD50
(delta -65536)` (20x). The lifted 0x50B0 allocates its frame with `ctx->gpr[11] = spu_il(-66880);
... sp += r11` (spu0_v2/spu_recomp.c:2277/2304), but `il` carries a 16-bit signed immediate and the same prologue
stores the back chain at sp-0x540: the real immediate is -1344 (0xFAC0), and -66880 = -1344 - 0x10000. The
epilogue gives back 1344, so every call leaves sp 0x10000 lower. **Root cause of the SPU chain (E420-E425): a
double sign-extension of the `il` immediate in the old SPU lifts.** Scope: out-of-range `spu_il` immediates in the
tracked lifts — spu0_v2 43, spu1_v2 39, spu2_v2 54, spu3_v2 43, spu4/5/6_v2 0 (lifted later); every value is
(negative s16) - 0x10000 (e.g. -69632 = -4096 - 65536, 8x in spu0).

E427 fix (gow2-recomp d0a4413, `patch_e427_spu_il_double_sext.py`): ps3recomp's tools/spu_disasm.py already
carries the `il` fix (its comment: "Extending it again mapped every NEGATIVE il immediate to (imm - 0x10000)"), so
spu0..3_v2 are stale lifts from before it. Other immediate forms checked in all four: ilh 0/8, ilhu 0/724,
iohl 0/0, ila 0/445 out of range — only `il` is affected. The script rewrites only `spu_il(v)` with v < -32768
to v + 65536 after checking the result is a negative s16 with the same low 16 bits: 43 + 39 + 54 + 43 = 179
immediates, second run ALREADY, 0 left, spu0 0x50B0 now `spu_il(-1344)`; the four SPU objects rebuilt.
A full re-lift of spu0..3 with the current tool is the principled follow-up, but it also brings the newer
heqi/fence decodes at once — kept separate so this change can be measured alone.

E427 run, interim at ~91 s (log line ~128 800): `indirect branch to unknown LS address` **0** (E420-E426: thousands
per run, 0x29A10), `SKIP ... GET ea=0x00000000` **0**, shaders 13, loader done 14 (last asset MAT_swordtrail, as
before), LDRSH 0. `sample` x2 (main_path.py, heaviest path): the main thread is no longer in the LFQueue wait
(func_0014F810); it runs `func_00424588 > func_00423AB4 > func_00293010 > func_00248F3C > func_00156D78 >
func_001658A0 > func_00159EE4 > func_0014E4E0 > func_002F0DD0`, samples spread (heaviest leaf 42-44 of ~3500),
not parked in one wait. Final numbers (401 s, deadline): unknown-LS branches 0, DMA SKIP 0, loader done still
**14** (last asset MAT_swordtrail), shaders 13, LDRSH 0, SHADERSRC 18, shells 6720 (slowly rising), 80 FIFO reads
at 0x6760.... The SPU chain is fixed; the level load still requests nothing after the 14th container — the next
wall is past the LFQueue wait, not in it. Render alive after the 14th container: 8079 flips after done #14,
2024 in the last 10 % of the log.

Visible Metal run of the same binary (run_visible2.sh, PS3_RSX_BACKEND=metal): it reaches real game content in
Metal — 10 engine shaders compiled, PSOs compiled from decompiled game FPs (one with **6 instructions**, earlier
runs only had 1-instruction blits), 72 bind_texture — then SIGSEGV at 30 s. Crash report: main thread,
`objc_retain+16 <- metal_present+10848 <- cellGcmSetFlipCommand`, address 0x3a09806f6a0. Disassembly of that
spot + `nm` on the binary: the loop over `s_draw.draws[i]` (records of 0x20 at _s_draw+0x38, count at
+0x2038 = METAL_MAX_DRAWS 256) skips prim == -1 and retains `dr->pso ? dr->pso : s_draw.pso`. rsx_metal_backend.m:
MetalDrawRec.pso and MetalShaderPso.pso are `__unsafe_unretained`, and when shader_cache is full (32) the victim is
`CFRelease`d at once (line 2871) while draws recorded earlier in the same frame still hold it via
metal_draw_pso_now() -> the replay at present retains a freed object. Fix in ps3recomp: evicted PSOs go to a
retired list released after the frame's draws are encoded. Same ownership pattern (not yet seen crashing): the
surface cache (lines 419/430) and the depth-stencil cache (line 348).

E428 visible run (retired-list fix, ps3recomp cadb1fab): got further (done 5, 11 shaders, 89 PSO lines by 30 s)
and then SIGSEGV again at 40 s. The fix's fallback fired right before it: `[RSX metal] pso_retired full (256) --
releasing an evicted PSO immediately` at log line 34 237 of 35 019. So more than 256 PSOs were evicted from the
32-entry shader_cache between two metal_begin_frame calls — the cache thrashes (also a likely cause of the visible
runs' slowness) and a per-frame retired list cannot bound it. The retired list is not enough; next: each draw
record owns a reference to its PSO (CFRetain at record time, CFRelease when the list is reset), so eviction can
release at once without touching recorded draws. The E428 crash report has the same frames as E427
(`objc_retain+16 <- metal_present+10848 <- cellGcmSetFlipCommand`, address 0x2dd036ed5a68). Implemented:
`metal_draw_rec_set_pso()` CFRetains at each of the 4 record sites; metal_begin_frame CFReleases the records'
references before resetting draw_count (the retired list stays; its immediate-release fallback is now safe).

E429 visible run (record-owned PSO references, ps3recomp ab2e6c86; interim at 480 s): **no crash** (E427/E428
died at 30/40 s), and the window run reaches the same point as headless — loader done 14, 13 engine shaders,
77 bind_texture, 6722 Metal PSO/pipeline log lines, LDRSH 0. Final (602 s): stopped by the deadline
(`rc=137` = the script's kill, not a crash), 24 913 flips, `pso_retired full` fired once (harmless now that
records own their PSO), same plateau (done 14, 13 shaders, LDRSH 0). Next measurement: E430 = shader_cache 256
(ps3recomp 77733570) with PS3_TRACE_METAL_CACHE eviction totals.

E430 visible run (shader_cache 256 + eviction counter; interim at 364 s): no crash, done 14, 72 bind_texture —
but already 82 `[RSX metal] shader_cache evictions=` lines (one per 1000), i.e. ~82 000 evictions with 256
entries: the cache size is not the problem, the key is. rsx_metal_backend.m:2800-2804 hashes the WHOLE fragment
program (`fp_hash = metal_fnv1a(ucode, rsx_fp_program_size(ucode))`); NV40 FPs carry their constants inline (a
16-byte block after each instruction that reads one), so a title that patches FP constants per draw makes every
draw a "new" shader -> compile + evict. Hypothesis (measured count, read cause): a hash that skips the inline
constant blocks is needed, AND the constants must then reach the MSL through a per-draw buffer instead of being
baked in as literals (otherwise a reused PSO would draw with stale constants). Checking how the FP decompiler
emits constants.

E430 final (602 s, deadline, no crash): **244 000 evictions** with shader_cache 256, done 14, 13 shaders,
24 320 flips, LDRSH 0. rsx_fp_decompiler.c:193-200 emits each inline constant as a `float4(...)` literal, and the
D3D12 backend keys its cache the same way (FNV-1a of the whole ucode). Fix being written: a decompiler mode that
reads constants as `rsx_fc[k]` from `[[buffer(0)]]`, a hash of the instruction words only, and a per-draw copy
of the current FP's constants bound at present (Metal only; literal mode stays the default elsewhere).
Written in ps3recomp 7725c4a1 (rsx_fp_hash_and_consts + rsx_fp_set_const_buffer_mode in the decompiler; Metal:
instruction-only key, per-draw fc_buf copy at record time, setFragmentBuffer at present; opt-out
PS3_METAL_FP_LITERAL_CONSTS=1). The lib compiles; libs/video/tests/test_fp_decompiler.c built standalone gives
**7 passed, 2 failed** — identical to the baseline recorded in CMakeLists.txt:272 (mov_dest / mad_const), so the
default literal mode is unchanged. In-game measurement (E431 visible) pending.

E431 visible, interim at 91 s (constant-buffer build): **16 eviction lines already** (~16 000 evictions), about the
same rate as E430 (82 lines at 364 s). So hashing without the inline constants does NOT shrink the key space —
retracted in the same round (said in chat): "the per-draw constant patching is what defeats the cache". The count
stands, the cause falls: either the FP instruction words themselves differ between draws, or vp_hash does
(hash of the whole transform_program array). The constant-buffer change stays (constants per draw is the
faithful model) but it is not the thrash fix. Next: a gated sample of (fp_hash, vp_hash) on misses.
E431 final (602 s, deadline, no crash): **192 000 evictions** (E430: 244 000, same 600 s window) — ~21 % fewer,
not a collapse (could be run variance); done 14, 13 shaders, 79 bind_texture, LDRSH 0.

E432 visible (miss probe PS3_TRACE_METAL_MISS, 1 run): the first 300 misses have **299 distinct fp_hash, 2 distinct
vp_hash and 300 distinct FP EAs**, stepping down by 0x80 (0x003F7A00, 0x003F7980, 0x003F7900, ...), fp_bytes
32..192, vp_words 16. The thrash is the fragment side, and the EAs look like raw offsets. Cause (read, fixed in
ps3recomp): NV4097_SET_SHADER_PROGRAM carries the DMA context in [1:0] as libgcm writes it — location + 1, raw
1 = local, 2 = main (rsx_dispatch.h:211; RPCS3 `(value & 3) - 1`) — but rsx_commands.c documented "0 = local,
1 = main" and metal_resolve_fp_ea (and rsx_d3d12_backend.c:2337-2346, same code) mapped raw 0 -> local and
returned every other value as a flat EA. GoW2's FPs are raw 1 (a local-memory ring at offset ~0x3F7xxx), so they
were read from game data at EA 0x3F7xxx: garbage "programs", a new one per draw. Fixed for Metal (raw 1/0 ->
0xC0000000 + offset, raw 2 -> gcm_io_offset_to_ea) and for Metal textures raw 2 (previously a flat offset);
comment in rsx_commands.c corrected. The D3D12 FP resolver has the same bug — left as is (not testable here).
Also in E432: SIGBUS at 121 s, `_platform_memmove <- rsx_deswizzle_texels <- metal_bind_texture`. The guard
sizes the source as rsx_morton_padded_texels(w,h) * bpp in u32: for dimensions >= 32768 on both axes the
padded area is 2^32 and wraps to 0, so ppu_guest_range_committed(ea, 0) passes and the deswizzle reads up to
4 GB. RSX 2D textures are at most 4096 per side; guard fix (size in 64 bits + reject > 4096) goes in after E433
measures the location fix alone.

E433 visible (location fix e318af5f, same miss probe), interim at 60 s: FP EAs now in RSX local memory
(`ea=0xC03FFD80`, `0xC03FD080`, `0xC0373300`, ...), and the first 300 misses carry **13 distinct fp_hash**
(E432: 299) over 300 distinct ring slots — the same program in different slots now shares one key, with its
constants going per draw through fc_buf. vp_hash: 26 distinct (E432: 2 — the garbage FPs hid the real pairs).
`shader_cache evictions` lines: **0** at 60 s (E431: 16 by 91 s). Final (201 s, deadline, no crash): **0 eviction
lines in the whole run** (E430: 244 000, E431: 192 000 in 600 s), 0 `decompile_msl failed`, 29 349 flips, done 8
at 181 s (visible runs are slower), LDRSH 0. The shader-cache thrash is closed by the location fix.
Texture guard (sides > 4096 rejected, source size in 64 bits) committed separately; E433b (600 s visible) measures it.
E433b (603 s, deadline, no crash): done 14, 13 shaders, 77 bind_texture, 0 eviction lines, 31 072 flips — but the
guard **never fired** (0 `exceeds 4096`), so this run does not prove it fixes the E432 SIGBUS (seen once); it only
shows the guard is harmless. It stays as host-side validation against the RSX limit.

FIFO walker, retracted in the same round (said in chat and in commit e3ec483d): "CALL without a saved return
address + RETURN as `get += 4` is what sends the walker to io+0x1F800000". E421 carries that fix (cellGcmSys.c
recompiled into the lib and linked) and still logs the same 40 `UNCOMMITTED read32 0x67600000.. ra=
rsx_process_fifo+0x2C0`, plus 40 new `OOB access 0xEFE0....`. The CALL/RETURN change stays (it is what the
NV40 FIFO does) but it is not the cause; the jump that yields get = 0x1F800000 is still unidentified.

## E427b — a second movie starts during the level load and never ends

E427 headless log (measured): the movie FSM enters state 10 twice — `st620 1 -> 10` at line 33 120 (intro) and
`st620 0 -> 10` at line 107 430, during the level's last container (done #14 at 109 595). Just before the second:
`[cellVdec] QueryAttrEx ... codec=0 -> memSize=4MB`, `Open codec=0 cbOpd=0x00530178 cbArg=0x00000002 -> handle=0`,
`StartSeq(handle=0) -> async decode thread`; then `[MOVIEEOS] overlay done (st620=10 seqdone=0 force_ms=0) ->
arming EOS read-hook at 0x00869F1C ([obj+0x744])` and one `read-hook HIT [0x00869F1C]->1`. From then on the
FSM heartbeat stays `st620 10 -> 10 f744=0 ... eos_ea=0x00869F1C` to the end of the run (69 lines), with no
[movieio] open after done #14 (4 before, all intro) and no [cellVdec] line after 107 430. gow2-recomp
movie_eos_arm.c latches the PS3_MOVIE_DONE_MS interval once (`static ivl`), consistent with force_ms=0 on the
second movie. Working hypothesis for the LDRSH wall (not yet proven): the level opens with a cutscene whose
stream never reaches our decoder, so the game never leaves it. Control pending: whether [cellVdec] logs
DecodeAu/PICOUT at all during the intro (an absence needs a positive in the same regime).

Control result — the absence is **uninformative** (corrected in the same round): the intro's `[cellVdec] DecodeAu`
lines stop at `#10` (lines 33 127-33 137) and `GetPicture` at `#6` (33 140-33 218) although the intro decodes for
seconds, so both prints are capped and the caps were spent in the intro. "No [cellVdec] line after 107 430" says
nothing about whether the level movie's AUs reach the decoder. What stands: sessions Open/StartSeq with cbArg 0
(intro, Close at 33 448), 1 (Open 33 876, Close 33 902) and 2 (Open 107 393, no Close by the end of the run), and
the FSM parked at 10. Next: uncapped per-session counters in cellVdec (gated) to measure the level movie.

Measured with the existing uncapped counter instead (E431-movie run, headless, boot_gow2_e430, 300 s, env
PS3_VDEC_STATS=1 PS3_TRACE_REG=1 confirmed in [AB-ENV]): `[VDECSTATS]` prints every 60 DecodeAu and printed
**0 times in the whole run** — so fewer than 60 AUs reached cellVdecDecodeAu over intro + level movie (capped
DecodeAu print #1..#10, 1 GetPicture, SEQDONE 0). A real cutscene needs hundreds: the level movie's stream is
starved (bound measured; where the stream should come from is still open).
Driver after the 14th container: `[LDRSTATE]` alternates **17 <-> 0**. From the EBOOT jump table (base 0x4234C):
case 0 -> 0x42304 (loop head), 16 -> 0x43880, 17 -> 0x42BD0, 22 -> 0x428D8. Case 17 calls a virtual on
*(g+0x58) (vtable+0x44), then `bl 0x3F2B0` (loader tick): busy -> another virtual (vtable+0x40) and back to the
loop; 0 -> 0x43C40 (sets bit 0x4 / clears bit 0 of entry[*(r31+0x480)]+0x1C, calls 0x41014, 0x2B273C,
0x2AAAA0, 0x3E1FC(1, entry+0x14, *(r31+0x450), ...), then the vtable+0x44 call again). The loader tick answers
**0 (done)** at this plateau (last `[TICK] ret #1489 r3=0x0 state=0`, no change after), so case 17 is not waiting
on the loader; what sends the driver back to 17 is being located (writes to +0x550).

Driver structure (static, EBOOT): the loop head (0x422E4-0x4234C) loads the next state from a SCRIPT —
`idx = *(r31+0x40)`, `state = *(u16*)(r31 + 0x4C + idx*32)` (32-byte entries), stores it in +0x550 and
dispatches through the table if <= 0x1A. Case 0 (0x42304) is the function EPILOGUE, so `[LDRSTATE] 0` is the
probe seeing the return: "17 <-> 0" = case 17 runs once per call and returns, the state stays 17. The only
`li r0,17` store is at 0x439F0/0x439F4, the tail of case 16 (0x43880): BAD10 (open the container, lr 0x439AC),
a virtual call (vtable+0x40) on the object picked by the current record's u16 at +6, then state 17. So the
driver is parked because case 17's done path (loader tick 0 -> 0x43C40) does not advance the script index
(+0x40) on this entry — the condition is being read.
Case 17's done path writes `state = r27` at 0x43D78 (then `b 0x42304`); r27 is the loader tick's r3 (`or r27,r3,r3`
at 0x42C04), i.e. 0 on this branch — the "0" in [LDRSTATE] is also that write. The next call reloads
script[idx] = 17 at the loop head, so the cycle is: case 17 -> loader done -> state 0 -> reload 17. The script
index (+0x40) is not advanced on this path; who advances it is being located.
Static read, closed: the script index is a 32-entry ring (`idx+1; if >31 -> 0`) advanced at 0x42474, 0x425A4,
0x42610, 0x42824 (case 23 area) and 0x429A4 (case 21 area); case 17's done path 0x43C40 -> 0x43D78 is
straight-line (finalize the container: flags, 0x41014, 0x2B273C, 0x2AAAA0, 0x3E1FC(1, entry+0x14, *(r31+0x450),
...), vtable+0x44 / +0x40 calls) and ends `stw r27(=0), 0x550(r31); b 0x42304` with no branch; 0x43D80 is a
separate entry reached from other cases. So the driver is not stuck inside case 17: it finishes and idles because
no new command is queued after this one. Inference (not proven): the producer is the level flow, which is parked
in the movie FSM (st620 = 10) on a cutscene whose stream is starved (< 60 AUs in the whole run).

Where the level movie's data comes from (E431-movie log): the only movie members opened through FIOS in the
whole run are the intro's (`[FIOSOP] ... path='/_movies/smlogo_v2.m2v'` 20 lines, `.wav` 38 lines, from line
23 900); movie_cache holds only SmLogo_v2.m2v/.wav (plus WAD caches). The third decoder session (cbArg 2, line
110 661) is preceded only by a loader container open (`[BAD78] after-open #12`), so it is not a /_movies/ file.
Lead (not yet tested): the movie player feeds an SPU decoder task through LFQueue 0x47CA9B80 (millions of pushes),
and the MPEG decoder task-lib is spu1, which is still opt-in (PS3_SPU1) because a failing job took the process
down — but spu1..3 carried the same `il` double sign-extension fixed in E427 (39/54/43 immediates). Next: headless
with PS3_SPU1=1 (then PS3_SPU_ALL=1) and PS3_VDEC_STATS=1.
Corrected in the same round: gow2_spu_register.c:42-53 documents spu1 as the **dearch / EDGE-zlib** job, not the
MPEG decoder — "the MPEG decoder task-lib is spu1" falls. What stands: spu1..6 are opt-in (PS3_SPU1..6 /
PS3_SPU_ALL), and spu1..3 had the E427 `il` bug. Which SPU workload the level movie needs is read from the
`[spu_workload] dispatch MISS fp=` lines instead.
Measured: E427 and E431-movie both log **41 `dispatch HIT` and 0 `dispatch MISS`** (2 `registered` lines); the
E418e-era run tick2_r1 had 19 959 MISS + 40 HIT. So no SPU workload is missing at dispatch now, and the lead
"the level movie needs an opt-in / unlifted SPU image" is not supported. The starvation of the level movie's
decoder (< 60 AUs) has to be traced inside the game's movie player (it feeds LFQueue 0x47CA9B80, millions of
pushes per run) — open.
Corrected in the same round (also said in chat): LFQueue 0x47CA9B80 is NOT the movie player's. `[LFQ] init` lines
(E431-movie log 92-163): q 0x47CA9B80 and q 0x47D59A80 are twins — size 0xE0, depth 2048, dir 2 (PPU->SPU),
each with its own SPURS task (contexts 0x47C37200 / 0x47CE7100) running the SAME SPU image (lsPat 0x4BA690, the
spu0 renderer task); a third queue q 0x43008180 (size 0x20, depth 12, dir 3 ANY2ANY, taskset 0x47C04081) has a
task with lsPat 0x4BF5A8. Next for the level movie: a gated per-session DecodeAu counter in cellVdec (VdecSlot
carries cbArg: 0/1 intro, 2 level), so the level session's AUs are counted directly instead of bounded.
E434 (headless, PS3_TRACE_VDEC_SESSION, interim at 152 s): the level session DOES receive AUs —
`[VDECSESS] cbArg=2 DecodeAu #1..#5` with real sizes (28 880, 22 060, 93 681 B) after the third Open (line
104 834); no `#100` line yet, so < 100 so far (consistent with the < 60-per-run VDECSTATS bound). Refuted: "the
game never feeds the level session". Next question: does our decoder produce pictures for it — cellVdec.c:974
says the pixels come from a host decoder, not the guest AU.
Answered by the VideoToolbox path's own log (E434): `[cellVdec-vt] sessao MPEG-2 640x448 (I420) aberta` (line
105 054, the level session) and later `[cellVdec-vt] fechado: in=15 out=15 err=0 drop=0` — the guest's AUs are
decoded (M9 path: cellVdecDecodeAu copies the AU bytes to the VT queue when vdec_vt_enabled(); the ffmpeg-cache
path is only the fallback), 15 frames out, no error, and the game closes the session. Intro sessions: in=38/out=38
and in=8/out=8. Retracted in the same round (said in chat): "the level cutscene is starved / never decoded". The
count stands (< 100 AUs for cbArg 2), the reading falls — it is a short 640x448 clip that plays and closes. The
persistent `[MOVIEFSM] st620 10` heartbeat is likely our sampler reading a stale player object. The LDRSH wall
is back to what E431 measured directly: the driver finishes case 17 and its command script gets no new entry.
E434 final (301 s, deadline): VT sessions in=38/out=38, in=8/out=8 and in=15/out=15 (level, 640x448), all err=0;
[VDECSESS] cbArg 0/1/2 each shows #1..#5 and no #100; done 14, 13 shaders, LDRSH 0.
Driver loop head (func_00042294, EBOOT 0x422A0-0x422E4): r31 = *(r2-0x7558) is the driver object;
`if (*(r31+0x54C)) return;` (pause/busy flag); `if (*(r31+0x550)) dispatch(state);` else
`if (*(r31+0x44) == 0) return;` (+0x44 = pending command count) else `state = script[*(r31+0x40)].op` (u16 at
r31+0x4C+idx*32) and dispatch. Whether the plateau is "count 0, idle" or "a head entry nobody pops" is measured
next (E435: gated dump of +0x40/+0x44/+0x54C/+0x550 and the 32 entry opcodes at the loop head).

E435 (`patch_e435_driver_queue_probe.py`, PS3_TRACE_DRVQ, headless 300 s, 31 lines): driver object
o = 0x0057CF78. The queue is **empty at the plateau** — last change (line 115 126, after done #14 at 107 094)
`idx=12 count=0 pause=0 state=0`, nothing after. Command history (ops by slot): 0D 19 15 0A 0A 12 10 10 10 10 0F 10
= 13, 25, 21, 10, 10, 18, 16 x4, 15, 16; the last pair (15, 16) was queued together at line 106 276 (count 2) and
consumed (idx 10 -> 11 -> 12, case 16 -> state 17 -> loader done -> 0). So the driver is idle, not stuck: the
producer (level/game flow) queues nothing after that pair. Next: catch the producer with a store watch on the count
(o+0x44 = EA 0x0057CFBC) and see why it does not queue the next command.

E436 (PS3_WATCH_STORE=0x0057CFBC, cap 100000, headless 300 s, E435 binary; 36 writes): the count is raised by
**func_0003EEF8+0x330** (the enqueue, 12 writes) and also written by func_0003DFE4+0xDE0 (5, via func_0003F5AC+0x5C)
and func_0003EFF4 (3); it is lowered by the driver cases (func_00043880+0x410 = case 16, func_000431EC+0x7F4,
func_00043608, func_00043684, func_00042DFC). The last producer activity is the (15, 16) pair: lines 105 053 /
105 054, count 0 -> 1 -> 2, from func_0003EEF8 called by **func_00040A34+0x44** and **func_000409E0+0xC4**; then
func_0003DFE4 (via func_0003F5AC) writes 2, and the driver pops to 0 at line 105 071. No write after that for the
rest of the run. So the level flow that calls 0x409E0 / 0x40A34 enqueues nothing after this pair — the next
question is what those callers wait for before queuing the next command.
Static read: 0x409E0 / 0x40A34 are tails of **func_0004097C** (0x4097C-0x40A34) = "request slot r4": entry =
*(o + 0x460 + r4*4); if entry+0x1C bit 0x1 (requested) is set it returns; otherwise it enqueues (15, 16) — or only
16 — via func_0003EEF8, sets bit 0x1 and calls 0x3731A0(entry, r3). The driver's case 17 done path sets bit 0x4
(loaded) and clears 0x1 (0x43C5C / 0x43C98). func_0004097C has 9 call sites, all in one region (0x8D79C, 0x8D7B4,
0x8D7CC, 0x8D7E4, 0x8D82C, 0x8D868, 0x8DA48, 0x8DAC0, 0x8DAE8): the level flow requesting slots one by one, which
in our run stops after the (15, 16) pair.
The call region (EBOOT 0x8D5xx-0x8D870; functions.json splits it into small fragments, the enclosing function starts
earlier) is a section-streaming manager: branches request slot 5 (name ptr at -0x68D8(r2), again -0x68E0(r2)),
2 (-0x68C8), 4 (-0x68EC), 3 (-0x68D0) and 1 (-0x68B0) through func_0004097C, each behind a condition read through
a pointer global — `lwz r9, -0x68A0(r2); lwz r0, 0(r9)` (also -0x68E4, -0x68BC) — plus a compare
`*(r9+0x18) == r3` at 0x8D760. With the TOC r2 = 0x541178 those pointer globals sit at EA 0x53A8D8, 0x53A894 and
0x53A8BC. So further slot requests depend on flags that game state (likely player zone/position) sets; in our run
none of them triggers another request after (15, 16). Next: read the whole manager and measure those flags at the
plateau (and, if possible, the console's values through the RPCS3 oracle once it can be driven into the level).
Corrected in the same round (said in chat): the TOC entries resolve (EBOOT data, r2 = 0x541178) to the slot names
**R_M_Lghtn / R_M_Elctrc (slot 5), R_M_Wind (2), R_M_Earth (4), R_M_Medusa (3), R_S_Hammer (1)** and to flag
globals at 0x5944BC, 0x59489C, 0x59478C (BSS, initially 0). So func_0004097C and this manager stream the magic /
sub-weapon asset packs, gated by per-magic flags — not level sections. Retracted: "the level flow stops enqueuing
the next command". What stands: the driver queue is empty at the plateau (E435) and the (15, 16) pair came from
this equipment manager (E436). An idle driver is consistent with the level being fully loaded; with 8079 flips
after done #14 and the main thread in game code ending at func_002F0DD0 (E427), the open question becomes whether
the game is already running the level — to be checked by looking at a rendered frame.

Frame capture (visible, boot_gow2_e435, PS3_FRAME_DUMP=1 PS3_FRAME_DUMP_STRIDE=2000, 604 s, deadline; done 14, 13
shaders, 77 bind_texture, 7697 PSO lines): the frame dumped after the level load (frame_32000.bmp, the drawable) shows
the **Bluepoint Games logo** — the intro movie (SmLogo_v2), left side clipped — not a game frame. So what Metal
presents at the plateau is the intro-movie overlay / content hold, not the game's colour render target; the game's
own draws are not what reaches the window (CLAUDE.md: overlay pixels do not count as game pixels). The frame cannot
answer "is the game in the level"; it does expose a present-path problem for the M2/M3 goal.
Read (present path, rsx_metal_backend.m:2146-2230): the drawable samples the movie overlay when s_movie.active,
else the content hold when s_content.hold, else the game's colour RT. s_movie.active is set only by
rsx_metal_movie_playback_begin(), whose only caller is the VideoToolbox overlay thread (movie_vt_metal.m:199), and
that thread always calls rsx_metal_movie_stop() when it ends (movie_vt_metal.m:306-310) — so the hypothesis "a later
HLE movie re-activates the overlay" is refuted by reading. Three candidates remain for the logo at the plateau:
(a) the VT overlay replays the only cached movie (SmLogo_v2; the Play#2 / replay path), (b) the content hold
(s_content.hold re-set at line 3422) shows a held host image, (c) the game itself draws the logo with its own
shaders/textures. A visible run with PS3_TRACE_TIMELINE=1 (present_mode movie/content/hold/active every 50 frames)
decides between them.
(a) ruled out: the run env has no PS3_MOVIE_VT_REPLAY ([VIS-ENV]: MOVIE_CACHE, MOVIE_DONE_MS=3000, MOVIE_EOS=1,
MOVIE_HLE=1, MOVIE_IO=1, PAD_AUTOSTART, VDEC_ASYNC), movie_vt_clear_overlay_done() has no caller, and the only
autostart (movie_hle.c:340) is blocked by the sticky overlay-done flag after the intro. (b) has a concrete
mechanism: rsx_host_content.c keeps a host "boot logo queue" (HOSTRES images); pre-movie logos are held at once
(line 137), post-movie ones are shown by rsx_host_boot_logo_on_movie_end() (line 159), which always restarts from
index 0, and advanced by the tick until "boot logo queue DONE — hold cleared". If that is called again when a
later movie ends (e.g. the level's 15-frame clip), the queue restarts and puts the logo back on screen.
Measured in the run that captured the logo (vis_e435_dump.log): the HOSTRES queue holds legalscreen720.ctxr (pre-movie,
held at line 235), scep_e.ctxr and bluepoint.ctxr (post-movie); it is armed ONCE, when the intro ends (line 34 023
`[CONTENT] movie end — boot logo queue armed`), goes NEXT bluepoint at 35 705 and ends `boot logo queue DONE — hold
cleared` at 38 736; no [CONTENT] event after that up to the end (127 474 lines; done #14 at 77 618). The tick runs in
every present (rsx_metal_backend.m:2122). So (b) "the queue re-arms" is refuted for this run too: at frame 32000
neither the VT overlay nor the host hold is active. By elimination the logo on the drawable is drawn by the game
itself through Metal (its own shaders + its bluepoint texture) — inference, to be confirmed by the timeline run's
present_mode (movie=0 content=0) at the end.
Confirmed by the timeline run (visible, PS3_TRACE_TIMELINE=1, 429 present_mode lines): the late frames
`[TL] f=30900..31000 present_mode movie=0 content=0 hold=0 paused=0 active=0 use_cv=0 draws=68` — no overlay, no host
hold; the drawable is the game's own colour RT with 68 draws per frame. So the Bluepoint logo on screen is **rendered
by the game through our Metal path (its shaders, its texture): real game pixels** (earlier phases: movie=1 hold=1
during the intro, content=1 during the host logo queue).
Retracted in the same round (said in chat since E420): "the game leaves the menu and loads the first level". What
was measured is the container loads (R_Colsus00, R_Hero, ...); what the screen shows at ~290 s of a visible run is
the game's own Bluepoint logo, i.e. it is still in its boot/logo sequence while preloading. The load counts stand;
the "left the menu" reading falls. Candidate for the logo screen not advancing: the known wall [G] (CLAUDE.md) — the
app state transitions are paced by the SCREAM audio clock, which stays 0 while the mixer PM (spu6, PS3_SPU6) is
opt-in.
Corrected in the same round (said in chat): that candidate had already been refuted as the SOLE gate in
2026-09-02-E147-E162 §4cl (E332): without spu6 the game passes ReadyCountStore(wid=0) via the "no image -> clear"
path (2/2) and still does not reach gameplay; §4cl concludes the front-end -> gameplay transition is NOT only the
audio clock and that the front-end state machine deciding menu vs level is uncharacterised. E437 (headless,
PS3_SPU6=1, this session) reproduces §4cj/E330: `registered 'gow2_spu6' fp=0xCEDB9A67A0C3A305` with **0 dispatch
HIT** (spu0 35 HIT, spu1 5 HIT at 110 s), plateau unchanged (rem 0x2620, ~6368 shells). Next: the guest app-state
machine itself (PS3_TRACE_STATE, CLAUDE.md step 0b), to compare with the console.

## E438 — the main-loop state handler: measured, and not a divergence

E437 (PS3_SPU6=1) was stopped early by PID once it had reproduced E330 (its interim numbers above stand).
E438 (headless, PS3_TRACE_STATE=all; probe `patch_242c94_state_dispatch_probe.py` on the per-frame handler OPD at
*(app+0x460C), app = 0x00730DF8): `[STATE] frame=1 0xFFFFFFFF -> 0x0052F3F0 (code 0x002B2DD0)`, then only
heartbeats `preso em 0x0052F3F0` at frames 600 / 1200 / 1800 / 2400 (done 14 reached meanwhile). The only lifted
writer of *(app+0x460C) is func_0024A7CC (recomp_macos_e419/ppu_recomp_000.cpp:539027).
E438 final (300 s, deadline): still `preso em 0x0052F3F0` at frame 6000; done 14, LDRSH 0.
Reading it: memory "parede-g" says 0x0052F3F0 (func_002B2DD0) is the main loop's eternal tick on the console too
(progress inside the tick, OPD never changes), citing "E110 §4". **Unverified**: the E110 ledger
(2026-09-02-E110-a-final-da-intro-console-vs-nos-e-o-filho-classe15.md) contains no mention of tick / F3F0 / 2DD0
at all, so the citation does not check out; the source is being looked for elsewhere. Until it is found or the
console is measured (oracle), "E438's constant OPD is normal" is an unconfirmed claim, and "the app never leaves its
first state" is equally unconfirmed as a divergence. The E438 measurement itself stands.
Resolved in the same round: the source is 2026-09-02-E93-a-parede-G-e-uma-cadeia-de-paliativos-nossos.md line 9 —
"o console fica em `0x0052F3F0` para sempre (amostrado até 150 s de guest)" (measured on the RPCS3 oracle; the memory
cited E110 by mistake). So E438's constant OPD matches the console over the front-end window; that oracle run had no
pad input either, so it covers the front-end only. E438 shows no divergence at the state-pointer level. Where the transition to gameplay fails stays as §4cl (E332) left it:
the front-end sub-state machine inside the tick (menu vs level) is uncharacterised. Measured and standing this
session: the game renders its own Bluepoint logo late in the run (E435), the loader's command queue is empty
(E435/E436), spu6 is not dispatched (E437), the level cutscene decodes (E434).

## E439 — container order matches the console; the level clip is not the gate; the start screens

Container order, ours (E438 log, `[WADLD-T1] name='WAD_*'`, first line of each): R_LglScA 49 984, R_Perm 50 402,
R_Hero 63 683, R_ShellA 64 469, R_SubWeapon 72 439, R_SavePt 72 976, rhod10 74 081, R_Rhsold00 87 651, R_Colsus00
88 611, R_M_Earth 92 054, R_M_Medusa 92 903, R_M_Wind 93 706, R_M_Lghtn 95 026, R_Weapon 96 067. Console (E93 §0 /
E94, file opens): r_hero01, r_shella, r_s_olympus0, r_savept, r_mcicon, rhod10, r_rhsold00, r_colsus00, r_m_earth0,
r_m_medusa0, r_m_wind0, r_m_lghtn2. The file names differ from the internal WAD names: the cached
`r_s_olympus0.wad_ps3` carries `WAD_R_SubWeapon` (strings of movie_cache/), so "we never load R_S_Olympus0" falls —
it is loaded, in the console's position. `r_mcicon.wad_ps3` (77 KB) has no printable names (compressed), so whether
we open it is unmeasured; no `.wad_ps3` path is printed in our log at all (the log shows internal names only).
Up to R_M_Lghtn the sequence matches the console.

Level clip, two runs compared: E434 (cbArg 2) — DecodeAu fed, `[cellVdec-vt] sessao MPEG-2 640x448` opened, Close,
`fechado: in=15 out=15`, then `[MOVIEFSM] st620 10 -> 0` and `clear hook` (106 973-107 005); LDRSH 0 anyway.
E438 (cbArg 3) — Open + StartSeq at 95 011, **no VT session opened and no Close** to the end; st620 stays 10 for the
rest of the run. So the clip is fed in one run and not in the other (flaky, unexplained), and in the run where it
completes the game still does not compile level shaders: the clip is not the LDRSH gate.
Retracted (E434's "the persistent st620 10 heartbeat is likely our sampler reading a stale player object"): the
sampled object is the live player — the level session's `QueryAttrEx type@0x00869F20` is obj+0x748 of the same
object whose +0x744 is the EOS hook (0x00869F1C), and in E434 st620 leaves 10 exactly at the Close.

Console reference restated (E417 oracle, line ~159 above): with no pad input the console runs 13 BAD10 and **LDRSH 0
over 312.9 s** too. So the autonomous front-end is not expected to reach level shaders; LDRSH>0 needs the game to
leave the start screens / menu into a level, i.e. input the game accepts.

Start screens (static, EBOOT): the paths `$/CP2/Legal_Screen/****/legalscreen720.ctxr`, `$/CP2/Start_Screens/****/
scep_e.ctxr`, `.../bluepoint.ctxr` (EA 0x4C55F8 / 0x4C5628 / 0x4C5650) are loaded through TOC slots -0x3F14 / -0x3F00
/ -0x3EF4 only by **func_00149650** (0x1496BC-0x149784: func_001E121C path + func_001EBC78 load, handles stored at
obj+0xC / +0x4 / +0x8, obj+0 = 3), called once from func_0014B1F0+0x1E0 (front-end system init; obj = r18). The
sampled plateau path (E427: ... func_001658A0 > func_00159EE4 > func_0014E4E0 > func_002F0DD0) is render code, not the
sequencer. Next (measurement, not static): a visible run with frame dumps every 3000 frames + PS3_TRACE_PAD, to get
the screen timeline and whether the START/CROSS taps keep reaching the game at the logo plateau.

E439 visible run (boot_gow2_e435, run_visible2.sh, 604 s deadline, env + PS3_TRACE_PAD PS3_FRAME_DUMP
PS3_FRAME_DUMP_STRIDE=3000 PS3_TRACE_TIMELINE PS3_TRACE_DRVQ; log vis_e439_pad.log, 135 249 lines): done 14, 13
shaders, LDRSH 0, driver queue empty (`idx=12 count=0`). Timeline: host VT overlay `movie=1 paused=1` from f=348 to
f=28 412 (the guest's intro ends at line 9 675, f~6 425 — so for ~22 000 frames our paused overlay hid the game), host
logo queue f=28 450-28 900, then the game's own RT: frame_30000.bmp = the Bluepoint logo (68 draws, as in E435).
Then **at f=30 417 the game compiles ShadowVolume.ps3fx and ApplyShadow.ps3fx** (log lines 89 819/89 821, the 12th and
13th shaders) and draws jump to 256/142/137/113 per frame from f=30 450 — a different, 3D scene — until the deadline
kill at f~31 150. The console's TTY.log (~/Library/Caches/rpcs3, last headless oracle run, 35 lines) ends at
Particle.ps3fx: our run is past that capture, and its game messages otherwise match ours line for line (only
Default CONSTCOLOR=1 vs 0, known).
Retracted (E435/E438 reading, also in memory "parede-d"): "the game stays in its logo sequence". The screen was
measured at ~290-600 s with the Bluepoint logo, but the window was too short: in E439 the game leaves it at ~590 s
and starts a scene with shadow shaders. The counts stand. Minimum window for the LDRSH question: well over 600 s
visible (the transition came at f~30 400, ~590 s); next run 1500 s with dumps every 1000 frames.

E439b long run (vis_e439b_long.log, 1503 s deadline, boot_gow2_e435): transition again (ShadowVolume/ApplyShadow at
f=29 688, ~240 s this time -- timing varies), then **LDRSH 0 for the whole 25 min**, no crash, driver queue empty. The
run only reached f=32 600: frame_31000 at 20:01:35 and frame_32000 at 20:09:15 = 1000 frames in 7 m 40 s (~2 fps)
after the transition. Log: 4 266 `[RSX metal] draw path PSO ready` after it. Cause (read): draw_ensure_pso rebuilt
the fixed pipeline from MSL source on every blend-key change, and metal_invalidate_blend_psos (on any blend or colour
mask change) released the WHOLE decompiled shader cache, so every blend toggle recompiled every game PSO from source
at its next set_shader. Also: an entry whose VS fell back to passthrough was stored with vp_hash 0 and could never be
hit again (a recompile per set_shader for those shaders). With the game at ~2 fps its logic cannot get far.

## E440 — the RSX surface/viewport method map was shuffled; per-draw render targets

Measured in E439 (vis_e439_pad, capped `set_render_target` log): `zeta_off=0x00000001` / `0x00000013` = exactly
CELL_GCM_SURFACE_TARGET_0 / TARGET_MRT1, and every surface 1280x720. libs/video/rsx_commands.h had the old shuffled
table (CLIP_H 0x204, CLIP_V 0x208, ZETA 0x220 = really COLOR_TARGET, PITCH_A 0x22C, VIEWPORT 0x300/0x304 = the
dither/alpha-test block); upstream sp00nznet fixed it in 247251b0 (2026-07-10, not in our merge). Ported the numbering
only (surface block + viewport). Also read: the Metal present replayed ALL of a frame's draws into one target with
the last viewport/scissor/textures -- every offscreen pass (shadow, downsample) landed on the drawable, which is the
dark quarter over the Bluepoint logo in frame_29000/30000.
New, gated PS3_METAL_PER_DRAW_RT=1 (OFF): each draw record keeps its colour/depth surface, viewport, scissor,
textures (retained) and depth-stencil state; clears are ordered records; one render pass per run of draws on the same
target; the flip's buffer (cellGcmGetDisplayInfo(buffer_id)->offset) is what is shown; surfaces only grow.
E440 run (boot_gow2_e440, 703 s visible, PS3_METAL_PER_DRAW_RT=1 PS3_TRACE_METAL_PERDRAW=1): no crash. With the
method fix the surfaces are real: three 1280x720 display buffers (0x400000/0x7C0000/0xB80000, show follows the flip
0/1/2), the scene 1216x684 with its depth, a downsample chain 608x342/304x171/152x85/76x42, plus a few suspicious ones
(off 0x3F800000 = 1.0f, 3x16, off 0) still unexplained. f=29000: 72 records, 6 passes, 67 draws, 5 clears. But the
dumped frame is **all black**. Hypothesis (to measure in E441): render-to-texture -- the final composite samples an
offscreen surface as a texture, and bind_texture read guest memory the GPU never wrote. E441 adds RTT (a local
texture at a colour surface's offset binds the surface), a per-pass trace, and the PSO cache fix (key = fp, original
vp, blend/mask state; compiled functions cached; the cache is no longer flushed; the fixed pipeline keeps variants).

## E441 — PSO cache fixed, RTT fires, the screen is the display buffer's clear colour; the FIFO drops subchannels

E441 (boot_gow2_e441 = E440 + RTT + PSO cache fix + per-pass trace; 902 s visible, per-draw ON): no crash, LDRSH 0,
transition at shader #12/#13 as before. PSO: `draw path PSO ready` 51 lines in the whole run (E439b: 4 266 after the
transition); `shader stats` fn_compiles=350 pso_builds=846 fns=350 -- every MSL compile is a distinct (fp, vp) pair,
variants come from cached functions. Frame rate after the transition is still ~3 fps (frame_29000 20:33:56,
frame_30000 20:39:23 = 1000 frames in 327 s), so the flushed cache was not the only cost (a host `sample` during the
3D scene is next). RTT binds fire (16 logged): the bloom chain (304x171, 76x42), surf[5] 1216x684, and surf[3]
(0x010A0000, the 1216x684 scene) read as a **2432x684** texture -- twice the surface width, consistent with a 64-bpp
(FP16) surface being sampled as 32-bit texels; our surfaces are all BGRA8 (open).
frame_29000 is a uniform (8, 24, 28): per-pass trace `S2:0 S2:0 S3:79 S3:0 S13:0 S5:0` -- all 79 draws go to surf[3],
the display buffers only get clears, and the shown buffer holds its clear colour. So the scene reaches the screen by a
non-draw path. Read: the FIFO walker decodes `method = cmd & 0x3FFC` and never looks at the subchannel
((cmd >> 13) & 7): the 2D objects' methods (NV3062 on 2/3, NV3089 on 6/7 per upstream) went to rsx_process_method as
3D methods at the same offsets -- NV3089 out_pt/out_sz/ds_dx/dt_dy (0x310-0x31C) into BLEND_ENABLE/SFACTOR/DFACTOR/
COLOR -- and no 2D blit (the scene -> display-buffer copy) ever ran. That blend churn is also what fed the PSO-cache
flushing. E442: the walker routes subchannel 0 to the 3D processor and 1-7 to a port of upstream's gcm_2d_method /
nv3089_blit (CPU copy in guest memory); nv3089_blit first offers the blit to the backend (new optional
rsx_backend.blit_scaled), and Metal (per-draw mode) records it in order as a scaled GPU copy between surfaces,
creating the destination surface so a later texture read finds it. PS3_TRACE_GCM2D prints first (subch, method) pairs
and the blits.

## E442 — subchannel routing on; the walker reads 3D payloads as headers; host profile of the 3D scene

E442 (boot_gow2_e442 = E441 + subchannel routing + 2D port + Metal blit hook; visible 900 s, PS3_TRACE_GCM2D=1):
`[GCM2D] first` sightings -- subch 6: 1 888 distinct methods, 7: 46, 1: 69, 4: 26, 2: 4. NV3089 has ~20 registers and
none of them (0x184/0x198/0x300-0x31C/0x400-0x40C) appears on 6/7; the "data" words are VP microcode (0x401F9C6C,
0x8086C0C3 ...), float constants (0x3F800000 ...) and FIFO headers (0x00041714 = count 1, method 0x1714). So the
walker is decoding 3D PAYLOAD as headers: it loses alignment. Before E442 this garbage (subch 6 has bit 13 = 0 under
the old 0x3FFC mask) went to the 3D processor as state. One garbage "blit" was logged: `65409x24737 fmt=0xC3
src=0x00088529 -> dst=0x00000000 pitch 0` (unsupported format and pitch 0, nothing written). Hypothesis (to measure
in E443, probe PS3_TRACE_FIFO_SYNC): GoW2 builds command lists on the SPU (TTY `Renderer_SPU: Startup`) and the RSX
waits for them with NV406E SEMAPHORE_ACQUIRE (0x68); our walker handles only 0x0010/0x0014 and 0x1D6C-0x1D74, not
0x60/0x64/0x68/0x6C, so it can read an SPU-built list before it is written (stale contents = last frame's payload).
Safety: CPU-side 2D writes into guest memory (NV308A inline, NV3089 copy) are gated OFF (PS3_GCM2D_CPU=1) from E443,
since a desynced stream feeds them arbitrary addresses.
Host profile, E442 during the 3D scene (sample_after_shadow.sh, two 5 s samples): mostly waits (cvwait 26-29k,
semwait 20k, mutexwait 13-16k), the lifted SPU renderer (spu0_spu_func_0000EAF8/E490/E5B8/... ~5k, about one thread
busy), and **rsx_deswizzle_texels 1 536 / 1 761** (~30-35 % of a thread): metal_bind_texture deswizzles and uploads on
every bind, with no cache. Read in the same code: metal_tex_upload_slot does replaceRegion on the unit's ONE texture
object, so with draws replayed at present every draw of a frame samples the LAST texture uploaded to its unit (the
per-draw retain points at that same object). Next: a texture cache (one MTLTexture per distinct guest texture, keyed
by ea/format/size/swizzle + a sampled content fingerprint; new object when the content changes).

## E443 — where the FIFO desyncs: the buffer wrap writes no JUMP

E442 final (903 s): LDRSH 0, no crash, 0 GPU blits, 1 garbage `[GCM2D] blit` (cpu path, nothing written: format 0xC3,
pitch 0), last frame f=21 350 (E441: 31 150; unexplained, to compare with E444).
E443 (boot_gow2_e443 = E442 + CPU 2D writes gated OFF + probe PS3_TRACE_FIFO_SYNC; 900 s visible). Semaphores: the
title DOES use NV406E -- `0x0064 SEMAPHORE_OFFSET = 0x10` then `0x0068 SEMAPHORE_ACQUIRE = 0` every few hundred bytes
(get=0x50C/0x514, 0x91C/0x924, 0xD2C/0xD34 ...); the walker ignores 0x60-0x6C (the probe's label value was read at the
NV4097 offset 0x410, not 0x10 -- uninformative). Desync: every `[FIFOSYNC] subch=...` line sits at the END of the
2 MB ring -- get=0x1FFDC4, 0x1FFE24, 0x1FFE34, 0x1FFF4C -- right after ordinary headers (1D74, 1D6C, 1808, 1824,
181C, 1714), sometimes after a run of zero words. Read: ps3_gcm_ctx_overflow (our CellGcmContextData callback,
PS3_GCM_CB) publishes put = current, drains, and sets current = begin, but writes NO jump at current. On the console
the wrap is a JUMP to begin at current (RPCS3 writes an old-style jump there); without it the next drain resumed at
the old get and read the rest of the ring -- the previous lap's words -- and past its end, until the walker's
200 000-word guard, then forced get = put. Every wrap therefore fed up to ~800 KB of stale words to the 3D processor
and the 2D handler: the garbage subchannels of E442, blend/state churn, and CPU time. The SPU-list/ACQUIRE hypothesis
of E442 is not what the desyncs show (they are all at the wrap); ACQUIRE is still unhonoured (separate item).
E444: the overflow callback writes `0x20000000 | (begin - io)` at current before publishing put (PS3_GCM_CB_NOJUMP=1
restores the old wrap), plus the texture cache (one MTLTexture per distinct guest texture, per-draw mode).

## E444 — the wrap fixed: zero desyncs, ~5x faster 3D scene; the 256-record cap cut the composite

E443 final (903 s): at least 512 wraps (`[GCM-CB] #512` printed; the print fires on the first 16 and powers of 2),
12 399 distinct garbage (subch, method) pairs, 20 desync lines (probe cap), LDRSH 0, no crash, f=30 550.
E444 (boot_gow2_e444 = E443 + wrap JUMP written at current + texture cache; visible, per-draw ON, FIFO probe ON),
interim at t~600 s: **`[FIFOSYNC] subch=` 0 and `[GCM2D] first` 0** -- the whole garbage stream is gone, and with it
any 2D method: GoW2 uses no NV3089 at all (0 blits), its composite is a draw. Frame rate after the transition:
frame_24000 21:23:38 -> frame_28000 21:27:57 = 4 000 frames in 259 s, **~15 fps** (E441: ~3 fps). `tex cache:` printed
0 times (fewer than 200 misses in the run: nearly every bind is a hit). LDRSH 0 at t=601 s.
Caveat in E444's fix: wrap #1 was `current=0x48000000` = begin + 2 MB exactly -- our cellGcmSetupContext sets
end = cmdbuf + cmdSize (cellGcmSys.c:354) with no slot reserved for the jump, so writing it at current touched the
word after the buffer (IO offset 0x200000). E445 replaces the memory write with a walker-side wrap
(s_fifo_wrap_at/_to, taken when get reaches it) -- same effect, no guest write.
Per-pass trace: f=24 000 `recs=256` -- exactly METAL_MAX_DRAWS; draw_arrays/draw_indexed returned silently past it, so
a frame's last passes (its composite) were dropped. f=29 500 (23 records) shows the whole tail: `S5:21 S5:0 S2:1`, the
last pass = 1 draw into display buffer 2 sampling S5 and S3 (triple buffering: drawn into the next buffer, shown at a
later flip). frame_25000 / frame_28000 dumps are still black. E445: METAL_MAX_DRAWS 4096, VERTS 262 144, INDICES
1 048 576, a per-frame `dropped=` count in [PERDRAW], and PS3_METAL_SURF_DUMP=<frame> (every BGRA8 colour surface
written as surf_f<frame>_s<idx>_<offset>.bmp at that frame and the next) to see where the pixels stop.

## E445 — every surface is black; draws dropped by the full vertex buffer; the VP constants were never uploaded

E444 final (902 s): desync 0, wraps >= 4 096 (`[GCM-CB] #4096`), LDRSH 0, no crash, f=35 350 (E443: 30 550).
E445 (boot_gow2_e445 = E444 with the walker-side wrap + caps 4096/262 144/1 048 576 + dropped counter + surface dump;
visible, per-draw ON, PS3_METAL_SURF_DUMP=30500): desync 0 again (walker-side wrap works, no guest write), transition
at #12/#13 as always. `dropped=` is large and NOT the record cap: f=30 000 recs=96 dropped=275, f=30 500 recs=106
dropped=3 089, f=31 000 recs=506 dropped=2 177. The vertex-layout "unsupported attrib0" print never appears; the
`[UNSUPPORTED]` attrib lines are disabled slots (x0 stride 0). What is left is upload_vertices_from_rsx returning 0
when the vertex buffer is full: indexed draws upload vertices 0..max_index for EVERY draw, so a mesh split into many
draws re-uploads its whole range and fills the 262 144-vertex buffer early in the frame.
Surface dumps at f=30 500 (all 12 BGRA8 colour surfaces): scene s3 (0x010A0000, 1216x684), post s5, display s0/s1/s2
-- **all uniformly black**, so even the draws that were recorded produce no pixels. Read: the decompiled VPs read
`constant float4* vp_c [[buffer(1)]]`, and s_draw.vp_const_buf is created once as 1024 zero float4 and never written;
rsx_state.vertex_constants (NV4097_SET_TRANSFORM_CONSTANT, captured by rsx_commands.c) never reached Metal. With every
transform matrix zero, all game geometry collapses. Also: MetalBasicVertex carries only position + colour (28 bytes),
so normals/tangents/extra UVs never reach a VP either (plan M6, separate).
E446: per-draw VP constant snapshots (512 float4 = 8 KB, a new one only when the constant file changed since the
last, memcmp; 32 MB ring per frame) bound at buffer(1) per record in both present paths.

## E446 — all draws reach Metal; still black; the clear hit the previously drawn buffer

E445 final (902 s): LDRSH 0, no crash, desync 0, f=41 400 (E444: 35 350).
E446 (boot_gow2_e446 = E445 + per-draw VP constants + indexed draws upload only [min_index, max_index] with rebased
indices): `dropped=0` and **f=23 000 recs=1 151, 32 passes, 1 146 draws** -- the game's full render pipeline now
reaches Metal (E445: ~100 recorded, 2-3k dropped); later frames reach 3 214 draws (dropped 94-490 when VB/IB fill,
a capacity question). But the surface dumps at f=30 500/30 501 -- 24 files, every BGRA8 colour surface -- are all
**max=0** (R=G=B=0 on every pixel), the display buffers included; so is frame_22000, a frame whose trace is one
clear plus ONE textured quad into a display buffer. So nothing drawn in per-draw mode was ever visible, even a single
quad; in legacy mode the Bluepoint logo showed. (E441's teal (8,24,28) was most likely a clear colour fed by the
desynced FIFO, gone since E442.)
Cause, from the per-pass trace already in hand (E444, consecutive frames): f=28272 `S0:0 S1:1 show=2`, f=28273
`S1:0 S2:1 show=0`, f=28274 `S2:0 S0:1 show=1` -- every display buffer is CLEARED in the frame right after the one
that drew it, before any flip shows it. On the RSX the game binds the new surface and clears it; our
rsx_commands.c calls set_render_target only in the draw-time dirty flush (lines 509-537), not before
NV4097_CLEAR_SURFACE, so the clear record took the backend's previous colour target -- the buffer just drawn. Legacy
mode applied clears frame-globally, which hid it. E447: CLEAR_SURFACE flushes pending surface state
(set_render_target, surface_dirty = 0) before dispatching the clear.

## E447 — the clear lands on the right buffer; still black; the "logo test" frame was not a logo

E447 (boot_gow2_e447 = E446 + CLEAR_SURFACE flushes surface state first): the per-pass trace now shows clear and draw
in ONE pass on the same surface (f=349 `S3:1`, f=350 `S1:1`, f=351 `S2:1`; display offsets 0x400000/0x7C0000/
0xB80000 = S1/S2/S3 here, each drawn in its frame and shown two frames later) -- the E446 cause is fixed. frame_22000
(22:10:45), frame_23000, frame_24000, frame_25000: all max=0.
Correction of E446's inference (same round): "frame_22000 is one textured quad, so the basic per-draw path is broken"
does not follow. The E447 timeline (`[TL] present_mode ... draws=` every 50 frames) shows 2 records per frame
(clear + 1 quad) from f~349 to f=22 400 and then **straight to the 3D scene** at f=22 450 (1 034 draws; 1 151 by
22 750) -- no ~68-draw logo phase at all with the FIFO parsed correctly (the E435/E439 "Bluepoint logo drawn by the
game" came from a stream still fed stale words). That single quad may simply be a black background/fade, so it tests
nothing. What stands: E447's 3D frames are black (max=0) with the clear fixed, VP constants bound and all draws
recorded.
E448 (diagnostic, gated): PS3_METAL_DEBUG_FILL=1 swaps every PSO's fragment function for constant magenta and
PS3_METAL_DEBUG_NODEPTH=1 disables the depth/stencil test in the per-draw replay -- magenta in the surface dumps
means geometry, viewport and present work and the black is the fragment shaders/textures; still black means
vertices/viewport/scissor. The trace adds each frame's first real draw (target, viewport, scissor, count, v0, c0).

E447 surface dumps (f=30 500/30 501, written 22:20:07; the 22:03 files are E446 leftovers with other indices):
three surfaces of the downsample chain are FULL -- s9 0x02720000 304x171 99.9 % non-black max 255, s10 0x02752C80
304x171 99.9 %, s11 0x027A0200 76x42 99.8 % -- while the scene target (0x010A0000), the post target (0x01E60000) and
the display buffers stay 0. The full ones are pure UV gradients: 43 776 distinct colours (= 256 x 171), mean
(127,127,0), R rising with x, G with y, B 0 (image checked). So per-draw rasterization, targets and surfaces work --
these are the first game pixels in per-draw mode -- and the fragment output is wrong.
Cause, measured in the E447 log: `set_shader: empty/no-sample FS (instr=21 sample=1 noop=1) -> textured passthrough`
(also instr=10/3/2 with sample=1). metal_set_shader replaced the decompiled FS whenever it had no `.sample(` OR
contained `return r[0];` -- and `return r[0]` is the normal NV40 colour output (the decompiler returns h[0] only for
H0-only shaders, rsx_fp_decompiler.c:527-538). So every sampling shader of the game was thrown away and every draw
copied texture unit 0 through tc0 (a UV-like input on those passes; the scene's unit 0 is black). E448: the
passthrough is opt-in (PS3_METAL_FS_PASSTHROUGH=1, A/B only); the decompiled FS is always used. Plan changed in the
same round: E448 runs WITHOUT the magenta/no-depth diagnostics (the UV gradients already prove rasterization), which
stay in the code gated OFF.

## E448 — the game's real fragment shaders run; every surface is 0

E448 (boot_gow2_e448 = E447 + FS passthrough opt-in; per-draw, no diagnostics): no shader failure in the log
(no `FS/VS library failed`, `PSO create failed`, `decompile ... failed`), `shader stats: fn_compiles=150
pso_builds=180`. First real draw of the 3D scene: `tgt=S4 vp=(0,0 1216x684) sc=(0,0 4095x4095) n=256 idx=1 pso=1
v0=(-386,0,1190) c0=(0,0,0,0)` (object/world coordinates; c0 alone says little). Surface dumps at f=30 500/30 501:
all 26 files max=0, including the three bloom surfaces that were UV gradients under the passthrough -- so the real
shaders run and write 0 (or nothing reaches the targets). frame_27000: black.
Not game pixels (checked in the same round): frame_26000 shows "Sony Computer Entertainment presents", but its
present_mode is `content=1 hold=1` -- the HOSTRES boot-logo queue (scep_e.ctxr, START at log line 31 440) -- i.e.
our host overlay, which does not count.
Next: E449 = the same binary with PS3_METAL_DEBUG_FILL=1 PS3_METAL_DEBUG_NODEPTH=1 (magenta in S4 -> geometry,
viewport and depth are fine and the FS output is the problem; still 0 -> vertices/matrices/clip); E450 = a new
build with PS3_METAL_DUMP_MSL=1 (every decompiled FS/VS pair written to msl_fp*_vp*.metal) to read the shaders.

E449 (same binary as E448 + PS3_METAL_DEBUG_FILL=1 PS3_METAL_DEBUG_NODEPTH=1; `debug fill fragment function ready`):
surface dumps at f=30 500/30 501 -- s9 0x02720000 and s10 0x02752C80 (304x171, downsample fullscreen quads) **100 %
magenta**; the scene target S4 0x010A0000, s8 0x01E60000, s14 608x342, s11-s13 and the display buffers **0 %**. With
every fragment forced to magenta and no depth test, nothing of the 3D geometry reaches its target -- only quads
already in NDC do. So the scene's black is NOT the fragment shaders: the vertex transform sends the geometry out of
the frame or collapses it. Consistent with the first scene draw's `c0=(0,0,0,0)`: if the transform matrix in the
snapshot is zero, every position becomes 0 and every triangle is degenerate. Next (E451): the first-draw trace counts
the non-zero constants of the snapshot and lists their indices, plus the MSL dump (which vp_c[] the VPs read).

## E451 — the VP constants are there (c[256+]); the VP decompiler rotated the operand register types

E451 (boot_gow2_e451 = E450 + constant census in the first-draw trace; per-draw, PS3_METAL_DUMP_MSL=1; E449 was
stopped by PID after its dump): first scene draw `tgt=S4 ... n=256 v0=(-386,0,1190) c0=(0,0,0,0)`, and
`first draw consts: nonzero=71/512 idx: 256 257 258 ...` -- the transform constants DO reach rsx_state, from index 256
up (0-255 empty, hence c0=0). 186 MSL files dumped; their vertex parts read vp_c[0], vp_c[256..263] and
vp_c[456..467]. One of them (msl_fpDD5B993E_vpC9D63F72.metal) computes the position as
`r0 = vp_c[257].yyyy*r[0]; r0 = vp_c[256].xxxx*r[0] + v[0]; ...; o0 = vp_c[259].wwww*r[0] + v[0]` -- the standard
Cg matrix chain `MUL R0, v0.yyyy, c257; MAD R0, v0.xxxx, c256, R0; ...` with the operands' register kinds rotated:
the input got the constant's name, the constant became r[0] (zero), the temp became v[0]. So r0 stays 0 and every
position collapses -- exactly E449's picture.
Cause (read, rsx_vp_decompiler.c emit_src): the 17-bit source reg_type was decoded 1 = input, 2 = constant, other =
temp; the NV40 encoding is 1 = temp, 2 = input, 3 = constant (upstream sp00nznet documents it as verified against the
oracle; our 17-bit field assembly already matched theirs). Same file, same round, against the same upstream rules:
VEC ADD read src0 + src1 (it reads src0 + src2); the VEC result went to o[] OR r[] (it goes to o[D3.dst] when
vec_result AND r[D0.dst_tmp]); the SCA unit read src0 (it reads src2) and wrote both o[] and r[]; the output map had
TEX0..7 = o[8..15] (it is o[7..14]). All five fixed in E452 (HLSL and MSL paths); relative constant addressing
(D3.index_const + a0, used by skinning) is still not decoded.

E452 (boot_gow2_e452 = E451 + the five VP-decompiler fixes): the regenerated msl_fpDD5B993E_vpC9D63F72.metal now reads
`r0 = v0.yyyy*c257; r0 = v0.xxxx*c256 + r0; r0 = v0.zzzz*c258 + r0; o0 = v0.wwww*c259 + r0` and forwards TEX0 from
o[7] -- the standard matrix chain. Run: 903 s to the deadline, no crash, f=45 743 (18.4 M draw calls). The first
game frames came at f~33 000 this time (the intro's frame count varies), so PS3_METAL_SURF_DUMP=30500 fell inside the
intro and wrote nothing. frame_33000 = the host logo hold again (1.6 %); **frame_34000 ... frame_45000 all black**. The
fixed transform alone does not put an image on screen. Suspects in order: shading inputs (MetalBasicVertex carries
only position + colour -- normals, texcoords and every other attribute the game's VPs read arrive as 0, plan M6);
the clip-space z convention (RSX -w..w vs Metal 0..w); relative constant addressing (skinning). E453 separates them:
magenta FS + no depth + PS3_METAL_SURF_DUMP=auto (first frame with >= 500 records and the next).

E453 (boot_gow2_e453 = E452 + PS3_METAL_SURF_DUMP=auto; magenta FS + no depth): auto dump at f=32 901/32 902 (26
files). With the fixed VP the picture changed from E449: the display buffers S1/S2/S3 (1280x720), s14 (608x342) and
the whole bloom chain s9-s13 are **100 % magenta** (E449: displays 0 %) -- the post chain and the final composite now
rasterize. The scene target S4 (0x010A0000, 1216x684), s8 (0x01E60000) and s6 (offset 0) stay **0 %**: the 3D scene
draws still produce no fragment, with no depth test and a constant FS. Open question: where the scene's vertices land
(w <= 0, |x|/w or |y|/w > 1, z outside Metal's 0..w). E454 prints c[256..263] for the first scene draw so v0's clip
position can be computed.
Vertex-attribute census (the 220 VS pairs E452 regenerated, program body only): v0 213, v8 (TEXCOORD0) **220**, v3 74,
v7 31, v2 8; not one reads only v0/v3. MetalBasicVertex feeds v0 (position) and one float4 (colour, or UV packed into
it); v8 is seeded from that float4's xy by a preamble hack, v2/v7 are 0. So once the scene's geometry shows, its
shading still needs plan M6 (all attributes). metal_fetch_attrib_f4 already decodes any RSX vertex type to float4.

E454 (boot_gow2_e454 = E453 + c[256..263] in the trace; magenta + no depth; stopped by pgrep PID once its data was in):
auto dump f=21 930/21 931 -- same picture as E453 (displays and post chain 100 % magenta, S4/s8/s6 0 %). The game's
constants, first scene draw: c[256..259] = (1.48461,0,0,0) (0,2.63931,0,0) (0,0,-1.0008,-1) (0,0,-10.008,0) -- a
standard perspective projection in columns (near ~5, far ~12 500); c[260..263] = a world-view with c[263] =
(0,0,0,1) (camera-centred, skybox-like). At f=22 500: the same projection family and c[263] = (17.33,-34.31,-6.31,1).
The scene VS (msl_fp59864B76_vpC106DEA9) now computes `r0 = WV*v0` (c260..263) then `o0 = P*r0` (c256..259) --
correct. By hand: the f=22 500 first vertex (13.1,334,-3.62) lands at NDC (0.90,-1.04,0.53), w=21.3 -- on screen, z/w
inside Metal's 0..1; the first (skybox) vertex lands behind the camera (w<0), plausible for one vertex. So the data
and the VS math hold.
Flaw found in my own diagnostic (same round): PS3_METAL_DEBUG_FILL only swapped the fragment function and kept the
game's colour write mask and blend state. A depth pre-pass (colour mask 0) or a multiplicative blend onto a black
target hides magenta completely, so "S4 0 % magenta" did not prove that nothing rasterizes into S4. E455: the debug
fill also forces writeMask = All and blending off (decompiled PSOs and the fixed pipeline).

E455 (boot_gow2_e455 = E454 + debug fill forcing writeMask=All and no blending; magenta + no depth; auto dump at
f=33 048/33 049): **the scene target S4 (0x010A0000) is 43.6 % magenta at f=33 048 and 100 % at f=33 049**; s8 100 %,
the displays and the post chain 100 %; only s6 (colour at offset 0) stays 0. So the 3D scene's geometry DOES
rasterize with the fixed VP. Retracted (E449/E453/E454 reading, same round): "nothing of the 3D geometry rasterizes"
-- those runs kept the game's colour mask and blend, which hide a constant FS. What stands: the real shaders leave S4
black, so the blocker is the colour mask / blend / depth test path (typical: a depth pre-pass with colour writes off,
then a main pass with depth EQUAL that fails if our depth between passes does not match) or the shading inputs (M6).
E456 separates depth from shading: the same binary, real shaders, PS3_METAL_DEBUG_NODEPTH=1.

## E456 — no depth test, real shaders: the scene stays black; the legal screen comes from the game's own textured draw

E456 (boot_gow2_e455, real shaders, PS3_METAL_DEBUG_NODEPTH=1, PS3_METAL_SURF_DUMP=auto; stopped by pgrep PID at
~7 min once its data was in): auto dump at f=34 191/34 192 (26 files). The scene target S4 (0x010A0000) is **0.0 %
non-black (max 0)**, s8 0 %, the bloom chain 0 %, S2 0 %. So with the depth test off the real shaders still write
black into the scene: the depth test is not the (only) blocker, the fragment output itself is 0 -- shading inputs
(plan M6: v[8]/v[7]/v[2] never delivered) or the FS constant/texture path.
The display buffers S1 (0x00400000) and S3 (0x00B80000) are 10.9 % non-black, ~75 colours: the game's **WARNING
(epilepsy) legal screen**, readable. The [PERDRAW] trace at f=34 000 has one record: a full-screen quad
`tgt=S3 n=4 idx=1 pso=1` with texture unit 0 bound (`tex0-3: T - - -`). The surface dump reads our per-draw surface
textures, which only records (draws, clears, blits) write, so that image is the game's own draw -- its fragment program
and a guest texture -- through the Metal per-draw path; not the host overlay.
Later: frame_36000 / frame_37000 (drawable dumps) black. f=37 500: 3 166 records, 30 passes (S4:738, S8:128, the bloom
chain S9..S12), **dropped=442** while under the 4 096-record cap -- the drops come from the 262 144-vertex buffer being
full (upload returns 0), not from the record cap.
E457 (boot_gow2_e457 = E456 + plan M6, same env incl. no depth, to isolate M6): every enabled RSX attribute of a draw
is decoded to float4 into a per-frame ring (64 MB); the decompiled VS reads v[0..15] from it by vertex_id through a
per-draw slot map (buffer(2) map, buffer(3) data; stage_in kept, the fixed pipeline untouched); S32K and CMP decoding
added to metal_fetch_attrib_f4 (CMP = 11/11/10 signed normalized as I recall RPCS3's decode -- not yet checked against
the oracle); METAL_MAX_VERTS 262 144 -> 1 048 576. Default ON only with PS3_METAL_PER_DRAW_RT (PS3_METAL_VA=0/1).

## E457-E458 — the M6 binaries take a different intro path (a regression of mine, deterministic)

> **ATTRIBUTION RETRACTED (E460, same round).** The divergence and every number below stand; the cause is not my
> Metal/VP code. The E455 binary was linked from the **recomp_macos_e435** lift (13 objects rebuilt there at
> 23:35-23:45, none in v2); every binary from E457 on came from `build_macos.sh` with no argument, whose default is
> **recomp_macos_v2** (17 objects rebuilt there after 23:50, none in e435) -- the lift with the always-on E404-E408
> patches. R1 (E459, all M6 behaviour gated off: VA=0, no binds, old vertex cap, old MSL signature) was still bad,
> consistent with that. Kept as the record of the error and of what caught it.

E457 (first run, stopped at 116 s): the attribute trace showed `va n=16` on the legal quad -- every array counted as
enabled, 13 of them `type F / size 0 / stride 0`. On RSX size 0 disables an array (upstream HEAD sets
`enabled = size != 0`; ours used `type != 0`). Filter fixed (size != 0): E457b/c show `va n=2: v0 t2/s3/st12,
v8 t2/s2/st8` -- position + TEXCOORD0, the legal screen's layout.
The regression, measured on every run of the new binaries (E457, E457b, E457c = M6; E458 with PS3_METAL_VA=0):
the intro FSM goes `st620 1 -> 10` where the E455 binary goes `1 -> 11 -> 1 -> 3`; the first vdec AU is **128 B**
(E456: **19 401 B**, the sequence header), then 144 B each (E456: 468/513); the VideoToolbox session closes
`in=8 out=0 err=8` (E456: `in=40 out=40 err=0`); no GetPicItem; 20 x `[tty] suspicious len=65536` (E456: 0); no
cellSaveDataAutoLoad2; 0 game frames; `done=0 shaders=2` for minutes (E455r: `done=14 shaders=13` at 60 s).
Upstream of the vdec: before AU #1, E456 inflates real data (csz ~64 KB -> 65 536 B) into 0x438BC540..; E457c
inflates near-empty chunks (csz 90..137) into 0x4361A400.. -- the guest reads different data.
Controls: **E455r** (the pre-M6 binary re-run now, 180 s, same env minus the surface dump): clean -- `1 -> 11 -> 1
-> 3`, tty 0, AutoLoad2 reached, 45 game frames, 38 998 flips, 6 428 shells. So neither the environment (./gamedata
untouched since 23:30) nor load (E457 ran alone) explains it. **E458 with PS3_METAL_VA=0**: still bad -> not the va
fill or its 64 MB ring. Refuted by measurement: the pad autostart (E457c calls cellPadGetData 0 times before
`1 -> 10`; E456 first polls at line 7735), guest-memory commit failures (0 in all runs), a guest write from the
Metal backend (none in the source). The only sources changed since the E455 binary: rsx_metal_backend.m,
rsx_vp_decompiler.c (+ rsx_commands.c/.h in E458); gen/ppu_hle_nids.cpp regenerated byte-identical; __bss +96 KB.
Mechanism still unknown (host timing/state is the remaining class, not proven). E459 = the same code + diagnostic
A/B gates, OFF = current behaviour: PS3_METAL_MAXV_OLD=1 (262 144-vertex buffer), PS3_METAL_VA_NOBIND=1 (no
buffer(2)/(3) binds), PS3_METAL_VS_OLDPRE=1 (pre-M6 MSL signature).
Also in E458/E459, checked offline only (vp_rel_test.c, synthetic VP: ARL + index_const + saturate -> `a0.x =
floor(v1.x)`, `vp_c[(10u + uint(a0.x)) & 511u]`, `saturate(...)`), not yet in-boot: relative constant addressing
(D3.index_const + a<addr_sel>.<addr_swz>, upstream HEAD's rule), ARL writing only the address register, D0.saturate;
NV4097_SET_VERTEX_DATA4F_M (0x1C00) kept in rsx_state.vertex_data4f (reset 0,0,0,1) and fed to disabled attributes
through the VS slot map.

## E460 — M6 + VP fixes on the right lift: healthy intro, no drops; the scene target still 0 %

E460 (boot_gow2_e460 = current code, all A/B gates OFF: M6 attributes + VERTEX_DATA4F + relative constant addressing +
ARL/saturate + 1 M-vertex buffer; linked with `./build_macos.sh recomp_macos_e435`, 13 objects there, 0 in v2; env as
E456, no depth test; 300 s): intro as E455/E456 -- AU #1 **19 401 B**, VT `in=16 out=16 err=0`, tty 0, AutoLoad2
reached, 48 game frames, `done=14 shaders=13` (the `st620 1 -> 10 -> 11 -> 1 -> 3` path is a variant, not a failure
marker). f=35 500: 1 731 draws in 31 passes (S4:1 113), **dropped=0** (E456: 442 at f=37 500). Auto dump f=35 120
(surf_stats.py): **S4 0.0 %**, s6/s8/bloom chain 0 %, S2/S3 11 % (the WARNING screen, 76 colours). So delivering every
vertex attribute (and the VP fixes) does not light the scene: the black is not (only) the missing inputs.
Next discriminator (E461/E462): PS3_METAL_DEBUG_FILL=uv paints fract(TEXCOORD0)+0.25 blue, =tex paints texture unit 0
at TEXCOORD0, both with colour mask forced on and blending off, on decompiled-VS PSOs.
Also this round (security-review finding on my uncommitted 2D engine code): the CPU 2D paths now check the whole
source/destination footprint against committed guest memory in u64 (gcm2d_range_ok), clamp NV309E log2 sizes to 12
before shifting, and cap NV3089 output at 4096; they stay gated OFF (PS3_GCM2D_CPU).

## E461 — UV debug fill: the scene rasterizes with real texture coordinates

E461 (boot_gow2_e461 = E460 + uv/tex debug modes + the 2D bounds checks; e435 lift; PS3_METAL_DEBUG_FILL=uv, no depth;
200 s; intro healthy, AU #1 19 401 B, tty 0): auto dump f=37 199. The scene target **S4 is 43.7 % covered with 1 024
UV colours** (mean 122,141,64; the 64 is the +0.25 blue) -- the same coverage E455's magenta had, now with varying
TEXCOORD0: the decompiled VS with M6 attributes delivers texture coordinates. The post chain s9..s14 and S0/S1 show
full 0..1 gradients (mean 127,127,64). S2/S3/s8 are one colour (0,0,64) = UV 0 in this frame, worth a look but not the
scene; s6 0 %. So geometry and UVs stand; the real scene's black is in the texture content/alpha, the FS math or the
blend/alpha state. Found reading the texture path meanwhile: the Metal backend never applies TEXTURE_CONTROL1 (the
component remap crossbar -- rsx_commands.c stores it, rsx_metal_backend.m never reads it), and the 32-bit upload keeps
the guest's 4th byte as alpha for D8R8G8B8 (0x9E), the scene's dominant texture format (64x64..256x256, swizzled).
Upstream HEAD applies the crossbar (rsx_texture_component_remap: selectors {A,R,G,B} in the low byte, ops 0 ZERO /
1 ONE / 2 REMAP in the high byte, unset = identity 0xAAE4, G8B8 lanes {G,R,G,R}).

## E462 — texture debug fill: the game's own textures fill the scene target

E462 (boot_gow2_e461, PS3_METAL_DEBUG_FILL=tex: texture unit 0 sampled at TEXCOORD0, colour mask on, no blending,
no depth; 200 s; intro healthy): auto dump f=34 301. **S4 (0x010A0000) 94.7 % non-black, 165 colours, mean RGB
(88,61,47)** -- brown/stone tones, the game's own textures sampled at the VS's texture coordinates, rasterized by
Metal into the scene target. S1 and s8 are one flat white (the dummy texture: those draws bind nothing valid on unit
0); the post chain samples render targets and is 0 in this mode. So vertices, attributes, UVs, texture decode and
upload all hold; the real shaders' black in S4 comes after the texture fetch -- the FP math/constants or the
alpha/blend path.
E463 = E462 code + TEXTURE_CONTROL1 applied as the Metal texture swizzle (metal_remap_swizzle, upstream HEAD's
crossbar rule; part of the texture-cache key; B8/G8B8 keep the identity for now, and the non-cache per-slot path
does not apply it yet), real shaders, no debug fill, env as E460; the first 16 distinct (format, control1) pairs
are traced.

## E463 — the texture remap is applied and was needed; the scene stays black with the real programs

E463 (boot_gow2_e463 = E462 code + TEXTURE_CONTROL1 as the Metal swizzle; real shaders, no debug fill, no depth):
the game's crossbar values, traced: **0x9E (D8R8G8B8) and 0x86 (DXT1) use control1 = 0xA9E4 = identity with alpha
forced to ONE**; 0x85 uses 0xAAE4 (identity) and 0xAA93 (a rotation, r=A g=R b=G a=B). Without the remap the Metal
path kept the stored 4th byte as alpha on the scene's D8R8G8B8 textures. The scene's first FP (msl_fp59864B76) is
`h0 = tex0(tc0) * tc1; h0.rgb *= h0.a; h0.a = h0.x`, and its VP writes `o[8] (TEX1) = c[467]`: the colour is texture x
a material constant, premultiplied by alpha; 114 of the 191 texturing FPs multiply by tc1. Auto dump f=36 531: **S4
still 0.0 %** (displays 11 %, the WARNING screen). So the remap is necessary but not sufficient. Left: c[467] (the
[PERDRAW] trace now prints c[456..467]) and the game's blend/colour-mask state -- E464 adds PS3_METAL_DEBUG_FILL=blend,
the REAL fragment programs with only the mask forced on and blending off.

## E464-E465 — the real programs light the scene with blending off; 24 state methods were mis-numbered

E464 (boot_gow2_e464, PS3_METAL_DEBUG_FILL=blend: the game's REAL fragment programs, only the colour mask forced on
and blending off; no depth): auto dump f=28 651, **S4 27.3 % non-black, 198 colours**, mean (21,29,38) -- a dark
scene with blue light falloff and a small character in the middle (the last passes overwrite, blending being off).
E463, same code with the game's blend: S4 exactly 0 (max 0). So the fragment programs compute colour, and our blend
state translation wipes it.
c[456..467] at the first scene draw (new trace): c[467] = (4,-0.5,0.5,0). E465 (same binary, PS3_METAL_DUMP_MSL=1)
regenerated msl_fp59864B76_vpC106DEA9 with the current decoders: the VP still writes `o[8] = c[467]` (no relative
read there), so that pair's output (tex0 * TEX1, rgb premultiplied by its alpha) is 0 by arithmetic -- one pass, not
the scene; whether that FP is decoded right is still open.
The cause of the blend result, found comparing every NV4097_* define with upstream HEAD: **24 state methods had
wrong numbers** in our rsx_commands.h -- blend SFACTOR/DFACTOR/EQUATION/COLOR 0x344/0x348/0x34C/0x350 (real
0x314/0x318/0x320/0x31C; our "sfactor" was reading STENCIL_OP_ZPASS), COLOR_MASK 0x028 (0x324), depth
TEST_ENABLE/FUNC/MASK 0x304/0x308/0x30C (real 0xA74/0xA6C/0xA70; our "depth test" was reading the ALPHA test
block), alpha test 0x104/0x108/0x10C (0x304/0x308/0x30C), the stencil block (0x328..0x344), cull/front face
(0x1830/0x1834/0x183C), COLOR/ZSTENCIL_CLEAR_VALUE (0x1D90/0x1D8C) and VERTEX_ATTRIB_OUTPUT_MASK (0x1FF4). The same
class as the surface table fixed from upstream 247251b0; upstream's values match RPCS3's gcm_enums. All 24
corrected; SET_BLEND_COLOR is now decoded too (the Metal side does not set the blend constant yet). This decoder is
shared, so the D3D12 path changes as well (not measured here). E466 re-measures with the real blend and
PS3_TRACE_RSX_STATE=1 to see the game's factors.

## E466 — with the state methods fixed, the game's real pipeline draws: the window shows an in-game frame with HUD

E466 (boot_gow2_e466 = the 24 method fixes + SET_BLEND_COLOR decode; real shaders, real blend, NO debug fill; no
depth test as E463; PS3_TRACE_RSX_STATE=1; e435 lift; 240 s; intro healthy, AU #1 19 401 B, tty 0). The game's
state, now decoded from the right registers: blend enable=1 sf=0x03020302 df=0x03030303 (SRC_ALPHA,
ONE_MINUS_SRC_ALPHA, RGB and alpha), color mask 0x00010101 (RGB, no alpha), depth test off, alpha test off (the
trace printed every call and its cap filled in the intro; it now prints changes only).
Auto dump f=35 325: **S4 29.2 % non-black, 1 090 colours** (E463, same program path with the wrong blend registers:
0.0 %) -- a character in T-pose on a platform under blue light (surf_f35325_s4); s8 46.3 %, the bloom/post chain
s9..s14 29-38 %, s0 46.6 %. The window dumps: **frame_36000 / 37000 / 38000 100 % non-black, 479 / 4 019 / 2 434
colours**; frame_37000 shows a character with glowing eyes and a lit weapon and the game's HUD (health bar and magic
orb, top left), under a strong yellow cast (the post chain's colour/bloom output) and with broken triangles;
**frame_38000 is the Power Up menu** ("Athena's Blades", "Level 5", "AT MAXIMUM", "Triangle = Back / Cross = Power
Up", the Spartan helmet art) in correct colours, a few glyphs missing. Checked they are the game's pixels, not host
content: at f=36 000/37 000/38 000 `present_mode movie=0 content=0 hold=0`, and the displayed buffer comes from 549 /
549 / 326 recorded game draws (f=38 000: 318 draws into s8, then the composite). So the game's own fragment programs,
textures and blend now reach the screen through Metal -- the goal's "Metal translates shaders and textures"
in-boot, not in a debug mode.
Not claimed: that this is a level. LDRSH is still 0 (13 shaders loaded/compiled, 6 472 shells); the HUD frame may
be the front end's attract/demo loop. Open: the yellow cast (post chain / blend constant, which Metal does not set
yet), the broken triangles, the non-cache texture path without the remap, the fp59864B76 pass that is 0 by
arithmetic.

## Retractions in this session (counts stand, inferences fall)

- "E457b's intro hang is the known intro flakiness" (said to the user): wrong -- 4/4 runs of the new binaries take
  the same wrong path, 3/3 of the old binary do not (E457-E458).
- E457-E458 "the M6 binaries regress the intro": the binaries differ by LIFT (recomp_macos_v2 vs the baseline's
  recomp_macos_e435), not only by my code; R1 with all M6 behaviour off was still bad. The Metal/VP changes are not
  implicated by that evidence (E460 re-measures them on the e435 lift).

- "fences end intro flakiness" (A'/F 4/4 vs 0/4) — F had 2/5 stuck in series 3: chance.
- "Metal M2/M3 not implemented" (said to the user) — both are implemented and ran on real game content
  (10 FP->MSL PSOs, 8 bind_texture) in the visible run; only 1-instruction blit shaders so far.
- "0x011A is the SPURS kernel return" — ledger §4i had already shown it is a missing split in spu1.
- E411 §4 "the 70 shells of E402 depended on the F2B fallback": refined — the mechanism is the
  sticky/recycled-op inheritance (E413); whether F2B also fired in E402 remains unmeasured.
