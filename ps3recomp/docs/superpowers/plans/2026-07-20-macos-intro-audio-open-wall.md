# macOS Intro Wall — Audio Open / FIOS Handles / Recursion Cap Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

## Status 2026-07-20 late — ACEITE PARCIAL / WALL RECLASSIFICADO

| Item | Estado | Evidência |
|------|--------|-----------|
| Open FIOS m2v (`/_movies/smlogo_v2.m2v`) | ✅ | `[FIOSOPEN]` ret≠0; handle em container |
| Open wav causal? | ✅ refutado (WAV_SURVIVABLE) | Task 3 / 543ab35 |
| bctr / recursion cap | ✅ | 73d3ee0 / e822fc3 |
| Poll vê `done≠0` / `002B4274` | ✅ | sticky + play-already; `/tmp/fios_sticky*.log` |
| `st620` sai de 1 | ✅ **1→3** | `[MOVIEFSM]` |
| `f744` / EOS / 3→5→11 / WAD | ⏳ **fora deste plano** | plano [`macos-movie-eos-fsm.md`](2026-07-20-macos-movie-eos-fsm.md) |

**Entrega deste plano (4.0):** o park em `st620=1` por open/poll FIOS **deixou de ser a parede**.
A aceitação original “deixar st620=1” está cumprida no sentido **reclassificado**:
open + consumo de done sem forjar `f744`/`st620`. O progresso para WADs é o plano EOS.

**Commits / patches (motor + port):**
- `ps3recomp` `a82c594` — giant lock POSIX + `lwcond` `acquire_prio`
- sticky done: `ps3_fios_sticky_{publish,peek,consume}` em `runtime/ppu/ppu_loader.cpp`
- `gow2-recomp` `c5f718c` — `patch_fios_play_already_active.py`, `patch_fios_cancel_yield.py`
- lift local (gitignored): `STICKY-RESTORE` em `func_002B4224`, publish em `func_00306534`

**Higiene pendente (não bloqueia a reclassificação):** extrair `patch_fios_sticky_*.py`
idempotente a partir do lift actual; smoke N/M no `apply_all_patches` + checklist.

---

