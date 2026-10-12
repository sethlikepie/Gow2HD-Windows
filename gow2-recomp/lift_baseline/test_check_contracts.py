#!/usr/bin/env python3
"""Testes fim-a-fim de check_contracts.py (D-4.2, 04-04-PLAN.md).

Cada teste invoca o SCRIPT como CLI via subprocess (prova do proprio binario,
nao so' das funcoes internas). Fixtures sinteticas em
tempfile.TemporaryDirectory(); nunca lift real nestes testes (o lift real e'
exercitado a parte, Task 1 acao 5 do plano).

Uso:  .venv/bin/python3 test_check_contracts.py
rc=0  todos os [PASS]
rc=1  pelo menos um [FAIL]
"""
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
SCRIPT = HERE / "check_contracts.py"


def run(contracts: Path, lift_dir: Path, patch: str) -> subprocess.CompletedProcess:
    return subprocess.run(
        [sys.executable, str(SCRIPT), str(contracts), str(lift_dir), "--patch", patch],
        capture_output=True, text=True,
    )


def write_contracts(path: Path, lines: list[str]) -> None:
    header = "# id\tpatch\tescopo\talvo\tpredicado\tagulha\tvalor\trazao\n"
    path.write_text(header + "\n".join(lines) + "\n")


def func_0032e200_body(n_call_opd: int, n_indirect: int) -> str:
    """Corpo sintetico de func_0032E200 com N ps3_call_opd( / M ps3_indirect_call."""
    lines = ["void func_0032E200(ppu_context* ctx) {"]
    for _ in range(n_call_opd):
        lines.append("        ps3_call_opd(ctx, (uint32_t)ctx->gpr[10]);")
    for _ in range(n_indirect):
        lines.append("        ps3_indirect_call(ctx);")
    lines.append("}")
    lines.append("void func_00330D54(ppu_context* ctx) {")
    lines.append("}")
    return "\n".join(lines) + "\n"


def test_1_contrato_ea_passa(tmp: Path) -> None:
    """7 ps3_call_opd( e 0 ps3_indirect_call -- os dois contratos PASSAM, rc=0."""
    lift = tmp / "t1_lift"
    lift.mkdir()
    (lift / "ppu_recomp_003.cpp").write_text(func_0032e200_body(7, 0))
    contracts = tmp / "t1_contracts.tsv"
    write_contracts(contracts, [
        "opd-32e200\tpatch_32e200_opd.py\tea\t0x0032E200\tcount_ge\tps3_call_opd(\t7\tteste",
        "opd-32e200-noindirect\tpatch_32e200_opd.py\tea\t0x0032E200\tcount_eq\tps3_indirect_call\t0\tteste",
    ])
    r = run(contracts, lift, "patch_32e200_opd.py")
    assert r.returncode == 0, f"rc esperado 0, obtido {r.returncode}: {r.stdout}{r.stderr}"
    assert "[PASS]" in r.stdout and "[FAIL]" not in r.stdout, r.stdout
    print("[PASS] teste 1: contrato EA passa (7 call_opd, 0 indirect) -- rc=0")


def test_2_contrato_ea_falha(tmp: Path) -> None:
    """So' 5 ps3_call_opd( -- contrato exige >=7, rc=1, detalhe cita 7/5."""
    lift = tmp / "t2_lift"
    lift.mkdir()
    (lift / "ppu_recomp_003.cpp").write_text(func_0032e200_body(5, 0))
    contracts = tmp / "t2_contracts.tsv"
    write_contracts(contracts, [
        "opd-32e200\tpatch_32e200_opd.py\tea\t0x0032E200\tcount_ge\tps3_call_opd(\t7\tteste",
    ])
    r = run(contracts, lift, "patch_32e200_opd.py")
    assert r.returncode == 1, f"rc esperado 1, obtido {r.returncode}: {r.stdout}{r.stderr}"
    assert "esperado 7 encontrado 5" in r.stdout, r.stdout
    print("[PASS] teste 2: contrato EA falha (5 de 7) -- rc=1, 'esperado 7 encontrado 5'")


