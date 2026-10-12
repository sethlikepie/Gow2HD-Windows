---
phase: 07-gate-de-aceite-que-detecta-a-cadeia
plan: 03
subsystem: infra
tags: [python, ppu-lifter, signal-handling, diagnostics, gow2]

requires:
  - phase: 06-diagnostico-da-regressao-ate-ao-codigo
    provides: "commit 09c3850 (patch_29af0_switch_probe.py/patch_cb56c_type_probe.py ja encadeados -- o padrao mecanico reutilizado aqui)"
provides:
  - "os sete patch_*.py restantes (CD498/17ACC/FLIPPATH/PRESENTLOOP/MENUPRESENT/SCHEDARM/POSTTHR) encadeiam SIGTERM em vez de o substituir -- nenhum SUMMARY se perde quando varios probes armam no mesmo binario"
affects: [08-correcao-na-origem]

tech-stack:
  added: []
  patterns:
    - "encadeamento de SIGTERM: typedef de ponteiro de funcao + variavel g_ps3_<prefixo>_prev_sigterm + handler que chama dump_summary() e depois o handler anterior (se != SIG_DFL/SIG_IGN)"
    - "upgrade-in-place: quando o marcador do bloco ja existe no lift baked mas a variavel _prev_sigterm nao, substituicao textual cirurgica (conta ocorrencias, aborta com SystemExit se != esperado) troca so as duas/tres linhas antigas, sem tocar no resto do bloco ja aplicado"

key-files:
  modified:
    - /Users/andrebrumcortezferreira/Documents/PESSOAL/gow2-recomp/recomp_mid_v2/patch_cd498_enter_probe.py
    - /Users/andrebrumcortezferreira/Documents/PESSOAL/gow2-recomp/recomp_mid_v2/patch_17acc_enter_probe.py
    - /Users/andrebrumcortezferreira/Documents/PESSOAL/gow2-recomp/recomp_mid_v2/patch_flip_path_enter_probe.py
    - /Users/andrebrumcortezferreira/Documents/PESSOAL/gow2-recomp/recomp_mid_v2/patch_present_loop_probe.py
    - /Users/andrebrumcortezferreira/Documents/PESSOAL/gow2-recomp/recomp_mid_v2/patch_menu_present_schedule_probe.py
    - /Users/andrebrumcortezferreira/Documents/PESSOAL/gow2-recomp/recomp_mid_v2/patch_sched_arm_probe.py
    - /Users/andrebrumcortezferreira/Documents/PESSOAL/gow2-recomp/recomp_mid_v2/patch_postthr_pc_probe.py

key-decisions:
  - "apply_all_patches.sh recomp_macos_v2 e o unico lift tocado; boot_gow2 de producao NUNCA e sobrescrito -- o binario de teste e sempre boot_gow2_diag02_test"
  - "prova de 'dois probes ao mesmo tempo' da Task 1 (CD498+17ACC) so mostrou 1 dos 2 SUMMARY -- documentado honestamente como achado esperado dado a ORDEM REAL de definicao no ficheiro (CD498 antes de 17ACC), nao um bug do encadeamento; a prova valida e' a dos SETE juntos (Task 2), que passou"

requirements-completed: [DIAG-02]

