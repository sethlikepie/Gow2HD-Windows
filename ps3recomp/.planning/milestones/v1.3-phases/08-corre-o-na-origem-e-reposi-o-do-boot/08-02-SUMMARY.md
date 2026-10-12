---
phase: 08-corre-o-na-origem-e-reposi-o-do-boot
plan: 02
subsystem: infra
tags: [bash, python, cmake, git-worktree, ppu-loader, spinlock, atomics, bisection]

requires:
  - phase: 08-corre-o-na-origem-e-reposi-o-do-boot
    provides: "Decisão A/B do Plano 08-01 (patches candidato, com confundimento documentado) + o candidato de runtime f6708cb identificado na sessão de planeamento"
provides:
  - "Medição real do binário para os dois candidatos (patches completos, runtime f6708cb) -- ambos REFUTADOS"
  - "patch_diag08_committed_range_revert_test.py: reversão cirúrgica reutilizável do spinlock+atomic de f6708cb, dentro de git worktree isolado"
  - "Veredicto REFUTADA-AMBAS com funil completo (Fase 6 + Fase 8) e próximo passo preciso: bissecção automatizada de runtime/+libs/ na janela 8648805..4e815cf"
affects: ["08-03 (correcção na origem, ramo REFUTADA-AMBAS: reconfirma rejeição, sem correcção forjada)"]

tech-stack:
  added: []
  patterns: ["git worktree isolado + rebuild só da lib runtime + relink puro (reaproveitando .o já compilados) para testar candidatos de runtime a custo baixo (~1min por candidato, não os 30-40min de build completo)"]

key-files:
  created:
    - "../gow2-recomp/patch_diag08_committed_range_revert_test.py"
    - "../gow2-recomp/notes/2026-07-30-fase8-plano02-veredicto.md"
  modified: []

key-decisions:
  - "Ramo patches: recomp_macos_v3ab_hoje (patches completos de hoje) reproduz exactamente a mesma falha que a produção (0/6, elo_stopped='2o movie (StartSeq)'); recomp_macos_v3ab_e6d65a2 (patches completos de e6d65a2) nem chega a compilar (ps3_timebase_now renomeado para ppu_timebase_now, giant-lock decls em falta) -- confirma o confundimento previsto pelo 08-01. Patches REFUTADOS por medição do binário."
  - "Ramo runtime: reversão cirúrgica de f6708cb (6 sítios: 1 lock + 3 unlock + 1 store + 1 load) num git worktree isolado, rebuild só da lib (127 objectos, sem erros) + relink puro contra recomp_macos_v3 já compilado. bisect_regression.sh --bin: REGRESSAO idêntica (startseq=1, thr_end=0, r_perma=0). f6708cb REFUTADO -- reverter não repõe o boot."
  - "Veredicto: REFUTADA-AMBAS. Próximo passo preciso: bissecção automatizada do resto de runtime/+libs/ na janela exacta 8648805..4e815cf (25 Jul 17:53 -> 26 Jul 02:59), commit a commit, bisect_regression.sh --bin como oráculo, git worktree isolado por candidato -- este plano só examinou os dois commits que tocam ppu_loader.cpp; o resto da janela continua por eliminar."
  - "Worktree /tmp/ps3recomp_diag08_rt removido (--force, continha o revert de diagnóstico); lifts/binários intermédios (recomp_macos_v3ab_*, boot_gow2_v3ab_*, boot_gow2_diag08_rt) apagados -- nenhum é a causa confirmada, nada sobrevive para o Plano 08-03 herdar por engano"

patterns-established:
  - "Reversão cirúrgica de commit candidato dentro de git worktree isolado (nunca o checkout principal), com guarda anti-produção e contagem exacta anti-silêncio (mesmo padrão de patch_diag06_147038_revert_test.py)"
  - "Relink puro (sem FORCE_REBUILD_LIFT) reaproveitando .o já compilados de um lift de referência -- reduz o custo de testar um candidato de runtime de 30-40min para ~1min"

requirements-completed: []

