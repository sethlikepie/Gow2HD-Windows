#!/usr/bin/env python3
"""Gated probe: what does the state-3 handler read at [obj+0x744]?

WHY
---
EOS Task 7 Step 3 (docs/superpowers/plans/2026-07-20-macos-movie-eos-fsm.md).
The jump table (resolved by lldb) maps st620=3 -> func_002C05F8. That handler:
    r0 = vm_read8(obj + 0x744);          # EOS byte
    if r0 == 0 -> func_002C0688          # NOT done
    else       -> st620=4; st620=5; ...  # ADVANCE

Measured (armed run, host healthy): st620 goes 0->1->3->3->0 and NEVER touches
4/5. So func_002C05F8 took the ==0 branch even though g_movie_eos_ea is armed at
obj+0x744 (0x869F1C) and the read-hook in vm_read8 returns 1 for that EA. The
lift declares vm_read8 as the same extern symbol the runtime defines (hook
present) -- so the hook SHOULD reach this read. This probe logs the exact value
func_002C05F8 reads, to discriminate:
  - reads 0 with hook armed -> the hook is not covering this read (fix: arm may
    need to WRITE the real byte, still replay, not intercept);
  - reads 1 -> the advance branch was taken and the 3->0 comes from downstream.

Gated by PS3_TRACE_ST3, OFF by default. Read-only (logs, no guest writes).
Idempotent (marker ST3-PROBE). Never forges st620 / +0x744.
"""
from pathlib import Path
import re
import sys

MARKER = "ST3-PROBE"

ROOT = (Path(sys.argv[1]) if len(sys.argv) > 1
        else Path(__file__).resolve().parent.parent / "recomp_macos_v2")

# CORRECCAO 2026-07-25 (shape-outro: fragmento virou label):
# a agulha literal exigia "void func_002C05F8(ppu_context* ctx) {" seguido do
# read de +0x744. O lifter actual deixou de emitir 0x2C05F8 como funcao propria:
# fundiu o fragmento na funcao maior e o EA aparece apenas como "loc_002C05F8:"
# (ppu_recomp_001.cpp:45603 do lift limpo). O COMPORTAMENTO e' o mesmo -- a
# proxima instrucao continua a ser exactamente
#   ctx->gpr[0] = vm_read8(ctx->gpr[30] + 0x744);
# (verificado: 5 sitios leem +0x744 no chunk, mas so' um vem logo a seguir ao
# label/assinatura de 0x2C05F8). Regex tolerante que casa AS DUAS formas, para
# o patch continuar a funcionar no lift antigo (funcao) e no novo (label).
# Continua a ser so' log: nao escreve estado guest nem forja st620/+0x744.
NEEDLE_RE = re.compile(
    r"(?:^|\n)"
    r"(?:void[ \t]+func_002C05F8\(ppu_context\*[ \t]*ctx\)[ \t]*\{|loc_002C05F8:)"
    r"[ \t]*\n"
    r"[ \t]*ctx->gpr\[0\][ \t]*=[ \t]*vm_read8\(ctx->gpr\[30\][ \t]*\+[ \t]*0x744\);"
    r"[ \t]*\n"
)

PROBE = (
    "        /* " + MARKER + ": valor lido no gate de avancar 3->5 */\n"
    "        { static int _on=-1; if(_on<0){extern char* getenv(const char*);\n"
    "            const char* _e=getenv(\"PS3_TRACE_ST3\"); _on=(_e&&*_e&&*_e!='0')?1:0;}\n"
    "          if(_on){ static int _n=0; if(_n++<400){\n"
    "            fprintf(stderr,\"[ST3] loc_002C05F8 obj=0x%08X ea=0x%08X read[+0x744]=%u\\n\",\n"
    "              (uint32_t)ctx->gpr[30],(uint32_t)(ctx->gpr[30]+0x744),(unsigned)ctx->gpr[0]);\n"
    "            fflush(stderr); } } }\n"
)


def patch_file(p: Path) -> str:
    t = p.read_text(encoding="utf-8", errors="replace")
    if MARKER in t:
        return "ALREADY"
    m = NEEDLE_RE.search(t)
    if not m:
        return "SKIP"
    t = t[: m.end()] + PROBE + t[m.end():]
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
        print("SKIP: needle not found (func_002C05F8)")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
