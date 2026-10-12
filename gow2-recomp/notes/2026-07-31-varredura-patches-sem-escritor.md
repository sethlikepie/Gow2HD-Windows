# Varredura: os 5 patches que não escrevem ficheiro — todos explicados

**Data:** 2026-07-31 · Varredura automática de `recomp_mid_v2/patch_*.py` por ausência de
`write_text` / `write` / `open(...,'w')`.

Motivo: esta sessão encontrou **três** patches que descreviam um fix e não o aplicavam
(`patch_fios_f2a_f2b_wad.py`, `patch_2b3d1c_movie_io.py`, e o órfão de `func_002BA9BC`).
Valia a pena saber se havia mais escondidos.

## Resultado: 5 encontrados, nenhum é um quarto caso

| patch | o que é | veredicto |
|---|---|---|
| `patch_fios_f2a_f2b_wad.py` | documental — era a causa da Fase 9 | **conhecido**, já marcado |
| `patch_type15_cb56c_product.py` | verificador | `already: PS3_TYPE15_CB56C presente` ✅ |
| `patch_factory_snap_ty15_pin.py` | verificador | `markers=3/3` ✅ (inclui `ty15_force_pin_freelist`) |
| `patch_b71_skip_icallb_reuse.py` | verificador | 4 marcadores presentes ✅ |
| `patch_ce03c_pre_play_stop.py` | verificador de **comportamento inexistente** | honesto e documentado |

O último merece nota: o próprio ficheiro declara, no docstring, que *"este ficheiro não
escreve nada: só confirma o marcador… este NÃO tem bloco para repor — o comportamento
nunca existiu em lado nenhum"*, com as três medições que o provam. **É o modelo do que os
outros deviam ter sido.**

## O que isto fecha

A varredura das três armadilhas da Fase 10 está completa:

1. marcadores em falta — feita (Fase 10, `inventory_lift_markers.py`)
2. patches em `SKIP` — feita (Fase 9, dois encontrados e corrigidos)
3. **patches sem escritor — feita agora, sem novos casos**

E confirma, de passagem, que o `ty15_force_pin_freelist` (o reparador de free-list do
TYPE15) **está presente no lift** — o que restringe a parede actual: o mecanismo existe,
a questão é se dispara e porque não resolve.

## Sugestão para o catálogo

Um `patch_*.py` que só verifica devia ser distinguível de um que aplica — hoje ambos
aparecem como `patch_*` e o `PATCH_CATALOG.tsv` classifica-os por sufixo do nome. Um campo
`VERIFICADOR` na coluna `classe` tornaria a varredura desnecessária da próxima vez.
