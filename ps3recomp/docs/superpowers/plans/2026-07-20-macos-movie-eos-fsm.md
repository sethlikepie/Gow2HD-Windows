# macOS Movie EOS / Intro FSM — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

## Status 2026-07-21 — T0–T5 FEITOS; Task 7 RE FEITA; WALL WAD → plano satélite

| Item | Estado | Notas |
|------|--------|-------|
| Tasks 0–5 (sampler, vdec FORCE port, arm NATURAL, smoke, SIGBUS) | ✅ | ver marcas FEITO abaixo |
| Pré-condição open/poll FIOS (`st620` 1) | ✅ **fora deste plano** | plano 4.0 Task 8: sticky + play-already → **st620 1→3** sem forjar `f744` |
| Task 7 Step 3 (RE 3→0 / C3644) | ✅ **refutado o “consumidor não-FSM”** | `st620=0` = Stop + `'SMPD'`; ver follow-up C3644 abaixo |
| Opens `R_LglScA` / `R_PermA` | ⏳ **wall activo noutro plano** | [`2026-07-20-macos-post-st3-smpd-wad.md`](2026-07-20-macos-post-st3-smpd-wad.md) |

**Próxima implementação:** **não continuar Task 7 aqui** — executar o plano
[`2026-07-20-macos-post-st3-smpd-wad.md`](2026-07-20-macos-post-st3-smpd-wad.md)
(sonda SMPD → consumidor → open WAD). FORCE/vdec só na faixa G4 desse plano.

---

**Goal:** On macOS/arm64, advance the GoW2 intro movie player off `st620=1` so the documented FSM (`1→3→5→11→done`) and subsequent WAD opens (`R_LglScA`, `R_PermA`) happen by the same two HLE channels already proven on Windows — FORCE SEQDONE via `cellVdec`, and NATURAL EOS via `g_movie_eos_ea` — without forging guest progress and without regressing the Windows host.

> **Nota late:** `st620=1` já **não** é o park por open FIOS (4.0). O goal operacional passa a: a partir de **st620=3** honesto, produzir EOS/`f744` e avançar para WADs sem forja.

**Architecture:** The Windows host has two *independent* completion channels into the guest player. macOS now has the **arm path + time-based producer** (Task 3); FORCE/vdec still does not sit on the intro critical path (Open/StartSeq=0).

| Channel | Windows (proven) | macOS late (measured) |
|---------|------------------|------------------------|
| **FORCE** | `cellVdec` SEQDONE → guest OPD | Portado (Task 2) mas **fora do path** (Open/StartSeq=0 no intro) |
| **NATURAL** | overlay done → arm `g_movie_eos_ea` | Sampler+arm (Task 3) + produtor time-based; **depende** de host RAM/produtor; com open sticky chega-se a st620=3 **antes** do EOS |
| **Open/poll FIOS** | n/a (Windows já lia) | **4.0:** sticky + play-already → `002B4274` → st620 1→3 |

The injection point `g_movie_eos_ea` + `vm_read8` is portable. The remaining wall is **what happens after f744/EOS at st620=3** (idle vs WAD phase), not “missing FIOS open”.

**Tech Stack:** POSIX `pthread` / `popen` / `ffmpeg`/`ffprobe`, existing `movie_io_*` (POSIX, commit `0318caa`), portable `ppu_guest_range_committed`, guest OPD via `g_ps3_guest_caller`, env gates (`PS3_MOVIE_*`, `PS3_VDEC_*`).

## Global Constraints

- FORBIDDEN as fixes: CRC bypass, stubbing a combination, hand-stamping magic, cosmetic null-checks, masking a guard. Diagnostic tools at most, always env-gated.
- All probes env-gated, OFF by default, no-op on baseline.
- Prefer CALLING the guest's own code (OPD callbacks, existing FSM) over simulating state transitions.
- Faithful to CELL/PPC behaviour; never diverge from console *just* to make something work. Existing Windows HLE (EOS read-hook, FORCE SEQDONE) is already the accepted contract — port it, do not invent a third shortcut that writes `obj+0x744` or force-sets `st620`.
- Never forge results: label **in-boot** vs **offline/unit** vs **not-exercised**.
- Windows must not regress; shared runtime changes stay behind the same env gates.
- Never commit game data (`*.m2v`, `*.wav`, `movie_cache/*`, WADs, EBOOT).
- Commits in Portuguese, no co-authorship trailers. Explicit `git add` paths only.

---

## Answers the plan must lock in (a–e)

### (a) Honest way to produce EOS on macOS — ranked

| Rank | Option | Verdict | Why |
|------|--------|---------|-----|
| **1** | **(4) Better: dual-channel parity with Windows** | **Do this** | Unblocks WADs the way Windows already did: **FORCE first** (guest SEQDONE callback — prefers calling guest code), then **NATURAL** (arm existing `g_movie_eos_ea` from a real completion event). Shared armer in runtime so hosts do not diverge. |
| **2** | **(2) Port `cellVdec.c` (FORCE + minimal Open/StartSeq/callback)** | **Task 2 — first green path** | Measured today: `PS3_VDEC_FORCE_SEQDONE_MS` + `PS3_VDEC_ASYNC=1` change nothing because POSIX `cellVdec*` are no-ops (`cellVdec.c:569–579`). On Windows, FORCE alone already opens WADs (`bt_intro_wads.sh`). Note: this is **ffmpeg-backed HLE**, not “SPU MPEG DMAs `+0x744`” — do not claim SPU natural DMA until a later fidelity track. |
| **3** | **(3) macOS sampler + arm**, driven by a *completion event* | **Task 3 — required for NATURAL** | Arming is legitimate **only** after the same class of event Windows uses (`movie_hle_overlay_done()` / SEQDONE / headless pass-complete). A sampler that arms on `st620==1` alone is forging (see b). |
| **4** | **(1) Full ffmpeg + Metal/SDL overlay** | **Defer** | Needed for *pixels* of the logo, not for FSM. File header of `movie_hle.c` already separates halves: overlay is “its own piece of work”. A **headless pass-complete** (`g_movie_overlay_done` without presenting frames) is enough for NATURAL EOS and is a smaller slice of (1). |
| — | True SPU MPEG decoder path that DMAs `obj+0x744` | **Out of scope for this plan** | Ultimate fidelity; months; blocked on guest demux/AU path + SPU image correctness. Do not block WAD bring-up on it. |

**Working order:** T0 diagnostics → T1 observability (no arm) → **T2 FORCE/cellVdec** (first WAD-capable green) → T3 portable armer + headless done for NATURAL → T4 recipe/smoke parity → T5 SIGBUS (secondary) → T6 optional visual overlay.

### (b) Replay vs forge — the line, and the discriminating test

**Accepted replay (already in tree on Windows):**
- Guest *polls* `obj+0x744` via normal loads → `vm_read8` returns 1 **only after** a completion event armed `g_movie_eos_ea` to that EA.
- Guest *receives* `SEQDONE` on its real cellVdec callback OPD after FORCE timeout or real decode EOF.
- Both are env-gated (`PS3_MOVIE_EOS`, `PS3_VDEC_FORCE_SEQDONE_MS`) and documented as HLE of hardware-side end-of-stream.

**Forbidden forge:**
- Host `vm_base[obj+0x744] = 1` (or `st620 = 3`) without guest read/callback.
- Arming `g_movie_eos_ea` at fixed wall-clock after boot, or as soon as `st620==1`, with no completion producer.
- Arming when `movie_hle_overlay_done()` is still always-0 (today’s macOS trap if someone copies the sampler blindly).

**Discriminating test (must be automated as smoke, not vibes):**

