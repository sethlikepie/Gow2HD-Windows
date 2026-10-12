"""Self-check for find_functions.py's `find_prologue_functions` mid-scan
safety-stop hardening (require a corroborating store-LR before trusting a
bare `mflr r0` as a genuine new-function boundary).

Background: the mid-scan safety-stop used to trust ANY `mflr r0` seen while
scanning forward for the current function's own `blr`, with zero
corroborating evidence -- exactly the false-positive class that truncated
the real func_002550C8's extent at func_002550E8 instead of its true
continuation 0x255178 (see games/gow2/recomp_mid_v2/patch_fallthrough_2550c8.py
and tools/audit_fallthrough.py). The fix requires the same store-LR evidence
(stw/std r0, X(r1)) within the next few instructions that the top-level
entry scan itself already requires, before treating the `mflr r0` as a
boundary and stopping.

Run: python3 tools/test_find_functions_prologue_corroboration.py
"""
from __future__ import annotations

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from find_functions import FunctionFinder
from ppu_disasm import Instruction


def _insn(addr: int, mnemonic: str, operands: str = "") -> Instruction:
    return Instruction(addr=addr, raw=0, mnemonic=mnemonic, operands=operands)


def test_uncorroborated_mflr_no_longer_truncates_early() -> None:
    """A bare `mflr r0` mid-body (no store-LR nearby -- a coincidental bit
    pattern, not a real prologue) must NOT stop the scan: it should continue
    past it and find the function's real `blr`, exactly reproducing the
    func_002550C8 shape where the true continuation lay further ahead than
    the coincidental match."""
    base = 0x1000
    insns = [
        _insn(base + 0x00, "mflr", "r0"),                 # genuine prologue
        _insn(base + 0x04, "stw", "r0, 4(r1)"),            # corroborated (entry scan)
        _insn(base + 0x08, "stwu", "r1, -0x20(r1)"),
        _insn(base + 0x0C, "or", "r5, r5, r5"),            # ordinary body insn
        _insn(base + 0x10, "or", "r6, r6, r6"),
        _insn(base + 0x14, "mflr", "r0"),                  # COINCIDENTAL, uncorroborated
        _insn(base + 0x18, "or", "r7, r7, r7"),            # no store-LR follows it
        _insn(base + 0x1C, "or", "r8, r8, r8"),
        _insn(base + 0x20, "or", "r9, r9, r9"),
        _insn(base + 0x24, "lwz", "r0, 4(r1)"),            # real epilogue
        _insn(base + 0x28, "mtlr", "r0"),
        _insn(base + 0x2C, "blr"),
    ]
    ff = FunctionFinder(insns, min_size=4)
    ff.find_prologue_functions()

    assert base in ff.functions, ff.functions
    func = ff.functions[base]
    assert func.end == base + 0x30, \
        f"expected the scan to run past the coincidental mflr to the real " \
        f"blr at 0x{base + 0x2C:X} (end 0x{base + 0x30:X}), got 0x{func.end:X}"
    # The coincidental address must NOT have been registered as its own
    # (phantom) function start by this pass.
    assert base + 0x14 not in ff.functions, ff.functions


def test_corroborated_adjacent_prologue_still_stops() -> None:
    """A genuinely adjacent function's prologue (mflr r0 immediately
    followed by a real store-LR) must still stop the outer scan exactly as
    before -- the hardening only removes trust in an UNCORROBORATED match,
    it must not regress the corroborated case."""
    base = 0x2000
    insns = [
        _insn(base + 0x00, "mflr", "r0"),
        _insn(base + 0x04, "stw", "r0, 4(r1)"),
        _insn(base + 0x08, "stwu", "r1, -0x20(r1)"),
        _insn(base + 0x0C, "or", "r5, r5, r5"),
        # No blr for this function within the scan window -- the next
        # instruction is a genuinely adjacent function's CORROBORATED
        # prologue, so the scan must stop right there.
        _insn(base + 0x10, "mflr", "r0"),
        _insn(base + 0x14, "std", "r0, 8(r1)"),           # corroborates it
        _insn(base + 0x18, "stdu", "r1, -0x30(r1)"),
        _insn(base + 0x1C, "or", "r6, r6, r6"),
        _insn(base + 0x20, "lwz", "r0, 8(r1)"),
        _insn(base + 0x24, "mtlr", "r0"),
        _insn(base + 0x28, "blr"),
    ]
    ff = FunctionFinder(insns, min_size=4)
    ff.find_prologue_functions()

    assert base in ff.functions, ff.functions
    func = ff.functions[base]
    assert func.end == base + 0x10, \
        f"expected the corroborated adjacent prologue at 0x{base + 0x10:X} " \
        f"to still stop the scan, got end 0x{func.end:X}"


def test_genuine_second_prologue_midbody_stays_a_boundary() -> None:
    """Finding 3: the counterpart the earlier tests were missing. A function
    whose body has NO blr of its own, followed mid-scan by a GENUINE second
    prologue (mflr r0 WITH its own store-LR), must still stop at that
    corroborated boundary -- the hardening must NOT let the first function
    grow past it and swallow the second function's blr. The second function
    must also be recovered as its own registered function. This is the case
    where the historical 0x2550E8, had it been a real mid-entry prologue,
    would legitimately have bounded the extent (so the hardening would NOT
    have grown it -- see the honest caveat in find_functions.py)."""
    base = 0x3000
    insns = [
        _insn(base + 0x00, "mflr", "r0"),                 # F1 genuine prologue
        _insn(base + 0x04, "stw", "r0, 4(r1)"),            # corroborates F1
        _insn(base + 0x08, "stwu", "r1, -0x20(r1)"),
        _insn(base + 0x0C, "or", "r5, r5, r5"),            # F1 body -- note: NO blr
        _insn(base + 0x10, "or", "r6, r6, r6"),
        _insn(base + 0x14, "mflr", "r0"),                  # F2 GENUINE prologue (mid-scan)
        _insn(base + 0x18, "std", "r0, 8(r1)"),            # corroborates F2 -> real boundary
        _insn(base + 0x1C, "stdu", "r1, -0x30(r1)"),
        _insn(base + 0x20, "or", "r7, r7, r7"),            # F2 body
        _insn(base + 0x24, "lwz", "r0, 8(r1)"),            # F2 epilogue
        _insn(base + 0x28, "mtlr", "r0"),
        _insn(base + 0x2C, "blr"),                          # F2's blr (belongs to F2, NOT F1)
    ]
    ff = FunctionFinder(insns, min_size=4)
    ff.find_prologue_functions()

    # F1 must stop at the corroborated F2 prologue, NOT grow to F2's blr.
    assert base in ff.functions, ff.functions
    assert ff.functions[base].end == base + 0x14, \
        f"F1 must stop at the genuine second prologue 0x{base + 0x14:X}, " \
        f"not swallow F2's blr; got end 0x{ff.functions[base].end:X}"
    # F2 must be recovered as its own registered function.
    assert base + 0x14 in ff.functions, ff.functions
    assert ff.functions[base + 0x14].end == base + 0x30, \
        f"F2 should run to its own blr (end 0x{base + 0x30:X}), got " \
        f"0x{ff.functions[base + 0x14].end:X}"


def main() -> int:
    test_uncorroborated_mflr_no_longer_truncates_early()
    test_corroborated_adjacent_prologue_still_stops()
    test_genuine_second_prologue_midbody_stays_a_boundary()
    print("ok: find_prologue_functions mid-scan mflr-r0 corroboration "
          "hardening (grows truncated extents on uncorroborated mflr, "
          "preserves genuine adjacent AND genuine mid-body second-prologue "
          "boundaries)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
