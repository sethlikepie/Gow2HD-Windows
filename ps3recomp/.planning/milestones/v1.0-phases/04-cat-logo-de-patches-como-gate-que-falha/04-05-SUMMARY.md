---
phase: 04-cat-logo-de-patches-como-gate-que-falha
plan: 05
subsystem: tooling
tags: [bash, python, patch-classification, gate, gow2, injecao-de-falha]

requires:
  - phase: 04-cat-logo-de-patches-como-gate-que-falha
    provides: "PATCH_CATALOG.tsv (04-01, 64 FUNCIONAL/23 PROBE), verify_lift.sh (04-03), CONTRACTS.tsv + check_contracts.py (04-04, 22/64 FUNCIONAL cobertos)"
provides:
  - "../gow2-recomp/apply_all_patches.sh (mirror games/gow2/apply_all_patches.sh): classificacao de SEIS estados (D-4.1) -- APPLIED/ALREADY-APPLIED/NO-MATCH/UNVERIFIED/FAILED[-PARTIAL]/SKIPPED"
  - "--status <tsv>: patch/status/classe por corrida, 87 linhas de dados + cabecalho"
  - "chamada a verify_lift.sh no fim (D-4.6), rc incorporado no calculo final"
  - "classe_de()/is_probe(): lookup de classe via awk sobre PATCH_CATALOG.tsv, PROBE excluido do gate (D-4.4)"
affects: ["Fase 5 (smoke de equivalencia e promocao) -- consome o apply_all_patches.sh fechado"]

tech-stack:
  added: []
  patterns:
    - "D-4.1 aplicado literalmente: rc==0-sem-mudanca deixa de ser ALREADY-APPLIED automatico -- passa por check_contracts.py e mapeia 0/1/2 para ALREADY-APPLIED/NO-MATCH/UNVERIFIED"
    - "PROBE fora do gate mas dentro do relatorio (D-4.4): sufixo '(PROBE, fora do gate)' impresso, contadores _gate separados dos contadores de relatorio"
    - "lookup de classe por awk (nao declare -A) -- portavel ao /bin/bash 3.2 de fabrica do macOS, que nao suporta array associativo"

key-files:
  created: []
  modified:
    - ../gow2-recomp/apply_all_patches.sh
    - games/gow2/apply_all_patches.sh

key-decisions:
  - "Dois commits sequenciais (nucleo de classificacao na Task 1; --status+verify_lift.sh+corrida real na Task 2), espelhando a estrutura do plano -- entre os dois commits o script fica temporariamente incompleto (n_nomatch/n_unverified usados antes de declarados), nunca executado como um todo nesse estado intermedio (so bash -n + PoC isolado), exatamente como o <done> da Task 1 antecipava."
  - "Rule 3 (bug bloqueante, auto-corrigido): declare -A falha no /bin/bash 3.2.57 de fabrica do macOS (sem homebrew bash em PATH) -- 'invalid option' e depois 'invalid arithmetic operator' ao indexar por string num array indexado implicito. Substituido por classe_de(), lookup via awk sobre PATCH_CATALOG.tsv por chamada -- 87 chamadas awk por corrida, custo desprezivel face aos ~87 subprocessos Python ja existentes."
  - "TOTAL: no rodape ganhou NO-MATCH=%d UNVERIFIED=%d explicitos (nao pedido literalmente pelo plano, mas necessario -- sem isso o TOTAL subcontava, porque ALREADY-APPLIED deixou de absorver os 3 estados)."

requirements-completed: [REQ-RDY0-5]

