# E79 — o disjuntor só dispara em alvo não resolvido

**Data:** 2026-08-08 · **Antecessor:** [E78](2026-08-08-E78-a-parede-e-um-ciclo-de-wrap-de-angulo.md)

Adenda ao E78, da terceira análise. Confirma a cadeia por outro caminho e
**corrige uma leitura minha sobre o que a ausência de `stuck/FATAL` prova**.

---

## Uma leitura minha, corrigida

O E75 escreveu:

> *«E `stuck/FATAL` = 0: o disjuntor `PS3_STUCK_ICALL_LIMIT` não disparou. Não é
> um icall a bater no limite; é um bloqueio genuíno lá dentro.»*

A primeira metade é facto; a segunda era inferência, e o mecanismo desmente-a.
Lido em `ppu_loader.cpp:2823-2873`: **o disjuntor só corre no ramo do alvo NÃO
RESOLVIDO.** Um icall com alvo resolvido que gire para sempre nunca o dispara.

Logo `stuck/FATAL = 0` **não distingue** «bloqueio genuíno» de «icall resolvido
em ciclo». Não invalida a conclusão do E78 — a cadeia de migalhas mostra onde
para — mas o argumento que usei para lá chegar não valia.

## A estrutura, com uma precisão

Os três ramos de `func_002B2660` **convergem**: em A e B o despacho virtual é
seguido da mesma chamada final que o ramo C faz sozinho.

```
(*(*(*TABLE[5])+0xC))(TABLE[5])
```

Não são três destinos — são dois despachos possíveis **mais um comum
obrigatório**. A pergunta «qual ramo» era menos importante do que parecia.

E a medição que eu já tinha responde-a na mesma:
`[B71] func_002B2660 #001 -> ctr=0x004244C0` → **correu o ramo C**, e o `ctr`
impresso **é** o alvo. `TABLE[5]->vt[0xC] = func_004244C0`.

## O registry é o mesmo dos gestores

`base = *(0x0053FCD8) = **0x00868D48**` — array de 256 slots, com `obj = TABLE[0]`
e o alvo do ramo C em `TABLE[5]`.

É **exactamente** o `FACT = 0x868D48` que a análise das fábricas (E76) tinha
identificado, alcançado por 146 aliases de TOC. Duas análises independentes, o
mesmo array.

## Dois mecanismos de bloqueio silencioso, para não se perderem

Nenhum está na cadeia medida, mas ambos são reais e invisíveis às sondas actuais:

**1. Espera activa em `FUN_002B3CB0`:**

```c
while (*(char*)(*(0x006FF480)+4) == '\0') { <stub de import>; usleep(500); }
```

Se a flag nunca ficar não-zero, o tick não volta **e não gasta CPU em icalls** —
compatível com tudo o que medi. Caminho:
`2B25AC → 0B765C → 02C3F0 → 2C00DC → 2B4340 → 2B4224 → 2B3CB0`.

**2. Cabeçalhos de lista circulares.** `FUN_0039D8EC`, `FUN_0042B9AC` e
`FUN_0042BA44` fazem `while (p) p = *p` **sem mutar a lista**, e `FUN_002B28BC`
constrói objectos cujo cabeçalho em `+0x24` é circular auto-referente
(`puVar3 = &obj[9]; *puVar3 = puVar3;`). Um desses percorrido por um destes
walkers não termina — sem log, sem icalls, sem CPU anómala.

`TABLE[0]` não é desses (`FUN_002B2998` põe `obj[9]=0`), mas os filhos podem ser.

## Refutado por medição

1. **«`stuck/FATAL = 0` prova que é bloqueio genuíno e não um icall em ciclo»** —
   o disjuntor só corre no ramo do alvo não resolvido.
2. **«os três ramos levam a sítios diferentes»** — convergem na mesma chamada
   final.
3. Tudo o que E11–E17 e E19–E78 já refutaram.

## Próximo

Inalterado face ao E78: **imprimir os FPRs à entrada de `func_00457E44`**. É o
passo que decide se o bloqueio é um float enorme a entrar no wrap — e, se for,
se a origem é a passagem de floats no lift.
