#!/usr/bin/env python3
"""Testa as invariantes de tools/audit_boundaries.py.

Sem ELF: o Code aceita segmentos crus, portanto os casos sao palavras PPC
escritas a mao. Cada invariante tem um caso que dispara e um que NAO dispara --
um detector que nunca fica calado e' tao inutil como um que nunca dispara.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from audit_boundaries import Code, audit

BLR = 0x4E800020                                   # blr        (terminador)
B_FWD = 0x48000004                                 # b +4       (terminador)
STW = 0x90640000                                   # stw r3,0(r4)  (nao termina)
LI = 0x38600001                                    # li r3,1       (nao termina)

FAILS = []


def check(name, cond, detail=""):
    if cond:
        print(f"[bounds-test] ok   {name}")
    else:
        print(f"[bounds-test] FAIL {name} {detail}")
        FAILS.append(name)


def seg(base, words):
    return (base, base + 4 * len(words),
            b"".join(w.to_bytes(4, "big") for w in words))


def run(ranges, code=None, oracles=(), lo=0x1000, hi=0x2000):
    return audit(sorted(ranges), code, list(oracles), lo, hi, 10)


def test_i1_overlap():
    f = run([(0x1000, 0x1100), (0x1080, 0x1200)])
    check("I1 apanha sobreposicao", len(f["I1"]) == 1, f["I1"])
    check("I1 mede os bytes sobrepostos",
          f["I1"] and f["I1"][0]["overlap_bytes"] == 0x80, f["I1"])
    f = run([(0x1000, 0x1100), (0x1100, 0x1200)])
    check("I1 nao dispara em ranges adjacentes", not f["I1"], f["I1"])


def test_i3_truncated():
    # 0x1000: li, stw  (acaba SEM terminador)   0x1008..0x1010: codigo no buraco
    # 0x1010: li, blr  (acaba COM terminador)
    code = Code([seg(0x1000, [LI, STW, LI, LI, LI, BLR])])
    f = run([(0x1000, 0x1008), (0x1014, 0x1018)], code)
    got = [x["func"] for x in f["I3"]]
    check("I3 apanha fim sem terminador", got == ["0x00001000"], got)
    check("I3 diz quantos bytes o trampolim salta",
          f["I3"] and f["I3"][0]["salta_bytes"] == 0x1014 - 0x1008, f["I3"])
    check("I3 identifica a ultima instrucao",
          f["I3"] and f["I3"][0]["ultima"] == "stw", f["I3"])

    # mesma funcao mas a acabar em blr -> nao dispara
    code2 = Code([seg(0x1000, [LI, BLR])])
    f2 = run([(0x1000, 0x1008), (0x1014, 0x1018)], code2)
    check("I3 nao dispara quando acaba em blr", not f2["I3"], f2["I3"])

    # acabar em `b` (salto incondicional) tambem termina
    code3 = Code([seg(0x1000, [LI, B_FWD])])
    f3 = run([(0x1000, 0x1008), (0x1014, 0x1018)], code3)
    check("I3 aceita `b` como terminador", not f3["I3"], f3["I3"])

    # se o `end` E' inicio registado, a continuacao e' explicita -> nao dispara
    code4 = Code([seg(0x1000, [LI, STW, LI, BLR])])
    f4 = run([(0x1000, 0x1008), (0x1008, 0x1010)], code4)
    check("I3 nao dispara quando o end e' inicio registado", not f4["I3"], f4["I3"])


def test_i2_gap():
    # buraco 0x1008..0x1018 com 4 instrucoes validas
    code = Code([seg(0x1000, [LI, BLR, LI, LI, LI, BLR, LI, BLR])])
    f = run([(0x1000, 0x1008), (0x1018, 0x1020)], code)
    check("I2 apanha lacuna com codigo valido", len(f["I2"]) == 1, f["I2"])
    check("I2 conta as instrucoes validas",
          f["I2"] and f["I2"][0]["insns_validas"] == 4, f["I2"])

    # buraco de zeros -> padding, nao dispara
    code0 = Code([seg(0x1000, [LI, BLR, 0, 0, 0, 0, LI, BLR])])
    f0 = run([(0x1000, 0x1008), (0x1018, 0x1020)], code0)
    check("I2 nao dispara em padding de zeros", not f0["I2"], f0["I2"])

    # buraco curto (2 instrucoes) fica abaixo do minimo
    code1 = Code([seg(0x1000, [LI, BLR, LI, LI, LI, BLR])])
    f1 = run([(0x1000, 0x1008), (0x1010, 0x1018)], code1)
    check("I2 ignora lacunas curtas", not f1["I2"], f1["I2"])


def test_i4_i5_oracle():
    ranges = [(0x1000, 0x1100), (0x1200, 0x1300)]
    orc = {
        0x1000: (0x1100, "igual", False),          # concorda
        0x1100: (0x1200, "so_no_oraculo", False),  # I4
        0x1200: (0x1280, "fim_diferente", False),  # I5
        0x1400: (0x1410, "thunk", True),           # thunk -> ignorado
    }
    f = run(ranges, None, [("ghidra", orc)])
    i4 = [x["addr"] for x in f["I4"]]
    check("I4 apanha funcao so' vista pelo oraculo", i4 == ["0x00001100"], i4)
    check("I4 ignora thunks", "0x00001400" not in i4, i4)
    i5 = [(x["addr"], x["nosso_end"], x["oraculo_end"]) for x in f["I5"]]
    check("I5 apanha desacordo de fim",
          i5 == [("0x00001200", "0x00001300", "0x00001280")], i5)
    check("I5 nao dispara quando concordam",
          all(x["addr"] != "0x00001000" for x in f["I5"]), f["I5"])


def test_sem_code_nao_inventa():
    """Sem --elf, I2/I3 nao podem produzir achados (nem falsos negativos calados)."""
    f = run([(0x1000, 0x1008), (0x1014, 0x1018)], None)
    check("sem ELF, I2/I3 ficam vazios", not f["I2"] and not f["I3"], f)


def main():
    test_i1_overlap()
    test_i3_truncated()
    test_i2_gap()
    test_i4_i5_oracle()
    test_sem_code_nao_inventa()
    print(f"\n[bounds-test] {'FAILED: ' + ', '.join(FAILS) if FAILS else 'todos os casos passaram'}")
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
