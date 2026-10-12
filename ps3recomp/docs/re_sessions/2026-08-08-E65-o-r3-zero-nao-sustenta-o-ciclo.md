# E65 — o `r3=0` não sustenta o ciclo: hipótese minha, refutada em uma rodada

**Data:** 2026-08-08 · **Antecessor:** [E64](2026-08-08-E64-tres-ciclos-em-cascata-dentro-do-passo-38.md)

Identifiquei o sítio do ciclo `ICALL`, formei uma hipótese sobre o que o
sustenta, e medi-a. **Está refutada.**

---

## O sítio

Um só fragmento no intervalo faz o despacho: `func_0014F774`, dentro de
`FUN_0014f6f8`. E é um **append a buffer**:

```c
puVar3 = *(uint **)(iVar1 + 8);                 /* cursor */
if (*(uint **)(iVar1 + 4) < puVar3 + 1) {       /* cheio? */
    iVar2 = (*(code *)**(undefined4 **)(iVar1 + 0xc))(iVar1,1);   /* vt[0] = crescer */
    if (iVar2 != 0) return;                     /* falhou -> desiste do append */
    puVar3 = *(uint **)(iVar1 + 8);
}
*(uint **)(iVar1 + 8) = puVar3 + 1;
*puVar3 = local_20[0] | 2;                      /* escreve */
```

O objecto tem `+4` = fim, `+8` = cursor, `+0xC` = vtable. No lift:

```c
ctx->gpr[9] = vm_read32(ctx->gpr[31] + 0xC);   /* devia ser descritor/vtable */
ctx->gpr[0] = vm_read32(ctx->gpr[9] + 0x0);
ctx->ctr = (uint32_t)ctx->gpr[0];
```

Com `ctr = 0x2F776164` (`"/wad"`), o campo `+0xC` **aponta para uma string de
caminho** — o fio «o caminho como objecto» do E14, outra vez.

## A hipótese

O disjuntor devolve `r3=0`. Neste sítio, `0` quer dizer **«cresceu com
sucesso»** — o guest não desiste, escreve para lá do fim e volta a tentar.
Parecia explicar os milhares de iterações com o operando constante.

**Previsão:** devolver não-zero fá-lo desistir do append e o ciclo colapsa.

## A medição

Gate `PS3_ICALL_SKIP_RET` (default `0` — baseline inalterado).

| | `r3=0` (default) | `r3=1` |
|---|---|---|
| `ICALL-ASCII` | ≥ 8 192 | **≥ 524 288** |
| `ICALL-HEAP` | ≥ 4 096 | ≥ 4 096 |
| `spu-dma SKIP` | ≥ 4 096 | ≥ 1 024 |
| linhas de log | 21 057 | 36 460 |
| `UNCOMMITTED` | 19 | **40** |

**O ciclo não colapsa — cresce 64×.** E o `UNCOMMITTED` mais que duplica.

## Refutada, e o que fica

**CAI:** «o `r3=0` é o que sustenta o ciclo». Não é. Devolver não-zero faz o
guest desistir *daquele* append, mas quem o chama continua a chamar, e o
sistema desloca-se para um estado pior, não melhor.

**SOBREVIVE**, porque é leitura estática e o experimento não lhe toca:

- o sítio: `FUN_0014f6f8`, `func_0014F774` no lift;
- a semântica do objecto: `+4` fim, `+8` cursor, `+0xC` vtable, `vt[0]` cresce;
- **o campo `+0xC` aponta para uma string de caminho** — é essa a corrupção, e
  ela é anterior a qualquer coisa que o disjuntor faça.

O default fica em `0`. O gate fica como diagnóstico, sem mudar comportamento.

## O que isto ensina sobre o disjuntor

O E62 disse que o disjuntor **esconde**. Isso mantém-se — e o E63/E64 mediram
quanto. Mas a tentação seguinte, «então o valor que ele devolve é o defeito»,
era minha e estava errada.

Um paliativo pode ser mau por esconder sem ser mau por agir. Separar as duas
coisas custou uma corrida.

## Refutado por medição

1. **«devolver não-zero colapsa o ciclo»** — passa de ≥ 8 192 para ≥ 524 288.
2. **«o `r3=0` é o que sustenta o ciclo»** — o ciclo sobrevive e agrava-se com
   `r3=1`; o sustentador está a montante.
3. Tudo o que E11–E17 e E19–E64 já refutaram.

## Próximo

1. **quem põe uma string de caminho no `+0xC`** do objecto de
   `FUN_0014f6f8` — é a corrupção real, e é anterior ao disjuntor;
2. o objecto vem de `**(int **)(iStack_64 + -0x3d40)` — um global; vigiar esse
   endereço com `PS3_WATCH_STORE` nomeia o escritor, como fez no E42;
3. porque é que a cascata é sequencial (E64);
4. `idx=24444` (E38) e o flake `FIOSOPEN=14` (E33), ainda por explicar.
