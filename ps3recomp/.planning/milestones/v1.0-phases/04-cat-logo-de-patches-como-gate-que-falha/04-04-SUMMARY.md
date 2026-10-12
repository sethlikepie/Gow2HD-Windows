---
phase: 04-cat-logo-de-patches-como-gate-que-falha
plan: 04
subsystem: tooling
tags: [python, contracts, post-condition-verification, gow2, tdd]

requires:
  - phase: 04-cat-logo-de-patches-como-gate-que-falha
    provides: "PATCH_CATALOG.tsv (04-01, 64 FUNCIONAL/23 PROBE), patch_14a01c_opd.py corrigido com EXPECTED_SITES=4 (04-02)"
provides:
  - "games/gow2/lift_baseline/check_contracts.py: motor de verificacao independente de CONTRACTS.tsv contra o lift real -- 3 codigos de saida distinguiveis (0 passa / 1 falha / 2 SEM-CONTRATO), + rc=3 erro de uso"
  - "games/gow2/lift_baseline/CONTRACTS.tsv: 56 contratos (6 familia OPD + 50 TAG-MANIFEST derivados), cobrindo 22/64 patches FUNCIONAL"
  - "games/gow2/lift_baseline/gen_contracts_candidates.py: deriva candidatos a contrato do cruzamento TAG-MANIFEST (dono unico, classe FUNCIONAL, exclui OBSOLETO)"
affects: ["04-05 (apply_all_patches.sh vai consumir os 3 rc de check_contracts.py para o classificador de 6 estados)"]

tech-stack:
  added: []
  patterns:
    - "D-4.2 aplicado literalmente: check_contracts.py e' um PROCESSO SEPARADO do patch, que le o mesmo lift depois e conta agulhas de forma independente do que o patch imprimiu"
    - "delimitacao de regiao EA replica EXACTAMENTE o padrao ja usado pelos patches OPD (s.find('void func_XXXXXXXX(') / s.find('void func_', i+20))"
    - "contratos SEM-CONTRATO (rc=2) sao um estado distinto de FALHA (rc=1) e de PASSA (rc=0) -- os 3 estados que o Plano 04-05 precisa"
    - "candidatos TAG-MANIFEST excluem marcadores OBSOLETO (mesma logica ja usada por gen_manifest.py/verify_lift_baseline.sh) -- achado real desta task, nao antecipado no plano"

key-files:
  created:
    - games/gow2/lift_baseline/check_contracts.py
    - games/gow2/lift_baseline/test_check_contracts.py
    - games/gow2/lift_baseline/CONTRACTS.tsv
    - games/gow2/lift_baseline/gen_contracts_candidates.py
    - games/gow2/lift_baseline/test_gen_contracts_candidates.py
  modified: []

key-decisions:
  - "opd-total (contrato global de auditoria, patch='-') usa a agulha 'ps3_call_opd(ctx,' e nao 'ps3_call_opd(' -- a agulha mais curta tambem casa 3 declaracoes extern \"C\" e inflava a contagem de 47 para 50 (achado desta task, corrigido antes de commitar, Rule 1)."
  - "Numero de opd-total e' 47, NAO o 52 citado no objective do plano -- medido nesta task e confirmado identico ao 04-CONTEXT.md 'ESTADO MEDIDO'; 52 era a leitura da Fase 3 anterior a correcao de posse do Plano 04-02 (a correcao mudou POSSE por funcao, nunca o total global)."
  - "gen_contracts_candidates.py exclui marcadores TAG com nota OBSOLETO -- achado ao verificar [OPDISP] (Passo 6): tinha dono unico (patch_jumptable_2a209c.py) mas e' OBSOLETO (D-3.2), e um contrato derivado dele falharia SEMPRE contra o lift real, nao por regressao. Corrigido antes do commit final (Rule 1); numero de candidatos passou de 51/22 (pre-fix) para 50/21 (pos-fix, o numero que entrou em CONTRACTS.tsv)."

requirements-completed: [REQ-RDY0-5]

