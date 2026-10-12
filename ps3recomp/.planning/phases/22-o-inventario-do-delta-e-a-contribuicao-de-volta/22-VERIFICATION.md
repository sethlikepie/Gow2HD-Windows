---
phase: 22-o-inventario-do-delta-e-a-contribuicao-de-volta
verified: 2026-08-12T17:58:18Z
status: passed
score: 4/4 success criteria verified
behavior_unverified: 0
overrides_applied: 0
---

# Phase 22: O inventário do delta e a contribuição de volta — Verification Report

**Phase Goal:** Ter um inventário versionado do delta upstream→nós, por área, com sha do
upstream + âncora `ficheiro:linha` do nosso lado + decisão accionável (UPST-01); e preparar
a nossa heurística de jump tables como contribuição de volta ao upstream (UPST-03).
**Verified:** 2026-08-12T17:58:18Z
**Status:** passed
**Re-verification:** No — initial verification

## Goal Achievement

### Observable Truths (Success Criteria do ROADMAP, todos re-executados nesta verificação)

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | `docs/UPSTREAM_DELTA.tsv` versionado, ≥4 linhas de dados, 5 colunas obrigatórias preenchidas, nenhuma linha sem `ancora_nossa`, vocabulário de decisão exato | ✓ VERIFIED | Ficheiro lido diretamente: 10 linhas de dados (`area`,`sha_upstream`,`ramo_upstream`,`caminho_upstream`,`ancora_nossa`,`ancora_prova`,`decisao`,`fase_destino`,`evidencia`,`razao`). `ancora_nossa` preenchida em todas as 10 linhas. `python3 tools/upstream_delta.py validate --tsv docs/UPSTREAM_DELTA.tsv` → rc=0, `adoptar=3 rejeitar=1 ja-temos=5 contribuir-de-volta=1` (só estes 4 valores usados — `grep -o` nas 10 linhas confirma; DECISOES no código é `["adoptar","rejeitar","já-temos","contribuir-de-volta"]`, com acento — o mesmo termo do ROADMAP, apenas com/sem acento ASCII) |
| 2 | As 4 lacunas motivadoras entram com âncora verificada e sha(s) do upstream: `spu_workload.h:6`, `cellGcmSys.c:615(-616)`, `lv2_register.c:825-826`, `spurs_kernel.c` | ✓ VERIFIED | Todas as 4 linhas presentes no TSV com os shas exigidos (SPUI: `aa3a85a,a88076e,8440ee0`; GCMF: `684dd71,0fcba98,f8ee763`; LV2S: `dd936c4,a984880`; SPRS: `b774c17,76f80b4`). Conteúdo real lido em cada âncora e comparado byte-a-byte com `ancora_prova` do TSV: `spu_workload.h:6` = "We do not interpret SPU ELFs at runtime" ✓; `cellGcmSys.c:616` = `vm_write32(GCM_CTRL_EA + 8, val)` ✓; `lv2_register.c:826` = `vm_write_be32(seg + 0x14, src_ea + p_off)` ✓; `spurs_kernel.c:117` = `static void kernel_loop(void)` ✓. Todos os 12 shas citados resolvem com `git cat-file -e` |
| 3 | Comparação corrida nesta fase contra ramo VIVO do upstream (nunca `upstream/master`), com comando/data/contagem escritos no cabeçalho | ✓ VERIFIED | `git for-each-ref --sort=-committerdate refs/remotes/upstream/` confirma `fix/fold-merge-dropped-fixes` (2026-08-09) é o ramo mais recente. Re-executado eu mesmo: `git rev-list --count HEAD..upstream/fix/fold-merge-dropped-fixes` = **461**, idêntico ao `delta_commits=461` do cabeçalho do TSV. `git rev-list --count HEAD..upstream/master` = **227**, idêntico à nota "esconde 227 commits" do cabeçalho. Cabeçalho traz `ramo_upstream`, `ramo_head_sha=ee97d01`, `ramo_head_data=2026-08-09`, `comando=git rev-list --count HEAD..upstream/fix/fold-merge-dropped-fixes`, `medido_em=2026-08-12`. `validate` recusa `ramo_upstream=upstream/master` (check `RAMO_NAO_MASTER`, confirmado no código) |
| 4 | UPST-03: teste que falha contra a heurística verbatim deles e passa contra a nossa, com controlo e prova por mutação; `tools/ppu_lifter.py` NÃO tocado por esta fase | ✓ VERIFIED | `python3 tools/test_upstream_jt_contrib.py` → rc=0: VECTOR A (discriminador) nosso=`[0x13000,0x13004,0x13008]` deles=`None`; VECTOR B (controlo) ambos=`[0x11100,0x11200]`; mutação reverte a nossa arbitração e VECTOR A fica vermelho (`MUTACAO_OK`), casando exatamente 1x. `tools/upstream_jt_snapshot.py --verify` → rc=0 (sha256 do recorte verbatim confere e é re-extraído do git). Patch `docs/upstream-contrib/0001-*.patch` aplicado com sucesso (`git apply --check` e `git apply`, rc=0) contra checkout limpo de `tools/ppu_lifter.py` no sha gravado `ee97d01…`. `git diff b309bbb~1..HEAD -- tools/ppu_lifter.py` → vazio (confirmado, sem alterações) |

