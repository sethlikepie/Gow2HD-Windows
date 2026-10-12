#!/usr/bin/env python3
"""Self-check for config-declared switch tables (Task B2 -- the network for
when `discover_jump_tables`'s static analysis doesn't reach a switch).

Plano: docs/superpowers/plans/2026-07-23-xenonrecomp-lessons-recomp-tooling.md:349-411
(licao do XenonRecomp: "quando o analisador estatico nao encontra uma switch
table, declara-se no config"). `tools/recomp_config.py`'s `load_switch_tables`
(its own test coverage lives in `tools/test_recomp_config.py`) parses the
`[[switch_table]]` file `[main].switch_table_file` points at into
`SwitchTable(dispatch, cases, default)`; THIS suite covers the CONSUMER that
was missing: `tools/ppu_lifter.py`'s `merge_switch_tables()` (config wins over
`discover_jump_tables` on a `dispatch` conflict; out-of-range case targets and
un-enclosed dispatchers are WARNED, never swallowed silently) and its wiring
into `main()` -- both the serial (`--jobs 1`) and parallel (`--jobs N` / real
Pool) lift paths, so a declared table doesn't only work in one of them (this
exact divergence already happened once in this plan, per the B2 brief).

Three brief-mandated design decisions this suite locks in (see
`tools/recomp_config.py`'s `SwitchTable` docstring for the full rationale):
  1. `index` in a `{index, target}` case is accepted but has NO effect --
     ppu_lifter.py's bctr emission keys arms by target ADDRESS, never index.
  2. A declared case target outside the dispatcher's enclosing function is
     WARNED (via stderr), not silently dropped without a trace -- emission
     itself still drops it (unchanged codegen), but now loudly.
  3. `default` must be the literal string "indirect" or `recomp_config.py`
     rejects it at parse time (covered there, not repeated here); B2's merge
     layer never sees any other value.

Review round 1 findings closed here:
  Important #1: `merge_switch_tables` must DEDUPLICATE case targets like
  `discover_jump_tables` already does (`sorted(set(best))`, ppu_lifter.py
  ~:3468) -- without it, a declared table where two indices share one target
  (`case 0: case 1: ...`, the ordinary shape of a real switch with
  fallthrough cases, not a synthetic edge case) emits the same
  `case 0x...u:` label twice and clang rejects the generated C with
  "duplicate case value", 5-8 minutes after the lift finished, not at lift
  time. See test_merge_dedupes_duplicate_case_targets_like_discovery_does
  and the CLI-level duplicate-case assertions in the wiring tests below.
  Minor: two [[switch_table]] entries sharing one `dispatch` are last-wins
  (mirrors [[invalid_instructions]]'s duplicate-`data` rule) and now WARNED
  -- see test_duplicate_declared_dispatch_last_wins_and_is_warned, which also
  pins down that `n_overridden` counts genuine discovery overrides only, not
  a declared-vs-declared duplicate.
  Minor: `--config` with no `switch_table_file` set does NOT leave stdout
  byte-identical to "no switch-table support at all" -- main() always prints
  one unconditional summary line regardless of --config. See the corrected
  docstring on test_cli_wiring_no_switch_table_file_is_unaffected.
  (The `or True` dead check that used to sit in this file was removed --
  the round-1 review flagged it as a check that could never fail while
  printing `ok`; see test_out_of_range_case_target_is_warned_not_silently_dropped.)

Sem pytest (convencao tools/test_*.py: so' stdlib, asserts simples + main()).
Corre com o .venv deste repo (uniões PEP 604 precisam de Python 3.10+):

Run: .venv/bin/python3 tools/test_switch_tables.py
"""
import json
import os
import struct
import subprocess
import sys
import tempfile
import textwrap
from pathlib import Path

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, TOOLS_DIR)

import ppu_lifter as PL
from ppu_lifter import LiftedFunction, PPULifter, merge_switch_tables
from recomp_config import SwitchTable

