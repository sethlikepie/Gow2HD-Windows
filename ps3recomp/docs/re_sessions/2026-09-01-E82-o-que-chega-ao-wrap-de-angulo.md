# E82 — o que chega ao wrap de ângulo (predições ANTES da sonda)

**Data:** 2026-09-01 · **Antecessor:** [E78](2026-08-08-E78-a-parede-e-um-ciclo-de-wrap-de-angulo.md)
· **Estado:** predições commitadas, sonda ainda não escrita

O E78 deixou o discriminador desenhado e não corrido: *imprimir os FPRs à entrada
de `func_00457E44`*. Antes de o escrever, uma leitura do lift muda o conjunto de
hipóteses — e por isso as predições são escritas aqui primeiro.

---

## O que o lift mostra e o E78 não registou

`ppu_recomp_001.cpp:449848`. Os dois valores do ciclo **não** vêm de endereços
absolutos: vêm do TOC.

```c
{ uint32_t tmp = vm_read32(ctx->gpr[2] + 0x10C); ... ctx->fpr[0]  = ftmp; }  /* minimo  */
{ double a = ctx->fpr[1]; double b = ctx->fpr[0]; ... }                      /* p1 < min? */
if ((!((ctx->cr >> 0) & 8))) { g_trampoline_fn = func_00457EA4; return; }    /* nao -> sai */
{ uint32_t tmp = vm_read32(ctx->gpr[2] + 0x108); ... ctx->fpr[13] = ftmp; }  /* periodo */
loc_00457E54:
        ctx->fpr[1] = ppu_fp_single(ppu_fadd(ctx->fpr[1], ctx->fpr[13]));
        ...
        if (((ctx->cr >> 0) & 8)) goto loc_00457E54;                          /* enquanto p1 < min */
```

Com o TOC do EBOOT (`0x00541178`, lido do OPD do `e_entry` no E80):

| expressão | EA | símbolo do E78 | valor no ELF |
|---|---|---|---|
| `r2 + 0x108` | `0x00541280` | `DAT_00541280` período | `0x40C90FDB` = 2π |
| `r2 + 0x10C` | `0x00541284` | `DAT_00541284` mínimo | `0x00000000` = 0 |

A aritmética de endereço bate com o E78 — **desde que `ctx->gpr[2]` seja
`0x00541178` quando esta função corre.** Isso nunca foi medido.

E o primeiro ciclo só é alcançado se `fpr1 < fpr0`. Com o mínimo a 0, é preciso
que **`fpr1` chegue negativo**. O E78 calculou `param_1 = 0` pela cadeia
`func_0032988C(8000)` — e com 0 o ciclo nem começa (sai pelo trampolim
`func_00457EA4`). Logo alguma das duas coisas é falsa em runtime.

## A pergunta, numa frase

**Quando `func_00457E44` trava, qual é o valor de `ctx->gpr[2]`, de `fpr1` à
entrada, e dos dois floats que o ciclo lê do TOC?**

## Predições

### P1 — o TOC está errado (`r2 != 0x00541178`)
Então `fpr0`/`fpr13` são lixo e o ciclo compara e soma valores que o jogo nunca
escreveu. A parede deixa de ser aritmética de float e passa a ser **provisão do
TOC na chamada** — classe de bug do lift, sistemática, e o alvo muda para como
`r2` é reposto nesta cadeia (trampolim / `ps3_indirect_call` / OPD).

### P2 — TOC certo e `periodo == 0` (ou negativo)
Então `fpr1 += 0` para sempre enquanto `fpr1 < min`: **não-convergência**, não
"muitas iterações". Aponta para `vm_read32` devolver 0 num EA que o ELF tem
inicializado — mapeamento/carga do segmento 1, não aritmética.

### P3 — TOC certo, período 2π, e `fpr1` finito muito negativo
A hipótese do E78 sobrevive: ~1,6·10²⁹ iterações, indistinguível de infinito. O
alvo passa a ser **quem produz `fpr1`** — a cadeia `func_0032988C` →
`func_0048B1C8` → `func_00457DBC`, e a passagem de floats em FPR no lift.

### P4 — TOC certo, período 2π, `fpr1` são (0 ou pequeno)
Então esta função **não é a parede**: entra e sai, e o bloqueio está depois dela
(no trampolim `func_00457EA4`, ou noutro sítio que o `[TICK]` ainda não numera).
O E78 teria confundido "última migalha impressa" com "onde para" — a mesma classe
de erro que o E73 cometeu com o degrau 33.

### Nenhuma das anteriores
Se a sonda imprime uma vez e não repete, o ciclo não é infinito e o bloqueio está
noutro lado — a migalha `[TICK]` é o último sítio *instrumentado*, não o último
*executado*. Nesse caso a medição seguinte é instrumentar a saída, não a entrada.

## Controle (sem ele, nada abaixo vale)

A sonda imprime o **EA calculado** e o **u32 cru** lido, nos dois sítios. Se o EA
não for `0x00541280`/`0x00541284`, o controle falhou e os floats não dizem nada
sobre `DAT_00541280`.

<!-- CUT LINE — nada acima é reescrito depois de correr -->

---

# RESULTADO — medido 2026-09-01, 3 corridas

