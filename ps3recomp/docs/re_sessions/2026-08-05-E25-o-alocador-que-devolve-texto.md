# E25 — o alocador que devolve texto, e é o objecto da parede 1

**Data:** 2026-08-05 · **Antecessor:** [E24](2026-08-05-E24-o-caminho-de-alocacao.md)

Sonda do alocador virtual de `func_00210878` (`PS3_TRACE_ALLOC878`, gated, OFF
por default). Uma corrida, tecto folgado. **Raiz encontrada.**

---

## A medição

**237 alocações. 236 devolvem ponteiros de heap sãos. Uma devolve texto.**

```
#1..#236   alvo=0x004067E4  obj=0x005147C8  slot=0x00527A70  -> r3=0x4077xxxx  (passo 0x38)
           alvo=0x00406974  obj=0x00514A80  slot=0x00527A80  -> r3=0x4077xxxx
#237       alvo=0x0040FD64  obj=0x00514E80  slot=0x00527FD0  -> r3=0x2F725F70  "/r_p"
```

A chamada má não é a mesma função a falhar: é **outro alocador**. Alvo
diferente, objecto diferente, slot de vtable diferente.

## O objecto é o da parede 1

`obj = 0x00514E80` é **o mesmo endereço** do aborto que definiu a parede 1:

```
[ppu] FATAL: stuck calling 0x00514E80
```

medido em 6 de 6 corridas antes do fix, e 0 de 6 depois
([E7](2026-08-05-E7-fix-verification-parede-1.md)).

O fix da parede 1 fez desaparecer o **aborto**. Não fez — nem pretendia fazer —
com que o objecto passasse a comportar-se. Ele continua lá, e agora vê-se o que
faz quando não aborta: **o seu alocador devolve texto**.

Isto liga as duas frentes da sessão. A parede 1 e a parede que persigo desde o
E8 são o mesmo objecto, vistas em momentos diferentes.

## A cadeia completa, do topo ao sintoma

```
objecto 0x00514E80  (o da parede 1)
  slot de vtable 0x00527FD0 -> codigo 0x0040FD64
  func_00210878 chama-o para alocar 0x40 bytes
  devolve 0x2F725F70  ("/r_p")            <- A RAIZ
func_00210ED4  ramo "nao encontrei" -> r3 = esse valor
               lr=0x00210F8C -> func_00227528 -> ... -> func_002182A4
func_002182A4  this = 0x2F725F70
               descritor derivado do this: divisor=130 (sos: 128)
               pool = *(base) = 5
func_00263554  head = vm_read32(9) -> guard barra -> 0
func_002210BC  this final = NULL
func_00220284  16 escritas UNCOMMITTED, stride 0x10
```

Cada elo está medido. As proporções também: 1 em 237 alocações, 1 em 13
chamadas a `func_002182A4`, 1 em 8625 pops da free-list — números diferentes
porque medem sítios diferentes da mesma cadeia.

## Uma pista lateral que fecha

O E14 mediu 13 entradas em `func_002182A4`, das quais **uma vinha de um chamador
diferente**: `lr=0x0040FD28`. O alocador mau está em `0x0040FD64` — **36 bytes
à frente**, na mesma função. Não é coincidência: `func_0040FD..` aparece dos
dois lados da cadeia.

Na altura registei que "o chamador raro tem `this` são" e passei à frente. Era
o mesmo sítio a assinar as duas anomalias.

## Refutado por medição — não voltar a perseguir

1. **`func_00210878` estar partida** — 236 de 237 alocações são sãs; o que muda
   é o alocador virtual invocado.
2. **Ser um problema do laço/lista** (E18, E24) — a lista e o laço estão bons; o
   valor entra pelo ramo de alocação.
3. Tudo o que E11–E17 e E19–E24 já refutaram (tabela de tipos, string no
   endereço 4, escrita através de nulo, célula escrita entre chamadas, `r29`
   vindo do TOC).

## Próximo (E26)

1. **Ler `0x0040FD64`** no lift: porque é que devolve `0x2F725F70` em vez de uma
   alocação. É uma função concreta, e o valor devolvido é um caminho — a
   suspeita natural é uma confusão entre "devolve o objecto" e "devolve o nome
   do objecto", ou um campo lido no offset errado.
2. **Verificar se o objecto `0x00514E80` está bem construído.** É o objecto da
   parede 1; o fix dessa parede podou um dispatch, não reparou o objecto.
3. `*(r30+0x460C)` em `func_00242C94` — o caminho declarado para o menu.

## Nota

O que fez a diferença nesta rodada foi o **tecto**. A primeira corrida parou em
200 acertos e mostrou 200 alocações sãs — teria sido fácil concluir "o alocador
está bom". O caso mau é o **#237**.

É a terceira vez nesta sessão que um tecto de sonda esconde a resposta (E12,
E17, e agora). A regra que fica: **numa sonda de reconhecimento, o tecto começa
alto e só se aperta depois de se saber a densidade** — e um resultado que bate
exactamente no tecto nunca é um resultado, é um aviso.