**Score:** 4/4 truths verified (0 present-but-behavior-unverified)

### Required Artifacts

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `docs/UPSTREAM_DELTA.tsv` | inventário versionado, ≥4 linhas | ✓ VERIFIED | 10 linhas de dados, cabeçalho medido, `validate` rc=0 |
| `tools/upstream_delta.py` | measure/validate/rows | ✓ VERIFIED | `validate` executado com sucesso; `DECISOES`/`OBRIGATORIAS`/checks nomeados presentes no código |
| `tools/test_upstream_delta.py` | prova por mutação do gate | ✓ VERIFIED | Registado em `scripts/test_python.sh` e `CMakeLists.txt`; passa dentro de `scripts/test_python.sh` |
| `tools/upstream_jt_snapshot.py` | recorte verbatim do upstream + proveniência | ✓ VERIFIED | `--verify` rc=0, sha256 confere com o git |
| `tools/test_upstream_jt_contrib.py` | vector discriminador + controlo + mutação | ✓ VERIFIED | Executado diretamente, rc=0, os três elementos presentes no output |
| `docs/upstream-contrib/0001-*.patch` | patch que aplica e corrige o lado deles | ✓ VERIFIED | `git apply --check` rc=0 contra `ee97d01:tools/ppu_lifter.py` verbatim |
| `docs/upstream-contrib/README.md` | proveniência + o que preserva | ✓ VERIFIED | Presente, sem marcadores de dívida, disclaimers `in-boot` corretos |

### Key Link Verification

| From | To | Via | Status | Details |
|------|----|----|--------|---------|
| `upstream_delta.py measure` | cabeçalho `#` do TSV | git for-each-ref/rev-list real | ✓ WIRED | Re-medição independente bateu com o ficheiro (461/227) |
| `upstream_delta.py validate` | ficheiros reais do repo | `ancora_nossa` + `ancora_prova` | ✓ WIRED | 4 âncoras motivadoras conferidas manualmente contra o código-fonte |
| `test_upstream_delta.py` / `test_upstream_jt_contrib.py` | CI | `scripts/test_python.sh` (lista `TESTS`) + `CMakeLists.txt` (`test_upstream_delta`, `test_upstream_jt_contrib`) | ✓ WIRED | `grep` confirma presença em ambos os pontos |
| `docs/UPSTREAM_DELTA.tsv` coluna `fase_destino` | Fases 23-26 | referência de shas por área | ✓ WIRED | LV2S→23, SPUI→24, GCMF→25, SPRS→26 presentes |

### Behavioral Spot-Checks

| Behavior | Command | Result | Status |
|----------|---------|--------|--------|
| Validador do inventário reprova sem flags nulas | `python3 tools/upstream_delta.py validate --tsv docs/UPSTREAM_DELTA.tsv` | rc=0, resumo com 10 linhas e contagens por decisão | ✓ PASS |
| Teste de contribuição de volta (3 pernas) | `python3 tools/test_upstream_jt_contrib.py` | rc=0, `DISCRIMINADOR_OK`, `CONTROLO_OK`, `MUTACAO_OK` | ✓ PASS |
| Snapshot verbatim re-confirmado contra o git | `python3 tools/upstream_jt_snapshot.py --verify` | rc=0, sha256 confere, re-extraído de `ee97d01c1668` | ✓ PASS |
| Patch aplica limpo sobre o sha gravado | `git apply --check` num checkout limpo de `ee97d01:tools/ppu_lifter.py` | rc=0 | ✓ PASS |
| `tools/ppu_lifter.py` intocado pela fase | `git diff b309bbb~1..HEAD -- tools/ppu_lifter.py` | saída vazia | ✓ PASS |
| Suite Python completa (uma corrida) | `bash scripts/test_python.sh` (com `PYTHON=.venv/bin/python3`) | PASS 15/SKIP 2/FAIL 2 — as 2 falhas são `test_ppu_lift.py` e `test_ppu_lifter_emit.py`, ambas com `'string.h' file not found` | ✓ PASS (falhas fora do âmbito, ver abaixo) |
| Pré-existência das 2 falhas confirmada por mim (não só citada da SUMMARY) | `git worktree add --detach <tmp> b309bbb` (commit imediatamente anterior ao primeiro commit desta fase) + `PYTHON=.venv/... bash scripts/test_python.sh` | Mesmas 2 falhas, mesma causa raiz (`'string.h' file not found`), no commit anterior à fase | ✓ PASS — confirma pré-existência, não é regressão desta fase |

