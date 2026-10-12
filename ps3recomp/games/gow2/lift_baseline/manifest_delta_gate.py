#!/usr/bin/env python3
"""manifest_delta_gate.py -- interpreta o passo MANIFEST de verify_lift.sh por DELTA (D-5.1).

Porque existe (05-CONTEXT.md, 05-02-PLAN.md Task 1)
-----------------------------------------------------
`gen_manifest.py --verify` (Fase 2/3) ja' suporta `GRUPO:<id>` (soma de
found/min_count, D-3.1c) e `OBSOLETO:<razao>` (T-03-08), mas continua a somar
contra um limiar ABSOLUTO -- o `MANIFEST.tsv` congelado no lift de referencia.
Isso torna o passo MANIFEST de verify_lift.sh cronicamente vermelho: o grupo
`opd-dispatch` tem um deficit de 36 (esperado>=15847, encontrado=15811) que e'
divida JA' DECLARADA e medida da Fase 3 (04-03-SUMMARY.md deixou este gap
explicitamente em aberto: "generalizar seria trabalho redundante fora do
escopo desta task"), nao uma regressao nova. Um gate que nasce sempre
vermelho e' um gate que ninguem le.

`baseline_delta.py` (D-4.5, Fase 4) ja' resolve exatamente este problema para
`lift_parity`/`audit_boundaries`: compara CURRENT vs um BASELINE congelado e
falha SO' por achado NOVO, nunca por achado resolvido (melhoria). Este script
e' o conversor que falta para estender o mesmo padrao ao MANIFEST -- le' a
saida ja' estavel de `gen_manifest.py --verify`, converte-a em IDs opacos, e
delega a comparacao a `baseline_delta.py` (invocado como subprocess, NUNCA
importado nem modificado -- reuso, nao reimplementacao).

A licao do deficit-36-nao-visto: sem magnitude (so' presenca/ausencia de um
id), uma piora do MESMO grupo (deficit crescer de 36 para 50) nao apareceria
como "novo" -- por isso os ids de grupo sao SINTETICOS POR UNIDADE de deficit
(`GRUPO:<id>:0`..`:N-1`): se o deficit cresce, os tokens extra (`:N`, `:N+1`,
...) sao ineditos face ao baseline congelado e `baseline_delta.py` acusa-os
como NOVO. Se o deficit encolhe, os tokens que desaparecem sao RESOLVIDOS
(baseline_delta.py nunca falha por isso).

Uso:
  manifest_delta_gate.py LIFT_DIR [--manifest MANIFEST.tsv] [--debt DEBT.json]
  manifest_delta_gate.py LIFT_DIR [--manifest MANIFEST.tsv] --freeze OUT.json

rc=0  nenhum achado novo face a divida congelada (pode haver achados resolvidos)
rc=1  ha' pelo menos um achado novo (individual ou unidade de deficit de grupo)
rc=2  erro de uso (propagado de gen_manifest.py/baseline_delta.py)
"""
from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent

# `AUSENTE  {kind:6} {marker}  (esperado >={want}, encontrado 0)`
AUSENTE_RE = re.compile(r"^AUSENTE\s+(\S+)\s+(\S+)")
# `A MENOS  {kind:6} {marker}  (esperado >={want}, encontrado {got})` -- SO'
# quando a linha nao contem "(grupo)" (essas sao tratadas a parte abaixo).
AMENOS_RE = re.compile(r"^A MENOS\s+(\S+)\s+(\S+)")
# `A MENOS (grupo)  {gid}: {names} esperado>={sum_want} encontrado={sum_found}`
GRUPO_RE = re.compile(
    r"A MENOS \(grupo\)\s+([^:\s]+):.*esperado>=(\d+)\s+encontrado=(\d+)"
)


def run_gen_manifest(lift_dir: str, manifest_path: str, python: str,
                     host_sources=None, no_host: bool = False) -> str:
    """Invoca gen_manifest.py --verify contra `lift_dir` e devolve o stdout.

    SEMPRE a copia de games/gow2/lift_baseline/ (HERE), nunca a orfa de
    ../gow2-recomp/lift_baseline/ (landmine medida e evitada, 05-02-PLAN.md).
    O rc do subprocesso e' ignorado aqui de proposito -- o que importa e' o
    TEXTO; o rc de gen_manifest.py --verify e' sobre o limiar ABSOLUTO, nao
    sobre o que este script decide (delta contra a divida congelada).
    """
    cmd = [python, str(HERE / "gen_manifest.py"), lift_dir, "--verify", str(manifest_path)]
    # D3: sem flags, gen_manifest.py ja' usa games/gow2/hooks/ por default --
    # estas so' existem para os testes de mutacao poderem apontar noutro sitio.
    if no_host:
        cmd.append("--no-host-sources")
    for src in (host_sources or []):
        cmd += ["--host-sources", src]
    result = subprocess.run(cmd, capture_output=True, text=True)
    return result.stdout


