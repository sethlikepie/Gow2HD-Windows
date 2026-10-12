# Intro cellVdecOpenEx / StartSeq → FORCE → WAD Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** On macOS/arm64 GoW2, make the intro movie FSM **actually enter state 4** and call **`cellVdecOpenEx` + `cellVdecStartSeq`**, so the existing **FORCE SEQDONE** watchdog can fire and the game opens **`R_LglScA` / `R_PermA`**, without forging `st620` or writing guest `obj+0x744` in RAM.

**Architecture:** G4 RE (`gow2-recomp/notes/2026-07-21-wad-trigger-g4.md`) proved the only static call sites for OpenEx/StartSeq live in **`func_002C069C` (state 4)**, gated on **`vm_read8(obj+0x744)==0`**. State 3 (`func_002C05F8`) runs the **same test first**: if `+0x744!=0` it jumps **3→4→5** without ever dispatching state 4. Our NATURAL EOS arm (`g_movie_eos_ea` + time-based `PS3_MOVIE_DONE_MS`) makes every `vm_read8(+0x744)` return 1 **while still at st=3**, so Open never runs. Fix: **delay the arm until after Open/StartSeq** (policy: arm only when `st620>=5` **or** after Open has been observed), keep `+0x744` honest (0) through states 3/4, then FORCE after StartSeq. Secondary: if st=3 still parks forever with `+0x744==0` (audio gate `obj+0x720`/`+0x1B8`), measure and unblock that path without forging st620.

**Tech Stack:** `gow2-recomp/movie_eos_arm.c` + unit `tests/test_movie_eos_policy.c`; runtime `ps3recomp/libs/codec/cellVdec.c` (FORCE already POSIX); lift `recomp_macos_v2` (gitignored) + existing `PS3_TRACE_SMPD` probes; smokes `./build_macos.sh` + PID-kill boots; env `PS3_MOVIE_EOS`, `PS3_MOVIE_DONE_MS`, `PS3_VDEC_ASYNC`, `PS3_VDEC_FORCE_SEQDONE_MS`.

## Global Constraints

- **Proibido forjar `st620`** (host write to `obj+0x620`) ou estampar `obj+0x744=1` na RAM guest “para avançar”.
- Read-hook (`g_movie_eos_ea`) é HLE de EOS **já aceite** no Windows/Task 3 — mas **timing** do arm deve deixar o Open correr.
- Probes env-gated, OFF por default; smokes de aceite matam por **PID** (`TERM` → `-9`), nunca `pkill -f boot_gow2`.
- Baseline honesto: sticky/open FIOS já dá **st620≥3** sem arm (`11a1c3c` recovery).
- Commits em português, paths explícitos, sem dados de jogo, sem co-authorship.
- Windows não regredir: mesma política de arm se partilhada; gates `PS3_*` iguais.
- Runtime Mac deve buildar: `cmake --build build-macos` + `gow2-recomp/./build_macos.sh`.
- Não reabrir merge total sp00nznet v0.7 no runtime (CLAUDE.md); tools-only já está.

## Measured facts (do not re-argue)

| Fact | Evidence |
|------|----------|
| OpenEx tramp `0x004B9398`, StartSeq `0x004B9458` | `PS3_DUMP_IMPORTS=1` + G4 note §1 |
| **Exactly one** static call site each in 31 chunks | `func_002C069C` ~`:36727`/`:36730` |
| State 3 skips state 4 if `+0x744!=0` | `func_002C05F8` ~`:36629–36634` |
| Early arm → `[EOSGATE] 002C05F8 val=1 branch=adv`, **zero** `site=002C069C` | G4 note §5.2 |
| Recipe with early arm: Open=0 StartSeq=0 FORCE=0 WAD=0 | measured 2026-07-21 wall 45s |
| FORCE latches only in `cellVdecStartSeq` | `cellVdec.c` ~`:460–481` |
| `obj+0x720` = audio handle, not vdec; video handle = `obj+0x668` | G4 note §3 |
| SMPD listener is no-op | `notes/2026-07-21-smpd-consumer.md` G2 |

