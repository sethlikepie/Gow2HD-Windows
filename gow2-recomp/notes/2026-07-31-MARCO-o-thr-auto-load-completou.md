# MARCO: o `thr_auto_load` completou — primeira vez

**Data:** 2026-07-31 · **Medido:** `/tmp/gate6_jtfix/run_2.log`, gate de 6 corridas contra
`boot_gow2.jtfix`.

## O facto

```
thr_auto_load() end        <- aos 62 s
```

**Primeira vez em toda a investigação.** O `thr_end` era 0 em todas as medições de todas
as fases, desde o início.

A corrida completa:

```
linhas=8307  startseq=2  nopic=4  r_perma=212  thr_end=1  SetFlip=6137
```

## O que a destravou

O lift regenerado com o fix `2e21639` (`first TOC-loaded operand wins` no
`discover_jump_tables`), que emite **+74 funções** — entre elas a `func_000B9354`, alvo da
chamada indirecta que dava watchdog.

```
recomp_macos_v2       (producao, gerado ANTES do fix):  func_000B9354 AUSENTE
recomp_macos_v2.jtfix (regenerado com o fix):           func_000B9354 presente
                                                        51 991 funcoes (era 51 917)
```

O fix foi a **primeira coisa feita nesta sessão**, vinda do PR #82 do upstream, provada
offline com teste RED→GREEN e prova por injecção. Ficou dez horas sem efeito por uma razão
banal: corrigiu-se o **lifter**, e todas as medições seguintes correram contra um **lift
antigo**.

## Mas o gate não fecha — 1 de 6

```
run 1  timeout na intro (st620=1)
run 2  thr_end=1                                    <- o marco
run 3  FATAL: stuck calling 0x40678C90 (2000 times)
run 4  FATAL: mesma parede
run 5  FATAL: mesma parede
run 6  FATAL: mesma parede
```

Critério (≥4/6) **não cumprido**. O caminho está completo e é percorrível — mas só uma vez
em seis.

## A parede que resta

`0x40678C90` **não é um EA de código** (a janela do binário é `0x00010000–0x0050C7E0`). É
um endereço de heap, vizinho de outros da mesma sequência (`0x40673474`, `0x4067A908`), e
aparece logo a seguir a:

```
[POSTINTRO] B71 skip icallB (null product)
[FACTORY] REPAIR obj=0x400D6808
```

**Hipótese (não confirmada):** o `ctx->ctr` está a ser carregado com um endereço de
objecto do heap em vez de um ponteiro de função, na cadeia B71/FACTORY. Pode ser outra
instância do mesmo padrão sistemático que o `2e21639` corrigiu — o que bate com o item
pendente do CLAUDE.md sobre auditar despacho indirecto no resto do lift.

## E o `cellPadGetData` continua em 0

Mesmo na corrida que completa o `thr_auto_load`, o comando nunca é lido e o `st620` fica
em `0 -> 0`. **O menu não foi alcançado.** O `SetFlip=6137` é da intro.

Ou seja: completar o `thr_auto_load` era **necessário mas não suficiente** para o menu.

## Achado em aberto, não investigado

O `apply_all_patches.sh` termina com `rc=1` por `MANIFEST FAIL`:

```
A MENOS PREAMBLE g_trampoline_fn (esperado >=168753, encontrado 168436)   -317
```

**INFERIDO, não confirmado:** pode ser efeito esperado do próprio fix do jump table —
menos fallback em `g_trampoline_fn` porque mais dispatchers resolvem por `switch` nativo.
Fica registado como achado, sem ser forçado.

E dois patches (`patch_1856a8_stream_opd.py`, `patch_icg_ctor_opd.py`) dão falso-negativo
porque usam uma janela de 40 000 caracteres para procurar a declaração `ps3_call_opd`, que
com o preâmbulo maior deste lift caiu no byte 43 497. **A verificação real (`--check`)
passa nos 5 marcadores críticos** — é a heurística do script que é frágil, não o lift.
