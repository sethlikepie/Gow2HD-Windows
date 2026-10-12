---
phase: 04-cat-logo-de-patches-como-gate-que-falha
verified: 2026-07-29T00:00:00Z
status: passed
score: 5/5 must-haves verified (ROADMAP Success Criteria)
behavior_unverified: 0
overrides_applied: 0
---

# Phase 4: Catálogo de patches como gate que falha — Verification Report

**Phase Goal:** Os scripts `patch_*.py` deixam de ser folclore de sessão e viram um gate
automatizado: ou aplicam-se limpos sobre o lift novo, ou o comando sai não-zero apontando
qual falhou.
**Verified:** 2026-07-29 (re-execução direta dos comandos, não aceitação do SUMMARY)
**Status:** passed
**Re-verification:** No — verificação inicial

## Método

Nada abaixo foi aceite por citação de SUMMARY. Cada afirmação com evidência "MEDIDO
nesta verificação" foi re-corrida por mim nesta sessão, contra o repositório real
(`../gow2-recomp`) e contra o monorepo (`ps3recomp`), sempre em cópias `/tmp` ou
`recomp_macos_v3_*_scratch` — nunca contra `recomp_macos_v2` (produção) ou contra
`recomp_macos_v3` in-place. Hashes de `recomp_macos_v2` e `recomp_macos_v3` foram
tirados antes e depois de todas as corridas e batem exatamente; todos os diretórios
scratch foram apagados no fim.

## Goal Achievement

### Observable Truths (5 Success Criteria do ROADMAP, números corrigidos por 04-CONTEXT.md)

