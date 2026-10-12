# Track M — TYPE15 product fidelity + post-B71 frame loop

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans. Parent INDEX: [`2026-07-23-00-gow2-playable-master-INDEX.md`](2026-07-23-00-gow2-playable-master-INDEX.md).

**Goal:** After thr + R_Perm + B71, the main thread leaves the two-object CC9D0 spin so the host observes **≥1 SetFlip after last R_Perm** and/or **cellPadGetData > 0** and/or a documented menu FSM transition — without theater and without regressing Gate B (StartSeq≥2 real, thr_end, R_Perm full).

**Architecture:** CB56C factory builds TYPE15 products; full attach (icall2 + `func_002A4FE4`) now completes after circular-list sanitize, but components tick forever in `func_000CC9D0` with `f4=f54=0`. Diagnose whether products are still shells / empty child lists / wrong parent scene graph; fix construction or list membership so main returns to the frame loop. Prefer guest-faithful construction over permanent CC9D0-SKIP.

**Tech Stack:** Local lift `ppu_recomp_000.cpp` / `001.cpp`, `patch_*.py` idempotentes, env-gated probes (`PS3_TRACE_CC9D0`, `PS3_TYPE15_*`), `rodar_gow2_menu_fast.sh`, Metal or `PS3_NO_RSX=1` for A/B.

## Global Constraints

- Herda todas as constraints do master INDEX.
- **Não reintroduzir PARK** / soft-park / StartSeq forge.
- **Não** aceitar menu por FlashUI sparse sem flip/pad.
- `PS3_TYPE15_SKIP_ATTACH=1` e `CC9D0-SKIP` são **diagnóstico**; aceite de M3 exige progresso **sem** depender só de skip permanente (ou documentar reclass com prova de que os dois objectos **não** devem estar na lista — com RE estática).
- Fix em lift → script `patch_*.py` commitado **ou** runtime em `ps3recomp` se for HLE genérico.

---

## File map

| Path | Role |
|------|------|
| `gow2-recomp/recomp_macos_v2/ppu_recomp_000.cpp` | `func_000CB56C`, `func_000CC9D0`, helpers TYPE15 |
| `gow2-recomp/recomp_macos_v2/ppu_recomp_001.cpp` | related post-intro / factory |
| `gow2-recomp/recomp_mid_v2/patch_type15_cb56c_product.py` | CB56C experiments |
| `gow2-recomp/recomp_mid_v2/patch_2a4fe4_trace.py` | 2A4FE4 |
| `gow2-recomp/rodar_gow2_menu_fast.sh` | recipe |
| `gow2-recomp/notes/2026-07-23-postthr-cc9d0-type15-wall.md` | wall statement |
| `gow2-recomp/notes/2026-07-23-type15-list-sanitize-fix.md` | fix shipped |
| `gow2-recomp/notes/2026-07-23-menu-fast-attempt.md` | menu-fast results |
| `ps3recomp/libs/codec/cellVdec.c` | REPLAY-NOPIC (do not break) |

### Interfaces already in tree (names to preserve)

```text
ps3_type15_product_list_reset(uint32_t prod_ea)   // circular empty at +0x70/+0x74
func_000CB56C(ctx)   // factory; default full attach after reset
func_002A4FE4(ctx)   // attach walk; SANITIZE / CLOSE-TAIL / LOOP-CAP
func_000CC9D0(ctx)   // component tick; optional SKIP range
```

Env (document any new names in the note, default OFF unless permanent):

| Env | Default | Meaning |
|-----|---------|---------|
| `PS3_TYPE15_SKIP_ATTACH` | unset | if `1`, skip icall2/2A4FE4 (legacy) |
| `PS3_TYPE15_UNSTICK` | menu-fast sets 1 | seed f4 (diagnostic) |
| `PS3_TYPE15_FORCE_ICALL2` | off | force attach path (historically hung; retest after sanitize) |
| `PS3_TRACE_CC9D0` | off | log this/f4/f54; treat env `"0"` as **false** |
| `PS3_VDEC_REPLAY_PIC` | off | if `1`, restore guest PICOUT (breaks thr) |

