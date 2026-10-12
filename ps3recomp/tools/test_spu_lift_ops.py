#!/usr/bin/env python3
"""Regressoes do decode/lift SPU que ja' custaram silencio ao projecto.

Todas as instrucoes aqui apareciam no C gerado como comentario ou eram
traduzidas com o registo errado -- nunca como erro. Sem SDK, sem ELF: constroi
as palavras a mao e verifica decode + traducao.

1. heqi/hgti/hlgti (RI10 0x7F/0x4F/0x5F): o decode existiu (7554685) e foi
   apagado pelo reset das tools ao upstream (11a1c3c). 136 das 138 ocorrencias
   de "TODO spu: .word" nas imagens do GoW2 sao heqi.
2. heq/hgt/hlgt e os imediatos passam a chamar spu_halt_if(), que estava em
   runtime/spu/spu_channels.c com ZERO chamadores desde a mesma regressao.
3. rotmah: a chave da tabela do lifter estava escrita "rothma", mnemonico que o
   disassembler nunca produz -> catch-all silencioso.
4. --trace: o registo destino das formas RRR esta nos bits 21-27, nao nos 7 bits
   baixos (que sao rC). Reportar o errado tornava inutil o diff contra o RPCS3.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from spu_disasm import spu_decode
from spu_lifter import SPULifter, LiftedFunction, _dest_reg

FAILS = []


def check(name, cond, detail=""):
    if cond:
        print(f"[spu-ops] ok   {name}")
    else:
        print(f"[spu-ops] FAIL {name} {detail}")
        FAILS.append(name)


def ri10(op8, ra, i10, rt=0):
    return ((op8 & 0xFF) << 24) | ((i10 & 0x3FF) << 14) | ((ra & 0x7F) << 7) | (rt & 0x7F)


def rr(op11, ra, rb, rt):
    return ((op11 & 0x7FF) << 21) | ((rb & 0x7F) << 14) | ((ra & 0x7F) << 7) | (rt & 0x7F)


def lifter():
    return SPULifter(), LiftedFunction(name="f", start_addr=0x3000, end_addr=0x4000)


def test_halt_immediate_decode():
    """A palavra real medida no lift do GoW2 (spu_miss_3512 @ LS 0x3280)."""
    i = spu_decode(0x7F000C00, 0x3280)
    check("heqi decodifica", i.mnemonic == "heqi", f"got={i.mnemonic}")
    check("heqi sem rT nos operandos", i.operands == "$r24, 0", f"got={i.operands!r}")
    for op8, mne in ((0x4F, "hgti"), (0x5F, "hlgti")):
        got = spu_decode(ri10(op8, 24, 0), 0x3280).mnemonic
        check(f"{mne} decodifica", got == mne, f"got={got}")
    # variante 0x7FFFCxxx = heqi rA, -1 (i10 com sinal)
    i = spu_decode(0x7FFFC000, 0)
    check("heqi imediato com sinal", i.operands == "$r0, -1", f"got={i.operands!r}")


def test_halt_immediate_lift():
    L, f = lifter()
    c = L._translate(spu_decode(0x7F000C00, 0x3280), f)
    check("heqi chama spu_halt_if", "spu_halt_if(ctx," in c, f"got={c}")
    check("heqi usa o slot preferido", "._u32[0]" in c, f"got={c}")
    check("heqi passa o endereco LS", "0x03280u" in c, f"got={c}")
    check("heqi nao e' comentario", not c.strip().startswith("/*"), f"got={c}")
    c = L._translate(spu_decode(ri10(0x5F, 24, 3), 0x100), f)
    check("hlgti compara sem sinal", "(uint32_t)" in c, f"got={c}")
    c = L._translate(spu_decode(ri10(0x4F, 24, 3), 0x100), f)
    check("hgti compara com sinal", "(int32_t)" in c and ">" in c, f"got={c}")


def test_halt_register_lift():
    L, f = lifter()
    for op11, mne, marker in ((0x258, "hgt", "(int32_t)"),
                              (0x2D8, "hlgt", "(uint32_t)"),
                              (0x3D8, "heq", "==")):
        i = spu_decode(rr(op11, 4, 5, 3), 0x3300)
        check(f"{mne} decodifica", i.mnemonic == mne, f"got={i.mnemonic}")
        c = L._translate(i, f)
        check(f"{mne} chama spu_halt_if", "spu_halt_if(ctx," in c, f"got={c}")
        check(f"{mne} compara rA com rB", "gpr[4]" in c and "gpr[5]" in c, f"got={c}")
        check(f"{mne} usa o comparador certo", marker in c, f"got={c}")


def test_rotmah():
    L, f = lifter()
    i = spu_decode(rr(0x05E, 4, 5, 3), 0)
    check("rotmah decodifica", i.mnemonic == "rotmah", f"got={i.mnemonic}")
    c = L._translate(i, f)
    check("rotmah traduz (nao e' TODO)", not c.strip().startswith("/* TODO"), f"got={c}")
    check("rotmah usa o helper do runtime", "spu_rothma(" in c, f"got={c}")


def test_rrr_dest_reg():
    """selb rt,rA,rB,rC: rt vive em 21-27; os 7 bits baixos sao rC."""
    w = (0b1000 << 28) | (6 << 21) | (5 << 14) | (4 << 7) | 3
    i = spu_decode(w, 0)
    check("selb decodifica", i.mnemonic == "selb", f"got={i.mnemonic}")
    check("_dest_reg devolve rt (21-27), nao rC",
          _dest_reg(i) == 6 and (i.raw & 0x7F) == 3,
          f"_dest_reg={_dest_reg(i)} raw&0x7F={i.raw & 0x7F}")


def test_trace_usa_dest_reg():
    """O --trace tem de reportar o destino real das formas RRR."""
    L = SPULifter()
    L.trace = True
    f = LiftedFunction(name="f", start_addr=0x0, end_addr=0x8)
    w = (0b1000 << 28) | (6 << 21) | (5 << 14) | (4 << 7) | 3
    insns = [spu_decode(w, 0x0)]
    try:
        out = L.lift_function(insns, 0x0, 0x4)
        body = "\n".join(out.body_lines) if hasattr(out, "body_lines") else ""
    except Exception:                                          # noqa: BLE001
        body = "\n".join(f.body_lines)
    ok = "spu_trace_rt(ctx, 6)" in body
    check("trace reporta rt=6 (nao rC=3)", ok,
          f"body={body!r}" if not ok else "")


def main():
    test_halt_immediate_decode()
    test_halt_immediate_lift()
    test_halt_register_lift()
    test_rotmah()
    test_rrr_dest_reg()
    test_trace_usa_dest_reg()
    print(f"\n[spu-ops] {'FAILED: ' + ', '.join(FAILS) if FAILS else 'todos os casos passaram'}")
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
