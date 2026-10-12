---
phase: 01-emiss-es-do-lifter-e-teste-ouro-do-pre-mbulo
plan: 03
subsystem: infra
tags: [ppu_lifter, g4-gate, build_macos-sync, mutation-testing, warn-only]

requires:
  - "01-01: helper de mutacao reutilizavel (assert_mutation_breaks) em runtime/ppu/tests/test_ppu_lifter_emit.py"
  - "01-02: convencao de secoes numeradas (4/5) no mesmo arquivo, reutilizada como secoes 6/7"
provides:
  - "Gate G4 (criterio 4 do ROADMAP): varredura por-ramo de tools/ppu_lifter.py inteiro contra <intrin.h>/Sleep(/__declspec/GetModuleHandleA fora de _MSC_VER ou _WIN32, com controle negativo obrigatorio"
  - "Gate de sincronizacao WARN-only do games/gow2/build_macos.sh vs ../gow2-recomp/build_macos.sh (D-05/D-06), com marcador de pulo que nao colide com PS3RECOMP_SKIP_REGEX"
  - "Ancora dos tres sitios que compilam ppu_stubs.cpp em games/gow2/build_macos.sh"
affects: []

tech-stack:
  added: []
  patterns:
    - "Rastreio de estado de guarda POR RAMO sobre diretivas de pre-processador embutidas em texto Python (if/ifdef/ifndef empilham; elif/else substituem o topo; endif desempilha)"
    - "Marcador de pulo que preserva a INTENCAO de uma convencao (pular, nunca falhar) sem usar o TOKEN literal que um classificador de CI desancorado casaria em qualquer posicao da saida"
    - "difflib.unified_diff para WARN-only textual, sem dependencia de terceiros"

key-files:
  created: []
  modified:
    - runtime/ppu/tests/test_ppu_lifter_emit.py

key-decisions:
  - "Guarda aceita para o G4 = _MSC_VER OU _WIN32 (nao so' _WIN32) -- as tres ocorrencias reais hoje estao sob _MSC_VER; confirmado no codigo real antes de escrever o gate, nao assumido"
  - "Escopo da varredura G4 = tools/ppu_lifter.py INTEIRO (nao so' o SOURCE_PREAMBLE), conforme D-09 ('tudo que o lifter pode emitir')"
  - "Marcador de pulo do gate de sync deliberadamente SEM a substring 'SKIP:' -- desvio declarado da letra de D-05, motivado por CMakeLists.txt:298 (PS3RECOMP_SKIP_REGEX desancorada) e verificado nesta sessao com PS3_SYNC_GATE_SIBLING apontando para um caminho inexistente"
  - "Ancora de D-05 fica em games/gow2/build_macos.sh (sempre presente); a comparacao com o irmao e' WARN-only e nunca contribui para o codigo de saida (D-06)"

requirements-completed: [REQ-RDY0-1]

coverage:
  - id: D1
    description: "Gate G4 com controle negativo: varredura por-ramo de tools/ppu_lifter.py, guarda _MSC_VER OU _WIN32, controle negativo obrigatorio em todo run (Sleep( injetado fora de guarda numa copia em memoria)"
    requirement: "REQ-RDY0-1"
    verification:
      - kind: unit
        ref: "runtime/ppu/tests/test_ppu_lifter_emit.py::check_g4_gate + check_g4_negative_control"
        status: pass
    human_judgment: false
  - id: D2
    description: "Gate de sincronizacao do build_macos.sh: metade ancora (3 sitios de ppu_stubs.cpp, com dentes) sempre roda; metade sincronizacao WARN-only nunca falha e pula sem colidir com o classificador SKIP da CI"
    requirement: "REQ-RDY0-1"
    verification:
      - kind: unit
        ref: "runtime/ppu/tests/test_ppu_lifter_emit.py::check_build_macos_anchor_present + check_build_macos_sync"
        status: pass
      - kind: integration
        ref: "PS3_SYNC_GATE_SIBLING=/nonexistent/... -> rc=0 e saida NAO casa PS3RECOMP_SKIP_REGEX lido de CMakeLists.txt -> CTEST-WOULD-CLASSIFY=PASSED"
        status: pass
    human_judgment: false

duration: ~50min
completed: 2026-07-26
status: complete
---

# Phase 1 Plan 3: Gate G4 de tokens proibidos e gate de sincronização do build_macos.sh — Summary

**Fecha o critério 4 (varredura de tokens proibidos por-ramo, com controle negativo) e a metade de sincronização do critério 1 (D-05/D-06) — último plano da Fase 1, que agora tem os quatro critérios do ROADMAP satisfeitos.**

## Performance

