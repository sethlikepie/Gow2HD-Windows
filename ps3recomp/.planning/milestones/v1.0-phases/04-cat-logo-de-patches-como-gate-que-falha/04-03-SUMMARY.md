---
phase: 04-cat-logo-de-patches-como-gate-que-falha
plan: 03
subsystem: testing
tags: [lift-verification, gate, delta-baseline, python, bash, ppc, gow2]

requires:
  - phase: 03-cat-logo-de-patches-como-gate-que-falha
    provides: "verify_lift_baseline.sh (MANIFEST.tsv, 243/246 contra v3, opd-dispatch com deficit 36) e o achado de que lift_parity/audit_boundaries existiam mas nunca corriam"
provides:
  - "games/gow2/verify_lift.sh -- ponto de entrada unico que orquestra lift_parity.py, verify_lift_baseline.sh e audit_boundaries.py, rc = OR das 3 falhas"
  - "games/gow2/lift_baseline/baseline_delta.py -- comparador generico por delta (achado novo falha, achado resolvido nunca falha), testado por mutacao sintetica via subprocess"
  - "games/gow2/lift_baseline/PARITY_BASELINE.json e BOUNDS_BASELINE.json -- baselines congelados a partir de medicao REAL contra recomp_macos_v3 (77 e 180 ids), regeneraveis pelo comando na chave _regenerar"
affects: ["04-01 (PATCH_CATALOG.tsv)", "04-05 (apply_all_patches.sh vai chamar verify_lift.sh no fim, D-4.6)"]

tech-stack:
  added: []
  patterns:
    - "Gate por DELTA contra baseline congelado (nao por limiar absoluto) -- generalizavel a qualquer verificador que produza uma lista de achados opacos"
    - "IDs opacos derivados do JSON de saida de cada verificador (ea/signal para paridade; I1:a|b / I3:func para fronteiras), nunca inventados"

key-files:
  created:
    - games/gow2/verify_lift.sh
    - games/gow2/lift_baseline/baseline_delta.py
    - games/gow2/lift_baseline/test_baseline_delta.py
    - games/gow2/lift_baseline/PARITY_BASELINE.json
    - games/gow2/lift_baseline/BOUNDS_BASELINE.json
  modified: []

key-decisions:
  - "D-4.5 implementada tal como decidida: gate por delta, nunca por limiar absoluto; I2 (2689 achados) fica FORA do baseline/gate, so informativo"
  - "Baselines medidos e congelados NESTA task (nao copiados do objective do plano) -- confirmaram exactamente os numeros citados na sessao de planeamento: 77 (9 TRUNCATED + 18 CALL_MISMATCH RED, 50 YELLOW) e 180 (I3), I1=0"
  - "verify_lift.sh chama verify_lift_baseline.sh directamente (script INALTERADO); so lift_parity e audit_boundaries passam por baseline_delta.py"

patterns-established:
  - "baseline_delta.py CURRENT.json BASELINE.json --label NOME: interface reutilizavel para qualquer par (current medido agora, baseline congelado), formato {\"ids\": [...]}"

requirements-completed:
  - REQ-RDY0-5

coverage:
  - id: D1
    description: "baseline_delta.py -- comparador generico por delta, rc=0 em delta limpo/melhoria, rc=1 so em achado novo, rc=2 em erro de uso"
    requirement: "REQ-RDY0-5"
    verification:
      - kind: unit
        ref: "games/gow2/lift_baseline/test_baseline_delta.py (5 testes via subprocess, CLI real)"
        status: pass
    human_judgment: false
  - id: D2
    description: "verify_lift.sh orquestra lift_parity/verify_lift_baseline/audit_boundaries contra recomp_macos_v3, imprime cabecalho com path absoluto + sha256, rodape PASS/FAIL, rc honesto"
    requirement: "REQ-RDY0-5"
    verification:
      - kind: integration
        ref: "games/gow2/verify_lift.sh ../gow2-recomp/recomp_macos_v3 (corrido 2x, rc=1 identico -- MANIFEST FAIL pre-existente, lift_parity e audit_boundaries PASS)"
        status: pass
    human_judgment: false
  - id: D3
    description: "prova de que o gate DETECTA regressao (id removido do baseline -> NOVO, rc=1) e NAO dispara em melhoria (id extra no baseline -> RESOLVIDO, rc=0 nesse passo)"
    requirement: "REQ-RDY0-5"
    verification:
      - kind: manual_procedural
        ref: "injeccao manual em PARITY_BASELINE.json, corrida, restauro verificado por diff (ver secao Prova do Gate abaixo)"
        status: pass
    human_judgment: false

