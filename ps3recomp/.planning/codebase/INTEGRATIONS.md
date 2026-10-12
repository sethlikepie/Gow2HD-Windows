# External Integrations

**Analysis Date:** 2026-07-26

This project has no web/SaaS integrations in the conventional sense. "Integrations"
here means (a) host-platform native APIs the runtime binds to per-OS, and (b) the
guest-side PS3 firmware surface (PS3 system libraries / LV2 syscalls) that this
project reimplements as HLE (high-level emulation) so recompiled games link and run
without a PS3 kernel.

## Host Graphics Backends (RSX translation)

**Windows:**
- Direct3D 12 - `libs/video/rsx_d3d12_backend.c` / `.h`; device FL 11.0, DXGI swap chain, vertex upload with BE byte-swap, DrawInstanced, texture format decode (per README changelog, ~25 RSX→DXGI mappings)

**macOS (native, per `CLAUDE.md` mandate — Metal preferred over MoltenVK where feasible):**
- Metal - `libs/video/rsx_metal_backend.m` (Objective-C, `-fobjc-arc`), linked via CMake against `Metal`, `MetalFX`, `QuartzCore`, `Foundation`, `AppKit` frameworks (`if(APPLE)` block in `CMakeLists.txt`)
- Supporting headers: `libs/video/rsx_metal_deswizzle.h` (texture deswizzle), `libs/video/rsx_metal_upscale.h` (MetalFX upscale)

**Cross-platform fallback/optional:**
- Vulkan - `libs/video/rsx_vulkan_backend.c` / `.h`; optional (`find_package(Vulkan QUIET)`), native on Linux, MoltenVK-backed on macOS; excluded from the build entirely if the SDK is absent
- Null/window backend - `libs/video/rsx_null_backend.c` / `.h`; Win32 GDI window, used to prove the RSX command pipeline without a full GPU backend

**Shared RSX command layer (backend-agnostic, feeds all of the above):**
- `libs/video/rsx_commands.c` / `.h` - NV47xx GPU command buffer parser and state tracker
- `libs/video/rsx_fp_decompiler.c` / `.c`, `rsx_vp_decompiler.c` - RSX fragment/vertex program → host shader translation
- `libs/video/rsx_dispatch.c`, `rsx_live_draw.c`, `rsx_frame_dump.c`, `rsx_host_content.c`, `rsx_game_identity.h` - draw dispatch, live-draw capture/replay, frame dumping, host-side content identity

## Media Decode / Video Playback

**macOS (native):**
- VideoToolbox / CoreMedia / CoreVideo - `libs/video/movie_vt_metal.m` + `libs/video/movie_vt_metal.h`; linked via `AVFoundation`, `CoreMedia`, `CoreVideo` frameworks (`if(APPLE)` in `CMakeLists.txt`); per `CLAUDE.md` this is used to decode the GoW2 intro `.m2v` without shelling out to `ffmpeg`

**Cross-platform / HLE:**
- `libs/video/movie_hle.c` / `.h` - movie playback HLE glue shared across backends
- `libs/codec/cellVdec.c` / `cellAdec.c` - guest video/audio decoder API surface; per README, actual decode is a stub (PICOUT/PCMOUT callbacks fire, but pixel/sample data needs FFmpeg — marked "Partial" in module status)

## Audio

**macOS (native):**
- CoreAudio / AudioToolbox - linked via `AudioToolbox`, `CoreAudio` frameworks (`if(APPLE)`); ring-buffer glue in `libs/audio/coreaudio_ring.h` (unit-tested via `libs/audio/tests/test_coreaudio_ring.c`)

**Windows:**
- WASAPI - backend for `libs/audio/cellAudio.c` (per README: "WASAPI/SDL2 backends, background mixing thread @ 5.33ms")

**Cross-platform fallback:**
- SDL2 audio - non-Windows fallback for `cellAudio.c`

## Input

**macOS (native):**
- GameController framework - `libs/input/cellpad_gamecontroller.m` (Objective-C), linked via `GameController` and `CoreHaptics` frameworks (`if(APPLE)`)

**Windows:**
- XInput - gamepad backend for `cellPad`, linked via `xinput` (Win32-only `target_link_libraries`)

**Cross-platform fallback:**
- SDL2 - non-Windows input backend (pad/keyboard/mouse)

## Windowing

- SDL2 - non-Windows window creation (`find_package(SDL2)` / pkg-config, hard-required per `CMakeLists.txt`)
- Win32 native window - used by `rsx_null_backend.c` and presumably the D3D12 backend on Windows

## Networking (guest-facing, HLE only — no real backend service)

- BSD sockets (POSIX) / Winsock2 - `libs/network/sysNet.c` wraps native sockets for the guest `sys_net` syscall family
- `libs/network/cellHttp.c` - real HTTP/1.1 client over native sockets (DNS via `getaddrinfo`, TCP connect, header parsing) — used by guest games, not by this project's own tooling
- `libs/network/cellHttps.c` - TLS stub with cert management (no real TLS handshake per README)
- `libs/network/cellSsl.c` - SSL/TLS lifecycle stub; RNG via `BCryptGenRandom` (Windows) / `/dev/urandom` (POSIX)
- `libs/network/sceNp*.c`, `sceNpTrophy.c`, `sceNpMatching2.c`, `sceNpSignaling.c`, `sceNpCommerce.c`/`sceNpCommerce2.c` - PlayStation Network surface, entirely offline/stubbed (no real PSN connectivity)

## Data Storage

**Databases:** None — no SQL/NoSQL database anywhere in the stack.

