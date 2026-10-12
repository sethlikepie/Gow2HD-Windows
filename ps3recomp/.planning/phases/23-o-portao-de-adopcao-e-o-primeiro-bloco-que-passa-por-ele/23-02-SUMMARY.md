---
phase: 23-o-portao-de-adopcao-e-o-primeiro-bloco-que-passa-por-ele
plan: 02
subsystem: runtime/syscalls (lv2 SPU image import)
tags: [lv2, spu, upstream-adoption, offline-unit, mutation-testing]
requires:
  - "docs/UPSTREAM_DELTA.tsv (Fase 22): a linha LV2S da o sha upstream dd936c4 e a ancora nossa"
  - "tools/upstream_delta.py validate (Fase 22): o gate de deriva de ancoras"
  - "CMakeLists.txt PS3RECOMP_BUILD_TESTS + scripts/test_runtime.sh (F0-5/F0-6)"
provides:
  - "sys_spu_image_import escreve o EA de origem do segmento COPY como u32 em +0x10"
  - "test_spu_image_import: teste offline que corre o caminho REAL do handler"
  - "test_spu_image_import_mutacao: prova por mutacao nos dois sentidos, com a producao verificada por sha256"
  - "linha LV2S do inventario re-ancorada (825-826 -> 832), validate a rc=0"
affects:
  - "Plano 23-04: e' este o primeiro bloco a atravessar o portao de adopcao"
  - "Fase 24 (interpretador SPU): passa a haver um src util em seg+0x10 para um leitor fiel"
tech-stack:
  added: []
  patterns:
    - "#include da unidade de traducao inteira (macro de indireccao LV2_REGISTER_C) para exercitar um handler `static` sem duplicar a logica"
    - "teste com caminho de falha explicito por verificacao em vez de assert() nu (imune a -DNDEBUG)"
    - "prova por mutacao em sandbox mktemp -d fora do repositorio, com sha256 antes/depois da producao"
key-files:
  created:
    - runtime/syscalls/tests/test_spu_image_import.c
    - runtime/syscalls/tests/run_spu_image_import_mutation.sh
    - .planning/phases/23-o-portao-de-adopcao-e-o-primeiro-bloco-que-passa-por-ele/deferred-items.md
  modified:
    - runtime/syscalls/lv2_register.c
    - CMakeLists.txt
    - docs/UPSTREAM_DELTA.tsv
decisions:
  - "O +0x14 fica como espelho do +0x10 (nao foi limpo): e' o que dd936c4 deixa ficar e e' inofensivo para um leitor que interprete o par como u64-lo"
  - "O ramo FILL nao foi tocado: la o +0x10 e' o campo `value` e 0 e' o valor CORRECTO; o caso 3 do teste existe para apanhar quem altere os dois ramos de uma vez"
  - "O teste NAO linka ps3recomp_runtime (daria simbolos duplicados, ja que inclui a TU); os 13 simbolos externos ficam stubados no proprio teste"
  - "A decisao `adoptar` da linha LV2S NAO foi alterada: `adoptar` significa 'entra pelo portao', e o registo de adopcao vive no Plano 23-04"
metrics:
  duration: ~50min
  completed: 2026-08-12
  tasks: 3
  commits: 4
status: complete
---

# Phase 23 Plan 02: O fix do `sys_spu_image_import` e a sua prova offline — Summary

O `sys_spu_image_import` passa a escrever o EA de origem do segmento COPY como
u32 em `+0x10` (fix upstream `dd936c4`), provado por um teste offline que corre o
caminho REAL do `lv2_register.c` e por uma prova de mutacao que mostra, nos dois
sentidos, que esse teste apanha o defeito antigo — com o inventario da Fase 22
re-ancorado na mesma leva para a CI nao ficar vermelha.

**Nivel de evidencia deste plano: `offline-unit`.** `grep -r image_import
games/gow2/` da' zero, logo **nao existe aceite `in-boot` para o `LV2S-01`** e
nenhum e' reclamado aqui. A afirmacao de nao-regressao da intro e' outra coisa e
vive no Plano 23-04.

## O defeito, e o que mudou

