---
phase: 11-a-parede-real-o-boot-n-o-entra-no-loop-principal
plan: 01
subsystem: re-instrumentation
tags: [ppu-lifter, midasm-hook, gow2, wall-4, d-11.2, census-probe]

requires:
  - phase: 11-a-parede-real-o-boot-n-o-entra-no-loop-principal
    provides: "D-11.1 (E2, estatico) -- os tres candidatos de fix (A/B/C) e a linha do desserializador 0x00254C40 que forca low16:=1"
provides:
  - "Contagem medida in-boot de D-11.2 (n=1, 3/3 corridas determinísticas) que decide entre o fix A e o fix B"
  - "Sonda gow2_midasm_OuterWalkSubtagCensus, gated PS3_OUTER_SUBTAG_CENSUS=1, OFF por default, no lift de producao (nao usada em 11-04)"
  - "Ledger docs/re_sessions/2026-08-04-E3-outer-walk-subtag-census.md com previsoes commitadas ANTES da sonda existir e resultados abaixo da linha de corte"
affects: [11-03, 11-04]

tech-stack:
  added: []
  patterns: ["mid-asm hook de censo -- conjunto pequeno de chaves distintas em vez de tabela de agregacao, dump por chamada em vez de periodico por tempo, quando o volume observado e' baixo"]

key-files:
  created:
    - docs/re_sessions/2026-08-04-E3-outer-walk-subtag-census.md
  modified:
    - games/gow2/config/gow2_recomp.toml
    - games/gow2/hooks/gow2_midasm_hooks.h
    - games/gow2/hooks/gow2_midasm_hooks.cpp

key-decisions:
  - "D-11.2 DECISION: B -- a contagem (o discriminador literal) e' exactamente 1 em 3/3 corridas de medicao independentes e deterministicas"
  - "A sonda de controlo C-known (esperava w0=0x40030001 directo) nao reproduziu; a correspondencia com o objecto da E1 apoia-se em endereco heap identico + mesma classe de corrupcao (float lido como ponteiro), nao no valor w0 directo -- ficou registada como inferencia, nao medicao, com o passo de fecho (ler piVar8+4) declarado para quem retomar"
  - "Janela de boot de 25s (default dos smokes curtos do repo) e' curta demais para alcancar o ponto de ancoragem nesta maquina (swap ~9.2GB/10GB); 60-90s bastou em 3/3 corridas"

requirements-completed: [REG-03]

coverage:
  - id: D1
    description: "Contagem D-11.2 medida in-boot (nao inferida) via sonda gated, decidindo entre o fix A e o fix B"
    requirement: "REG-03"
    verification:
      - kind: integration
        ref: "boot_gow2_e3probe EBOOT.ELF (3 corridas de medicao D/E/F, gate PS3_OUTER_SUBTAG_CENSUS=1) -- ver docs/re_sessions/2026-08-04-E3-outer-walk-subtag-census.md secao RESULTADOS"
        status: pass
    human_judgment: true
    rationale: "A identidade exacta do unico objecto contado (se e' o MESMO registo WAD que a E1 mediu) depende de uma inferencia por endereco+classe-de-corrupcao, nao de uma leitura directa de piVar8+4 -- fica declarado como nao-100%-fechado para o 11-03 avaliar antes de escrever o fix B"

duration: 50min
completed: 2026-08-04
status: complete
---

# Phase 11 Plan 01: Sonda de contagem D-11.2 (outer-walk subtag census) Summary

**Mid-asm hook `OuterWalkSubtagCensus` em `func_0041FF70` (0x0042003C), gated e OFF por default, mede in-boot a contagem que o CONTEXT.md exige para decidir entre o fix A (desserializador) e o fix B (walk exterior) -- resultado: n=1 em 3/3 corridas, DECISÃO: B, com uma ressalva escrita para o 11-03.**

## D-11.2 DECISION: B

Sustentada por: contagem determinística de **n=1** em 3 corridas de medição independentes
(D, E, F -- gates de diagnóstico ON + `PS3_OUTER_SUBTAG_CENSUS=1`), mesma chave
(`low16=0x0000 subtag=0x000`), mesmo valor corrompido bit-a-bit (`h_child=0x3F800000`),
endereço do objecto contado (`piVar8=0x4077AD5C`, medido em 2/3 corridas) idêntico ao `h` que a
sonda E1 mediu independentemente para o registo WAD patológico. Controlo C3 (corrida G, mesma
janela de 90s, gate OFF) prova zero mudança de comportamento. Detalhe completo, incluindo a
ressalva do C-known que não reproduziu directamente, em
`docs/re_sessions/2026-08-04-E3-outer-walk-subtag-census.md`.

