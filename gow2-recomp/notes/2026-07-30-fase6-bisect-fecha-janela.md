# Fase 6, Plano 06-01: o bissect dos 9 binários guardados, medido pela ferramenta

**Data:** 2026-07-30 · **Método:** `./bisect_regression.sh` (sem argumentos, modo lista),
uma corrida por binário, recipe menu-fast Metal, `TIMEOUT=90`, matado sempre por PID
(`TERM` → espera 1s → `-9`, nunca `pkill -f`). TSV bruto em
`/tmp/bisect_regression_full_20260730_122956.tsv`.

Esta nota é a **MEDIÇÃO** desta ferramenta — não repete os achados das notas de 22-25 Jul
como se fossem novos; corrobora-os com uma corrida sistemática sobre os 9 binários de uma
vez, coisa que antes só tinha sido feita à mão, em três.

## A tabela completa

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

(TSV completo, colunas idênticas, também commitado em
`gow2-recomp/notes/2026-07-30-bisect-9-binarios.tsv`.)

## O veredicto de fecho de janela

`boot_gow2.pre_v3` (25 Jul 18:23, **OK** — `startseq=2`, `thr_auto_load_end=1`,
`r_perma_full=1`, `replay_nopic=4`) → `boot_gow2_v3fix` (26 Jul 02:23, **STRUCTURAL_FAIL**,
76 linhas — morre cedo demais, não delimita nada, per CLAUDE.md a família ~55-101 linhas é
falha estrutural, nunca uma paragem lógica) → `boot_gow2_v4` (26 Jul 02:35, **REGRESSAO** —
`startseq=1`, `thr_auto_load_end=0`).

É o par (`boot_gow2.pre_v3`, `boot_gow2_v4`) que fecha a janela do lado de baixo, não o
`v3fix` — exactamente como o plano previa.

## Achado adicional desta corrida sistemática: dois binários OK antes do `pre_v3`

A tabela dá mais contexto do que os três binários testados à mão nas notas de 22-25 Jul:
`boot_gow2.rdy0_ref` (24 Jul 21:56) e `boot_gow2.wip` (25 Jul 17:47) são **ambos OK**, com
os mesmos quatro números do `pre_v3` (`startseq=2`, `thr_end=1`, `r_perma=1`, `nopic=4`).
Isto alarga a fronteira "que funciona" para trás no tempo — três binários OK consecutivos,
não um só — o que é relevante para a Tarefa 1 do Plano 06-02 (ver correcção abaixo).

## Correcção importante, descoberta por esta mesma tabela

O `boot_gow2.pre_v4` (mesma mtime do `pre_v3`, 25 Jul 18:23 — os dois foram renomeados
pelo mesmo backup-por-rename) **FALHA**: 813 linhas, `startseq=0`, `thr_auto_load_end=0`,
classe `REGRESSAO`. Isto invalidou uma conclusão anterior desta sessão de planeamento — ver
`gow2-recomp/notes/2026-07-30-CORRECCAO-as-2855-comparavam-dois-lifts-que-falham.md` — que
tinha usado o lift `recomp_macos_v2.pre_v4` (o lift que gerou este mesmo `pre_v4`) como
referência do "lift que funciona". Não era. A ferramenta desta tarefa apanhou o erro na
primeira corrida sistemática sobre os 9 binários.

## Não ficam órfãos processos depois da corrida

`pgrep -x` por cada um dos 9 nomes de binário, no fim da corrida completa: 0 em todos.
(Nota operacional corrigida durante esta tarefa: `pgrep -f boot_gow2` dá falsos positivos —
casa watchers de shell doutras sessões cujo comando de espera contém a substring
"boot_gow2"; `pgrep -x` compara o nome do processo, não a linha de comando inteira, e não
tem esse problema. Medido 5 falsos positivos com `-f` contra 0 reais com `-x`, na mesma
corrida. `bisect_regression.sh` foi corrigido para usar `-x` — commit `a51ed45`.)

## Entrega

- **DIAG-01**: um único comando (`./bisect_regression.sh`, sem argumentos) imprime e grava
  a tabela de `StartSeq`/`thr_auto_load end`/`R_PermA`/`REPLAY-NOPIC`/linhas por binário,
  reutilizável na próxima regressão sem rebuilds.
- **REG-01**: a janela fecha-se em `boot_gow2.pre_v3` (`thr_end=1`) → `boot_gow2_v4`
  (`thr_end=0`), medido pela própria ferramenta, não pelas notas de 22-25 Jul.
