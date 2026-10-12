---
phase: 01-emiss-es-do-lifter-e-teste-ouro-do-pre-mbulo
plan: 01
subsystem: infra
tags: [ppu_lifter, ppu_stubs, ppu_timebase_now, teste-ouro, mutation-testing]

requires: []
provides:
  - "PPULifter.undefined_targets() / PPULifter.emit_fallback_stubs() restaurados (ppu_stubs.cpp)"
  - "Teste-ouro do preambulo com as 4 emissoes do criterio 1 do ROADMAP fixadas por assercao exata"
  - "Helper de mutacao reutilizavel (assert_mutation_breaks) para os planos 02 e 03 desta fase"
affects: ["01-02-PLAN.md", "01-03-PLAN.md"]

tech-stack:
  added: []
  patterns:
    - "Mutacao em copia de memoria (nao escreve em disco) contra checks de presenca por substring"
    - "Filtro anti-auto-invalidacao: descartar linhas de comentario antes de exigir a assinatura de um definidor"

key-files:
  created: []
  modified:
    - tools/ppu_lifter.py
    - runtime/ppu/tests/test_ppu_lifter_emit.py

key-decisions:
  - "emit_fallback_stubs()/undefined_targets() portados literalmente do commit e52da4a (fork), sem redesenho -- ja validado em campo (0 undefined refs no lift real do GoW2)"
  - "--max-mid-passes deliberadamente NAO restaurado (fora de escopo desta fase); max_passes=25 continua hardcoded"
  - "CASES['mftb']/CASES['mftbu'] reconfirmados nesta sessao rodando o decoder e o tradutor de producao, nao transcritos de cabeca"
  - "Check do definidor em sys_timer.c descarta linhas de comentario antes de procurar a assinatura, com um teste explicito provando que um texto so-comentario NAO satisfaz o check"

requirements-completed: [REQ-RDY0-1]

coverage:
  - id: D1
    description: "ppu_stubs.cpp volta a ser gerado: PPULifter.undefined_targets()/emit_fallback_stubs() restaurados, flags --fallback-stubs/--no-fallback-stubs, limpeza de stub obsoleto em main()"
    requirement: "REQ-RDY0-1"
    verification:
      - kind: unit
        ref: "runtime/ppu/tests/test_ppu_lifter_emit.py::check_ppu_stubs_emission (Testes 1/2/4)"
        status: pass
      - kind: unit
        ref: ".venv/bin/python3 -c \"...emit_fallback_stubs()...\" (script de verify do plano)"
        status: pass
    human_judgment: false
  - id: D2
    description: "Prova por mutacao (D-07): as quatro emissoes do criterio 1 (ppu_stubs, stdlib.h, ramo __APPLE__ do TLS, contrato ppu_timebase_now) tem cada uma uma mutacao automatizada que roda em todo run"
    requirement: "REQ-RDY0-1"
    verification:
      - kind: unit
        ref: "runtime/ppu/tests/test_ppu_lifter_emit.py::check_ppu_stubs_mutation + check_preamble_mutations (4 linhas PASS de mutacao)"
        status: pass
    human_judgment: false
  - id: D3
    description: "Contrato ppu_timebase_now (D-10): CASES['mftb']/CASES['mftbu'] fixam a traducao mftb/mftbu -> ppu_timebase_now(); definidor exigido em runtime/syscalls/sys_timer.c fora de comentario, com filtro anti-auto-invalidacao provado"
    requirement: "REQ-RDY0-1"
    verification:
      - kind: unit
        ref: "runtime/ppu/tests/test_ppu_lifter_emit.py::check_goldens (mftb/mftbu) + check_timebase_definer_present"
        status: pass
    human_judgment: false

duration: ~35min (estimado -- sem timestamp inicial cronometrado nesta sessao)
completed: 2026-07-26
status: complete
---

# Phase 1 Plan 1: Emissoes do lifter e teste-ouro do preambulo — Summary

**Restaura a geracao de `ppu_stubs.cpp` (apagada pelo `11a1c3c`) e fixa por teste-ouro as quatro emissoes que o build consome, com prova por mutacao em todo run.**

## Performance

- **Duration:** ~35min (estimado)
- **Completed:** 2026-07-26
- **Tasks:** 2/2 completas
- **Files modified:** 2 (`tools/ppu_lifter.py`, `runtime/ppu/tests/test_ppu_lifter_emit.py`)

## Accomplishments

- `PPULifter.undefined_targets()` e `PPULifter.emit_fallback_stubs()` restaurados no lifter (portados literalmente do commit `e52da4a`, sem redesenho), com as flags `--fallback-stubs`/`--no-fallback-stubs` (default on) e limpeza de `ppu_stubs.c`/`.cpp` obsoleto antes de escrever. `max_passes = 25` continua hardcoded — `--max-mid-passes` foi deliberadamente deixado fora do escopo.
- Teste-ouro do preambulo (`runtime/ppu/tests/test_ppu_lifter_emit.py`) fixa as quatro emissoes do criterio 1 do ROADMAP: `#include <stdlib.h>`, o ramo `#elif defined(__APPLE__)` do TLS de `g_trampoline_fn`, a geracao de `ppu_stubs.cpp`, e o contrato `ppu_timebase_now` (declaracao no preambulo + definidor em `runtime/syscalls/sys_timer.c` + traducao exata de `mftb`/`mftbu`).
- Prova por mutacao (D-07) automatizada em todo run: quatro mutacoes, uma por emissao, cada uma provando que o check correspondente falha quando a emissao e removida (em copia de memoria, nunca em disco).
- Helper `assert_mutation_breaks()` escrito para ser reutilizado pelos planos 02 e 03 da mesma fase (instrumentacao dos atomicos e gate G4/sync).

## Task Commits

