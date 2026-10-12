#!/usr/bin/env python3
"""E162: 8 jump-table cases in func_0043E118 (SCREAM stream dispatcher).

The lifter emitted one case (0x43E468) and sent the other seven indices to
ps3_indirect_tail. Index 7 is 0x43E480, which is unlabeled fallthrough in the
same function (li r9,0; b func_0043DD90), not a fragment start → ICALL-BAD
and return without epilogue.

ELF table at 0x43E134 (8 signed offsets). Targets already exist as unlabeled
PPC in the fragment; this patch only names them and lists them in the switch.

Idempotent. Usage:
  python3 patch_e162_43e118_jt.py [LIFT_DIR]
  python3 patch_e162_43e118_jt.py --selftest
"""
from __future__ import annotations

import sys
from pathlib import Path

try:
    from lift_paths import resolve_lift_paths
except ImportError:
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from lift_paths import resolve_lift_paths

MARKER = "E162 FIX: 8-case jump table 43E118"

SWITCH_1 = (
    "switch ((uint32_t)ctx->ctr) { case 0x0043E468u: goto loc_0043E468; "
    "default: ps3_indirect_tail(ctx); return; }"
)
SWITCH_8 = (
    "switch ((uint32_t)ctx->ctr) { "
    "case 0x0043E468u: goto loc_0043E468; "
    "case 0x0043E470u: goto loc_0043E470; "
    "case 0x0043E454u: goto loc_0043E454; "
    "case 0x0043E478u: goto loc_0043E478; "
    "case 0x0043E1E4u: goto loc_0043E1E4; "
    "case 0x0043E488u: goto loc_0043E488; "
    "case 0x0043E490u: goto loc_0043E490; "
    "case 0x0043E480u: goto loc_0043E480; "
    "default: ps3_indirect_tail(ctx); return; }"
    " /* " + MARKER + " */"
)

# Unlabeled 8-byte cases after loc_0043E468 (li r9,imm; b ...).
CASES_OLD = """loc_0043E468:
        ctx->gpr[9] = (int64_t)(int32_t)(0xF0);
        goto loc_0043E1E8;
        ctx->gpr[9] = (int64_t)(int32_t)(0x12C);
        goto loc_0043E1E8;
        ctx->gpr[9] = (int64_t)(int32_t)(0x5A);
        { g_trampoline_fn = (void(*)(void*))func_0043DD90; return; }
        ctx->gpr[9] = (int64_t)(int32_t)(0);
        { g_trampoline_fn = (void(*)(void*))func_0043DD90; return; }
        ctx->gpr[9] = (int64_t)(int32_t)(0xD2);
        goto loc_0043E1E8;
        ctx->gpr[9] = (int64_t)(int32_t)(0xB4);
        goto loc_0043E1E8;"""

CASES_NEW = """loc_0043E468:
        ctx->gpr[9] = (int64_t)(int32_t)(0xF0);
        goto loc_0043E1E8;
loc_0043E470:
        ctx->gpr[9] = (int64_t)(int32_t)(0x12C);
        goto loc_0043E1E8;
loc_0043E478:
        ctx->gpr[9] = (int64_t)(int32_t)(0x5A);
        { g_trampoline_fn = (void(*)(void*))func_0043DD90; return; }
loc_0043E480:
        ctx->gpr[9] = (int64_t)(int32_t)(0);
        { g_trampoline_fn = (void(*)(void*))func_0043DD90; return; }
loc_0043E488:
        ctx->gpr[9] = (int64_t)(int32_t)(0xD2);
        goto loc_0043E1E8;
loc_0043E490:
        ctx->gpr[9] = (int64_t)(int32_t)(0xB4);
        goto loc_0043E1E8;"""

LI_14A_OLD = (
    "        ctx->gpr[9] = (int64_t)(int32_t)(0x14A);\n"
    "loc_0043E1E8:"
)
LI_14A_NEW = (
    "loc_0043E1E4:\n"
    "        ctx->gpr[9] = (int64_t)(int32_t)(0x14A);\n"
    "loc_0043E1E8:"
)

