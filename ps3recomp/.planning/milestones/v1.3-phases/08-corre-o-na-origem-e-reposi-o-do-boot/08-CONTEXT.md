# Fase 8: Correção na origem e reposição do boot - Contexto

**Reunido:** 2026-07-30
**Estado:** Pronto para planeamento **assim que a Fase 6 nomear a causa**
**Modo:** Escrito a partir de medição. Deliberadamente **não** especula sobre a causa —
essa é a entrega da Fase 6, e escrevê-la aqui seria pré-julgar o diagnóstico.

<domain>
## Fronteira da fase

Corrigir, **na origem**, a causa que a Fase 6 nomear, e provar que o boot volta a chegar
ao `thr_auto_load` e ao `R_PermA` — medido pelo gate de cadeia que a Fase 7 constrói.

**NÃO faz parte:** diagnosticar (Fase 6) nem construir o gate (Fase 7). Esta fase depende
das duas.
</domain>

<decisions>
## Decisões já tomadas

- **A correcção é na origem, nunca paliativo.** O CLAUDE.md proíbe explicitamente como
  "fix": CRC bypass, stub de combination, estampar magic à mão, null-check cosmético,
  mascarar guard. Se a causa for o lifter, corrige-se o lifter; se for um patch, o patch.
- **Se a causa for o lifter, a correcção tem de sobreviver a um re-lift do zero**
  (`FORCE_REBUILD_LIFT=1`), sem uma única edição manual no lift. É o REQ-04, e é o que o
  marco v1.0 tornou possível.
- **A prova é o gate da Fase 7**, não o `st620`. Foi precisamente medir pelo `st620` que
  deixou esta regressão passar quatro dias.
- **Aceite: `StartSeq=2`, `thr_end=1`, `R_PermA=20169344` em ≥4 de 6 corridas.**
</decisions>

<code_context>
## O que a Fase 6 já eliminou (não repetir)

Três hipóteses testadas e **refutadas por medição** antes desta fase começar:

1. **`truncated-bounds repair`** — A/B mediu que **adiciona 78** funções, não remove.
   E o lift `v3` foi gerado às 01:46, antes do commit das 02:31 — nem sequer o tinha.
2. **`invalid_instructions`** — o próprio commit diz "sem config, byte-a-byte idêntico ao
   scan antigo", e o `build_macos.sh:77` invoca o lifter **sem `--config`**. No-op.
3. **Sub-emissão da cadeia `CD7B4`** — o lift que **falha** tem **19** chamadas a
   `func_000CD7B4`; o que **funciona** tem **8**. Está sobre-emitida, não sub-emitida.

Detalhe: `../gow2-recomp/notes/2026-07-30-duas-hipoteses-do-lifter-refutadas-por-ab.md`
e `2026-07-30-o-bug-first-operand-wins-do-upstream-tambem-e-nosso.md`.

## O que já foi corrigido nesta sessão, e que a Fase 8 NÃO precisa de refazer

**O bug "first TOC-loaded operand wins"** (`ppu_lifter.py`, commit `2e21639`): o
registo-base da jump table era o primeiro operando que decodificasse, não o que valida
alvos. Corrigido, com teste RED→GREEN provado por injecção, suite 22/22, e output que
compila.

Efeito medido: **+20 dispatchers, +208 case targets, +74 funções emitidas** (51 917 →
51 991). Das que acrescenta, **100 estão na lista das 2 855 perdidas**.

> **ATENÇÃO — os números de referência mudaram a meio do marco.** Qualquer plano ou nota
> escrita antes de `2e21639` cita `137 dispatchers` e `51 917 funções`. Hoje são **157** e
> **51 991**. Verificar antes de comparar.

## O alvo — CORRIGIDO, e o alvo anterior estava errado

