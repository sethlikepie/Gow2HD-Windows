#!/usr/bin/env python3
"""Testes de order_patches.py + da ordem efectiva de apply_all_patches.sh.

Porque existe
-------------
Medido em 2026-08-03 (`games/gow2/notes/2026-08-03-seis-patches-que-nao-reaplicam.md`):
dos 6 patches que nao reaplicam a um lift fresco, **nenhum** falha por deriva de
forma do lifter. Cinco falham porque o glob alfabetico corre um patch ANTES do
patch que escreve a ancora de que ele depende:

    patch_2b3d1c_movie_io.py        (glob #33) depende de patch_f2b_multimb_install.py (#62)
    patch_fios_done_yield.py        (#74)      depende de patch_fios_sticky.py         (#87)
    patch_fios_host_pop.py          (#83)      depende de patch_fios_open_probe.py     (#84)
    patch_autoload_chain_probes.py  (#44)      TEM DE CORRER DEPOIS de b71_cb56c       (#47)
    patch_b71_callsite_trace.py     (#46)      TEM DE CORRER DEPOIS de b71_cb56c       (#47)

O rebuild medido em `2026-08-03-rebuild-e2e-medicao.md` provou que os tres
primeiros valem **dois elos** da cadeia de boot (elo 2 -> elo 4). A ordem nao e'
cosmetica: e' a correccao com melhor relacao custo/valor do corpus.

Este ficheiro e' o VERMELHO desse fix: falha enquanto o mecanismo de ordem nao
existir, passa depois. Segue a convencao dos outros test_*.py desta pasta
(script auto-contido, sem pytest -- o .venv do motor nao o tem).

Uso:  .venv/bin/python3 test_order_patches.py
rc=0  todos os [PASS]
rc=1  pelo menos um [FAIL]
"""
from __future__ import annotations

import os
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
SCRIPT = HERE / "order_patches.py"
DEPS_TSV = HERE / "PATCH_DEPS.tsv"
GOW2 = HERE.parent                       # games/gow2
APPLY = GOW2 / "apply_all_patches.sh"

# As cinco relacoes que a analise de 2026-08-03 identificou, como (patch, depende_de).
CINCO_RELACOES = [
    ("patch_2b3d1c_movie_io.py", "patch_f2b_multimb_install.py"),
    ("patch_fios_done_yield.py", "patch_fios_sticky.py"),
    ("patch_fios_host_pop.py", "patch_fios_open_probe.py"),
    ("patch_autoload_chain_probes.py", "patch_b71_cb56c_reuse_block.py"),
    ("patch_b71_callsite_trace.py", "patch_b71_cb56c_reuse_block.py"),
]

_results: list[tuple[str, bool, str]] = []


def check(nome: str, ok: bool, detalhe: str = "") -> None:
    _results.append((nome, ok, detalhe))
    print(f"[{'PASS' if ok else 'FAIL'}] {nome}" + ("" if ok else f"\n       -> {detalhe}"))


def skip(nome: str, razao: str) -> None:
    print(f"[SKIP] {nome}\n       -> {razao}")


def run_order(names: list[str], deps_text: str | None = None,
              deps_path: Path | None = None) -> subprocess.CompletedProcess:
    """Invoca order_patches.py como CLI (fim-a-fim do binario, nao das funcoes)."""
    with tempfile.TemporaryDirectory() as td:
        if deps_path is None:
            deps_path = Path(td) / "PATCH_DEPS.tsv"
            deps_path.write_text(deps_text if deps_text is not None else "")
        return subprocess.run(
            [sys.executable, str(SCRIPT), "--deps", str(deps_path)],
            input="\n".join(names) + "\n", capture_output=True, text=True,
        )


def ordem(proc: subprocess.CompletedProcess) -> list[str]:
    return [ln for ln in proc.stdout.splitlines() if ln.strip()]


# ---------------------------------------------------------------- unidade ----
def t_sem_deps_preserva_ordem() -> None:
    nomes = ["patch_a.py", "patch_b.py", "patch_c.py"]
    p = run_order(nomes, deps_text="# so comentarios\n")
    check("sem deps -> ordem do glob intacta",
          p.returncode == 0 and ordem(p) == nomes,
          f"rc={p.returncode} out={ordem(p)} err={p.stderr.strip()}")