- **Duration:** ~50min
- **Completed:** 2026-07-26
- **Tasks:** 2/2 completas
- **Files modified:** 1 (`runtime/ppu/tests/test_ppu_lifter_emit.py`)

## Accomplishments

- **Gate G4** (`check_g4_gate`/`check_g4_negative_control`): varre `tools/ppu_lifter.py` inteiro (não só o `SOURCE_PREAMBLE`, per D-09) atrás de `<intrin.h>`, `Sleep(`, `__declspec` e `GetModuleHandleA` fora de uma condição que mencione `_MSC_VER` ou `_WIN32`. Rastreio de estado de guarda **por ramo**: `if`/`ifdef`/`ifndef` empilham a condição, `elif`/`else` **substituem o topo** (não empilham) — é o que impede o falso-negativo no ramo `#elif defined(__APPLE__)` da TLS de `g_trampoline_fn`, que está dentro do mesmo `#if` que o ramo `_MSC_VER` mas não é guardado por ele. Reconhecedor de diretiva: `#` seguido imediatamente (sem espaço) de uma das seis palavras, e esse `#` no início da linha (já stripada) ou logo após uma aspa — a primeira regra já descarta por construção o comentário Python `fallthrough_to: int = 0  # if it falls off the end...` (a linha real tem `# if` com espaço), a segunda captura a forma `lines.append("#if ...")`.
- **Controle negativo obrigatório em todo run**: injeta `Sleep(1000);` fora de qualquer condicional numa cópia em memória de `tools/ppu_lifter.py` (nada escrito em disco) e exige exatamente 1 violação detectada — prova de que o gate não está verde por vacuidade.
- **Gate de sincronização do `build_macos.sh`** (`check_build_macos_anchor_present`/`check_build_macos_sync`), duas metades:
  - **Ancora** (sempre roda, pode falhar): `games/gow2/build_macos.sh` existe e referencia `ppu_stubs.cpp` em >= 3 sítios distintos — com assertção de dentes (um texto sem a substring falha o check).
  - **Sincronização** (WARN-only, D-06): compara com `../gow2-recomp/build_macos.sh` via `difflib.unified_diff`; pula (marcador sem a substring `"SKIP:"`) quando o irmão não existe, PASSA quando idêntico, AVISA (nomeando a contagem de hunks e as primeiras linhas do diff) quando divergente, sem nunca mudar o código de saída. `PS3_SYNC_GATE_SIBLING` permite redirecionar o caminho do irmão só para testar o ramo de ausência (OFF por default).
  - **Desvio declarado da letra de D-05** (documentado em comentário de bloco no código, citando `CMakeLists.txt:298` e `:325-330`): o marcador de pulo não contém a substring `"SKIP:"` porque essa regex é desancorada e aplicada a este mesmo teste — na CI, onde o irmão nunca existe, imprimir esse token classificaria o teste **inteiro** (4 pinos de emissão + OOB + G4) como *Skipped* em toda execução, e `scripts/test_python.sh` (grep ancorado em `^`) discordaria do CTest. A intenção de D-05/D-06 (pular, avisar, nunca falhar) é preservada; só a forma literal do token muda.

## Task Commits

1. **Task 1: Gate G4 de tokens proibidos, com controle negativo** - `6574844` (test)
   - Prototipado antes de escrever no arquivo de teste: rodado contra o `tools/ppu_lifter.py` real (0 violações, 0 diagnósticos de subfluxo, profundidade residual 0) e contra uma mutação com `#include <intrin.h>`/`Sleep(` injetado (1 violação detectada) — medido nesta sessão, não assumido do plano.
   - `.venv/bin/python3 runtime/ppu/tests/test_ppu_lifter_emit.py` → `rc=0`, linhas PASS do gate G4 e do controle negativo.
   - `./scripts/test_python.sh` → `rc=0`, 7 PASS / 2 SKIP / 0 FAIL.
2. **Task 2: Gate de sincronização do build_macos.sh e ancoragem dos sítios de ppu_stubs.cpp** - `06a6b54` (test)
   - `.venv/bin/python3 runtime/ppu/tests/test_ppu_lifter_emit.py` → `rc=0`; com o irmão presente e divergente (estado real desta máquina), stdout contém `[AVISO] ... DIVERGENTE do irmao (2 hunk(s))`.
   - `PS3_SYNC_GATE_SIBLING=/nonexistent/path/build_macos.sh .venv/bin/python3 runtime/ppu/tests/test_ppu_lifter_emit.py` → `rc=0`; padrão `PS3RECOMP_SKIP_REGEX` lido de `CMakeLists.txt` (`SKIP:`) **não** casa a saída → `CTEST-WOULD-CLASSIFY=PASSED`.
   - `./scripts/test_python.sh` → `rc=0`, 7 PASS / 2 SKIP / 0 FAIL, `runtime/ppu/tests/test_ppu_lifter_emit.py` contado como PASS.

