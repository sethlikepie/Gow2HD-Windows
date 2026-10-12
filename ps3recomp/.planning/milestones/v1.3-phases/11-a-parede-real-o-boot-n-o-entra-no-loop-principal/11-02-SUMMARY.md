---
phase: 11-a-parede-real-o-boot-n-o-entra-no-loop-principal
plan: 02
subsystem: testing
tags: [python, ppu-disasm, elf-parser, offline-test, ctest, tdd, wall4, subtag]

requires:
  - phase: 11-a-parede-real-o-boot-n-o-entra-no-loop-principal (plan 11-01, paralelo)
    provides: nada em runtime -- este plano e' puramente offline/Python e nao depende
      da sonda in-boot que o 11-01 esta a escrever
provides:
  - tools/test_wall4_subtag_fix.py com os tres testes offline D-11.4
  - find_low16_force_writes() -- detector reusavel do padrao rlwinm/ori/stw que forca
    low16 := 1 no desserializador 0x00254C40
  - prune(w0) -- o predicado exacto que o fix candidato B (11-03) tera de satisfazer
affects: [11-03-fix-candidato-B]

tech-stack:
  added: []
  patterns:
    - "Detector de padrao PPC via string-matching sobre Instruction.operands
      (janela de N instrucoes, nao 3-em-linha) -- reusavel para outros padroes
      rlwinm/ori/stw no lift"
    - "Prova por mutacao inline: correr as MESMAS assercoes contra uma variante
      deliberadamente errada dentro de try/except, exigir que falhem"

key-files:
  created:
    - tools/test_wall4_subtag_fix.py
  modified:
    - CMakeLists.txt
    - scripts/test_python.sh

key-decisions:
  - "Usei os 12 tags legitimos medidos por E1 ({3,4,9,15,16,17,18,20,21,23,25,32}),
    nao a lista de CONTEXT.md D-11.4 -- o proprio texto da task apontou que
    CONTEXT.md tem um '12' duplicado por lapso de transcricao"
  - "O grupo de corroboracao real-ELF NAO e' condicional por design de codigo --
    e' condicional por os dados existirem nesta maquina. Nesta maquina existem,
    por isso o grupo correu de facto (nao imprimiu SKIP:) e confirmou os dois EAs
    exactos (0x254E3C, 0x255074) contra o ELF real, nao so' a fixture sintetica"

requirements-completed: [REG-03]

coverage:
  - id: D1
    description: "Desassemblador encontra as duas escritas de low16 em 0x00254C40 (fixture sintetica + corroboracao real-ELF)"
    requirement: "REG-03"
    verification:
      - kind: unit
        ref: "tools/test_wall4_subtag_fix.py#test_deserializer_low16_writes_synthetic"
        status: pass
      - kind: unit
        ref: "tools/test_wall4_subtag_fix.py#test_deserializer_low16_writes_real_elf_optional"
        status: pass
    human_judgment: false
  - id: D2
    description: "+0x7C de um registo WAD e' matriz[0][3] (fixture de layout de func_0024C1F8)"
    requirement: "REG-03"
    verification:
      - kind: unit
        ref: "tools/test_wall4_subtag_fix.py#test_wad_matrix_offset_is_0x7c"
        status: pass
    human_judgment: false
  - id: D3
    description: "Predicado de poda por subtag poda o WAD (0x40030001) e nao poda nenhum dos 12 tags legitimos medidos por E1 -- da dentes ao fix B antes de existir"
    requirement: "REG-03"
    verification:
      - kind: unit
        ref: "tools/test_wall4_subtag_fix.py#test_outer_walk_subtag_prune_predicate"
        status: pass
    human_judgment: false
  - id: D4
    description: "Os dois runners (ctest, scripts/test_python.sh) registam o mesmo teste novo"
    requirement: "REG-03"
    verification:
      - kind: integration
        ref: "ctest --test-dir build-tests -R test_wall4_subtag_fix"
        status: pass
      - kind: integration
        ref: "scripts/test_python.sh (grep tools/test_wall4_subtag_fix.py)"
        status: pass
    human_judgment: false

