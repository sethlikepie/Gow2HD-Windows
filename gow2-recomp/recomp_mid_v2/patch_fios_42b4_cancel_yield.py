#!/usr/bin/env python3
"""Yield after FIOS cancel in func_002B42B4 (early DONE path) before clearing container+8.

WHY
---
Terceira peca do inventario FIOS-* (regressao do marco v1.1, 2026-07-31).
Irma de patch_fios_done_cancel_yield.py (func_002B4274) mas num sitio
DIFERENTE: func_002B42B4 e' um caminho de entrada mais cedo do poll DONE
que TAMBEM cancela a op assincrona (func_0030AE58) e zera container+8 logo
a seguir, sem dar tempo ao escalonador FIOS de libertar a op -> a mesma
classe de F2a (SEM OP LIVRE) que os outros cancel-yield fecham nos seus
sitios.

Bloco extraido *verbatim* de
`recomp_macos_v2.pre_v4/ppu_recomp_005.cpp:51946-51966` -- nao foi
reescrito de memoria. So' existe 1 sitio (func_002B42B4); confirmado que a
mesma sequencia de 6 linhas (sem o func_002B3CB0 que precede o sitio irmao
em func_002B4274) aparece exactamente 1x no lift actual, e so' no chunk que
contem func_002B42B4 (ppu_recomp_005.cpp) -- nunca no chunk 001 onde vive
func_002B4274 (irmao, ja' coberto por patch_fios_done_cancel_yield.py).

FIX
---
Mesmo padrao que FIOS-DONE-CANCEL-YIELD: liberta o giant lock, dorme
(PS3_FIOS_DONE_YIELD_MS, default 50 -- este sitio usava 50ms no lift antigo,
nao 20ms como o irmao 4274), reacquire, so' depois zera container+8.

Marker: FIOS-42B4-CANCEL-YIELD. Idempotente (whole-file marker check).
"""
from pathlib import Path
import re
import sys

MARKER = "FIOS-42B4-CANCEL-YIELD"
ROOT = (Path(sys.argv[1]) if len(sys.argv) > 1
        else Path(__file__).resolve().parent.parent / "recomp_macos_v2")

# Ancora: a sequencia de 6 linhas do cancel+clear em func_002B42B4 (sem o
# func_002B3CB0 que precede o sitio irmao em func_002B4274 -- por isso o
# guard "void func_002B42B4" abaixo, nao a regex sozinha, e' quem separa os
# dois sitios).
NEEDLE_RE = re.compile(
    r"( *ctx->gpr\[9\] = vm_read32\(ctx->gpr\[2\] \+ -0x1460\);\n"
    r" *ctx->gpr\[4\] = vm_read32\(ctx->gpr\[31\] \+ 0x8\);\n"
    r" *ctx->gpr\[3\] = vm_read32\(ctx->gpr\[9\] \+ 0x118\);\n"
    r" *(?:ctx->lr = 0x[0-9A-Fa-f]+; )?func_0030AE58\(ctx\); DRAIN_TRAMPOLINE\(ctx\);\n"
    r" */\* nop \*/;\n)"
    r"( *ctx->gpr\[0\] = \(int64_t\)\(int32_t\)\(0\);\n"
    r" *vm_write32\(ctx->gpr\[31\] \+ 0x8, ctx->gpr\[0\]\);\n)"
)

YIELD = (
    "        /* " + MARKER + ": DONE early path cancel must free op */\n"
    "        { ppu_giant_lock_release();\n"
    "          ps3recomp_giant_lock_yield_sleep1();\n"
    "          { static int _ms=-1; if(_ms<0){const char* e=getenv(\"PS3_FIOS_DONE_YIELD_MS\");\n"
    "              _ms=(e&&*e)?atoi(e):50;}\n"
    "            if(_ms>0) usleep((useconds_t)_ms*1000u); }\n"
    "          ppu_giant_lock_acquire();\n"
    "          fprintf(stderr,\"[FIOSOPEN] 42B4-CANCEL-YIELD after DONE early cancel\\n\");\n"
    "          fflush(stderr); }\n"
)

DECL = (
    "/* " + MARKER + " decls */\n"
    'extern "C" void ppu_giant_lock_release(void);\n'
    'extern "C" void ppu_giant_lock_acquire(void);\n'
    'extern "C" void ps3recomp_giant_lock_yield_sleep1(void);\n'
    "#include <unistd.h>\n"
    "#include <stdlib.h>\n"
    "\n"
)


def _insert(m: "re.Match[str]") -> str:
    return m.group(1) + YIELD + m.group(2)


def patch_file(p: Path) -> str:
    t = p.read_text(encoding="utf-8", errors="replace")
    if MARKER in t:
        return "ALREADY"
    if "void func_002B42B4" not in t:
        return "SKIP"
    if not NEEDLE_RE.search(t):
        return "SKIP"
    t = NEEDLE_RE.sub(_insert, t, count=1)
    if 'extern "C" void ppu_giant_lock_release(void);' not in t:
        t = DECL + t
    p.write_text(t, encoding="utf-8")
    return "APPLIED"


def main() -> int:
    files = sorted(ROOT.glob("ppu_recomp_*.cpp"))
    if not files:
        print("nenhum ppu_recomp_*.cpp em %s" % ROOT)
        return 1
    for f in files:
        print("%s: %s" % (f.name, patch_file(f)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
