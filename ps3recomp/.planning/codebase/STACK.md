# Technology Stack

**Analysis Date:** 2026-07-26

## Languages

**Primary:**
- C17 (`CMAKE_C_STANDARD 17`) - Bulk of the HLE runtime libraries (`libs/*/*.c`) and lower-level runtime pieces (`runtime/memory/`, `runtime/syscalls/`, `runtime/spu/`)
- C++20 (`CMAKE_CXX_STANDARD 20`) - PPU boot scaffold and per-game recompiled code (`runtime/ppu/`, `runtime/host/`, lifter-generated `ppu_recomp_*.cpp`)
- Objective-C (enabled via `enable_language(OBJC)` in `CMakeLists.txt`, only `if(APPLE)`) - macOS-native backends: `libs/video/rsx_metal_backend.m`, `libs/video/movie_vt_metal.m`, `libs/input/cellpad_gamecontroller.m`, compiled with `-fobjc-arc`
- Python 3.11+ (`pyproject.toml`: `requires-python = ">=3.11"`) - the entire binary-analysis/recompilation pipeline lives in `tools/*.py` (ELF/PRX parsing, PPU/SPU disassembly and lifting, NID resolution)

**Secondary:**
- Bash (POSIX) - test harness/orchestration scripts (`scripts/test_all.sh`, `scripts/test_python.sh`, `scripts/test_runtime.sh`, `scripts/smoke_macos.sh`, `runtime/spu/tests/run_tests.sh`, `runtime/trace/tests/run_tests.sh`); CI (`.github/workflows/ci.yml`) forces `shell: bash` on every runner including Windows (relies on Git Bash)
- TOML - per-game recompiler configuration (`config/example.toml`, `templates/project/config.toml`), parsed by `tools/recomp_config.py` via stdlib `tomllib` (Python 3.11+)

## Runtime

**Environment:**
- Native compiled executables — no interpreter/VM at game runtime; only the Python tooling needs an interpreter, at recompile time
- Target platforms: macOS/arm64 (the only platform actually exercised and CI-blocking), Windows (MSVC), Linux (informational CI only, "not yet verified green" per `README.md`)

**Package Manager:**
- Python: `pip` (`pip install -r requirements.txt`); no lockfile — `requirements.txt` and `pyproject.toml` `[project.optional-dependencies].dev` both list zero actually-imported third-party packages as of the 2026-07-24 audit baked into `pyproject.toml`'s comments (grep-verified; see `.superpowers/sdd/task-F0-2-report.md`)
- C/C++: no package manager — dependencies are found via CMake `find_package`/`pkg_config` (SDL2, Vulkan, Threads) or linked as system/Apple frameworks; no vcpkg/Conan manifest present
- Lockfile: not applicable/missing (no `requirements.lock`, no `Cargo.lock`-equivalent)

## Frameworks

**Core:**
- CMake 3.20+ (`cmake_minimum_required(VERSION 3.20)`) - build system for the game-agnostic runtime static library `ps3recomp_runtime` (`CMakeLists.txt`)
- Per-game builds are NOT driven by the root CMakeLists: `runtime/ppu/` and `runtime/host/` are explicitly excluded from the library (`list(FILTER RUNTIME_SOURCES EXCLUDE REGEX ".../runtime/ppu/|/runtime/host/")`) and compiled per-title against lifter-generated `ppu_recomp.h` (see `docs/PPU_RECOMP.md`); the GoW2 port's actual build lives in the sibling repo `../gow2-recomp` (`build_macos.sh`)