duration: 35min
completed: 2026-08-04
status: complete
---

# Phase 11 Plan 02: Testes offline D-11.4 (dentes do fix B) Summary

**Os tres testes offline exigidos por D-11.4 existem, passam, e cada grupo discriminativo
tem prova por mutacao verificada duas vezes -- dentro e fora do proprio ficheiro de
teste -- antes de uma unica linha do fix candidato B (11-03) ser escrita.**

## Performance

- **Duration:** ~35 min
- **Tasks:** 2/2 completas
- **Files modified:** 3 (1 criado, 2 editados)

## Accomplishments

- `tools/test_wall4_subtag_fix.py`: quatro grupos de teste, zero dependencia obrigatoria
  de `../gow2-recomp` (o unico grupo que a toca, `test_deserializer_low16_writes_real_elf_optional`,
  SKIPa de forma limpa quando os dados nao existem -- e nesta maquina existem, por isso
  correu de facto e confirmou os dois EAs exactos contra o ELF real: `0x00254E3C` e
  `0x00255074`).
- `find_low16_force_writes()`: detector do padrao `rlwinm rX,rX,0,4,15` -> `ori rX,rX,0x1`
  -> `stw rX,0x0(rY)` dentro de uma janela de instrucoes (nao 3-em-linha; o ELF real
  intercala 2-5 instrucoes nao relacionadas entre cada passo). Prova por mutacao:
  inverter o imediato do 1o `ori` de 1 para 2 derruba a contagem de 2 para 1 matches.
- `prune(w0)`: o predicado exacto de D-11.1 candidato B
  (`(w0 & 0xFFFF) == 1 and ((w0 >> 16) & 0xFFF) != 1`), testado contra o WAD
  (`0x40030001`, deve podar), a familia de listas legitima (`0xC0010001`, nao pode
  podar), e os 12 tags legitimos que a sonda E1 mediu in-boot (nao pode podar
  nenhum). Prova por mutacao: inverter `!=` para `==` faz a mesma suite de
  assercoes falhar.
- Ambos os runners (`ctest`, `scripts/test_python.sh`) registam o mesmo teste novo,
  na mesma posicao relativa (depois de `test_spu_lift_ops`/`test_recomp_config.py`,
  antes de `lift_selftest`/`lift_selftest.py`) -- sem recriar a divergencia corrigida
  hoje entre os dois.

## Task Commits

1. **Task 1: Os tres testes D-11.4, sem dados de jogo** - `7c7a0f8` (test)
2. **Task 2: Registo nos dois runners** - `29a4ec0` (test)

_Nota: nao houve commit de metadados (docs) separado -- este plano corre em paralelo
com 11-01 no mesmo working tree e o utilizador pediu explicitamente para nao tocar em
STATE.md/ROADMAP.md/REQUIREMENTS.md aqui, para nao colidir com o outro agente._

**Nota de higiene do repo:** o 1o commit da Task 1 (`git commit`) apanhou, sem eu ter
pedido, tres ficheiros que o agente paralelo (11-01) ja tinha staged
(`games/gow2/config/gow2_recomp.toml`, `games/gow2/hooks/gow2_midasm_hooks.cpp`,
`games/gow2/hooks/gow2_midasm_hooks.h`) -- porque partilhamos o mesmo working tree e
o mesmo index git. Detectei via `git show --stat` (4 files changed em vez de 1),
desfiz com `git reset --soft HEAD~1`, dessage-ei os tres ficheiros alheios com
`git restore --staged`, e recommitei so' o meu ficheiro (`7c7a0f8`, 1 file changed,
363 insertions). Confirmado depois que o working tree do outro agente ficou intacto
(`git status --short` mostra os tres de volta como `M`, nao staged, nao perdidos).
CLAUDE.md regra 7 ("Nao commitar working tree alheio") cumprida por correcao, nao por
prevencao -- fica registado como aviso para quem correr em paralelo com outro agente
no mesmo checkout.

## Files Created/Modified