coverage:
  - id: D1
    description: "Os sete patch_*.py (CD498/17ACC/FLIPPATH/PRESENTLOOP/MENUPRESENT/SCHEDARM/POSTTHR) encadeiam o handler SIGTERM anterior em vez de o substituir"
    requirement: "DIAG-02"
    verification:
      - kind: unit
        ref: "grep -c '_prev_sigterm' em cada um dos 7 patch_*.py (todos >0)"
        status: pass
    human_judgment: false
  - id: D2
    description: "apply_all_patches.sh recomp_macos_v2 aplica o upgrade de facto (UPGRADED, nao ALREADY) nos sete, sem introduzir regressao no gate (UNVERIFIED por delta continua 42/42, lift_parity/MANIFEST/audit_boundaries continuam PASS)"
    requirement: "DIAG-02"
    verification:
      - kind: integration
        ref: "./apply_all_patches.sh recomp_macos_v2 (corrida real, 2x -- Task 1 confirma UPGRADED para CD498, Task 2 confirma UPGRADED para os outros seis)"
        status: pass
    human_judgment: false
  - id: D3
    description: "Um binario de teste isolado (boot_gow2_diag02_test) com os sete gates PS3_TRACE_* activos em simultaneo mostra as sete tags [TAG] SUMMARY distintas no mesmo log; com os gates desligados, zero linhas (no-op)"
    requirement: "DIAG-02"
    verification:
      - kind: e2e
        ref: "boot_gow2_diag02_test EBOOT.ELF com os 7 PS3_TRACE_*=1 por 15s, morto por PID (7 tags distintas confirmadas); mesmo binario sem PS3_TRACE_* por 10s (0 linhas)"
        status: pass
    human_judgment: false

duration: 40min
completed: 2026-07-30
status: complete
---

# Phase 07 Plan 03: Encadeamento dos sete probes de SIGTERM (DIAG-02) Summary

**Os sete `patch_*.py` que competiam por `SIGTERM` no lift agora encadeiam o handler anterior em vez de o `signal()` substituir em silencio -- prova real com os sete gates activos ao mesmo tempo mostra as sete tags `SUMMARY`, nenhuma perdida.**

## Performance

- **Duration:** ~40min
- **Tasks:** 2/2 completos
- **Files modified:** 7 (`patch_*.py`, fonte idempotente -- os `.cpp` gerados ficam fora, gitignored)

## Accomplishments

