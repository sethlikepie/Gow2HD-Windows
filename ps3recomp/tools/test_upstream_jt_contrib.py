#!/usr/bin/env python3
"""Diferencial jump tables: a NOSSA `discover_jump_tables` vs a do UPSTREAM.

Porque existe (UPST-03): e' o unico item do marco em que o fluxo e' nos ->
upstream. Para poder afirmar "o upstream descarta este dispatcher" sem forjar
nada, o lado deles nao e' uma reconstrucao: e' o recorte VERBATIM do ramo vivo,
com sha e sha256 gravados em `tools/upstream_jt_snapshot.py`, carregado por
`exec` num namespace proprio.

O defeito, RE-MEDIDO contra o ramo vivo (nao a frase historica)
----------------------------------------------------------------
O PR #82 do upstream descreve o defeito das duas linhagens como "first
TOC-loaded operand wins". Contra `ee97d01` isso e' **largo demais**: eles ja'
ganharam uma reordenacao sintactica dos candidatos -- procuram no janela um
`add` que combine o valor carregado com um dos dois operandos do `lwzx` e poem
esse operando a frente. Por isso a forma com `add` (tabela de OFFSETS) ja'
resolve bem do lado deles, e o vector antigo de
`test_ppu_jumptables.py::test_primeiro_operando_nao_ganha_sozinho`
**nao discrimina** contra o ramo vivo -- e' por isso que entra aqui como
CONTROLO, nao como acusacao.

O que resta e' mais estreito e e' esse que o VECTOR A ataca: quando NAO ha `add`
(tabela ABSOLUTA), a reordenacao nao tem por onde pegar, a ordem dos candidatos
fica a ordem sintactica crua dos operandos do `lwzx`, e eles fixam o PRIMEIRO
candidato cuja definicao mais proxima e' uma carga do TOC -- com `break`, sem
nunca contar alvos validados. Se o operando errado (o indice) tambem tiver sido
carregado do TOC, ganha ele, a base lida e' a errada, zero alvos validam e o
dispatcher inteiro e' descartado em silencio.

Nao e' um cenario de palha: e' o proprio comentario deles que o descreve ("Both
operands are often TOC-loaded, so 'first candidate with a TOC-load definition'
can pick an unrelated index reg whose own stale `lwz idx,d(r2)` reads back a
valid-but-wrong base"). A reordenacao por `add` que eles acrescentaram cobre
esse cenario apenas quando existe um `add` -- ou seja, nao o cobre nas tabelas
absolutas.

A raiz e' a mesma nas duas leituras: o registo-base e' **escolhido** em vez de
ser **arbitrado por numero de alvos validados**. Eles ja' usam arbitracao por
contagem -- mas so' entre candidatos a TOC, nunca entre candidatos a registo-base.

Nivel de evidencia: offline-unit. Sem ELF, sem rede, sem boot.
"""
import argparse
import hashlib
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import upstream_jt_snapshot as snap
from ppu_lifter import discover_jump_tables as nossa
# Helpers de encoding: importadas, nao copiadas, para haver UMA fonte de verdade
# sobre como se monta uma palavra PPC nos testes. Se as encodings mudarem, os
# dois harnesses mudam juntos em vez de divergirem em silencio.
from test_ppu_jumptables import (TEXT_LO, TEXT_HI, TOC, decode_all, mem_reader,
                                 w_add, w_bctr, w_cmplwi, w_lwz, w_lwzx, w_mtctr)

LIFTER = os.path.join(os.path.dirname(os.path.abspath(__file__)), "ppu_lifter.py")

FAILS = []


def check(name, cond, detail=""):
    if cond:
        print(f"[jtcontrib] ok   {name}")
    else:
        print(f"[jtcontrib] FAIL {name} {detail}")
        FAILS.append(name)


def sha256_file(path):
    with open(path, "rb") as fh:
        return hashlib.sha256(fh.read()).hexdigest()


# ---------------------------------------------------------------------------
# Os dois vectores
# ---------------------------------------------------------------------------

