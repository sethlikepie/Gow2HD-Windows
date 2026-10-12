# GoW2 Playable — Master Implementation Plan (INDEX 2026-07-23)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement **one track at a time**. Steps use checkbox (`- [ ]`) syntax. Do **not** open Track N+1 while Track N acceptance is red, unless the track is marked **parallel**.

**Goal:** God of War II HD (NPUA80491) 100% usable on static recomp: boot → intro → WAD → **main menu** → first playable interaction, with natural content path (shaders/textures/draws) on macOS Metal, without emulator and without theater (no forged StartSeq/st620, no PARK-as-success).

**Architecture:** Two repositories — motor `ps3recomp` (runtime/libs/tools) + port `gow2-recomp` / `../gow2_work` (lifted guest, launchers, game data local-only). Critical path is **guest product construction + frame loop after AUTO_LOAD**; Metal/shader tooling and Xenon-style recomp tooling run **in parallel** but never redefine “menu reached”.

**Tech Stack:** C++20/C11 PPU recomp, SPU tables, lv2 HLE, cellVdec VT, Metal RSX backend, Python lifters, gated env probes, smoke scripts (`rodar_gow2_menu_fast.sh`, `env_gow2.sh`).

## Global Constraints

- **Never commit game data** (`EBOOT.ELF`, `extracted/`, `movie_cache/`, `*.psarc`, `spu_images` raw).
- **Commits em português**, paths explícitos; **sem** `Co-Authored-By`; **sem** `git add -A`.
- **Probes default OFF.** Permanent correctness fixes may stay always-on only if documented + opt-out env exists.
- **Proibidos como aceite:** CRC bypass, GATE-FORCE, SHADER_DEMO, forged st620/StartSeq, PARK as 2b, soft-park inject as “progress”, baseline = copy-of-after.
- **Aceite distingue:** in-boot vs offline/unit vs não-exercitado.
- **Push só fork pessoal** (`andrebrumdev`); nunca sp00nznet.
- **Kill por PID** (TERM depois -9); nunca `pkill -f boot_gow2`.
- **Fix fiel ao CELL/PPC** — sanitizers de listas corruptas são OK se o root for state residual nosso; não “consertar” o jogo divergindo do console sem prova.
- **Re-lift safety:** fixes em `ppu_recomp_*.cpp` → `patch_*.py` idempotente **ou** mid-asm/weak (Track T tooling).

---

## Status snapshot (2026-07-23, pós Track M M0–M5 + M3+ R1–R3) — o que já não é o wall

| Gate | Estado | Evidência / nota |
|------|--------|------------------|
| Mac link + intro st620 0→1→≥3 (FORCE/EOS paths) | ✅ | planos 4.0 / 4.EOS |
| 2nd StartSeq **real** (async, PARK=0) | ✅ | REPLAY-NOPIC; Gate **B GREEN** |
| thr_auto_load + AutoLoad + R_Perm 20 169 344 | ✅ | dual-gate B; smokes M0–M4 + R1–R3 |
| TYPE15 product+0x70 circular hang em 2A4FE4 | ✅ | list sanitize + full attach |
| TYPE15 empty list pós-RESET (H1) | ✅ | M2 CLOSE-PRESERVE nodes=12 (`1f5d61c`) |
| Menu-fast skip intro (FORCE 1.5s) | ✅ caminho | thr+R_Perm; **sem** menu UI |
| Track M diagnóstico M0–M4 | ✅ fechado | veredito Gate A **RED**; `notes/2026-07-23-gate-a-verdict.md` |
| M3+ residual R1–R3 (ICALL / HEAP40 / flip issuer) | ✅ diag fechado | ICALL freelist **não** sole cause; H3 DEAD; issuer reclass R5–R8; **no re-arm** |
| M3+ R5–R8 (issuer real + present-loop parents) | ✅ diag fechado | issuer=`2EFD60`; hot=`CDBA4…CDD88` ~41k **post=0**; CE0A0 exit guards |
| M3+ R9 (menu present schedule) | ✅ diag fechado | H1: schedule parents post=0; menu path tot=0; 2C0508 pre-only |
| M3+ R10 (sched arm climb) | ✅ diag fechado | B6150/B94* tot=0; 25C838=1 36A598=0; CE03C wait not sole; Gate A RED |
| M3+ R11 (OPD + post-thr PC) | ✅ diag fechado | B94EC/B61F4 no OPD; B6150 OPD orphan; POSTTHR 100% {CC9D0,2B2DD0,B951C}; Gate A RED |
| M3+ R12 (post-thr spin leave) | ✅ diag fechado | 2B2DD0 parent; ratio 2:1:1; B951C ≠ present; f4=0 residual M2; **sem fix** |
| Post-thr st620 leave idle (criterion 2a) | ❌ | só 0→0 |
| SetFlip / cellPadGetData **após** R_Perm/B71 | ❌ | **0** (TIMEOUT=120); Gate **A RED** — falta **re-arm** present pós-load |
| Main menu UI | ❌ | **NO** — wall activo = **TYPE15 f4/work** dentro `2B2DD0→B951C→CC9D0` (R12) |
| Playable | ❌ | **NO** — explícito |
| LDRSH natural N>0 / pixels de jogo | ❌ | Track S; entry após flip/present ou prova independente |
| SPU0–5 tables registadas | 🟡 | tables complete; jobs deep + opcodes residual |
| Xenon tooling (TOML/midasm/…) | 🟡 | A1 `recomp_config` shipped; resto paralelo |

