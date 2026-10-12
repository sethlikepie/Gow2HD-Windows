#!/usr/bin/env python3
"""Probe [OPDISP-ENT] a' entrada de func_002A209C (gated PS3_TRACE_TYMAP)."""
from pathlib import Path
import re
import sys

from lift_paths import resolve_lift_paths

DEFAULT_LIFT = str(Path(__file__).resolve().parent.parent / "recomp_macos_v2")

MARKER = "OPDISP-ENT"
FN = "func_002A209C"

# CORRECCAO 2026-07-25 (shape-callee-save + chunk-fixo):
#
# 1. shape-callee-save -- o lifter passou a emitir, logo a seguir a abertura da
#    funcao, um snapshot dos callee-save:
#        void func_002A209C(ppu_context* ctx) {
#                uint64_t _cs_22 = ctx->gpr[22];
#                ... (ate' _cs_31)
#                { int64_t a = (int64_t)ctx->gpr[4]; ...
#    A agulha literal antiga colava a chaveta de abertura directamente ao
#    "{ int64_t a = ..." e deixou de casar. Passa a ser um regex que aceita
#    zero ou mais linhas "_cs_NN" no meio -- casa com o lift antigo (sem elas)
#    e com o novo. A probe entra DEPOIS do snapshot, imediatamente antes da
#    primeira instrucao guest, para nao se meter entre a abertura e os _cs_*.
#
# 2. chunk-fixo -- o script abria "ppu_recomp_001.cpp" pelo nome. O lifter
#    actual produz 7 chunks (eram 31) e a funcao pode mudar de ficheiro; passa
#    a aceitar um DIRECTORIO via resolve_lift_paths e a procurar o chunk que
#    DEFINE a funcao.
#
# A probe em si e' identica a' original (read-only, gated, cap 16).
ANCHOR_RE = re.compile(
    r"(void " + FN + r"\(ppu_context\* ctx\) \{\n"
    r"(?:        uint64_t _cs_\d+ = ctx->gpr\[\d+\];\n)*)"
    r"(        \{ int64_t a = \(int64_t\)ctx->gpr\[4\];)"
)

PROBE = """        { static int on=-1; if(on<0){extern char* getenv(const char*); on=getenv("PS3_TRACE_TYMAP")?1:0;}
          if(on){ static int n=0; if(n++<16)
            fprintf(stderr,"[OPDISP-ENT] #%d r3=0x%08X r4=0x%08X r5=0x%08X\\n",n,(uint32_t)ctx->gpr[3],(uint32_t)ctx->gpr[4],(uint32_t)ctx->gpr[5]); fflush(stderr);} }
"""


def main() -> int:
    paths = resolve_lift_paths(sys.argv[1:], DEFAULT_LIFT)
    for p in paths:
        if not p.is_file():
            print(f"skip {p} (missing)")
            continue
        s = p.read_text(encoding="utf-8", errors="replace")
        if MARKER in s:
            print(f"{p.name}: ALREADY ({MARKER} presente)")
            return 0
        if not re.search(r"^void " + FN + r"\(ppu_context\* ctx\) \{", s, re.M):
            continue
        m = ANCHOR_RE.search(s)
        if not m:
            print(f"{p.name}: needle missing (shape de {FN} mudou outra vez)")
            return 1
        s = s[: m.end(1)] + PROBE + s[m.start(2) :]
        p.write_text(s, encoding="utf-8", newline="\n")
        print(f"{p.name}: APPLIED entry probe [{MARKER}] em {FN}")
        return 0
    print(f"FAILED: {FN} nao definido em nenhum chunk")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
