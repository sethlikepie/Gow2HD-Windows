# E24 — não é uma iteração a mais: é o caminho de alocação

**Data:** 2026-08-05 · **Antecessor:** [E23](2026-08-05-E23-a-raiz-e-o-this.md)

Leitura de `func_00210ED4` (o laço em `0x00210F8C`), 66 linhas. Substitui a
hipótese do E18 por uma explicação estrutural que encaixa em todos os números.

---

## A função é uma travessia de lista com dois desfechos

```
r8  = this
r10 = r8 + 0x18                      // SENTINELA da lista
loc_00210F14:                        // topo do laco
  if (r10 == r0) goto loc_00210F70   // voltou a' sentinela -> LISTA ESGOTADA
  r9 = *(r3 + 0x14)                  // flags do no'
  if ((r9 & 0x90) == 0 && (r9 & *(r4)) != 0) goto loc_00210F80   // ENCONTROU
  r3 = *(r3 + 0x4)                   // next
  goto loc_00210F14

loc_00210F70:                        // <- desfecho A: nao encontrou
  r3 = func_00210878(this, 0x40)     //    ALOCA / cria
loc_00210F80:                        // <- desfecho B: encontrou (r3 = o no')
  lr = 0x00210F8C; func_00227528(r3, ...)
```

`lr = 0x00210F8C` é a chamada a **`func_00227528`**, e é por aí que se chega
indirectamente a `func_002182A4` — o que explica porque é que a sonda do
chamador via sempre o mesmo `lr` nos bons e no mau (E14, E18).

## O que isto explica

| observado | explicação |
|---|---|
| 12 `this` com passo **`0x34`**, em dois blocos | são **nós da lista** — `0x34` é o tamanho do nó |
| o 13º sem relação com os outros | vem do **outro desfecho**: o retorno de `func_00210878` |
| o mesmo `lr` nas 13 | os dois desfechos convergem em `loc_00210F80` antes da chamada |
| o 13º ser texto (`0x2F725F70` = `"/r_p"`) | **`func_00210878(this, 0x40)` devolveu texto** |

**Não é uma iteração a mais.** É o caminho de "não encontrei, cria" a correr
uma vez — o que é comportamento normal — e o alocador desse caminho a devolver
lixo com forma de string.

Isto **supera a hipótese do E18** ("travessia que dá um passo a mais, ou
terminador corrompido"). O terminador está bom: a lista é circular e o laço
pára ao voltar à sentinela `this + 0x18`.

## A ligação que fecha a série

`func_00210878(this, 0x40)` aloca 64 bytes. Se essa alocação passa pelo mesmo
mecanismo de free-list que `func_00263554` — o pop cujo `head` sai `0x726D??2E`
(`"rm?."`) — então **os dois valores patológicos da série inteira têm a mesma
origem**:

| valor | onde apareceu | janela em `/r_perma.wad` |
|---|---|---|
| `0x2F725F70` `"/r_p"` | retorno de `func_00210878` (E14, E24) | offset 0 |
| `0x726D612E` `"rma."` | `head` da free-list (E8, E12) | offset 5 |

Uma free-list cujos nós contêm **texto de caminhos** em vez de ligações
explicaria os dois de uma só vez, e explicaria também porque é que o problema é
raro (1 em 13, 1 em 8625): só afecta quem calha apanhar o nó envenenado.

**Isto é inferência a partir do código, não medição.** A ligação
`func_00210878 → free-list` não está verificada. É a hipótese do E25 e tem um
teste directo.

## Refutado

1. **«iteração que dá um passo a mais»** (E18) — o laço é circular com
   sentinela `this+0x18` e pára correctamente; o 13º vem do outro ramo.
2. **«terminador corrompido»** (E18) — mesma razão.
3. Tudo o que E11–E17 e E19–E23 já refutaram.

Do E18 sobrevive o essencial e o que estava medido: os 12 são regulares, o 13º
não pertence à série, e o chamador é o mesmo. Só a explicação proposta cai.

## Próximo (E25)

1. **Sondar o retorno de `func_00210878`** — uma linha, no sítio onde `r3`
   volta (`loc_00210F70`, logo a seguir ao `DRAIN_TRAMPOLINE`). Confirma que é
   ele que devolve `0x2F725F70` e mostra com que argumentos.
2. **Verificar se `func_00210878` chama `func_00263554`** (o pop da free-list).
   Se chamar, os dois valores patológicos da série têm uma raiz só, e o alvo
   passa a ser o conteúdo da free-list.
3. `*(r30+0x460C)` em `func_00242C94` — o caminho declarado para o menu.

## Nota

Este ledger saiu de ler 66 linhas de código, depois de dezasseis rodadas de
medição. Não teria sido possível antes: só se sabia *que função* ler porque a
sonda do chamador deu `lr=0x00210F8C` (E14), e só se sabia *o que procurar*
porque estava medido que os 12 têm passo `0x34` e o 13º não (E18).

**Ler código cedo teria dado a mesma leitura sem saber o que nela importava.**
A medição não substituiu a leitura — dirigiu-a.
