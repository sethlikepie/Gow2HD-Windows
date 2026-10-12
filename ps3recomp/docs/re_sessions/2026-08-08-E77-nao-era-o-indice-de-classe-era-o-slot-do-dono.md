# E77 — não era o índice de classe: era o slot do dono

**Data:** 2026-08-08 · **Antecessor:** [E76](2026-08-08-E76-push-e-pop-usam-indices-diferentes-da-mesma-palavra.md)

Análise estática do contrato do campo `+6`. **Corrige uma premissa que carreguei
desde o E38** e reenquadra o gate do E74.

---

## A correcção

O índice de classe **não** está em `obj+6`. Está em **`obj+2`**. O `+6` só é
índice de classe quando o ponteiro na mão é um **handle = `obj-4`** — ajuste de
ponteiro de classe-base do C++. **O binário usa as duas convenções lado a lado.**

Três provas independentes, com a mesma expressão escrita das duas formas no
mesmo corpo:

| | função | o que mostra |
|---|---|---|
| P1 | `FUN_000415f0` `0x000415F0` | `TABLE[*(u16*)(p+6)](…, p+4)` e logo `uVar3 = p+4; TABLE[*(u16*)(uVar3+2)](…, uVar3)` |
| P2 | `FUN_002bc824` `0x002BC824` | `iVar8 = iVar5-4; iVar7 = iVar8+4;` depois `TABLE[*(u16*)(iVar7+2)]` e `TABLE[*(u16*)(iVar8+6)]` |
| P3 | `FUN_00253f8c` `0x00253F8C` | objecto de `vt[0x14]` guardado como `iVar5-4`, lido em `+6` e `+8` |

E em `FUN_0024e198` o `iVar11` é **objecto, não handle** — é passado cru a
`vt[0x18]` com índice em `iVar11+2`, e o payload da lista foi empurrado cru por
`FUN_0024e558` (`piVar6[2] = iVar5`, sem `-4`).

**Layout:**

```
obj+0    u16  ?
obj+2    u16  indice de classe   -> TABLE[classIdx]
obj+4    u16  flags (bit 0x4 = registado)
obj+6    u16  <-- ESTE: slot do DONO no array de instancias do pool
obj+0x20 int  slot proprio do objecto no mesmo array
```

Logo a linha que persegui trinta ledgers lê-se:

> `obj->donoSlot = produtoCorrente->meuSlot`

## O que isto corrige, e o que não

**Corrige a nomenclatura e a semântica** em toda a cadeia (E38, E41, E44, E48,
E52, E59, E74, E76): onde escrevi «índice de classe» leia-se **«slot do dono»**.

**Não corrige as medições.** As escritas, as vítimas, as contagens (1441/6), a
ponte do `0x40780850` — tudo se mantém. Mudou o que o campo significa, não o que
lá foi escrito.

## O zero é legítimo — e é por isso que é perigoso

`FUN_0039f4d8` (alocador de slot): o **primeiro objecto de um pool recebe o slot
0**, e o 0 volta à free-list quando libertado. Não há reserva nem sentinela.

E **nenhum leitor trata o 0 como caso especial** — varridas as 17 comparações
`+6 == 0` do binário, nenhuma é desta família. A única defesa é `arr[idx] != 0`:
**valida o ponteiro, não o índice.**

Consequência para as 6 escritas más:

- se `arr[0]` estiver ocupado → os objectos ficam atribuídos ao **dono errado**,
  sem erro nem log;
- se `arr[0]` estiver vazio → `FUN_0041b4e0` faz `return` e `FUN_00254954` /
  `FUN_0041e780` **saltam-nos no loop** — existem mas nunca são ligados.

## O default do jogo é **não escrever** — e isso reenquadra o E74

`FUN_0024f028` decide `param_3`, e o agente decodifica-o melhor do que o
[E61](2026-08-08-E61-o-guard-do-jogo-vigia-a-coisa-errada.md):

> `param_3 = (WAD do pool == WAD corrente)`

Com `param_3 == 0`, o `+6` fica **intacto** — mantém o valor herdado do descritor
de classe pelo construtor. **Esse é o valor correcto por omissão.**

Ou seja: o gate `PS3_SKIP_NULLTYPE` que escrevi no E74 como «hack de
diagnóstico, não fiel ao CELL» é, afinal, **mais fiel** que o comportamento
actual. Escrever 0 é activamente pior do que não escrever, porque 0 é um slot
válido que ninguém rejeita.

**Mas o veredicto do E74 mantém-se**: com o gate ligado, `degrau máximo=32`,
`loop=107 voltas`, `st620=11` — **inalterados**. Mais fiel e na mesma não
desbloqueia.

E o `UNCOMMITTED` subir de 19 para 40 com o gate ganha explicação: com o slot
herdado o objecto resolve para um dono **real** e é processado; com 0 talvez
caísse no `arr[0]==0` e fosse saltado. Mais trabalho feito, não mais corrupção —
**hipótese, não medida**.

## Dois gestores, com nome

Por strings, não por suposição:

| slot | gestor | evidência |
|---|---|---|
| `TABLE[22]` | **WADs / contentores** | `"WAD_R_Perm"`, `"WAD_R_Hero"`, `"WAD_R_SubWeapon"`, `"WAD_R_M_Earth"` |
| `TABLE[1]` | **entidades** | `"EntityCount"`, lookup por id em `vt[0x50]` |

É a primeira vez nesta cadeia que os gestores têm nome em vez de endereço.

## Refutado por medição

1. **«o campo `+6` é o índice de classe»** — é `+2`; `+6` é o slot do dono. Três
   provas com as duas convenções no mesmo corpo.
2. **«escrever 0 é o comportamento por omissão»** — o omissão é **não escrever**;
   o campo fica com o valor herdado do descritor.
3. **«0 é um valor inválido que algum leitor rejeita»** — 0 é um slot legítimo e
   nenhum leitor o trata à parte.
4. Tudo o que E11–E17 e E19–E76 já refutaram.

## Próximo

1. **porque é que `TABLE[1]` não tem objecto corrente** quando `FUN_0024f028`
   decide `param_3 = 1` — é o alvo a montante, agora com o gestor nomeado;
2. a parede real continua a ser `func_002B2660` (E75);
3. `PS3_GCM_CB` por promover; intermitente do SPU parado (E72).
