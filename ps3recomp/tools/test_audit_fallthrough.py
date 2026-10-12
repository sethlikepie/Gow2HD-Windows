"""Self-check for audit_fallthrough.py (matches this repo's tools/test_*.py
convention -- plain asserts + main(), no pytest dependency; the `tmp_path`
parameter is a real pathlib.Path handed in from main(), not a pytest
fixture).

Run: python3 tools/test_audit_fallthrough.py
"""
from __future__ import annotations

import json
import os
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import audit_fallthrough as af


def _write_functions_json(tmp_path: Path, entries: list[tuple[int, int]]) -> Path:
    p = tmp_path / "functions.json"
    p.write_text(json.dumps(
        [{"start": f"0x{s:08X}", "end": f"0x{e:08X}"} for s, e in entries]),
        encoding="utf-8")
    return p


# ---------------------------------------------------------------------------
# functions.json structural audit
# ---------------------------------------------------------------------------

def test_json_audit_clean_contiguous(tmp_path: Path) -> None:
    """A well-formed, fully contiguous chain has no findings at all: every
    `end` matches the next `start` exactly (the common case) and no range
    is malformed or backward."""
    entries = [
        (0x10000, 0x10020),
        (0x10020, 0x10040),
        (0x10040, 0x10060),
    ]
    p = _write_functions_json(tmp_path, entries)
    funcs = af.load_functions_json(str(p))
    findings = af.audit_json_functions(funcs)
    assert findings == [], findings


def test_json_audit_malformed_range_flagged(tmp_path: Path) -> None:
    """end <= start is always a hard, unambiguous CRITICAL finding."""
    entries = [(0x20000, 0x20020), (0x20020, 0x20010)]  # second entry inverted
    p = _write_functions_json(tmp_path, entries)
    funcs = af.load_functions_json(str(p))
    findings = af.audit_json_functions(funcs)
    malformed = [f for f in findings if f.kind == "malformed-range"]
    assert len(malformed) == 1, findings
    assert malformed[0].severity == af.SEV_CRITICAL
    assert malformed[0].start == 0x20020


def test_json_audit_gap_reported_as_info_not_critical(tmp_path: Path) -> None:
    """A function whose `end` lands in a gap (not itself a registered start)
    is common and expected (inter-function padding/data) -- INFO, not
    CRITICAL/WARN, and it must correctly compute what ppu_lifter's fallback
    would resolve to (the next function's start in address order)."""
    entries = [
        (0x30000, 0x30010),   # end 0x30010 is NOT a registered start (gap)
        (0x30020, 0x30040),
    ]
    p = _write_functions_json(tmp_path, entries)
    funcs = af.load_functions_json(str(p))
    findings = af.audit_json_functions(funcs)
    gaps = [f for f in findings if f.kind == "end-not-a-boundary"]
    assert len(gaps) == 1, findings
    assert gaps[0].severity == af.SEV_INFO
    assert gaps[0].start == 0x30000
    assert gaps[0].target == 0x30020  # the fallback (next start in order)


def test_json_audit_duplicate_start_flagged(tmp_path: Path) -> None:
    p = tmp_path / "dup_functions.json"
    p.write_text(json.dumps([
        {"start": "0x40000", "end": "0x40010"},
        {"start": "0x40000", "end": "0x40020"},
    ]), encoding="utf-8")
    funcs = af.load_functions_json(str(p))
    findings = af.audit_json_functions(funcs)
    dups = [f for f in findings if f.kind == "duplicate-start"]
    assert len(dups) == 1, findings


def test_json_audit_heuristic_tiny_chain(tmp_path: Path) -> None:
    """--heuristics-only speculative chain detector: 3 contiguous functions
    each <= threshold bytes should be reported as one chain finding; a
    single isolated tiny function (no contiguous tiny neighbor) should not."""
    entries = [
        (0x50000, 0x50008),   # 8B  \
        (0x50008, 0x50010),   # 8B   > chain of 3 tiny, contiguous
        (0x50010, 0x50018),   # 8B  /
        (0x50018, 0x50100),   # normal-sized, breaks the chain
        (0x60000, 0x60008),   # isolated tiny function, no tiny neighbor
        (0x60008, 0x60200),   # normal-sized
    ]
    p = _write_functions_json(tmp_path, entries)
    funcs = af.load_functions_json(str(p))

    without = af.audit_json_functions(funcs, heuristics=False)
    assert not any(f.kind == "tiny-chain-heuristic" for f in without), \
        "heuristic must be OFF by default"

    with_h = af.audit_json_functions(funcs, heuristics=True, tiny_threshold=8)
    chains = [f for f in with_h if f.kind == "tiny-chain-heuristic"]
    assert len(chains) == 1, chains
    assert chains[0].start == 0x50000 and chains[0].end == 0x50018


