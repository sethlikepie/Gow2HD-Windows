---
phase: 06-diagn-stico-da-regress-o-at-ao-c-digo
plan: 01
subsystem: infra
tags: [bash, diagnostics, bisect, gow2, cellVdec]

requires: []
provides:
  - "gow2-recomp/bisect_regression.sh — ferramenta versionada de bissect por binários guardados"
  - "Tabela medida dos 9 binários guardados, veredito de fecho de janela (pre_v3 OK -> v3fix STRUCTURAL_FAIL -> v4 REGRESSAO)"
  - "Correcção descoberta pela própria ferramenta: boot_gow2.pre_v4 FALHA (invalida referência usada nas '2855 funções perdidas')"
affects: ["06-02", "07-gate", "08-fix"]

tech-stack:
  added: []
  patterns:
    - "Bissect por binários guardados (sem rebuild) em vez de bissect por commit — ~2min/corrida vs 30-40min"
    - "pgrep -x por nome exacto de processo em vez de pgrep -f (substring), para evitar falsos positivos de watchers de shell doutras sessões"

key-files:
  created:
    - "../gow2-recomp/bisect_regression.sh"
    - "../gow2-recomp/notes/2026-07-30-fase6-bisect-fecha-janela.md"
  modified: []

key-decisions:
  - "Classificação por 3 sinais: log_lines<200 -> STRUCTURAL_FAIL; senão thr_auto_load_end>=1 -> OK; senão REGRESSAO"
  - "grep -c 'StartSeq(handle=' em vez de grep -c 'StartSeq' — o segundo também conta linhas REPLAY-NOPIC e infla o número (6 vs 2 medido)"
  - "pgrep -x em vez de pgrep -f no relatório de órfãos, corrigido a meio da execução após o coordenador reportar 5 falsos positivos"

requirements-completed: [DIAG-01, REG-01]

coverage:
  - id: D1
    description: "Um único comando (./bisect_regression.sh) corre a recipe menu-fast Metal contra binários guardados e imprime/grava StartSeq, thr_auto_load end, R_PermA, REPLAY-NOPIC e linhas por binário"
    requirement: "DIAG-01"
    verification:
      - kind: manual_procedural
        ref: "./bisect_regression.sh --bin boot_gow2.pre_v3 -> OK; ./bisect_regression.sh --bin boot_gow2_v4 -> REGRESSAO; ./bisect_regression.sh (modo lista) -> tabela de 9 linhas em /tmp/bisect_regression_full_20260730_122956.tsv"
        status: pass
    human_judgment: false
  - id: D2
    description: "A janela fecha-se em boot_gow2.pre_v3 (OK) -> boot_gow2_v4 (REGRESSAO), medido pela ferramenta"
    requirement: "REG-01"
    verification:
      - kind: manual_procedural
        ref: "notes/2026-07-30-fase6-bisect-fecha-janela.md — tabela + veredito"
        status: pass
    human_judgment: false

duration: 35min
completed: 2026-07-30
status: complete
---

# Phase 6 Plan 01: bisect_regression.sh — ferramenta versionada, fecha a janela e apanha um erro próprio

**Ferramenta de bissect por binários guardados (sem rebuild), commitada e reutilizável — e que, na primeira corrida completa sobre os 9 binários, descobriu que uma referência usada num diagnóstico anterior desta mesma sessão de planeamento estava do lado errado.**

## Performance

- **Duration:** ~35 min (Tarefa 1 ~10min incluindo dois runs de 90s; Tarefa 2 ~20min para o sweep completo de 9 binários + escrita da nota; fix do pgrep intercalado)
- **Tasks:** 2/2 completas
- **Files modified:** 2 criados (`bisect_regression.sh`, a nota de fecho de janela) + 1 fix subsequente no próprio script

## Accomplishments

- `bisect_regression.sh` criado, com dois modos (`--bin CAMINHO` e modo lista dos 9
  binários conhecidos), classificação automática (`STRUCTURAL_FAIL`/`OK`/`REGRESSAO`) e
  saída em tabela alinhada + TSV.
- Task 1 (tracer) provou os dois lados com binários reais: `boot_gow2.pre_v3` →
  `OK` (`startseq=2`, `thr_auto_load_end=1`, `r_perma_full=1`, `replay_nopic=4`);
  `boot_gow2_v4` → `REGRESSAO` (`startseq=1`, `thr_auto_load_end=0`) — reproduz
  exactamente os números já medidos à mão nesta sessão.
- Task 2 correu o sweep completo dos 9 binários (`~20min`, sem compilar nada). A tabela
  fecha a janela: `pre_v3` (OK) → `v3fix` (`STRUCTURAL_FAIL`, 76 linhas, não delimita) →
  `v4` (`REGRESSAO`). Achado extra: `boot_gow2.rdy0_ref` (24 Jul) e `boot_gow2.wip`
  (25 Jul 17:47) também são `OK`, alargando a fronteira "funciona" para trás no tempo.
