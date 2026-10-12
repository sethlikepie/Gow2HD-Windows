# E23 — a raiz é o `this`, e a tabela de tipos está inocente

**Data:** 2026-08-05 · **Antecessor:** [E22](2026-08-05-E22-a-sonda-que-recalculava.md)

Sonda nova da âncora **viva** (`PS3_TRACE_ANCHOR`), corrida em par com a
`TOCCHAIN`. Uma corrida, 13 chamadas, as duas sondas na mesma função a ~20
linhas de distância.

---

## A medição emparelhada

| | ANCHOR (no ponto da leitura) | TOCCHAIN (20 linhas depois) |
|---|---|---|
| #1–#12 | `A=0x00868D48` `celula=0x00868D54` **`B=0x400C6210`** · `this` são | `B=0x406387E0` |
| **#13** | `A=0x00868D48` `celula=0x00868D54` **`B=0x400C6210`** · **`this=0x2F725F70`** | `B=0x400C6B50` |

**A âncora, a célula e `*(célula)` são idênticos nas treze chamadas.** Inclusive
na má. A tabela de tipos não participa na divergência.

O que diverge no ponto tardio acompanha o **`this`**: `this` são → `0x406387E0`;
`this` = texto → `0x400C6B50`.

## O que isto fecha

**E20 fica inteiramente refutado** — não por inferência (E22) mas por medição
directa: `*(0x00868D54)` vale `0x400C6210` nas 13, portanto não é onde a
divergência nasce. E confirma o E22 pelo lado positivo: a vigia de tradução
tinha razão em não ver leituras que importassem naquela célula.

**E19 também cai.** Escrevi que `r29` vinha da cadeia do TOC e não de `this`.
A medição diz o contrário: o valor que alimenta `r29` acompanha o `this`
chamada a chamada. A cadeia `A→B` existe e é constante — alimenta outra coisa.

Errei nos dois casos da mesma maneira: **li o lift linearmente e atribuí
proveniências sem as medir.** O E22 nomeou-o para a sonda; aplica-se igualmente
à leitura de código.

## A cadeia, reduzida a uma raiz

```
laco em 0x00210F8C entrega um 13o "objecto" que e' texto: 0x2F725F70 "/r_p"
func_002182A4  this = texto
               -> descritor derivado do this: divisor=130 (sas: 128)
               -> base -> pool = *(base) = 5
func_00263554  head = vm_read32(9) -> guard barra -> 0
func_002210BC  this final = NULL
func_00220284  16 escritas UNCOMMITTED, stride 0x10
```

Uma raiz, cinco consequências. Tudo o que os E8–E23 mediram ao vivo encaixa, e
nada exige a tabela de tipos, nem escritas em memória baixa, nem strings
mapeadas no endereço 4.

## O alvo, agora sem ambiguidade

**O que é o 13º elemento do laço em `0x00210F8C`?**

O que se sabe dele, medido: os 12 anteriores são objectos regulares em dois
blocos contíguos com passo `0x34`, e o 13º não pertence a nenhum bloco e vale
`0x2F725F70` — que lido como bytes é `"/r_p"`, os primeiros quatro caracteres de
um caminho. O chamador é o mesmo nos treze.

Isso é a assinatura de uma travessia que lê um elemento a mais, ou cujo
terminador está corrompido. **Não provado.**

## Refutado por medição — não voltar a perseguir

1. **A tabela de tipos / `*(0x00868D54)`** — `0x400C6210` idêntico nas 13.
2. **`r29` vir da cadeia do TOC** (E19) — acompanha o `this`.
3. **A célula ser escrita entre chamadas** (E20, E21) — não muda de todo.
4. **String no endereço 4** (E15) — refutada no E17.
5. **Escrita através de nulo pelo guest** (E15) — refutada no E16.
6. Tudo o que E11–E14 já refutaram.

## Próximo (E24)

1. **Sonda no laço `0x00210F8C`**: contador, limite e endereço de onde lê cada
   elemento. Decide entre "passo a mais" e "elemento corrompido", e é a última
   pergunta desta frente.
2. `*(r30+0x460C)` em `func_00242C94` — o caminho declarado para o menu.

## Nota de método — o que esta série custou e o que rendeu

Dezasseis ledgers (E8–E23) para chegar a uma raiz que é uma frase. Pelo
caminho, **quatro conclusões minhas foram refutadas por medição** (E15, E19,
E20, E21) e duas ferramentas minhas estavam partidas (a macro que não compilou,
a sonda que recalculava).

Nenhuma dessas refutações veio de ler melhor o código — vieram todas de um
**número que tinha de aparecer e não apareceu**, ou de um **controlo**. O que
mudou a taxa de erro não foi pensar mais; foi passar a medir a proveniência em
vez de a deduzir, e a correr o controlo primeiro.
