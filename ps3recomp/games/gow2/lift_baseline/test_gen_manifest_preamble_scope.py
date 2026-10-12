#!/usr/bin/env python3
"""Testes do contrato do marcador PREAMBLE: o limiar mede o PREAMBULO.

Porque existe
-------------
Medido em 2026-08-03 (`games/gow2/notes/2026-08-03-trampoline-fn-deficit.md` §2 e §10):
o marcador `g_trampoline_fn` e' de tipo `PREAMBLE`, e o proprio `gen_manifest.py`
declara o que esse tipo existe para verificar -- *"confirma que o preambulo puro
(nao-injectado) SOBREVIVE no chunk alvo"*. Mas a implementacao conta o ficheiro
INTEIRO, e o limiar congelado (168753) e' a contagem do ficheiro inteiro:

    ocorrencias no preambulo : 56      (producao)  ==  56  (lift fresco)
    ocorrencias no corpo     : 168697  (producao)  vs 168379 (lift fresco)

**56 de 168753 (0,03 %) medem o proposito; os outros 99,97 % medem a FORMA do
lifter** -- quantos saltos cross-fragment ele decidiu emitir como trampolim.
Qualquer melhoria de fronteiras mexe-lhe: o fix `0585636` (2026-07-30, registo-base
da jump table) recuperou 22 dispatchers, nao perdeu um unico dos 125 que ja'
existiam, e mesmo assim o gate acusou -318 como perda.

O defeito NAO e' o valor -- e' o contrato. Bumpar 168753 -> 168435 re-armaria a
mesma armadilha ao proximo fix de fronteiras. A correccao e' o marcador contar
onde diz que conta, declarado marcador-a-marcador pela nota `AMBITO:preambulo`
(5a coluna do MANIFEST.tsv, aditiva e retro-compativel como `OBSOLETO:`/`GRUPO:`).

**Alcance deliberado: so' os marcadores com a nota.** `ps3_indirect_call` tem o
MESMO defeito de contrato (preambulo=7, limiar 15759) mas a sua correccao e' uma
decisao a parte, com prova a parte -- mexe no grupo `opd-dispatch`. Aqui nao se
toca: o teste `t_sem_nota_e_o_comportamento_antigo` existe precisamente para
garantir que nao se tocou.

Prova por MUTACAO, nos dois sentidos (a unica que distingue um gate de um adorno):
  VERMELHO -- apagar o marcador do PREAMBULO -> achado, gate falha
  VERDE    -- melhoria legitima do lifter (corpo encolhe, preambulo intacto) -> sem achado
              (e o mesmo corpus da VERMELHO sob o contrato antigo -- e' a armadilha)

Uso:  .venv/bin/python3 test_gen_manifest_preamble_scope.py
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
MANIFEST_REAL = HERE / "MANIFEST.tsv"
# Lifts reais (nao versionados). Os testes que dependem deles SALTAM se nao
# existirem -- uma maquina sem os artefactos tem de poder correr a suite.
LIFT_PROD = HERE.parent.parent.parent.parent / "gow2-recomp" / "recomp_macos_v2"

_results: list[tuple[str, bool, str]] = []


def check(nome: str, ok: bool, detalhe: str = "") -> None:
    _results.append((nome, ok, detalhe))
    print(f"[{'PASS' if ok else 'FAIL'}] {nome}" + ("" if ok else f"\n       -> {detalhe}"))


def skip(nome: str, razao: str) -> None:
    print(f"[SKIP] {nome}\n       -> {razao}")


def fixture(td: Path, n_preambulo: int, n_corpo: int, nota: str, limiar: int,
            marcador: str = "g_demo_tramp") -> tuple[Path, Path]:
    """Lift de 1 chunk com `n_preambulo` ocorrencias antes da 1a funcao e
    `n_corpo` depois. `preamble_end()` corta na 1a linha `void func_`."""
    lift = td / "lift"
    lift.mkdir(parents=True, exist_ok=True)
    preamb = "".join(f"static void* {marcador}_slot{i};\n" for i in range(n_preambulo))
    corpo = "".join(f"  {marcador} = (void*)&f{i};\n" for i in range(n_corpo))
    (lift / "ppu_recomp_000.cpp").write_text(
        "// preambulo puro\n" + preamb + "void func_000(ppu_context* ctx) {\n" + corpo + "}\n")
    man = td / "MANIFEST.tsv"
    linha = f"{marcador}\tPREAMBLE\t{limiar}\tppu_recomp_000.cpp"
    if nota:
        linha += f"\t{nota}"
    man.write_text("# marcador\ttipo\tmin_count\tchunks_origem\tnota\n" + linha + "\n")
    return lift, man


def gen_verify(lift: Path, man: Path) -> subprocess.CompletedProcess:
    return subprocess.run(
        [sys.executable, str(GEN), str(lift), "--verify", str(man), "--no-host-sources"],
        capture_output=True, text=True)


# ------------------------------------------------------- o contrato novo ------
def t_ambito_conta_so_o_preambulo() -> None:
    """8 no preambulo + 500 no corpo, limiar 8 com AMBITO:preambulo -> VERDE."""
    with tempfile.TemporaryDirectory() as t:
        lift, man = fixture(Path(t), 8, 500, "AMBITO:preambulo razao declarada", 8)
        r = gen_verify(lift, man)
        check("AMBITO:preambulo conta so' o preambulo (8+500, limiar 8) -> VERDE",
              r.returncode == 0 and "A MENOS" not in r.stdout and "AUSENTE" not in r.stdout,
              f"rc={r.returncode} out={r.stdout.strip()!r}")


def t_ambito_ignora_o_corpo_no_limiar() -> None:
    """O corpo NAO pode satisfazer o limiar: 1 no preambulo, limiar 8 -> VERMELHO
    apesar das 500 ocorrencias no corpo. E' o inverso do teste anterior e e' o
    que prova que a contagem mudou de ambito (e nao so' de valor)."""
    with tempfile.TemporaryDirectory() as t:
        lift, man = fixture(Path(t), 1, 500, "AMBITO:preambulo razao declarada", 8)
        r = gen_verify(lift, man)
        check("AMBITO:preambulo: 500 no corpo NAO satisfazem o limiar do preambulo",
              r.returncode == 1 and "A MENOS" in r.stdout and "encontrado 1" in r.stdout,
              f"rc={r.returncode} out={r.stdout.strip()!r}")


def t_mutacao_apagar_do_preambulo_da_vermelho() -> None:
    """MUTACAO (sentido VERMELHO): o preambulo desaparece, o corpo fica intacto.
    E' exactamente a regressao que o marcador PREAMBLE existe para apanhar --
    e a que o contrato antigo NAO apanhava (500 >= limiar 8, verde)."""
    with tempfile.TemporaryDirectory() as t:
        td = Path(t)
        lift, man = fixture(td, 0, 500, "AMBITO:preambulo razao declarada", 8)
        novo = gen_verify(lift, man)

        # e o mesmo corpus sob o contrato ANTIGO (sem nota, limiar do ficheiro
        # inteiro): passa em verde -- a demonstracao de que o gate antigo era
        # cego a esta regressao.
        lift2, man2 = fixture(td / "antigo", 0, 500, "", 8)
        antigo = gen_verify(lift2, man2)

        check("MUTACAO: preambulo apagado -> VERMELHO (e o contrato antigo dava VERDE)",
              novo.returncode == 1 and "AUSENTE" in novo.stdout and antigo.returncode == 0,
              f"novo rc={novo.returncode} ({novo.stdout.strip()!r}) | "
              f"antigo rc={antigo.returncode} ({antigo.stdout.strip()!r})")


def t_mutacao_melhoria_do_lifter_da_verde() -> None:
    """MUTACAO (sentido VERDE): melhoria legitima do lifter -- o corpo encolhe
    (menos trampolins porque mais dispatchers foram resolvidos), o preambulo fica
    igual. Com AMBITO:preambulo -> VERDE. Sem a nota, com o limiar congelado do
    ficheiro inteiro (508) -> VERMELHO: e' a armadilha, reproduzida."""
    with tempfile.TemporaryDirectory() as t:
        td = Path(t)
        lift, man = fixture(td, 8, 100, "AMBITO:preambulo razao declarada", 8)
        novo = gen_verify(lift, man)

        lift2, man2 = fixture(td / "antigo", 8, 100, "", 508)
        antigo = gen_verify(lift2, man2)

        check("MUTACAO: melhoria do lifter (corpo 500->100) -> VERDE (antigo: VERMELHO)",
              novo.returncode == 0 and antigo.returncode == 1 and "A MENOS" in antigo.stdout,
              f"novo rc={novo.returncode} ({novo.stdout.strip()!r}) | "
              f"antigo rc={antigo.returncode} ({antigo.stdout.strip()!r})")


def t_sem_nota_e_o_comportamento_antigo() -> None:
    """RETRO-COMPATIBILIDADE: um PREAMBLE SEM a nota conta o ficheiro inteiro,
    byte a byte como antes. E' o que mantem `ps3_indirect_call` (limiar 15759,
    grupo opd-dispatch) intocado por esta mudanca."""
    with tempfile.TemporaryDirectory() as t:
        lift, man = fixture(Path(t), 8, 500, "", 508)
        r = gen_verify(lift, man)
        check("sem nota: PREAMBLE conta o ficheiro inteiro (508) -- pre-existente intacto",
              r.returncode == 0 and "A MENOS" not in r.stdout,
              f"rc={r.returncode} out={r.stdout.strip()!r}")


def t_nota_aparece_no_relatorio() -> None:
    """Um ambito diferente contado em silencio seria um verde opaco. O relatorio
    tem de dizer que aquele marcador foi medido no preambulo."""
    with tempfile.TemporaryDirectory() as t:
        lift, man = fixture(Path(t), 8, 500, "AMBITO:preambulo razao declarada", 8)
        r = gen_verify(lift, man)
        check("relatorio declara o ambito preambulo do marcador",
              "AMBITO" in r.stdout.upper() and "g_demo_tramp" in r.stdout,
              f"out={r.stdout.strip()!r}")


def t_gate_delta_ve_o_mesmo() -> None:
    """manifest_delta_gate.py (o rc autoritativo de verify_lift.sh) tem de ver o
    mesmo que gen_manifest.py -- verde com o preambulo intacto, vermelho sem ele."""
    with tempfile.TemporaryDirectory() as t:
        td = Path(t)
        debt = td / "debt.json"
        debt.write_text(json.dumps({"ids": []}))

        lift, man = fixture(td, 8, 500, "AMBITO:preambulo razao declarada", 8)
        verde = subprocess.run(
            [sys.executable, str(GATE), str(lift), "--manifest", str(man),
             "--debt", str(debt), "--no-host-sources"], capture_output=True, text=True)

        lift2, man2 = fixture(td / "mut", 0, 500, "AMBITO:preambulo razao declarada", 8)
        vermelho = subprocess.run(
            [sys.executable, str(GATE), str(lift2), "--manifest", str(man2),
             "--debt", str(debt), "--no-host-sources"], capture_output=True, text=True)

        check("gate por delta: verde com preambulo, vermelho sem ele",
              verde.returncode == 0 and vermelho.returncode == 1,
              f"verde rc={verde.returncode} ({verde.stdout.strip()!r}) | "
              f"vermelho rc={vermelho.returncode} ({vermelho.stdout.strip()!r})")


# ------------------------------------------------ o corpus real (se existir) --
def t_corpus_real_producao_verde() -> None:
    """A producao (recomp_macos_v2) continua a passar o marcador rebaselinado --
    a nao-regressao do lift que este gate protege hoje."""
    nome = "corpus real: recomp_macos_v2 passa g_trampoline_fn com o limiar novo"
    if not LIFT_PROD.is_dir():
        skip(nome, f"{LIFT_PROD} nao existe (artefacto nao versionado)")
        return
    r = subprocess.run([sys.executable, str(GEN), str(LIFT_PROD), "--verify", str(MANIFEST_REAL)],
                       capture_output=True, text=True)
    linhas = [ln for ln in r.stdout.splitlines() if "g_trampoline_fn" in ln
              and (ln.startswith("A MENOS") or ln.startswith("AUSENTE"))]
    check(nome, not linhas, f"achados: {linhas!r}")


def t_corpus_real_contagem_do_preambulo() -> None:
    """O 56 do MANIFEST tem de ser reproduzivel por quem ler a linha -- e' para
    isso que existe `--preamble-counts`."""
    nome = "corpus real: --preamble-counts reproduz o 56 da producao"
    if not LIFT_PROD.is_dir():
        skip(nome, f"{LIFT_PROD} nao existe (artefacto nao versionado)")
        return
    r = subprocess.run([sys.executable, str(GEN), str(LIFT_PROD), "--preamble-counts"],
                       capture_output=True, text=True)
    linha = [ln for ln in r.stdout.splitlines() if ln.startswith("g_trampoline_fn\t")]
    ok = bool(linha) and linha[0].split("\t")[1] == "56"
    check(nome, ok, f"rc={r.returncode} linha={linha!r} out={r.stdout.strip()[:300]!r}")


def main() -> int:
    for fn in (t_ambito_conta_so_o_preambulo,
               t_ambito_ignora_o_corpo_no_limiar,
               t_mutacao_apagar_do_preambulo_da_vermelho,
               t_mutacao_melhoria_do_lifter_da_verde,
               t_sem_nota_e_o_comportamento_antigo,
               t_nota_aparece_no_relatorio,
               t_gate_delta_ve_o_mesmo,
               t_corpus_real_producao_verde,
               t_corpus_real_contagem_do_preambulo):
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