```text
Matrix (same binary, 25s each, kill after):

  M0 baseline:  unset PS3_MOVIE_EOS PS3_VDEC_FORCE_SEQDONE_MS PS3_MOVIE_HLE
                → st620 may reach 1 or 3 via open/poll (4.0 sticky); MUST NOT see
                  [MOVIEEOS] arming / read-hook HIT; MUST NOT open R_* WADs from intro.

  M1 FORCE only: PS3_MOVIE_IO=1 PS3_MOVIE_CACHE=… PS3_VDEC_ASYNC=1
                 PS3_VDEC_FORCE_SEQDONE_MS=4000
                 unset PS3_MOVIE_EOS  (or EOS=0)
                → MUST log [cellVdec] FORCE SEQDONE… if vdec session exists;
                  st620 leaves park; WAD opens; arming may be absent (FORCE path).
                  (Today: Open/StartSeq=0 on intro → FORCE may still be cold.)

  M2 NATURAL:    PS3_MOVIE_IO=1 PS3_MOVIE_CACHE=… PS3_MOVIE_EOS=1
                 + headless/overlay done producer
                 unset PS3_VDEC_FORCE_SEQDONE_MS
                → MUST log overlay/done then [MOVIEEOS] arming then read-hook HIT;
                  f744 path exercised; prefer st620 3→5→11 (not 3→0 only).

  M3 FORGE-trap: PS3_MOVIE_EOS=1, no done producer, no FORCE
                → arming MUST NOT fire; f744 stays 0.
                  st620 may still be 1→3 via open (4.0) — that is NOT forge.
                  Forge = arm/HIT or f744 without producer, or host write of st620/f744.
```

If M3 arms or sets f744 without a producer, the change is illegal and must be reverted. If M1 works and M2/M3 do not, FORCE is honest and NATURAL is incomplete — not “fixed.”

### (c) VirtualQuery → portable commit check

Do **not** reintroduce VirtualQuery (or `mach_vm_region`) on the hot path.

| Windows sampler | Portable equivalent |
|-----------------|---------------------|
| `VirtualQuery(slot)` / `MEM_COMMIT` | `ppu_guest_range_committed(ea, n)` (`ppu_loader.cpp:424`, declared in `ppu_memory.h`) |
| Read BE word at `obj+off` | same BE unpack after commit check; or `vm_read32` / helpers once ranges are registered |

**Caveat:** `ppu_guest_range_committed` uses the **registered** range list (`ppu_register_committed_range`), not OS page tables. When `g_committed_count==0`, the guard is intentionally open (all “committed”). `boot_macos.cpp` currently `vm_commit`s regions but does **not** call `ppu_register_committed_range` (Windows `boot_main` does). Task 1 registers the same ranges so the sampler and the rest of the runtime share one truth.

`vm_uncommitted_guard` is internal to `vm_read*`/`vm_write*`; the sampler should call `ppu_guest_range_committed` only (read-only host-side peek of `vm_base`).

### (d) `.vpk` vs `.m2v` / `.wav` for smlogo_v2

**Possibly significant — must be measured before blaming EOS alone.**

- Play (`func_002C00DC`) formats `"/_movies/%s.vpk"` and `"/_movies/%s.wav"`.
- Cache has `SmLogo_v2.m2v` + `SmLogo_v2.wav`, **no** `.vpk` (other titles have `.m2v`+`.vpk`).
- `movie_io_resolve` (`movie_hle.c:463+`): path **with** extension tries basename as-is only; bare name tries `base`, `base.m2v`, `base.wad_ps3`. So:
  - `…/SmLogo_v2.vpk` → miss → guest FIOS/psarc (likely fail without dearch).
  - `…/SmLogo_v2` or `…/SmLogo_v2.m2v` → hit `.m2v` (Windows recipe logs this).
  - `…/SmLogo_v2.wav` → hit `.wav` if case-insensitive rescan works.

`st620: 0→1` proves the player **entered** state 1; it does **not** prove the elementary stream is being fed. A silent `.vpk` open fail can leave the player parked at 1 waiting for decode/EOS that never comes — **orthogonal** to missing arm, and easy to confuse with it.

Task 0 logs every `movie_io` / `cellFs` open for `smlogo`/`SmLogo`/`.vpk`/`.m2v`/`.wav` and classifies hit vs miss before any “EOS fix.”

### (e) SIGBUS / recursion cap — order

| Placement | Rationale |
|-----------|-----------|
| **After T1, parallel-safe with T2, must not block T0–T1** | Without FSM logs you cannot tell “parked at 1” from “died mid-run.” |
| **Before multi-minute WAD acceptance runs (T4)** | ~1/4 of 25s runs crash → flaky green is forged. |
| **Not the first “fix” for EOS** | Proven: main-thread stack overflow after recursion cap @ `0x0045CB90` depth=4001; 0 `[SPUCRASH]`; unrelated to EOS byte. |

Treat as **Task 5**: raise host stack and/or soft-fail the recursion without forging guest control flow; accept only if crash rate drops and T2 metrics still hold.

---

## File map

| File | Role |
|------|------|
| `libs/codec/cellVdec.c` | Port FORCE + minimal decode API off `_WIN32`; keep env gates identical |
| `libs/video/movie_hle.c` | Optional headless done producer on POSIX; keep overlay Windows-only until T6 |
| `libs/video/movie_eos_arm.c` (**new**) + `.h` | Portable sampler: `[MOVIEFSM]` + one-shot arm from `movie_hle_overlay_done()` |
| `runtime/ppu/tests/boot_main.cpp` | Call shared armer instead of inline lambda (no behaviour change) |
| `gow2-recomp/boot_macos.cpp` | Register committed ranges; start shared armer; no VirtualQuery |
| `libs/video/tests/test_movie_eos_policy.c` (**new**) | Unit: arm policy matrix M0/M3 offline |
| `gow2-recomp/smoke_intro_macos.sh` (**new**) | In-boot M1/M2 matrix |
| `gow2-recomp/env_gow2.sh` | Align with Windows recipe (`PS3_VDEC_ASYNC`, FORCE optional) |

---

### Task 0: Open-path inventory (`.vpk` / `.m2v` / `.wav`) — diagnostic only

> ✅ **FEITO** — inventario feito: 0 tentativas .vpk/_movies; so gow2.psarc

**Files:**
- Modify (probe only if missing): `libs/video/movie_hle.c` open log lines (already logs `[movieio] open`)
- Create: `gow2-recomp/smoke_movie_opens.sh`
- No behaviour change required if existing logs are enough

**Interfaces:**
- Consumes: `PS3_MOVIE_IO=1`, `PS3_MOVIE_CACHE`, existing `[movieio] open` / `[movieio] open … no cache`
- Produces: classified open table for the next tasks

- [x] **Step 1: Write the failing smoke (red = incomplete evidence)**

```bash
#!/usr/bin/env bash
# gow2-recomp/smoke_movie_opens.sh
set -euo pipefail
cd "$(dirname "$0")"
. ./env_gow2.sh
export PS3_MOVIE_IO=1 PS3_MOVIE_EOS=0
unset PS3_VDEC_FORCE_SEQDONE_MS || true
LOG=$(mktemp)
# 20s boot; kill; require at least one movie-related open line
timeout 20s ./build/gow2_boot ./EBOOT.ELF 2>"$LOG" || true
echo "=== movie opens ==="
grep -E '\[movieio\] open|cellFsOpen|smlogo|SmLogo|\.vpk|\.m2v|\.wav' "$LOG" || true
# RED until we can classify:
grep -qiE 'SmLogo|smlogo' "$LOG" || { echo "FAIL: no SmLogo open lines (cannot refute vpk miss)"; exit 1; }
```