duration: 12min
completed: 2026-07-26
status: complete
---

# Phase 4 Plan 3: verify_lift.sh -- ponto de entrada unico por delta contra baselines congelados Summary

**`verify_lift.sh` orquestra lift_parity.py (--functions), audit_boundaries.py (I1/I3) e verify_lift_baseline.sh contra baselines congelados nesta task (77 e 180 achados reais contra recomp_macos_v3), falhando so por achado NOVO -- prova por injecao de que detecta regressao e nunca falha por melhoria.**

## Performance

- **Duration:** 12 min (18:17-18:29 America/New_York, dois commits d730b71/38a973 no intervalo)
- **Tasks:** 2/2 completas
- **Files modified:** 5 criados (nenhum modificado)

## Estado (factual, MEDIDO)

Antes desta task, medido em `04-CONTEXT.md`/`04-03-PLAN.md`:
```
audit_boundaries       invocada em 0 scripts .sh
lift_parity            invocada em 0 scripts .sh
apply_all_patches.sh   chama verify_lift_baseline: 0 vezes
```
Depois desta task: os 3 verificadores correm juntos por `verify_lift.sh`, com rc unico.
`apply_all_patches.sh` ainda nao chama `verify_lift.sh` -- isso e' D-4.6, Plano 04-05.

## Tarefa 1 -- baseline_delta.py (tracer, TDD)

**Comando:**
```
.venv/bin/python3 games/gow2/lift_baseline/test_baseline_delta.py
```
**Saida real:**
```
[PASS] teste 1: delta limpo -> rc=0, DELTA OK
[PASS] teste 2: achado novo (regressao) -> rc=1, cita o id exacto
[PASS] teste 3: achado resolvido (melhoria) -> rc=0, nunca falha
[PASS] teste 4: corpus grande (1000 ids) identico -> rc=0
[PASS] teste 5: ficheiro current inexistente -> rc=2, mensagem clara

5/5 testes [PASS]
rc=0
```
Cada teste invoca o script como CLI via `subprocess.run` (fim-a-fim do proprio binario, nao so das funcoes internas), com JSON sinteticos em `tempfile.TemporaryDirectory()`.

**Gate de tracer (feedback antes de expandir):** o `<verify>` da Task 1 (o proprio conjunto de 5 testes) foi corrido e confirmado 5/5 PASS antes de avancar para a Task 2 -- nenhuma camada nova foi construida sobre uma fatia nao provada.

**Commit:** `38a9739` -- `feat(04-03): baseline_delta.py -- comparador generico por delta, testado por mutacao`

## Tarefa 2 -- verify_lift.sh + baselines congelados

### Medicao real que gerou os baselines

```
$ .venv/bin/python3 tools/lift_parity.py ../gow2-recomp/EBOOT.ELF ../gow2-recomp/recomp_macos_v3 \
      --functions ../gow2-recomp/functions.json --limit 0 --json /tmp/parity_freeze.json
...
=== sinais de paridade lift vs ELF ===
  YELLOW  CALL_MISMATCH   50
  RED     CALL_MISMATCH   18
  RED     TRUNCATED       9
total: 77 sinais, 27 RED
```
```
$ .venv/bin/python3 tools/audit_boundaries.py ../gow2-recomp/functions.json --elf ../gow2-recomp/EBOOT.ELF \
      --json /tmp/bounds_freeze.json --max-report 3
[bounds] 20750 ranges, janela 0x00010000-0x0050C7E0
I1 sobreposicao de ranges                                          0
I2 lacuna que descodifica como codigo                           2689
I3 fim sem terminador e sem continuacao registada (classe 2550C8)    180
I4/I5  0  -- nao avaliado (sem --oracle, deferido)
```
Numeros IDENTICOS aos citados na sessao de planeamento (77/27/180/0) -- confirmados por remedicao nesta task, nao copiados.

Transformados em IDs opacos e congelados: `PARITY_BASELINE.json` (77 ids `"{ea}/{signal}"`), `BOUNDS_BASELINE.json` (180 ids `"I1:{a}|{b}"`/`"I3:{func}"`; I2 fica FORA do baseline, informativo). Cada JSON tem a chave `_regenerar` com o comando exacto usado.

```
$ python3 -c "import json; d=json.load(open('games/gow2/lift_baseline/PARITY_BASELINE.json')); print('parity_ids=', len(d['ids']))"
parity_ids= 77
$ python3 -c "import json; d=json.load(open('games/gow2/lift_baseline/BOUNDS_BASELINE.json')); print('bounds_ids=', len(d['ids']))"
bounds_ids= 180
```

