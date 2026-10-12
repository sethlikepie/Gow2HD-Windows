#!/usr/bin/env python3
"""F2B open: no re-queue dearch + restatus before 6610 + pump always-on.

Measured 2026-07-21 (Mac arm64, after R_Perm FULL via STREAM-PUMP):

  4274-after-6610 ret=0x8001070A for R_Lgl/R_Perm (m2v ret=0)
  -> 4274 takes func_002B4330 error path
  -> ICALL-BAD ctr=0x2F776164 ("/wad" as OPD) + freelist tag abort
  -> WADLD-BODY=0 (state machine never delivers members)

Root: F2B DONEFORCE then falls into natural success tail that trampolines
to func_0030B058 (push open op to media queue -- required to link op to
container). Dearch rejects /wad/* again and stamps op+44=0x8001070A.
6610 returns that error -> 4274 error path.

Fix: F2B-KEEP-DONE + F2B-RESTATUS + F2B-FO-SIZE + F2B-STREAM-FILL
(WADLD ring type_sys+0x1A8 from movie_io; compact refill)
(clear +44, keep +90). Do NOT skip 0030B058 (poll never sees DONE).

Also: STREAM-PUMP lived inside PS3_TRACE_FIOSOPEN probe (n<8) -- silent
no-op without TRACE; moved out.

Markers (lift gitignored -- re-apply after re-lift):
  F2B-KEEP-DONE, F2B-RESTATUS, F2B-FO-SIZE, F2B-STREAM-FILL, F2B-STREAM-PUMP outside TRACE

Usage: python3 recomp_mid_v2/patch_fios_f2b_open_success.py [DIR_DE_LIFT]

ESTADO 2026-07-25 -- VERIFICADOR PURO, ORFAO SEM ESCRITOR
--------------------------------------------------------
Este ficheiro NAO escreve nada (grep write_text/.write(/open-w = 0): so'
confirma marcadores. O comportamento que ele verifica (F2B-KEEP-DONE,
F2B-RESTATUS, F2B-FO-SIZE, F2B-STREAM-FILL, F2B-STREAM-PUMP) nunca teve script
escritor -- era edicao manual de sessao dentro do lift gitignored:

  grep -l 'F2B-KEEP-DONE' recomp_mid_v2/*.py   -> so' este ficheiro
  grep -rl 'F2B-KEEP-DONE' <repo> (fora do lift) -> so' este ficheiro
  recomp_macos_v2 (lift de producao, 31 chunks) -> presente (edicao manual)
  recomp_macos_v3 / lift limpo (7 chunks)       -> ausente

Logo, num lift limpo estes marcadores NAO existem e este verificador TEM de
falhar. Fazer o check passar sem o comportamento existir seria forjar resultado
(regra 4 do CLAUDE.md). O check FICA como esta': mesma lista de marcadores,
mesmo limiar (ok >= 3 -> rc 0, senao rc 2).

O que se corrigiu aqui foi so' o efeito de "chunk-fixo": o script abria
"ppu_recomp_001.cpp" pelo nome. O lifter passou de 31 para 7 chunks e o codigo
pode nascer noutro ficheiro; passa a aceitar um DIRECTORIO (resolve_lift_paths)
e a avaliar a UNIAO dos chunks, com um unico veredicto -- em vez de um veredicto
preso ao numero/ordem dos chunks.
"""
from __future__ import annotations

import sys
from pathlib import Path

from lift_paths import resolve_lift_paths

DEFAULT_LIFT = str(Path(__file__).resolve().parent.parent / "recomp_macos_v2")

MARKERS = (
    "F2B-KEEP-DONE",
    "F2B-RESTATUS",
    "F2B-STREAM-PUMP",
    "F2B-FO-SIZE",
    "F2B-STREAM-FILL",
)
# Pump must not be nested only under TRACE probe count
NOTE = "NOTE: must run even when PS3_TRACE_FIOSOPEN is off"


def main() -> int:
    paths = [p for p in resolve_lift_paths(sys.argv[1:], DEFAULT_LIFT)]
    seen: dict[str, str] = {}
    n_files = 0
    for p in paths:
        if not p.is_file():
            print(f"missing {p}")
            continue
        n_files += 1
        text = p.read_text(errors="replace")
        for m in MARKERS:
            if m in text and m not in seen:
                seen[m] = p.name
        if NOTE in text and NOTE not in seen:
            seen[NOTE] = p.name
    if n_files == 0:
        print("FAILED: nenhum chunk de lift encontrado")
        return 2

    ok = 0
    for m in MARKERS:
        if m in seen:
            print(f"lift: {m} present ({seen[m]})")
            ok += 1
        else:
            print(f"lift: {m} MISSING")
    if NOTE in seen:
        print(f"lift: STREAM-PUMP outside TRACE (note present, {seen[NOTE]})")
        ok += 1
    else:
        print("lift: STREAM-PUMP TRACE-independence note MISSING")

    if ok < 3:
        print(f"FAILED: {ok}/6 marcadores em {n_files} chunk(s).")
        print("  ORFAO SEM ESCRITOR: nenhum patch_*.py instala o comportamento F2B-*;")
        print("  copia forense do lift de producao em ../recomp_macos_v2 (gitignored).")
        print("  O check NAO foi enfraquecido -- ver cabecalho deste ficheiro.")
    return 0 if ok >= 3 else 2


if __name__ == "__main__":
    raise SystemExit(main())
