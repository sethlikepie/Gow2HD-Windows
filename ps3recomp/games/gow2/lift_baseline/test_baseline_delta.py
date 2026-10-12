#!/usr/bin/env python3
"""Testes por mutacao sintetica de baseline_delta.py (D-4.5, 04-03-PLAN.md Task 1).

Cada teste invoca o SCRIPT como CLI via subprocess (nao so' as funcoes
internas) -- prova fim-a-fim do proprio binario. Todos os JSON sao sinteticos
e vivem em tempfile.TemporaryDirectory(); nunca ficheiros reais do lift.

Uso:  .venv/bin/python3 test_baseline_delta.py
rc=0  as 5 [PASS]
rc=1  pelo menos um [FAIL]
"""
import json
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
SCRIPT = HERE / "baseline_delta.py"


def write_ids(path: Path, ids: list[str]) -> None:
    path.write_text(json.dumps({"ids": ids}))


def run(current: Path, baseline: Path) -> subprocess.CompletedProcess:
    return subprocess.run(
        [sys.executable, str(SCRIPT), str(current), str(baseline)],
        capture_output=True, text=True,
    )


def test_1_delta_limpo(tmp: Path) -> None:
    """current == baseline -> rc=0, 'DELTA OK'."""
    cur, base = tmp / "cur1.json", tmp / "base1.json"
    ids = ["a", "b", "c"]
    write_ids(cur, ids)
    write_ids(base, ids)
    r = run(cur, base)
    assert r.returncode == 0, f"rc esperado 0, obtido {r.returncode}: {r.stdout}{r.stderr}"
    assert "DELTA OK" in r.stdout, r.stdout
    print("[PASS] teste 1: delta limpo -> rc=0, DELTA OK")


def test_2_achado_novo(tmp: Path) -> None:
    """current tem um id extra face a baseline -> rc=1, cita o id."""
    cur, base = tmp / "cur2.json", tmp / "base2.json"
    write_ids(base, ["a", "b"])
    write_ids(cur, ["a", "b", "REGRESSAO_NOVA"])
    r = run(cur, base)
    assert r.returncode == 1, f"rc esperado 1, obtido {r.returncode}: {r.stdout}{r.stderr}"
    assert "REGRESSAO_NOVA" in r.stdout, r.stdout
    assert "NOVO" in r.stdout, r.stdout
    print("[PASS] teste 2: achado novo (regressao) -> rc=1, cita o id exacto")


def test_3_achado_resolvido(tmp: Path) -> None:
    """baseline tem um id que current ja' nao tem -> rc=0 (melhoria nunca falha).

    Prova a licao do PREAMBLE ps3_indirect_call (Fase 3): um achado resolvido
    (found -> 0) e' informativo, nunca faz o gate falhar.
    """
    cur, base = tmp / "cur3.json", tmp / "base3.json"
    write_ids(base, ["a", "b", "ACHADO_RESOLVIDO"])
    write_ids(cur, ["a", "b"])
    r = run(cur, base)
    assert r.returncode == 0, f"rc esperado 0, obtido {r.returncode}: {r.stdout}{r.stderr}"
    assert "ACHADO_RESOLVIDO" in r.stdout, r.stdout
    assert "RESOLVIDO" in r.stdout, r.stdout
    print("[PASS] teste 3: achado resolvido (melhoria) -> rc=0, nunca falha")


def test_4_corpus_grande(tmp: Path) -> None:
    """1000 ids sinteticos identicos -> rc=0 (magnitude do corpus e' irrelevante)."""
    cur, base = tmp / "cur4.json", tmp / "base4.json"
    ids = [f"id_{i:04d}" for i in range(1000)]
    write_ids(base, ids)
    write_ids(cur, ids)
    r = run(cur, base)
    assert r.returncode == 0, f"rc esperado 0, obtido {r.returncode}: {r.stdout}{r.stderr}"
    assert "current=1000 baseline=1000" in r.stdout, r.stdout
    print("[PASS] teste 4: corpus grande (1000 ids) identico -> rc=0")


def test_5_erro_de_uso(tmp: Path) -> None:
    """ficheiro current inexistente -> rc=2, mensagem clara (nao traceback cru)."""
    cur, base = tmp / "nao_existe.json", tmp / "base5.json"
    write_ids(base, ["a"])
    r = run(cur, base)
    assert r.returncode == 2, f"rc esperado 2, obtido {r.returncode}: {r.stdout}{r.stderr}"
    assert "Traceback" not in r.stderr, f"traceback cru vazou: {r.stderr}"
    assert "ERRO" in r.stderr, r.stderr
    print("[PASS] teste 5: ficheiro current inexistente -> rc=2, mensagem clara")


def main() -> int:
    tests = [test_1_delta_limpo, test_2_achado_novo, test_3_achado_resolvido,
             test_4_corpus_grande, test_5_erro_de_uso]
    failed = 0
    with tempfile.TemporaryDirectory() as tmpdir:
        tmp = Path(tmpdir)
        for t in tests:
            try:
                t(tmp)
            except AssertionError as e:
                failed += 1
                print(f"[FAIL] {t.__name__}: {e}")
    if failed:
        print(f"\n{failed}/{len(tests)} testes falharam")
        return 1
    print(f"\n{len(tests)}/{len(tests)} testes [PASS]")
    return 0


if __name__ == "__main__":
    sys.exit(main())
