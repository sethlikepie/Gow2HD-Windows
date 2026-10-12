# XenonRecomp Lessons → ps3recomp Tooling Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Import the high-value engineering patterns from [hedge-dev/XenonRecomp](https://github.com/hedge-dev/XenonRecomp) (config-driven recomp, mid-asm hooks, jump-table analyse, weak function overrides, ABI opts, SIMDe, opcode suite) into ps3recomp **without** breaking the GoW2 macOS bring-up path or inventing a second runtime.

**Architecture:** Keep the monorepo split (lifter in `tools/`, runtime in `runtime/`+`libs/`, game port in `gow2-recomp`). Add a **declarative recomp config layer** (TOML) that drives the lifter; emit hooks and overrides into host-owned sources so `ppu_recomp_*.cpp` / `spu_recomp.c` stay regenerable. Ship each phase with unit/selftest gates and a GoW2 smoke so intro/WAD/TYPE15 progress does not regress.

**Tech Stack:** Python 3.11+ (`tools/ppu_lifter.py`, new analyse tools), TOML (`tomllib` / `tomli`), C++20/C11 (lifted output + hooks), Clang preferred for weak-alias hooks, CMake/Ninja (runtime), existing `lift_selftest.py` / `spu_lift_selftest.py`, GoW2 `env_gow2.sh` + `boot_gow2`.

## Global Constraints

- **Never commit game data** (`EBOOT.ELF`, `extracted/`, `movie_cache/`, `*.psarc`, `spu_images/*.elf` raw).
- **Commits em português**, paths explícitos; **sem** `Co-Authored-By`; **sem** `git add -A`.
- **Probes/hooks default OFF** unless explicitly a permanent correctness fix (document opt-out).
- **No CRC bypass / GATE-FORCE / SHADER_DEMO / forged st620** as acceptance of “done”.
- **GoW2 smoke after every phase that touches lifter or runtime:** intro st620 0→1→≥3 (or FORCE path with thr), no OOB right after TLS; prefer 25–40s headless with `PS3_NO_RSX=1` when only tooling changes.
- **Re-lift safety:** any permanent game fix that today lives in `patch_*.py` must migrate to mid-asm / weak override **or** remain an idempotent script until migration is proven.
- **Opts (`*_as_local`, skip_lr) only after Phase A+B green** — never enable by default before a full GoW2 intro+thr smoke.
- **Clang 18+** recommended for weak-alias hooks; document MSVC/clang-cl limits.
- **Push only personal fork** (`andrebrumdev`); never upstream sp00nznet from agents.
- Reference read: XenonRecomp README (jump tables, midasm, optimisations, TOML); our `tools/ppu_lifter.py`, `gow2-recomp/recomp_mid_v2/patch_*.py`, `docs/PPU_RECOMP.md`.

---

## File / Module Map

| Path | Responsibility |
|------|----------------|
| `tools/recomp_config.py` | Parse/validate TOML recomp config into dataclasses |
| `tools/schemas/recomp_config.schema.json` | Optional schema for editors / CI |
| `tools/ps3_analyse.py` | Jump-table + (later) restgpr detection → TOML |
| `tools/ppu_lifter.py` | Consume config: midasm, invalid ranges, switch tables, opts |
| `tools/spu_lifter.py` | Later: same invalid-skip / TODO policy consistency |
| `runtime/ppu/ppc_hooks.h` | Macros `PPC_FUNC` / `PPC_FUNC_IMPL` / weak aliases |
| `runtime/ppu/ppc_indirect.h` (+ `.c`/`.cpp`) | O(1) indirect dispatch table API |
| `runtime/ppu/ppu_memory.h` | `volatile` load/store policy (if changed) |
| `gow2-recomp/config/gow2_recomp.toml` | Per-title recomp config |
| `gow2-recomp/config/gow2_switch_tables.toml` | Detected/hand jump tables |
| `gow2-recomp/hooks/gow2_midasm_hooks.cpp` | Host implementations of mid-asm hooks |
| `gow2-recomp/hooks/gow2_func_overrides.cpp` | Weak overrides of lifted functions |
| `gow2-recomp/recomp_mid_v2/patch_*.py` | Legacy; migrate then deprecate |
| `tools/test_recomp_config.py` | Unit tests for config + midasm emission |
| `tools/test_ps3_analyse_jtables.py` | Synthetic jump-table fixtures |
| `tools/lift_selftest.py` / `spu_lift_selftest.py` | Extend for new opts / SIMDe paths |
| `docs/RECOMP_CONFIG.md` | Author-facing config reference |
| `docs/superpowers/plans/2026-07-23-xenonrecomp-lessons-recomp-tooling.md` | This plan |

### Dependency graph (phases)

```
Phase A  Config + midasm + weak overrides     ──► unblocks clean re-lift
Phase B  Jump-table analyse + emission        ──► correctness (fallthrough/bctr)
Phase C  Indirect O(1) + volatile loads       ──► multi-thread / icall
Phase D  ABI locals opts (gated)              ──► size + perf (post-stable)
Phase E  SIMDe VMX + opcode suite             ──► portability + regression
Phase F  Shader recomp track (optional)       ──► pixels (parallel to menu walls)
```

**Do not start D until A+B complete and GoW2 thr smoke is green with re-lifted chunks.**

