#!/usr/bin/env python3
"""E403 -- sondas BAD10: entrada de func_002BAD10 + r3 AFTER RETURN no call site lr=0x41F6C.

Entry: ctx->lr / r3 / r4 ainda vivos na primeira linha do corpo (controlo).
After-return: no unico `ctx->lr = 0x00041F6C; func_002BAD10; DRAIN` — r3 e' o
retorno do callee, nao recalculado.

Gate PS3_TRACE_BAD10 (OFF por default). Cap PS3_TRACE_BAD10_CAP (default 400)
so' na entrada. Diagnostico, nao e' fix.

Marcadores: E403-BAD10-LR (entrada), E403-BAD10-RET-41F6C (after-return).

rc: 0 aplicado ou ja-aplicado; 2 agulha ausente (0); 3 agulha != 1.
"""
from __future__ import annotations

import sys
from pathlib import Path

from lift_paths import resolve_lift_paths

MARKER_ENTRY = "E403-BAD10-LR"
MARKER_RET = "E403-BAD10-RET-41F6C"
FN_SIG = "void func_002BAD10(ppu_context* ctx) {\n"
RET_NEEDLE = "        ctx->lr = 0x00041F6C; func_002BAD10(ctx); DRAIN_TRAMPOLINE(ctx);\n"

ENTRY_PROBE = (
    FN_SIG
    + "        /* E403-BAD10-LR */ { static int _on=-1; if(_on<0){ extern char* getenv(const char*);\n"
    "          const char* _e=getenv(\"PS3_TRACE_BAD10\"); _on=(_e&&*_e&&*_e!='0')?1:0; }\n"
    "          if(_on){ static unsigned _n=0; static unsigned _cap=0; if(!_cap){\n"
    "            const char* _c=getenv(\"PS3_TRACE_BAD10_CAP\"); _cap=(_c&&*_c)?(unsigned)atoi(_c):400u; if(!_cap)_cap=400u; }\n"
    "            if(_n++<_cap){ fprintf(stderr,\"[BAD10] #%u entry desc(r3)=0x%08X flags(r4)=0x%08X lr=0x%08X\\n\",\n"
    "              _n,(uint32_t)ctx->gpr[3],(uint32_t)ctx->gpr[4],(uint32_t)ctx->lr); fflush(stderr);} } }\n"
)

RET_REPL = (
    RET_NEEDLE
    + "        /* E403-BAD10-RET-41F6C */ { static int _on=-1; if(_on<0){ extern char* getenv(const char*);\n"
    "          const char* _e=getenv(\"PS3_TRACE_BAD10\"); _on=(_e&&*_e&&*_e!='0')?1:0; }\n"
    "          if(_on){ fprintf(stderr,\"[BAD10] ret lr=0x00041F6C r3=0x%08X\\n\",\n"
    "            (uint32_t)ctx->gpr[3]); fflush(stderr);} }\n"
)


def _apply_site(texts: dict[Path, str], needle: str, marker: str, repl: str, label: str):
    """Apply one unique needle across all chunks. Returns (texts, status, rc_if_fail)."""
    already = [p for p, s in texts.items() if marker in s]
    counts = [(p, s.count(needle)) for p, s in texts.items()]
    total = sum(n for _, n in counts)
    if already:
        return texts, f"{label}: ALREADY", 0
    if total == 0:
        print(f"E403: {label}: needle 0x (esperado 1)", file=sys.stderr)
        return texts, f"{label}: FAILED 0x", 2
    if total != 1:
        print(f"E403: {label}: needle {total}x (esperado 1)", file=sys.stderr)
        return texts, f"{label}: FAILED {total}x", 3
    for p, n in counts:
        if n == 1:
            texts[p] = texts[p].replace(needle, repl, 1)
            print(f"E403: {p.name}: {label} APPLIED")
            break
    return texts, f"{label}: APPLIED", 0


def main() -> int:
    paths = resolve_lift_paths(sys.argv[1:], "recomp_macos_v2")
    texts: dict[Path, str] = {}
    for p in paths:
        if p.is_file():
            texts[p] = p.read_text(errors="replace")
    if not texts:
        print("E403: no ppu_recomp_*.cpp", file=sys.stderr)
        return 2

    texts, st_e, rc_e = _apply_site(texts, FN_SIG, MARKER_ENTRY, ENTRY_PROBE, "entry")
    if rc_e != 0:
        print(st_e)
        return rc_e
    texts, st_r, rc_r = _apply_site(texts, RET_NEEDLE, MARKER_RET, RET_REPL, "ret-41F6C")
    if rc_r != 0:
        print(st_r)
        return rc_r

    for p, s in texts.items():
        orig = p.read_text(errors="replace") if p.exists() else ""
        if s != orig:
            p.write_text(s)
    print(f"E403: {st_e}; {st_r}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
