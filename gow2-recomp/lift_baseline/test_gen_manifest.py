#!/usr/bin/env python3
"""
ps3recomp / GoW2 - teste-ouro dos marcadores kind=PREAMBLE em gen_manifest.py.

Fase 2, Task 2 (D-2.1/D-2.2, 02-CONTEXT.md). Prova por MUTACAO -- nao so'
caminho feliz -- que os dois marcadores novos (g_trampoline_fn,
ps3_indirect_call) tem dentes: se um re-lift os perder, verify() tem de
reportar rc=1 e a mensagem exacta "AUSENTE  PREAMBLE <simbolo>", nao so'
um codigo de saida silencioso.

Todas as mutacoes operam em ficheiros dentro de tempfile.TemporaryDirectory()
-- nunca no lift real (`../gow2-recomp/recomp_macos_v2`) nem no MANIFEST.tsv
committed.

Run directly:
  .venv/bin/python3 games/gow2/lift_baseline/test_gen_manifest.py
Exit code 0 = as 5 verificacoes passaram (5 linhas [PASS]).
"""
from __future__ import annotations

import contextlib
import io
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import gen_manifest  # noqa: E402


def _write_tmp(dir_path: Path, name: str, text: str) -> Path:
    """Escreve `text` num ficheiro `name` dentro de `dir_path`. Devolve o Path."""
    p = dir_path / name
    p.write_text(text)
    return p


def check_verify_pass_with_both_preamble_markers() -> None:
    """Teste 1: verify() passa (rc=0) quando os 2 marcadores PREAMBLE estao presentes."""
    import tempfile

    with tempfile.TemporaryDirectory() as d:
        dirp = Path(d)
        chunk = _write_tmp(
            dirp,
            "ppu_recomp_000.cpp",
            "extern \"C\" void ps3_indirect_call(ppu_context* ctx);\n"
            "extern \"C\" thread_local void (*g_trampoline_fn)(void*);\n"
            "void func_00000000() {}\n",
        )
        manifest = _write_tmp(
            dirp,
            "MANIFEST.tsv",
            "g_trampoline_fn\tPREAMBLE\t1\tppu_recomp_000.cpp\n"
            "ps3_indirect_call\tPREAMBLE\t1\tppu_recomp_000.cpp\n",
        )
        rc = gen_manifest.verify([chunk], manifest)
        assert rc == 0, f"esperado rc=0 com os 2 marcadores presentes, obtido {rc}"
    print("[PASS] verify() sai 0 quando g_trampoline_fn e ps3_indirect_call estao presentes")


def check_verify_mutation_missing_trampoline_fn() -> None:
    """Teste 2 (mutacao): sem g_trampoline_fn no texto, verify() reporta AUSENTE PREAMBLE."""
    import tempfile

    with tempfile.TemporaryDirectory() as d:
        dirp = Path(d)
        # mutacao: texto SEM g_trampoline_fn (so' ps3_indirect_call sobrevive)
        chunk = _write_tmp(
            dirp,
            "ppu_recomp_000.cpp",
            "extern \"C\" void ps3_indirect_call(ppu_context* ctx);\n"
            "void func_00000000() {}\n",
        )
        manifest = _write_tmp(
            dirp,
            "MANIFEST.tsv",
            "g_trampoline_fn\tPREAMBLE\t1\tppu_recomp_000.cpp\n"
            "ps3_indirect_call\tPREAMBLE\t1\tppu_recomp_000.cpp\n",
        )
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc = gen_manifest.verify([chunk], manifest)
        out = buf.getvalue()
        assert rc == 1, f"esperado rc=1 com g_trampoline_fn ausente, obtido {rc}"
        assert "AUSENTE  PREAMBLE g_trampoline_fn" in out, (
            f"mensagem AUSENTE PREAMBLE g_trampoline_fn nao encontrada na saida: {out!r}"
        )
    print(
        "[PASS] verify() por mutacao detecta ausencia de g_trampoline_fn "
        "(rc=1, mensagem 'AUSENTE  PREAMBLE g_trampoline_fn' confirmada)"
    )


