---
phase: 05-smoke-de-equival-ncia-e-promo-o
plan: 01
subsystem: infra
tags: [bash, smoke-test, ppu-lift, macos-arm64, gow2]

requires:
  - phase: 03-04 (patch pipeline / lift regeneration)
    provides: recomp_macos_v3 candidato, apply_all_patches.sh, PATCH_CATALOG.tsv
provides:
  - "smoke_relift_equiv.sh -- script real, executavel, com build automatico (FORCE_REBUILD_LIFT=1), 6 execucoes reais, TSV por corrida, portao >=4-de-6, modo --bin sem rebuild"
  - "mirror games/gow2/smoke_relift_equiv.sh byte-identico"
  - "correcao (encontrada por este plano, aplicada pelo orquestrador) de uma chaveta em falta em recomp_mid_v2/patch_wadld_alloc_probe.py que partia a compilacao de recomp_macos_v3"
affects: [05-02, 05-03, 05-04, 05-05]

tech-stack:
  added: []
  patterns:
    - "smoke com build automatico + FORCE_REBUILD_LIFT=1 (nunca testar binario desactualizado)"
    - "classificacao por assinatura medida (log_lines + allocate) em vez de threshold cego"

key-files:
  created:
    - ../gow2-recomp/smoke_relift_equiv.sh
    - games/gow2/smoke_relift_equiv.sh
  modified: []

key-decisions:
  - "T-05-01 confirmado na pratica: FORCE_REBUILD_LIFT=1 apanhou um bug real (chaveta em falta) que um build incremental escondia ha 3 dias -- prova de que a mitigacao vale o custo do rebuild completo."
  - "O achado (bug em patch_wadld_alloc_probe.py) ficou fora do files_modified deste plano -- reportado ao orquestrador em vez de corrigido por improvisacao; a correcao foi feita e commitada pelo orquestrador (gow2-recomp 6410170, ps3recomp 775831b), fora do escopo desta entrega."
  - "A distincao FLAKE_SUSPEITA vs REGRESSAO_SUSPEITA nao pode ser provada pela corrida real (6/6 saiu OK) -- verificada offline com fixtures sinteticas rotuladas como tal, correndo a mesma logica de extracao/classificacao do script."

requirements-completed: [REQ-RDY0-6]

coverage:
  - id: D1
    description: "smoke_relift_equiv.sh existe e sai rc=0 quando st620>=3 em >=4 de 6 execucoes reais contra recomp_macos_v3, com TSV por corrida"
    requirement: "REQ-RDY0-6"
    verification:
      - kind: manual_procedural
        ref: "./smoke_relift_equiv.sh recomp_macos_v3 6 /tmp/smoke_relift_v3.tsv (corrida real, 6/6 OK, rc=0)"
        status: pass
    human_judgment: false
  - id: D2
    description: "Modo --bin (sem rebuild) reusa o pipeline de execucao/classificacao contra um binario ja compilado"
    verification:
      - kind: manual_procedural
        ref: "./smoke_relift_equiv.sh --bin ./boot_gow2 1 /tmp/smoke_relift_prod_check.tsv (sem mensagem 'a construir', class=OK)"
        status: pass
    human_judgment: false
  - id: D3
    description: "Discriminador FLAKE_SUSPEITA vs REGRESSAO_SUSPEITA classifica corretamente por assinatura medida (log_lines>=1000 + allocate vs <1000 sem allocate), nao por contagem cega"
    verification:
      - kind: unit
        ref: "logica de classificacao do script corrida contra fixtures sinteticas /tmp/fixture_flake.log (1206 linhas, allocate presente) e /tmp/fixture_regr.log (52 linhas, sem allocate) -- ver SUMMARY secao 'Prova do discriminador'"
        status: pass
    human_judgment: false
  - id: D4
    description: "Mirror games/gow2/smoke_relift_equiv.sh identico byte-a-byte ao script real"
    verification:
      - kind: other
        ref: "diff ../gow2-recomp/smoke_relift_equiv.sh games/gow2/smoke_relift_equiv.sh (rc=0, saida vazia)"
        status: pass
    human_judgment: false

duration: ~50min (com uma pausa para checkpoint de decisao do orquestrador)
completed: 2026-07-29
status: complete
---

