# Codebase Structure

**Analysis Date:** 2026-07-26

## Directory Layout

```
ps3recomp/                          # monorepo root: the engine (runtime/libs/tools)
├── tools/                          # offline lift/codegen pipeline (Python)
│   ├── ghidra/                     # Ghidra-based cross-reference oracle scripts
│   ├── harness/                    # test harness support for the tools suite
│   ├── shadertest/                 # RSX shader decompiler test fixtures
│   └── __pycache__/                # not source; compiled bytecode cache
├── runtime/                        # C/C++ runtime, compiled per-game and as shared lib
│   ├── ppu/                        # PPU context/loader — compiled PER-GAME, not in libps3recomp_runtime.a
│   ├── spu/                        # SPU context/DMA/workload + tests/ (out_*/spu_recomp.c)
│   ├── prx/                        # PRX (dynamic module) loader for the LLE path
│   ├── memory/                     # guest VM address space (vm.h)
│   ├── syscalls/                   # LV2 kernel syscall table + implementations
│   ├── trace/                      # checkpoint/OOB/mask tracing, env-var gated
│   └── host/                       # minimal game-agnostic test host (host_main.c) — excluded from lib
├── libs/                           # HLE reimplementation of ~100+ Cell OS libraries
│   ├── system/                     # cellSysutil, cellSysmodule, sysPrxForUser, cellRtc, ...
│   ├── filesystem/                 # cellFs, cellGame, cellSaveData, cellStorage
│   ├── input/                      # cellPad, cellKb, cellMouse, cellGem, cellpad_gamecontroller.m (macOS)
│   ├── audio/                      # cellAudio, cellAdec, libmixer, libsnd3, libsynth2
│   ├── video/                      # cellGcmSys, cellVdec, cellResc, cellVideoOut, cellVpost, RSX backend layer, movie overlay
│   ├── network/                    # cellNetCtl, cellHttp(s), cellSsl, sceNp*
│   ├── spurs/                      # cellSpurs, cellFiber, cellSpursJq, cellDaisy (SPU task management)
│   ├── sync/                       # cellSync, cellSync2 (cross-PPU/SPU synchronization)
│   ├── codec/                      # cellJpgDec, cellPngDec, cellGifDec, cellPamf, cellDmux, audio/video decoders
│   ├── font/                       # cellFont, cellFontFT, cellFreeType, cellL10n
│   ├── misc/                       # cellScreenshot, cellSubdisplay, cellImeJp, cellVideoExport, ...
│   └── hardware/                   # cellCamera, cellGem, cellUsbd
├── include/ps3emu/                 # shared cross-cutting headers (endian, error codes, NID, guest_call, ps3types)
├── config/                         # example.toml — recompiler pipeline configuration reference
├── templates/project/              # scaffold for starting a NEW per-title recompilation project
├── scripts/                        # top-level dev scripts (baseline_run.sh, smoke_macos.sh, test_*.sh)
├── docs/                           # extensive design docs, bring-up notes, per-subsystem references
│   ├── bringup/                    # GoW2 bring-up session notes
│   ├── notes/                      # misc dev notes
│   └── superpowers/                # execution plans (M0-M10 Metal stack, wall-chain plans, etc.)
├── gow2_gen/                       # checked-in GENERATED artifact for GoW2: ppu_hle_nids.cpp (NID import table)
├── games/gow2/                     # GoW2 port subtree (imported via `git subtree` from andrebrumdev/gow2-recomp)
│   ├── tools/, tests/, notes/      # GoW2-specific tooling/tests/notes
│   ├── lift_baseline/, spu_lifted/ # GoW2 lift artifacts checked into the subtree
│   ├── recomp_mid_v2/              # Windows build output tree (boot_v2_new.exe lives here per CLAUDE.md)
│   ├── boot_macos.cpp              # GoW2 macOS entry point (main())
│   ├── host_gow2_factory.cpp       # GoW2 host factory / wiring
│   ├── build_macos.sh, build3.sh   # GoW2 build scripts
│   └── CLAUDE.md                   # GoW2-specific operational guide (references this repo's CLAUDE.md)
├── build-macos/                    # local CMake/Ninja build directory (generated, not source)
├── .venv/                          # local Python virtualenv (generated, not source)
├── .planning/, .superpowers/       # GSD / SDD planning and execution ledgers
├── CMakeLists.txt                  # top-level build: libps3recomp_runtime.a + optional CTest suite
├── README.md                       # project pitch + architecture summary
└── CLAUDE.md                       # operational guide for AI agents working in this repo
```