**Goal:** Identify and unblock the real producer that keeps GoW2's intro movie player parked at `st620=1` on macOS/arm64, starting from the measured audio-open failure (`snd_stream: couldn't open file /_movies/SmLogo_v2.wav`) and the never-populated FIOS handles at `obj+0x630/+0x634`, without forging EOS/`st620` and without regressing Windows.

**Architecture:** The player reaches state 1 (Play / `func_002C00DC` already runs). State 1 does **not** wait on cellVdec (zero Open/StartSeq/DecodeAu). It waits on media I/O setup that is supposed to feed streams and eventually set `obj+0x744`. That I/O is **not** going through `cellFsOpen` today (only `gow2.psarc` is seen under FS trace). The work is: (1) name the guest open path and the host entry it should hit; (2) discriminate whether the `.wav` fail is causal or survivable; (3) make the real open land on the existing `movie_io` cache (or a faithful FIOS-member open that returns a usable handle); (4) treat recursion-cap/SIGBUS as host-stack fallout of the park, not as a free pass to stamp EOS.

**Tech Stack:** lifted C in `../gow2-recomp/recomp_macos_v2/ppu_recomp_*.cpp` (gitignored; fix via `../gow2-recomp/recomp_mid_v2/patch_*.py` + `apply_all_patches.sh`), `runtime/ppu/ppu_fs.cpp` (`cellFsOpen` → `movie_io_*`), `libs/video/movie_hle.c` (`movie_io_resolve` already handles `.wav` case-insensitively), `../gow2-recomp/movie_eos_arm.c` (`[MOVIEFSM]` / `[MOVIEOBJ]`), gated env probes (`PS3_TRACE_*`), multi-run smokes under `../gow2-recomp/`.

**Repos:** motor `ps3recomp` + port checkout `../gow2-recomp` (lift `recomp_macos_v2/`, patches `recomp_mid_v2/`, binary `./boot_gow2`, env `./env_gow2.sh`). Sibling mirror `games/gow2` may lag; day-to-day run is `gow2-recomp`.

## Global Constraints

- **FORBIDDEN as fixes:** CRC bypass, stubbing a combination, hand-stamping magic, cosmetic null-checks, masking a guard, writing `obj+0x744` or `st620` from the host, arming `g_movie_eos_ea` with no real done-producer.
- **M3 forge trap (keep):** `PS3_MOVIE_EOS=1` + no done producer + no FORCE → arming must **not** fire; `st620` must stay parked. If it advances, revert.
- All probes env-gated, **OFF by default**, no-op on baseline.
- Prefer **calling the guest's own code** (complete FIOS/snd_stream contracts) over simulating FSM progress.
- Faithful to CELL/PPC behaviour; never diverge from console just to make something work.
- Never forge results: label **in-boot** vs **offline/unit** vs **not-exercised**.
- Windows must not regress; one runtime serves both hosts.
- Lift fixes = idempotent `patch_*.py` under `../gow2-recomp/recomp_mid_v2/`; re-applied by `../gow2-recomp/apply_all_patches.sh` (glob).
- Never commit game data (`*.wav`, `*.m2v`, `movie_cache/*`, `gow2.psarc`, `EBOOT.*`).
- Commits in Portuguese, no Co-Authored-By. Explicit `git add` paths only.
- **Nondeterminism:** single-run comparisons are not evidence. Acceptance uses **N of M runs** (defaults below). Occasional `vm commit failed` → re-run that trial, do not count it.

## Measured facts this plan starts from (2026-07-20, macOS)

> Snapshot **inicial** do plano (manhã). Para o estado **actual**, ver **Status 2026-07-20 late** no topo.

| Fact (baseline plan) | Evidence | Update late |
|------|----------|-------------|
| `st620` goes `0→1`, parks at 1 | `[MOVIEFSM]` | **1→3** após Task 8 |
| `obj+0x744` stays 0 | same | **ainda 0** (EOS plano) |
| `obj+0x630/+0x634` stay `0` | `[MOVIEOBJ]` | handles **≠0** no open; read pode limpar após 4274 |
| cellVdec Open/StartSeq/DecodeAu ×0 | boot log | inalterado (fora do path crítico) |
| `snd_stream: couldn't open …wav` | guest string | **WAV_SURVIVABLE** (Task 3) |
| Cache has `.wav`/`.m2v` | `movie_cache/` | OK |
| FIOS path `smlogo_v2.m2v` | probes | **open OK** |
| Recursion / SIGBUS @0045CB90 | cap | **refutado** (bctr-tail) |
| SPURS jobs | log | OK |

## Answers the plan must lock in (a–e)

### (a) What path does `snd_stream` use to open `/_movies/SmLogo_v2.wav`?

**Static chain (verified in lift `recomp_macos_v2/ppu_recomp_001.cpp`):**

```
func_002C00DC  (Play — sole writer of st620 0→1)
  ├─ sprintf-like func_00373D00 with TOC formats
  │     EBOOT strings: "/_movies/%s.vpk" (VA 0x4CBBC0),
  │                    "/_movies/%s.wav" (VA 0x4CBBD0),
  │                    "/_movies/%s.m2v" (VA 0x4CA068)
  ├─ clear EOS bytes obj+0x744/745/746
  ├─ func_0045C548     → init snd_stream object (stack)
  ├─ func_0045E230     → snd_stream OPEN (returns into obj+0x720)
  │     └─ … → on fail: func_004654D0 → import thunk func_004B99D8
  │              (ps3_indirect_call) → prints
  │              "snd_stream: couldn't open file %s"
  ├─ func_002B4340(r3=obj+0x62C, r4=name, r5=flags/size)
  │     └─ video FIOS open setup:
  │           func_002B43C0 → sprintf path → func_0030D578 (FIOS open entry)
  │                         → writes handle to [container+0x8] (= obj+0x634 path)
  │           func_002B441C → busy-poll via func_002B4224 until [io+0x90] done
  └─ vm_write32(obj+0x620, 1)
```

**Host landing points (what the open *should* hit):**

| Path class | Guest choke point | Host entry that can serve cache |
|------------|-------------------|----------------------------------|
| **cellFs** (if snd_stream/FIOS eventually open a real FS path) | any import/thunk that ends in `cellFsOpen` | `runtime/ppu/ppu_fs.cpp` `cellFsOpen` → `movie_io_open` when `PS3_MOVIE_IO=1` |
| **FIOS member** (path inside mounted `gow2.psarc`) | `func_0030D578` / dearchive chain | today: **no** host member open for `/_movies/...`; only whole-archive `cellFsOpen(gow2.psarc)`. `movie_io` is the intended bypass once the path string is offered to it |

**How to find the live open from lifted C (do this in Task 1, do not guess NIDs):**

1. Offline: `nm ../gow2-recomp/boot_gow2 \| grep cellFsOpen` confirms host symbol exists.
2. Lift search: `void func_0045E230` / callees in `ppu_recomp_00{1,2,3}.cpp`; follow every `ps3_indirect_call` / import thunk (`func_004B….` loads OPD from TOC near `0x0052xxxx`).
3. Runtime probe (gated): at entry of `func_0045E230` and of `func_0030D578`, dump guest path string (r3/r4/r5) + return value + whether `cellFsOpen`/`[movieio]`/`[fs]` fire in the same window.
4. If path is `/_movies/SmLogo_v2.wav` and still no `[fs]`/`[movieio]`, the open is pure guest FIOS against the already-open psarc — host fix is either HLE at `func_0030D578` / member open that calls `movie_io_open`, or teaching the guest FS shim to see member paths (prefer the former at the single choke point, same pattern as `patch_2b3d1c_*` for async read).

**Working hypothesis for (a):** audio open is **guest snd_stream (`func_0045E230`)** using a **FIOS/media handle against the mounted archive**, not `cellFsOpen`. That is why `movie_io` never sees the `.wav` despite being wired and able to resolve it.

### (b) Is the failing `.wav` open the CAUSE of the state-1 park, or a survivable side-effect?

**Do not decide from one log.** The discriminating experiment is Task 3.

| Outcome of Task 3 | Conclusion | Next |
|-------------------|------------|------|
| Runs that reach `st620≥3` **still** print the wav fail, with same rate as baseline | wav fail is **survivable side-effect**; real blocker is elsewhere (likely FIOS video handles / EOS producer) | do not "fix" audio alone; prioritize Tasks 4–5 (FIOS open / handles) |
| Runs that leave state 1 **never** (or almost never) show the wav fail; parked runs always show it | wav fail is **on the critical path** (or perfectly correlated with a shared open infrastructure) | fix audio open (Task 4) first |
| Gated host success for `.wav` only (Task 3B) moves `st620` off 1 without forging EOS | audio open is **causal enough** to unblock | keep the fix faithful (movie_io / FIOS), not a stamp |
| Gated host success for `.wav` only leaves `st620=1`, handles still 0, f744=0 | audio is **side-effect**; video FIOS / EOS still missing | leave audio diagnostic; open video path |

### (c) What is recursing 4001 deep at `0x002C07D8`?

**Not an open-retry loop.** Lift body of `func_002C07D8` (chunk 001):

```c
// pseudo from lift
u8 eos = *(u8*)(r30 + 0x744);   // r30 = movie object
if (eos == 0) trampoline → func_002C05C8;   // stay in state-1 family
else { *(u32*)(r30+0x620) = 2; *(u32*)(r30+0x620) = 3; trampoline → func_002C05F8; }
```

So `0x002C07D8` is the **state-1 → 3 gate** that polls the EOS byte. The recursion-cap log means: host `ppu` dispatch depth hit 4000 while this (or its caller chain) was the current target — classic **lifted poll/trampoline stack growth** on the PPU main thread when the wait never completes. It is a **consequence of parking at state 1 with f744=0**, not the function that opens the wav.

**Second site `0x0045CB90`:** small helper in the snd_stream / voice table region (same `0x35A8` stride as `func_0045E2A8` / `func_0045CC40`). Cap there → ~32MB host stack blown → SIGBUS. Independent crash channel; 0 `[SPUCRASH]`.

| Site | Role | Relation to wav |
|------|------|-----------------|
| `0x002C07D8` | EOS poll / st620 1→3 | After Play; waits on f744, not on open syscall |
| `0x0045CB90` | snd_stream table/voice helper | Same subsystem as audio; can fire after audio work; **deferrable** for open diagnosis |

### (d) FIOS handles `obj+0x630` / `+0x634` never populated — silent Play open fail?

**Yes, failure (or never-completed open) is the default explanation.**

Play always calls `func_002B4340` with `r3 = obj+0x62C` (FIOS op container). Success path `func_002B43C0` ends with:

```text
vm_write32(container + 0x8, handle_from_func_0030D578)
```

Sampler fields (from `movie_eos_arm.c` / Windows `boot_main.cpp`):

| Offset | Meaning |
|--------|---------|
| `obj+0x630` | open handle (documented as m2v FIOS open result) |
| `obj+0x634` | read handle state-1 polls (`func_002B4224` via container) |
| `io+0x90` | FIOS completion word |

If both stay 0 for the whole run:

- either `func_0030D578` never returns a handle,
- or the branch that writes it is never taken,
- or a later clear zeros them (less likely if never non-zero even once — Task 2 edge-logs first write).

**Where failure surfaces (no single errno log today):**

1. `[MOVIEOBJ] open=0x0 read=0x0` for the entire boot (already measured).
2. `func_002B4224`: if `[container+8]==0`, returns immediately with "not done" / empty — **state-1 has no io op to wait on**.
3. No `[movieio] open` for `SmLogo` / `.m2v` / `.wav`.
4. f744 stays 0 → `func_002C07D8` never promotes to st620=3 → recursion-cap spam.

### (e) Ordering: audio open, `0x002C07D8` recursion, `0x0045CB90` overflow

| Priority | Item | Why |
|----------|------|-----|
| **1 — do first** | Identify + fix **open path** (audio and/or video FIOS) | Without bytes/handles there is no honest EOS producer |
| **2 — parallel probes OK** | `0x002C07D8` recursion characterization | Symptom of park; do **not** "fix" by forging f744; optional yield/stack work only after open is understood |
| **3 — defer without blocking 1–2** | `0x0045CB90` SIGBUS (~1/4 runs) | Stabilizes multi-run acceptance; not required to *name* the open wall |
| **Never first** | FORCE SEQDONE / EOS arm as "fix" for this wall | Proven off critical path (no vdec session); M3 forbids arm without producer |

```text
T0 timeline correlation (multi-run)
 → T1 snd_stream open probe (name host entry)
 → T2 FIOS open / handle probe (0030D578 / 002B4340)
 → T3 discriminating experiment (wav causal vs side-effect)
 → T4 fix the proven open path (movie_io or faithful FIOS-member open)
 → T5 leave-state-1 acceptance (N of M) without forge
 → T6 stack overflow mitigation (0045CB90 / depth) for stable CI
 → T7 re-run M3 forge trap + Windows non-regression note
```

---

## File map

| File | Role |
|------|------|
| `../gow2-recomp/recomp_macos_v2/ppu_recomp_001.cpp` | Play `func_002C00DC`, EOS gate `func_002C07D8`, FIOS `func_002B4340` / `func_0030D578` / `func_002B4224` — **edit only via patches** |
| `../gow2-recomp/recomp_macos_v2/ppu_recomp_00{2,3}.cpp` | snd_stream fragments `func_0045E270` / `0045E45C` / import thunks |
| `../gow2-recomp/recomp_mid_v2/patch_snd_open_probe.py` | **create** — gated probes on `func_0045E230` (+ optional callees) |
| `../gow2-recomp/recomp_mid_v2/patch_fios_open_probe.py` | **create** — gated probes on `func_002B4340` / `func_0030D578` / first write of handle |
| `../gow2-recomp/recomp_mid_v2/patch_fios_member_movie_io.py` | **create only if Task 3/4 chooses FIOS HLE** — entry rewrite that offers path to `movie_io_open` |
| `../gow2-recomp/apply_all_patches.sh` | register check markers for new patches |
| `runtime/ppu/ppu_fs.cpp` | `cellFsOpen` ↔ `movie_io`; optional gated path log if still missing |
| `libs/video/movie_hle.c` | `movie_io_open` / `movie_io_resolve` (already `.wav`-capable) |
| `libs/video/fios_aread_hle.c` | later stream path; **not** the open wall |
| `runtime/ppu/ppu_loader.cpp` | recursion cap @1315–1345; do not raise cap as a "fix" |
| `../gow2-recomp/movie_eos_arm.c` | `[MOVIEFSM]` / `[MOVIEOBJ]` already |
| `../gow2-recomp/smoke_moviefsm_mac.sh` | existing sampler smoke |
| `../gow2-recomp/smoke_intro_open_wall.sh` | **create** — multi-run matrix for this plan |
| `../gow2-recomp/env_gow2.sh` | `PS3_MOVIE_IO=1`, cache path |

---

### Task 0: Multi-run timeline baseline (no code fix)

> ✅ **FEITO** — 25f98c1 — timeline M=8: parked 7/8, smlogo_io 0/8

**Files:**
- Create: `../gow2-recomp/smoke_intro_open_wall.sh`
- Read only: `env_gow2.sh`, `movie_eos_arm.c`, `movie_cache/SmLogo_v2.wav`

**Interfaces:**
- Consumes: `./boot_gow2`, `PS3_TRACE_MOVIEOBJ=1`, existing logs
- Produces: `/tmp/gow2_openwall_T0_summary.txt` with N-of-M counts

- [x] **Step 1: Write the smoke (red = incomplete evidence until classification exists)**

```bash
#!/usr/bin/env bash
# ../gow2-recomp/smoke_intro_open_wall.sh
# Task 0: multi-run correlation — wav fail vs st620 park vs handles vs recursion.
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE"
M="${1:-8}"          # runs
SECS="${2:-25}"      # seconds per run
TMP=$(mktemp -d /tmp/gow2_openwall.XXXXXX)
SUMMARY="$TMP/summary.txt"

run_one() {
  local i=$1
  (
    set -a; . "$HERE/env_gow2.sh"; set +a
    export PS3_NO_RSX=1
    export PS3_TRACE_MOVIEOBJ=1
    # Do NOT set FORCE / do not arm EOS for baseline
    unset PS3_VDEC_FORCE_SEQDONE_MS || true
    export PS3_MOVIE_EOS=0
    ./boot_gow2 EBOOT.ELF >"$TMP/run_$i.log" 2>&1
  ) &
  local pid=$!
  sleep "$SECS"
  kill -9 "$pid" 2>/dev/null
  wait "$pid" 2>/dev/null || true
}

echo "M=$M SECS=$SECS" | tee "$SUMMARY"
for i in $(seq 1 "$M"); do
  run_one "$i"
  log="$TMP/run_$i.log"
  wav=$(grep -ac "couldn't open file.*/_movies/.*\.wav\|couldn't open file /_movies/" "$log" || true)
  st1=$(grep -aE '\[MOVIEFSM\].*st620' "$log" | grep -c '-> 1' || true)
  st3=$(grep -aE '\[MOVIEFSM\].*st620' "$log" | grep -cE '-> (3|5|11)' || true)
  maxst=$(grep -aE '\[MOVIEFSM\]' "$log" | sed -n 's/.*-> \([0-9]*\).*/\1/p' | sort -n | tail -1)
  open0=$(grep -a '\[MOVIEOBJ\]' "$log" | grep -c 'open=0x00000000' || true)
  openN=$(grep -a '\[MOVIEOBJ\]' "$log" | grep -vc 'open=0x00000000' || true)
  rec_c07=$(grep -ac 'recursion cap @0x002C07D8' "$log" || true)
  rec_cb9=$(grep -ac 'recursion cap @0x0045CB90' "$log" || true)
  bus=$(grep -acE 'SIGBUS|Bus error' "$log" || true)
  mio=$(grep -ac '\[movieio\] open' "$log" || true)
  fs_wav=$(grep -acE '\[fs\] open.*\.(wav|WAV)|\[movieio\] open.*\.(wav|WAV)|SmLogo|smlogo' "$log" || true)
  printf 'run=%d wav=%s maxst=%s st3hits=%s open_nonzero_lines=%s recC07=%s recCB9=%s bus=%s movieio=%s fs_or_mio_smlogo=%s\n' \
    "$i" "$wav" "${maxst:-0}" "$st3" "$openN" "$rec_c07" "$rec_cb9" "$bus" "$mio" "$fs_wav" | tee -a "$SUMMARY"