def vector_a():
    """DISCRIMINADOR -- tabela ABSOLUTA, sem `add`, ambos operandos do TOC.

        cmplwi cr0, r9, 2
        lwz    r9,  8(r2)     <- carregado do TOC, mas NAO e' a tabela
        lwz    r11, 16(r2)    <- a base a serio
        lwzx   r0,  r9, r11   <- indice primeiro, base depois
        mtctr  r0 ; bctr      <- sem `add`: a tabela e' ABSOLUTA

    Sem `add`, a reordenacao sintactica deles nao tem por onde pegar: r9 fica em
    primeiro, a definicao mais proxima de r9 e' `lwz r9,8(r2)`, e eles fixam-no
    como base -- lendo a tabela de lixo. Aqui os dois `lwz` estao lado a lado por
    minimalidade; em codigo real o errado e' tipicamente uma carga obsoleta da
    funcao ANTERIOR que sangra pela janela de 30 instrucoes, que e' exactamente
    o cenario que o comentario deles descreve.
    """
    insns = decode_all([w_cmplwi(0, 9, 2), w_lwz(9, 2, 8), w_lwz(11, 2, 16),
                        w_lwzx(0, 9, 11), w_mtctr(0), w_bctr()])
    lixo, tabela = 0x18000, 0x200400
    mem = {
        TOC + 8: lixo,                                  # candidato ERRADO
        lixo + 0: 0xDEADBEEF, lixo + 4: 0xDEADBEEF,     # fora do .text -> 0 alvos
        TOC + 16: tabela,                               # candidato CERTO
        tabela + 0: 0x13000, tabela + 4: 0x13004, tabela + 8: 0x13008,
        tabela + 12: 0x0,                               # terminador
    }
    return dict(nome="A (discriminador, tabela absoluta sem add)", insns=insns,
                mem=mem, toc=[TOC], esperado=[0x13000, 0x13004, 0x13008])


def vector_b():
    """CONTROLO -- tabela de OFFSETS com `add`; os dois lados concordam.

        cmplwi cr0, r9, 2
        lwz    r9,  8(r2)
        lwz    r11, 16(r2)
        lwzx   r0,  r9, r11
        add    r0,  r0, r11   <- o `add` nomeia r11: e' o que os desempata
        mtctr  r0 ; bctr

    E' o vector que ja' existe em `test_ppu_jumptables.py`. Fica aqui DE
    PROPOSITO: sem um vector onde os dois lados acertam, um diferencial verde nao
    distingue "defeito deles" de "harness enviesado a nosso favor". E deixa
    escrito que o teste antigo deixou de discriminar contra o ramo vivo.
    """
    insns = decode_all([w_cmplwi(0, 9, 2), w_lwz(9, 2, 8), w_lwz(11, 2, 16),
                        w_lwzx(0, 9, 11), w_add(0, 0, 11), w_mtctr(0), w_bctr()])
    lixo, tabela = 0x18000, 0x11000
    mem = {
        TOC + 8: lixo,
        lixo + 0: 0xDEADBEEF, lixo + 4: 0xDEADBEEF,
        TOC + 16: tabela,
        tabela + 0: 0x100, tabela + 4: 0x200, tabela + 8: 0xFFFFFFFF,
    }
    return dict(nome="B (controlo, tabela de offsets com add)", insns=insns,
                mem=mem, toc=[TOC], esperado=[0x11100, 0x11200])


def corre(discover, vec):
    """Alvos que `discover` devolve para o `bctr` do vector (None se descartado)."""
    tabelas = discover(vec["insns"], mem_reader(vec["mem"]), vec["toc"],
                       TEXT_LO, TEXT_HI)
    return tabelas.get(vec["insns"][-1].addr)


# ---------------------------------------------------------------------------
# Modos
# ---------------------------------------------------------------------------

def teste_snapshot_intacto():
    """Sem isto o diferencial nao vale nada: podia estar a correr codigo editado."""
    got = hashlib.sha256(snap.verbatim_source().encode()).hexdigest()
    check("snapshot verbatim intacto (sha256 == gravado)",
          got == snap.UPSTREAM_FUNC_SHA256,
          f"got={got} esperado={snap.UPSTREAM_FUNC_SHA256}")
    print(f"[jtcontrib] lado deles: {snap.UPSTREAM_REF} @ {snap.UPSTREAM_SHA[:12]} "
          f"{snap.UPSTREAM_PATH}:{snap.UPSTREAM_LINES[0]}-{snap.UPSTREAM_LINES[1]}")


def teste_diferencial():
    deles = snap.load_upstream_func()

    a = vector_a()
    nosso_a, deles_a = corre(nossa, a), corre(deles, a)
    check(f"VECTOR {a['nome']}: nos resolvemos os alvos certos",
          nosso_a == a["esperado"], f"got={nosso_a}")
    check(f"VECTOR {a['nome']}: eles descartam o dispatcher",
          not deles_a, f"got={deles_a}")
    if nosso_a == a["esperado"] and not deles_a:
        print("[jtcontrib] DISCRIMINADOR_OK "
              f"nosso={[hex(t) for t in nosso_a]} deles={deles_a}")
    elif deles_a == a["esperado"]:
        # Medicao contraria a hipotese. Nao se procura um vector mais rebuscado
        # ate algo ficar vermelho -- um vermelho fabricado nao e' evidencia.
        print("[jtcontrib] MEDICAO: o ramo vivo do upstream RESOLVE o VECTOR A. "
              "O defeito pode ter sido corrigido; registar no SUMMARY e rever a "
              "linha `contribuir-de-volta` do inventario.")

    b = vector_b()
    nosso_b, deles_b = corre(nossa, b), corre(deles, b)
    check(f"VECTOR {b['nome']}: nos acertamos", nosso_b == b["esperado"],
          f"got={nosso_b}")
    check(f"VECTOR {b['nome']}: eles acertam TAMBEM (controlo negativo)",
          deles_b == b["esperado"], f"got={deles_b}")
    if nosso_b == deles_b == b["esperado"]:
        print(f"[jtcontrib] CONTROLO_OK ambos={[hex(t) for t in nosso_b]}")


