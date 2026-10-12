# Fase 6, Plano 06-02, Tarefa 3: veredicto final das três hipóteses

**Data:** 2026-07-30 · Documento de veredicto de REG-02 (Marco v1.1, Fase 6). Consolida
o resultado medido das três hipóteses registadas: TOCFIX, `strip_block`, e a regressão
comportamental do lifter (o novo alvo desta fase, pós-ERRATA).

## Resumo executivo

**A causa NÃO ficou nomeada por ficheiro:linha nesta fase.** As três hipóteses foram
todas medidas e todas **REFUTADAS** (ou, no caso da terceira, medida-mas-não-confirmada
com uma refutação parcial forte). Isto é um resultado honesto, não um fracasso do
processo — eliminámos, com medição real (não inferência), três candidatos que pareciam
promissores no início da sessão de planeamento. A pista mais forte que resta para a
Fase 8 está na secção "Próxima medição" no fim deste documento.

## Hipótese 1 — TOCFIX / `ctx->lr`: **REFUTADA-EM-COMBINAÇÃO**

**O que era:** o lifter troca o restauro dinâmico do TOC
(`ctx->gpr[2] = vm_read64(ctx->gpr[1] + 0x28)`) por um valor estático
(`ctx->gpr[2] = 0x00541178ULL; /*TOCFIX*/`), e adiciona `ctx->lr = 0x{ENDERECO}ULL;`
explícito antes de cada `bl` dentro de `func_00147038` (thr_auto_load) — ambos ausentes
no lift antigo (documentados em `2026-07-26-tocfix-matou-36-conversoes-opd.md`).

**Medido nesta tarefa (Plano 06-02, Tarefa 2):**
- Confirmação fresca por `grep -n`: 7 sítios `ctx->lr = 0x...;` (não 6, como a evidência
  antiga do plano assumia — o lift foi regenerado desde então) + 2 sítios TOCFIX = 9
  substituições no total, dentro do corpo de `func_00147038`.
- `patch_diag06_147038_revert_test.py` reverteu os 9 sítios numa cópia isolada
  (`recomp_macos_v2.diag06_test`, nunca a produção), contando as substituições e
  abortando se o número não batesse (guarda anti-silêncio, per a lição de
  `2026-07-26-tocfix-matou-36-conversoes-opd.md`).
- Rebuild de 1 chunk (`ppu_recomp_000.cpp`, ~12s neste ambiente) + medição real com a
  ferramenta do Plano 06-01: `bisect_regression.sh --bin ./boot_gow2_diag06_combo`.
- **Resultado: `REGRESSAO`** (`startseq=1`, `thr_auto_load_end=0`, 3242 linhas) —
  **idêntico** ao binário não corrigido (`boot_gow2_v4`/produção).

**Veredicto: REFUTADA-EM-COMBINAÇÃO.** Reverter os dois mecanismos simultaneamente, na
única função conhecida por os conter (`func_00147038`), não mudou o resultado. Não é
necessário isolar TOCFIX de `ctx->lr` separadamente (o plano previa um segundo rebuild só
se o combinado desse `OK` — não deu, então esse passo é dispensado). Corrobora também a
medição estática independente da Tarefa 1: o corpo de `func_00147038` (incluindo estes 9
sítios) é byte-a-byte idêntico entre o estado do lifter que gerou o binário OK mais
recente (`145fe58`) e o estado de hoje que gera a produção que falha — TOCFIX/`ctx->lr`
já existiam, inalterados, quando o boot ainda funcionava.

## Hipótese 2 — `strip_block` comeu o lift: **REFUTADA**

**O que era:** duas versões anteriores de `strip_block()` (em
`recomp_mid_v2/patch_zz_host_api_decls.py`) tinham bugs de delimitação que comiam código
alheio — um regex `re.S` guloso que truncava chunks inteiros a 2 linhas, e uma heurística
"pára na primeira linha que não é `#include`/`extern`" que comia os `#include <stdio.h>`
do próprio lifter (documentado em `2026-07-26-strip-block-comeu-o-lift.md`).