---

## Phase A — Declarative config, mid-asm hooks, weak overrides

### Task A1: Recomp config schema + parser

**Files:**
- Create: `tools/recomp_config.py`
- Create: `tools/test_recomp_config.py`
- Create: `docs/RECOMP_CONFIG.md` (stub filled as fields land)
- Create: `gow2-recomp/config/gow2_recomp.toml` (minimal)

**Interfaces:**
- Produces: `RecompConfig` dataclass with at least:
  - `main.elf_path: str`
  - `main.functions_json: str`
  - `main.out_directory: str`
  - `main.switch_table_file: str | None`
  - `midasm_hooks: list[MidasmHook]`
  - `functions_override: list[{address:int, size:int}]`
  - `invalid_instructions: list[{data:int, size:int}]`
  - `optimizations: OptFlags` (all default false)
- Consumes: TOML path via `load_recomp_config(path: str | Path) -> RecompConfig`

- [ ] **Step 1: Write failing tests for parser**

```python
# tools/test_recomp_config.py
from pathlib import Path
import textwrap
from recomp_config import load_recomp_config

def test_load_minimal(tmp_path: Path):
    p = tmp_path / "r.toml"
    p.write_text(textwrap.dedent("""
        [main]
        elf_path = "EBOOT.ELF"
        functions_json = "functions.json"
        out_directory = "recomp_macos_v2"
        switch_table_file = "config/gow2_switch_tables.toml"

        [optimizations]
        skip_lr = false
        cr_as_local = false
    """), encoding="utf-8")
    cfg = load_recomp_config(p)
    assert cfg.main.elf_path.endswith("EBOOT.ELF")
    assert cfg.optimizations.skip_lr is False
    assert cfg.midasm_hooks == []

def test_midasm_hook_fields(tmp_path: Path):
    p = tmp_path / "r.toml"
    p.write_text(textwrap.dedent("""
        [main]
        elf_path = "EBOOT.ELF"
        functions_json = "functions.json"
        out_directory = "out"

        [[midasm_hook]]
        name = "Ce03cWaitIdle"
        address = 0x000CE03C
        registers = ["r2", "r30"]
        after_instruction = false
    """), encoding="utf-8")
    cfg = load_recomp_config(p)
    assert len(cfg.midasm_hooks) == 1
    h = cfg.midasm_hooks[0]
    assert h.name == "Ce03cWaitIdle"
    assert h.address == 0xCE03C
    assert h.registers == ["r2", "r30"]
```

- [ ] **Step 2: Run tests — expect FAIL (module missing)**

```bash
cd /path/to/ps3recomp
python -m pytest tools/test_recomp_config.py -v
```

Expected: `ModuleNotFoundError: recomp_config`

- [ ] **Step 3: Implement `tools/recomp_config.py`**

- Use `tomllib` (3.11+) with fallback `tomli` if needed.
- Resolve paths relative to the TOML file directory.
- Validate mutually exclusive midasm fields (mirror Xenon: no `return` + `jump_address`).
- Raise `RecompConfigError` with file:line-ish message on bad TOML.

- [ ] **Step 4: Run tests — expect PASS**

- [ ] **Step 5: Add minimal `gow2-recomp/config/gow2_recomp.toml`**

```toml
[main]
elf_path = "../EBOOT.ELF"   # local only; never commit ELF
functions_json = "../functions.json"
out_directory = "../recomp_macos_v2"
switch_table_file = "gow2_switch_tables.toml"

[optimizations]
skip_lr = false
skip_msr = false
ctr_as_local = false
xer_as_local = false
reserved_as_local = false
cr_as_local = false
non_argument_as_local = false
non_volatile_as_local = false
```

- [ ] **Step 6: Commit**

```bash
git add tools/recomp_config.py tools/test_recomp_config.py \
  gow2-recomp/config/gow2_recomp.toml docs/RECOMP_CONFIG.md
git commit -m "feat(tools): schema e parser TOML de recomp (estilo XenonRecomp)"
```

---

### Task A2: Mid-asm hook emission in `ppu_lifter.py`

**Files:**
- Modify: `tools/ppu_lifter.py` (function body emission path — where each insn becomes C)
- Modify: `tools/recomp_config.py` if hook signature helpers needed
- Create: `tools/test_midasm_emit.py`
- Create: `gow2-recomp/hooks/gow2_midasm_hooks.cpp` (stubs)
- Create: `gow2-recomp/hooks/gow2_midasm_hooks.h`

**Interfaces:**
- Consumes: `RecompConfig.midasm_hooks`
- Produces: emitted C lines like:
  - `gow2_midasm_Ce03cWaitIdle(ctx);` before/after insn at `address`
- Hook C ABI: `extern "C" void gow2_midasm_<Name>(ppu_context* ctx);`  
  (registers list reserved for later locals-opt; v1 always passes full `ctx`)

- [ ] **Step 1: Failing unit test — emit hook before target insn**

Build a tiny synthetic function buffer (4–8 words) with known guest base, config hook on word 2, run lifter emit on that function only, assert substring `gow2_midasm_TestHook(ctx)` appears once before the translated insn for that address.

- [ ] **Step 2: Implement emission**

In the per-instruction loop of `ppu_lifter.py`:

1. Build `dict[int, list[MidasmHook]]` from config.
2. Before translating insn at `ea`, if hooks with `after_instruction=false`, emit calls.
3. Translate insn.
4. If `after_instruction=true`, emit calls after.
5. Support `return=true` → emit `return;` after hook (v1 only; jump_address in A2b optional follow-up).

- [ ] **Step 3: Wire CLI**

```bash
python tools/ppu_lifter.py EBOOT.ELF \
  --functions functions.json \
  --recomp-config gow2-recomp/config/gow2_recomp.toml \
  --output recomp_macos_v2
```

If `--recomp-config` omitted, behavior identical to today (no hooks).

- [ ] **Step 4: Stub hook library + link in `gow2-recomp/build_macos.sh`**

```cpp
// gow2-recomp/hooks/gow2_midasm_hooks.cpp
#include "ppu_context.h"  // correct include path as used by boot
extern "C" void gow2_midasm_Ce03cWaitIdle(ppu_context* ctx) {
    (void)ctx; /* Phase A3 moves CE03C body here */
}
```

- [ ] **Step 5: Commit**

```bash
git commit -m "feat(lifter): mid-asm hooks via TOML (Xenon-style)"
```

---

### Task A3: Migrate one real GoW2 hook (CE03C wait-idle) to mid-asm

**Files:**
- Modify: `gow2-recomp/config/gow2_recomp.toml` — add `[[midasm_hook]]` for CE03C entry
- Modify: `gow2-recomp/hooks/gow2_midasm_hooks.cpp` — move logic from patched `func_000CE03C`
- Modify: `gow2-recomp/recomp_mid_v2/patch_ce03c_wait_idle_f2b_movie.py` — either stop injecting wait-idle **or** make it a no-op when `PS3_MIDASM_CE03C=1`
- Test: structural string test + GoW2 smoke 30s

**Goal of migration:** prove re-lift does not delete wait-idle.

- [ ] **Step 1: Capture baseline smoke log** (`PS3_NO_RSX=1`, FORCE 8s or menu-fast) — greps: `CE03C wait-idle`, `thr_auto_load`, `StartSeq`
- [ ] **Step 2: Implement hook body** (copy only wait-idle + sticky EOS clear; keep longjmp pad decision documented)
- [ ] **Step 3: Re-lift or surgically strip old injection from one local `ppu_recomp_001.cpp` function** so only midasm remains
- [ ] **Step 4: Smoke — expect same or better thr path**
- [ ] **Step 5: Commit**

```bash
git commit -m "refactor(gow2): CE03C wait-idle via mid-asm hook (re-lift safe)"
```

---

### Task A4: Weak function overrides (`PPC_FUNC` / `PPC_FUNC_IMPL`)

**Files:**
- Create: `runtime/ppu/ppc_hooks.h`
- Create: `gow2-recomp/hooks/gow2_func_overrides.cpp`
- Modify: `tools/ppu_lifter.py` — emit weak definition + strong `__imp__` symbol names
- Modify: `gow2-recomp/build_macos.sh` — compile/link overrides
- Create: `tools/test_weak_override_symbols.py` (nm on a tiny lifted object if feasible)

**Interfaces:**
- Header macros (Clang):

```cpp
// runtime/ppu/ppc_hooks.h
#pragma once
#include "ppu_context.h"

#if defined(__clang__)
#define PPC_FUNC_IMPL(name) \
    __attribute__((weak)) void name(ppu_context* ctx)
#define PPC_FUNC(name) \
    void name(ppu_context* ctx)
#else
// Fallback: plain symbols; override requires link-order tricks
#define PPC_FUNC_IMPL(name) void name(ppu_context* ctx)
#define PPC_FUNC(name) void name(ppu_context* ctx)
#endif
```

- Lifter emission (conceptual):
  - `void func_000CE03C(ppu_context* ctx)` becomes weak **or** renames body to `__imp_func_000CE03C` and provides weak wrapper.
  - Prefer Xenon pattern: `PPC_FUNC_IMPL(__imp_func_XXXXXXXX);` body; `PPC_FUNC(func_XXXXXXXX){ __imp_...(ctx); }` weak outer.

- [ ] **Step 1: Document chosen symbol scheme in `docs/RECOMP_CONFIG.md`**
- [ ] **Step 2: Implement emission behind `--recomp-config` + `emit_weak_wrappers=true`**
- [ ] **Step 3: Override one non-critical probe function first** (e.g. empty POSTINTRO enter) before CE03C
- [ ] **Step 4: Smoke GoW2**
- [ ] **Step 5: Commit**

```bash
git commit -m "feat(lifter): weak wrappers para override de funções guest"
```

---

## Phase B — Jump-table analysis and correct emission

### Task B1: `ps3_analyse.py` jump-table detector

**Files:**
- Create: `tools/ps3_analyse.py`
- Create: `tools/test_ps3_analyse_jtables.py`
- Create: `tools/fixtures/jtable_synth.bin` (or generate in test)
- Create: `gow2-recomp/config/gow2_switch_tables.toml` (may start empty + comments)

**Interfaces:**
- CLI: `python tools/ps3_analyse.py EBOOT.ELF -o gow2_switch_tables.toml`
- Output TOML:

```toml
[[switch_table]]
dispatch = 0x00ABCDEF   # address of bctr / mtctr site
default = "indirect"    # or fallthrough EA
cases = [
  { index = 0, target = 0x00AB0100 },
  { index = 1, target = 0x00AB0200 },
]
```

- Detection heuristics (document limitations):
  1. Scan for `mtctr rX` followed within N insns by `bctr` / `bcctr`.
  2. Backtrack for `rlwinm`/`addi`/`lwzx` table base patterns (PPC).
  3. Read table words as BE guest absolute targets; keep only targets inside `.text`.
  4. Confidence score; emit `confidence` field; low confidence → comment-only unless `--include-low`.

- [ ] **Step 1: Synthetic fixture test** — hand-craft 8 PPC words that match heuristic, assert TOML cases
- [ ] **Step 2: Implement detector** (reuse `ppu_disasm.py` / `spu_disasm` patterns)
- [ ] **Step 3: Run on GoW2 EBOOT locally** (not committed); save human-reviewed tables under `gow2-recomp/config/`
- [ ] **Step 4: Commit tool + empty/hand-reviewed tables (no ELF)**

```bash
git commit -m "feat(tools): ps3_analyse jump tables → TOML"
```

---

### Task B2: Lifter consumes switch tables as real `switch(ctr)`

**Files:**
- Modify: `tools/ppu_lifter.py` — already has `self.jump_tables`; load from config TOML
- Modify: `tools/recomp_config.py` — parse switch file
- Test: extend `test_ppu_lift.py` or new `test_jtable_emit.py`