**Note on absent directories:** `extracted/` (decrypted game assets, ~20GB) and real
per-title generated lift output (`ppu_recomp_*.cpp`) do not exist in this checkout —
they are gitignored and live in the sibling `../gow2-recomp` working tree per
`CLAUDE.md`. Do not assume they are simply "not yet mapped."

## Directory Purposes

**`tools/`:**
- Purpose: everything that runs offline, at recompile time, never linked into a
  game binary.
- Contains: ELF/SELF-wrapper parsing (`elf_parser.py` — parses only, does not decrypt), PPU
  disassembler/lifter (`ppu_disasm.py`, `ppu_lifter.py`), SPU
  disassembler/lifter (`spu_disasm.py`, `spu_lifter.py`, `extract_spu_images.py`,
  `find_spu_functions.py`, `wrap_spu_elf.py`), PRX analysis (`prx_analyzer.py`,
  `prx_exports.py`, `prx_relocate.py`), NID handling (`nid_database.py`,
  `gen_hle_nids.py`, `generate_stubs.py`), config parsing (`recomp_config.py`),
  and a large family of `test_*.py` unit tests plus audit/diagnostic scripts
  (`audit_boundaries.py`, `audit_fallthrough.py`, `objdump_audit.py`,
  `ghidra_lookup.py`, `baseline_report.py`).
- Key files: `tools/ppu_lifter.py`, `tools/spu_lifter.py`, `tools/elf_parser.py`,
  `tools/recomp_config.py`.

**`runtime/ppu/`:**
- Purpose: the PPU "virtual CPU" and guest-boot glue that lifted code links
  against; compiled per-game against that game's generated `ppu_recomp.h`.
- Contains: `ppu_context.h` (register file struct), `ppu_memory.h` (memory
  macros), `ppu_ops.h` (instruction op macros), `ppu_loader.cpp` (loader/boot/
  giant lock/`ppu_run`), `ppu_hle.cpp`/`ppu_imports.cpp`/`ppu_sysprx.cpp` (HLE
  import glue), `host_res_inflate.c`/`host_wad_tex.c` (GoW2-specific host-side
  asset helpers), `ppu_movie_eos_trace.c` (movie EOS trace probe), `tests/`.
- Key files: `runtime/ppu/ppu_loader.cpp`, `runtime/ppu/ppu_context.h`.

**`runtime/spu/`:**
- Purpose: SPU "virtual CPU" model (interpreter-by-default per
  `config/example.toml`), DMA, channels, SPURS-style workload dispatch.
- Contains: `spu_context.h`, `spu_channels.c`, `spu_dma.h`, `spu_helpers.h`,
  `spu_workload.c`/`.h`, `spu_lifted_job.h`, `spu_stop.c`, and `tests/` with a
  matching `_main.c` harness + checked-in generated `out_<scenario>/spu_recomp.c`
  for each self-contained scenario (`out_bringup`, `out_dma`, `out_event`,
  `out_lfqueue`, `out_shufb`, `out_spu_task_abi`, `out_sum`, `out_taskset`,
  `out_workload`, `out_adapter`, `out_brsl_return`, `out_spu_channel_blocking`).

**`runtime/syscalls/`:**
- Purpose: LV2 kernel syscall table and implementations.
- Contains: `lv2_syscall_table.h` (dispatch table), `lv2_register.c`,
  `sys_ppu_thread.c/.h`, `sys_mutex.c/.h`, `sys_cond.c/.h`, `sys_rwlock.c/.h`,
  `sys_semaphore.c/.h`, `sys_memory.c/.h`, `sys_vm.c/.h`, `sys_fs.c/.h`,
  `sys_event.c/.h`, `sys_timer.c/.h`, `spu_fallback.c`, `spu_lifted_fallback.c`.

**`runtime/trace/`:**
- Purpose: diagnostic instrumentation, always env-var gated and OFF by
  default — never a normal-path dependency.