# ---------------------------------------------------------------------------
# Generated .cpp trampoline audit
# ---------------------------------------------------------------------------

def test_cpp_flags_known_2550c8_bug_shape(tmp_path: Path) -> None:
    """Reproduces the CONSISTENT func_002550C8 -> func_002550E8 shape that a
    real re-lift actually produces (findings 1 & 2): functions.json and the
    generated .cpp are emitted by the SAME lift, so they AGREE -- the JSON
    records func_002550C8 already TRUNCATED to end 0x2550E8, and the .cpp
    trampolines to func_002550E8 == that recorded end. The true continuation
    (0x255178) lies PAST the phantom neighbour and is invisible to
    functions.json.

    Key numeric point this test now protects (and the reason the tool was
    redesigned): the wrong target 0x2550E8 EQUALS the recorded end, and is
    HIGHER than the site's own start -- so BOTH the old "target < start" and
    "target < end" predicates are FALSE here and would MISS the production
    bug. The redesigned detector catches it structurally: the tail-position
    trampoline lands exactly on the site's own recorded `end`, an adjacent
    registered function, and the site fragment is suspiciously small (0x20
    bytes). The `assert bugs[0].target == bugs[0].end` below pins that
    target == end (i.e. proves the pre-fix `target < end` logic could not
    have fired -- run this test against the pre-fix code and it fails).
    """
    cpp = tmp_path / "ppu_recomp_000.cpp"
    cpp.write_text(
        "void func_002550C8(ppu_context* ctx) {\n"
        "        ctx->gpr[3] = ctx->gpr[4];\n"
        "        if (((ctx->cr >> 0) & 2)) { g_trampoline_fn = (void(*)(void*))func_00254E84; return; }\n"
        "        { g_trampoline_fn = (void(*)(void*))func_002550E8; return; }\n"
        "}\n"
        "\n"
        "void func_002550E8(ppu_context* ctx) {\n"
        "        ctx->gpr[3] = ctx->gpr[3] + 1;\n"
        "        return;\n"
        "}\n"
        "\n"
        "void func_00255178(ppu_context* ctx) {\n"
        "        ctx->gpr[3] = 0;\n"
        "        return;\n"
        "}\n",
        encoding="utf-8")

    # CONSISTENT truncated triple: A=[site, B_start] (truncated, small),
    # B registered at its own start, and the TRUE continuation C further out.
    entries = [
        (0x2550C8, 0x2550E8),  # A: truncated end == B's start (the phantom split)
        (0x2550E8, 0x255178),  # B: the phantom neighbour A wrongly falls into
        (0x255178, 0x2551A0),  # C: the TRUE sequential continuation, further out
    ]
    fjson = _write_functions_json(tmp_path, entries)
    funcs = af.load_functions_json(str(fjson))

    findings = af.audit_cpp_trampolines(str(cpp), funcs)
    bugs = [f for f in findings if f.kind == "cpp-consistent-truncated-fallthrough"]
    assert len(bugs) == 1, findings
    assert bugs[0].severity == af.SEV_HIGH
    assert bugs[0].start == 0x2550C8
    assert bugs[0].target == 0x2550E8
    assert bugs[0].end == 0x2550E8
    # Pins the production-mode invariant: target == end (NOT target < end).
    # The pre-fix detector keyed on `target < end`, which is False here --
    # this assertion documents why the pre-fix logic missed the real bug.
    assert bugs[0].target == bugs[0].end, bugs[0]

    # The conditional trampoline to func_00254E84 (an `if (...) { ... }`
    # guard) must NEVER be flagged -- it is a genuine conditional branch,
    # not a fallthrough, regardless of its target's direction.
    assert not any(f.target == 0x254E84 for f in findings), findings