---

## Task M0: Freeze honest baseline (menu-fast)

**Files:**
- Create: `gow2-recomp/notes/2026-07-23-m0-baseline-counts.md` (or date of run)
- Read-only: `gow2-recomp/rodar_gow2_menu_fast.sh`
- Logs under local SCRATCH (not committed)

**Interfaces:**
- Produces: table Gate B green / Gate A red with md5 of log + command line
- Consumes: current `boot_gow2` + env from script

- [x] **Step 1: Run menu-fast once; kill by PID after 60–90s**

```bash
cd /path/to/gow2-recomp
./rodar_gow2_menu_fast.sh > /tmp/m0_menu_fast.log 2>&1 &
BPID=$!
# script may already timeout; if not:
sleep 90
kill -TERM $BPID 2>/dev/null; sleep 1; kill -9 $BPID 2>/dev/null
md5 /tmp/m0_menu_fast.log   # or md5sum
```

- [x] **Step 2: Extract counts into the note**

```bash
python3 - <<'PY'
import re, pathlib
t = pathlib.Path("/tmp/m0_menu_fast.log").read_text(errors="replace")
def c(p): return len(re.findall(p, t))
rows = {
  "StartSeq": c(r"cellVdecStartSeq|StartSeq"),
  "PARK": c(r"\bPARK\b"),
  "REPLAY-NOPIC": c(r"REPLAY-NOPIC"),
  "thr_end": c(r"thr_auto_load\(\) end|thr_auto_load end"),
  "R_Perm_full": c(r"R_Perm.*20169344|full 20169344"),
  "TYPE15_RESET": c(r"product list RESET"),
  "attach_full": c(r"attach=full"),
  "CC9D0": c(r"\[CC9D0\]"),
  "SetFlip": c(r"SetFlip|cellGcmSetFlip"),
  "Pad": c(r"cellPadGetData"),
  "FATAL": c(r"FATAL"),
}
for k,v in rows.items(): print(f"{k}\t{v}")
PY
```

- [x] **Step 3: Write note with expected shape**

| Signal | Expected M0 |
|--------|-------------|
| PARK | 0 |
| thr_end | ≥1 |
| R_Perm full | ≥1 |
| attach=full | ≥1 (post sanitize) |
| SetFlip after last R_Perm line | **0** |
| Pad | **0** |
| FATAL | 0 |

- [x] **Step 4: Commit note only (no code)**

```bash
git -C /path/to/gow2-recomp add notes/2026-07-23-m0-baseline-counts.md
git -C /path/to/gow2-recomp commit -m "docs(gow2): baseline M0 menu-fast Gate B green / A red"
```

**Pass:** note exists with numbers; Gate B holds; Gate A red explicit.

---

## Task M1: Discriminate CC9D0 spin root

**Files:**
- Modify (probe only, gated): `ppu_recomp_000.cpp` `func_000CC9D0` / `func_000CB56C` via existing or new `patch_type15_cc9d0_disc.py`
- Create: `gow2-recomp/notes/2026-07-23-m1-cc9d0-discriminator.md`
- Test: one boot log per hypothesis

**Interfaces:**
- Hypotheses (exactly one must survive):

| ID | Hypothesis | Discriminator |
|----|------------|---------------|
| H1 | Child list at product+0x70 empty after reset → tick never installs work | count children after 2A4FE4; if always 0, H1 live |
| H2 | f4/f54 never written by real guest writers (only UNSTICK) | run `PS3_TYPE15_UNSTICK=0`; if still f4=0 and same spin, H2 not sole; if writers exist, log EA of stores to +4 |
| H3 | Parent scene list only contains the two shell objs | dump parent walk head/next once per 1k ticks; if only 2 EAs forever, H3 live |
| H4 | Giant-lock / yield starves pad/flip workers | with CC9D0 yield ON vs OFF compare pad/flip; if both 0, H4 weak |
| H5 | Wrong product EA / vt still shell (0x00200000 class) | log vt + magic after icall2; shell → H5 |