coverage:
  - id: D1
    description: "apply_all_patches.sh classifica em 6 estados (D-4.1); NO-MATCH/UNVERIFIED nao sao verdes; PROBE excluido do calculo do rc (D-4.4)"
    requirement: "REQ-RDY0-5"
    verification:
      - kind: integration
        ref: "corrida real contra recomp_macos_v3: APPLIED=0 ALREADY-APPLIED=22 NO-MATCH=0 UNVERIFIED=62 FAILED=3 SKIPPED=0 (87 total); 22+42=64 FUNCIONAL, 20+3=23 PROBE, reconciliado exatamente com 04-04-SUMMARY.md"
        status: pass
    human_judgment: false
  - id: D2
    description: "--status <tsv> escreve patch/status/classe por corrida, 87 linhas de dados + cabecalho"
    requirement: "REQ-RDY0-5"
    verification:
      - kind: integration
        ref: "./apply_all_patches.sh recomp_macos_v3 --status /tmp/status_04_05.tsv -> wc -l 88 (87+cabecalho)"
        status: pass
    human_judgment: false
  - id: D3
    description: "Gate sai nao-zero por injeccao de falha REAL fim-a-fim (nao so a chamada isolada de check_contracts.py), rodando o apply_all_patches.sh INTEIRO contra uma copia scratch completa"
    requirement: "REQ-RDY0-5"
    verification:
      - kind: manual_procedural
        ref: "copia scratch de recomp_macos_v3 convergida, 1 de 7 ps3_call_opd(ctx, removida da regiao de func_0032E200, apply_all_patches.sh corrido pela 3a vez -> patch_32e200_opd.py = NO-MATCH, rc=1 do script inteiro; copia apagada; v2/v3 reais confirmados intactos por hash"
        status: pass
    human_judgment: false
  - id: D4
    description: "apply_all_patches.sh chama verify_lift.sh no fim (D-4.6), rc incorporado no calculo final"
    requirement: "REQ-RDY0-5"
    verification:
      - kind: integration
        ref: "secao '-- verify_lift.sh (D-4.6) --' na saida real, com verify_lift_rc=1 (MANIFEST FAIL, divida da Fase 3) entrando no OR final"
        status: pass
    human_judgment: false

duration: ~2h
completed: 2026-07-27
status: complete
---

# Phase 4 Plan 05: apply_all_patches.sh -- seis estados, --status, injecao de falha real, verify_lift.sh no fim (D-4.1/D-4.6) Summary

**`apply_all_patches.sh` reescrito com classificacao de SEIS estados (D-4.1) verificada por `check_contracts.py`, `--status <tsv>`, encadeamento de `verify_lift.sh` no fim (D-4.6) e prova de injecao de falha REAL fim-a-fim (NO-MATCH + rc=1 numa copia scratch inteira, nao so a chamada isolada) -- fecha a Fase 4; corrida real honesta contra `recomp_macos_v3` fica em rc=1 por 42 `UNVERIFIED` nao cobertos por contrato e pelo `MANIFEST FAIL` herdado da Fase 3, exatamente como o plano previa.**

## Performance

- **Duration:** ~2h (dominado por duas corridas completas do script contra `recomp_macos_v3`/copia scratch, ~2min cada, por chamarem `check_contracts.py` -- que relê o lift inteiro -- 62-65 vezes por corrida)
- **Tasks:** 2/2 completas
- **Files modified:** 2 (o script real em `../gow2-recomp` + o seu mirror em `games/gow2`, byte-identicos)

## Estado

Ultimo plano da Fase 4. Os quatro criterios do ROADMAP para `REQ-RDY0-5` estao
**literalmente satisfeitos** (detalhe na secao "REQ-RDY0-5" abaixo), apesar do
`rc` GLOBAL do script ficar em `1` numa corrida real -- exatamente a
consequencia honesta e antecipada pelo objective do plano (cobertura parcial
de `CONTRACTS.tsv`, 22/64 `FUNCIONAL`).

## Por tarefa: comando e saida real

### Task 1 -- nucleo de classificacao de 6 estados (sem `--status` ainda)

Acao: adicionado `PS3_ENGINE_ROOT` (ponte para o monorepo), carregamento de
`PATCH_CATALOG.tsv`, `is_probe()`, e o ramo `else` (rc==0, conteudo
inalterado) do loop principal passou a chamar `check_contracts.py` e mapear
o seu rc (0/1/2/outro) para `ALREADY-APPLIED`/`NO-MATCH`/`UNVERIFIED`/
`UNVERIFIED`, em vez de assumir `ALREADY-APPLIED` automaticamente.