def test_cpp_explicit_backward_tailcall_not_flagged(tmp_path: Path) -> None:
    """Finding 5: a SMALL function whose last instruction is an explicit
    backward `b func_EARLIER` (a legitimate tail-call to an earlier
    subroutine) must NOT be flagged, even though the pre-fix `target < end`
    logic WOULD have flagged it (0x500000 < 0x600040). The redesigned
    detector distinguishes it structurally: an auto-appended fallthrough can
    only ever target the resolved fallthrough (the site's recorded end,
    0x600040), so a trampoline to 0x500000 != 0x600040 is provably an
    explicit branch, not the fallthrough bug class."""
    cpp = tmp_path / "ppu_recomp_backtail.cpp"
    cpp.write_text(
        "void func_00600000(ppu_context* ctx) {\n"
        "        ctx->gpr[3] = 0;\n"
        "        { g_trampoline_fn = (void(*)(void*))func_00500000; return; }\n"
        "}\n"
        "\n"
        "void func_00500000(ppu_context* ctx) {\n"
        "        return;\n"
        "}\n",
        encoding="utf-8")
    # func_00600000 is small (0x40) AND falls to an EARLIER address -- the exact
    # false-positive the pre-fix `target < end` HIGH check produced.
    entries = [
        (0x500000, 0x500040),
        (0x600000, 0x600040),
        (0x600040, 0x600080),
    ]
    fjson = _write_functions_json(tmp_path, entries)
    funcs = af.load_functions_json(str(fjson))

    findings = af.audit_cpp_trampolines(str(cpp), funcs)
    assert findings == [], \
        f"explicit backward tail-call must not be flagged (target 0x500000 " \
        f"!= resolved fallthrough 0x600040): {findings}"


def test_cpp_large_consistent_fallthrough_gated_by_size(tmp_path: Path) -> None:
    """A LARGE function that genuinely falls through to its adjacent
    registered neighbour (target == recorded end, an ordinary multi-piece
    split) must NOT be flagged HIGH by default -- only suspiciously SMALL
    truncated fragments are. Proven by flipping the same fixture with a
    larger --fallthrough-max-size, which then DOES flag it: the size gate is
    the sole discriminator here, not some incidental mismatch."""
    cpp = tmp_path / "ppu_recomp_bigsplit.cpp"
    cpp.write_text(
        "void func_00800000(ppu_context* ctx) {\n"
        "        ctx->gpr[3] = ctx->gpr[3] + 1;\n"
        "        { g_trampoline_fn = (void(*)(void*))func_00800400; return; }\n"
        "}\n"
        "\n"
        "void func_00800400(ppu_context* ctx) {\n"
        "        return;\n"
        "}\n",
        encoding="utf-8")
    entries = [(0x800000, 0x800400), (0x800400, 0x800800)]  # site is 0x400 bytes
    fjson = _write_functions_json(tmp_path, entries)
    funcs = af.load_functions_json(str(fjson))

    # Default threshold (0x80): 0x400-byte site is too large -> not flagged.
    default = af.audit_cpp_trampolines(str(cpp), funcs)
    assert default == [], default

    # Raise the threshold above the site size -> the SAME consistent
    # fallthrough now qualifies as a small-fragment candidate.
    raised = af.audit_cpp_trampolines(str(cpp), funcs, fallthrough_max_size=0x800)
    bugs = [f for f in raised if f.kind == "cpp-consistent-truncated-fallthrough"]
    assert len(bugs) == 1, raised
    assert bugs[0].severity == af.SEV_HIGH
    assert bugs[0].start == 0x800000
    assert bugs[0].target == 0x800400 == bugs[0].end


def test_cpp_no_functions_json_falls_back_to_low_confidence(tmp_path: Path) -> None:
    """Same shape as above, but WITHOUT --functions: 0x2550E8 > 0x2550C8
    (the site's start) so the LOW-confidence fallback (target < site start)
    correctly does NOT fire either -- this documents the tool's honest
    limitation rather than forging a finding it cannot support."""
    cpp = tmp_path / "ppu_recomp_000_nostructural.cpp"
    cpp.write_text(
        "void func_002550C8(ppu_context* ctx) {\n"
        "        ctx->gpr[3] = ctx->gpr[4];\n"
        "        { g_trampoline_fn = (void(*)(void*))func_002550E8; return; }\n"
        "}\n",
        encoding="utf-8")
    findings = af.audit_cpp_trampolines(str(cpp), funcs=None)
    assert findings == [], findings