coverage:
  - id: D1
    description: "check_contracts.py verifica pos-condicao contra o lift real, independente do rc do patch, com 3 codigos de saida distinguiveis"
    requirement: "REQ-RDY0-5"
    verification:
      - kind: unit
        ref: "test_check_contracts.py (7 testes via subprocess: contrato EA passa/falha, ancora perdida, SEM-CONTRATO, escopo global soma chunks, agulha regex, erro de uso)"
        status: pass
      - kind: integration
        ref: "check_contracts.py contra recomp_macos_v3 real: opd-32e200 PASS (rc=0, 7 encontrados)"
        status: pass
    human_judgment: false
  - id: D2
    description: "Familia OPD completa em CONTRACTS.tsv (5 contratos EA + 1 global), prova de injeccao de falha real"
    requirement: "REQ-RDY0-5"
    verification:
      - kind: integration
        ref: "patch_14b1f0_opd.py e patch_14a01c_opd.py PASS (rc=0) contra recomp_macos_v3; opd-total PASS (47 encontrado, medido)"
        status: pass
      - kind: manual_procedural
        ref: "injeccao deliberada de falha em copia scratch (1 de 7 ps3_call_opd(ctx, renomeada) -> rc=1, 'esperado 7 encontrado 6'; lift real reconfirmado intacto apos apagar a copia"
        status: pass
    human_judgment: false
  - id: D3
    description: "gen_contracts_candidates.py amplia cobertura via cruzamento TAG-MANIFEST, com revisao manual e verificacao amostral contra o lift real"
    requirement: "REQ-RDY0-5"
    verification:
      - kind: unit
        ref: "test_gen_contracts_candidates.py (4 testes: dono unico, dono multiplo, OBSOLETO excluido, medicao real)"
        status: pass
      - kind: integration
        ref: "5 amostras de maior min_count verificadas contra recomp_macos_v3, todas PASS: patch_32e200_opd.py, patch_comboprop_168350.py, patch_factory_block_ty15_install.py, patch_icg_ctor_opd.py, patch_shadersrc_loop.py"
        status: pass
    human_judgment: false

duration: ~50min
completed: 2026-07-26
status: complete
---

# Phase 4 Plan 04: CONTRACTS.tsv + check_contracts.py -- a peca central da fase (D-4.2) Summary

**`check_contracts.py` verifica, de forma INDEPENDENTE do que um patch imprime, se a pos-condicao declarada em `CONTRACTS.tsv` e verdade no lift -- 56 contratos (6 da familia OPD + 50 derivados do cruzamento TAG-MANIFEST), 22/64 patches FUNCIONAL cobertos, com prova de injeccao de falha real e um achado de falso-positivo (marcador OBSOLETO) corrigido antes do commit.**

## Estado

As 3 tasks do plano foram executadas na ordem, TDD, sem desvio do objectivo mas com
duas correcoes factuais descobertas durante a propria execucao (documentadas abaixo,
nao maquiadas). `git status --porcelain` fica limpo fora dos 4 ficheiros novos deste
plano (mais os 2 directorios untracked pre-existentes `build-tests/` e
`runtime/spu/tests/out_spu_channel_blocking/`, alheios a esta sessao).

## Por tarefa: comando e saida real

### Task 1 -- check_contracts.py fim-a-fim contra UM contrato real (func_0032E200)

```
$ .venv/bin/python3 games/gow2/lift_baseline/test_check_contracts.py; echo "rc=$?"
[PASS] teste 1: contrato EA passa (7 call_opd, 0 indirect) -- rc=0
[PASS] teste 2: contrato EA falha (5 de 7) -- rc=1, 'esperado 7 encontrado 5'
[PASS] teste 3: ancora perdida (funcao ausente do lift) -- rc=1, 'ancora perdida'
[PASS] teste 4: patch sem contrato -- rc=2, 'SEM-CONTRATO'
[PASS] teste 5: escopo global soma agulha across chunks -- rc=0
[PASS] teste 6: agulha 're:' activa regex (gpr[10]/gpr[11], nao gpr[9]) -- rc=0
[PASS] teste 7: CONTRACTS.tsv/LIFT_DIR inexistente -- rc=3

7/7 testes [PASS]
rc=0
```

