# A cadeia FIOS foi reposta — a parede andou uma etapa, o REG-03 não passou

**Data:** 2026-07-31 · **Gate: 0 de 6.** O REG-03 **não está cumprido**, e isto não é
arredondado para "quase lá".

Mas o que mudou é grande e está medido.

## O que se repôs

Inventário completo dos marcadores `FIOS-*`: **18** no lift antigo (`pre_v4`, que gera o
binário bom), **8** no actual antes desta sessão. Dos 10 em falta:

| marcador | classe | acção |
|---|---|---|
| `FIOS-HOST-POP` | funcional | restaurado (`patch_fios_host_pop.py`) |
| `FIOS-16C-SELFHEAL` | funcional | restaurado |
| `FIOS-42B4-CANCEL-YIELD` | funcional | restaurado |
| `FIOS-DONE-YIELD` | funcional | restaurado |
| `FIOS-DONE-CANCEL-YIELD` | funcional, script existia | **corrigido** — mesma deriva `ctx->lr`, dava SKIP silencioso |
| 6 marcadores de diagnóstico puro | `fprintf` sem alterar estado | não restaurados |

Mais os dois da primeira tentativa: `FIOS-STOP-YIELD` (agulha corrigida) e
`FIOS-FREELIST-REBUILD` (novo).

**Sete scripts funcionais**, todos idempotentes (2.ª passagem = `ALREADY`), três chunks
afectados, `errors=0`.

## O que isso conseguiu, medido em 6/6

| sinal | produção (antes) | com os patches | |
|---|---:|---:|---|
| `SEM OP LIVRE (F2a)` | a falha medida | **0** | ✅ resolvido |
| `startseq` | 1 | **2** | ✅ o 2.º movie arranca |
| `R_PermA` (`bytes_read=20169344`) | 0 | **1** | ✅ **o WAD de 20 MB é lido inteiro** |
| `st620` | 0, 11, 0 (instável) | **11** ×6 | ✅ estabilizou |
| `elo_stopped` | `2o movie (StartSeq)` | `re-Play (NOPIC)` | ⬆ **uma etapa à frente** |
| `thr_auto_load end` | 0 | **0** | ❌ continua |

`HOST-POP` disparou 4× por corrida, `FREELIST-REBUILD` 1×, `42B4-CANCEL-YIELD` 4×.

**A exaustão da free-list FIOS — a causa diagnosticada — está confirmada resolvida por
medição.**

## O número que nunca tinha aparecido

```
run 6:  setflip_after_rperm = 107
```

A Fase 7, ao construir o gate, registou que os elos 6/7 (`SetFlip`/`Pad` pós-WAD) davam
**0 mesmo no binário bom** — e concluiu, correctamente na altura, que nenhum binário
conhecido chegava a desenhar depois do WAD.

Numa das seis corridas, este desenhou **107 frames**. É território que nenhum binário desta
investigação tinha alcançado, incluindo o `boot_gow2.pre_v3` que serviu de referência de
"bom".

Só numa de seis, portanto **não é um resultado** — é um sinal de que a cadeia a jusante
passou a estar viva o suficiente para às vezes progredir.

## A parede nova

`re-Play (NOPIC)`. O `REPLAY-NOPIC` tem a condição `g_vdec_startseq_count >= 2` e o
`startseq` é agora 2 — mas `nopic=0/4` em 6/6. Os logs terminam num loop de
`[MOVIEFSM] st620 0 -> 0` intercalado com `[ICALL-HEAP] skip`, padrão diferente do
esgotamento de free-list original.

**É um problema diferente do que foi diagnosticado**, e fica por investigar.

## O que esta sessão estabeleceu, ao todo

1. **A causa da regressão do 2.º `StartSeq`**: a cadeia FIOS foi apagada pelo re-lift
   porque nunca tinha sido convertida em patches. Confirmada por reposição e medição.
2. **Dez candidatos eliminados** por medição dirigida, todos documentados.
3. **Três métodos falhados** e porquê: bisect dirigido, bisect exaustivo, diff de lifts.
4. **O gate que apanha esta classe de regressão** (Fase 7), em produção.
5. **Sete patches** que repõem código que existia mas não estava versionado.

## O que fica por fazer

- A parede `re-Play (NOPIC)` — nova, não diagnosticada.
- Rever se `SKIP` num patch `FUNCIONAL` devia ser alarme em vez de estado benigno. Foi um
  `SKIP` silencioso que escondeu metade disto.
- O `patch_fios_f2a_f2b_wad.py` continua a ser documental. Está marcado como tal, mas
  aparece no catálogo como patch.

## Honestidade sobre o estado

`thr_auto_load end = 0` em 6/6. O boot **não** foi reposto. O que foi reposto é a cadeia
FIOS, e isso levou o boot de "não abre um único ficheiro de conteúdo" a "lê os 20 MB do
WAD em todas as corridas" — mas a etapa seguinte tem uma parede própria.
