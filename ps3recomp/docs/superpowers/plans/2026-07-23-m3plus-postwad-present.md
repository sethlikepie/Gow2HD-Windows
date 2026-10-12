# M3+ — Post-WAD present / frame arm (residual Gate A)

> **For agentic workers:** Use superpowers:subagent-driven-development. Parent: [`2026-07-23-00-gow2-playable-master-INDEX.md`](2026-07-23-00-gow2-playable-master-INDEX.md). Prior: Track M M0–M5 closed with Gate A **RED**.

**Goal:** Restore guest re-entry into `cellGcmSetFlip*` **after** `R_Perm` full (Gate A partial green: `SetFlip_after_R_Perm ≥ 1`) without theater and without regressing Gate B.

**Architecture:** Guest leaves present path at WAD open (measured). Main then spins CC9D0 on 2 TYPE15 objs with f4=0; ICALL-BAD `ctr=0x40637488` ×12 post-R_Perm. Diagnose whether bad icall kills UI/render arm; restore natural call path or fix OPD/vtable corruption; only then expect flip/pad.

**Tech Stack:** Local lift, `count_menu_gate.py`, `rodar_gow2_menu_fast.sh`, gated probes, RE static + in-boot.

## Status (2026-07-23, R1–R4)

| Gate | Status |
|------|--------|
| **B** | **GREEN** (R1–R3 smokes) |
| **A** | **RED** — `SetFlip_after_R_Perm=0` mesmo TIMEOUT=120 |
| Menu / Playable | **NO** |

**Root class (R3):** único emissor de flip pré-WAD = banda intro `func_00017ACC`; open `R_LglScA` encerra o path; **sem re-arm** pós thr. ICALL freelist **não** é sole cause (R1–R2 H3 DEAD). Content-hold / handler / late-flip **DEAD**.

**Wall next:** present re-arm — callers de `func_00017ACC` + f4 writers TYPE15 (sem flip HLE, sem HEAP40 default).

Veredito: `gow2-recomp/notes/2026-07-23-gate-a-verdict.md` (secção M3+).

## Global Constraints

- Herda master INDEX (no game data, PT commits, no Co-Authored-By, no git add -A, probes OFF default, no PARK/forge, kill by PID).
- **Do not** claim menu/playable from FlashUI or intro flips alone.
- **Do not** make LIVE_SKIP/YIELD/HEAP40/TRACE_MAINLOOP default.

## Tasks

### Task R1: RE — ICALL-BAD 0x40637488 + last pre-WAD SetFlip caller — ✅ DONE

**SHA (gow2):** `1a540c8` — `diag(gow2): R1 RE ICALL-BAD e present pós-WAD`  
**Note:** `gow2-recomp/notes/2026-07-23-r1-icall-bad-present.md`

**Outcome:**
- `0x40637488` = freelist/heap main (arena `0x40004020`), **não** vtable TYPE15 / guest text
- r12=`0x40004024` = arena+4; r3=`0x400C3D88` estável
- Último SetFlip **antes** de `movieio open R_LglScA`; 12× ICALL-BAD **depois** do corte
- ICALL **não** é causa única do zero flip (timeline)

**Aceite:**
- [x] Document what `0x40637488` is (heap cookie? freelist? vtable?) → **freelist-as-code**
- [x] Document r3/r11/r12 context from in-boot quotes
- [x] Static or dynamic: which guest path issues the bad icall (after thr/WAD)
- [x] Hypothesis ordered list for R2
- [x] **Step 1:** Re-grep ICALL-BAD lines from menu-fast log; count unique ctr/r3/r12
- [x] **Step 2:** Map EA band vs known TYPE15/factory/pin slabs
- [x] **Step 3:** Find last SetFlip before WAD in log; note nearby guest functions if traced
- [x] **Step 4:** Optional: enable stack/lr dump on ICALL-BAD (gated env) — done in R2 MEM
- [x] **Step 5:** Note + commit `diag(gow2): R1 RE ICALL-BAD e present pós-WAD`