- [x] **Step 1: Write failing structural test for probe env**

```python
# gow2-recomp/recomp_mid_v2/test_trace_cc9d0_env.py
# offline: ensure getenv("PS3_TRACE_CC9D0") treats "0" and unset as off
# (paste the same parse helper used in the patch)
def env_on(name: str, raw: str | None) -> bool:
    if raw is None: return False
    v = raw.strip().lower()
    return v not in ("", "0", "false", "off", "no")

assert env_on("PS3_TRACE_CC9D0", None) is False
assert env_on("PS3_TRACE_CC9D0", "0") is False
assert env_on("PS3_TRACE_CC9D0", "1") is True
```

Run: `python3 recomp_mid_v2/test_trace_cc9d0_env.py` — must PASS before trusting logs.

- [x] **Step 2: Instrument (gated) once**

Emit at most **20** lines total per process:

```text
[CC9D0-DISC] this=0x.. f4=.. f54=.. prod=0x.. vt=0x.. child_head=0x.. parent=0x..
[CB56C-DISC] obj=0x.. prod=0x.. reused=.. vt=0x.. after_attach child_head=0x..
```

Cap with static counter; never spam 1e6.

- [x] **Step 3: Run three boots**

```bash
# A: UNSTICK=0 TRACE on
PS3_TYPE15_UNSTICK=0 PS3_TRACE_CC9D0=1 ./rodar_gow2_menu_fast.sh
# B: default menu-fast
# C: PS3_TYPE15_SKIP_ATTACH=1 (contrast)
```

- [x] **Step 4: Kill hypotheses in the note**

Template:

```markdown
| H | Status | Evidence |
|---|--------|----------|
| H1 | LIVE/DEAD | … |
…
Survivor: Hx
Next task: M2 targets Hx
```

- [x] **Step 5: Commit probe script + note**

```bash
git add recomp_mid_v2/patch_type15_cc9d0_disc.py notes/2026-07-23-m1-cc9d0-discriminator.md
git commit -m "diag(gow2): discriminador CC9D0/TYPE15 pós-attach (M1)"
```

**Pass:** exactly one primary survivor (or ordered shortlist ≤2); no Gate B regression.  
**Done:** `967525e` / `b87e6a3` — survivor H1 primary (+H3).

---

## Task M2: Fix product construction / list membership (survivor-driven)

**Files:**
- Modify: helpers around CB56C / freelist / icall1 return product (paths from M1)
- Possibly: `ps3_type15_*` in lift or `runtime/ppu` if generic
- Create: `gow2-recomp/notes/2026-07-23-m2-product-fix.md`
- Script: extend `patch_type15_*.py` idempotent

**Interfaces:**
- Consumes: survivor Hx from M1
- Produces: in-boot proof that either:

**Success criterion (any one is enough for M2):**

1. After attach, `child_head` / list walk installs **>0** real children **and** CC9D0 sees **>2** distinct `this` EAs within 5s post-thr, **or**
2. f4 or f54 becomes non-zero via **guest** writer (not only UNSTICK) and tick exits early path, **or**
3. The two objects are **removed** from parent tick list with RE proof they should not be there (document addresses + who unlinks on console path)

- [x] **Step 1: RE static anchors (before coding)**

```bash
# Confirm call graph (adjust paths to local nm/llvm-objdump if used)
rg -n "func_000CB56C|func_000CC9D0|func_002A4FE4|0x0039D428" \
  gow2-recomp/recomp_macos_v2/ppu_recomp_000.cpp | head -40
```

Document in note: icall2 code EA `0x0039D428` role; fields `obj+4` (f4), `obj+0x54` (f54), `prod+0x70`.

- [x] **Step 2: Minimal fix for survivor**

