---
phase: 1
slug: emiss-es-do-lifter-e-teste-ouro-do-pre-mbulo
# status lifecycle: draft (seeded by plan-phase) → validated (set by validate-phase §6)
# audit-milestone §5.5 distinguishes NOT-VALIDATED (draft) from PARTIAL (validated + nyquist_compliant: false) (#2117)
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-07-26
---

# Phase 1 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | Nenhum. Self-tests Python com `assert` puro (sem pytest), agregados por CTest |
| **Config file** | `CMakeLists.txt` (raiz) — testes só existem com `-DPS3RECOMP_BUILD_TESTS=ON` (default `OFF`, `CMakeLists.txt:24`) |
| **Quick run command** | `PYTHON=python3.12 python3.12 runtime/ppu/tests/test_ppu_lifter_emit.py` |
| **Full suite command** | `./scripts/test_all.sh` (roda `test_python.sh` + `test_runtime.sh`, exit 0 só se AMBOS passarem) |
| **Estimated runtime** | ~5–10 s para o teste isolado; alguns minutos para a suíte completa (inclui cmake configure + build + ctest) |

**Armadilha de build-type (verificada, `scripts/test_runtime.sh`):** os testes **não** podem
ser configurados com `-DCMAKE_BUILD_TYPE=Release`. Release injeta `-DNDEBUG`, que desativa
silenciosamente `assert()`. `test_runtime.sh` configura com `-DCMAKE_BUILD_TYPE=` (vazio
explícito) para evitar isso.

**Convenção SKIP (relevante para o gate de sync, D-05):** um teste é SKIP quando o exit
code é `0` **E** o stdout contém uma linha com `SKIP:`. O exit code é avaliado PRIMEIRO —
exit ≠ 0 é sempre FAIL, mesmo com `SKIP:` no stdout. Regex do CMake é desancorado
(`PS3RECOMP_SKIP_REGEX = "SKIP:"`, `CMakeLists.txt:296-330`).

---

## Sampling Rate

- **Após cada commit de tarefa:** `python3 runtime/ppu/tests/test_ppu_lifter_emit.py`
- **Após cada wave do plano:** `./scripts/test_python.sh`
- **Antes de `/gsd-verify-work`:** `./scripts/test_all.sh` verde
- **Latência máxima de feedback:** ~10 s (teste isolado)

---

## Per-Task Verification Map

> Preenchido pelo `gsd-planner` em 2026-07-26. Toda tarefa desta fase tem verificação
> **automatizada**: a fase inteira é sobre substituir "descobrir por link quebrado" por
> "descobrir por teste". Nenhuma tarefa depende de Wave 0 — a infraestrutura já existe.
>
> **Interpretador:** use `.venv/bin/python3` (3.14 nesta máquina). O `python3` nu é 3.9.6 e
> não satisfaz o piso `>=3.11` do projeto.

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| 01-01-T1 | 01 | 1 | REQ-RDY0-1 | T-01-01 | Stub degrada com log (`ppu_loader.cpp:2522`), nunca em silêncio | unit | `.venv/bin/python3 runtime/ppu/tests/test_ppu_lifter_emit.py` | ✅ | ⬜ pending |
| 01-01-T2 | 01 | 1 | REQ-RDY0-1 | T-01-02 | Mutação automatizada impede verde vazio | unit | `.venv/bin/python3 runtime/ppu/tests/test_ppu_lifter_emit.py` | ✅ | ⬜ pending |
| 01-02-T1 | 02 | 2 | REQ-RDY0-1 | T-01-03, T-01-04, T-01-05, T-01-06 | Bounds check antes de `memcpy`/CAS; report-only (D-04) | unit | `.venv/bin/python3 runtime/ppu/tests/test_ppu_lifter_emit.py` | ✅ | ⬜ pending |
| 01-02-T2 | 02 | 2 | REQ-RDY0-1 | T-01-03, T-01-04, T-01-05 | Prova em runtime: OOB reporta e não aborta; in-bounds inalterado | unit (compile+run) | `.venv/bin/python3 runtime/ppu/tests/test_ppu_lifter_emit.py` | ✅ | ⬜ pending |
| 01-03-T1 | 03 | 3 | REQ-RDY0-1 | — | Gate G4 com controle negativo (dentes provados) | unit | `.venv/bin/python3 runtime/ppu/tests/test_ppu_lifter_emit.py` | ✅ | ⬜ pending |
| 01-03-T2 | 03 | 3 | REQ-RDY0-1 | T-01-07, T-01-08, T-01-09, T-01-10 | Marcador de pulo não colide com o classificador do CTest | unit | `PS3_SYNC_GATE_SIBLING=/nonexistent/path/build_macos.sh .venv/bin/python3 runtime/ppu/tests/test_ppu_lifter_emit.py` | ✅ | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

`runtime/ppu/tests/test_ppu_lifter_emit.py` já existe (598 linhas) e já está registrado
nos **dois** lugares que precisam concordar — `scripts/test_python.sh:110` e
`CMakeLists.txt:327` (como `py.ppu_lifter_emit`). Por decisão **D-08**, esta fase
**estende** esse arquivo em vez de criar irmãos, o que dispensa fiação nova.

*Infraestrutura existente cobre todos os requisitos da fase — nenhum Wave 0 necessário.*

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Gate de sync `build_macos.sh` com o irmão presente | REQ-RDY0-1 (D-05/D-06) | A CI faz checkout só de `ps3recomp`; o caminho `../gow2-recomp` nunca existe lá, então o ramo não-pulado é inalcançável em CI por construção (verificado em `.github/workflows/ci.yml` durante a pesquisa) | Numa máquina com os dois checkouts lado a lado, rodar o teste e confirmar que o WARN de divergência aparece (a divergência do bloco `host_gow2_factory` existe hoje). **Nota:** o irmão FOI verificado presente nesta máquina em 2026-07-26, portanto o executor da tarefa 01-03-T2 consegue exercitar este ramo. Se por algum motivo não conseguir, deve declarar **não-exercitado** no SUMMARY em vez de afirmar (CLAUDE.md regra 4). |

---

## Validation Sign-Off

- [x] Todas as tarefas têm verify `<automated>` ou dependência de Wave 0 — 6/6 com `<automated>`, zero MISSING
- [x] Continuidade de amostragem: sem 3 tarefas consecutivas sem verify automatizado — nenhuma tarefa sem
- [x] Wave 0 cobre todas as referências MISSING — não há Wave 0; a infraestrutura já existe e já está registrada nos dois sítios
- [x] Sem flags de watch-mode
- [x] Latência de feedback < 10 s (teste isolado; os dois harnesses C++ são a única parte lenta e já eram custo aceito)
- [ ] `nyquist_compliant: true` no frontmatter — a definir por `/gsd-validate-phase` após execução

**Approval:** mapa preenchido pelo `gsd-planner` em 2026-07-26; `status` permanece `draft` até `/gsd-validate-phase`.
