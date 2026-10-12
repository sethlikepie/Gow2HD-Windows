# Coding Conventions

**Analysis Date:** 2026-07-26

This is a static PS3 (PPC64 Cell / SPU) → native Windows/macOS-arm64 recompiler. Code spans four languages with distinct conventions: C17 (runtime/libs), C++20 (generated lift output + game host glue), Objective-C (`.m`, macOS Metal/AVFoundation), and Python 3.11+ (`tools/`). The canonical style reference is `CONTRIBUTING.md` (root) — this document expands it with what's actually observed in the code.

## Naming Patterns

**C files (`runtime/`, `libs/`):**
- `snake_case` for functions/variables: `vm_read32()`, `spu_channels_read()`
- `SCREAMING_CASE` for constants/macros: `CELL_OK`, `PS3_MAX_MODULES`
- HLE (High-Level Emulation) function names mirror the PS3 SDK verbatim: `cellAudioInit()`, `cellGcmSetFlipMode()`, `cellFsOpen()` — never renamed/adapted, so they match RPCS3/official docs for cross-referencing
- Static module-local state prefixed `s_`: `static int s_initialized;`, `static CellFooConfig s_config;`
- One-shot lazily-initialized flags follow a terse cached pattern seen throughout `runtime/ppu/*.cpp` and `runtime/spu/*.c`: `static int on=-1; if(on<0){on=getenv("PS3_TRACE_X")?1:0;}` — evaluate env var once, cache as `-1`(unset)/`0`/`1`.

**C++ (generated lift output, `runtime/ppu/`, `runtime/spu/`):**
- Lifted PPU functions: `func_<hexaddr>` (e.g. `func_002550C8`) — address-derived, never renamed even after understanding semantics (traceability to the original binary is load-bearing)
- Trampoline pointers for cross-fragment fallthrough fixes: `g_trampoline_fn`

**Python (`tools/`):**
- `snake_case` throughout, type hints encouraged (not strictly enforced everywhere)
- Test functions: `def test_<behavior>() -> None:` (see `tools/test_recomp_config.py`)
- Module-level CLI scripts use `if __name__ == "__main__":` — these are standalone scripts, not a pip-installable package (`pyproject.toml` declares no `[build-system]`)

**Objective-C (`libs/video/*.m`):**
- Static file-local state prefixed `s_`, same convention as C: `s_running`, `s_overlay_done`, `s_thr`
- Function-scope helpers named for what they gate: `hle_on()` mirrors the `PS3_*_on()` env-gate pattern from C

## Code Style

**Formatting:**
- No enforced formatter/linter config found (no `.clang-format`, `.eslintrc`, or `.flake8` at root) — style is convention-driven, not tool-enforced
- 4-space indentation, no tabs, in C/C++ (per `CONTRIBUTING.md`)
- LF line endings; Git handles CRLF conversion on Windows
- No trailing whitespace
- Keep HLE module files under ~500 lines when practical — self-contained per module

**Comments:**
- `/* C-style */` block comments for section headers, inline `//` or `/* */` for non-obvious logic
- Heavy use of long prose comments in Portuguese explaining *why* (not just what) for non-obvious fixes, especially in test/build scripts (`scripts/test_runtime.sh`, `scripts/test_python.sh`) — these document root-cause investigations, not just current behavior
- Comments frequently cite the originating task/round of investigation (e.g. "achado da revisão pós-review", "Task 4 Minor #3") — this is a deliberate practice of preserving debugging provenance inline, not decorative

## Import Organization