FAILS: list[str] = []


def check(label: str, cond: bool, detail: str = "") -> None:
    if cond:
        print(f"[switch-tables] ok   {label}")
    else:
        print(f"[switch-tables] FAIL {label}  {detail}")
        FAILS.append(label)


def w(*words: int) -> bytes:
    """Pack big-endian 32-bit words (PPU is always BE here)."""
    return b"".join(struct.pack(">I", x & 0xFFFFFFFF) for x in words)


# mtctr r0 (encoded literally -- see tools/test_ppu_jumptables.py's w_mtctr for
# the derivation of the swapped SPR halves for CTR=SPR 9). Parenthesise the
# whole spr value before shifting by 11 -- `<<` binds tighter than `|` in
# Python, so `a | b << 11` is `a | (b << 11)`, NOT `(a | b) << 11`.
_SPR_CTR = ((9 & 0x1F) << 5) | ((9 >> 5) & 0x1F)
MTCTR_R0 = (31 << 26) | (0 << 21) | (_SPR_CTR << 11) | (467 << 1)
BCTR = (19 << 26) | (20 << 21) | (528 << 1)
LI_R3_1 = (14 << 26) | (3 << 21) | (0 << 16) | 1     # li r3, 1
LI_R3_2 = (14 << 26) | (3 << 21) | (0 << 16) | 2     # li r3, 2
BLR = 0x4E800020


# ---------------------------------------------------------------------------
# merge_switch_tables() -- pure-function unit tests
# ---------------------------------------------------------------------------

def test_no_declared_tables_is_pure_passthrough() -> None:
    """Empty `declared` list: merged must be a copy equal to `discovered`,
    n_config=0, n_overridden=0 -- byte-for-byte the pre-B2 behaviour when
    --config has no switch_table_file (or none of it applies)."""
    discovered = {0x1000: [0x1010, 0x1020]}
    merged, n_config, n_overridden = merge_switch_tables(
        discovered, [], [(0x1000, 0x2000)])
    check("passthrough: merged == discovered", merged == discovered, merged)
    check("passthrough: not the SAME dict object (no aliasing surprise)",
          merged is not discovered)
    check("passthrough: n_config == 0", n_config == 0, n_config)
    check("passthrough: n_overridden == 0", n_overridden == 0, n_overridden)


def test_declared_table_for_new_dispatch_is_added() -> None:
    """A declared dispatch NOT already in `discovered` is simply added."""
    discovered = {0x1000: [0x1010]}
    declared = [SwitchTable(dispatch=0x2000, cases=[0x2010, 0x2020])]
    merged, n_config, n_overridden = merge_switch_tables(
        discovered, declared, [(0x1000, 0x1100), (0x2000, 0x2100)])
    check("new dispatch present with declared cases",
          merged.get(0x2000) == [0x2010, 0x2020], merged)
    check("pre-existing discovery entry untouched",
          merged.get(0x1000) == [0x1010], merged)
    check("n_config == 1 (one declared table)", n_config == 1, n_config)
    check("n_overridden == 0 (no conflicting dispatch)", n_overridden == 0, n_overridden)


def test_config_wins_over_discovery_for_same_dispatch() -> None:
    """THE brief's central requirement: same `dispatch` in both discovery and
    config -- config's cases REPLACE discovery's, they are not unioned."""
    discovered = {0x1000: [0x1010, 0x1020, 0x1030]}  # discovery found 3 cases
    declared = [SwitchTable(dispatch=0x1000, cases=[0x1099])]  # config: just 1
    merged, n_config, n_overridden = merge_switch_tables(
        discovered, declared, [(0x1000, 0x1100)])
    check("config's cases fully REPLACE discovery's (not unioned)",
          merged[0x1000] == [0x1099], merged[0x1000])
    check("n_overridden == 1 (the conflict was recorded)", n_overridden == 1, n_overridden)