**File Storage:**
- Local filesystem only. `libs/filesystem/cellFs.c` / `cellFsUtility.c` implement the guest `cellFs` API with real host file I/O and configurable PS3-path→host-path mapping.
- `libs/system/cellSaveData.c` - callback-driven save/load flow against local files (PARAM.SFO), no cloud save backend.
- `libs/network/sceNpTrophy.c` - persistent trophy state via local JSON storage (per README changelog v0.2.0), not a PSN server.

**Caching:** None (no Redis/Memcached-equivalent).

## Authentication & Identity

- No real auth provider. `sceNp*` modules simulate a configurable "PSN identity" entirely locally/offline — there is no network call to Sony or any identity provider.

## Guest Firmware Surface Reimplemented as HLE (the actual "integration" this project ships)

This is the PS3 system-library surface the runtime provides so recompiled EBOOTs link
and run. Organized by `libs/` subdirectory (see `docs/MODULE_STATUS.md` /
`docs/MODULES_REFERENCE.md` for the full 97+-module inventory):

| Category | Directory | Representative modules |
|----------|-----------|------------------------|
| LV2 kernel / threading / memory | `runtime/syscalls/` | `sys_ppu_thread.h`, `sys_mutex.h`, `sys_memory.h`, `sys_fs.h`, `sys_prx.h`, `lv2_syscall_table.h` |
| System / core runtime | `libs/system/` | `sysPrxForUser.c`, `cellSysutil.c`, `cellSysmodule.c`, `cellRtc.c`, `cellSaveData.c`, `cellGame.c`, `cellUserInfo.c`, `cellMsgDialog.c` |
| Filesystem | `libs/filesystem/` | `cellFs.c`, `cellFsUtility.c` |
| Graphics | `libs/video/` | `cellGcmSys.c`, `cellVideoOut.c`, `cellResc.c` (see RSX backends above) |
| Input | `libs/input/` | `cellPad`, `cellKb`, `cellMouse`, `cellGem` |
| Audio | `libs/audio/` | `cellAudio.c`, `cellMic.c`, `cellVoice.c` |
| Network | `libs/network/` | see Networking section above |
| SPURS (SPU task management) | `libs/spurs/` | `cellSpurs.c`, `cellSpursJq.c`, `cellDaisy.c`, `cellFiber.c`, `cellSpursLFQueue.c`, `spurs_kernel.c` |
| Sync | `libs/sync/` | `cellSync`, `cellSync2` |
| Codec / media | `libs/codec/` | `cellPngDec.c`, `cellJpgDec.c`, `cellGifDec.c`, `cellPamf.c`, `cellDmux.c`, `cellVdec.c`, `cellAdec.c`, `cellSail.c` |
| Font | `libs/font/` | `cellFont`, `cellFontFT`, `cellFreeType`, `cellL10n` |
| Misc/export | `libs/misc/` | `cellScreenshot`, `cellSubdisplay`, `cellImeJp`, `cellVideoExport`, `cellGameRecording` |
| Hardware | `libs/hardware/` | `cellUsbd`, `cellCamera`, `cellGem` (stubs, no real devices) |

Behavioral reference for all of the above: RPCS3 (https://github.com/RPCS3/rpcs3),
cited throughout `docs/` as "the primary reference for HLE module behavior and system
call semantics."

## Build-Time Toolchain Integrations

- CMake 3.20+ / Ninja - `CMakeLists.txt`, invoked as `cmake -B build -G Ninja && cmake --build build`
- Python 3.11+ recompilation pipeline (`tools/`) - not a runtime dependency of shipped games, but the mandatory build-time toolchain: `elf_parser.py` → `ppu_disasm.py`/`spu_disasm.py` → `ppu_lifter.py`/`spu_lifter.py` → generated C/C++ linked against `ps3recomp_runtime`
- IDA Pro (optional, `tools/ida_analyze.py`) - imports `idapro`/`ida_funcs`/`ida_auto`/`idautils` from IDA's embedded Python (idalib); not a pip package, opportunistic integration only when IDA is present
- Ghidra (optional) - `tools/ghidra/`, `tools/ghidra_analyze.py`, `tools/ghidra_lookup.py`, `tools/ghidra_names.py` — cross-reference tooling against Ghidra analysis output

## CI/CD & Deployment

**Hosting:** None — this is a library/toolkit repo, not a deployed service.

**CI Pipeline:**
- GitHub Actions - `.github/workflows/ci.yml`; three jobs: `macos` (blocking — the only platform exercised), `linux` and `windows` (both `continue-on-error: true`, informational only, "not yet verified green")
- Each job: `actions/checkout@v4` → `actions/setup-python@v5` (pins Python 3.12, satisfying the `>=3.11` minimum) → runs `scripts/test_python.sh` and `scripts/test_runtime.sh` (which drives `cmake` configure/build/`ctest`)
- No deployment/release/publish step in the workflow — CI is test-only

## Environment Configuration

**Required env vars (this repo's own build/test, not the GoW2 game runtime):**
- `PYTHON` - overrides Python interpreter discovery in `scripts/test_python.sh` / `scripts/test_runtime.sh` (set explicitly in CI from `actions/setup-python`'s resolved interpreter)
- `PS3RECOMP_BUILD_DIR` - overrides the CMake build directory location for `scripts/test_runtime.sh`

**Secrets location:** None found. No `.env`, credentials, or API-key files detected in the repo.

## Webhooks & Callbacks

**Incoming:** None (no HTTP server exposed by this project).

**Outgoing:** None from the tooling itself. The guest `cellHttp`/`cellHttps` HLE modules let recompiled *games* make outbound HTTP requests, but this project does not itself call out to any service.

---

*Integration audit: 2026-07-26*