def test_3_ancora_perdida(tmp: Path) -> None:
    """Nenhum 'void func_0032E200(' no lift -- rc=1 (falha de contrato, nao erro de uso)."""
    lift = tmp / "t3_lift"
    lift.mkdir()
    (lift / "ppu_recomp_003.cpp").write_text("void func_00330D54(ppu_context* ctx) {}\n")
    contracts = tmp / "t3_contracts.tsv"
    write_contracts(contracts, [
        "opd-32e200\tpatch_32e200_opd.py\tea\t0x0032E200\tcount_ge\tps3_call_opd(\t7\tteste",
    ])
    r = run(contracts, lift, "patch_32e200_opd.py")
    assert r.returncode == 1, f"rc esperado 1, obtido {r.returncode}: {r.stdout}{r.stderr}"
    assert "ancora perdida" in r.stdout, r.stdout
    print("[PASS] teste 3: ancora perdida (funcao ausente do lift) -- rc=1, 'ancora perdida'")


def test_4_sem_contrato(tmp: Path) -> None:
    """Patch sem nenhuma linha em CONTRACTS.tsv -- rc=2, imprime SEM-CONTRATO."""
    lift = tmp / "t4_lift"
    lift.mkdir()
    (lift / "ppu_recomp_003.cpp").write_text(func_0032e200_body(7, 0))
    contracts = tmp / "t4_contracts.tsv"
    write_contracts(contracts, [
        "opd-32e200\tpatch_32e200_opd.py\tea\t0x0032E200\tcount_ge\tps3_call_opd(\t7\tteste",
    ])
    r = run(contracts, lift, "patch_inexistente_no_ficheiro.py")
    assert r.returncode == 2, f"rc esperado 2, obtido {r.returncode}: {r.stdout}{r.stderr}"
    assert "SEM-CONTRATO" in r.stdout, r.stdout
    print("[PASS] teste 4: patch sem contrato -- rc=2, 'SEM-CONTRATO'")


def test_5_escopo_global(tmp: Path) -> None:
    """Contrato global: agulha presente em SO' UM de 2 chunks -- soma across chunks, PASS."""
    lift = tmp / "t5_lift"
    lift.mkdir()
    (lift / "ppu_recomp_000.cpp").write_text("void func_AAAAAAAA(ppu_context* ctx) {}\n")
    (lift / "ppu_recomp_001.cpp").write_text(
        "void func_BBBBBBBB(ppu_context* ctx) {\n"
        "    fprintf(stderr, \"[WADLD-ALLOC] ok\\n\");\n"
        "}\n"
    )
    contracts = tmp / "t5_contracts.tsv"
    write_contracts(contracts, [
        "tag-wadld-alloc\tpatch_wadld_alloc_probe.py\tglobal\t-\tcount_ge\t[WADLD-ALLOC]\t1\tteste",
    ])
    r = run(contracts, lift, "patch_wadld_alloc_probe.py")
    assert r.returncode == 0, f"rc esperado 0, obtido {r.returncode}: {r.stdout}{r.stderr}"
    assert "[PASS]" in r.stdout, r.stdout
    print("[PASS] teste 5: escopo global soma agulha across chunks -- rc=0")


def test_6_agulha_regex(tmp: Path) -> None:
    """Prefixo 're:' activa re.findall em vez de str.count."""
    lift = tmp / "t6_lift"
    lift.mkdir()
    body = (
        "void func_0032E200(ppu_context* ctx) {\n"
        "        ps3_call_opd(ctx, (uint32_t)ctx->gpr[10]);\n"
        "        ps3_call_opd(ctx, (uint32_t)ctx->gpr[11]);\n"
        "        ps3_call_opd(ctx, (uint32_t)ctx->gpr[9]);\n"
        "}\n"
        "void func_00330D54(ppu_context* ctx) {}\n"
    )
    (lift / "ppu_recomp_003.cpp").write_text(body)
    contracts = tmp / "t6_contracts.tsv"
    agulha = r"re:ps3_call_opd\(ctx, \(uint32_t\)ctx->gpr\[1[01]\]\)"
    line = "\t".join(
        ["opd-regex", "patch_32e200_opd.py", "ea", "0x0032E200", "count_ge", agulha, "2", "teste"]
    )
    write_contracts(contracts, [line])
    r = run(contracts, lift, "patch_32e200_opd.py")
    assert r.returncode == 0, f"rc esperado 0, obtido {r.returncode}: {r.stdout}{r.stderr}"
    assert "esperado 2 encontrado 2" in r.stdout, r.stdout
    print("[PASS] teste 6: agulha 're:' activa regex (gpr[10]/gpr[11], nao gpr[9]) -- rc=0")