def parse_findings(stdout: str) -> dict[str, list[str]]:
    """Converte a saida de gen_manifest.py --verify em ids opacos.

    individual: um id por linha AUSENTE/A MENOS (nao-grupo).
    group:      tokens sinteticos POR UNIDADE de deficit de grupo
                (GRUPO:<gid>:0..deficit-1) -- ver docstring do modulo.
    """
    individual_ids: list[str] = []
    group_ids: list[str] = []

    for raw_line in stdout.splitlines():
        line = raw_line.strip()
        if not line:
            continue

        if "A MENOS (grupo)" in line:
            m = GRUPO_RE.search(line)
            if not m:
                continue
            gid, sum_want, sum_found = m.group(1), int(m.group(2)), int(m.group(3))
            deficit = sum_want - sum_found
            for i in range(deficit):
                group_ids.append(f"GRUPO:{gid}:{i}")
            continue

        m = AUSENTE_RE.match(line)
        if m:
            kind, marker = m.group(1), m.group(2)
            individual_ids.append(f"AUSENTE:{kind}:{marker}")
            continue

        if line.startswith("A MENOS"):
            m = AMENOS_RE.match(line)
            if m:
                kind, marker = m.group(1), m.group(2)
                individual_ids.append(f"AMENOS:{kind}:{marker}")
            continue

    return {"individual": individual_ids, "group": group_ids}


def build_current_ids(lift_dir: str, manifest_path: str, python: str,
                      host_sources=None, no_host: bool = False) -> set[str]:
    stdout = run_gen_manifest(lift_dir, manifest_path, python, host_sources, no_host)
    findings = parse_findings(stdout)
    return set(findings["individual"]) | set(findings["group"])


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("lift_dir", help="diretorio do lift a verificar")
    ap.add_argument("--manifest", default=str(HERE / "MANIFEST.tsv"),
                     help="MANIFEST.tsv a usar (default: %(default)s)")
    ap.add_argument("--debt", default=str(HERE / "MANIFEST_DEBT_BASELINE.json"),
                     help="divida congelada a comparar (default: %(default)s)")
    ap.add_argument("--freeze", metavar="OUT.json", default=None,
                     help="em vez de comparar, escreve a divida medida agora em OUT.json")
    ap.add_argument("--python", default=sys.executable,
                     help="interpretador usado para invocar gen_manifest.py/baseline_delta.py")
    ap.add_argument("--host-sources", action="append", default=None,
                     help="(D3) fonte host adicional a contar; default de gen_manifest.py = games/gow2/hooks/")
    ap.add_argument("--no-host-sources", action="store_true",
                     help="(D3) conta so' o lift -- comportamento pre-D3")
    args = ap.parse_args(argv)

    current_ids = build_current_ids(args.lift_dir, args.manifest, args.python,
                                    args.host_sources, args.no_host_sources)

    if args.freeze is not None:
        payload = {
            "_regenerar": (
                f"manifest_delta_gate.py {args.lift_dir} --manifest {args.manifest} "
                f"--freeze {args.freeze}"
            ),
            "ids": sorted(current_ids),
        }
        with open(args.freeze, "w", encoding="utf-8") as f:
            json.dump(payload, f, indent=1)
        print(f"[MANIFEST_DEBT] congelados {len(current_ids)} ids em {args.freeze}")
        return 0

    tmp_current = tempfile.NamedTemporaryFile(
        mode="w", suffix=".json", delete=False, encoding="utf-8"
    )
    try:
        json.dump({"ids": sorted(current_ids)}, tmp_current)
        tmp_current.close()
        result = subprocess.run(
            [args.python, str(HERE / "baseline_delta.py"),
             tmp_current.name, args.debt, "--label", "MANIFEST_DEBT"],
            capture_output=True, text=True,
        )
        sys.stdout.write(result.stdout)
        sys.stderr.write(result.stderr)
        return result.returncode
    finally:
        Path(tmp_current.name).unlink(missing_ok=True)


if __name__ == "__main__":
    sys.exit(main())
