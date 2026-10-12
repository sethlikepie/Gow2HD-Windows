---
phase: 11-a-parede-real-o-boot-n-o-entra-no-loop-principal
plan: 03
subsystem: re-fix
tags: [ppu-lifter, patch-idempotente, gow2, wall-4, d-11.2, candidato-b]

requires:
  - phase: 11-a-parede-real-o-boot-n-o-entra-no-loop-principal
    provides: "D-11.2 DECISION: B (11-01, contagem in-boot n=1, 3/3) + tools/test_wall4_subtag_fix.py::prune(w0) (11-02, o predicado offline)"
provides:
  - "games/gow2/recomp_mid_v2/patch_outer_subtag_prune.py -- fix candidato B, idempotente, tri-estado, verificado in-boot em 3/3 corridas a remover exactamente o push do registo WAD do walk exterior func_0041FF70"
  - "docs/re_sessions/2026-08-04-E4-fix-verification-parede-4.md -- ledger com as DUAS rondas: v1 (fiel ao plano) medida sem efeito nenhum + causa raiz + v2 corrigida e verificada"
affects: [11-04]

tech-stack:
  added: []
  patterns:
    - "Verificacao in-boot de um patch de lift NAO e opcional mesmo quando o mecanismo parece correcto na leitura do decompile -- a Task 2 desta fase mediu que a v1 (fiel ao texto do plano) tinha ZERO efeito, e so' a instrumentacao ad-hoc directa no lift (nao commitada) revelou a causa raiz"
    - "Reusar um destino de goto JA EXISTENTE (loc_0041FFE8, o skip do gate 'tem filho') para um TERCEIRO filtro de enumeracao, em vez de tentar guiar um compare condicional que so' protege parte dos dispatches da funcao"

key-files:
  created:
    - games/gow2/recomp_mid_v2/patch_outer_subtag_prune.py
    - docs/re_sessions/2026-08-04-E4-fix-verification-parede-4.md
  modified:
    - games/gow2/lift_baseline/PATCH_MIGRATION.tsv

key-decisions:
  - "v1 do fix (fiel ao mecanismo literal descrito por 11-03-PLAN.md -- mutar gpr[25] antes do compare iVar6==iVar2) foi implementada, testada em fixtures, aplicada a producao em copia, e VERIFICADA IN-BOOT COMO SEM EFEITO NENHUM -- nao foi descartada em silencio, ficou documentada com a causa raiz antes da correcao (regra CLAUDE.md 4: nunca forjar resultados)"
  - "Causa raiz medida: o 'h_child' que o v1 lia (vm_read32(gpr28+0x88)) e' lixo (0x3F800000, a MESMA leitura que a ressalva do 11-01/E3 ja deixara em aberto sem a fechar) -- nao o registo WAD. O push real vem de um SEGUNDO dispatch vt+0x40, INCONDICIONAL, em loc_00420090, que le a classificacao do PROPRIO no' de topo (vm_read32(gpr28+4)), nao a de h_child"
  - "v2 insere um terceiro filtro de enumeracao (nao um guard de consumo -- continua a nao ser candidato C) logo apos gpr28 ser estabelecido, reusando o destino loc_0041FFE8 que o gate 'tem filho' e os dois filtros de flags ja usam para a MESMA semantica de skip"

requirements-completed: [REG-03]

coverage:
  - id: D1
    description: "Fix candidato B implementado como patch_*.py idempotente, tri-estado (CONVERTIDO/JA-APLICADO/SEM-EFEITO rc!=0), incondicional, fiel ao predicado offline de tools/test_wall4_subtag_fix.py"
    requirement: "REG-03"
    verification:
      - kind: unit
        ref: "fixtures sinteticas (scratchpad, nao commitadas) -- tri-estado provado antes de tocar na producao"
        status: pass
      - kind: integration
        ref: "copia do ppu_recomp_001.cpp de producao real (recomp_macos_v2) -- CONVERTIDO confirmado, producao nunca tocada (hash 136ab046... identico antes/depois)"
        status: pass
    human_judgment: false
  - id: D2
    description: "Fix verificado in-boot a remover exactamente o push do registo WAD do walk exterior, sem afectar os 175 pushes legitimos, 3/3 corridas deterministicas"
    requirement: "REG-03"
    verification:
      - kind: integration
        ref: "boot_gow2_e4fix2 EBOOT.ELF, corridas D/E/F (90s, gates 1-3 ON, PS3_TYPEWALK_TRACE=1, G6 hygiene) -- ver docs/re_sessions/2026-08-04-E4-fix-verification-parede-4.md secao 'Ronda 2'"
        status: pass
    human_judgment: false
  - id: D3
    description: "Sem regressao nas suites offline nem no boot apos o fix"
    requirement: "REG-03"
    verification:
      - kind: unit
        ref: "ctest --test-dir build-tests -- 23/23 (2 skip esperados: lift_selftest, spu_lift_selftest, SDK PS3 indisponivel)"
        status: pass
      - kind: unit
        ref: "scripts/test_python.sh -- 14 PASS / 2 SKIP / 0 FAIL"
        status: pass
      - kind: integration
        ref: "st620 max=11 identico entre v1(quebrado)/v2(corrigido)/mesma janela -- sem regressao na progressao da intro/movie-FSM"
        status: pass
    human_judgment: false