### Track M outcomes (M0–M5)

| Task | Status | SHA (gow2-recomp) | Outcome |
|------|--------|-------------------|---------|
| M0 | ✅ | `5920aff` | baseline B green / A red |
| M1 | ✅ | `967525e` (+ `b87e6a3` I1) | H1 primary; H3 co-sinal (2 this) |
| M2 | ✅ estrutural | `1f5d61c` | CLOSE-PRESERVE; f4 still 0; flip still 0 |
| M3 | ✅ diag | `90829b6` | SetFlip_after=0; root class abandon present |
| M4 | ✅ diag | `e7cf689` | Pad_after=0; GetData never; no menu FSM |
| M5 | ✅ docs | `124d697` | **Gate A RED** honesto; playable=NO |

### M3+ residual (R1–R3) — Gate A still RED

| Task | Status | SHA (gow2 / ps3recomp) | Outcome |
|------|--------|------------------------|---------|
| R1 | ✅ diag | `1a540c8` | ICALL-BAD = freelist-as-code; flip cut **antes** do 1º ICALL |
| R2 | ✅ disc | `55e702a` / `e7dce33` | HEAP40 H3 **DEAD**; after=0; MEM freelist |
| R3 | ✅ diag | `541c37b` / `8eb5e00` | único issuer `func_00017ACC`; content-hold/handler/late **DEAD**; TIMEOUT=120 after=0 |
| R4 | ✅ docs | veredito note | **A RED**; wall = present re-arm |
| R5 | ✅ diag | 17ACC tot=0 | issuer fingerprint residual; 17ACC DEAD |
| R6 | ✅ diag | host BT + NID | issuer real=`2EFD60`/`2EFDA4` NID `0x21397818` |
| R7 | ✅ diag | FLIPPATH | 2EFD60/14FE18/156680 ~29k **post=0**; 2B2E74=1 |
| R8 | ✅ diag | PRESENTLOOP | hot `CDBA4→CDC08→CDD00→CDD88` ~41k **post=0**; CE0A0=1; exit u8+4 / movie_idx==4 |
| R9 | ✅ diag | MENUPRESENT | H1 schedule post=0; 194FFC/2B21*/B61* tot=0; 2C0508=6147 pre-only; H4 DEAD |
| R10 | ✅ diag | SCHEDARM | arm B6150/B94* tot=0; 25C838=1 36A598=0; BB414→BB37C 1×; CDFDC 11k pre; CE03C wait H3 DEAD sole |
| R11 | ✅ diag | OPD+POSTTHR | B94EC/B61F4 no OPD; B6150 OPD 0 refs; post thr only CC9D0+2B2DD0+B951C; ICALL menu 0 |
| R12 | ✅ diag | POSTTHR spin | 2B2DD0→B951C→CC9D0 ratio 2:1:1; B951C ICALL ctr junk; f4=0; **sem fix** no gate |

