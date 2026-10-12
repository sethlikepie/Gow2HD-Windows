# Testing Patterns

**Analysis Date:** 2026-07-26

**Honesty note (per project rule #4 in `CLAUDE.md` — never forge results):** this document distinguishes what is actually exercised by automated tests (CTest/self-tests, CI-blocking on macOS) from what is validated only by manual boot smoke runs of the game (`../gow2-recomp`, a sibling repo, not covered here). Do not treat "the game boots" as automated coverage — it isn't.

## Test Framework

**Runner:** CTest (via CMake), aggregating three kinds of tests:
1. Python self-tests (plain scripts with `assert`, no pytest)
2. Native C/C++ unit tests (built as CTest executables)
3. Shell-driven integration suites (SPU codegen/build/run, registered as single CTest entries)

**Config:** `CMakeLists.txt` (root) — tests only exist when configured with `-DPS3RECOMP_BUILD_TESTS=ON` (default `OFF`; `option(PS3RECOMP_BUILD_TESTS "Build unit tests" OFF)` at `CMakeLists.txt:24`). With the option off, `enable_testing()` and Python3 discovery don't even run (`CMakeLists.txt:29-41`).

**No pytest, no Jest/Vitest, no gtest/Catch2** — assertion is plain Python `assert` / C `assert()`.

**Run commands (verified against `scripts/*.sh`):**
```bash
./scripts/test_all.sh       # runs both suites below, always both, exit 0 only if BOTH pass
./scripts/test_python.sh    # Python self-tests only, no C/C++ build needed
./scripts/test_runtime.sh   # cmake configure (BUILD_TESTS=ON, no Release/-DNDEBUG) + build + ctest
```
Honor `$PYTHON` (interpreter override, must be ≥3.11) and `$PS3RECOMP_BUILD_DIR` (default `<repo>/build`) env vars.

**Critical build-type note (from `scripts/test_runtime.sh` header comment, verified):** tests must NOT be configured with `-DCMAKE_BUILD_TYPE=Release`. Release injects `-DNDEBUG`, which silently disables `assert()` — and `libs/system/tests/test_autoload_map.c` verifies everything via bare `assert()` with no alternate failure path. This was proven by fault injection: an `assert(0 && "...")` inserted in a private copy still printed "PASS" and exited 0 under Release. `test_runtime.sh` configures with `-DCMAKE_BUILD_TYPE=` (explicit empty) to avoid this trap.

## Test File Organization

**Python self-tests** (root-relative paths). **Correção de 2026-07-26: as duas listas NÃO estão espelhadas** — esta afirmação estava errada na versão original deste documento. Medido: `scripts/test_python.sh` lista 8; o `foreach(_ps3_pytest ...)` do `CMakeLists.txt` lista 13. Cinco rodam em CTest e **não** no `test_python.sh`: `test_audit_boundaries`, `test_ghidra_lookup`, `test_ppu_disasm_regs`, `test_ppu_jumptables`, `test_spu_lift_ops` (todos passam quando invocados à mão com `.venv/bin/python3`). `test_ppu_lifter_emit` aparece só no `test_python.sh` porque o CMake o registra à parte, fora do `foreach` — isso é esperado, não deriva. A intenção continua sendo manter as duas em sincronia ao adicionar um teste; a realidade atual é esta:
```
tools/test_audit_fallthrough.py
tools/test_find_functions_prologue_corroboration.py
tools/test_objdump_audit.py
tools/test_ppu_lift.py                  # known FAIL off Windows (shells out to `cmd /c` for MSVC)
tools/test_recomp_config.py
tools/lift_selftest.py
tools/spu_lift_selftest.py
runtime/ppu/tests/test_ppu_lifter_emit.py
```
Other `tools/test_*.py` files exist (`test_audit_boundaries.py`, `test_ghidra_lookup.py`, `test_invalid_instructions.py`, `test_ppu_disasm_regs.py`, `test_ppu_jumptables.py`, `test_spu_lift_ops.py`, `test_switch_tables.py`, `test_trace_parse.py`) — confirm against `CMakeLists.txt` and `scripts/test_python.sh`'s `TESTS` var before assuming they run in CI; not all `test_*.py` files in `tools/` are necessarily wired into the aggregator.

**Native tests:** live alongside the code they test, in `tests/` subdirectories per module, e.g.:
- `runtime/ppu/tests/` — `test_ppu_lifter_emit.py`, PPU-specific C tests (`test_ppu_fpu_lmw`, `test_ppu_blrl_call`, per `CMakeLists.txt:346-349`)
- `runtime/trace/tests/` — trace/checkpoint harness, `trace_link_stubs.h`
- `libs/system/tests/` — `test_autoload_map.c` (`CMakeLists.txt:375`)
- `libs/audio/tests/test_coreaudio_ring.c` → CTest target `test_coreaudio_ring` (`CMakeLists.txt:379-380`); `libs/video/tests/test_texture_deswizzle.c` → `test_texture_deswizzle` (`CMakeLists.txt:385-390`, needs an explicit `-I libs/video`). The `tests/` directories that actually exist under `libs/` are: `libs/audio/tests`, `libs/video/tests`, `libs/codec/tests`, `libs/system/tests`, `libs/spurs/tests` — there is no `libs/hardware/tests`.
- `runtime/spu/tests/` — the big SPU integration suite (see below)

**Naming:** `test_<thing>.py` for Python; `test_<thing>_main.c` or `test_<thing>.c` for C harnesses; CTest names use dotted namespaces like `py.<name>`, `suite.runtime_spu`, `suite.runtime_trace`, or bare `test_<name>` for native C tests.

## Native (C/C++) Test Structure

Self-contained CTest executables registered directly in `CMakeLists.txt`, e.g.:
```cmake
add_test(NAME test_ppu_fpu_lmw COMMAND test_ppu_fpu_lmw)
add_test(NAME test_autoload_map COMMAND test_autoload_map)
add_test(NAME test_coreaudio_ring COMMAND test_coreaudio_ring)
add_test(NAME test_texture_deswizzle COMMAND test_texture_deswizzle)
```
These are plain executables that exit 0/non-zero and use `assert()` internally (not a test framework with individual-case reporting) — see the Release/`-DNDEBUG` pitfall above, which is specific to this pattern.

## SPU Integration Test Suite (`runtime/spu/tests/`) — the largest test surface

This directory holds ~13 named SPU test cases, each a **generate → compile → run** pipeline, driven by `runtime/spu/tests/run_tests.sh`:

**Per-test file pair:**
- `gen_test_<name>.py` — encodes an SPU program (hand-written instruction sequence) and wraps it in a synthetic ELF
- `test_<name>_main.c` — C harness with channel-overriding stubs + `assert()`-based checks
- Output lands in `out_<name>/spu_recomp.{c,h}` — **generated and committed to the repo** (this is why `git status` shows these files as modified after every `test_runtime.sh` run: the SPU suite regenerates them deterministically each run, and the runner reverts them afterward if the working tree was clean before, per its provenance-based cleanup logic)

**Known test names** (from `runtime/spu/tests/out_*` and `gen_test_*.py`): `adapter`, `bringup`, `brsl_return`, `dma`, `dma_trace`, `event`, `lfqueue`, `lfq_inflate`, `seh_isolation`, `shufb`, `spu_channel_blocking`, `spu_channel_tsan`, `spu_fma_double`, `spu_fma_precision`, `spu_task_abi`, `spurs_completion`, `spurs_e2e`, `spurs_job_dma`, `spurs_kernel`, `spurs_kick_async`, `spurs_printf`, `sum`, `taskset`, `workload`.

**Registered in CTest** as a single aggregate entry `suite.runtime_spu` (`CMakeLists.txt:409`) that shells out to `run_tests.sh` — individual SPU test cases are NOT separately visible in CTest output, only the aggregate pass/fail.

**Compiler discovery in `run_tests.sh`:** `clang` on Darwin (system compiler; `gcc` is only a shim on macOS), `gcc` elsewhere; overridable via `$GCC`.

**Interpreter discovery:** `$PYTHON` → `.venv/bin/python` → `python3` (no bare `python`, since it's absent on macOS/modern Linux).

**Side effect to know before running:** this suite always rewrites `out_<name>/spu_recomp.{c,h}` for the ~11-13 versioned cases and (as of 2026-07-26) also creates `out_spu_channel_blocking/` fresh (added after the others were committed, never itself committed — appears as untracked `??` after every run; confirmed in the working tree at analysis time). `scripts/test_runtime.sh` reverts tracked modifications with `git checkout --` and removes the new untracked dir with `rm -rf`, but **only** when it can prove (via a before/after `git status` snapshot) that the dirtiness didn't pre-exist the run — pre-existing developer changes to those paths are never touched.

## SKIP Convention

A test is `SKIP` (not `FAIL`, not counted as a failure) when:
- Its exit code is `0`, AND
- Its stdout contains a line matching `SKIP:` (not necessarily the first line — `tools/test_ppu_lift.py` prints preamble checks before an eventual `SKIP:` when a host C++ compiler is unavailable)

**Precedence rule (deliberately enforced, per a documented past bug):** exit code is checked FIRST. A non-zero exit is always `FAIL`, even if stdout happens to contain a `SKIP:` line — this guards against silently masking a real failure as a skip.

**CMake wiring:** `SKIP_REGULAR_EXPRESSION "${PS3RECOMP_SKIP_REGEX}"` on `add_test()`, where `PS3RECOMP_SKIP_REGEX` is `"SKIP:"` **without** a leading `^` anchor — CMake's anchor binds to the start of the *entire captured output*, not per-line, so an anchored regex mis-classified `py.test_ppu_lift` as "Passed" instead of "Skipped" when its `SKIP:` line wasn't first. Fixed in `CMakeLists.txt` (see comments around line 280-322); `test_runtime.sh` also does a post-hoc cross-check between raw `SKIP:` line counts and CTest's own "Skipped" count as a regression guard, warning (not failing) on mismatch.

## Mocking / Fakes

No mocking framework. SPU/PPU test harnesses (`test_<name>_main.c`) use **channel-overriding stubs**: hand-written C functions that intercept SPU channel reads/writes to inject synthetic values instead of real hardware/runtime behavior — this is the project's substitute for a mock library, always hand-rolled per test.

`runtime/trace/tests/trace_link_stubs.h` provides shared link-time stubs for the four `ps3_checkpoint.c` functions needed by any harness linking `ps3_checkpoint.h`/`ps3_trace.h` instrumentation, so individual harnesses don't hand-roll duplicate stubs (a Task 4 post-review fix, per `run_tests.sh` comments).

## Coverage

No coverage tooling configured (no `.coveragerc`, no `gcov`/`llvm-cov` invocation found in scripts or CMakeLists). Coverage is not tracked or enforced.

## CI (`.github/workflows/ci.yml`)

**Platform policy (explicit human decision, documented in the workflow header):**
- **macOS** (`macos-latest`): **blocking** — the only platform continuously exercised so far
- **Linux** (`ubuntu-latest`): `continue-on-error: true` — non-blocking, exists to *discover* what breaks, not yet promoted to gating
- **Windows**: also non-blocking (same rationale) — check current `ci.yml` for the exact job name/runner if verifying

**Steps per job:** install Python 3.12 via `actions/setup-python`, export `$PYTHON` to the resolved interpreter, then run `scripts/test_python.sh` and `scripts/test_runtime.sh` directly — CI does not reimplement any test-discovery logic, it only calls the two scripts (single source of truth stays in the scripts + `CMakeLists.txt`).

**Deliberately excluded from CI:** `scripts/smoke_macos.sh` — known-broken (step 3 fails to link, missing symbols from `cellGcmSys.c`/`movie_vt_metal.m` not in its hardcoded compile list); documented as out of scope to fix, not silently made to pass.

## What Has NO Automated Coverage (be honest about this)

- **Full game boot** (GoW2 intro → WADs → shaders → gameplay pixels) — validated only by manual smoke runs against the sibling `../gow2-recomp` checkout, using scripts that live in *that* repo (`bt_intro_wads.sh`, `bt_visual_combo.sh`, `smoke_asset_pipeline.sh`, `smoke_rsx_spu.sh`, `smoke_intro_to_rsx.sh`, `run_basecase.sh`) — none of these are part of this repo's CTest/CI surface
- **`scripts/smoke_macos.sh`** (this repo) — known-broken, excluded from CI, run manually "at your own risk" per its own comment
- RSX/GCM rendering correctness (D3D12 backend on Windows, Metal backend on macOS) beyond the narrow `test_texture_deswizzle` unit test
- Lifter correctness against real game binaries (`tools/ppu_lifter.py`, `tools/spu_lifter.py`) beyond the synthetic instruction sequences in `runtime/spu/tests/gen_test_*.py` and the small Python self-tests (`test_ppu_lift.py`, `lift_selftest.py`) — no corpus-based regression testing against a real EBOOT.ELF is part of this repo's automated suite
- SPURS scheduler behavior beyond the specific `spurs_*` SPU test cases

## Common Patterns

**Python self-test shape** (see `tools/test_recomp_config.py`):
```python
def test_load_minimal(tmp_path: Path) -> None:
    cfg = load_config(...)
    assert cfg.main.elf_path.endswith("EBOOT.ELF"), cfg.main.elf_path
    assert cfg.optimizations.skip_lr is False
```
Plain functions named `test_*`, run directly via `python3.11 tools/test_recomp_config.py` (not pytest-discovered) — each file has its own runner logic (commonly a `main()`/`if __name__ == "__main__":` block that calls every `test_*` function and reports PASS/FAIL/SKIP itself).

**SPU harness shape** (`test_<name>_main.c`): define channel-override stubs → build the generated `spu_recomp.c` for that case → run it → `assert()` on expected channel/memory outcomes. Env-var gating is used inside some harnesses too, e.g. `test_spu_fma_precision_main.c` reads `PS3_SPU_FMA_ISA` to select which ISA path to exercise.

---

*Testing analysis: 2026-07-26*
