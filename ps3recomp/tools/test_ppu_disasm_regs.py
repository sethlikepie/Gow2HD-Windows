#!/usr/bin/env python3
"""O disassembler tem de distinguir GPR de FPR nos load/store.

O PPC tem ficheiros de registos separados. Imprimir `lfs r31, 0(r9)` onde e'
`lfs f31, 0(r9)` faz uma listagem parecer que um GPR vivo foi destruido por um
load de virgula flutuante. Custou uma conclusao errada na analise do CC9D0
(2026-07-25): o objecto vivia em r31 e o `lfs f31` parecia sobrescreve-lo.

O registo BASE (rA) e' sempre GPR, nas duas familias -- so' o registo de
dados muda.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import ppu_disasm

FAILS = []


def d(op, rt, ra, imm=0):
    return (op << 26) | (rt << 21) | (ra << 16) | (imm & 0xFFFF)


def x(rt, ra, rb, xo):
    return (31 << 26) | (rt << 21) | (ra << 16) | (rb << 11) | (xo << 1)


CASES = [
    # D-form FP -> fN
    ("lfs f31, 0x0(r9)",     d(48, 31, 9)),
    ("lfsu f5, 0x10(r6)",    d(49, 5, 6, 0x10)),
    ("lfd f1, 0x8(r3)",      d(50, 1, 3, 8)),
    ("lfdu f2, 0x8(r3)",     d(51, 2, 3, 8)),
    ("stfs f2, 0x4(r4)",     d(52, 2, 4, 4)),
    ("stfsu f2, 0x4(r4)",    d(53, 2, 4, 4)),
    ("stfd f31, 0xD8(r1)",   d(54, 31, 1, 0xD8)),
    ("stfdu f3, 0x8(r4)",    d(55, 3, 4, 8)),
    # D-form inteiro -> rN (nao pode ter regredido)
    ("lwz r30, 0x4(r31)",    d(32, 30, 31, 4)),
    ("lbz r3, 0x1(r4)",      d(34, 3, 4, 1)),
    ("stw r3, 0x8(r4)",      d(36, 3, 4, 8)),
    ("lmw r28, 0x0(r1)",     d(46, 28, 1)),
    ("stmw r28, 0x0(r1)",    d(47, 28, 1)),
    # X-form FP -> fN
    ("lfsx f7, r8, r9",      x(7, 8, 9, 535)),
    ("lfdx f7, r8, r9",      x(7, 8, 9, 599)),
    ("stfsx f7, r8, r9",     x(7, 8, 9, 663)),
    ("stfdx f7, r8, r9",     x(7, 8, 9, 727)),
    ("stfiwx f7, r8, r9",    x(7, 8, 9, 983)),
    # X-form inteiro -> rN
    ("lwzx r7, r8, r9",      x(7, 8, 9, 23)),
    ("stwx r7, r8, r9",      x(7, 8, 9, 151)),
    ("ldx r7, r8, r9",       x(7, 8, 9, 21)),
]


def main():
    for want, word in CASES:
        i = ppu_disasm.decode(word, 0)
        got = f"{i.mnemonic} {i.operands}"
        if got == want:
            print(f"[disasm-regs] ok   {got}")
        else:
            print(f"[disasm-regs] FAIL {got}   (esperado: {want})")
            FAILS.append(want)

    # o lifter tem de continuar a resolver o indice a partir do prefixo novo
    from ppu_lifter import PPULifter, LiftedFunction
    L = PPULifter()
    f = LiftedFunction(name="f", start_addr=0, end_addr=0x100)
    for want, word in CASES:
        if not want.startswith(("lf", "stf")):
            continue
        c = L._translate(ppu_disasm.decode(word, 0), f)
        if c.strip().startswith("/* TODO"):
            print(f"[disasm-regs] FAIL o lifter deixou de traduzir {want}")
            FAILS.append(f"lift:{want}")
    if not any(x.startswith("lift:") for x in FAILS):
        print("[disasm-regs] ok   o lifter continua a traduzir todos os FP load/store")

    print(f"\n[disasm-regs] {'FAILED: ' + ', '.join(FAILS) if FAILS else 'todos os casos passaram'}")
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
