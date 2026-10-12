# O baseline não é reconstruível a partir de HEAD — e a causa é nova

**Data:** 2026-07-25 · Fecha o passo 6 da Task 1 do RDY-0.

## Experimento

Trocar os dois ficheiros modificados por outra sessão (`movie_eos_arm.c`,
`recomp_mid_v2/gow2_spu_register.c`) pelo conteúdo em `HEAD`, construir, e ver
qual dos dois estados é consistente com o lift em produção. WIP salvaguardado
byte a byte e reposto com verificação de md5 nas duas trocas.

## Resultado: só o WIP linka

| build | resultado |
|---|---|
| com o WIP da outra sessão | **linka** — `md5 d0f32f9e…` |
| com o conteúdo de `HEAD`  | **falha no link**, um único símbolo |

```
Undefined symbols for architecture arm64:
  "_movie_done_timebased_reset", referenced from:
      func_000CE03C(ppu_context*) in ppu_recomp_001.cpp.o
```

## Porque isto é um problema novo, e não uma preferência

O `movie_done_timebased_reset()` é adicionado pelo WIP não-committado. O lift em
produção **chama-o** a partir de `func_000CE03C` — 2 ocorrências. E:

```
grep -ln 'movie_done_timebased_reset' recomp_mid_v2/*.py  ->  NENHUM
```

**Nenhum `patch_*.py` instala esse call site.** É um órfão de dois lados:

1. uma função de host que só existe numa working tree não-committada
2. um call site no lift gitignored que nenhum script versionado repõe

Ou seja, é a mesma classe de problema que o RDY-0 existe para resolver (os 21
marcadores sem escritor, o `host_gow2_factory.cpp`), mas é uma instância **nova**
e não está no catálogo.

## Consequência para o RDY-0

A pergunta "esperar pela outra sessão ou reconstruir a partir de HEAD?" não tem
a segunda opção: **HEAD não constrói.** O baseline tem de ser gravado com o WIP
e isso tem de ficar declarado — está, em `lift_baseline/counters_pre.tsv`.

Para o aceite do RDY-0 ("uma cópia privada reconstrói sem edição manual") faltam
agora duas coisas concretas, ambas accionáveis por quem detém o WIP:

1. commitar o `movie_eos_arm.c` (a função)
2. criar o `patch_*.py` que instala o call site em `CE03C` (a chamada)

## Efeito colateral resolvido

O outro lado do bloqueio caiu sozinho com o merge de `origin`: os
`ps3_checkpoint_fire` e `ps3_mem_oob_report` que o binário de referência pedia
vivem em `runtime/trace/`, que veio nos 21 commits de instrumentação. Exigiu
reconstruir o `libps3recomp_runtime.a` (o de 24 jul não os tinha). O engine root
canónico fica fixado: **`ps3recomp` @ `d039469`**.

## Baseline 2

Com o root fixado e a lib nova: **`st620=11` em 6 de 6 corridas** (o baseline 1
deu 4/6, com mais 700 MB de swap). `56223` funções, `13 modules / 151 imports`,
zero órfãos.