- `tools/test_wall4_subtag_fix.py` - os 4 grupos de teste D-11.4 (novo, 363 linhas)
- `CMakeLists.txt` - `test_wall4_subtag_fix` no `foreach(_ps3_pytest IN ITEMS ...)`,
  depois de `test_spu_lift_ops`
- `scripts/test_python.sh` - `tools/test_wall4_subtag_fix.py` na lista `TESTS=`,
  depois de `tools/test_recomp_config.py`

## Decisions Made

- Os 12 tags legitimos usados no predicado de poda vieram da tabela "As 14 chaves" de
  `docs/re_sessions/2026-08-03-E1-typewalk-trace.md` (`{3,4,9,15,16,17,18,20,21,23,25,32}`,
  todos `n=172 poda=0` em 6 corridas), NAO da lista em CONTEXT.md D-11.4, que a propria
  task assinalou como tendo um `12` duplicado por lapso de transcricao.
- O grupo de corroboracao real-ELF nao foi implementado como um teste "sempre SKIP em
  CI" -- e' condicional aos dados existirem, e nesta maquina existem (`../gow2-recomp/
  EBOOT.ELF`, `functions.json`, `ghidra_out/` todos presentes), por isso correu para
  valer e confirmou os dois EAs exactos, nao so' a fixture sintetica.
- Tamanho da funcao `0x00254C40` revalidado em runtime via
  `tools/ghidra_lookup.py ../gow2-recomp/ghidra_out 0x00254C40 --no-c` (deu `size=1832`,
  o mesmo numero que o `11-CONTEXT.md`/E2 citam) em vez de confiar cegamente no
  literal `1832` -- se `ghidra_out/` nao existir, o teste cai de volta para o literal
  com o comentario a citar a sessao, como o plano pediu.

## Deviations from Plan

None - plano executado exactamente como escrito. As duas unicas coisas que fugiram
ao guiao foram (a) a correcao do commit misturado com o working tree do agente
paralelo, documentada acima como "Nota de higiene do repo" (nao uma mudanca ao
codigo, uma correcao de processo git), e (b) reconfigurar `build-tests` com
`cmake -B build-tests -DCMAKE_BUILD_TYPE=` para o `ctest` apanhar o teste novo --
implicito no fluxo normal de "adicionar um `foreach` item", nao uma mudanca de
comportamento.

## Issues Encountered

**O commit da Task 1 arrastou ficheiros do agente paralelo (11-01).** Resolvido
imediatamente apos deteccao (ver "Nota de higiene do repo" acima). Nenhum dado
perdido, nenhum ficheiro do 11-01 tocado no conteudo -- so' o estado de staging foi
corrigido.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

`prune(w0)` em `tools/test_wall4_subtag_fix.py` e' o CONTRATO que o 11-03 (fix
candidato B, se a sonda de contagem D-11.2 confirmar B em vez de A) tem de
satisfazer: exactamente `(w0 & 0xFFFF) == 1 and ((w0 >> 16) & 0xFFF) != 1`, aplicado
no walk exterior `0x0041FF70`. Se o 11-03 implementar B, o caminho mais directo e'
extrair este predicado para um modulo partilhado (ou reproduzir a formula tal como
esta, ja que e' uma linha) e reusar exactamente esta suite como regressao -- nao
reescrever os 12 tags legitimos, eles ja estao aqui medidos e ancorados na sessao E1.

Nenhum bloqueador. Este plano nao interage com 11-01 (a sonda in-boot) em runtime --
zero dependencia de execucao entre os dois; a unica sobreposicao foi de working tree
git, ja corrigida.

---
*Phase: 11-a-parede-real-o-boot-n-o-entra-no-loop-principal*
*Completed: 2026-08-04*

## Self-Check: PASSED

- FOUND: `tools/test_wall4_subtag_fix.py`
- FOUND: `.planning/phases/11-a-parede-real-o-boot-n-o-entra-no-loop-principal/11-02-SUMMARY.md`
- FOUND commit `7c7a0f8` (Task 1)
- FOUND commit `29a4ec0` (Task 2)
- `grep -c test_wall4_subtag_fix CMakeLists.txt scripts/test_python.sh` -> 1, 1 (ambos os runners)
