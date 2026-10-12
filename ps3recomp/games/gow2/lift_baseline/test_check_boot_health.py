#!/usr/bin/env python3
"""Testes do criterio "NAO PIOR QUE A PRODUCAO" (check_boot_health.py).

Porque existe
-------------
Dois criterios de aceite de um re-lift estavam escritos contra um ideal que a
producao nao cumpre -- `ICALL-BAD=0` (a producao tem 12) e o elo `nenhum` da
perna 4 do `accept_relift.sh` (a producao para no elo 4, e "AUTO_LOAD nunca
criada" e' o comportamento CORRECTO de um jogo que ainda esta a correr, medido
em `lib_boot_chain_metrics.sh:94-118`). Um gate que nasce vermelho e' um gate
que ninguem le.

A correccao so' vale se o gate continuar a saber dizer NAO. Por isso os testes
sao de MUTACAO, nos dois sentidos:
  VERDE     -- candidato igual a producao, ou melhor -> ACEITE
  VERMELHO  -- uma unica unidade pior em qualquer metrica -> REJEITADO
              (e o binario do E2E, dois elos abaixo, tem de ser recusado)

Uso:  .venv/bin/python3 test_check_boot_health.py
rc=0  todos os [PASS]
rc=1  pelo menos um [FAIL]
"""
from __future__ import annotations

import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
CHECK = HERE / "check_boot_health.py"
REF_REAL = HERE / "PRODUCTION_REFERENCE.tsv"
# Corpus real da leva anterior (nao versionado, em /tmp): os testes que
# dependem dele SALTAM se ja' tiver sido limpo.
TSV_E2E_ANTIGO = Path("/tmp/ctl_e2e_old.tsv")

CABECALHO = ("run\tst620\tstartseq\tnopic\tthr_created\tthr_end\tr_perma\t"
             "setflip_after_rperm\tpad_total\telo_stopped\tclass")

_results: list[tuple[str, bool, str]] = []


def check(nome: str, ok: bool, detalhe: str = "") -> None:
    _results.append((nome, ok, detalhe))
    print(f"[{'PASS' if ok else 'FAIL'}] {nome}" + ("" if ok else f"\n       -> {detalhe}"))


def skip(nome: str, razao: str) -> None:
    print(f"[SKIP] {nome}\n       -> {razao}")


def chain_tsv(td: Path, elos: list[str], st620: int = 11) -> Path:
    p = td / "chain.tsv"
    linhas = [CABECALHO]
    for i, elo in enumerate(elos, 1):
        cls = "OK" if elo == "nenhum" else "REGRESSAO"
        linhas.append(f"{i}\t{st620}\t2\t4\t0\t0\t1\t0\t0\t{elo}\t{cls}")
    p.write_text("\n".join(linhas) + "\n")
    return p


def logs(td: Path, n: int, icall: int = 12, oob: int = 19, ffff: int = 16,
         fatal_addr: str = "0x000B9354") -> list[Path]:
    out = []
    for i in range(n):
        p = td / f"run{i}.log"
        corpo = (["[ICALL-BAD] ctr=0x0 lr=0x0"] * icall
                 + ["[vm] OOB access 0x00001234 (+4)"] * (oob - ffff)
                 + ["[vm] OOB access 0xFFFF0E00 (+4)"] * ffff)
        if fatal_addr:
            corpo.append(f"[ppu] FATAL: stuck calling {fatal_addr} (2000 times) -- aborting run")
        p.write_text("\n".join(corpo) + "\n")
        out.append(p)
    return out


def correr(tsv: Path, log_paths: list[Path], ref: Path = REF_REAL) -> subprocess.CompletedProcess:
    cmd = [sys.executable, str(CHECK), "--chain-tsv", str(tsv), "--reference", str(ref)]
    for l in log_paths:
        cmd += ["--log", str(l)]
    return subprocess.run(cmd, capture_output=True, text=True)


# ------------------------------------------------------------------ VERDE ----
def t_igual_a_producao_e_aceite() -> None:
    with tempfile.TemporaryDirectory() as t:
        td = Path(t)
        tsv = chain_tsv(td, ["AUTO_LOAD (nunca criada)"] * 6)
        r = correr(tsv, logs(td, 6))
        check("candidato IGUAL a producao (elo 4, icall 12, oob 19) -> ACEITE",
              r.returncode == 0 and "ACEITE" in r.stdout,
              f"rc={r.returncode} out={r.stdout[-400:]!r}")


def t_melhor_que_a_producao_e_aceite() -> None:
    with tempfile.TemporaryDirectory() as t:
        td = Path(t)
        tsv = chain_tsv(td, ["AUTO_LOAD (nunca criada)"] * 6)
        r = correr(tsv, logs(td, 6, icall=12, oob=1, ffff=1))
        check("candidato MELHOR (oob 1 contra 19) -> ACEITE",
              r.returncode == 0, f"rc={r.returncode} out={r.stdout[-400:]!r}")


def t_elo_no_limiar_e_aceite() -> None:
    """4 de 6 no elo 4 e' exactamente o limiar (RUNS*2+2)/3 -- tem de passar."""
    with tempfile.TemporaryDirectory() as t:
        td = Path(t)
        tsv = chain_tsv(td, ["AUTO_LOAD (nunca criada)"] * 4 + ["intro (st620)"] * 2)
        r = correr(tsv, logs(td, 6))
        check("elo 4 em 4/6 (o limiar exacto) -> ACEITE",
              r.returncode == 0, f"rc={r.returncode} out={r.stdout[-400:]!r}")


