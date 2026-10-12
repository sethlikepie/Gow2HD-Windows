---
phase: 05-smoke-de-equival-ncia-e-promo-o
plan: 03
subsystem: infra
tags: [bash, ppu-lift, gate, gow2, macos-arm64, promotion-gate]

requires:
  - phase: 05-01
    provides: "smoke_relift_equiv.sh -- perna 1 do aceite (build automatico + 6 corridas + TSV + modo --bin)"
  - phase: 05-02
    provides: "verify_lift.sh delta-aware (manifest_delta_gate.py + MANIFEST_DEBT_BASELINE.json) -- perna 2 ja nao nasce vermelho por divida conhecida"
provides:
  - "accept_relift.sh -- orquestrador do aceite de tres pernas (D-5.1) + contadores por delta (D-5.2), rc = AND de tudo"
  - "mirror games/gow2/accept_relift.sh byte-identico"
  - "games/gow2/lift_baseline/COUNTERS_BASELINE.tsv -- baseline REAL congelado contra a producao (recomp_macos_v2/boot_gow2): function_table_count=51917, boot_lifted_functions=52068, imp_modules=13, imp_imports=151, orfaos=0"
affects: [05-04, 05-05]

tech-stack:
  added: []
  patterns:
    - "Orquestrador que NUNCA usa o rc bruto de uma ferramenta subjacente quando esse rc mistura divida conhecida (UNVERIFIED) com regressao real (NO-MATCH/FAILED) -- parseia o artefacto estruturado (TSV) diretamente."
    - "Baseline congelado por medicao real na primeira corrida em que o ficheiro nao existe (mesmo padrao de PARITY_BASELINE.json/BOUNDS_BASELINE.json/MANIFEST_DEBT_BASELINE.json da Fase 4/05-02), reaproveitado nas corridas seguintes."
    - "Prova de rejeicao por mutacao em copia scratch (recomp_macos_v3_scratch_legN), nunca no lift real -- hash MD5 do corpus confirmado identico antes/depois de cada prova."

key-files:
  created:
    - ../gow2-recomp/accept_relift.sh
    - games/gow2/accept_relift.sh
    - games/gow2/lift_baseline/COUNTERS_BASELINE.tsv
  modified: []

key-decisions:
  - "Perna 3 nunca le o rc bruto de apply_all_patches.sh -- parseia o --status TSV diretamente por NO-MATCH e FAILED/FAILED-PARTIAL fora de classe PROBE. Confirmado na corrida real: rc bruto=1 (por causa de UNVERIFIED=62), leg3_pass=1 (NO-MATCH=0, FAILED-fora-PROBE=0) -- os dois numeros DIVERGEM exatamente como D-5.1 previu, e essa divergencia e o proprio mecanismo que evita o gate nascer vermelho por divida conhecida."
  - "Bug real encontrado e corrigido nesta task (Rule 1): a extracao de function_table_count com 'grep -oE [0-9]+' sozinho casava o '64' de 'uint64_t' ANTES do valor real na mesma linha ('const uint64_t function_table_count = 51917;'), dando producao=candidato=64 em vez de 51917 na primeira corrida. Corrigido extraindo so o numero apos '= ' ('grep -oE \"= [0-9]+\" | grep -oE \"[0-9]+\"'). Confirmado na corrida seguinte: 51917/51917/delta=0."
  - "As tres pernas correm sempre, sem abortar cedo (mesmo com a perna 1 a falhar) -- confirmado na prova de rejeicao da perna 1: mesmo com o build a falhar (perna 1 FAIL), as pernas 2 e 3 correram na mesma e reportaram o seu proprio veredito."

requirements-completed: [REQ-RDY0-6]

