#!/usr/bin/env python3
"""order_patches.py -- ordenacao topologica ESTAVEL do corpus de patch_*.py.

Porque existe
-------------
`apply_all_patches.sh` corria os 140 patches pela ordem do glob (alfabetica).
Medido em 2026-08-03 (`games/gow2/notes/2026-08-03-seis-patches-que-nao-reaplicam.md`),
cinco dos seis patches que nao reaplicam a um lift fresco falham SO' por isso: a
agulha de um patch e' texto que outro patch escreve mais tarde no alfabeto. E a
consequencia nao e' cosmetica -- o rebuild medido em
`2026-08-03-rebuild-e2e-medicao.md` mostrou que tres dessas relacoes valem **dois
elos** da cadeia de boot (elo 2 -> elo 4).

A ordem passa a ser DECLARADA em `PATCH_DEPS.tsv` (patch <TAB> depende_de <TAB>
razao) e resolvida aqui. Nao ha ordem hard-coded dispersa pelo shell.

Propriedades garantidas (todas exercitadas em test_order_patches.py)
-------------------------------------------------------------------
1. **Estavel/minimal**: sem relacoes declaradas a saida e' identica a entrada
   (a ordem do glob). Cada relacao move o MINIMO -- e' Kahn com desempate pelo
   indice original, nao um topo-sort qualquer. Uma reordenacao gratuita seria
   uma mudanca de comportamento nao medida em 140 patches.
2. **Ponto fixo**: reordenar uma lista ja ordenada devolve-a inalterada.
3. **Permutacao exacta**: nenhum patch desaparece nem duplica.
4. **Nome ausente = AVISO**: uma relacao que cite um patch que nao existe neste
   host (SKIP_LIST, corpus podado) e' ignorada com aviso no stderr, nunca aborta
   o corpus inteiro.
5. **Ciclo = falha ALTA**: rc=1, com o ciclo nomeado. Nunca se escolhe um lado
   em silencio -- um ciclo significa que duas agulhas se exigem mutuamente, e
   isso e' um facto sobre os patches que tem de ser resolvido a mao.

Uso:
  order_patches.py --deps PATCH_DEPS.tsv < nomes-um-por-linha
  ls patch_*.py | xargs -n1 basename | order_patches.py --deps PATCH_DEPS.tsv

rc=0  ordem escrita no stdout, um nome por linha
rc=1  ciclo de dependencias (nada escrito no stdout)
rc=2  erro de uso (--deps ausente/ilegivel)
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path


def parse_deps(text: str) -> list[tuple[str, str]]:
    """Le o TSV declarativo. Devolve [(patch, depende_de), ...] pela ordem do ficheiro."""
    pares: list[tuple[str, str]] = []
    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        cols = raw.split("\t")
        if len(cols) < 2:
            continue
        patch, dep = cols[0].strip(), cols[1].strip()
        if patch and dep:
            pares.append((patch, dep))
    return pares


def order(names: list[str], deps: list[tuple[str, str]],
          warn=lambda m: None) -> list[str]:
    """Kahn com desempate pelo indice original (topo-sort estavel).

    Levanta ValueError se houver ciclo, com os nomes envolvidos na mensagem.
    """
    idx = {n: i for i, n in enumerate(names)}
    conhecidos = set(idx)

    # arestas dep -> patch  ("dep tem de correr antes de patch")
    sucessores: dict[str, list[str]] = {n: [] for n in names}
    grau: dict[str, int] = {n: 0 for n in names}
    vistas: set[tuple[str, str]] = set()
    for patch, dep in deps:
        faltam = [x for x in (patch, dep) if x not in conhecidos]
        if faltam:
            warn("AVISO: relacao ignorada (patch nao existe neste corpus): "
                 + ", ".join(faltam) + f"  [{patch} depende de {dep}]")
            continue
        if patch == dep:
            warn(f"AVISO: relacao ignorada (auto-dependencia): {patch}")
            continue
        if (dep, patch) in vistas:      # duplicada: nao contar o grau duas vezes
            continue
        vistas.add((dep, patch))
        sucessores[dep].append(patch)
        grau[patch] += 1

    prontos = sorted((n for n in names if grau[n] == 0), key=idx.__getitem__)
    saida: list[str] = []
    while prontos:
        # menor indice original entre os disponiveis -> desvio minimo do glob
        n = prontos.pop(0)
        saida.append(n)
        novos = []
        for s in sucessores[n]:
            grau[s] -= 1
            if grau[s] == 0:
                novos.append(s)
        if novos:
            prontos = sorted(prontos + novos, key=idx.__getitem__)

    if len(saida) != len(names):
        presos = sorted((n for n in names if n not in set(saida)), key=idx.__getitem__)
        raise ValueError("ciclo de dependencias entre: " + ", ".join(presos))
    return saida


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--deps", required=True, help="PATCH_DEPS.tsv declarativo")
    ap.add_argument("--names", default="-",
                    help="ficheiro com um nome por linha (default: - = stdin)")
    args = ap.parse_args(argv)

    deps_path = Path(args.deps)
    if not deps_path.is_file():
        print(f"ERRO: --deps inexistente: {deps_path}", file=sys.stderr)
        return 2
    deps = parse_deps(deps_path.read_text(encoding="utf-8", errors="replace"))

    raw = sys.stdin.read() if args.names == "-" else Path(args.names).read_text()
    names = [ln.strip() for ln in raw.splitlines() if ln.strip()]
    if len(set(names)) != len(names):
        print("ERRO: nomes duplicados na entrada", file=sys.stderr)
        return 2

    try:
        ordered = order(names, deps, warn=lambda m: print(m, file=sys.stderr))
    except ValueError as e:
        print(f"ERRO: {e}", file=sys.stderr)
        return 1

    sys.stdout.write("\n".join(ordered) + ("\n" if ordered else ""))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
