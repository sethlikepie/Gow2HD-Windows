---
phase: 06-diagn-stico-da-regress-o-at-ao-c-digo
plan: 02
subsystem: infra
tags: [ppu-lifter, diagnostics, gow2, TOCFIX, strip_block, cellVdec]

requires:
  - phase: 06-diagn-stico-da-regress-o-at-ao-c-digo (Plano 01)
    provides: "bisect_regression.sh (ferramenta de medição), tabela dos 9 binários guardados"
provides:
  - "Comparação comportamental completa entre 9 estados históricos do ppu_lifter.py + baseline de hoje"
  - "Reversão cirúrgica real de TOCFIX+ctx->lr, medida com binário real (REFUTADA-EM-COMBINAÇÃO)"
  - "Veredicto explícito das três hipóteses (TOCFIX, strip_block, regressão comportamental do lifter)"
  - "ERRATA aplicada antes da execução: o alvo original ('2855 funções perdidas') estava invalidado"
affects: ["08-fix"]

tech-stack:
  added: []
  patterns:
    - "A/B de commits históricos do lifter (git show COMMIT:path > variante.py) contra o ELF/functions.json de hoje, sem build de C++"
    - "Diff byte-a-byte de funções liftadas individuais entre estados do lifter, em vez de contagem agregada, para isolar vizinhanças de chamada específicas"
    - "Reversão cirúrgica com contagem de substituições e abort se o número não bater (guarda anti-silêncio)"

key-files:
  created:
    - "../gow2-recomp/notes/2026-07-30-fase6-diag-funcoes-perdidas.md"
    - "../gow2-recomp/patch_diag06_147038_revert_test.py"
    - "../gow2-recomp/notes/2026-07-30-fase6-veredictos.md"
  modified: []

key-decisions:
  - "ERRATA aplicada antes de executar: o alvo original (2855 funções perdidas) comparava dois lifts que ambos falham (recomp_macos_v2.pre_v4 gerou boot_gow2.pre_v4, REGRESSAO) -- abandonado, novo alvo é comportamento, não contagem"
  - "9 variantes históricas do ppu_lifter.py testadas (46a4c3f..4e815cf) + baseline de hoje, todas via A/B sem build de C++ (~15s cada)"
  - "Contagem de funções lifted cresce monotonicamente ao longo de toda a janela (51726->51839->51917->51991) -- refuta 'perda de descoberta por contagem'"
  - "Vizinhança de 3 níveis de thr_auto_load (12 funções) é byte-a-byte idêntica entre o binário OK mais recente e a produção actual -- refuta mudança do lifter nesta vizinhança"
  - "Reversão real de TOCFIX+ctx->lr (9 sítios, não 8 como o plano assumia -- confirmado por grep fresco) dá REGRESSAO idêntica ao binário não corrigido -- REFUTADA-EM-COMBINAÇÃO"
  - "strip_block(): delimitador END_MARKER explícito já em produção desde f9ec60f (26 Jul 01:29), antes do primeiro binário da janela -- REFUTADA"

requirements-completed: [REG-02]

coverage:
  - id: D1
    description: "Comparação comportamental entre variantes do lifter (Tarefa 1) -- refuta hipótese de contagem e de mudança na vizinhança imediata de thr_auto_load"
    requirement: "REG-02"
    verification:
      - kind: manual_procedural
        ref: "notes/2026-07-30-fase6-diag-funcoes-perdidas.md -- tabela de 10 estados + diff byte-a-byte de 12 funções"
        status: pass
    human_judgment: false
  - id: D2
    description: "Reversão cirúrgica real de TOCFIX+ctx->lr, medida com binário construído e bisect_regression.sh"
    requirement: "REG-02"
    verification:
      - kind: manual_procedural
        ref: "patch_diag06_147038_revert_test.py + boot_gow2_diag06_combo -> REGRESSAO (idêntico ao não corrigido)"
        status: pass
    human_judgment: false
  - id: D3
    description: "Veredicto explícito das três hipóteses, causa não nomeada -- diagnóstico honesto entregue"
    requirement: "REG-02"
    verification:
      - kind: manual_procedural
        ref: "notes/2026-07-30-fase6-veredictos.md"
        status: pass
    human_judgment: true
    rationale: "REG-02 exige causa nomeada por ficheiro:linha; esta tarefa refutou três hipóteses mas não a nomeou -- requer confirmação humana de que o diagnóstico honesto (com a lista do que foi eliminado) é aceite como entrega válida desta fase, per CLAUDE.md regra 4."

duration: ~2h
completed: 2026-07-30
status: complete
---

# Phase 6 Plan 02: três hipóteses testadas e refutadas por medição real — a causa não ficou nomeada

**TOCFIX/ctx->lr refutados por reversão real e rebuild (REGRESSAO idêntica), strip_block refutado por leitura de código, e a hipótese de "perda de descoberta" refutada com medição completa (contagem só cresce; vizinhança de 3 níveis de thr_auto_load é byte-a-byte idêntica entre o binário OK e a produção que falha) — REG-02 não fechado, mas com um funil de causa muito mais estreito para a Fase 8.**