- Contains: `ps3_checkpoint.c` (CHECKPOINTS/SNAPSHOT), `ps3_mem_trace.c`
  (memory/OOB), `ps3_mask_trace.c` (mask observation), `ps3_trace.c`
  (general trace core).

**`libs/`:**
- Purpose: HLE reimplementations of Cell OS PRX modules, grouped by category
  matching the RPCS3 module taxonomy referenced in `README.md`.
- Naming: one `.c`/`.h` pair per Cell module, named after its real PS3 module
  (e.g. `libs/video/cellGcmSys.c`, `libs/filesystem/cellFs.c`). Platform-specific
  Objective-C implementations use `.m` extension and live alongside their C
  siblings (`libs/video/rsx_metal_backend.m`, `movie_vt_metal.m`,
  `libs/input/cellpad_gamecontroller.m`).
- Each category directory has its own `tests/` subdirectory with self-contained
  unit tests that define their own `main()` (excluded from the shared static
  library build via `list(FILTER HLE_SOURCES EXCLUDE REGEX "/tests/")`).

**`libs/video/` (RSX + movie subsystem specifically):**
- `rsx_commands.c/.h`: NV47xx FIFO command processor → backend-agnostic
  `rsx_state`.
- `rsx_dispatch.c/.h`: dispatch sink abstraction over rsx_state.
- `rsx_d3d12_backend.c/.h`: Windows Direct3D12 implementation of `rsx_backend`.
- `rsx_metal_backend.m/.h`, `rsx_metal_deswizzle.h`, `rsx_metal_upscale.h`:
  macOS native Metal implementation of `rsx_backend` (Objective-C, ARC).
- `rsx_vulkan_backend.c/.h`: optional (only built if Vulkan SDK found).
- `rsx_null_backend.c/.h`: headless/no-op backend, explicitly NOT an
  acceptance target for "pixels of the game" per `CLAUDE.md`.
- `rsx_fp_decompiler.c/.h`, `rsx_vp_decompiler.c/.h`: fragment/vertex program
  → shader-source decompilers.
- `rsx_texture_deswizzle.h`, `rsx_texture_formats.h`, `rsx_vertex_formats.h`,
  `rsx_primitives.h`: format/layout tables shared by backends.
- `rsx_host_content.c/.h`, `rsx_live_draw.c/.h`, `rsx_frame_dump.c/.h`,
  `rsx_game_identity.h`: host-side content hold, live-draw probes, frame
  dumping for diagnostics.
- `movie_hle.c`, `movie_vt_metal.h/.m`: the `movie_hle_*` contract and its
  macOS VideoToolbox+Metal implementation.

**`include/ps3emu/`:**
- Purpose: small set of cross-cutting shared headers included from both
  `runtime/` and `libs/`.
- Contains: `endian.h`, `error_codes.h`, `guest_call.h`, `host_compat.h`,
  `module.h`, `nid.h`, `ps3types.h`, `spu_fallback.h`.

**`config/`:**
- Purpose: reference recompiler configuration.
- Contains: `example.toml` — documents every option (input ELF, output dir,
  optimization level, `[modules]` HLE/LLE strategy per Cell library, memory
  layout, threading, graphics backend selection).

**`templates/project/`:**
- Purpose: starter scaffold for bootstrapping a brand-new per-title
  recompilation project outside this repo.
- Contains: `CMakeLists.txt`, `config.toml`, `main.cpp`, `stubs.cpp`.

**`gow2_gen/`:**
- Purpose: the one generated GoW2 artifact that IS checked into this repo
  (unlike the rest of the lift output).
- Contains: `ppu_hle_nids.cpp` — the NID→HLE-stub import table generated by
  `tools/gen_hle_nids.py`.

**`games/gow2/`:**
- Purpose: the GoW2 port itself, imported into this monorepo via `git subtree`
  from `andrebrumdev/gow2-recomp` (mirror repo). Day-to-day GoW2 development
  physically happens in the sibling `../gow2-recomp` repo (owns the ~20GB
  non-versioned game data) and is synced in via `git subtree pull`/`push` —
  see `CLAUDE.md` "Estrutura de monorepo" section.