# A mutacao reverte a NOSSA arbitracao: trunca os candidatos ao primeiro, que e'
# exactamente o comportamento "o primeiro que decodifica ganha" de antes do fix
# de 2026-07-30. E' aplicada ao TEXTO recortado e corrida por `exec`; nenhum
# caminho de escrita e' aberto sobre `tools/ppu_lifter.py`.
MUTACAO_ANCORA = "        if not base_candidates:\n"
MUTACAO_ENXERTO = (
    "        r_base_cands = r_base_cands[:1]      # MUTACAO\n"
    "        base_candidates = base_candidates[:1]  # MUTACAO\n"
)


def teste_mutacao():
    """Revertida a arbitracao por contagem de alvos, o VECTOR A tem de ficar VERMELHO.

    Sem isto o diferencial provaria apenas que os dois lados divergem, nao que e'
    a NOSSA arbitracao que faz a diferenca. Um teste que continuasse verde com a
    heuristica revertida nao estaria a medir a heuristica.
    """
    corte, span = snap.cut_function(open(LIFTER, encoding="utf-8").read())
    mutado, n = re.subn(re.escape(MUTACAO_ANCORA),
                        MUTACAO_ENXERTO + MUTACAO_ANCORA, corte)
    # 0 casamentos = a mutacao nao se aplicou; 2+ = aplicou-se onde nao devia.
    # Nos dois casos um "vermelho" a seguir seria um erro de carregamento
    # disfarcado de prova. Por isso a contagem e' assercao, nao aviso.
    check("mutacao casa exactamente uma vez", n == 1, f"casou {n}x")
    if n != 1:
        return
    func = snap.load_upstream_func(mutado)
    a = vector_a()
    got = corre(func, a)
    check("mutada, a NOSSA funcao deixa de resolver o VECTOR A",
          not got, f"got={got}")
    # O VECTOR B pode continuar verde sob mutacao -- e' esperado e nao invalida
    # a prova; o que a prova exige e' que o A fique vermelho.
    got_b = corre(func, vector_b())
    if not got:
        print(f"[jtcontrib] MUTACAO_OK arbitracao revertida em "
              f"{LIFTER.rsplit('/', 1)[-1]}:{span[0]}-{span[1]} (copia em memoria) "
              f"-> VECTOR A={got}, VECTOR B={got_b}")


def modo_alvo(caminho):
    """Corre os dois vectores contra a `discover_jump_tables` de um ficheiro.

    Usado para provar que o patch de contribuicao nao so' APLICA no codigo deles
    como o CORRIGE: o alvo e' o `tools/ppu_lifter.py` deles ja' com o patch.
    """
    texto = open(caminho, encoding="utf-8").read()
    corte, span = snap.cut_function(texto)
    func = snap.load_upstream_func(corte)
    print(f"[jtcontrib] alvo: {caminho} linhas {span[0]}-{span[1]}")
    ok = True
    for vec in (vector_a(), vector_b()):
        got = corre(func, vec)
        check(f"alvo VECTOR {vec['nome']}", got == vec["esperado"], f"got={got}")
        ok = ok and got == vec["esperado"]
    if ok:
        print("[jtcontrib] PATCHED_OK os dois vectores resolvem no ficheiro alvo")
    print("[jtcontrib] nivel de evidencia: offline-unit")
    return 0 if ok and not FAILS else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--alvo", metavar="FICHEIRO",
                    help="corre os vectores contra a discover_jump_tables deste "
                         "ficheiro (usado para validar o patch de contribuicao)")
    a = ap.parse_args()
    if a.alvo:
        return modo_alvo(a.alvo)

    # O lifter de producao e' medido no inicio e reconfirmado no fim: se a suite
    # alguma vez escrever nele, e' aqui que se ve.
    h0 = sha256_file(LIFTER)
    teste_snapshot_intacto()
    teste_diferencial()
    teste_mutacao()
    h1 = sha256_file(LIFTER)
    check("tools/ppu_lifter.py intacto (sha256 igual antes e depois)", h0 == h1,
          f"antes={h0} depois={h1}")
    print(f"\n[jtcontrib] {'FALHOU: ' + ', '.join(FAILS) if FAILS else 'todos os casos passaram'}")
    print("[jtcontrib] nivel de evidencia: offline-unit")
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
