#!/usr/bin/env python3
"""Extract func_00293010 + func_00248F3C + func_00156D78 into one TU.

Strips those definitions from 003.cpp / 004.cpp so the symbols live only
in the new file. The hot-tree TU keeps 004's lift preamble (DRAIN, ppc_*
helpers) and is meant to be compiled -O3 without 7-TU LTO. Idempotent.
"""
from __future__ import annotations

import argparse
from pathlib import Path

FUNCS = ("func_00248F3C", "func_00293010", "func_00156D78")


def _func_span(text: str, name: str) -> tuple[int, int] | None:
    start = f"void {name}("
    i = text.find(start)
    if i < 0:
        return None
    j = text.find("\nvoid func_", i + len(start))
    if j < 0:
        j = len(text)
    return i, j


def _preamble(text: str) -> str:
    i = text.find("\nvoid func_")
    if i < 0:
        i = text.find("void func_")
        if i < 0:
            return text
        return text[:i]
    return text[: i + 1]


def transform(src003: str, src004: str) -> tuple[str, str, str, str]:
    spans = {}
    for name in FUNCS:
        src = src004 if name != "func_00156D78" else src003
        sp = _func_span(src, name)
        if sp is None:
            return src003, src004, "", "NO-EFFECT"
        spans[name] = (src, sp)

    bodies = []
    for name in FUNCS:
        src, (i, j) = spans[name]
        bodies.append(src[i:j].rstrip() + "\n")

    pre = _preamble(src004)
    if "DRAIN_TRAMPOLINE" not in pre or "ppc_rldicl" not in pre:
        return src003, src004, "", "NO-EFFECT"

    hot = pre.rstrip() + "\n\n" + "\n".join(bodies)
    if not hot.endswith("\n"):
        hot += "\n"

    def _strip(text: str, names: tuple[str, ...]) -> str:
        out = text
        for name in names:
            sp = _func_span(out, name)
            if sp is None:
                continue
            i, j = sp
            out = out[:i] + out[j:]
        return out

    new003 = _strip(src003, ("func_00156D78",))
    new004 = _strip(src004, ("func_00248F3C", "func_00293010"))
    return new003, new004, hot, "APPLIED"


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("src003", type=Path)
    ap.add_argument("src004", type=Path)
    ap.add_argument("out_hot", type=Path)
    args = ap.parse_args()
    s003 = args.src003.read_text(encoding="utf-8", errors="replace")
    s004 = args.src004.read_text(encoding="utf-8", errors="replace")
    new003, new004, hot, state = transform(s003, s004)
    if state == "NO-EFFECT":
        print(f"NO-EFFECT {args.src003} {args.src004}")
        return 1
    args.src003.write_text(new003, encoding="utf-8")
    args.src004.write_text(new004, encoding="utf-8")
    args.out_hot.write_text(hot, encoding="utf-8")
    print(f"{state} {args.out_hot.name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
