# GetWorkloadInfo ready=1 loop vs 2nd-movie FSM (2026-07-22)

## Goal
Classify the post-AUTO_LOAD stall when the game hammers `cellSpursGetWorkloadInfo`
with `ready=1`, and name **one** primary next wall.

**Proof:** `{SCRATCH}/gwi_wait_static.txt`, `{SCRATCH}/gwi_fsm_stall.log`,
`{SCRATCH}/gwi_fsm_counts.txt`, `{SCRATCH}/test_gwi_ready_wait.out`

**Recipe:** `env_gow2.sh` + `PS3_AUTO_LOAD_RUN=1` + `PS3_NO_RSX=1` +
`PS3_TRACE_POSTINTRO=1` + `PS3_B71_FULL_393E0=1` (~through AUTO_LOAD + 12s).

---

## Criterion 1 — wait site (**1a: Spurs readyCount**)

### Static chain (lift)

| Site | Role |
|------|------|
| Import trampoline `0x004B9678` | `cellSpursGetWorkloadInfo` (NID `0x4E153E3E`) |
| `func_002F2C20` | Wrapper: `GetWorkloadInfo(spurs=*(obj+4), wid=*(obj+8), info=sp+0x70)`; reads **readyCount** at `info+0x23` (= `stack+0x93`) |
| `func_002F4FD8` | Pre-check then calls `002F2C20` |
| `func_00455BD4` | `r3 = GWI_result XOR 1` → **1 iff readyCount ≠ 0** |
| **`func_00454C60`** | **Wait loop:** call `00455BD4`; if r3≠0 → `lv2_syscall` then retry; if r3==0 → return 1 (done) |

So the title is **not** polling hasSignal/contention as the primary gate here:
it **spins until readyCount goes to 0**.

### Why ready stayed 1 (HLE)

In-boot (before fix):

```
ReadyCountStore(wid=0 val=1)   // no matching PM work
GetWorkloadInfo(wid=0 ready=1 sizePm=0)  ×16
```

`sizePm=0` / `pm=NULL` → nothing on the SPU side can consume ready. Wait loop
never exits → looks like “GWI spam” + st620 stuck.

### HLE fix (shipped)

In `libs/spurs/cellSpurs.c`:

1. **`spurs_ready_consume(wid)`** after a workload job finishes (and on empty job).
2. **`ReadyCountStore`**: if value>0 but workload has **no PM** (`!pm || !sizePm`),
   **clear readyCount to 0** and log `[cellSpurs] ReadyCountStore(...) cleared (no PM)`.

This is not a forge of menu/registry — it matches “no work → ready idle”.

### In-boot after fix

```
[cellSpurs] ReadyCountStore(wid=0 val=1) cleared (no PM)
[cellSpurs] GetWorkloadInfo(wid=0 ready=0 sizePm=0 pm=0x0 cont=0)  ×2
thr_auto_load() start … end
```

| Signal | Before | After |
|--------|-------:|------:|
| GetWorkloadInfo N (post window) | ~16 spin | **2** |
| ready in GWI | 1 | **0** |
| thr_auto_load end | yes | yes |
| AutoLoad 0x8002B40B | 0 | 0 |

**Spurs readyCount wait: classified and unblocked for this path.**

---

## Criterion 2 — 2nd movie / st620 (primary wall **now**)

| Signal | N | Meaning |
|--------|--:|---------|
| st620 intro | 0→1→3→11→0 | intro still OK |
| st620 post-AUTO_LOAD | **0→0** only | **no 2nd movie FSM** |
| StartSeq | **1** | only intro vdec |
| GetWorkloadInfo spin | gone | Spurs not the blocker anymore |

**Primary next wall: 2nd movie / st620 arming** (same family as
`shaderwall-gated-on-2nd-movie.md`), **not** further readyCount tuning for this site.

Spurs is **refuted as the ongoing post-AUTO_LOAD FSM blocker** after the ready fix
(criterion 1a resolved; criterion 2 points at movie/FSM).

---

## Criterion 3–4

| Item | Status |
|------|--------|
| Natural AUTO_LOAD + GWI capture | PASS (`gwi_fsm_stall.log`) |
| One next gate | **2nd movie: why only one `StartSeq` / st620 stuck at 0 after thr_auto_load** |
| Menu / registry / pixels green | **not claimed** |

---

## Single next gate

Investigate **why the post-intro movie FSM never leaves st620=0** after AUTO_LOAD
(no 2nd `cellVdec` StartSeq). Discriminate: missing open/StartSeq arm for the
next title movie vs pad/UI gate vs other HLE after AUTO_LOAD.

Do **not** treat “raise readyCount” or forge OPD 534CB8 as the next step.

---

## Code / tests

| Piece | Path |
|-------|------|
| Wait analysis | this note + `gwi_wait_static.txt` |
| ready fix | `libs/spurs/cellSpurs.c` (`spurs_ready_consume`, ReadyCountStore no-PM clear) |
| Structural test | `{SCRATCH}/test_gwi_ready_wait.out` |
