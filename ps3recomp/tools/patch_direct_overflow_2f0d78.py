#!/usr/bin/env python3
"""Replace func_002F0D78's overflow bctrl with a direct HLE call.

Sampled path: 00156D78 → 001658A0 → 001791A4 → 002F0D78 → ps3_indirect_call
→ ps3_gcm_ctx_overflow. Skip lookup/drain/TOC; keep the space check.
Idempotent. Does not touch other functions.
"""
from __future__ import annotations

import argparse
from pathlib import Path

DRAIN = (
    "#define DRAIN_TRAMPOLINE(ctx) do { \\\n"
    "    while (g_trampoline_fn) { \\\n"
    "        void(*_tf)(void*) = g_trampoline_fn; \\\n"
    "        g_trampoline_fn = 0; \\\n"
    "        _tf((void*)(ctx)); \\\n"
    "    } \\\n"
    "} while(0)\n"
)
DECL = 'extern "C" int32_t ps3_gcm_ctx_overflow(uint32_t ctx_ea);\n'
START = "void func_002F0D78(ppu_context* ctx) {"
OLD = (
    "        ctx->gpr[9] = vm_read32(ctx->gpr[10] + 0xC);\n"
    "        ctx->gpr[0] = vm_read32(ctx->gpr[9] + 0x0);\n"
    "        vm_write64(ctx->gpr[1] + 0x28, ctx->gpr[2]);\n"
    "        ctx->ctr = (uint32_t)ctx->gpr[0];\n"
    "        ctx->gpr[2] = vm_read32(ctx->gpr[9] + 0x4);\n"
    "        ps3_indirect_call(ctx); DRAIN_TRAMPOLINE(ctx);\n"
    "        ctx->gpr[2] = 0x00541178ULL; /*TOCFIX ld r2,N(r1)*/\n"
)
NEW = (
    "        ctx->gpr[3] = (int64_t)(int32_t)ps3_gcm_ctx_overflow("
    "(uint32_t)ctx->gpr[10]);\n"
)


def transform(text: str) -> tuple[str, str]:
    i = text.find(START)
    if i < 0:
        return text, "NO-EFFECT"
    j = text.find("\nvoid func_", i + len(START))
    body = text[i:j] if j >= 0 else text[i:]
    if "ps3_gcm_ctx_overflow((uint32_t)ctx->gpr[10])" in body:
        out = text
        if DECL not in out and DRAIN in out:
            out = out.replace(DRAIN, DRAIN + "\n" + DECL, 1)
        return out, "ALREADY"
    if OLD not in body:
        return text, "NO-EFFECT"
    new_body = body.replace(OLD, NEW, 1)
    if body.count(OLD) != 1:
        return text, "NO-EFFECT"
    out = text[:i] + new_body + (text[j:] if j >= 0 else "")
    if DECL not in out:
        if DRAIN not in out:
            return text, "NO-EFFECT"
        out = out.replace(DRAIN, DRAIN + "\n" + DECL, 1)
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
