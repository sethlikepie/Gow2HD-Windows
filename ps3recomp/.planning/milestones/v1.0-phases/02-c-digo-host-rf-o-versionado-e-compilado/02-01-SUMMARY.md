---
phase: 02-c-digo-host-rf-o-versionado-e-compilado
plan: 01
subsystem: infra
tags: [gen_manifest, MANIFEST.tsv, preamble_pure, mutation-testing, verify_lift_baseline]

requires:
  - phase: 01-emiss-es-do-lifter-e-teste-ouro-do-pre-mbulo
    provides: "ppu_timebase_now como contrato correto (commit e441f37), superseding ps3_timebase_now"
provides:
  - "gen_manifest.py tabula kind=PREAMBLE (g_trampoline_fn, ps3_indirect_call), cobrindo o preambulo puro no gate"
  - "MANIFEST.tsv com 246 marcadores (244+2) e cabecalho reconciliando 1603/1662/1793"
  - "test_gen_manifest.py: teste-ouro por mutacao para os 2 marcadores PREAMBLE, dedicado (games/gow2/ nao tem CI propria)"
affects: ["02-02-PLAN.md", "02-03-PLAN.md", "Fase 3 (re-lift/re-baseline completo)"]

tech-stack:
  added: []
  patterns:
    - "PREAMBLE conta contra o `text` do ficheiro INTEIRO (nao contra `blob`/`injected`) -- confirma sobrevivencia, nao deteta injeccao"
    - "Exclusao deliberada codificada como set-subtraction (`lifter_syms - {simbolo}`), provada por mutacao correndo a geracao contra um alvo sintetico QUE CONTEM o simbolo excluido -- nao um acaso do alvo real nunca o ter"
    - "Edicao cirurgica de dado forense (append de N linhas + cabecalho), nunca overwrite/regeneracao completa, quando o lift alvo divergiu de marcadores nao relacionados com a task actual"

key-files:
  created:
    - games/gow2/lift_baseline/test_gen_manifest.py
  modified:
    - games/gow2/lift_baseline/gen_manifest.py
    - games/gow2/lift_baseline/MANIFEST.tsv

key-decisions:
  - "ps3_timebase_now EXCLUIDO do kind=PREAMBLE -- superseded por ppu_timebase_now (Fase 1, commit e441f37); so' sobrevive numa probe do chunk 000 de uma safra mista; gate-lo produziria alarme falso num re-lift limpo"
  - "Os 2 marcadores PREAMBLE (168753/15795) foram medidos contra o recomp_macos_v2 REAL (producao pos-promocao 57b418f, 7 chunks) -- e' o lift que o gate deve proteger daqui para a frente, e coincide com os valores ja citados no proprio texto do plano"
  - "O --verify de aceitacao desta task correu contra recomp_macos_v2.pre_v4 (o lift preservado de 20 jul que efectivamente gerou o baseline de 244 marcadores -- confirmado empiricamente: 244/244 rc=0 ANTES desta mudanca) para isolar a prova de D-2.1 da deriva pre-existente e nao-relacionada do lift novo (33/244 marcadores, Fase 3)"
  - "MANIFEST.tsv editado cirurgicamente (append de 2 linhas + bloco de cabecalho) -- git diff confirma zero linha das 244 pre-existentes alterada; nenhuma regeneracao completa"

requirements-completed: [REQ-RDY0-2]

