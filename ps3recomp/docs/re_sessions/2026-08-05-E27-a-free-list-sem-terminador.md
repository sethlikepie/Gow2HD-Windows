# E27 — H-A refutada: a free-list acaba em texto, não em zero

> **H-C RETIRADA em 2026-08-05 pelo [E28](2026-08-05-E28-a-linha-que-nao-fecha.md).**
> A hipotese do terminador assentava na linha `[FLHEAD] pool=0 head=0x2F725F70`, que
> nao fecha: a vigia de traducao (instrumento verificado) mostra ZERO leituras abaixo
> de 0x1000 na mesma corrida, logo `vm_read32(4)` devolveria 0. As medicoes das 12
> alocacoes e a ausencia de replenish antes MANTEM-SE.
> Corrige tambem a nota sobre o predicado cirurgico: o da FLHEAD apanha 0x726D612E
> mas NAO apanha 0x2F725F70 -- discrimina metade dos casos, nao os dois.


**Data:** 2026-08-05 · **Antecessor:** [E26](2026-08-05-E26-uma-so-doenca.md)
**Predições:** [E27-predições](2026-08-05-E27-predicoes.md), commit anterior à corrida.

`PS3_TRACE_ALLOC878` com tecto folgado, 3 corridas (a 1ª foi flake e está
declarada). **H-A refutada, e o mecanismo real é mais simples.**

---

## Controlo

A 1ª corrida deu `FACTORY=0` **e** `ALLOC878=0`, com apenas 159 `FLHEAD` contra
as ~8625 de uma corrida sã: **flake, inconclusiva**, descartada sem interpretar.
As duas seguintes: `FACTORY=2`, `ALLOC878=237`, envenenado=1 — **idênticas**.

## As 12 alocações do pool culpado

`obj=0x00514E80` aloca **12 vezes**, não uma:

| # | r3 | | # | r3 |
|---|---|---|---|---|
| 16 | `0x4077F5B4` | | 184 | `0x407D5ACC` |
| 17 | `0x4077F5E8` | | 189 | `0x407D5B00` |
| 126 | `0x4077F61C` | | 192 | `0x407D5B34` |
| 134 | `0x4077F650` | | 220 | `0x407D5B68` |
| 142 | `0x4077F684` | | **237** | **`0x2F725F70`** `"/r_p"` |
| 152 | `0x4077F6B8` | | | |
| 179 | `0x4077F720` | | | |

**Passo `0x34`, em dois blocos contíguos.** É o mesmo `0x34` dos 12 `this` que o
E18 mediu em `func_002182A4` — são estes objectos, deslocados de `0x40` (o
cabeçalho que `func_00217BC0` inicializa).

**Onze nós bons. O décimo segundo devolve texto.**

## H-A refutada

Os dois `[FACTORY] REPLENISH` estão nas linhas **4810 e 4812**; o pop envenenado
está na **4758**. **O reabastecimento acontece depois, não antes.** Não há
nenhum antes.

- **P1 (H-A apoiada)** — refutada.
- **P2 (sem replenish antes)** — confirmada na primeira cláusula.
- Mas o alvo **não** passa a H-B: não há aqui um buffer vivo devolvido ao pool.

## O mecanismo (H-C)

Uma free-list que serve 11 nós e ao 12º devolve texto é uma free-list
**esgotada cujo terminador não é zero**.

O caminho do vazio existe e está correcto: `func_002635A4` devolve `0` quando
`head == 0` (medido no E12 via Grok). **Nunca dispara**, porque o terminador não
é `0` — é texto, e texto não é zero.

```
no' 1 ... no' 11   ->  next validos, entregues em sequencia (passo 0x34)
no' 11.next        ->  deveria ser 0; contem "/r_p"
pop #12            ->  head != 0, logo nao e' "vazio"; devolve o texto
```

E explica a família `0x726D??2E` (`"rma."`) do E8/E12 do mesmo modo: **outro
pool, o mesmo defeito** — o terminador guarda o que estava na memória, e essa
memória contém `/r_perma.wad`.

Também explica porque é que o valor é sempre uma **janela de um caminho**: a
região da free-list fica sobre memória que teve um path, e o campo que devia ser
zerado ficou com os bytes antigos.

**H-C não está provada.** O que está medido é: 11 pops bons, o 12º texto, sem
replenish antes, determinístico em 2 corridas. A leitura do terminador em si não
foi instrumentada.

## Candidatos a fix

| tipo | o que muda | nota |
|---|---|---|
| **Raiz** | quem constrói a free-list zerar o `next` do último nó | toca a construção de todos os pools |
| **Cirúrgico** | o pop tratar um `head` que não é ponteiro plausível como lista vazia | 1 ponto de decisão; o predicado é o que a sonda `FLHEAD` já usa (`_h<0x10000 || _h>=0x50000000`) |
| **Paliativo** | `patch_218364_pool_null_gate.py` | já existe, gated — não é fix |

O cirúrgico tem conjunto negativo medido: **11 pops que não podem ser podados**
(`0x4077F5B4`…`0x407D5B68`) e 1 que deve. E o predicado já está escrito e em uso
noutra sonda deste repo, o que é um bom sinal de que discrimina.

## Refutado por medição

1. **H-A (replenish antes do pop mau)** — os dois `[FACTORY]` vêm 52 linhas
   depois.
2. **O pool ser mau desde o início** — 11 pops sãos antes, com passo regular.
3. **`func_0040FD64` ou `func_00210878` serem o culpado** (E25) — são
   invólucros; o valor vem do pop.
4. Tudo o que E11–E17 e E19–E26 já refutaram.

## Próximo (E28)

1. **Instrumentar o terminador**: no pop, quando `head` não é plausível,
   imprimir também o **nó anterior** e o conteúdo à volta. Confirma H-C e mostra
   se o `next` foi escrito com texto ou nunca foi escrito.
2. Encontrar quem constrói esta free-list (o `fo=0x400D6808` do `[FACTORY]` é
   uma pista) e ver se zera o último `next`.
3. `*(r30+0x460C)` em `func_00242C94` — o caminho declarado para o menu.

## Nota

A 1ª corrida foi um flake e teria sido lida como "H-A refutada, zero factory" se
eu não tivesse posto o `FACTORY` como controlo. Deu zero nos dois contadores ao
mesmo tempo — e é isso que distingue um flake de uma refutação: **quando tudo dá
zero, o instrumento não mediu; quando só o alvo dá zero, mediu.**