- [x] **Step 2: Run once and fill the classification table (in-boot)**

| Guest path seen | movie_io result | Host file | Notes |
|-----------------|-----------------|-----------|-------|
| `…` | hit/miss | `…` | |

Expected hypotheses to confirm/refute:
1. Bare `SmLogo_v2` / `.m2v` hits cache → stream available; park is pure EOS/VDEC.
2. Only `.vpk` attempted and misses → extend resolve **only** if guest truly needs it (prefer serving real bytes: if `.vpk` is a container, document; do not invent a fake VPK). Smallest legal fix if open is required: map known bare/m2v alias already used on Windows — **not** invent container parse.

- [x] **Step 3: Acceptance**

- **In-boot:** table filled; decision recorded in the smoke script comment header (`# DECISION: …`).
- If `.m2v` already opens (Windows-like), **do not** change resolve in this plan; proceed to T1.
- If `.vpk` miss is proven to block state>1 independently of EOS, open a **follow-up task** (not a silent alias that returns wrong bytes).

- [x] **Step 4: Commit** (script + comment only)

```bash
git add gow2-recomp/smoke_movie_opens.sh   # or path under gow2_work / mirror
git commit -m "diag(macos): inventário de opens SmLogo vpk/m2v/wav no boot"
```

---

### Task 1: Portable movie-object sampler (observe only — no arm)

> ✅ **FEITO** — 6a4d9ad — sampler [MOVIEFSM]/[MOVIEOBJ], sem armar (eos_ea=0 em todas)

**Files:**
- Create: `libs/video/movie_eos_arm.h`, `libs/video/movie_eos_arm.c`
- Modify: `gow2-recomp/boot_macos.cpp` (start sampler; register committed ranges)
- Modify: `CMakeLists.txt` / gow2 link line to include `movie_eos_arm.c`
- Test: `gow2-recomp/smoke_moviefsm_mac.sh`

**Interfaces:**
- Consumes: `vm_base`, `ppu_guest_range_committed(uint32_t,uint32_t)`, singleton EA `0x540054`, fields `+0x620`, `+0x744`, `+0x746`
- Produces: `void movie_eos_sampler_start(void);` — starts pthread if `PS3_TRACE_MOVIEOBJ` **or** (later) `PS3_MOVIE_EOS`; **this task: arming code compiled out or hard-disabled**

- [x] **Step 1: Unit test for commit-safe BE load (offline)**

```c
/* libs/video/tests/test_movie_eos_policy.c — partial in T1 */
#include <assert.h>
#include <stdint.h>
/* Mock: when ppu_guest_range_committed returns 0, reader must not touch memory. */
/* RED: function movie_eos_peek_st620 does not exist yet. */
```

Minimal API to implement:

```c
/* movie_eos_arm.h */
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Starts background sampler if PS3_TRACE_MOVIEOBJ or PS3_MOVIE_EOS is set.
 * Arming of g_movie_eos_ea is enabled only when PS3_MOVIE_EOS is set AND a
 * completion producer has fired (see movie_hle_overlay_done). */
void movie_eos_sampler_start(void);
/* Test seam: returns 1 if (obj,st620) readable given current commit map. */
int  movie_eos_can_sample(uint32_t obj_ea);
#ifdef __cplusplus
}
#endif
```

- [x] **Step 2: Implement sampler body (POSIX + Win thread)**

Use:
- `ppu_guest_range_committed(0x540054, 4)` then BE load obj.
- `ppu_guest_range_committed(obj + 0x620, 4)` etc.
- Edge log identical to Windows:

```c
fprintf(stderr,
  "[MOVIEFSM] st620 %u -> %u  f744=%u f746=%u eos_ea=0x%08X overlay_done=%d\n",
  prev, st, f744, f746, g_movie_eos_ea, (int)(movie_hle_overlay_done() ? 1 : 0));
```

**Do not arm in this task** (even if `PS3_MOVIE_EOS=1`). Comment: `/* arm lands in Task 3 */`.

- [x] **Step 3: Register committed ranges in `boot_macos.cpp` after each `vm_commit`**

```cpp
extern "C" void ppu_register_committed_range(uint32_t lo, uint32_t hi);
// after vm_commit(0, GUEST_LOW_MB):
ppu_register_committed_range(0, GUEST_LOW_MB);
// after stack + RSX commits: register those spans too
```

- [x] **Step 4: Call `movie_eos_sampler_start()` before `ppu_run`**

- [x] **Step 5: Red → Green in-boot**

```bash
export PS3_TRACE_MOVIEOBJ=1
# 15s run
# RED before sampler: no [MOVIEFSM]
# GREEN: at least one [MOVIEFSM] st620 … and st620 reaches 1 (already measured)
grep -E '\[MOVIEFSM\]' "$LOG" | head
```

**Acceptance (in-boot):** `[MOVIEFSM]` lines appear; `st620 0 -> 1` (or first observed transition) without any `[MOVIEEOS] arming`; process may still park at 1.

- [x] **Step 6: Commit**

```bash
git commit -m "feat(movie): sampler portátil de st620 no macOS (sem arm de EOS)"
```

---

### Task 2: Port `cellVdec` FORCE path to POSIX (first unblocking channel)

> ✅ **FEITO** — 049fdf9 — portado, e PROVADO fora do caminho critico: cellVdecOpen/StartSeq/DecodeAu = 0

**Files:**
- Modify: `libs/codec/cellVdec.c` (collapse `#ifdef _WIN32` around FORCE + Open/StartSeq/EndSeq/callback; keep ffmpeg pipe portable with `popen`/`pclose`)
- Optional: thin `host_thread` helpers if CreateThread/`Interlocked*` need POSIX
- Test: unit for FORCE gate offline; in-boot `smoke_intro_macos.sh` matrix M1

**Interfaces:**
- Consumes: `g_ps3_guest_caller`, `movie_hle_cache_path()`, `ppu_giant_lock_*` if used on Win
- Produces: real `cellVdecOpen` / `StartSeq` / `FORCE SEQDONE` logs on macOS; guest SEQDONE callback invoked

- [x] **Step 1: Red — prove stubs**

```bash
# Current binary, FORCE recipe 15s:
export PS3_MOVIE_IO=1 PS3_MOVIE_CACHE=… PS3_VDEC_ASYNC=1
export PS3_VDEC_FORCE_SEQDONE_MS=4000
# Expected RED today:
#   grep FORCE SEQDONE → 0
#   st620 stays 1
```

- [x] **Step 2: Minimal port strategy (YAGNI)**

Port in this order inside `cellVdec.c`:

1. Replace `CreateThread` → `pthread_create` (detached).
2. Replace `Sleep` → `nanosleep` / `usleep`.
3. Replace `InterlockedCompareExchange` → `_Atomic` / `__sync_val_compare_and_swap`.
4. Replace `_popen`/`_pclose` → `popen`/`pclose` (already the Win names via compat if present; else ifdef).
5. Keep **FORCE watchdog** as a standalone thread started from `StartSeq` when `PS3_VDEC_FORCE_SEQDONE_MS>0` (same log string: `[cellVdec] FORCE SEQDONE watchdog after %d ms`).
6. Full async PICOUT pipe can land in the same PR if it compiles cleanly; if not, ship **FORCE-only first** with Open/StartSeq/Close returning CELL_OK and EndSeq/DecodeAu stubs that do not crash — **but** StartSeq must start the watchdog.

**Do not** claim natural SPU DMA. Log clearly this is HLE.

- [x] **Step 3: Offline unit (FORCE single-shot)**