def test_merge_dedupes_duplicate_case_targets_like_discovery_does() -> None:
    """Review round 1, Important #1: discover_jump_tables already deduplicates
    its own output (`tables[addr] = sorted(set(best))`, ppu_lifter.py
    ~:3468). A declared table with two indices sharing one target
    (`{index=0,target=0x1010}, {index=1,target=0x1010}, {index=2,
    target=0x1020}` -- the plan's own proposed shape, and the ORDINARY form
    of a real switch's fallthrough cases, not a synthetic edge case) must
    collapse to ONE entry per target here too, or ppu_lifter.py emits the
    same `case 0x...u:` label twice and clang rejects the generated C with
    'duplicate case value' -- a failure that only surfaces at compile time,
    5-8 minutes after the lift finished."""
    declared = [SwitchTable(dispatch=0x1000, cases=[0x1010, 0x1020, 0x1010])]
    merged, _, _ = merge_switch_tables({}, declared, [(0x1000, 0x1100)])
    check("duplicate declared case target collapses to ONE entry, matching "
          "discover_jump_tables' own sorted(set(...)) normalization",
          merged[0x1000] == [0x1010, 0x1020], merged[0x1000])


def test_duplicate_declared_dispatch_last_wins_and_is_warned() -> None:
    """Review round 1, Minor: two [[switch_table]] entries declaring the SAME
    `dispatch` are last-wins in file order (same rule as a duplicate
    [[invalid_instructions]] `data` entry) and must be WARNED, not silent.
    Also pins down that `n_overridden` counts genuine discovery overrides
    ONLY -- a declared-vs-declared duplicate must not inflate it, since no
    discovery entry existed here at all."""
    warnings: list[str] = []
    declared = [
        SwitchTable(dispatch=0x1000, cases=[0x1010]),
        SwitchTable(dispatch=0x1000, cases=[0x1020]),
    ]
    merged, n_config, n_overridden = merge_switch_tables(
        {}, declared, [(0x1000, 0x1100)], warn=warnings.append)
    check("last declared entry wins for a duplicate dispatch",
          merged[0x1000] == [0x1020], merged[0x1000])
    check("a duplicate-dispatch warning fired",
          any("more than once" in w for w in warnings), warnings)
    check("n_overridden counts discovery overrides only -- no discovery "
          "entry existed here, so the declared-vs-declared duplicate must "
          "NOT be counted as one",
          n_overridden == 0, n_overridden)
    check("n_config still counts both declared entries (as written)",
          n_config == 2, n_config)


def test_out_of_range_case_target_is_warned_not_silently_dropped() -> None:
    """Armadilha 2: a declared case target outside the dispatcher's enclosing
    function must be reported via `warn`, not swallowed. The merge itself
    does NOT filter it out (that's emission's job, unchanged) -- it stays in
    the returned dict verbatim so a caller inspecting `merged` sees exactly
    what was declared; only an explicit warning marks the coming loss."""
    warnings: list[str] = []
    discovered: dict[int, list[int]] = {}
    declared = [SwitchTable(dispatch=0x1000, cases=[0x1010, 0x9999])]
    merged, _, _ = merge_switch_tables(
        discovered, declared, [(0x1000, 0x1100)], warn=warnings.append)
    check("merge does not filter the oob case out of the returned dict "
          "(filtering is emission's job, unchanged)",
          merged[0x1000] == [0x1010, 0x9999], merged[0x1000])
    check("exactly one warning fired", len(warnings) == 1, warnings)
    if warnings:
        check("warning names the dispatch", "0x00001000" in warnings[0], warnings[0])
        check("warning names the out-of-range target", "0x00009999" in warnings[0], warnings[0])
        # Review round 1: the previous version of this check was `... or True`
        # (always green, proved nothing). Real assertion: the message's
        # trailing enumerated oob list (everything after the last "): ") must
        # be EXACTLY "0x00009999" -- a single address, not a comma-joined
        # list that also (wrongly) includes the in-range 0x00001010.
        enumerated_tail = warnings[0].rsplit("): ", 1)[-1]
        check("warning's enumerated oob list contains ONLY the out-of-range "
              "target -- the in-range 0x00001010 is not wrongly included",
              enumerated_tail == "0x00009999", repr(enumerated_tail))