## File map

| Path | Role |
|------|------|
| `gow2-recomp/movie_eos_arm.c` / `.h` | Arm policy + sampler; **primary fix site** |
| `gow2-recomp/tests/test_movie_eos_policy.c` | Offline unit for `movie_eos_should_arm` |
| `ps3recomp/libs/codec/cellVdec.c` | Open/StartSeq/FORCE logs (already ported) |
| `ps3recomp/runtime/ppu/ppu_loader.cpp` | `g_movie_eos_ea` / `vm_read8` hook (do not remove) |
| `gow2-recomp/recomp_macos_v2/ppu_recomp_001.cpp` | Lift sites (gitignored; only via `patch_*.py` if needed) |
| `gow2-recomp/recomp_mid_v2/patch_smpd_probe.py` | Optional extend site state-4 if not already applied |
| `gow2-recomp/smoke_intro_vdec_wad.sh` | **Create** — aceite M0/M_open/M_force/M_wad/M3 |
| `gow2-recomp/notes/2026-07-21-wad-trigger-g4.md` | Source of truth for RE |
| `ps3recomp/docs/superpowers/plans/2026-07-20-00-wall-chain-INDEX.md` | Point wall activo → this plan |

---

### Task 1: RED baseline — reproduce “arm early kills Open”

**Status 2026-07-21 remeasure:** DONE (measurement). Classic early-arm RED
**obsoleto** no binário actual (Task 2+A3b já landed: `e5e1495`, `b51013e`).
Tabela viva: `gow2-recomp/notes/2026-07-21-wad-trigger-g4.md` §15.
Logs: `/tmp/vdec_m0.log`, `/tmp/vdec_red.log`.

**Files:**
- Read: `gow2-recomp/notes/2026-07-21-wad-trigger-g4.md`
- Test: boot recipe below (no code change yet)

**Interfaces:**
- Consumes: recovered boot (`st620≥3` without arm)
- Produces: `/tmp/vdec_red.log` metrics table committed in note or smoke comments

- [x] **Step 1: Confirm baseline without arm (must stay green)**

```bash
cd /Users/andrebrumcortezferreira/Documents/PESSOAL/gow2-recomp
./build_macos.sh
(
  set -a; . ./env_gow2.sh; set +a
  unset $(env | awk -F= '/^PS3_TRACE_/ {print $1}') 2>/dev/null || true
  export PS3_NO_RSX=1 PS3_PERF_FSM=1 PS3_MOVIE_EOS=0
  unset PS3_MOVIE_DONE_MS PS3_VDEC_FORCE_SEQDONE_MS
  exec ./boot_gow2 EBOOT.ELF
) > /tmp/vdec_m0.log 2>&1 & BPID=$!
sleep 30; kill -TERM $BPID 2>/dev/null; sleep 1; kill -9 $BPID 2>/dev/null; wait $BPID 2>/dev/null
grep -E '\[MOVIEFSM\] st620|cellVdec' /tmp/vdec_m0.log | head -20
```

Expected **GREEN**: `st620` max **≥3**; `Open` count **0** is OK on M0 (no arm required).

**Medido 2026-07-21:** st max **11**, Open=**1**, StartSeq=**1** (A3b desbloqueia
st3→4 mesmo com EOS=0). GREEN acima do mínimo do plano.

- [x] **Step 2: RED recipe — early arm (current policy)**