## Performance

- **Duration:** ~50 min
- **Started:** 2026-08-04T15:45:00Z (aprox.)
- **Completed:** 2026-08-04T16:35:00Z (aprox.)
- **Tasks:** 2/2
- **Files modified:** 4 (3 código + 1 doc, ambos mirrorados para `../gow2-recomp/`)

## Accomplishments
- Âncoras re-verificadas contra o `EBOOT.ELF` real (`tools/show_func.py`) antes de tocar em código -- os quatro checks (flags&0x10, flags&3, has-child, `0x0042003C`) batem byte a byte com o plano.
- Previsões da sonda escritas e **commitadas** (`8e6d1f0`) antes de o hook `OuterWalkSubtagCensus` existir no `.cpp`.
- Hook implementado (`26afb7f`), gated `PS3_OUTER_SUBTAG_CENSUS=1`/OFF-default, leitura pura (sem escrita em memória guest, sem tocar `ctx->gpr`), mirrorado para `../gow2-recomp/{config,hooks}/`.
- Lift dedicado `recomp_macos_e3probe/` + binário `boot_gow2_e3probe` (produção `recomp_macos_v2`/`boot_gow2` intocada, `mtime` confirmado).
- 7 boots (G6: kill por PID, `pgrep` antes/depois de cada, zero órfãos, logs só em `/tmp`) -- 3 de medição deram a mesma contagem determinística.
- Ledger `docs/re_sessions/2026-08-04-E3-outer-walk-subtag-census.md` fechado com o veredicto abaixo da linha de corte, incluindo a ressalva honesta sobre o controlo C-known.

## Task Commits

Each task was committed atomically:

1. **Task 1: Wire the counting probe end-to-end and write the prediction before running** - previsões: `8e6d1f0` (docs) ; código do hook: `26afb7f` (feat)
2. **Task 2: Run the census (>=3 boots, G6 hygiene), decide A or B, write the verdict** - `7bfc88b` (docs -- resultados + veredicto)

**Plan metadata:** (este commit, a seguir)

_Nota: Task 1 gerou dois commits porque a disciplina do RE_PLAYBOOK_GOW2.md (secção 4) exige
as previsões commitadas ANTES do código da sonda existir -- não é um TDD RED/GREEN, é a mesma
regra que a E1 seguiu (commit `af5a6cb` da E1, "anterior à existência da sonda")._

## Files Created/Modified
- `docs/re_sessions/2026-08-04-E3-outer-walk-subtag-census.md` - Ledger da sessão: pergunta, âncoras, previsões (antes de correr), controlos, resultados medidos, veredicto D-11.2
- `games/gow2/config/gow2_recomp.toml` - Quarta entrada `[[midasm_hook]]` da família SONDA E1: `OuterWalkSubtagCensus` @ `0x0042003C`
- `games/gow2/hooks/gow2_midasm_hooks.h` - Declaração `gow2_midasm_OuterWalkSubtagCensus(ppu_context* ctx)`
- `games/gow2/hooks/gow2_midasm_hooks.cpp` - Corpo do hook: leitura read-only, gate `PS3_OUTER_SUBTAG_CENSUS`, conjunto de até 16 chaves distintas, dump por chamada

Todos os três ficheiros de código foram espelhados byte-a-byte para `../gow2-recomp/{config,hooks}/` (o checkout físico com `EBOOT.ELF`/`functions.json`) antes do lift, seguindo o padrão "Espelho" já usado pela Fase 19-03.