coverage:
  - id: D1
    description: "accept_relift.sh implementa literalmente as tres pernas de D-5.1 (smoke, verify_lift.sh, apply_all_patches.sh --status parseado por NO-MATCH/FAILED-fora-de-PROBE, nunca pelo rc bruto) e sai rc=0 so se as tres passarem"
    requirement: "REQ-RDY0-6"
    verification:
      - kind: integration
        ref: "./accept_relift.sh recomp_macos_v3 6 /tmp (corrida real: PERNA1 OK=6/6, PERNA2 rc=0, PERNA3 NO-MATCH=0/FAILED-fora-PROBE=0, ACEITE rc=0)"
        status: pass
    human_judgment: false
  - id: D2
    description: "Prova de rejeicao real: cada uma das tres pernas forcada a falhar isoladamente (copia scratch, lift real confirmado intacto por hash antes/depois), accept_relift.sh sai rc=1 nomeando corretamente a perna"
    requirement: "REQ-RDY0-6"
    verification:
      - kind: integration
        ref: "3 corridas reais contra recomp_macos_v3_scratch_leg{1,2,3} -- ver secao 'Prova de rejeicao' do SUMMARY para rc e mensagem de cada uma"
        status: pass
    human_judgment: false
  - id: D3
    description: "Contadores por delta (D-5.2): COUNTERS_BASELINE.tsv congelado por medicao real contra a producao (recomp_macos_v2/boot_gow2), candidato medido por delta, imp_modules/imp_imports/orfaos absolutos e bloqueantes, function_table_count/boot_lifted_functions informativos"
    requirement: "REQ-RDY0-6"
    verification:
      - kind: integration
        ref: "cat games/gow2/lift_baseline/COUNTERS_BASELINE.tsv (51917/52068/13/151/0) + corrida real mostrando producao=candidato=51917 delta=0 e 13/151/0 batendo exatamente"
        status: pass
    human_judgment: false
  - id: D4
    description: "Mirror games/gow2/accept_relift.sh identico byte-a-byte ao script real"
    verification:
      - kind: other
        ref: "diff ../gow2-recomp/accept_relift.sh games/gow2/accept_relift.sh (rc=0, saida vazia)"
        status: pass
    human_judgment: false
  - id: D5
    description: "Sem regressao do motor: ctest --test-dir build-tests continua 22/22 (2 py skipped)"
    verification:
      - kind: integration
        ref: "ctest --test-dir build-tests"
        status: pass
    human_judgment: false

duration: ~75min
completed: 2026-07-29
status: complete
---

# Phase 5 Plan 03: accept_relift.sh -- o aceite de tres pernas (D-5.1 + D-5.2) Summary

**`accept_relift.sh` orquestra o aceite de promocao inteiro num so comando: smoke (perna 1) + `verify_lift.sh` (perna 2) + `apply_all_patches.sh --status` parseado por NO-MATCH/FAILED-fora-de-PROBE (perna 3, NUNCA o rc bruto que tambem conta os 62 UNVERIFIED conhecidos) + contadores por delta contra um baseline REAL congelado (D-5.2) -- rc=0 so se tudo passar, provado ACEITANDO `recomp_macos_v3` real e REJEITANDO em tres mutacoes isoladas, uma por perna.**

## Performance

- **Duration:** ~75min (dominado por 5 corridas reais completas do script: build ~20s + ate 6 execucoes de 25s + verify_lift + apply_all_patches ~2m14s, cada uma)
- **Completed:** 2026-07-29
- **Tasks:** 2/2
- **Files modified:** 3 (`../gow2-recomp/accept_relift.sh`, `games/gow2/accept_relift.sh`, `games/gow2/lift_baseline/COUNTERS_BASELINE.tsv`)

## Accomplishments

