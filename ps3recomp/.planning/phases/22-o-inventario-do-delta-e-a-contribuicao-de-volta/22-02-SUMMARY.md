---
phase: 22-o-inventario-do-delta-e-a-contribuicao-de-volta
plan: 02
subsystem: tooling/upstream-integration
tags: [upstream, contribuicao, jump-tables, ppu-lifter, diferencial, mutacao, offline-unit]
requires:
  - docs/UPSTREAM_DELTA.tsv (linha `contribuir-de-volta` criada no Plano 22-01)
  - tools/upstream_delta.py (validate/rows)
provides:
  - tools/upstream_jt_snapshot.py (recorte verbatim do upstream + proveniencia verificavel)
  - docs/upstream-contrib/ (patch que aplica e corrige o lado deles)
  - linha `contribuir-de-volta` fechada com caracterizacao re-medida
affects:
  - scripts/test_python.sh
  - CMakeLists.txt
  - docs/UPSTREAM_DELTA.tsv
tech-stack:
  added: []
  patterns:
    - recorte verbatim de terceiros com sha256 re-verificado do git
    - diferencial nosso-vs-deles com vector de CONTROLO obrigatorio
    - prova por mutacao com assercao de contagem de casamentos
    - patch de contribuicao gerado mecanicamente em repo temporario
key-files:
  created:
    - tools/upstream_jt_snapshot.py
    - tools/test_upstream_jt_contrib.py
    - docs/upstream-contrib/README.md
    - docs/upstream-contrib/0001-ppu_lifter-jumptable-try-both-lwzx-operands.patch
  modified:
    - scripts/test_python.sh
    - CMakeLists.txt
    - docs/UPSTREAM_DELTA.tsv
decisions:
  - "A frase historica do PR #82 (`first TOC-loaded operand wins`) e LARGA DEMAIS para o ramo vivo: eles ganharam reordenacao sintactica por `add`. O defeito re-medido e mais estreito e foi escrito assim, mesmo sendo menos vistoso"
  - "O vector antigo (offsets com `add`) deixou de discriminar contra o ramo vivo -- por isso entra como CONTROLO, nao como acusacao"
  - "O lado deles no diferencial e recorte verbatim com sha256 re-verificado do git, nunca uma reconstrucao nossa"
  - "O patch estende a arbitracao por contagem de alvos que ELES ja usam para multi-TOC ao candidato a registo-base -- nao substitui a funcao deles pela nossa"
  - "A preferencia sintactica por `add` sobrevive como desempate barato, nao como decisao final"
  - "Aceitacao pelo upstream NAO e criterio: nao ha PR aberto nem push para sp00nznet"
metrics:
  duration: ~1h
  completed: 2026-08-12
  tasks: 3
  commits: 3
  vectores: 2
status: complete
---

# Phase 22 Plan 02: A contribuição de volta — jump-table base arbitration — Summary

**UPST-03 fechado** com as três pernas exigidas: o VECTOR A falha contra o
código **verbatim** do ramo vivo do upstream e passa contra o nosso; existe um
VECTOR B de **controlo** onde os dois lados concordam; e reverter a nossa
arbitração numa cópia em memória põe o VECTOR A vermelho — com a mutação
provada como aplicada.

**Nível de evidência: `offline-unit`.** Vectores unitários, memória por
dicionário, sem ELF, sem rede, sem boot. Nenhum aceite `in-boot` é reclamado
nem insinuado.

## A medição que decidiu o plano

A instrução era explícita: **não** assumir a frase histórica; ler a função
deles no ramo vivo e re-derivar o defeito, mesmo que ficasse mais estreito.

Foi o que aconteceu.