coverage:
  - id: D1
    description: "gen_manifest.py tabula preamble_syms = lifter_syms - {ps3_timebase_now} e conta g_trampoline_fn/ps3_indirect_call contra o text INTEIRO de cada chunk, sob kind=PREAMBLE; verify() ja generica, zero alteracao"
    requirement: "REQ-RDY0-2"
    verification:
      - kind: unit
        ref: "games/gow2/lift_baseline/gen_manifest.py <LIFT> 2>/dev/null | grep PREAMBLE -- 2 linhas exatas, sem ps3_timebase_now"
        status: pass
      - kind: unit
        ref: "games/gow2/lift_baseline/test_gen_manifest.py::check_generation_excludes_ps3_timebase_now"
        status: pass
    human_judgment: false
  - id: D2
    description: "MANIFEST.tsv com os 2 marcadores PREAMBLE novos (246 total) e cabecalho reconciliando 1603 (desactualizada) / 1662 (gate anterior) / 1793 (real)"
    requirement: "REQ-RDY0-2"
    verification:
      - kind: unit
        ref: "gen_manifest.py ../gow2-recomp/recomp_macos_v2.pre_v4 --verify MANIFEST.tsv -> baseline: 246/246, rc=0"
        status: pass
      - kind: unit
        ref: "games/gow2/lift_baseline/test_gen_manifest.py::check_manifest_header_reconciles_three_figures"
        status: pass
    human_judgment: false
  - id: D3
    description: "Prova por mutacao (D-07-style): remover g_trampoline_fn ou ps3_indirect_call de uma copia sintetica faz verify() reportar rc=1 e a mensagem exata AUSENTE PREAMBLE <simbolo>"
    requirement: "REQ-RDY0-2"
    verification:
      - kind: unit
        ref: "games/gow2/lift_baseline/test_gen_manifest.py::check_verify_mutation_missing_trampoline_fn + check_verify_mutation_missing_indirect_call"
        status: pass
    human_judgment: false

duration: ~50min
completed: 2026-07-26
status: complete
---

# Phase 2 Plan 1: Marcadores kind=PREAMBLE ponta-a-ponta (gerador -> MANIFEST.tsv -> gate) — Summary

**O gate `verify_lift_baseline.sh` passa a cobrir as 131 linhas do preambulo puro via 2 marcadores `kind=PREAMBLE` (`g_trampoline_fn`, `ps3_indirect_call`), provados por mutacao, com `ps3_timebase_now` deliberadamente excluido e as tres cifras do criterio 1 (1603/1662/1793) reconciliadas no proprio `MANIFEST.tsv`.**

## Performance

- **Duration:** ~50min
- **Completed:** 2026-07-26
- **Tasks:** 2/2 completas
- **Files modified:** 3 (`gen_manifest.py`, `MANIFEST.tsv`, `test_gen_manifest.py` novo)

## Accomplishments

- `gen_manifest.py::main()` ganha `preamble_syms = lifter_syms - {"ps3_timebase_now"}` e um bloco novo dentro do loop `for path in chunks:` que conta `g_trampoline_fn`/`ps3_indirect_call` contra o `text` INTEIRO de cada chunk (nao contra `blob`/`injected` como o bloco SYM/GLOBAL) sob `kind=PREAMBLE`. `verify()` **nao foi tocada** — ja e generica sobre a coluna `tipo`.
- `MANIFEST.tsv` ganha 2 linhas novas (`g_trampoline_fn PREAMBLE 168753 ...`, `ps3_indirect_call PREAMBLE 15795 ...`), inseridas cirurgicamente entre `k_ty15_pin` (ultima GLOBAL) e `f2b_fo_mfd_get` (primeira SYM) — `git diff` confirma so' insercoes, zero linha das 244 pre-existentes alterada.
- Cabecalho de `MANIFEST.tsv` reconcilia as tres cifras do criterio 1 (1603 desactualizada / 1662 gate anterior / 1793 real), documenta a exclusao de `ps3_timebase_now` com a razao medida, e regista a divergencia conhecida e fora de escopo do `recomp_macos_v2` real face aos 244 marcadores antigos.
- `test_gen_manifest.py` novo (dedicado, `games/gow2/` sem CI propria): 5 verificacoes, 2 delas mutacao capturando a MENSAGEM exata (`AUSENTE  PREAMBLE <simbolo>`), nao so' o codigo de saida.

## Decisao critica tomada nesta execucao (contra qual lift medir)

O prompt avisou que o lift de producao mudou entre o planeamento e a execucao (`recomp_macos_v2` foi substituido pelo lift regenerado, `.pre_v4` preserva o antigo) e que eu tinha de decidir consciente contra qual medir. Medi os dois antes de decidir:

| Lift | `--verify` (antes de qualquer mudanca) | Chunks |
|------|------|--------|
| `recomp_macos_v2` (novo, producao pos-57b418f) | `211/244`, rc=1 (9 ausentes, 24 a menos — deriva pre-existente, **nao causada por este plano**) | 7 |
| `recomp_macos_v2.pre_v4` (antigo, 20 jul) | `244/244`, rc=0 | 31 |

