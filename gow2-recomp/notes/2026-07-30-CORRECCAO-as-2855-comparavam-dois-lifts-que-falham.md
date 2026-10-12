# CORRECÇÃO: as "2855 funções perdidas" comparavam dois lifts que ambos falham

**Data:** 2026-07-30 · **Descoberto por:** a tabela do `bisect_regression.sh` (Fase 6),
que mediu os 9 binários guardados de forma sistemática — coisa que eu tinha feito à mão,
em três, e mal.

## O que a tabela mostrou

```
bin                             build_date      linhas  startseq  thr_end  r_perma  nopic  class
boot_gow2.rdy0_ref              24 Jul 21:56      4115         2        1        1      4  OK
boot_gow2.wip                   25 Jul 17:47      4145         2        1        1      4  OK
boot_gow2.pre_v3                25 Jul 18:23      4111         2        1        1      4  OK
boot_gow2.pre_v4                25 Jul 18:23       813         0        0        0      0  REGRESSAO
boot_gow2_v3fix                 26 Jul 02:23        76         0        0        0      0  STRUCTURAL_FAIL
boot_gow2_v4                    26 Jul 02:35      3211         1        0        0      0  REGRESSAO
boot_gow2.pre_20260729_150649   26 Jul 15:28      3222         1        0        0      0  REGRESSAO
boot_gow2_relift_test           26 Jul 16:31      3176         0        0        0      0  REGRESSAO
boot_gow2                       29 Jul 15:07      3227         1        0        0      0  REGRESSAO
```

**`boot_gow2.pre_v4` FALHA** — 813 linhas, `StartSeq=0`, morre muito cedo.

## O erro

Comparei o lift `recomp_macos_v2.pre_v4` com o `recomp_macos_v2` (produção) e concluí que
"se perderam 2855 funções de código real entre o lift que funciona e o que falha".

**`recomp_macos_v2.pre_v4` é o lift do `boot_gow2.pre_v4` — o binário que FALHA.**

Comparei dois lifts que ambos produzem binários partidos. A diferença de 2855 funções é
real como facto aritmético, mas **não suporta a conclusão que tirei dela**: não é a
diferença entre funcionar e não funcionar.

E o lift do binário que de facto funciona — `boot_gow2.pre_v3` — **não está guardado**.
Só existem `recomp_macos_v2`, `.pre_v4`, `.pre_20260729_150649` e `recomp_macos_v3`.

## O que fica invalidado

| artefacto | estado |
|---|---|
| `2026-07-30-a-regressao-ja-tinha-sido-medida-em-26-jul.md` — a tabela de contagens e a conclusão sobre as 2855 | **inválido como diagnóstico da regressão** |
| `2026-07-30-fn-perdidas-reais.tsv` (as 2855) | continua a ser um diff correcto entre dois lifts, mas **não é "o que se perdeu na regressão"** |
| `2026-07-30-duas-hipoteses-do-lifter-refutadas-por-ab.md` — o alvo "2855" | o alvo estava mal posto |
| `08-CONTEXT.md` — "o alvo que sobra: ~2755 funções" | **tem de ser reescrito** |
| commits `eedea6b`, `257f789`, `b2e8915`, `f577a0f` | mantêm-se no histórico; esta nota é a errata |

## O que CONTINUA válido

- **O bissect de binários**, agora muito melhor: 3 binários OK (`rdy0_ref` 24 Jul,
  `wip` 25 Jul 17:47, `pre_v3` 25 Jul 18:23) e 6 em regressão.
- **As duas refutações por A/B** (`truncated-bounds repair` adiciona 78 e não estava no
  `v3`; `invalid_instructions` é no-op sem `--config`). Não dependiam do alvo errado —
  eram sobre o mecanismo, não sobre a contagem.
- **O fix do `first-operand-wins`** (`2e21639`) — vale por si, tem teste RED→GREEN provado
  por injecção, e o A/B mede +20 dispatchers / +208 case targets. Não depende disto.
- **O commit `21eecdc` de 26 Jul** continua a dizer o que diz: a regressão foi medida na
  altura e o gate M0 deixou-a passar. Isso é história documentada, não inferência minha.
- **"Mais não é melhor"** — agora com prova ainda mais forte: `pre_v4` tem **56 072**
  funções, mais do que qualquer outro lift, e é dos piores binários da tabela (813 linhas).

## O que isto ensina, e é a parte que interessa

Fiz o bissect à mão em três binários e tirei conclusões. A ferramenta, corrida sobre os
nove, mostrou que **um dos meus dois pontos de referência estava do lado errado**.

Era exactamente para isto que a DIAG-01 existia — e ela pagou-se na primeira corrida, ao
apanhar um erro do próprio autor do requisito.

Nota lateral sobre o método: os binários `pre_v3` e `pre_v4` têm a **mesma mtime**
(25 Jul 18:23) porque foram renomeados pelo mesmo backup-por-rename. A mtime de um backup
não diz quando o artefacto foi produzido. Já tinha apanhado isto para o lift `pre_v4`
(sem carimbo `lifter-rev`) e não o generalizei para os binários.

## O alvo, reposto honestamente

A janela do binário está agora entre **`boot_gow2.pre_v3` (OK)** e **`boot_gow2.pre_v4`
(REGRESSAO)** — mas a ordem real entre eles é desconhecida, porque as mtimes são de
rename. `boot_gow2.wip` (25 Jul 17:47, OK) é o último OK com data fiável.

Para comparar lifts como deve ser, é preciso **um lift que produza um binário OK**. Como
nenhum está guardado, as opções são:

1. **Regenerar** com o lifter de um commit anterior (barato: ~16 s por variante) e medir
   até encontrar um que dê `thr_auto_load end >= 1`. É o A/B que já está montado, agora
   com o critério certo — comportamento, não contagem.
2. Aceitar que o diff de lifts não é o caminho e atacar pelo lado do binário.

A (1) é a que aproveita tudo o que já está construído, e o critério de paragem passa a ser
o gate da Fase 7, não uma contagem de funções.