# --------------------------------------------------------------- VERMELHO ----
def t_mutacao_um_icall_a_mais_e_rejeitado() -> None:
    """UMA unidade pior. Se o gate nao apanhar isto, nao gateia nada."""
    with tempfile.TemporaryDirectory() as t:
        td = Path(t)
        tsv = chain_tsv(td, ["AUTO_LOAD (nunca criada)"] * 6)
        r = correr(tsv, logs(td, 6, icall=13))
        check("MUTACAO: ICALL-BAD 13 (um a mais que a producao) -> REJEITADO",
              r.returncode == 1 and "icall_bad" in r.stdout and "REJEITADO" in r.stdout,
              f"rc={r.returncode} out={r.stdout[-400:]!r}")


def t_mutacao_um_fatal_a_mais_e_rejeitado() -> None:
    with tempfile.TemporaryDirectory() as t:
        td = Path(t)
        tsv = chain_tsv(td, ["AUTO_LOAD (nunca criada)"] * 6)
        ls = logs(td, 6)
        ls[0].write_text(ls[0].read_text()
                         + "[ppu] FATAL: stuck calling 0x00000001 (2000 times)\n")
        r = correr(tsv, ls)
        check("MUTACAO: 2 FATAL numa corrida (producao tem 1) -> REJEITADO",
              r.returncode == 1 and "fatal" in r.stdout,
              f"rc={r.returncode} out={r.stdout[-400:]!r}")


def t_mutacao_dois_elos_abaixo_e_rejeitado() -> None:
    """O binario do E2E parava no elo 2 (2o movie). Tem de ser recusado --
    e' precisamente a regressao que a perna 4 existe para apanhar."""
    with tempfile.TemporaryDirectory() as t:
        td = Path(t)
        tsv = chain_tsv(td, ["2o movie (StartSeq)"] * 6)
        r = correr(tsv, logs(td, 6, icall=0, oob=0, ffff=0, fatal_addr=""))
        check("MUTACAO: elo 2 em 6/6 (o binario do E2E) -> REJEITADO",
              r.returncode == 1 and "elo_atingido" in r.stdout,
              f"rc={r.returncode} out={r.stdout[-400:]!r}")


def t_elo_abaixo_do_limiar_e_rejeitado() -> None:
    with tempfile.TemporaryDirectory() as t:
        td = Path(t)
        tsv = chain_tsv(td, ["AUTO_LOAD (nunca criada)"] * 3 + ["intro (st620)"] * 3)
        r = correr(tsv, logs(td, 6))
        check("elo 4 em 3/6 (abaixo do limiar 4) -> REJEITADO",
              r.returncode == 1, f"rc={r.returncode} out={r.stdout[-400:]!r}")


def t_sem_logs_nunca_passa() -> None:
    """Um gate sem medicao nunca passa -- senao bastava nao dar logs."""
    with tempfile.TemporaryDirectory() as t:
        td = Path(t)
        tsv = chain_tsv(td, ["AUTO_LOAD (nunca criada)"] * 6)
        r = correr(tsv, [])
        check("sem logs -> REJEITADO (um gate sem medicao nunca passa)",
              r.returncode == 1 and "SEM LOGS" in r.stdout,
              f"rc={r.returncode} out={r.stdout[-400:]!r}")


def t_elo_desconhecido_nao_passa_em_silencio() -> None:
    """Se o instrumento mudar de vocabulario, o gate tem de falhar, nao passar."""
    with tempfile.TemporaryDirectory() as t:
        td = Path(t)
        tsv = chain_tsv(td, ["um elo que nunca existiu"] * 6)
        r = correr(tsv, logs(td, 6))
        check("elo_stopped desconhecido -> REJEITADO (nunca tratado como sucesso)",
              r.returncode == 1, f"rc={r.returncode} out={r.stdout[-400:]!r}")


# ------------------------------------------------- contrato do ficheiro ------
def t_referencia_tem_data_e_proveniencia() -> None:
    """A regra desta leva: o valor de referencia fica DECLARADO com data e
    proveniencia, nunca hard-coded sem explicacao."""
    linhas = [l for l in REF_REAL.read_text().splitlines()
              if l.strip() and not l.startswith("#") and not l.startswith("metrica")]
    maus = [l.split("\t")[0] for l in linhas
            if len(l.split("\t")) < 6 or not l.split("\t")[4].strip()
            or len(l.split("\t")[5]) < 40]
    check("PRODUCTION_REFERENCE.tsv: toda a linha tem data e proveniencia substantiva",
          not maus and len(linhas) >= 6, f"linhas sem proveniencia: {maus!r} (total {len(linhas)})")


def t_corpus_real_e2e_antigo_rejeitado() -> None:
    nome = "corpus real: o TSV do binario do E2E (elo 2) e' REJEITADO"
    if not TSV_E2E_ANTIGO.is_file():
        skip(nome, f"{TSV_E2E_ANTIGO} ja' nao existe (log nao versionado, /tmp)")
        return
    with tempfile.TemporaryDirectory() as t:
        r = correr(TSV_E2E_ANTIGO, logs(Path(t), 1, icall=0, oob=0, ffff=0, fatal_addr=""))
        check(nome, r.returncode == 1, f"rc={r.returncode} out={r.stdout[-400:]!r}")


def main() -> int:
    for fn in (t_igual_a_producao_e_aceite, t_melhor_que_a_producao_e_aceite,
               t_elo_no_limiar_e_aceite, t_mutacao_um_icall_a_mais_e_rejeitado,
               t_mutacao_um_fatal_a_mais_e_rejeitado, t_mutacao_dois_elos_abaixo_e_rejeitado,
               t_elo_abaixo_do_limiar_e_rejeitado, t_sem_logs_nunca_passa,
               t_elo_desconhecido_nao_passa_em_silencio,
               t_referencia_tem_data_e_proveniencia, t_corpus_real_e2e_antigo_rejeitado):
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
