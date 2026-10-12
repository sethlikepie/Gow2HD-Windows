---
phase: 07-gate-de-aceite-que-detecta-a-cadeia
plan: 02
subsystem: testing
tags: [bash, acceptance-gate, gow2, boot-chain]

requires:
  - phase: 07-gate-de-aceite-que-detecta-a-cadeia
    provides: "smoke_chain_gate.sh (Plano 07-01) -- consumido aqui como PERNA 4"
provides:
  - "accept_relift.sh com 4 pernas -- promocoes que nao chegam ao thr_auto_load sao rejeitadas por defeito, nao so por PERNA 1/2/3"
affects: [08-correcao-na-origem]

tech-stack:
  added: []
  patterns:
    - "PERNA nova reutiliza o binario ja construido por uma perna anterior (nunca um segundo build a partir do mesmo LIFT_REL)"
    - "leitura do resultado de uma perna sempre via TSV explicito (awk -F'\\t'), nunca o rc bruto do subprocesso de forma opaca"

key-files:
  modified:
    - /Users/andrebrumcortezferreira/Documents/PESSOAL/gow2-recomp/accept_relift.sh

key-decisions:
  - "PERNA 4 nao dispara segundo build -- reutiliza $REPO/boot_gow2_${TAG#recomp_macos_} que a PERNA 1 acabou de construir, mesma convencao de nomes de smoke_relift_equiv.sh"
  - "rc final passa a ser AND das QUATRO pernas + contadores bloqueantes (antes: tres); as quatro correm sempre, nunca aborta cedo"

requirements-completed: [GATE-03]

coverage:
  - id: D1
    description: "accept_relift.sh tem uma quarta perna (chain gate) que reutiliza o binario da PERNA 1 sem segundo rebuild, le o TSV (nunca o rc bruto), e o rc final depende dela"
    requirement: "GATE-03"
    verification:
      - kind: unit
        ref: "grep -q 'PERNA 4' e grep -q 'smoke_chain_gate.sh' em accept_relift.sh; bash -n accept_relift.sh"
        status: pass
    human_judgment: false
  - id: D2
    description: "accept_relift.sh recomp_macos_v2 (a lift de producao actual, partida) sai rc!=0 e a PERNA 4 nomeia o elo que falhou"
    requirement: "GATE-03"
    verification:
      - kind: e2e
        ref: "./accept_relift.sh recomp_macos_v2 (corrida real completa, 6 runs por perna) -- rc=1, PERNA 4 FAIL elo_stopped='2o movie (StartSeq)'"
        status: pass
    human_judgment: false
  - id: D3
    description: "PERNAS 1-3 + CONTADORES continuam a imprimir no MESMO formato de antes -- sem regressao do entregavel da Fase 5 (D-5.1/D-5.2)"
    requirement: "GATE-03"
    verification:
      - kind: e2e
        ref: "mesma corrida real: PERNA 1 PASS (OK=6/6), PERNA 2 PASS (rc=0), PERNA 3 FAIL (pre-existente, nao causado por esta mudanca), CONTADORES PASS"
        status: pass
    human_judgment: false

duration: 35min
completed: 2026-07-30
status: complete
---

# Phase 07 Plan 02: PERNA 4 em accept_relift.sh (GATE-03) Summary

**`accept_relift.sh` passa a ter quatro pernas -- a nova PERNA 4 chama `smoke_chain_gate.sh` sobre o binário já construído pela PERNA 1 e rejeita, hoje, o lift de produção (`recomp_macos_v2`) que não chega ao `thr_auto_load`.**

## Performance

- **Duration:** ~35min (dominado pela corrida real de aceite: ~13min de execução do próprio `accept_relift.sh`)
- **Tasks:** 2/2 completos
- **Files modified:** 1 (`accept_relift.sh`)

## Accomplishments

- `accept_relift.sh` ganhou uma secção "PERNA 4: smoke_chain_gate.sh
  (cadeia)" logo a seguir à PERNA 1, antes da PERNA 2. Reutiliza o MESMO
  binário que a PERNA 1 acabou de construir (`$REPO/boot_gow2_${TAG#recomp_macos_}`,
  mesma convenção de `smoke_relift_equiv.sh`) — nunca dispara um segundo
  build a partir do mesmo `LIFT_REL`. Lê o TSV produzido (nunca o rc bruto
  de forma opaca) e nomeia o `elo_stopped` mais frequente entre as
  corridas que falharam.
- Cabeçalho actualizado ("AS TRÊS PERNAS" → "AS QUATRO PERNAS", com a
  descrição da PERNA 4) e o cálculo final de rc passa a ser AND das
  **quatro** pernas + contadores bloqueantes
  (`leg1_rc==0 && leg2_rc==0 && leg3_pass==1 && leg4_rc==0 && counters_pass==1`),
  preservando a disciplina de as quatro pernas correrem sempre (nunca
  aborta cedo) e o relatório final imprimir sempre o estado das quatro.
