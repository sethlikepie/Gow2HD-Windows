# E48 — o salto é um ciclo, e o jogo mesmo não verifica

**Data:** 2026-08-07 · **Antecessor:** [E47](2026-08-07-E47-um-so-fio-nao-e-corrida.md)

Discriminador do E47, corrido em ferramenta **independente do nosso lifter**:
`ghidra_out/functions.json` e `ghidra_out/decompiled.json`. Responde às duas
perguntas em aberto, e **a minha hipótese do E47 cai**.

---

## Retracção: o salto para trás é um ciclo legítimo

O E47 levantou a hipótese de que `func_0024E3D0` acabar em
`g_trampoline_fn = func_0024E2AC` (salto de 0x124 bytes para trás) fosse o
padrão do bug `2550C8` — fallthrough cross-fragment inventado pelo lifter.

**Refutada.** O Ghidra vê **uma só função** na região:

```
0x0024E198  size=960  end=0x0024E558   FUN_0024e198
```

`0x24E2AC`, `0x24E3D0` e `0x24E424` estão todos **dentro dela**. E a
decompilação mostra a estrutura que justifica o salto — um `for` sobre uma
lista ligada, dentro de um `do/while` sobre 13 ranhuras:

```c
do {
  for (puVar3 = *(undefined4 **)(iVar11 + iVar10 + 0xb8); puVar3 != NULL;
       puVar3 = (undefined4 *)*puVar3) {
      ...
      if (param_3 != 0) { /* vt[0x48] aqui */ }
  }
  bVar1 = iVar10 != 0x30;
  iVar10 = iVar10 + 4;
} while (bVar1);
```

O salto `func_0024E3D0 → func_0024E2AC` é a **aresta de retorno do `for`**. Não
há bug de lift aqui. O padrão `2550C8` continua por auditar no resto do lift,
mas **este caso não é um exemplar dele**, e a auditoria não ganha o caso
concreto que o E47 lhe queria dar.

## E o jogo, no original, também não verifica o nulo

Isto é o resultado que vale a rodada. As duas chamadas a `vt[0x48]` na
decompilação do **binário original**, sem passar pelo nosso lifter:

```c
/* fora do ciclo, fábrica FIXA table[1] */
iVar10 = (*(code *)**(undefined4 **)(**(int **)(puVar14 + 4) + 0x48))
             (*(int **)(puVar14 + 4));
*(short *)(iVar11 + 6) = (short)*(undefined4 *)(iVar10 + 0x20);

/* dentro do ciclo, fábrica indexada por tag */
iVar9 = (*(code *)**(undefined4 **)
          (**(int **)(puVar14 + (uint)*(ushort *)((int)puVar3 + -0x12) * 4) + 0x48))(...);
*(short *)((int)puVar3 + -0xe) = (short)*(undefined4 *)(iVar9 + 0x20);
```

**Nenhuma das duas verifica o retorno.** Chamam e dereferenciam `+0x20` na
instrução seguinte.

Logo a ausência de check que o E44 mediu no lift **é fiel ao original** — não é
uma instrução perdida pelo nosso lifter. O E44 tinha-o inferido pela assimetria
dos guards; agora está confirmado por uma segunda ferramenta que não partilha
código nenhum com a primeira.

## O que isto reposiciona

A invariante do jogo é real e agora está lida no original: *«a fábrica tem
produto quando eu chego aqui»*. O jogo aposta nisso 13 ranhuras vezes o
comprimento de cada lista, sem uma única verificação.

Portanto o defeito **não é o deref, não é o lift do deref, e não é o salto**. É
o estado: quatro fábricas estão vazias num ponto em que o jogo garante que não
podem estar.

E o sítio onde a nossa cadeia falha é o **do ciclo** — o indexado por tag
(`*(ushort *)(elem - 0x12)`), que é o que bate com os dois `lr=0x0024E424` a
darem fábricas diferentes (`0x400C6210` e `0x403008E8`) na mesma corrida.

## Duas pistas novas que a decompilação oferece

1. **As chamadas estão atrás de `param_3 != 0`.** Se `param_3` fosse 0, não
   haveria pedido nenhum e a cadeia não disparava. Vale medir o que o passa.
2. **O ramo onde vivem é o do `else`** — `piVar8[1] >= 1`, o caminho que *tira*
   um objecto de uma lista, em contraste com o `if` que o *cria* via
   `vt[0x14]`/`vt[0x18]`. A cadeia só falha no caminho de reutilização.

## Refutado por medição

1. **«o salto para trás de `func_0024E3D0` é o padrão `2550C8`»** — é a aresta
   de retorno de um `for`, dentro de uma única função `0x24E198..0x24E558`
   (retracção da hipótese do E47).
2. **«o check de nulo existe no original e o lifter perdeu-o»** — não existe no
   original; duas chamadas, duas derefs imediatas.
3. Tudo o que E11–E17 e E19–E47 já refutaram.

## Próximo

1. **quem popula as fábricas** — o jogo garante produto; medir quem empurra e
   porque é que não empurrou para estas quatro antes do ciclo;
2. **`param_3`** — de onde vem e porque é que é não-zero nesta passagem;
3. o `if` que cria (`vt[0x14]`/`vt[0x18]`) contra o `else` que reutiliza: se a
   criação nunca correu para estes tipos, a fábrica nunca teve o que devolver;
4. `idx=24444` (E38) e o flake `FIOSOPEN=14` (E33), ainda por explicar.