`sys_spu_segment.src` e' um **u32 em `+0x10`** — o proprio comentario de contrato
dentro do ficheiro ja' o dizia (`0x18, src@0x10`). Escreviamos o endereco como um
par u64 BE (`+0x10`=0 / `+0x14`=addr), portanto um leitor fiel que faca
`read32(seg+0x10)` lia **zero** e faria DMA do endereco 0.

```c
-        vm_write_be32(seg + 0x10, 0);                           /* src_pa hi  */
-        vm_write_be32(seg + 0x14, src_ea + p_off);              /* src_pa lo  */
+        vm_write_be32(seg + 0x10, src_ea + p_off);              /* src (u32 EA)   */
+        vm_write_be32(seg + 0x14, src_ea + p_off);              /* espelho u64-lo */
```

O diff em producao e' de **uma expressao** (mais comentario). Nada de `libs/`,
`runtime/ppu/` ou `runtime/host/` foi tocado — deliberado: alargar o diff
impediria o portao do Plano 23-04 de atribuir efeito a este bloco.

## O "antes" re-confirmado (nao herdado do planeamento)

O teste foi corrido **antes** do fix e ficou vermelho, reproduzindo exactamente a
medicao do planeamento:

```
rc=1 — test_spu_image_import: FAIL (5 verificacoes)
caso1 (p_offset=0)      seg+0x10: esperado=0x00100000 lido=0x00000000
caso2 (p_offset=0x1000) seg+0x10: esperado=0x00101000 lido=0x00000000
caso2                   seg+0x14: 0x00101000  (ok — o par u64 estava la')
caso3 seg0+0x10, caso4 seg0+0x10 e seg1+0x10: idem, todos a ler 0x00000000
```

Os casos que **passaram** antes do fix sao tao importantes como os que falharam:
`caso5` (controlo negativo: `src` sem magic ELF -> `-8`, imagem pre-enchida com
`0xAA` que fica a zero) prova que o handler **correu mesmo**, e `caso3
seg1+0x10 = 0` prova que o ramo FILL ja' estava certo. Depois do fix: 32
verificacoes ok, `rc=0`.

## Como o teste corre o caminho real

`sys_spu_image_import_handler` e' `static`. A unica forma de o exercitar sem
duplicar a logica — o que nao provaria nada sobre a producao — e' incluir a
unidade de traducao, e o caminho e' indirecto (macro `LV2_REGISTER_C`) para o
script de mutacao poder apontar o **mesmo** ficheiro de teste a uma copia mutada.
A superficie de link fechou nos 13 stubs previstos (`sys_*_init` x10,
`sys_fs_translate_path`, `spu_lookup_ppu_fallback`, `sys_event_queue_push_by_id`)
mais `vm_base`; compilou a primeira, sem descoberta por tentativa e erro.

Duas escolhas defensivas: os leitores BE do teste sao **proprios** (`rd32`), para
a verificacao nao ser validada pelo codigo que mede; e cada verificacao tem
caminho de falha explicito (esperado/lido em hex + contador) em vez de `assert()`
nu, que um configure `-DCMAKE_BUILD_TYPE=Release` desligaria via `-DNDEBUG`.

## A prova por mutacao

Um teste verde so' diz que a arvore actual passa; nao diz que o teste
**apanharia** o defeito. `run_spu_image_import_mutation.sh` repoe
`vm_write_be32(seg + 0x10, 0)` numa copia fora do repositorio e exige os dois
sentidos:

```
MUTACAO: sentido_mutado=1 sentido_limpo=0 producao_intacta=sim
producao sha256 ANTES : fd5f152625a5ab400fb089ed4bc57924807c67b355fadd3d7eb82d88f8ecb719
producao sha256 DEPOIS: fd5f152625a5ab400fb089ed4bc57924807c67b355fadd3d7eb82d88f8ecb719
```

O controlo positivo (`sentido_limpo=0`) nao e' cerimonia: sem ele, um vermelho
podia vir do ambiente de compilacao (um `-I` em falta, sysroot errado) em vez da
mutacao. O script tambem aborta com `rc=2` se o alvo nao for exactamente uma
ocorrencia (ambiguo, ou fix ausente da arvore) ou se a substituicao nao tiver
tido efeito.

## Re-ancoragem do inventario (nao era um comentario)