## Files Created/Modified

- `runtime/ppu/tests/test_ppu_lifter_emit.py` — seção 6 (`_g4_scan`, `check_g4_gate`, `check_g4_negative_control`, `G4_FORBIDDEN_TOKENS`, `G4_GUARD_MACROS`); seção 7 (`check_build_macos_anchor`, `check_build_macos_anchor_present`, `check_build_macos_sync`, `_default_sibling_build_macos_path`, `BUILD_MACOS_REL`, `PPU_STUBS_MIN_SITES`); import de `difflib`; `main()` religado nas duas novas seções, na sequência existente.

## Decisions Made

- Guarda aceita do G4 = `_MSC_VER` **OU** `_WIN32`, confirmado por grep no código real antes de implementar (as três ocorrências existentes — `__declspec(align(16))` em `~:67`, `#include <intrin.h>` em `~:237`, `__declspec(thread)` em `~:413` — estão todas sob `_MSC_VER`; nenhuma sob `_WIN32` puro).
- Reconhecedor de diretiva prototipado num script standalone antes de entrar no arquivo de teste (disciplina que o plano pediu explicitamente), rodado contra o arquivo real e contra a mutação, confirmando 0 subfluxos/0 profundidade residual/0 violações no caminho feliz e 1 violação exata no controle negativo.
- Escolhido `Sleep(1000);` como token do controle negativo (em vez de `#include <intrin.h>`) para não colidir com o reconhecedor de diretiva do próprio G4 (`#include` não é uma das seis palavras reconhecidas, então qualquer um dos quatro tokens serviria, mas `Sleep(` deixa a intenção mais clara: uma chamada Win32 nua, sem `#`).
- Marcador de pulo escolhido: `[PULADO] sub-gate de sincronizacao build_macos.sh: irmao ausente em {path}` — não contém `"SKIP:"` em nenhuma posição, comentário de bloco extenso ao lado citando as linhas exatas do `CMakeLists.txt` que motivam o desvio.

## Deviations from Plan

**Nenhum desvio material além do já declarado no próprio plano (D-05, forma do marcador de pulo — ver `<objective>` do `01-03-PLAN.md` e a decisão D-05 ratificada em `01-CONTEXT.md`).** Todas as âncoras `arquivo:linha` citadas no `<read_first>` bateram com o código real, com uma ressalva menor: os números de linha absolutos do plano (`:66-67`, `:236-238`, `:377-383`, `:482`) estão levemente desatualizados porque os planos 01-01/01-02 inseriram código entre eles — as mesmas construções existem, só deslocadas (`~:67`, `~:237`, `~:412-417`, `:517`). Verificado por grep antes de implementar, não corrigido no plano (fora de escopo desta execução).

## Issues Encountered

A armadilha conhecida do CLAUDE.md se confirmou de novo: `ctest --test-dir build-tests` e `./scripts/test_all.sh` regeneraram os 12 arquivos de fixture `runtime/spu/tests/out_*/spu_recomp.[ch]` (6 pares). `test_all.sh` já os restaura sozinho (`git checkout --` embutido no próprio script, confirmado no log desta corrida: "a suite SPU regenerou saída versionada/gerada NESTA corrida ... repondo"); a corrida isolada de `ctest` fora do `test_all.sh` foi restaurada manualmente com `git checkout --` antes do commit. Nunca staged, nunca commitados. Os diretórios `build-tests/` e `runtime/spu/tests/out_spu_channel_blocking/` já estavam untracked antes desta sessão começar (confirmado no `git status` inicial) e ficaram fora de qualquer commit, conforme instrução do usuário.

## User Setup Required

None - nenhuma configuração de serviço externo necessária.

## Verification Evidence (medido nesta sessão)

