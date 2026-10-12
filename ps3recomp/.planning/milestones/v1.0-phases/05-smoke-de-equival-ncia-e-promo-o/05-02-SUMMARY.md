---
phase: 05-smoke-de-equival-ncia-e-promo-o
plan: 02
subsystem: testing
tags: [python, bash, lift-verification, delta-gate, ps3-recomp]

requires:
  - phase: 04-verificadores-e-catalogo-de-patches
    provides: "baseline_delta.py (D-4.5) -- comparador generico de ids opacos por delta, reusado sem modificacao"
provides:
  - "manifest_delta_gate.py -- converte a saida de gen_manifest.py --verify em ids opacos e delega a comparacao por delta a baseline_delta.py"
  - "MANIFEST_DEBT_BASELINE.json -- divida congelada real (36 ids GRUPO:opd-dispatch:0..35, medidos contra recomp_macos_v3)"
  - "verify_lift.sh com manifest_step() reescrito -- rc do passo MANIFEST vem da interpretacao por delta, nao do limiar absoluto"
affects: [05-03-promocao, 05-04, 05-05]

tech-stack:
  added: []
  patterns:
    - "Conversor-adaptador para baseline_delta.py: um script novo (nao uma modificacao) traduz a saida de um verificador existente em ids opacos, reusando o comparador por delta sem o tocar."
    - "Tokens sinteticos por unidade de deficit de grupo (GRUPO:<id>:0..N-1) para dar granularidade a um contador agregado que, sem isso, so' teria presenca/ausencia binaria."

key-files:
  created:
    - games/gow2/lift_baseline/manifest_delta_gate.py
    - games/gow2/lift_baseline/test_manifest_delta_gate.py
    - games/gow2/lift_baseline/MANIFEST_DEBT_BASELINE.json
  modified:
    - games/gow2/verify_lift.sh

key-decisions:
  - "manifest_delta_gate.py invoca gen_manifest.py e baseline_delta.py sempre via subprocess (nunca import), preservando-os byte-identicos -- confirmado por git diff --stat vazio nos dois."
  - "O deficit do grupo opd-dispatch medido nesta task e' 36 (esperado>=15847, encontrado=15811), identico ao valor citado no 05-CONTEXT.md -- nao mudou desde o planeamento."
  - "manifest_step() em verify_lift.sh mantem a chamada a verify_lift_baseline.sh (detalhe por-marcador, transparencia humana) mas o rc autoritativo passa a vir de manifest_delta_gate.py."

patterns-established:
  - "Prova de injecao em dois sentidos (piora falha, melhoria nunca falha) contra copia scratch, com hash do lift real confirmado intacto antes/depois -- mesmo padrao ja estabelecido no Plano 04-05."

requirements-completed: [REQ-RDY0-6]