- **`accept_relift.sh`** criado do zero: orquestra `smoke_relift_equiv.sh` (perna 1, Plano 05-01), `games/gow2/verify_lift.sh` via ponte `PS3_ENGINE_ROOT` (perna 2, Plano 05-02), e `apply_all_patches.sh --status` (perna 3) -- as tres correm sempre, sem abortar cedo, e o relatorio final mostra PASS/FAIL de cada uma antes do veredito.
- **Perna 3 nunca le o `rc` bruto** de `apply_all_patches.sh`: parseia o `--status` TSV diretamente (`awk -F'\t'` sobre as colunas `status`/`classe`), exigindo so zero `NO-MATCH` e zero `FAILED`/`FAILED-PARTIAL` fora de classe `PROBE`. `UNVERIFIED` fica citado como informativo.
- **Contadores por delta (D-5.2)**: `COUNTERS_BASELINE.tsv` congelado por medicao REAL contra a producao (`recomp_macos_v2`/`boot_gow2`) na primeira corrida em que nao existia; candidato medido por delta a cada corrida seguinte (reaproveitando os dados ja extraidos pela perna 1, sem correr outro smoke). `imp_modules`/`imp_imports`/`orfaos` sao absolutos e BLOQUEANTES; `function_table_count`/`boot_lifted_functions` sao INFORMATIVOS.
- **Mirror byte-identico** em `games/gow2/accept_relift.sh` (`diff` vazio).
- **Bug real encontrado e corrigido** (Rule 1, ver Deviations): a extracao inicial de `function_table_count` casava `64` de `uint64_t` em vez do valor real, dando `51917` -> `64` na primeira tentativa de congelar o baseline. Corrigido e reconfirmado com valores certos.
- **As tres provas de rejeicao exigidas** ("regra de ouro"): cada perna forcada a falhar isoladamente numa copia scratch de `recomp_macos_v3` (nunca no lift real -- hash MD5 confirmado identico antes/depois de cada prova), `accept_relift.sh` a sair `rc=1` e a nomear corretamente a perna que falhou nas tres vezes.

## Corrida real contra `recomp_macos_v3` (aceite)

Comando: `cd ../gow2-recomp && ./accept_relift.sh recomp_macos_v3 6 /tmp`

Primeira corrida (com o bug do `uint64_t` ainda presente, ver Deviations):

```
PERNA 1 (smoke)          PASS   (OK=5/6)
PERNA 2 (verify_lift)    PASS   (rc=0)
PERNA 3 (apply_patches)  PASS   (NO-MATCH=0 FAILED-fora-PROBE=0, UNVERIFIED=62 informativo)
CONTADORES (D-5.2)       PASS   (imp_modules/imp_imports/orfaos bloqueantes)   <- valores errados (64/64), corrigido a seguir
ACEITE: rc=0
```

Segunda corrida (apos o fix do bug de extracao, `COUNTERS_BASELINE.tsv` recongelado):

```
==============================================================
 RELATORIO DE ACEITE (D-5.1 + D-5.2)
==============================================================
PERNA 1 (smoke)          PASS   (OK=5/6)
PERNA 2 (verify_lift)    PASS   (rc=0)
PERNA 3 (apply_patches)  PASS   (NO-MATCH=0 FAILED-fora-PROBE=0, UNVERIFIED=62 informativo)
CONTADORES (D-5.2)       PASS   (imp_modules/imp_imports/orfaos bloqueantes)

ACEITE: rc=0 -- as tres pernas + contadores bloqueantes passaram.
```

Secao de contadores (segunda corrida, valores corretos, `COUNTERS_BASELINE.tsv` congelado nesta mesma corrida):

```
function_table_count     producao=51917    candidato=51917    delta=0      [INFORMATIVO -- delta justificado no relatorio de aceite]
boot_lifted_functions    producao=52068    candidato=52068    delta=0      [INFORMATIVO -- delta justificado no relatorio de aceite]
imp_modules              producao=13       candidato=13       delta=0      [BLOQUEANTE]
imp_imports              producao=151      candidato=151      delta=0      [BLOQUEANTE]
orfaos                   producao=0        candidato=0        delta=0      [BLOQUEANTE]
```