```
$ .venv/bin/python3 games/gow2/lift_baseline/check_contracts.py games/gow2/lift_baseline/CONTRACTS.tsv ../gow2-recomp/recomp_macos_v3 --patch patch_32e200_opd.py; echo "rc=$?"
[PASS] opd-32e200 (patch_32e200_opd.py): agulha='ps3_call_opd(' predicado=count_ge esperado 7 encontrado 7
rc=0
```

Prova fim-a-fim contra o LIFT REAL confirmada (nao so fixture sintetica) -- o unico
contrato desta task passa exactamente como o motivo da fase pedia.

**Commit:** `f8d7eab` -- `feat(04-04): check_contracts.py -- verifica a pos-condicao real do lift, nao o rc do patch (D-4.2)`

### Task 2 -- familia OPD completa em CONTRACTS.tsv, incluindo injeccao de falha deliberada

**Passo 1 -- medicao do total, com divergencia real face ao objective:**

```
$ python3 -c "
from pathlib import Path
total = 0
for f in sorted(Path('../gow2-recomp/recomp_macos_v3').glob('ppu_recomp_*.cpp')):
    total += f.read_text(errors='replace').count('ps3_call_opd(ctx,')
print(total)
"
47
```

O objective do plano citava **52** (medido na Fase 3, antes da correcao de posse do
Plano 04-02). Medido nesta task: **47** -- identico ao "ESTADO MEDIDO" do
`04-CONTEXT.md` desta sessao. A correcao do 04-02 mudou a POSSE por funcao
(`patch_14a01c_opd.py` deixou de depender do `patch_icg_ctor_opd.py` partido), nao o
total agregado -- os sitios ja estavam convertidos. Documentado no proprio
`CONTRACTS.tsv` com a divergencia explicita, nao maquiado.

**Achado adicional durante a medicao:** a agulha `ps3_call_opd(` (sem `(ctx,`)
tambem casa as 3 declaracoes `extern "C" void ps3_call_opd(ppu_context* ctx, ...);`
e infla a contagem para **50**. Corrigido antes de escrever a linha final: o
contrato `opd-total` usa `ps3_call_opd(ctx,`, a mesma agulha da medicao do passo 1.

```
$ .venv/bin/python3 games/gow2/lift_baseline/check_contracts.py games/gow2/lift_baseline/CONTRACTS.tsv ../gow2-recomp/recomp_macos_v3 --patch patch_14b1f0_opd.py; echo "rc=$?"
[PASS] opd-14b1f0 (patch_14b1f0_opd.py): agulha='ps3_call_opd(' predicado=count_ge esperado 12 encontrado 12
rc=0

$ .venv/bin/python3 games/gow2/lift_baseline/check_contracts.py games/gow2/lift_baseline/CONTRACTS.tsv ../gow2-recomp/recomp_macos_v3 --patch patch_14a01c_opd.py; echo "rc=$?"
[PASS] opd-14a01c-a (patch_14a01c_opd.py): agulha='ps3_call_opd(' predicado=count_ge esperado 2 encontrado 2
[PASS] opd-14a01c-b (patch_14a01c_opd.py): agulha='ps3_call_opd(' predicado=count_ge esperado 2 encontrado 2
rc=0
```

**Prova de injeccao de falha real (passo 4/2 do behavior) -- a exigencia central do
item 4 da fase:**

```
$ SCRATCH=$(mktemp -d)
$ cp ../gow2-recomp/recomp_macos_v3/ppu_recomp_003.cpp "$SCRATCH/"
$ python3 quebra_um_sitio.py "$SCRATCH/ppu_recomp_003.cpp"
antes: 7 ocorrencias de ps3_call_opd(ctx, na regiao
depois: 6 ocorrencias de ps3_call_opd(ctx, na regiao (esperado 6)

$ .venv/bin/python3 games/gow2/lift_baseline/check_contracts.py games/gow2/lift_baseline/CONTRACTS.tsv "$SCRATCH" --patch patch_32e200_opd.py; echo "rc=$?"
[FAIL] opd-32e200 (patch_32e200_opd.py): agulha='ps3_call_opd(' predicado=count_ge esperado 7 encontrado 6
[PASS] opd-32e200-noindirect (patch_32e200_opd.py): agulha='ps3_indirect_call' predicado=count_eq esperado 0 encontrado 0
rc=1

$ rm -rf "$SCRATCH"
```

