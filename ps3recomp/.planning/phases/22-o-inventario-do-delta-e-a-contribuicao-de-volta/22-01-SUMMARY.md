---
phase: 22-o-inventario-do-delta-e-a-contribuicao-de-volta
plan: 01
subsystem: tooling/upstream-integration
tags: [upstream, inventario, tsv, gate, ci, offline-unit]
requires: []
provides:
  - docs/UPSTREAM_DELTA.tsv (input das Fases 23-26: que sha fazer cherry-pick)
  - tools/upstream_delta.py (measure/validate/rows)
  - gate de deriva de ancoras registado na CI
affects:
  - scripts/test_python.sh
  - CMakeLists.txt
tech-stack:
  added: []
  patterns: [TSV versionado com cabecalho medido, prova por mutacao, gate de ancora com substring]
key-files:
  created:
    - tools/upstream_delta.py
    - tools/test_upstream_delta.py
    - docs/UPSTREAM_DELTA.tsv
  modified:
    - scripts/test_python.sh
    - CMakeLists.txt
decisions:
  - "A base de comparacao e' o ramo vivo escolhido por committerdate, nunca upstream/master (congelado, esconde 227 commits medidos)"
  - "Uma ancora e' `ficheiro:linha` MAIS uma substring ASCII de prova; so' o numero mentiria a primeira edicao acima dele"
  - "`adoptar` significa 'entra pelo portao da Fase 23+', nao 'ja foi adoptado' -- este ficheiro nao simula estado de adopcao"
  - "A linha SPRS NAO decide a substituicao do SPURS HLE; o veredicto PROMOTE/HOLD e' o SPRS-01 na Fase 26"
metrics:
  duration: ~1h
  completed: 2026-08-12
  tasks: 3
  commits: 4
  data_rows: 10
status: complete
---

# Phase 22 Plan 01: O inventário do delta upstream→nós — Summary

Inventário versionado do delta upstream→nós (**UPST-01**): 10 linhas onde cada
uma tem o sha do upstream de um lado e a âncora `ficheiro:linha` nossa do outro,
com um cabeçalho **medido nesta corrida** contra o ramo vivo e um gate de deriva
registado na CI que sabe reprovar — provado por cinco mutações, não por uma
corrida verde.

## O que foi construído

| Artefacto | O que faz |
|---|---|
| `tools/upstream_delta.py` | `measure` (lê git real e reescreve só as linhas `#`), `validate` (11 checks nomeados), `rows` (o que as Fases 23-26 consomem) |
| `docs/UPSTREAM_DELTA.tsv` | cabeçalho medido + 10 linhas de dados, 10 colunas |
| `tools/test_upstream_delta.py` | 27 checks: 5 mutações + controlo negativo + deriva total + prova não-ASCII + `--fix-anchors` + o TSV real |

## A medição (critério 3 do ROADMAP) — corrida NESTA fase

**Re-medida do zero, não copiada.** O ponto de partida de 2026-08-12 era 461; a
re-medição confirmou 461, e é o valor medido que ficou escrito com a sua data.

| Chave | Valor medido | Comando |
|---|---|---|
| `ramo_upstream` | `upstream/fix/fold-merge-dropped-fixes` | `git for-each-ref --sort=-committerdate` |
| `ramo_head_sha` / `data` | `ee97d01` / 2026-08-09 | idem |
| `delta_commits` | **461** | `git rev-list --count HEAD..<ramo>` |
| `nosso_head` | `b309bbb` (no momento do `measure` da Task 1) | `git rev-parse --short HEAD` |
| `nosso_delta_commits` | **1050** | `git rev-list --count <ramo>..HEAD` |
| `master_congelado` | `25e5efe` (2026-07-16), esconde **227** commits | `git rev-list --count HEAD..upstream/master` |

O `<verify>` da Task 1 re-correu `git rev-list --count HEAD..$RAMO` e confirmou
que o número escrito bate com o git (T-22-01 mitigado). O plano dizia "~230
commits escondidos"; a medição deu **227** e foi 227 que ficou no ficheiro.

