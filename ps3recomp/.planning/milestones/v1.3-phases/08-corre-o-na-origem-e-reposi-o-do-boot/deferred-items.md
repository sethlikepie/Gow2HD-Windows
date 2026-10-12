# Itens adiados — Fase 8

Descobertas fora do escopo do task actual (nao corrigidas, per SCOPE BOUNDARY do executor).

## 08-01 Tarefa 1

- **`../gow2-recomp/recomp_mid_v2/patch_1856a8_stream_opd.py`** — nao idempotente: ao
  ser reaplicado contra um lift ja' totalmente patched (`recomp_macos_v2`, producao),
  falha com `no ps3_indirect_call decl in 000` (rc!=0), tanto no conjunto de hoje (89
  patches, PATCH_DIR por omissao) como no conjunto filtrado (64, sem PROBE) —
  comportamento IDENTICO em ambos, portanto nao introduzido pelo PS3_PATCH_DIR
  desta fase. Medido em `/tmp/status_default.tsv` e `/tmp/status_test.tsv`
  (2026-07-30).
- **`../gow2-recomp/recomp_mid_v2/patch_icg_ctor_opd.py`** — mesma classe de
  problema (FAILED identico em ambas as corridas, pre-existente).

Nenhum dos dois esta em `files_modified` do Plano 08-01; nao sao a causa da
regressao investigada por esta fase (a pista dos patches e' avaliada pelo DIFF
do lift resultante, Tarefa 2, nao pela contagem de FAILED do runner). Registados
aqui para uma sessao futura de manutencao dos patches, nao bloqueiam a Fase 8.
