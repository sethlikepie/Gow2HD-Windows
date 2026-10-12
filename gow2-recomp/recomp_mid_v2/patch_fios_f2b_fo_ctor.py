#!/usr/bin/env python3
"""F2B FO via natural guest ctor (0031F1A4 + 00307774) — path hash +0x34.

Replaces hand-stamped FO after movie_io open of /wad/*.wad_ps3.

Natural file_new sequence (func_0030D29C):
  alloc FO → func_0031F1A4(FO) → FO+8=media FO+50=flags
  → func_003044EC(path) → func_00307774(FO+0x30, path)
  → FO+0x30=pathptr FO+0x34=hash → media list link

In-boot 2026-07-21: +34=BBCA40F5 (Lgl) / 8C9CF26A (Perm). Layout matches
natural FO-DUMP. Freelist 0x840000xx after R_Perm DONE still blocks stream
(see notes G4 §20–§21) — FO layout alone is not the remaining wall.

Markers: F2B-FO-CTOR, hash=+34=

Usage: python3 recomp_mid_v2/patch_fios_f2b_fo_ctor.py [recomp_macos_v2]

Estado 2026-07-25 — VERIFICADOR PURO, agora com ESCRITOR
---------------------------------------------------------
Este ficheiro nao escreve nada (0 write_text/open('w')): so' confirma que o
bloco F2B esta' presente no lift (gitignored, regeneravel).

Historico: ate' esta leva o bloco era uma edicao MANUAL de sessao e NENHUM
script versionado o repunha — nem este, nem o dono nominal do bloco maior
(`patch_fios_f2a_f2b_wad.py`, que so' imprime a receita). Num lift limpo com
todos os patches aplicados os marcadores ficavam a 0 e este verificador falhava
para sempre, correctamente: o comportamento nao existia.

O escritor que faltava e' `patch_fios_f2b_fo_block.py` (criado 2026-07-25):
extrai do lift de producao `recomp_macos_v2` o bloco FIOS-F2B-MOVIEIO inteiro
— e' a caixa onde o F2B-FO-CTOR vive — e insere-o em `func_0030D5CC`, ancorado
no trio gerado `0x8001<<16 / r31=r3 / r0|=0x70A`. Corre antes deste ficheiro na
ordem alfabetica do apply_all_patches.sh, por isso o verde aqui e' legitimo e
na mesma passagem.

Dependencias do bloco que continuam orfas (ver o docstring do escritor):
`g_f2b_natural_movie_fo`, `f2b_fo_mfd_put` e `movie_io_pread` sao injeccoes de
preambulo (`lift_baseline/injected_001.cpp`) sem escritor proprio; sem elas o
chunk nao compila. O escritor avisa quais faltam, nunca as esconde.

Correccao 2026-07-25 (mecanica, nao enfraquece nada): chunk-fixo. Abria sempre
ROOT/"ppu_recomp_001.cpp"; o lifter passou de 31 para 7 chunks e nada garante
que o bloco caia nesse. Passa a procurar em todos os ppu_recomp_*.cpp do lift
via resolve_lift_paths(). A logica de decisao e' a mesma, agora com parenteses
explicitos (o `or`/`and` sem parenteses ja' se agrupava assim; so' estava
ilegivel).
"""
from __future__ import annotations

import sys
from pathlib import Path

from lift_paths import resolve_lift_paths

MARKER = "F2B-FO-CTOR"


def main() -> int:
    args = sys.argv[1:] or [str(Path(__file__).resolve().parent.parent / "recomp_macos_v2")]
    paths = [p for p in resolve_lift_paths(args, "recomp_macos_v2") if p.exists()]
    if not paths:
        print(f"missing: nenhum ppu_recomp_*.cpp em {args}")
        return 1
    for target in paths:
        text = target.read_text(errors="replace")
        if MARKER in text or (
            "func_0031F1A4(ctx); DRAIN_TRAMPOLINE(ctx);" in text and "F2B-MOVIEIO" in text
        ):
            # Detect live lift: ctor call inside F2B block
            if "func_00307774(ctx)" in text and "F2B-MOVIEIO" in text:
                print(f"{target.name}: F2B guest ctor already present")
                return 0
    print(f"F2B FO ctor not found em {len(paths)} chunk(s) — re-apply from session or G4 §21")
    print("  -> marcadores F2B-FO-CTOR/F2B-MOVIEIO sem escritor no repo "
          "(edicao manual orfa; comportamento ausente, nao e' bug de agulha)")
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
