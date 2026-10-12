# E50 — o `lr` não é o chamador: o lifter não o escreve em chamadas indirectas

> **UMA TABELA CORRIGIDA e depois REPOSTA** (ver «O que a sonda de escopo mediu»):
> marcada sem base pelo [E54](2026-08-07-E54-o-pop-negativo-escreve-retracto-o-mecanismo-do-E43.md),
> reposta por medicao pelo [E55](2026-08-07-E55-o-instrumento-que-perdia-campos.md).
> A retraccao central deste ledger -- o `lr` nao e' o chamador -- mantem-se.

**Data:** 2026-08-07 · **Antecessor:** [E49](2026-08-07-E49-nao-e-fase-e-sitio.md)

A sonda de escopo (`patch_39da78_scope_probe.py`, push e pop) deu um resultado
que não podia ser: **`0x002B2DE0` aparece como sítio de push E como sítio de
pedido falhado.** Ou era real, ou o instrumento mentia. Mentia.

---

## A raiz do erro de leitura

O lifter escreve `ctx->lr` **só em chamadas directas**:

```c
ctx->lr = 0x002B2DE0; func_002B2660(ctx); DRAIN_TRAMPOLINE(ctx);   // directa: escreve
ps3_indirect_call(ctx); DRAIN_TRAMPOLINE(ctx);                      // indirecta: NAO escreve
```

E `ps3_indirect_call` também não o escreve — varridas as **467 linhas** da
função em `runtime/ppu/ppu_loader.cpp`: **zero** escritas em `->lr`, só três
leituras dentro de `fprintf`.

Logo, quando `vt[0x48]` corre (chamada **indirecta**), o `ctx->lr` que a sonda
imprime é o da **última chamada directa**, seja ela de que função for.

Os oito valores confirmam-no — cada um é o retorno de uma directa para uma
função sem relação nenhuma com a fábrica:

```
ctx->lr = 0x0024E424;  func_0020EE8C(ctx);
ctx->lr = 0x0027C724;  func_0024C1F8(ctx);
ctx->lr = 0x002B2DE0;  func_002B2660(ctx);
```

O «mesmo sítio faz push e pede produto» era isto: o `lr` estava velho da
chamada a `func_002B2660`, e tanto o push como o `vt[0x48]` correram com ele
ainda pendurado.

## Retracção ao E49

**CAI:** a coluna «função» da tabela do E49 lida como *chamador de `vt[0x48]`*.
O `lr` marca um ponto por onde o fio passou, não quem fez a chamada.

**SOBREVIVE:** a partição. Oito valores distintos de `lr`, `253 ok` contra
`6 VAZIO`, **nenhum grupo misto**. Como o `lr` só muda numa chamada directa,
ele funciona como **marcador de contexto dinâmico**: as chamadas que acertam e
as que falham vivem em contextos disjuntos. Isso continua a refutar «é fase da
corrida», que era o ponto do E49.

## O que se salva por outra via, e o que não

**Salva-se `FUN_0024e198`** — e não pelo `lr`. A decompilação do E48 mostra a
ordem dentro do ciclo:

```c
for (puVar3 = ...; puVar3 != NULL; puVar3 = *puVar3) {
    if ((uVar12 & 0xffff) == 3) { _opd_FUN_0020ee8c(puVar3 + -5,0,0); }   // <- poe lr=0x24E424
    ...
    if (param_3 != 0) { iVar9 = (*(code *)**(... + 0x48))(...); }          // <- vt[0x48], lr velho
}
```

`FUN_0020ee8c` é chamada **na mesma iteração, poucas instruções antes** do
`vt[0x48]`. O `lr` velho é exactamente o que se espera se o pedido vier deste
ciclo. Confirmado pelo fluxo de controlo, não pela semântica do `lr`.

