# E72 — generalizei de duas linhas em doze, e o cap era meu

**Data:** 2026-08-08 · **Antecessor:** [E71](2026-08-08-E71-e-a-fila-BA10-alinhada-a-16-e-nao-a-128.md)

Duas correcções ao E71, e a segunda é embaraçosa: **a sonda que escrevi tinha o
mesmo defeito de cap que passei o dia inteiro a corrigir noutros sítios.**

---

## Correcção 1 — li duas linhas de doze

O E71 concluiu, das duas primeiras linhas:

> *«É a fila. `0xBA10` é múltiplo de 16 mas não de 128 [...] ambos os valores
> estão dentro dos 256 KB do LS»*

As **doze** linhas capturadas dizem outra coisa:

| `fila` | `& 127` | vezes | plausível como endereço? |
|---|---|---|---|
| `0x0000BA10` | 16 | 2 | **sim** — é literal no código (`spu_ila(0xBA10)`, 11 sítios) |
| `0x0000FFFF` | 127 | 4 | não — parece sentinela |
| `0x00010203` | 3 | 6 | não — parece padrão de bytes |

E os buffers incluem `0xF6900003`, `0xF7300003`, `0xFA700003` — **fora dos
256 KB do LS**. A frase «ambos dentro do LS» é falsa para a maioria da amostra.

E o `[EDGEATT1]` mostra **quatro** códigos distintos, não um:

```
12  [EDGEATT1] a=0x00000000 b=0x80410911
 6  [EDGEATT1] a=0x0000BA10 b=0x80410910
15  [EDGEATT1] a=0x0000FFFF b=0x80410910
 3  [EDGEATT1] a=0x43008180 b=0x80410901
```

**SOBREVIVE:** a sonda funciona; nas amostras com `0xBA10` é mesmo a fila que
falha (`buffer & 15 = 0`); e `0xBA10` é um endereço estático do código, com
`& 127 = 16`.

**CAI:** «é a fila, `0xBA10`, defeito de colocação em LS» como *o* diagnóstico.
É **um** de pelo menos três padrões de entrada, e dois deles não parecem
endereços de todo — o que aponta para lixo a chegar ao check, não para um objecto
mal colocado.

## Correcção 2 — o cap era meu

A v1 da sonda copiou das vizinhas:

```c
{ static int n = 0; if (n < 12) { n++; ... } }
```

Cap no **print**. Deu exactamente **12 linhas em 12** — ou seja, o cap batido, e
a contagem real desconhecida. É o mesmo defeito do `ICALL-HEAP`, do
`ICALL-ASCII` e do `spu-dma SKIP`, que eu tinha corrigido **no dia anterior**
(E63/E64), e cujo comentário eu próprio escrevi: *«um instrumento que se cala ao
chegar ao limite mente da mesma forma que um que não cobre todos os caminhos»*.

Copiei o estilo das sondas ao lado sem lhe aplicar a regra que tinha acabado de
derivar. A v2 usa as primeiras 16 mais cada potência de 2, como o resto da
árvore.

## E o intermitente deixou de reproduzir

| leva | corridas | disparos |
|---|---|---|
| até ao E70 | ~11 | **4** (~36%) |
| `PS3_SPU_DBG=1` | 10 | 0 |
| sem a variável | 4 | 1 |
| sonda v2, sem a variável | 6 | 0 |

**1 em 20 nas últimas levas**, contra ~36% antes. Dois confundidores candidatos,
nenhum resolvido:

1. a variável `PS3_SPU_DBG` (o E71 já o tinha anotado);
2. **carga da máquina** — as corridas antigas eram intercaladas com builds; estas
   correram em série, sem nada mais a compilar.

O segundo é o que eu não tinha considerado, e é o mais provável dos dois: o
defeito depende de timing, e eu mudei o ambiente sem reparar.

Enquanto isto não estiver resolvido, **não consigo reproduzir a falha a pedido**,
e qualquer fix ficaria sem prova.

## O que não muda, pela quinta vez

`UNCOMMITTED=19` e `st620=11` nas **vinte** corridas, com e sem a variável, com
e sem a falha, com as duas versões da sonda.

## Refutado por medição

1. **«é a fila `0xBA10`, um defeito de colocação em LS»** — é um de três padrões
   de entrada; dois não são endereços plausíveis.
2. **«ambos os valores estão dentro do LS»** — `0xF6900003` e companhia não estão.
3. **«há um só modo de falha»** — o `[EDGEATT1]` mostra quatro códigos distintos.
4. Tudo o que E11–E17 e E19–E71 já refutaram.

## Próximo

1. **reproduzir a pedido** antes de mais nada — testar a hipótese da carga da
   máquina correndo com um build em paralelo;
2. só depois: de onde vêm `0xFFFF` e `0x00010203` no `r8`;
3. os quatro códigos do `[EDGEATT1]` são quatro falhas distintas ou uma cascata?
4. promover o default do `PS3_GCM_CB`;
5. a parede dos E41–E61: fábricas por-tag não repovoadas, B71 no #60.
