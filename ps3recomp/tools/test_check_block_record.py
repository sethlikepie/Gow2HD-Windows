#!/usr/bin/env python3
"""Self-test do verificador de registos de bloco (Fase 23 / UPST-02).

Porque existe: um verificador que so' foi visto a passar prova apenas que sabe
dizer "sim". O que interessa e' que saiba REPROVAR -- e, em particular, que
reprove o registo que FUNDE "a intro nao regrediu" com "o caminho corrigido
funciona", que e' o modo de falha que o criterio 4 da Fase 23 existe para
impedir.

Por isso o ficheiro inteiro e' prova por mutacao: cada caso parte de um registo
sintetico VALIDO e estraga-lhe UMA coisa de cada vez, exigindo `rc=1` **e** o
nome do check que caiu. O controlo positivo (o registo intacto sair `rc=0`) e'
obrigatorio: sem ele, um verificador que reprovasse tudo passaria todas as
mutacoes e este ficheiro nao estaria a medir mutacao nenhuma.

Hermetico: todos os registos sinteticos sao escritos em
`tempfile.TemporaryDirectory()`, NUNCA em `docs/upstream-blocks/`. Um unico caso
final corre o verificador sobre o `docs/upstream-blocks/` REAL do repositorio --
e' esse que fica vermelho quando um registo de bloco novo nasce mal formado.

Invariante duro: a suite NAO escreve no artefacto que valida -- o sha256 de
cada registo real e' medido no inicio e reconfirmado no fim.
"""
from __future__ import annotations

import contextlib
import hashlib
import io
import os
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import check_block_record as cbr

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DIR_REAL = os.path.join(RAIZ, "docs", "upstream-blocks")

FAILS: list[str] = []


def check(nome: str, cond: bool, detalhe: str = "") -> None:
    if cond:
        print(f"[bloco-test] ok   {nome}")
    else:
        print(f"[bloco-test] FAIL {nome} {detalhe}")
        FAILS.append(nome)


def sha256(caminho: str) -> str:
    with open(caminho, "rb") as fh:
        return hashlib.sha256(fh.read()).hexdigest()


def correr(*alvos: str) -> tuple[int, str]:
    """Corre o verificador em processo, capturando stdout+stderr."""
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf), contextlib.redirect_stderr(buf):
        rc = cbr.main(["check_block_record.py", *alvos])
    return rc, buf.getvalue()


# ------------------------------------------------------- registo sintetico valido
REGISTO_VALIDO = """# Bloco XPTO -- um bloco sintetico de teste

- bloco: XPTO
- sha_upstream: abc1234,def5678
- ancora_nossa: runtime/exemplo.c:42
- fase: 23
- data: 2026-08-12
- portao: /tmp/gate/XPTO-01-gate.tsv

## Medicao

| leg | st620 |
|-----|-------|
| antes | 11 |

## O que este portao prova

- A intro nao regrediu entre as duas pontas do bloco [evidencia: in-boot]
- O caminho X escreve o campo Y, provado por teste offline [evidencia: offline-unit]

## O que este portao NAO prova

- Que o caminho corrigido foi exercitado em boot [evidencia: nao-exercitado]
- Que nao ha' perdas a jusante da metrica medida [evidencia: nao-exercitado]

## Nota final

Texto solto que nao e' afirmacao de nenhuma das seccoes.
"""