**Primary next wall:** **TYPE15 f4 writers / product work** (residual M2: nodes=12 freelist ≠ UI) **dentro** do parent `2B2DD0→B951C` — present re-arm depende disto (R12; **não** bug no gate 2B2DD0/B951C; **não** OPD B94EC — R11 DEAD).  
**Refutado como sole cause:** empty-list (M2), ICALL-BAD (R1–R2), HEAP40 (R2), content-hold/handler/late-flip (R3), 17ACC (R5), circuit-breaker em 2EFD60/CDBA4 (R7–R8), early-return nos present menu (R9 H2), CE03C wait-idle sole block (R10 H3), thr re-entry (R10 H2), static OPD schedule B94EC/B6150 (R11 H1–H2), early-return/gate bug em 2B2DD0/B951C (R12 H3).  
→ `gow2-recomp/notes/2026-07-23-gate-a-verdict.md`  
→ `gow2-recomp/notes/2026-07-23-r12-postthr-spin.md`  
→ plano residual: [`2026-07-23-m3plus-postwad-present.md`](2026-07-23-m3plus-postwad-present.md)

---

## Tracks (dependency graph)

```
Track M  Menu / post-thr frame loop     ──► CRITICAL PATH (start here)
Track S  Shaders natural + guest draw   ──► after M green or partial flip
Track G  Metal content stack (M2–M10)   ──► PARALLEL (pixels when draws exist)
Track U  SPU jobs deep + residual ISA   ──► PARALLEL (menu may need SPU later)
Track T  Xenon-style recomp tooling     ──► PARALLEL (never blocks M)
Track P  Playable polish (input/save)   ──► after M + first content frame
```

| Track | Plan file | Priority |
|-------|-----------|----------|
| **M** | **este doc +** [`2026-07-23-menu-type15-frame-loop.md`](2026-07-23-menu-type15-frame-loop.md) | **P0 — executar primeiro** |
| **S** | [`2026-07-21-shader-registry-typemap-walk.md`](2026-07-21-shader-registry-typemap-walk.md) + [`2026-07-20-05-rsx-shaders-pixels.md`](2026-07-20-05-rsx-shaders-pixels.md) | P1 após flip pós-B71 |
| **G** | [`2026-07-21-00-macos-metal-stack-INDEX.md`](2026-07-21-00-macos-metal-stack-INDEX.md) | P1 paralelo |
| **U** | [`2026-07-20-03-spu-jobs-protocol.md`](2026-07-20-03-spu-jobs-protocol.md) + audit SPU TODO | P2 paralelo |
| **T** | [`2026-07-23-xenonrecomp-lessons-recomp-tooling.md`](2026-07-23-xenonrecomp-lessons-recomp-tooling.md) | P2 paralelo (A1 primeiro) |
| **P** | (futuro) input/save/audio polish | P3 |

**Ordem de execução recomendada num único agent:** M1→M5, depois S ou G conforme pixels; T em sessão separada.

---

## File / module map (critical path)

| Path | Responsibility |
|------|----------------|
| `gow2-recomp/recomp_macos_v2/ppu_recomp_000.cpp` | `func_000CB56C`, `func_000CC9D0`, TYPE15 attach helpers |
| `gow2-recomp/recomp_macos_v2/ppu_recomp_001.cpp` | CE03C / F2B / post-intro helpers (local lift) |
| `gow2-recomp/recomp_mid_v2/patch_type15_*.py` | scripts idempotentes re-lift |
| `gow2-recomp/recomp_mid_v2/patch_2a4fe4_*.py` | sanitize/trace 2A4FE4 |
| `ps3recomp/libs/codec/cellVdec.c` | REPLAY-NOPIC (StartSeq≥2) |
| `ps3recomp/runtime/ppu/ppu_loader.cpp` | sticky, ICALL guards, TYPE15 stomp hooks |
| `gow2-recomp/rodar_gow2_menu_fast.sh` | recipe skip-intro → thr |
| `gow2-recomp/notes/2026-07-23-*.md` | wall evidence ledger |
| `tools/ppu_lifter.py` | re-lift; Track T hooks |

---

## Dual-gate acceptance (never collapse)