def check_verify_mutation_missing_indirect_call() -> None:
    """Teste 3 (mutacao): sem ps3_indirect_call no texto, verify() reporta AUSENTE PREAMBLE."""
    import tempfile

    with tempfile.TemporaryDirectory() as d:
        dirp = Path(d)
        # mutacao: texto SEM ps3_indirect_call (so' g_trampoline_fn sobrevive)
        chunk = _write_tmp(
            dirp,
            "ppu_recomp_000.cpp",
            "extern \"C\" thread_local void (*g_trampoline_fn)(void*);\n"
            "void func_00000000() {}\n",
        )
        manifest = _write_tmp(
            dirp,
            "MANIFEST.tsv",
            "g_trampoline_fn\tPREAMBLE\t1\tppu_recomp_000.cpp\n"
            "ps3_indirect_call\tPREAMBLE\t1\tppu_recomp_000.cpp\n",
        )
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc = gen_manifest.verify([chunk], manifest)
        out = buf.getvalue()
        assert rc == 1, f"esperado rc=1 com ps3_indirect_call ausente, obtido {rc}"
        assert "AUSENTE  PREAMBLE ps3_indirect_call" in out, (
            f"mensagem AUSENTE PREAMBLE ps3_indirect_call nao encontrada na saida: {out!r}"
        )
    print(
        "[PASS] verify() por mutacao detecta ausencia de ps3_indirect_call "
        "(rc=1, mensagem 'AUSENTE  PREAMBLE ps3_indirect_call' confirmada)"
    )


def check_generation_excludes_ps3_timebase_now() -> None:
    """Teste 4: a exclusao de ps3_timebase_now e' codificada, nao um acaso do alvo nunca o conter.

    Corre a geracao real de gen_manifest.main() contra um preambulo sintetico
    que CONTEM ps3_timebase_now(void) -- monkeypatching gen_manifest.PURE para
    apontar para esse ficheiro sintetico, nunca para o preamble_pure.cpp real.
    A saida NAO deve conter uma linha kind=PREAMBLE para ps3_timebase_now,
    mas DEVE conter as dos outros dois simbolos (prova que o mecanismo corre,
    nao que o alvo sintetico e' vazio por acidente).
    """
    import tempfile

    fake_preamble_text = (
        "extern \"C\" uint64_t ps3_timebase_now(void);\n"
        "extern \"C\" void ps3_indirect_call(ppu_context* ctx);\n"
        "extern \"C\" thread_local void (*g_trampoline_fn)(void*);\n"
        "void func_00000000() {}\n"
    )
    with tempfile.TemporaryDirectory() as d:
        dirp = Path(d)
        fake_pure = _write_tmp(dirp, "preamble_pure.cpp", fake_preamble_text)
        lift_dir = dirp / "lift"
        lift_dir.mkdir()
        _write_tmp(lift_dir, "ppu_recomp_000.cpp", fake_preamble_text)

        orig_pure = gen_manifest.PURE
        orig_argv = sys.argv
        buf = io.StringIO()
        try:
            gen_manifest.PURE = fake_pure
            sys.argv = ["gen_manifest.py", str(lift_dir)]
            with contextlib.redirect_stdout(buf):
                rc = gen_manifest.main()
        finally:
            gen_manifest.PURE = orig_pure
            sys.argv = orig_argv

        out = buf.getvalue()
        assert rc == 0, f"geracao devia sair 0, obteve {rc}"
        assert "ps3_timebase_now\tPREAMBLE" not in out, (
            f"ps3_timebase_now NAO devia ganhar marcador PREAMBLE: {out!r}"
        )
        assert "g_trampoline_fn\tPREAMBLE" in out, (
            f"g_trampoline_fn devia ganhar marcador PREAMBLE (mecanismo nao correu?): {out!r}"
        )
        assert "ps3_indirect_call\tPREAMBLE" in out, (
            f"ps3_indirect_call devia ganhar marcador PREAMBLE (mecanismo nao correu?): {out!r}"
        )
    print(
        "[PASS] geracao exclui ps3_timebase_now do kind=PREAMBLE por decisao codificada "
        "(preamble_syms = lifter_syms - {'ps3_timebase_now'}), confirmado com alvo sintetico que O CONTEM"
    )