**Nota honesta sobre a perna 1:** a primeira corrida deu `6/6` e a segunda `5/6` (`run4` classificado `REGRESSAO_SUSPEITA`, `st620_max=0`, `log_lines=2840`, sem `allocate(0x7D00000)`) -- ambas passam o portao (`>=4 de 6`), e a variancia entre corridas confirma a nota do 05-CONTEXT.md ("tipicamente 5-6 de 6"; um run preso costuma ser timing). Nenhuma corrida foi descartada nem re-executada para "melhorar" o numero -- os dois resultados reais estao citados aqui tal como saíram.

`apply_all_patches.sh --status` (perna 3, TSV real): `TOTAL: 87 patches | ALREADY-APPLIED=22 NO-MATCH=0 UNVERIFIED=62 FAILED=3 SKIPPED=0` -- os 3 `FAILED` sao todos classe `PROBE` (confirmado, fora do gate), por isso `FAILED-fora-de-PROBE=0`. `rc` bruto do comando = `1` (por causa do `UNVERIFIED=62`) mas `leg3_pass=1` -- a divergencia entre estes dois numeros e' exatamente o mecanismo de D-5.1 em acao, nao um bug.

`verify_lift.sh` (perna 2): `lift_parity PASS / MANIFEST PASS / audit_boundaries PASS`, `rc=0` -- ja delta-aware desde o Plano 05-02 (a divida de 36 tokens `GRUPO:opd-dispatch` fica congelada e conhecida, nao bloqueia).

## Prova de rejeicao (a "regra de ouro" -- accept_relift.sh REJEITA de verdade)

Todas as tres provas correram contra COPIAS SCRATCH de `recomp_macos_v3` (`recomp_macos_v3_scratch_leg1/2/3`), apagadas logo a seguir a cada teste. O `recomp_macos_v3` real foi confirmado intacto por hash MD5 do corpus (`cat ppu_recomp_*.cpp | md5`) ANTES e DEPOIS das tres provas: `cf414438b43ec4874ebed455f6e16148` (identico nas quatro medicoes -- antes, apos leg1, apos leg2, apos leg3).

### Perna 1 -- build quebrado (`#error` injetado em `ppu_recomp_006.cpp`)

Mutacao: uma linha `#error PROVA_DE_REJEICAO_ACCEPT_RELIFT_LEG1` a seguir aos `#include` do chunk 006.

Comando: `./accept_relift.sh recomp_macos_v3_scratch_leg1 1 /tmp`

```
---- PERNA 1: smoke_relift_equiv.sh ----
=== a construir .../boot_gow2_v3_scratch_leg1 a partir de .../recomp_macos_v3_scratch_leg1 (FORCE_REBUILD_LIFT=1) ===
=== 1. lifted chunks -> .o  ...
ERRO: build_macos.sh falhou (rc=1) para .../recomp_macos_v3_scratch_leg1 -- nao vou correr um binario desactualizado
PERNA 1: OK=0 de 1, rc=1
...
PERNA 1 (smoke)          FAIL   (OK=0/1)
PERNA 2 (verify_lift)    PASS   (rc=0)
PERNA 3 (apply_patches)  PASS   (NO-MATCH=0 FAILED-fora-PROBE=0, UNVERIFIED=62 informativo)
CONTADORES (D-5.2)       FAIL   (imp_modules/imp_imports/orfaos bloqueantes)   <- valores em falta (candidato nao mediu, build falhou)

REJEITADO: rc=1 -- pelo menos uma perna ou os contadores bloqueantes falharam (ver PASS/FAIL acima).
```

**rc real do processo (medido separadamente, sem `tee`, via `$?` direto): `1`.** As pernas 2 e 3 correram ate ao fim mesmo com a perna 1 a falhar (confirma a regra "nunca abortar cedo").

### Perna 2 -- regressao real via lift_parity (removida 1 chamada `ps3_call_opd` de `func_0014A01C`, `ppu_recomp_001.cpp:30608`)