coverage:
  - id: D1
    description: "manifest_delta_gate.py converte a saida de gen_manifest.py --verify em ids opacos (individuais + tokens sinteticos GRUPO:<id>:0..N-1) e delega a comparacao por delta a baseline_delta.py, sem modificar nenhum dos dois"
    requirement: "REQ-RDY0-6"
    verification:
      - kind: unit
        ref: "games/gow2/lift_baseline/test_manifest_delta_gate.py (6/6 [PASS], fim-a-fim via subprocess)"
        status: pass
    human_judgment: false
  - id: D2
    description: "MANIFEST_DEBT_BASELINE.json congelado por medicao real contra recomp_macos_v3 (36 ids GRUPO:opd-dispatch:0..35, zero individuais)"
    requirement: "REQ-RDY0-6"
    verification:
      - kind: other
        ref: ".venv/bin/python3 -c \"import json; print(len(json.load(open('games/gow2/lift_baseline/MANIFEST_DEBT_BASELINE.json'))['ids']))\" -> 36"
        status: pass
    human_judgment: false
  - id: D3
    description: "verify_lift.sh: passo MANIFEST reporta a divida de 36 como conhecida (nao nova), PASS; rc global do script passa a 0"
    requirement: "REQ-RDY0-6"
    verification:
      - kind: integration
        ref: "games/gow2/verify_lift.sh ../gow2-recomp/recomp_macos_v3 -> rc=0, rodape lift_parity/MANIFEST/audit_boundaries todos PASS"
        status: pass
    human_judgment: false
  - id: D4
    description: "Injecao real de deficit PIOR (remover mais 1 ps3_call_opd() numa copia scratch) produz rc=1 citando o token novo GRUPO:opd-dispatch:36; recomp_macos_v3 real confirmado intacto por hash"
    requirement: "REQ-RDY0-6"
    verification:
      - kind: integration
        ref: "manifest_delta_gate.py contra ../gow2-recomp/recomp_macos_v3_scratch_manifest_fault (apagada apos o teste) -> rc=1, 'NOVO GRUPO:opd-dispatch:36'; hash md5 de recomp_macos_v3 identico antes/depois (eef1688495317aab6c5674aaa388168b)"
        status: pass
    human_judgment: false
  - id: D5
    description: "Injecao simulada de deficit MELHOR (divida temporariamente inflada a 40 tokens contra deficit real de 36) nunca falha o gate -- rc=0, resolvidos=4"
    requirement: "REQ-RDY0-6"
    verification:
      - kind: integration
        ref: "manifest_delta_gate.py contra recomp_macos_v3 real com MANIFEST_DEBT_BASELINE.json temporariamente editado para 40 ids -> rc=0, 'current=36 baseline=40 novos=0 resolvidos=4'; ficheiro restaurado e confirmado identico por diff"
        status: pass
    human_judgment: false
  - id: D6
    description: "Sem regressao do motor: ctest --test-dir build-tests continua 22/22 (2 skipped, baseline)"
    verification:
      - kind: integration
        ref: "ctest --test-dir build-tests"
        status: pass
    human_judgment: false

duration: ~40min
completed: 2026-07-29
status: complete
---

# Phase 5 Plan 2: manifest_delta_gate.py -- MANIFEST por delta, nao por limiar absoluto Summary

**`verify_lift.sh` deixa de nascer cronicamente vermelho: o passo MANIFEST passa a exigir zero achados NOVOS contra uma dívida congelada e medida (36 ids `GRUPO:opd-dispatch`), não `rc=0` literal do verificador subjacente -- provado por injeção real nos dois sentidos.**

## Performance

- **Duration:** ~40min (estimativa; hora de início da sessão não foi capturada explicitamente no arranque, medição indireta pelo volume de trabalho -- não forjada)
- **Completed:** 2026-07-29T13:59:29Z
- **Tasks:** 2/2
- **Files modified/created:** 4 (`manifest_delta_gate.py`, `test_manifest_delta_gate.py`, `MANIFEST_DEBT_BASELINE.json`, `verify_lift.sh`)

## Accomplishments

- **`manifest_delta_gate.py`** (novo, ~185 linhas): converte a saída de `gen_manifest.py --verify` em ids opacos --
  `AUSENTE:<kind>:<marker>` / `AMENOS:<kind>:<marker>` para achados individuais, e `GRUPO:<gid>:0`..`:N-1` (um token
  sintético POR UNIDADE de déficit) para achados de grupo -- e delega a comparação por delta a `baseline_delta.py`
  (invocado como subprocesso, nunca importado nem modificado). Modos: comparação (default, `--debt`) e `--freeze OUT.json`
  (congela a dívida medida agora, sem comparar a nada).
- **6 testes RED->GREEN** (`test_manifest_delta_gate.py`, fim-a-fim via subprocess, mesmo padrão de `test_baseline_delta.py`):
  caminho limpo (rc=0), achado individual novo (rc=1, cita o marcador), déficit de grupo igual à dívida (rc=0, dívida
  conhecida), déficit de grupo PIOR (rc=1, cita os tokens novos), déficit de grupo MELHOR (rc=0, nunca falha), modo
  `--freeze` (escreve os ids exatos sem comparar).