**Prova de conceito isolada (a peca central, provada ANTES de integrar no
loop inteiro):**

```
$ mkdir -p /tmp/apply_poc && cp ../gow2-recomp/recomp_macos_v3/ppu_recomp_003.cpp /tmp/apply_poc/
$ python3 -c "... remove 1 de 7 'ps3_call_opd(ctx, (uint32_t)ctx->gpr[10]); DRAIN_TRAMPOLINE(ctx);' na regiao de func_0032E200 ..."
antes: 7 depois: 6
$ .venv/bin/python3 games/gow2/lift_baseline/check_contracts.py games/gow2/lift_baseline/CONTRACTS.tsv /tmp/apply_poc --patch patch_32e200_opd.py; echo "rc=$?"
[FAIL] opd-32e200 (patch_32e200_opd.py): agulha='ps3_call_opd(' predicado=count_ge esperado 7 encontrado 6
[FAIL] opd-32e200-noindirect (patch_32e200_opd.py): agulha='ps3_indirect_call' predicado=count_eq esperado 0 encontrado 1
rc=1
$ rm -rf /tmp/apply_poc
```

`rc=1` confirmado -- mapearia para `NO-MATCH`. Copia scratch apagada.
`recomp_macos_v3` real reconfirmado intacto (`md5 -q ppu_recomp_003.cpp` =
`f0871682e32ffd268ec5fce250891ef9`, identico antes e depois).

```
$ bash -n ../gow2-recomp/apply_all_patches.sh; echo "syntax_rc=$?"
syntax_rc=0
$ diff ../gow2-recomp/apply_all_patches.sh games/gow2/apply_all_patches.sh; echo "diff_rc=$?"
diff_rc=0
```

**Commits:** `25b0762` (gow2-recomp), `7584174` (ps3recomp/games/gow2 mirror).

### Task 2 -- `--status`, `verify_lift.sh` (D-4.6), corrida real, injecao de falha fim-a-fim

**Bug bloqueante encontrado e corrigido antes de qualquer corrida completa
(Rule 3):** a primeira tentativa de rodar o script inteiro falhou:

```
./apply_all_patches.sh: line 108: declare: -A: invalid option
./apply_all_patches.sh: line 113: patch_14a01c_opd.py: syntax error: invalid arithmetic operator (error token is ".py")
```

`bash --version` nesta maquina: `GNU bash, version 3.2.57(1)-release
(arm64-apple-darwin25)` -- o `/bin/bash` de fabrica do macOS, sem homebrew
bash em `PATH`. Bash 3.2 nao suporta array associativo (`declare -A`); um
subscrito de string num array indexado implicito e' avaliado como expressao
aritmetica, daí o segundo erro. Corrigido: `classe_de()` faz o lookup via
`awk -F'\t' -v p="$name" '$1==p{print $3; exit}'` sobre `PATCH_CATALOG.tsv`
por chamada, sem nenhum array associativo -- portavel ao bash 3.2.

**Fim-a-fim 1 (`--status`) + fim-a-fim 4 (corrida real honesta), no mesmo
comando:**

```
$ cd ../gow2-recomp && ./apply_all_patches.sh recomp_macos_v3 --status /tmp/status_04_05.tsv; echo "rc=$?"
...
TOTAL: 87 patches | APPLIED=0 ALREADY-APPLIED=22 NO-MATCH=0 UNVERIFIED=62 FAILED=3 SKIPPED=0
FALHARAM: patch_cc9d0_live_yield.py patch_ce03c_pre_play_stop.py patch_ce03c_wait_idle_f2b_movie.py
(NAO forcar: pode ser patch obsoleto para o novo shape do lift)
UNVERIFIED (sem contrato declarado): patch_2b0fb4_opd.py ... (42 patches FUNCIONAL sem contrato)
...
-- verify_lift.sh (D-4.6) --
== verify_lift.sh ==
...
== rodape ==
lift_parity          PASS
MANIFEST             FAIL
audit_boundaries     PASS
rc=1
$ wc -l /tmp/status_04_05.tsv
88 /tmp/status_04_05.tsv
$ cut -f2 /tmp/status_04_05.tsv | tail -n +2 | sort | uniq -c
  22 ALREADY-APPLIED
   3 FAILED
  62 UNVERIFIED
```