| If survivor | Fix direction |
|-------------|----------------|
| H1 empty list | Do **not** leave list empty if icall2 expects populated freelist; re-run icall1 path or copy legitimate template list **with circular close**; measure |
| H5 shell vt | Reject shell product; force fresh alloc path; log `REHOME` / freelist |
| H3 parent only 2 | Find who should register menu/UI components post-B71; check AUTO_LOAD / thr completion flags the guest reads |
| H2 no f4 writers | Trace stores to `this+4` from icall2 body; fix TOC/r2 if icall wrong |

Example **list close** pattern already proven (do not re-break):

```c
// product+0x70 and +0x74 point to product+0x70 (empty circular)
// Never leave NULL-terminated pool when 2A4FE4 assumes circular
```

- [x] **Step 3: Smoke Gate B must stay green**

```bash
./rodar_gow2_menu_fast.sh
# require: thr_end>=1, R_Perm full, PARK=0, FATAL=0
```

- [x] **Step 4: Measure M2 signals**

```bash
# post-thr window: distinct CC9D0 this=, non-zero f4, or child_head!=sentinel
```

- [x] **Step 5: Commit fix + note**

```bash
git commit -m "fix(gow2): TYPE15 product/list pós-sanitize (M2 — <Hx>)"
```

**Pass:** M2 criterion 1 **or** 2 **or** 3 with log quotes; Gate B green.  
**Done:** `1f5d61c` — criterion **1** (CLOSE-PRESERVE nodes=12); f4/flip residual (2/3 fail).

**Fail / stop:** if only SKIP_ATTACH restores thr — do not ship SKIP as fix; reclassify.

---

## Task M3: Post-B71 SetFlip (Gate A partial)

**Files:**
- Depends on M2 outcome
- Possibly: pad/flip path, movie FSM residual, `PS3_NO_RSX` vs Metal
- Create: `gow2-recomp/notes/2026-07-23-m3-postb71-flip.md`
- Modify: smoke script to print “SetFlip after R_Perm” count

**Interfaces:**
- Produces: automated count in `rodar_gow2_menu_fast.sh` footer:

```python
# after log complete
import re
lines = open(log).read().splitlines()
rperm_i = max(i for i,l in enumerate(lines) if "R_Perm" in l and "20169344" in l)
post = "\n".join(lines[rperm_i:])
print("SetFlip_after_R_Perm", len(re.findall(r"SetFlip|cellGcmSetFlip", post)))
print("Pad_after_R_Perm", len(re.findall(r"cellPadGetData", post)))
```

- [x] **Step 1: Add footer counters to menu_fast script (or companion `count_menu_gate.py`)**
- [x] **Step 2: Run Metal backend once and `PS3_NO_RSX=1` once**

If flip only missing under NO_RSX, document that Gate A uses Metal for this metric.  
**Done:** NO_RSX suficiente (SetFlip loga no intro); Metal não re-medido para Gate A metric.

- [x] **Step 3: If still 0 SetFlip after M2**

Discriminate:

| Exp | Meaning |
|-----|---------|
| main stuck higher than CC9D0 | stack/probe parent of CC9D0 |
| flip called but backend silent | log in Metal present path |
| thr flags block UI mode | dump guest mode enum post-B71 |

**Done:** YIELD/LIVE_SKIP; root class = guest abandons present at WAD open (`90829b6`).

- [x] **Step 4: Minimal unblock** — only after measurement  
  *(não desbloqueado — Gate A continua RED; sem fix cosmético)*
- [x] **Step 5: Commit**

```bash
git commit -m "fix(gow2): frame loop pós-B71 (SetFlip after R_Perm ≥1)"
```

**Pass planeado:** `SetFlip_after_R_Perm >= 1` …  
**Outcome:** **FAIL** aceite flip; **PASS** diag honesto `diag(gow2): M3…` (`90829b6`). Gate A **RED**.

---

## Task M4: Pad + menu FSM signal

**Files:**
- `libs` cellPad / autostart already exist — verify wiring
- Create: `gow2-recomp/notes/2026-07-23-m4-pad-menu.md`