- `patch_cd498_enter_probe.py` (o mais simples, um so' sitio de `signal()`)
  passou a encadear: `HELPER_BLOCK` de insercao fresca ja' gera o typedef +
  `g_ps3_cd498_prev_sigterm` + handler que chama o anterior; ramo de
  UPGRADE novo em `patch_file()` faz a mesma troca num bloco JA baked
  (o caso real de `recomp_macos_v2`). `apply_all_patches.sh recomp_macos_v2`
  confirmou `UPGRADED` (nao `ALREADY`).
- Os seis restantes (`17ACC` -- 1 sitio; `FLIPPATH`/`PRESENTLOOP`/
  `MENUPRESENT`/`SCHEDARM`/`POSTTHR` -- 2 sitios cada) receberam o MESMO
  padrao mecanico. `apply_all_patches.sh recomp_macos_v2` corrido de novo
  reportou `UPGRADED` para os seis, sem introduzir regressao no gate:
  `UNVERIFIED` por delta continua `42/42` (nenhum achado novo),
  `lift_parity`/`MANIFEST`/`audit_boundaries` continuam `PASS`.
- **Prova real de DIAG-02**: `boot_gow2_diag02_test` (binario de teste
  isolado, `NUNCA` `boot_gow2` de produção), corrido com os SETE gates
  (`PS3_TRACE_CD498/17ACC/FLIPPATH/PRESENTLOOP/MENUPRESENT/SCHEDARM/
  POSTTHR_PC=1`) ao mesmo tempo por 15s e morto por PID (`TERM`→espera 1s→
  `-9`, nunca `pkill -f`), mostrou as **sete** tags `[TAG] SUMMARY`
  distintas no mesmo log — nenhuma se perdeu. Com os gates desligados
  (baseline), **zero** linhas das sete tags — no-op confirmado.

## Task Commits

1. **Task 1: Encadear CD498 (1 sitio) -- provar o padrao de upgrade-in-place num so probe** - `40823b0` (feat)
2. **Task 2: Encadear os restantes seis (1 e 2 sitios) + prova dos sete em simultaneo** - `e3ea7c7` (feat)

## Files Created/Modified
- `recomp_mid_v2/patch_cd498_enter_probe.py` (gow2-recomp) - HELPER_BLOCK encadeado + ramo de UPGRADE em `patch_file()`
- `recomp_mid_v2/patch_17acc_enter_probe.py` (gow2-recomp) - idem, Grupo A (1 sitio)
- `recomp_mid_v2/patch_flip_path_enter_probe.py` (gow2-recomp) - idem, Grupo B (2 sitios)
- `recomp_mid_v2/patch_present_loop_probe.py` (gow2-recomp) - idem, Grupo B (2 sitios)
- `recomp_mid_v2/patch_menu_present_schedule_probe.py` (gow2-recomp) - idem, Grupo B (2 sitios)
- `recomp_mid_v2/patch_sched_arm_probe.py` (gow2-recomp) - idem, Grupo B (2 sitios)
- `recomp_mid_v2/patch_postthr_pc_probe.py` (gow2-recomp) - idem, Grupo B (2 sitios)

## Decisions Made
- `recomp_macos_v2/ppu_recomp_000.cpp` (gerado, gitignored) foi tocado pelo `apply_all_patches.sh` para provar o upgrade, mas nunca commitado -- so' os sete `.py` idempotentes entram no git, sobrevivendo a um re-lift.
- O binario de produção `boot_gow2` nunca foi tocado; todo o build/teste desta fase usou `boot_gow2_diag02_test`.

## Deviations from Plan

### Auto-fixed Issues

Nenhuma correcao automatica de bug foi necessaria -- o padrao mecanico do
commit `09c3850` aplicou-se directamente aos sete ficheiros.

### Achado documentado (nao e' um bug, e' ordem de definicao no ficheiro)

A "prova minima do encadeamento" da Task 1 (`PS3_TRACE_CD498=1
PS3_TRACE_17ACC=1`, com so' o CD498 ja encadeado) mostrou **so' o
`[17ACC] SUMMARY`**, nao os dois. Investigado antes de reportar: no
`ppu_recomp_000.cpp` de `recomp_macos_v2`, o bloco do CD498 aparece
**antes** do bloco do 17ACC (linha 18 vs 122) -- e os construtores C++
correm por ordem de definição no ficheiro (nota já registada nos patches
29AF0/CB56C). Isso significa que o ctor do 17ACC corre **depois** do ctor
do CD498 e, como o 17ACC ainda não estava encadeado nesta Task 1, o seu
`signal()` simples **substituiu** o handler do CD498 sem o chamar --
exactamente o defeito original, ainda presente porque só um dos dois tinha
sido corrigido. Isto não invalida o encadeamento do CD498 (que está correcto
e foi confirmado pelo `grep`/`UPGRADED`); é uma consequência esperada de
testar o encadeamento com uma fracção dos sete probes corrigidos.
A prova que importa -- os **sete** activos em simultâneo, todos encadeados,
Task 2 -- confirmou as sete tags sem perdas, o que é o critério de aceite
real de DIAG-02.

## Issues Encontrados
Ver secção acima -- resolvido ao completar a Task 2 (todos os sete
encadeados), sem necessidade de alterar o desenho.

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness

Os sete probes SIGTERM estão prontos para a Fase 8 (correcão), que pode agora
activar qualquer combinação deles sem perder diagnóstico por competição de
sinal. `recomp_macos_v2` continua com o código antigo dos sete
gitignored até ao próximo `apply_all_patches.sh recomp_macos_v2` real (esta
sessão já correu e confirmou os `UPGRADED`, mas não fez commit do `.cpp`
gerado -- é reproduzível a qualquer momento a partir dos sete `.py`).

---
*Phase: 07-gate-de-aceite-que-detecta-a-cadeia*
*Completed: 2026-07-30*

## Self-Check: PASSED

- FOUND: recomp_mid_v2/patch_cd498_enter_probe.py
- FOUND: recomp_mid_v2/patch_17acc_enter_probe.py
- FOUND: recomp_mid_v2/patch_flip_path_enter_probe.py
- FOUND: recomp_mid_v2/patch_present_loop_probe.py
- FOUND: recomp_mid_v2/patch_menu_present_schedule_probe.py
- FOUND: recomp_mid_v2/patch_sched_arm_probe.py
- FOUND: recomp_mid_v2/patch_postthr_pc_probe.py
- FOUND: commit 40823b0
- FOUND: commit e3ea7c7