| | caracterização |
|---|---|
| **Frase histórica** (PR #82) | "first TOC-loaded operand wins" |
| **Medido em `ee97d01`** | **larga demais** — eles já reordenam os candidatos por um `add` que combine o valor carregado com um dos operandos do `lwzx` |

O ramo vivo (`tools/ppu_lifter.py:3011-3229`) faz, por ordem: monta `_cands =
[p[1], p[2]]`; varre a janela à procura de um `add` que combine o valor
carregado (ou a sua extensão de sinal) com um dos candidatos e põe esse à
frente; e **só depois** fixa o primeiro candidato cuja definição mais próxima é
uma carga do TOC, com `break`, **sem nunca contar alvos validados**.

O defeito residual é, portanto, mais estreito e é esse que o teste ataca:

> Numa tabela **ABSOLUTA** não há `add`, a reordenação não tem por onde pegar, a
> ordem fica a ordem sintáctica crua dos operandos do `lwzx`. Se o operando
> errado (o índice) também tiver sido carregado do TOC, ganha ele, a base lida é
> a errada, zero alvos validam, `best` fica vazio — e o dispatcher inteiro é
> descartado em silêncio.

**Não é um cenário de palha:** é a colisão que o comentário deles já descreve
("Both operands are often TOC-loaded, so 'first candidate with a TOC-load
definition' can pick an unrelated index reg whose own stale `lwz idx,d(r2)`
reads back a valid-but-wrong base"). A reordenação por `add` que eles
acrescentaram cobre esse cenário **apenas quando existe um `add`** — ou seja,
não o cobre nas tabelas absolutas.

A raiz é a mesma nas duas leituras: o registo-base é **escolhido** em vez de
**arbitrado por contagem de alvos validados**. Eles já arbitram assim — mas só
entre candidatos a TOC, nunca entre candidatos a registo-base.

## Os dois vectores — medidos

| vector | forma | nós | eles (`ee97d01`, verbatim) |
|---|---|---|---|
| **A — discriminador** | tabela ABSOLUTA, sem `add`, ambos operandos do TOC, o errado primeiro | `[0x13000, 0x13004, 0x13008]` | **`None`** (dispatcher descartado) |
| **B — controlo** | tabela de OFFSETS, com `add` | `[0x11100, 0x11200]` | `[0x11100, 0x11200]` — **acertam também** |

O VECTOR B é o vector que já existia em
`test_ppu_jumptables.py::test_primeiro_operando_nao_ganha_sozinho`. Fica no
teste **de propósito**: sem um vector onde os dois lados concordam, um
diferencial verde não distingue "defeito deles" de "harness enviesado". E deixa
escrito que **o teste antigo deixou de discriminar** contra o ramo vivo — que é
exactamente o aviso que abriu este plano.

## O lado deles é verbatim, não uma reconstrução

`tools/upstream_jt_snapshot.py` guarda a função deles copiada byte a byte:

```
repo    https://github.com/sp00nznet/ps3recomp
ramo    upstream/fix/fold-merge-dropped-fixes
sha     ee97d01c16685e3b963c4bfc88053bb20831c304
ficheiro tools/ppu_lifter.py  linhas 3011-3229
sha256  3a903fc46e1838f31171cef8b8535cd28ea894548152bbbcb692f4a7b771a5f4
licenca MIT — Copyright (c) 2026 Ned Heller / sp00nznet (T-22-11)
```

- `--verify` compara o texto entre sentinelas com o sha256 gravado **e**
  re-extrai do git para confirmar. Provado que **sabe reprovar**: editar uma
  linha do recorte numa cópia dá rc=1 com o sha256 divergente.
- Sem o remote `upstream` (o caso da CI) imprime
  `[jtsnap] nota remote upstream ausente -- snapshot nao reconfirmado` e sai
  rc=0 — o auto-teste offline continua duro. **Nunca imprime `SKIP:`**
  (convenção F0-3: classificaria o teste inteiro como saltado).
- `--refresh` reescreve o módulo inteiro para **stdout**, nunca sobre si
  próprio; confirmado idempotente.
- O carregamento é por `exec` num **namespace próprio** — o módulo deles
  chama-se `ppu_lifter` tal como o nosso, e importá-lo por cima trocaria os dois
  lados sem aviso (T-22-08).

## O patch — aplica **e** corrige

Gerado **mecanicamente** num repo temporário sobre o ficheiro deles no sha
gravado, nunca escrito à mão.

| aceite | resultado |
|---|---|
| `git apply --check` sobre `ee97d01:tools/ppu_lifter.py` | **rc=0** |
| aplicado, VECTOR A do lado deles | passa a `[0x13000, 0x13004, 0x13008]` (`PATCHED_OK`) |
| aplicado, VECTOR B | `[0x11100, 0x11200]` — não regride |
| diff semântico (ignorando a re-indentação) | **21 inserções / 8 remoções** |

**Não-regressão medida** — seis vectores contra a função deles antes e depois:

| vector | antes | depois |
|---|---|---|
| 1-nível absoluta | 3 alvos | 3 alvos |
| 2-níveis (feature nossa, o patch não a adiciona) | descartado | descartado |
| offsets | 2 alvos | 2 alvos |
| multi-TOC | 2 alvos | 2 alvos |
| offsets + colisão de 1º operando | 2 alvos | 2 alvos |
| **absoluta + colisão de 1º operando** | **descartado** | **3 alvos** |

**Zero regressões, um ganho.**

**O que o patch preserva** (listado no README para o revisor deles): base
carregada com `ld` do TOC ELFv1 (metade baixa da entrada de 64 bits), contagem
pelo **maior** imediato comparado contra o índice **cru** (incluindo o walk da
definição de shift), `scan` generoso com terminação estrutural no menor alvo à
frente da tabela, e a preferência sintáctica por `add` — que sobrevive,
despromovida de decisão final a **desempate barato** (empates ficam com o
candidato anterior, que é o emparelhado com `add` sempre que a reordenação
disparou). **Não** substitui a função deles pela nossa.

## A prova por mutação (T-22-09)

| | |
|---|---|
| **Mutação** | trunca `r_base_cands` e `base_candidates` ao primeiro elemento — o comportamento "o primeiro que decodifica ganha" de antes do fix de 2026-07-30 |
| **Onde** | cópia **em memória** do recorte da nossa função; nenhum caminho de escrita aberto sobre `tools/ppu_lifter.py` |
| **Resultado** | VECTOR A deixa de resolver → **vermelho** (`MUTACAO_OK`) |
| **Contagem** | `re.subn` com asserção `n == 1` |

**O guard sabe reprovar:** com uma âncora que não casa, a suite sai **rc=1**
com `FAIL mutacao casa exactamente uma vez casou 0x`. Sem isto, um "vermelho"
vindo de uma mutação que nunca se aplicou passaria por prova.

Sob mutação o VECTOR B também ficou `None` — esperado e irrelevante: a prova
exige que **A** fique vermelho.

**T-22-07:** `tools/ppu_lifter.py` medido no início e reconfirmado no fim da
suite (`sha256=5e694c82…`), e confirmado externamente antes/depois de cada
task. Nunca alterado — `git diff --name-only f8f7568..HEAD` **não** o inclui.

## Deviations from Plan

### [Rule 3 - Bloqueante] `--alvo` era exigido pela Task 2 mas só existia na Task 3

- **Encontrado em:** Task 1→2
- **Problema:** o `<verify>` da Task 2 corre
  `test_upstream_jt_contrib.py --alvo <ficheiro>`, mas o plano só manda
  implementar `--alvo` no passo 2 da Task 3. A Task 2 não podia verificar-se.
- **Fix:** `--alvo` implementado na Task 1, cujo `<files>` já inclui
  `tools/test_upstream_jt_contrib.py`. A Task 3 acrescentou só a mutação, o
  registo na CI e o TSV.
- **Commit:** `3746e1c`

### [Rule 1 - Bug] `_git_source(sha=UPSTREAM_SHA)` fixava a constante na assinatura

- **Encontrado em:** Task 1, ao testar o caminho da CI (sem remote)
- **Problema:** o default ligava `UPSTREAM_SHA` no momento da **definição**, não
  da chamada. O primeiro teste do caminho "sem remote" reportou
  `re-extraido de 000000000000 -- verbatim confirmado`: `_verify` imprimia o sha
  do módulo enquanto `_git_source` lia o do `def`. O caminho da CI nunca chegou
  a ser exercitado, e dois valores podiam discordar sem aviso.
- **Fix:** defaults resolvidos **dentro** da função. Re-testado: com o sha
  inexistente sai `[jtsnap] nota remote upstream ausente` e rc=0.
- **Commit:** `3746e1c`

### [Nota de ambiente] `$SHA:tools/...` em zsh

O `<verify>` do plano usa `git show "$SHA:tools/ppu_lifter.py"`. O shell desta
sessão é **zsh**, que aplica `:t` como modificador de expansão de parâmetro — o
comando resolvia para `ee97d01…ools/ppu_lifter.py` e produzia um ficheiro
vazio. Corrido com `"${SHA}:tools/ppu_lifter.py"`. Não é alteração de
comportamento, mas fica registado porque um repo temporário com ficheiro vazio
teria dado um `apply --check` falso.

## Checkpoint do tracer

A Task 1 é `type="tracer"`. O `<verify>` correu **completo** e verde
(`TRACER_OK ref=upstream/fix/fold-merge-dropped-fixes sha=ee97d01…`) antes de
qualquer task de expansão, com os dois vectores medidos ponta a ponta. Com
`parallelization.skip_checkpoints=true` e instrução explícita do orquestrador
para executar todas as tasks, a execução seguiu em vez de parar — a função do
portão (não empilhar expansão sobre fundação partida) ficou satisfeita pela
medição, não dispensada.

## Verificação — o que ficou vermelho e porquê

O item `bash scripts/test_python.sh` termina com **`FAIL: 0`** **não foi
atingido**, e não vou pintá-lo de verde:

```
test_python.sh -- resumo (19 testes)   PASS: 15   SKIP: 2   FAIL: 2
  - tools/test_ppu_lift.py (exit 1)
  - runtime/ppu/tests/test_ppu_lifter_emit.py (exit 1)
```

**São pré-existentes, medido** [`offline-unit`] — e desta vez provado contra o
baseline **deste** plano, não o do 22-01: `git worktree` destacado em
`f8f7568` (= `3746e1c^`, o commit imediatamente anterior ao primeiro deste
plano), ambos rc=1, mesma causa
(`include/ps3emu/endian.h:12:10: fatal error: 'string.h' file not found` —
toolchain do host). Nenhuma alteração deste plano entra no caminho de compilação
desses harnesses. Registado em `deferred-items.md`; **não corrigido**, por
fronteira de âmbito.

`tools/test_upstream_jt_contrib.py` aparece na lista e **passa** (PASS subiu de
14 para 15).

## Resto da verificação

| item | resultado |
|---|---|
| `test_upstream_jt_contrib.py` com `MUTACAO_OK` e `CONTROLO_OK` | rc=0 |
| `upstream_jt_snapshot.py --verify` | rc=0 |
| `git apply --check` sobre o sha gravado | rc=0 |
| `git diff --name-only` inclui `tools/ppu_lifter.py`? | **não** |
| `upstream_delta.py validate` | rc=0, `contribuir-de-volta=1`, âncora `:3844` intacta |
| Algum `git push` / PR? | **nenhum** — `upstream` continua em `ee97d01` |

## A linha do inventário

`docs/UPSTREAM_DELTA.tsv`, linha `LIFT` / `contribuir-de-volta`:
`sha_upstream` passou ao sha completo do snapshot, `evidencia` de
`nao-exercitado` para **`medido`**, e a razão passou a citar a caracterização
**re-medida** (incluindo dizer, por escrito, que a frase histórica é larga
demais) e a apontar para `docs/upstream-contrib/`. A âncora
`tools/ppu_lifter.py:3844` com a prova `r_base_cands: list[str] = []` mantém-se
— este plano não tocou no lifter, logo a linha não se deslocou.

A hipótese alternativa prevista no plano (passo 5 da Task 3: se o ramo vivo já
não tivesse o defeito, a linha mudaria para `já-temos`) **não** se concretizou:
o defeito existe, medido. Mas existe **mais estreito** do que a hipótese de
partida, e é essa versão que ficou escrita.

## Known Stubs

Nenhum.

## Threat Flags

Nenhuma superfície de segurança nova. O plano não instala pacotes (stdlib
apenas), não abre rede em tempo de teste e não escreve fora de `tools/`,
`docs/` e do inventário. As três fronteiras do `<threat_model>` foram
exercitadas: `exec` de código de terceiros com sha256 verificado (T-22-08,
T-22-11), mutação sem caminho de escrita sobre o ficheiro real (T-22-07,
T-22-09), e nenhum `git push` / `gh pr create` em parte alguma (T-22-10).

## Self-Check: PASSED

Os 4 ficheiros criados existem; os 3 modificados contêm as alterações; os 3
commits (`3746e1c`, `6bf2f09`, `0bf77fc`) resolvem em `git log`.

## Aceites — nível de evidência declarado

| Afirmação | Nível |
|---|---|
| VECTOR A: nós resolvemos 3 alvos, eles descartam o dispatcher | `offline-unit` |
| VECTOR B (controlo): os dois lados devolvem os mesmos alvos | `offline-unit` |
| O recorte é verbatim de `ee97d01` (sha256 re-extraído do git) | `offline-unit` |
| O patch aplica limpo e corrige o lado deles | `offline-unit` |
| Zero regressões em 6 vectores antes/depois do patch | `offline-unit` |
| Mutação revertida põe o VECTOR A vermelho, e casa 1x | `offline-unit` |
| +20 dispatchers / +208 case targets no GoW2 (citado do A/B de 2026-07-30) | `offline-unit` — **contagem de emissão do lifter, não `in-boot`**, com a ressalva "mais não é melhor" a viajar junto |
| Aceitação pelo upstream | **não reclamada** — sem PR, sem push |

**Nenhum aceite `in-boot` é reclamado por este plano.** Não houve build de
`boot_gow2` nem smoke de intro — nem eram precisos.
