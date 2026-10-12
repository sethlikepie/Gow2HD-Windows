# Onboarding Summary

Gerado por `/gsd-onboard` em 2026-07-26. Índice do que o onboarding aprendeu e
qual é o próximo comando.

## Estado do projeto

- `PROJECT.md`: presente (269 linhas)
- `REQUIREMENTS.md`: presente (394 linhas)
- `ROADMAP.md`: presente (213 linhas)
- `STATE.md`: presente (89 linhas)

Todos gerados por `gsd-roadmapper` a partir do intel do ingest, não por
questionário — as decisões vieram dos documentos que o repo já tinha.

## Contexto de codebase

- Repo brownfield: sim
- Prontidão do mapa: `complete`
- Mapa: `.planning/codebase/` (7 documentos, 1.388 linhas) — STACK, ARCHITECTURE,
  STRUCTURE, CONVENTIONS, TESTING, INTEGRATIONS, CONCERNS
- Mapa fast disponível: sim (o completo satisfaz o subconjunto fast)

Produzido por 4 mappers paralelos com varredura de repo inteiro. **302 caminhos
repo-relativos citados nos documentos foram verificados contra o disco.** Cinco
alegações factuais erradas foram corrigidas antes do commit — a mais relevante:
não existe descriptografia de SELF em lugar nenhum do repo, o pipeline pressupõe
ELF já descriptografado na entrada.

## Contexto de documentos

- Candidatos ADR/PRD/SPEC/RFC detectados pela heurística de diretório: 46
- **Efetivamente ingeridos: 44**, via `.planning/ingest-manifest.yaml`

A varredura por convenção encontra 386 arquivos — acima do cap de 50 — dos quais
247 são worktrees obsoletas em `.claude/`. O manifest fixa escopo e tipo; as 342
exclusões estão registradas no rodapé dele, não são cobertura silenciosa.

Distribuição final: 3 ADR / 1 SPEC / 16 PRD / 24 DOC.
Síntese: 63 requisitos com estado — 46 OPEN, 4 PARTIAL, 10 DONE, 2 SUPERSEDED,
1 CONTESTED. **Zero decisões travadas** (nenhum ADR-fonte tem `Status: Accepted`).

Relatório de conflito: `.planning/INGEST-CONFLICTS.md` — 0 BLOCKERS / 5 WARNINGS
/ 16 INFO.

## O que carregar antes de agir

Quatro coisas que não estão óbvias nos artefatos e mudam decisões:

1. **Marco 1 é RDY-0 (destravar o re-lift), 5 fases.** Escolhido por ser
   pré-requisito duro: enquanto estiver aberto, qualquer correção no lifter tem
   efeito **zero** no binário do GoW2, porque o build não re-roda o lifter.

2. **O gate `verify_lift_baseline.sh` está incompleto.** Cobre 1662 linhas (só os
   `injected_*.cpp`), deixando as 131 linhas de `preamble_pure.cpp` fora da rede
   de proteção do re-lift — exatamente o que o RDY-0 existe para impedir. Três
   cifras conflitam: 1603 (plano), 1662 (gate), 1793 (inventário real).

3. **O plano RDY-0 tem premissas desatualizadas**, verificadas no código em
   2026-07-26: `tools/ppu_lifter.py:146` já emite `#include <stdlib.h>` e
   `:378-379` já tem o branch `__APPLE__` com `thread_local`. Só `ppu_stubs` está
   genuinamente ausente. E são **73** `patch_*.py`, não 71.

4. **`REQ-track-g-metal-content-stack` está `[CONTESTED]`** e assim deve
   permanecer: 2 PRDs dizem que o stack Metal M2–M10 está todo GREEN, 2 dizem que
   está aberto. Precedência não resolve (todos são PRD) e desempatar por data
   seria arbitrário. O Marco 5 abre **medindo** o estado real em vez de assumir.

Marcos posteriores ficaram em duas trilhas paralelas, não numa cadeia: Trilha A =
Marcos 2–8 (motor), Trilha B = Marcos 9–10 (port GoW2). O intel põe a parede do
GoW2 fora do escopo do plano do motor, com dono diferente.

## Ressalva de confiabilidade

As regras duras, a aritmética do inventário e as premissas verificáveis do RDY-0
foram auditadas. As 965 linhas dos quatro artefatos de planejamento **não** foram
conferidas linha a linha. Como apareceram defeitos em três dos quatro pontos
testados, trate `ROADMAP.md` e `REQUIREMENTS.md` como rascunho forte — revise os
critérios de aceite de uma fase antes de executá-la.

## Próximo passo recomendado

- `/gsd-manager`

Alternativa direta, se quiser começar a executar já: `/gsd-plan-phase 1`
(Marco 1, Fase 1 — emissões do lifter e teste-ouro do preâmbulo).
