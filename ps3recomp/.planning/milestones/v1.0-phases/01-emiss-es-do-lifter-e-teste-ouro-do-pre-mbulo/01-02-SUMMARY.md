---
phase: 01-emiss-es-do-lifter-e-teste-ouro-do-pre-mbulo
plan: 02
subsystem: infra
tags: [ppu_lifter, ppu_res_atomics, oob-instrumentation, mutation-testing, report-only]

requires:
  - "01-01: helper de mutacao reutilizavel (assert_mutation_breaks) em runtime/ppu/tests/test_ppu_lifter_emit.py"
provides:
  - "Os quatro ppu_res_* (lwarx/stwcx/ldarx/stdcx) do SOURCE_PREAMBLE chamam ps3_mem_oob_report em acesso OOB, com matriz len/op D-03 fixada por assercao e mutacao"
  - "Harness C++ dedicado que compila e roda a fatia real do preambulo, provando report-only em execucao (nao so' por texto)"
affects: ["01-03-PLAN.md"]

tech-stack:
  added: []
  patterns:
    - "Bounds check auto-contido no preambulo (sem depender de vm_oob static nem de #include novo -- forward-declaration extern C)"
    - "Mutacao em copia de memoria contra a matriz len/op, aplicada duas vezes: sobre o SOURCE_PREAMBLE inteiro e sobre a fatia extraida para compilacao"
    - "Harness C++ dedicado e pequeno para a fatia dos atomicos, separado do HARNESS_TEMPLATE de emissao por instrucao (28 slots), evitando poluir os testes existentes"

key-files:
  created: []
  modified:
    - tools/ppu_lifter.py
    - runtime/ppu/tests/test_ppu_lifter_emit.py

key-decisions:
  - "D-01..D-04 do CONTEXT.md aplicados literalmente: instrumentacao inline no SOURCE_PREAMBLE, chamando ps3_mem_oob_report (nao vm_oob, que e' static), matriz len/op estatica 4/4/8/8 read/write/read/write, comportamento report-only (nunca abortar)"
  - "extract_ppu_res_bodies() cai para fim-de-string quando o comentario de dispatch de chamada indireta nao esta' presente no texto dado -- permite reusar o mesmo extrator de corpos tanto sobre o SOURCE_PREAMBLE inteiro (Task 1) quanto sobre a fatia isolada extraida para compilacao (Task 2), sem duplicar a logica de fatiamento"
  - "Buffer guest do harness dos atomicos dimensionado em 64 bytes (ATOMICS_BUF_SIZE), suficiente para exercitar controle dentro-dos-limites, os quatro casos OOB e o caso de fronteira exata sem overlap entre eles"

requirements-completed: [REQ-RDY0-1]

coverage:
  - id: D1
    description: "Os quatro ppu_res_* instrumentados: ppu_res_oob() auto-contido, forward-declarations extern C de ppu_vm_size e ps3_mem_oob_report, macro PPU_RES_RA() com split _MSC_VER/portavel, guard OOB nos quatro corpos com a matriz len/op D-03"
    requirement: "REQ-RDY0-1"
    verification:
      - kind: unit
        ref: "runtime/ppu/tests/test_ppu_lifter_emit.py::check_ppu_res_instrumentation_present (Testes 1 e 2)"
        status: pass
      - kind: unit
        ref: "script de verify inline do plano (matriz len/op via regex sobre SOURCE_PREAMBLE)"
        status: pass
    human_judgment: false
  - id: D2
    description: "Prova por mutacao: remover a chamada de reporte de qualquer um dos quatro corpos (copia em memoria, SOURCE_PREAMBLE inteiro E a fatia extraida para compilacao) derruba a matriz D-03"
    requirement: "REQ-RDY0-1"
    verification:
      - kind: unit
        ref: "runtime/ppu/tests/test_ppu_lifter_emit.py::check_ppu_res_mutation + check_ppu_res_slice_mutation (5 linhas PASS de mutacao: 4 sobre o preambulo inteiro + 1 sobre a fatia)"
        status: pass
    human_judgment: false
  - id: D3
    description: "A fatia dos atomicos compila como C++ contra ppu_context.h real e roda: caminho dentro-dos-limites bit-a-bit igual ao anterior (CAS com sucesso, CR0[EQ], memoria guest big-endian), os quatro caminhos OOB reportam sem abortar com o (len, op) correto observado por um mock em execucao, e a fronteira exata (> nao >=, soma em 64 bits) e' exercitada"
    requirement: "REQ-RDY0-1"
    verification:
      - kind: integration
        ref: "runtime/ppu/tests/test_ppu_lifter_emit.py::build_and_run_atomics (harness C++ compilado e executado, 21 linhas PASS)"
        status: pass
      - kind: integration
        ref: "ctest --test-dir build-tests -> py.ppu_lifter_emit PASS; ./scripts/test_python.sh 7 PASS / 2 SKIP / 0 FAIL"
        status: pass
    human_judgment: false