def test_cpp_does_not_flag_legitimate_forward_tailcall(tmp_path: Path) -> None:
    """A tail-position unconditional trampoline to a LATER, unrelated
    function (target >= this function's own end) is a normal forward
    continuation / tail call and must never be flagged."""
    cpp = tmp_path / "ppu_recomp_fwd.cpp"
    cpp.write_text(
        "void func_00300000(ppu_context* ctx) {\n"
        "        ctx->gpr[3] = ctx->gpr[3] + 1;\n"
        "        { g_trampoline_fn = (void(*)(void*))func_00400000; return; }\n"
        "}\n"
        "\n"
        "void func_00400000(ppu_context* ctx) {\n"
        "        return;\n"
        "}\n",
        encoding="utf-8")
    entries = [(0x300000, 0x300010), (0x400000, 0x400010)]
    fjson = _write_functions_json(tmp_path, entries)
    funcs = af.load_functions_json(str(fjson))

    findings = af.audit_cpp_trampolines(str(cpp), funcs)
    assert findings == [], findings


def test_cpp_does_not_flag_midbody_backward_loop_trampoline(tmp_path: Path) -> None:
    """A genuine cross-fragment loop-back-edge / earlier-subroutine tail
    call (tools/ppu_lifter.py:1374) sitting in the MIDDLE of a function's
    body (i.e. NOT the last statement before the closing brace) is
    legitimate in any direction and must never be flagged, even though its
    target (0x4FFF00) is numerically lower than the enclosing function's own
    start (0x500000) -- exactly the "backward branch-trampoline (loop)"
    case this tool is required to leave alone."""
    cpp = tmp_path / "ppu_recomp_loop.cpp"
    cpp.write_text(
        "void func_00500000(ppu_context* ctx) {\n"
        "        ctx->gpr[5] = ctx->gpr[5] - 1;\n"
        "        { g_trampoline_fn = (void(*)(void*))func_004FFF00; return; }\n"
        "        ctx->gpr[3] = 0;\n"
        "        return;\n"
        "}\n",
        encoding="utf-8")
    entries = [(0x4FFF00, 0x500000), (0x500000, 0x500020)]
    fjson = _write_functions_json(tmp_path, entries)
    funcs = af.load_functions_json(str(fjson))

    candidates = af.scan_cpp_trampolines(str(cpp))
    assert candidates == [], \
        f"mid-body trampoline must never be a tail-position candidate: {candidates}"

    findings = af.audit_cpp_trampolines(str(cpp), funcs)
    assert findings == [], findings


def test_cpp_low_confidence_candidate_without_functions_json(tmp_path: Path) -> None:
    """Without --functions, a tail-position unconditional trampoline whose
    target IS lower than the enclosing function's own start is reported,
    but only as a LOW-confidence WARN candidate (never CRITICAL/HIGH) --
    the tool cannot rule out a legitimate backward tail-call without ELF
    ground truth."""
    cpp = tmp_path / "ppu_recomp_candidate.cpp"
    cpp.write_text(
        "void func_00700000(ppu_context* ctx) {\n"
        "        ctx->gpr[3] = 0;\n"
        "        { g_trampoline_fn = (void(*)(void*))func_00100000; return; }\n"
        "}\n",
        encoding="utf-8")
    findings = af.audit_cpp_trampolines(str(cpp), funcs=None)
    assert len(findings) == 1, findings
    assert findings[0].severity == af.SEV_WARN
    assert findings[0].kind == "cpp-backward-fallthrough-candidate"


def main() -> int:
    with tempfile.TemporaryDirectory() as d:
        tmp = Path(d)
        test_json_audit_clean_contiguous(tmp)
        test_json_audit_malformed_range_flagged(tmp)
        test_json_audit_gap_reported_as_info_not_critical(tmp)
        test_json_audit_duplicate_start_flagged(tmp)
        test_json_audit_heuristic_tiny_chain(tmp)
        test_cpp_flags_known_2550c8_bug_shape(tmp)
        test_cpp_explicit_backward_tailcall_not_flagged(tmp)
        test_cpp_large_consistent_fallthrough_gated_by_size(tmp)
        test_cpp_no_functions_json_falls_back_to_low_confidence(tmp)
        test_cpp_does_not_flag_legitimate_forward_tailcall(tmp)
        test_cpp_does_not_flag_midbody_backward_loop_trampoline(tmp)
        test_cpp_low_confidence_candidate_without_functions_json(tmp)
    print("ok: audit_fallthrough structural + .cpp trampoline checks pass "
          "(CONSISTENT 2550C8 phantom-split shape flagged HIGH; explicit "
          "backward tail-calls, large splits, forward tail-calls and "
          "mid-body backward loop trampolines correctly left unflagged)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
