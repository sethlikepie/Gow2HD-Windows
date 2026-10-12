#!/usr/bin/env python3
"""Testes do defeito D3: o gate do MANIFEST le uma migracao como regressao.

Porque existe
-------------
Medido em 2026-08-03 (`games/gow2/notes/2026-08-03-seis-patches-que-nao-reaplicam.md` §5):
o `patch_ce03c_introseq_block.py` esta `migrated` no ledger -- os seus 8 marcadores
`[INTROSEQ]` mudaram de casa, do texto do lift para dois ficheiros host VERSIONADOS
(`games/gow2/hooks/gow2_midasm_hooks.cpp`, 5; `gow2_func_overrides.cpp`, 3).
**8 de 8 contabilizados, zero perdidos.** Mas `gen_manifest.py --verify` so' conta
nos `ppu_recomp_*.cpp`, por isso reporta `A MENOS TAG [INTROSEQ] (esperado>=18,
encontrado 10)` -- divida FALSA.

O custo sistemico e' pior do que os 8: **cada migracao futura para mecanismo v1.3
(mid-asm / weak override) vai gerar outra divida falsa**. Um gate que acusa
sucessos ensina toda a gente a ignora-lo -- e o v1.3 existe precisamente para
promover migracoes.

Prova por MUTACAO, nos dois sentidos (a unica que distingue "conta os hooks" de
"deixou de contar"):
  VERDE  -- migracao legitima: marcador vivo nos hooks, ausente do lift -> sem achado
  VERMELHO -- apagar um marcador do hook -> achado NOVO, gate falha

Uso:  .venv/bin/python3 test_gen_manifest_hooks.py
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
GEN = HERE / "gen_manifest.py"
GATE = HERE / "manifest_delta_gate.py"
HOOKS_REAIS = HERE.parent / "hooks"

_results: list[tuple[str, bool, str]] = []


def check(nome: str, ok: bool, detalhe: str = "") -> None:
    _results.append((nome, ok, detalhe))
    print(f"[{'PASS' if ok else 'FAIL'}] {nome}" + ("" if ok else f"\n       -> {detalhe}"))


def fixture(td: Path, lift_tags: str, hook_tags: str) -> tuple[Path, Path, Path]:
    """Lift de 1 chunk + dir de hooks + MANIFEST que exige 3x [DEMO]."""
    lift = td / "lift"
    lift.mkdir()
    (lift / "ppu_recomp_000.cpp").write_text(
        "// preambulo\nvoid func_000(ppu_context* ctx) {\n" + lift_tags + "}\n")
    hooks = td / "hooks"
    hooks.mkdir()
    (hooks / "gow2_hooks.cpp").write_text("// host versionado\n" + hook_tags)
    man = td / "MANIFEST.tsv"
    man.write_text("# m\ttipo\tmin\tchunks\n[DEMO]\tTAG\t3\tppu_recomp_000.cpp\n")
    return lift, hooks, man


def gen_verify(lift: Path, man: Path, hooks: Path | None) -> subprocess.CompletedProcess:
    cmd = [sys.executable, str(GEN), str(lift), "--verify", str(man)]
    if hooks is not None:
        cmd += ["--host-sources", str(hooks)]
    else:
        cmd += ["--no-host-sources"]
    return subprocess.run(cmd, capture_output=True, text=True)


# ------------------------------------------------------------ gen_manifest ----
def t_migracao_legitima_e_verde() -> None:
    """1 no lift + 2 nos hooks = 3: a migracao NAO e' divida."""
    with tempfile.TemporaryDirectory() as t:
        lift, hooks, man = fixture(Path(t), 'p("[DEMO] a");\n', 'p("[DEMO] b");\np("[DEMO] c");\n')
        r = gen_verify(lift, man, hooks)
        check("migracao legitima (1 lift + 2 hooks = 3) da VERDE",
              r.returncode == 0 and "A MENOS" not in r.stdout and "AUSENTE" not in r.stdout,
              f"rc={r.returncode} out={r.stdout.strip()!r}")


def t_mutacao_apagar_do_hook_da_vermelho() -> None:
    """MUTACAO: apagar 1 marcador do hook (2 -> 1) tem de acusar."""
    with tempfile.TemporaryDirectory() as t:
        lift, hooks, man = fixture(Path(t), 'p("[DEMO] a");\n', 'p("[DEMO] b");\n')
        r = gen_verify(lift, man, hooks)
        check("MUTACAO: marcador apagado do hook da VERMELHO",
              r.returncode == 1 and "A MENOS" in r.stdout and "[DEMO]" in r.stdout,
              f"rc={r.returncode} out={r.stdout.strip()!r}")


def t_mutacao_hooks_vazios_da_vermelho() -> None:
    """MUTACAO extrema: hooks sem nenhum marcador -> so' o lift conta."""
    with tempfile.TemporaryDirectory() as t:
        lift, hooks, man = fixture(Path(t), 'p("[DEMO] a");\n', '// nada aqui\n')
        r = gen_verify(lift, man, hooks)
        check("MUTACAO: hooks sem marcador nenhum da VERMELHO",
              r.returncode == 1 and "[DEMO]" in r.stdout,
              f"rc={r.returncode} out={r.stdout.strip()!r}")


