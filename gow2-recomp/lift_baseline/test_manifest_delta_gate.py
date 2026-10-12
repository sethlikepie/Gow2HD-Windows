#!/usr/bin/env python3
"""Testes fim-a-fim de manifest_delta_gate.py (D-5.1, 05-02-PLAN.md Task 1).

Cada teste invoca o SCRIPT como CLI via subprocess (fim-a-fim do proprio
binario, nao so' das funcoes internas), com fixtures sinteticas dentro de
tempfile.TemporaryDirectory() -- nunca o lift real (`recomp_macos_v3`) nem o
MANIFEST.tsv/MANIFEST_DEBT_BASELINE.json committed. O lift real e' exercitado
a parte (Task 2 do plano).

Prova a licao do deficit-36-nao-visto (grupo opd-dispatch, Fase 3) aplicada ao
passo MANIFEST de verify_lift.sh: um deficit de grupo PIOR que a divida
congelada tem de aparecer como achado NOVO (rc=1); um deficit IGUAL ou MELHOR
nunca falha o gate -- a mesma propriedade que baseline_delta.py (D-4.5) ja
prova para lift_parity/audit_boundaries, aqui estendida ao MANIFEST via
tokens sinteticos por unidade de deficit (GRUPO:<id>:<i>).

Uso:  .venv/bin/python3 test_manifest_delta_gate.py
rc=0  todos os [PASS]
rc=1  pelo menos um [FAIL]
"""
from __future__ import annotations

import json
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
SCRIPT = HERE / "manifest_delta_gate.py"


def _write_tmp(dir_path: Path, name: str, text: str) -> Path:
    p = dir_path / name
    p.write_text(text)
    return p


def write_debt(path: Path, ids: list[str]) -> None:
    path.write_text(json.dumps({"ids": ids}))


def run(lift_dir: Path, manifest: Path, debt: Path | None = None,
        freeze: Path | None = None) -> subprocess.CompletedProcess:
    args = [sys.executable, str(SCRIPT), str(lift_dir),
            "--manifest", str(manifest), "--python", sys.executable]
    if freeze is not None:
        args += ["--freeze", str(freeze)]
    else:
        args += ["--debt", str(debt)]
    return subprocess.run(args, capture_output=True, text=True)


def check_caminho_limpo(tmp: Path) -> None:
    """Teste 1: MANIFEST.tsv sintetico sem AUSENTE/A MENOS, DEBT.json vazio -> rc=0."""
    d = tmp / "t1"
    d.mkdir()
    _write_tmp(d, "ppu_recomp_000.cpp", "[FOO] presente uma vez\n")
    manifest = _write_tmp(d, "MANIFEST.tsv", "[FOO]\tTAG\t1\tppu_recomp_000.cpp\n")
    debt = _write_tmp(d, "DEBT.json", "")
    write_debt(debt, [])
    r = run(d, manifest, debt=debt)
    assert r.returncode == 0, f"rc esperado 0, obtido {r.returncode}: {r.stdout}{r.stderr}"
    print("[PASS] teste 1: caminho limpo (sem AUSENTE/A MENOS) -> rc=0")


def check_achado_individual_novo(tmp: Path) -> None:
    """Teste 2: marcador AUSENTE que nao esta na divida congelada -> rc=1, cita o marcador."""
    d = tmp / "t2"
    d.mkdir()
    _write_tmp(d, "ppu_recomp_000.cpp", "nada relevante aqui\n")
    manifest = _write_tmp(d, "MANIFEST.tsv", "[BAR]\tTAG\t1\tppu_recomp_000.cpp\n")
    debt = _write_tmp(d, "DEBT.json", "")
    write_debt(debt, [])
    r = run(d, manifest, debt=debt)
    assert r.returncode == 1, f"rc esperado 1, obtido {r.returncode}: {r.stdout}{r.stderr}"
    assert "[BAR]" in r.stdout, r.stdout
    print("[PASS] teste 2: achado individual novo (AUSENTE fora da divida) -> rc=1, cita o marcador")


def check_deficit_grupo_igual_a_divida(tmp: Path) -> None:
    """Teste 3: deficit de grupo == divida congelada (N=2 tokens) -> rc=0 (dívida conhecida)."""
    d = tmp / "t3"
    d.mkdir()
    # ps3_indirect_call: min_count=10, found=6; ps3_call_opd: min_count=2, found=4
    # soma: min_count=12, found=10 -- deficit=2
    _write_tmp(
        d, "ppu_recomp_000.cpp",
        "ps3_indirect_call(ctx);\n" * 6 + "ps3_call_opd(ctx,\n" * 4,
    )
    manifest = _write_tmp(
        d, "MANIFEST.tsv",
        "ps3_indirect_call\tPREAMBLE\t10\tppu_recomp_000.cpp\tGRUPO:opd-dispatch\n"
        "ps3_call_opd\tSYM\t2\tppu_recomp_000.cpp\tGRUPO:opd-dispatch\n",
    )
    debt = _write_tmp(d, "DEBT.json", "")
    write_debt(debt, ["GRUPO:opd-dispatch:0", "GRUPO:opd-dispatch:1"])
    r = run(d, manifest, debt=debt)
    assert r.returncode == 0, f"rc esperado 0, obtido {r.returncode}: {r.stdout}{r.stderr}"
    print("[PASS] teste 3: deficit de grupo igual a divida congelada (N=2) -> rc=0")


