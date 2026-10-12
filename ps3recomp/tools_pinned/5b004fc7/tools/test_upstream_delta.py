#!/usr/bin/env python3
"""Self-test do gate do inventario upstream (Fase 22 / UPST-01).

Porque existe: um validador que so' foi visto a passar nao prova nada -- prova
que sabe dizer "sim". O que interessa e' que saiba **REPROVAR**. Por isso todo
o ficheiro e' prova por mutacao: cada caso parte de um TSV sintetico VALIDO e
estraga-lhe uma coisa de cada vez, exigindo rc=1 **e** o nome do check que caiu.

O controlo negativo (o TSV sintetico intacto sair rc=0) e' obrigatorio: sem ele
um validador que reprovasse tudo passaria todas as mutacoes e o ficheiro nao
estaria a medir mutacao nenhuma.

Hermetico: as mutacoes correm sobre TSVs e ficheiros-ancora sinteticos criados
em tempfile.TemporaryDirectory(), com `--raiz` a apontar para la -- nao dependem
do estado do repo nem do remote `upstream` (que a CI nao tem). Um unico caso
final valida o TSV REAL do repo: e' esse que fica vermelho quando o codigo se
mexe e o inventario deixa de apontar para o sitio certo.

Invariante duro: a suite NAO pode escrever no artefacto que valida -- o sha256
de docs/UPSTREAM_DELTA.tsv e' medido no inicio e reconfirmado no fim.
"""
from __future__ import annotations

import contextlib
import hashlib
import io
import os
import shutil
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import upstream_delta as ud

RAIZ = ud.REPO_ROOT
TSV_REAL = os.path.join(RAIZ, "docs", "UPSTREAM_DELTA.tsv")

FAILS = []


def check(nome, cond, detalhe=""):
    if cond:
        print(f"[delta-test] ok   {nome}")
    else:
        print(f"[delta-test] FAIL {nome} {detalhe}")
        FAILS.append(nome)


def sha256(caminho):
    with open(caminho, "rb") as fh:
        return hashlib.sha256(fh.read()).hexdigest()


# --------------------------------------------------------------- cenario sintetico
CABECALHO = [
    "# UPSTREAM_DELTA.tsv -- sintetico de teste",
    "# medido_em=2026-08-12",
    "# fetch=nao",
    "# ramo_upstream=upstream/fix/ramo-vivo",
    "# ramo_head_sha=abcdef1",
    "# ramo_head_data=2026-08-09",
    "# delta_commits=461",
    "# comando=git rev-list --count HEAD..upstream/fix/ramo-vivo",
    "# nosso_head=1234567",
    "# nosso_delta_commits=1050",
    "# master_congelado=25e5efe (2026-07-16) -- nota, nao e' a base",
    "# nivel_evidencia=medido",
]

# 8 linhas; a prova de cada ancora esta na linha indicada no TSV sintetico.
ALVO = "\n".join([
    "linha 1 preenchimento",
    "linha 2 preenchimento",
    "PROVA_ALFA aqui",                     # linha 3
    "linha 4 preenchimento",
    "PROVA_BETA aqui",                     # linha 5
    "linha 6 preenchimento",
    "PROVA_GAMA aqui",                     # linha 7
    "linha 8 preenchimento",
]) + "\n"


def linha(area, sha, ancora, prova, decisao, evidencia="medido", razao="razao sintetica"):
    return "\t".join([area, sha, "upstream/fix/ramo-vivo", "caminho/deles.c",
                      ancora, prova, decisao, "-", evidencia, razao])


def cenario(tmp, linhas=None):
    """Escreve alvo.c + inventario.tsv validos em `tmp`; devolve o caminho do TSV."""
    with open(os.path.join(tmp, "alvo.c"), "w", encoding="utf-8", newline="\n") as fh:
        fh.write(ALVO)
    if linhas is None:
        linhas = [
            linha("LV2S", "dd936c4", "alvo.c:3", "PROVA_ALFA", "adoptar"),
            linha("GCMF", "684dd71", "alvo.c:4-5", "PROVA_BETA", "rejeitar"),
            linha("LIFT", "6bc76ae", "alvo.c:7", "PROVA_GAMA", "já-temos"),
            linha("TOOLS", "76f80b4", "alvo.c:1", "linha 1", "contribuir-de-volta",
                  "nao-exercitado"),
        ]
    caminho = os.path.join(tmp, "inventario.tsv")
    with open(caminho, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(CABECALHO + ["\t".join(ud.COLUNAS)] + linhas) + "\n")
    return caminho


def correr(tsv, raiz, extra=None):
    """Corre `validate` em processo e devolve (rc, stdout)."""
    argv = ["validate", "--tsv", tsv, "--raiz", raiz] + (extra or [])
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        try:
            rc = ud.main(argv)
        except SystemExit as e:                                   # noqa: PERF203
            rc = e.code if isinstance(e.code, int) else 1
    return rc, buf.getvalue()


