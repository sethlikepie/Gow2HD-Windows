#!/usr/bin/env python3
"""VERIFICADOR: B71 skip-icallB quando o produto veio de reuse + CB56C prefer real product.

Este ficheiro NAO escreve nada -- so' confirma que os 4 marcadores do
comportamento estao no lift (gitignored, regeneravel):

  g_b71_product_reused ................ func_000B71B8 (flag do reuse do icallA)
  "B71 skip icallB (reuse product" .... func_000B71B8 (salta o re-attach)
  PS3_TYPE15_CB56C .................... func_000CB56C (gate do prefer product)
  was_shell=%d ........................ func_000CB56C (log do shell rejeitado)

Quem os INSTALA (2026-07-25)
----------------------------
`patch_b71_cb56c_reuse_block.py` -- escritor criado nesta leva. Ate' entao os 4
marcadores eram uma edicao MANUAL dentro do lift gitignored, sem escritor
nenhum: num lift limpo com os 74 patches aplicados ficavam a 0/4 e este
verificador falhava para sempre. O verificador continua a verificar; o
comportamento passou a existir.

O sitio do CB56C tem CO-DONO: `patch_type15_cb56c_prefer_product_install.py`
instala o MESMO bloco por outra via (faz upgrade do bloco pequeno de
`patch_type15_cb56c_highbit.py`). Os dois sao guardados pelo marcador
`PS3_TYPE15_CB56C`, logo o segundo a correr fica ALREADY -- nunca ha' insercao
dupla. O `patch_b71_cb56c_reuse_block.py` corre primeiro (ordem alfabetica) por
necessidade: este verificador corre no slot #14 e tem de ver o marcador ja' na
MESMA passagem do apply_all_patches.sh.

Correccao 2026-07-25 (mecanica, NAO enfraquece o check): chunk-fixo
-------------------------------------------------------------------
O `apply_all_patches.sh` passa o DIRECTORIO do lift, e `resolve_lift_paths`
expande-o para TODOS os `ppu_recomp_*.cpp`. A versao anterior exigia os 4
marcadores em CADA chunk -- impossivel, porque as duas funcoes vivem num unico
chunk (hoje o 000, e o numero de chunks passou de 31 para 7 com o lifter novo).
Passa a exigir os 4 marcadores na UNIAO dos chunks e a dizer em qual estao. A
severidade e' a mesma: falta um marcador -> rc=1.

Uso:  patch_b71_skip_icallb_reuse.py [LIFT_DIR_OU_FICHEIRO...]
Contrato: rc=0 se os 4 marcadores existem no lift; rc=1 caso contrario.
"""
from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from lift_paths import resolve_lift_paths            # noqa: E402

MARKERS = [
    "g_b71_product_reused",
    "B71 skip icallB (reuse product",
    "was_shell=%d",
    "PS3_TYPE15_CB56C",
]


def main() -> int:
    paths = resolve_lift_paths(sys.argv[1:], "recomp_macos_v2/ppu_recomp_000.cpp")
    texts: dict[str, str] = {}
    rc = 0
    for p in paths:
        if not p.exists():
            print(f"missing {p}")
            rc = 1
            continue
        texts[p.name] = p.read_text(errors="replace")
    if not texts:
        print("ERRO: nenhum ficheiro de lift legivel", file=sys.stderr)
        return 1

    for m in MARKERS:
        where = [name for name, t in texts.items() if m in t]
        if where:
            print(f"ok: {m} -> {', '.join(sorted(where))}")
        else:
            print(f"MISSING: {m} (ausente nos {len(texts)} chunk(s))")
            rc = 1
    if rc:
        print("  -> instalador: recomp_mid_v2/patch_b71_cb56c_reuse_block.py")
    return rc


if __name__ == "__main__":
    raise SystemExit(main())