| Gate | Meaning | Green when |
|------|---------|------------|
| **B** | thr path | `thr_auto_load() end` ≥1, AutoLoad OK, `R_Perm` full, StartSeq≥2 **real** (PARK=0) |
| **A** | post-thr life | st620 leaves 0→0 **or** SetFlip/pad after B71 **or** menu FSM non-idle with proof |

Gate B is **already green**. Track M targets Gate A without breaking B.

### Canonical smokes

**Menu-fast (Gate B + probe A):**

```bash
cd /path/to/gow2-recomp   # or ../gow2_work sibling layout
./rodar_gow2_menu_fast.sh
# after ~90s kill by PID; count:
# thr_auto_load end, R_Perm full, StartSeq, SetFlip after last R_Perm line,
# cellPadGetData, [CC9D0], [TYPE15], FATAL
```

**Headless tooling (Track T):**

```bash
set -a; . ./env_gow2.sh; set +a
export PS3_NO_RSX=1 PS3_AUTO_LOAD_RUN=1 PS3_VDEC_ASYNC=1
export PS3_VDEC_FORCE_SEQDONE_MS=8000
./boot_gow2 EBOOT.ELF > /tmp/smoke.log 2>&1 & BPID=$!
sleep 35; kill -TERM $BPID; sleep 1; kill -9 $BPID 2>/dev/null
```

---

## Track M overview (tasks live in satellite plan)

Detailed steps: [`2026-07-23-menu-type15-frame-loop.md`](2026-07-23-menu-type15-frame-loop.md)

| Task | Title | Aceite (planeado) | Outcome 2026-07-23 |
|------|-------|-------------------|---------------------|
| M0 | Freeze baseline logs (menu-fast) | md5 + counts B green, A red | ✅ B green / A red |
| M1 | Discriminate CC9D0: empty list vs dead f4 vs parent walk | one hypothesis left | ✅ H1 primary (+H3) |
| M2 | Product construction fidelity | attach full **and** f4 progress **or** leave tick list | ✅ lista; **f4/flip residual** |
| M3 | Unblock post-B71 frame loop (flip≥1 after R_Perm) | Gate A partial green | ❌ A still RED; root class |
| M4 | Pad + menu FSM arm | cellPadGetData>0 or menu state | ❌ pad=0; no menu |
| M5 | Document Gate A + update INDEX; no theater | notes + INDEX; playable≠yes | ✅ **A RED** documentado |

**Track M diagnostic closed; Gate A not green.** M3+ R1–R12 closed diagnosis (ICALL/HEAP40/issuer/schedule/OPD/spin); next work = **TYPE15 f4/work** (present re-arm), not “menu done”.

Residual plan: [`2026-07-23-m3plus-postwad-present.md`](2026-07-23-m3plus-postwad-present.md) (R1–R12 ✅ diag; fix re-arm ⬜).

---

## Track S / G / U / T / P — entry only

### Track S — shaders natural

- **Entry when:** post-B71 **SetFlip_after_R_Perm≥1** (or equivalent present path) **or** typemap walk proven independent of flip without theater.
- **Blocked-by (2026-07-23):** Gate A still RED — no present re-arm after thr (`gate-a-verdict.md` M3+ R1–R12).
- **Aceite final:** `[LDRSH]` N>0 natural; SHADERSRC non-empty; set_shader hash ≠ demo.
- **Do not:** CRC bypass as “done”.

### Track G — Metal

- M0/M1 already GREEN (overlay + draw path).
- Next: M2 MSL → M3 bind_texture → M5/M6 state → M7 RT present.
- Aceite de conteúdo só com draws do guest (Track S/M), não demo triangle.

### Track U — SPU

- Tables spu0–5 registered; ~90 TODO residual opcodes.
- Deep: SPURS job DMA/LFQueue TDD; promote spu2/3 only with smoke.
- Aceite: job real complete com proof, não só register.

### Track T — Xenon tooling

- Full plan: [`2026-07-23-xenonrecomp-lessons-recomp-tooling.md`](2026-07-23-xenonrecomp-lessons-recomp-tooling.md)
- **A1 done** (`aa0a652` recomp_config TOML); next A2 midasm etc. in separate session.
- Rule: every phase that touches lifter/runtime → Gate B smoke.

### Track P — playable polish

