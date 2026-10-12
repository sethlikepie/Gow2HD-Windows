---
phase: 21-endurecimento-metricas-e-fecho
plan: 02
subsystem: lift-verification
tags: [ledger, suite, rc, verify_lift, smoke, midasm, relift, macos-arm64, gow2]
requires:
  - "games/gow2/verify_lift.sh (3+1 passos, oraculo opt-in da Fase 20)"
  - "games/gow2/lift_baseline/ (8 suites + baselines congelados)"
  - "tools/test_*.py (7 suites do marco)"
  - "../gow2-recomp/recomp_macos_v2 (lift de producao) e recomp_macos_v3_midasm (piloto)"
provides:
  - "games/gow2/notes/milestone-2026-08-xenon-re-results.md com a suite T1-T10 e rc REAL por linha"
  - "baseline de smoke re-medido hoje: st620_max=11, OOB=0, 52068 lifted, 0 orfaos"
  - "prova de re-lift do piloto mid-asm re-exercitada (nao citada das Fases 17/19)"
  - "achado: o lift de PRODUCAO nao tem chamadas de mid-asm -- o mecanismo ainda nao esta no caminho do jogador"
affects: [21-03 metricas e handoff, auditoria do marco v1.3]
tech-stack:
  added: []
  patterns:
    - "rc medido e registado; nao-exercitado quando nao foi (G5)"
    - "delta contra baseline congelado, nunca limiar absoluto"
    - "logs de medicao em /tmp, fora dos dois repos (G6)"
key-files:
  created:
    - games/gow2/notes/milestone-2026-08-xenon-re-results.md
  modified: []
decisions:
  - "T6 corrido nas DUAS leituras (T6a switch_tables, T6b lift_parity) para nao haver ambiguidade sobre o que foi medido"
  - "T8b (VERIFY_ORACLE=1) corrido como linha extra apesar de nao ser exigido pelo LEDG-02 -- custou 56 s e fecha a duvida"
  - "T10 re-exercitado hoje em vez de citar as Fases 17/19; as ancoras arquivo:linha da Fase 17 ja nao valiam (o directorio foi re-liftado depois)"
  - "T9 corrido UMA vez, sem retry, com o binario de producao -- e por isso prova nao-regressao da intro, NAO o mid-asm em execucao"
  - "sticky_pub>=1 NAO usado como criterio de aceite: re-medido hoje, nao e observavel neste build"
metrics:
  duration: "~35 min"
  completed: 2026-08-02
  tasks: 3
  files: 1
status: complete
---

# Phase 21 Plano 02: Medição da suite T1–T10 — Summary

**O `rc` era o entregável, e foi medido: 12 linhas corridas de facto, 0 presumidas, 0
declaradas `não-exercitado`.**

## O que foi feito

Criado `games/gow2/notes/milestone-2026-08-xenon-re-results.md` — o sítio único de onde
um auditor re-corre a suite do marco — com comando exacto, `rc` observado, nível de
evidência e caminho do log (`/tmp`, fora dos repos) por cada linha.

## A tabela entregue

| ID | Comando | `rc` | Evidência |
|---|---|---|---|
| T1 | `.venv/bin/python3 tools/test_recomp_config.py` | **0** | `offline-unit` |
| T2 | `.venv/bin/python3 tools/test_midasm_emit.py` | **0** | `offline-unit` |
| T3 | `.venv/bin/python3 tools/test_invalid_instructions.py` | **0** | `offline-unit` |
| T4 | `.venv/bin/python3 tools/test_ps3_analyse_jtables.py` | **0** | `offline-unit` |
| T5 | `.venv/bin/python3 tools/test_weak_override_symbols.py` | **0** | `offline-unit` |
| T6a | `.venv/bin/python3 tools/test_switch_tables.py` | **0** | `offline-unit` |
| T6b | `.venv/bin/python3 tools/test_lift_parity.py` | **0** | `offline-unit` (< 1 s) |
| T7 | as **8** suites de `games/gow2/lift_baseline/test_*.py` | **0 nas 8** | `offline-unit` |
| T8 | `PS3_ENGINE_ROOT=$(pwd) ./games/gow2/verify_lift.sh ../gow2-recomp/recomp_macos_v2` | **0** (59 s) | `offline-unit` |
| T8b | idem com `VERIFY_ORACLE=1` | **0** (56 s) | `offline-unit` |
| T9 | smoke de intro 30 s, kill por PID | `st620_max=11`, `OOB=0`, órfãos=0 | **`in-boot`** |
| T10 | prova de re-lift do piloto mid-asm (`grep`/`nm`) | **0** | `offline-unit` |

**Bónus (não substitui nenhuma linha T):** `tools/test_oracle_manifest.py` → rc=0.

**Interpretador registado:** `.venv/bin/python3` = **3.14.6**. O `python3` do sistema é
**3.9.6**, sem `tomllib` — não serve para T1/T6a.

## O que NÃO é vitória (e por isso está escrito)

1. **O lift de PRODUÇÃO não tem uma única chamada de mid-asm.** `grep -rn 'gow2_midasm'
   ../gow2-recomp/recomp_macos_v2/` → rc=1, 0 linhas. Dos 21 directórios de lift do repo
   de build, só 6 (`recomp_macos_v3_midasm`, `_jt18`, `_mig19`, `_mig19b`, `_mig19c`,
   `_ovr19`) têm o símbolo. Isto **não é regressão** — o marco não promoveu nada de
   propósito —, mas significa que o mid-asm ainda **não** está no caminho que o jogador
   corre. Só apareceu por ter corrido o T10 hoje em vez de citar.