duration: 70min
completed: 2026-08-04
status: complete
---

# Phase 11 Plan 03: Fix candidato B (poda por subtag no walk exterior) Summary

**`patch_outer_subtag_prune.py` -- fix idempotente, incondicional, verificado in-boot em 3/3 corridas a remover exactamente o push do registo WAD (`w0=0x40030001`) de `func_0041FF70`, depois de uma primeira versão fiel ao texto do plano ter sido medida, in-boot, como tendo ZERO efeito -- a Task 2 encontrou e corrigiu a causa raiz antes de fechar.**

## Estado

D-11.2 DECISION: B (herdada de `11-01-SUMMARY.md`, contagem n=1 em 3/3 corridas
determinísticas) foi implementada. O mecanismo literal descrito por `11-03-PLAN.md`
(mutar `ctx->gpr[25]` antes do compare `iVar6==iVar2` em `func_0041FF70`) foi
implementado (v1), testado em fixtures e contra uma cópia da produção real, e commitado
(`7ce01bf`). A verificação in-boot da Task 2 (obrigatória por este plano) mediu que a v1
**não mudava NADA** no boot real -- `enters=176`, `chaves=14`, `w0_40030001=2`,
`no_rep` crescendo para dezenas de milhões, idêntico à baseline sem fix. Investigação
com instrumentação ad-hoc (não commitada) localizou a causa: o "`h_child`" que o v1 lia
é lixo (`0x3F800000`, a mesma leitura que a ressalva do 11-01/E3 já tinha deixado em
aberto), e o push real do WAD vem de um **segundo** dispatch, incondicional, que usa a
classificação do **próprio** nó de topo. A v2, corrigida, foi implementada, testada em
fixtures novas, aplicada a uma cópia real, e **verificada in-boot em 3/3 corridas
(D/E/F)**: `enters` 176→175, `chaves` 14→12, `w0_40030001=0`, `no_rep=0`, assinatura do
WAD ausente nas três, `st620` sem regressão.

## Performance

- **Duration:** ~70 min
- **Started:** 2026-08-04T15:20:00-04:00 (aprox., primeiro commit `7ce01bf`)
- **Completed:** 2026-08-04T16:07:47-04:00
- **Tasks:** 2/2 (com uma ronda de correção intermédia dentro da Task 1/2, ver Deviations)
- **Files modified:** 3 (1 patch, 1 TSV, 1 doc novo)

## Accomplishments
- `patch_outer_subtag_prune.py` (v2, final): terceiro filtro de enumeração em
  `func_0041FF70`, reusando o destino `loc_0041FFE8` já usado pelos dois filtros de
  flags e pelo gate "tem filho" -- filtra pela classificação (`low16`/`subtag`) do
  próprio nó de topo, incondicional, sem gate `PS3_*`.
- Tri-estado (CONVERTIDO/JÁ-APLICADO/SEM-EFEITO rc!=0) provado com fixtures sintéticas
  E contra cópias reais do `ppu_recomp_001.cpp` de produção, nas duas rondas (v1 e v2) --
  produção (`recomp_macos_v2`) nunca tocada em nenhum momento da sessão (hash
  `136ab046d1a0ced07fc5bda330eba8263783e7732434bb5a8253e7db15d0a989` idêntico do início
  ao fim).
- Verificação in-boot completa: 2 lifts dedicados (`recomp_macos_e4fix`/v1,
  `recomp_macos_e4fix2`/v2), 2 binários dedicados, 5 boots de medição total (D/E/F ×2
  rondas + 1 de diagnóstico ad-hoc), G6 hygiene (kill por PID, zero órfãos em todas).
- Ledger `docs/re_sessions/2026-08-04-E4-fix-verification-parede-4.md` documenta as
  DUAS rondas por inteiro -- a v1 refutada por medição não foi apagada nem escondida.
- Suites de não-regressão do CLAUDE.md re-executadas e confirmadas: `ctest` 23/23,
  `scripts/test_python.sh` 14 PASS/2 SKIP/0 FAIL.