(tempo real: `2:13,95 total`, dominado pelas 65 chamadas a `check_contracts.py`
que reléem o lift inteiro cada vez -- custo pre-existente de `check_contracts.py`,
04-04, fora do escopo deste plano corrigir.)

**Reconciliacao exata com o 04-04-SUMMARY.md:** 22 `ALREADY-APPLIED` = os
22/64 `FUNCIONAL` cobertos por contrato, todos PASS; 62 `UNVERIFIED` = 42
`FUNCIONAL` sem contrato + 20 `PROBE` sem contrato (23 `PROBE` totais - 3
`FAILED`); 22+42=64 `FUNCIONAL`, 20+3=23 `PROBE`, 64+23=87. Bate exatamente,
sem ajuste.

**Fim-a-fim 3 (injecao de falha REAL, fim-a-fim, contra o script INTEIRO --
nao so a chamada isolada da Task 1):**

```
$ cp -R ../gow2-recomp/recomp_macos_v3 ../gow2-recomp/recomp_macos_v3_scratch_fault   # 393M
$ ./apply_all_patches.sh recomp_macos_v3_scratch_fault; echo "rc=$?"   # corrida de convergencia
... rc=1 (identico a v3 real -- MANIFEST FAIL pre-existente, nao regressao)
$ python3 -c "... remove 1 de 7 ps3_call_opd(ctx, na regiao de func_0032E200 do scratch ..."
antes: 7 depois: 6
$ ./apply_all_patches.sh recomp_macos_v3_scratch_fault; echo "rc=$?"   # 3a corrida, pos-fault
...
NO-MATCH         patch_32e200_opd.py
...
TOTAL: 87 patches | APPLIED=0 ALREADY-APPLIED=21 NO-MATCH=1 UNVERIFIED=62 FAILED=3 SKIPPED=0
NO-MATCH (pos-condicao falsa, D-4.2): patch_32e200_opd.py
...
rc=1
$ rm -rf ../gow2-recomp/recomp_macos_v3_scratch_fault
```

`patch_32e200_opd.py` classifica `NO-MATCH` (ALREADY-APPLIED caiu de 22 para
21, NO-MATCH subiu de 0 para 1) e o `rc` do script INTEIRO e' `1` -- provado
contra uma copia scratch completa do lift, corrida do `apply_all_patches.sh`
por inteiro (nao a chamada isolada de `check_contracts.py`), exatamente o
item 3 exigido pela fase.

**Prova de nao-contaminacao (hash antes/depois de toda a task):**

```
recomp_macos_v3/ppu_recomp_003.cpp:  f0871682e32ffd268ec5fce250891ef9  (identico antes/depois)
recomp_macos_v2 (producao, find *.cpp+*.h | md5 -q | md5 -q):
  antes:  52e6fd930f5b757e4149b5e520336511
  depois: 52e6fd930f5b757e4149b5e520336511  -> INTACTO
recomp_macos_v3_scratch_fault: apagado apos a prova
```

**Nao-regressao do motor:**

```
$ ctest --test-dir build-tests 2>&1 | tail -6
100% tests passed out of 22
Total Test time (real) =  24.74 sec
The following tests did not run:
     13 - py.lift_selftest (Skipped)
     14 - py.spu_lift_selftest (Skipped)
```

22/22, identico ao baseline citado em `04-03-SUMMARY.md`. Fixtures SPU
regeneradas pelo `ctest` (`runtime/spu/tests/out_*/spu_recomp.{c,h}`)
restauradas com `git checkout --` antes do commit, conforme a regra do
CLAUDE.md.