```bash
(
  set -a; . ./env_gow2.sh; set +a
  unset $(env | awk -F= '/^PS3_TRACE_/ {print $1}') 2>/dev/null || true
  export PS3_NO_RSX=1 PS3_PERF_FSM=1 PS3_TRACE_SMPD=1
  export PS3_MOVIE_EOS=1 PS3_MOVIE_DONE_MS=4000
  export PS3_VDEC_ASYNC=1 PS3_VDEC_FORCE_SEQDONE_MS=8000
  exec ./boot_gow2 EBOOT.ELF
) > /tmp/vdec_red.log 2>&1 & BPID=$!
sleep 45; kill -TERM $BPID 2>/dev/null; sleep 1; kill -9 $BPID 2>/dev/null; wait $BPID 2>/dev/null
echo "arm=$(grep -c 'arming EOS' /tmp/vdec_red.log || true)"
echo "hit=$(grep -c 'read-hook HIT' /tmp/vdec_red.log || true)"
echo "open=$(grep -c '\[cellVdec\] Open' /tmp/vdec_red.log || true)"
echo "start=$(grep -c 'StartSeq' /tmp/vdec_red.log || true)"
echo "force=$(grep -c 'FORCE SEQDONE' /tmp/vdec_red.log || true)"
echo "st4=$(grep -c 'site=002C069C' /tmp/vdec_red.log || true)"
echo "wad=$(grep -ciE 'R_LglScA|R_PermA' /tmp/vdec_red.log || true)"
```

Expected **RED** (documents the wall) — **histórico G4 §5.2**:
- `arm≥1`, `hit≥1`
- `open==0`, `start==0`, `force==0`, `st4==0` (state 4 never dispatched)
- `wad==0`

**Medido 2026-07-21 (parede nova):** arm=1 hit=1 **open=1 start=1 st4=1**
force=0 wad=0. Arm só em st=11 (gate Task 2). Pós-HIT: STOPENTRY+Close →
st 11→0. Classic early-arm RED **não se reproduz**. Próximo = **Task 4**
(FORCE→WAD). Tasks 2/3/3b já no git — re-correr só se regressão.

- [x] **Step 3: Commit nothing (measurement only) or append one paragraph to G4 note with today’s log paths**

```bash
# optional: append measured counts to notes/2026-07-21-wad-trigger-g4.md
# git add notes/... && git commit -m "diag(g4): reconfirma RED Open=0 com arm early"
```

**Feito:** §15 em `notes/2026-07-21-wad-trigger-g4.md` (gow2-recomp). Commit
opcional no gow2-recomp quando o utilizador quiser.

---

### Task 2: Offline unit — arm policy requires post-open state

**Status:** DONE in git `e5e1495` (`fix(movie): atrasa arm EOS ate st620>=5`). Remeasure
2026-07-21 confirma arm só em st=11. Reabrir só se regressão.

**Files:**
- Modify: `gow2-recomp/movie_eos_arm.h` (signature of `movie_eos_should_arm`)
- Modify: `gow2-recomp/movie_eos_arm.c` (implementation)
- Modify: `gow2-recomp/tests/test_movie_eos_policy.c` (cases)
- Modify: sampler call site in `movie_eos_arm.c` (~`:363`)

**Interfaces:**
- Consumes: `st` from sampler, `eos_env`, `eos_ea`, `overlay_done`
- Produces: `int movie_eos_should_arm(int eos_env, uint32_t eos_ea, long overlay_done, uint32_t st620)`  
  Returns 1 only if env on, not yet armed, done producer true, **and `st620 >= 5`**  
  (state 5 is first state after Open/StartSeq writes `st620=5` in `func_002C069C`).

**Rationale:** `st>=5` implies state 4 body ran (Open+StartSeq then `st=5`). Arming only then keeps `+0x744` as 0 for states 3/4.

- [ ] **Step 1: Write failing unit cases**

In `tests/test_movie_eos_policy.c`, add (adjust includes to match existing file):

```c
/* RED until signature + body updated */
assert(movie_eos_should_arm(1, 0, 1, /*st*/3) == 0);  /* done@st3 must NOT arm */
assert(movie_eos_should_arm(1, 0, 1, /*st*/4) == 0);  /* still pre-open complete */
assert(movie_eos_should_arm(1, 0, 1, /*st*/5) == 1);  /* post Open/StartSeq */
assert(movie_eos_should_arm(1, 0, 1, /*st*/11) == 1);
assert(movie_eos_should_arm(1, 0x869F1C, 1, 5) == 0); /* already armed */
assert(movie_eos_should_arm(0, 0, 1, 5) == 0);        /* env off */
assert(movie_eos_should_arm(1, 0, 0, 5) == 0);        /* no producer */
```