**Testing:**
- CTest (CMake's test driver), gated behind `option(PS3RECOMP_BUILD_TESTS "Build unit tests" OFF)` in `CMakeLists.txt` — off by default
- Self-contained C/C++ test executables (no external test framework, plain `assert()`/`main()` harnesses): `runtime/ppu/tests/test_ppu_fpu_lmw.cpp`, `runtime/ppu/tests/test_ppu_blrl_call.cpp`, `libs/system/tests/test_autoload_map.c`, `libs/audio/tests/test_coreaudio_ring.c`, `libs/video/tests/test_texture_deswizzle.c`
- Python self-tests are plain scripts with `if __name__ == "__main__":`, not pytest (`tools/test_*.py`, `tools/lift_selftest.py`, `tools/spu_lift_selftest.py`, `runtime/ppu/tests/test_ppu_lifter_emit.py`) — no pytest/unittest runner is used
- Bash-driven generator suites: `runtime/spu/tests/run_tests.sh` (generates SPU test ELFs via `gen_test_*.py`, lifts them with `tools/spu_lifter.py`, compiles+runs — 21 cases), `runtime/trace/tests/run_tests.sh`

**Build/Dev:**
- Ninja - preferred CMake generator (`cmake -B build -G Ninja` in README quickstart; `build-macos` Ninja arm64 per `CLAUDE.md`)
- MSVC 19.x / GCC 12+ / Clang 15+ - supported C17/C++20 compilers (README prerequisites)
- LLVM - referenced (README "Building Blocks We Leverage") as intended path for optimized native codegen from lifted C; not itself vendored/invoked by the build scripts inspected

## Key Dependencies

**Critical:**
- SDL2 (version auto-detected via `find_package(SDL2)`/pkg-config) - required on all non-Windows hosts (`if(NOT WIN32)` in `CMakeLists.txt`) for pad/audio/window HLE backends; build fails with `FATAL_ERROR` if missing (macOS: `brew install sdl2 pkg-config`)
- XenonRecomp's PowerPC lifter (adapted, not vendored as a dependency) - `tools/ppu_lifter.py`/`tools/ppu_disasm.py` are adapted from the Xbox 360 PowerPC recompiler; `patches/xenonrecomp-ppu.patch` documents the delta
- stb_image / stb_image_write (vendored, `libs/codec/stb_image.h`, `libs/codec/stb_image_write.h`) - PNG/JPEG/GIF decode and encode backend for `cellPngDec`/`cellJpgDec`/`cellGifDec`/`cellPngEnc`/`cellJpgEnc`

**Infrastructure:**
- Vulkan (optional, `find_package(Vulkan QUIET)`) - RSX backend alternative (`libs/video/rsx_vulkan_backend.c`); silently dropped from the build if the SDK isn't found (native on Linux, via MoltenVK on macOS)
- Threads (`find_package(Threads REQUIRED)`, non-Windows only) - POSIX threading backing `sys_ppu_thread`, `sysPrxForUser` lwmutex/lwcond
- Python 3.11+ (`find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter)`) - only required at configure time when `PS3RECOMP_BUILD_TESTS=ON`, to run the Python self-tests via CTest

## Configuration

**Environment:**
- No project-level `.env` mechanism for the engine itself; the GoW2 port (sibling repo `../gow2-recomp`) drives runtime behavior through many `PS3_*` env vars (e.g. `PS3_VFS_ROOT`, `PS3_VM_LOW_MB`, `PS3_MOVIE_IO`, `PS3_RSX_BACKEND`) — documented in `CLAUDE.md`, not part of this repo's config surface
- `PS3RECOMP_BUILD_DIR` env var - overrides the CMake build directory used by `scripts/test_runtime.sh` and CI log collection

**Build:**
- `CMakeLists.txt` (root) - the single build config for the game-agnostic runtime library; key `option()`: `PS3RECOMP_BUILD_TESTS` (default OFF)
- `templates/project/CMakeLists.txt` - starter build file for new per-game ports
- `config/example.toml`, `templates/project/config.toml` - recompiler configuration format consumed by `tools/recomp_config.py`

## Platform Requirements

**Development:**
- Python 3.11+, CMake 3.20+, a C17/C++20 compiler (MSVC 19.x / GCC 12+ / Clang 15+) — per `README.md` prerequisites
- macOS: Xcode command-line tools (for Objective-C / Apple frameworks) + Homebrew SDL2/pkg-config

**Production:**
- Native x86-64 (and arm64 on macOS) executables per recompiled game; no ffmpeg/emulator dependency at runtime
- macOS/arm64 is the only platform currently exercised end-to-end and the sole CI-blocking job (`.github/workflows/ci.yml`); Windows and Linux CI jobs run with `continue-on-error: true` and are informational only

---

*Stack analysis: 2026-07-26*