def muta(tsv, alvo_sub, novo_sub):
    """Reescreve o TSV trocando a primeira ocorrencia de `alvo_sub`."""
    with open(tsv, "r", encoding="utf-8") as fh:
        s = fh.read()
    assert alvo_sub in s, f"mutacao nao aplicavel: {alvo_sub!r}"
    with open(tsv, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(s.replace(alvo_sub, novo_sub, 1))


# ------------------------------------------------------------------------- casos
def test_controlo_negativo():
    """Sem isto, um validador que reprovasse tudo passava todas as mutacoes."""
    with tempfile.TemporaryDirectory() as tmp:
        rc, out = correr(cenario(tmp), tmp, ["--min-linhas", "4"])
        check("controlo negativo: TSV sintetico valido sai rc=0", rc == 0, out.strip())
        check("controlo negativo: imprime a linha de resumo",
              "[delta] resumo " in out, out.strip())


def test_ancora_prova_ok():
    with tempfile.TemporaryDirectory() as tmp:
        rc, out = correr(cenario(tmp), tmp, ["--min-linhas", "4"])
        check("ANCORA_PROVA: prova dentro do intervalo passa", rc == 0, out.strip())


def test_mutacao_1_sem_ancora():
    with tempfile.TemporaryDirectory() as tmp:
        tsv = cenario(tmp)
        muta(tsv, "\talvo.c:3\t", "\t\t")
        rc, out = correr(tsv, tmp, ["--min-linhas", "4"])
        check("mutacao 1 (linha sem ancora): rc!=0", rc != 0, out.strip())
        check("mutacao 1 nomeia CAMPOS", "CAMPOS" in out, out.strip())


def test_mutacao_2_ancora_deslocada():
    with tempfile.TemporaryDirectory() as tmp:
        tsv = cenario(tmp)
        muta(tsv, "\talvo.c:3\t", "\talvo.c:6\t")          # PROVA_ALFA esta na 3, nao na 6
        rc, out = correr(tsv, tmp, ["--min-linhas", "4"])
        check("mutacao 2 (ancora deslocada): rc!=0", rc != 0, out.strip())
        check("mutacao 2 nomeia ANCORA_DESLOCADA", "ANCORA_DESLOCADA" in out, out.strip())
        check("mutacao 2 diz a LINHA NOVA (accionavel)",
              "linha_actual=3" in out, out.strip())


def test_mutacao_3_decisao_invalida():
    with tempfile.TemporaryDirectory() as tmp:
        tsv = cenario(tmp)
        muta(tsv, "\tadoptar\t", "\ttalvez\t")
        rc, out = correr(tsv, tmp, ["--min-linhas", "4"])
        check("mutacao 3 (decisao fora do vocabulario): rc!=0", rc != 0, out.strip())
        check("mutacao 3 nomeia VOCABULARIO", "VOCABULARIO" in out, out.strip())


def test_mutacao_4_base_master():
    with tempfile.TemporaryDirectory() as tmp:
        tsv = cenario(tmp)
        muta(tsv, "# ramo_upstream=upstream/fix/ramo-vivo",
             "# ramo_upstream=upstream/master")
        rc, out = correr(tsv, tmp, ["--min-linhas", "4"])
        check("mutacao 4 (base = master congelado): rc!=0", rc != 0, out.strip())
        check("mutacao 4 nomeia RAMO_NAO_MASTER", "RAMO_NAO_MASTER" in out, out.strip())


def test_mutacao_5_inventario_esvaziado():
    with tempfile.TemporaryDirectory() as tmp:
        tsv = cenario(tmp, linhas=[
            linha("LV2S", "dd936c4", "alvo.c:3", "PROVA_ALFA", "adoptar"),
            linha("GCMF", "684dd71", "alvo.c:5", "PROVA_BETA", "rejeitar"),
            linha("LIFT", "6bc76ae", "alvo.c:7", "PROVA_GAMA", "já-temos"),
        ])
        rc, out = correr(tsv, tmp, ["--min-linhas", "4"])
        check("mutacao 5 (3 linhas de dados): rc!=0", rc != 0, out.strip())
        check("mutacao 5 nomeia MINIMO_LINHAS", "MINIMO_LINHAS" in out, out.strip())


def test_deriva_total():
    """Prova que desapareceu do ficheiro inteiro: nao ha' linha nova a sugerir."""
    with tempfile.TemporaryDirectory() as tmp:
        tsv = cenario(tmp)
        muta(tsv, "\tPROVA_ALFA\t", "\tPROVA_QUE_NAO_EXISTE\t")
        rc, out = correr(tsv, tmp, ["--min-linhas", "4"])
        check("deriva total: rc!=0", rc != 0, out.strip())
        check("deriva total nomeia ANCORA_PROVA", "ANCORA_PROVA" in out, out.strip())


def test_prova_nao_ascii():
    with tempfile.TemporaryDirectory() as tmp:
        tsv = cenario(tmp)
        muta(tsv, "\tPROVA_ALFA\t", "\tPROVA_ÁLFA\t")
        rc, out = correr(tsv, tmp, ["--min-linhas", "4"])
        check("prova nao-ASCII: rc!=0", rc != 0, out.strip())
        check("prova nao-ASCII nomeia PROVA_ASCII", "PROVA_ASCII" in out, out.strip())


def test_ancora_ficheiro_inexistente():
    with tempfile.TemporaryDirectory() as tmp:
        tsv = cenario(tmp)
        muta(tsv, "\talvo.c:3\t", "\tnao_existe.c:3\t")
        rc, out = correr(tsv, tmp, ["--min-linhas", "4"])
        check("ficheiro de ancora inexistente: rc!=0", rc != 0, out.strip())
        check("nomeia ANCORA_FICHEIRO", "ANCORA_FICHEIRO" in out, out.strip())


def test_fix_anchors():
    """--fix-anchors repara a deriva; sem a flag, `validate` NUNCA escreve."""
    with tempfile.TemporaryDirectory() as tmp:
        tsv = cenario(tmp)
        muta(tsv, "\talvo.c:3\t", "\talvo.c:6\t")
        antes = sha256(tsv)
        rc_seco, _ = correr(tsv, tmp, ["--min-linhas", "4"])
        check("validate sem --fix-anchors nao escreve",
              sha256(tsv) == antes and rc_seco != 0, "o ficheiro mudou")
        rc_fix, out_fix = correr(tsv, tmp, ["--min-linhas", "4", "--fix-anchors"])
        rc2, out2 = correr(tsv, tmp, ["--min-linhas", "4"])
        check("--fix-anchors re-ancora e a 2a validacao sai rc=0",
              rc2 == 0, f"rc_fix={rc_fix} out_fix={out_fix.strip()} out2={out2.strip()}")
        with open(tsv, encoding="utf-8") as fh:
            check("--fix-anchors escreveu a linha correcta (3)",
                  "\talvo.c:3\t" in fh.read(), "")


def test_tsv_real_do_repo():
    """O caso que faz a CI ficar vermelha quando o codigo se mexe."""
    if not os.path.exists(TSV_REAL):
        check("TSV real existe", False, TSV_REAL)
        return
    rc, out = correr(TSV_REAL, RAIZ)
    check("TSV real do repo valida (ancoras ainda apontam para o sitio certo)",
          rc == 0, out.strip())
    check("TSV real imprime resumo", "[delta] resumo " in out, out.strip())


def test_shas_do_repo():
    """Opcional por desenho: a CI nao tem o remote `upstream`."""
    import subprocess
    if not os.path.exists(TSV_REAL):
        return
    _, _, dados = ud.ler_tsv(TSV_REAL)
    faltam = []
    for _, campos in dados:
        if len(campos) != len(ud.COLUNAS):
            continue
        for s in dict(zip(ud.COLUNAS, campos))["sha_upstream"].split(","):
            s = s.strip()
            if not s:
                continue
            r = subprocess.run(["git", "cat-file", "-e", f"{s}^{{commit}}"],
                               cwd=RAIZ, capture_output=True)
            if r.returncode != 0:
                faltam.append(s)
    if faltam:
        print(f"[delta] nota remote upstream ausente ou shas nao alcancaveis "
              f"({','.join(faltam)}) -- shas nao verificados")
    else:
        check("todos os sha_upstream resolvem localmente", True)


def main():
    h0 = sha256(TSV_REAL) if os.path.exists(TSV_REAL) else None

    test_controlo_negativo()
    test_ancora_prova_ok()
    test_mutacao_1_sem_ancora()
    test_mutacao_2_ancora_deslocada()
    test_mutacao_3_decisao_invalida()
    test_mutacao_4_base_master()
    test_mutacao_5_inventario_esvaziado()
    test_deriva_total()
    test_prova_nao_ascii()
    test_ancora_ficheiro_inexistente()
    test_fix_anchors()
    test_tsv_real_do_repo()
    test_shas_do_repo()

    if h0 is not None:
        check("a suite NAO escreveu no TSV de producao (sha256 igual)",
              sha256(TSV_REAL) == h0, "o artefacto validado foi modificado pela suite")

    print(f"\n[delta-test] {'FAILED: ' + ', '.join(FAILS) if FAILS else 'todos os casos passaram'}")
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