Nível de evidência: **`offline-unit`** (comparação git, sem boot).

## O inventário (10 linhas)

`adoptar=3 rejeitar=1 já-temos=5 contribuir-de-volta=1` — os quatro valores do
vocabulário representados.

As **quatro lacunas do marco** (critério 2), cada uma com âncora aberta e lida:

| área | âncora nossa | sha upstream | decisão | fase |
|---|---|---|---|---|
| SPUI | `runtime/spu/spu_workload.h:6` | `aa3a85a,a88076e,8440ee0` | adoptar | 24 |
| GCMF | `libs/video/cellGcmSys.c:615-616` | `684dd71,0fcba98,f8ee763` | adoptar | 25 |
| LV2S | `runtime/syscalls/lv2_register.c:825-826` | `dd936c4,a984880` | adoptar | 23 |
| SPRS | `libs/spurs/spurs_kernel.c:117` | `b774c17,76f80b4` | já-temos | 26 |

As decisões gravadas para **nunca serem reinvestigadas**: as três correcções que
o upstream restaurou em `6bc76ae` (`ctx->lr` no `bl`, TOCFIX `ld r2,N(r1)`, regra
rA=0 do D-form) e o oráculo RPCS3. `tools/compare/` entra como `rejeitar`.

Todos os 14 shas resolvem com `git cat-file -e` (T-22-02 mitigado).

## Correcções à tabela de partida do plano (medidas, não assumidas)

O plano avisava "nada de copiar desta tabela sem confirmar". Duas divergências:

1. **Âncora do rA=0 D-form:** o plano apontava `tools/ppu_lifter.py:1943`; a linha
   real é **`:1944`** (`:1943` é o comentário acima). Ficou `:1944`. Esta é
   exactamente a classe de deriva silenciosa que o gate existe para apanhar.
2. **Proveniência do LLE de firmware:** o plano atribuía `docs/FIRMWARE_LLE.md` +
   `fw_spu/spu_libsre/` a `b774c17`. Medido: `b774c17` traz
   `docs/FIRMWARE_LLE.md` + `tools/lift_prx.py`; os lifts de `fw_spu/spu_libsre/`
   entram em `76f80b4`. A linha SPRS regista **os dois** shas.

Verificado também que `76f80b4` de facto introduz `tools/compare/` (o assunto do
commit fala de SPURS, mas `git log -- tools/compare` aponta-o), e que
`tools/rpcs3_probe/selftest.py` é **nosso** (não existe do lado deles).

## O gate sabe reprovar (T-22-03)

Não basta o validador dizer "sim". Cada mutação exige rc=1 **e** o nome do check:

| Mutação | Check nomeado |
|---|---|
| linha sem âncora | `CAMPOS` |
| âncora deslocada | `ANCORA_DESLOCADA` + `linha_actual=<n>` |
| decisão `talvez` | `VOCABULARIO` |
| `ramo_upstream=upstream/master` | `RAMO_NAO_MASTER` |
| 3 linhas de dados | `MINIMO_LINHAS` |
| prova desapareceu do ficheiro | `ANCORA_PROVA` |
| prova não-ASCII | `PROVA_ASCII` |

**Controlo negativo:** o TSV sintético válido sai rc=0 — sem ele, um validador
que reprovasse tudo passaria todas as mutações e o teste não estaria a medir nada.

**Deriva provada sobre o TSV real** (numa cópia): mover a âncora LV2S para
`800-801` deu
`FAIL ANCORA_DESLOCADA linha=29 ... linha_actual=826 (corrigir com --fix-anchors)`.

**T-22-04:** o sha256 de `docs/UPSTREAM_DELTA.tsv` é medido no início e
reconfirmado no fim da suite (`d7005afa...` estável); `validate` sem
`--fix-anchors` não escreve em ficheiro nenhum; as mutações correm todas em
`tempfile.TemporaryDirectory()` com `--raiz` a apontar para lá.

## Deviations from Plan

