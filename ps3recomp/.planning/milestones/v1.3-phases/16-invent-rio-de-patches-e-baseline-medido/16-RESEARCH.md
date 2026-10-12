# Phase 16 — RESEARCH (snapshot de planeamento)

**Data:** 2026-08-02  
**Modo:** research embutido no planeamento (CONTEXT já existia; sem spawn separado).  
**Fontes:** `16-CONTEXT.md`, `ROADMAP.md` Phase 16, `REQUIREMENTS.md` LEDG-01, plano superpowers M0, artefactos em `games/gow2/lift_baseline/` e `../gow2-recomp/`.

## Achados medidos (não copiados)

| Métrica | Valor | Como |
|---|---|---|
| Patches no corpus de build | **139** | `ls ../gow2-recomp/recomp_mid_v2/patch_*.py \| wc -l` |
| Patches no espelho monorepo | **88** | `ls games/gow2/recomp_mid_v2/patch_*.py \| wc -l` |
| Linhas no catálogo actual | **89** patches classificados (95 linhas ficheiro, com cabeçalho) | `PATCH_CATALOG.tsv` cabeçalho: "89 patches … 64 FUNCIONAL / 25 PROBE" |
| Divergência catálogo↔build | **~50** patches invisíveis ao catálogo | 139 − 89; CONTEXT confirma probes Fases 9–11 |
| Lift de produção | `../gow2-recomp/recomp_macos_v2` | default de `build_macos.sh` e de `apply_all_patches.sh` |
| `verify_lift.sh` | existe, 3 passos por delta | `games/gow2/verify_lift.sh LIFT_DIR` |
| Smoke canónico intro | `env_gow2.sh` + `PS3_NO_RSX=1` + kill por PID | CLAUDE.md; `smoke_m0_baseline.sh` para st620 |

## Mapa de vocabulário (obrigatório no ledger)

O catálogo usa `FUNCIONAL`/`PROBE`. O ledger (LEDG-01 / ROADMAP) usa
`CORRECTNESS`/`PROBE`/`OPD`/`BUILD`/`OBSOLETE`. Mapeamento **derivado**, nunca
editado à mão:

| `PATCH_CATALOG.classe` | `PATCH_CATALOG.subclasse` | `PATCH_MIGRATION.classe` |
|---|---|---|
| `PROBE` | `orfao-sem-fonte` | `OBSOLETE` |
| `PROBE` | qualquer outra | `PROBE` |
| `FUNCIONAL` | `opd` | `OPD` |
| `FUNCIONAL` | qualquer outra | `CORRECTNESS` |

`BUILD` fica reservado para patches que só mexem em build/scripts (hoje: nenhum no
corpus de `recomp_mid_v2` — coluna fica disponível, não forçada).

## Candidatos de alto impacto (âncoras EA verificadas nos scripts)

| patch | EA / função | alvo sugerido (decisão humana na 16-02) |
|---|---|---|
| `patch_fallthrough_2550c8.py` | `0x002550C8` / `func_002550C8` | `midasm` ou fix de lifter |
| `patch_jumptable_2a209c.py` | `0x002A209C` / base JT `0x002A2134` | `switch_table` |
| `patch_jumptable_2b11b8.py` | `0x002B11B8` / base JT `0x002B1228` | `switch_table` |
| `patch_ce03c_introseq_block.py` | `0x000CE03C` / `func_000CE03C` | `midasm` ou `weak` |
| `patch_ce03c_movie_done_reset.py` | `0x000CE03C` | `runtime` / `midasm` |
| `patch_factory_opd_gate.py` | `func_0039E6B4` (+ gate em `func_002B0FB4`) | `weak` / `OPD` |
| `patch_icg_ctor_opd.py` | `func_00329490`, `func_00151248` | `weak` / `OPD` |
| `patch_host_res_inflate.py` | `0x001E7B50` / `func_001E7B50` | **`runtime`** (já HLE parcial — não forçar midasm) |
| `patch_14b1f0_opd.py` | `0x0014B1F0` | `weak` / `OPD` |
| `patch_32e200_opd.py` | `0x0032E200` | `weak` / `OPD` |

## Reutilizáveis (não reimplementar)

- `games/gow2/lift_baseline/gen_catalog.py` + `test_gen_catalog.py`
- `PATCH_CATALOG.tsv`, `CONTRACTS.tsv`, `check_contracts.py`
- `games/gow2/verify_lift.sh`, `accept_relift.sh`, `smoke_m0_baseline.sh`
- `../gow2-recomp/apply_all_patches.sh` (`--status` → `patch/status/classe`)

## Fora de escopo (confirmado)

Migrar patches, apagar órfãos, reconciliar subtree 88↔139, corrigir `verify_lift` vermelho,
runtime/libs/lifter.