# Phase 5 Plan 01: smoke_relift_equiv.sh Summary

**Script de smoke com build automatico (FORCE_REBUILD_LIFT=1), 6 execucoes reais contra recomp_macos_v3 (st620=11 em 6/6, rc=0), TSV por corrida, portao >=4-de-6, modo --bin sem rebuild, e discriminador FLAKE/REGRESSAO provado offline com fixtures sinteticas -- descobriu no processo uma chaveta em falta que partia a compilacao do candidato ha 3 dias sem que ninguem soubesse.**

## Performance

- **Duration:** ~50 min (inclui uma pausa para reportar e receber decisao do orquestrador sobre um bloqueio factual)
- **Completed:** 2026-07-29
- **Tasks:** 2/2
- **Files modified:** 2 (`../gow2-recomp/smoke_relift_equiv.sh`, `games/gow2/smoke_relift_equiv.sh`)

## Accomplishments

- `smoke_relift_equiv.sh` criado do zero (não existia antes deste plano) -- entrega #1 da Fase 5 (REQ-RDY0-6, critério 1 do ROADMAP).
- Build automático do binário a partir do lift candidato com `FORCE_REBUILD_LIFT=1` (nunca testa um binário desatualizado, T-05-01) -- e essa mitigação **apanhou um bug real** na primeira corrida (ver Deviations).
- 6 execuções reais contra `recomp_macos_v3`, protocolo G6 completo (kill por PID, `PS3_NO_RSX=1`, `PS3_RSX_BACKEND=trace`, `PS3_ENGINE_ROOT` explícito), TSV por corrida, portão `>=4-de-6` literal.
- Modo `--bin BIN_PATH` para reuso sem rebuild (confirmado contra `../gow2-recomp/boot_gow2` de produção).
- Discriminador `FLAKE_SUSPEITA`/`REGRESSAO_SUSPEITA` codificado e provado offline (a corrida real não gerou nenhum caso dos dois -- 6/6 saiu OK).
- Mirror byte-idêntico em `games/gow2/smoke_relift_equiv.sh`.

## Prova real: 6 execuções contra `recomp_macos_v3`

Comando: `cd ../gow2-recomp && ./smoke_relift_equiv.sh recomp_macos_v3 6 /tmp/smoke_relift_v3.tsv`

Build: `dur=19s objs=7 errors=0` (após o fix da chaveta -- ver Deviations).

| run | st620_max | lifted | modules | imports | log_lines | alloc | class |
|---|---|---|---|---|---|---|---|
| 1 | 11 | 52068 | 13 | 151 | 3826 | 0 | OK |
| 2 | 11 | 52068 | 13 | 151 | 3864 | 0 | OK |
| 3 | 11 | 52068 | 13 | 151 | 3872 | 0 | OK |
| 4 | 11 | 52068 | 13 | 151 | 3864 | 0 | OK |
| 5 | 11 | 52068 | 13 | 151 | 3840 | 0 | OK |
| 6 | 11 | 52068 | 13 | 151 | 3861 | 0 | OK |

Rodapé real: `st620>=3 em 6 de 6 (limiar: 4)`, `orfaos_pos_run: 0`, **rc=0**.

`st620_max=11` em todas as 6 (bem acima do limiar `>=3`); `alloc=0` porque nenhuma corrida ficou presa (o marcador `allocate(0x7D00000)` só é relevante quando `st620<3` -- aqui o boot progrediu normalmente e não precisou de o script recorrer ao discriminador).

## Modo `--bin` (sem rebuild)

Comando: `./smoke_relift_equiv.sh --bin ./boot_gow2 1 /tmp/smoke_relift_prod_check.tsv`

Saída: nenhuma linha `=== a construir ===` (confirma que o build foi saltado), `run1 st620_max=11 lifted=52068 modules=13 imports=151 log_lines=3830 alloc=0 class=OK`.

## Prova do discriminador (offline, com fixtures sintéticas -- MEDIDO vs INFERIDO)

A corrida real (6/6 OK) não exercitou os ramos `FLAKE_SUSPEITA`/`REGRESSAO_SUSPEITA` -- não há como provar isso com uma corrida real que não falhou. Provado em vez disso correndo a **mesma lógica de extração/classificação** (copiada linha a linha do script, não uma reimplementação) contra dois logs fabricados que reproduzem as assinaturas medidas citadas no 05-CONTEXT.md (log saudável-mas-lento ~3180-3236 linhas + `allocate`; log de regressão real ~55 linhas sem `allocate`):

