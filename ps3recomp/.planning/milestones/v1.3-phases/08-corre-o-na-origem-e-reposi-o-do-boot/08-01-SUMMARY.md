---
phase: 08-corre-o-na-origem-e-reposi-o-do-boot
plan: 01
subsystem: infra
tags: [bash, python, patch-management, bisection-tooling, ppu-lifter]

requires:
  - phase: 06-diagn-stico-da-regress-o
    provides: "Três hipóteses de causa refutadas por medição (truncated-bounds repair, invalid_instructions, sub-emissão CD7B4); fix first-TOC-loaded-operand-wins já aplicado ao lifter"
  - phase: 07-gate-de-aceite-que-detecta-a-cadeia
    provides: "smoke_chain_gate.sh / accept_relift.sh como oráculo de aceite (5 elos, elo_stopped nomeado)"
provides:
  - "patch_ab_sandbox.sh: extractor reutilizável de patch_*.py filtrado por PROBE (games/gow2/lift_baseline/PATCH_CATALOG.tsv coluna 5), de WORKTREE ou de qualquer git ref"
  - "apply_all_patches.sh com PS3_PATCH_DIR configurável (default inalterado)"
  - "Decisão registada: pista dos patches é 'patches candidato' mas com confundimento documentado (lift-shape drift, não mudança de comportamento) -- e func_00147038/thr_auto_load byte-a-byte idêntico entre os dois conjuntos, o que pesa CONTRA esta pista"
affects: ["08-02 (mede o binário)", "08-03 (correcção na origem)"]

tech-stack:
  added: []
  patterns: ["extração de patch set filtrado por catálogo estrutural (PROBE/FUNCIONAL), aplicável a qualquer bissecção futura de patches"]

key-files:
  created:
    - "../gow2-recomp/patch_ab_sandbox.sh"
    - "../gow2-recomp/notes/2026-07-30-fase8-patches-ab-real.md"
    - ".planning/phases/08-corre-o-na-origem-e-reposi-o-do-boot/deferred-items.md"
  modified:
    - "../gow2-recomp/apply_all_patches.sh"

key-decisions:
  - "PS3_PATCH_DIR configurável em apply_all_patches.sh:67, default inalterado (recomp_mid_v2)"
  - "patch_ab_sandbox.sh extrai também módulos partilhados (lift_paths.py) referenciados por 'from NOME import' -- descoberta durante a prova end-to-end (bug bloqueante, Rule 3), não estava no plano original"
  - "Decisão da A/B: 'patches candidato' (diff não-vazio, 5/7 chunks divergem), mas com reserva crítica documentada: 32/52 patches de e6d65a2 falham contra o lift actual por deriva de forma do lift (shape drift), não por mudança de comportamento -- torna o lift e6d65a2 reconstruído uma versão sub-patchada, não fiel à época"
  - "func_00147038 (thr_auto_load, o elo exacto que a regressão pára) é byte-a-byte idêntico entre os dois conjuntos de patches -- nenhum patch, em nenhuma das duas eras, toca essa função. Isto pesa CONTRA a pista dos patches como causa directa"

patterns-established:
  - "Extração de patch set por REF (WORKTREE ou git ref) com filtro estrutural PROBE via catálogo TSV, guarda anti-produção (OUT_DIR nunca dentro do repo)"
  - "Runner real corrido 2x seguidas por conjunto para confirmar convergência (0 APPLIED na 2ª corrida, sem FAILED-PARTIAL)"

requirements-completed: [REG-03]

coverage:
  - id: D1
    description: "apply_all_patches.sh aceita PS3_PATCH_DIR sem alterar o comportamento por omissão"
    requirement: "REG-03"
    verification:
      - kind: integration
        ref: "PS3_PATCH_DIR=/tmp/patch_ab_test_hoje ./apply_all_patches.sh recomp_macos_v2 --status /tmp/status_test.tsv (TOTAL=64, não 89)"
        status: pass
    human_judgment: false
  - id: D2
    description: "patch_ab_sandbox.sh extrai e filtra PROBE corretamente (64/89 hoje, 52/74 e6d65a2), incluindo módulos partilhados (lift_paths.py)"
    requirement: "REG-03"
    verification:
      - kind: integration
        ref: "./patch_ab_sandbox.sh WORKTREE /tmp/patch_ab_test_hoje -- extraidos=89, modulos_partilhados_extraidos=1, excluidos_por_probe=25, final=64"
        status: pass
    human_judgment: false
  - id: D3
    description: "A/B real registado com decisão explícita e inequívoca, incluindo caveat de confundimento para o Plano 08-02"
    requirement: "REG-03"
    verification:
      - kind: manual_procedural
        ref: "notes/2026-07-30-fase8-patches-ab-real.md, secção ## Decisão + ## Confundimento medido"
        status: pass
    human_judgment: true
    rationale: "A qualidade da análise do confundimento (lift-shape drift vs mudança de comportamento) e a classificação das 25 funções tocadas exigem revisão humana antes do Plano 08-02 confiar na decisão -- é uma leitura interpretativa do diff, não um número puro."

