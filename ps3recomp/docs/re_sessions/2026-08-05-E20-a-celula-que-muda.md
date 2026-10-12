# E20 — a célula que muda: `*(tabela_de_tipos + 0xC)`

> **CONCLUSOES RETIRADAS em 2026-08-05 pelo [E22](2026-08-05-E22-a-sonda-que-recalculava.md).**
> O `A` desta analise era um **recalculo** (`vm_read32(r2-0x2B18)` refeito na sonda),
> nao o registo vivo -- que ja' tinha sido sobrescrito. A vigia de traducao mostrou
> **zero** leituras de `0x00868D54` numa corrida com 13 chamadas, o que e' impossivel
> se `B = *(A+0xC)`. As medicoes de `B`, `C`, `D`, `divisor`, `base` e `pool` mantem-se.


**Data:** 2026-08-05 · **Antecessor:** [E19](2026-08-05-E19-o-descritor-vem-do-TOC.md)

Sonda nova dos quatro degraus da cadeia do TOC (`PS3_TRACE_TOCCHAIN`, gated,
OFF por default). Uma corrida, 13 chamadas, e a divergência fica isolada num
único degrau.

---

## A medição

```
12x   A=0x00868D48  B=0x406387E0  C=0x400C4A20  D=0x406387E4
 1x   A=0x00868D48  B=0x400C6B50  C=0x400C4A20  D=0x400C6B54
```

| degrau | expressão | resultado |
|---|---|---|
| **A** | `*(r2 - 0x2B18)` | **idêntico nas 13** |
| **B** | `*(A + 0xC)` | **DIVERGE** — `0x406387E0` (12×) vs `0x400C6B50` (1×) |
| **C** | `*(B + 0x0)` | **idêntico nas 13** |
| **D** | `*(C + 0x50)` | acompanha B (`= B + 4` nos dois casos) |

A âncora é a mesma, o passo seguinte não. **A divergência nasce em
`*(A + 0xC)`** e tudo a jusante é consequência.

## A âncora é a tabela de tipos

`A = 0x00868D48` não é um endereço qualquer: é a **tabela de tipos**, o mesmo
endereço que serviu de sonda de CONTROLO no trabalho da parede 4
(`if (r28 != 0x00868D48) tab_bad++`). Esta frente e a cadeia de assets já
mapeada partilham a mesma raiz de dados.

`C = 0x400C4A20` é constante e também vive na vizinhança `0x400Cxxxx` da
fábrica (`0x400C5048`) e do "objecto de Julho" (`0x400C61C8`) mapeados a
2026-08-01.

## O que a constância de A e C implica

Se `A` é o mesmo e `*(A + 0xC)` devolve dois valores diferentes ao longo da
mesma corrida, então **a célula em `0x00868D54` é escrita entre chamadas**.
Não é uma leitura de sítio errado — é a mesma leitura, do mesmo sítio, a dar
respostas diferentes no tempo.

As 12 sãs dão todas `0x406387E0`; a má dá `0x400C6B50`. Como as 12 vêm antes e
a má é a última (E18: é a 13ª e vem depois das outras doze), o quadro compatível
é: **a célula muda uma vez, e a chamada seguinte lê o valor novo.**

Isto **não está provado** — a ordem temporal das 13 está medida (E18), mas não
instrumentei a escrita da célula. É a hipótese que o E21 tem de testar.

## Correcção a mim próprio

Nos E13–E19 chamei `obj` ao valor que a sonda `POOLIDX` imprime e tratei-o como
`r3`. Comparando com esta medição: `0x406388F8 − 0x406387E0 = 0x118`, e
`0x400C6C68 − 0x400C6B50 = 0x118`. Logo o que a `POOLIDX` chama `obj` é
**`r29 = B + 0x118`**, não `B`. As conclusões não mudam (as duas famílias
continuam a ser duas), mas o rótulo estava errado e alguém a ler os ledgers
seguiria o registo errado.

## A cadeia, com a raiz um degrau acima

```
[?] alguem escreve 0x400C6B50 em *(0x00868D48 + 0xC)     <- A RAIZ (por provar)
func_002182A4  B = *(A+0xC) = 0x400C6B50   (nas 12 anteriores: 0x406387E0)
               r29 = B + 0x118 = 0x400C6C68
               divisor = *(r29) = 130      (nas sas: 128)
               base = *(r29+0x14) = 0x4007FCE8
               pool = *(base) = 5
func_00263554  head = vm_read32(9) -> guard barra -> 0    (E17)
func_002210BC  this final = NULL
func_00220284  16 escritas UNCOMMITTED                     (E8, E10)
```

## Refutado por medição — não voltar a perseguir

1. **A âncora do TOC estar errada** — `A = 0x00868D48` nas 13 chamadas.
2. **`C` ou `D` serem a origem da divergência** — `C` é idêntico nas 13; `D`
   apenas acompanha `B`.
3. **`obj` da `POOLIDX` ser `r3`** — é `r29 = B + 0x118` (correcção acima).
4. Tudo o que E11–E14 e E16–E19 já refutaram.

## Próximo (E21)

1. **Quem escreve `*(0x00868D48 + 0xC)`?** O endereço é fixo e conhecido, o que
   torna isto o caso mais fácil da série: uma vigia de escrita nesse endereço
   exacto, com o `func_<EA>` de quem escreve. A vigia de memória baixa do E16
   já tem a forma certa — falta-lhe aceitar uma gama arbitrária em vez de
   `[0, N)`.
2. Confirmar a ordem: a célula muda **uma** vez, e é entre a 12ª e a 13ª
   chamada? (imprimir a célula a cada chamada resolve, e é uma linha na sonda
   que já existe)
3. `*(r30+0x460C)` em `func_00242C94` — o caminho declarado para o menu.