**Medido nesta tarefa:** lido `recomp_mid_v2/patch_zz_host_api_decls.py:105-124`
(`strip_block()`) no estado ACTUAL do repositório. A implementação usa o delimitador
EXPLÍCITO `MARKER`...`END_MARKER` — `re.sub(rf"/\* {{MARKER}}:[^\n]*\n(?:(?!{{END_MARKER}})[^\n]*\n)*{{END_MARKER}}\n", "", text)` — **não** o regex `re.S` guloso do bug 1, **não** a
heurística do bug 2. O próprio docstring da função documenta os dois bugs antigos e
declara "Delimitador explícito, nunca heurística."

O commit que introduziu esta versão final é `f9ec60f` (26 Jul 01:29, "rdy0:
patch_zz_host_api_decls — fecha o orfão das declarações de API host") — **antes** do
commit `21eecdc` (26 Jul 01:46) que gerou o primeiro binário do lift regenerado
(`boot_gow2_v4`, `REGRESSAO`).

**Veredicto: REFUTADA.** O bug existiu durante o desenvolvimento (documentado em
`2026-07-26-strip-block-comeu-o-lift.md`) mas foi corrigido ANTES de qualquer binário
desta janela ter sido gerado. O código actualmente em produção nunca teve o defeito.

## Hipótese 3 — regressão comportamental do lifter (novo alvo, pós-ERRATA): **MEDIDA — REFUTADA para a janela e vizinhança testadas, pista NÃO fechada**

**O que era originalmente:** "2855 funções perdidas reais entre o lift que funciona e o
lift que falha" — **este alvo está INVALIDADO**, ver ERRATA
(`notes/2026-07-30-CORRECCAO-as-2855-comparavam-dois-lifts-que-falham.md`, commit
`61d57dd`): o lift usado como referência do "que funciona"
(`recomp_macos_v2.pre_v4`) gerou, na verdade, um binário `REGRESSAO`
(`boot_gow2.pre_v4`, medido pelo `bisect_regression.sh` do Plano 06-01). As 2855 comparam
dois lifts que ambos falham — não são o que se perdeu entre funcionar e não funcionar.

**O que foi medido no lugar (Tarefa 1, `notes/2026-07-30-fase6-diag-funcoes-perdidas.md`):**
- Comparação comportamental entre 9 estados históricos do `ppu_lifter.py`
  (`46a4c3f` 24 Jul → `4e815cf` 26 Jul 02:59) + o baseline de hoje (`0585636`/`2e21639`,
  30 Jul), todos correndo contra o `EBOOT.ELF`/`functions.json` actual.
- A contagem de funções lifted **cresce monotonicamente** ao longo de toda a janela:
  51726 (`46a4c3f`, precursor de `rdy0_ref` OK) → 51839 (`145fe58`, precursor de `wip`
  OK) → 51917 (`4e815cf`, precursor de `v4` REGRESSAO) → 51991 (hoje, produção
  REGRESSAO). **Nunca cai.**
- A vizinhança de chamada de 3 níveis de `thr_auto_load` (`func_00147038` + 6 callees
  directos + 5 netos, 12 funções ao todo) é **byte-a-byte idêntica** em código C liftado
  entre `145fe58` (precursor do binário OK mais recente) e o estado de hoje (produção
  que falha).

**Veredicto: MEDIDA-MAS-NÃO-CONFIRMADA para o mecanismo de contagem, e REFUTADA para a
vizinhança de 3 níveis testada.** A perda de descoberta de funções, medida por contagem
total do lifter, não explica a regressão nesta janela — pelo contrário, a contagem só
cresce. E nenhuma mudança do lifter na vizinhança imediata de `thr_auto_load` explica o
problema, porque essa vizinhança nunca mudou. Isto não fecha a pista de "regressão no
lifter" em geral — só a refuta nos dois ângulos mais baratos e mais óbvios (contagem
total, vizinhança imediata da função onde a cadeia pára). Não convergiu numa causa
nomeada; per o desenho desta tarefa, prosseguiu para a Tarefa 2 (TOCFIX/`ctx->lr`), que
também refutou.