**Mirror:**

```
$ diff ../gow2-recomp/apply_all_patches.sh games/gow2/apply_all_patches.sh; echo "diff_rc=$?"
diff_rc=0
```

**Commits:** `b49a5ee` (gow2-recomp), `a622920` (ps3recomp/games/gow2 mirror).

## Distribuicao final dos SEIS estados sobre os 87 patches (corrida real, `recomp_macos_v3`)

| Estado | Contagem | Nota |
|---|---:|---|
| APPLIED | 0 | Lift ja convergido (medicao da sessao de planeamento) |
| ALREADY-APPLIED | 22 | 22/64 FUNCIONAL cobertos por contrato, todos verificados PASS |
| NO-MATCH | 0 | Nenhum na corrida honesta contra v3 real (so na injecao deliberada) |
| UNVERIFIED | 62 | 42 FUNCIONAL sem contrato + 20 PROBE sem contrato |
| FAILED | 3 | `patch_cc9d0_live_yield.py`, `patch_ce03c_pre_play_stop.py`, `patch_ce03c_wait_idle_f2b_movie.py` -- **todos classe PROBE** |
| SKIPPED | 0 | `SKIP_LIST` vazio nesta corrida |
| **TOTAL** | **87** | |

**Zero FAILED fora de PROBE** (criterio 2 do ROADMAP, alcancado literalmente
-- os 3 FAILED sao 100% PROBE). **42 UNVERIFIED fora de PROBE** entram no
`rc` final por D-4.1 ("um UNKNOWN nao pode contar como sucesso").

## rc GLOBAL do script: honesto, `1`

`rc=1` na corrida real -- causado por DOIS fatores independentes, ambos
citados sem maquiagem:

1. `n_unverified_gate=42` (os 42 `FUNCIONAL` sem contrato em `CONTRACTS.tsv`,
   divida conhecida e explicita do Plano 04-04 -- cobertura parcial, nao um
   defeito desta task).
2. `verify_lift_rc=1` -- `verify_lift.sh` reporta `MANIFEST FAIL` (grupo
   `opd-dispatch`, deficit 36, divida herdada da Fase 3, ja citada em
   `03-03-SUMMARY.md` e `04-03-SUMMARY.md`, **nao** regressao desta task).

`lift_parity` e `audit_boundaries` (dentro de `verify_lift.sh`) dao `PASS`
(delta zero contra os baselines congelados no Plano 04-03).

## REQ-RDY0-5: avaliacao dos 4 criterios do ROADMAP

Os 4 criterios (ROADMAP.md linhas 125-128, numeros corrigidos por
`04-CONTEXT.md`) estao **literalmente satisfeitos**, mesmo com `rc` global
`1`:

1. ✅ `PATCH_CATALOG.tsv` com os 87 classificados, zero indeterminados
   (fechado no Plano 04-01, reconfirmado nesta task).
2. ✅ `./apply_all_patches.sh recomp_macos_v3 --status <tsv>` existe e
   reporta **zero `FAILED` fora de PROBE** (medido: 3 FAILED, todos PROBE).
3. ✅ O gate sai nao-zero por injecao de falha real, provado fim-a-fim contra
   o script inteiro (nao so o caminho feliz).
4. ✅ Os patches PROBE ficam fora do gate, listados nominalmente com o
   sufixo `(PROBE, fora do gate)` no relatorio.

**`REQ-RDY0-5` marcado COMPLETO.** O `rc` global nao-zero (por `UNVERIFIED`
e por `MANIFEST FAIL` da Fase 3) nao invalida o requisito -- e a
consequencia honesta e antecipada da cobertura parcial de `CONTRACTS.tsv`
(D-4.4/04-04), exatamente como o objective do plano e as instrucoes desta
task previam explicitamente.

## Task Commits

1. **Task 1: nucleo de classificacao de 6 estados via check_contracts.py**
   - `25b0762` (gow2-recomp, feat), `7584174` (ps3recomp mirror, feat)
