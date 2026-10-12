#!/usr/bin/env python3
"""Promote ctx->gpr[N] to host locals only inside func_00293010.

Spill/fill around every guest call (func_*/ps3_indirect_call + DRAIN).
Idempotent. Neighbors unchanged.
"""
from __future__ import annotations

import argparse
import re
from pathlib import Path

START = "void func_00293010("
MARK = "uint64_t r0 = ctx->gpr[0];"
GPR_RE = re.compile(r"ctx->gpr\[(\d+)\]")
CALL_RE = re.compile(
    r"(?:func_[0-9A-Fa-f]+|ps3_indirect_call)\(ctx\); DRAIN_TRAMPOLINE\(ctx\);"
)


def _func_span(text: str) -> tuple[int, int] | None:
    i = text.find(START)
    if i < 0:
        return None
    j = text.find("\nvoid func_", i + len(START))
    if j < 0:
        j = len(text)
    return i, j


def _sync_block(regs: list[int], kind: str) -> str:
    if kind == "spill":
        lines = [f"        ctx->gpr[{n}] = r{n};" for n in regs]
    else:
        lines = [f"        r{n} = ctx->gpr[{n}];" for n in regs]
    return "\n".join(lines)


def transform(text: str) -> tuple[str, str]:
    span = _func_span(text)
    if span is None:
        return text, "NO-EFFECT"
    i, j = span
    func = text[i:j]
    if MARK in func:
        return text, "ALREADY"
    brace = func.find("{")
    if brace < 0:
        return text, "NO-EFFECT"
    body = func[brace + 1 :]
    regs = sorted({int(n) for n in GPR_RE.findall(body)})
    if not regs:
        return text, "NO-EFFECT"
    loc_body = GPR_RE.sub(lambda m: f"r{m.group(1)}", body)

    def wrap_call(m: re.Match[str]) -> str:
        return (
            _sync_block(regs, "spill")
            + "\n        "
            + m.group(0)
            + "\n"
            + _sync_block(regs, "fill")
        )

    loc_body = CALL_RE.sub(wrap_call, loc_body)
    ret = loc_body.rfind("        return;")
    if ret >= 0:
        loc_body = loc_body[:ret] + _sync_block(regs, "spill") + "\n" + loc_body[ret:]
    loads = "\n".join(f"        uint64_t r{n} = ctx->gpr[{n}];" for n in regs)
    new_func = func[: brace + 1] + "\n" + loads + loc_body
    return text[:i] + new_func + text[j:], "APPLIED"


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