coverage:
  - id: D1
    description: "Ramo patches medido pelo binário (não só o diff): recomp_macos_v3ab_hoje via smoke_chain_gate.sh (6 corridas), recomp_macos_v3ab_e6d65a2 tentado e documentado como falha de build"
    requirement: "REG-03"
    verification:
      - kind: integration
        ref: "smoke_chain_gate.sh recomp_macos_v3ab_hoje 6 /tmp/gate_hoje.tsv -- elo_stopped='2o movie (StartSeq)' em 6/6"
        status: pass
    human_judgment: false
  - id: D2
    description: "Candidato de runtime f6708cb testado por reversão cirúrgica isolada + bisect_regression.sh --bin"
    requirement: "REG-03"
    verification:
      - kind: integration
        ref: "bisect_regression.sh --bin ./boot_gow2_diag08_rt -- REGRESSAO (startseq=1, thr_end=0, r_perma=0)"
        status: pass
    human_judgment: false
  - id: D3
    description: "Veredicto explícito REFUTADA-AMBAS com funil completo (Fase 6 + Fase 8) e próximo passo preciso"
    requirement: "REG-03"
    verification:
      - kind: manual_procedural
        ref: "notes/2026-07-30-fase8-plano02-veredicto.md, secção ## Veredicto + tabela do funil"
        status: pass
    human_judgment: true
    rationale: "A qualidade do diagnóstico honesto (nenhuma causa forjada) e a recomendação do próximo passo exigem revisão humana antes do Plano 08-03 decidir como prosseguir."

duration: ~75min (execução activa; sessão teve uma interrupção de watchdog a meio do relink, retomada sem perda de trabalho)
completed: 2026-07-31
status: complete
---

# Phase 8 Plan 2: Mede o binário; candidato de runtime Summary

**Ambos os candidatos refutados por medição real do binário: patches completos de hoje reproduzem a mesma falha da produção (e os de `e6d65a2` nem compilam contra o lift moderno), e a reversão cirúrgica de `f6708cb` (spinlock+atomic em `ppu_register_committed_range`/`vm_uncommitted`) não repõe o boot -- veredicto `REFUTADA-AMBAS` com próximo passo preciso.**

## Performance

- **Duration:** ~75 min de execução activa (sessão pausada por watchdog a meio do relink do worktree de diagnóstico; retomada a partir do ponto exacto, sem refazer trabalho)
- **Started:** 2026-07-30T21:28:51Z
- **Completed:** 2026-07-31T12:47:53Z (relógio de parede inclui a pausa; trabalho activo ~75min)
- **Tasks:** 2
- **Files modified:** 2 (ambos criados, em `../gow2-recomp`)

## Accomplishments
- Ramo patches medido pelo BINÁRIO (não só o diff do 08-01): `recomp_macos_v3ab_hoje` (89 patches completos, `--include-probe`) construído e medido — 0/6, `elo_stopped='2o movie (StartSeq)'`, idêntico à produção. `recomp_macos_v3ab_e6d65a2` (74 patches completos de `e6d65a2`) nem compila — confirma o confundimento já previsto no 08-01 (deriva de forma do lift entre épocas, não mudança de comportamento)
- `patch_diag08_committed_range_revert_test.py` (novo): reversão cirúrgica dos 6 sítios introduzidos por `f6708cb` em `runtime/ppu/ppu_loader.cpp`, dentro de um `git worktree` isolado, com guarda anti-produção e contagem exacta
- Runtime `f6708cb` testado por reversão + rebuild só da lib (127 objectos, sem erros) + relink puro (segundos, reaproveitando `.o` já compilados de `recomp_macos_v3`) + `bisect_regression.sh --bin`: **REGRESSAO idêntica** — reverter não repõe o boot
- Veredicto `REFUTADA-AMBAS` registado com funil completo (3 hipóteses da Fase 6 + 2 desta fase) e próximo passo preciso: bissecção automatizada do resto de `runtime/`+`libs/` na janela exacta `8648805..4e815cf`

## Task Commits

1. **Tarefa 1: mede o binário — confirma/refuta patches, testa candidato de runtime** - `25cbc0b` (feat) — em `../gow2-recomp`
2. **Tarefa 2: isola a causa e escreve o veredicto final** - `6126597` (docs) — em `../gow2-recomp`

**Plan metadata:** (commit no monorepo `ps3recomp`, a seguir a esta SUMMARY)

