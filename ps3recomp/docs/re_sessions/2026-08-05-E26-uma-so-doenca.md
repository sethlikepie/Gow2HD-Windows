# E26 — uma só doença: os dois valores são cabeças de free-list

**Data:** 2026-08-05 · **Antecessor:** [E25](2026-08-05-E25-o-alocador-que-devolve-texto.md)

Leitura de duas funções (30 + 45 linhas). Confirma por código a hipótese que o
E24 tinha marcado como **inferência, não medição** — e unifica os dois valores
patológicos que a série persegue desde o E8.

---

## A descida, dois níveis

`func_0040FD64(this, size)` — o alocador mau do E25 — é fino:

```c
r29 = this;  r27 = size (0x40)
func_00212ED0(r3=this, r4=0)              // A ALOCACAO
r28 = r3
func_00217BC0(r3=r28, r4=size, r5=this)   // construtor
return r28                                 // devolve o que o alocador deu
```

Não decide nada: devolve o que `func_00212ED0` devolver.

E `func_00212ED0` termina assim:

```c
r9  = *(r2 - 0x2B7C)              // ancora no TOC
...  ps3_indirect_call            // obtem o gestor -> r31
if (*(r31 + 0x12C) != 0) func_0022831C(...)     // reabastece?
loc_00212F38:
r9 = (r30 << 2) + 0xD0            // r30 = 0 (passado por func_0040FD64)
r3 = *(r31 + r9)                  // pool = *(gestor + 0xD0)   -- indice 0
func_00263554(r3)                 // <- O POP DA FREE-LIST
return r3
```

**O último acto é `func_00263554` — o pop.** O valor devolvido é uma **cabeça de
free-list**.

## Os dois valores são a mesma coisa

| valor | via | janela em `/r_perma.wad` |
|---|---|---|
| `0x2F725F70` `"/r_p"` | pop do pool `*(gestor + 0xD0)` (E25, E26) | offset 0 |
| `0x726D612E` `"rma."` | pop do pool vindo da tabela (E8, E12) | offset 5 |

**Ambos são cabeças de free-list, e ambos contêm texto de caminhos.** Não são
duas paredes: é uma free-list — ou um conjunto de pools — cujos nós guardam
texto em vez de ligações.

Isto fecha o que o E24 escreveu como hipótese explícita:

> *"Se `func_00210878` aloca pela mesma free-list de `func_00263554`, então os
> dois valores patológicos da série inteira têm a mesma origem."*

**Confirmado**, e por leitura de código, não por mais medição.

## O que sobra, e a hipótese com nome

Se os nós da free-list contêm texto, alguém pôs texto numa região que foi depois
entregue ao alocador como nós livres. Duas formas, e o código aponta para a
primeira:

**H-A — reabastecimento sobre memória com strings.** `func_00212ED0` chama
`func_0022831C` quando `*(gestor + 0x12C) != 0`. Se esse reabastecimento talha
nós a partir de um buffer que já contém caminhos, a free-list fica com texto.
O log da sessão já mostra a operação a existir:

```
[FACTORY] freelist REPLENISH fo=0x400D6808 fl=0x47AB4000 shell=0x47AB4400 type=0x0004
```

**H-B — free duplo / uso após libertação.** Um buffer de caminho ainda vivo é
devolvido ao pool e depois entregue como nó.

Nenhuma está verificada. **H-A tem sonda existente** (`[FACTORY]`) e é a mais
barata de testar: correlacionar o endereço reabastecido com o endereço do nó
envenenado.

## Também explica a raridade

`pool = *(gestor + 0xD0)` com `r30 = 0` → **índice 0**. E no `func_002182A4` a
aritmética do E13 também dava `idx = 0`. Os dois caminhos batem no **mesmo
primeiro pool**, o que é consistente com um único pool contaminado e com as
proporções medidas: 1 em 237 alocações, 1 em 13 chamadas, 1 em 8625 pops.

## Refutado por leitura

1. **`func_0040FD64` ser o culpado** (E25) — é um invólucro; devolve o que lhe
   dão.
2. **Os dois valores patológicos serem problemas distintos** — ambos saem de
   `func_00263554`.
3. Tudo o que E11–E17 e E19–E25 já refutaram.

## Próximo (E27)

1. **Testar H-A**: correlacionar `[FACTORY] freelist REPLENISH` com o pop
   envenenado — o `fl=` do reabastecimento cai na mesma região do nó mau?
   A sonda já existe; falta uma corrida com as duas ligadas.
2. Se H-A cair, testar H-B com uma sonda no `free` do pool índice 0.
3. `*(r30+0x460C)` em `func_00242C94` — o caminho declarado para o menu.

## Nota — o que a série custou e onde chegou

Dezanove ledgers, do sintoma (16 escritas em memória não commitada) até uma
frase: **um pool cujos nós livres contêm texto de caminhos**.

Pelo caminho, cinco conclusões minhas foram refutadas por medição e três
ferramentas minhas estavam partidas. Nenhuma refutação veio de pensar melhor:
vieram de controlos e de números que tinham de aparecer e não apareceram.

E as duas últimas rodadas — as que mais avançaram — foram **leitura de código**,
não medição. Só foram possíveis porque a medição tinha dito exactamente que
funções ler.
