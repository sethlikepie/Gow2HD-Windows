#!/usr/bin/env python3
"""Specialize func_00156D78 bctrl on the guest OPD pointer of func_001658A0.

EBOOT.ELF contains one BE 0x001658A0 at vma 0x00522988, immediately followed
by TOC 0x00541178 — the Cell OPD {code, toc}. The previous CTR==0x001658A0
compare never hit because the vtable holds this OPD, not the code EA.

On OPD match: set r2 to that TOC and call func_001658A0 directly (skip
OPD-deref, TOC spill, lookup, drain). Other OPDs keep the fat icall.
Idempotent. 00156D78 only.
"""
from __future__ import annotations

import argparse
from pathlib import Path

START = "void func_00156D78(ppu_context* ctx) {"
OPD = "0x00522988u"
TOC = "0x00541178ULL"
MARKER = f"(uint32_t)ctx->gpr[11] == {OPD}"
# two shapes: OPD in r11 vs r9
BLOCKS = (
    (
        "        ctx->gpr[0] = vm_read32(ctx->gpr[11] + 0x0);\n"
        "        vm_write64(ctx->gpr[1] + 0x28, ctx->gpr[2]);\n"
        "        ctx->ctr = (uint32_t)ctx->gpr[0];\n"
        "        ctx->gpr[2] = vm_read32(ctx->gpr[11] + 0x4);\n"
        "        ps3_indirect_call(ctx); DRAIN_TRAMPOLINE(ctx);\n"
        "        ctx->gpr[2] = 0x00541178ULL; /*TOCFIX ld r2,N(r1)*/\n",
        "        if ((uint32_t)ctx->gpr[11] == 0x00522988u) {\n"
        "            ctx->gpr[2] = 0x00541178ULL;\n"
        "            func_001658A0(ctx); DRAIN_TRAMPOLINE(ctx);\n"
        "            ctx->gpr[2] = 0x00541178ULL;\n"
        "        } else {\n"
        "            ctx->gpr[0] = vm_read32(ctx->gpr[11] + 0x0);\n"
        "            vm_write64(ctx->gpr[1] + 0x28, ctx->gpr[2]);\n"
        "            ctx->ctr = (uint32_t)ctx->gpr[0];\n"
        "            ctx->gpr[2] = vm_read32(ctx->gpr[11] + 0x4);\n"
        "            ps3_indirect_call(ctx); DRAIN_TRAMPOLINE(ctx);\n"
        "            ctx->gpr[2] = 0x00541178ULL; /*TOCFIX ld r2,N(r1)*/\n"
        "        }\n",
    ),
    (
        "        ctx->gpr[0] = vm_read32(ctx->gpr[9] + 0x0);\n"
        "        vm_write64(ctx->gpr[1] + 0x28, ctx->gpr[2]);\n"
        "        ctx->ctr = (uint32_t)ctx->gpr[0];\n"
        "        ctx->gpr[2] = vm_read32(ctx->gpr[9] + 0x4);\n"
        "        ps3_indirect_call(ctx); DRAIN_TRAMPOLINE(ctx);\n"
        "        ctx->gpr[2] = 0x00541178ULL; /*TOCFIX ld r2,N(r1)*/\n",
        "        if ((uint32_t)ctx->gpr[9] == 0x00522988u) {\n"
        "            ctx->gpr[2] = 0x00541178ULL;\n"
        "            func_001658A0(ctx); DRAIN_TRAMPOLINE(ctx);\n"
        "            ctx->gpr[2] = 0x00541178ULL;\n"
        "        } else {\n"
        "            ctx->gpr[0] = vm_read32(ctx->gpr[9] + 0x0);\n"
        "            vm_write64(ctx->gpr[1] + 0x28, ctx->gpr[2]);\n"
        "            ctx->ctr = (uint32_t)ctx->gpr[0];\n"
        "            ctx->gpr[2] = vm_read32(ctx->gpr[9] + 0x4);\n"
        "            ps3_indirect_call(ctx); DRAIN_TRAMPOLINE(ctx);\n"
        "            ctx->gpr[2] = 0x00541178ULL; /*TOCFIX ld r2,N(r1)*/\n"
        "        }\n",
    ),
)


def transform(text: str) -> tuple[str, str]:
    i = text.find(START)
    if i < 0:
        return text, "NO-EFFECT"
    j = text.find("\nvoid func_", i + len(START))
    body = text[i:j] if j >= 0 else text[i:]
    if "0x00522988u" in body:
        return text, "ALREADY"
    new_body = body
    n = 0
    for old, new in BLOCKS:
        c = new_body.count(old)
        if c:
            new_body = new_body.replace(old, new)
            n += c
    if n == 0:
        return text, "NO-EFFECT"
    out = text[:i] + new_body + (text[j:] if j >= 0 else "")
    return out, "APPLIED"


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("file", type=Path)
    args = ap.parse_args()
    original = args.file.read_text(encoding="utf-8", errors="replace")
    updated, state = transform(original)
    if state == "NO-EFFECT":
        print(f"NO-EFFECT {args.file}")
        return 1
    if state == "APPLIED":
        args.file.write_text(updated, encoding="utf-8")
    print(f"{state} {args.file.name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
