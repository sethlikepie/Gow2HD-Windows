#!/usr/bin/env python3
"""Count Gate A/B signals from a menu-fast (or any) boot log.

Gate A focus: SetFlip / cellPadGetData *after* the last R_Perm full line
(bytes_read=20169344). Gate B: thr_end + R_Perm + FATAL/PARK.

Usage:
  python3 count_menu_gate.py [/tmp/gow2_menu_fast.log]
  LOG=/tmp/foo.log python3 count_menu_gate.py
"""
from __future__ import annotations

import os
import re
import sys


def count_gate(log_path: str) -> dict:
    text = open(log_path, errors="replace").read()
    lines = text.splitlines()

    def c(pat: str, s: str = text) -> int:
        return len(re.findall(pat, s, re.I))

    rperm_idxs = [
        i
        for i, l in enumerate(lines)
        if "R_Perm" in l and "20169344" in l
    ]
    post = ""
    rperm_i = -1
    if rperm_idxs:
        rperm_i = max(rperm_idxs)
        post = "\n".join(lines[rperm_i:])

    out = {
        "log": log_path,
        "lines": len(lines),
        "rperm_line": rperm_i,
        "R_Perm_full": 1 if rperm_idxs else 0,
        "thr_end": c(r"thr_auto_load\(\) end"),
        "PARK": c(r"\bPARK\b"),
        "FATAL": c(r"FATAL"),
        "attach_full": c(r"attach=full"),
        "CLOSE_PRESERVE": c(r"CLOSE-PRESERVE"),
        "SetFlip_total": c(r"SetFlip|cellGcmSetFlip"),
        "Pad_total": c(r"cellPadGetData"),
        "SetFlip_after_R_Perm": (
            len(re.findall(r"SetFlip|cellGcmSetFlip", post, re.I)) if post else 0
        ),
        "Pad_after_R_Perm": (
            len(re.findall(r"cellPadGetData", post, re.I)) if post else 0
        ),
        "CC9D0_SKIP": c(r"CC9D0-SKIP"),
        "ICALL_BAD": c(r"ICALL-BAD"),
        "ICALL_BAD_after_R_Perm": (
            len(re.findall(r"ICALL-BAD", post, re.I)) if post else 0
        ),
    }
    return out


def main() -> int:
    log = (
        sys.argv[1]
        if len(sys.argv) > 1
        else os.environ.get("LOG", "/tmp/gow2_menu_fast.log")
    )
    if not os.path.isfile(log):
        print(f"missing log: {log}", file=sys.stderr)
        return 2
    d = count_gate(log)
    print("=== MENU-GATE COUNTS ===")
    for k in (
        "log",
        "lines",
        "rperm_line",
        "R_Perm_full",
        "thr_end",
        "PARK",
        "FATAL",
        "attach_full",
        "CLOSE_PRESERVE",
        "SetFlip_total",
        "SetFlip_after_R_Perm",
        "Pad_total",
        "Pad_after_R_Perm",
        "CC9D0_SKIP",
        "ICALL_BAD",
        "ICALL_BAD_after_R_Perm",
    ):
        print(k, d[k])
    gate_b = (
        d["thr_end"] >= 1
        and d["R_Perm_full"] >= 1
        and d["PARK"] == 0
        and d["FATAL"] == 0
    )
    gate_a = d["SetFlip_after_R_Perm"] >= 1 or d["Pad_after_R_Perm"] >= 1
    print("Gate_B", "GREEN" if gate_b else "RED")
    print("Gate_A", "GREEN" if gate_a else "RED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