def test_7_erro_de_uso(tmp: Path) -> None:
    """CONTRACTS.tsv ou LIFT_DIR inexistente -- rc=3 (erro de uso, nao falha de contrato)."""
    lift = tmp / "t7_lift_nao_existe"
    contracts = tmp / "t7_contracts_nao_existe.tsv"
    r = run(contracts, lift, "patch_32e200_opd.py")
    assert r.returncode == 3, f"rc esperado 3, obtido {r.returncode}: {r.stdout}{r.stderr}"
    assert "ERRO" in r.stderr, r.stderr
    print("[PASS] teste 7: CONTRACTS.tsv/LIFT_DIR inexistente -- rc=3")


def test_8_weak_override_nao_e_ancora_perdida(tmp: Path) -> None:
    """Fase 19 (XEN-04): o corpo de uma funcao com [[functions_override]] sai em
    `PPC_FUNC_IMPL(func_X) {`, seguido de um wrapper FRACO `PPC_FUNC(func_X)`.

    Nasceu VERMELHO de uma medicao (2026-08-02): contra um lift com a forma
    nova, os contratos `escopo=ea` de 0x0032E200 davam "ancora perdida ... nao
    existe no lift" -- vermelho FALSO sobre uma funcao inteira. E' precisamente
    o alvo que o plano 19-03 vai migrar, portanto o defeito estava programado
    para so' aparecer quando ja' custasse caro.

    Tres asseveracoes, todas necessarias:
      - a ancora e' encontrada (senao: vermelho falso);
      - a regiao PARA no wrapper (senao: a contagem inclui `__imp_...(ctx)`);
      - a regiao NAO invade a funcao seguinte (o delimitador de fim tem de
        conhecer as tres formas de inicio, nao so' `void func_`)."""
    lift = tmp / "t8_lift"
    lift.mkdir()
    (lift / "ppu_recomp_003.cpp").write_text(
        "PPC_FUNC_IMPL(func_0032E200) {\n"
        + "        ps3_call_opd(ctx, (uint32_t)ctx->gpr[10]);\n" * 7
        + "}\n"
        "PPC_FUNC(func_0032E200) {\n"
        "    __imp_func_0032E200(ctx);\n"
        "}\n"
        "void func_00330D54(ppu_context* ctx) {\n"
        "        ps3_call_opd(ctx, (uint32_t)ctx->gpr[3]);\n"
        "}\n"
    )
    contracts = tmp / "t8_contracts.tsv"
    write_contracts(contracts, [
        "opd-32e200\tpatch_32e200_opd.py\tea\t0x0032E200\tcount_ge\tps3_call_opd(\t7\tteste",
        # count_eq 7 aperta os dois lados: 8 significaria ter apanhado a funcao
        # seguinte; menos de 7 significaria ter parado cedo demais.
        "opd-32e200-exacto\tpatch_32e200_opd.py\tea\t0x0032E200\tcount_eq\tps3_call_opd(\t7\tteste",
        "opd-32e200-sem-imp\tpatch_32e200_opd.py\tea\t0x0032E200\tcount_eq\t__imp_func_\t0\tteste",
    ])
    r = run(contracts, lift, "patch_32e200_opd.py")
    assert "ancora perdida" not in r.stdout, r.stdout
    assert r.returncode == 0, f"rc esperado 0, obtido {r.returncode}: {r.stdout}{r.stderr}"
    assert "[FAIL]" not in r.stdout, r.stdout
    print("[PASS] teste 8: weak override -- ancora encontrada, regiao delimitada "
          "pelo wrapper e nao pela funcao seguinte")


def main() -> int:
    tests = [
        test_1_contrato_ea_passa, test_2_contrato_ea_falha, test_3_ancora_perdida,
        test_4_sem_contrato, test_5_escopo_global, test_6_agulha_regex,
        test_7_erro_de_uso, test_8_weak_override_nao_e_ancora_perdida,
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
