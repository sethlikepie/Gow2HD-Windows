# Plan 04 — Content / intro / WAD (W13–W15) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: subagent-driven-development or executing-plans.

## Status 2026-07-20 late — ⏳ À ESPERA DO 4.EOS Task 7

| Pré-condição | Estado |
|--------------|--------|
| Boot / M2 (planos 01–02) | ✅ |
| Open FIOS + poll done (plano 4.0) | ✅ st620 1→3 |
| EOS / f744 / 3→5→11 (plano EOS Task 7) | ⏳ **bloqueia aceite WAD da intro natural** |
| Tasks 1–6 deste plano | ⬜ em geral não executadas no Mac pós-merge |

Não começar smokes de WAD da intro natural enquanto EOS Task 7 não classificar 3→5 vs 3→0. Recipes intro-skip / FORCE no Win continuam o caminho de paridade documentado.

---

**Goal:** O GoW2 recompilado **abre o pipeline de assets** (intro FSM → WADs `R_LglScA` / `R_PermA`) de forma reprodutível no host (Win primeiro se Mac RSX ainda fraco), com métricas e smokes verdes.

**Architecture:** VFS (`PS3_VFS_ROOT` → `extracted/USRDIR`) + psarc + FIOS/dearchive (SPU spu1 e/ou `lfq_host_inflate`) + state machine de intro (patches OPD/type-map em `recomp_mid_v2`). Detalhe histórico Windows: plano `2026-07-14-gow2-intro-movie-fsm-wads.md`.

**Tech Stack:** `cellFs`/`sys_fs`, `tools/psarc_list.py`, `movie_hle` (Win), env `PS3_NOMOVIES` / intro-skip / `PS3_VDEC_ASYNC`, patches `patch_*.py`.

**Depends on:** Plano 02 (boot) + Plano 03 (dearchive/SPU) + **4.0 open/poll** + **4.EOS Task 7** (f744 / fase WAD).  
**Unblocks:** Plano 05 (RSX com conteúdo real).

## Global Constraints

- Nunca commitar WADs/movies/psarc.
- Patches de lift = scripts **idempotentes** reaplicáveis após re-lift.
- `PS3_VDEC_ASYNC=1` obrigatório no recipe intro-skip (armadilha já documentada).

---

### Task 1: VFS smoke (TDD)

**Files:**
- `runtime/ppu/ppu_fs.cpp` / `libs/filesystem/cellFs.c`
- Create: `gow2-recomp/smoke_vfs.sh`

- [ ] **Step 1: Vermelho — open de path conhecido**

```bash
# Boot com PS3_TRACE_FS=1 (adicionar se faltar)
# expected FAIL se gow2.psarc ou USRDIR errado:
# cellFsOpen fail / ENOENT em ficheiro que deve existir
export PS3_VFS_ROOT="$PWD/extracted/USRDIR"
# listar:
ls "$PS3_VFS_ROOT/gow2.psarc"
```

- [ ] **Step 2: Teste automatizado**

```bash
#!/usr/bin/env bash
# smoke_vfs.sh — 10s boot, exige pelo menos 1 open sucesso de psarc ou wad
grep -E 'cellFsOpen|sys_fs.*open' "$LOG" | grep -vi fail | head
# PASS se count opens OK >= 1 e zero "VFS root missing"
```

- [ ] **Step 3: Verde — path mapping**

Corrigir `host_path` / `PS3_VFS_ROOT` se opens falharem em `/dev_bdvd/...` ou `/app_home/...`.

- [ ] **Step 4: Commit**

---

### Task 2: Catálogo de patches (TDD de reaplicação)

**Files:**
- `gow2-recomp/recomp_mid_v2/patch_*.py`
- Create: `gow2-recomp/apply_all_patches.sh`
- Lift dir: `recomp_macos` ou `games/gow2` equivalent

- [ ] **Step 1: Vermelho — lift limpo sem patches**

```bash
# após re-lift fresco, boot pode falhar em OPD/type-map
# documentar marker de falha conhecido (ex. R_PermA gate)
```

- [ ] **Step 2: Script idempotente**

```bash
#!/usr/bin/env bash
set -euo pipefail
LIFT=${1:-recomp_macos}
for p in recomp_mid_v2/patch_*.py; do
  echo "== $p =="
  python3 "$p" --lift-dir "$LIFT" 2>/dev/null || python3 "$p"
done
```

