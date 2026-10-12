---
phase: 08-corre-o-na-origem-e-reposi-o-do-boot
plan: 03
subsystem: infra
tags: [bash, ppu-lifter, root-cause, boot-regression, acceptance-gate]

requires:
  - phase: 08-corre-o-na-origem-e-reposi-o-do-boot
    provides: "Veredicto REFUTADA-AMBAS do Plano 08-02 (patches e runtime f6708cb ambos refutados por medição do binário)"
provides:
  - "Estado final honesto da Fase 8: REG-03 NÃO cumprido, REG-04 não aplicável, funil completo de 9 candidatos eliminados"
  - "Reconfirmação de que accept_relift.sh mantém o mesmo veredicto (REJEITADO) antes e depois da fase -- o gate não regrediu"
  - "Próximo passo preciso para a próxima sessão: bissecção automatizada de runtime/+libs/ na janela 8648805..4e815cf"
affects: ["próxima sessão de diagnóstico (fora do âmbito do marco v1.1 tal como planeado)"]

tech-stack:
  added: []
  patterns: []

key-files:
  created:
    - "../gow2-recomp/notes/2026-07-30-fase8-reposicao-final.md"
  modified: []

key-decisions:
  - "Ramo REFUTADA-AMBAS seguido sem hesitação: nenhuma correcção forjada, nenhum ficheiro-fonte editado nesta rodada (patch_*.py, runtime/ppu/ppu_loader.cpp intocados)"
  - "accept_relift.sh recomp_macos_v3 6 (relift completo do zero) reconfirma REJEITADO/rc=1, elo_stopped='2o movie (StartSeq)', idêntico ao estado pré-Fase-8 -- prova de que o gate não regrediu, só que a causa real continua por corrigir"
  - "REG-03 declarado explicitamente NÃO cumprido; REG-04 declarado explicitamente não aplicável (nenhuma causa corrigida)"
  - "Funil completo consolidado: 9 candidatos eliminados (6 da Fase 6 + patches + runtime f6708cb + a própria rejeição reconfirmada), com o próximo passo exacto para a sessão seguinte"

patterns-established: []

requirements-completed: []

coverage:
  - id: D1
    description: "REG-03 medido e declarado explicitamente não cumprido, com evidência do gate GATE-03 (PERNA 4)"
    requirement: "REG-03"
    verification:
      - kind: integration
        ref: "accept_relift.sh recomp_macos_v3 6 -- PERNA 4 FAIL, OK=0/6, elo_stopped='2o movie (StartSeq)', rc=1"
        status: pass
    human_judgment: false
  - id: D2
    description: "REG-04 declarado explicitamente não aplicável (nenhuma causa corrigida nesta fase)"
    requirement: "REG-04"
    verification:
      - kind: manual_procedural
        ref: "notes/2026-07-30-fase8-reposicao-final.md, secção ## REG-04"
        status: pass
    human_judgment: false
  - id: D3
    description: "Funil completo (9 candidatos) e próximo passo preciso documentados para a sessão seguinte"
    requirement: "REG-03"
    verification:
      - kind: manual_procedural
        ref: "notes/2026-07-30-fase8-reposicao-final.md, tabela do funil + secção final"
        status: pass
    human_judgment: true
    rationale: "A qualidade e completude do diagnóstico honesto (nenhuma causa forjada, próximo passo accionável) exige revisão humana antes de encerrar o marco v1.1 sem causa nomeada."

duration: ~25min
completed: 2026-07-31
status: complete
---

# Phase 8 Plan 3: Correção na origem e reposição do boot Summary

**Nenhuma correção aplicada -- REG-03 declarado explicitamente não cumprido, REG-04 não aplicável; `accept_relift.sh` reconfirma o mesmo veredicto (REJEITADO) de antes da fase, provando que o gate não regrediu -- a causa da regressão do 2º `StartSeq` continua por nomear, com um próximo passo preciso para a sessão seguinte.**

## Performance

- **Duration:** ~25 min
- **Started:** 2026-07-31T~08:45:00Z (após o veredicto do 08-02)
- **Completed:** 2026-07-31T13:07:46Z
- **Tasks:** 2
- **Files modified:** 1 (`../gow2-recomp/notes/2026-07-30-fase8-reposicao-final.md`, criado e editado em 2 commits)