Binário `boot_gow2_fprwrap` (lift `recomp_macos_v2` + `patch_457e44_fprwrap_probe.py`),
gate `PS3_TRACE_FPRWRAP=1`, env canónico do `env_gow2.sh`, 90 s por corrida, kill por PID.

Presença da sonda no binário confirmada antes de correr (`strings | grep
PS3_TRACE_FPRWRAP` = 1, mais as 6 format strings) — o build reusou objectos em 9 s
e sem esta verificação um zero teria sido lido como resultado.

## As linhas

```
[FPRWRAP] entra #1 r2=0x00541178 r3=0x4381A520 p1=0 p2=6.28318548
[FPRWRAP] sai-cedo #1 p1=0 it_add=0 it_sub=0
[FPRWRAP] entra #2 r2=0x00541178 r3=0x4381A520 p1=0 p2=360
[FPRWRAP] sai-cedo #2 p1=0 it_add=0 it_sub=0
[FPRWRAP] entra #3 r2=0x00541178 r3=0x4381A520 p1=0 p2=360
[FPRWRAP] pre-loop #3 ea_min=0x00541284 min=8.96831017e-44(0x00000040)
                      ea_per=0x00541280 per=-200(0xC3480000) p1=0 CTRL=OK
[FPRWRAP] loop-add #3 it=1          p1=-200
[FPRWRAP] loop-add #3 it=1000000    p1=-197368704
[FPRWRAP] loop-add #3 it=1000000000 p1=-4.2949673e+09
```

**Controle: `CTRL=OK`** — os dois EAs são `0x00541284` e `0x00541280`, exactamente os
símbolos do E78. O que se lê abaixo é sobre `DAT_00541280`/`DAT_00541284` e não sobre
outro sítio.

**Determinismo:** `run1` e `run2` idênticos ao bit nas seis primeiras linhas
(`run3` foi cortado pelo timeout antes do `pre-loop`).

## Veredito por predição

| | resultado |
|---|---|
| **P1** TOC errado | **REFUTADA** — `r2 = 0x00541178`, o TOC do OPD do `e_entry`. Certo. |
| **P2** período 0 ou negativo | **CONFIRMADA no mecanismo, corrigida na causa** |
| **P3** `fpr1` muito negativo | **REFUTADA** — `p1 = 0` à entrada, exactamente o que o E78 calculou pela cadeia. O argumento é são. |
| **P4** função sã, parede depois | **REFUTADA** — ela trava mesmo: 1e9 iterações e a subir. |

A P2 previu a **não-convergência** e acertou; previu a causa como «`vm_read32` devolver
0 num EA que o ELF tem inicializado» e **errou**. Não devolve 0: devolve **outro dado**.

## O mecanismo, completo

| EA | no ELF (lido do ficheiro, segmento de dados — não `.bss`) | em runtime |
|---|---|---|
| `0x00541280` período | `0x40C90FDB` = **2π** | `0xC3480000` = **−200,0** |
| `0x00541284` mínimo | `0x00000000` = **0** | `0x00000040` = **64** (8,97e−44 como float) |

Com esses valores: `min` passa a ser positivo, portanto `0 < min` é **verdade** e o
ciclo é alcançado; e o «período» é **negativo**, portanto `p1 += período` **afasta**
`p1` do mínimo em vez de o aproximar. Não é «1,6·10²⁹ iterações»: é
**não-convergência por construção**. `p1` satura em −4,29e9 e o ciclo nunca sai.

## O que isto muda — a parede não é o que o E78 disse

O E78 concluiu «o suspeito é o que chega, não o que está escrito» e apontou a
**passagem de floats em FPR no lift**. As duas metades caem:

1. O que chega (`p1 = 0`) está **certo**.
2. O que está escrito está **errado** — mas não no ELF, e sim na memória em runtime.

`func_00457E44` é **vítima**, não culpado. A parede real é **uma escrita por cima do
rodata do jogo**.

## O discriminador acidental que data a corrupção

As chamadas **#1 e #2 saíram cedo**. Sair cedo exige `!(0 < min)`, isto é `min <= 0` —
consistente com o `0x00000000` do ELF. A **#3** entrou, logo já lia `0x00000040`.

**A memória estava correcta e mudou durante o boot, entre a 2ª e a 3ª chamada.**
Isso exclui erro de carga/mapeamento do segmento e fixa uma janela temporal estreita
para o escritor.

## Refutado por medição in-boot — não voltar a perseguir

1. **TOC errado nesta cadeia** — `r2 = 0x00541178` em 3/3 corridas.
2. **Passagem de floats em FPR no lift como causa desta parede** — `p1 = 0` à entrada,
   são; o E78 apontava para aqui.
3. **`fpr1` finito enorme (~1,6·10²⁹ iterações)** — o valor de entrada é 0.
4. **Constantes corrompidas no ELF** — lidas do ficheiro por leitor independente de
   program headers: 2π e 0, em dados inicializados.
5. **Erro de carga do segmento 1** — as chamadas #1/#2 leram o valor correcto.

## Nível de evidência

`in-boot`, 3 corridas, controle passado, determinismo confirmado em 2/2 corridas
completas. O **mecanismo** está nomeado; o **escritor** não — isso é a medição
seguinte, e nomear o mecanismo não é o mesmo que ter o fix.