- [ ] **Step 2: Run unit — expect FAIL (old 3-arg signature or wrong behaviour)**

```bash
cd /Users/andrebrumcortezferreira/Documents/PESSOAL/gow2-recomp
clang -std=c11 -O0 -Wall -I . tests/test_movie_eos_policy.c movie_eos_arm.c -o /tmp/test_policy
/tmp/test_policy
```

Expected: compile error or assertion fail on st=3 case.

- [ ] **Step 3: Implement policy**

`movie_eos_arm.h`:

```c
/* Arm EOS read-hook only after the intro FSM has passed state 4
 * (OpenEx+StartSeq). st620>=5 is written by func_002C069C after StartSeq. */
int movie_eos_should_arm(int eos_env, uint32_t eos_ea, long overlay_done, uint32_t st620);
```

`movie_eos_arm.c`:

```c
#define MOVIE_STATE_POST_OPEN  5u

int movie_eos_should_arm(int eos_env, uint32_t eos_ea, long overlay_done, uint32_t st620)
{
    if (!eos_env || eos_ea != 0 || !overlay_done) return 0;
    if (st620 == 0xFFFFFFFFu) return 0;
    if (st620 < MOVIE_STATE_POST_OPEN) return 0;
    return 1;
}
```

Sampler:

```c
if (movie_eos_should_arm(s_sampler_eos, g_movie_eos_ea, done, st)) {
    g_movie_eos_ea = obj + MOVIE_OFF_EOS;
    fprintf(stderr,
        "[MOVIEEOS] %s done (st620=%u) -> arming EOS read-hook at 0x%08X ([obj+0x744])\n",
        done_overlay ? "overlay" : "time-based", st, g_movie_eos_ea);
    fflush(stderr);
}
```

Update any other call sites of `movie_eos_should_arm` (grep).

- [ ] **Step 4: Run unit — expect PASS**

```bash
clang -std=c11 -O0 -Wall -I . tests/test_movie_eos_policy.c movie_eos_arm.c -o /tmp/test_policy
/tmp/test_policy && echo PASS
```

- [ ] **Step 5: Commit (gow2-recomp)**

```bash
cd /Users/andrebrumcortezferreira/Documents/PESSOAL/gow2-recomp
git add movie_eos_arm.c movie_eos_arm.h tests/test_movie_eos_policy.c
git commit -m "fix(movie): atrasa arm EOS ate st620>=5 (deixa estado 4 abrir vdec)"
```

---

### Task 3: In-boot GREEN — Open + StartSeq without early skip

**Status:** DONE via Task 3b A3b `b51013e` + remeasure Task1 2026-07-21
(open=1 start=1 st4=1). Ver G4 §14–§15.

**Files:**
- Uses: rebuilt `boot_gow2` after Task 2
- Optional: `PS3_TRACE_SMPD=1` for `[EOSGATE] site=002C069C`

**Interfaces:**
- Consumes: delayed-arm binary
- Produces: log with Open≥1 and StartSeq≥1

- [ ] **Step 1: Rebuild host**

```bash
cd /Users/andrebrumcortezferreira/Documents/PESSOAL/gow2-recomp
./build_macos.sh
```

- [ ] **Step 2: Boot recipe — natural progress to Open**

