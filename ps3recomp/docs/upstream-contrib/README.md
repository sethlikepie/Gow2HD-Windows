# Contribution back to sp00nznet/ps3recomp — jump-table base arbitration

This directory holds a patch we would like to offer **upstream**. It is written
in English because it is addressed to the upstream maintainers.

**Evidence level: `offline-unit`.** Everything below is measured with unit
vectors — hand-assembled PowerPC words and a dictionary-backed memory reader.
No ELF, no network, no boot. Nothing here claims in-boot behaviour.

## Files

| File | What it is |
|---|---|
| `0001-ppu_lifter-jumptable-base-arbitration.patch` | `git format-patch` output, generated mechanically against `upstream/master` |
| `../../tools/upstream_jt_snapshot.py` | verbatim cut of your `discover_jump_tables`, with provenance and a sha256 re-verified from git — still pinned at `ee97d01`, see *Re-anchoring* below |
| `../../tools/test_upstream_jt_contrib.py` | our differential: the discriminating vector, a control vector, and a mutation proof |

The patch applies cleanly to:

```
repo   https://github.com/sp00nznet/ps3recomp
branch master
sha    8a26ba61  (2026-08-29, "Merge PR #106")
file   tools/ppu_lifter.py  (discover_jump_tables)
```

## The defect, re-measured on 2026-09-01

`discover_jump_tables` **selects** the table-base register instead of
**arbitrating** it. It walks the two `lwzx` operands and fixes the first one
whose nearest definition resolves to a base load, with a `break`, without ever
counting validated targets:

```python
        break                           # first definition of cand wins/loses
    if disp is not None:
        break
```

The `add`-based reordering above it does mitigate this — but only where an `add`
combines the loaded value with a candidate, i.e. on **offset tables**. On an
**absolute** table there is no `add`, the reordering has nothing to act on, and
the candidate order stays the raw `lwzx` operand order. If the index operand
also carries a TOC load, it wins, the wrong base is read, no entry validates,
`best` stays empty, and the whole dispatcher is dropped silently.

This is not a straw scenario — it is the collision your own comment in this
function documents:

> *"Both operands are often TOC-loaded, so 'first candidate with a TOC-load
> definition' can pick an unrelated index reg whose own stale `lwz idx,d(r2)`
> reads back a valid-but-wrong base"*

**Reach across your branches.** We ran the discriminating vector against the
`discover_jump_tables` of every `upstream/` ref that carries `tools/ppu_lifter.py`:
**31 of 31 drop the dispatcher**, `master` and `fix/jumptable-base-disambiguation`
included.

## The discriminating vector

Absolute table, no `add`, both `lwzx` operands TOC-loaded, the wrong one first:

```
cmplwi cr0, r9, 2
lwz    r9,  8(r2)      <- TOC-loaded, but NOT the table
lwz    r11, 16(r2)     <- the real base
lwzx   r0,  r9, r11    <- index first, base second
mtctr  r0
bctr                   <- no `add`: the table is ABSOLUTE
```

with `TOC+8` pointing at unrelated data and `TOC+16` at a real 3-entry table.

| | result |
|---|---|
| `8a26ba6` (unpatched) | **no dispatcher** (`None`) |
| with this patch | `[0x13000, 0x13004, 0x13008]` |

## What the patch preserves

The patch is deliberately **not** a replacement of your function with ours.
Everything your lineage has moves inside the per-candidate loop unchanged:

- the **two-level base** walk (`lwz r30,d1(r2); lwz r11,d2(r30)`) with its
  blr-bounded widening — this used to be a fork-only feature on our side and is
  now yours; the patch does not touch it;
- the **64-bit `sldi;add;lwz` idiom** synthesis that fabricates the equivalent
  `lwzx` operands;
- `_read_ptr`'s **ELFv1 high-word check** (a non-zero high word means the wrong
  slot was read);
- the **case count from the largest immediate** compared against the *raw* index
  register, including the shift-def walk;
- the **generous `scan`** with structural end-of-table termination;
- the **syntactic `add` preference** — it survives, demoted from final decision
  to cheap tie-break (ties go to the earlier candidate, which is the
  `add`-paired one whenever the reordering fired).