## Performance

- **Duration:** ~2h (Tarefa 1 ~45min incluindo A/B de 9 variantes + diffs; Tarefa 2 ~30min incluindo rebuild real; Tarefa 3 ~20min de leitura+escrita; mais o tempo de actualizar o plano com a ERRATA antes de executar)
- **Tasks:** 3/3 completas (Tarefa 2 correu — não foi dispensada, porque a Tarefa 1 não nomeou a causa)
- **Files modified:** 3 criados

## Accomplishments

- **ERRATA aplicada ANTES da execução**: actualizei `06-02-PLAN.md` per instrução do
  coordenador, documentando que o alvo original ("2855 funções perdidas") estava
  invalidado — descoberto pela própria tabela do `bisect_regression.sh` do Plano 06-01
  (`recomp_macos_v2.pre_v4`, usado como referência do "lift que funciona", na verdade
  gerou um binário `REGRESSAO`).
- **Tarefa 1**: A/B de 9 estados históricos do `ppu_lifter.py` (`46a4c3f` 24 Jul até
  `4e815cf` 26 Jul 02:59) + baseline de hoje, todos correndo contra o
  `EBOOT.ELF`/`functions.json` actual (~15s cada, zero build). Achado principal: a
  contagem de funções lifted **cresce monotonicamente** ao longo de TODA a janela
  (51726→51839→51917→51991) — nunca cai. E a vizinhança de chamada de 3 níveis de
  `thr_auto_load` (12 funções) é **byte-a-byte idêntica** entre o estado que precede o
  binário OK mais recente e o estado de hoje que gera a produção que falha. Decisão:
  `pista nao fechada`.
- **Tarefa 2** (correu, porque a Tarefa 1 não nomeou a causa): confirmei por `grep -n`
  fresco que há **7** sítios `ctx->lr=` (não 6, como a evidência antiga do plano
  assumia) + 2 sítios TOCFIX = 9 no total dentro de `func_00147038`. Escrevi
  `patch_diag06_147038_revert_test.py` com guarda anti-produção e contagem obrigatória
  (aborta se o número de substituições não bater). Revertei numa cópia isolada
  (`recomp_macos_v2.diag06_test`), rebuild de 1 chunk (~12s neste ambiente) e medi com
  `bisect_regression.sh --bin ./boot_gow2_diag06_combo`. **Resultado: `REGRESSAO`**
  (idêntico ao binário não corrigido) — TOCFIX/`ctx->lr` REFUTADOS-EM-COMBINAÇÃO.
- **Tarefa 3**: confirmei o veredicto `strip_block` por leitura de
  `patch_zz_host_api_decls.py:105-124` (delimitador `END_MARKER` explícito, corrigido em
  `f9ec60f`, 26 Jul 01:29, antes do primeiro binário da janela) — **REFUTADA**. Escrevi
  `2026-07-30-fase6-veredictos.md` consolidando as três hipóteses com rótulo exacto e uma
  secção "Próxima medição" honesta para a Fase 8.

## Task Commits

Todos em `../gow2-recomp` (repo irmão):

1. **Tarefa 1: comparação comportamental entre variantes** - `a73dc1b` (docs)
2. **Tarefa 2: reversão TOCFIX+ctx->lr, refutada** - `30e6fbe` (feat)
3. **Tarefa 3: veredicto final das três hipóteses** - `73658f8` (docs)

**Correção de plano antes da execução (não é um deviation-rule, é a instrução explícita
do coordenador):** `4d1f1f9` (ps3recomp, docs) — actualiza `06-02-PLAN.md` com a ERRATA
e o novo alvo comportamental.

## Files Created/Modified
- `../gow2-recomp/notes/2026-07-30-fase6-diag-funcoes-perdidas.md` - comparação comportamental entre 9 variantes do lifter + baseline de hoje
- `../gow2-recomp/patch_diag06_147038_revert_test.py` - reversão cirúrgica idempotente de TOCFIX+ctx->lr (diagnóstico, não fix)
- `../gow2-recomp/notes/2026-07-30-fase6-veredictos.md` - veredicto final das três hipóteses

## Decisions Made
- Adoptar o alvo comportamental do coordenador (A/B de commits do lifter em vez de
  cruzar listas de funções perdidas) desde o início da execução, actualizando o
  `06-02-PLAN.md` antes de tocar em código, para deixar rastro explícito da mudança de
  alvo.
- Extrair `elf_parser.py`/`elf_symbols.py` para cada variante testada (dependência
  lazy-import não incluída na cópia inicial do `ppu_lifter.py` histórico) — sem isto os
  9 lifts históricos falhavam com `UnboundLocalError`.
- Usar diff byte-a-byte de funções individuais (não apenas contagens agregadas) para a
  vizinhança de `thr_auto_load` — deu um resultado muito mais forte e directamente
  interpretável do que cruzar uma lista de "perdidas".