## Files Created/Modified
- `../gow2-recomp/patch_diag08_committed_range_revert_test.py` - reversão cirúrgica dos 6 sítios de `f6708cb` (spinlock + atomics), guarda anti-produção
- `../gow2-recomp/notes/2026-07-30-fase8-plano02-veredicto.md` - veredicto `REFUTADA-AMBAS`, funil completo, próximo passo

## Decisions Made
- Ramo patches: `hoje` completo reproduz a falha da produção; `e6d65a2` completo não compila (erros de nome não relacionados com o timing da regressão) — tratado como "sem evidência de binário a favor da pista dos patches", per a orientação do próprio 08-01 para tratar este cenário como neutro
- Ramo runtime: reversão isolada de `f6708cb` não muda o resultado — `REGRESSAO` idêntica antes/depois
- Veredicto final: `REFUTADA-AMBAS`, com o funil completo (Fase 6 + Fase 8) e o próximo passo exacto (bissecção da janela `8648805..4e815cf`)
- Limpeza: worktree de diagnóstico removido (`--force`), lifts/binários intermédios apagados — nada sobrevive para o Plano 08-03 herdar por engano

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] `.venv` não existe dentro do `git worktree` de diagnóstico, quebrando o relink**
- **Found during:** Tarefa 1 (relink de `boot_gow2_diag08_rt`)
- **Issue:** `build_macos.sh` invoca `$PS3_ENGINE_ROOT/.venv/bin/python` para `gen_hle_nids.py`; o `git worktree` não inclui `.venv` (não é versionado em git), causando `No such file or directory`.
- **Fix:** Symlink `/tmp/ps3recomp_diag08_rt/.venv -> .../ps3recomp/.venv` — não altera nenhum ficheiro-fonte, só o ambiente de invocação do worktree de diagnóstico (descartável).
- **Files modified:** nenhum ficheiro versionado (symlink em `/tmp`, removido com o worktree)
- **Verification:** Relink completou com sucesso após o symlink
- **Committed in:** N/A (artefacto de `/tmp`, não commitado)

---

**Total deviations:** 1 auto-corrigido (Rule 3 — ambiente de build, não código)
**Impact on plan:** Nenhum impacto na correcção da medição — o fix é puramente de ambiente (resolver um caminho de interpretador Python), não altera nenhuma lógica testada.

## Issues Encountered

- **Interrupção de watchdog a meio da execução:** o comando de relink síncrono excedeu o tempo sem progresso visível reportado (o `build_macos.sh` estava a correr normalmente em background, mas o comando bash síncrono ficou bloqueado à espera). Retomado exactamente do ponto indicado (relink contra `recomp_macos_v3`), sem refazer nenhum trabalho anterior (worktree, reversão e build da lib runtime já estavam intactos e correctos). Lição aplicada: builds/relinks subsequentes foram lançados com `nohup ... &` + `disown` e sondados com loops `until` de baixa frequência, em vez de comandos síncronos longos.
- **`ctest` deixou ficheiros gerados modificados no `ps3recomp`** (`runtime/spu/tests/out_*/spu_recomp.{c,h}`) — efeito colateral conhecido (`CMakeLists.txt:406`), corrigido externamente pelo orquestrador antes da retoma (`git checkout --`); confirmado limpo antes de commitar.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

O Plano 08-03 lê `## Veredicto: REFUTADA-AMBAS` e, per o próprio plano, **não aplica nenhuma correcção** nesta rodada — reconfirma a rejeição actual (`accept_relift.sh`) e entrega o estado final honesto da Fase 8, incluindo REG-04 declarado "não aplicável — nenhuma causa foi corrigida nesta fase". O próximo passo real (bissecção automatizada de `runtime/`+`libs/` na janela `8648805..4e815cf`) fica para uma sessão futura, fora do âmbito do Plano 08-03 tal como escrito.

## Self-Check: PASSED

- FOUND: `../gow2-recomp/patch_diag08_committed_range_revert_test.py`
- FOUND: `../gow2-recomp/notes/2026-07-30-fase8-plano02-veredicto.md`
- FOUND commit: `25cbc0b` (gow2-recomp)
- FOUND commit: `6126597` (gow2-recomp)

---
*Phase: 08-corre-o-na-origem-e-reposi-o-do-boot*
*Completed: 2026-07-31*