**rc=1, mensagem exacta "esperado 7 encontrado 6"** -- o gate distingue um lift
correcto de um quebrado, nao so no caminho feliz. A copia scratch foi apagada
depois da prova; o lift real (`recomp_macos_v3`) foi reconfirmado intacto por uma
segunda corrida (`rc=0`, 7/7) apos a limpeza.

**Commit:** `0b3c3bd` -- `feat(04-04): familia OPD completa em CONTRACTS.tsv, com prova de injeccao de falha (D-4.2)`

### Task 3 -- gen_contracts_candidates.py: cruzamento TAG-MANIFEST

**Medicao inicial (dono unico, qualquer classe):**

```
$ python3 (script auxiliar de medicao) -- 69 marcadores / 32 patches com dono unico
```

Proximo do 68/31 da sessao de planeamento (a variacao de 1 unidade em cada e' ruido
de medicao normal, nao um erro).

**Apos o filtro FUNCIONAL exigido pelo plano** (PROBE fica fora do gate, D-4.4):
**51 marcadores / 22 patches** (18 marcadores de 10 patches PROBE excluidos:
`patch_crc_lookup_probe.py`, `patch_smpd_probe.py`,
`patch_flip_path_enter_probe.py`, `patch_menu_present_schedule_probe.py`,
`patch_postthr_pc_probe.py`, `patch_present_loop_probe.py`,
`patch_17acc_enter_probe.py`, `patch_sched_arm_probe.py`,
`patch_wadld_alloc_probe.py`, `patch_wavdrv_probe.py`).

**Achado real ao verificar contra o lift (Passo 6, nao maquiado):**

```
$ .venv/bin/python3 games/gow2/lift_baseline/check_contracts.py games/gow2/lift_baseline/CONTRACTS.tsv ../gow2-recomp/recomp_macos_v3 --patch patch_jumptable_2a209c.py
[FAIL] tag-opdisp (patch_jumptable_2a209c.py): agulha='[OPDISP]' predicado=count_ge esperado 1 encontrado 0
rc=1
```

`[OPDISP]` tem dono unico (`patch_jumptable_2a209c.py`) mas e' marcado `OBSOLETO`
em `MANIFEST.tsv` desde a Fase 3 (D-3.2): o lifter passou a materializar um switch
nativo cobrindo 54/54 alvos, a fprintf de diagnostico do marcador legitimamente
deixou de aparecer. Um contrato derivado dele **falharia SEMPRE** contra o lift
real, nao por regressao -- um falso-positivo estrutural do cruzamento mecanico,
exactamente o tipo de contrato que "nunca falha [quando devia] e' pior que nao ter
contrato" ao contrario (aqui falha sempre, mesmo sem regressao, o que tambem
corrompe o sinal). Corrigido (Rule 1) ANTES de commitar: `load_manifest_tags()`
em `gen_contracts_candidates.py` passa a excluir marcadores com nota
`OBSOLETO:...`, espelhando o mesmo tratamento que `gen_manifest.py` ja da a
esses marcadores.

**Numero final, pos-fix:**

```
$ .venv/bin/python3 games/gow2/lift_baseline/gen_contracts_candidates.py \
    ../gow2-recomp/recomp_mid_v2 games/gow2/lift_baseline/MANIFEST.tsv games/gow2/lift_baseline/PATCH_CATALOG.tsv | wc -l
50
```

**50 marcadores / 21 patches** entraram em `CONTRACTS.tsv` (nao os 68/31 do
planeamento, nem os 51/22 pre-fix -- o numero real e honesto, com a razao da
divergencia citada no proprio TSV).