> **ERRATA (`61d57dd`).** Uma versão anterior deste contexto dizia que o alvo eram
> "2 855 funções perdidas". **Está invalidado.** O sweep dos 9 binários mostrou que
> `boot_gow2.pre_v4` **FALHA** (813 linhas, `StartSeq=0`) — e foi o lift dele
> (`recomp_macos_v2.pre_v4`) que serviu de referência do "que funciona". A comparação
> foi entre dois lifts que ambos produzem binários partidos.
> **Não perseguir as 2 855. Não usar "recuperar funções" como critério.**
> Detalhe: `../gow2-recomp/notes/2026-07-30-CORRECCAO-as-2855-comparavam-dois-lifts-que-falham.md`

O que a tabela do `bisect_regression.sh` estabelece:

```
boot_gow2.rdy0_ref   24 Jul 21:56   StartSeq=2 thr_end=1 r_perma=1   OK
boot_gow2.wip        25 Jul 17:47   StartSeq=2 thr_end=1 r_perma=1   OK
boot_gow2.pre_v3     25 Jul 18:23   StartSeq=2 thr_end=1 r_perma=1   OK
boot_gow2.pre_v4     25 Jul 18:23   StartSeq=0 thr_end=0 r_perma=0   REGRESSAO
... e todos os posteriores                                            REGRESSAO
```

**Nenhum lift que produza um binário OK está guardado.** Logo o diff de lifts, tal como
foi feito, não tem um dos lados.

**O critério passa a ser comportamento, não contagem:** regenerar o lift com o
`ppu_lifter.py` de commits anteriores (~16 s por variante) até um produzir um binário com
`thr_auto_load end >= 1`, medido pelo gate da Fase 7.

Aritmética que continua útil para saber **onde** olhar dentro do lifter:

```
20 750 declaradas (functions.json)
30 927 mid-function wrappers      <- quase tudo vem daqui
   237 case funcs de jump tables
------
51 914  ≈  51 917 emitidas (antes do fix do first-operand-wins)
```

## Custos reais, medidos nesta sessão

| operação | custo |
|---|---|
| lift completo (`ppu_lifter.py`) | ~16 s |
| `-fsyntax-only` de um chunk | ~2 min |
| build de um chunk + relink | ~8 min |
| **build completo** (`build_macos.sh`) | ~30–40 min |
| uma corrida de boot | ~2 min |
| gate de 6 corridas | ~12 min |

O A/B do lifter é barato; o build não é. Ordenar o trabalho por isso.
</code_context>

<specifics>
## Ideias específicas

### O padrão de correcção do lifter está estabelecido

O commit `2e21639` é o modelo para qualquer fix de lifter nesta fase:

1. teste RED→GREEN com fixture mínima e sintética (sem ELF, sem rede);
2. **prova por injecção nos dois sentidos** — repor o bug numa cópia e ver o teste falhar;
3. A/B contra o `EBOOT.ELF` real com os contadores que o lifter já imprime;
4. `-fsyntax-only` do output;
5. suite completa;
6. e dizer explicitamente o que **não** ficou provado (in-boot, tipicamente).

### Um cuidado que já mordeu duas vezes nesta sessão

**Mais não é melhor.** O lift antigo tinha 1 398 funções em endereços que não existem no
binário; o novo tem zero. E a cadeia `CD7B4` está sobre-emitida no lift que falha. Um
aceite baseado em "a contagem subiu" é tão fraco como o `st620 ≥ 3`.

### Se a causa for fora do lifter

A nota de 07-25 avisa que o binário daquela medição tinha WIP não-committado em
`movie_eos_arm.c`, que é a fonte do `st620`. O runtime, o HLE de `cellVdec` e o
`movie_eos_arm.c` estão no âmbito e não devem ser excluídos por comodidade.
</specifics>

<deferred>
## Adiado para depois deste marco

- A parede TYPE15 e os dois probes já construídos (`29AF0`, `CB56C`) — desbloqueiam assim
  que o boot voltar ao `R_PermA`.
- Cherry-picks do upstream: PRs #81, #79, #86, #87, #88.
- `grep -i assert` como passo obrigatório do smoke — lição da issue #75 do upstream: os
  `tw`/`twi` são no-op no lifter, logo os asserts do jogo imprimem e ele **continua com
  estado inválido**. Nunca fizemos isto no GoW2.
</deferred>