```c
/* test_cellvdec_force_gate.c — optional small harness
 * setenv FORCE, fake slot, ensure second SEQDONE does not fire (seqDoneSent).
 */
```

- [x] **Step 4: In-boot green (matrix M1)**

```bash
export PS3_MOVIE_IO=1
export PS3_MOVIE_CACHE="$PWD/movie_cache"
export PS3_VDEC_ASYNC=1
export PS3_VDEC_FORCE_SEQDONE_MS=4000
# PS3_MOVIE_EOS intentionally off
unset PS3_MOVIE_EOS || true
# 30s
# GREEN:
#   grep -c 'FORCE SEQDONE' >= 1
#   grep '\[MOVIEFSM\]' shows st620 leaving 1 (e.g. 1->3 or higher)
#   grep -E "R_LglScA|R_PermA" movieio open >= 1 each (target; if SEQDONE fires but WAD still closed, document next wall — still a partial green)
```

**Acceptance:**
- **In-boot:** `[cellVdec] FORCE SEQDONE` appears; `st620` leaves 1 at least once within FORCE+margin.
- **Target (same as Windows `bt_intro_wads`):** `[movieio] open` for `R_LglScA` and `R_PermA`. If SEQDONE works but WADs do not, **stop and diagnose** (do not stamp EOS); open a follow-up — still count T2 as “FORCE channel live.”
- **Regression:** Windows build of `cellVdec.c` still links; string of FORCE log unchanged.

- [x] **Step 5: Commit**

```bash
git commit -m "feat(cellVdec): FORCE SEQDONE e StartSeq portáteis no POSIX"
```

---

### Task 3: Portable EOS arm (NATURAL channel) — policy + wire-up

> ⚠️ **IMPLEMENTADO E FORJA-SEGURO; precondicao bloqueada por ambiente** — 9504715.
> O arm do canal NATURAL esta feito, com a MESMA disciplina do boot_main.cpp:367
> (movie_eos_should_arm = eos_env && eos_ea==0 && overlay_done). M3 (falsificador)
> confirmado independente: produtor OFF -> 0 armado, st620 nao sai do maximo
> natural. Produtor: time-based (interval-replay) medindo os 14738 ms REAIS do
> .wav do proprio filme, rotulado no log como nao-EOF -- nao ha sinal
> consumido==total visivel no host (o .m2v e servido pelo dearchiver DENTRO do
> guest, nao passa pelo movie_io). ffmpeg/ffprobe ausentes neste host, por isso
> o produtor do plano nao serve.
>
> A cadeia produtor->arm->read-hook HIT->st620 sai de 3 foi DEMONSTRADA a
> disparar mais cedo na sessao (M=8, 6/6). Deixou de reproduzir por EXAUSTAO DE
> MEMORIA do host (8,5 GB em swap; a VM guest de 1,3 GB nao fica residente,
> throughput cai ~3x, st620 fica em 1 numa janela de 40s). Binario e lift
> identicos entre a corrida que funcionou e a que nao -- so o ambiente mudou.
> Para reproduzir: aliviar a pressao de memoria.
>
> Wall a JUSANTE, ja nomeado quando o arm disparou: st620 vai 3->0 (idle), NAO
> 3->5->11. O player trata obj+0x744=1 como "stream acabou, volta a idle" em vez
> de "avanca para a fase dos WADs". R_LglScA/R_PermA nunca abrem. Fora do ambito
> desta task.


**Files:**
- Modify: `libs/video/movie_eos_arm.c` (enable arm)
- Modify: `libs/video/movie_hle.c` POSIX half: headless done producer (see below)
- Modify: `runtime/ppu/tests/boot_main.cpp` — replace inline arm with `movie_eos_sampler_start()` (behaviour parity)
- Test: `libs/video/tests/test_movie_eos_policy.c` (M0/M3 offline) + in-boot M2/M3

**Interfaces:**
- Consumes: `movie_hle_overlay_done()`, `g_movie_eos_ea`, `PS3_MOVIE_EOS`
- Produces: one-shot arm log identical to Windows:

```text
[MOVIEEOS] overlay done (st620=%u) -> arming EOS read-hook at 0x%08X ([obj+0x744])
```

#### Headless done producer (slice of option 1, not full Metal overlay)

On POSIX, when `PS3_MOVIE_HLE=1` **or** a narrower gate `PS3_MOVIE_DONE=1` (prefer reusing `PS3_MOVIE_HLE` for recipe parity):

1. On `movie_io_open` of `.m2v` (already records `s_movie_cache_path`), spawn a thread that:
   - `ffprobe -v error -show_entries format=duration -of csv=p=0 "$path"` **or** decode with `ffmpeg -i … -f null -` and wait for exit;
   - set `g_movie_overlay_done = 1` and log `[movie] EOS flag set: g_movie_overlay_done=1` (same string as Windows).
2. **Do not** present frames (no Metal). Visual overlay is T6.
3. OFF when env unset — baseline no-op.

This is **replay of “stream finished”**, not forge: duration/EOF is measured from the real cached elementary stream the guest opened.

- [x] **Step 1: Offline policy tests (TDD)**

```c
/* test_movie_eos_policy.c */
/* Expose for test: movie_eos_should_arm(eos_env, already_armed, overlay_done) */

static int movie_eos_should_arm(int eos_env, uint32_t eos_ea, long overlay_done) {
    return eos_env && eos_ea == 0 && overlay_done != 0;
}

void test_m0_no_env(void) {
    assert(movie_eos_should_arm(0, 0, 1) == 0);
}
void test_m3_eos_without_done(void) {
    assert(movie_eos_should_arm(1, 0, 0) == 0);  /* FORGE-trap */
}
void test_natural_ready(void) {
    assert(movie_eos_should_arm(1, 0, 1) == 1);
}
void test_oneshot(void) {
    assert(movie_eos_should_arm(1, 0x1234, 1) == 0);
}
```

- [x] **Step 2: Red — run tests, fail until function exists and sampler uses it**

- [x] **Step 3: Implement arm in sampler (same condition as `boot_main.cpp:367–370`)**

```c
if (s_eos && !g_movie_eos_ea && movie_hle_overlay_done()) {
    g_movie_eos_ea = obj + 0x744;
    fprintf(stderr, "[MOVIEEOS] overlay done (st620=%u) -> arming EOS read-hook at 0x%08X ([obj+0x744])\n",
            st_arm, g_movie_eos_ea);
}
```

- [x] **Step 4: Headless done on POSIX**

Implement under `#!_WIN32` in `movie_hle.c` (or `movie_eos_arm.c` calling into cache path): real `movie_hle_from_guest` / start that only schedules done; leave D3D/Metal out.

- [x] **Step 5: In-boot matrix**

| Case | Expect |
|------|--------|
| M3 `PS3_MOVIE_EOS=1`, HLE/DONE off, no FORCE | no arming; st620 stays 1 |
| M2 `PS3_MOVIE_EOS=1` + HLE/DONE, no FORCE | `[movie] EOS flag set` → arming → `[MOVIEEOS] read-hook HIT` → st620 > 1 |
| M0 all off | no arming, no FORCE |

**Acceptance:**
- **Unit:** policy tests green.
- **In-boot M3:** zero arm lines (forge-trap held).
- **In-boot M2:** arm + read-hook HIT; st620 progresses without FORCE.
- **Windows:** `boot_main` uses shared sampler; `bt_visual_combo` / natural recipe still arms (no regression).

- [x] **Step 6: Commit**

```bash
git commit -m "feat(movie): arm portátil de EOS + done headless no POSIX"
```

---

### Task 4: Recipe + smoke parity (macOS ↔ Windows)