duration: ~40min
completed: 2026-07-26
status: complete
---

# Phase 1 Plan 2: Instrumentação OOB dos quatro atômicos ppu_res_* — Summary

**Desarma a mina do re-lift: `ppu_res_lwarx/stwcx/ldarx/stdcx` do `SOURCE_PREAMBLE` agora chamam `ps3_mem_oob_report` em acesso fora dos limites (report-only, matriz len/op D-03), com a fatia compilada e rodada por um harness dedicado que prova o caminho dentro-dos-limites permanece bit-a-bit igual.**

## Performance

- **Duration:** ~40min
- **Completed:** 2026-07-26
- **Tasks:** 2/2 completas
- **Files modified:** 2 (`tools/ppu_lifter.py`, `runtime/ppu/tests/test_ppu_lifter_emit.py`)

## Accomplishments

- `tools/ppu_lifter.py`: bloco novo inserido no `SOURCE_PREAMBLE` entre o comentário de design e `ppu_res_bswap32` — `extern "C" uint32_t ppu_vm_size;`, forward-declaration de `ps3_mem_oob_report` (assinatura idêntica a `include/ps3_mem_trace.h:65`), macro `PPU_RES_RA()` (split `_MSC_VER`/portável, sem `#include` novo) e `static inline int ppu_res_oob(uint32_t ea, uint32_t len)` com a soma promovida a 64 bits.
- Os quatro `ppu_res_*` instrumentados espelhando `runtime/ppu/ppu_loader.cpp:1691-1752`: `lwarx`/`ldarx` reportam e retornam 0 limpando a reserva **antes** do `memcpy` (a leitura OOB nunca chega a acontecer); `stwcx`/`stdcx` reportam e **não** executam o CAS quando o endereço da reserva já-validada está fora dos limites, deixando `CR0[EQ]` em zero. Matriz len/op: `lwarx=(4,read)` `stwcx=(4,write)` `ldarx=(8,read)` `stdcx=(8,write)`.
- `runtime/ppu/tests/test_ppu_lifter_emit.py` estendido com: (1) presença + matriz D-03 por regex sobre o `SOURCE_PREAMBLE` real; (2) mutação — remover a chamada de reporte de qualquer um dos quatro corpos (cópia em memória, nunca em disco) derruba a matriz, testado tanto sobre o preâmbulo inteiro quanto sobre a fatia isolada extraída para compilação; (3) um harness C++ **dedicado** (`ATOMICS_HARNESS_TEMPLATE`, separado do harness de emissão por instrução de 28 slots) que compila a fatia real extraída do `SOURCE_PREAMBLE` contra `ppu_context.h` e roda 21 asserções: controle dentro-dos-limites (zero reportes, CAS com sucesso, memória guest em big-endian — prova de que a instrumentação é aditiva), os quatro casos OOB (não aborta, reporta com o `(len, op)` certo, `lwarx`/`ldarx` devolvem 0 e limpam a reserva, `stwcx`/`stdcx` não executam o CAS e zeram `CR0[EQ]`), e o caso de fronteira exata (4 bytes terminando no último byte não reporta; um byte antes do fim reporta — pina `>` em vez de `>=` e a promoção a 64 bits).

## Task Commits

