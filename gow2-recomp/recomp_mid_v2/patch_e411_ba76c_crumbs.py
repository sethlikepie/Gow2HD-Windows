#!/usr/bin/env python3
"""E411 -- breadcrumbs in BA76C state-1 path (BA990/BA9BC/2B9F00).

Question: where does the first BA76C tick hang?
P1 never BA990; P2 BA990's 4224; P3 2B9F00/2B45D0; P4 2B9F54+.
Gate PS3_TRACE_BAD10. Marker E411-BA76C-CRUMB. rc 0/2/3.
"""
from __future__ import annotations

import sys
from pathlib import Path

from lift_paths import resolve_lift_paths

MARK = "E411-BA76C-CRUMB"

def crumb(tag: str) -> str:
    return (
        f"        /* E411-BA76C-CRUMB {tag} */ {{ static int _on=-1; if(_on<0){{ extern char* getenv(const char*);\n"
        f"          const char* _e=getenv(\"PS3_TRACE_BAD10\"); _on=(_e&&*_e&&*_e!='0')?1:0; }}\n"
        f"          if(_on){{ fprintf(stderr,\"[BA76C] {tag} r3=0x%08X\\n\", (uint32_t)ctx->gpr[3]); fflush(stderr); }} }}\n"
    )

SITES = [
    (
        "void func_002BA990(ppu_context* ctx) {\n",
        "void func_002BA990(ppu_context* ctx) {\n" + crumb("BA990-entry"),
        1,
        "BA990",
    ),
    (
        "void func_002BA9BC(ppu_context* ctx) {\n",
        "void func_002BA9BC(ppu_context* ctx) {\n" + crumb("BA9BC-entry"),
        1,
        "BA9BC",
    ),
    (
        "        ctx->lr = 0x002B9F20; func_002B45D0(ctx); DRAIN_TRAMPOLINE(ctx);\n",
        crumb("2B9F00-pre-45D0")
        + "        ctx->lr = 0x002B9F20; func_002B45D0(ctx); DRAIN_TRAMPOLINE(ctx);\n"
        + crumb("2B9F00-post-45D0"),
        1,
        "2B9F00-45D0",
    ),
]


def main() -> int:
    paths = resolve_lift_paths(sys.argv[1:], "recomp_macos_v2")
    texts: dict[Path, str] = {}
    for p in paths:
        if p.is_file():
            texts[p] = p.read_text(errors="replace")
    if not texts:
        print("E411: no ppu_recomp_*.cpp", file=sys.stderr)
        return 2
    if sum(s.count(MARK) for s in texts.values()):
        print("E411: ALREADY")
        return 0
    for needle, repl, expect, label in SITES:
        n = sum(s.count(needle) for s in texts.values())
        if n != expect:
            print(f"E411: {label} {n}x (esperado {expect})", file=sys.stderr)
            return 3 if n else 2
        for p, s in list(texts.items()):
            if needle in s:
                texts[p] = s.replace(needle, repl, 1)
                print(f"E411: {p.name}: {label} APPLIED")
    for p, s in texts.items():
        if s != p.read_text(errors="replace"):
            p.write_text(s)
    print("E411: APPLIED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