def t_dependencia_move_o_dependente() -> None:
    nomes = ["patch_a.py", "patch_b.py", "patch_c.py"]
    p = run_order(nomes, deps_text="patch_a.py\tpatch_c.py\tancora escrita por c\n")
    check("dependente corre DEPOIS da sua dependencia",
          p.returncode == 0 and ordem(p).index("patch_a.py") > ordem(p).index("patch_c.py"),
          f"rc={p.returncode} out={ordem(p)}")


def t_ordem_estavel_move_o_minimo() -> None:
    """So' o dependente se move; tudo o resto mantem a posicao relativa."""
    nomes = [f"patch_{c}.py" for c in "abcde"]
    p = run_order(nomes, deps_text="patch_a.py\tpatch_d.py\t\n")
    esperado = ["patch_b.py", "patch_c.py", "patch_d.py", "patch_a.py", "patch_e.py"]
    check("ordem estavel: move o minimo necessario",
          p.returncode == 0 and ordem(p) == esperado,
          f"esperado={esperado} obtido={ordem(p)}")


def t_transitiva() -> None:
    nomes = ["patch_a.py", "patch_b.py", "patch_c.py"]
    deps = "patch_a.py\tpatch_b.py\t\npatch_b.py\tpatch_c.py\t\n"
    p = run_order(nomes, deps_text=deps)
    o = ordem(p)
    ok = p.returncode == 0 and o.index("patch_c.py") < o.index("patch_b.py") < o.index("patch_a.py")
    check("dependencia transitiva respeitada", ok, f"rc={p.returncode} out={o}")


def t_dependencia_desconhecida_nao_e_fatal() -> None:
    """Um patch pode nao existir neste host (SKIP_LIST, corpus podado):
    a relacao e' ignorada com AVISO, nunca faz o corpus inteiro abortar."""
    nomes = ["patch_a.py", "patch_b.py"]
    p = run_order(nomes, deps_text="patch_a.py\tpatch_inexistente.py\t\n")
    check("dependencia para patch ausente: aviso, nao erro",
          p.returncode == 0 and ordem(p) == nomes and "patch_inexistente.py" in p.stderr,
          f"rc={p.returncode} out={ordem(p)} err={p.stderr.strip()!r}")


def t_ciclo_falha_alto() -> None:
    nomes = ["patch_a.py", "patch_b.py"]
    deps = "patch_a.py\tpatch_b.py\t\npatch_b.py\tpatch_a.py\t\n"
    p = run_order(nomes, deps_text=deps)
    saida = p.stdout + p.stderr
    check("ciclo falha ALTO (rc!=0), nunca em silencio",
          p.returncode != 0 and "patch_a.py" in saida and "patch_b.py" in saida,
          f"rc={p.returncode} saida={saida.strip()!r}")


def t_idempotente() -> None:
    """A ordem e' ponto fixo: reordenar uma lista ja ordenada nao a muda.
    E' o analogo, ao nivel da ordem, da convergencia que a arvore tem de ter."""
    nomes = [f"patch_{c}.py" for c in "abcde"]
    deps = "patch_a.py\tpatch_d.py\t\npatch_b.py\tpatch_e.py\t\n"
    p1 = run_order(nomes, deps_text=deps)
    p2 = run_order(ordem(p1), deps_text=deps)
    check("ordenar duas vezes da a mesma ordem (ponto fixo)",
          p1.returncode == 0 and p2.returncode == 0 and ordem(p1) == ordem(p2),
          f"1a={ordem(p1)} 2a={ordem(p2)}")


# ------------------------------------------------------- corpus real (e2e) ----
def _patch_dir() -> Path | None:
    for cand in (GOW2 / "recomp_mid_v2",
                 HERE.parents[3] / "gow2-recomp" / "recomp_mid_v2"):
        if cand.is_dir() and list(cand.glob("patch_*.py")):
            return cand
    return None


def _env_apply(pdir: Path) -> dict:
    """Env para invocar apply_all_patches.sh.

    PS3_ENGINE_ROOT explicito de proposito: o default do script e'
    `$REPO/../ps3recomp`, que so' resolve quando ele e' invocado da COPIA do
    checkout de build (`../gow2-recomp/`). A partir da copia do monorepo o mesmo
    default aponta para `games/ps3recomp`, que nao existe -- o mesmo quirk que
    docs/RELIFT_CANONICAL.md §0 ja' documenta para accept_relift.sh. Declara-se
    aqui em vez de mudar a resolucao do script (mudanca fora do ambito e que
    divergiria do que esta escrito).
    """
    return dict(os.environ, PS3_PATCH_DIR=str(pdir),
                PS3_ENGINE_ROOT=str(HERE.parents[2]))