1. **Task 1: Instrumentar os quatro ppu_res_* inline no SOURCE_PREAMBLE** - `4e815cf` (fix)
   - RED confirmado: `AssertionError: instrumentacao OOB dos ppu_res_* ausente` antes da implementação (as asserções de presença/matriz foram escritas e rodadas contra o preâmbulo NÃO instrumentado, confirmando que não são no-ops).
   - GREEN: bloco de instrumentação inserido, quatro corpos guardados, matriz len/op verificada por regex e por script de verify independente do plano.
   - `git diff tools/ppu_lifter.py` confirmado restrito ao intervalo do `SOURCE_PREAMBLE` entre o fim do comentário de design e o fim de `ppu_res_stdcx` — nenhuma outra linha tocada.
2. **Task 2: Compilar e rodar a fatia dos atômicos, provando report-only** - `3065674` (test)
   - `extract_ppu_res_slice()` fatia VERBATIM o bloco novo + os quatro atômicos; harness dedicado compila e roda 21 asserções contra a fatia real.
   - `extract_ppu_res_bodies()` ajustado para cair em fim-de-string quando o comentário de dispatch de chamada indireta não está presente (a fatia extraída para compilação termina antes dele) — permitiu reusar o extrator de corpos na mutação sobre a fatia sem duplicar lógica.
   - `git status --porcelain tools/ppu_lifter.py` confirmado vazio após a run — a mutação da Task 2 nunca escreveu em disco.

## Files Created/Modified

- `tools/ppu_lifter.py` — bloco de instrumentação OOB (declarações extern + macro + `ppu_res_oob`) e guard nos quatro `ppu_res_*`. Diff restrito ao intervalo documentado; nenhuma outra linha do `SOURCE_PREAMBLE` tocada (confirmado por `git diff`).
- `runtime/ppu/tests/test_ppu_lifter_emit.py` — `import re`; seção 4 (presença/matriz/mutação contra o `SOURCE_PREAMBLE`); seção 5 (extrator de fatia, `ATOMICS_HARNESS_TEMPLATE`, `build_and_run_atomics`, mutação sobre a fatia); `main()` religado nas duas novas seções.

## Decisions Made

- Seguidas literalmente as decisões D-01 a D-04 do `01-CONTEXT.md`: instrumentação inline (não fusão com `ppu_memory.h`, não uso de `vm_oob` que é `static`, sem gate `PS3_*` próprio — o gate/cap já vive dentro de `ps3_mem_oob_report`).
- `extract_ppu_res_bodies()` generalizado para aceitar tanto o `SOURCE_PREAMBLE` completo quanto a fatia isolada, evitando um segundo extrator de corpos duplicado para a Task 2 — a diferença é só a presença ou ausência do comentário-agulha final.
- Buffer guest do harness da Task 2 dimensionado em 64 bytes (`ATOMICS_BUF_SIZE`): pequeno suficiente para manter os testes legíveis, grande suficiente para separar claramente os endereços de controle, OOB e fronteira sem overlap.
- Task 2 passo 5 do plano ("mutação sobre a fatia extraída") implementado como uma mutação adicional e explícita (`check_ppu_res_slice_mutation`), distinta da mutação da Task 1 sobre o preâmbulo inteiro — literal ao texto do plano, mesmo que redundante em cobertura com a Task 1 (a intenção declarada era pinar especificamente a fatia usada para compilar, não só o preâmbulo).

## Deviations from Plan

Nenhum desvio material. Todas as âncoras `arquivo:linha` citadas no plano (`ppu_vm_size` em `ppu_loader.cpp:376`, `vm_oob` `static` em `:399-401`, a implementação dos quatro atômicos do runtime em `:1691-1752`, `ps3_mem_oob_report` em `include/ps3_mem_trace.h:65` e `runtime/trace/ps3_mem_trace.c:36-46`) bateram exatamente com o código real medido no início da execução.

Único ajuste técnico (não um desvio de decisão, apenas de implementação): `extract_ppu_res_bodies()` precisou de um fallback para fim-de-string quando a agulha de fim (`/* Indirect call dispatch`) não está presente no texto — necessário porque a fatia extraída pela Task 2 (usada para compilação) termina *antes* dessa agulha por construção. Sem o fallback, reusar o mesmo extrator sobre a fatia levantava `ValueError`. Corrigido inline durante a Task 2 antes do primeiro commit da task (nunca chegou a ficar quebrado em um commit).

