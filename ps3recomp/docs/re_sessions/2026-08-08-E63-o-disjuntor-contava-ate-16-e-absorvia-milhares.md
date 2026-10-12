# E63 — o disjuntor contava até 16 e absorvia milhares

**Data:** 2026-08-08 · **Antecessor:** [E62](2026-08-07-E62-o-B71-para-no-60-e-ha-uma-segunda-corrupcao.md)

O E62 escreveu *«16 despachos por texto absorvidos em silêncio»*. **O 16 era o
cap do `fprintf`, não a contagem.** Os números reais são três ordens de
grandeza acima.

---

## O defeito no disjuntor

```c
if (addr >= 0x20000000u && addr < 0x40000000u) {
    static int n = 0;
    if (n++ < 16)                    /* <- cap no PRINT */
        fprintf(stderr, "[ICALL-HEAP] skip ...");
    ctx->gpr[3] = 0;
    return;                          /* <- o skip nao tem cap nenhum */
}
```

O mesmo padrão em `[ICALL-ASCII]`. O disjuntor absorvia despachos
indefinidamente e o log mostrava sempre 16 — que foi o número que eu publiquei.

É o mesmo defeito que o próprio repositório já tinha documentado, no comentário
do `ps3_watch_store_cap`:

> *«Um instrumento que se cala ao chegar ao limite mente da mesma forma que um
> que não cobre todos os caminhos.»*

## A correcção

Contador `unsigned long` sempre incrementado, e imprime as **primeiras 16**
(detalhe) **mais cada potência de 2** (magnitude). Sem `atexit`: as corridas são
mortas por `kill -TERM` e um dump no fim nunca sairia.

## Os números reais

| disjuntor | `ctr` | ASCII | primeira → última linha | contagem |
|---|---|---|---|---|
| `ICALL-ASCII` | `0x005F6D6F` | `"\0_mo"` | 3511 → 3891 | **≥ 8192** |
| `ICALL-HEAP` | `0x2F776164` | `"/wad"` | 4283 → 5094 | **≥ 4096** |

**≥ 12 288 despachos por fragmentos de texto**, absorvidos em silêncio até
hoje. E são limites inferiores: o total verdadeiro está entre a última potência
de 2 impressa e a seguinte.

Os dois `ctr` são **constantes** ao longo de todas as ocorrências, e todos os
`lr` caem em `0x0014E478..0x0014F1E8` — a região de `FUN_0014e570` (2116 bytes)
e vizinhas. São **dois ciclos apertados**: 8192 iterações em 380 linhas de log,
4096 em 811.

## O que isto muda no mapa

Esta não é a cadeia dos E41–E61. É mais cedo (linhas 3511–5094 contra 20778),
muito maior, e vive noutra função. A cadeia que segui produz **4** escritas
estragadas; esta absorve **milhares** de saltos.

E era invisível: o disjuntor devolve `r3=0` e segue, o jogo continua, e o log
mostrava dezasseis linhas de rodapé.

## Não-regressão

A corrida com o contador corrigido reproduz tudo o resto: `VICTIM=4`,
`UNCOMMITTED=19`, `FIOSOPEN=243`, B71 até **#060**. A mudança é só de contagem.

## Refutado por medição

1. **«16 despachos por texto absorvidos» (E62)** — 16 era o cap do print; os
   reais são ≥ 8192 e ≥ 4096, em dois disjuntores distintos.
2. **«o `"/wad"` é uma ocorrência isolada»** — é um ciclo de milhares de
   iterações com o `ctr` constante.
3. Tudo o que E11–E17 e E19–E62 já refutaram.

## Próximo

1. **`FUN_0014e570`** — o que é o ciclo, e de onde vem o `0x005F6D6F` /
   `0x2F776164` que ele mete no `ctr`;
2. **auditar os outros caps do runtime** com o mesmo padrão `if (n++ < K)` — se
   este mentiu, os irmãos também mentem;
3. o que o E62 deixou: porque é que as fábricas por-tag não são repovoadas, e o
   B71 encalhado no #60;
4. `idx=24444` (E38) e o flake `FIOSOPEN=14` (E33), ainda por explicar.