```bash
(
  set -a; . ./env_gow2.sh; set +a
  unset $(env | awk -F= '/^PS3_TRACE_/ {print $1}') 2>/dev/null || true
  export PS3_NO_RSX=1 PS3_PERF_FSM=1 PS3_TRACE_SMPD=1
  export PS3_MOVIE_EOS=1
  # Keep time-based producer; arm now waits for st>=5
  export PS3_MOVIE_DONE_MS=auto
  # Do NOT enable FORCE yet — this task only proves Open path
  unset PS3_VDEC_FORCE_SEQDONE_MS
  export PS3_VDEC_ASYNC=1
  exec ./boot_gow2 EBOOT.ELF
) > /tmp/vdec_open.log 2>&1 & BPID=$!
sleep 60; kill -TERM $BPID 2>/dev/null; sleep 1; kill -9 $BPID 2>/dev/null; wait $BPID 2>/dev/null
echo "open=$(grep -c '\[cellVdec\] Open' /tmp/vdec_open.log || true)"
echo "start=$(grep -c 'StartSeq' /tmp/vdec_open.log || true)"
echo "st4=$(grep -c 'site=002C069C' /tmp/vdec_open.log || true)"
echo "st3_adv=$(grep -c 'site=002C05F8.*val=1' /tmp/vdec_open.log || true)"
grep '\[MOVIEFSM\] st620' /tmp/vdec_open.log | head -15
```

Expected **GREEN** (primary):
- At least one `[EOSGATE] site=002C069C ... val=0 branch=wait` **or** `val=0` then Open
- `open≥1`, `start≥1`
- Prefer **not** seeing `002C05F8 val=1` **before** first Open

**If RED — still open==0 after 60s with st stuck at 3 and +0x744 effectively 0:**  
go to **Task 3b** (audio / `+0x746` gate). Do **not** re-enable early arm.

- [ ] **Step 3: Commit log snippet in note (gow2-recomp)**

```bash
# append short "Task 3 result" block to notes/2026-07-21-wad-trigger-g4.md
git add notes/2026-07-21-wad-trigger-g4.md
git commit -m "diag(g4): Open/StartSeq in-boot apos arm atrasado"
```

---

### Task 3b (only if Task 3 RED): unblock state 3→4 with +0x744==0

**Files:**
- Read: `func_002C0688`, `func_002C0FA0`, `func_0045B2A8` (G4 note §3)
- Probe: optional `patch_st3_audio_gate.py` gated `PS3_TRACE_AUDGATE=1` logging `obj+0x720`, result of `0045B2A8`, `+0x746`

**Interfaces:**
- Produces: classification  
  - **A3:** `+0x746==0` forever → need audio path progress  
  - **A3b:** `0045B2A8` returns 0 (audio session+0x1B8==0) forever → audio “done” never set  
  - **A3c:** advances to 4 but Open fails in HLE → fix `cellVdecOpen`

- [ ] **Step 1: Instrument (read-only)**

Log in lift via small idempotent patch at `func_002C0FA0` after `func_0045B2A8`:

```c
/* PS3_TRACE_AUDGATE */
fprintf(stderr, "[AUDGATE] h720=0x%08X rc=%d f746=%u st=%u\n",
        (unsigned)vm_read32(obj+0x720), (int)(int32_t)ctx->gpr[3],
        (unsigned)vm_read8(obj+0x746), (unsigned)vm_read32(obj+0x620));
```

- [ ] **Step 2: Measure 45s at st=3 without EOS arm**

```bash
export PS3_MOVIE_EOS=0 PS3_TRACE_AUDGATE=1 PS3_PERF_FSM=1 PS3_NO_RSX=1
# ... boot 45s ...
```

- [ ] **Step 3: Minimal fix by class (pick ONE)**

| Class | Fix (no st620 forge) |
|-------|----------------------|
| A3b audio field stuck 0 | Prefer guest progress; if host must help, HLE only the **audio session field** that `0045B2A8` reads (`session+0x1B8`) after real wav open — document as HLE of stream-complete, not FSM forge |
| A3 +0x746 stuck | Trace who writes `+0x746`; do not stamp blindly |
| A3c Open HLE fail | Fix `cellVdecOpen` args / return |

- [ ] **Step 4: Re-run Task 3 Step 2 until open≥1**

- [ ] **Step 5: Commit probe + fix**

---

### Task 4: FORCE SEQDONE after StartSeq → WAD opens

**Status 2026-07-21:** PRIMARY **GREEN** (`force≥1`, SEQDONE→guest). WAD still
0 → **Task 4b**. Fix: defer EOS arm while FORCE pending
(`movie_eos_force_blocks_arm` + `g_vdec_seqdone_fired`). Evidence G4 §16,
`/tmp/vdec_force2.log`.