- **`MANIFEST_DEBT_BASELINE.json` congelado por medição real** contra `../gow2-recomp/recomp_macos_v3`: exatamente 36
  ids, todos `GRUPO:opd-dispatch:0`..`:35`, zero individuais -- idêntico ao valor citado em `05-CONTEXT.md`
  (`esperado>=15847, encontrado=15811`, deficit=36). O número não mudou desde o planeamento.
- **`verify_lift.sh` reescrito** (`manifest_step()`): continua a imprimir o detalhe por-marcador de
  `verify_lift_baseline.sh` (transparência humana -- linhas `A MENOS (grupo)`, `OBSOLETO`, contagem `243/246`), mas o
  `rc` autoritativo do passo passa a vir de `manifest_delta_gate.py`. Corrido contra `recomp_macos_v3` real, o passo
  `MANIFEST` agora reporta `[MANIFEST_DEBT] current=36 baseline=36 novos=0 resolvidos=0` / `DELTA OK` / `[MANIFEST] PASS`,
  e o `rc` GLOBAL do script (`lift_parity` + `MANIFEST` + `audit_boundaries`) passa a **0** -- todos os 3 passos `PASS`.
- **Cabeçalho de `verify_lift.sh` atualizado** (Rule 2 -- documentação desatualizada que induziria erro): a descrição
  do passo 2 como "continua a ser por-marcador, como sempre foi" ficou factualmente errada após a mudança; corrigida
  para refletir que agora falha por delta, tal como os passos 1 e 3.

## Prova de injeção (as duas direções, D-5.1)

Ambas correram contra uma **cópia scratch** de `recomp_macos_v3`
(`../gow2-recomp/recomp_macos_v3_scratch_manifest_fault`), apagada no fim de cada prova. O `recomp_macos_v3` real foi
confirmado intacto por hash MD5 antes e depois: `eef1688495317aab6c5674aaa388168b` (idêntico).

**Injeção 1 -- déficit PIOR (rc≠1 esperado seria falso; rc=1 é o correto):**
```
$ .venv/bin/python3 games/gow2/lift_baseline/manifest_delta_gate.py \
    ../gow2-recomp/recomp_macos_v3_scratch_manifest_fault \
    --manifest games/gow2/lift_baseline/MANIFEST.tsv \
    --debt games/gow2/lift_baseline/MANIFEST_DEBT_BASELINE.json
[MANIFEST_DEBT] current=37 baseline=36 novos=1 resolvidos=0
[MANIFEST_DEBT] NOVO (regressao face ao baseline):
  NOVO  GRUPO:opd-dispatch:36
rc=1
```
(mutação: removida 1 linha com `ps3_call_opd(ctx,` de `ppu_recomp_001.cpp` na cópia scratch, contagem de
`ps3_call_opd(ctx,` caiu de 47 para 46 no corpus inteiro -- deficit sobe de 36 para 37).

**Injeção 2 -- déficit MELHOR (rc=0 nunca falha):** o `recomp_macos_v3` real já ESTÁ no deficit mínimo medido (36);
não havia margem real para simular uma melhoria sem inventar uma conversão adicional. Seguindo o passo 5 do plano,
simulou-se editando **temporariamente** `MANIFEST_DEBT_BASELINE.json` para uma dívida maior (40 tokens em vez de 36) e
correndo contra o `recomp_macos_v3` real (deficit real inalterado, 36):
```
$ .venv/bin/python3 games/gow2/lift_baseline/manifest_delta_gate.py ../gow2-recomp/recomp_macos_v3 \
    --manifest games/gow2/lift_baseline/MANIFEST.tsv \
    --debt games/gow2/lift_baseline/MANIFEST_DEBT_BASELINE.json   # temporariamente com 40 ids
[MANIFEST_DEBT] current=36 baseline=40 novos=0 resolvidos=4
[MANIFEST_DEBT] RESOLVIDO (melhoria, nao falha o gate):
  RESOLVIDO  GRUPO:opd-dispatch:36
  RESOLVIDO  GRUPO:opd-dispatch:37
  RESOLVIDO  GRUPO:opd-dispatch:38
  RESOLVIDO  GRUPO:opd-dispatch:39
[MANIFEST_DEBT] DELTA OK -- nenhum achado novo face ao baseline
rc=0
```
`MANIFEST_DEBT_BASELINE.json` foi restaurado ao conteúdo original imediatamente a seguir e confirmado idêntico por
`diff` (o ficheiro commitado nesta task já é o valor real de 36, não o valor temporário de 40 usado só para a prova).