## Task Commits

Each task was committed atomically (com uma correção intermédia):

1. **Task 1 (v1, fiel ao plano)** - `7ce01bf` (feat) -- implementação inicial, tri-estado
   provado, mas **medida sem efeito no boot** pela Task 2 (ver abaixo)
2. **Correção (v2, apos a Task 2 refutar a v1)** - `3a75977` (fix) -- mecanismo corrigido,
   novo tri-estado provado
3. **Task 2 (ledger E4)** - `9ebf0d7` (docs) -- as duas rondas, causa raiz, verificação
   final in-boot em 3/3

**Plan metadata:** este SUMMARY.md (sem commit adicional -- ver nota abaixo)

_Nota: por instrução explícita do utilizador nesta sessão, `.planning/STATE.md` e
`.planning/ROADMAP.md` NÃO foram tocados (incidente anterior de corrupção). Não há
commit final de metadados GSD; o utilizador regista o progresso manualmente._

## Files Created/Modified
- `games/gow2/recomp_mid_v2/patch_outer_subtag_prune.py` - fix candidato B (v2, final);
  mirror byte-a-byte em `../gow2-recomp/recomp_mid_v2/` (não commitado nesse repo, por
  desenho -- é o checkout físico dos dados de build, não versionado por esta sessão)
- `games/gow2/lift_baseline/PATCH_MIGRATION.tsv` - nova linha
  `patch_outer_subtag_prune.py` (CORRECTNESS, alvo=lifter, `0x0041FF70`, P0,
  `re_session:E4-fix-verification-parede-4`)
- `docs/re_sessions/2026-08-04-E4-fix-verification-parede-4.md` - ledger completo das
  duas rondas (v1 refutada + v2 verificada)

## Decisions Made

