#!/usr/bin/env python3
"""Idempotent markers: CE03C wait-idle + F2B movie re-open (2nd Play wall).

Survives re-lift only as a *check* script — apply edits live in
recomp_macos_v2/ppu_recomp_001.cpp (gitignored). Re-apply by re-running
the bring-up edit if markers go missing after lift.

Estado 2026-07-26 (relift limpo) — VERIFICADOR PURO, CONTINUA A FALHAR
----------------------------------------------------------------------
Este ficheiro nao escreve nada (0 write_text/open('w')): so' confirma
marcadores. Medido contra um lift limpo do ppu_lifter.py actual, com TODOS os
patches disponiveis aplicados (`./apply_all_patches.sh <clone-do-lift-limpo>`):

  marcador                              escritor conhecido
  ------------------------------------  --------------------------------------
  CE03C wait-idle 1st movie             patch_ce03c_introseq_block.py  (OK)
  const uint64_t _sv_r30 = ctx->gpr[30] patch_ce03c_introseq_block.py  (OK)
  strstr(_pt,"movies")                  patch_fios_* _install (OK desde 26-07)
  strstr(_pt,".m2v")                    patch_fios_* _install (OK desde 26-07)
  CE03C idle ok -> Play#2               NENHUM  (e inexistente nos dois lifts)
  CE03C skip Play#2                     NENHUM  (e inexistente nos dois lifts)
  PS3_CE03C_PLAY2                       NENHUM  (e inexistente nos dois lifts)

Correccao factual (2026-07-26): a versao anterior deste cabecalho dizia que os
5 marcadores em falta nao existiam no lift de producao. So' e' verdade para 3.
Contagens medidas, com grep -F:

  marcador                  recomp_macos_v2  recomp_macos_v3  limpo+patches
  ------------------------  ---------------  ---------------  -------------
  strstr(_pt,"movies")            4                0            0 -> 4 (*)
  strstr(_pt,".m2v")              4                0            0 -> 4 (*)
  CE03C idle ok -> Play#2         0                0                0
  CE03C skip Play#2               0                0                0
  PS3_CE03C_PLAY2                 0                0                0

  (*) os dois primeiros nunca estiveram em func_000CE03C: vivem em
      func_002B4274 (pump F2B) e func_0030D5CC (FIOSOPEN / g_f2b_natural_movie_fo).
      Passaram a existir no lift limpo quando os instaladores dos blocos F2B/FIOS
      ganharam dono (patch_fios_f2b_open_block_install.py /
      patch_fios_stream_guards_install.py). Nao eram comportamento do CE03C.

Os 3 que restam descrevem um gate de re-Play condicionado ao path do filme que
foi deliberadamente ABANDONADO: ver notes/2026-07-22-2nd-movie-st620-wall.md,
linha "No `PS3_CE03C_PLAY2` inject gate | absent from CE03C body". Em todo o
repo so' aparecem no texto DESTE ficheiro e dessa nota — nem em
recomp_macos_v2/v3, nem em lift_baseline/injected_*.cpp. O bloco que o
patch_ce03c_introseq_block.py instala hoje e' a variante wait-idle + re-Play
natural, sem esse gate.

Portanto NAO ha' instalador possivel para estes 3: nao existe fonte de verdade
de onde recortar o bloco, e escreve-lo de raiz seria inventar comportamento
(regra 4 do CLAUDE.md). O MISSING e' um resultado CORRECTO, o check NAO foi
enfraquecido e o rc continua != 0. Corrigir o veredicto exige um escritor para
esse gate (ou apagar os 3 marcadores da lista, decisao de produto que nao cabe
a um script de patch).

Unica correccao aplicada em 2026-07-25 (mecanica, nao enfraquece nada):
o script foi escrito para UM ficheiro, mas o apply_all_patches.sh passa um
DIRECTORIO e o resolve_lift_paths() expande-o para os 7 chunks -- o que fazia
o script exigir os 7 marcadores dentro de CADA chunk, condicao impossivel
(cada bloco vive num chunk so'). Passa a exigir cada marcador em ALGUM chunk
do lift, que e' a semantica original com um unico ficheiro.
"""
from pathlib import Path
import sys
from lift_paths import resolve_lift_paths

MARKERS = [
    "CE03C wait-idle 1st movie",
    "CE03C idle ok → Play#2",
    "CE03C skip Play#2",
    "const uint64_t _sv_r30 = ctx->gpr[30]",
    'strstr(_pt,"movies")',
    'strstr(_pt,".m2v")',
    "PS3_CE03C_PLAY2",
]

# Marcadores sem escritor conhecido em todo o repo E inexistentes tanto no lift
# de producao como num lift limpo com todos os patches (ver tabela do
# cabecalho). Listados para diagnostico; NAO sao excluidos do check.
SEM_ESCRITOR = {
    "CE03C idle ok → Play#2",
    "CE03C skip Play#2",
    "PS3_CE03C_PLAY2",
}


def main():
    paths = resolve_lift_paths(sys.argv[1:], "recomp_macos_v2/ppu_recomp_001.cpp")
    onde = {m: [] for m in MARKERS}
    vistos = 0
    for p in paths:
        if not p.exists():
            print(f"skip {p} (inexistente)")
            continue
        vistos += 1
        t = p.read_text(errors="replace")
        for m in MARKERS:
            if m in t:
                onde[m].append(p.name)
    if not vistos:
        print("MISSING: nenhum chunk de lift encontrado")
        return 1
    missing = [m for m in MARKERS if not onde[m]]
    for m in MARKERS:
        if onde[m]:
            print(f"ok: {m} ({', '.join(onde[m])})")
    if missing:
        orfaos = [m for m in missing if m in SEM_ESCRITOR]
        print(f"MISSING ({len(missing)}/{len(MARKERS)}): {missing}")
        if orfaos:
            print(f"  -> sem escritor conhecido no repo (comportamento ausente, "
                  f"nao e' bug de agulha): {orfaos}")
        return 1
    print(f"ok: todos os {len(MARKERS)} marcadores presentes no lift")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