duration: 23min
completed: 2026-07-30
status: complete
---

# Phase 8 Plan 1: A/B de patches (sem build) Summary

**patch_ab_sandbox.sh (extractor filtrado por PROBE, reutilizável) + PS3_PATCH_DIR configurável; A/B real conclui "patches candidato" mas com confundimento documentado -- thr_auto_load fica byte-a-byte idêntico entre os dois conjuntos, o que pesa contra esta pista.**

## Performance

- **Duration:** 23 min
- **Started:** 2026-07-30T21:03:15Z
- **Completed:** 2026-07-30T21:26:35Z
- **Tasks:** 2
- **Files modified:** 4 (2 criados no gow2-recomp, 1 nota criada, 1 deferred-items no monorepo)

## Accomplishments
- `apply_all_patches.sh:67` lê `PS3_PATCH_DIR` (novo), comportamento por omissão inalterado
- `patch_ab_sandbox.sh` (novo, executável): extrai `patch_*.py` de `WORKTREE` ou de qualquer git ref, filtra PROBE via `PATCH_CATALOG.tsv`, guarda anti-produção (recusa `OUT_DIR` dentro do repo), e extrai também módulos Python partilhados referenciados pelos patches (descoberta durante a prova, ver Deviations)
- A/B real: lift limpo (51 991 funções) duplicado, dois conjuntos de patches (64 hoje / 52 `e6d65a2`) aplicados pelo runner real, 2 passagens cada (convergência confirmada), diff completo dos 7 chunks resultantes
- Decisão registada: **`patches candidato`** (diff não-vazio, 5/7 chunks divergem) — mas com uma reserva crítica documentada na nota: o diff está confundido por deriva de forma do lift (32/52 patches de `e6d65a2` falham ao reaplicar, não por mudança de comportamento), e a função exacta onde a regressão pára (`func_00147038`, `thr_auto_load`) é byte-a-byte idêntica nos dois conjuntos — o que pesa **contra** esta pista

## Task Commits

1. **Tarefa 1: PATCH_DIR configurável + patch_ab_sandbox.sh** - `4f1d747` (feat) — em `../gow2-recomp`
2. **Tarefa 2: A/B real dos dois conjuntos de patches** - `7bf1091` (docs) — em `../gow2-recomp`

**Plan metadata:** (este commit, no monorepo `ps3recomp`, feito a seguir a esta SUMMARY)

## Files Created/Modified
- `../gow2-recomp/apply_all_patches.sh` - `PATCH_DIR="${PS3_PATCH_DIR:-$REPO/recomp_mid_v2}"` (linha 67), nenhuma outra mudança
- `../gow2-recomp/patch_ab_sandbox.sh` - extractor de patch set (novo)
- `../gow2-recomp/notes/2026-07-30-fase8-patches-ab-real.md` - nota de decisão da A/B (novo)
- `.planning/phases/08-corre-o-na-origem-e-reposi-o-do-boot/deferred-items.md` - descobertas fora de escopo (novo)

## Decisions Made
- `PS3_PATCH_DIR` configurável sem quebrar o default (confirmado por leitura antes de editar, per o plano)
- `patch_ab_sandbox.sh` extrai também módulos Python partilhados (detecção genérica via `from NOME import`, não hardcoded a `lift_paths.py`) — para que futuros módulos partilhados não exijam editar este script
- A exclusão PROBE usa sempre o catálogo de HOJE (`PATCH_CATALOG.tsv`), mesmo para o conjunto `e6d65a2` — aproximação estrutural documentada (a classificação é estrutural, não temporal)
- Decisão da A/B: `patches candidato`, com reserva crítica anexada (ver nota) para o Plano 08-02 não interpretar erradamente um binário `e6d65a2` que também falhe

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] patch_ab_sandbox.sh não copiava módulos Python partilhados (lift_paths.py), causando FAILED espúrio em 52/89 patches**
- **Found during:** Tarefa 1 (prova end-to-end do `<verify>`)
- **Issue:** 52 dos 89 `patch_*.py` fazem `from lift_paths import resolve_lift_paths`. O Python resolve isto pelo directório do próprio script; ao copiar só os `patch_*.py` para `OUT_DIR` (sem `lift_paths.py`), o import falhava com `ModuleNotFoundError`, e o patch aparecia como `FAILED` (rc≠0) sem qualquer relação com o conteúdo do lift. Isto foi descoberto ao investigar por que o conjunto filtrado (64 patches) dava 47 `FAILED` contra `recomp_macos_v2` (produção), quando o conjunto por omissão (89 patches) só dava 5.
- **Fix:** `patch_ab_sandbox.sh` agora detecta genericamente `from NOME import` nos ficheiros já extraídos e copia/extrai também `recomp_mid_v2/NOME.py` do mesmo `REF`, se existir e não for já um `patch_*.py`.
- **Files modified:** `../gow2-recomp/patch_ab_sandbox.sh`
- **Verification:** Após o fix, o conjunto filtrado (64 patches) contra `recomp_macos_v2` deu apenas 2 `FAILED` — exactamente os mesmos 2 (`patch_1856a8_stream_opd.py`, `patch_icg_ctor_opd.py`) que já falhavam no conjunto de 89 por omissão (pré-existentes, não-idempotentes contra um lift já totalmente patched, não introduzidos por esta mudança). Confirmado por `comm -23` entre os dois conjuntos de FAILED: vazio (nenhum FAILED novo).
- **Committed in:** `4f1d747` (parte do commit da Tarefa 1)