## Tabela de resultados medidos

| Hipótese | Método | Resultado medido | Veredicto |
|---|---|---|---|
| TOCFIX/`ctx->lr` combinados | reversão cirúrgica em `func_00147038`, rebuild 1 chunk, `bisect_regression.sh --bin` | `boot_gow2_diag06_combo`: `REGRESSAO` (startseq=1, thr_end=0) — idêntico ao não corrigido | REFUTADA-EM-COMBINAÇÃO |
| `strip_block` | leitura de `patch_zz_host_api_decls.py:105-124`, confirmação de delimitador `END_MARKER` | implementação actual usa delimitador explícito, sem os 2 bugs antigos; corrigido em `f9ec60f` (26 Jul 01:29), antes do primeiro binário da janela | REFUTADA |
| Regressão comportamental do lifter (contagem) | A/B de 9 commits históricos + hoje, contra `EBOOT.ELF`/`functions.json` actual | contagem de funções cresce monotonicamente 51726→51839→51917→51991, nunca cai | MEDIDA-MAS-NÃO-CONFIRMADA (refutada como mecanismo de contagem) |
| Regressão comportamental do lifter (vizinhança de `thr_auto_load`) | diff byte-a-byte de 12 funções (3 níveis de chamada) entre 3 estados do lifter | as 12 funções são idênticas entre o estado precursor do binário OK mais recente e o estado de hoje (produção que falha) | REFUTADA (para esta vizinhança) |

## O que isto significa para a Fase 8

**A causa NÃO está nomeada por ficheiro:linha.** O que ficou eliminado, com medição real:

1. TOCFIX estático em vez de restauro dinâmico do TOC, e `ctx->lr` explícito antes de
   cada `bl` — ambos dentro de `func_00147038` — não são, isolados ou em combinação, a
   causa (binário revertido continua a falhar exactamente da mesma forma).
2. `strip_block()` nunca teve o bug em produção nesta janela.
3. A contagem total de funções emitidas pelo lifter não caiu em nenhum ponto desta
   janela — cresceu sempre.
4. O código C liftado da vizinhança imediata (3 níveis) de `thr_auto_load` nunca mudou
   entre o binário OK mais recente e a produção actual que falha.

**Próxima medição (para a Fase 8, não construída aqui):** já que a vizinhança imediata
do lifter está descartada e a reversão directa em `func_00147038` não muda nada, a causa
tem de estar OU (a) fora da vizinhança de 3 níveis testada (uma função mais distante na
cadeia de chamada, ou uma mudança global no preâmbulo/TOC/patches aplicados pelo
`build_macos.sh`/`patch_*.py` que afecta o comportamento em runtime sem mudar o texto
liftado da vizinhança imediata), OU (b) no runtime/HLE (`ppu_loader.cpp`, `cellVdec`,
`movie_eos_arm.c`) e não no `ppu_lifter.py` de todo. O sinal de log dos dois fallbacks do
runtime (`ppu_unlifted_stub`, `unresolved indirect call`) ficou registado como
INCONCLUSIVO quando normalizado ao mesmo alcance de boot (ver
`notes/2026-07-30-o-boot-nao-chega-ao-auto-load.md` e a secção C do `06-02-PLAN.md`) — um
teste decisivo instrumentaria o binário que funciona para parar exactamente onde o que
falha pára, comparando os dois fallbacks no MESMO alcance. Isso não foi construído nesta
fase (fora do âmbito de diagnóstico desta tarefa) e fica como o desenho recomendado para
a Fase 8.