### Corrida real contra recomp_macos_v3

**Comando:**
```
games/gow2/verify_lift.sh ../gow2-recomp/recomp_macos_v3; echo "rc=$?"
```
**Saida real (resumida, cabecalho + rodape; o meio tem os findings completos):**
```
== verify_lift.sh ==
LIFT:      /Users/.../PESSOAL/gow2-recomp/recomp_macos_v3
SHA256:    799cdeb31d7a2e17b601ab18fa17898d581251204f72ac8bd394b7ae5f7f9a16
ELF:       /Users/.../PESSOAL/gow2-recomp/EBOOT.ELF
FUNCTIONS: /Users/.../PESSOAL/gow2-recomp/functions.json

---- [lift_parity] ----
... (77 sinais, 27 RED, identico a medicao acima)
[lift_parity] current=77 baseline=77 novos=0 resolvidos=0
[lift_parity] DELTA OK -- nenhum achado novo face ao baseline
[lift_parity] PASS

---- [MANIFEST] ----
-- baseline do lift: recomp_macos_v3 --
OBSOLETO  TAG  [OPDISP] -- ... (D-3.2, reconfirmado nesta task contra v3 fresco)
A MENOS (grupo)  opd-dispatch: ps3_indirect_call+ps3_call_opd esperado>=15847 encontrado=15811
baseline: 243/246 marcadores intactos, 0 ausentes, 0 a menos
obsoletos: 1 citados (nunca falham o gate) | grupos: 0 passaram, 1 falharam (de 1 grupos, 2 marcadores)
[MANIFEST] FAIL (rc=1)

---- [audit_boundaries] ----
I1 sobreposicao de ranges                                          0
I2 lacuna que descodifica como codigo                           2689
I3 fim sem terminador e sem continuacao registada (classe 2550C8)    180
[audit_boundaries] current=180 baseline=180 novos=0 resolvidos=0
[audit_boundaries] DELTA OK -- nenhum achado novo face ao baseline
[audit_boundaries] PASS

== rodape ==
lift_parity          PASS
MANIFEST             FAIL
audit_boundaries     PASS
```
`rc=1` -- honesto: `MANIFEST` falha pela divida pre-existente da Fase 3 (grupo `opd-dispatch`, deficit 36 conversoes), **nao** por regressao introduzida nesta task. `lift_parity` e `audit_boundaries` dao PASS porque o baseline acabou de ser congelado contra este MESMO estado (delta zero contra si proprio -- prova de que o mecanismo funciona).

**Determinismo:** corrido uma segunda vez imediatamente a seguir, produziu o MESMO resultado (`rc=1`, mesmos 3 vereditos, mesmas contagens `current=77 baseline=77 novos=0 resolvidos=0` / `current=180 baseline=180 novos=0 resolvidos=0`).

### Prova do gate: detecta regressao, nao dispara em melhoria

**Prova 1 -- regressao injectada (id removido do baseline, simulando um achado novo no lift):**
```
removido do baseline (simula regressao nao vista antes): 0x0001DD64/CALL_MISMATCH
...
[lift_parity] current=77 baseline=76 novos=1 resolvidos=0
[lift_parity] NOVO (regressao face ao baseline):
  NOVO  0x0001DD64/CALL_MISMATCH
[lift_parity] FAIL (rc=1)
```
rc do script global tambem `1` (ja seria por MANIFEST, mas o passo `lift_parity` isolado passou de PASS para FAIL -- o gate detecta).

**Prova 2 -- melhoria simulada (achado extra no baseline que o current ja nao tem):**
```
[lift_parity] current=77 baseline=78 novos=0 resolvidos=1
[lift_parity] RESOLVIDO (melhoria, nao falha o gate):
  RESOLVIDO  0xFFFFFFFF/CALL_MISMATCH_FANTASMA
[lift_parity] DELTA OK -- nenhum achado novo face ao baseline
[lift_parity] PASS
```
O passo `lift_parity` continua PASS -- a melhoria nunca belisca o rc. Esta e' exactamente a propriedade que o `PREAMBLE ps3_indirect_call` violava antes desta task existir.

Ambas as injeccoes foram feitas em copias temporarias e RESTAURADAS (`diff` confirmou o baseline final byte-identico ao gerado pela medicao real, antes do commit).

**Commit:** `d730b71` -- `feat(04-03): verify_lift.sh -- orquestra os 3 verificadores por delta, baselines congelados`