```
fixture_flake.log      st620_max=1 log_lines=1206 alloc=1 -> class=FLAKE_SUSPEITA
fixture_regr.log       st620_max=0 log_lines=52   alloc=0 -> class=REGRESSAO_SUSPEITA
```

Isto **não é** uma corrida de boot real -- é uma verificação unitária/offline da lógica de decisão do script, rotulada como tal (regra "nunca forjar resultados": distinguir sempre provado in-boot vs offline/unit). Confirma que o script distingue os dois casos exatamente pelas duas condições codificadas (`log_lines>=1000 E allocate presente` -> FLAKE; caso contrário -> REGRESSAO), nunca reclassificando nenhum dos dois como `OK`.

## Task Commits

1. **Task 1: uma execução fim-a-fim (build+run+classificação)** - `375d58d` (feat, `gow2-recomp`)
2. **Task 2: loop de 6 execuções, TSV, portão >=4-de-6, mirror** - `7d6303e` (feat, `gow2-recomp`) + `d52bb91` (feat, `ps3recomp`/`games/gow2`, mirror)

**Commits externos (achados durante a Task 1, aplicados pelo orquestrador -- não deste plano, citados para rastreabilidade):**
- `gow2-recomp` `6410170` -- fix da chaveta em falta em `recomp_mid_v2/patch_wadld_alloc_probe.py`
- `ps3recomp` `775831b` -- espelho do fix

## Files Created/Modified

- `../gow2-recomp/smoke_relift_equiv.sh` - script real (153 linhas na Task 1, expandido para ~190 na Task 2): parsing de duas formas (`[LIFT_DIR] [RUNS] [TSV]` / `--bin BIN_PATH [RUNS] [TSV]`), build com `FORCE_REBUILD_LIFT=1`, loop de N corridas com protocolo G6, TSV, portão `>=4-de-6`.
- `games/gow2/smoke_relift_equiv.sh` - mirror byte-idêntico (confirmado por `diff`, rc=0).

## Decisions Made

- **T-05-01 confirmado na prática, não só na teoria:** o `FORCE_REBUILD_LIFT=1` (deliberadamente sempre-ligado, nunca confiando em staleness por mtime) apanhou uma chaveta em falta em `patch_wadld_alloc_probe.py` que quebrava a compilação do chunk `ppu_recomp_002.cpp` de `recomp_macos_v3` desde as 16:47 de 26/jul -- 3 dias sem que ninguém tivesse feito um rebuild completo para notar. O binário antigo (`boot_gow2_v3`, linkado às 01:36 do mesmo dia, antes do patch quebrar) continuava a "funcionar" sobre um chunk que já não compilava -- exatamente o cenário que a mitigação existe para prevenir.
- **Bloqueio reportado, não improvisado:** o ficheiro com o bug (`recomp_mid_v2/patch_wadld_alloc_probe.py`) estava fora do `files_modified` deste plano (só os dois `smoke_relift_equiv.sh`) e pertencia à linhagem de patches da Fase 3. Parei a Task 1 no passo 10, diagnostiquei a causa raiz com precisão (contagem de chavetas ignorando strings/comentários, localizando a linha exata e o patch de origem), e reportei ao orquestrador em vez de corrigir por conta própria. O orquestrador confirmou o diagnóstico de forma independente e aplicou a correção (commits `6410170`/`775831b`), incluindo reparar o `recomp_macos_v3/ppu_recomp_002.cpp` já danificado. Produção (`recomp_macos_v2`) nunca foi afetada -- medido, 0 erros, o patch nunca lhe foi aplicado.
- **Discriminador provado offline, não forçado numa corrida real:** dado que as 6 corridas reais saíram todas `OK`, a distinção `FLAKE_SUSPEITA`/`REGRESSAO_SUSPEITA` foi verificada com fixtures sintéticas rotuladas como tal (não uma corrida de boot), preservando a regra de nunca forjar um resultado de boot que não aconteceu.

## Deviations from Plan

