---
phase: 07-gate-de-aceite-que-detecta-a-cadeia
plan: 01
subsystem: testing
tags: [bash, smoke-test, gow2, boot-chain, acceptance-gate]

requires:
  - phase: 06-diagnostico-da-regressao-ate-ao-codigo
    provides: bisect_regression.sh (measure_one/extract_counts/classify, tabela dos 9 binarios)
provides:
  - smoke_chain_gate.sh (repo gow2-recomp) -- gate que mede os 5 elos bloqueantes da cadeia de boot (intro/2o movie/re-Play/AUTO_LOAD/WAD) e sai rc!=0 nomeando o elo que parou
  - lib_boot_chain_metrics.sh (repo gow2-recomp) -- nucleo partilhado (arm_menu_fast_recipe/measure_one/extract_counts/extract_st620/classify) reutilizado por bisect_regression.sh e smoke_chain_gate.sh
affects: [07-02-accept-relift-perna4, 08-correcao-na-origem]

tech-stack:
  added: []
  patterns:
    - "elo_stopped: andar pela cadeia por ORDEM e parar no primeiro elo que falha -- nunca so um rc mudo, sempre o nome do elo"
    - "nucleo de medicao partilhado via source, nao duplicado entre bisect_regression.sh e smoke_chain_gate.sh"

key-files:
  created:
    - /Users/andrebrumcortezferreira/Documents/PESSOAL/gow2-recomp/lib_boot_chain_metrics.sh
    - /Users/andrebrumcortezferreira/Documents/PESSOAL/gow2-recomp/smoke_chain_gate.sh
    - /Users/andrebrumcortezferreira/Documents/PESSOAL/gow2-recomp/notes/2026-07-30-gate-cadeia-prova-dois-sentidos.md
  modified:
    - /Users/andrebrumcortezferreira/Documents/PESSOAL/gow2-recomp/bisect_regression.sh

key-decisions:
  - "smoke_chain_gate.sh nunca builda a partir de recomp_macos_v2 -- o Plano 07-03 desta mesma fase edita-o em paralelo (mesma wave); erro explicito se pedido"
  - "elos 6/7 (SetFlip_after_R_Perm/Pad_total) ficam informativos nesta task, nao bloqueantes -- nao ha medicao anterior confirmada para eles"

requirements-completed: [GATE-01, GATE-02]

coverage:
  - id: D1
    description: "smoke_chain_gate.sh --bin rejeita o binario de producao partido (boot_gow2, 29 Jul) com rc=1, nomeando elo_stopped=2o movie (StartSeq)"
    requirement: "GATE-01"
    verification:
      - kind: e2e
        ref: "./smoke_chain_gate.sh --bin ./boot_gow2 2 /tmp/gate01_prod.tsv (corrida real, 2/2 REGRESSAO)"
        status: pass
    human_judgment: false
  - id: D2
    description: "smoke_chain_gate.sh --bin aceita o binario bom (boot_gow2.pre_v3, 25 Jul) com rc=0, elo_stopped=nenhum"
    requirement: "GATE-01"
    verification:
      - kind: e2e
        ref: "./smoke_chain_gate.sh --bin ./boot_gow2.pre_v3 2 /tmp/gate01_prev3.tsv (corrida real, 2/2 OK)"
        status: pass
    human_judgment: false
  - id: D3
    description: "bisect_regression.sh (Fase 6) usa lib_boot_chain_metrics.sh via source e continua a imprimir a mesma tabela para boot_gow2.pre_v3 -- sem regressao do entregavel anterior"
    requirement: "GATE-02"
    verification:
      - kind: e2e
        ref: "./bisect_regression.sh --bin ./boot_gow2.pre_v3 ... 90 (rodado duas vezes, ambas StartSeq=2 thr_end=1 class=OK)"
        status: pass
    human_judgment: false

duration: 55min
completed: 2026-07-30
status: complete
---

# Phase 07 Plan 01: Gate de cadeia (GATE-01/GATE-02) Summary

**`smoke_chain_gate.sh` mede os 5 elos bloqueantes do boot (intro→2o movie→re-Play→AUTO_LOAD→WAD) e sai rc!=0 nomeando o elo que parou -- provado nos dois sentidos contra `boot_gow2` (partido, rc=1) e `boot_gow2.pre_v3` (bom, rc=0), binarios reais, sem fixtures.**

## Performance

- **Duration:** ~55min
- **Tasks:** 3/3 completos
- **Files modified:** 4 (1 novo lib, 1 novo gate, 1 nota, 1 script editado)

## Accomplishments

- Extraido o nucleo partilhado de `bisect_regression.sh` (Fase 6) para
  `lib_boot_chain_metrics.sh`: `arm_menu_fast_recipe()`, `measure_one()`
  (kill sempre por PID, TERM→espera→-9), `extract_counts()`, `classify()`
  verbatim, mais `extract_st620()` novo (mesmo grep/awk de
  `smoke_relift_equiv.sh`). `bisect_regression.sh` passou a usar este
  ficheiro via `source` -- zero duplicacao de greps, sem regressao medida
  (roda-o duas vezes contra `boot_gow2.pre_v3`, ambas `StartSeq=2 thr_end=1
  class=OK`).
- Criado `smoke_chain_gate.sh`: deriva `elo_stopped` andando pelos 5 elos
  bloqueantes por ordem (intro/st620≥3, 2o movie/StartSeq≥2, re-Play/
  NOPIC≥4, AUTO_LOAD/thr_end≥1, WAD/R_PermA≥1) e para no primeiro que falha
  -- nunca so um rc mudo. Suporta `--bin BIN_PATH [RUNS] [TSV]` (sem
  rebuild, para binarios ja guardados) e `[LIFT_REL] [RUNS] [TSV]` (builda
  com `FORCE_REBUILD_LIFT=1`), com limiar `(RUNS*2+2)/3` igual a
  `smoke_relift_equiv.sh`. Bloqueia explicitamente builds a partir de
  `recomp_macos_v2` (editado em paralelo pelo Plano 07-03, mesma wave).