### Task R2: Discriminate — is ICALL-BAD causal for zero flip? — ✅ DONE (H3 DEAD)

**SHA (gow2):** `55e702a` — `diag(gow2): R2 H3 DEAD — HEAP40 não restaura SetFlip_after`  
**SHA (ps3recomp):** `e7dce33` — `feat(ppu): PS3_ICALL_HEAP40 + TRACE_ICALL_BAD_MEM (R2 disc)`  
**Note:** `gow2-recomp/notes/2026-07-23-r2-icall-flip-disc.md`

**Outcome:**
- Disc H3: `PS3_ICALL_HEAP40=1` → after=**0** (base after=0); **DEAD** como sole cause
- MEM dump reforça freelist tag `0x9102FFFF` em r3; r12 arena control
- Gate B GREEN ambos runs; probes default OFF

**Aceite:**
- [x] Design one discriminator (skip/abort bad icall vs fix OPD at source) → HEAP40 skip
- [x] Gate B must stay green
- [x] Note results — H3 DEAD; SetFlip_after never restored

### Task R3: Minimal fix for present re-entry (if R2 finds path) — ✅ DIAG DONE / ❌ FIX FAIL

**SHA (gow2):** `541c37b` — `diag(gow2): R3 present pós-WAD — mode cut + no re-arm`  
**SHA (ps3recomp):** `8eb5e00` — `diag(gow2): R3 PS3_TRACE_MAINLOOP guest flip watermark`  
**Note:** `gow2-recomp/notes/2026-07-23-r3-present-reentry.md`

R2 **não** encontrou path de flip via ICALL — R3 pivota para root class de present.

**Outcome:**
- `SetFlip_after_R_Perm ≥ 1` → **FAIL** (0 @ TIMEOUT=90 e **120**)
- Único fingerprint guest em todos os flips: `g={0x00017ADE,0x00017ADF}` ∈ `func_00017ACC`
- D1 content-hold **DEAD**; D2 handler/mode **DEAD**; D3 GetFlipStatus **DEAD**; D4 late-flip **DEAD**
- Classe A PRIMARY: mode cut intro→load **sem re-arm** pós thr
- Probe MAINLOOP gated OFF default

**Aceite original (fix):**
- [ ] `SetFlip_after_R_Perm ≥ 1` on menu-fast — **FAIL / aberto**
- [x] Gate B green
- [x] no theater
- [x] Root class mais afiada + disc novos (DONE_WITH_CONCERNS)

### Task R4: Document Gate A update (partial green or still red) — ✅ DONE (still RED)

Update `gate-a-verdict.md` + master INDEX status.

- [x] Veredito + SHAs R1–R3
- [x] Master INDEX wall next / changelog
- [x] Este plano: checkboxes outcomes
- [x] wall-chain INDEX one-liner (opcional)

**Commit target:** `docs(gow2): M3+ R1–R3 Gate A RED — present re-arm wall` (gow2)  
**Commit target:** `docs: M3+ residual fecho R1–R3 Gate A RED` (ps3recomp)

---

## Smoke

```bash
cd gow2-recomp
TIMEOUT=90 LOG=/tmp/r_m3plus.log PS3_NO_RSX=1 ./rodar_gow2_menu_fast.sh
python3 count_menu_gate.py /tmp/r_m3plus.log
# Expect: Gate B green; SetFlip_after_R_Perm=0 until re-arm fix
```

## Follow-up (fora R1–R4 — wall activo)

1. RE callers estáticos de `func_00017ACC` (`1E1A8` / `1E34C` / `1EAD8` / `25064` / `25614` / `1E3DC` …) pós thr.
2. f4 writers TYPE15 no fluxo natural.
3. **Não** reabrir H3 HEAP40 como fix; **não** forjar flip HLE.
