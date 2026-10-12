# E85 — a fábrica esvazia-se por desenho, `TABLE[4]` existe, e o NULL está mais fundo

**Data:** 2026-09-01 · **Antecessor:** [E84](2026-09-01-a-fabrica-vazia-devolve-0-e-o-0-vira-o-TOC.md)

O E84 fechou a cadeia do sintoma até à fábrica vazia. Esta sessão sobe a partir daí.
Cinco corridas, cada uma a matar uma hipótese. Nível: `in-boot` onde marcado.

---

## 1. A fábrica não «perde» produtos — é esvaziada por fluxo normal (in-boot)

`PS3_TRACE_SCOPE` + `EMPTYFAB`, sem cap. A fábrica `0x400D6DA0` recebe **175 PUSH / 175
POP** e a consulta do E84 é feita com `cursor=-1`:

```
[EMPTYFAB] fab=0x400D6DA0 cursor=-1 VAZIO->devolve-0 ra1=func_00264BA8 ra2=func_00237CDC
[EMPTYFAB+] ra3=func_000B70B0 ra4=func_000B71B8          <- a cadeia de init B71
```

O último POP antes disso vem de `lr=0x000415A4` — e **esse POP atinge as 15 fábricas
exactamente uma vez cada**: é `FUN_000c2e68`, chamado de `FUN_00041494`, que o
decompilado mostra ser «carrega uma coisa dentro de um escopo»: `vt[0x40]` push →
trabalho → `FUN_000c2e68` (pop em todas) → `vt[0x44]` pop. **Teardown de fim de fase,
por desenho.** Entre esse teardown e a consulta: **zero PUSH**.

Ou seja: no nosso boot, a cadeia de init B71 pergunta à fábrica **depois** de todos os
escopos terem sido fechados. No console isso crasharia (`NULL+0x48` numa página não
mapeada) — logo no console a ordem, ou o conteúdo, é outro.

## 2. Entre o teardown e a consulta: 8 despachos virtuais sobre NULL (in-boot)

```
[ICALL-BAD] ctr=0x00514E80 lr=0x000C9994 r3=0 r11=0
[ICALL-BAD] ctr=0x00514E80 lr=0x0013EF60 r3=0 r11=0
... (8, todos ctr=0x00514E80, quase todos r3=r11=0)
```

`0x00514E80` é uma **vtable** (`0x528020 = {0x218804, TOC}`), não um OPD: chamar uma
vtable como código é o que sai de `*(*(0)+off)`. Oito sítios, o mesmo NULL.

Os 8 `lr` são `bl 0x251230` — chamadas **directas** a `FUN_00251230`, um inicializador
puro (sem `bctrl`) de um descritor na stack. O `lr` volta ao chamador pelo `mtlr` do
epílogo; o `bctrl` que falha é o **seguinte** em cada chamador:

```
lwz r31, -0x5EF0(r2)     ; r31 = *(TOC slot) = 0x00868D48 = a TABLE de tipos
lwz r11, 0x10(r31)       ; r11 = TABLE[4]   <- o gestor de tipo 4
... r11->vt[5](r11, descritor)
```

Todos os 8 chamadores são chamados **directamente de `func_000B71B8`** (B71).

## 3. `TABLE[4]` existe — H-"nunca registado" refutada (in-boot)

`PS3_WATCH_STORE=0x868D58,0x868D4C,0x868DA0` (TABLE[4], e TABLE[1]/[22] como controles):

```
 89: TABLE[1]  := 0            func_002B2998   (zeragem inicial)
 90: TABLE[4]  := 0            func_002B2998
 91: TABLE[22] := 0            func_002B2998
365: TABLE[22] := 0x400C3D88   func_0042B078   (registo)
366: TABLE[1]  := 0x400C5048   func_0041FA90   (registo)
367: TABLE[4]  := 0x400D6808   func_0041CA58   (registo)   <- EXISTE
4377: [ICALL-BAD] ... r11=0                                 <- 4000 linhas depois
```

O registador escreve `TABLE[(mgr+4 >> 16) & 0xFFF] = mgr` — o índice é o subtag do
próprio gestor (ledger 2026-08-03: vtable do gestor `0x515AA0`, `+0x08` registar).

## 4. H1 (TOC errado) e H2 (zerado por escrita host) — refutadas na mesma corrida (in-boot)

Sonda `TAB4` no sítio exacto de `func_000C992C`, capturando `r2` antes do load e
`r31`/`r11` depois, com leitura directa de `0x868D58` como controle:

```
[TAB4] r2_antes=0x00541178 r31=0x00868D48 r11=0x400D6808 mem[0x868D58]=0x400D6808 CTRL=TOC-OK
```

TOC certo, TABLE certa, **`r11 = 0x400D6808`, não NULL**, e a memória concorda com o
registo capturado. **O `bctrl` deste sítio é feito sobre um objecto válido.**

## 5. Logo o NULL está um nível abaixo

O `ps3_indirect_call` **não** actualiza o `lr` do guest (E50). O `ICALL-BAD` com
`lr=0x000C9994` e `r3=0` acontece **dentro** do método `0x400D6808->vt[5](obj, desc)` —
ou mais fundo — onde *outro* objecto está NULL. É esse método que a sonda `TAB4 v2`
identifica (captura o `ctr`), para se ler o lift dele e ver o que carrega o `r3` que
chega a zero.

## Refutado nesta sessão (não voltar a perseguir)

1. **«um push desapareceu / a assimetria push-pop do E76 esvazia a fábrica»** — 175/175,
   e o esvaziamento é o teardown normal de `FUN_000c2e68`. A pergunta é de **ordem**
   (quem consulta depois do fecho), não de contagem.
2. **«o gestor de tipo 4 nunca é registado»** — `TABLE[4] := 0x400D6808` na linha 367.
3. **«o TOC está errado nos 8 chamadores»** — `r2 = 0x00541178`, medido no sítio.
4. **«`TABLE[4]` é zerado por escrita host entre o registo e a consulta»** — `r11` capturado
   e `mem[0x868D58]` lido no mesmo instante, ambos `0x400D6808`.
5. **«os 8 `ICALL-BAD` são o `bctrl` dos 8 chamadores»** — são de dentro do callee; o `lr`
   do guest não distingue níveis abaixo de um icall.

## O que sobrevive de tudo (medido)

Toda a cadeia do E84 (fábrica vazia → 0 → TOC → rodata → wrap). O teardown por desenho
(§1). Os 8 despachos sobre NULL com o mesmo `ctr` (§2). `TABLE[4]` existente (§3). O
sítio dos 8 chamadores a despachar sobre objecto válido (§4).
