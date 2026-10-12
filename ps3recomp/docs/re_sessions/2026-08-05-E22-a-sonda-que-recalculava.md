# E22 — a sonda que recalculava em vez de medir

**Data:** 2026-08-05 · **Antecessor:** [E21](2026-08-05-E21-a-celula-muda-sem-o-guest.md)

Vigia de tradução estendida a endereço exacto. **Controlo passa** (2 acertos em
`0x00869EEC`). **Medição: zero** traduções de `0x00868D54` em 90 s — numa
corrida em que a `TOCCHAIN` disparou **13 vezes**.

Cada uma dessas 13 leituras de `B` teria de traduzir esse endereço. Zero é
impossível. **O E20 está errado, e o defeito é da minha sonda.**

---

## O defeito

A sonda `TOCCHAIN` imprime `A` assim:

```c
uint32_t _A = vm_read32((uint32_t)ctx->gpr[2] - 0x2B18u);   /* relido */
```

**`_A` é um recálculo, não uma captura.** O `gpr[10]` que o programa usou em
`492864` foi lido em `492839` e **sobrescrito em `492870`**, antes do ponto
onde a sonda corre. Em vez de guardar o registo vivo, refiz a expressão e
assumi que dava o mesmo.

Daí que `A` saísse constante (`0x00868D48`) nas 13 chamadas: eu estava a medir
a **minha própria aritmética**, que é determinística, e não o valor que o
programa usou.

A cadeia que o E20 declarou — `A → B = *(A+0xC)` — não está medida. `B` está
(é `gpr[3]`, capturado vivo, e diverge de facto). `A` não.

> Regra: **uma sonda que recalcula um valor não está a medir o programa — está
> a medir-se a si própria.** Se o registo já foi sobrescrito no ponto de
> inserção, a sonda tem de ser inserida onde ele ainda vive, não reconstruída
> mais à frente.

## O que sobrevive e o que cai

| | estado |
|---|---|
| `B` diverge (`0x406387E0` 12× / `0x400C6B50` 1×) | **medido, mantém-se** |
| `C`, `D`, `r29`, `divisor`, `base`, `pool=5` | **medidos, mantêm-se** |
| `A = 0x00868D48` constante nas 13 | **CAI** — é recálculo |
| «a divergência nasce em `*(A+0xC)`» | **CAI** |
| «a âncora é a tabela de tipos» | **CAI** — assentava em `A` |
| «a célula `0x00868D54` é escrita entre chamadas» (E20/E21) | **CAI** — nunca é sequer lida |

O E21 continua válido no que mediu (zero escritas naquela célula) mas a
pergunta que ele fazia era sobre uma célula que **não participa**. Os dois
ledgers ficam com as suas medições e sem as suas conclusões.

## O que isto não muda

A frente principal continua intacta e é a do E18: a **13ª chamada** a
`func_002182A4` é anómala em tudo o que foi capturado ao vivo — `this` é texto
(`0x2F725F70`), `B` é de outra família, `divisor` é 130 em vez de 128, `pool`
é 5, e o resultado final é `this=NULL` com 16 escritas `UNCOMMITTED`. Doze
chamadas antes dela são regulares, com passo `0x34`.

Nada disso dependia de `A`.

## Próximo (E23)

1. **Capturar `gpr[10]` vivo em `492839`**, e `gpr[11]` vivo em `492864` — uma
   sonda no sítio certo, não uma reconstrução mais à frente. Só então se sabe
   qual é a âncora real e onde a cadeia diverge.
2. Com a âncora real em mão, repetir o `PS3_WATCH_EA` sobre a célula
   verdadeira.
3. `*(r30+0x460C)` em `func_00242C94` — o caminho declarado para o menu.

## Nota de método — o balanço dos zeros

Quatro zeros nesta sessão, e cada um significou uma coisa diferente:

| | zero | significado | o que o separou |
|---|---|---|---|
| E12 | `FLHEAD` sem tecto | **inconclusivo** | comparar nº de linhas |
| E16 | escritas em `[0,0x1000)` | **refutação válida** | controlo com limite alto |
| E21 | escritas na célula | **inconclusivo** (macro não compilada) | controlo + `strings` no binário |
| E22 | traduções da célula | **refutação válida** — e refuta o meu próprio E20 | controlo, e 13 leituras que tinham de aparecer |

Os quatro são indistinguíveis olhando só para o resultado. **O controlo é o que
os separa, e nesta sessão pagou-se quatro vezes.**

E o E22 acrescenta um caso novo: um zero pode refutar não a hipótese em teste,
mas **o instrumento que produziu a hipótese anterior**. Foi preciso um número
que *tinha* de aparecer (13 leituras) e não apareceu.