Isto confirmou empiricamente que `MANIFEST.tsv` (244 marcadores) foi gerado contra o **`.pre_v4`**, nao contra o `recomp_macos_v2` actual. Decisao aplicada:

- **Contagens dos 2 marcadores PREAMBLE novos** (168753/15795): medidas contra o `recomp_macos_v2` **REAL/novo** (producao actual) — coincide exatamente com os valores ja citados no texto do plano, e e' o lift que o gate deve proteger daqui para a frente.
- **`--verify` de aceitacao desta task**: corrido contra `.pre_v4` — isola a prova de D-2.1 (cobertura do preambulo) da deriva pre-existente e nao-relacionada dos 33 marcadores antigos, que pertence a Fase 3. Resultado: **246/246, rc=0**.
- **Tambem medido, para transparencia** (nao forjar): `--verify` contra o `recomp_macos_v2` novo da **`213/246`, rc=1** — os 33 marcadores antigos continuam a falhar (mesma deriva do "antes"), mas **nenhum dos 2 marcadores PREAMBLE novos falha em nenhum dos dois lifts**.

## Task Commits

1. **Task 1: Marcadores kind=PREAMBLE ponta-a-ponta** - `dd7170a` (feat)
   - RED confirmado: `grep -c PREAMBLE` em `MANIFEST.tsv` e `gen_manifest.py` imprimiam `0` nos dois antes da mudanca.
   - GREEN: geracao contra `recomp_macos_v2` real produz exatamente as 2 linhas PREAMBLE esperadas, sem `ps3_timebase_now`.
2. **Task 2: Teste-ouro com mutacao e reconciliacao das tres cifras** - `c7d893e` (test)
   - 5 verificacoes, `[PASS]` x5, mutacao com captura de mensagem para os 2 marcadores.

**Plan metadata:** este commit (docs, `.planning/`).

## Files Created/Modified

- `games/gow2/lift_baseline/gen_manifest.py` — `preamble_syms` (D-2.1) + bloco de tabulacao PREAMBLE dentro do loop de chunks. `verify()` inalterada.
- `games/gow2/lift_baseline/MANIFEST.tsv` — 2 linhas PREAMBLE novas + bloco de cabecalho com a reconciliacao 1603/1662/1793 (D-2.2), a exclusao documentada, e a divergencia conhecida do lift novo.
- `games/gow2/lift_baseline/test_gen_manifest.py` (novo) — teste-ouro dedicado: `check_verify_pass_with_both_preamble_markers`, `check_verify_mutation_missing_trampoline_fn`, `check_verify_mutation_missing_indirect_call`, `check_generation_excludes_ps3_timebase_now`, `check_manifest_header_reconciles_three_figures`.

## Decisions Made

- Ver secao "Decisao critica" acima — a escolha do lift de referencia (novo para as contagens PREAMBLE, `.pre_v4` para o `--verify` de aceitacao) e' o desvio mais significativo desta execucao, motivado por medicao, nao por conveniencia.
- `ps3_timebase_now` mantido fora do `kind=PREAMBLE`, exatamente como o plano exigia, com a razao codificada em comentario junto de `preamble_syms = lifter_syms - {"ps3_timebase_now"}`.
- `MANIFEST.tsv` editado por insercao cirurgica (nunca regenerado por completo) — confirmado por `git diff | grep '^-' | grep -v '^---'` vazio.
- Cabecalho reescrito uma vez para nao violar o proprio criterio de aceitacao `grep -c PREAMBLE MANIFEST.tsv == 2`: a primeira redacao mencionava a palavra "PREAMBLE" em prosa 3 vezes (total 5 ocorrencias); reescrita para "coluna `tipo`"/"tipo novo" na prosa, deixando a palavra literal `PREAMBLE` so' nas 2 linhas de dados.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug no proprio SUMMARY/artefacto em curso] Cabecalho violava `grep -c PREAMBLE == 2`**
- **Found during:** Task 1, verificacao final antes do commit
- **Issue:** o primeiro rascunho do bloco de cabecalho usava a palavra "PREAMBLE" em prosa (explicando o mecanismo) 3 vezes, fazendo `grep -c PREAMBLE MANIFEST.tsv` sair `5` em vez do `2` exigido pelo criterio de aceitacao.
- **Fix:** reescrita da prosa para "coluna `tipo`"/"tipo novo", preservando o conteudo tecnico sem usar a palavra literal fora das 2 linhas de dados.
- **Files modified:** `games/gow2/lift_baseline/MANIFEST.tsv`
- **Verification:** `grep -c PREAMBLE MANIFEST.tsv` -> `2`.
- **Committed in:** `dd7170a` (parte do commit da Task 1, corrigido antes do commit final).

