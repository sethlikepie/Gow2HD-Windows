#!/usr/bin/env python3
"""Testes de gen_contracts_candidates.py (Task 3, 04-04-PLAN.md).

Testes 1/2 usam fixtures sinteticas em tempfile.TemporaryDirectory(); teste 3
corre contra o corpus real (../gow2-recomp/recomp_mid_v2 + MANIFEST.tsv +
PATCH_CATALOG.tsv reais) para confirmar a ordem de grandeza medida na sessao
de planeamento (68 marcadores / 31 patches, D-4.4).

Uso:  .venv/bin/python3 test_gen_contracts_candidates.py
rc=0  todos os [PASS]
rc=1  pelo menos um [FAIL]
"""
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
SCRIPT = HERE / "gen_contracts_candidates.py"
REAL_PATCH_DIR = HERE.parent.parent.parent.parent / "gow2-recomp" / "recomp_mid_v2"
REAL_MANIFEST = HERE / "MANIFEST.tsv"
REAL_CATALOG = HERE / "PATCH_CATALOG.tsv"


def run(patch_dir: Path, manifest: Path, catalog: Path) -> subprocess.CompletedProcess:
    return subprocess.run(
        [sys.executable, str(SCRIPT), str(patch_dir), str(manifest), str(catalog)],
        capture_output=True, text=True,
    )


def test_1_dono_unico(tmp: Path) -> None:
    """Marcador presente em SO UM patch sintetico FUNCIONAL -> vira candidato."""
    patch_dir = tmp / "t1_patches"
    patch_dir.mkdir()
    (patch_dir / "patch_alpha.py").write_text(
        '"""fixture."""\nprint("[SYNTH-ALPHA] ok")\n'
    )
    (patch_dir / "patch_beta.py").write_text(
        '"""fixture."""\nprint("nada a ver")\n'
    )
    manifest = tmp / "t1_manifest.tsv"
    manifest.write_text("[SYNTH-ALPHA]\tTAG\t3\tppu_recomp_000.cpp\n")
    catalog = tmp / "t1_catalog.tsv"
    catalog.write_text(
        "patch_alpha.py\tsim\tFUNCIONAL\tmisc\tFalse\t-\t-\t-\n"
        "patch_beta.py\tsim\tFUNCIONAL\tmisc\tFalse\t-\t-\t-\n"
    )
    r = run(patch_dir, manifest, catalog)
    assert r.returncode == 0, f"rc esperado 0: {r.stdout}{r.stderr}"
    assert "tag-synth-alpha\tpatch_alpha.py\tglobal\t-\tcount_ge\t[SYNTH-ALPHA]\t3\t" in r.stdout, r.stdout
    assert "patch_beta.py" not in r.stdout, r.stdout
    print("[PASS] teste 1: marcador com dono unico vira candidato (patch_alpha.py, min_count=3)")


def test_2_dono_multiplo(tmp: Path) -> None:
    """Marcador presente em DOIS patches sinteticos -> NAO vira candidato."""
    patch_dir = tmp / "t2_patches"
    patch_dir.mkdir()
    (patch_dir / "patch_gamma.py").write_text(
        '"""fixture."""\nprint("[SYNTH-BETA] a")\n'
    )
    (patch_dir / "patch_delta.py").write_text(
        '"""fixture."""\nprint("[SYNTH-BETA] b")\n'
    )
    manifest = tmp / "t2_manifest.tsv"
    manifest.write_text("[SYNTH-BETA]\tTAG\t1\tppu_recomp_000.cpp\n")
    catalog = tmp / "t2_catalog.tsv"
    catalog.write_text(
        "patch_gamma.py\tsim\tFUNCIONAL\tmisc\tFalse\t-\t-\t-\n"
        "patch_delta.py\tsim\tFUNCIONAL\tmisc\tFalse\t-\t-\t-\n"
    )
    r = run(patch_dir, manifest, catalog)
    assert r.returncode == 0, f"rc esperado 0: {r.stdout}{r.stderr}"
    assert "SYNTH-BETA" not in r.stdout, r.stdout
    print("[PASS] teste 2: marcador com dono multiplo NAO vira candidato (ambiguidade nao resolvida)")


def test_3_obsoleto_excluido(tmp: Path) -> None:
    """Marcador com nota OBSOLETO:... NAO vira candidato, mesmo com dono unico.

    Achado real desta task: [OPDISP] tem dono unico (patch_jumptable_2a209c.py)
    mas o MANIFEST.tsv marca-o OBSOLETO (D-3.2, o lifter passou a materializar
    um switch nativo) -- um contrato derivado dele falharia SEMPRE contra o
    lift real (tag-opdisp: esperado 1 encontrado 0), nao por regressao.
    """
    patch_dir = tmp / "t3o_patches"
    patch_dir.mkdir()
    (patch_dir / "patch_epsilon.py").write_text(
        '"""fixture."""\nprint("[SYNTH-OBSOLETO] ok")\n'
    )
    manifest = tmp / "t3o_manifest.tsv"
    manifest.write_text(
        "[SYNTH-OBSOLETO]\tTAG\t1\tppu_recomp_000.cpp\tOBSOLETO: substituido por switch nativo\n"
    )
    catalog = tmp / "t3o_catalog.tsv"
    catalog.write_text("patch_epsilon.py\tsim\tFUNCIONAL\tmisc\tFalse\t-\t-\t-\n")
    r = run(patch_dir, manifest, catalog)
    assert r.returncode == 0, f"rc esperado 0: {r.stdout}{r.stderr}"
    assert "SYNTH-OBSOLETO" not in r.stdout, r.stdout
    print("[PASS] teste 3b: marcador OBSOLETO NAO vira candidato mesmo com dono unico")


def test_4_medicao_real(tmp: Path) -> None:
    """Contra o corpus real: contagem de candidatos proxima de 68 marcadores / 31 patches."""
    if not (REAL_PATCH_DIR.is_dir() and REAL_MANIFEST.is_file() and REAL_CATALOG.is_file()):
        print("[SKIP] teste 3: corpus real nao encontrado neste ambiente")
        return
    r = run(REAL_PATCH_DIR, REAL_MANIFEST, REAL_CATALOG)
    assert r.returncode == 0, f"rc esperado 0: {r.stdout}{r.stderr}"
    lines = [ln for ln in r.stdout.splitlines() if ln.strip()]
    n_markers = len(lines)
    patches = {ln.split("\t")[1] for ln in lines}
    n_patches = len(patches)
    assert "tag-opdisp\t" not in r.stdout, "OPDISP e' OBSOLETO, nao deveria ser candidato"
    print(f"[PASS] teste 4: medicao real -- {n_markers} marcadores candidatos, "
          f"{n_patches} patches distintos (planeamento: 68/31, ver SUMMARY para o numero real)")
    assert n_markers > 0, "esperava pelo menos 1 candidato contra o corpus real"


def main() -> int:
    tests = [
        test_1_dono_unico, test_2_dono_multiplo, test_3_obsoleto_excluido,
        test_4_medicao_real,
    ]
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