CASES_8 = (
    "0x0043E468u",
    "0x0043E470u",
    "0x0043E454u",
    "0x0043E478u",
    "0x0043E1E4u",
    "0x0043E488u",
    "0x0043E490u",
    "0x0043E480u",
)


def apply_text(src: str) -> tuple[str, int]:
    """Return (new_text, n_edits). 0 edits if already applied."""
    n = 0
    out = src
    if SWITCH_1 in out:
        c = out.count(SWITCH_1)
        out = out.replace(SWITCH_1, SWITCH_8)
        n += c
    if CASES_OLD in out:
        c = out.count(CASES_OLD)
        out = out.replace(CASES_OLD, CASES_NEW)
        n += c
    # LI_14A_OLD is a suffix of LI_14A_NEW — skip once loc_0043E1E4 exists.
    if "loc_0043E1E4:" not in out and LI_14A_OLD in out:
        c = out.count(LI_14A_OLD)
        out = out.replace(LI_14A_OLD, LI_14A_NEW)
        n += c
    return out, n


def switch_covers_eight(src: str) -> bool:
    """True when every 8-case switch lists all ELF targets including 0x43E480."""
    n_one = src.count(SWITCH_1)
    if n_one:
        return False
    n_marked = src.count(MARKER)
    if n_marked == 0:
        return False
    for c in CASES_8:
        if src.count(f"case {c}:") < n_marked:
            return False
    if "loc_0043E480:" not in src:
        return False
    return True


FIXTURE = """void func_0043E118(ppu_context* ctx) {
        ctx->ctr = (uint32_t)ctx->gpr[0];
        switch ((uint32_t)ctx->ctr) { case 0x0043E468u: goto loc_0043E468; default: ps3_indirect_tail(ctx); return; } return;
        ctx->gpr[9] = (int64_t)(int32_t)(0x14A);
loc_0043E1E8:
        ctx->gpr[10] = 0;
loc_0043E454:
        ctx->gpr[9] = 0;
loc_0043E468:
        ctx->gpr[9] = (int64_t)(int32_t)(0xF0);
        goto loc_0043E1E8;
        ctx->gpr[9] = (int64_t)(int32_t)(0x12C);
        goto loc_0043E1E8;
        ctx->gpr[9] = (int64_t)(int32_t)(0x5A);
        { g_trampoline_fn = (void(*)(void*))func_0043DD90; return; }
        ctx->gpr[9] = (int64_t)(int32_t)(0);
        { g_trampoline_fn = (void(*)(void*))func_0043DD90; return; }
        ctx->gpr[9] = (int64_t)(int32_t)(0xD2);
        goto loc_0043E1E8;
        ctx->gpr[9] = (int64_t)(int32_t)(0xB4);
        goto loc_0043E1E8;
}
"""


def selftest() -> int:
    ok = 1
    def check(cond, msg):
        nonlocal ok
        if not cond:
            print("  [FAIL]", msg)
            ok = 0

    patched, n = apply_text(FIXTURE)
    check(n > 0, f"expected edits, got {n}")
    check(switch_covers_eight(patched), "fixture must list 8 cases")
    check("case 0x0043E480u:" in patched, "0x43E480 must be a case")
    check("loc_0043E480:" in patched, "loc_0043E480 label")
    check("loc_0043E1E4:" in patched, "loc_0043E1E4 label")
    check("ps3_indirect_tail" in patched, "default tail kept")
    again, n2 = apply_text(patched)
    check(n2 == 0, f"idempotent, got {n2} edits")
    check(again == patched, "second apply is no-op")
    print("test_patch_e162_43e118_jt: PASS" if ok else "test_patch_e162_43e118_jt: FAIL")
    return 0 if ok else 1


def main(argv: list[str]) -> int:
    if "--selftest" in argv:
        return selftest()
    args = [a for a in argv[1:] if a != "--selftest"]
    paths = resolve_lift_paths(args, "recomp_macos_v2")
    total = 0
    for p in paths:
        if not p.is_file():
            print("skip", p)
            continue
        src = p.read_text(errors="replace")
        new, n = apply_text(src)
        if n == 0:
            continue
        p.write_text(new)
        total += n
        print(f"OK: {p.name} edits={n}")
    print("OK total", total)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