- Corrigir o número esperado de substituições no patch de reversão de 8 (assumido pelo
  plano) para 9 (medido fresco por `grep -n`), documentando o desvio no próprio script
  em vez de silenciar.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Variantes históricas do `ppu_lifter.py` falhavam sem `elf_parser.py`**
- **Found during:** Tarefa 1 (execução das 9 variantes A/B)
- **Issue:** As cópias do `tools/ppu_lifter.py` de commits antigos importam
  `elf_parser` (via `from elf_parser import ELFFile, PT_LOAD`, lazy) mas o directório de
  trabalho de cada variante só tinha `ppu_lifter.py`/`ppu_disasm.py` copiados — todas as
  9 variantes falhavam com `UnboundLocalError: cannot access local variable 'elf'`.
- **Fix:** copiei `tools/elf_parser.py`/`tools/elf_symbols.py` (versão actual, uma
  dependência de parsing não relacionada com a lógica de descoberta sob teste) para cada
  directório de variante antes de re-correr.
- **Files modified:** nenhum ficheiro do repositório — só directórios de trabalho em
  `/tmp/toolsab_diag06_run_<commit>/`.
- **Verification:** as 9 variantes correram com `rc=0` depois do fix.
- **Committed in:** N/A (artefactos de `/tmp`, não versionados; o resultado da medição
  está na nota commitada em `a73dc1b`).

**2. [Rule 1 - Bug] Número esperado de substituições no patch de reversão (8 -> 9)**
- **Found during:** Tarefa 2, confirmação por `grep -n` antes de escrever o patch
- **Issue:** o plano assumia 6 sítios `ctx->lr=` + 2 TOCFIX = 8, com base na evidência da
  sessão de planeamento. A confirmação fresca deu 7 sítios `ctx->lr=` + 2 TOCFIX = 9 — o
  lift tinha sido regenerado entre o planeamento e a execução (exactamente o cenário que
  o plano previu e mandou confirmar antes de escrever).
- **Fix:** o script usa o número FRESCO (9) como esperado, com guarda que aborta se
  divergir, em vez de aceitar silenciosamente o número antigo do plano.
- **Files modified:** `../gow2-recomp/patch_diag06_147038_revert_test.py`
- **Verification:** corrida real deu "ctx->lr sites revertidos: 7, TOCFIX sites
  revertidos: 2, total: 9" e `rc=0`; `diff` contra o ficheiro original confirmou
  exactamente 9 linhas alteradas, todas dentro do corpo de `func_00147038`.
- **Committed in:** `30e6fbe`

---

**Total deviations:** 2 auto-fixed (1 blocking - dependência em falta; 1 bug - número
esperado desactualizado). **Impact on plan:** nenhum impacto negativo — ambos eram
exactamente os cenários "confirmar antes de escrever, o lift pode ter mudado" que o
plano já antecipava.

## Issues Encountered
Nenhum bloqueio que impedisse a conclusão da fase. O resultado mais importante — a causa
NÃO ficou nomeada — é documentado explicitamente como tal, não maquiado. Ver "Known
Stubs" abaixo para o que fica em aberto.

## Known Stubs / Trabalho Não Fechado

Esta fase **não** produz uma correcção — é diagnóstico (per o objectivo da Fase 6). O
`REG-02` (causa nomeada por ficheiro:linha) **não** foi cumprido integralmente: as três
hipóteses previstas foram medidas e refutadas, mas nenhuma nomeia a causa positiva. A
nota `2026-07-30-fase6-veredictos.md` regista explicitamente a próxima medição
recomendada para a Fase 8 (instrumentar o binário que funciona para parar no mesmo
alcance de boot do que falha, e olhar para fora da vizinhança de 3 níveis de
`thr_auto_load` testada aqui, incluindo o runtime/`ppu_loader.cpp`/HLE). Isto não é um
stub de código — é o estado honesto do diagnóstico desta fase.

## User Setup Required
None - nenhuma configuração de serviço externo necessária.

## Next Phase Readiness

A Fase 8 (correcção) **não tem ainda** um ficheiro:linha para corrigir — três candidatos
promissores da sessão de planeamento foram eliminados por medição real nesta fase. O
funil ficou mais estreito: não é TOCFIX/`ctx->lr` isolados nem em combinação, não é
`strip_block`, e não é uma mudança na vizinhança imediata de 3 níveis de `thr_auto_load`
nem na contagem total de funções emitidas. A pista mais forte e barata para a próxima
sessão é o desenho de teste normalizado do sinal de log dos dois fallbacks do runtime
(comparar `ppu_unlifted_stub`/`unresolved indirect call` no MESMO alcance de boot entre
um binário OK e um que falha) — registado explicitamente na nota de veredictos, não
construído aqui.

---
*Phase: 06-diagn-stico-da-regress-o-at-ao-c-digo*
*Completed: 2026-07-30*

## Self-Check: PASSED
