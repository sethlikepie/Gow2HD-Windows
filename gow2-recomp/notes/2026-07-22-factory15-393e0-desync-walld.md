# Goal verdict: factory 0x15 + full 393E0 + DESYNC residual + Wall D

**Date:** 2026-07-22  
**Binary:** `gow2-recomp/boot_gow2` (lift `recomp_macos_v2`, original RESYNC path)  
**Proof log (canonical):** `{SCRATCH}/b71_full_393e0.log`  
(also copied to `factory_015.log`, `rperm_stream_end.log`, `wall_d_natural.log`)

## Recipe

```bash
. ./env_gow2.sh
export PS3_NO_RSX=1 PS3_PERF_FSM=1
export PS3_MOVIE_EOS=1 PS3_VDEC_ASYNC=1
export PS3_VDEC_FORCE_SEQDONE_MS=6000   # reliable intro EOS only
export PS3_TRACE_POSTINTRO=1
export PS3_B71_FULL_393E0=1             # full registry path (not HLE-lite)
./boot_gow2 EBOOT.ELF
```

No CRC bypass, no GATE synthetic WAD stream, no SHADER_DEMO as acceptance.

## Pass/fail table

| # | Criterion | Result | Evidence |
|---|-----------|--------|----------|
| 1 | Factory type 0x15 live (not SKIP-only) | **PASS** | `TYPE15 SNAP vt=0x00516D70`; `REPAIR was=0x5F436F75`; `CB56C icall1 tab=0x00868D48 idx=0x54 ent=0x401002F0 vt=0x00516D70 opd=0x0051B300 code=0x0039E794`; `CB56C icall1 SKIP` = **0** |
| 2 | Full 393E0 returns + B71 exit | **PASS** | `B71 after 393E0` with `HLE-lite=0`; `exit func_000B71B8 #1 … +0x64=1` |
| 3 | R_Perm end: zero DESYNC-STOP | **PASS** | `DESYNC-STOP=0`; `EOF-QUIET pos=20169344/20169344` (×8); R_Perm FULL markers ≥1. Mid-FO `RESYNC` still present (cap 16) — band-aid, not criterion fail |
| 4 | Wall D natural diagnostic | **PASS (diagnostic branch)** | Typemap **built** (prior TYMAP-DUMP vt+8=0x522E70); walk still 0: CMP-ENTER=0, TYMAP-171=0, LDRSH=0. Next gate: natural schedule into `00468C3C` / vcall OPD `0x522E70` (see `notes/2026-07-22-wall-d-goal-verdict.md`) |

## Criterion 1 — factory 0x15 detail

| Step | What |
|------|------|
| Stamp | First resolve at `SNDX_R_LglScA`: live vt `0x516D70` on obj `0x401002F0` (table `0x868D48[0x54]`) |
| Corruption | After R_Perm TOC / before `SNDX_R_Perm`, `*obj` stomped with ASCII `0x5F436F75` (`_Cou…`) — same class as SBP buffer-as-object |
| Fix | `ps3_type15_note_resolve` SNAP + `ps3_type15_repair_if_needed` before CB56C icall1 |
| Live call | CB56C uses repaired vt → valid OPD/code; product may be 0 / junk `0x27182814` (rejected) — early-out, not SKIP |

**Honesty:** root stomp (who writes stream bytes over the factory object) is **mitigated**, not eliminated. Acceptance is live resolve without SKIP-only path.

## Criterion 2 — full 393E0

With `PS3_B71_FULL_393E0=1`, `func_000393E0` is invoked (no HLE-lite stamp). Returns; B71 continues through 251230 / icallA guard / `+0x64=1` exit.

Post-exit noise: `FATAL stuck calling 0x27182818` (heap-as-code, same band as CB56C reject product) — **not** a B71-exit failure.

## Criterion 3 — DESYNC residual

| Before (earlier leva) | After |
|----------------------|-------|
| DESYNC-STOP at FO FULL with ASCII sizes | **EOF-QUIET** at FULL, state/rem idle |
| T1 stuck ~010 | Members through TXR/Comic/**HealthChest** (when T1 probes on) |
| Residual hangs 41D5C forever | RESYNC cap → EOF-QUIET → B71 proceeds |

`F2B-STREAM-RESYNC` (≤16) remains a mid-FO band-aid after HealthChest. Criterion requires **zero DESYNC-STOP**, not zero RESYNC.

## Criterion 4 — Wall D next gate

Unchanged from diagnostic note:

> Natural **schedule** of micro-ctor / hashmap-set that would enter `00468C3C` (or any site that vcalls `vt+8` / OPD `0x522E70`).

Not next: CRC host fill, combination stub, GATE-FORCE synthetic stream as pass.

## Code sites (lift, gitignored)

| Piece | Where |
|-------|--------|
| TYPE15 SNAP/REPAIR | `ppu_recomp_001.cpp` `ps3_type15_*` |
| CB56C live + product reject | `ppu_recomp_000.cpp` `func_000CB56C` |
| Full 393E0 gate | `ppu_recomp_000.cpp` B71 path `PS3_B71_FULL_393E0` |
| DESYNC/RESYNC/EOF-QUIET | `ppu_recomp_001.cpp` body header path + `f2b_stream_*` |
| Stream multi-MB base | `recomp_mid_v2/patch_f2b_multimb_stream.py` |

## Non-regression

- Intro/EOS: st620 0→1→3→**≥11** with FORCE seqdone
- R_Perm open + FULL `20169344`
- B71 exit `+0x64=1`
- No freelist hang `need=0x687DD790` on this path

## Still open (follow-ups, not this goal)

1. Root of factory-object stomp (stop REPAIR need)
2. True members past HealthChest without RESYNC spam
3. Post-B71 FATAL `0x27182818` / AUTO_LOAD path
4. Wall D natural walk N≥1 → menu/registry green