- [ ] **Step 3: Verde — segunda corrida do script é no-op**

```bash
./apply_all_patches.sh recomp_macos
./apply_all_patches.sh recomp_macos   # sem diff inesperado
```

- [ ] **Step 4: Rebuild + smoke boot**

```bash
./build_macos.sh && ./smoke_boot_mac.sh 30
```

- [ ] **Step 5: Commit script**

---

### Task 3: Recipe intro-skip (TDD de env)

**Files:**
- Create: `gow2-recomp/rodar_gow2_intro_skip.sh` (POSIX de `rodar_gow2_intro_skip.cmd`)
- `env_gow2.sh`

- [ ] **Step 1: Vermelho — recipe incompleto**

Sem `PS3_VDEC_ASYNC=1`, WADs não abrem (documentado).

```bash
# run 60s sem VDEC_ASYNC → métrica WAD opens = 0
```

- [ ] **Step 2: Verde — recipe canónico**

```bash
#!/usr/bin/env bash
set -euo pipefail
. ./env_gow2.sh
# NÃO setar PS3_NOMOVIES=1 junto com intro movie path
unset PS3_NOMOVIES || true
export PS3_MOVIE_IO=1
export PS3_MOVIE_CACHE=../movie_cache   # se existir
export PS3_MOVIE_EOS=1
export PS3_VDEC_FORCE_SEQDONE_MS=8000
export PS3_VDEC_ASYNC=1
export PS3_RSX_BACKEND="${PS3_RSX_BACKEND:-sdl}"  # mac; d3d12 no win
export PS3_PAD_AUTOSTART=1
./boot_gow2 EBOOT.ELF
```

- [ ] **Step 3: Assert métricas**

```bash
# smoke_intro_wads.sh
# PASS se log contiver abertura R_LglScA ou R_PermA (string/path do jogo)
# ou contador instrumentado [WAD] open
```

- [ ] **Step 4: Commit**

---

### Task 4: Dearchive path (liga Plano 03)

**Files:**
- `lfq_host_inflate.c`, spu1 register
- Env: `PS3_SPU1=1` / `PS3_SPU_ALL=1`

- [ ] **Step 1: Vermelho — render wait eterno em dearchive**

```bash
# sample mostra thread em wait de op FIOS
# LFQueue depth stuck
```

- [ ] **Step 2: Verde — host inflate OU spu1 job**

Preferir provar **um** caminho de cada vez:

| Path | Env | PASS |
|------|-----|------|
| Host inflate | default + lfq fulfiller | `[LFQ] inflate ok` |
| SPU1 | `PS3_SPU1=1` | `[SPU] image=1 complete` |

- [ ] **Step 3: Commit** do path que passar

---

### Task 5: Smoke de pipeline de assets

**Files:**
- Portar `smoke_asset_pipeline.sh` para Mac se for Win-only
- Create: `gow2-recomp/smoke_content_mac.sh`

- [ ] **Step 1: Definir métricas mínimas**

```
opens_psarc >= 1
wad_related_opens >= 1   # quando intro-skip
no host crash
```

- [ ] **Step 2: Vermelho no Mac se conteúdo não flui**
- [ ] **Step 3: Verde no Win se Mac RSX bloquear visual** — documentar “content PASS, pixels no Plano 05”
- [ ] **Step 4: Commit**

---

### Task 6: Note de paridade Win ↔ Mac

```markdown
# Content parity
| Métrica | Windows | Mac |
|---------|---------|-----|
| R_LglScA open | | |
| R_PermA open | | |
| dearchive | | |
| crash | | |
```

- [ ] **Step 1: Preencher com runs reais**
- [ ] **Step 2: Commit note**

---

## Aceite do Plano 04

| Critério | Evidência |
|----------|-----------|
| VFS estável | smoke_vfs PASS |
| Patches reaplicáveis | apply_all_patches 2× |
| Intro-skip recipe | script + WAD open metric |
| Dearchive | inflate ou spu1 |
| Sem regressão boot | smoke_boot_mac PASS |

**Done → Plano 05** (mesmo com pixels só no Windows, se conteúdo flui).