| # | Truth (ROADMAP) | Status | Evidência (medida nesta sessão) |
|---|---|---|---|
| 1 | `PATCH_CATALOG.tsv` com **todos** os `patch_*.py` classificados, zero indeterminados | ✓ VERIFIED | Regenerei `gen_catalog.py ../gow2-recomp/recomp_mid_v2 MANIFEST.tsv` com o comando **exato** do cabeçalho do próprio ficheiro — `diff` contra o `PATCH_CATALOG.tsv` commitado deu vazio (byte-idêntico). 87 patches reais no diretório que `apply_all_patches.sh` de facto corre, 64 FUNCIONAL / 23 PROBE, zero linhas fora de `{PROBE,FUNCIONAL}`. Testei também a propriedade "nenhum patch fica invisível": adicionei um `patch_zz_verificador_novo.py` sintético a uma cópia do diretório — foi capturado automaticamente como linha 88, classe `FUNCIONAL/misc`, nunca indeterminado nem omitido (responde à pergunta 5 do briefing). |
| 2 | `./apply_all_patches.sh <lift> --status <tsv>` existe e reporta **zero** `FAILED` fora de `PROBE` | ✓ VERIFIED | `--status` não existia antes desta fase (confirmado no `04-CONTEXT.md`); hoje existe e produz TSV `patch\tstatus\tclasse` com 88 linhas (87+cabeçalho). Corri duas vezes contra uma cópia scratch completa de `recomp_macos_v3`: `TOTAL: 87 patches \| APPLIED=0 ALREADY-APPLIED=22 NO-MATCH=0 UNVERIFIED=62 FAILED=3 SKIPPED=0`, resultado idêntico nas duas corridas (determinismo). Os 3 `FAILED` (`patch_cc9d0_live_yield.py`, `patch_ce03c_pre_play_stop.py`, `patch_ce03c_wait_idle_f2b_movie.py`) são **todos** classe `PROBE` em `PATCH_CATALOG.tsv` — confirmado por grep cruzado. Zero `FAILED` fora de `PROBE`, literalmente. |
| 3 | O comando sai **não-zero** quando um patch não aplica, provado por **injeção de falha**, não só caminho feliz | ✓ VERIFIED | Reproduzido ponta-a-ponta por mim (ver secção dedicada abaixo), com as duas correções pedidas pelo utilizador: (a) quebrar **dentro** da região de `func_0032E200` dispara os **dois** contratos complementares (`opd-32e200` e `opd-32e200-noindirect`); quebrar a mesma agulha **fora** da função (1ª ocorrência do chunk, que pertence a outra função) corretamente **não** dispara nada — prova de que a delimitação por função funciona, não só por texto solto. (b) Corrida `apply_all_patches.sh` **inteira** contra uma cópia scratch completa de `recomp_macos_v3`: antes da injeção, `patch_32e200_opd.py` = `ALREADY-APPLIED`; depois de reverter 1 dos 7 sítios para a forma pré-conversão, `patch_32e200_opd.py` = `NO-MATCH` e o `rc` do script inteiro passa de `0`-por-esse-patch a fazer parte de um `rc=1` global — nomeando exatamente o patch quebrado no relatório e no TSV. |
| 4 | Os patches classe `PROBE` ficam fora do gate, listados **nominalmente** | ✓ VERIFIED | 23 PROBE no catálogo, **0** com coluna `razao` vazia (`awk` confirmou). No relatório de `apply_all_patches.sh`, cada PROBE aparece com o sufixo `(PROBE, fora do gate)` e é excluído dos contadores `_gate` que decidem o `rc` — confirmado pelos 3 `FAILED` (todos PROBE) não impedirem `n_failed_gate=0`. |
| 5 | *(acrescentado D-4.5/D-4.6)* `verify_lift.sh` existe, corre os 3 verificadores por delta contra baselines congelados, e `apply_all_patches.sh` chama-o no fim | ✓ VERIFIED | `games/gow2/verify_lift.sh` existe, executável, chamado dentro de `apply_all_patches.sh` (secção `-- verify_lift.sh (D-4.6) --` visível na saída real). `PARITY_BASELINE.json` (77 ids) e `BOUNDS_BASELINE.json` (180 ids) existem e batem com a contagem re-medida. O passo `MANIFEST` já não é o `rc=1` fixo citado no `04-05-SUMMARY.md` — a Fase 5 (`05-02`, já executada, fora do escopo desta verificação) estendeu o mesmo princípio de delta ao MANIFEST via `manifest_delta_gate.py`; isto é uma evolução compatível com D-4.5, não uma regressão da Fase 4. |

**Score:** 5/5 truths verified (0 present-but-behavior-unverified).

### Required Artifacts

| Artifact | Expected | Status | Details |
|---|---|---|---|
| `games/gow2/lift_baseline/gen_catalog.py` | `classify()` puro por AST, 4 ramos | ✓ VERIFIED | 11/11 testes `[PASS]` (`test_gen_catalog.py`), reconfirmados nesta sessão |
| `games/gow2/lift_baseline/PATCH_CATALOG.tsv` | 87 linhas, derivado | ✓ VERIFIED | Byte-idêntico à regeneração pelo comando documentado no próprio cabeçalho |
| `../gow2-recomp/recomp_mid_v2/patch_14a01c_opd.py` + mirror `games/gow2/...` | tri-estado, `EXPECTED_SITES=4` | ✓ VERIFIED | `diff` entre os dois ficheiros vazio (confirmei eu próprio); `EXPECTED_SITES=4`/`RC_NO_EFFECT=3` presentes nos dois. **Nota:** `04-02-SUMMARY.md` afirma que o espelho do monorepo "continua com a versão antiga" — **isto está desatualizado**: um commit posterior (`1f9dfed`, `fix(fase4-02): mirror do fix...`) corrigiu o espelho antes do fecho da fase. O estado atual do código está correto; só o texto do SUMMARY ficou obsoleto (ver Gaps/Observações). |
| `games/gow2/lift_baseline/check_contracts.py` | motor de verificação, rc 0/1/2/3 | ✓ VERIFIED | 7/7 testes `[PASS]`; rc=0/1/2 reproduzidos por mim contra o lift real e contra cópias com falha injetada |
| `games/gow2/lift_baseline/CONTRACTS.tsv` | ≥ família OPD + cruzamento TAG | ✓ VERIFIED | 56 linhas de dados, 22 patches distintos cobertos (medido: `cut -f2 \| sort -u` = 22, bate exatamente o SUMMARY) |
| `games/gow2/lift_baseline/gen_contracts_candidates.py` | deriva candidatos TAG-MANIFEST | ✓ VERIFIED | Re-corrido: 50 linhas candidatas, idêntico ao que está em `CONTRACTS.tsv` |
| `games/gow2/verify_lift.sh` | orquestrador 3 verificadores | ✓ VERIFIED | Existe, executável, corrido com sucesso contra cópia scratch |
| `games/gow2/lift_baseline/baseline_delta.py` | comparador genérico por delta | ✓ VERIFIED | 5/5 testes `[PASS]` |
| `../gow2-recomp/apply_all_patches.sh` + mirror | 6 estados, `--status`, chama `verify_lift.sh` | ✓ VERIFIED | Código lido linha a linha; comportamento reproduzido em 3 corridas reais nesta sessão (caminho feliz, isolado, e injeção fim-a-fim) |