def check_deficit_grupo_pior(tmp: Path) -> None:
    """Teste 4: deficit de grupo PIOR que a divida congelada -> rc=1, cita os tokens novos."""
    d = tmp / "t4"
    d.mkdir()
    # mesmo grupo, agora found ainda menor -- deficit sobe de 2 (N) para 4 (N+2)
    _write_tmp(
        d, "ppu_recomp_000.cpp",
        "ps3_indirect_call(ctx);\n" * 6 + "ps3_call_opd(ctx,\n" * 2,
    )
    manifest = _write_tmp(
        d, "MANIFEST.tsv",
        "ps3_indirect_call\tPREAMBLE\t10\tppu_recomp_000.cpp\tGRUPO:opd-dispatch\n"
        "ps3_call_opd\tSYM\t2\tppu_recomp_000.cpp\tGRUPO:opd-dispatch\n",
    )
    debt = _write_tmp(d, "DEBT.json", "")
    write_debt(debt, ["GRUPO:opd-dispatch:0", "GRUPO:opd-dispatch:1"])
    r = run(d, manifest, debt=debt)
    assert r.returncode == 1, f"rc esperado 1, obtido {r.returncode}: {r.stdout}{r.stderr}"
    assert "GRUPO:opd-dispatch:2" in r.stdout, r.stdout
    assert "GRUPO:opd-dispatch:3" in r.stdout, r.stdout
    print("[PASS] teste 4: deficit de grupo PIOR (N+2) -> rc=1, cita os 2 tokens novos")


def check_deficit_grupo_melhor_nunca_falha(tmp: Path) -> None:
    """Teste 5: deficit de grupo MELHOR que a divida congelada -> rc=0 (nunca falha)."""
    d = tmp / "t5"
    d.mkdir()
    # soma min_count=12, found=6+4=10 -- deficit=2, divida congelada tem 4 tokens (pior no passado)
    _write_tmp(
        d, "ppu_recomp_000.cpp",
        "ps3_indirect_call(ctx);\n" * 6 + "ps3_call_opd(ctx,\n" * 4,
    )
    manifest = _write_tmp(
        d, "MANIFEST.tsv",
        "ps3_indirect_call\tPREAMBLE\t10\tppu_recomp_000.cpp\tGRUPO:opd-dispatch\n"
        "ps3_call_opd\tSYM\t2\tppu_recomp_000.cpp\tGRUPO:opd-dispatch\n",
    )
    debt = _write_tmp(d, "DEBT.json", "")
    write_debt(debt, [f"GRUPO:opd-dispatch:{i}" for i in range(4)])
    r = run(d, manifest, debt=debt)
    assert r.returncode == 0, f"rc esperado 0, obtido {r.returncode}: {r.stdout}{r.stderr}"
    assert "RESOLVIDO" in r.stdout, r.stdout
    print("[PASS] teste 5: deficit de grupo MELHOR que a divida congelada -> rc=0, nunca falha")


def check_modo_freeze(tmp: Path) -> None:
    """Teste 6: --freeze OUT.json escreve {"_regenerar": ..., "ids": [...]} sem comparar a nenhum DEBT."""
    d = tmp / "t6"
    d.mkdir()
    _write_tmp(
        d, "ppu_recomp_000.cpp",
        "ps3_indirect_call(ctx);\n" * 6 + "ps3_call_opd(ctx,\n" * 2,
    )
    manifest = _write_tmp(
        d, "MANIFEST.tsv",
        "ps3_indirect_call\tPREAMBLE\t10\tppu_recomp_000.cpp\tGRUPO:opd-dispatch\n"
        "ps3_call_opd\tSYM\t2\tppu_recomp_000.cpp\tGRUPO:opd-dispatch\n",
    )
    out = tmp / "t6_out.json"
    r = run(d, manifest, freeze=out)
    assert r.returncode == 0, f"rc esperado 0, obtido {r.returncode}: {r.stdout}{r.stderr}"
    assert out.exists(), f"OUT.json nao foi escrito: {r.stdout}{r.stderr}"
    frozen = json.loads(out.read_text())
    assert "_regenerar" in frozen, frozen
    assert sorted(frozen["ids"]) == ["GRUPO:opd-dispatch:0", "GRUPO:opd-dispatch:1",
                                      "GRUPO:opd-dispatch:2", "GRUPO:opd-dispatch:3"], frozen
    print("[PASS] teste 6: --freeze escreve ids exatos sem comparar a DEBT.json")


def main() -> int:
    checks = (
        check_caminho_limpo,
        check_achado_individual_novo,
        check_deficit_grupo_igual_a_divida,
        check_deficit_grupo_pior,
        check_deficit_grupo_melhor_nunca_falha,
        check_modo_freeze,
    )
    failed = 0
    with tempfile.TemporaryDirectory() as tmpdir:
        tmp = Path(tmpdir)
        for check in checks:
            try:
                check(tmp)
            except AssertionError as e:
                failed += 1
                print(f"[FAIL] {check.__name__}: {e}")
    if failed:
        print(f"\n{failed}/{len(checks)} testes falharam")
        return 1
    print(f"\n{len(checks)}/{len(checks)} testes [PASS]")
    return 0


if __name__ == "__main__":
    sys.exit(main())