2. **O smoke T9 usa o binário de produção**, logo prova **não-regressão da intro** e
   **não** o mid-asm em execução. O `nm boot_gow2` mostra `T _gow2_midasm_Ce03cWaitIdle`
   (o corpo host está ligado), mas nada no lift o chama.
3. **`sticky` continua não-observável**, re-medido hoje: os três `ps3_fios_sticky_*` de
   `ppu_loader.cpp` não imprimem nada; o único `fprintf` com `sticky` em `runtime/`+`libs/`
   é do overlay VideoToolbox (`movie_vt_metal.m:446`), outra coisa. **O critério
   `sticky_pub>=1` que o `CLAUDE.md` prescreve no checklist pós-merge não é verificável
   neste build.**
4. **`bytes_read` continua não-exercitado**: 0 ocorrências no log, 1 único `[movieio] open`
   (o `gow2.psarc`). O aceite `smoke:` do `patch_fallthrough_2550c8.py` continua por correr.
5. **Só o wait-idle do CE03C foi migrado para mid-asm.** O pad de `setjmp`/`longjmp` do
   mesmo patch é o `[[function_override]]` weak da Fase 19 — mecanismo diferente. Dizer
   «o CE03C está migrado» sem esta distinção seria meia-verdade.
6. **`verify_lift.sh` já estava no caminho de aceitação antes deste marco**
   (`accept_relift.sh:73`). O T8 **confirma** que continua verde; não é entrega nova.

## Proveniência medida

- `LIFT_PROD_SHA256` = `73c8abd2…` — **byte-a-byte igual** ao que a Fase 16 registou no
  cabeçalho de `PATCH_MIGRATION.tsv`. O lift de produção não mudou durante o marco.
- `LIFT_MIDASM_SHA256` = `5a5d9c96…` (`recomp_macos_v3_midasm`).
- As âncoras `arquivo:linha` da Fase 17 para o T10 **já não valiam**
  (`ppu_recomp_002.cpp:19537 → 19700`) porque o directório foi re-liftado nas Fases 18–19 —
  exactamente a razão pela qual o `CLAUDE.md` manda re-verificar âncoras antes de as usar.

## Desvios ao plano

**Nenhum desvio de regra.** Três diferenças de execução, todas declaradas:

1. **Um commit em vez de três.** O plano previa commit por task, mas as três tasks escrevem
   **o mesmo ficheiro único**. Dividir teria criado estados intermédios que nunca
   existiram — o oposto do que este plano pede. Um commit atómico, mensagem em pt-BR,
   caminho explícito.
2. **T8b corrido sem ser exigido** (o plano marca-o opcional). Custou 56 s e fecha a dúvida.
3. **T10 re-exercitado em vez de citado** (o plano permitia citar as Fases 17/19). Foi essa
   decisão que produziu o achado nº 1 acima.

Não foi preciso aplicar nenhuma regra de desvio (1–4): não houve bug, funcionalidade em
falta nem bloqueio. **Nenhuma suite falhou** — e se alguma tivesse falhado, o vermelho ia
para a tabela tal como saiu.

## Known Stubs

Duas secções do results doc ficam **deliberadamente por preencher**, por desenho do plano
(o 21-03 escreve nelas, no mesmo ficheiro, para a auditoria ter um alvo único):

| Secção | Ficheiro | Razão |
|---|---|---|
| `## Métricas antes/depois` | `games/gow2/notes/milestone-2026-08-xenon-re-results.md` | entrega do plano 21-03 (`LEDG-02`, outra metade) |
| `## Handoff` | idem | entrega do plano 21-03 |

A secção `## Ressalvas do marco` está parcialmente escrita: os três itens que **esta**
corrida re-mediu (sticky, `bytes_read`, mid-asm fora da produção) já lá estão; os restantes
do `21-CONTEXT.md` são do 21-03.

## Restrições respeitadas

- Árvore principal, sem worktree. Commit em pt-BR, sem `Co-Authored-By`, path explícito,
  sem `git add -A`.
- Zero dados de jogo, logs de boot ou lifts commitados — todos os logs em `/tmp/m21_*`.
- Nenhum ficheiro proibido tocado (`CLAUDE.md`, `.planning/*` protegidos, `scripts/gsd_plan*.sh`).
- Nenhum ficheiro do 21-01 tocado (`docs/RELIFT_CANONICAL.md`, `games/gow2/README.md`,
  `docs/MACOS_PORT_PLAN.md`).
- Nenhum verbo `state.*` nem `roadmap.update-plan-progress` corrido.
- Produção intacta: nada promovido, `recomp_macos_v2` inalterado (sha256 idêntico ao da Fase 16).
- T9 morto **por PID** (TERM → -9), nunca `pkill -f boot_gow2`. Órfãos pós-run: **0**.

## Commits

| Commit | Mensagem |
|---|---|
| `a2fb1ea` | `docs(21-02): suite T1-T10 com rc medido -- 12 linhas corridas, 0 presumidas` |

## Self-Check: PASSED

- `games/gow2/notes/milestone-2026-08-xenon-re-results.md` — existe em disco
- `.planning/phases/21-endurecimento-metricas-e-fecho/21-02-SUMMARY.md` — existe em disco
- commit `a2fb1ea` — existe em `git log`
- `git diff --diff-filter=D HEAD~1 HEAD` — **vazio** (nenhuma deleção acidental)