## Decisions Made
- **D-11.2 DECISION: B** -- ver acima. A regra de decisão pré-registada (`== 1 => B`, `> 1 => A`) aplicou-se literalmente à contagem medida.
- A sonda C-known, tal como especificada no plano (`w0 == 0x40030001` directo no `OUTER_SUBTAG_MATCH`), **não** reproduziu -- diagnosticado como consequência de o hook ler um campo diferente (`piVar8+0x88`, depois `+4`) do que a E1 leu (`h+4` de dentro do push), não como evidência de um objecto diferente. A correspondência com o objecto da E1 apoia-se em dois factos medidos (endereço heap idêntico; mesma classe de corrupção -- float de matriz lido como ponteiro, já provada pela E1/E2 noutro campo do mesmo objecto) e uma inferência não fechada nesta sessão (o valor de `piVar8+4`, não lido). Documentado como tal, sem forjar o fecho.
- Janela de boot estendida de 25s (default dos smokes curtos) para 60-90s: a corrida C (25s) não alcançou o ponto de ancoragem; D (60s) e E/F (90s) alcançaram todas, de forma consistente. Atribuído à pressão de swap medida na máquina (~9,2GB/10GB), não a um defeito do hook.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Janela de boot de 25s insuficiente para alcançar o ponto de ancoragem**
- **Found during:** Task 2, corrida C (gates ON + censo ON, 25s)
- **Issue:** Zero linhas `OUTER_` (nem sequer `OUTER_SUBTAG_ALIVE`), sugerindo que o ponto de ancoragem não foi alcançado dentro da janela default dos smokes curtos do repositório.
- **Fix:** Aumentada a janela para 60s (corrida D) e 90s (corridas E, F, G) -- todas alcançaram o ponto de forma consistente. `smoke_intro_macos.sh` já documenta `SECS=40` como default e swap pressure como causa conhecida de progresso mais lento; medido nesta sessão `vm.swapusage: used=9474 MB de 10240 MB`.
- **Files modified:** Nenhum ficheiro de código -- apenas o parâmetro de `sleep` das invocações de teste em Bash, não commitado (não é código de produção).
- **Verification:** 3/3 corridas de medição (D, E, F) com janela ≥60s deram resultado idêntico e determinístico.
- **Committed in:** N/A (ajuste de execução de teste, não de código)

---

**Total deviations:** 1 auto-fixed (1 blocking)
**Impact on plan:** Ajuste de timing de execução de teste, sem alteração de código. Não afecta o escopo nem a fidelidade da sonda.

## Issues Encountered

- **Colisão de commit com o agente paralelo do `11-02`.** O primeiro `git commit` do código do hook (toml+h+cpp) falhou com "no changes added to commit": o agente que corre `11-02-PLAN.md` em paralelo, no mesmo checkout git (sem isolamento de worktree), executou o seu próprio `git add`+`git commit` entre o meu `git add` e o meu `git commit`, varrendo os meus ficheiros staged para dentro do commit dele (`470aec4`). Esse agente detectou a contaminação e corrigiu com um novo commit (`7c7a0f8`, sem os meus ficheiros) -- comportamento correcto da parte dele. Recuperado re-adicionando e re-committando apenas os meus 3 ficheiros (`26afb7f`), verificado com `git diff --stat` antes de committar para confirmar que só continha as minhas adições. Nenhum dado foi perdido; nenhuma reescrita de histórico foi feita (regra: nunca `amend`/`reset --hard` fora do fluxo normal).
- **A sonda de controlo C-known não reproduziu o valor esperado.** Ver "Decisions Made" acima -- diagnosticado e documentado honestamente no ledger, não escondido nem forçado.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- **11-03** pode consumir `D-11.2 DECISION: B` directamente desta página, mas deve ler a ressalva completa em `docs/re_sessions/2026-08-04-E3-outer-walk-subtag-census.md` (secção "Veredicto" e "Próximo experimento") antes de escrever o fix -- em particular considerar se vale a pena gastar uma leitura extra (`piVar8+4`) para fechar a inferência de identidade antes de escrever o filtro por subtag em `0x0041FF70`.
- **11-02** (paralelo, testes offline `tools/test_wall4_subtag_fix.py`) já tem `test_outer_walk_subtag_prune_predicate` pronto para dar dentes ao fix B assim que ele existir -- não foi tocado nem re-executado por esta sessão.
- Produção (`recomp_macos_v2`/`boot_gow2`) intocada; nenhum bloqueio conhecido para o `11-03` avançar.

---
*Phase: 11-a-parede-real-o-boot-n-o-entra-no-loop-principal*
*Plan: 01*
*Completed: 2026-08-04*

## Self-Check: PASSED

- Ficheiros: `docs/re_sessions/2026-08-04-E3-outer-walk-subtag-census.md`,
  `games/gow2/config/gow2_recomp.toml`, `games/gow2/hooks/gow2_midasm_hooks.h`,
  `games/gow2/hooks/gow2_midasm_hooks.cpp`, este SUMMARY.md -- todos `FOUND`.
- Commits: `8e6d1f0`, `26afb7f`, `7bfc88b` -- todos `FOUND` em `git log --oneline --all`.
- Binário: `../gow2-recomp/boot_gow2_e3probe` `FOUND`; `nm | grep -c
  gow2_midasm_OuterWalkSubtagCensus` = `1`.