2. **Task 2: --status, verify_lift.sh (D-4.6), corrida real, injecao de falha real**
   - `b49a5ee` (gow2-recomp, feat), `a622920` (ps3recomp mirror, feat)

Nenhum push. Nenhum commit tocou dados de jogo. Commits em portugues, sem
co-autoria, ficheiros explicitos (`git add <path>`, nunca `git add -A`/`.`).

## Files Created/Modified

- `../gow2-recomp/apply_all_patches.sh` (modificado) -- script real que roda:
  6 estados, `--status`, `verify_lift.sh` no fim, `classe_de()`/`is_probe()`.
- `games/gow2/apply_all_patches.sh` (modificado) -- mirror byte-identico no
  monorepo (confirmado por `diff` apos cada commit).

## Decisions Made

- Dois commits sequenciais espelhando a estrutura de duas tasks do plano
  (nucleo de classificacao primeiro, orquestracao completa depois) --
  intencionalmente deixa o script "quebrado" se executado por inteiro entre
  os dois commits (referencia a `n_nomatch`/`n_unverified` antes de
  declarados), mas isso nunca acontece: a Task 1 so corre `bash -n` + a PoC
  isolada de `check_contracts.py`, nunca o script completo.
- `classe_de()` via `awk` em vez de `declare -A` -- decisao de portabilidade,
  nao arquitetural: mesma interface (`is_probe "$name"`), mesmo resultado,
  apenas o mecanismo de lookup muda para funcionar no `/bin/bash` 3.2 de
  fabrica do macOS (Regra 3, bloqueante).
- `TOTAL` do rodape ganhou `NO-MATCH=%d UNVERIFIED=%d` explicitos -- sem
  isso a soma do `TOTAL` ficaria incorreta (o `ALREADY-APPLIED` antigo
  absorvia os 3 estados novos; sem contá-los à parte, `TOTAL` subcontaria).

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] `declare -A` nao existe no `/bin/bash` de fabrica do macOS**
- **Encontrado durante:** Task 2, primeira tentativa de corrida completa.
- **Problema:** `bash --version` nesta maquina = `GNU bash, version
  3.2.57(1)-release (arm64-apple-darwin25)` (sem homebrew bash em `PATH`,
  logo `#!/usr/bin/env bash` resolve para o `/bin/bash` de fabrica). Bash 3.2
  nao suporta array associativo -- `declare -A PATCH_CLASSE` (introduzido na
  Task 1) falhava com `invalid option`, e o subsequente
  `${PATCH_CLASSE[$name]}` era avaliado como expressao aritmetica sobre um
  array indexado implicito, explodindo com `invalid arithmetic operator`.
  Isto bloqueava toda e qualquer corrida do script inteiro nesta maquina.
- **Fix:** substituida a leitura em array associativo por `classe_de()`,
  lookup por linha via `awk -F'\t' -v p="$name" '$1==p{print $3; exit}'`
  sobre `PATCH_CATALOG.tsv`, chamado uma vez por patch (87 chamadas por
  corrida, custo desprezivel face aos ~87 subprocessos Python ja existentes
  no loop). `is_probe()` passou a chamar `classe_de()` em vez de indexar o
  array.
- **Ficheiros:** `../gow2-recomp/apply_all_patches.sh`,
  `games/gow2/apply_all_patches.sh`.
- **Verificacao:** `bash -n` limpo; corrida completa contra `recomp_macos_v3`
  produziu 87/87 linhas classificadas sem erro de shell.
- **Commit:** `b49a5ee` (Task 2 commit, gow2-recomp); `a622920` (mirror).

**2. [Rule 2 - Missing Critical] `TOTAL` do rodape nao contava NO-MATCH/UNVERIFIED**
- **Encontrado durante:** Task 2, ao adicionar os contadores dos 2 estados
  novos.