- **Prova real de GATE-03**: `./accept_relift.sh recomp_macos_v2` (a lift
  de que `boot_gow2` de produção foi de facto construído, RUNS=6 default,
  corrida completa e real — sem cortar corridas). Resultado:

  ```
  PERNA 1 (smoke)          PASS   (OK=6/6)
  PERNA 2 (verify_lift)    PASS   (rc=0)
  PERNA 3 (apply_patches)  FAIL   (NO-MATCH=0 FAILED-fora-PROBE=2, UNVERIFIED=64 informativo)
  PERNA 4 (chain gate)     FAIL   (OK=0/6, elo_stopped mais frequente: 2o movie (StartSeq))
  CONTADORES (D-5.2)       PASS   (imp_modules/imp_imports/orfaos bloqueantes)

  REJEITADO: rc=1
  ```

  A **PERNA 4 nomeia exactamente o elo esperado** ("2o movie (StartSeq)"),
  consistente com 07-CONTEXT.md (`StartSeq=1, thr_end=0` para esta lift) e
  com a prova do Plano 07-01 contra o binário `boot_gow2` (mesmo lift).

## Task Commits

1. **Task 1: PERNA 4 -- accept_relift.sh chama smoke_chain_gate.sh e nomeia o elo que falhou** - `c6c10d2` (feat)
2. **Task 2: Prova de GATE-03 -- rejeicao real do lift de producao actual + sem regressao** - _sem commit novo_ (ver "Deviations from Plan")

## Files Created/Modified
- `accept_relift.sh` (gow2-recomp) - quarta perna (chain gate), cabeçalho e cálculo de rc actualizados

## Decisions Made
- PERNA 4 nunca dispara um segundo build — reutiliza o binário já construído pela PERNA 1.
- Leitura do resultado da PERNA 4 sempre via TSV (`awk -F'\t'`), nunca o rc bruto de forma opaca — mesmo padrão já usado nas pernas 1/3.

## Deviations from Plan

### Nota de processo (não é uma correção de bug)

O Task 2 do plano pedia para rodar `./accept_relift.sh recomp_macos_v2`,
confirmar os quatro veredictos, e "commit em gow2-recomp ... de
accept_relift.sh". A corrida real (Task 2) passou **sem exigir nenhuma
alteração adicional** ao código escrito na Task 1 — a implementação já
estava correta na primeira tentativa (confirmado por `bash -n` e pela
corrida real completa). Por isso não há um segundo commit de código: o
commit `c6c10d2` (Task 1) já contém a implementação completa e testada; o
registo da corrida de prova (Task 2) fica neste SUMMARY, que é o "próprio
commit" a que o plano se refere (o output completo do `accept_relift.sh`
está reproduzido acima, na íntegra, com os quatro veredictos).

**Achado pré-existente, fora do escopo desta task**: a PERNA 3
(`apply_all_patches.sh --status`) reportou `FAILED-fora-PROBE=2` nesta
corrida — dois patches funcionais falharam fora da classe PROBE. Isto é
**pré-existente** (não introduzido pelas mudanças da Fase 7: nem `07-01`
nem `07-03` tocam em patches fora da classe PROBE) e já era esperado dado
que `recomp_macos_v2` é o lift de produção conhecido como quebrado nesta
sessão. Registado aqui para não ficar escondido, mas fora do escopo de
GATE-03 (que é sobre a PERNA 4, não sobre corrigir a PERNA 3). Fica para a
Fase 8.

---

**Total deviations:** 0 de código (implementação correta à primeira); 1 nota de processo (commit único em vez de dois) + 1 achado pré-existente documentado, não corrigido (fora de escopo).
**Impact on plan:** nenhum -- GATE-03 está provado, PERNAS 1/2/CONTADORES sem regressão, PERNA 3 tem uma falha pré-existente e não relacionada.

## Issues Encountered
A corrida real de `accept_relift.sh recomp_macos_v2` demorou ~13 minutos
(PERNA 1: 6 builds+corridas reais; PERNA 4: 6 corridas de até 90s cada,
já que o binário nunca sai antes do timeout; PERNA 2/3: alguns minutos
adicionais). Executada em segundo plano com polling periódico em vez de um
único comando bloqueante, para respeitar o limite de tempo por chamada de
ferramenta.

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness

`accept_relift.sh` agora rejeita, por defeito, qualquer promoção cuja
cadeia de boot não chegue ao `thr_auto_load` -- o buraco que promoveu
`boot_gow2` a 29 Jul está fechado. A Fase 8 pode usar `accept_relift.sh`
como gate de promoção assim que a causa da regressão for corrigida: uma
promoção só passará quando a PERNA 4 também reportar `PASS`.

---
*Phase: 07-gate-de-aceite-que-detecta-a-cadeia*
*Completed: 2026-07-30*

## Self-Check: PASSED

- FOUND: accept_relift.sh
- FOUND: commit c6c10d2
- FOUND: "PERNA 4" text in accept_relift.sh
