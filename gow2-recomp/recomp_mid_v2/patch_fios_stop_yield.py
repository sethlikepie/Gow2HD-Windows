#!/usr/bin/env python3
"""Yield after MovieStop SMPD epilogue so FIOS can free ops before WAD open.

WHY
---
After SEQDONE + EOS arm, MovieStop (st=11 path func_002BFFE0/FFF4/C0048 in
chunk 002, and loc_002C0048 in 001) cancels media and broadcasts SMPD, then
returns to a caller that immediately opens R_LglScA. Under the giant lock the
FIOS scheduler rarely drains cancel before that open -> SEM OP LIVRE (F2a).

Measured 2026-07-21 Task 4b: yield alone is NOT sufficient when the movie op
was already cancelled without free at DONE (see patch_fios_done_cancel_yield).
Still useful when cancel is pending at Stop time.

REGRESSAO v1.1 (2026-07-31)
----------------------------
A agulha literal (sem "ctx->lr = 0x...;") deixou de bater em qualquer lift
novo -- o lifter passou a prefixar esta chamada com o LR de retorno
("ctx->lr = 0x002C0074; func_0043FF30(ctx); ..."), o MESMO padrao ja visto e
corrigido em patch_fios_cancel_yield.py para func_0030AE58. SKIP nos 7 chunks
(4 sitios reais: 1 em ppu_recomp_001.cpp, 3 em ppu_recomp_002.cpp).

Fix: mesma tecnica -- regex com o prefixo ctx->lr OPCIONAL, ancorada nas 3
linhas fixas (write 0x620 + chamada + nop), que continuam a casar
EXATAMENTE os 4 sitios reais (confirmado por varredura: nenhum outro
call-site de func_0043FF30 nos 7 chunks tem este write 0x620 imediatamente
antes). O grupo capturado devolve a linha da chamada tal e qual -- nunca
reescrita -- para nao perder o LR novo do lifter.

Marker: FIOS-STOP-YIELD. Idempotente (lookahead nega re-insercao onde o
marcador ja segue o anchor). Gated por PS3_FIOS_STOP_YIELD_MS (default 50;
0 desliga).
"""
from pathlib import Path
import re
import sys

MARKER = "FIOS-STOP-YIELD"
ROOT = (Path(sys.argv[1]) if len(sys.argv) > 1
        else Path(__file__).resolve().parent.parent / "recomp_macos_v2")

# Ancora: as 3 linhas fixas do epilogo do MovieStop (write do SMPD st620=0,
# a chamada a func_0043FF30 -- com ou sem o prefixo ctx->lr do lifter novo --
# e o nop pos-DRAIN). Idempotencia e' a nivel de ficheiro (MARKER in t), nao
# por lookahead: patch_fios_freelist_rebuild.py ancora no MESMO trio de
# linhas e pode correr antes ou depois deste script (ordem alfabetica do
# apply_all_patches.sh corre "freelist_rebuild" antes de "stop_yield" -- ver
# nota de ordem no proprio patch_fios_freelist_rebuild.py). Um lookahead por
# marcador proprio quebraria em re-execucoes com a ordem trocada; o check
# "MARKER in t" de ficheiro inteiro e' o que ja se usa em
# patch_fios_done_cancel_yield.py e sobrevive a qualquer ordem.
NEEDLE_RE = re.compile(
    r"( *vm_write32\(ctx->gpr\[31\] \+ 0x620, ctx->gpr\[0\]\);\n"
    r" *(?:ctx->lr = 0x[0-9A-Fa-f]+; )?func_0043FF30\(ctx\); DRAIN_TRAMPOLINE\(ctx\);\n"
    r" */\* nop \*/;\n)"
)


def _insert(m: "re.Match[str]") -> str:
    anchor = m.group(1)
    block = (
        "        /* " + MARKER + ": free FIOS ops before caller opens R_LglScA (F2a).\n"
        "         * Default 50ms; PS3_FIOS_STOP_YIELD_MS=0 disables. */\n"
        "        { static int _ms=-1; if(_ms<0){const char* e=getenv(\"PS3_FIOS_STOP_YIELD_MS\");\n"
        "            _ms = (e&&*e) ? atoi(e) : 50;}\n"
        "          if(_ms>0){ ppu_giant_lock_release();\n"
        "            usleep((useconds_t)_ms * 1000u);\n"
        "            ppu_giant_lock_acquire();\n"
        "            fprintf(stderr,\"[FIOSOPEN] STOP-YIELD %d ms after MovieStop\\n\", _ms);\n"
        "            fflush(stderr); } }\n"
    )
    return anchor + block


DECL = (
    "/* " + MARKER + " decls */\n"
    'extern "C" void ppu_giant_lock_release(void);\n'
    'extern "C" void ppu_giant_lock_acquire(void);\n'
    "#include <unistd.h>\n"
    "#include <stdlib.h>\n"
    "\n"
)


def patch_file(p: Path) -> str:
    t = p.read_text(encoding="utf-8", errors="replace")
    if MARKER in t:
        return "ALREADY"
    matches = list(NEEDLE_RE.finditer(t))
    if not matches:
        return "SKIP"
    n = 0

    def repl(m: "re.Match[str]") -> str:
        nonlocal n
        n += 1
        return _insert(m)

    t = NEEDLE_RE.sub(repl, t)
    if MARKER + " decls" not in t:
        if "#include <math.h>\n" in t:
            t = t.replace("#include <math.h>\n", "#include <math.h>\n" + DECL, 1)
        else:
            t = DECL + t
    p.write_text(t, encoding="utf-8")
    return "APPLIED x%d" % n


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