Mutacao: a linha `ps3_call_opd(ctx, (uint32_t)ctx->gpr[10]); DRAIN_TRAMPOLINE(ctx);` substituida por um comentario (removida por completo, nao trocada por `ps3_indirect_call` -- essa troca NAO altera a contagem combinada `ps3_indirect_call+ps3_call_opd` que o MANIFEST usa, confirmado por tentativa fallida antes desta).

Comando: `./accept_relift.sh recomp_macos_v3_scratch_leg2 1 /tmp`

```
---- PERNA 2: verify_lift.sh ----
lift_parity          FAIL
MANIFEST             PASS
audit_boundaries     PASS
PERNA 2: rc=1 (log completo: /tmp/accept_recomp_macos_v3_scratch_leg2_verify.log)
...
PERNA 1 (smoke)          PASS   (OK=1/1)
PERNA 2 (verify_lift)    FAIL   (rc=1)
PERNA 3 (apply_patches)  PASS   (NO-MATCH=0 FAILED-fora-PROBE=0, UNVERIFIED=62 informativo)
CONTADORES (D-5.2)       PASS   (imp_modules/imp_imports/orfaos bloqueantes)

REJEITADO: rc=1 -- pelo menos uma perna ou os contadores bloqueantes falharam (ver PASS/FAIL acima).
```

**rc real do processo: `1`.** Detalhe do `lift_parity`: `current=78 baseline=77 novos=1`, `NOVO 0x002B0AA8/CALL_MISMATCH` -- a EA nova reportada esta na vizinhanca exata da linha mutada, confirmando causalidade (nao ruido). Curiosidade honesta: o `MANIFEST` (grupo `opd-dispatch`) NAO detetou esta mutacao especifica (`current=36 baseline=36`, inalterado) -- a regressao foi apanhada pelo `lift_parity`, nao pelo `MANIFEST` desta vez; ainda assim a perna 2 como um todo rejeitou corretamente (`rc=1`), que e o que D-5.1 exige.

### Perna 3 -- `NO-MATCH` real (removidas as 2 chamadas `ps3_call_opd` de `func_0014A01C`, `ppu_recomp_000.cpp:281499` e `:281512`)

Mutacao: as 2 linhas exigidas pelo contrato `opd-14a01c-a`/`opd-14a01c-b` (`CONTRACTS.tsv`: `count_ge ps3_call_opd( 2` em cada uma das duas funcoes) substituidas por comentarios -- derruba a pos-condicao do patch `patch_14a01c_opd.py` (classe `FUNCIONAL`, nao `PROBE`).

Comando: `./accept_relift.sh recomp_macos_v3_scratch_leg3 1 /tmp`

```
---- PERNA 3: apply_all_patches.sh --status (rc bruto ignorado para o gate) ----
PERNA 3: NO-MATCH=1 FAILED-fora-de-PROBE=0 (UNVERIFIED=62, informativo, nao bloqueia)
PERNA 3: rc bruto (inclui UNVERIFIED, nao bloqueia D-5.1)=1 | leg3_pass=0
...
PERNA 1 (smoke)          PASS   (OK=1/1)
PERNA 2 (verify_lift)    FAIL   (rc=1)
PERNA 3 (apply_patches)  FAIL   (NO-MATCH=1 FAILED-fora-PROBE=0, UNVERIFIED=62 informativo)
CONTADORES (D-5.2)       PASS   (imp_modules/imp_imports/orfaos bloqueantes)

REJEITADO: rc=1 -- pelo menos uma perna ou os contadores bloqueantes falharam (ver PASS/FAIL acima).
```