**Files:**
- Read: `ps3recomp/libs/codec/cellVdec.c` (`vdec_force_seqdone`, `StartSeq` latch)
- Test: smoke recipe M_force

**Interfaces:**
- Consumes: Open+StartSeq from Task 3
- Produces: `[cellVdec] FORCE SEQDONE` + guest callback + preferably `R_*` opens

- [x] **Step 1: RED if StartSeq never ran** — abort; Task 3 incomplete

- [x] **Step 2: Boot with FORCE**

```bash
(
  set -a; . ./env_gow2.sh; set +a
  unset $(env | awk -F= '/^PS3_TRACE_/ {print $1}') 2>/dev/null || true
  export PS3_NO_RSX=1 PS3_PERF_FSM=1
  export PS3_MOVIE_EOS=1 PS3_MOVIE_DONE_MS=auto
  export PS3_VDEC_ASYNC=1
  export PS3_VDEC_FORCE_SEQDONE_MS=4000
  exec ./boot_gow2 EBOOT.ELF
) > /tmp/vdec_force.log 2>&1 & BPID=$!
sleep 70; kill -TERM $BPID 2>/dev/null; sleep 1; kill -9 $BPID 2>/dev/null; wait $BPID 2>/dev/null
echo "open=$(grep -c '\[cellVdec\] Open' /tmp/vdec_force.log || true)"
echo "start=$(grep -c 'StartSeq' /tmp/vdec_force.log || true)"
echo "force=$(grep -c 'FORCE SEQDONE' /tmp/vdec_force.log || true)"
echo "wad=$(grep -ciE 'R_LglScA|R_PermA' /tmp/vdec_force.log || true)"
grep -E '\[MOVIEFSM\] st620|FORCE|Open|StartSeq|R_Lgl|R_Perm' /tmp/vdec_force.log | head -40
```

Expected **GREEN**:
- `open≥1`, `start≥1`, `force≥1`
- **Target:** `wad≥1` (either string). If force fires but wad==0 → **Task 4b** (trace who opens WAD post-SEQDONE; do not forge).

**Medido pós-fix:** open=1 start=1 force=1 seqdone caller=yes arm pós-SEQDONE
close=1 st 11→0 **wad=0**. Primary GREEN; target WAD → 4b.

- [x] **Step 3: Commit smoke script**

Create `gow2-recomp/smoke_intro_vdec_wad.sh` encoding M0 / M_open / M_force / M3 checks from this plan (PID-kill, probes off by default, force only in M_force).

```bash
chmod +x smoke_intro_vdec_wad.sh
git add smoke_intro_vdec_wad.sh
git commit -m "test(intro): smoke vdec Open/StartSeq/FORCE/WAD"
```

---

### Task 4b (only if FORCE without WAD): locate post-SEQDONE WAD trigger