def check_manifest_header_reconciles_three_figures() -> None:
    """Teste 5: MANIFEST.tsv committed reconcilia as tres cifras do criterio 1 (D-2.2)."""
    manifest_path = HERE / "MANIFEST.tsv"
    header_text = manifest_path.read_text()
    for figure in ("1603", "1662", "1793"):
        assert figure in header_text, (
            f"cifra {figure} nao encontrada em {manifest_path} -- D-2.2 exige as tres "
            "reconciliadas no proprio artefacto"
        )
    print(
        "[PASS] MANIFEST.tsv reconcilia as tres cifras do criterio 1 "
        "(1603 desactualizada / 1662 gate anterior / 1793 real) no cabecalho"
    )


def check_gap_before_obsoleto_grupo_mechanism() -> None:
    """Teste RED (gap real): confirma que o mecanismo OBSOLETO/GRUPO existe no
    codigo -- se este teste falhar, a Task 1 (03-03-PLAN.md) ainda nao foi
    implementada. Nao mede comportamento; mede presenca do mecanismo."""
    src = (HERE / "gen_manifest.py").read_text()
    assert "OBSOLETO" in src and "GRUPO" in src, (
        "gen_manifest.py ainda nao tem o mecanismo OBSOLETO/GRUPO (Task 1, 03-03-PLAN.md)"
    )
    print("[PASS] gen_manifest.py contem o mecanismo OBSOLETO/GRUPO (gap fechado)")


def check_obsoleto_marker_missing_does_not_fail_gate() -> None:
    """Teste 1: marcador OBSOLETO ausente (found==0) nao aparece em AUSENTE,
    aparece numa categoria informativa separada, e nao faz verify() falhar."""
    import tempfile

    with tempfile.TemporaryDirectory() as d:
        dirp = Path(d)
        chunk = _write_tmp(dirp, "ppu_recomp_000.cpp", "void func_00000000() {}\n")
        manifest = _write_tmp(
            dirp,
            "MANIFEST.tsv",
            "[OPDISP]\tTAG\t1\tppu_recomp_001.cpp\t"
            "OBSOLETO: lifter materializa switch nativo cobrindo 54/54 alvos\n",
        )
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc = gen_manifest.verify([chunk], manifest)
        out = buf.getvalue()
        assert rc == 0, f"esperado rc=0 (marcador so' obsoleto ausente), obtido {rc}"
        assert "AUSENTE" not in out, f"OBSOLETO nao deve aparecer como AUSENTE: {out!r}"
        assert "OBSOLETO" in out and "[OPDISP]" in out, (
            f"linha OBSOLETO informativa nao encontrada: {out!r}"
        )
    print(
        "[PASS] marcador OBSOLETO ausente (found==0) nao aparece em AUSENTE, "
        "verify() continua rc=0"
    )


def check_obsoleto_is_selective_not_global_switch() -> None:
    """Teste 2 (regressao/seletividade): um marcador SEM nota, ausente, continua
    a aparecer em AUSENTE e a fazer verify() devolver 1 -- OBSOLETO nao mascara
    outros marcadores no mesmo manifesto."""
    import tempfile

    with tempfile.TemporaryDirectory() as d:
        dirp = Path(d)
        chunk = _write_tmp(dirp, "ppu_recomp_000.cpp", "void func_00000000() {}\n")
        manifest = _write_tmp(
            dirp,
            "MANIFEST.tsv",
            "[OPDISP]\tTAG\t1\tppu_recomp_001.cpp\tOBSOLETO: prova citada\n"
            "[WADLD-ALLOC]\tTAG\t1\tppu_recomp_002.cpp\n",
        )
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc = gen_manifest.verify([chunk], manifest)
        out = buf.getvalue()
        assert rc == 1, f"esperado rc=1 (marcador sem nota, ausente), obtido {rc}"
        assert "AUSENTE  TAG    [WADLD-ALLOC]" in out, (
            f"[WADLD-ALLOC] (sem nota) devia continuar AUSENTE: {out!r}"
        )
    print(
        "[PASS] OBSOLETO e' seletivo por marcador -- marcador sem nota continua "
        "AUSENTE e verify() devolve 1 (nao e' um interruptor global)"
    )


