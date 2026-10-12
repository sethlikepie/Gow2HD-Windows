# E19 — o descritor vem do TOC, não de `this`

> **CONCLUSAO RETIRADA em 2026-08-05 pelo [E23](2026-08-05-E23-a-raiz-e-o-this.md).**
> A medicao emparelhada mostra que o valor que alimenta `r29` acompanha o `this`
> chamada a chamada, nao a cadeia do TOC. A cadeia `A->B` existe e e' CONSTANTE
> nas 13 chamadas (`B=0x400C6210`) -- alimenta outra coisa. A leitura linear do
> lift atribuiu uma proveniencia que nao foi medida.


**Data:** 2026-08-05 · **Antecessor:** [E18](2026-08-05-E18-a-13a-e-a-mesma.md)

Leitura estática do lift, sem corridas. Fecha a tensão que o E13 abriu e o E18
deixou declarada.

---

## A tensão

`this = 0x2F725F70` (texto) na 13ª chamada, mas `divisor=130` e
`base=0x4007FCE8` são **valores lidos com sucesso**. Se os campos viessem de
`this`, o guard tê-los-ia devolvido a 0.

## A resolução

`func_002182A4`, entre a entrada (`ppu_recomp_000.cpp:492831`) e o sítio do
divisor:

```
492839  gpr[10] = vm_read32(gpr[2] + -0x2B18)   <- TOC
492864  gpr[11] = vm_read32(gpr[10] + 0xC)
492868  gpr[3]  = gpr[11]
492870  gpr[10] = vm_read32(gpr[11] + 0x0)
492874  gpr[11] = vm_read32(gpr[10] + 0x50)
492875  gpr[0]  = vm_read32(gpr[11] + 0x0)
492882  gpr[0]  = gpr[3] + 0x118
492884  gpr[29] = rldicl(gpr[0], 0, 32)         <- r29 = r3 + 0x118
492886  gpr[11] = vm_read32(gpr[29] + 0x14)     <- base da tabela
```

**`r29 = r3 + 0x118`**, e `r3` (o `obj` que a sonda `POOLIDX` imprime) é
carregado de uma cadeia ancorada no **TOC**, em `*(r2 - 0x2B18)`. O `this` que
chega no `r3` de entrada é **sobrescrito** em 492868, antes de qualquer uso
nesta parte.

Ou seja: a tensão não era tensão. Os campos são lidos de uma estrutura global
válida, e por isso saem valores reais mesmo com `this` a ser texto.

## O que isto muda no alvo

Confirma que `obj` é `r3`, e a aritmética bate nas duas famílias:

| | `obj` (`r3`) | `r29 = obj+0x118` | `base = *(r29+0x14)` |
|---|---|---|---|
| 12 sãs | `0x406388F8` | `0x40638A10` | `0x40773710` |
| 1 má | `0x400C6C68` | `0x400C6D80` | `0x4007FCE8` |

A pergunta deixa de ser sobre `this` e passa a ser: **porque é que a cadeia
ancorada no TOC devolve `0x400C6C68` numa chamada e `0x406388F8` nas outras
doze?**

`0x400C6C68` continua na vizinhança `0x400Cxxxx` da fábrica (`0x400C5048`) e do
"objecto de Julho" (`0x400C61C8`) mapeados a 2026-08-01 — o que liga esta
frente ao trabalho antigo em vez de abrir uma nova.

## Relação com o `this` = texto

Continua a ser verdade que a 13ª chamada recebe `this = 0x2F725F70` (E14) e que
é a mesma que dá `pool=5` (E18, linhas consecutivas). Mas **o `this` mau não é
a causa do descritor mau** — são dois sintomas da mesma chamada anómala, não um
a causar o outro.

O que os une é o laço em `0x00210F8C`, que produz doze iterações regulares
(passo `0x34`) e uma décima terceira que não pertence a nenhum bloco.

## Refutado por medição — não voltar a perseguir

1. **`r29` (o descritor) ser derivado de `this`** — é `r3 + 0x118`, com `r3`
   vindo do TOC e a sobrescrever o `this` de entrada em 492868.
2. **A tensão do E13/E18** (valores reais com `this` inválido) — resolvida, não
   havia contradição.
3. **O `this` mau causar o descritor mau** — são irmãos, não pai e filho.
4. Tudo o que E11–E14 e E16–E18 já refutaram.

## Próximo (E20)

1. **Porque é que a cadeia do TOC dá `0x400C6C68` na 13ª chamada?** Os passos
   são poucos e todos `vm_read32` — uma sonda que imprima `gpr[10]`, `gpr[11]`
   e `gpr[3]` nos quatro pontos (492839, 492864, 492870, 492874) mostra em qual
   deles as duas famílias divergem. É a mesma técnica que já resolveu o
   `POOLIDX`.
2. O 13º elemento do laço em `0x00210F8C` (E18) — provavelmente a mesma raiz.
3. `*(r30+0x460C)` em `func_00242C94` — o caminho declarado para o menu.