def test_dispatch_outside_any_known_function_is_warned() -> None:
    """A declared `dispatch` that isn't inside ANY known function bound at
    all (typo'd address, wrong title's config, etc.) must also warn, not
    silently produce an unreachable, unverifiable entry."""
    warnings: list[str] = []
    declared = [SwitchTable(dispatch=0xDEAD0000, cases=[0x1010])]
    merged, _, _ = merge_switch_tables(
        {}, declared, [(0x1000, 0x1100)], warn=warnings.append)
    check("entry still present in merged (no crash, no silent drop)",
          merged.get(0xDEAD0000) == [0x1010], merged)
    check("exactly one warning fired for the un-enclosed dispatch",
          len(warnings) == 1, warnings)
    if warnings:
        check("warning names the dispatch", "0xDEAD0000" in warnings[0], warnings[0])


def test_default_warn_prints_to_stderr(capsys=None) -> None:
    """When `warn` is not supplied, the default must go to stderr (not stdout,
    not swallowed) -- checked via subprocess since this suite has no pytest
    capsys fixture."""
    proc = subprocess.run(
        [sys.executable, "-c",
         "import sys; sys.path.insert(0, %r); from ppu_lifter import merge_switch_tables; "
         "from recomp_config import SwitchTable; "
         "merge_switch_tables({}, [SwitchTable(dispatch=1, cases=[2])], [], warn=None)"
         % TOOLS_DIR],
        capture_output=True, text=True, timeout=30)
    check("default warn() writes to stderr", "Warning:" in proc.stderr, proc.stderr)
    check("default warn() does not pollute stdout", "Warning:" not in proc.stdout, proc.stdout)


# ---------------------------------------------------------------------------
# Direct worker-plumbing tests (mirrors test_invalid_instructions.py's guard
# 1/3: exercise _worker_init/_worker_lift directly, bypassing main()/CLI)
# ---------------------------------------------------------------------------

def test_worker_init_and_worker_lift_thread_jump_tables() -> None:
    """The parallel path's own consumer (_worker_lift, fed via
    _worker_init/_WORKER_STATE) must see a MERGED jump_tables dict exactly
    like the serial path does. Deleting jump_tables from _worker_init's
    storage, or from _worker_lift's `lifter.jump_tables =` assignment,
    breaks this. (This parameter existed before Task B2 for
    discover_jump_tables' output; this test is new -- nothing previously
    exercised it end to end.)"""
    seg_addr = 0x3000
    dispatch = seg_addr + 4          # bctr is the SECOND word
    data = w(MTCTR_R0, BCTR, LI_R3_1, LI_R3_2, BLR)
    merged = {dispatch: [seg_addr + 8, seg_addr + 0x10]}  # both in-range
    PL._worker_init(segs=[(seg_addr, data)], big_endian=True, name_map={},
                     prefix="", jump_tables=merged)
    check("_worker_init stores jump_tables in _WORKER_STATE",
          PL._WORKER_STATE.get("jump_tables") == merged,
          PL._WORKER_STATE.get("jump_tables"))

    idx0, results, _ct, _bt = PL._worker_lift((0, [(seg_addr, seg_addr + 0x14)]))
    check("_worker_lift produced exactly one function", len(results) == 1, len(results))
    if results:
        _name, _s, _e, body, _calls, _ft = results[0]
        joined = "\n".join(body)
        check(f"worker path emits a switch arm for case 0x{seg_addr + 8:08X}",
              f"case 0x{seg_addr + 8:08X}u: goto loc_{seg_addr + 8:08X};" in joined,
              joined)
        check(f"worker path emits a switch arm for case 0x{seg_addr + 0x10:08X}",
              f"case 0x{seg_addr + 0x10:08X}u: goto loc_{seg_addr + 0x10:08X};" in joined,
              joined)