- **A ferramenta pagou-se na primeira corrida sistemática**: revelou que
  `boot_gow2.pre_v4` (mesma mtime do `pre_v3` — backup por rename) na verdade **FALHA**
  (813 linhas, `startseq=0`). Isto invalidou a referência "lift que funciona" usada no
  diagnóstico das "2855 funções perdidas" de uma nota anterior desta sessão — ver
  `../gow2-recomp/notes/2026-07-30-CORRECCAO-as-2855-comparavam-dois-lifts-que-falham.md`
  (commit `61d57dd`, não desta tarefa, escrito pelo coordenador ao ler a tabela).

## Task Commits

Todos em `../gow2-recomp` (repo irmão, per CLAUDE.md — o checkout físico do GoW2 vive lá):

1. **Tarefa 1: bisect_regression.sh mede pre_v3 e v4** - `c8bb16a` (feat)
2. **Tarefa 2: tabela completa + nota de fecho de janela** - `c2fbbfe` (docs)

**Fix intercalado (deviation Rule 1):** `a51ed45` (fix) — `orfaos_pos_run` usava
`pgrep -f boot_gow2`, que casa substring de comando inteiro e apanhava watchers de shell
doutras sessões (5 falsos positivos medidos); trocado por `pgrep -x` por nome exacto de
processo (0 falsos positivos, confirmado).

## Files Created/Modified
- `../gow2-recomp/bisect_regression.sh` - ferramenta de bissect versionada (DIAG-01)
- `../gow2-recomp/notes/2026-07-30-fase6-bisect-fecha-janela.md` - tabela dos 9 binários + veredito (REG-01)

## Decisions Made
- Classificar por `log_lines < 200` antes de `thr_auto_load_end` — evita que um log
  curtíssimo (falha estrutural) seja lido como "REGRESSAO" quando na verdade nunca
  chegou a correr a recipe.
- `grep -c 'StartSeq(handle='` em vez de `grep -c 'StartSeq'` sozinho, porque o segundo
  também conta as linhas `REPLAY-NOPIC skip guest PICOUT (StartSeq_count=...)` e infla o
  número (medido: 6 vs 2 no mesmo log de `pre_v3`).
- `pgrep -x` em vez de `pgrep -f` para o relatório de órfãos pós-corrida — corrigido a
  meio da execução após o coordenador reportar o falso positivo (ver Deviations).

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] `pgrep -f` no relatório de órfãos apanhava watchers de shell**
- **Found during:** revisão do coordenador após o sweep completo da Tarefa 2
- **Issue:** `pgrep -f boot_gow2` compara contra a linha de comando inteira, não só o
  nome do processo — watchers de shell de sessões anteriores (loops `until ! pgrep -f
  boot_gow2...`) contêm a substring "boot_gow2" no próprio texto do comando e eram
  contados como órfãos, mesmo não sendo processos `boot_gow2*` reais. 5 falsos positivos
  medidos.
- **Fix:** substituído por um loop `pgrep -x "$bin_rel"` por cada um dos 9 nomes
  conhecidos — `-x` compara o nome exacto do processo (argv[0] basename), nunca casa um
  `zsh`/`bash` com texto "boot_gow2" no corpo do comando.
- **Files modified:** `../gow2-recomp/bisect_regression.sh`
- **Verification:** `pgrep -x boot_gow2 | wc -l` → 0; `pgrep -x boot_gow2.pre_v3 | wc -l`
  → 0, confirmado depois do fix, na mesma sessão em que `pgrep -f boot_gow2` continuava a
  dar 5.
- **Committed in:** `a51ed45`

---

**Total deviations:** 1 auto-fixed (Rule 1 - bug de precisão do relatório de órfãos)
**Impact on plan:** Não afecta a classificação OK/REGRESSAO/STRUCTURAL_FAIL (que usa
`log_lines`/`thr_auto_load_end`, não o `pgrep`); afecta só a linha informativa de órfãos
no fim do modo lista. Sem impacto nos aceites DIAG-01/REG-01.

## Issues Encountered
Nenhum bloqueio. O único ajuste foi o fix de precisão do `pgrep` acima, descoberto pelo
coordenador ao ler a saída do sweep completo — exactamente o tipo de erro que a
ferramenta (DIAG-01) existe para apanhar.

## Next Phase Readiness

`bisect_regression.sh` está pronto para o Plano 06-02 usar (`--bin` para medir binários
experimentais de reversão cirúrgica, se a Tarefa 2 desse plano correr). **Atenção**: a
tabela desta tarefa invalidou a referência "lift que funciona" que o Plano 06-02 ia usar
para as "2855 funções perdidas" — ver a errata do coordenador
(`../gow2-recomp/notes/2026-07-30-CORRECCAO-as-2855-comparavam-dois-lifts-que-falham.md`,
commit `61d57dd`) e o `06-02-PLAN.md`, que foi actualizado antes da execução para reflectir
o novo alvo (comportamento, não contagem de funções).

---
*Phase: 06-diagn-stico-da-regress-o-at-ao-c-digo*
*Completed: 2026-07-30*

## Self-Check: PASSED