- [x] **Step 1: Load TOML into `jump_tables` dict** (merge with discover_jump_tables; config wins on conflict)
  Implemented as `tools/recomp_config.SwitchTable` + `load_switch_tables()`
  (parses the SEPARATE file `[main].switch_table_file` points at, same
  convention as `functions_json`) and `tools/ppu_lifter.merge_switch_tables()`
  (config replaces, not unions, discovery's entry for the same `dispatch`).
  Three design decisions the brief flagged as non-obvious:
  1. **`index`** in the plan's `{index, target}` case shape is parsed (so a
     missing `target` next to a present `index` still errors) but has NO
     effect on anything -- ppu_lifter.py's bctr codegen keys arms by target
     ADDRESS (`case 0x{c:08X}u: ...`, ppu_lifter.py ~:1536-1538), never by
     position. A bare int case (`cases = [0x200, 0x300]`) is also accepted;
     both shapes can mix in one `cases` array.
  2. **Out-of-function case targets** (and a `dispatch` outside any known
     function at all) are no longer silent: emission still drops them
     unchanged (unmodified codegen, out of scope), but `merge_switch_tables`
     now reports every such case via a stderr warning naming the dispatch and
     the specific offending address(es).
  3. **`default = "indirect"`** is accepted as a documented no-op (it is
     already, unconditionally, what the bctr codegen falls back to); any
     other value is a hard `RecompConfigError` at parse time in
     `recomp_config.py` -- there is no slot for an explicit fallthrough EA in
     `dict[int, list[int]]` without a codegen change, which stays out of
     scope.
  Merge happens once, right before `lifter.jump_tables` is assigned in
  `main()` (before the `--jobs`>1 branch), so both the serial lift loop and
  `_parallel_lift`'s initargs (which reads `lifter.jump_tables` at call time)
  see the identical merged dict -- no separate propagation code needed for
  the worker path, since it already threaded `jump_tables` through for
  `discover_jump_tables`'s output.
  Tests: `tools/test_recomp_config.py` (schema/loader: index-ignored,
  bare-int cases, mixed shapes, default-rejection, missing dispatch/target,
  missing file, empty section) + new `tools/test_switch_tables.py` (merge
  unit tests: passthrough, add, config-wins-on-conflict, oob/un-enclosed
  warnings; direct `_worker_init`/`_worker_lift` and real-Pool
  `_parallel_lift` wiring guards; CLI subprocess wiring with `--jobs 1` and
  `--jobs 2` against the same fixture; a `--config` run with no
  `switch_table_file` set proven unaffected). Wired into
  `scripts/smoke_macos.sh` section 2c.
  Mutation-tested every propagation call site (apply, confirm the expected
  test(s) fail and nothing else, revert): dropping the config load, bypassing
  the merge assignment, breaking `_worker_init`'s/`_worker_lift`'s storage of
  `jump_tables`, dropping it from `_parallel_lift`'s initargs tuple, breaking
  "config wins", and dropping the oob warning -- each isolated to exactly the
  guard(s) built for it; critically, breaking the worker/parallel-only sites
  left the `--jobs 1` CLI test passing while `--jobs 2`/real-Pool tests
  failed, reproducing (as a caught regression, not a live bug) the `-j1`
  vs. `-jN` divergence the brief warned about.
  Status: **offline-unit only** -- no in-boot GoW2 evidence; not re-lifted
  against the real EBOOT.

  **Review round 1 (Important #1, fixed):** the merge did not deduplicate
  case targets while `discover_jump_tables` already does
  (`tables[addr] = sorted(set(best))`, `ppu_lifter.py` ~:3468). A declared
  table where two indices share one target -- `case 0: case 1: ...`, the
  ORDINARY shape of a real switch's fallthrough cases, not a synthetic edge
  case -- emitted the same `case 0x...u:` label twice; clang rejects that
  with "duplicate case value", 5-8 minutes after the lift, not at lift time.
  Fixed with `sorted(set(cases))` in `merge_switch_tables`
  (`tools/ppu_lifter.py`), matching discovery's own normalization exactly --
  case order has no semantic meaning to the emitted `switch` and `index` was
  already decorative. Confirmed red first (reverted the dedup line, three
  tests failed: the unit test plus both `--jobs 1`/`--jobs 2` CLI wiring
  tests), then green. New test:
  `test_merge_dedupes_duplicate_case_targets_like_discovery_does` +
  duplicate-target assertions added to the existing CLI wiring tests in
  `tools/test_switch_tables.py`.
  **Minor (fixed):** two `[[switch_table]]` entries sharing one `dispatch`
  were last-wins in silence, and `n_overridden` double-counted the
  declared-vs-declared collision as a discovery override. Now warned
  (mirrors the `[[invalid_instructions]]` duplicate-`data` last-wins rule)
  and `n_overridden` is computed against the original `discovered` dict, not
  the dict `merge_switch_tables` mutates as it iterates. New test:
  `test_duplicate_declared_dispatch_last_wins_and_is_warned`.
  **Minor (fixed):** a dead `... or True` assertion in
  `tools/test_switch_tables.py` (always green, proved nothing) replaced with
  a real check that the warning's enumerated out-of-range list contains
  exactly the offending address and not the in-range one.
  **Minor (fixed):** `tools/test_switch_tables.py`'s
  `test_cli_wiring_no_switch_table_file_is_unaffected` claimed stdout was
  "identical to no switch-table support at all" when it is not -- `main()`
  always prints the `  switch tables: N from discovery, M from config, K
  overridden by config, T total` summary line (`ppu_lifter.py`, right before
  `lifter.jump_tables` is assigned) regardless of `--config`. Docstring and
  assertions corrected to check the real behaviour (no `N switch_table(s)
  declared` line, but the unconditional summary line DOES appear with
  all-zero counts).
  **Known gap, registered but NOT implemented (flagged as new validation,
  out of the B2 brief -- reviewer and I agree it's real):** a declared case
  target that falls INSIDE the dispatcher's enclosing function bounds but
  does NOT land on an actual decoded instruction boundary produces no
  warning at all today. `ppu_lifter.py`'s per-function emission
  (`lift_function`, ~:666-675) fills any in-range branch target with no
  matching `insn.addr` with a bare `loc_X: ; /* branch into undecoded region
  */` label at the function tail -- so that `case` silently falls through to
  an implicit return instead of reaching real code. A one-digit typo in a
  hand-written `target` (in range, wrong offset) reproduces exactly the
  "silently wrong, not caught" failure class this task exists to close, and
  `merge_switch_tables` cannot detect it without comparing every declared
  target against the set of ACTUALLY DECODED instruction addresses for that
  function (not just its `[start, end)` range) -- a real validation gap,
  left for a follow-up task rather than expanded here.
- [ ] **Step 2: Ensure emission uses `switch ((uint32_t)ctx->ctr)` with goto/trampoline cases** (code path already partially present around bctr) --
  UNCHANGED by Task B2 on purpose (see Step 1): this codegen already existed
  (ppu_lifter.py's `_translate` bctr handler) before B2 and needed no edits.
- [x] **Step 3: Audit known fallthrough bug class** (guest EA target lower than site — document search script)
  `tools/audit_fallthrough.py` (+ `tools/test_audit_fallthrough.py`,
  `tools/test_find_functions_prologue_corroboration.py`): statically
  re-derives ppu_lifter's fallthrough-trampoline resolution from a
  `functions.json`-style file, flags malformed ranges, duplicate starts and
  `end`-not-a-boundary gaps, and (given `--cpp`) greps a generated
  `ppu_recomp_XXX.cpp` for unconditional tail-position `g_trampoline_fn`
  assignments.
  **Correction after external review (findings 1/2/5):** the real
  func_002550C8 → func_002550E8 failure is CONSISTENT — functions.json and
  the generated .cpp come from the SAME lift, so both record the already
  truncated end 0x2550E8 and the .cpp trampolines to it. The wrong target
  EQUALS the recorded end and is ABOVE the site start, so the tool's first
  cut (`target < start` in the JSON pass, `target < end` in the .cpp pass)
  was structurally dead for well-formed input / only ever matched legitimate
  explicit backward `b` tail-calls. The `target < start` JSON check was
  removed (provably unreachable: an auto-appended fallthrough always resolves
  to `end` ≥ start). The .cpp detector was redesigned to key on the actual
  signature: a tail-position trampoline whose target EQUALS the site's own
  recorded `end` (an adjacent registered function) on a suspiciously SMALL
  fragment (`--fallthrough-max-size`, default 0x80) → HIGH heuristic
  candidate; a target that does NOT match the computed fallthrough resolution
  is an explicit `b`/`bc` tail-call and is left unflagged (so legitimate
  backward tail-calls no longer false-positive). Tests reproduce the
  CONSISTENT triple and assert `target == end` (proving the pre-fix
  `target < end` logic could not have fired), plus explicit-backward-tailcall
  and size-gate fixtures.
  Ran the fixed detector against the committed `games/gow2/functions.json`
  (20 750 entries): **0 CRITICAL/HIGH** structural findings, exit 0 — 3282
  INFO `end-not-a-boundary` gaps and (under `--heuristics`) 1015 speculative
  tiny-fragment chains, no malformed ranges, no duplicate starts. This is
  NOT evidence the tool "works" against a live bug: the concrete 2550C8
  instance is only detectable with the generated .cpp, which was not
  available in this sandbox — the .cpp detector is exercised by unit fixtures
  only.
  Also applied a narrow, conservative hardening to `tools/find_functions.py`'s
  `find_prologue_functions` mid-scan safety-stop (require a corroborating
  store-LR before trusting a bare `mflr r0` as a new function boundary). This
  is a PLAUSIBLE, narrowly-scoped robustness improvement of one truncation
  path with the same shape as the 2550C8 split — **NOT confirmed to be that
  bug's root cause**: 0x2550E8 was never disassembled here, so whether it was
  a bare coincidental `mflr r0` (caught) or a genuine mid-entry prologue with
  its own store-LR (correctly still treated as a real boundary, hence
  unchanged) is unverified against the real EBOOT bytes. Covered by fixtures
  for all three cases (uncorroborated mflr grows the extent; genuine adjacent
  prologue still stops; genuine mid-body second prologue still stops and is
  recovered as its own function). Unexercised beyond these unit fixtures + the
  structural pass over the real functions.json.
- [ ] **Step 4: GoW2 smoke after re-lift of one chunk that contains a known table**
- [ ] **Step 5: Commit**

```bash
git commit -m "feat(lifter): jump tables TOML → switch(ctr) fiel"
```

---

### Task B3: Invalid instruction / padding skip

**Files:**
- Modify: `tools/ppu_lifter.py` — skip ranges from `invalid_instructions`
- Modify: `gow2-recomp/config/gow2_recomp.toml` — add known padding/data-in-text if any
- Test: unit with synthetic `.word` gap

- [x] **Step 1: Config field already in A1 — wire skip in linear scan**
- [x] **Step 2: When skipping, do not emit TODO spam; emit comment `/* invalid skip N bytes */`**
- [x] **Step 3: Commit**

```bash
git commit -m "feat(lifter): invalid_instructions skip (padding/data-in-text)"
```

Nota (revisao round 1, item fora de escopo — NAO corrigido, so' documentado): "block"
em `disassemble_bytes` significa coisas diferentes nos dois call sites. O scan serial
em `main()` ve o segmento PT_LOAD inteiro; `_worker_lift` (lift paralelo) ve so' a
janela de bytes de UMA funcao. Um `size` declarado grande e' clampado ao fim desse
"block" — no worker isso e' o fim da funcao, no serial e' o fim do segmento. Um `size`
improvavelmente grande para padding real atravessaria uma fronteira de funcao no
caminho serial mas ficaria clampado a' funcao no paralelo, produzindo lifts `--jobs 1`
vs `--jobs N` diferentes. So' morde com um `size` fora do que um padding real teria;
nao corrigido porque exigiria passar os limites verdadeiros do segmento para a janela
por-funcao do worker (fora do escopo de B3). Ver docstring de `disassemble_bytes` em
`tools/ppu_disasm.py`.

Nota (revisao round 2 — gap fechado, opcao (a)): o round 1 tinha deixado o ramo ELF
de `main()` (tools/ppu_lifter.py, `if args.raw: ... else: ...` ~:3707) sem nenhum
teste de subprocess -- so' havia um teste de CLI e corria sempre com `--raw`. Pior:
a docstring desse teste afirmava cobrir "qualquer" call site de `main()`, o que era
falso (afirmar mais cobertura do que a suite tem e' o mesmo defeito corrigido
noutros sitios nesta sessao). Fechado construindo um fixture ELF64 minimo em codigo
de teste puro (so' `struct.pack`, sem dependencias novas -- `_build_minimal_elf64`
em `tools/test_invalid_instructions.py`) e tres testes de subprocess dedicados, um
por call site: `test_cli_wiring_elf_primary_call_site` (PT_LOAD executavel, loop
primario ~:3721), `test_cli_wiring_elf_fallback_call_site` (PT_LOAD sem PF_X, loop
de fallback ~:3729) e `test_cli_wiring_elf_exception_fallback_call_site` (ELF
invalido sem `--raw`, cai no fallback de excepcao ~:3736). Confirmado por mutation
testing manual (remover `invalid_instructions=` de cada um dos tres sitios, um de
cada vez, correr a suite, ver exactamente esse teste falhar sozinho, repor).

Correcao (revisao round 3, so' contagem, nao mexe em codigo): sao **cinco** call
sites reais de `disassemble_bytes` em `tools/ppu_lifter.py` (4 em `main()` +
`_worker_lift`), nao seis -- `_parallel_lift` NAO chama `disassemble_bytes`
directamente, despacha para `_worker_lift` via `pool.imap_unordered`, portanto e'
o MESMO call site (`:3531`) exercitado por dois mecanismos de invocacao diferentes
(chamada directa vs. `Pool` real), nao um sexto. A formulacao correcta e' "5 call
sites, 6 guardas de regressao dedicadas": `:3531` tem duas guardas porque testam
falhas distintas e legitimas (o wiring directo via `_worker_init`/`_worker_lift`, e
o tuplo posicional `initargs` de `_parallel_lift`).

---

## Phase C — Indirect dispatch O(1) + memory volatility

### Task C1: O(1) guest→host function map

**Files:**
- Create: `runtime/ppu/ppc_indirect_map.h`
- Create: `runtime/ppu/ppc_indirect_map.cpp`
- Modify: `runtime/ppu/ppu_loader.cpp` — `ps3_indirect_call` uses map after build
- Modify: `tools/ppu_lifter.py` — emit registration array `ppc_func_entry[]` (addr, fn) if not already complete
- Test: unit test map insert/lookup with fake entries

**Interfaces:**
- `void ppc_indirect_map_build(const ppc_func_entry* table, size_t n);`
- `ppu_fn ppc_indirect_map_lookup(uint32_t guest_ea);` → null if missing
- Strategy v1: **sorted table + bsearch** (simple, correct). v2: open-address hash if profiling shows hot.

- [ ] **Step 1: Failing test lookup**
- [ ] **Step 2: Implement bsearch map**
- [ ] **Step 3: Wire `ps3_indirect_call` — keep ICALL-ASCII / HEAP guards**
- [ ] **Step 4: GoW2 smoke (ICALL-ASCII residual should not increase)**
- [ ] **Step 5: Commit**

```bash
git commit -m "feat(runtime): mapa O(log n)/O(1) guest EA → função host"
```

---

### Task C2: Volatile guest memory accesses

**Files:**
- Modify: `runtime/ppu/ppu_memory.h` (or equivalent `vm_read32`/`vm_write32` path)
- Test: document only + optional compile-time assert; smoke for no new OOB

**Policy:**
- All guest load/store helpers used by lifted code treat underlying access as not reorderable across calls (C `volatile` on pointer cast, or compiler barrier).
- Do **not** mark entire VM array volatile (perf death); only the access macros.

- [ ] **Step 1: Audit macros in `ppu_memory.h` / `vm.h`**
- [ ] **Step 2: Apply minimal volatile/cast barrier pattern**
- [ ] **Step 3: Build runtime + GoW2 link**
- [ ] **Step 4: Smoke intro**
- [ ] **Step 5: Commit**

```bash
git commit -m "fix(runtime): barreiras volatile em load/store guest (anti-reorder)"
```

---

## Phase D — ABI local-variable optimisations (post-stable)

**Gate:** Phase A+B done; GoW2 thr + R_Perm smoke green on re-lifted tree.

### Task D1: Optimisation flags in lifter

**Files:**
- Modify: `tools/ppu_lifter.py` — when `cr_as_local` etc., emit locals and reduce ctx stores
- Modify: `gow2-recomp/config/gow2_recomp.toml` — keep false by default
- Test: `lift_selftest.py` with opts on **and** off

**Flags (mirror Xenon names):**
- `skip_lr`, `ctr_as_local`, `xer_as_local`, `reserved_as_local`, `cr_as_local`, `non_argument_as_local`, `non_volatile_as_local`

- [ ] **Step 1: Implement one flag end-to-end (`cr_as_local`)** — smallest blast radius
- [ ] **Step 2: lift_selftest PASS with flag true**
- [ ] **Step 3: GoW2 smoke with flag true on one chunk only**
- [ ] **Step 4: Document enable order in RECOMP_CONFIG.md**
- [ ] **Step 5: Commit per flag or batch after all green**

```bash
git commit -m "feat(lifter): opt cr_as_local (off by default)"
```

### Task D2: restgpr / savegpr auto-detect (optional)

**Files:**
- Extend `tools/ps3_analyse.py` — find `lmw`/`stmw` prologues and TOC restore patterns
- Emit addresses into TOML for special emission

- [ ] Only if D1 proves opts useful; otherwise skip (YAGNI).

---

## Phase E — SIMDe + expanded opcode suite

### Task E1: SIMDe for VMX/AltiVec helpers

**Files:**
- Modify: `runtime/ppu/` SIMD helpers (or new `runtime/ppu/ppu_vmx_simde.h`)
- Modify: CMake — optional `PS3RECOMP_USE_SIMDE=ON`
- Vendor or FetchContent SIMDe (header-only)
- Test: compile arm64 + x86_64; run existing float/VMX unit tests if any

- [ ] **Step 1: CMake option + include path**
- [ ] **Step 2: Map one VMX op family through SIMDe**
- [ ] **Step 3: Build macOS arm64 + (if available) x86_64**
- [ ] **Step 4: Commit**

```bash
git commit -m "feat(runtime): SIMDe opcional para VMX (portabilidade arm64/x86)"
```

### Task E2: Expand lift_selftest corpus + CI script

**Files:**
- Modify: `tools/lift_selftest.py` — more cases (shifts, rotates, CR fields, loads if harness allows)
- Create: `tools/run_lift_regression.sh`
- Optional: CI workflow if project has CI

- [ ] **Step 1: Add ≥10 new corpus functions**
- [ ] **Step 2: Script fails non-zero on mismatch**
- [ ] **Step 3: Commit**

```bash
git commit -m "test(lifter): corpus alargado + script de regressão"
```

---

## Phase F — Shader recomp track (parallel, optional)

**Only after** natural SHADERSRC/LDRSH or explicit decision to recomp HOSTRES shaders.

### Task F1: Inventory + design note

**Files:**
- Create: `docs/SHADER_RECOMP_DESIGN.md`
- No code until LDRSH natural path is understood

Content:
- Map CGOWShader / combination / CRC wall
- Compare to XenosRecomp (HLSL/MSL emission)
- Decide: recomp guest FS/VS vs keep Metal decompiler path

- [ ] **Step 1: Write design note with acceptance metrics** (hash FS ≠ demo, bind texture post-stream)
- [ ] **Step 2: Commit note only**

---

## Phase G — GoW2 migration of remaining patch_*.py (ongoing)

After A2/A3 work:

| Legacy patch | Target mechanism | Priority |
|--------------|------------------|----------|
| `patch_ce03c_wait_idle_f2b_movie.py` | midasm + hooks | P0 (A3) |
| `patch_2a4fe4_trace.py` | midasm or keep gated TRACE in source template | P1 |
| `patch_factory_snap_ty15_pin.py` | weak override or runtime always-on (already runtime-ish) | P1 |
| `patch_f2b_multimb_stream.py` | midasm on stream ops | P1 |
| OPD patches (`patch_*_opd.py`) | recomp.toml `functions` + analyse | P2 |

- [ ] Maintain a checklist in `gow2-recomp/notes/patch-migration-ledger.md`
- [ ] Rule: **no new permanent game fix as only patch.py** without TOML/hook path after Phase A ships

---

## Verification matrix (whole program)

| Phase | Unit | GoW2 smoke (min) | Pass criteria |
|-------|------|------------------|---------------|
| A1 | `pytest tools/test_recomp_config.py` | — | all pass |
| A2 | `pytest tools/test_midasm_emit.py` | build `boot_gow2` | links |
| A3 | — | 30–40s headless | `CE03C wait` or midasm log; thr if recipe includes AUTO_LOAD |
| A4 | nm weak symbols | smoke | no missing symbols |
| B1 | `pytest tools/test_ps3_analyse_jtables.py` | — | pass |
| B2 | jtable emit test | re-lift smoke | no new FATAL OOB at boot |
| C1 | map unit test | smoke | ICALL-ASCII not worse |
| C2 | build | smoke | no regression |
| D* | lift_selftest ±opts | smoke per flag | thr green |
| E* | build dual-arch | selftest | pass |

### Canonical GoW2 smoke (tooling phases)

```bash
cd /path/to/gow2-recomp
set -a; . ./env_gow2.sh; set +a
export PS3_NO_RSX=1 PS3_AUTO_LOAD_RUN=1 PS3_VDEC_ASYNC=1
export PS3_VDEC_FORCE_SEQDONE_MS=8000   # or 1500 for menu-fast
# optional: PS3_PERF_FSM=1
./boot_gow2 EBOOT.ELF > /tmp/recomp_phase_smoke.log 2>&1 &
BPID=$!; sleep 35; kill -TERM $BPID 2>/dev/null; sleep 1; kill -9 $BPID 2>/dev/null
grep -E 'st620|thr_auto_load|StartSeq|FATAL|OOB' /tmp/recomp_phase_smoke.log | head -40
```

**Fail phase if:** process dies before TLS; `FATAL` spike; thr regresses when recipe previously had thr; R_Perm disappears on full WAD recipe.

---

## Out of scope (explicit)

- Full GoW2 menu/playable (parallel wall-chain; this plan only enables better tooling).
- Porting Unleashed runtime or Xenia wholesale.
- Perfect-hash 8GB virtual allocation (Xenon abandoned it).
- Enabling all ABI opts by default.
- Committing EBOOT or generated multi-hundred-MB recomp diffs without explicit human request.

---

## Suggested schedule (human or agent)

| Week | Deliverable |
|------|-------------|
| 1 | Phase A1–A2 (config + midasm emit + link stubs) |
| 2 | Phase A3–A4 (CE03C migration + weak wrappers) |
| 3 | Phase B1–B3 (analyse + switch + invalid skip) |
| 4 | Phase C (indirect map + volatile) |
| 5+ | Phase D opts one-by-one; Phase E SIMDe/suite |
| // | Phase F design when shader wall is primary |

---

## Self-review (spec coverage)

| Xenon lesson | Task |
|--------------|------|
| TOML recomp config | A1 |
| Mid-asm hooks | A2, A3 |
| Weak function override | A4 |
| Jump-table analyse + switch | B1, B2 |
| Invalid instruction skip | B3 |
| Indirect O(1)-ish | C1 |
| Volatile loads/stores | C2 |
| Register locals / skip LR | D1 |
| restgpr detect | D2 |
| SIMDe | E1 |
| Large opcode tests | E2 |
| Shader recomp (Xenos analogue) | F1 |
| Patch migration | G |

No TBD placeholders; each phase ends in testable software.

---

## Execution handoff

**Plan complete and saved to** `docs/superpowers/plans/2026-07-23-xenonrecomp-lessons-recomp-tooling.md`.

**Two execution options:**

1. **Subagent-Driven (recommended)** — one subagent per task, review between tasks  
2. **Inline Execution** — this session, `executing-plans`, checkpoints after each task  

**Which approach?** Start with **Phase A Task A1** unless you prefer another entry point (e.g. jump tables first).