def test_parallel_lift_real_pool_threads_jump_tables() -> None:
    """The reviewer's strongest ask in the B3 precedent: run the REAL
    _parallel_lift with a 1-process Pool. This executes the actual initargs
    tuple _parallel_lift builds from `lifter.jump_tables` -- deleting
    jump_tables from that tuple, or desyncing its position from
    _worker_init's signature, breaks this test (unlike calling _worker_init
    directly above, which bypasses _parallel_lift's own source)."""
    seg_addr = 0x4000
    dispatch = seg_addr + 4
    data = w(MTCTR_R0, BCTR, LI_R3_1, LI_R3_2, BLR)
    lifter = PPULifter()
    lifter.jump_tables = {dispatch: [seg_addr + 8, seg_addr + 0x10]}
    PL._parallel_lift(lifter, [(seg_addr, seg_addr + 0x14)], [(seg_addr, data)], True, 1)
    check("parallel lift (real Pool, jobs=1) produced exactly one function",
          len(lifter.functions) == 1, len(lifter.functions))
    if lifter.functions:
        joined = "\n".join(lifter.functions[0].body_lines)
        check("parallel path (real Pool) emits the switch statement",
              "switch ((uint32_t)ctx->ctr)" in joined, joined)
        check(f"parallel path (real Pool) emits case 0x{seg_addr + 8:08X}",
              f"case 0x{seg_addr + 8:08X}u" in joined, joined)


# ---------------------------------------------------------------------------
# Full CLI wiring (subprocess, --raw so discover_jump_tables never runs --
# any switch arm in the output MUST have come through the config merge)
# ---------------------------------------------------------------------------

_DUMMY_MAIN_TOML_TMPL = """
[main]
elf_path = "unused.elf"
functions_json = "unused.json"
out_directory = "unused_out"
switch_table_file = "{switch_file}"
"""


def _run_lifter_cli_with_switch_table(switch_toml: str, jobs: str,
                                       extra_args: tuple[str, ...] = ()
                                       ) -> tuple["subprocess.CompletedProcess[str]", str | None]:
    """Write a raw binary with one mtctr/bctr dispatcher + two candidate case
    targets + a config (main TOML + separate switch-table TOML) to a fresh
    tmpdir, and run the REAL ppu_lifter.py CLI with --raw (so
    discover_jump_tables' ELF/TOC-dependent discovery never runs -- any
    switch arm reaching the generated C can only have come from the config
    merge, not from discovery finding the same thing independently)."""
    with tempfile.TemporaryDirectory() as d:
        tmp = Path(d)
        base = 0x5000
        data = w(MTCTR_R0, BCTR, LI_R3_1, LI_R3_2, BLR)
        input_path = tmp / "input.bin"
        input_path.write_bytes(data)

        functions_path = tmp / "functions.json"
        functions_path.write_text(json.dumps([{"start": base, "end": base + 0x14}]))

        switch_path = tmp / "switch.toml"
        switch_path.write_text(switch_toml, encoding="utf-8")

        config_path = tmp / "cfg.toml"
        config_path.write_text(
            _DUMMY_MAIN_TOML_TMPL.format(switch_file=switch_path.name), encoding="utf-8")

        out_dir = tmp / "out"
        proc = subprocess.run(
            [sys.executable, os.path.join(TOOLS_DIR, "ppu_lifter.py"),
             str(input_path), "--raw", "--base", hex(base),
             "--config", str(config_path),
             "--functions", str(functions_path), "--single-file",
             "--output", str(out_dir), "--jobs", jobs, "--allow-jt-failure",
             *extra_args],
            capture_output=True, text=True, timeout=60)

        src_path = out_dir / "ppu_recomp.c"
        src = src_path.read_text() if src_path.is_file() else None
        return proc, src