def cenario(tmp: str, texto: str = REGISTO_VALIDO, nome: str = "XPTO-01.md") -> str:
    caminho = os.path.join(tmp, nome)
    with open(caminho, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(texto)
    return caminho


def muta(texto: str, alvo: str, novo: str) -> str:
    assert alvo in texto, f"mutacao nao aplicavel: {alvo!r}"
    return texto.replace(alvo, novo, 1)


# ------------------------------------------------------------------------- casos
def test_controlo_positivo():
    """Controlo positivo: sem ele, um verificador que reprovasse tudo passava
    todas as mutacoes e a suite nao mediria nada."""
    with tempfile.TemporaryDirectory() as tmp:
        rc, out = correr(cenario(tmp))
        check("controlo positivo: registo sintetico valido sai rc=0", rc == 0, out.strip())
        check("controlo positivo: imprime a linha de resumo",
              "[bloco] resumo " in out, out.strip())


def test_mutacao_1_sem_seccao_nao_prova():
    with tempfile.TemporaryDirectory() as tmp:
        texto = REGISTO_VALIDO.split("## O que este portao NAO prova")[0]
        rc, out = correr(cenario(tmp, texto))
        check("mutacao 1 (sem a seccao NAO prova): rc!=0", rc != 0, out.strip())
        check("mutacao 1 nomeia SECCAO_EM_FALTA", "SECCAO_EM_FALTA" in out, out.strip())


def test_mutacao_2_seccoes_fundidas():
    """O modo de falha que o criterio 4 existe para impedir."""
    with tempfile.TemporaryDirectory() as tmp:
        texto = muta(REGISTO_VALIDO,
                     "## O que este portao prova",
                     "## O que este portao prova e o que NAO prova")
        texto = muta(texto, "## O que este portao NAO prova\n\n", "")
        rc, out = correr(cenario(tmp, texto))
        check("mutacao 2 (as duas seccoes fundidas numa so'): rc!=0", rc != 0, out.strip())
        check("mutacao 2 nomeia SECCAO_EM_FALTA nas DUAS seccoes",
              out.count("SECCAO_EM_FALTA") == 2, out.strip())


def test_mutacao_3_seccao_duplicada():
    with tempfile.TemporaryDirectory() as tmp:
        texto = muta(REGISTO_VALIDO, "## Nota final",
                     "## O que este portao prova\n\n- Outra coisa [evidencia: in-boot]\n\n## Nota final")
        rc, out = correr(cenario(tmp, texto))
        check("mutacao 3 (seccao 'prova' duplicada): rc!=0", rc != 0, out.strip())
        check("mutacao 3 nomeia SECCAO_DUPLICADA", "SECCAO_DUPLICADA" in out, out.strip())


def test_mutacao_4_afirmacao_sem_marca():
    with tempfile.TemporaryDirectory() as tmp:
        texto = muta(REGISTO_VALIDO,
                     "- A intro nao regrediu entre as duas pontas do bloco [evidencia: in-boot]",
                     "- A intro nao regrediu entre as duas pontas do bloco")
        rc, out = correr(cenario(tmp, texto))
        check("mutacao 4 (afirmacao sem marca de evidencia): rc!=0", rc != 0, out.strip())
        check("mutacao 4 nomeia SEM_EVIDENCIA", "SEM_EVIDENCIA" in out, out.strip())
        check("mutacao 4 imprime o vocabulario admitido",
              "nao-exercitado" in out and "offline-unit" in out, out.strip())


def test_mutacao_5_vocabulario_invalido():
    with tempfile.TemporaryDirectory() as tmp:
        texto = muta(REGISTO_VALIDO, "[evidencia: offline-unit]", "[evidencia: funciona]")
        rc, out = correr(cenario(tmp, texto))
        check("mutacao 5 (marca fora do vocabulario): rc!=0", rc != 0, out.strip())
        check("mutacao 5 nomeia EVIDENCIA_INVALIDA", "EVIDENCIA_INVALIDA" in out, out.strip())


def test_mutacao_6_duas_marcas_na_mesma_linha():
    with tempfile.TemporaryDirectory() as tmp:
        texto = muta(REGISTO_VALIDO,
                     "- A intro nao regrediu entre as duas pontas do bloco [evidencia: in-boot]",
                     "- A intro nao regrediu [evidencia: in-boot] e o fix foi corrido "
                     "[evidencia: nao-exercitado]")
        rc, out = correr(cenario(tmp, texto))
        check("mutacao 6 (duas marcas na mesma afirmacao): rc!=0", rc != 0, out.strip())
        check("mutacao 6 nomeia EVIDENCIA_DUPLICADA", "EVIDENCIA_DUPLICADA" in out, out.strip())


def test_mutacao_7_in_boot_no_que_nao_se_prova():
    with tempfile.TemporaryDirectory() as tmp:
        texto = muta(REGISTO_VALIDO,
                     "- Que o caminho corrigido foi exercitado em boot [evidencia: nao-exercitado]",
                     "- Que o caminho corrigido foi exercitado em boot [evidencia: in-boot]")
        rc, out = correr(cenario(tmp, texto))
        check("mutacao 7 (in-boot dentro da seccao NAO prova): rc!=0", rc != 0, out.strip())
        check("mutacao 7 nomeia IN_BOOT_NO_QUE_NAO_SE_PROVA",
              "IN_BOOT_NO_QUE_NAO_SE_PROVA" in out, out.strip())


def test_mutacao_8_cabecalho_sem_sha_upstream():
    with tempfile.TemporaryDirectory() as tmp:
        texto = muta(REGISTO_VALIDO, "- sha_upstream: abc1234,def5678\n", "")
        rc, out = correr(cenario(tmp, texto))
        check("mutacao 8 (cabecalho sem sha_upstream): rc!=0", rc != 0, out.strip())
        check("mutacao 8 nomeia CABECALHO_INCOMPLETO", "CABECALHO_INCOMPLETO" in out, out.strip())
        check("mutacao 8 diz QUAL a chave em falta", "sha_upstream" in out, out.strip())


def test_mutacao_9_seccao_vazia():
    with tempfile.TemporaryDirectory() as tmp:
        texto = muta(REGISTO_VALIDO,
                     "- Que o caminho corrigido foi exercitado em boot [evidencia: nao-exercitado]\n"
                     "- Que nao ha' perdas a jusante da metrica medida [evidencia: nao-exercitado]\n",
                     "")
        rc, out = correr(cenario(tmp, texto))
        check("mutacao 9 (seccao NAO prova vazia): rc!=0", rc != 0, out.strip())
        check("mutacao 9 nomeia SECCAO_VAZIA", "SECCAO_VAZIA" in out, out.strip())


def test_cabecalho_sem_traco_inicial():
    """O README documenta o cabecalho em lista (`- chave: valor`); o formato do
    plano usa `chave: valor`. Os dois sao aceites -- e provado nos dois sentidos."""
    with tempfile.TemporaryDirectory() as tmp:
        texto = REGISTO_VALIDO.replace("\n- bloco:", "\nbloco:") \
                              .replace("\n- sha_upstream:", "\nsha_upstream:") \
                              .replace("\n- ancora_nossa:", "\nancora_nossa:") \
                              .replace("\n- fase:", "\nfase:") \
                              .replace("\n- data:", "\ndata:")
        rc, out = correr(cenario(tmp, texto))
        check("cabecalho sem '- ' inicial tambem passa", rc == 0, out.strip())


def test_seleccao_de_registos_num_directorio():
    """O README.md e o GATE-MUTATION-PROOF.md NAO sao registos de bloco: se o
    verificador os tratasse como tal, reprovaria documentacao valida e o gate
    nunca poderia ficar verde."""
    with tempfile.TemporaryDirectory() as tmp:
        cenario(tmp)
        with open(os.path.join(tmp, "README.md"), "w", encoding="utf-8") as fh:
            fh.write("# Portao\n\n## O que este portao prova\n\nprosa sem marca\n")
        with open(os.path.join(tmp, "GATE-MUTATION-PROOF.md"), "w", encoding="utf-8") as fh:
            fh.write("# Prova\n\n## O que este portao NAO prova\n\nprosa sem marca\n")
        rc, out = correr(tmp)
        check("directorio: README.md e GATE-MUTATION-PROOF.md ficam FORA da seleccao",
              rc == 0, out.strip())
        check("directorio: so' o registo XPTO-01.md foi verificado",
              "registos=1" in out and "XPTO-01.md" in out, out.strip())


def test_nome_disfarcado_nao_escapa():
    """Mudar o nome do ficheiro nao pode ser forma de fugir ao verificador."""
    with tempfile.TemporaryDirectory() as tmp:
        texto = muta(REGISTO_VALIDO, "## O que este portao NAO prova", "## Outra coisa")
        cenario(tmp, texto, nome="notas-do-bloco.md")
        rc, out = correr(tmp)
        check("directorio: ficheiro com `bloco:` no topo e' verificado mesmo com nome livre",
              rc != 0, out.strip())
        check("nome disfarcado nomeia SECCAO_EM_FALTA", "SECCAO_EM_FALTA" in out, out.strip())


def test_directorio_sem_registos():
    with tempfile.TemporaryDirectory() as tmp:
        with open(os.path.join(tmp, "README.md"), "w", encoding="utf-8") as fh:
            fh.write("# so' documentacao\n")
        rc, out = correr(tmp)
        check("directorio sem nenhum registo: rc!=0 (gate que nao ve nada nao e' gate)",
              rc != 0, out.strip())
        check("directorio sem registos nomeia SEM_REGISTOS", "SEM_REGISTOS" in out, out.strip())


def test_registos_reais_do_repo():
    """O caso que fica vermelho quando um registo de bloco real nasce mal
    formado -- a mesma politica de test_upstream_delta.py::test_tsv_real_do_repo."""
    if not os.path.isdir(DIR_REAL):
        check("directorio real de registos existe", False, DIR_REAL)
        return
    rc, out = correr(DIR_REAL)
    check("docs/upstream-blocks/ real valida (rc=0)", rc == 0, out.strip())
    check("docs/upstream-blocks/ real imprime resumo", "[bloco] resumo " in out, out.strip())


def main() -> int:
    reais = []
    if os.path.isdir(DIR_REAL):
        reais = [os.path.join(DIR_REAL, n) for n in sorted(os.listdir(DIR_REAL))
                 if n.endswith(".md")]
    antes = {c: sha256(c) for c in reais}

    test_controlo_positivo()
    test_mutacao_1_sem_seccao_nao_prova()
    test_mutacao_2_seccoes_fundidas()
    test_mutacao_3_seccao_duplicada()
    test_mutacao_4_afirmacao_sem_marca()
    test_mutacao_5_vocabulario_invalido()
    test_mutacao_6_duas_marcas_na_mesma_linha()
    test_mutacao_7_in_boot_no_que_nao_se_prova()
    test_mutacao_8_cabecalho_sem_sha_upstream()
    test_mutacao_9_seccao_vazia()
    test_cabecalho_sem_traco_inicial()
    test_seleccao_de_registos_num_directorio()
    test_nome_disfarcado_nao_escapa()
    test_directorio_sem_registos()
    test_registos_reais_do_repo()

    for caminho, h in antes.items():
        check(f"a suite NAO escreveu em {os.path.basename(caminho)} (sha256 igual)",
              sha256(caminho) == h, "o artefacto validado foi modificado pela suite")

    print(f"\n[bloco-test] {'FAILED: ' + ', '.join(FAILS) if FAILS else 'todos os casos passaram'}")
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