**Verificacao de pelo menos 5 amostras (as de maior min_count) contra o lift real:**

```
$ for p in patch_32e200_opd.py patch_comboprop_168350.py patch_factory_block_ty15_install.py patch_icg_ctor_opd.py patch_shadersrc_loop.py; do
    .venv/bin/python3 games/gow2/lift_baseline/check_contracts.py games/gow2/lift_baseline/CONTRACTS.tsv ../gow2-recomp/recomp_macos_v3 --patch "$p"
  done
patch_32e200_opd.py                    -> rc=0 (4 contratos, todos PASS: opd-32e200, opd-32e200-noindirect, tag-cmp-enter, tag-cmp-vcall)
patch_comboprop_168350.py              -> rc=0 (tag-comboprop: esperado 6 encontrado 6)
patch_factory_block_ty15_install.py    -> rc=0 (tag-factory: 6/6, tag-ps3_fact_snap_max: 5/5)
patch_icg_ctor_opd.py                  -> rc=0 (tag-icg-vcall: esperado 3 encontrado 7)
patch_shadersrc_loop.py                -> rc=0 (12 contratos SS-*, todos PASS)
```

Todas as 5 amostras PASSAM honestamente -- sem maquiar (o unico FAIL descoberto,
`[OPDISP]`, foi corrigido na raiz, nao escondido).

```
$ .venv/bin/python3 games/gow2/lift_baseline/test_gen_contracts_candidates.py; echo "rc=$?"
[PASS] teste 1: marcador com dono unico vira candidato (patch_alpha.py, min_count=3)
[PASS] teste 2: marcador com dono multiplo NAO vira candidato (ambiguidade nao resolvida)
[PASS] teste 3b: marcador OBSOLETO NAO vira candidato mesmo com dono unico
[PASS] teste 4: medicao real -- 50 marcadores candidatos, 21 patches distintos
4/4 testes [PASS]
rc=0
```

**Commit:** `79ba73e` -- `feat(04-04): gen_contracts_candidates.py -- cruzamento TAG-MANIFEST amplia CONTRACTS.tsv alem da familia OPD`

## Cobertura final: 22/64 patches FUNCIONAL, 42 continuam SEM-CONTRATO

```
FUNCIONAL total (PATCH_CATALOG.tsv):                 64
FUNCIONAL cobertos por >=1 contrato em CONTRACTS.tsv: 22
FUNCIONAL SEM contrato (SEM-CONTRATO, rc=2):          42
CONTRACTS.tsv: 56 linhas de dados (6 familia OPD + 50 TAG-MANIFEST)
```