def t_sem_hooks_e_o_comportamento_antigo() -> None:
    """--no-host-sources reproduz exactamente o gate de antes (retro-compat)."""
    with tempfile.TemporaryDirectory() as t:
        lift, hooks, man = fixture(Path(t), 'p("[DEMO] a");\n', 'p("[DEMO] b");\np("[DEMO] c");\n')
        r = gen_verify(lift, man, None)
        check("--no-host-sources = comportamento pre-D3 (VERMELHO)",
              r.returncode == 1 and "A MENOS" in r.stdout,
              f"rc={r.returncode} out={r.stdout.strip()!r}")


def t_saida_declara_a_origem() -> None:
    """A contagem dos hooks tem de aparecer no relatorio -- uma migracao contada
    em silencio seria trocar uma divida falsa por um verde opaco."""
    with tempfile.TemporaryDirectory() as t:
        lift, hooks, man = fixture(Path(t), 'p("[DEMO] a");\n', 'p("[DEMO] b");\np("[DEMO] c");\n')
        r = gen_verify(lift, man, hooks)
        check("relatorio declara quanto veio dos hooks host",
              "[DEMO]" in r.stdout and "host" in r.stdout.lower(),
              f"out={r.stdout.strip()!r}")


def t_hooks_ausentes_nao_estoiram() -> None:
    with tempfile.TemporaryDirectory() as t:
        lift, hooks, man = fixture(Path(t), 'p("[DEMO] a");\np("[DEMO] b");\np("[DEMO] c");\n', '')
        r = gen_verify(lift, man, Path(t) / "nao_existe")
        check("dir de hooks inexistente: nao estoira, so' nao soma",
              r.returncode == 0, f"rc={r.returncode} out={r.stdout.strip()!r} err={r.stderr.strip()!r}")


# ------------------------------------------------------- gate fim-a-fim -------
def t_gate_delta_propaga_host_sources() -> None:
    """manifest_delta_gate.py tem de ver o mesmo que gen_manifest.py.
    VERDE com a migracao intacta; VERMELHO com o marcador apagado do hook --
    e o VERMELHO tem de ser um id NOVO face a divida congelada (que esta vazia)."""
    with tempfile.TemporaryDirectory() as t:
        td = Path(t)
        lift, hooks, man = fixture(td, 'p("[DEMO] a");\n', 'p("[DEMO] b");\np("[DEMO] c");\n')
        debt = td / "debt.json"
        debt.write_text(json.dumps({"ids": []}))

        base = [sys.executable, str(GATE), str(lift), "--manifest", str(man), "--debt", str(debt)]
        verde = subprocess.run(base + ["--host-sources", str(hooks)],
                               capture_output=True, text=True)
        # mutacao: apagar um marcador do hook
        (hooks / "gow2_hooks.cpp").write_text('// host versionado\np("[DEMO] b");\n')
        vermelho = subprocess.run(base + ["--host-sources", str(hooks)],
                                  capture_output=True, text=True)
        check("gate por delta: verde com migracao, vermelho com mutacao",
              verde.returncode == 0 and vermelho.returncode == 1,
              f"verde rc={verde.returncode} ({verde.stdout.strip()!r}) | "
              f"vermelho rc={vermelho.returncode} ({vermelho.stdout.strip()!r})")


def t_default_aponta_para_os_hooks_reais() -> None:
    """Sem flag nenhuma, o default tem de ser games/gow2/hooks/ -- senao o fix
    nao chega ao caminho de aceite (verify_lift.sh nao passa flags)."""
    with tempfile.TemporaryDirectory() as t:
        td = Path(t)
        lift = td / "lift"
        lift.mkdir()
        (lift / "ppu_recomp_000.cpp").write_text("void func_000(ppu_context* ctx) {}\n")
        man = td / "MANIFEST.tsv"
        man.write_text("# m\ttipo\tmin\tchunks\n[INTROSEQ]\tTAG\t8\tppu_recomp_000.cpp\n")
        r = subprocess.run([sys.executable, str(GEN), str(lift), "--verify", str(man)],
                           capture_output=True, text=True)
        vivos = sum(p.read_text(errors="replace").count("[INTROSEQ]")
                    for p in sorted(HOOKS_REAIS.glob("*.cpp"))) if HOOKS_REAIS.is_dir() else 0
        check("default = games/gow2/hooks/*.cpp (os 8 [INTROSEQ] migrados contam)",
              vivos == 8 and r.returncode == 0,
              f"[INTROSEQ] vivos nos hooks reais={vivos} (esperado 8); "
              f"verify rc={r.returncode} out={r.stdout.strip()!r}")


def main() -> int:
    for fn in (t_migracao_legitima_e_verde, t_mutacao_apagar_do_hook_da_vermelho,
               t_mutacao_hooks_vazios_da_vermelho, t_sem_hooks_e_o_comportamento_antigo,
               t_saida_declara_a_origem, t_hooks_ausentes_nao_estoiram,
               t_gate_delta_propaga_host_sources, t_default_aponta_para_os_hooks_reais):
        try:
            fn()
        except Exception as e:  # noqa: BLE001
            check(fn.__name__, False, f"excepcao: {e!r}")
    falhas = [n for n, ok, _ in _results if not ok]
    print()
    if falhas:
        print(f"{len(falhas)} FALHA(S): " + ", ".join(falhas))
        return 1
    print(f"{len(_results)} testes, todos PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