> ✅ **FEITO** — 8363f4c — smoke_intro_macos.sh (3 estados: PROVADO forja-segura / BLOQ-AMBIENTE / PENDENTE) + rodar_gow2_intro_skip.sh + PS3_VDEC_ASYNC exportado no env. A barra forja-segura (M3) e ambiente-independente e passa; o progresso da FSM fica gated pela RAM.

**Files:**
- Modify: `gow2-recomp/env_gow2.sh` — add `PS3_VDEC_ASYNC=1` default when using intro path; document FORCE as optional skip
- Create: `gow2-recomp/smoke_intro_macos.sh` (mirror metrics of `recomp_mid_v2/bt_intro_wads.sh` + `bt_visual_combo.sh`)
- Create: `gow2-recomp/rodar_gow2_intro_skip.sh` if missing (Plan 04 Task 3)

- [x] **Step 1: Red — smoke exists and fails on current metrics until T2/T3 land**

```bash
#!/usr/bin/env bash
# smoke_intro_macos.sh — dual path
set -euo pipefail
. ./env_gow2.sh
export PS3_MOVIE_IO=1 PS3_MOVIE_CACHE="${PS3_MOVIE_CACHE}"
export PS3_VDEC_ASYNC=1

run_case() {
  local name=$1; shift
  local log="smoke_${name}.log"
  # "$@" are env assignments
  env "$@" timeout 45s ./build/gow2_boot ./EBOOT.ELF >"$log" 2>&1 || true
  echo "== $name =="
  echo "force=$(grep -ac 'FORCE SEQDONE' "$log" || true)"
  echo "arm=$(grep -ac 'arming EOS read-hook' "$log" || true)"
  echo "hit=$(grep -ac 'read-hook HIT' "$log" || true)"
  echo "fsm=$(grep -ac '\[MOVIEFSM\]' "$log" || true)"
  echo "lgl=$(grep -ac "R_LglScA" "$log" || true)"
  echo "perm=$(grep -ac "R_PermA" "$log" || true)"
}

run_case force PS3_VDEC_FORCE_SEQDONE_MS=4000 PS3_MOVIE_EOS=0
run_case natural PS3_MOVIE_EOS=1 PS3_MOVIE_HLE=1
# assert force>=1 && (lgl|perm) ; natural arm>=1 …
```

- [x] **Step 2: Green when T2+T3 acceptance holds**

**Acceptance (in-boot):**
- FORCE case: `force>=1`, `st620` left 1, WAD opens (target).
- NATURAL case: `arm>=1`, `hit>=1`, no FORCE lines required, WAD opens within SmLogo duration + 30s margin.
- Baseline case in CI/smoke: no arm when EOS off.

- [x] **Step 3: Commit**

```bash
git commit -m "test(macos): smoke intro FORCE/NATURAL alinhado ao Windows"
```

---

### Task 5: SIGBUS / host stack after recursion cap (secondary)

> ✅ **FEITO (refutacao + diagnostico)** — d0b1ad7 (sonda PS3_TRACE_HOST_STACK) + 0cc7c7e.
> O SIGBUS ja NAO reproduz: 0 em 36+ corridas no HEAD (confirmado 3/3 independente).
> O fix do bctr (73d3ee0) ja o tinha resolvido -- 0x0045CB90 era a mesma classe
> `bctr`-como-chamada que 0x002C07D8. A teoria do stack overflow e MEDIVELMENTE
> falsa: no cap de 4000, 1.96MB usados de 32MB, ~514 B/nivel. O crash real era o
> cap a devolver r3=0x84010002 (EA no buraco 0x51M-0xD0M) que o chamador
> desreferenciava. Nao se subiu a stack (seria fix sobre diagnostico errado).
> Achado latente registado: threads PPU criadas tem stack de 512KB; a 514 B/nivel
> uma recursao estoura ~1000, antes do cap de 4000 disparar -- o cap devia ser
> sensivel a headroom, nao a profundidade fixa. Cross-platform, fora do ambito.


**Files:**
- Modify: `gow2-recomp` link flags (`-Wl,-stack_size,0x2000000` already 32MB — measure actual; consider 64MB **or** reduce recursion depth / convert deep mutual recursion at `0x0045CB90` to trampoline loop if lifter pattern)
- Modify: `runtime/ppu/ppu_loader.cpp:1319` recursion cap — ensure skip is safe (already skips); confirm it is not the root of stack burn **after** skip
- Probe: env-gated stack high-water log (`PS3_TRACE_HOST_STACK=1`) OFF by default

- [ ] **Step 1: Red — reproduce crash rate**

```bash
# 8× 25s runs; count SIGBUS / exit codes; confirm 0 [SPUCRASH]
# Document rate (~1/4)
```

- [ ] **Step 2: Discriminating experiments (one at a time)**

1. Double host stack → rate down?  
2. Lower recursion cap + always-skip → rate down but guest progress?  
3. If progress regresses, prefer stack increase over forging control flow.

- [ ] **Step 3: Green**

**Acceptance:** crash rate ≤ 1/20 on 25s intro recipe; T2/T4 metrics still pass; no SPU job required to explain.

- [ ] **Step 4: Commit**

```bash
git commit -m "fix(ppu): mitiga overflow de stack do host pós recursion cap"
```

---

### Task 6 (optional, later): Visual ffmpeg overlay on Metal/SDL

> ⬜ **OPCIONAL, ADIADA** — overlay visual ffmpeg/Metal: so pixels do logo, nao move a FSM, e o ffmpeg nem esta instalado neste host. Fase de A/V, fora do bring-up.

**Out of critical path for FSM.** Only after T4 green.

- Port frame path from `movie_hle.c` Win half to present via existing `PS3_RSX_BACKEND=metal|sdl`.
- Keep done-flag producer shared with T3 (do not duplicate EOS logic).
- Acceptance: visible logo **and** NATURAL metrics unchanged.

---

### Task 7: Pós-st620=3 — f744 / 3→5→11 / WAD (wall a jusante da Task 3)

> ⬜ **ABERTA (2026-07-20 late)** — com open/poll (4.0) a intro chega a **st620=3** e
> `f744=0` sem forja. O arm EOS (Task 3) já foi implementado e, quando disparou,
> observou-se **3→0 idle** em vez de **3→5→11**; `R_LglScA`/`R_PermA` não abriram.
> Esta task fecha o gap entre “estamos em 3” e “WADs da intro abrem”.

**Pré-condições:**
- 4.0: sticky + play-already no lift; `002B4274` / `st620≥3` em smoke curto.
- M3: `PS3_MOVIE_EOS=1` sem produtor → zero `[MOVIEEOS] arming`, `f744=0`.

**Files (esperados — confirmar na execução):**
- Read: `movie_eos_arm.c`, `movie_hle.c` (produtor time-based / overlay_done)
- Read: lift `func_002C07D8` / ramos de estado 3 (porque 3→0)
- Smoke: `smoke_intro_macos.sh` / `oracle_intro_checklist.sh` + contagens WAD

**Passos:**

- [ ] **Step 1: Baseline pós-4.0 (EOS OFF)**  
  N=4, 30s: `st620_max≥3` ≥3/4; `DONE4274≥1` ≥3/4; `f744` max=0; zero arm.

- [ ] **Step 2: NATURAL arm com open estável**  
  `PS3_MOVIE_EOS=1` + produtor done (time-based ou HLE). Esperado: arm + HIT; log de `f744`/read-hook.  
  Classificar: **A** 3→5→11 · **B** 3→0 · **C** fica em 3 · **D** sem arm (produtor frio).