### [Rule 3 - Bloqueante] Contradição interna do plano: `MINIMO_LINHAS` vs o tracer

- **Encontrado em:** Task 1
- **Problema:** a Task 1 manda implementar `MINIMO_LINHAS` (>=4 linhas) *e* o seu
  `<verify>` corre `validate` sem flags exigindo rc=0 — mas o tracer tem **uma**
  linha por desenho. As duas exigências não podem ser ambas verdadeiras.
- **Fix:** `validate` ganhou `--min-linhas N` com **default 4** (o contrato de
  produção fica intacto). O `<verify>` da Task 1 correu com `--min-linhas 1`.
  A partir da Task 2 o `validate` sem flags sai rc=0 com as 10 linhas.
- **Ficheiro:** `tools/upstream_delta.py`
- **Commit:** `65d6675`

### [Rule 2 - Funcionalidade em falta] As notas de honestidade tinham de sair do gerador

- **Encontrado em:** Task 2
- **Problema:** a Task 2 manda acrescentar notas `#` ao cabeçalho, mas `measure`
  reescreve **todas** as linhas `#` — a nota morreria no primeiro regen, que é
  precisamente quando ela mais faz falta.
- **Fix:** as notas passaram a ser emitidas pelo `measure`. Confirmado que
  sobrevivem: regenerei o cabeçalho e as 10 linhas de dados ficaram intactas.
- **Commit:** `61a8e9c`

### [Rule 3] `--raiz` no `validate`

O plano exige mutações hermáticas em tempdir com ficheiros-âncora sintéticos, mas
o validador resolvia as âncoras sempre contra a raiz do repo — os ficheiros do
tempdir eram inalcançáveis. Adicionado `--raiz` (default: raiz do repo, resolvida
de `__file__`, nunca do cwd). Commit `934cca7`.

## Verificação — o que ficou vermelho e porquê

O item de verificação "`bash scripts/test_python.sh` termina sem `FAIL`"
**não foi atingido**, e não vou pintá-lo de verde:

```
test_python.sh -- resumo (18 testes)   PASS: 14   SKIP: 2   FAIL: 2
  - tools/test_ppu_lift.py (exit 1)
  - runtime/ppu/tests/test_ppu_lifter_emit.py (exit 1)
```

**São pré-existentes, medido** [`offline-unit`]: corri ambos num `git worktree`
destacado em `65d6675^` — o commit imediatamente **anterior** ao primeiro deste
plano — e falham com rc=1 e a mesma causa
(`include/ps3emu/endian.h:12:10: fatal error: 'string.h' file not found`, um
problema de toolchain do host). Nenhuma alteração deste plano entra no caminho de
compilação desses harnesses. Registado em `deferred-items.md`; **não corrigido**,
por fronteira de âmbito.

`tools/test_upstream_delta.py` aparece na lista e **passa** (27 checks, rc=0).

## Known Stubs

Nenhum. O único item declaradamente não-exercitado é por desenho: a linha
`contribuir-de-volta` traz `evidencia=nao-exercitado` porque a caracterização
exacta do defeito do upstream é **re-medida no Plano 22-02** (UPST-03), que
actualiza essa linha.

## Self-Check: PASSED

Os 5 ficheiros existem; os 4 commits (`65d6675`, `61a8e9c`, `ad7b4c0`, `934cca7`)
resolvem em `git log`.

## Aceites — nível de evidência declarado

| Afirmação | Nível |
|---|---|
| delta=461 contra o ramo vivo, master esconde 227 | `offline-unit` (git) |
| 14 shas do upstream resolvem | `offline-unit` |
| 10 âncoras abertas e lidas no repo | `medido` por linha |
| o gate reprova as 5 mutações nomeando o check | `offline-unit` |
| o TSV de produção não é escrito pela suite (sha256) | `offline-unit` |
| RPCS3 não arbitra a parede da intro (contador 2 vs nosso 3) | `medido` (E81, citado) |

**Nenhum aceite `in-boot` é reclamado por este plano.** Não houve build de
`boot_gow2` nem smoke de intro — nem eram precisos.