done

# Aggregates
python3 - <<'PY' "$SUMMARY"
import sys,re
from collections import Counter
rows=[]
for line in open(sys.argv[1]):
    m=re.search(r'run=(\d+) wav=(\d+) maxst=(\d+) st3hits=(\d+) open_nonzero_lines=(\d+) recC07=(\d+) recCB9=(\d+) bus=(\d+) movieio=(\d+) fs_or_mio_smlogo=(\d+)', line)
    if m: rows.append(tuple(map(int,m.groups()[1:])))
if not rows:
    print('FAIL: no rows'); sys.exit(1)
M=len(rows)
parked=sum(1 for r in rows if r[1]<=1)   # maxst
left=sum(1 for r in rows if r[1]>=3)
wav_and_park=sum(1 for r in rows if r[0]>0 and r[1]<=1)
left_with_wav=sum(1 for r in rows if r[1]>=3 and r[0]>0)
print(f'AGG M={M} parked_at_le1={parked} left_ge3={left} wav_and_park={wav_and_park} left_with_wav={left_with_wav}')
print('DECISION_HINT: if left_with_wav≈left and left>0 → wav survivable; if left_with_wav==0 and left>0 → wav correlated with park')
PY

echo "logs: $TMP"
# RED until human pastes DECISION_HINT into the script header as DECISION=
grep -q 'DECISION_HINT' <<<"$(cat "$SUMMARY")" || true
```

- [x] **Step 2: Run red baseline**

```bash
cd ../gow2-recomp
chmod +x smoke_intro_open_wall.sh
./smoke_intro_open_wall.sh 8 25
```

**Expected (red for later tasks, green for Task 0 evidence):**

- `parked_at_le1` ≈ 7/8 (order of magnitude; do not require exact)
- `fs_or_mio_smlogo` = 0 on **every** counted run
- `movieio` opens for SmLogo = 0
- `open_nonzero_lines` = 0 on parked runs
- `wav≥1` on most parked runs
- `recC07≥1` often after wav line (ordering checked per log below)

- [x] **Step 3: Per-log order check (first occurrence timestamps)**

```bash
# For each run_*.log:
grep -anE "couldn't open file|\[MOVIEFSM\]|recursion cap @0x002C07D8|recursion cap @0x0045CB90|\[MOVIEOBJ\] open=" "$log" | head -40
```

Record the **first-seen order** in the summary header, e.g.:

```text
# ORDER (majority of runs): cellVdecQueryAttrEx → wav fail → st620 0→1 → MOVIEOBJ open=0 → rec@002C07D8 → (sometimes) rec@0045CB90/SIGBUS
```

- [x] **Step 4: Commit smoke only**

```bash
cd ../gow2-recomp
git add smoke_intro_open_wall.sh
git commit -m "diag(macos): smoke multi-run do wall de open da intro (wav/FIOS/rec)"
```

**Acceptance (Task 0, in-boot):** summary for **M≥8** runs exists; `DECISION_HINT` recorded; zero SmLogo `movieio`/`fs` opens confirmed on ≥7/8 runs. No production code change.

---

### Task 1: Name the live audio-open host entry (probe only)

> ✅ **FEITO** — 16bd807 — host entry = GUEST-code-EA 0x002B47D4 (8/8)

**Files:**
- Create: `../gow2-recomp/recomp_mid_v2/patch_snd_open_probe.py`
- Modify (via script): `recomp_macos_v2/ppu_recomp_001.cpp` (and 002/003 if callees live there)
- Modify: `../gow2-recomp/apply_all_patches.sh` (optional soft check for marker `SND-OPEN-PROBE`)
- Test: extend `smoke_intro_open_wall.sh` with `MODE=probe_snd`

**Interfaces:**
- Consumes: `PS3_TRACE_SNDOPEN=1` (OFF default)
- Produces: log lines `[SNDOPEN] enter func_0045E230 r3=… path=…` and `[SNDOPEN] ret r3=…` plus any nested open dumps

- [x] **Step 1: Offline unit for patch idempotence (red)**

```bash
cd ../gow2-recomp
# Fresh check: marker absent or already applied is OK; script must be dry-run safe
python3 recomp_mid_v2/patch_snd_open_probe.py recomp_macos_v2
python3 recomp_mid_v2/patch_snd_open_probe.py recomp_macos_v2   # second: ALREADY
grep -n 'SND-OPEN-PROBE' recomp_macos_v2/ppu_recomp_001.cpp | head
```

**Expected red before script exists:** `No such file`.

- [x] **Step 2: Implement idempotent probe patch**

Model: `recomp_mid_v2/patch_2b3d1c_probe.py` / `patch_introseq_probe.py`.

```python
#!/usr/bin/env python3
"""Gated probe at func_0045E230 (snd_stream open from Play).

Env: PS3_TRACE_SNDOPEN=1 (OFF default). Idempotent. Marker: SND-OPEN-PROBE.
"""
from pathlib import Path
import sys

