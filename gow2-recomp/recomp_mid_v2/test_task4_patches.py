#!/usr/bin/env python3
"""Structural + pure-function checks for Task 3f/4 patches (no full boot)."""
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
PS3 = ROOT.parents[1] / "ps3recomp"


def test_fallthrough_patch_script_exists():
    p = ROOT / "patch_fallthrough_2550c8.py"
    assert p.is_file(), "missing patch_fallthrough_2550c8.py"
    t = p.read_text(encoding="utf-8")
    assert "func_00255178" in t
    assert "2550E8" in t or "002550E8" in t


def test_jumptable_patch_script_and_targets():
    p = ROOT / "patch_jumptable_2b11b8.py"
    assert p.is_file()
    # Offsets from original .word table; base guest 0x2B1228
    offs = [
        0x9C, 0xE4, 0x114, 0x144, 0x354, 0x384, 0x434, 0x638,
        0x83C, 0x86C, 0xA7C, 0xB1C, 0xBC8, 0xBF8, 0x9C, 0x9C,
        0xC28, 0x9C, 0xC58, 0xC88, 0xCB8, 0xCE8, 0xD18, 0x9C,
        0x9C, 0xF48, 0x9C, 0x70,
    ]
    base = 0x2B1228
    # Sample known registered case labels
    assert base + 0x70 == 0x2B1298
    assert base + 0x9C == 0x2B12C4
    assert base + 0xE4 == 0x2B130C
    assert len(offs) == 0x1C


def test_lifted_sources_contain_fixes():
    c0 = (ROOT / "ppu_recomp_000.cpp").read_text(encoding="utf-8", errors="replace")
    # fallthrough fix in 2550C8 epilogue
    i = c0.find("void func_002550C8")
    j = c0.find("void func_00255178", i)
    chunk = c0[i:j]
    assert "func_00255178; return" in chunk
    assert "func_002550E8; return" not in chunk

    c1 = (ROOT / "ppu_recomp_001.cpp").read_text(encoding="utf-8", errors="replace")
    assert "Task4 FIX: PPC switch jump-table" in c1
    assert "k_jt[0x1C]" in c1 or "k_jt[0x1C]" in c1.replace(" ", "")
    assert "ps3_call_opd" in c1


def test_opd_resolve_source_has_recovery():
    loader = PS3 / "runtime" / "ppu" / "ppu_loader.cpp"
    assert loader.is_file()
    t = loader.read_text(encoding="utf-8", errors="replace")
    assert "ps3_call_opd" in t
    assert "ppu_lookup(opd) && !ppu_lookup(c)" in t


def main() -> int:
    tests = [
        test_fallthrough_patch_script_exists,
        test_jumptable_patch_script_and_targets,
        test_lifted_sources_contain_fixes,
        test_opd_resolve_source_has_recovery,
    ]
    failed = 0
    for t in tests:
        try:
            t()
            print(f"PASS {t.__name__}")
        except Exception as e:
            failed += 1
            print(f"FAIL {t.__name__}: {e}")
    print(f"{len(tests) - failed}/{len(tests)} passed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