### Requirements Coverage

| Requirement | Source Plan | Description | Status | Evidence |
|-------------|-------------|-------------|--------|----------|
| UPST-01 | 22-01-PLAN.md | Inventário versionado do delta upstream→nós | ✓ SATISFIED | `docs/UPSTREAM_DELTA.tsv` + `tools/upstream_delta.py`, validado nesta verificação |
| UPST-03 | 22-02-PLAN.md | Heurística de jump tables preparada como contribuição de volta | ✓ SATISFIED | `tools/test_upstream_jt_contrib.py` + `docs/upstream-contrib/`, executado e confirmado nesta verificação |

Ambos os requisitos aparecem em `.planning/REQUIREMENTS.md` mapeados à Fase 22 como "Completo" — confirmado com evidência direta, não apenas citação do documento.

### Anti-Patterns Found

Nenhum. `grep` por `TBD|FIXME|XXX|TODO|HACK|PLACEHOLDER|not yet implemented` nos 5 ficheiros novos/centrais da fase (`tools/upstream_delta.py`, `tools/test_upstream_delta.py`, `tools/upstream_jt_snapshot.py`, `tools/test_upstream_jt_contrib.py`, `docs/upstream-contrib/README.md`) não encontrou nenhum marcador de dívida — só ocorrências inócuas da palavra "TODO" em prosa portuguesa ("todos os casos passaram") e não como marcador de trabalho pendente.

### Disciplina de nível de evidência

`grep -i "in-boot"` em todos os artefactos da fase confirma: todas as ocorrências são disclaimers explícitos ("Nenhum aceite `in-boot` é reclamado", "not `in-boot`") — nenhuma reclamação indevida de evidência `in-boot` encontrada. O nível declarado em todo o lado é `offline-unit` / `medido` (git), consistente com o que foi de facto executado nesta verificação (sem build de `boot_gow2`, sem smoke de intro).

### Falhas fora do âmbito (confirmadas pré-existentes, não bloqueiam a fase)

`bash scripts/test_python.sh` termina com `FAIL: 2` (`tools/test_ppu_lift.py`, `runtime/ppu/tests/test_ppu_lifter_emit.py`), ambas por `include/ps3emu/endian.h:12:10: fatal error: 'string.h' file not found` — problema de toolchain do host (SDK C não expõe `string.h` ao compilador invocado pelos harnesses), não código desta fase. Confirmei eu mesmo, além da citação da SUMMARY: corri a mesma suite num `git worktree` destacado em `b309bbb` (o commit imediatamente anterior ao primeiro commit da Fase 22) e obtive as mesmas 2 falhas com a mesma causa raiz. `git diff b309bbb~1..HEAD` não toca em nenhum ficheiro do caminho de compilação desses harnesses. Registado em `deferred-items.md` da fase — tratado como dívida fora do âmbito, conforme instruído.

### Human Verification Required

Nenhuma. Todos os critérios de sucesso são verificáveis por execução direta de comandos (git, validadores Python, comparação de conteúdo de ficheiros) e foram efetivamente executados nesta verificação — não apenas lidos das SUMMARYs.

### Gaps Summary

Nenhum gap. Os 4 critérios de sucesso do ROADMAP para a Fase 22 foram re-executados e confirmados diretamente no repositório:

1. Inventário TSV existe, 10 linhas (≥4), todas com âncora nossa, vocabulário de decisão exato.
2. As 4 lacunas motivadoras do marco estão presentes com âncora e prova textual verificadas linha-a-linha contra o código real, e shas do upstream que resolvem.
3. A comparação foi re-executada por mim contra o ramo vivo (`upstream/fix/fold-merge-dropped-fixes`, confirmado o mais recente por `committerdate`) e bate exatamente com os números escritos no ficheiro (461 delta / 227 escondidos por `upstream/master`).
4. `tools/test_upstream_jt_contrib.py` passa com as três pernas exigidas (discriminador, controlo, mutação), e `tools/ppu_lifter.py` está comprovadamente intocado por esta fase.

As duas falhas pré-existentes em `scripts/test_python.sh` foram confirmadas — por mim, num worktree no commit anterior à fase — como não causadas por esta fase.

---

_Verified: 2026-08-12T17:58:18Z_
_Verifier: Claude (gsd-verifier)_