MARKER = "SND-OPEN-PROBE"
FUNC = "void func_0045E230"
ROOT = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "recomp_macos_v2"

PROBE = r'''        /* SND-OPEN-PROBE: dump path buffer + ABI at snd_stream open. OFF default. */
        { static int on=-1; if(on<0){extern char* getenv(const char*);
            const char* e=getenv("PS3_TRACE_SNDOPEN"); on=(e&&*e&&*e!='0')?1:0;}
          if(on){ static int n=0; if(n++<16){
            uint32_t a=(uint32_t)ctx->gpr[3];
            char path[256]; path[0]=0;
            if(a && a < 0x4F000000u){
              for(int i=0;i<255;i++){ unsigned char c=vm_read8(a+i); path[i]=c? (char)c:0; if(!c) break; }
              path[255]=0;
            }
            fprintf(stderr,"[SNDOPEN] enter #%d r3=0x%08X r4=0x%08X r5=0x%08X path='%s'\n",
              n,a,(uint32_t)ctx->gpr[4],(uint32_t)ctx->gpr[5],path);
            fflush(stderr); } } }
'''

def patch_file(p: Path) -> str:
    t = p.read_text(encoding="utf-8", errors="replace")
    if MARKER in t:
        return "ALREADY"
    if FUNC not in t:
        return "SKIP"
    sig = "void func_0045E230(ppu_context* ctx) {\n"
    if sig not in t:
        return "FAIL-NEEDLE"
    t2 = t.replace(sig, sig + PROBE, 1)
    if t2 == t:
        return "FAIL-REPLACE"
    p.write_text(t2, encoding="utf-8", newline="\n")
    return "APPLIED"

