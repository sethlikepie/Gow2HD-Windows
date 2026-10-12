# Task R9 report — menu/game present schedule after thr

**Date:** 2026-07-23  
**Status:** DONE (diag) — Gate B GREEN · Gate A RED · **no fix** (H1, not H4)  
**Log:** `/tmp/r9_menupresent.log` (TIMEOUT=150, `PS3_NO_RSX=1`, `PS3_TRACE_MENUPRESENT=1`)

## Deliverables

| Item | Path |
|------|------|
| Probe (idempotent) | `gow2-recomp/recomp_mid_v2/patch_menu_present_schedule_probe.py` |
| Note | `gow2-recomp/notes/2026-07-23-r9-menu-present-schedule.md` |
| Verdict update | `gow2-recomp/notes/2026-07-23-gate-a-verdict.md` (R9 + R10 wall) |
| INDEX | `ps3recomp/docs/superpowers/plans/2026-07-23-00-gow2-playable-master-INDEX.md` |

## Caller map (static RE)

| Target | Parent(s) |
|--------|-----------|
| `194FFC` | `BB4E0` only → JT case of `BB424` ← `B61F4` ← `B6150` ← `B94EC` |
| `2B21C4` | only `2B25AC` (backward tramp; no external call) |
| `2B21F8` / `2B2288` | internal frags of `2B21C4` |
| `2C07F8` | movie FSM JT **state 11** via `2C0508` ← `CE03C` (intro) |

Boot chain thr-adjacent: `10230→10354→25C838→2B2E74→B71B8→CE0A0` (oneshot).

## Enter counts (SUMMARY, rperma_full=1)

| fn | tot | post |
|----|----:|-----:|
| 2C0508 | 6147 | **0** |
| 2C07F8 | 2 | **0** |
| CE03C / CE0A0 / B71 / 2B2E74 / 25C838 | 1 each | **0** |
| 194FFC / BB4E0 / BB424 / B61F4 / 2B21* / 2B25AC | 0 | 0 |

**SetFlip_after_R_Perm = 0** · thr_end=1 · R_Perm full=1

## Hypotheses

| H | Verdict |
|---|---------|
| H1 never scheduled post thr | **CONFIRMED** |
| H2 early-return before 156680 | **DEAD** (no enter on menu path) |
| H3 TYPE15 f4 sole block | **co-sinal only** (TYPE15 runs post thr without MENUPRESENT post) |
| H4 our skip/breaker | **DEAD** |

## Next (R10)

Who **schedules** menu/frame present parent after thr: arm of `B61F4`/`BB424`, real OPD entry of `2B21C4`, or re-call of `25C838`/`2B2E74`. Aceite: SetFlip_after_R_Perm≥1 or enter-post≥1 on that scheduler.

## Commits

- gow2-recomp: probe + notes + gate-a-verdict (see git log)
- ps3recomp: task-R9-report + INDEX one-liners