- [ ] **Step 3: Se classe B (3→0)** — RE estática do ramo idle vs fase WAD  
  Documentar condição de guest (flags além de `+0x744`?). **Proibido** forjar `st620=5`.

- [ ] **Step 4: FORCE re-eval**  
  Se vdec continuar Open=0, documentar “FORCE cold on intro”; não contar como regressão do sticky.

- [ ] **Step 5: Aceite**  
  **in-boot:** ≥1 open de `R_LglScA` ou `R_PermA` **ou** reclassificação escrita com stack guest.  
  M3 continua verde.

```bash
# Esboço de smoke (a integrar)
grep -E 'R_LglScA|R_PermA|STICKY-RESTORE|002B4274 DONE|st620 .*->' /tmp/intro.log
```

---

## Ordered execution summary

| # | Task | Estado | Red | Green / acceptance |
|---|------|--------|-----|--------------------|
| 0 | SmLogo open inventory | ✅ | no classified opens | in-boot table; vpk risk decided |
| 1 | Sampler + commit ranges | ✅ | no `[MOVIEFSM]` | `[MOVIEFSM]` park/progress; **no arm** until T3 |
| 2 | `cellVdec` FORCE POSIX | ✅ (cold on intro) | FORCE log = 0 | portado; Open/StartSeq=0 no path critico |
| 3 | Portable arm + headless done | ✅ arm; wall 3→0 | M3 would arm if forged | M3 no arm; arm+HIT quando produtor vivo |
| 4 | Smokes/recipes | ✅ | smoke fails | M3 ambiente-independente; FSM gated |
| 5 | SIGBUS stack | ✅ refutado | ~1/4 crash | 0 repro; bctr-tail |
| 6 | Visual overlay | ⬜ opcional | n/a | pixels later |
| **7** | **pós-st620=3 → WAD** | ⬜ **activo** | st620=3 f744=0 | 3→5→11 e/ou open R_* / reclass |

---

## Self-review checklist

| Spec question | Task |
|---------------|------|
| (a) ranking | Answers section + order T2 before full (1) |
| (b) forge line + test | Answers (b) + T3 unit M3 + T4 matrix + **st620 1→3 via 4.0 is not forge** |
| (c) VirtualQuery portable | Answers (c) + T1 `ppu_guest_range_committed` + range register |
| (d) vpk asymmetry | Task 0 first |
| (e) SIGBUS order | Task 5 after observability; not before T2 |
| Windows non-regression | Shared armer; same env names; Win boot_main delegates |
| No stamp magic | Explicit ban on host write of `+0x744` / `st620` |
| TDD red/green | Each task has red command and green acceptance |
| Open/poll FIOS | **Plano 4.0 Task 8** (sticky) — precondição, não reabrir aqui |

**Not in this plan (follow-ups):** true SPU MPEG DMA of EOS; full Metal overlay (T6); post-WAD shader registry (Plan 05); caça ao clear fantasma de `[op+0x90]` (sticky contorna).

---

### Task 7 Step 3 — achado estático (2026-07-20, RE cirurgica)

RE feita a `sed -n` (fatias), sem runtime, com o host recuperado a load ~3
(depois de matar 10 boots orfaos a 90% CPU que tinham posto o load a 75 -- ver
a correccao no `smoke_intro_macos.sh`).

**A FSM avanca em CASCATA, 2 estados de cada vez, enquanto `obj+0x744` for 1.**
Cada gate re-le o EOS e salta por trampolim NO MESMO tick:

```
func_002C07D8:  le [+0x744]; ==0 -> func_002C05C8 (poll do open, func_002B4224)
                             !=0 -> st620=2; st620=3; -> func_002C05F8
func_002C05F8:  le [+0x744]; ==0 -> func_002C0688 (le [+0x746], 2o flag)
                             !=0 -> st620=4; st620=5; -> func_002C060C
```

Ou seja, com o hook a devolver 1 para `[+0x744]`, `func_002C05F8` **escreve
st620=4 e depois 5** e segue -- a transicao 3->5 acontece sozinha. Um segundo
flag (`[+0x746]`) so e lido no ramo `[+0x744]==0` (`func_002C0688`), que o hook
impede de ser alcancado.

**Consequencia: o `3->0` observado NAO e este handler a recusar avancar** -- se
fosse, ia para 5, nao para 0. E um **reset concorrente**. O suspeito e o
**double-Play** ja documentado: o `Play` corre 2x, e a 2a entrada
(`func_002BFF88` -> `func_002B3F78`) faz teardown do container e zera o st620. O
`patch_fios_play_already_active.py` trava o re-open mas pode nao travar o
teardown/reset.

**Refuta a hipotese do plano 07-14** ("gate do estado 3 em `func_0045B2A8`"): no
lift macOS a cascata nao precisa de um 2o flag enquanto `[+0x744]` se mantem 1;
precisa e' de CORRER sem o Play#2 a resetar antes.

**Proximo experimento (agora viavel, host a load 3):** instrumentar a entrada de
`func_002C05F8` (foi alcancada com `[+0x744]==1`?) vs o teardown de
`func_002B3F78` (zerou o st620 antes?). Isso discrimina "a cascata nunca corre
porque o Play#2 resetou primeiro" de "a cascata corre mas leu 0 no momento
errado". **Proibido** forjar st620=5.

#### Correcção ao Step 3 (mesma sessão, host recuperado a load ~2)

As duas hipóteses acima caem por medição. Jump table do pump resolvida por lldb
(`[TOC-0x10F4]`=`[0x540084]`=`0x2C0598`; handler = base+offset):

| st620 | offset | handler |
|---|---|---|
| 1 | 0x240 | `func_002C07D8` |
| **3** | **0x60** | **`func_002C05F8`** |
| 5 | 0x1C8 | `func_002C0760` |

`func_002C05F8` (o handler do estado 3, confirmado) faz: `[+0x744]==1` →
`st620=4; st620=5` → avança. `[+0x744]==0` → `func_002C0688`.

**Corrida armada (load 2, sã):** `st620` faz `0→1→3→3→0` e **nunca toca 4/5**.
Logo `func_002C05F8` **não tomou o ramo de avançar** — leu `[+0x744]` como 0,
apesar de `g_movie_eos_ea` estar armado em `0x869F1C` (=obj+0x744) e o arm ter
disparado ANTES do `3→0` (linha 22666 vs 24943).

- **Double-Play refutado:** o `Play#2` sai cedo (patch play-already) e o `st620`
  fica 1→3 depois dele; não houve teardown/reset.
- **Cascata avança-e-reseta refutado:** se avançasse, `st620` tocava 4/5; não tocou.

**Facto exacto:** o read-hook (`vm_read8` devolve 1 para `g_movie_eos_ea`) **não
está a alcançar o read de `func_002C05F8`**. O guest lê `vm_read8(gpr[30]+0x744)`
com `gpr[30]=obj`, que dá exactamente `0x869F1C` — devia bater. Candidatos:
(a) o read do estado 3 passa por um caminho que não o `vm_read8` unitário (bulk/
cache); (b) o game reescreve 0 no byte real entre o arm e este read e o hook,
por algum motivo, não intercepta este site; (c) `func_002C05F8` corre numa
thread cujo `obj` difere.

**Próxima sonda (única, discriminadora):** instrumentar o ramo de `func_002C05F8`
— logar o valor que ele lê em `[+0x744]` e qual ramo toma. Um `patch_*.py` de 1
sítio. Se ler 0 com o hook armado, o problema é o hook não cobrir este read (a),
e o fix é fazer o arm ESCREVER o byte real (ainda replay, o EOS é real) em vez de
só interceptar leituras — ou cobrir o site. **Proibido** forjar `st620`.

