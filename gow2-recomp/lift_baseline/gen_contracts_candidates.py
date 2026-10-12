#!/usr/bin/env python3
"""gen_contracts_candidates.py -- deriva candidatos a CONTRACTS.tsv do
cruzamento TAG-MANIFEST (Task 3, 04-04-PLAN.md).

Porque existe
--------------
CONTRACTS.tsv (check_contracts.py) so' cobre a familia OPD por leitura manual
(Task 1/2). Este script amplia a cobertura mecanicamente: cruza os marcadores
TAG de MANIFEST.tsv (o inventario forense do que o lift tem de preservar)
com o texto-fonte de cada patch_*.py. Quando um marcador TAG e' referenciado
LITERALMENTE por UM SO patch, esse patch e' o dono inequivoco do marcador --
um candidato natural a contrato `escopo=global count_ge <marcador> <min_count>`,
reaproveitando o min_count que o MANIFEST.tsv ja tem (nunca inventando um
numero novo).

Um marcador com DOIS OU MAIS donos NAO vira candidato -- a ambiguidade de
posse nao e' resolvida automaticamente (exigiria ler o efeito semantico de
cada patch, fora do orcamento desta task).

So' patches de classe FUNCIONAL (PATCH_CATALOG.tsv) entram no cruzamento --
os PROBE ficam fora do gate (D-4.4), nao precisam de contrato.

Uso:
  gen_contracts_candidates.py PATCH_DIR MANIFEST.tsv PATCH_CATALOG.tsv

Imprime em stdout linhas TSV candidatas (8 colunas, mesmo formato de
CONTRACTS.tsv), para revisao manual e colagem -- NUNCA `>>` cego (Task 3
Passo 4/5 do plano).
"""
from __future__ import annotations

import sys
from pathlib import Path


def load_manifest_tags(manifest_path: Path) -> dict[str, int]:
    """{marcador: min_count} para linhas com tipo=='TAG' em MANIFEST.tsv.

    Marcadores com nota `OBSOLETO:...` (5a coluna opcional) ficam FORA -- o
    proprio MANIFEST.tsv/gen_manifest.py ja os trata como "nunca falha o
    gate" (ex.: [OPDISP], D-3.2: o lifter passou a materializar um switch
    nativo, a fprintf de diagnostico legitimamente deixou de aparecer). Um
    candidato de contrato derivado desse marcador seria um FAIL permanente,
    nao um sinal de regressao -- descoberto nesta task ao verificar
    tag-opdisp contra o lift real (Task 3 Passo 6).
    """
    tags: dict[str, int] = {}
    for line in manifest_path.read_text(encoding="utf-8").splitlines():
        if line.startswith("#") or not line.strip():
            continue
        parts = line.split("\t")
        if len(parts) < 3:
            continue
        marker, kind, n = parts[0], parts[1], parts[2]
        nota = parts[4] if len(parts) > 4 else ""
        if kind == "TAG" and not nota.startswith("OBSOLETO"):
            tags[marker] = int(n)
    return tags


def load_functional_patches(catalog_path: Path) -> set[str]:
    """Nomes dos patches de classe FUNCIONAL em PATCH_CATALOG.tsv."""
    functional: set[str] = set()
    for line in catalog_path.read_text(encoding="utf-8").splitlines():
        if line.startswith("#") or not line.strip():
            continue
        parts = line.split("\t")
        if len(parts) < 3:
            continue
        patch, _escreve, classe = parts[0], parts[1], parts[2]
        if classe == "FUNCIONAL":
            functional.add(patch)
    return functional


def load_patch_sources(patch_dir: Path) -> dict[str, str]:
    """{nome_do_ficheiro: conteudo} para todos os patch_*.py de PATCH_DIR."""
    return {
        p.name: p.read_text(encoding="utf-8", errors="replace")
        for p in sorted(patch_dir.glob("patch_*.py"))
    }


def owners_of(marker: str, patches: dict[str, str]) -> list[str]:
    """Lista de nomes de patch cujo texto-fonte contem `marker` literalmente."""
    return [name for name, src in patches.items() if marker in src]


def candidate_id(marker: str) -> str:
    """[WADLD-ALLOC] -> tag-wadld-alloc."""
    return "tag-" + marker.strip("[]").lower()


def build_candidates(
    patches: dict[str, str], tags: dict[str, int], functional: set[str],
) -> list[tuple[str, str, str, str, str, str, int, str]]:
    """Gera as linhas candidatas (donos unicos, classe FUNCIONAL)."""
    rows: list[tuple[str, str, str, str, str, str, int, str]] = []
    for marker, min_count in sorted(tags.items()):
        owners = owners_of(marker, patches)
        if len(owners) != 1:
            continue
        owner = owners[0]
        if owner not in functional:
            continue
        rows.append((
            candidate_id(marker), owner, "global", "-", "count_ge", marker,
            min_count,
            "marcador TAG com dono unico em MANIFEST.tsv (derivado por gen_contracts_candidates.py)",
        ))
    return rows


def render_tsv(rows: list[tuple[str, str, str, str, str, str, int, str]]) -> str:
    return "\n".join("\t".join(str(c) for c in row) for row in rows)


def main(argv: list[str] | None = None) -> int:
    args = argv if argv is not None else sys.argv[1:]
    if len(args) != 3:
        print(
            "uso: gen_contracts_candidates.py PATCH_DIR MANIFEST.tsv PATCH_CATALOG.tsv",
            file=sys.stderr,
        )
        return 2

    patch_dir = Path(args[0])
    manifest_path = Path(args[1])
    catalog_path = Path(args[2])

    patches = load_patch_sources(patch_dir)
    tags = load_manifest_tags(manifest_path)
    functional = load_functional_patches(catalog_path)

    rows = build_candidates(patches, tags, functional)
    out = render_tsv(rows)
    if out:
        print(out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