# index=3's target (0x5008) DUPLICATES index=0's -- the ordinary shape of a
# real switch's fallthrough cases ("case 0: case 1: ..."), not a synthetic
# edge case (review round 1, Important #1). Without dedup in
# merge_switch_tables, this emits `case 0x00005008u:` TWICE and clang
# rejects the generated C with "duplicate case value".
_SWITCH_TOML = textwrap.dedent("""
    [[switch_table]]
    dispatch = 0x5004
    default  = "indirect"
    cases = [
      { index = 0, target = 0x5008 },
      { index = 1, target = 0x5010 },
      { index = 2, target = 0x9999999 },
      { index = 3, target = 0x5008 },
    ]
""")


def test_cli_wiring_serial_jobs_1() -> None:
    """--jobs 1 (serial path): a declared switch table reaches the generated
    C as a real `switch(ctx->ctr)` with the declared in-range case arms --
    the config's out-of-range case (0x9999999, well past the function's end
    0x5014) is both ABSENT from the emitted arms (unchanged codegen) and
    WARNED about on stderr (armadilha 2); and the duplicated target
    (0x5008, declared twice under different indices) emits ONLY ONCE
    (review round 1, Important #1 -- a second emission would be a clang
    'duplicate case value' waiting to happen at compile time)."""
    proc, src = _run_lifter_cli_with_switch_table(_SWITCH_TOML, jobs="1")
    check("CLI exits 0", proc.returncode == 0,
          f"rc={proc.returncode}\nSTDOUT:\n{proc.stdout}\nSTDERR:\n{proc.stderr}")
    check("CLI reports 1 switch_table declared", "1 switch_table" in proc.stdout, proc.stdout)
    check("CLI reports the oob target via a warning (not silence)",
          "0x09999999" in proc.stderr or "0x9999999".upper() in proc.stderr.upper(),
          proc.stderr)
    check("generated source exists", src is not None)
    if src is not None:
        check("emits the switch statement", "switch ((uint32_t)ctx->ctr)" in src, src)
        check("emits case 0x00005008 EXACTLY ONCE despite being declared "
              "twice (index=0 and index=3)",
              src.count("case 0x00005008u: goto loc_00005008;") == 1, src)
        check("emits case 0x00005010", "case 0x00005010u: goto loc_00005010;" in src, src)
        check("does NOT emit the out-of-range case (codegen unchanged)",
              "9999999" not in src, src)


def test_cli_wiring_parallel_jobs_2() -> None:
    """Same fixture, --jobs 2: the parallel path must reach IDENTICAL switch
    arms, including the deduplicated case. This is the brief's explicit
    divergence concern -- if the merge were only assigned to
    lifter.jump_tables AFTER _parallel_lift's call (or not threaded to it at
    all), -j1 and -j2 would disagree silently."""
    proc, src = _run_lifter_cli_with_switch_table(_SWITCH_TOML, jobs="2")
    check("CLI exits 0 (jobs=2)", proc.returncode == 0,
          f"rc={proc.returncode}\nSTDOUT:\n{proc.stdout}\nSTDERR:\n{proc.stderr}")
    check("generated source exists (jobs=2)", src is not None)
    if src is not None:
        check("jobs=2 emits the switch statement", "switch ((uint32_t)ctx->ctr)" in src, src)
        check("jobs=2 emits case 0x00005008 EXACTLY ONCE (deduplicated)",
              src.count("case 0x00005008u: goto loc_00005008;") == 1, src)
        check("jobs=2 emits case 0x00005010", "case 0x00005010u: goto loc_00005010;" in src, src)