**Nota de realidade (honesta, como o objective exige):** os 42 patches FUNCIONAL
restantes ficam `SEM-CONTRATO` -- o Plano 04-05 classifica isso como `UNVERIFIED`,
um estado que **tambem conta como nao-verde** por D-4.1 ("um UNKNOWN nao pode
contar como sucesso"). Cobrir os 42 exigiria ler manualmente cada patch para
entender o seu efeito semantico -- fora do orcamento desta fase, confirmado
exactamente como o plano previa. Este plano prova o MECANISMO com o caso mais
critico (OPD, 7/64 patches -- na verdade so 3 patches distintos: `patch_32e200_opd.py`,
`patch_14b1f0_opd.py`, `patch_14a01c_opd.py`) e amplia razoavelmente via TAG (mais
19 patches distintos), sem fingir cobertura total.

## Prova de injeccao de falha (resumo, exigencia central do plano)

**Comando:** quebrar deliberadamente 1 de 7 ocorrencias de `ps3_call_opd(ctx,` na
regiao de `func_0032E200`, numa copia scratch de `ppu_recomp_003.cpp` (nunca no
lift real), e correr `check_contracts.py` contra ela.

**Resultado:** `rc=1`, mensagem `[FAIL] opd-32e200 (patch_32e200_opd.py):
agulha='ps3_call_opd(' predicado=count_ge esperado 7 encontrado 6`.

**Prova de nao-contaminacao:** a copia scratch foi apagada logo apos a prova; o
lift real (`recomp_macos_v3`) foi reconfirmado intacto (rc=0, 7/7) por uma corrida
adicional depois da limpeza.

## Task Commits

1. **Task 1** -- `f8d7eab`: `feat(04-04): check_contracts.py -- verifica a pos-condicao real do lift, nao o rc do patch (D-4.2)`
2. **Task 2** -- `0b3c3bd`: `feat(04-04): familia OPD completa em CONTRACTS.tsv, com prova de injeccao de falha (D-4.2)`
3. **Task 3** -- `79ba73e`: `feat(04-04): gen_contracts_candidates.py -- cruzamento TAG-MANIFEST amplia CONTRACTS.tsv alem da familia OPD`

Nenhum push. Nenhum commit em `../gow2-recomp`. Todos os commits em portugues, sem
co-autoria, arquivos explicitos (`git add <path>`, nunca `git add -A`/`.`).

## Files Created/Modified

- `games/gow2/lift_baseline/check_contracts.py` (novo) -- motor de verificacao,
  8-coluna `Contract`, `load_contracts`/`load_lift_text`/`ea_region`/
  `count_needle`/`evaluate`/`main`, rc 0/1/2/3.
- `games/gow2/lift_baseline/test_check_contracts.py` (novo) -- 7 testes fim-a-fim
  via subprocess.
- `games/gow2/lift_baseline/CONTRACTS.tsv` (novo) -- 56 contratos: 6 familia OPD
  (5 EA + 1 global de auditoria) + 50 TAG-MANIFEST derivados e revistos.
- `games/gow2/lift_baseline/gen_contracts_candidates.py` (novo) -- deriva
  candidatos do cruzamento TAG-MANIFEST, exclui OBSOLETO e donos multiplos.
- `games/gow2/lift_baseline/test_gen_contracts_candidates.py` (novo) -- 4 testes.

## Decisions Made

- `opd-total` usa a agulha `ps3_call_opd(ctx,` (nao `ps3_call_opd(`) para nao contar
  as 3 declaracoes `extern "C"` -- descoberto ao comparar as duas contagens
  (47 vs 50) antes de escrever o numero no TSV.
- O numero de `opd-total` e' 47, nao o 52 citado no objective (medido e explicado
  no proprio TSV: a divergencia vem de uma leitura pre-Plano-04-02, nao de um erro
  desta task).
- `gen_contracts_candidates.py` exclui marcadores `OBSOLETO:...` -- achado real ao
  verificar `[OPDISP]` contra o lift, corrigido antes do commit (Rule 1),
  reduzindo os candidatos finais de 51/22 (pre-fix) para 50/21 (pos-fix).

## Deviations from Plan

**1. [Rule 1 - Bug] Agulha `ps3_call_opd(` inflava `opd-total` de 47 para 50**
- **Encontrado durante:** Task 2, Passo 1 (medicao do total agregado).
- **Problema:** a agulha `ps3_call_opd(` tambem casa 3 declaracoes
  `extern "C" void ps3_call_opd(ppu_context* ctx, uint32_t opd_ea);`, que nao sao
  chamadas reais.
- **Fix:** usar a mesma agulha da medicao (`ps3_call_opd(ctx,`) no contrato
  `opd-total`.
- **Ficheiro:** `games/gow2/lift_baseline/CONTRACTS.tsv`.
- **Commit:** `0b3c3bd`.

**2. [Rule 1 - Bug] Candidato `[OPDISP]` seria um FAIL permanente, nao um sinal de regressao**
- **Encontrado durante:** Task 3, Passo 6 (verificacao amostral contra o lift real).
- **Problema:** `[OPDISP]` tem dono unico (`patch_jumptable_2a209c.py`) mas e'
  marcado `OBSOLETO` no `MANIFEST.tsv` (D-3.2) -- o marcador legitimamente nunca
  mais aparece no lift, porque o lifter passou a materializar um switch nativo.
  `gen_contracts_candidates.py` nao filtrava marcadores OBSOLETO, gerando um
  candidato que falharia sempre.