- After **menu UI real** (Gate A green + pad/menu signal): New Game path, first level load, pad mapping, audio continuity.
- **Not entered:** M5 + M3+ R1–R3 closed with Gate A **RED** / playable=NO — no Track P until present+menu.

---

## Theater blacklist (review every PR)

- [ ] No `PARK` counted as StartSeq success
- [ ] No CE03C soft-park inject claiming 2nd movie done
- [ ] No identical baseline/after md5 for “fix”
- [ ] No FORCE-only StartSeq without async path proof
- [ ] No CC9D0-SKIP alone claimed as menu
- [ ] No FlashUI sparse as menu without thr/flip correlation
- [ ] No HEAP40 / ICALL skip as permanent “fix” for Gate A
- [ ] No forged flip HLE for SetFlip_after_R_Perm

---

## Suggested schedule

| Window | Focus |
|--------|--------|
| **Now** | **TYPE15 f4 writers / product work** (R12 parent mapeado; residual M2 nodes≠UI; present re-arm) |
| +parallel | Track T A2+ (midasm) se 2ª sessão; Track G Metal M2+ |
| After flip ≥1 pós-R_Perm | Track S typemap natural + pad re-smoke |
| After menu UI | Track P + U deep jobs |
| Ongoing | Migrate patch_*.py → midasm (T Phase G) |

---

## Execution handoff

**Track M M0–M5 + M3+ R1–R12 diagnostic closed (2026-07-23).** Gate B GREEN; Gate A **RED**; playable=NO.  
Veredito: `gow2-recomp/notes/2026-07-23-gate-a-verdict.md`  
Satellite M: `docs/superpowers/plans/2026-07-23-menu-type15-frame-loop.md`  
Residual M3+: `docs/superpowers/plans/2026-07-23-m3plus-postwad-present.md`  
R11 note: `gow2-recomp/notes/2026-07-23-r11-opd-sched.md`  
R12 note: `gow2-recomp/notes/2026-07-23-r12-postthr-spin.md`

Tooling paralelo:  
`docs/superpowers/plans/2026-07-23-xenonrecomp-lessons-recomp-tooling.md`

**Default next:** RE/fix **TYPE15 f4 writers / product work** (R12: parent `2B2DD0→B951C` mapeado, f4=0 residual M2; R11: OPD B94EC/B6150 DEAD) — not Track S as if menu were done; not CE03C wait theater.  
**If tooling only:** Track T A2+.

---

## Changelog deste INDEX

| Data | Mudança |
|------|---------|
| 2026-07-23 | INDEX mestre: wall activo = post-thr TYPE15/CC9D0/frame loop; 2b+sanitize ✅; tracks M/S/G/U/T/P |
| 2026-07-23 | **Track M fecho M0–M5:** Gate A **RED** honesto; M2 CLOSE-PRESERVE shipped; wall next = post-WAD present (não empty-list) |
| 2026-07-23 | **M3+ R1–R3 fecho:** ICALL freelist classificado; H3 HEAP40 DEAD; issuer=`func_00017ACC` only; TIMEOUT=120 after=0; wall next = **present re-arm**; R4 docs |
| 2026-07-23 | **M3+ R5–R8:** 17ACC DEAD; issuer=`2EFD60`; hot loop `CDBA4…CDD88` ~41k post=0; CE0A0 exit guards; wall next = R9 frame path pós thr |
| 2026-07-23 | **M3+ R9:** MENUPRESENT H1 — schedule parents post=0; menu path never; 2C0508 pre-only; wall next = R10 who schedules B61F4/2B21/25C838 |
| 2026-07-23 | **M3+ R10:** SCHEDARM — B6150/B94* tot=0; 25C838=1 36A598=0; CE03C wait H3 DEAD sole; wall next = R11 OPD arm + residual B71 |
| 2026-07-23 | **M3+ R11:** OPD orphan B6150; B94EC/B61F4 no OPD; POSTTHR only CC9D0+2B2DD0+B951C; wall next = R12 leave TYPE15 spin |
| 2026-07-23 | **M3+ R12:** 2B2DD0 parent of CC9D0 (2:1:1); B951C ≠ present; f4=0 residual M2; wall next = **TYPE15 f4 writers** |