---

**Total deviations:** 1 auto-fixado (Rule 1, cosmetico/mecanico, sem impacto em logica).
**Impact on plan:** Nenhum scope creep. O desvio de fundo real (qual lift medir) foi antecipado e mandatado pelo proprio prompt de execucao, nao e' um desvio das regras 1-4, e esta documentado em detalhe na secao "Decisao critica" acima.

## Issues Encountered

- **A leitura factual mais importante desta execucao:** o `recomp_macos_v2` que o plano assumia ja tinha sido substituido pelo lift regenerado (promocao `57b418f`) antes da execucao comecar. Isto foi tratado seguindo a instrucao explicita do prompt — medir os dois, decidir consciente, documentar — em vez de assumir qual "e' o real".
- `test_ppu_lifter_emit.py` (Fase 1) foi lido apenas como referencia de forma/estilo, nao estendido, exatamente como o `<read_first>` instruia (`games/gow2/` nao tem entrada em `CMakeLists.txt` nem em `scripts/test_python.sh`).
- Nenhum ficheiro alheio (`.planning/codebase/TESTING.md`, `scripts/test_python.sh`, fixtures SPU) foi tocado. `build-tests/` e `runtime/spu/tests/out_spu_channel_blocking/` ficaram como untracked de sessoes anteriores — nao adicionados a nenhum commit.
- `.planning/ROADMAP.md`/`.planning/STATE.md` apareceram modificados por um agente paralelo (`02-02-PLAN.md`, que corre em simultaneo neste mesmo repositorio) — confirmados como fora do meu escopo e excluidos de ambos os commits via `git add` explicito (nunca `-A`/`.`).

## User Setup Required

None - nenhuma configuracao de servico externo necessaria.

## Next Phase Readiness

- Criterio 1 do ROADMAP fechado para o gap do preambulo (D-2.1) e a reconciliacao das tres cifras (D-2.2) — ambos vivem no artefacto (`MANIFEST.tsv`), nao so' num plano.
- **Nao resolvido, e explicitamente fora de escopo (Fase 3):** os 33 marcadores TAG/SYM/GLOBAL pre-existentes (de 244) que ja divergem do `recomp_macos_v2` real de producao (`211/244`). A Fase 3 ("Re-lift num diretorio novo com gate de baseline") tera' um lift fresco para medir/regenerar contra — nao corrigido aqui para nao misturar essa deriva nao-relacionada com a cobertura do preambulo.
- `verify_lift_baseline.sh` continua sem nenhuma linha de codigo alterada — herdou a cobertura nova automaticamente, exatamente como D-2.1 previa.
- 02-02 (F2B em ficheiro versionado proprio, `host_gow2_f2b.c`) e 02-03 (build_macos.sh) correm em paralelo neste mesmo checkout; nenhuma sobreposicao de ficheiros detectada nos meus commits.

---
*Phase: 02-c-digo-host-rf-o-versionado-e-compilado*
*Completed: 2026-07-26*

## Self-Check: PASSED

- FOUND: `games/gow2/lift_baseline/gen_manifest.py`
- FOUND: `games/gow2/lift_baseline/MANIFEST.tsv`
- FOUND: `games/gow2/lift_baseline/test_gen_manifest.py`
- FOUND: `.planning/phases/02-c-digo-host-rf-o-versionado-e-compilado/02-01-SUMMARY.md`
- FOUND commit `dd7170a` (feat(gow2-lift-baseline): cobre o preambulo puro com marcadores kind=PREAMBLE)
- FOUND commit `c7d893e` (test(gow2-lift-baseline): teste-ouro por mutacao dos marcadores PREAMBLE)