- **v1 implementada, testada, e verificada in-boot como SEM EFEITO -- registada, não
  escondida.** O mecanismo que `11-03-PLAN.md` descrevia literalmente (mutar
  `ctx->gpr[25]` para influenciar o compare `iVar6==iVar2`) provou-se, por medição
  directa in-boot, não ser o que decide se o registo WAD é empurrado por
  `func_0041FF70`. Por CLAUDE.md regra 4 ("nunca forjar resultados") e pela instrução
  explícita da tarefa ("se um passo estiver factualmente errado face ao código real,
  PARA e reporta -- não improvises"), a Task 2 não foi escondida atrás de um "sucesso"
  forjado: a v1 ficou documentada com a causa raiz, e a v2 foi escrita, testada, e
  re-verificada in-boot antes de este plano ser dado como concluído.
- **Causa raiz:** `ctx->gpr[27]` (o "`h_child`" da leitura `vm_read32(gpr28+0x88)`) vale
  `0x3F800000` para o registo WAD -- a MESMA leitura de lixo que a sonda de censo do
  11-01/E3 já tinha medido e deixado como ressalva não fechada
  (`docs/re_sessions/2026-08-04-E3-outer-walk-subtag-census.md`, secção "Porque o
  C-known não reproduziu"). O compare `iVar6==iVar2` que o v1 tentava influenciar só
  protege UM de DOIS dispatches `vt+0x40` dentro de `func_0041FF70`; o segundo,
  incondicional, alcançado sempre via `loc_00420090`, usa a classificação do **próprio**
  nó de topo (`vm_read32(gpr28+4)`) -- é esse que despacha o WAD.
- **v2:** terceiro filtro de enumeração, reusando o destino `loc_0041FFE8` já presente,
  aplicado logo após `gpr28` ser estabelecido -- corresponde literalmente à frase de
  D-11.1 para o candidato B ("filtrar também por subtag, não só por flags"). Continua a
  não ser o candidato C: filtra ANTES de qualquer push acontecer, pela classificação do
  próprio nó, sem tocar em `func_0041F700` nem validar a lista que ele consome.
- **A ressalva herdada do 11-01/E3** (identidade exacta do objecto contado, dependente
  de uma leitura de `piVar8+4` não feita nessa sessão) fica, na prática, fechada: a
  leitura adjacente `vm_read32(gpr28+4)` que a v2 faz, no MESMO ponto do código, mede
  directamente `0x40030001` -- confirma que o nó tem a classificação patológica
  esperada, sem precisar de uma sonda extra dedicada.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] `Path.write_text(newline=...)` requer py3.10+, `python3` do sistema é 3.9.6**
- **Found during:** Task 1, gate tri-estado com fixtures (RED)
- **Issue:** O patch (modelado em `patch_fallthrough_2550c8.py`) usava
  `path.write_text(src, encoding="utf-8", newline="\n")`, que lança `TypeError` em
  Python 3.9.6 (armadilha já documentada no CLAUDE.md).
- **Fix:** Substituído por `path.write_bytes(new_src.encode("utf-8"))` -- portável em
  qualquer versão, sem risco de tradução de fim de linha (o texto de origem já usa só
  `\n`).
- **Files modified:** `games/gow2/recomp_mid_v2/patch_outer_subtag_prune.py`
- **Verification:** Tri-estado completo (CONVERTIDO/JÁ-APLICADO/SEM-EFEITO) passou com
  `/usr/bin/python3` (3.9.6) após a correcção.
- **Committed in:** `7ce01bf` (parte do commit inicial da Task 1)

**2. [Rule 1 - Bug, achado pela verificação in-boot obrigatória da Task 2] Mecanismo do fix v1 sem efeito no boot real**
- **Found during:** Task 2, primeira corrida de medição (D) contra `boot_gow2_e4fix`
- **Issue:** ver "Decisions Made" acima e o ledger E4 completo -- o mecanismo prescrito
  pelo plano não é o que decide o push do WAD.
- **Fix:** Reescrita completa do patch (v2) com um mecanismo diferente, dentro do mesmo
  candidato B (D-11.1), verificado in-boot antes de fechar.
- **Files modified:** `games/gow2/recomp_mid_v2/patch_outer_subtag_prune.py`
- **Verification:** 3/3 corridas in-boot (D/E/F), ver ledger E4.
- **Committed in:** `3a75977`

---

**Total deviations:** 2 auto-fixed (1 bug de portabilidade, 1 bug de mecanismo -- este
último é o motivo de existir uma "Task 2 obrigatória de verificação in-boot" no plano, e
é exactamente o que ela apanhou).
**Impact on plan:** Nenhum desvio de escopo -- ambos os fixes são o MESMO candidato B
(D-11.1/D-11.2), apenas corrigidos até serem verificados como funcionando de facto, não
apenas na leitura do decompile.

## Issues Encountered

- **A primeira leitura do decompile do Ghidra (citada pelo `11-03-PLAN.md`) não bastava
  para prever o comportamento real do código liftado.** `func_0041FF70` tem DOIS
  dispatches `vt+0x40` distintos (um condicional via `h_child`, outro incondicional via
  a classificação do próprio nó), e o texto do plano só descrevia o primeiro. Só a
  verificação in-boot (Task 2) revelou isto -- reforça a razão de esta Task 2 ser
  obrigatória e não um "carimbo" opcional.
- Nenhum bloqueio não resolvido.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- **11-04** (medição de aceite, critério 3 do ROADMAP: `AUTO_LOAD` ≥1 vez sem gates de
  diagnóstico, ≥4 de 6 corridas) pode consumir `patch_outer_subtag_prune.py` (v2) como
  fix fechado e verificado -- mas TODAS as corridas desta sessão correram COM os gates
  1-3 ON (a mesma dependência que E1/E3 já documentaram: o walk exterior só é alcançado
  hoje com esses gates). O 11-04 precisa de medir se isso ainda se aplica sem o fix
  aplicado a mudar essa dependência, e reportar honestamente se não conseguir.
- `games/gow2/lift_baseline/PATCH_MIGRATION.tsv` regista o novo patch como
  `CORRECTNESS`/`lifter`, não migrado para weak-override/mid-asm nesta fase (por desenho
  -- fica um `patch_*.py` de texto).
- **NÃO tocado nesta sessão, por instrução explícita:** `.planning/STATE.md`,
  `.planning/ROADMAP.md`. O utilizador regista o progresso manualmente.
- Lifts/binários dedicados desta sessão (`../gow2-recomp/recomp_macos_e4fix{,2}/`,
  `boot_gow2_e4fix{,2,dbg}`) ficam no checkout físico, fora do git tracking desta
  sessão (nunca commitados por mim em `gow2-recomp`) -- seguem o mesmo padrão que os
  probes `e1probe`/`e3probe` das sessões anteriores.

---
*Phase: 11-a-parede-real-o-boot-n-o-entra-no-loop-principal*
*Plan: 03*
*Completed: 2026-08-04*

## Self-Check: PASSED

- Ficheiros: `games/gow2/recomp_mid_v2/patch_outer_subtag_prune.py`,
  `games/gow2/lift_baseline/PATCH_MIGRATION.tsv`,
  `docs/re_sessions/2026-08-04-E4-fix-verification-parede-4.md`, este SUMMARY.md --
  todos `FOUND`.
- Commits: `7ce01bf`, `3a75977`, `9ebf0d7` -- todos `FOUND` em `git log --oneline --all`.