**C/C++:**
- Local header first (matching `.c`/`.cpp`'s own `.h`), then system/platform headers, then other project headers
- Example (`libs/video/movie_vt_metal.m`): Apple frameworks (`#import <AVFoundation/AVFoundation.h>`) → local headers (`#include "movie_vt_metal.h"`, `"rsx_metal_backend.h"`) → libc (`<pthread.h>`, `<stdio.h>`, `<string.h>`...)
- Include guards: `#ifndef PS3RECOMP_CELL_FOO_H` / `#define PS3RECOMP_CELL_FOO_H` / `#endif` (per-header, matches file's logical module name, prefixed `PS3RECOMP_`)
- `extern "C"` wraps every public HLE header so C++ game projects can include C runtime headers directly:
  ```c
  #ifdef __cplusplus
  extern "C" {
  #endif
  /* ... declarations ... */
  #ifdef __cplusplus
  }
  #endif
  ```

## Error Handling

**HLE module return codes:**
- Every HLE function returns `s32`; success is `CELL_OK`, failures are PS3-SDK-faithful negative error codes defined per-module: `CELL_FOO_ERROR_NOT_INIT (s32)0x80410001`
- Standard defensive checks at function entry: not-initialized guard, null-pointer guard (`CELL_EFAULT`), already-initialized guard — see the canonical example in `CONTRIBUTING.md`'s "Adding an HLE Module" section
- Fidelity rule (from `CLAUDE.md`): fixes must match original CELL/PPC behavior — never "fix" by diverging from console semantics, even when the recompiled behavior looks wrong

**Guest-code lift fixes:**
- Never hand-edit generated `ppu_recomp_XXX.cpp`/`spu_recomp.c` output permanently — any fix to lifted output must be re-expressed as an idempotent `patch_*.py` script (survives re-lift). This is a hard project rule from `CLAUDE.md`, not a style preference.

**Shell scripts (`scripts/*.sh`):**
- `set -eu` (or documented deliberate omission of `-e`, e.g. `scripts/test_all.sh` intentionally omits `-e` because it must run both sub-suites even if the first fails)
- Aggregator scripts capture sub-exit-codes explicitly via `if cmd; then RC=0; else RC=$?; fi` rather than relying on `$?` after a plain call (survives being sourced under an inherited `set -e`)
- Provenance-first cleanup: before any script auto-reverts generated/dirty files, it snapshots `git status` *before* running, and only touches paths whose dirtiness is provably new (see `scripts/test_runtime.sh`'s `BEFORE_DIRTY`/`NEW_DIRTY` comm-based diffing) — never blindly `git checkout --`/`rm -rf` on pattern match alone

## Instrumentation / Env-Var-Gated Tracing (first-class pattern)

The dominant cross-cutting convention in `runtime/` and `libs/` is **env-var-gated, lazily-cached debug tracing**, using the `PS3_*` prefix. This is required by `CLAUDE.md` rule 6: "probes/experiments sempre gated por env var, OFF por default."

**Canonical shape** (seen dozens of times in `runtime/ppu/ppu_loader.cpp`, `runtime/spu/spu_channels.c`, `runtime/ppu/ppu_hle.cpp`, `runtime/ppu/ppu_sysprx.cpp`):
```c
{ static int on=-1; if(on<0){extern char* getenv(const char*); on=getenv("PS3_TRACE_GIANTLOCK")?1:0;}
  if (on) printf("[giantlock] ...\n"); }
```
or as a named helper:
```c
static int spinread_on() { static int v=-1; if(v<0)v=getenv("PS3_TRACE_SPINREAD")?1:0; return v; }
```

**Naming convention for these vars:**
- `PS3_TRACE_<SUBSYSTEM>` — verbose tracing (e.g. `PS3_TRACE_SPURS`, `PS3_TRACE_ALLOC`, `PS3_TRACE_GIANTLOCK`, `PS3_TRACE_OOBRA`, `PS3_TRACE_XLEN`, `PS3_TRACE_HEAPCFG`, `PS3_TRACE_SHADERFMT`)
- `PS3_<FEATURE>` — behavior toggles, not just tracing (e.g. `PS3_COMMIT_HIGH`, `PS3_WATCH_SHELLA`, `PS3_SPU_HALT`, `PS3_SPU_TASK_IDLE_MS`, `PS3_MOVIE_HLE`, `PS3_VDEC_ASYNC`)
- `PS3_<MODULE>_<PROBE>` for module-scoped diagnostic probes (e.g. `PS3_SPUSYS_EXIT`, `PS3_SPU_BRPROBE`, `PS3_WAD_TEX_BIND`)

**Rules for adding a new one (inferred from existing code + CLAUDE.md):**
1. Default OFF (absence of var = disabled; presence, regardless of value or `!= '0'`, usually = enabled — check surrounding code, both idioms exist: `getenv(x) ? 1 : 0` and `v && v[0] && v[0] != '0'`)
2. Cache the lookup once in a `static int` seeded to `-1`, never call `getenv` on every hot-path invocation
3. Prefix `PS3_`; keep the subsystem name recognizable against the file/module it instruments
4. Log via `printf`/`fprintf(stderr, ...)` with a bracketed subsystem tag, e.g. `[movieio]`, `[giantlock]`, `[LDRSH]`, `[cellFoo]` — this tag convention is used for grepping logs during boot smoke runs (see `CLAUDE.md`'s smoke-run recipes)

## Function Design

**HLE modules:** thin — validate args, touch static module state, log, return `CELL_*`. Real work is either inline (simple modules) or delegates into `runtime/` for anything touching guest memory (`vm_read32`/`vm_write32` etc.) or host OS services (audio, filesystem, GPU).

**Lifted PPU/SPU code:** never hand-designed — one function per guest function at `func_<addr>`, mechanically generated by `tools/ppu_lifter.py` / `tools/spu_lifter.py`. Do not apply normal "small function" review pressure to this code; treat it as compiler output.

## Module Design

**HLE module layout** (`libs/<category>/cellFoo.{h,c}`):
- Header declares error codes, structs, function prototypes, wrapped in `extern "C"`
- Implementation is a flat `.c` file with `static` module state at top, functions in typical call/declaration order
- Registered into a game project via `reg_func(&mod_cellFoo, "cellFooInit", (void*)bridge_cellFooInit);` in that project's `hle_modules.cpp` — the NID/bridge registration is the seam between generic runtime and game-specific glue

**Categories under `libs/`:** `audio/`, `codec/`, `filesystem/`, `font/`, `hardware/`, `input/`, `misc/`, `network/`, `spurs/`, `sync/`, `system/`, `video/` — new HLE modules go in the matching category directory; new categories only when an existing one doesn't fit.

## Platform Portability Discipline (`#ifdef` conventions)

Cross-platform code (Windows/macOS/Linux) uses `#ifdef _WIN32` / `#ifdef __APPLE__` guards, not separate build trees, for most runtime/libs code. Documented pitfalls from `CLAUDE.md` (verify against actual file when touching):
- `cellFs`: Darwin's `struct stat` uses `st_atimespec`, not Linux's `st_atim`; `st_atime` may be undefined if macros were `#undef`ed for `CellFsStat`
- Never leave a bare Win32 `Sleep()` call in a POSIX code path — use a wrapper like `gcm_sleep_ms()` instead (see `libs/hardware`/GCM code)
- Never `#include <intrin.h>` unconditionally on Apple targets — SPU channel code (`runtime/spu/spu_channels.c`) must gate x86 intrinsics behind `#ifdef _WIN32` or use `__sync_*` GCC/Clang builtins instead
- `GetModuleHandleA` and other Win32-only calls must be `#ifdef _WIN32`-gated; POSIX equivalent is `dlopen(NULL)` where needed
- macOS-only source (`.m` files under `libs/video/`) is compiled only into the macOS host target; the CMake build excludes `runtime/ppu/` object files from the static library on macOS — the game's own build script (`build_macos.sh` in the sibling `gow2-recomp` repo) links those separately

**Objective-C (`libs/video/movie_vt_metal.m`, `libs/video/rsx_metal_backend.m`):**
- `#import` for Apple frameworks, `#include` for C/C++ project headers — both appear in the same file, consistently split by origin
- ARC-managed Obj-C objects held in C-style static globals are annotated inline (`/* __strong under ARC */`)
- Gated by the same `PS3_*` env-var pattern as C code (e.g. `PS3_MOVIE_HLE`)

## Python Conventions (`tools/`)

- Python ≥3.11 required (`pyproject.toml`, `requires-python = ">=3.11"`); the repo is **not** a pip-installable package — no `[build-system]` table, deliberately, because `tools/` is a set of standalone scripts, not an importable package
- Prefer stdlib over third-party deps: `pyproject.toml`'s `[project.optional-dependencies].dev` documents that (as of the 2026-07-24 audit) *zero* third-party packages are actually imported anywhere in `tools/*.py` — `requirements.txt` / `tools/requirements.txt` list `pycryptodome`, `capstone`, `construct`, `tabulate`, `tqdm` for backward-compat with existing `pip install -r` docs only, each annotated per-line as unused
- `tomllib` (stdlib since 3.11) used for TOML config parsing in `tools/recomp_config.py` — no `tomli` fallback needed given the 3.11 floor
- Self-tests are plain scripts with `assert`, not pytest-based (`if __name__ == "__main__":` + direct `assert` calls) — see Testing section

---

*Convention analysis: 2026-07-26*