### Key Link Verification

| From | To | Via | Status | Details |
|---|---|---|---|---|
| `apply_all_patches.sh` | `PATCH_CATALOG.tsv` | `classe_de()` via `awk` | ✓ WIRED | Lookup portável ao bash 3.2 do macOS (achado próprio da Fase 4, corrigido); confirmei que PROBE recebe sufixo e é excluído do gate |
| `apply_all_patches.sh` | `check_contracts.py`/`CONTRACTS.tsv` | chamada no ramo `rc==0 && conteúdo inalterado` | ✓ WIRED | Reproduzi a chamada isolada e a chamada dentro do loop completo; mapeamento 0→ALREADY-APPLIED / 1→NO-MATCH / 2,outro→UNVERIFIED confirmado por leitura do `case` e por execução real |
| `apply_all_patches.sh` | `verify_lift.sh` | chamada após `run_checks()` | ✓ WIRED | Secção "-- verify_lift.sh (D-4.6) --" presente na saída real; `verify_lift_rc` entra no `OR` final (`if ... \|\| [ "$verify_lift_rc" -ne 0 ]`) |
| `verify_lift.sh` | `lift_parity.py` / `audit_boundaries.py` / `baseline_delta.py` | orquestração por delta | ✓ WIRED | Corri `verify_lift.sh` contra a cópia scratch: `lift_parity PASS`, `audit_boundaries PASS`, `MANIFEST PASS` (Fase 5 já converteu este terceiro passo também para delta) |
| `gen_catalog.py` | `../gow2-recomp/recomp_mid_v2` (não o espelho `games/gow2/recomp_mid_v2`, 75/87 desatualizado) | argumento posicional explícito | ✓ WIRED | Confirmado: o catálogo committed cita 87 no diretório certo; o espelho do monorepo continua com menos ficheiros, mas isso é fora de escopo da fase (autorização de `git subtree push` pendente, tal como declarado) |

### Prova de injeção de falha (reproduzida por mim, ponta-a-ponta)

**Camada 1 — `check_contracts.py` isolado, com controlo negativo:**

```
# quebrar FORA da regiao de func_0032E200 (1a ocorrencia do chunk, de outra funcao)
$ check_contracts.py CONTRACTS.tsv <copia> --patch patch_32e200_opd.py
[PASS] opd-32e200 ... esperado 7 encontrado 7
[PASS] opd-32e200-noindirect ... esperado 0 encontrado 0
rc=0                                    <- correto: a delimitacao por funcao protege

# quebrar DENTRO da regiao de func_0032E200 (reverter 1 sitio para pre-conversao)
$ check_contracts.py CONTRACTS.tsv <copia> --patch patch_32e200_opd.py
[FAIL] opd-32e200 ... esperado 7 encontrado 6
[FAIL] opd-32e200-noindirect ... esperado 0 encontrado 1
rc=1                                    <- os DOIS contratos complementares disparam
```