#### Step 3 RESOLVIDO — o read-hook funciona; o `+0x744` e um byte de "done" PARTILHADO

Sonda `patch_st3_probe.py` (`PS3_TRACE_ST3`, OFF por default) no read de
`func_002C05F8`. Corrida armada, host sao (load ~2):

1. **O read-hook funciona.** Antes do arme, `func_002C05F8` le `[+0x744]=0`
   (correcto). Depois do arme: `[MOVIEEOS] read-hook HIT [0x869F1C]->1`. Confirmado
   que o `vm_read8` do lift e o mesmo simbolo com o hook, e que ha UM so
   `g_movie_eos_ea` (extern partilhado, `nm`=1). Sem bug de linkagem.

2. **Mas `func_002C05F8` NAO volta a correr depois do arme** (0 linhas `[ST3]`
   pos-arme). O `st620` sai de 3 ANTES de o pump despachar o estado 3 com o byte
   ja a 1. Por isso nunca toca 4/5.

3. **O primeiro reader do `+0x744` armado nao e a FSM** -- e `func_000C3644`
   (chunk 004, subsistema de movie, longe do `0x2C0xxx`), mapeado do `ra=` do
   read-hook HIT. Le o byte por EA computado (nao literal). Ou seja `+0x744` e um
   byte de "filme acabou" PARTILHADO por varios subsistemas.

**Diagnostico:** o read-hook e cego -- faz TODOS os leitores de `0x869F1C`
verem 1 no mesmo instante. Um consumidor nao-FSM (`func_000C3644`) reage ao
"done" e leva o player a idle (`st620->0`), ganhando a corrida contra o avanco
`3->5` da FSM. No Windows o FORCE SEQDONE conduz o callback do `cellVdec`, que
sequencia isto na ordem certa; no Mac o `cellVdec` nem abre (Open/StartSeq=0),
e o read-hook global dispara o caminho de idle primeiro.

**Pergunta em aberto (honesta):** o `st620->0` e' mesmo um erro, ou e' o jogo a
devolver o player de intro a idle enquanto a carga dos WADs e' disparada por
OUTRO subsistema? A premissa "FSM 3->5->11 e' o unico caminho para os WADs"
pode estar errada. Confirmar exige ler `func_000C3644` (o que faz com o "done")
e comparar com a sequencia do FORCE no Windows.

**Fix candidato (NAO feito -- decisao pendente, toca a fronteira da forja):** em
vez do read-hook cego, entregar o "done" so ao consumidor certo, ou reabrir o
canal `cellVdec` FORCE para sequenciar como no Windows. Escrever `+0x744=1` na
memoria real (em vez de interceptar) so pioraria -- os multiplos consumidores
continuariam a competir. **Proibido** forjar `st620`.

---

#### Task 7 Step 3 / follow-up C3644 — RE ESTATICA: o diagnostico acima esta REFUTADO

Leitura estatica de `func_000C3644` e de todos os leitores de `+0x744`
(`gow2-recomp/recomp_macos_v2/`). Diagnostico puro, sem alterar runtime nem lift.

**1. `func_000C3644` NAO pode ser o leitor do HIT. Refutado por construcao.**

`ppu_recomp_004.cpp:156798-156820` — o corpo INTEIRO (22 linhas):

```c
void func_000C3644(ppu_context* ctx) {
        ctx->gpr[9]  = vm_read32(ctx->gpr[2] + -0x5FA4);
        ...
        { uint32_t tmp = vm_read32(ctx->gpr[11] + 0x14); float ftmp; memcpy(&ftmp,&tmp,4); ctx->fpr[13] = ftmp; }
        ctx->fpr[12] = ctx->fpr[13] * ctx->fpr[0];
        ...
        ctx->fpr[0]  = ctx->fpr[12] * ctx->fpr[0] + ctx->fpr[13];
        { float ftmp = (float)ctx->fpr[0]; uint32_t tmp; memcpy(&tmp,&ftmp,4); vm_write32(ctx->gpr[29] + 0x0, tmp); }
        ctx->gpr[29] = vm_read64(ctx->gpr[1] + -0x18);   /* epilogo red-zone */
        ...
}
```

- **Zero `vm_read8`.** O read-hook vive DENTRO de `vm_read8` (`ppu_loader.cpp:723-726`);
  uma funcao sem `vm_read8` nao pode disparar o HIT — nem por EA literal nem computado.
  So tem `vm_read32`/`vm_read64` (loads float/spill).
- **Zero chamadas, zero trampolins.** Nao chama teardown, nao chama Play/cancel,
  nao abre WAD nem FIOS. Unica escrita: `vm_write32(r29+0)`, um store **float**.
- **Zero referencias a `0x620`/`0x744`.**
- Nao e "subsistema de movie": e um bloco de interpolacao/blend de curva,
  alcancado por trampolim de `func_000C35D8` (`ppu_recomp_003.cpp:300424-300438`),
  que despacha em `r8 ∈ {2,3,...}` e faz `*dst = *dst + a*b*c`. Avaliador de keyframes.

A atribuicao veio do `ra=` do host — exactamente a armadilha que o `CLAUDE.md` do
ps3recomp ja documenta: *"`ra` do host stack e retorno de trampolim (varia entre
runs) — rastrear callers por fluxo estatico + `nm`, nao pelo stack."* Layout no
`.o`: `func_000C35C8 @0x41953c` → `func_000C3644 @0x4195a0` (adjacentes; um `ra`
off-by-one ou simbolizado contra enderecos pre-link cai aqui trivialmente).

**2. NAO existe consumidor nao-FSM de `+0x744`. Os 10 leitores sao TODOS a FSM.**

`grep "vm_read8(ctx->gpr[N] + 0x744)"` no lift inteiro — 10 sitios, todos `gpr[30]`:

| ficheiro:linha | funcao | estado |
|---|---|---|
| `ppu_recomp_002.cpp:224703` | `func_002C0570` | caminho de open/poll FIOS |
| `ppu_recomp_002.cpp:224755` | `func_002C05C8` | caminho de open/poll FIOS |
| `ppu_recomp_001.cpp:36550` | `func_002C0508` | idem (copia inline) |
| `ppu_recomp_001.cpp:36596` | `func_002C05F8` | **st=3** (o unico com sonda `[ST3]`) |
| `ppu_recomp_001.cpp:36652` | `func_002C069C` | **st=4** |
| `ppu_recomp_001.cpp:36736` | `func_002C0788` | **st=10** |
| `ppu_recomp_001.cpp:36761` | `func_002C07D8` | **st=1** |
| `ppu_recomp_001.cpp:36792` | `func_002C07F8` | **st=11** (loop de playback) |
| `ppu_recomp_005.cpp:54731` | `func_002C0804` | bloco de `st=11` |
| `ppu_recomp_021.cpp:538095` | `func_002C082C` | bloco de `st=11` |

Nenhum fora de `0x2C0xxx`. A premissa "byte de done PARTILHADO por varios
subsistemas / consumidor nao-FSM leva o player a idle" **cai**.

**3. A evidencia "nunca toca 4/5" nao tem poder discriminador.** `func_002C05F8`
(`ppu_recomp_001.cpp:36606-36609`) escreve 4 e 5 em **stores consecutivos**, sem
nada pelo meio:

```c
ctx->gpr[0] = 4; vm_write32(ctx->gpr[30] + 0x620, ctx->gpr[0]);
ctx->gpr[0] = 5; vm_write32(ctx->gpr[30] + 0x620, ctx->gpr[0]);
```