def t_deps_tsv_so_cita_patches_existentes() -> None:
    """Guarda contra gralha: uma relacao com nome errado seria um no-op silencioso."""
    pdir = _patch_dir()
    if pdir is None:
        skip("PATCH_DEPS.tsv so cita patches existentes", "corpus de patches nao encontrado")
        return
    if not DEPS_TSV.exists():
        check("PATCH_DEPS.tsv so cita patches existentes", False, f"ausente: {DEPS_TSV}")
        return
    existentes = {p.name for p in pdir.glob("patch_*.py")}
    faltam = []
    for line in DEPS_TSV.read_text().splitlines():
        if line.startswith("#") or not line.strip():
            continue
        cols = line.split("\t")
        for nome in cols[:2]:
            if nome and nome not in existentes:
                faltam.append(nome)
    check("PATCH_DEPS.tsv so cita patches existentes", not faltam,
          "nomes que nao existem em " + str(pdir) + ": " + ", ".join(sorted(set(faltam))))


def t_cinco_relacoes_na_ordem_efectiva() -> None:
    """O teste que importa: a ordem que apply_all_patches.sh REALMENTE usa."""
    pdir = _patch_dir()
    if pdir is None:
        skip("as 5 relacoes na ordem efectiva de apply_all_patches.sh",
             "corpus de patches nao encontrado")
        return
    env = _env_apply(pdir)
    p = subprocess.run(["bash", str(APPLY), "--print-order"],
                       capture_output=True, text=True, env=env)
    ordem_ef = [ln.strip() for ln in p.stdout.splitlines() if ln.strip().startswith("patch_")]
    if p.returncode != 0 or not ordem_ef:
        check("as 5 relacoes na ordem efectiva de apply_all_patches.sh", False,
              f"--print-order rc={p.returncode}; stdout={p.stdout[:400]!r} stderr={p.stderr[:400]!r}")
        return
    quebradas = []
    for alvo, dep in CINCO_RELACOES:
        if alvo not in ordem_ef or dep not in ordem_ef:
            quebradas.append(f"{alvo} ou {dep} ausente do corpus")
        elif ordem_ef.index(dep) >= ordem_ef.index(alvo):
            quebradas.append(
                f"{alvo} (#{ordem_ef.index(alvo)+1}) corre ANTES de {dep} (#{ordem_ef.index(dep)+1})")
    check("as 5 relacoes na ordem efectiva de apply_all_patches.sh", not quebradas,
          "; ".join(quebradas))


def t_ordem_efectiva_e_permutacao_do_glob() -> None:
    """Nenhum patch pode desaparecer nem duplicar por causa da reordenacao."""
    pdir = _patch_dir()
    if pdir is None:
        skip("ordem efectiva e' permutacao exacta do glob", "corpus nao encontrado")
        return
    env = _env_apply(pdir)
    p = subprocess.run(["bash", str(APPLY), "--print-order"],
                       capture_output=True, text=True, env=env)
    ordem_ef = [ln.strip() for ln in p.stdout.splitlines() if ln.strip().startswith("patch_")]
    glob_nomes = sorted(x.name for x in pdir.glob("patch_*.py"))
    check("ordem efectiva e' permutacao exacta do glob",
          p.returncode == 0 and sorted(ordem_ef) == glob_nomes
          and len(ordem_ef) == len(glob_nomes),
          f"glob={len(glob_nomes)} ordenados={len(ordem_ef)} "
          f"perdidos={sorted(set(glob_nomes) - set(ordem_ef))} "
          f"extra={sorted(set(ordem_ef) - set(glob_nomes))}")


def main() -> int:
    if not SCRIPT.exists():
        print(f"[FAIL] order_patches.py existe\n       -> ausente: {SCRIPT}")
        print("\n1 FALHA(S)")
        return 1
    for fn in (t_sem_deps_preserva_ordem, t_dependencia_move_o_dependente,
               t_ordem_estavel_move_o_minimo, t_transitiva,
               t_dependencia_desconhecida_nao_e_fatal, t_ciclo_falha_alto,
               t_idempotente, t_deps_tsv_so_cita_patches_existentes,
               t_cinco_relacoes_na_ordem_efectiva,
               t_ordem_efectiva_e_permutacao_do_glob):
        try:
            fn()
        except Exception as e:  # noqa: BLE001 -- um teste que estoura e' um FAIL
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
