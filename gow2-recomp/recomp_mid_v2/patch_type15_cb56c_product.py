#!/usr/bin/env python3
"""Idempotent TYPE15 CB56C product experiments (gated PS3_TYPE15_CB56C=1).

VERIFICADOR PURO -- nao escreve nada. Quem instala o comportamento e'
`patch_type15_cb56c_prefer_product_install.py` (corre antes deste por ordem
alfabetica do glob do apply_all_patches.sh).

Estado 2026-07-25
-----------------
Ate' hoje o marcador `PS3_TYPE15_CB56C` era um ORFAO SEM ESCRITOR: existia so'
no lift de producao (recomp_macos_v2, edicao manual de sessao) e nenhum
patch_*.py o repunha depois de um re-lift. Passou a ter escritor; este ficheiro
continua a ser so' o check.

Correccao aplicada hoje (mecanica, "chunk-fixo" -- NAO enfraquece o check):
o script iterava os caminhos devolvidos por resolve_lift_paths e fazia
sys.exit(1) no PRIMEIRO chunk sem o marcador. Com um DIRECTORIO em argv (que
e' o contrato do apply_all_patches.sh) isso exigia o marcador nos 7 chunks,
quando func_000CB56C vive num so'. Medido:

    patch_type15_cb56c_product.py recomp_macos_v2/ppu_recomp_000.cpp -> rc=0
    patch_type15_cb56c_product.py recomp_macos_v2                    -> rc=1
        already: .../ppu_recomp_000.cpp
        NOT present ...: .../ppu_recomp_001.cpp

ou seja, o proprio lift de PRODUCAO -- a referencia forense do comportamento --
reprovava. Era layout, nao comportamento. Passa a avaliar a UNIAO dos chunks
(mesma convencao ja' aplicada a patch_fios_stream_pump.py e outros): o marcador
tem de estar no lift, em pelo menos um chunk. A agulha e o rc de falha (1) sao
os mesmos.
"""
from pathlib import Path
import sys
from lift_paths import resolve_lift_paths

MARKER = "PS3_TYPE15_CB56C"


def main() -> int:
    paths = [Path(p) for p in resolve_lift_paths(
        sys.argv[1:], "recomp_macos_v2/ppu_recomp_000.cpp")]
    hits = []
    n_files = 0
    for ps in paths:
        if not ps.is_file():
            print(f"skip {ps}")
            continue
        n_files += 1
        if MARKER in ps.read_text(errors="replace"):
            hits.append(ps.name)
    if n_files == 0:
        print("FAILED: nenhum chunk de lift legivel")
        return 1
    if hits:
        print(f"already: {MARKER} presente em {', '.join(hits)}")
        return 0
    print(f"NOT present: {MARKER} ausente dos {n_files} chunk(s) do lift.")
    print("  Instalador: recomp_mid_v2/patch_type15_cb56c_prefer_product_install.py")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