- Contains: `boot_macos.cpp` (real `main()`), `host_gow2_factory.cpp`, build
  scripts (`build_macos.sh`, `build3.sh`, `build_boot_gow.sh`,
  `build_boot_hle.sh`), diagnostic/attach scripts (`attach_mem*.sh`,
  `attach_threads.sh`, `diag_hang.sh`), `tools/`, `tests/`, `notes/`,
  `lift_baseline/`, `spu_lifted/`, `recomp_mid_v2/` (Windows build tree), its
  own `CLAUDE.md`.

**`docs/`:**
- Purpose: extensive prose design/reference documentation — treat as the
  human-facing counterpart to these generated `.planning/codebase/` docs, and
  cross-check against it (e.g. `docs/ARCHITECTURE.md`, `docs/PPU_RECOMP.md`,
  `docs/RUNTIME.md`, `docs/RSX_GRAPHICS.md`, `docs/SPURS_BRINGUP.md`,
  `docs/SPU_LIFTER.md`, `docs/PLATFORM_ABSTRACTION.md`,
  `docs/GOW2_BOOT_STATE.md`, `docs/gow2-asset-pipeline-map.md`,
  `docs/MACOS_PORT_PLAN.md`).
- `docs/superpowers/plans/`: dated, checkbox-tracked execution plans (Metal
  stack M0–M10, wall-chain bring-up plans referenced in `CLAUDE.md`).

## Key File Locations

**Entry Points:**
- `games/gow2/boot_macos.cpp:358`: real macOS `main()` for GoW2.
- `runtime/host/host_main.c:135`: minimal game-agnostic test host `main()`.
- `tools/ppu_lifter.py`, `tools/spu_lifter.py`: offline lifter CLI entry points.

**Configuration:**
- `CMakeLists.txt`: root build (static library, tests, platform link flags).
- `config/example.toml`: recompiler pipeline configuration reference.
- `templates/project/config.toml`: starter config for a new title.

**Core Logic:**
- `runtime/ppu/ppu_loader.cpp`: guest VM boot, function registry, giant lock.
- `libs/video/rsx_commands.c`: RSX command decode / state tracking.
- `libs/video/rsx_d3d12_backend.c`, `libs/video/rsx_metal_backend.m`: platform
  draw backends.
- `libs/video/movie_hle.c`: movie overlay contract + platform dispatch.

**Testing:**
- `runtime/*/tests/`: per-subsystem C/C++ self-contained tests (own `main()`).
- `tools/test_*.py`: Python unit tests for the offline pipeline.
- `libs/*/tests/`: per-HLE-module unit tests.
- `scripts/test_all.sh`, `scripts/test_python.sh`, `scripts/test_runtime.sh`,
  `scripts/smoke_macos.sh`, `scripts/baseline_run.sh`: top-level test/smoke
  entry points.

## Naming Conventions

**Files:**
- HLE library modules: named exactly after the PS3 module they reimplement,
  in Cell's own CamelCase (`cellGcmSys.c`, `sysPrxForUser.c`, `sceNpCommerce2.c`).
- Runtime subsystem files: `snake_case` prefixed by subsystem
  (`ppu_loader.cpp`, `spu_channels.c`, `sys_ppu_thread.c`, `vm.h`).
- Generated lift output (when present): `ppu_recomp_XXX.cpp`/`.h`,
  `spu_recomp.c`/`.h` — `XXX` is a shard/chunk index for large titles.
- Objective-C platform files: `.m` extension, same base name pattern as their
  C/Metal-agnostic counterpart when one exists (`rsx_metal_backend.m` pairs
  with `rsx_metal_backend.h`; `movie_vt_metal.m`/`.h`).
- Tests: `test_<subject>.c`/`.cpp`/`.py`; SPU test harnesses additionally use
  `test_<scenario>_main.c` to distinguish the harness entry point from any
  shared helper file in the same directory.
- Patch/idempotent-fix scripts for gitignored generated code (per CLAUDE.md):
  `patch_<description>.py` (e.g. `patch_fallthrough_2550c8.py`), committed even
  though the file they patch is not.

**Directories:**
- `libs/<category>/` groups modules by functional area (audio, video, input,
  network, filesystem, system, spurs, sync, codec, font, misc, hardware), not
  by original PS3 SDK path.
