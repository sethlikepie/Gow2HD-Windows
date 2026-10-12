# Repor peça a peça não chega — o UNSTICK sozinho desestabiliza

**Data:** 2026-07-31 · **Medido:** gate de 3 corridas, `boot_gow2_type15_unstick_base`.

## O resultado

```
run  st620  startseq  nopic  thr_end  r_perma  elo_stopped
1    11     2         4      0        1        AUTO_LOAD (thr_end)   <- o melhor de sempre
2    1      0         0      0        0        intro (st620)         <- pior que o baseline
3    1      0         0      0        0        intro (st620)         <- pior que o baseline
```

**1 de 3.** E o estado anterior (FIOS + `AREAD-HLE` + órfão) dava **6/6 consistentes** a
parar no `re-Play (NOPIC)`.

## O que o binário testado tinha

```
recomp_macos_v2.type15us_base:  UNSTICK=1  SHELL-REHOME=0  FL-FIXUP=0
```

**Só o `UNSTICK`.** Nenhum dos outros dois fixes desta cadeia. Portanto a instabilidade
não vem de interacção entre eles — **vem do `UNSTICK` isolado**.

## Porque isso não é surpreendente, revendo

O bloco `TYPE15-UNSTICK-SKIP` faz, a cada 1024 ticks:

```c
ppu_giant_lock_release();
usleep(500);
ppu_giant_lock_acquire();
```

Isso **muda o escalonamento de todas as threads guest**. Introduzido sozinho, num boot
cujas outras peças de sincronização TYPE15 ainda faltam, abre janelas de corrida que antes
não existiam — daí as corridas 2 e 3 nem passarem da intro.

E o binário de referência, que tem o `UNSTICK`, é estável em 5/5 — **porque tem também
tudo o resto**.

## A lição, e é a repetição de uma que já tínhamos escrito

A Fase 9 aprendeu: *"inventário antes de código — descobrir peças uma a uma custou duas
iterações do gate"*. Escrevi isso no contexto da Fase 10 como aviso.

E fizemos exactamente o contrário outra vez: repusemos o `UNSTICK` sozinho porque foi o
que a comparação revelou primeiro.

**O inventário diz que faltam ainda 7 marcadores `TYPE15` e 6 `FACTORY`** face ao lift que
funciona. Repor um de cada vez e medir entre cada um custa um gate (~10 min) por peça, e
mede combinações que nunca existiram em lado nenhum.

## A decisão

**Repor o conjunto completo de uma vez**, e só depois medir. O critério de comparação
passa a ser o lift de referência inteiro, não peça a peça.

Se o conjunto completo estabilizar, isola-se depois o que for dispensável — na direcção
certa, que é remover de um estado que funciona, não adicionar a um que não funciona.

## Ressalva

O `UNSTICK` **não** é um erro: é código real, apagado pelo re-lift, e a corrida 1 mostra o
que ele destrava (`nopic=4`, a parede a avançar para o `AUTO_LOAD`). Fica commitado
(`5a45b78`). O que está errado é a **ordem** em que o estamos a repor.