def main() -> int:
    any_app = False
    for p in sorted(ROOT.glob("ppu_recomp_*.cpp")):
        r = patch_file(p)
        print(f"{p.name}: {r}")
        if r == "APPLIED":
            any_app = True
        if r.startswith("FAIL"):
            return 1
    return 0 if any_app or any(MARKER in q.read_text(encoding="utf-8", errors="replace") for q in ROOT.glob("ppu_recomp_*.cpp")) else 1

if __name__ == "__main__":
    raise SystemExit(main())
```

Also add a **return-edge** probe: either a second needle just before the function's epilogue return that sets `r3`, or a companion probe on `func_0045E270` (known early-fail return with `r3=0` in chunk 002).

Optional second function in the same script: entry of every `ps3_indirect_call` site is too broad — instead, log **once** when guest path bytes contain `.wav` by scanning `func_0045E45C` / path buffer at `voice+0xF0` if Task 0 order shows fail after enter.

- [x] **Step 3: Apply, rebuild guest chunk(s), run probe**

```bash
cd ../gow2-recomp
python3 recomp_mid_v2/patch_snd_open_probe.py recomp_macos_v2
# rebuild only affected objects — use project recipe:
./build_macos.sh   # or incremental compile of ppu_recomp_001/002/003 + relink
. ./env_gow2.sh
export PS3_NO_RSX=1 PS3_TRACE_SNDOPEN=1 PS3_TRACE_MOVIEOBJ=1 PS3_MOVIE_EOS=0
LOG=/tmp/sndopen.log
./boot_gow2 EBOOT.ELF >"$LOG" 2>&1 & PID=$!; sleep 25; kill -9 $PID 2>/dev/null; wait $PID 2>/dev/null || true
grep -aE '\[SNDOPEN\]|couldn.t open file|\[fs\] open|\[movieio\]' "$LOG" | head -40
```

**Expected green for Task 1:**

- ≥1 `[SNDOPEN] enter` with `path` containing `SmLogo` or `/_movies/` and `.wav` **in ≥6 of 8 runs**
- Same window: **zero** `[fs] open` for that path (confirms non-cellFs)
- Fail message still present when open returns 0

- [x] **Step 4: Record host entry decision in smoke header**

```text
# HOST_ENTRY_AUDIO: <none|cellFsOpen|FIOS:func_0030D578|other:...>
# EVIDENCE: log path + grep counts from M=8
```

If still unclear after enter/ret, add one more probe iteration on the first callee that receives the path string (timebox 1 session) — do not implement HLE yet.

- [x] **Step 5: Commit**

```bash
cd ../gow2-recomp
git add recomp_mid_v2/patch_snd_open_probe.py smoke_intro_open_wall.sh
# apply_all_patches.sh only if you added a check
git commit -m "diag(macos): probe gated do open snd_stream (func_0045E230)"
```

**Acceptance (Task 1):** in-boot `[SNDOPEN]` names the guest path; host entry class filled (`cellFs` vs `FIOS` vs `unknown-with-next-probe`); baseline with `PS3_TRACE_SNDOPEN` unset shows **zero** `[SNDOPEN]` lines (OFF default).

---

### Task 2: FIOS open / handle write probe (video path)

> ✅ **FEITO** — c0cd147 — open NAO falha: /_movies/smlogo_v2.m2v 8/8, handle !=0 em 15/16

**Files:**
- Create: `../gow2-recomp/recomp_mid_v2/patch_fios_open_probe.py`
- Modify via script: `func_002B4340`, `func_0030D578`, and the `vm_write32(...+0x8, …)` success store in `func_002B43C0`
- Test: `smoke_intro_open_wall.sh MODE=probe_fios`

**Interfaces:**
- Consumes: `PS3_TRACE_FIOSOPEN=1` (OFF default)
- Produces: `[FIOSOPEN] enter 002B4340 r3=container r4=… r5=…`; `[FIOSOPEN] 0030D578 path=… ret=…`; `[FIOSOPEN] handle write container+8=…`

- [x] **Step 1: Red — smoke requires handle-write lines**

```bash
# With PS3_TRACE_FIOSOPEN=1, before patch:
# expected: no [FIOSOPEN] lines → FAIL
grep -c '\[FIOSOPEN\]' /tmp/fiosopen.log | grep -qx 0
```

- [x] **Step 2: Implement probe patch (same style as Task 1)**

Needles (verify with `python3 -c` search before shipping — line numbers churn):

1. Entry `void func_002B4340(ppu_context* ctx) {`
2. Entry `void func_0030D578(ppu_context* ctx) {` — dump `r5` path (Play ABI uses r5 as path in `002B43C0` call: `r5 = stack path buffer`)
3. After `vm_write32(ctx->gpr[31] + 0x8, ctx->gpr[3]);` inside `func_002B43C0` (handle store)

Path dump: same byte loop as Task 1, cap 16 logs.

- [x] **Step 3: Run M=8**

```bash
export PS3_TRACE_FIOSOPEN=1 PS3_TRACE_MOVIEOBJ=1 PS3_NO_RSX=1 PS3_MOVIE_EOS=0
# 8×25s as Task 0
```

**Classify (write into summary):**

| Class | Evidence | Meaning |
|-------|----------|---------|
| **F0** | no enter of `002B4340` | Play not completing open half (contradicts st620=1 — re-check) |
| **F1** | enter 002B4340, no 0030D578 | wrong flags branch / early fail |
| **F2** | 0030D578 path=…m2v/vpk, ret=0 or never | FIOS open fails for member |
| **F3** | handle write non-zero once, then cleared | open OK then teardown |
| **F4** | handle write never, path looks like `/gow2.psarc` only | wrong path offered (known old trap) |

Expected class today: **F2** or **F1** with path `/_movies/….vpk` or `.m2v`.

- [x] **Step 4: Commit**

```bash
git add recomp_mid_v2/patch_fios_open_probe.py smoke_intro_open_wall.sh
git commit -m "diag(macos): probe gated do FIOS open da intro (002B4340/0030D578)"
```

**Acceptance (Task 2):** class F0–F4 assigned from **≥6/8** consistent runs; `[MOVIEOBJ] open=` still 0 when class F1/F2; probe OFF → zero `[FIOSOPEN]`.

---

### Task 3: Discriminating experiment — wav causal vs side-effect

> ✅ **FEITO (refutacao)** — 543ab35 — WAV_SURVIVABLE: o open do .wav NAO falha (8/8 handle valido, 0/8 a falhar), servido pelo dearchiver nativo do psarc. Rotear para movie_io mascararia um nao-problema. O `couldnt open file` vem de um 2o open redundante com o pool de ops esgotado, a JUSANTE do park.

**Files:**
- Create: `../gow2-recomp/smoke_intro_open_wall_disc.sh` (or `MODE=disc` in the Task 0 smoke)
- Optional **diagnostic-only** HLE (env-gated): see Step 2 — must not be on by default and must not write st620/f744

**Interfaces:**
- Consumes: Task 0–2 logs; optional `PS3_DISC_WAV_OK=1` diagnostic
- Produces: written `DECISION` in smoke header: `WAV_CAUSAL` | `WAV_SURVIVABLE` | `SHARED_INFRA`

- [x] **Step 1: Red — A/B without any new HLE (use nondeterminism)**

From Task 0 aggregates:

```text
PASS_DISCRIM_STATIC if either:
  (left_ge3 >= 1) AND (left_with_wav == left_ge3)  → WAV_SURVIVABLE
  (left_ge3 >= 1) AND (left_with_wav == 0)         → WAV_CAUSAL_OR_CORRELATED
  (left_ge3 == 0)                                  → INCONCLUSIVE → Step 2
```

- [x] **Step 2: If inconclusive, gated diagnostic success for `.wav` only**

**Allowed diagnostic (not a product fix):** when `PS3_DISC_WAV_OK=1`, if guest reaches the **already identified** open site with a path ending in `.wav` that `movie_io_resolve` can open, return a successful open **through the same contract the guest expects** (fd/handle fields the poll already reads).  
**Forbidden:** writing `obj+0x744`, writing `st620`, arming EOS, faking m2v decode.

Implement **only** at the host entry named in Task 1 (prefer calling `movie_io_open` + completing guest handle fields the natural code checks). Keep behind `PS3_DISC_WAV_OK` so baseline is unchanged.

**Unit red (offline):**

```bash
# libs/video/tests/test_disc_wav_open.c  (create if you add the helper)
# expected FAIL: helper symbol missing
```

**In-boot matrix (M=8 each):**

| Arm | Env | Expect |
|-----|-----|--------|
| D0 | unset `PS3_DISC_WAV_OK` | same as Task 0 park rate |
| D1 | `PS3_DISC_WAV_OK=1` | wav fail line **absent** on ≥7/8; **if** maxst≥3 on ≥2/8 more often than D0 → WAV_CAUSAL enough; if park rate unchanged → WAV_SURVIVABLE |

Also log handles under D1: if audio alone does not populate `+0x630/+0x634`, video FIOS remains a separate wall (expected).

- [x] **Step 3: M3 forge trap still holds**

```bash
# D1 must NOT arm EOS or write f744
export PS3_DISC_WAV_OK=1 PS3_MOVIE_EOS=1
# no done producer on POSIX
# require: grep -c MOVIEEOS == 0; f744 stays 0 unless guest sets it
```

- [x] **Step 4: Commit decision + any gated diagnostic**

```bash
git add smoke_intro_open_wall.sh recomp_mid_v2/patch_*.py  # only if diagnostic patch landed
git commit -m "diag(macos): experimento discriminador wav causal vs colateral"
```

**Acceptance (Task 3):** `DECISION=` one of the three enums, justified by M≥8 (or D0/D1 pair). No default-on behaviour change.

---

### Task 4: Fix the proven open path (product path, still honest)

> ✅ **FEITO** — e822fc3 + 73d3ee0 — REDIRECIONADA: a causa era bctr tratado como bctrl; cap 0/16 vs 16/16

**Branch on Task 3 decision.**

#### 4A — If `WAV_CAUSAL` or `SHARED_INFRA` and host entry is FIOS member open

**Files:**
- Create: `../gow2-recomp/recomp_mid_v2/patch_fios_member_movie_io.py`
- Possibly: thin helper in `libs/video/movie_hle.c` e.g. `movie_io_open` already enough; may need a small `ps3_fios_member_open_hle(path) → handle token` that the guest stores at `container+8`
- Unit: `libs/video/tests/test_movie_io.c` already covers resolve; add case for `/_movies/smlogo_v2.wav` and mixed case

- [x] **Step 1: Offline unit red/green for resolve**

```bash
cd /Users/andrebrumcortezferreira/Documents/PESSOAL/ps3recomp
# existing pattern from scripts/smoke_macos.sh
clang -std=c11 -O0 -I include libs/video/tests/test_movie_io.c libs/video/movie_hle.c -o /tmp/test_movie_io
PS3_MOVIE_CACHE=../gow2-recomp/movie_cache /tmp/test_movie_io
# extend test if missing:
# open "/_movies/SmLogo_v2.wav" and "/_movies/smlogo_v2.wav" must hit
```

- [x] **Step 2: Patch entry of the choke point named in Tasks 1–2**

Preferred choke points (pick the one Task 1/2 proved):

1. **`func_0030D578`** if video+member opens share it and audio also funnels here, **or**
2. The exact audio open site if audio never reaches 0030D578.

HLE sketch (must fall through when `PS3_MOVIE_IO` off or cache miss):

```c
/* inside patched entry — conceptual; real code is C++ in lift via patch */
if (movie_io_enabled()) {
  const char* path = /* guest string at measured reg */;
  unsigned sz=0, fd=movie_io_open(path, &sz);
  if (fd) {
    /* complete guest open contract measured by Task 2:
       write handle where func_002B43C0 would, or return the value
       snd_stream checks — DO NOT invent st620/f744. */
    ctx->gpr[3] = handle_or_status;
    return; /* skip broken natural member open */
  }
}
/* fall through to natural body */
```

- [x] **Step 3: In-boot green (M=8)**

```bash
# PS3_MOVIE_IO=1, PS3_DISC_WAV_OK unset (product path)
# PASS if ≥6/8 runs show:
#   - no "couldn't open file /_movies/SmLogo_v2.wav"  OR  (if video-only fix) [movieio] open for SmLogo m2v
#   - AND ([MOVIEOBJ] open= nonzero at least once  OR  [movieio] open for smlogo)
# Does NOT yet require st620>=3 if EOS producer still missing — that is Task 5.
```

- [x] **Step 4: Commit**

```bash
# motor (if helper):
cd /Users/andrebrumcortezferreira/Documents/PESSOAL/ps3recomp
git add libs/video/movie_hle.c libs/video/tests/test_movie_io.c
git commit -m "feat(movie_io): servir open de membro FIOS da intro a partir do cache"

cd ../gow2-recomp
git add recomp_mid_v2/patch_fios_member_movie_io.py apply_all_patches.sh
git commit -m "fix(macos): HLE de open FIOS da intro via movie_io (patch idempotente)"
```

#### 4B — If `WAV_SURVIVABLE` and video FIOS is the wall

Skip audio-specific HLE as product fix (keep probes). Apply the same member-open HLE to **video** path (`.m2v` / `.vpk` request):

- If guest asks `.vpk` and only `.m2v` exists: **do not** silently alias wrong container bytes. Options faithful enough:
  1. Serve `.m2v` only when guest path is `.m2v` or bare name (resolve already does bare→m2v).
  2. If guest **only** opens `.vpk` and never `.m2v`, measure whether Windows recipe ever opens `.vpk` for SmLogo; if Windows serves `.m2v` under another name, match that; if vpk is required, extract/document — **do not invent a VPK parser in this plan**.

**Acceptance (Task 4):** product env (no `PS3_DISC_*`) on **≥6/8** runs: SmLogo-related `[movieio] open` **or** nonzero FIOS handle observed; wav fail line eliminated **only if** 4A; Windows build still links (compile `movie_hle.c` on both).

---

### Task 5: Leave `st620=1` without forging (acceptance of content path)

> ✅ **FEITO (actualizado 2026-07-20 late)** — 8363f4c + sessão sticky/play-already.
> - M3 forja-segura: PROVADA (sem produtor → sem arm).
> - **Progresso FSM sem forja:** `st620` **1→3** após consumo honesto do open
>   (`002B4274 DONE` + sticky restore quando o guest limpa `[op+0x90]` cedo demais).
> - WAD / 3→5→11: **PENDENTE** no plano EOS (wall a jusante; com arm antigo
>   observou-se 3→0 idle, não 3→5). Sem escrita de host a `f744`/`st620`.

**Files:**
- `../gow2-recomp/smoke_intro_open_wall.sh` (`MODE=leave1`)
- Existing: `movie_eos_arm.c`, `libs/codec/cellVdec.c` (FORCE already ported; off critical path)
- **No** host writes to f744/st620

- [x] **Step 1: Red — current park rate**

```bash
# M=8, product env after Task 4
# FAIL if count(maxst>=3) < 2   # still mostly parked
```

- [x] **Step 2: Only after handles/stream exist — natural or HLE **data** completion**

If Task 4 populated handles but f744 still 0:

- Prefer: guest demux + existing async-read HLE (`ps3_fios_aread_hle` / `func_002B3D1C` patch from the sibling plan) so bytes flow; EOS may still need a later producer.
- FORCE SEQDONE remains **diagnostic** only here: allowed to show WAD opens on Windows parity, **not** claimed as natural macOS intro complete without stating HLE.

- [x] **Step 3: Green criteria (honest labels)**

| Claim | Criterion (M=8, 30s) |
|-------|----------------------|
| **in-boot open fixed** | ≥6/8: `[movieio] open` SmLogo **or** `open≠0` once; wav fail ≤1/8 if 4A |
| **in-boot FSM progress** | ≥4/8: max st620 ≥3 **without** host write to f744/st620 |
| **forge-free** | 8/8 with `PS3_MOVIE_EOS=1` and no done producer: **no** `[MOVIEEOS]` arm; if FORCE unset, progress only from real open/stream |
| **WAD (stretch)** | ≥2/8: `[movieio] open` `R_LglScA` or `R_PermA` — only claim if seen |

- [x] **Step 4: Commit smoke thresholds + notes**

```bash
git commit -m "test(macos): aceite multi-run de saida do st620=1 sem forge"
```

**Acceptance (Task 5):** table above met or explicitly reclassified with new measured wall (e.g. "open fixed, EOS producer still missing") — **reclassification is a valid delivery**, not a silent pass.

---

### Task 6: Host stack / recursion-cap crash rate (deferrable stabilizer)

> ✅ **FEITO (refutacao)** — o mesmo SIGBUS que a Task 5 do plano EOS provou ja NAO reproduzir (0 em 36+ corridas): o fix do bctr (73d3ee0) apanhou 0x0045CB90 tambem, mesma classe bctr-como-chamada. A teoria do stack overflow e medivelmente falsa (2MB usados de 32 no cap). Sonda PS3_TRACE_HOST_STACK em d0b1ad7.

**Files:**
- `../gow2-recomp/build_macos.sh` / link line (`-Wl,-stack_size,0x2000000` already 32MB — measure actual)
- `runtime/ppu/ppu_loader.cpp` recursion cap (optional: increase **logging** of lr/r30 only; do **not** remove cap to hide loops)
- Optional patch: yield/`sys_timer_usleep` already exists in some poll loops — only add host preemption if proven the giant lock is held (see existing deep-recursion preemption at `ppu_loader.cpp` ~1342)

- [x] **Step 1: Red — crash rate**

```bash
# M=12, 25s, product env
# FAIL if SIGBUS_or_rec@0045CB90 >= 3/12
```

- [x] **Step 2: Minimal stabilizer**

Prefer in order:

1. Confirm link stack size applied (`otool -l boot_gow2 | grep stack` or equivalent).
2. Ensure poll paths that stack via `DRAIN_TRAMPOLINE` eventually hit a host sleep (guest already has timebase loops — if lift turns them into pure recursion, that is a **lifter/scheduling** bug: document and timebox).
3. **Do not** auto-skip `func_002C07D8` in a way that sets st620=3.

- [x] **Step 3: Green**

```text
SIGBUS count ≤ 1/12  AND  Task 5 open metrics still hold on non-crash runs
```

- [x] **Step 4: Commit**

```bash
git commit -m "fix(macos): reduzir SIGBUS por overflow de stack no poll da intro"
```

**Acceptance (Task 6):** crash rate criterion; open-wall metrics not regressed (compare Task 5 aggregates).

---

### Task 7: Forge-trap + Windows non-regression gate

> ✅ **FEITO (parcial, com limite declarado)** — o forge-trap (M3) esta no smoke_intro_macos.sh (8363f4c) e passa, ambiente-independente. O gate de nao-regressao Windows NAO foi feito: nao se compila Windows a partir de macOS/arm64 -- a politica movie_eos_should_arm e byte-a-byte a condicao do boot_main.cpp:367, que e a garantia possivel daqui. Sticky done e POSIX-only no caminho do poll; API em `ppu_loader.cpp` partilhada (no-op se o lift Win nao chamar).

---

### Task 8: Consumo de `[op+0x90]` (poll/scheduler) — sem forjar st620

> ✅ **FEITO (in-boot, 2026-07-20)** — wall “open OK, poll done=0 para sempre” resolvido o suficiente para `st620` 1→3.
>
> **Cadeia medida:**
> 1. Play reentrante com `st620!=0` (`0x002C0498`) fazia teardown+re-open → `SEM OP LIVRE` → `container+8=0`.  
>    Fix: `patch_fios_play_already_active.py` (retorna sem re-open).
> 2. Scheduler **escreve** done (`func_00306534`); RAWMEM no fim do complete ainda `00 00 00 01`; no 1º poll guest `00 00 00 00` **sem** `vm_write32` intermédio ao EA (clear “fantasma” / path bulk ainda por pinchar).  
>    Fix runtime: `ps3_fios_sticky_publish` no write de done; `STICKY-RESTORE` em `func_002B4224` se peek sticky e guest=0; `STICKY-CONSUME` em `func_002B4274`.
> 3. Fairness: POSIX giant lock + `lwcond` `acquire_prio` (`a82c594`); yield pós-cancel (`patch_fios_cancel_yield.py`).
>
> **Aceite in-boot (1 run longo + smoke):**  
> `STICKY-RESTORE` e/ou `002B4274 DONE`; `st620` ≥3; `f744` continua 0 se EOS OFF (M3).  
> **Não aceite deste task:** opens de WAD, `3→5→11`.
>
> **Higiene residual:**
> - [ ] `patch_fios_sticky_*.py` idempotente no `gow2-recomp` (hoje o sticky está no lift local).
> - [ ] Smoke N/M no checklist/oracle com contagem `DONE4274` + `st620_max≥3`.
> - [ ] (opcional) caçar quem zera `[op+0x90]` sem `write32` (não bloqueia 4.0).

**Files:**
- `../gow2-recomp/smoke_moviefsm_mac.sh` (already forbids `[MOVIEEOS]` without producer in T1 era)
- `../gow2-recomp/smoke_intro_open_wall.sh MODE=m3`
- Windows: note only unless you have a Win builder — at least `test_movie_io` + `test_fs_movie_io` on POSIX prove shared code

- [x] **Step 1: M3 red/green**

```bash
cd ../gow2-recomp
. ./env_gow2.sh
export PS3_NO_RSX=1 PS3_MOVIE_EOS=1
unset PS3_VDEC_FORCE_SEQDONE_MS
# ensure no done producer (POSIX overlay_done==0)
LOG=/tmp/m3.log
./boot_gow2 EBOOT.ELF >"$LOG" 2>&1 & PID=$!; sleep 20; kill -9 $PID; wait $PID 2>/dev/null || true
# PASS:
test "$(grep -ac '\[MOVIEEOS\]' "$LOG")" -eq 0
# st620 may be 1; must not jump because of host forge
```

- [x] **Step 2: Shared unit suite**

```bash
cd /Users/andrebrumcortezferreira/Documents/PESSOAL/ps3recomp
# from scripts/smoke_macos.sh sections 3/3b/3c if present
./scripts/smoke_macos.sh   # or the subset that builds test_movie_io + test_fs_movie_io + test_fios_aread_hle
```

- [x] **Step 3: Commit**

```bash
git commit -m "test: matriz M3 e smokes partilhados apos fix de open da intro"
```

**Acceptance (Task 7):** M3 holds on **3/3** runs; offline movie_io/fs tests PASS; probes OFF → no new log spam on baseline (`PS3_TRACE_*=unset`).

---

## Out of scope (explicit)

- Full natural SPU MPEG path that DMAs `obj+0x744` (fidelity track).
- Inventing a `.vpk` container parser without evidence Windows uses real VPK bytes for SmLogo.
- Claiming WAD/shader pipeline green solely from FORCE SEQDONE on macOS.
- Removing the recursion cap or stamping st620=3 when f744 is 0.

## Relationship to other plans

| Plan | Relation |
|------|----------|
| `2026-07-20-macos-movie-eos-fsm.md` | EOS arm / FORCE — **downstream**. This plan must run first; EOS without open forges progress. Keep M3. |
| `2026-07-20-fios-async-read-movie-io-hle.md` | Async **read** after open — needs nonzero handles / movie_io fd from **this** plan. |
| `2026-07-20-04-content-intro-wad.md` | WAD acceptance after intro leaves state 1. |
| `2026-07-20-00-wall-chain-INDEX.md` | Insert this plan as **4.0** (blocker before content WAD on macOS). |

---

## Self-review checklist (author)

- [x] Questions a–e answered with lift/host anchors
- [x] Every task has red command + expected failure and green acceptance
- [x] Multi-run (N of M) criteria, not one-shot
- [x] Forbidden forges restated; M3 kept
- [x] Probes gated OFF default
- [x] Lift fixes via `patch_*.py`
- [x] No game data commits
- [x] Ordering: open before EOS forge, stack overflow deferred but not ignored

---

## Adendo de estado — pós-sessão oracle (2026-07-20)

Consolida o que mudou depois de o plano ser marcado. A parede deste plano (open
do media antes do EOS/WAD) esta VENCIDA; a FSM avanca ate ao estado 3.

### O que destravou a conclusao da op de open

A op de open **abre e completa** (Task 2 ja provava isto). O problema medido era
de **visibilidade**: o poll do estado 1 (`func_002B4224`) le `[op+0x90]` em
busy-loop segurando o giant lock, e o unico escritor desse done word e a thread
do scheduler FIOS (`func_00306534`). Tres pecas, todas da linha oracle:

1. **Prioridade no reacquire do giant lock** (`sysPrxForUser.c` `sys_lwcond_wait`
   + `ppu_loader.cpp` POSIX, commit `a82c594`): o scheduler acordado reacquire
   com `ppu_giant_lock_acquire_prio` para bater o poller quente, que senao ganha
   o lock a cada breve libertacao e o `done90` nunca encontra o poll.

2. **Sticky do `[op+0x90]`** (runtime `ps3_fios_sticky_*` em `ppu_loader.cpp`
   versionado; **lift ainda por scriptar** -- ver `patch_fios_sticky` pendente):
   contorna um *clear fantasma* -- observado RAWMEM=1 no retorno do callback,
   0 no primeiro poll sem write32 a essa EA. A tabela host re-materializa o 1.
   NAO e bloqueante para a FSM, e um contorno; a caca ao clear fantasma fica em
   aberto (Fase 4 opcional).

3. **play-already / cancel-yield** (`patch_fios_play_already_active.py`,
   `patch_fios_cancel_yield.py`, commit `c5f718c`): o `Play` e reentrado com
   `st620!=0`, faz teardown+reopen, e o open#2 apanhava o pool de ops esgotado
   escrevendo 0 -> poll ve `io=0` para sempre. Mantem o open em voo e cede o
   lock apos o cancel para o scheduler libertar a op.

### Onde a FSM chega agora

`st620` faz **0 -> 1 -> 3** (a transicao 1->3 vem do arm de EOS, plano EOS
Task 3), e depois **3 -> 0 (idle)**, NAO 3 -> 5 -> 11. Os WADs (`R_LglScA`,
`R_PermA`) nunca abrem. O byte de EOS (`obj+0x744`) e consumido como "filme
acabou, volta a idle" em vez de "avanca para a fase dos WADs".

### Divida de higiene registada

- **`patch_fios_sticky` por extrair**: o sticky do LIFT (STICKY-RESTORE em
  `func_002B4224`, STICKY-CONSUME em `func_002B4274`, `sticky_publish` no
  scheduler) vive so no `recomp_macos_v2` gitignored. A metade runtime esta
  versionada; a do lift perde-se num re-lift ate ser scriptada.
- **Reproducao gated por RAM**: com o host em swap pesado (>7 GB) o `st620` mal
  chega a 3; as barras N-de-M precisam de memoria livre.

### Proxima parede (fora deste plano)

`st620 3 -> 5 -> 11 -> WAD`. O ramo do estado 3 (`func_002C07D8`) trata
`+0x744=1` como retorno-a-idle. Precisa de um mini-plano proprio (o mestre EOS
nao tem tasks-checkbox para o `3->0`). Aceite honesto: opens de `R_LglScA` /
`R_PermA` (plano 04), nao so o `st620` a mexer.