## O que `verify_lift.sh` passa a dizer no passo MANIFEST

Antes desta task (medido no início da sessão, citado em `05-CONTEXT.md`): `verify_lift.sh recomp_macos_v3` dava
`lift_parity PASS / MANIFEST FAIL / audit_boundaries PASS`, `rc=1` GLOBAL -- cronicamente vermelho por uma dívida já
conhecida.

Depois desta task, corrido de novo contra o mesmo `recomp_macos_v3`:
```
---- [MANIFEST] ----
-- baseline do lift: recomp_macos_v3 --
OBSOLETO  TAG  [OPDISP] -- lifter materializa switch nativo cobrindo 54/54 alvos ...
A MENOS (grupo)  opd-dispatch: ps3_indirect_call+ps3_call_opd esperado>=15847 encontrado=15811

baseline: 243/246 marcadores intactos, 0 ausentes, 0 a menos
obsoletos: 1 citados (nunca falham o gate) | grupos: 0 passaram, 1 falharam (de 1 grupos, 2 marcadores)
[MANIFEST_DEBT] current=36 baseline=36 novos=0 resolvidos=0
[MANIFEST_DEBT] DELTA OK -- nenhum achado novo face ao baseline
[MANIFEST] PASS

== rodape ==
lift_parity          PASS
MANIFEST             PASS
audit_boundaries     PASS
```
`rc=$?` = **0** (GLOBAL). O detalhe por-marcador antigo (`A MENOS (grupo)  opd-dispatch: ...`) continua visível --
transparência humana preservada -- mas já não é o que decide o `rc`; quem decide é `[MANIFEST_DEBT] ... novos=0`.

## Task Commits

1. **Task 1: manifest_delta_gate.py -- RED primeiro (5+1 casos sintéticos), depois GREEN** - `30e6dfd` (feat)
2. **Task 2: congelar MANIFEST_DEBT_BASELINE.json real, reescrever manifest_step(), provar injeção** - `5097a7c` (fix)

**Plan metadata:** commit deste SUMMARY (a seguir), `docs(05-02): completa plano 05-02`.

## Files Created/Modified

- `games/gow2/lift_baseline/manifest_delta_gate.py` - conversor MANIFEST-para-ids-opacos + delegação a `baseline_delta.py`
- `games/gow2/lift_baseline/test_manifest_delta_gate.py` - 6 testes fim-a-fim (subprocess) provando os 2 sentidos de delta
- `games/gow2/lift_baseline/MANIFEST_DEBT_BASELINE.json` - dívida congelada real (36 ids `GRUPO:opd-dispatch`)
- `games/gow2/verify_lift.sh` - `manifest_step()` reescrito (rc por delta) + cabeçalho atualizado

## Decisions Made

- **Nenhuma decisão nova fora do plano.** O plano já continha as decisões-chave (tokens sintéticos por unidade de
  déficit, reuso sem modificação de `baseline_delta.py`/`gen_manifest.py`, prova por injeção em cópia scratch) e foram
  seguidas literalmente.
- **Rule 2 (documentação desatualizada, não código):** o cabeçalho de `verify_lift.sh` (linhas 9-13) descrevia o
  passo 2 como "continua a ser por-marcador, como sempre foi" -- isto ficou factualmente errado após a Task 2 e foi
  corrigido no mesmo commit, para não deixar o próprio script a mentir sobre o seu comportamento.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 2 - Missing/Stale] Cabeçalho de `verify_lift.sh` desatualizado após o fix**
