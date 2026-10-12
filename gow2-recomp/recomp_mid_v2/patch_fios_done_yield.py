#!/usr/bin/env python3
"""Hand the giant lock to the state-1 poller right after publishing [op+0x90].

WHY
---
Quarta peca do inventario FIOS-* (regressao do marco v1.1, 2026-07-31).
func_00306534 e' o PRODUTOR do done-word (publica [op+0x90] e chama
ps3_fios_sticky_publish -- ja presente no lift actual, ver CLAUDE.md
"O megaprojeto vencido" e o check "publish caminho A" em
apply_all_patches.sh). Mas sob o giant lock unico, sem um yield logo a
seguir a publicacao, esta mesma thread tende a correr TODO o resto do
`complete` (func_0030600C e adiante) antes de o poller do estado 1
(func_002B4224) sequer ter hipotese de observar done!=0 -- o sticky
publish/restore mitiga a janela, mas o lift antigo (a fonte do binario que
funciona) tambem entregava deliberadamente o lock aqui, ANTES de tentar o
resto do complete.

Bloco extraido *verbatim* de
`recomp_macos_v2.pre_v4/ppu_recomp_001.cpp:108526-108532` -- nao foi
reescrito de memoria. So' existe 1 sitio em todo o lift antigo e o mesmo 1
sitio (a ancora do publish) existe no lift actual, confirmado unico por
varredura.

FIX
---
Logo depois do `if (...) ps3_fios_sticky_publish(...)` e ANTES de zerar
[gpr1+0x70]/[gpr1+0x74] e chamar func_0030600C: liberta o giant lock,
cede a vez (ps3recomp_giant_lock_yield_sleep1, sem sleep extra por env --
o original nao gateava por ms aqui), reacquire. Sempre-on (o bloco original
nao tinha um PS3_..._MS a desligar; so' o STOP/DONE-CANCEL variants tinham).

Marker: FIOS-DONE-YIELD. Idempotente (whole-file marker check).
"""
from pathlib import Path
import re
import sys

MARKER = "FIOS-DONE-YIELD"
ROOT = (Path(sys.argv[1]) if len(sys.argv) > 1
        else Path(__file__).resolve().parent.parent / "recomp_macos_v2")

# Ancora: o if+publish do done-word em func_00306534 -- confirmado unico
# (1 ocorrencia) em todo o lift.
NEEDLE_RE = re.compile(
    r"( *if \(\(\(uint32_t\)ctx->gpr\[11\]\) != 0u\)\n"
    r" *ps3_fios_sticky_publish\(\(uint32_t\)ctx->gpr\[31\]\);\n)"
)

BLOCK = (
    "        /* " + MARKER + ": publish [op+0x90] then hand the giant lock to the\n"
    "         * state-1 poller before the rest of complete runs. Under the single\n"
    "         * big lock the FIOS thread otherwise finishes the whole complete\n"
    "         * path (and any clear) before the poller can observe done!=0. */\n"
    "        { ppu_giant_lock_release();\n"
    "          ps3recomp_giant_lock_yield_sleep1();\n"
    "          ppu_giant_lock_acquire(); }\n"
)

DECL = (
    "/* " + MARKER + " decls */\n"
    'extern "C" void ppu_giant_lock_release(void);\n'
    'extern "C" void ppu_giant_lock_acquire(void);\n'
    'extern "C" void ps3recomp_giant_lock_yield_sleep1(void);\n'
    "\n"
)


def _insert(m: "re.Match[str]") -> str:
    return m.group(1) + BLOCK


def patch_file(p: Path) -> str:
    t = p.read_text(encoding="utf-8", errors="replace")
    if MARKER in t:
        return "ALREADY"
    if "void func_00306534" not in t:
        return "SKIP"
    matches = list(NEEDLE_RE.finditer(t))
    if not matches:
        return "SKIP"
    n = 0

    def repl(m: "re.Match[str]") -> str:
        nonlocal n
        n += 1
        return _insert(m)

    t = NEEDLE_RE.sub(repl, t)
    if 'extern "C" void ppu_giant_lock_release(void);' not in t:
        t = t.replace(
            "void func_00306534(ppu_context* ctx) {",
            DECL + "void func_00306534(ppu_context* ctx) {",
            1,
        )
    p.write_text(t, encoding="utf-8")
    return "APPLIED x%d" % n


def main() -> int:
    files = sorted(ROOT.glob("ppu_recomp_*.cpp"))
    if not files:
        print("nenhum ppu_recomp_*.cpp em %s" % ROOT)
        return 1
    any_hit = False
    for f in files:
        r = patch_file(f)
        if r != "SKIP":
            any_hit = True
        print("%s: %s" % (f.name, r))
    if not any_hit:
        print("SKIP: needle not found (func_00306534 sticky publish)")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