**Camada 2 — `apply_all_patches.sh` inteiro, contra cópia scratch completa do lift:**

```
$ ./apply_all_patches.sh recomp_macos_v3_e2e_fault --status /tmp/status_e2e_fault.tsv
...
NO-MATCH         patch_32e200_opd.py
...
TOTAL: 87 patches | APPLIED=0 ALREADY-APPLIED=21 NO-MATCH=1 UNVERIFIED=62 FAILED=3 SKIPPED=0
NO-MATCH (pos-condicao falsa, D-4.2): patch_32e200_opd.py
$ echo $?
1
```

`patch_32e200_opd.py` nomeado exatamente no relatório e no `--status` TSV; `rc=1` do
script inteiro. Antes/depois: `recomp_macos_v3` real (`md5 -q ppu_recomp_003.cpp` =
`f0871682e32ffd268ec5fce250891ef9`) e `recomp_macos_v2` real (hash do conjunto
`*.cpp`+`*.h` = `1d3fcbaea49ddd61d4046329249bab17`) permanecem intactos — confirmado
antes e depois de toda a sessão de verificação. Diretórios scratch apagados.

### Behavioral Spot-Checks

| Behavior | Command | Result | Status |
|---|---|---|---|
| `PATCH_CATALOG.tsv` é derivado, não escrito à mão (D-4.3) | regenerar com o comando do próprio cabeçalho, `diff` | byte-idêntico | ✓ PASS |
| Um patch novo nunca fica invisível ao catálogo | injetar `patch_zz_verificador_novo.py` sintético | classificado `FUNCIONAL/misc`, linha 88, nunca omitido | ✓ PASS |
| `gen_contracts_candidates.py` reproduz `CONTRACTS.tsv` (parte TAG) | re-correr contra o corpus real | 50 linhas, idêntico ao commitado | ✓ PASS |
| `ctest --test-dir build-tests` (sem `-DCMAKE_BUILD_TYPE=Release`) | baseline do motor | 100% (22/22), 2 skipped (pré-existente) | ✓ PASS |
| Fault injection isolada (`check_contracts.py`) | ver acima | rc 0 (controlo negativo) / rc 1 (positivo, 2 FAILs) | ✓ PASS |
| Fault injection fim-a-fim (`apply_all_patches.sh`) | ver acima | `NO-MATCH` nomeado, `rc=1` do script inteiro | ✓ PASS |

### Requirements Coverage

| Requirement | Source Plan | Description | Status | Evidence |
|---|---|---|---|---|
| `REQ-RDY0-5` | 04-01..04-05 | Catálogo de patches como gate que falha | ✓ SATISFIED | `.planning/REQUIREMENTS.md:215-216,519` marca `[COMPLETO]`; as 5 Success Criteria do ROADMAP verificadas independentemente nesta sessão |

Nenhum requisito órfão encontrado (REQUIREMENTS.md mapeia só `REQ-RDY0-5` para a Fase 4,
e é o único citado nos 5 PLANs).

### Anti-Patterns Found

Nenhum bloqueador. Verificação por leitura de `apply_all_patches.sh`, `check_contracts.py`,
`gen_catalog.py`, `verify_lift.sh`, `baseline_delta.py`, `gen_contracts_candidates.py`
(todos os ficheiros criados/modificados nesta fase): sem `TODO`/`FIXME`/`XXX`/`HACK`
sem referência de follow-up, sem retorno vazio disfarçado de sucesso, sem dados
hardcoded a fingir dinamismo. Os `SEM-CONTRATO`/`UNVERIFIED` (42 patches) são
explicitamente declarados como dívida conhecida em `CONTRACTS.tsv`, `04-04-SUMMARY.md`
e `04-05-SUMMARY.md` — não escondidos, não maquiados, exatamente o padrão que
`CLAUDE.md` regra 4 exige.

