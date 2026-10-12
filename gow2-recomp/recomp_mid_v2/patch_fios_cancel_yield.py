#!/usr/bin/env python3
"""Yield the PPU giant lock after FIOS cancel so the scheduler can free the op.

WHY
---
Play() when st620!=0 does:
  0x002C0498 -> bl 0x002BFF88 -> b 0x002C0124  (re-enter full Play, re-open)

func_002BFF88 for st620 in [1..10] calls func_002B3F78(container):
  if [container+8] != 0:
    func_0030AE58(media, op)   # async cancel
    [container+8] = 0          # clear handle immediately

Cancel is async; the op stays allocated until the FIOS scheduler runs
estado=2. The re-open that follows still holds the giant lock, so the
scheduler cannot free the slot -> SEM OP LIVRE -> open returns 0 and the
intro state-1 poll has nothing to wait on (io=0 forever).

Measured (macOS 2026-07-20): baseline luck let complete estado=2 interleave
between Play#2 entry and 002B4340; without a yield after cancel that window
closes and open#2 fails.

FIX
---
After the cancel call returns, release the giant lock for 1 ms so the FIOS
scheduler thread can drain the cancel and free the op before this thread
clears the handle and continues to re-open.

Marker: FIOS-CANCEL-YIELD. Idempotent.
"""
from pathlib import Path
import sys

MARKER = "FIOS-CANCEL-YIELD"

ROOT = (Path(sys.argv[1]) if len(sys.argv) > 1
        else Path(__file__).resolve().parent.parent / "recomp_macos_v2")

# NOTA (2026-07-25): o lifter passou a prefixar as chamadas com o LR de
# retorno -- "ctx->lr = 0x002B3FB0; func_0030AE58(ctx); ...". A agulha literal
# deixou de bater em qualquer lift novo. Passa a regex com o prefixo OPCIONAL,
# para casar com as duas formas; o grupo 1 devolve a linha da chamada tal e
# qual, para a reinserir sem lhe tocar.
import re

NEEDLE_RE = re.compile(
    r"( *(?:ctx->lr = 0x[0-9A-Fa-f]+; )?func_0030AE58\(ctx\); DRAIN_TRAMPOLINE\(ctx\);\n)"
    r"( */\* nop \*/;\n)"
    r"( *ctx->gpr\[0\] = \(int64_t\)\(int32_t\)\(0\);\n)"
    r"( *vm_write32\(ctx->gpr\[31\] \+ 0x8, ctx->gpr\[0\]\);\n)"
)

YIELD = (
    "        /* " + MARKER + ": let FIOS scheduler free the cancelled op\n"
    "         * before we clear container+8 and the caller re-opens. */\n"
    "        { ppu_giant_lock_release();\n"
    "          ps3recomp_giant_lock_yield_sleep1();\n"
    "          ppu_giant_lock_acquire(); }\n"
)


def _insert(m):
    """Reinsere as linhas originais e mete o yield entre o nop e o clear."""
    return m.group(1) + m.group(2) + YIELD + m.group(3) + m.group(4)

DECL = (
    "/* " + MARKER + " decls (C linkage; must be namespace scope) */\n"
    'extern "C" void ppu_giant_lock_release(void);\n'
    'extern "C" void ppu_giant_lock_acquire(void);\n'
    'extern "C" void ps3recomp_giant_lock_yield_sleep1(void);\n'
    "\n"
)


def patch_file(p: Path) -> str:
    t = p.read_text(encoding="utf-8", errors="replace")
    if MARKER in t and "ppu_giant_lock_release();" in t:
        return "ALREADY"
    if "void func_002B3F78" not in t:
        return "SKIP"
    if not NEEDLE_RE.search(t):
        return "SKIP"
    t = NEEDLE_RE.sub(_insert, t, count=1)
    if MARKER + " decls" not in t:
        t = t.replace(
            "void func_002B3F78(ppu_context* ctx) {",
            DECL + "void func_002B3F78(ppu_context* ctx) {",
            1,
        )
    p.write_text(t, encoding="utf-8")
    return "APPLIED"


def main() -> int:
    files = sorted(ROOT.glob("ppu_recomp_*.cpp"))
    if not files:
        print("nenhum ppu_recomp_*.cpp em %s" % ROOT)
        return 1
    any_hit = False
    for p in files:
        r = patch_file(p)
        if r != "SKIP":
            any_hit = True
            print("%s: %s" % (p.name, r))
    if not any_hit:
        print("SKIP: needle not found (func_002B3F78 cancel site)")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