1. **Task 1: Restaurar a geracao de ppu_stubs.cpp e pina-la ponta-a-ponta** - `34e5bab` (fix)
   - RED confirmado: `AttributeError: 'PPULifter' object has no attribute 'emit_fallback_stubs'` antes da implementacao.
   - GREEN: `undefined_targets()`/`emit_fallback_stubs()` adicionados, teste passa.
2. **Task 2: Pinar as outras tres emissoes e o contrato ppu_timebase_now, com mutacao** - `4cb463c` (test)
   - `CASES['mftb']`/`CASES['mftbu']` + checks de presenca do preambulo + check do definidor + 3 mutacoes.

_Nota: a Task 1 tem `tdd="true"` no plano; a Task 2 e' `type="auto"` (sem gate RED/GREEN obrigatorio) — os checks de presenca das emissoes 1/2/4-declaracao ja passavam de imediato porque essas emissoes ja existiam no lifter (medido no CONTEXT.md); apenas o definidor e as mutacoes eram trabalho genuinamente novo._

## Files Created/Modified

- `tools/ppu_lifter.py` — `undefined_targets()`, `emit_fallback_stubs()`, flags `--fallback-stubs`/`--no-fallback-stubs`, limpeza de stub obsoleto e escrita de `ppu_stubs.cpp`/`.c` em `main()`. Nenhuma outra linha do `SOURCE_PREAMBLE` foi tocada (confirmado por `git diff` — todo o diff sao adicoes puras, zero remocoes fora do loop de `emit_header()` que foi trocado para chamar `undefined_targets()`).
- `runtime/ppu/tests/test_ppu_lifter_emit.py` — `CASES['mftb']`/`CASES['mftbu']`; `assert_mutation_breaks()`; `check_ppu_stubs_emission()`/`check_ppu_stubs_mutation()`; `check_preamble_emissions()`/`check_timebase_definer_present()`/`check_preamble_mutations()`; `main()` religado na nova sequencia.

## Decisions Made

- Portar o `e52da4a` literalmente (nao redesenhar): ja validado em campo no lift real do GoW2 ("0 undefined refs, was 16113"), conforme instruido pelo plano.
- `--max-mid-passes` explicitamente NAO restaurado — nao e nenhuma das quatro emissoes do criterio 1 e o plano marca isso fora de escopo.
- Golden pair `mftb`/`mftbu` reconfirmado nesta sessao rodando `ppu_disasm.decode` + `PPULifter._translate_op` ao vivo (nao transcrito de cabeca), batendo exatamente com os valores do plano.
- Check do definidor em `sys_timer.c` descarta linhas de comentario ANTES de procurar a assinatura, com uma assercao explicita (`comment_only`) provando que um texto so'-comentario nao satisfaz o check — evita a auto-invalidacao que o proprio arquivo teria produzido (seu bloco de comentario cita `ppu_timebase_now` varias vezes em prosa).
- Divisao dos commits por tarefa: como escrevi as duas secoes de teste numa unica edicao inicial (para verificar o RED da Task 1 de forma completa), reparti a alteracao do arquivo de teste em dois commits que espelham exatamente a fronteira das tarefas do plano, confirmando isoladamente que cada metade passa por si so' antes de commitar.

## Deviations from Plan

None - plano executado exatamente como escrito. Todas as siglas/linhas indicadas em `<read_first>` e `<action>` bateram com o codigo real medido no inicio da execucao (nenhuma linha do plano estava desatualizada em relacao ao repositorio).

## Issues Encountered

Nenhum bloqueio. Uma armadilha conhecida do CLAUDE.md se confirmou na pratica: rodar a suite completa de CTest (`ctest --test-dir build-tests`) para checagem de nao-regressao regenerou os fixtures `runtime/spu/tests/out_*/spu_recomp.[ch]` (12 arquivos, 6 pares .c/.h). Foram restaurados com `git checkout --` antes do commit, exatamente como instruido — nao commitados.

## User Setup Required

None - nenhuma configuracao de servico externo necessaria.

## Next Phase Readiness

- O criterio 1 do ROADMAP (as quatro emissoes) esta fixado por assercao exata; o criterio 2 (prova por mutacao) esta satisfeito para essas quatro.
- O helper `assert_mutation_breaks()` esta pronto para reuso pelos planos 02 (instrumentacao OOB dos quatro atomicos) e 03 (gate G4 + gate de sincronizacao do `build_macos.sh`) da mesma fase, como a `<action>` da Task 1 previa.
- Nao ha bloqueios conhecidos para `01-02-PLAN.md`.
- `./scripts/test_python.sh` sai 0 e classifica `runtime/ppu/tests/test_ppu_lifter_emit.py` como PASS (nao SKIP) — confirmado nesta sessao.
- `ctest --test-dir build-tests` (configurado com `-DCMAKE_BUILD_TYPE=` vazio, conforme a armadilha documentada) roda 21/21 testes, 0 FAIL, 2 SKIP esperados (`py.lift_selftest`/`py.spu_lift_selftest`, falta do SDK PS3) — sem regressao medida.

---
*Phase: 01-emiss-es-do-lifter-e-teste-ouro-do-pre-mbulo*
*Completed: 2026-07-26*

## Self-Check: PASSED

- FOUND: `tools/ppu_lifter.py`
- FOUND: `runtime/ppu/tests/test_ppu_lifter_emit.py`
- FOUND: `.planning/phases/01-emiss-es-do-lifter-e-teste-ouro-do-pre-mbulo/01-01-SUMMARY.md`
- FOUND commit `34e5bab` (fix(lifter): restaura geracao de ppu_stubs.cpp e pina-a por teste-ouro)
- FOUND commit `4cb463c` (test(lifter): pina as outras tres emissoes do preambulo + contrato ppu_timebase_now)