E os handlers 5→6→7→8→9→10 (`func_002C0760`/`0768`/`0770`/`0778`/`0780`,
linhas 36705-36733) sao pares `li`+`stw` puros encadeados por fallthrough. O
amostrador corre a **200 ms** (`MOVIE_TICK_MS`, `movie_eos_arm.c:87`). Os estados
4..10 sao transientes de nanossegundos: **um avanco 100% bem-sucedido produz
exactamente o mesmo log `3 → 0`**. A ausencia de 4/5 e esperada nos dois casos.

**4. E "0 linhas `[ST3]` pos-arme" tambem nao prova que a FSM parou.** A sonda
`patch_st3_probe.py` esta **so** em `func_002C05F8`. Mas o mesmo gate `+0x744`
esta replicado inline noutros 3 sitios que avancam sozinhos — p.ex.
`func_002C05C8` (`ppu_recomp_002.cpp:224750-224761`):

```c
[r30+0x620] = 2;  [r30+0x620] = 3;
r0 = vm_read8(r30 + 0x744);          /* <-- NAO SONDADO */
if (r0 == 0) -> func_002C0688;
[r30+0x620] = 4;  [r30+0x620] = 5;
```

E `func_002C07D8` (st=1, `36761-36768`): se `[+0x744] != 0`, escreve 2, 3 e salta
para `func_002C05F8`; se `== 0`, vai para `func_002C05C8`. Ou seja, com o hook
armado ha caminhos que fazem 3→4→5 **sem passar pelo sitio sondado**. Zero linhas
`[ST3]` e compativel com avanco bem-sucedido.

**5. O `st620 = 0` nao vem do pump — vem de um `Stop()` externo, que BROADCASTA.**
Os unicos writes de 0 em `+0x620` estao em `func_002BFF88` / `func_002BFFE0` /
`func_002BFFF4` / `func_002C0048` (`ppu_recomp_001.cpp:36203`,
`ppu_recomp_002.cpp:224594/224637/224657`), sempre com a mesma cauda:

```c
func_00445C48(...); [r31+0x720] = 0;        /* fecha handles */
ctx->gpr[3] = 0x534D << 16 | 0x5044;        /* 'SMPD' */
ctx->gpr[4] = 0x1C;  r5..r9 = 0;
vm_write32(ctx->gpr[31] + 0x620, ctx->gpr[0]);   /* st620 = 0 */
func_0043FF30(ctx);                          /* broadcast 'SMPD'/0x1C */
```

`func_002BFF88` e chamada de **13 sitios de jogo** (chunks 000/001/005/013/021),
nenhum dentro do pump `0x2C0xxx`. E teardown/`Stop()` do player, e a ultima coisa
que faz e **anunciar** a conclusao. O pump, ao chegar a `st=0xB`
(`func_002C0788:36748`), so retorna (`func_002C0620` = epilogo).

**Conclusao — classificacao: (A) REFUTADA; (B) fortemente suportada, falta 1 sonda.**

Nao ha "consumidor nao-FSM que ganha a corrida e mata o path dos WADs": esse
consumidor nao existe no lift. O `3 → 0` observado e compativel — e provavelmente
identico — a: hook armado → gate `+0x744` passa num dos sitios inline → FSM corre
4..0xB em microsegundos (invisivel a 200 ms) → jogo chama `Stop()` → `st620=0` +
broadcast `'SMPD'`. Isto e a **conclusao documentada**, nao um idle parasita.

Corolario: o fix candidato do bloco anterior ("entregar o done so ao consumidor
certo") atacaria um problema inexistente. A parede real, a confirmar-se B, esta
**a jusante do `'SMPD'`** — quem devia reagir ao broadcast e abrir `R_LglScA`/
`R_PermA`. A comparacao com o Windows FORCE reforca: la o `cellVdec` SEQDONE
sequencia os callbacks e os WADs saem; aqui ha que ver se o `'SMPD'` chega a
sair e quem o consome.

**Sonda unica e discriminadora (env-gated, OFF por default, 1 `patch_*.py`):**
estender o padrao do `patch_st3_probe.py` a **4 sitios**, todos so leitura/print:

1. os 3 gates `+0x744` inline nao sondados — `func_002C0508:36550`,
   `func_002C0570:224703`, `func_002C05C8:224755` — logar valor lido + ramo;
2. uma linha em `func_002BFF88` (`ppu_recomp_001.cpp:36203`) antes do
   `st620 = 0`, logando `Stop()` + o broadcast `'SMPD'` que se segue.

Leitura do resultado:
- gate inline le **1** e `[STOP]` aparece a seguir → **B confirmado**: a FSM
  completou, o filme acabou, e a investigacao muda para o consumidor de `'SMPD'`
  (`func_0043FF30`) e para porque nao dispara os WADs.
- nenhum gate le 1 e o `[STOP]` vem de outro sitio → a FSM nao avancou e volta-se
  a hipotese de cobertura do hook, agora com o caller real identificado.

**Proibido** (inalterado): forjar `st620`, escrever `+0x744=1` na memoria real,
`Sleep(1)` no poll.

#### CORRECÇÃO ao Step 3 (o meu commit c37e708/8bd5700 estava errado)

RE estática independente (nota `gow2-recomp/notes/2026-07-20-st620-3to0-static.md`,
`5d5dece`) refutou a minha conclusão, e **verifiquei as duas afirmações por
leitura directa antes de a aceitar**:

1. **`func_000C3644` NÃO lê `obj+0x744`** — a função faz float math; o único
   `0x744` nela é uma escrita de stack. O `ra=` que eu mapeei do read-hook HIT
   era artefacto de endereço de retorno, não o reader. **`obj+0x744` é lido SÓ
   pela FSM (9 readers).** A minha história do "byte partilhado / read-hook cego"
   cai.

2. **O gate real é `func_0045B2A8`** (`ppu_recomp_001.cpp:425951`): resolve a
   sessão (`func_004479E8`, de `obj+0x720`) e devolve `vm_read32(sessao+0x1B8)`
   ou `-1`. É o **status da sessão de decode** — exactamente o "state-3 gate" que
   o plano 07-14 previa e que eu tinha descartado por engano. No ramo
   `+0x744==0` de `func_002C05F8`, é `func_002C0FA0` (`ppu_recomp_002.cpp:224850`)
   que chama `func_0045B2A8(obj+0x720)`: `!=0` avança, `==0`/-1 fica parado.

3. **O `3→0` é um Stop, não a FSM.** `st620=0` é escrito **só** por MovieStop
   (`func_002BFF88`, `ppu_recomp_001.cpp:36141`) e teardowns, chamável de código
   de jogo de nível superior em qualquer estado 1..10. Nenhum handler da FSM
   escreve 0. Bate com o observável "o `st620` sai de 3 antes de o handler
   re-correr" -- o MovieStop ganha a corrida.

**Conclusão corrigida:** a precondição em falta é o **status da sessão vdec**
`*(sessão+0x1B8)`, NÃO o `+0x744`. No Mac o `cellVdec` nem abre
(Open/StartSeq=0, Task 2), logo `func_0045B2A8` devolve 0/-1 e a FSM fica em 3
até o MovieStop a zerar. **Isto reabre o canal FORCE/cellVdec como o caminho
certo** -- que eu tinha parqueado como "fora do caminho crítico". Estava errado.

**Próxima sonda (diagnóstico, do agente):** instrumentar `func_0045B2A8` (logar o
handle `obj+0x720` e o `+0x1B8` devolvido) + trap no MovieStop `func_002BFF88`
(logar o `st620` no Stop). Discrimina "sessão nunca existiu → fix é o canal
FORCE/vdec" de "o Stop corre com o avanço".
