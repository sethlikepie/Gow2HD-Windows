# E42 — o cursor da fábrica passa abaixo de zero: 11 pops sem push

> **CONCLUSAO REFUTADA em 2026-08-07 pelo [E43](2026-08-07-E43-o-cursor-nunca-passa-abaixo-de-zero.md).**
> O cursor **nao** passa abaixo de zero e **nao** ha' pops a mais. Duas medicoes:
> (a) a tabela deste proprio ledger da' **176 escritas de cada lado** (push 165x`0x01`
> + 11x`0x00`; pop 165x`0x00` + 11x`0xFF`) -- as 11 escritas de `0x00` pelo push cairam
> da conta que produziu «165 contra 176»; (b) o lift de `func_0039DAB0` mostra que o
> decremento **sai sem escrever** quando o valor ja' e' negativo (`if (cr & 8) return;`
> antes do `vm_write8`), logo underflow e' estruturalmente impossivel. `-1` e' o vazio
> da pilha, posto no arranque por `func_002B11B8`.
> **SOBREVIVE:** as 353 escritas e a sua distribuicao, os dois sitios alternantes,
> `0xFF`=-1 confirmado por medicao directa, e toda a cadeia **abaixo** do cursor.
> **CAI:** «11 pops sem push», «o cursor passa abaixo de zero», a aritmetica
> «165 contra 176», e o passo seguinte n.2 (procurar um push saltado por um ramo).
> Mantido como registo do erro -- nao construir sobre isto.


**Data:** 2026-08-07 · **Antecessor:** [E41](2026-08-07-E41-convergencia-com-01-08.md)

Vigia de gama sobre `0x400C62D8` (= fábrica `0x400C6210` + `0xC8`), tecto alto,
corrida sã. **353 escritas, e o padrão é inequívoco.**

---

## A medição

| escritas | valor | quem |
|---|---|---|
| 165 | `0x01` | `func_0039DA78+0x150` |
| 165 | `0x00` | `func_0039DAB0+0x1A8` |
| **11** | **`0xFF`** | **`func_0039DAB0+0x1A8`** |
| 11 | `0x00` | `func_0039DA78+0x150` |
| 1 | `0xFF` | `func_002B11B8+0x1DF8` (a primeira de todas) |

Duas funções alternam — **push** (`0039DA78`, escreve 1) e **pop**
(`0039DAB0`, escreve 0). É um contador de profundidade.

## `0xFF` é −1

`0xFF` lido como byte **com sinal** é `-1`. É exactamente o valor que a nota de
2026-08-01 registou:

```
[FAB48] fab=0x400C6210 code=0x0039E5A8 cursor+C8=-1  <NEGATIVO>
```

**Confirmado por medição directa, seis dias depois e por outro caminho.**

## O defeito

165 pares `1 → 0` são equilibrados. Mas **11 vezes o pop escreve `0xFF`** — o
contador desce abaixo de zero. Um pop sem push correspondente.

E a primeira escrita da corrida é `0xFF`, posta por `func_002B11B8` — ou seja,
o cursor **começa** em −1, antes de qualquer push.

Com o cursor negativo, `func_0039E5A8` (o `vt[0x48]` da fábrica) devolve
**produto nulo**, e `func_0024E3D0` escreve `*(NULL + 0x20)` = 0 como tipo do
objecto — o elo que o E41 fixou.

## Porque é que isto é um defeito e não desenho

A nota de 2026-08-01 já o tinha argumentado, e a medição de hoje sustenta-o:

> *«Num PS3 real isso faria fault na página nula. Logo o jogo nunca pode tomar
> este caminho com produto nulo: a invariante dele é "o produto existe aqui".»*

Se `-1` fosse um estado normal de "pilha vazia", o consumidor teria de o
verificar — e não verifica. **Ou o consumidor tem um guard em falta, ou os 11
pops desequilibrados não deviam acontecer.** As duas leituras são testáveis e
levam a fixes diferentes.

## A cadeia, agora completa em onze níveis

```
11 pops sem push  ->  cursor 0x400C62D8 = 0xFF (-1)          <- A RAIZ
func_0039E5A8   cursor negativo -> devolve produto NULL
func_0024E3D0   *(obj+6) = *(NULL+0x20) = 0
func_0039D3C4   idx 0 -> slot 0 -> devolve 0x400C6B50
func_00212ED0   le'-o como gestor; pool = *(+0xD0) = 0
func_00263554   endereco 0 como pool; head = *(4) = "/r_p"
func_0040FD64   devolve texto como objecto alocado
func_002182A4   this = texto -> divisor 130 -> pool 5
func_002210BC   this final = NULL
func_00220284   16 escritas UNCOMMITTED, stride 0x10
```

O E8 abriu no último elo. Onze níveis depois, a raiz é um contador que passa
abaixo de zero onze vezes por corrida.

## Refutado por medição

1. **«o cursor é posto a -1 uma vez, por engano»** — acontece **12 vezes** por
   corrida (1 inicial + 11 pops), determinístico.
2. **«-1 é lixo de memória não inicializada»** — é escrito de propósito, por
   duas funções identificadas, num padrão push/pop.
3. Tudo o que E11–E17 e E19–E41 já refutaram.

## Próximo, e é a última pergunta desta cadeia

**Porque é que há 11 pops sem push?** Duas formas de o atacar, ambas pequenas:

1. sonda em `func_0039DA78` / `func_0039DAB0` com o `lr` — quem chama o pop a
   mais, e de que caminho;
2. verificar se algum push é **saltado** por um ramo (o padrão do `2550C8`) —
   165 pushes contra 176 pops é uma diferença de exactamente 11.

A segunda hipótese é a mais interessante para o recomp: um push perdido num
fallthrough seria um bug de lift, não do jogo.