**Interfaces:**
- Aceite: `cellPadGetData` count > 0 after thr **or** clear menu state log (`[MENU]`, FlashUI with correlated flip, or New Game arm)

- [x] **Step 1: Confirm `PS3_PAD_AUTOSTART=1` reaches HLE**
- [x] **Step 2: If pad HLE never called, find guest poll site post-menu-mode**
- [x] **Step 3: Smoke 90s Metal; user-visible window optional**  
  *(NO_RSX; Pad_total=0 — GetData never)*
- [x] **Step 4: Commit**

```bash
git commit -m "fix(gow2): pad/menu FSM arm pós-frame-loop (M4)"
```

**Pass planeado:** pad>0 or menu FSM …  
**Outcome:** **FAIL** aceite; **PASS** diag `e7cf689` — pad never polled; no menu; no theater.

---

## Task M5: Close Gate A documentation + INDEX update

**Files:**
- Modify: `docs/superpowers/plans/2026-07-23-00-gow2-playable-master-INDEX.md` status table
- Modify: `docs/superpowers/plans/2026-07-20-00-wall-chain-INDEX.md` (wall activo → post-WAD present; Gate A RED)
- Create: `gow2-recomp/notes/2026-07-23-gate-a-verdict.md`

- [x] **Step 1: Fill verdict table**

```markdown
| Gate | Status | Evidence log |
|------|--------|--------------|
| B | GREEN | thr, R_Perm, StartSeq real, PARK=0, FATAL=0 |
| A | RED | SetFlip_after_R_Perm=0 / Pad_after_R_Perm=0 |
| Menu UI | NO | sem FSM / NewGame pós thr |
| Playable | NO | explicit |
```

- [x] **Step 2: Update both INDEXes**
- [x] **Step 3: List follow-ups for Track S** (LDRSH natural)
- [x] **Step 4: Commit docs in ps3recomp + gow2-recomp as needed**

```bash
git -C ps3recomp commit -m "docs: Track M fecho — Gate A RED + wall pós-WAD present"
```

**Pass:** INDEX and verdict agree with logs; playable **not** claimed.  
Veredito: `gow2-recomp/notes/2026-07-23-gate-a-verdict.md`.

---

## Verification matrix (Track M)

| Task | Unit / offline | In-boot | Fail if |
|------|----------------|---------|---------|
| M0 | — | counts table | Gate B red |
| M1 | env_on test | ≤20 disc lines | spam; all H live |
| M2 | — | criterion 1/2/3 | thr lost; SKIP-only |
| M3 | count script | SetFlip after R_Perm≥1 | only intro flips |
| M4 | — | pad or menu FSM | FlashUI alone |
| M5 | docs | — | claim playable |

---

## Out of scope (Track M)

- Natural SHADERSRC/LDRSH (Track S)
- Metal MSL/PSO completeness (Track G)
- Xenon TOML lifter (Track T)
- Full New Game → level (Track P)
- Reverting REPLAY-NOPIC without replacement for GetPicture path-as-TOC

---

## Relationship to TYPE15 sanitize (already shipped)

Do **not** re-open NULL-terminated pool hang unless regression appears.  
M2 builds **on top of** circular reset + full attach completing.

If full attach regresses thr: bisect list reset vs icall2 body; keep LOOP-CAP.

---

## Execution handoff

**Track M M0–M5 diagnostic closed 2026-07-23 (Gate A RED).**

- Master: `docs/superpowers/plans/2026-07-23-00-gow2-playable-master-INDEX.md`
- Este plano (checkboxes M0–M5)
- Veredito: `gow2-recomp/notes/2026-07-23-gate-a-verdict.md`
- (tooling) `docs/superpowers/plans/2026-07-23-xenonrecomp-lessons-recomp-tooling.md`

**Next wall:** post-WAD present arm (não empty-list; não Track S como menu-done).  
**If tooling only:** Track T Task A1.