- **Problema:** o `printf 'TOTAL: ...'` original somava so
  `n_applied+n_already+n_failed+n_skipped` -- com `ALREADY-APPLIED` agora
  dividido em tres (`ALREADY-APPLIED`/`NO-MATCH`/`UNVERIFIED`), o `TOTAL`
  ficaria matematicamente errado (menor que 87) se nao incluisse os 2 novos
  contadores.
- **Fix:** `TOTAL` passou a somar e imprimir tambem `NO-MATCH=%d` e
  `UNVERIFIED=%d` explicitos.
- **Ficheiros:** `../gow2-recomp/apply_all_patches.sh`,
  `games/gow2/apply_all_patches.sh`.
- **Verificacao:** corrida real confirma `TOTAL: 87 patches |
  APPLIED=0 ALREADY-APPLIED=22 NO-MATCH=0 UNVERIFIED=62 FAILED=3 SKIPPED=0`
  -- soma bate 87.
- **Commit:** `b49a5ee` (gow2-recomp); `a622920` (mirror).

---

**Total deviations:** 2 (1 Rule 3 bloqueante, 1 Rule 2 critica-em-falta),
ambas corrigidas dentro do orcamento normal da Task 2, testadas antes do
commit.
**Impact on plan:** Nenhum desvio de escopo -- ambos os fixes eram
necessarios para o script simplesmente RODAR nesta maquina (Rule 3) e para
o relatorio ser matematicamente correto (Rule 2). Sem eles, nenhum dos
fim-a-fim exigidos pelo plano seria sequer executavel.

## Issues Encountered

- Cada corrida completa do `apply_all_patches.sh` demora ~2min14s (nao os
  poucos segundos esperados de um script bash), porque `check_contracts.py`
  releé o lift inteiro (`ppu_recomp_*.cpp`, centenas de milhares de linhas)
  a cada chamada, e e' chamado ~62-65 vezes por corrida (uma vez por patch
  `rc==0`-sem-mudanca). Isto e' um custo pre-existente de `check_contracts.py`
  (Plano 04-04), nao introduzido por este plano, e fora do escopo desta task
  otimizar (nao bloqueia nenhum criterio de aceitacao) -- registrado aqui
  para quem for otimizar `check_contracts.py` no futuro (ex.: cache do texto
  do lift entre chamadas, ou um modo batch que verifica todos os patches
  numa so invocacao do interpretador Python).

## User Setup Required

None -- nenhuma configuracao de servico externo necessaria.

## Next Phase Readiness

- `apply_all_patches.sh` fechado: 6 estados, `--status`, `verify_lift.sh`
  encadeado, injecao de falha provada fim-a-fim. `REQ-RDY0-5` completo
  (4/4 criterios do ROADMAP), Fase 4 pronta para fechar (5/5 planos).
- Divida conhecida que continua aberta, nao desta task fechar: 42 dos 64
  `FUNCIONAL` continuam `UNVERIFIED` (cobertura parcial de `CONTRACTS.tsv`,
  04-04); o `MANIFEST FAIL` do grupo `opd-dispatch` (deficit 36, Fase 3)
  continua sem fechar. Ambos citados honestamente em cada corrida do
  `apply_all_patches.sh`, nunca escondidos.
- Custo de performance de `check_contracts.py` (~2min/corrida completa)
  registrado em "Issues Encountered" para otimizacao futura, se a
  frequencia de uso justificar.
- Nenhum conflito com trabalho paralelo -- unico plano da Wave 3,
  `depends_on: ["04-03", "04-04"]`, ambos ja fechados antes desta execucao.

---
*Phase: 04-cat-logo-de-patches-como-gate-que-falha*
*Completed: 2026-07-27*

## Self-Check: PASSED

Ficheiros confirmados no disco: `../gow2-recomp/apply_all_patches.sh`,
`games/gow2/apply_all_patches.sh` (diff vazio entre os dois), este SUMMARY.
Commits confirmados por `git log --oneline`: `25b0762`, `b49a5ee`
(gow2-recomp); `7584174`, `a622920` (ps3recomp). `bash -n` limpo nos dois
ficheiros. `ctest --test-dir build-tests`: 22/22, sem regressao.
