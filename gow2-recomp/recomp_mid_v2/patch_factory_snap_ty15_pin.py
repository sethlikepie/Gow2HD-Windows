#!/usr/bin/env python3
"""Idempotent factory/TYPE15 pin fixes for post-AUTO_LOAD product path.

1) PS3_FACT_SNAP_MAX 32 → 128 + LRU (B71 factory 0x400D6808 was dropped)
2) ty15_force_pin_freelist + call after TYPE15 construct
3) Markers for detection after re-lift

Source of truth lives in recomp_macos_v2 (gitignored). Re-apply after lift.
"""
from pathlib import Path
import sys
from lift_paths import resolve_lift_paths

MARKERS = [
    "PS3_FACT_SNAP_MAX 128",
    "ty15_force_pin_freelist",
    "FORCE freelist pin",
]


# CORRECCAO 2026-07-25 (chunk-fixo, so' no RELATORIO -- o check NAO foi
# enfraquecido):
# Este ficheiro e' um VERIFICADOR PURO (nao escreve nada; grep write_text/open-w
# = 0). O codigo que ele verifica (factory snap LRU + ty15_force_pin_freelist)
# nunca teve script escritor: era edicao manual de sessao dentro do lift
# gitignored. Copia forense em gow2-recomp/lift_baseline/injected_001.cpp e
# extraccao versionada em gow2-recomp/host_gow2_factory.cpp (ainda NAO ligado
# ao build, ver cabecalho desse ficheiro). Logo, num lift limpo os marcadores
# nao existem e este verificador TEM de falhar -- e continua a falhar (rc=1).
# O que se corrige aqui e' so' o efeito do numero de chunks ter mudado (31 -> 7):
# os marcadores vivem todos num unico chunk (MANIFEST.tsv: ppu_recomp_001.cpp),
# por isso avaliar chunk-a-chunk imprimia 6 "MISSING" falsos mesmo quando o
# comportamento estava presente. Passa a avaliar-se a UNIAO dos chunks, com um
# unico veredicto. Mesma severidade: ok exige os 3 marcadores presentes.
def main() -> int:
    paths = resolve_lift_paths(sys.argv[1:], "recomp_macos_v2/ppu_recomp_001.cpp")
    seen: dict[str, str] = {}
    n_files = 0
    rc = 0
    for p in paths:
        if not p.exists():
            print(f"missing {p}")
            rc = 1
            continue
        n_files += 1
        t = p.read_text(errors="replace")
        for m in MARKERS:
            if m in t and m not in seen:
                seen[m] = p.name
    if n_files == 0:
        print("FAILED: nenhum chunk de lift encontrado")
        return 1
    ok = len(seen) == len(MARKERS)
    print(f"{'ok' if ok else 'MISSING'}: lift ({n_files} chunks) "
          f"markers={len(seen)}/{len(MARKERS)}")
    for m in MARKERS:
        print(f"  {'ok  ' if m in seen else 'FALTA'} {m}"
              + (f" ({seen[m]})" if m in seen else ""))
    if not ok:
        rc = 1
        print("  re-apply from session: factory snap LRU + ty15_force_pin_freelist")
        print("  fonte versionada do comportamento: ../host_gow2_factory.cpp"
              " e ../lift_baseline/injected_001.cpp (sem script escritor)")
    return rc


if __name__ == "__main__":
    raise SystemExit(main())
