#!/usr/bin/env python3
"""audit_fallthrough.py -- auditor for the "phantom mid-function split ->
wrong fallthrough trampoline" bug class first found (and fixed) in
func_002550C8.

Background (see this repo's CLAUDE.md and
games/gow2/recomp_mid_v2/patch_fallthrough_2550c8.py): tools/ppu_lifter.py
emits a `g_trampoline_fn = func_XXXXXXXX;` continuation whenever a lifted
function's last instruction is not a terminator (PPC "falls through" to the
next physical address -- tools/ppu_lifter.py:629-650, `_function_def_lines`
at tools/ppu_lifter.py:3115-3148). Critically, that auto-appended
continuation always targets the function's own recorded `end`
(`func.fallthrough_to = end`, tools/ppu_lifter.py:650): if `end` is itself a
registered function start it trampolines there ("exact"); otherwise it falls
back to "the next function in address order" ("fallback",
tools/ppu_lifter.py:3135-3144). BOTH resolutions are always >= the recorded
`end` (and therefore > the function's own start). `end` boundaries come from
tools/find_functions.py's heuristic passes. When one of those heuristics
mis-detects a boundary *inside* what should be a single function's true span
(e.g. a coincidental `mflr r0` bit pattern mid-body, mistaken for a new
function's prologue -- what truncated func_002550C8 at func_002550E8), the
recorded `end` is drawn too early and the auto-appended fallthrough
trampolines into that phantom neighbour instead of continuing to the true,
later body (0x255178).

THE REAL BUG IS "CONSISTENT" -- this is the key correction over the tool's
first cut. functions.json and the generated .cpp are produced by the SAME
lift, so in the real failure they AGREE: functions.json records
func_002550C8 = [0x2550C8, 0x2550E8] (already truncated), and the .cpp
trampolines to func_002550E8 == that recorded end. The wrong target
(0x2550E8) is HIGHER than the site's own start (0x2550C8) AND EQUAL to (not
below) the recorded end. So neither "target < start" NOR "target < end" can
catch the production shape -- both are artefacts of an INCONSISTENT
hand-constructed input and never occur in a real re-lift. The detectable
signature is instead: an auto-appended fallthrough whose target IS the
recorded `end` boundary (a registered, adjacent, DIFFERENT function) sitting
suspiciously CLOSE to the site (a small truncated fragment). The true
continuation lies past that neighbour, invisible to functions.json alone.

This tool has two complementary checks:

1. functions.json structural audit (always runs; the only required input).
   functions.json ALONE cannot tell whether any function actually falls
   through (that needs the real disassembly/.cpp), and -- because a genuine
   auto-appended fallthrough always targets `end` (>= end > start) -- it
   also cannot exhibit a "backward" resolved target for well-formed input.
   So the JSON pass is limited to structural facts:
     - CRITICAL "malformed-range": end <= start (a broken entry).
     - WARN "duplicate-start": two entries share a start (one shadows the
       other in func_by_addr).
     - INFO "end-not-a-boundary" ("gap"): the function's recorded `end` is
       not itself a registered function start, so IF this function falls
       through, ppu_lifter's fallback path would aim at a DIFFERENT address
       than the committed `end`. Common and usually harmless (most functions
       never fall through), reported as a summary + capped sample.
   Optional (--heuristics, OFF by default, clearly speculative):
     - INFO "tiny-chain-heuristic": chains of >=2 contiguous, suspiciously
       tiny functions -- the observed SHAPE of a phantom mid-function split,
       but a coarse proxy with a high false-positive rate and NOT proof of a
       bug on its own.
   NOTE: the concrete func_002550C8 instance is NOT visible from
   functions.json alone (the truncated triple looks perfectly well-formed).
   The real detector for it is check 2 (needs the generated .cpp).

2. Generated .cpp trampoline audit (the real detector; only if --cpp given).
   Greps literal, UNCONDITIONAL `{ g_trampoline_fn = (void(*)(void*))func_
   XXXXXXXX; return; }` assignments (NOT the `if (cond) { ... }` guarded form
   emitted for conditional branches -- tools/ppu_lifter.py:1441-1454 --
   which is always a genuine conditional branch, never a fallthrough). Among
   the unconditional ones, only the trampoline that is the LAST statement of
   its enclosing function (immediately before the closing `}`) is a
   fallthrough CANDIDATE -- the only position the auto-appended continuation
   can occupy (tools/ppu_lifter.py:3144). Mid-body unconditional trampolines
   are ordinary cross-fragment branches / tail-calls / loop-back-edges
   (tools/ppu_lifter.py:1362,1374), legitimate in ANY direction, and are
   NEVER flagged.

   For a tail-position candidate, the tool distinguishes an AUTO-APPENDED
   fallthrough from an EXPLICIT `b`/`bc` tail-call WITHOUT needing to tell
   them apart textually (they are byte-identical). It uses the invariant
   above: an auto-appended fallthrough can ONLY target what
   `resolve_fallthrough_target` computes from functions.json (the recorded
   `end`, or the next start on a gap). So, with --functions:
     - target == the resolved fallthrough AND that resolution is the
       recorded `end` itself (an "exact" adjacent registered function) AND
       the site fragment is small (<= --fallthrough-max-size): this is the
       consistent phantom-split shape -- func_002550C8 -> func_002550E8.
       Flagged HIGH (a heuristic candidate: a genuine small function that
       really falls through into its neighbour, or a legitimate multi-piece
       split at the right boundary, would also match -- confirm against the
       real disassembly; the size gate is a proxy for "truncated too
       early", not a proof).
     - target == the resolved fallthrough but large, or a "fallback"
       forward continuation across a gap: a normal split / forward
       continuation -- NOT flagged.
     - target != the resolved fallthrough: the trampoline is NOT an
       auto-appended fallthrough at all -- it is an explicit `b`/`bc`
       tail-call (a real PPC branch, legitimate in any direction, including
       backward to an earlier subroutine). NOT flagged. This is the key
       correction that stops the tool from crying "backward fallthrough" at
       every legitimate backward tail-call: only the fallthrough resolution
       is the risky auto-appended kind.
   Without --functions there is no `end`/resolution to compare against, so
   the tool falls back to the cruder "target < site's own start" signal at
   LOW confidence (WARN candidate only -- a legitimate backward tail-call
   would also match; pass --functions for the principled check).

LIMITATION -- read before treating any finding as a proven bug: this tool
never has access to the real PPU disassembly in this environment. Every
finding is "unexercised" static analysis over functions.json / a generated
.cpp's TEXT only. The HIGH .cpp finding is a heuristic candidate, not a
proof. Confirm against the actual ppu_recomp_XXX.cpp and a real boot before
calling anything here a fix.

Usage:
    python tools/audit_fallthrough.py games/gow2/functions.json
    python tools/audit_fallthrough.py games/gow2/functions.json --heuristics
    python tools/audit_fallthrough.py games/gow2/functions.json \\
        --cpp /path/to/ppu_recomp_000.cpp
    python tools/audit_fallthrough.py games/gow2/functions.json \\
        --cpp /path/to/ppu_recomp_000.cpp --fallthrough-max-size 0x40
    python tools/audit_fallthrough.py games/gow2/functions.json --json > report.json

Exit code: 0 if no CRITICAL/HIGH findings, 2 if any were found, 1 on a usage
error (bad input file). This is informational tooling, not a CI gate by
default -- callers who want to gate on it can check the exit code.
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path

SEV_CRITICAL = "CRITICAL"
SEV_HIGH = "HIGH"
SEV_WARN = "WARN"
SEV_INFO = "INFO"

# Default upper bound (bytes) on a tail-position fallthrough site's own size
# for it to count as a "suspiciously small / truncated-too-early" phantom-split
# candidate in the --cpp audit. The real func_002550C8 fragment was
# 0x2550E8 - 0x2550C8 = 0x20 bytes (8 instructions); 0x80 (32 instructions)
# leaves headroom while still excluding ordinary function-sized fallthrough
# splits. Tunable via --fallthrough-max-size.
DEFAULT_FALLTHROUGH_MAX_SIZE = 0x80

_SEV_ORDER = {SEV_CRITICAL: 0, SEV_HIGH: 1, SEV_WARN: 2, SEV_INFO: 3}


@dataclass
class Finding:
    severity: str
    kind: str
    message: str
    start: int | None = None
    end: int | None = None
    target: int | None = None

    def to_dict(self) -> dict:
        d = {"severity": self.severity, "kind": self.kind, "message": self.message}
        if self.start is not None:
            d["start"] = f"0x{self.start:08X}"
        if self.end is not None:
            d["end"] = f"0x{self.end:08X}"
        if self.target is not None:
            d["target"] = f"0x{self.target:08X}"
        return d


@dataclass
class FuncRange:
    start: int
    end: int
    index: int  # position in the source functions.json, for stable reporting


# ---------------------------------------------------------------------------
# functions.json structural audit
# ---------------------------------------------------------------------------

def load_functions_json(path: str) -> list[FuncRange]:
    with open(path, encoding="utf-8") as f:
        raw = json.load(f)
    if not isinstance(raw, list):
        raise ValueError(f"{path}: expected a JSON list of {{start,end}} objects")
    out: list[FuncRange] = []
    for i, e in enumerate(raw):
        try:
            s = int(str(e["start"]), 0)
            en = int(str(e["end"]), 0)
        except (KeyError, ValueError, TypeError) as exc:
            raise ValueError(f"{path}: entry {i} malformed: {e!r} ({exc})") from exc
        out.append(FuncRange(s, en, i))
    return out


def build_lookup(funcs: list[FuncRange]):
    """Same shape as ppu_lifter.py's `func_by_addr` / `sorted_addrs` /
    `addr_index` (tools/ppu_lifter.py:3168-3169, ~3115 params)."""
    func_by_addr = {f.start: f for f in funcs}
    sorted_addrs = sorted(func_by_addr)
    addr_index = {a: i for i, a in enumerate(sorted_addrs)}
    return func_by_addr, sorted_addrs, addr_index


def resolve_fallthrough_target(func: FuncRange, func_by_addr, sorted_addrs, addr_index):
    """Mirrors tools/ppu_lifter.py:3135-3144 EXACTLY: given a function's own
    recorded `end`, return (target_start_or_None, "exact"|"fallback"|"none").
    "none" means ppu_lifter would emit no trampoline line at all (this
    function is last in address order and its `end` isn't itself a
    registered start) -- not itself a bug, just nothing to resolve."""
    if func.end in func_by_addr:
        return func.end, "exact"
    idx = addr_index.get(func.start)
    if idx is not None and idx + 1 < len(sorted_addrs):
        return sorted_addrs[idx + 1], "fallback"
    return None, "none"


def audit_json_functions(funcs: list[FuncRange], *, heuristics: bool = False,
                          tiny_threshold: int = 24) -> list[Finding]:
    findings: list[Finding] = []

    # Duplicate starts (would make func_by_addr silently drop one entry).
    seen: dict[int, int] = {}
    for f in funcs:
        if f.start in seen:
            findings.append(Finding(
                SEV_WARN, "duplicate-start",
                f"start 0x{f.start:08X} appears more than once "
                f"(entries {seen[f.start]} and {f.index}) -- one silently "
                f"shadows the other in func_by_addr",
                start=f.start))
        else:
            seen[f.start] = f.index

    ordered = sorted(funcs, key=lambda ff: ff.start)
    if [f.start for f in ordered] != [f.start for f in funcs]:
        findings.append(Finding(
            SEV_INFO, "not-sorted",
            "functions.json is not sorted ascending by start; the audit "
            "sorts internally (matching ppu_lifter's own sorted_addrs), but "
            "an unsorted source file is itself worth a second look"))

    # Malformed ranges.
    for f in funcs:
        if f.end <= f.start:
            findings.append(Finding(
                SEV_CRITICAL, "malformed-range",
                f"func_{f.start:08X}: end 0x{f.end:08X} <= start "
                f"0x{f.start:08X} (zero/negative size)",
                start=f.start, end=f.end))

    func_by_addr, sorted_addrs, addr_index = build_lookup(funcs)
    for f in funcs:
        if f.end <= f.start:
            continue  # already reported as malformed above
        target, kind = resolve_fallthrough_target(f, func_by_addr, sorted_addrs, addr_index)
        if target is None:
            continue
        # A genuine auto-appended fallthrough always targets `end`
        # (func.fallthrough_to = end, ppu_lifter.py:650), resolving to the
        # recorded end itself ("exact") or the next start on a gap
        # ("fallback"). BOTH are always >= end > start for well-formed input,
        # so functions.json alone can never exhibit a "backward" resolved
        # target -- the earlier CRITICAL "backward-fallthrough" check here was
        # structurally dead code and has been removed. The only fallthrough
        # signal derivable from functions.json alone is the gap below; the
        # concrete phantom-split instance (func_002550C8 -> func_002550E8) is
        # only detectable with the generated .cpp (audit_cpp_trampolines).
        if kind == "fallback":
            findings.append(Finding(
                SEV_INFO, "end-not-a-boundary",
                f"func_{f.start:08X}: recorded end 0x{f.end:08X} is not "
                f"itself a registered function start; IF this function "
                f"falls through, ppu_lifter's fallback would trampoline to "
                f"0x{target:08X} instead of the committed end -- usually "
                f"harmless (most functions never fall through), but worth "
                f"a second look if this function is later confirmed to "
                f"lack a terminator",
                start=f.start, end=f.end, target=target))

    if heuristics:
        findings.extend(_heuristic_tiny_chains(ordered, tiny_threshold))

    return findings


def _heuristic_tiny_chains(ordered: list[FuncRange], tiny_threshold: int) -> list[Finding]:
    """SPECULATIVE, high-false-positive-rate proxy for the phantom
    mid-function split shape (a chain of contiguous, suspiciously tiny
    functions -- the pattern find_prologue_functions's uncorroborated
    `mflr r0` safety-stop can manufacture). Needs the real ELF/disassembly
    to confirm any single chain; this only narrows where to look."""
    findings: list[Finding] = []
    i = 0
    n = len(ordered)
    while i < n:
        if ordered[i].end - ordered[i].start <= tiny_threshold:
            j = i
            while (j + 1 < n
                   and ordered[j + 1].end - ordered[j + 1].start <= tiny_threshold
                   and ordered[j + 1].start == ordered[j].end):
                j += 1
            if j > i:
                findings.append(Finding(
                    SEV_INFO, "tiny-chain-heuristic",
                    f"0x{ordered[i].start:08X}..0x{ordered[j].end:08X}: "
                    f"{j - i + 1} contiguous functions each <= "
                    f"{tiny_threshold} bytes -- SPECULATIVE candidate for a "
                    f"phantom mid-function split chain; NOT a proven bug, "
                    f"needs the real ELF/disassembly to confirm",
                    start=ordered[i].start, end=ordered[j].end))
                i = j + 1
                continue
        i += 1
    return findings


# ---------------------------------------------------------------------------
# Generated .cpp trampoline audit
# ---------------------------------------------------------------------------

# void func_XXXXXXXX(ppu_context* ctx) {           (tools/ppu_lifter.py:3122)
_FUNC_DEF_RE = re.compile(
    r'^\s*(?:extern\s+"C"\s+)?void\s+(?:\w*_)?func_([0-9A-Fa-f]{8})\s*'
    r'\(\s*ppu_context\s*\*\s*ctx\s*\)\s*\{\s*$')

# { g_trampoline_fn = (void(*)(void*))func_XXXXXXXX; return; }
# Anchored on a bare `{` (only whitespace before it) so the `if (cond) { ... }`
# conditional form (tools/ppu_lifter.py:1441-1442,1454) never matches here --
# that form always has `if (` before the brace.
_TRAMP_UNCOND_RE = re.compile(
    r'^\s*\{\s*g_trampoline_fn\s*=\s*\(void\(\*\)\(void\*\)\)\s*(?:\w*_)?'
    r'func_([0-9A-Fa-f]{8})\s*;\s*return;\s*\}\s*$')

# A lone `}` on its own line: the function's own closing brace
# (tools/ppu_lifter.py:3146 -- `lines.append("}")`, nothing else on the line).
_CLOSE_BRACE_RE = re.compile(r'^\s*\}\s*$')


def scan_cpp_trampolines(path: str) -> list[tuple[int, int, int]]:
    """Returns a list of (site_start, target_start, line_no) for every
    UNCONDITIONAL g_trampoline_fn assignment that is the LAST content line
    of its enclosing function (the only position a genuine fallthrough
    continuation can occupy)."""
    text = Path(path).read_text(encoding="utf-8", errors="replace")
    lines = text.splitlines()

    candidates: list[tuple[int, int, int]] = []
    in_func = False
    func_start: int | None = None
    last_content: str | None = None
    for lineno, raw in enumerate(lines, 1):
        if not in_func:
            m = _FUNC_DEF_RE.match(raw)
            if m:
                in_func = True
                func_start = int(m.group(1), 16)
                last_content = None
            continue

        if _CLOSE_BRACE_RE.match(raw):
            if last_content is not None:
                um = _TRAMP_UNCOND_RE.match(last_content)
                if um:
                    tgt = int(um.group(1), 16)
                    candidates.append((func_start, tgt, lineno))
            in_func = False
            func_start = None
            last_content = None
            continue

        if raw.strip():
            last_content = raw

    return candidates


def audit_cpp_trampolines(
        path: str, funcs: list[FuncRange] | None = None, *,
        fallthrough_max_size: int = DEFAULT_FALLTHROUGH_MAX_SIZE) -> list[Finding]:
    """See the module docstring's "Generated .cpp trampoline audit" section.

    A tail-position unconditional `g_trampoline_fn = func_T; return;` is
    byte-identical whether the lifter auto-appended it as a fallthrough
    continuation or it is an explicit `b`/`bc` tail-call, so the two cannot
    be told apart textually. They ARE told apart structurally: an
    auto-appended fallthrough can only ever target what
    `resolve_fallthrough_target` computes from functions.json
    (func.fallthrough_to = end -> the recorded end, or the next start on a
    gap). So, with --functions:

      - target == the resolved fallthrough, that resolution is the recorded
        `end` ("exact", i.e. an adjacent registered DIFFERENT function), and
        the site fragment is small (<= fallthrough_max_size): the consistent
        phantom-split shape (func_002550C8 -> func_002550E8). Flagged HIGH
        as a heuristic candidate (needs the real disassembly to confirm; the
        size gate is a proxy for "truncated too early", not a proof).
      - target == the resolved fallthrough but the site is large, or the
        resolution is a "fallback" forward continuation across a gap: a
        normal split / forward continuation -- NOT flagged.
      - target != the resolved fallthrough: NOT an auto-appended fallthrough
        -- it is an explicit `b`/`bc` tail-call (legitimate in any
        direction, backward included) -- NOT flagged. This is what stops the
        tool from flagging every legitimate backward tail-call.

    Without --functions there is no resolution to compare against, so the
    tool falls back to the cruder "target < site's own start" LOW-confidence
    WARN candidate.
    """
    findings: list[Finding] = []
    # NOTE: `funcs is not None` (not truthiness) -- an explicitly-passed empty
    # list is still "functions.json was given", just with nothing in it; it
    # must not silently fall back to the no-cross-reference LOW-confidence path.
    if funcs is not None:
        func_by_addr, sorted_addrs, addr_index = build_lookup(funcs)
    else:
        func_by_addr = sorted_addrs = addr_index = None

    for site, target, lineno in scan_cpp_trampolines(path):
        site_func = func_by_addr.get(site) if func_by_addr is not None else None
        if site_func is not None:
            expected, kind = resolve_fallthrough_target(
                site_func, func_by_addr, sorted_addrs, addr_index)
            if target != expected:
                # Not the fallthrough resolution -> an explicit `b`/`bc`
                # tail-call (real PPC branch), legitimate in any direction.
                # Never the auto-appended fallthrough bug class. Not flagged.
                continue
            if kind != "exact":
                # "fallback": a forward continuation across a gap to the next
                # function in address order (target >= end). Normal. Not
                # flagged.
                continue
            # kind == "exact": the trampoline lands on the site's own recorded
            # end, which is itself a registered adjacent function -- the
            # auto-appended fallthrough-to-neighbour shape.
            size = site_func.end - site_func.start
            if size <= fallthrough_max_size:
                findings.append(Finding(
                    SEV_HIGH, "cpp-consistent-truncated-fallthrough",
                    f"{path}:{lineno}: func_{site:08X} (only "
                    f"0x{size:X} bytes) falls through to its immediately "
                    f"adjacent registered neighbour func_{target:08X} == its "
                    f"own recorded end -- the consistent phantom mid-function "
                    f"split shape (func_002550C8 -> func_002550E8, whose true "
                    f"continuation lay past the neighbour at the later "
                    f"func_00255178). HEURISTIC candidate: a genuine small "
                    f"function that really falls through into its neighbour, "
                    f"or a legitimate multi-piece split at the right "
                    f"boundary, would also match -- confirm the true extent "
                    f"against the real disassembly before treating it as a "
                    f"bug.",
                    start=site, end=site_func.end, target=target))
            # else: a large function falling through to its neighbour -- an
            # ordinary function-split chain, not a truncated fragment. Not
            # flagged.
        elif target < site:
            findings.append(Finding(
                SEV_WARN, "cpp-backward-fallthrough-candidate",
                f"{path}:{lineno}: func_{site:08X}'s tail-position "
                f"unconditional trampoline targets func_{target:08X} "
                f"(LOWER than its own start) -- CANDIDATE (LOW confidence: "
                f"no functions.json entry for this site, so the fallthrough "
                f"resolution cannot be computed; pass --functions alongside "
                f"--cpp for the principled check. A legitimate backward "
                f"`b`/`bc` tail-call as the function's true last instruction "
                f"would also match this position-only heuristic).",
                start=site, target=target))

    return findings


# ---------------------------------------------------------------------------
# Reporting
# ---------------------------------------------------------------------------

def _print_report(findings: list[Finding], *, as_json: bool, limit: int) -> None:
    if as_json:
        print(json.dumps([f.to_dict() for f in findings], indent=2))
        return

    if not findings:
        print("ok: no anomalies found")
        return

    by_kind: dict[str, list[Finding]] = {}
    for f in findings:
        by_kind.setdefault(f.kind, []).append(f)

    ordered_kinds = sorted(by_kind, key=lambda k: _SEV_ORDER.get(by_kind[k][0].severity, 9))
    for kind in ordered_kinds:
        group = by_kind[kind]
        sev = group[0].severity
        print(f"\n[{sev}] {kind} ({len(group)})")
        shown = group if limit <= 0 else group[:limit]
        for f in shown:
            print(f"  - {f.message}")
        if limit > 0 and len(group) > limit:
            print(f"  ... {len(group) - limit} more (pass --limit 0 to show all)")

    print("\nsummary:")
    for sev in (SEV_CRITICAL, SEV_HIGH, SEV_WARN, SEV_INFO):
        n = sum(1 for f in findings if f.severity == sev)
        if n:
            print(f"  {sev}: {n}")


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("functions_json", metavar="FUNCTIONS_JSON",
                    help="functions.json-style file: a JSON list of "
                         "{\"start\": \"0x...\", \"end\": \"0x...\"} entries "
                         "(e.g. games/gow2/functions.json).")
    p.add_argument("--cpp", metavar="FILE", default=None,
                    help="optional path to a generated ppu_recomp_XXX.cpp "
                         "to additionally grep for literal unconditional "
                         "g_trampoline_fn assignments (see module docstring).")
    p.add_argument("--heuristics", action="store_true",
                    help="also run the speculative tiny-function-chain "
                         "heuristic (OFF by default -- high false-positive "
                         "rate, see module docstring).")
    p.add_argument("--tiny-threshold", type=int, default=24, metavar="BYTES",
                    help="max size (bytes) for a function to count as "
                         "'tiny' in --heuristics (default: 24).")
    p.add_argument("--fallthrough-max-size", type=lambda s: int(s, 0),
                    default=DEFAULT_FALLTHROUGH_MAX_SIZE, metavar="BYTES",
                    help="max size (bytes) of a --cpp tail-position "
                         "fallthrough site for it to count as a "
                         "'suspiciously small / truncated' phantom-split "
                         "candidate (default: 0x%X). Accepts 0x hex."
                         % DEFAULT_FALLTHROUGH_MAX_SIZE)
    p.add_argument("--limit", type=int, default=15, metavar="N",
                    help="max example findings printed per category "
                         "(default: 15; 0 = show all). Ignored with --json.")
    p.add_argument("--json", action="store_true",
                    help="emit findings as a JSON array instead of the "
                         "human-readable report.")
    args = p.parse_args(argv)

    try:
        funcs = load_functions_json(args.functions_json)
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    findings = audit_json_functions(
        funcs, heuristics=args.heuristics, tiny_threshold=args.tiny_threshold)

    if args.cpp:
        try:
            findings.extend(audit_cpp_trampolines(
                args.cpp, funcs,
                fallthrough_max_size=args.fallthrough_max_size))
        except OSError as exc:
            print(f"error: {exc}", file=sys.stderr)
            return 1

    _print_report(findings, as_json=args.json, limit=args.limit)

    if any(f.severity in (SEV_CRITICAL, SEV_HIGH) for f in findings):
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
