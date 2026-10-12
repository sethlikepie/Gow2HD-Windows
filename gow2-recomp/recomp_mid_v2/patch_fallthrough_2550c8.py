#!/usr/bin/env python3
"""Plano 3 Task 3f — fix fallthrough errado em func_002550C8 (chunk 000).

Bug: o epílogo de func_002550C8 (após o check de count em ~0x25516x) era
emitido como trampoline para func_002550E8 (0x2550E8, reentrada do corpo do
grow). O alvo correto é o fallthrough sequencial 0x255178 = func_00255178
(fase fill/next), igual ao epílogo já correto da cópia em ppu_recomp_002.cpp
(func_002550E8 → func_00255178).

Efeito do bug: com r3 ainda apontando para a tabela recém-alocada, 2550E8
fazia 2639F8(r3) e empurrava o bloco de DADOS cru como "arena corrente".
O alocador lia [bloco+4] como free-head → OOB em 0x8400000x → R_PermA
congelava em 5.619.712 bytes.

Fix: 2550C8 fallthrough → 255178. Aceite medido: bytes_read=20.169.344.

Idempotente. Roda a partir de recomp_mid_v2/ (ou com path no argv).
"""
from __future__ import annotations

import sys
from pathlib import Path

NEEDLE = (
    "        if (((ctx->cr >> 0) & 2)) { g_trampoline_fn = (void(*)(void*))func_00254E84; return; }\n"
    "        { g_trampoline_fn = (void(*)(void*))func_002550E8; return; }\n"
    "}\n"
    "\n"
    "void func_00255178(ppu_context* ctx) {"
)

REPL = (
    "        if (((ctx->cr >> 0) & 2)) { g_trampoline_fn = (void(*)(void*))func_00254E84; return; }\n"
    "        /* Task 3f FIX: fallthrough guest after count-check at ~0x25516x is 0x255178\n"
    "         * (fill/next phase), NOT 0x2550E8 (grow body re-entry). Wrong target re-enters\n"
    "         * grow with r3=allocated table and 2639F8-pushes the raw data block as arena\n"
    "         * scope → free-list OOB → R_PermA freeze at 5.6MB. Chunk-002 epilogue already\n"
    "         * targets 255178. */\n"
    "        { g_trampoline_fn = (void(*)(void*))func_00255178; return; }\n"
    "}\n"
    "\n"
    "void func_00255178(ppu_context* ctx) {"
)

ALREADY = "func_00255178; return; }\n}\n\nvoid func_00255178"


def main() -> int:
    root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent
    path = root / "ppu_recomp_000.cpp"
    if not path.is_file():
        print(f"FAIL: {path} not found", file=sys.stderr)
        return 1
    src = path.read_text(encoding="utf-8", errors="replace")
    if "Task 3f FIX: fallthrough" in src or (
        "func_002550C8" in src
        and src.count(NEEDLE) == 0
        and "g_trampoline_fn = (void(*)(void*))func_00255178; return;" in src
        and "void func_00255178" in src
    ):
        # Heuristic: after 254E84 check inside 000, the non-eq path points at 255178
        # and is immediately followed by func_00255178 definition.
        idx = src.find("void func_002550C8")
        end = src.find("void func_00255178", idx)
        chunk = src[idx:end] if idx >= 0 and end > idx else ""
        if "func_002550E8; return;" not in chunk and "func_00255178; return;" in chunk:
            print("OK: already patched")
            return 0
    if NEEDLE not in src:
        print("FAIL: needle not found (lifted source layout changed?)", file=sys.stderr)
        return 2
    # Only patch the occurrence that sits right before func_00255178 (chunk 000 body).
    path.write_text(src.replace(NEEDLE, REPL, 1), encoding="utf-8", newline="\n")
    print(f"OK: patched {path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