### Achado durante a execução (não uma correção deste plano)

**1. [Bloqueio factual, fora do escopo -- reportado, corrigido pelo orquestrador] Chaveta em falta em `patch_wadld_alloc_probe.py` partia a compilação de `recomp_macos_v3`**
- **Encontrado durante:** Task 1, passo 10 (corrida real fim-a-fim contra `recomp_macos_v3`)
- **Sintoma:** `build_macos.sh` com `FORCE_REBUILD_LIFT=1` falhava com `clang++ error: function definition is not allowed here` a partir da linha 273444 de `ppu_recomp_002.cpp`, repetido até ao `-ferror-limit`.
- **Causa raiz (medida, não inferida):** contagem de chavetas do ficheiro inteiro (ignorando strings/comentários) dava profundidade final 1 (nunca negativa) -- uma abertura sem fecho. Localizado em `func_002BACE8`: o bloco de diagnóstico `[WADLD-ALLOC]` (gated por `PS3_TRACE_TYMAP`, classe `PROBE` em `PATCH_CATALOG.tsv:90`) injetado por `recomp_mid_v2/patch_wadld_alloc_probe.py` abria 3 chavetas (`{ static int on`, `if(on){`, `if(n++<40){`) e só fechava 2 (`fflush(stderr);} }`).
- **Por que não corrigi:** o ficheiro estava fora do `files_modified` deste plano (só os dois `smoke_relift_equiv.sh`) e a correção pertencia à linhagem de patches da Fase 3, não à Fase 5. Segui a instrução explícita "se um passo estiver factualmente errado, pare e reporte -- não improvise".
- **Resolução:** reportado ao orquestrador com diagnóstico completo (causa raiz, cronologia dos mtimes, recomendação de fix de 1 linha). O orquestrador confirmou de forma independente e corrigiu em `gow2-recomp` (`6410170`) e `ps3recomp` (`775831b`), reparando também o `recomp_macos_v3/ppu_recomp_002.cpp` já danificado. Verifiquei de forma independente antes de retomar: `git show` dos dois commits, `clang++ -fsyntax-only` sobre `ppu_recomp_002.cpp` (0 erros, era 20), e a corrida real subsequente (`class=OK`).
- **Impacto:** nenhum no escopo deste plano -- o script (Task 1) comportou-se exatamente como desenhado (recusou prosseguir com um binário não construído, imprimiu erro claro, saiu com o `rc` da build). A descoberta é a prova de que a mitigação T-05-01 vale o custo do rebuild completo.

---

**Total deviations:** 0 correções feitas por este plano (1 bloqueio encontrado, diagnosticado e reportado; corrigido por fora do escopo pelo orquestrador).
**Impact on plan:** Nenhum scope creep -- o script permanece exatamente o que o plano pediu; o achado ficou documentado e a correção foi rastreada por commit externo.

## Issues Encountered

- Ver "Deviations from Plan" acima -- o único issue foi o bloqueio de build em `recomp_macos_v3`, resolvido fora do escopo deste plano.

## User Setup Required

None - nenhuma configuração de serviço externo necessária.

## Next Phase Readiness

- `smoke_relift_equiv.sh` está pronto para ser consumido pelo Plano 05-03 (perna 1 do aceite de três pernas, D-5.1) e pelos Planos 05-04/05-05 via modo `--bin` (medir binários já compilados sem custo de rebuild).
- `ctest --test-dir build-tests`: 22/22 (2 py skipped) -- sem regressão do motor.
- `recomp_macos_v3` está agora limpo (0 erros de compilação) e provado com `class=OK` em 6/6 -- pronto para as pernas 2/3 do aceite (`verify_lift.sh`, `apply_all_patches.sh --status`) do Plano 05-03/05-02.
- Nenhum bloqueio conhecido para os planos seguintes desta fase.

## Self-Check: PASSED

- FOUND: `../gow2-recomp/smoke_relift_equiv.sh`
- FOUND: `games/gow2/smoke_relift_equiv.sh`
- FOUND (gow2-recomp): `375d58d`, `7d6303e`, `6410170`
- FOUND (ps3recomp): `d52bb91`, `775831b`

---
*Phase: 05-smoke-de-equival-ncia-e-promo-o*
*Completed: 2026-07-29*