**Status 2026-07-21:** **R_LglScA + R_PermA OPEN GREEN** (F2a+F2b+FO layout+42A1D4).
Guest requests both WADs; freelist rebuild+host-pop+movie_io+DONEFORCE → DONE.
R_PermA was blocked by lifter empty-count mid-function jump (`0042A1D4`→walk
with `r28=0` → UNCOMMITTED `0x676C7367`). Evidence: G4 §17–§19,
`/tmp/vdec_emptyfix.log` (`nome='R_PermA'` + F2B 20169344 + DONE #3).
Bulk stream R_PermA **FULL** in-boot 2026-07-21 (`bytes_read=20169344`,
preads=154, GATE-FORCE) via F2B-STREAM-PUMP + freelist tag guard — G4 §22.
Natural guest WAD `002B3D1C` submit still open follow-up.

**Files:**
- Grep guest callback OPD from Open log `cbOpd=`
- Trace first `[fs]`/`[movieio]` after FORCE

- [x] **Step 1: Log SEQDONE callback entry** (existing HLE callback path) — `[cellVdec] SEQDONE -> guest ... caller=yes` com `cbOpd=0x00530178`
- [x] **Step 2: Map callback → WAD open or document missing dep** — OPD→`002BF960`→SEQDONE `+0x610=1`→MovieStop→`002B4340 nome=R_LglScA`→`/wad/r_lglsca.wad_ps3`→**F2a**
- [x] **Step 3: Minimal HLE only if proven Windows-equivalent** — yields STOP/DONE tentados; **não** GREEN wad; dep freelist documentada (sem forja)
- [x] **Step 4: Commit note + any fix** — scripts `patch_fios_stop_yield.py` + `patch_fios_done_cancel_yield.py` + G4 §17

---

### Task 5: Forge-trap + INDEX + non-regression

**Files:**
- Modify: `ps3recomp/docs/superpowers/plans/2026-07-20-00-wall-chain-INDEX.md`
- Modify: `ps3recomp/docs/superpowers/plans/2026-07-20-macos-post-st3-smpd-wad.md` (link G4 exec here)
- Test: M3 + M0

- [ ] **Step 1: M3 forge-trap**

```bash
export PS3_MOVIE_EOS=1
unset PS3_MOVIE_DONE_MS   # no producer
unset PS3_VDEC_FORCE_SEQDONE_MS
# 25s boot
# MUST: arm==0, hit==0, no host write of st620
```

- [ ] **Step 2: M0 still st620≥3 without EOS**

- [ ] **Step 3: Update INDEX**

Point **4.EOS+ / wall activo** to this plan; mark “Open path” as the execution focus.

- [ ] **Step 4: Commits**

```bash
cd /Users/andrebrumcortezferreira/Documents/PESSOAL/ps3recomp
git add docs/superpowers/plans/2026-07-20-00-wall-chain-INDEX.md \
        docs/superpowers/plans/2026-07-20-macos-post-st3-smpd-wad.md \
        docs/superpowers/plans/2026-07-21-intro-vdec-open-force-wad.md
git commit -m "docs(plan): intro vdec Open/StartSeq/FORCE/WAD (G4 execucao)"
```

---

## Ordered execution summary

| # | Task | Red | Green |
|---|------|-----|-------|
| 1 | RED baseline early-arm | open=0 with arm@st3 | table in log |
| 2 | Policy unit st≥5 | unit fails | unit passes |
| 3 | In-boot Open/StartSeq | open=0 | open≥1 start≥1 |
| 3b | Audio/746 gate (if needed) | park@3 forever | reach state 4 |
| 4 | FORCE → WAD | force=0 or wad=0 | force≥1; wad≥1 target |
| 4b | Post-SEQDONE (if needed) | wad still 0 | wad≥1 or reclass |
| 5 | M3 + INDEX | forge arms | M3 clean; docs updated |

## Anti-patterns (do not)

- Arm EOS at st=1/3 to “force cascade” (this plan’s RED).
- `vm_write32(obj+0x620, 4 or 5)` from host.
- Host `vm_base[obj+0x744]=1` before Open.
- Claiming WAD green from Windows `bt_intro_wads` without Mac in-boot `R_*`.
- Full sp00nznet runtime merge mid-task (breaks shared `boot_gow2`).

## Self-review

| Spec requirement | Task |
|------------------|------|
| Guest calls Open/StartSeq (or OpenEx) on intro | 2+3 |
| FORCE after StartSeq | 4 |
| R_LglScA / R_PermA | 4 / 4b |
| No forge st620 | Global + anti-patterns |
| Root cause = early arm vs state 4 gate | Task 1 + G4 note |
| M3 forge trap | Task 5 |

No TBD steps: sites, env vars, commands, and policy constant `MOVIE_STATE_POST_OPEN=5` are fixed.

---

## Execution handoff

Plan complete and saved to:

`docs/superpowers/plans/2026-07-21-intro-vdec-open-force-wad.md`

**Two execution options:**

1. **Subagent-Driven (recommended)** — one subagent per task, review between tasks  
2. **Inline Execution** — this session with `executing-plans` checkpoints  

Which approach?