- `.venv/bin/python3 runtime/ppu/tests/test_ppu_lifter_emit.py` → `rc=0`. Stdout contém, além de tudo dos planos 01-01/01-02: 2 linhas PASS do gate G4 (real + controle negativo), 2 linhas PASS da âncora D-05 (real + dentes), e o bloco `[AVISO]` da sincronização (2 hunks, divergência `host_gow2_factory` nomeada).
- `PS3_SYNC_GATE_SIBLING=/nonexistent/path/build_macos.sh .venv/bin/python3 runtime/ppu/tests/test_ppu_lifter_emit.py` → `rc=0`; `[PULADO] sub-gate de sincronizacao build_macos.sh: irmao ausente em /nonexistent/path/build_macos.sh`; padrão `PS3RECOMP_SKIP_REGEX` (`"SKIP:"`, lido de `CMakeLists.txt` na hora, não digitado) **não** casa a saída → `CTEST-WOULD-CLASSIFY=PASSED`.
- `./scripts/test_python.sh` → `rc=0`: `PASS: 7  SKIP: 2  FAIL: 0` (os 2 SKIP são `lift_selftest`/`spu_lift_selftest`, esperados por falta do SDK PS3); `test_ppu_lifter_emit.py` contado como PASS.
- `ctest --test-dir build-tests --output-on-failure` (configurado com `cmake -B build-tests -G Ninja -DPS3RECOMP_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=` vazio, evitando a armadilha `-DNDEBUG`) → **21/21 testes passam, 0 FAIL, 2 SKIP esperados** (`py.lift_selftest`, `py.spu_lift_selftest`). `py.ppu_lifter_emit` PASS (1.18s). Idêntico ao baseline medido antes desta sessão.
- `./scripts/test_all.sh` → `rc=0`; `test_python.sh: OK (exit 0)`, `test_runtime.sh: OK (exit 0)`.
- Prova ponta-a-ponta (`../gow2-recomp`, `python3 ../ps3recomp/tools/ppu_lifter.py EBOOT.ELF --functions functions.json -o /tmp/lift_chk_0103b -j 4`) → **idêntica ao esperado**: `Truncated-bounds repair: extended 180 function(s), 18984 byte(s) of code recovered`, `0 fallback stubs (every referenced target is defined)`, 7 chunks (`ppu_recomp_000.cpp` .. `ppu_recomp_006.cpp`). Sem regressão.
- `git status --short` — vazio de alterações não intencionais após cada corrida de teste; só os pré-existentes `build-tests/` e `runtime/spu/tests/out_spu_channel_blocking/` (não criados por esta sessão, deixados intocados) permanecem untracked.

## Comportamento medido do gate de sync nos dois caminhos

- **Irmão presente e divergente** (estado real desta máquina): `[AVISO]` com contagem de hunks (2) e as primeiras linhas do diff unificado, nomeando o bloco `host_gow2_factory` (commit `1f651aa`) como a causa conhecida; código de saída do teste permanece `0`.
- **Irmão ausente** (`PS3_SYNC_GATE_SIBLING` apontando para caminho inexistente): `[PULADO]` sem a substring `"SKIP:"`; código de saída permanece `0`; confirmado que nem o CTest (`SKIP_REGULAR_EXPRESSION` desancorada) nem `scripts/test_python.sh` (grep ancorado em `^`) classificariam o teste como Skipped — ambos concordam em PASS.

## Next Phase Readiness

- **Fase 1 completa**: os quatro critérios de sucesso do ROADMAP para esta fase estão satisfeitos — (1) as quatro emissões fixadas por golden exato (01-01); (2) prova por mutação das quatro emissões (01-01); (3) instrumentação OOB dos quatro `ppu_res_*`, report-only, desarmando a mina do re-lift (01-02); (4) gate G4 com controle negativo, aceitando `_MSC_VER` ou `_WIN32` (01-03, este plano).
- `REQ-RDY0-1` avaliado como **completável**: os três planos da fase (`01-01`, `01-02`, `01-03`) estão todos com `status: complete` e self-check PASSED nos seus respectivos SUMMARY, e o critério de aceite literal do requisito ("o teste-ouro do preâmbulo está registrado em `scripts/test_python.sh` e no CTest, passa hoje, e falha comprovadamente quando cada emissão é removida") está satisfeito e verificado nesta sessão. Marcado `[COMPLETO]` em `REQUIREMENTS.md` como parte deste plano.
- Nenhum bloqueio conhecido para a Fase 2 (código host órfão versionado e compilado, `REQ-RDY0-2`) — a divergência do `build_macos.sh` (bloco `host_gow2_factory`) que motivou D-05/D-06 é justamente o domínio dessa próxima fase; o gate WARN-only aqui construído já a torna visível, não a resolve.
- `Windows não regride`: nenhuma linha nova depende de plataforma; o gate G4 e o gate de sincronização são testes Python puros (stdlib-only: `re`, `difflib`), sem tocar em `tools/ppu_lifter.py` nem em `games/gow2/build_macos.sh`.

---
*Phase: 01-emiss-es-do-lifter-e-teste-ouro-do-pre-mbulo*
*Completed: 2026-07-26*

## Self-Check: PASSED

- FOUND: `runtime/ppu/tests/test_ppu_lifter_emit.py`
- FOUND: `.planning/phases/01-emiss-es-do-lifter-e-teste-ouro-do-pre-mbulo/01-03-SUMMARY.md`
- FOUND commit `6574844` (test(lifter): gate G4 de tokens proibidos com controle negativo)
- FOUND commit `06a6b54` (test(lifter): gate de sincronizacao WARN-only do build_macos.sh (D-05/D-06))