**rc real do processo: `1`.** Linha exata do TSV3 confirmando a causa: `patch_14a01c_opd.py	NO-MATCH	FUNCIONAL` (via `grep -P '\tNO-MATCH\t' /tmp/accept_recomp_macos_v3_scratch_leg3_status.tsv`). A perna 2 tambem rejeitou nesta prova (efeito colateral esperado -- a mesma mutacao tambem regride `lift_parity`/`MANIFEST`), mas a perna 3 e a que esta explicitamente sob prova aqui e o `NO-MATCH=1` (classe `FUNCIONAL`, fora de `PROBE`) e' a causa inequivoca do `leg3_pass=0`.

## Task Commits

1. **Task 1: as tres pernas (D-5.1) contra `recomp_macos_v3`** - `fdf1f48` (feat, `gow2-recomp`)
2. **Task 2: contadores por delta (D-5.2), baseline, mirror** - `a3cca16` (feat, `gow2-recomp`) + `757a40b` (feat, `ps3recomp`/`games/gow2`, mirror + `COUNTERS_BASELINE.tsv`)

## Files Created/Modified

- `../gow2-recomp/accept_relift.sh` (221 linhas) -- orquestrador real: cabecalho explicando D-5.1/D-5.2 e a licao de 2026-07-26, resolucao de caminho + ponte `PS3_ENGINE_ROOT`, tres pernas + secao de contadores + relatorio final + rc = AND de tudo.
- `games/gow2/accept_relift.sh` -- mirror byte-identico (`diff` vazio, confirmado).
- `games/gow2/lift_baseline/COUNTERS_BASELINE.tsv` -- baseline congelado por medicao real contra a producao (`recomp_macos_v2`/`boot_gow2`): `function_table_count=51917`, `boot_lifted_functions=52068`, `imp_modules=13`, `imp_imports=151`, `orfaos=0`. Cabecalho cita explicitamente que `56072`/`56223` (ROADMAP.md/REQUIREMENTS.md) sao do lift antigo escrito a mao.

## Decisions Made

- **Perna 3 nunca le o `rc` bruto** de `apply_all_patches.sh` -- decisao central de D-5.1, implementada e provada: `rc` bruto=1 (UNVERIFIED=62) vs `leg3_pass=1` (NO-MATCH=0, FAILED-fora-PROBE=0) na corrida de aceite real -- os dois numeros divergem de proposito.
- **As tres pernas correm sempre**, mesmo que a primeira falhe -- confirmado na prova da perna 1: o build falhou e as pernas 2/3 ainda assim correram e reportaram o seu proprio resultado.
- **Contadores congelados uma so vez**, na primeira corrida em que `COUNTERS_BASELINE.tsv` nao existe, e reaproveitados nas seguintes -- evita repetir a medicao de producao a cada corrida do aceite.
- **Reaproveitamento de dados da perna 1** para os contadores do candidato (`lifted`/`modules`/`imports` extraidos do mesmo `TSV1`, ultima corrida) -- nao corre outro smoke so para os contadores, como o plano exigia.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Extracao de `function_table_count` casava o `64` de `uint64_t` em vez do valor real**
- **Found during:** Task 2, primeira corrida real de `./accept_relift.sh recomp_macos_v3 6 /tmp` (a mesma corrida que congelou o `COUNTERS_BASELINE.tsv` pela primeira vez)
- **Issue:** A linha do lift e' `const uint64_t function_table_count = 51917;`. O comando `grep -h "function_table_count = " ... | grep -oE '[0-9]+' | head -1` (literal do action step 1 do plano) casa TODOS os numeros da linha na ordem em que aparecem -- e o `64` de `uint64_t` aparece ANTES do `51917` na mesma linha. `head -1` pegou o `64`, nao o valor real. Resultado: `COUNTERS_BASELINE.tsv` foi congelado com `function_table_count=64` em vez de `51917`, e o candidato tambem media `64` (mesmo bug, mesmo ficheiro-alvo) -- coincidentemente `delta=0` (ambos errados igualmente), o que teria escondido o bug se nao fosse verificado contra o valor esperado conhecido (51917, medido manualmente antes de escrever o script).
- **Fix:** Extracao corrigida em `counters_baseline_freeze()` (producao) e no calculo do candidato: `grep -oE '= [0-9]+' | grep -oE '[0-9]+'` -- isola primeiro a substring `"= 51917"` (que so casa apos o sinal de igual, nunca dentro de `uint64_t`) e so depois extrai o numero.
- **Files modified:** `../gow2-recomp/accept_relift.sh` (mesmo ficheiro da Task 2, antes do commit)
- **Verification:** apaguei o `COUNTERS_BASELINE.tsv` errado e corri `./accept_relift.sh recomp_macos_v3 6 /tmp` de novo -- baseline recongelado com `function_table_count=51917`, candidato `51917`, `delta=0` (valor correto, confirmado manualmente antes via `grep` direto no shell).
- **Committed in:** `a3cca16` (o bug nunca chegou a ser commitado -- corrigido antes do commit da Task 2)