Ignoring the re-indentation, the semantic diff is **14 insertions / 6
deletions** (`git diff -w`).

## Non-regression

Eight vectors, run against your function before and after the patch:

| vector | before | after |
|---|---|---|
| one-level absolute | 3 targets | 3 targets |
| two-level indirection | 2 targets | 2 targets |
| offset table | 2 targets | 2 targets |
| multi-TOC | 2 targets | 2 targets |
| offset + first-operand collision | 2 targets | 2 targets |
| **absolute + first-operand collision** | **dropped** | **3 targets** |
| scalar `toc` (int) | ok | ok |
| `toc=None` | ok | ok |

Zero regressions, one gain. Those vectors ship with the patch as
`tools/test_ppu_jumptables.py`; `.github/workflows/linux.yml` already globs
`tools/test_*.py` under `set -e`, so the suite runs with no CI change. Against
`8a26ba6` the new file fails exactly one vector, which is what makes it a test
rather than a decoration.

Your existing suites — `test_ppu_lift.py` (1311 conformance checks),
`test_ppu_branches.py`, `test_lifter_dedupe.py` — pass unchanged, before and
after. `lift_selftest.py` was **not exercised**: it needs the PS3 SDK PPU
compiler, which we do not have on this machine.

## The trade-off, stated

Arbitration runs the decode loop once per `(base, toc)` pair instead of once per
`toc` — at most 2× the reads, on a path that only runs on `bctr` sites. And
arbitration by count can in principle prefer a wrong base that happens to
validate more entries than the right one; that is the same risk your multi-TOC
arbitration already accepts, and the per-entry text-range validation is the same
guard. We think counting beats guessing here, but the choice is yours to make.

## What we measured in our fork — and what it does not prove

On our fork (2026-07-30), an A/B of `ppu_lifter.py` against a real `EBOOT.ELF`
gave **+20 dispatchers and +208 case targets** for the equivalent fix.

That figure is a **lifter emission count, `offline-unit` — not `in-boot`**: a
lift that emits more functions is not automatically better. We report it as an
indication that the defect has non-trivial reach on a real binary, not as proof
that the fix improves runtime behaviour.

## Re-anchoring (known gap on our side)

`tools/upstream_jt_snapshot.py` still carries the verbatim cut of `ee97d01`
(`fix/fold-merge-dropped-fixes`, 2026-08-09), so
`python3 tools/test_upstream_jt_contrib.py` with no arguments measures against
that sha. The `--alvo` mode is what we used for every claim above about
`8a26ba6`. Re-cutting the snapshot at `master` is a follow-up on our side; it
does not affect the patch.

## Status

- Branch `fix/jumptable-base-arbitration` (commit `1c7a0fe`) is published at
  <https://github.com/andrebrumdev/ps3recomp>, on top of `8a26ba6`.
- **No pull request could be opened**: `andrebrumdev/ps3recomp` is a standalone
  repository, not a GitHub fork of `sp00nznet/ps3recomp`, so the two are not in
  the same network and GitHub refuses a cross-repo PR. Opened instead as
  <https://github.com/sp00nznet/ps3recomp/issues/115>, with cherry-pick
  instructions.
- Upstream acceptance is **not** a completion criterion on our side — we do not
  control it. If the patch is useful, take it; if you would rather fix it
  differently, the test vectors stand on their own.

## Reproducing

```sh
# the patch applies to master
git clone https://github.com/sp00nznet/ps3recomp /tmp/up && cd /tmp/up
git apply --check .../0001-ppu_lifter-jumptable-base-arbitration.patch

# it fixes the vector, and the vectors ship with it
git apply .../0001-ppu_lifter-jumptable-base-arbitration.patch
python3 tools/test_ppu_jumptables.py      # 9 checks, all green
git stash && python3 tools/test_ppu_jumptables.py   # 1 FAIL: the dropped dispatcher

# our own differential, from this repo
python3 tools/test_upstream_jt_contrib.py --alvo /tmp/up/tools/ppu_lifter.py
```