## Non-regressao verificada

```
$ ctest --test-dir build-tests
100% tests passed out of 22
```
(2 testes py.* skipped, comportamento pre-existente, nao desta task). Este plano nao tocou codigo C/C++; corrido por prudencia dado o CLAUDE.md.

## Task Commits

1. **Task 1: baseline_delta.py -- comparador generico por delta, testado por mutacao sintetica** - `38a9739` (feat)
2. **Task 2: verify_lift.sh -- orquestra os 3 verificadores por delta, baselines congelados a partir da medicao real** - `d730b71` (feat)

## Files Created/Modified
- `games/gow2/lift_baseline/baseline_delta.py` - comparador generico de conjuntos de IDs opacos por delta
- `games/gow2/lift_baseline/test_baseline_delta.py` - 5 testes por mutacao sintetica via subprocess
- `games/gow2/verify_lift.sh` - orquestrador dos 3 verificadores, cabecalho com path absoluto + sha256, rodape PASS/FAIL
- `games/gow2/lift_baseline/PARITY_BASELINE.json` - 77 ids congelados contra recomp_macos_v3 (medicao real desta task)
- `games/gow2/lift_baseline/BOUNDS_BASELINE.json` - 180 ids congelados (I1+I3; I2 fora, informativo)

## Decisions Made
- Baselines gerados por medicao REAL nesta task, nao copiados do objective do plano -- confirmaram os numeros da sessao de planeamento (77/180/0) sem divergencia.
- `manifest_step` chama `verify_lift_baseline.sh` directamente, sem passar por `baseline_delta.py` -- esse verificador ja e' por-marcador (OBSOLETO/GRUPO) desde a Fase 3, generalizar seria trabalho redundante fora do escopo desta task.
- `--oracle` (I4/I5) explicitamente NAO invocado em `audit_boundaries.py` -- item 3 do plano de adopcao, deferido em `04-CONTEXT.md`.

## Deviations from Plan

None - plano executado exactamente como escrito. Os numeros medidos coincidiram com os citados no objective (77 sinais/27 RED, I1=0/I2=2689/I3=180), como o plano previa que pudessem ("podem divergir ligeiramente ... se divergirem, cite o numero real da task") -- neste caso nao divergiram.

## Known Stubs

Nenhum. `verify_lift.sh` e `baseline_delta.py` sao implementacoes completas, sem placeholders.

## Threat Flags

Nenhuma superficie nova fora do `<threat_model>` do plano (T-04-06, T-04-07 ja cobertos pela mitigacao processual de revisao humana em diffs dos baselines e pela checagem `[ -f "$ELF" ]`/`[ -f "$FUNCTIONS" ]` implementada em `verify_lift.sh`).

## Issues Encountered
Nenhum. `.venv/bin/python3` resolveu para Python 3.14 (>=3.11 exigido pela precondicao da Task 1) sem intervencao.

## Nota de realidade (CI)

Como o `CLAUDE.md` e o proprio plano exigem: o CI **nunca** correra estes gates contra o artefacto real, porque `EBOOT.ELF`/`functions.json`/o lift sao dados de jogo nao versionados. `verify_lift.sh` corre localmente, invocado a mao ou (a partir do Plano 04-05) pelo `apply_all_patches.sh`. Nada neste SUMMARY finge integracao em CI.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- `verify_lift.sh` pronto para ser chamado por `apply_all_patches.sh` no Plano 04-05 (D-4.6).
- O gap do MANIFEST (`opd-dispatch`, deficit 36) continua aberto -- dívida da Fase 3, nao desta task fechar; `verify_lift.sh` reporta-o honestamente a cada corrida.
- Os 27 RED do `lift_parity` (9 TRUNCATED, 18 CALL_MISMATCH) continuam no baseline aceite -- investigar/fechar fica para depois (`04-CONTEXT.md`, secao `<deferred>`).
- Nao ha conflito com o trabalho paralelo do 04-01 (`PATCH_CATALOG.tsv`, `gen_catalog.py`, `test_gen_catalog.py` ficaram intocados, confirmado por `git status` antes de cada commit) nem do 04-02 (`patch_14a01c_opd.py`, `patch_icg_ctor_opd.py` nao tocados).

---
*Phase: 04-cat-logo-de-patches-como-gate-que-falha*
*Completed: 2026-07-26*

## Self-Check: PASSED

Todos os 5 ficheiros criados confirmados em disco (`FOUND`); ambos os commits (`38a9739`, `d730b71`) confirmados em `git log --all`. Nada em falta.