---

**Total deviations:** 1 auto-fixed (Rule 1, bug de extracao)
**Impact on plan:** Nenhum impacto de escopo -- o bug foi encontrado e corrigido DENTRO da mesma task antes do commit; o `COUNTERS_BASELINE.tsv` commitado ja tem o valor certo (`51917`), nunca o errado (`64`).

## Issues Encountered

- Nenhum bloqueio. A unica surpresa foi a variancia natural da perna 1 entre corridas (6/6 na primeira corrida, 5/6 na segunda) -- ja documentada no 05-CONTEXT.md como esperada ("tipicamente 5-6 de 6"), nao um bug do `accept_relift.sh`.
- Na prova de rejeicao da perna 2, a mutacao (remover 1 `ps3_call_opd`) foi apanhada pelo `lift_parity`, nao pelo `MANIFEST` como eu esperava a priori (a contagem do grupo `opd-dispatch` do MANIFEST ficou inalterada -- possivelmente porque conta so um subconjunto especifico de padroes, nao a soma simples de `ps3_call_opd(ctx,`). Documentado honestamente acima em vez de forcar uma segunda mutacao so para acertar no sub-passo "certo" -- o requisito e' que a PERNA 2 rejeite, e ela rejeitou (`rc=1`).

## User Setup Required

None - nenhuma configuracao de servico externo necessaria.

## Next Phase Readiness

- `accept_relift.sh` esta pronto para ser consumido pelo Plano 05-04 (promocao): rc=0 real contra `recomp_macos_v3` confirmado, e as tres provas de rejeicao confirmam que o gate NAO deixa passar uma regressao disfarcada -- exatamente a garantia que faltava em 2026-07-26.
- `COUNTERS_BASELINE.tsv` esta congelado e commitado; qualquer lift futuro sera medido por delta contra ele, nao contra os absolutos desatualizados do ROADMAP.md.
- `ctest --test-dir build-tests`: 22/22 (2 py skipped) -- sem regressao do motor.
- Nenhum orfao de processo (`pgrep -fl boot_gow2` vazio apos todas as corridas); `recomp_macos_v3` real confirmado intacto por hash em cada uma das provas de rejeicao.
- **Nao resolvido nesta task, propositadamente fora de escopo:** o `MANIFEST` nao ter apanhado a mutacao da prova da perna 2 (ficou para o `lift_parity` apanhar) e uma observacao, nao um bug -- fica registado aqui para quem for investigar a fundo o `gen_manifest.py`/grupo `opd-dispatch` no futuro.

## Self-Check: PASSED

- FOUND: `../gow2-recomp/accept_relift.sh`
- FOUND: `games/gow2/accept_relift.sh`
- FOUND: `games/gow2/lift_baseline/COUNTERS_BASELINE.tsv`
- FOUND (gow2-recomp): `fdf1f48`, `a3cca16`
- FOUND (ps3recomp): `757a40b`

---
*Phase: 05-smoke-de-equival-ncia-e-promo-o*
*Completed: 2026-07-29*