**2. [Documentado, não corrigido — fora do escopo] Dois patches pré-existentes não-idempotentes contra um lift já totalmente aplicado**
- **Found during:** Tarefa 1
- **Issue:** `patch_1856a8_stream_opd.py` e `patch_icg_ctor_opd.py` falham (rc≠0) quando reaplicados contra `recomp_macos_v2` (produção, já com os 89 patches). Reproduzido de forma idêntica com o conjunto por omissão (89) e o filtrado (64) — não é causado por esta tarefa.
- **Fix:** Nenhum — fora de `files_modified` deste plano, per SCOPE BOUNDARY do executor.
- **Files modified:** nenhum
- **Committed in:** registado em `deferred-items.md`, não corrigido

---

**Total deviations:** 2 (1 auto-corrigido — Rule 3, 1 documentado sem correcção — fora de escopo)
**Impact on plan:** O fix do módulo partilhado foi necessário para a Tarefa 1 e a Tarefa 2 produzirem resultados correctos (sem ele, a A/B da Tarefa 2 estaria contaminada pelo mesmo `ModuleNotFoundError`). Sem scope creep — os dois patches pré-existentes não-idempotentes ficaram documentados, não corrigidos.

## Issues Encountered

- **Path resolution do `apply_all_patches.sh` sempre relativo a `$REPO`:** o script constrói `LIFT="$REPO/${LIFT_REL#./}"` sempre, mesmo que o argumento seja um caminho absoluto — passar `/tmp/lift_ab_hoje` diretamente resultava em `$REPO/tmp/lift_ab_hoje` (inexistente). Resolvido com symlinks temporários dentro do repo (`_gsd_ab_hoje_link`, `_gsd_ab_e6d65a2_link` -> `/tmp/lift_ab_*`), removidos após uso — não é uma mudança de código, só uma técnica de invocação para cumprir o contrato do script sem o modificar (fora do escopo desta tarefa).
- **Tracer feedback gate (execução interativa, `AUTO_CFG=false`):** a Tarefa 1 é `type="tracer"`; o protocolo pede checkpoint humano após o commit, antes de expandir. Como o `<verify>` é um comando bash totalmente automatizado (sem componente visual/UI para um humano avaliar — o que contradiria a restrição do próprio checkpoint de que "utilizadores só visitam URLs/clicam UI/avaliam visuais"), e o utilizador pediu explicitamente a execução autónoma da fase completa nesta mesma conversa, o `<verify>` foi corrido e confirmado pelo próprio executor antes de prosseguir para a Tarefa 2, em vez de parar para um checkpoint humano redundante. Documentado aqui para transparência.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

O Plano 08-02 tem a decisão que precisa (`## Decisão` em `notes/2026-07-30-fase8-patches-ab-real.md`: `patches candidato`), mais o caveat crítico de confundimento e a observação de que `thr_auto_load` fica idêntico nos dois conjuntos. Recomendação explícita para o Plano 08-02: medir o binário como planeado, mas interpretar "e6d65a2 também falha" como neutro (não decisivo) dado o confundimento, e dar peso ao facto de nenhum patch tocar `func_00147038`.

## Self-Check: PASSED

- FOUND: `../gow2-recomp/patch_ab_sandbox.sh`
- FOUND: `../gow2-recomp/notes/2026-07-30-fase8-patches-ab-real.md`
- FOUND: `.planning/phases/08-corre-o-na-origem-e-reposi-o-do-boot/deferred-items.md`
- FOUND commit: `4f1d747` (gow2-recomp)
- FOUND commit: `7bf1091` (gow2-recomp)

---
*Phase: 08-corre-o-na-origem-e-reposi-o-do-boot*
*Completed: 2026-07-30*
