#!/usr/bin/env python3
"""Yield after FIOS DONE-path cancel (func_002B4274) so the op returns to freelist.

WHY
---
func_002B4274 (poll 'done' branch) does:
  func_0030AE58(cancel)   # async
  container+8 = 0         # immediately, no yield

MovieStop later sees container+8==0 and skips cancel. The cancelled op only
returns to the freelist when the FIOS scheduler runs. Under the giant lock
that often never happens before the next open (R_LglScA) -> SEM OP LIVRE (F2a).

Measured 2026-07-21 Task 4b: guest opens R_LglScA after SEQDONE+MovieStop but
op_alloc #4 returns 0. STOP-YIELD 50ms after MovieStop alone was insufficient
because the movie op was already cancelled (without free) at DONE time.

FIX
---
Same pattern as FIOS-CANCEL-YIELD on func_002B3F78: release giant lock, sleep
(PS3_FIOS_DONE_YIELD_MS default 20), re-acquire, then clear container+8.

Marker: FIOS-DONE-CANCEL-YIELD. Idempotent.

REGRESSAO v1.1 (2026-07-31)
----------------------------
Mesma deriva de forma do lifter ja vista e corrigida em
patch_fios_cancel_yield.py e patch_fios_stop_yield.py: as DUAS chamadas da
agulha (func_002B3CB0 e func_0030AE58) passaram a vir prefixadas com
"ctx->lr = 0x...;" no lift actual. A agulha literal dava SKIP nos 7 chunks.
Fix: regex com o prefixo ctx->lr OPCIONAL em cada uma, grupo capturado
devolvido tal e qual.
"""
from pathlib import Path
import re
import sys

MARKER = "FIOS-DONE-CANCEL-YIELD"
ROOT = (Path(sys.argv[1]) if len(sys.argv) > 1
        else Path(__file__).resolve().parent.parent / "recomp_macos_v2")

NEEDLE_RE = re.compile(
    r"( *(?:ctx->lr = 0x[0-9A-Fa-f]+; )?func_002B3CB0\(ctx\); DRAIN_TRAMPOLINE\(ctx\);\n"
    r" *ctx->gpr\[9\] = vm_read32\(ctx->gpr\[2\] \+ -0x1460\);\n"
    r" *ctx->gpr\[4\] = vm_read32\(ctx->gpr\[31\] \+ 0x8\);\n"
    r" *ctx->gpr\[3\] = vm_read32\(ctx->gpr\[9\] \+ 0x118\);\n"
    r" *(?:ctx->lr = 0x[0-9A-Fa-f]+; )?func_0030AE58\(ctx\); DRAIN_TRAMPOLINE\(ctx\);\n"
    r" */\* nop \*/;\n)"
    r"( *ctx->gpr\[0\] = \(int64_t\)\(int32_t\)\(0\);\n"
    r" *vm_write32\(ctx->gpr\[31\] \+ 0x8, ctx->gpr\[0\]\);\n)"
)

YIELD = (
    "        /* " + MARKER + ": let FIOS scheduler free the cancelled op\n"
    "         * before we clear container+8 (F2a after R_LglScA). */\n"
    "        { ppu_giant_lock_release();\n"
    "          ps3recomp_giant_lock_yield_sleep1();\n"
    "          { static int _ms=-1; if(_ms<0){const char* e=getenv(\"PS3_FIOS_DONE_YIELD_MS\");\n"
    "              _ms=(e&&*e)?atoi(e):20;}\n"
    "            if(_ms>0) usleep((useconds_t)_ms*1000u); }\n"
    "          ppu_giant_lock_acquire();\n"
    "          fprintf(stderr,\"[FIOSOPEN] DONE-CANCEL-YIELD after 002B4274\\n\");\n"
    "          fflush(stderr); }\n"
)


def _insert(m: "re.Match[str]") -> str:
    return m.group(1) + YIELD + m.group(2)


def patch_file(p: Path) -> str:
    t = p.read_text(encoding="utf-8", errors="replace")
    if MARKER in t:
        return "ALREADY"
    if "void func_002B4274" not in t:
        return "SKIP"
    if not NEEDLE_RE.search(t):
        return "SKIP"
    t = NEEDLE_RE.sub(_insert, t, count=1)
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