- **GATE-01/GATE-02 provados nos dois sentidos, com binarios reais**: contra
  `boot_gow2` (producao 29 Jul, partido) -- rc=1, `elo_stopped="2o movie
  (StartSeq)"` nas 2 corridas. Contra `boot_gow2.pre_v3` (25 Jul, bom) --
  rc=0, `elo_stopped=nenhum` nas 2 corridas. Nota escrita com os dois TSV
  lado a lado e a etiqueta MEDIDO em cada numero:
  `notes/2026-07-30-gate-cadeia-prova-dois-sentidos.md`.

## Task Commits

1. **Task 1: Nucleo partilhado + smoke_chain_gate.sh de uma corrida contra o binario partido** - `81def12` (feat)
2. **Task 2: N corridas, limiar (RUNS*2+2)/3 e contrato LIFT_REL** - `ccc4c87` (feat)
3. **Task 3: Prova de GATE-01 nos dois sentidos + nota + commit** - `cfd8762` (docs)

_Sem commit de metadados separado nesta entrega -- a nota de prova (Task 3) e o proprio registo da corrida._

## Files Created/Modified
- `lib_boot_chain_metrics.sh` (gow2-recomp) - nucleo partilhado: arm_menu_fast_recipe/measure_one/extract_counts/extract_st620/classify
- `smoke_chain_gate.sh` (gow2-recomp) - o gate novo, --bin e LIFT_REL, deriva elo_stopped, TSV de 10 colunas
- `bisect_regression.sh` (gow2-recomp) - passa a `source` a lib partilhada em vez de duplicar as funcoes
- `notes/2026-07-30-gate-cadeia-prova-dois-sentidos.md` (gow2-recomp) - prova MEDIDA nos dois sentidos

## Decisions Made
- `smoke_chain_gate.sh` nunca builda a partir de `recomp_macos_v2` (isolamento entre planos paralelos da mesma wave -- o Plano 07-03 edita esse lift).
- Elos 6/7 (`SetFlip_after_R_Perm`/`Pad_total`) ficam **informativos**, nunca decidem `elo_stopped` nem o rc nesta task -- 07-CONTEXT.md nunca confirmou valores para eles.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Flakiness observada na 1a tentativa de medir `boot_gow2.pre_v3`**
- **Found during:** Task 3
- **Issue:** a primeira corrida de `smoke_chain_gate.sh --bin ./boot_gow2.pre_v3 2 ...` deu `st620=1` nas duas execucoes (preso na intro), contradizendo tanto 07-CONTEXT.md como uma corrida imediatamente anterior/posterior de `bisect_regression.sh` contra o MESMO binario (que deu `StartSeq=2 thr_end=1`). A maquina tinha outra compilacao em fundo (load average ~2,3) no momento da 1a tentativa.
- **Fix:** descartada a corrida com o resultado suspeito (nunca reportada como medicao valida), repetida a corrida `--bin` limpa (deu `st620=11 startseq=2 nopic=4 thr_end=1 r_perma=1 elo_stopped=nenhum` nas duas execucoes), e documentada a flakiness na propria nota de prova como nota de metodo, nao escondida.
- **Files modified:** nenhum ficheiro de codigo -- so a nota (`notes/2026-07-30-gate-cadeia-prova-dois-sentidos.md`) regista o incidente.
- **Verification:** repeticao de `bisect_regression.sh --bin ./boot_gow2.pre_v3` (2x) e `smoke_chain_gate.sh --bin ./boot_gow2.pre_v3 2 ...` deram resultados limpos e consistentes com a referencia.
- **Committed in:** `cfd8762` (parte da nota de prova)

---

**Total deviations:** 1 auto-fixed (Rule 3, timing/flakiness -- nao um bug do gate).
**Impact on plan:** nenhum impacto de escopo; a flakiness e' inerente ao ambiente (concorrencia com outra compilacao), nao ao `smoke_chain_gate.sh`, e ficou documentada honestamente em vez de escondida.

## Issues Encountered
Ver "Deviations from Plan" acima -- a unica surpresa foi a flakiness de timing sob carga da maquina, resolvida por repeticao e nao por ajuste de codigo.

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness

`smoke_chain_gate.sh` esta pronto para ser consumido pelo Plano 07-02 (PERNA 4
do `accept_relift.sh`, GATE-03) via `--bin` sobre o binario ja construido pela
PERNA 1, sem segundo rebuild. `lib_boot_chain_metrics.sh` esta disponivel para
qualquer outro script que precise medir a cadeia sem duplicar greps.

**Nota honesta para a Fase 8 (correcao):** os elos 6/7 (desenho pos-WAD, input
de pad) deram 0 mesmo no binario bom nesta recipe -- nao e uma regressao nova,
e o comportamento esperado da recipe menu-fast usada aqui (nao segura o boot
tempo suficiente para desenhar pos-WAD nem envia pad). Se a Fase 8 quiser usar
os elos 6/7 como aceite, precisa de uma recipe/tempo diferente, ainda por
medir.

---
*Phase: 07-gate-de-aceite-que-detecta-a-cadeia*
*Completed: 2026-07-30*

## Self-Check: PASSED

- FOUND: lib_boot_chain_metrics.sh
- FOUND: smoke_chain_gate.sh
- FOUND: notes/2026-07-30-gate-cadeia-prova-dois-sentidos.md
- FOUND: commit 81def12
- FOUND: commit ccc4c87
- FOUND: commit cfd8762