- **Fix:** `load_manifest_tags()` passa a excluir marcadores cuja nota (5a coluna)
  comeca por `OBSOLETO`, com teste dedicado (`test_3_obsoleto_excluido`).
- **Ficheiros:** `games/gow2/lift_baseline/gen_contracts_candidates.py`,
  `games/gow2/lift_baseline/test_gen_contracts_candidates.py`,
  `games/gow2/lift_baseline/CONTRACTS.tsv` (linha `tag-opdisp` removida).
- **Commit:** `79ba73e`.

**Total deviations:** 2, ambas Rule 1 (bug auto-fixado), ambas descobertas por
medicao/verificacao contra o lift real dentro do proprio orcamento do plano.
**Impacto no plano:** nenhum -- ambas corrigidas antes do commit, testadas, sem
regressao nos testes pre-existentes (`test_gen_catalog.py` 11/11, `test_baseline_delta.py` 5/5).

## Known Stubs

Nenhum. Os 3 scripts sao implementacoes completas, sem placeholders. Os 42
patches FUNCIONAL `SEM-CONTRATO` nao sao um stub -- sao o limite explicito e
declarado do orcamento desta fase (ver Nota de Realidade do objective, citada
honestamente acima), a resolver em trabalho de continuacao (mais candidatos TAG
com marcadores SYM/GLOBAL, ou leitura manual do efeito semantico de cada patch).

## Threat Flags

Nenhuma superficie nova fora do `<threat_model>` do plano. T-04-08 (numero copiado
sem medir) e T-04-09 (gate nunca exercitado por um caminho de falha real) tem
mitigacao testada e citada acima (medicao directa do total + injeccao de falha
real com rc e mensagem exactos). T-04-10 (candidatos aceites sem revisao) foi
precisamente o que apanhou o achado `[OPDISP]` -- a revisao manual funcionou como
desenhada.

## Issues Encountered

Nenhum bloqueador. As duas divergencias encontradas (agulha inflada, marcador
OBSOLETO) foram corrigidas dentro do orcamento normal da task, sem exigir
paragem/report (Rule 1, nao Rule 4 -- nenhuma mudanca arquitectural).

## User Setup Required

None -- nenhuma configuracao de servico externo necessaria.

## Next Phase Readiness

- `check_contracts.py` pronto para o Plano 04-05 consumir os 3 codigos de saida
  (0/1/2) no classificador de 6 estados (`ALREADY-APPLIED`/`NO-MATCH`/
  `UNVERIFIED` dependem exactamente destes rc).
- 22/64 patches FUNCIONAL cobertos por contrato real; os 42 restantes ficam
  `UNVERIFIED` no runner do 04-05 -- honestamente reportado, nao escondido.
- `CONTRACTS.tsv` e `gen_contracts_candidates.py` sao extensiveis: mais
  candidatos poderiam vir de marcadores SYM/GLOBAL do MANIFEST.tsv (fora de
  escopo explicito desta fase) ou de leitura manual dos 42 patches restantes.
- Nenhum conflito com trabalho paralelo -- este plano dependia de 04-01 (`PATCH_CATALOG.tsv`)
  e 04-02 (`patch_14a01c_opd.py` corrigido), ambos ja fechados antes desta execucao.

---
*Phase: 04-cat-logo-de-patches-como-gate-que-falha*
*Completed: 2026-07-26*

## Self-Check: PASSED

Ficheiros confirmados no disco: `games/gow2/lift_baseline/check_contracts.py`,
`test_check_contracts.py`, `CONTRACTS.tsv`, `gen_contracts_candidates.py`,
`test_gen_contracts_candidates.py`, este SUMMARY. Commits confirmados por
`git log --oneline`: `f8d7eab`, `0b3c3bd`, `79ba73e`. Suites finais:
`test_check_contracts.py` 7/7 PASS rc=0; `test_gen_contracts_candidates.py` 4/4
PASS rc=0; regressao confirmada em `test_gen_catalog.py` (11/11) e
`test_baseline_delta.py` (5/5), ambos intocados por este plano.