def check_grupo_sum_passes_combined_threshold() -> None:
    """Teste 3: dois marcadores com o mesmo GRUPO:<id>, um individualmente abaixo
    do min_count mas a SOMA >= soma dos min_count -- verify() devolve 0, nenhum
    dos dois aparece em A MENOS individual."""
    import tempfile

    with tempfile.TemporaryDirectory() as d:
        dirp = Path(d)
        # ps3_indirect_call: min_count=10, found=6 (individualmente 'a menos')
        # ps3_call_opd:      min_count=2,  found=8 (individualmente 'ok')
        # soma: min_count=12, found=14 -- grupo passa
        chunk = _write_tmp(
            dirp,
            "ppu_recomp_000.cpp",
            "ps3_indirect_call(ctx);\n" * 6 + "ps3_call_opd(ctx,\n" * 8,
        )
        manifest = _write_tmp(
            dirp,
            "MANIFEST.tsv",
            "ps3_indirect_call\tPREAMBLE\t10\tppu_recomp_000.cpp\tGRUPO:opd-dispatch\n"
            "ps3_call_opd\tSYM\t2\tppu_recomp_000.cpp\tGRUPO:opd-dispatch\n",
        )
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc = gen_manifest.verify([chunk], manifest)
        out = buf.getvalue()
        assert rc == 0, f"esperado rc=0 (soma do grupo cumpre o limiar), obtido {rc}"
        assert "A MENOS  PREAMBLE ps3_indirect_call" not in out, (
            f"membro do grupo nao deve aparecer em A MENOS individual: {out!r}"
        )
        assert "A MENOS (grupo)" not in out, f"grupo nao devia falhar: {out!r}"
    print(
        "[PASS] GRUPO soma as contagens -- par com soma >= soma dos min_count "
        "passa (rc=0), sem punir o membro individualmente abaixo do seu proprio limiar"
    )


def check_grupo_sum_below_threshold_fails() -> None:
    """Teste 4: mesmo par GRUPO, mas com a SOMA de found abaixo da soma de
    min_count -- verify() devolve 1 e cita o id do grupo."""
    import tempfile

    with tempfile.TemporaryDirectory() as d:
        dirp = Path(d)
        # soma min_count=12, soma found = 6+2 = 8 < 12 -- grupo falha
        chunk = _write_tmp(
            dirp,
            "ppu_recomp_000.cpp",
            "ps3_indirect_call(ctx);\n" * 6 + "ps3_call_opd(ctx,\n" * 2,
        )
        manifest = _write_tmp(
            dirp,
            "MANIFEST.tsv",
            "ps3_indirect_call\tPREAMBLE\t10\tppu_recomp_000.cpp\tGRUPO:opd-dispatch\n"
            "ps3_call_opd\tSYM\t2\tppu_recomp_000.cpp\tGRUPO:opd-dispatch\n",
        )
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc = gen_manifest.verify([chunk], manifest)
        out = buf.getvalue()
        assert rc == 1, f"esperado rc=1 (soma do grupo abaixo do limiar), obtido {rc}"
        assert "A MENOS (grupo)  opd-dispatch" in out, (
            f"linha do grupo falhado (citando o id) nao encontrada: {out!r}"
        )
    print(
        "[PASS] GRUPO cuja soma nao cumpre o limiar combinado faz verify() devolver 1, "
        "citando o id do grupo na saida"
    )


def main() -> int:
    checks = (
        check_verify_pass_with_both_preamble_markers,
        check_verify_mutation_missing_trampoline_fn,
        check_verify_mutation_missing_indirect_call,
        check_generation_excludes_ps3_timebase_now,
        check_manifest_header_reconciles_three_figures,
        check_gap_before_obsoleto_grupo_mechanism,
        check_obsoleto_marker_missing_does_not_fail_gate,
        check_obsoleto_is_selective_not_global_switch,
        check_grupo_sum_passes_combined_threshold,
        check_grupo_sum_below_threshold_fails,
    )
    for check in checks:
        check()
    return 0


if __name__ == "__main__":
    sys.exit(main())