## Issues Encountered

Nenhum bloqueio. A armadilha conhecida do CLAUDE.md se confirmou de novo: `ctest --test-dir build-tests` regenerou os 12 arquivos de fixture `runtime/spu/tests/out_*/spu_recomp.[ch]` (6 pares). Restaurados com `git checkout --` antes de qualquer commit — nunca staged, nunca commitados.

## User Setup Required

None - nenhuma configuração de serviço externo necessária.

## Verification Evidence (medido nesta sessão)

- `.venv/bin/python3 runtime/ppu/tests/test_ppu_lifter_emit.py` → `rc=0`. Stdout contém: 2 linhas PASS de presença/matriz (Testes 1-2), 5 linhas PASS de mutação dos `ppu_res_*` (4 sobre o preâmbulo inteiro + 1 sobre a fatia), e 21 linhas PASS do harness compilado dos atômicos (controle + 4 OOB + fronteira exata).
- Script de verify inline do plano (matriz len/op via regex direto sobre `SOURCE_PREAMBLE` importado) → `OK matriz len/op D-03`, `rc=0`.
- `./scripts/test_python.sh` → `test_python.sh -- resumo (9 testes): PASS: 7  SKIP: 2  FAIL: 0` (os 2 SKIP são `lift_selftest`/`spu_lift_selftest`, esperados por falta do SDK PS3).
- `ctest --test-dir build-tests --output-on-failure` (configurado com `-DCMAKE_BUILD_TYPE=` vazio) → **21/21 testes passam, 0 FAIL, 2 SKIP esperados** (`py.lift_selftest`, `py.spu_lift_selftest`) — idêntico ao baseline medido antes desta sessão (pós-plano 01-01). `py.ppu_lifter_emit` em PASS (1.23s).
- Prova ponta-a-ponta (`../gow2-recomp`, `python3 ../ps3recomp/tools/ppu_lifter.py EBOOT.ELF --functions functions.json -o /tmp/lift_chk_0102 -j 4`) → **idêntica ao esperado**: `Truncated-bounds repair: extended 180 function(s), 18984 byte(s) of code recovered`, `0 fallback stubs (every referenced target is defined)`, 7 chunks (`ppu_recomp_000.cpp` .. `ppu_recomp_006.cpp`). Sem regressão.
- `git status --porcelain tools/ppu_lifter.py` vazio após a run de teste completa — nenhuma mutação escreveu em disco.

## Next Phase Readiness

- Critério 3 do ROADMAP satisfeito: a instrumentação foi ADICIONADA (D-01), não declarada como perda; a mina do re-lift registrada como bloqueador do Marco 1 no `STATE.md` está desarmada.
- O caminho dentro-dos-limites permanece bit-a-bit equivalente ao anterior — provado em execução pelo harness da Task 2, não só por inspeção de texto.
- `assert_mutation_breaks()` (do plano 01-01) reutilizado sem modificação; `extract_ppu_res_bodies()`/`extract_ppu_res_slice()` ficam disponíveis para o plano 01-03 se precisar inspecionar o mesmo trecho do preâmbulo.
- Nenhum bloqueio conhecido para `01-03-PLAN.md` (gate G4 de tokens proibidos + gate de sincronização do `build_macos.sh`).
- `Windows não regride`: as adições estão fora de guarda e portáveis (`ppu_res_oob`, a chamada de reporte) ou dentro do split `_MSC_VER` já existente (a macro `PPU_RES_RA()`); nenhum `#include` novo foi acrescentado ao preâmbulo.

---
*Phase: 01-emiss-es-do-lifter-e-teste-ouro-do-pre-mbulo*
*Completed: 2026-07-26*

## Self-Check: PASSED

- FOUND: `tools/ppu_lifter.py`
- FOUND: `runtime/ppu/tests/test_ppu_lifter_emit.py`
- FOUND: `.planning/phases/01-emiss-es-do-lifter-e-teste-ouro-do-pre-mbulo/01-02-SUMMARY.md`
- FOUND commit `4e815cf` (fix(lifter): instrumenta os quatro ppu_res_* com report-only OOB)
- FOUND commit `3065674` (test(lifter): compila e roda a fatia dos ppu_res_*, prova report-only)