def test_cli_wiring_no_switch_table_file_is_unaffected() -> None:
    """--config given but [main] omits switch_table_file entirely: no
    [[switch_table]] config is loaded (declared_switch_tables stays empty)
    and the generated C falls back to plain ps3_indirect_call(ctx), same as
    the pre-B2 lift.

    Review round 1, Minor: stdout is NOT byte-identical to "no switch-table
    support existing at all" -- main() prints one unconditional summary line
    ("  switch tables: N from discovery, M from config, K overridden by
    config, T total") regardless of --config (ppu_lifter.py, right before
    lifter.jump_tables is assigned), so a --config run with no
    switch_table_file still shows that line with all-zero counts. What IS
    identical to "no switch-table support at all": no "N switch_table(s)
    declared" line (that one is gated on switch_table_file being set) and
    the generated C's dispatch codegen. This docstring previously (wrongly)
    claimed stdout was fully unaffected; the assertions below now check the
    real, corrected claim instead."""
    with tempfile.TemporaryDirectory() as d:
        tmp = Path(d)
        base = 0x6000
        data = w(MTCTR_R0, BCTR, LI_R3_1, BLR)
        input_path = tmp / "input.bin"
        input_path.write_bytes(data)
        functions_path = tmp / "functions.json"
        functions_path.write_text(json.dumps([{"start": base, "end": base + 0x10}]))
        config_path = tmp / "cfg.toml"
        config_path.write_text(textwrap.dedent("""
            [main]
            elf_path = "unused.elf"
            functions_json = "unused.json"
            out_directory = "unused_out"
        """), encoding="utf-8")
        out_dir = tmp / "out"
        proc = subprocess.run(
            [sys.executable, os.path.join(TOOLS_DIR, "ppu_lifter.py"),
             str(input_path), "--raw", "--base", hex(base),
             "--config", str(config_path),
             "--functions", str(functions_path), "--single-file",
             "--output", str(out_dir), "--jobs", "1", "--allow-jt-failure"],
            capture_output=True, text=True, timeout=60)
        check("CLI exits 0 without switch_table_file", proc.returncode == 0,
              f"rc={proc.returncode}\nSTDERR:\n{proc.stderr}")
        check("no 'N switch_table(s) declared' config line when the field "
              "is absent (that line is gated on switch_table_file)",
              "switch_table(s) declared" not in proc.stdout, proc.stdout)
        check("the unconditional 'switch tables: ...' summary line STILL "
              "prints, with all-zero counts -- stdout is NOT byte-identical "
              "to 'no switch-table support at all' (review round 1, Minor)",
              "switch tables: 0 from discovery, 0 from config, 0 overridden "
              "by config, 0 total" in proc.stdout, proc.stdout)
        src_path = out_dir / "ppu_recomp.c"
        src = src_path.read_text() if src_path.is_file() else None
        check("generated source exists", src is not None)
        if src is not None:
            check("falls back to plain indirect dispatch (no config, no discovery)",
                  "ps3_indirect_call(ctx); return;" in src, src)
            check("no switch statement emitted", "switch ((uint32_t)ctx->ctr)" not in src, src)


def main() -> int:
    test_no_declared_tables_is_pure_passthrough()
    test_declared_table_for_new_dispatch_is_added()
    test_config_wins_over_discovery_for_same_dispatch()
    test_merge_dedupes_duplicate_case_targets_like_discovery_does()
    test_duplicate_declared_dispatch_last_wins_and_is_warned()
    test_out_of_range_case_target_is_warned_not_silently_dropped()
    test_dispatch_outside_any_known_function_is_warned()
    test_default_warn_prints_to_stderr()
    test_worker_init_and_worker_lift_thread_jump_tables()
    test_parallel_lift_real_pool_threads_jump_tables()
    test_cli_wiring_serial_jobs_1()
    test_cli_wiring_parallel_jobs_2()
    test_cli_wiring_no_switch_table_file_is_unaffected()

    print()
    if FAILS:
        print(f"[switch-tables] FAILED: {', '.join(FAILS)}")
        return 1
    print("ok: config-declared switch tables (recomp_config.SwitchTable -> "
          "ppu_lifter.merge_switch_tables, config-wins merge, case-target "
          "dedup, duplicate-dispatch last-wins warning, oob/un-enclosed "
          "warnings, serial+parallel CLI wiring)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