## Accomplishments
- Ramo `REFUTADA-AMBAS` do veredicto 08-02 seguido sem forjar nenhuma correção — nenhum `patch_*.py` nem `runtime/ppu/ppu_loader.cpp` foi editado
- `accept_relift.sh recomp_macos_v3 6` (relift completo do zero, `FORCE_REBUILD_LIFT=1`) reconfirmou a rejeição actual: PERNA 4 (GATE-03, chain gate) reprova 0/6, `elo_stopped='2o movie (StartSeq)'`, idêntico ao estado medido antes da Fase 8 (`07-02-SUMMARY.md`)
- REG-03 declarado explicitamente **NÃO cumprido**, sem maquilhagem
- REG-04 declarado explicitamente **não aplicável** (nenhuma causa corrigida nesta fase)
- Funil completo consolidado: 9 candidatos eliminados (3 da Fase 6 sobre o lifter, 2 da Fase 6 sobre a região do código, patches — Fase 8, runtime `f6708cb` — Fase 8, e a própria rejeição actual reconfirmada), com o próximo passo preciso documentado

## Task Commits

1. **Tarefa 1: aplica correcção (ramo REFUTADA-AMBAS: nenhuma) e prova/reconfirma REG-03** - `e2e8864` (docs) — em `../gow2-recomp`
2. **Tarefa 2: declara REG-04 e confirma o veredicto de `accept_relift.sh`** - `9d4678e` (docs) — em `../gow2-recomp`

**Plan metadata:** (commit no monorepo `ps3recomp`, a seguir a esta SUMMARY)

## Files Created/Modified
- `../gow2-recomp/notes/2026-07-30-fase8-reposicao-final.md` - estado final da Fase 8: REG-03 não cumprido, REG-04 não aplicável, funil completo de 9 candidatos, próximo passo preciso

## Decisions Made
- Nenhuma correção forjada — per CLAUDE.md regra 4 ("nunca forjar resultados"), o veredicto `REFUTADA-AMBAS` do Plano 08-02 foi respeitado sem tentar "salvar" a fase com um fix especulativo
- `accept_relift.sh` corrido mais uma vez (relift completo, não uma corrida de triagem) para provar que o gate da Fase 7 continua a funcionar correctamente — reconfirma REJEITADO, não regrediu
- Funil documentado com TODAS as 9 hipóteses eliminadas até agora (não só as duas desta fase), para a próxima sessão não repetir trabalho já feito

## Deviations from Plan

None — plan executado exactamente como escrito para o ramo `REFUTADA-AMBAS`.

## Issues Encountered

- Achado lateral (fora do escopo desta investigação, não corrigido): os CONTADORES do `accept_relift.sh` (D-5.2) também reprovam por conta própria — `orfaos` no lift candidato (`recomp_macos_v3`) = 2, vs 0 na produção. Isto é um achado pré-existente do processo de aceite, não relacionado com a regressão do `2o movie/StartSeq` que esta fase investigou. Registado na nota final, não corrigido (não está em `files_modified` deste plano).

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

**A causa da regressão do 2º `StartSeq` continua não nomeada.** O marco v1.1
("o boot volta ao AUTO_LOAD, e o smoke passa a saber") cumpre o SEGUNDO
objectivo (o smoke agora sabe — Fase 7, `GATE-01`/`GATE-02`/`GATE-03`
provados) mas não o primeiro (o boot não voltou ao `AUTO_LOAD`). Próximo
passo preciso, pronto para uma sessão futura fora do âmbito desta fase:
bissecção automatizada commit a commit da janela exacta `8648805..4e815cf`
(25 Jul 17:53 → 26 Jul 02:59) sobre TODOS os ficheiros de `runtime/`+`libs/`
que mudaram nesse intervalo (não só os dois commits que tocam
`ppu_loader.cpp`, já eliminados), usando `bisect_regression.sh --bin` como
oráculo e `git worktree` isolado por candidato — o padrão de custo baixo
(~1min por candidato) já provado no Plano 08-02.

## Self-Check: PASSED

- FOUND: `../gow2-recomp/notes/2026-07-30-fase8-reposicao-final.md`
- FOUND commit: `e2e8864` (gow2-recomp)
- FOUND commit: `9d4678e` (gow2-recomp)

---
*Phase: 08-corre-o-na-origem-e-reposi-o-do-boot*
*Completed: 2026-07-31*