A linha `LV2S` estava ancorada precisamente nas linhas que a Task 1 editou. O
validador ficou vermelho, como o cabecalho de HONESTIDADE do proprio ficheiro
previa pelo nome:

```
[delta] FAIL ANCORA_DESLOCADA linha=29 runtime/syscalls/lv2_register.c:
prova 'vm_write_be32(seg + 0x14, src_ea + p_off)' nao esta em 825-826,
linha_actual=832                                              (validate rc=1)
```

`--fix-anchors` moveu `825-826 -> 832`; o `validate` sem flag voltou a `rc=0`. O
diff do TSV e' de **uma linha e so' no campo da ancora** — `decisao=adoptar`,
`sha_upstream`, `evidencia` e `razao` ficaram intactos. `tools/test_upstream_delta.py`
passa (27 casos, incluindo `test_tsv_real_do_repo`, que e' o que a CI corre).

## Suites completas — e a distincao que o criterio 4 obriga a escrever

| Suite | Resultado | FAIL |
|---|---|---|
| `scripts/test_python.sh` | 19 testes: **PASS 15, SKIP 2, FAIL 2** | `tools/test_ppu_lift.py`, `runtime/ppu/tests/test_ppu_lifter_emit.py` |
| `scripts/test_runtime.sh` | 28 testes: **24 passed, 4 failed** | os dois acima espelhados no CTest (`py.test_ppu_lift`, `py.ppu_lifter_emit`) + `suite.runtime_spu`, `suite.runtime_trace` |

**Nenhum FAIL novo.** Os dois do `test_python.sh` sao a divida de toolchain
declarada na Fase 22 (`.planning/phases/22-o-inventario-do-delta-e-a-contribuicao-de-volta/deferred-items.md`),
causa medida `'string.h' file not found`. **Nao sao regressao desta fase** e nao
foram corrigidos: estao explicitamente fora da fronteira.

Os outros dois (`suite.runtime_spu`, `suite.runtime_trace`) sao a **mesma** causa
raiz — os scripts dessas suites invocam um `cc` **nu**, sem `-isysroot` — mas a
Fase 22 nunca chegou a registar o `test_runtime.sh`. Foram provados
pre-existentes, nao assumidos: corridos num `git worktree` destacado em
`62a3033` (o commit imediatamente anterior ao primeiro desta leva), onde falham
identicamente (`0 passed, 19 failed` / `'stdio.h' file not found`). O registo
fica alargado em `deferred-items.md` desta fase. O teste novo **nao** herda a
divida: resolve o SDK por `xcrun`.

## Deviations from Plan

### 1. [Rule 1 — Bug no proprio gate] A regex de aceite do `ctest` nao bate nesta versao

- **Encontrado em:** Task 2, terceiro bloco `<automated>`.
- **Problema:** o plano exigia `grep -qE '2 tests.*100% tests passed|100% tests passed, 0 tests failed out of 2'`. O CTest 4.4.0 desta maquina imprime `100% tests passed out of 2` — sem o `, 0 tests failed`. O gate ficava vermelho com **os dois testes a passar**, ou seja, media a versao do CTest e nao o codigo.
- **Correccao:** assercao substituida por `grep -qE '100% tests passed.*out of 2'` mais a contagem de linhas `Passed` (== 2). Nenhuma alteracao de codigo ou de teste.
- **Saida real medida:** `1/2 Test #25: test_spu_image_import ... Passed` / `2/2 Test #26: test_spu_image_import_mutacao ... Passed` / `100% tests passed out of 2`.

### 2. [Rule 3 — Referencia partida] Caminho errado do `deferred-items.md` da Fase 22

- **Encontrado em:** Task 3, `<read_first>`.
- **Problema:** o plano aponta para `.planning/phases/22-a-que-mede-o-que-o-upstream-avancou-e-nos-nao-temos/deferred-items.md`, directorio que nao existe.
- **Correccao:** o ficheiro real e' `.planning/phases/22-o-inventario-do-delta-e-a-contribuicao-de-volta/deferred-items.md`, lido de la'. Sem impacto no conteudo.

### 3. [Rule 2 — Robustez do proprio instrumento] Duas defesas acrescentadas ao script de mutacao

- **Porque:** um script de prova que falhe em silencio e' pior do que nenhum. Acrescentado (a) `PYTHON` honrado e verificado por `command -v`, com o CMake a passar `Python3_EXECUTABLE` (mesmo padrao da `suite.runtime_spu`), e (b) uma assercao pos-mutacao de que o alvo **desapareceu** da copia — uma substituicao sem efeito daria um `sentido_mutado` que so' repetia o sentido limpo, isto e', uma "prova" vazia.
- A substituicao e' **literal**, nao regex, de proposito: o alvo contem `+` e parenteses.

### 4. [Processo] Gate de feedback do tracer registado, nao encaminhado como checkpoint humano

- A Task 1 e' `type="tracer"`. Com `auto_advance=false` o protocolo pediria um `checkpoint:human-verify` apos o commit do tracer; o plano declara `autonomous: true` e nao contem nenhuma task `checkpoint:*`, e o `<verify>` do tracer e' inteiramente automatizado. Foi corrido ponta-a-ponta e passou (`ctest` 1/1, mais os dois gates de `grep`), pelo que a execucao seguiu para as tasks de expansao. Registado aqui por transparencia.

## Threat Flags

Nenhuma superficie nova. `T-23-07` (o script de mutacao contaminar a arvore)
ficou **mitigada e medida**: sandbox `mktemp -d` fora do repositorio, `trap EXIT`
a remove-la, e `sha256` da producao igual antes/depois (`fd5f1526...`).
`T-23-08` (falta de limite superior em `phoff + i*phentsz` / `p_offset` antes dos
`vm_read_be32`/`vm_write_be32`) permanece `accept` e pre-existente, registada em
`deferred-items.md` para reavaliacao na Fase 24.

## Known Stubs

Nenhum stub em producao. Os 13 stubs de `runtime/syscalls/tests/test_spu_image_import.c`
sao **superficie de link do teste**, nao codigo de producao: nenhum deles entra no
caminho do `image_import`, e existem apenas para o ficheiro linkar sem puxar a
biblioteca inteira (o que daria simbolos duplicados, ja' que o teste inclui a TU).

## Commits

| Commit | Gate | O que |
|---|---|---|
| `24ad9d6` | RED | `test(23-02)`: teste offline que corre o caminho real; falha de proposito, ainda nao registado no CTest para nao pintar a CI de vermelho a meio da leva |
| `687718a` | GREEN | `fix(23-02)`: `+0x10` passa a levar `src_ea + p_off`; registo no CTest |
| `16d7433` | — | `test(23-02)`: prova por mutacao nos dois sentidos + registo no CTest |
| `4b8f477` | — | `docs(23-02)`: re-ancoragem do inventario + `deferred-items.md` |

## Verificacao final (todos os 6 criterios do plano)

| # | Criterio | Resultado |
|---|---|---|
| 1 | `ctest -R test_spu_image_import` corre 2 testes, ambos passam | `100% tests passed out of 2` |
| 2 | Exactamente 1 `vm_write_be32(seg + 0x10, 0)` (o ramo FILL); COPY escreve `src_ea + p_off` | `fill_zero=1`, `copy_fix=1` |
| 3 | Mutacao: mutado != 0, limpo == 0, producao intacta por sha256 | `sentido_mutado=1 sentido_limpo=0 producao_intacta=sim` |
| 4 | `upstream_delta.py validate` a `rc=0` e `test_upstream_delta.py` passa | `validate_rc=0`, `test_upstream_delta_rc=0` |
| 5 | `test_python.sh` sem FAIL fora dos dois de divida | `comm -23` vazio — `SEM_FAIL_NOVO_OK` |
| 6 | Diff nao toca `libs/`, `runtime/ppu/`, `runtime/host/`, `.planning/PROJECT.md`/`MILESTONES.md`/`config.json`/`GSD-MODEL-ROUTING.md` | 6 ficheiros, nenhum na lista proibida — `V6_FRONTEIRA_OK` |

## Self-Check: PASSED

Ficheiros criados confirmados no disco (`test_spu_image_import.c`,
`run_spu_image_import_mutation.sh`, `deferred-items.md`); os 4 commits
(`24ad9d6`, `687718a`, `16d7433`, `4b8f477`) confirmados por `git log`.