## Resposta às 5 perguntas céticas do briefing

**1. O `rc=1` global é honesto ou é um gate que ninguém vai poder usar?**

É dívida declarada, não um gate natimorto — mas com uma limitação real de desenho que
vale registar para a continuação. Distingo dois níveis:

- **O critério 2 do ROADMAP ("zero FAILED fora de PROBE") está satisfeito literalmente**,
  medido por mim: 3 FAILED, todos PROBE. Este é o número que o ROADMAP pede, e bate.
- **O `rc` GLOBAL do script (que também soma `NO-MATCH`/`UNVERIFIED` fora de PROBE) fica
  em `1`** porque 42 dos 64 patches FUNCIONAL não têm contrato declarado (62 UNVERIFIED
  = 42 FUNCIONAL sem contrato + 20 PROBE sem contrato; confirmei a soma: 22 ALREADY-APPLIED
  + 42 UNVERIFIED-FUNCIONAL = 64 FUNCIONAL; 20 UNVERIFIED-PROBE + 3 FAILED-PROBE = 23 PROBE).
  Isto é dívida **conhecida e citada** desde o `04-04-SUMMARY.md` ("22/64 cobertos, nao
  fingir cobertura total") — não é uma surpresa nem uma tentativa de esconder nada.

O que faz este gate **não** ser natimorto: ele **detecta regressão de verdade**, provado
por mim em duas camadas independentes (isolada e fim-a-fim) — o defeito original da fase
(um patch correr, não fazer nada, e sair verde) está **fechado**. Um consumidor que olhe
para `NO-MATCH` especificamente (via `--status` TSV) tem sinal accionável desde já.

O que **limita** o uso do `rc` global como gate binário de CI hoje: como o cálculo do
`rc` soma `UNVERIFIED` por **valor absoluto** (não por delta contra um baseline congelado,
ao contrário do que `verify_lift.sh` faz para `lift_parity`/`audit_boundaries`/MANIFEST
via D-4.5), o `rc` global do `apply_all_patches.sh` vai ficar permanentemente `1`
enquanto `CONTRACTS.tsv` não cobrir os 64 FUNCIONAL inteiros — mesmo que nada tenha
regredido. Isto é exatamente a mesma classe de "limiar absoluto pune melhorias/nunca
zera" que a própria Fase 4 identificou e corrigiu para os outros três verificadores
(D-4.5), mas que **não foi estendida ao contador `UNVERIFIED` do próprio
`apply_all_patches.sh`**. Não é um defeito desta fase — nenhum plano prometeu isso, e
o `04-05-SUMMARY.md` é explícito sobre o `rc=1` esperado — mas é uma lacuna de desenho
real que vale nomear: hoje, `apply_all_patches.sh $?` sozinho não distingue "regredi"
de "ainda tenho a mesma dívida de sempre". Um `--status` diffado contra uma corrida
anterior já resolveria isto sem mudar o script. Recomendação para a fase seguinte:
aplicar o mesmo princípio de delta (D-4.5) ao `UNVERIFIED`/`NO-MATCH` agregados, não só
aos três verificadores de `verify_lift.sh`.

**Veredicto: dívida declarada, não natimorto.** O mecanismo funciona e é accionável por
quem olhar para `NO-MATCH`/`--status`; o `rc` bruto sozinho ainda não é um sinal
binário limpo de "nada regrediu" — é o próximo passo óbvio, não uma falha desta fase.

**2. Prova de injeção de falha, ponta-a-ponta, numa cópia (nunca `recomp_macos_v2`)**

Feita e documentada acima em duas camadas. Resultado: `rc=1`, com o patch nomeado
(`patch_32e200_opd.py` → `NO-MATCH`) tanto na saída humana quanto no `--status` TSV.
Controlo negativo confirmado: quebrar a mesma agulha fora da função-alvo não dispara
nada (`rc=0`) — prova de que a delimitação por região (`void func_XXXXXXXX(` até ao
próximo `void func_`) funciona e não é um gatilho cego por substring. `recomp_macos_v2`
e `recomp_macos_v3` reais confirmados intactos por hash antes/depois.

**3. `PATCH_CATALOG.tsv` é mesmo DERIVADO (D-4.3) ou foi escrito à mão?**

Derivado, confirmado por regeneração byte-idêntica usando o comando exato citado no
próprio cabeçalho do ficheiro. Testei também a propriedade de cobertura automática
(um patch novo nunca fica invisível) injetando um ficheiro sintético — foi capturado
corretamente.

**4. Os 23 PROBE estão fora do gate mas listados nominalmente COM razão (D-4.4)?**

Sim. 23 linhas classe PROBE, zero com coluna `razão` vazia, cada uma com o suffix
`(PROBE, fora do gate)` na saída de `apply_all_patches.sh` e excluída dos contadores
`_gate` que decidem o `rc`.

**5. Há algum patch que o catálogo não conheça? O teste devia falhar nesse caso.**

Testado por mim: injetei `patch_zz_verificador_novo.py` (sintético) num diretório
cópia — `gen_catalog.py` capturou-o automaticamente (linha 88, `FUNCIONAL/misc`), a
contagem de linhas de dados acompanhou o `len(list(PATCH_DIR.glob(...)))` em tempo de
execução, nunca um `87` fixo morto. O teste de cobertura de `test_gen_catalog.py`
(`check_cobertura_87_87_contra_corpus_real`) já cobre esta propriedade de forma
equivalente contra o corpus real.

## Achado desta verificação: nota do SUMMARY 04-02 está desatualizada (não bloqueante)

`04-02-SUMMARY.md` (escrito pelo orquestrador após uma falha de API a meio da execução)
afirma: *"O espelho `games/gow2/recomp_mid_v2/patch_14a01c_opd.py` no monorepo continua
com a versão antiga"*. Medi eu próprio: **isto não é verdade hoje** — `diff` entre
`../gow2-recomp/recomp_mid_v2/patch_14a01c_opd.py` e o espelho do monorepo dá vazio, e
um commit posterior (`1f9dfed`, `fix(fase4-02): mirror do fix patch_14a01c_opd.py +
posse unica icg_ctor (D-4.7)`) fechou exatamente essa lacuna antes do fecho da fase.
Não bloqueia nada — o **estado atual do código** (o que importa para esta verificação)
está correto — mas é um exemplo concreto de por que este agente não aceita SUMMARYs
como prova: o texto do 04-02-SUMMARY ficou obsoleto face a um commit de correção
posterior, e só a releitura direta do repositório revelou isso.

## Gaps Summary

Nenhum gap bloqueante. Os 5 critérios de sucesso do ROADMAP (com os números corrigidos
pelo `04-CONTEXT.md`) estão verificados por execução direta, não por citação de
SUMMARY, incluindo a prova de injeção de falha ponta-a-ponta que o próprio ROADMAP
exige explicitamente (critério 3).

Duas observações não-bloqueantes, já para a continuação:

1. **`rc` global de `apply_all_patches.sh` não usa delta para `UNVERIFIED`** (ver
   resposta à pergunta 1) — recomendação para uma fase futura, não uma lacuna desta.
2. **`04-02-SUMMARY.md` tem uma afirmação desatualizada** sobre o estado do espelho do
   monorepo (corrigida por um commit posterior, `1f9dfed`) — cosmético, documental, sem
   impacto no código real.

---

*Verified: 2026-07-29*
*Verifier: Claude (gsd-verifier)*
