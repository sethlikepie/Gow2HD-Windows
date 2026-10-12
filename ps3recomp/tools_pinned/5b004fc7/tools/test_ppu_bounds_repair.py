#!/usr/bin/env python3
"""O lifter nao pode DESCARTAR codigo quando o mapa de funcoes trunca um `end`.

O problema (medido no GoW2, 2026-07-26)
---------------------------------------
Quando a ultima instrucao dentro de [start, end) nao e' um terminador, o lifter
poe `func.fallthrough_to = end` (ppu_lifter.py ~697). Na emissao (~3222), se
`end` nao for o inicio de uma funcao emitida, cai no fallback "proxima funcao
POR ORDEM DE ENDERECO" -- e salta por cima de tudo o que estiver no meio.

O comentario que justifica o fallback assume que a proxima funcao "starts
exactly at `end` for contiguous functions". O functions.json do GoW2 tem 3282
lacunas somando 1.134.364 bytes, por isso o pressuposto e' falso e o codigo da
lacuna desaparece SEM AVISO.

Caso real que custou o boot inteiro: func_0037C384 esta' declarada com
end=0x37C38C (2 instrucoes) mas o codigo corre ate' ao `blr` em 0x37C3B0. As 10
instrucoes descartadas incluem `bl 0x00379C70` -- a chamada que aloca o heap de
125 MB do jogo. Sem ela o jogo nunca aloca o heap, nenhuma thread PPU e' criada
e o boot fica num busy-poll. Sao 180 funcoes nesta situacao (invariante I3 do
tools/audit_boundaries.py), a mesma classe do bug func_002550C8.

Uso: python3 test_ppu_bounds_repair.py   (rc=0 verde, rc=1 falha)
"""
from __future__ import annotations

import json
import re
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
LIFTER = HERE / "ppu_lifter.py"

FAILS: list[str] = []


def check(name: str, got, want) -> None:
    if got != want:
        FAILS.append(f"{name}\n    got : {got!r}\n    want: {want!r}")


# Bytes REAIS de func_0037C384 do GoW2 (ppu_disasm sobre o EBOOT), rebaseados
# para 0x1000. Nao sao inventados: e' o caso que partiu o boot.
#
#   +0x00  7C641B78  or     r4, r3, r3
#   +0x04  80621964  lwz    r3, 0x1964(r2)
#   +0x08  F821FF91  stdu   r1, -0x70(r1)   <-- o functions.json corta AQUI
#   +0x0C  7C0802A6  mflr   r0
#   +0x10  F8010080  std    r0, 0x80(r1)
#   +0x14  4800005D  bl     0x1070          <-- a chamada que se perde
#   +0x18  60000000  nop
#   +0x1C  E8010080  ld     r0, 0x80(r1)
#   +0x20  78630020  rldicl r3, r3, 0, 32
#   +0x24  7C0803A6  mtlr   r0
#   +0x28  38210070  addi   r1, r1, 112
#   +0x2C  4E800020  blr                    <-- fim REAL da funcao
#   +0x30  F821FF91  stdu   r1, -0x70(r1)   <-- proxima funcao (0x1030)
#   +0x34  4E800020  blr
#   ...
#   +0x70  4E800020  blr                    <-- alvo do bl (0x1070)
BASE = 0x1000
WORDS = {
    0x00: 0x7C641B78, 0x04: 0x80621964, 0x08: 0xF821FF91, 0x0C: 0x7C0802A6,
    0x10: 0xF8010080, 0x14: 0x4800005D, 0x18: 0x60000000, 0x1C: 0xE8010080,
    0x20: 0x78630020, 0x24: 0x7C0803A6, 0x28: 0x38210070, 0x2C: 0x4E800020,
    0x30: 0xF821FF91, 0x34: 0x4E800020,
    0x70: 0x4E800020,
}
SIZE = 0x74

FUNCS = [
    {"start": "0x00001000", "end": "0x00001008"},   # TRUNCADA de proposito
    {"start": "0x00001030", "end": "0x00001038"},
    {"start": "0x00001070", "end": "0x00001074"},
]


def lift() -> str:
    """Corre o lifter sobre o blob e devolve o C emitido (todos os chunks)."""
    with tempfile.TemporaryDirectory() as td:
        tmp = Path(td)
        blob = bytearray(b"\x00" * SIZE)
        for off, w in WORDS.items():
            struct.pack_into(">I", blob, off, w)
        (tmp / "in.bin").write_bytes(bytes(blob))
        (tmp / "funcs.json").write_text(json.dumps(FUNCS), encoding="utf-8")
        r = subprocess.run(
            [sys.executable, str(LIFTER), str(tmp / "in.bin"), "--raw",
             "--base", hex(BASE), "--functions", str(tmp / "funcs.json"),
             "-o", str(tmp / "out"), "-j", "1"],
            capture_output=True, text=True)
        if r.returncode != 0:
            FAILS.append(f"lifter falhou rc={r.returncode}\n{r.stdout}\n{r.stderr}")
            return ""
        return "\n".join(p.read_text(encoding="utf-8", errors="replace")
                         for p in sorted((tmp / "out").glob("*.cpp")))


def body_of(src: str, name: str) -> str:
    m = re.search(rf"^void {name}\(ppu_context\* ctx\) \{{(.*?)^\}}",
                  src, re.M | re.S)
    return m.group(1) if m else ""


def main() -> int:
    src = lift()
    if not src:
        print("FAIL: o lifter nao produziu output", file=sys.stderr)
        return 1

    body = body_of(src, "func_00001000")
    check("func_00001000 foi emitida", bool(body.strip()), True)

    # O NUCLEO: a chamada que vive na lacuna tem de sobreviver ao lift.
    check("a chamada da lacuna (bl 0x1070) e' emitida",
          "func_00001070(ctx)" in body, True)

    # E o corpo tem de acabar no `blr` real, nao num trampolim para o vizinho.
    check("nao trampolina por cima da lacuna",
          "func_00001030" in body, False)

    # Prova de que nao se perdeu mais nada da lacuna: o epilogo tem de la' estar.
    check("o epilogo da lacuna e' emitido (mtlr/addi/return)",
          "ctx->lr = ctx->gpr[0]" in body and "return;" in body, True)

    if FAILS:
        print(f"FAIL ({len(FAILS)}):", file=sys.stderr)
        for f in FAILS:
            print("  " + f, file=sys.stderr)
        print("\n--- corpo emitido de func_00001000 ---", file=sys.stderr)
        print(body or "(vazio)", file=sys.stderr)
        return 1
    print("[test_ppu_bounds_repair] OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