**Não se salvam as outras sete atribuições.** Para `lr=0x0027C724`, tudo o que
se sabe é que nenhuma chamada directa ocorreu entre `func_0024C1F8` e o
`vt[0x48]` — o pedido pode vir de `FUN_0027c6f0` depois do retorno, ou de
dentro de `func_0024C1F8`, ou de qualquer descendente que só use chamadas
indirectas. É um **cone**, não um ponto.

Fica também sem base a vizinhança `FUN_002b2dd0` / `func_002B2E04` que o E49
anotou: assentava em ler `0x002B2DE0` como chamador.

(O E45 já tinha escrito, sobre a mesma tabela, que os nomes eram *«o fragmento
que contém o endereço de retorno guest, não necessariamente a função lógica»*.
A ressalva estava certa; o E49 é que a esqueceu.)

## O que a sonda de escopo mediu, e isso fica

| fábrica | push | pop |
|---|---|---|
| `0x400C6210` | 176 | 176 |
| `0x403008E8` | 175 | 175 |
| `0x40300E80` | 175 | 175 |
| `0x400D6DA0` | 175 | 175 |
| `0x401002F0` | 176 | 176 |

> **NUMEROS SEM BASE PARA QUATRO DELAS, em 2026-08-07, pelo
> [E54](2026-08-07-E54-o-pop-negativo-escreve-retracto-o-mecanismo-do-E43.md).**
> A sonda `SCOPE` que produziu esta tabela esta' ancorada DEPOIS do salto, dentro
> de `func_0039DAB0` -- os pops que passam por `func_0039DAF4` (o ramo do cursor
> negativo, que **escreve**) nao sao contados. Para `0x400C6210` a vigia
> `PS3_WATCH_STORE` cobre todas as larguras e todos os sitios, e confirma
> 176/176; para as outras quatro o equilibrio **nao esta' estabelecido**.
>
> **REPOSTO em 2026-08-07 pelo [E55](2026-08-07-E55-o-instrumento-que-perdia-campos.md):**
> refeitas as contagens com a vigia sobre os cinco cursores, as cinco fabricas
> **estao equilibradas** (176/176, 175/175, 176/176, 175/175, 175/175) e
> `func_0039DAF4` -- o ramo negativo do pop -- **nunca corre**. O numero do E50
> estava certo; era o instrumento que nao o podia sustentar.

**As cinco fábricas que falham são todas empurradas ~175 vezes, e equilibradas.**
Nenhuma está esfomeada; nenhuma tem fuga. Isto reforça o E43 (LIFO encaixado) e
alarga-o das 176 de uma fábrica para cinco fábricas independentes.

## O instrumento certo para a rodada seguinte

A vigia `WATCHSTORE` já faz o correcto e serve de modelo: **endereço de retorno
do HOST** via `__builtin_return_address` + `dladdr`, que imprime
`ra0=_Z13func_0039DA78P11ppu_context+0x150 ra1=ps3_indirect_call+0xB10`. Esse
`ra` é o chamador real, imune a este problema.

Para a `EMPTYFAB`: `ra(1)` — o nível acima de `ps3_indirect_call` — dá o
fragmento guest que fez a chamada. É a próxima sonda, e só depois dela se pode
voltar a nomear chamadores.

## Refutado por medição

1. **«`ctx->lr` identifica quem chamou `vt[0x48]`»** — o lifter só o escreve em
   chamadas directas, e `ps3_indirect_call` não o escreve nas 467 linhas dele.
2. **«`0x002B2DE0` faz push e pede produto»** — é um `lr` velho de
   `func_002B2660`, partilhado pelos dois eventos.
3. **«alguma das cinco fábricas está esfomeada»** — todas com ~175 push e ~175
   pop, equilibradas.
4. Tudo o que E11–E17 e E19–E49 já refutaram.

## Próximo

1. **`ra` de host na sonda `EMPTYFAB`**, como a `WATCHSTORE` já faz — é o único
   caminho para nomear chamadores com base;
2. só então repetir a pergunta do E49 (o que estabelece o escopo) com nomes
   fiáveis;
3. `idx=24444` (E38) e o flake `FIOSOPEN=14` (E33), ainda por explicar.
