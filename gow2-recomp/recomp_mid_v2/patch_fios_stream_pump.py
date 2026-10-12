#!/usr/bin/env python3
"""F2B stream: freelist tag guard + host STREAM-PUMP after WAD open DONE.

Measured 2026-07-21 Mac arm64:
  - Guest never submits 002B3D1C for R_PermA (freelist desync first).
  - FREELIST-TAG-GUARD in func_00263178: [node+4] with bit31 → abort r3=0
    (stops UNCOMMITTED-HI flood 0x840000xx).
  - ALLOC-NULL-GUARD in 2550C8/2550E8: sub-alloc r3=0 → no stamp@EA0.
  - F2B-STREAM-PUMP after 4274 DONE: rebind container limit/cursor per FO,
    loop movie_io_pread into 0x40080000 until size.
  - Do NOT call ps3_fios_aread_hle(container): it writes STATUS_DONE=0 at
    op+0x08, which on the OPEN container is the live io pointer → 6610
    returns 0x8001070A (G4 §23).
  - In-boot: R_PermA bytes_read=20169344 preads=154 GATE-FORCE full;
    after-6610 ret=0 for Lgl+Perm (with F2B-RESTATUS).

Markers: FREELIST-TAG-GUARD, ALLOC-NULL-GUARD, F2B-STREAM-PUMP
Lift gitignored — re-apply after re-lift from session / G4 §22–§23.

Usage: python3 recomp_mid_v2/patch_fios_stream_pump.py [DIR_DE_LIFT]

ESTADO 2026-07-25 -- VERIFICADOR PURO, ORFAO SEM ESCRITOR
--------------------------------------------------------
Este ficheiro NAO escreve nada (grep write_text/.write(/open-w = 0): so'
confirma marcadores. O comportamento que ele verifica NUNCA teve script
escritor -- foi edicao manual de sessao dentro do lift gitignored:

  grep -l 'ALLOC-NULL-GUARD' recomp_mid_v2/*.py  -> so' este ficheiro
  grep -rl 'ALLOC-NULL-GUARD' <repo>             -> este ficheiro, notes/*.md
                                                    e lift_baseline/MANIFEST.tsv
  recomp_macos_v2 (lift de producao, 31 chunks)  -> presente (edicao manual):
      [FREELIST-TAG-GUARD] x5 em ppu_recomp_000.cpp
      [ALLOC-NULL-GUARD]   x1 em 000 + x1 em 002 (sitios 2550C8 / 2550E8)
      F2B-STREAM-PUMP      x3 em ppu_recomp_001.cpp
  lift limpo do ppu_lifter.py actual (7 chunks)   -> AUSENTE

Logo, num lift limpo estes marcadores NAO existem e este verificador TEM de
falhar (rc=2). Fazer o check passar sem o comportamento existir seria forjar
resultado (regra 4 do CLAUDE.md). O check FICA com a mesma forca: 4 unidades
de prova, limiar ok >= 3.

O que se corrigiu foi so' o "chunk-fixo" (e um falso positivo que ele abria)
-----------------------------------------------------------------------------
1. O script abria "ppu_recomp_000.cpp"/"001"/"002" pelo NOME e devolvia rc=1
   ("missing <path>") se algum nao existisse. O lifter passou de 31 para 7
   chunks e o codigo migra de ficheiro a cada re-lift. Passa a usar
   resolve_lift_paths (aceita DIRECTORIO) e a avaliar a UNIAO dos chunks.
2. Ao passar para a uniao dos chunks, a agulha NUA "FREELIST-TAG-GUARD"
   passaria a casar com um FALSO POSITIVO: patch_ce03c_introseq_block.py
   injecta um corpo que contem o comentario "...so WAD open after intro does
   not FREELIST-TAG-GUARD" (mencao em prosa, nao o guard). Para nao
   ENFRAQUECER o check, as duas agulhas de guard passam a exigir a forma
   ENTRE PARENTESES RECTOS -- `[FREELIST-TAG-GUARD]` / `[ALLOC-NULL-GUARD]` --
   que e' exactamente a forma registada em lift_baseline/MANIFEST.tsv
   (linhas 157 e 176) e a que o lift de producao usa nos fprintf. Isto e'
   MAIS estrito que antes, nunca menos.
3. A dupla entrada de ALLOC-NULL-GUARD (000 e 002) nao pode ser contada duas
   vezes por nome de ficheiro; passa a exigir presenca em >= 2 chunks
   distintos (os dois sitios 2550C8 / 2550E8 do MANIFEST), preservando as
   mesmas 4 unidades de prova do check original sem depender do layout.
"""
from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from lift_paths import resolve_lift_paths  # noqa: E402

# Forma exacta registada em lift_baseline/MANIFEST.tsv (TAG entre [ ]).
TAG_FREELIST = "[FREELIST-TAG-GUARD]"
TAG_ALLOCNULL = "[ALLOC-NULL-GUARD]"
TAG_PUMP = "F2B-STREAM-PUMP"


def main() -> int:
    paths = [p for p in resolve_lift_paths(
        sys.argv[1:], str(Path(__file__).resolve().parent.parent / "recomp_macos_v2"))
        if p.is_file()]
    if not paths:
        print("FAILED: nenhum chunk de lift encontrado")
        return 2

    hits: dict[str, list[str]] = {TAG_FREELIST: [], TAG_ALLOCNULL: [], TAG_PUMP: []}
    for p in paths:
        text = p.read_text(errors="replace")
        for tag in hits:
            if tag in text:
                hits[tag].append(p.name)

    ok = 0
    # 1) freelist tag guard (func_00263178)
    if hits[TAG_FREELIST]:
        print("lift: %s present (%s)" % (TAG_FREELIST, ",".join(hits[TAG_FREELIST])))
        ok += 1
    else:
        print("lift: %s MISSING" % TAG_FREELIST)
    # 2+3) alloc null guard: dois sitios (2550C8 / 2550E8) => >= 2 chunks
    n_alloc = len(hits[TAG_ALLOCNULL])
    if n_alloc >= 1:
        print("lift: %s present (%s)" % (TAG_ALLOCNULL, ",".join(hits[TAG_ALLOCNULL])))
        ok += 1
    else:
        print("lift: %s MISSING" % TAG_ALLOCNULL)
    if n_alloc >= 2:
        print("lift: %s em >=2 chunks (2550C8 + 2550E8)" % TAG_ALLOCNULL)
        ok += 1
    else:
        print("lift: %s 2o sitio MISSING (esperado 2550C8 E 2550E8)" % TAG_ALLOCNULL)
    # 4) host stream pump
    if hits[TAG_PUMP]:
        print("lift: %s present (%s)" % (TAG_PUMP, ",".join(hits[TAG_PUMP])))
        ok += 1
    else:
        print("lift: %s MISSING" % TAG_PUMP)

    if ok < 3:
        print("FAILED: %d/4 unidades de prova em %d chunk(s)." % (ok, len(paths)))
        print("  ORFAO SEM ESCRITOR: nenhum patch_*.py instala FREELIST-TAG-GUARD /")
        print("  ALLOC-NULL-GUARD / F2B-STREAM-PUMP; copia forense do comportamento")
        print("  em ../recomp_macos_v2 (gitignored) e em lift_baseline/MANIFEST.tsv.")
        print("  O check NAO foi enfraquecido -- ver cabecalho deste ficheiro.")
    return 0 if ok >= 3 else 2


if __name__ == "__main__":
    raise SystemExit(main())