- **Found during:** Task 2 (reescrita de `manifest_step()`)
- **Issue:** O comentário no topo do script (`# Os passos 1 e 3 falham por DELTA ... O passo 2 continua a ser
  por-marcador, como sempre foi.`) tornou-se factualmente incorreto assim que o `rc` do passo 2 passou a vir do
  delta gate -- um leitor futuro seria induzido em erro sobre como o script decide `PASS`/`FAIL`.
- **Fix:** Reescrito o bloco de comentário para descrever os 3 passos como delta-contra-baseline (incluindo
  `MANIFEST_DEBT_BASELINE.json`), mantendo a menção ao detalhe por-marcador como informativo/transparência.
- **Files modified:** `games/gow2/verify_lift.sh` (mesmo commit da Task 2)
- **Verification:** `bash -n games/gow2/verify_lift.sh` (sintaxe OK) + corrida completa contra `recomp_macos_v3`
  real, `rc=0`, rodapé com os 3 passos `PASS`.
- **Committed in:** `5097a7c` (parte do commit da Task 2)

---

**Total deviations:** 1 auto-fixed (documentação, Rule 2)
**Impact on plan:** Nenhum impacto de escopo -- correção textual no mesmo ficheiro e commit que a task já tocava.

## Issues Encountered

- Nenhum. O deficit medido nesta task (36, grupo `opd-dispatch`) confirmou exatamente o valor citado em
  `05-CONTEXT.md`/`04-03-SUMMARY.md` -- nada mudou no lift desde o planeamento desta fase.
- A injeção de "deficit MELHOR" não pôde ser feita por mutação real do lift (o `recomp_macos_v3` real já está no
  mínimo de deficit conhecido, 36; não há margem para "melhorar" sem inventar uma conversão adicional que não existe).
  Seguiu-se a alternativa já prevista no próprio plano (passo 5 da Task 2): editar temporariamente a dívida congelada
  para um valor maior (40) e confirmar `resolvidos=4`, restaurando o ficheiro real a seguir e confirmando por `diff`
  que ficou byte-idêntico ao commitado.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- A **perna 2** do aceite de três pernas (D-5.1) está pronta para o Plano 05-03 (promoção): `verify_lift.sh` já não
  herda um `rc=1` crônico por uma dívida já conhecida -- só falha por achado NOVO real.
- `manifest_delta_gate.py`/`MANIFEST_DEBT_BASELINE.json` seguem o mesmo padrão de `PARITY_BASELINE.json`/
  `BOUNDS_BASELINE.json` (Fase 4) -- qualquer plano futuro que precise recongelar a dívida (ex.: depois de um fix real
  no grupo `opd-dispatch`) usa `manifest_delta_gate.py <LIFT> --freeze MANIFEST_DEBT_BASELINE.json`.
- **Não resolvido nesta task, propositadamente fora de escopo:** a landmine `../gow2-recomp/lift_baseline/gen_manifest.py`
  + `verify_lift_baseline.sh` (cópia órfã da Fase 2, sem `GRUPO`/`OBSOLETO`) continua sem uso por nenhum caller vivo --
  confirmado de novo nesta task que `games/gow2/verify_lift.sh` só invoca a cópia de `games/gow2/lift_baseline/`.
- Bloqueadores: nenhum. Nada em `gow2-recomp` foi tocado (os 3 ficheiros `M` de outra sessão e o diretório `config/`/
  `notes/*.md` ficaram intocados, conforme instruído).

## Self-Check: PASSED

- `games/gow2/lift_baseline/manifest_delta_gate.py`: FOUND
- `games/gow2/lift_baseline/test_manifest_delta_gate.py`: FOUND
- `games/gow2/lift_baseline/MANIFEST_DEBT_BASELINE.json`: FOUND
- commit `30e6dfd`: FOUND
- commit `5097a7c`: FOUND

---
*Phase: 05-smoke-de-equival-ncia-e-promo-o*
*Completed: 2026-07-29*
