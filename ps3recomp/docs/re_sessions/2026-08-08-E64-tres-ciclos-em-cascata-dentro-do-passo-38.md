# E64 — três ciclos em cascata dentro do passo #38, ≥16 384 eventos invisíveis

**Data:** 2026-08-08 · **Antecessor:** [E63](2026-08-08-E63-o-disjuntor-contava-ate-16-e-absorvia-milhares.md)

O E63 pediu uma auditoria: *«se este cap mentiu, os irmãos mentem»*. Feita — e
revelou um terceiro absorvedor, este a descartar **DMAs de SPU**.

---

## A auditoria do padrão `if (n++ < K)`

**102 ocorrências** em 8 ficheiros. A esmagadora maioria são sondas puras, onde
o cap só limita impressão e é inofensivo. O perigo é o cap que **esconde um
efeito**.

Filtrando por «bloco com mudança de estado por perto»: **12 candidatos**, dos
quais 10 se revelaram falsos positivos do meu filtro (`[SHADERSTREAM]`,
`[SHADERVEC]`, `[OPD-ICG]`, `[GIANT]`, e dois `while (... && n++ < N)` que são
limites de spin, não caps de print — legítimos e outra coisa).

**Dois reais**, além do par já corrigido no E63:

| sítio | efeito escondido | estado |
|---|---|---|
| `ppu_loader.cpp` `ICALL-HEAP40` | salta a chamada, `r3=0` | opt-in, default OFF |
| `spu_dma.h` `SKIP out-of-range` | **`return -1`: descarta a DMA** | **activo** |

Os dois passam a imprimir as primeiras 16 mais cada potência de 2.

## O terceiro absorvedor, medido

```
[spu-dma] SKIP #4096 out-of-range GET ea=0x00003300 size=13184 lsa=0x0BC00
```

**≥ 4096 DMAs descartadas**, das quais o log mostrava **20**. Três operações
distintas, todas com o mesmo endereço base em **memória baixa do guest**:

```
GET ea=0x00003300 size=13184 lsa=0x0BC00
PUT ea=0x00003308 size=13056 lsa=0x03780
PUT ea=0x00003308 size=8     lsa=0x00008
```

`0x3300` está abaixo de `0x10000` — o limiar do guard. É a mesma família do
«memória baixa como ponteiro» do E15/E29 e do fallback `0x1c4` do E53.

## Três ciclos, em cascata

| ciclo | linhas | contagem | operando constante |
|---|---|---|---|
| `ICALL-ASCII` | 3523 → 3907 | **≥ 8192** | `ctr=0x005F6D6F` |
| `ICALL-HEAP` | 4297 → 5115 | **≥ 4096** | `ctr=0x2F776164` (`"/wad"`) |
| `spu-dma SKIP` | 5486 → 6076 | **≥ 4096** | `ea=0x00003300` |

**Sequenciais e não sobrepostos.** Um acaba, o seguinte começa. Cada um com o
operando **constante** ao longo de milhares de iterações — a assinatura de uma
máquina de estados encalhada a repetir a mesma operação inválida.

E os três cabem inteiros dentro do passo **#38** do B71 (linhas 4009 → 20767).

## O que isto reposiciona

A cadeia dos E41–E61 produz **4** escritas estragadas, na linha 20778. Estes
três ciclos produzem **≥ 16 384** eventos absorvidos, entre as linhas 3523 e
6076 — **antes**, e quatro ordens de grandeza acima.

Não é a mesma parede. E era inteiramente invisível: três disjuntores nossos, os
três com o cap no `fprintf`, os três a devolver «não aconteceu nada» ao jogo.

## Não-regressão

`VICTIM=4`, `UNCOMMITTED=19`, `FIOSOPEN=241`, B71 até **#060**, e a lib do
runtime a compilar em `build-macos`. As mudanças são só de contagem.

## Refutado por medição

1. **«o defeito do cap era só nos dois disjuntores ICALL»** — há um terceiro, no
   `spu_dma.h`, e esse descarta DMAs.
2. **«102 caps é uma dívida enorme»** — 10 dos 12 candidatos eram sondas puras;
   a dívida real eram 4 sítios, agora fechados.
3. Tudo o que E11–E17 e E19–E63 já refutaram.

## Próximo

1. **quem pede a DMA de `ea=0x00003300`** — é um job de SPU com uma base
   inválida; ligar ao lado PPU que a monta;
2. **`FUN_0014e570`** — o ciclo dos dois `ICALL`, e de onde vem
   `0x005F6D6F` / `"/wad"` para o `ctr`;
3. porque é que a cascata é sequencial — se o segundo ciclo é consequência do
   primeiro, há um só defeito a montante dos três;
4. o que o E62 deixou: fábricas por-tag não repovoadas, B71 encalhado no #60;
5. `idx=24444` (E38) e o flake `FIOSOPEN=14` (E33), ainda por explicar.