- `<subsystem>/tests/` is the consistent pattern for co-located self-contained
  tests across both `runtime/` and `libs/`.
- `games/<title>/` is the fixed pattern for per-title ports in the monorepo.

## Where to Add New Code

**New HLE module (Cell library reimplementation):**
- Implementation: `libs/<category>/<cellModuleName>.c` (+ matching `.h`),
  choosing `<category>` by functional area per the table above.
- Register NIDs via `tools/nid_database.py` / regenerate via
  `tools/gen_hle_nids.py` so the lifter's import resolution finds it.
- Tests: `libs/<category>/tests/test_<module>.c`.
- Add module strategy entry to `config/example.toml` `[modules]` if it should
  default to `"hle"`.

**New RSX backend (new host graphics API):**
- Implement the full `rsx_backend` vtable (`libs/video/rsx_commands.h:323`) in
  `libs/video/rsx_<name>_backend.c` (or `.m` for Objective-C), following the
  `rsx_d3d12_backend.c` "state tracking → callback → backend" comment pattern.
- Wire conditional compilation/linking into `CMakeLists.txt` (see the
  `if(APPLE)`/`find_package(Vulkan)` blocks for the existing pattern).

**New runtime subsystem (PPU/SPU/memory/syscall primitive):**
- PPU-side additions used by lifted code: `runtime/ppu/` (careful — this
  directory is excluded from `libps3recomp_runtime.a` and compiled per-game;
  changes here affect every title's per-game build).
- SPU-side additions: `runtime/spu/`.
- New LV2 syscalls: `runtime/syscalls/sys_<name>.c/.h`, registered in
  `runtime/syscalls/lv2_syscall_table.h`.
- Guest memory primitives: `runtime/memory/vm.h`.

**New offline tool/analysis pass:**
- `tools/<name>.py`, with a paired `tools/test_<name>.py` if it has
  deterministic, non-ELF-dependent behavior to unit test (see the
  `PS3RECOMP_BUILD_TESTS` CTest wiring in `CMakeLists.txt` for how Python
  self-tests get registered).

**New per-title port:**
- `games/<title>/`, imported via `git subtree` from a dedicated per-title
  mirror repo, following the GoW2 pattern documented in root `CLAUDE.md`
  ("Estrutura de monorepo (games/)").

**Shared cross-cutting types/macros:**
- `include/ps3emu/` — keep this set small; it's included from both `runtime/`
  and `libs/`.

## Special Directories

**`build-macos/`:**
- Purpose: local CMake/Ninja build output.
- Generated: Yes.
- Committed: No (build artifact directory).

**`.venv/`:**
- Purpose: local Python virtualenv for the `tools/` pipeline (Python ≥3.11
  required, per `CMakeLists.txt` comment referencing `pyproject.toml`).
- Generated: Yes.
- Committed: No.

**`gow2_gen/`:**
- Purpose: the single exception to "generated lift output is gitignored" —
  the GoW2 HLE NID import table is checked in.
- Generated: Yes (by `tools/gen_hle_nids.py`).
- Committed: Yes.

**`runtime/spu/tests/out_*/`:**
- Purpose: checked-in generated `spu_recomp.c`/`.h` per synthetic SPU test
  scenario (not from a real game) — the one place generated lift output lives
  in version control besides `gow2_gen/`.
- Generated: Yes (by `tools/spu_lifter.py` via `runtime/spu/tests/run_tests.sh`
  / `gen_test_*.py`).
- Committed: Yes.

**`games/gow2/recomp_mid_v2/`:**
- Purpose: Windows build/output tree for the GoW2 boot executable
  (`boot_v2_new.exe`), referenced directly in root `CLAUDE.md`'s build/run
  recipe.
- Generated: Partially (build artifacts mixed with checked-in build scripts).
- Committed: Mixed — check `.gitignore` before assuming a file here is tracked.

**`.planning/`, `.superpowers/`:**
- Purpose: GSD (this tool's) planning documents and SDD (Spec-Driven
  Development) execution ledgers (`.superpowers/sdd/progress.md` is the
  cross-session ledger referenced throughout `CLAUDE.md`).
- Generated: Partially (created/updated by planning tools, hand-edited too).
- Committed: Yes.

---

*Structure analysis: 2026-07-26*
