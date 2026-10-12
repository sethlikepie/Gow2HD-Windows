# GoW2 Runtime Overlay Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a native in-game settings and diagnostics overlay for the GoW2 host on Windows D3D12 and macOS Metal, opened with F1 or the controller Select+Start chord (amended in Task 6: a lone Start is GoW2's pause and must reach the game; the menu always starts closed).

**Architecture:** A shared overlay core owns UI state, settings, input capture, diagnostics, and bounded log history. D3D12 and Metal consume the same UI draw data through backend-native render passes; the GoW2 host supplies runtime configuration and WAD/shader diagnostic providers. Settings are persisted in a versioned per-user file and are only presented as live controls when a real consumer applies them.

**Tech Stack:** C17 shared runtime modules, Dear ImGui, native D3D12 and Objective-C/Metal renderers, SDL2 events/controller state where already used, platform configuration-directory APIs, CTest and GoW2 host smoke scripts.

**Spec:** `docs/superpowers/specs/2026-09-23-gow2-runtime-overlay-design.md`

## Global Constraints

- Keep D3D12/Win32 code out of the macOS target; Metal rendering is native and does not use SDL_Renderer.
- Keep UI draws separate from guest RSX draw counters and guest GPU state.
- Use measured values; report unavailable data as unavailable rather than zero.
- Preserve existing input and rendering behavior while the overlay is closed.
- Keep current user changes in `libs/audio/cellAudio.c`, `runtime/spu/spu_workload.c`, and sibling checkout `g2audio` untouched.
- GoW2 day-to-day port changes belong in `../gow2-recomp`; engine/runtime changes belong in this monorepo. Never stage game data or use `git add .` / `git add -A`.
- Do not report overlay visibility as evidence that natural game pixels rendered.

## Review Focus

- A held Select+Start chord or focus loss must not repeatedly toggle/capture input — Task 3 tests edge detection, release, and focus reset.
- Malformed, truncated, unknown-version, and out-of-range settings must fall back to defaults without losing unrelated valid settings — Task 2 tests validation and recovery.
- Overlay input must not reach `cellPadGetData` as gameplay input while the menu is active, and must return unchanged when closed — Task 3 tests both modes.
- Overlay rendering must not increment guest draw metrics or disturb movie/content presentation — Tasks 4 and 5 verify pass ordering and guest counter snapshots.
- Missing WAD/shader providers and log-buffer contention must remain safe and visibly unavailable — Task 6 tests missing-provider rendering and bounded log publication.

---

## File map

| File | Responsibility |
|---|---|
| `third_party/imgui/` | Pinned Dear ImGui core sources and upstream license; no platform renderer backends are compiled from upstream. |
| `libs/video/rsx_overlay.h`, `rsx_overlay.c` | Shared overlay API, open/close state, key/controller navigation, per-frame diagnostics snapshot, Dear ImGui widgets and settings callbacks. |
| `libs/video/rsx_overlay_settings.h`, `rsx_overlay_settings.c` | Versioned settings model, validation, platform user-config path, atomic load/save. |
| `libs/video/rsx_overlay_log.h`, `rsx_overlay_log.c` | Fixed-capacity, thread-safe recent-log ring and snapshot API. |
| `libs/video/rsx_overlay_d3d12.c` | ImGui draw-data upload and alpha-blended pass in D3D12 frame command list. |
| `libs/video/rsx_overlay_metal.m` | ImGui draw-data upload and alpha-blended pass in the Metal drawable command buffer. |
| `libs/input/cellPad.c`, `libs/input/cellPad.h` | Apply configured host-to-guest mapping/deadzone and let the overlay consume host state before guest delivery. |
| `libs/video/rsx_d3d12_backend.c`, `rsx_metal_backend.m` | Backend lifecycle, event forwarding, render pass, frame metrics, and native settings consumers. |
| `games/gow2/boot_macos.cpp` and `../gow2-recomp/boot_macos.cpp` | Load runtime settings before backend setup; register GoW2 WAD/shader state providers; pass config path. |
| `games/gow2/gow2_launcher.py` and `../gow2-recomp/gow2_launcher.py` | Preserve setup JSON compatibility and optionally pass the runtime settings path. |
| `CMakeLists.txt` | Pin/build ImGui core, select native renderer source by platform, add focused tests and link requirements. |

## Interfaces shared between tasks

Add `libs/video/rsx_overlay.h` with these stable interfaces:

```c
typedef struct rsx_overlay_settings rsx_overlay_settings;
typedef struct rsx_overlay_snapshot {
    double fps;
    double frame_ms;
    uint64_t guest_draws_frame;
    uint64_t guest_draws_total;
    int wad_state_available;
    char wad_state[96];
    int shader_stats_available;
    uint32_t shader_hits, shader_misses, shader_compiles, shader_failures;
} rsx_overlay_snapshot;

typedef struct rsx_overlay_backend_metrics {
    double fps;
    double frame_ms;
    uint64_t guest_draws_frame;
    uint64_t guest_draws_total;
} rsx_overlay_backend_metrics;

int  rsx_overlay_init(const char* settings_path);
void rsx_overlay_shutdown(void);
int  rsx_overlay_visible(void);
int  rsx_overlay_handle_key(int key, int down, unsigned modifiers);
int  rsx_overlay_handle_pad(uint16_t host_buttons, uint16_t* guest_buttons);
void rsx_overlay_focus_lost(void);
void rsx_overlay_set_game_provider(
    void (*provider)(rsx_overlay_snapshot* out, void* userdata), void* userdata);
void rsx_overlay_publish_backend_metrics(
    const rsx_overlay_backend_metrics* metrics);
void rsx_overlay_build_draw_data(void);
```

`rsx_overlay_build_draw_data()` runs on the window/event thread. The core copies Dear ImGui vertices, indices, and draw commands into an immutable bounded packet; backend renderers consume the latest packet without touching ImGui state. This permits Metal's decode-thread movie present to composite the last UI frame safely. `rsx_overlay_handle_*` returns nonzero when that input was consumed. `rsx_overlay_publish_backend_metrics()` updates backend-owned metrics; `rsx_overlay_set_game_provider()` supplies GoW2-specific WAD/shader values without either producer overwriting the other's fields.

The settings structure is defined in `rsx_overlay_settings.h` and contains version, overlay enabled/diagnostics visible, toggle key, window dimensions/fullscreen, VSync, analog deadzone, and the host-button-to-guest-button map. Store backend capabilities separately so unsupported fields render as unavailable/restart-required.

## Task 1: Pin Dear ImGui and define portable build integration

**Files:**
- Create: `third_party/imgui/` (upstream core source, license, and pinned revision note)
- Modify: `CMakeLists.txt`
- Test: configure-only platform checks using existing `build-macos` plus a Windows cross/host configure where available

- [ ] Confirm the exact upstream Dear ImGui release and revision; record its license and immutable revision in `third_party/imgui/README.ps3recomp.md`.
- [ ] Import only core ImGui files needed by `imgui.cpp`, `imgui_draw.cpp`, `imgui_tables.cpp`, `imgui_widgets.cpp`; do not compile SDL, Vulkan, OpenGL, DX11, or Metal sample backends.
- [ ] Add a `ps3recomp_imgui` target and link it into `ps3recomp_runtime` without changing default runtime feature selection.
- [ ] Add Objective-C Metal renderer source only under `APPLE`; add D3D12 renderer source only under `WIN32`.
- [ ] Configure `build-macos` and inspect generated compile/link commands to prove no Windows renderer or SDK is selected; configure Windows and prove the Metal source is excluded.
- [ ] Commit only the dependency and CMake files with a pt-BR commit message.

## Task 2: Settings model, validation, and persistence

**Files:**
- Create: `libs/video/rsx_overlay_settings.h`, `libs/video/rsx_overlay_settings.c`
- Create: `libs/video/tests/test_rsx_overlay_settings.c`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces `rsx_overlay_settings` and:

```c
void rsx_overlay_settings_defaults(rsx_overlay_settings* out);
int rsx_overlay_settings_load(rsx_overlay_settings* out, const char* path);
int rsx_overlay_settings_save(const rsx_overlay_settings* settings, const char* path);
int rsx_overlay_settings_validate(rsx_overlay_settings* settings);
```

- [ ] Write tests for defaults, valid round-trip, invalid enum/range fallback, truncated file, unknown version, and invalid/non-bijective button maps.
- [ ] Run the focused test and confirm the expected failures before implementation.
- [ ] Implement a bounded versioned `key=value` format with strict numeric parsing; unknown keys are ignored and invalid values retain defaults.
- [ ] Save through a sibling temporary file and atomic rename; preserve the old file if writing or rename fails.
- [ ] Resolve the default per-user directory natively: Windows `%APPDATA%/ps3recomp/gow2/` and macOS `~/Library/Application Support/ps3recomp/gow2/`. An explicit path overrides the default.
- [ ] Run the focused test and the existing relevant CTest target; inspect file permissions and ensure no game-data path is used.
- [ ] Commit settings code and tests.

## Task 3: Shared overlay core, menu input, remapping, and log ring

**Files:**
- Create: `libs/video/rsx_overlay.h`, `rsx_overlay.c`
- Create: `libs/video/rsx_overlay_log.h`, `rsx_overlay_log.c`
- Create: `libs/video/tests/test_rsx_overlay.c`, `test_rsx_overlay_log.c`
- Modify: `libs/input/cellPad.h`, `cellPad.c`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes Task 2 settings APIs.
- Produces the shared interfaces listed above, plus:

```c
void rsx_overlay_set_snapshot_provider(
    void (*provider)(rsx_overlay_snapshot* out, void* userdata), void* userdata);
void rsx_overlay_log_write(int level, const char* message);
size_t rsx_overlay_log_snapshot(char* dst, size_t capacity);
void cellPad_set_host_mapping(const uint16_t guest_button_for_host_bit[16],
                              uint8_t deadzone);
```

- [ ] Write tests for F1 edge toggling, Select+Start chord rising-edge toggling (Task 6 amendment; lone Start reaches the guest), focus-loss reset, menu navigation consumption, the chord not leaking into guest input, and unchanged input when hidden.
- [ ] Write tests for mapping application and deadzone boundary behavior using the real host-to-guest conversion helper.
- [ ] Write log-ring tests proving bounded capacity, newest-entry retention, safe concurrent publication/snapshot, and truncation of oversized entries.
- [ ] Run the focused tests and confirm they fail for the missing APIs/behavior.
- [ ] Implement the shared UI tabs: Graphics, Controls, Diagnostics, and Logs. Do not expose a control until its setting consumer/capability is registered.
- [ ] Implement nonblocking bounded log publication; the render thread copies a snapshot and performs all ImGui calls.
- [ ] Apply remapping and deadzone to the host input snapshot before the guest cellPad serialization, preserving controller discovery, pressure fields, autostart and autoplay behavior.
- [ ] Run the focused tests and relevant `test_pad_*` CTests; commit the shared core and input path.

## Task 4: D3D12 native renderer and Windows event routing

**Files:**
- Create: `libs/video/rsx_overlay_d3d12.c`
- Create: `libs/video/tests/test_rsx_overlay_d3d12.c` (native Windows smoke harness or existing backend harness extension)
- Modify: `libs/video/rsx_d3d12_backend.c`, `libs/video/rsx_d3d12_backend.h`, `CMakeLists.txt`

- [ ] Inspect current D3D12 frame-resource/fence ownership in `rsx_d3d12_backend.c`; allocate per-frame UI vertex/index upload storage protected by the existing frame fence.
- [ ] Implement ImGui vertex/index upload, font texture, scissor rectangles, alpha blend PSO, and command recording without reusing or mutating guest shader/texture descriptors.
- [ ] Place the UI pass after guest and host movie composition and before transition to `PRESENT`; keep guest draw counters sampled before overlay submission.
- [ ] Route Win32 key events to `rsx_overlay_handle_key`; preserve close, fullscreen, and existing window handling. Consume UI keys only while the overlay is open.
- [ ] Route `WM_KILLFOCUS` to `rsx_overlay_focus_lost()` and convert Win32 key/modifier values to the shared overlay key representation.
- [ ] Replace the rough FPS estimate with elapsed-time FPS and frame-time measurements; publish backend metrics through `rsx_overlay_publish_backend_metrics()` using guest draw counts sampled before overlay submission.
- [ ] Add truthful settings consumers/capabilities for D3D12 window size/fullscreen and VSync; if a live swapchain change is not safe, report restart-required.
- [ ] Build the Windows target and run backend smoke with overlay hidden and visible; verify repeated toggles, resize/fullscreen, no D3D12 debug-layer errors, and no guest-draw counter inflation.
- [ ] Commit D3D12 renderer and event integration.

## Task 5: Metal native renderer and SDL event routing

**Files:**
- Create: `libs/video/rsx_overlay_metal.m`
- Create: `libs/video/tests/test_rsx_overlay_metal.m` (native Metal smoke harness or existing backend harness extension)
- Modify: `libs/video/rsx_metal_backend.m`, `rsx_metal_backend.h`, `CMakeLists.txt`

- [ ] Trace `metal_present_impl` branches for guest, movie, content-hold, and decode-thread presentation; identify the drawable and render encoder that owns the final composite.
- [ ] Implement shared-buffer vertex/index upload, Metal font texture, scissor clipping, alpha blend pipeline, and command encoding on the presentation-owned drawable.
- [ ] Composite UI above the currently selected visible layer (movie/content/guest); never take drawable ownership away from active VideoToolbox playback or add a second competing present.
- [ ] Keep overlay UI and settings mutation on the normal event/presentation thread; publish input/snapshot changes through bounded synchronized state when movie presentation originates on another thread.
- [ ] Publish immutable copied ImGui draw packets from the event thread; Metal's decode-thread present reads a stable packet and never calls ImGui or consumes SDL events.
- [ ] Route SDL focus loss to `rsx_overlay_focus_lost()` and publish Metal FPS/frame-time/guest-draw metrics through `rsx_overlay_publish_backend_metrics()`.
- [ ] Route SDL key/controller events to the shared overlay core; preserve F11/fullscreen, Escape, Cmd+Q, focus-loss and mouse-grab behavior.
- [ ] Add only settings consumers that Metal can apply and mark the rest restart-required/unavailable.
- [ ] Build with `build-macos` arm64 and run Metal smoke with overlay hidden/visible during guest, movie-HLE, and content-hold presentation; inspect Metal validation output and FPS/draw accounting.
- [ ] Commit Metal renderer and event integration.

## Task 6: GoW2 diagnostics providers and launcher/runtime configuration handoff

**Files:**
- Modify: `games/gow2/boot_macos.cpp`, `../gow2-recomp/boot_macos.cpp`
- Modify: `games/gow2/movie_eos_arm.c`, `libs/video/rsx_d3d12_backend.c`, `libs/video/rsx_metal_backend.m`; add a GoW2 runtime adapter under `games/gow2/` if the WAD state has no safe public accessor
- Modify: `games/gow2/gow2_launcher.py`, `../gow2-recomp/gow2_launcher.py` only to pass the runtime settings path
- Create: `games/gow2/tests/test_overlay_config_handoff.py` if launcher handoff changes

- [ ] Trace WAD/movie state in `movie_eos_arm.c` and shader cache/compile counters in each backend; record exact symbol/file/line anchors in the task ledger before editing.
- [ ] Register `rsx_overlay_set_game_provider()` from each GoW2 host boot; copy WAD/movie state and shader counters without retaining guest pointers or taking the guest giant lock.
- [ ] Verify each backend calls `rsx_overlay_publish_backend_metrics()` and unavailable flags remain clear for metrics not supplied by a backend.
- [ ] Route relevant runtime diagnostics through `rsx_overlay_log_write` at their existing log sites; do not redirect process stdout/stderr or add synchronous disk I/O to hot paths.
- [ ] Load settings before backend initialization and pass the resolved config path from the launcher/host consistently on Windows and macOS; retain compatibility with existing setup `user_config.json` fields.
- [ ] Verify malformed config, missing provider, startup without launcher, and save/reload behavior; run launcher unit tests if the handoff code changed.
- [ ] Commit only the GoW2 integration paths, with explicit paths and no game artifacts.

## Task 7: End-to-end acceptance and user documentation

**Files:**
- Modify: `games/gow2/README.md` and the user-facing setup documentation that currently explains `gow2_launcher.py`
- Add/update: focused overlay CTest targets from Tasks 2–5 and `games/gow2` smoke invocation

- [ ] Run the focused CTests for settings, input capture/remapping, log ring, and each available renderer harness.
- [ ] Build `build-macos` arm64 and the Windows GoW2 target; record platform/build availability without treating an unavailable host as a pass.
- [ ] Run an in-boot check with overlay closed, open via F1, navigate, close, then repeat with the controller Select+Start chord and verify the chord does not reach the game while a lone Start still pauses it.
- [ ] During an active movie frame, open the overlay and confirm the movie remains presented; check guest draw counters exclude overlay draws.
- [ ] Change mappings and a graphics option, restart, and verify persistence plus restart-required labeling where appropriate.
- [ ] Inspect `git diff --check`, explicit staged paths, repository status, and both repository worktrees for unrelated user changes and game data.
- [ ] Document controls, settings file location, backend limitations, and diagnostic meanings; report build/unit/in-boot evidence separately.

## Self-review

- **Spec coverage:** Menu/toggle/navigation and capture are Tasks 3–5; native D3D12 and Metal passes are Tasks 4–5; settings persistence and fallback are Task 2; graphics and controller controls are Tasks 2–5; measured FPS/draw and WAD/shader providers are Tasks 4–6; logs are Tasks 3 and 6; config handoff is Task 6; in-boot/platform evidence and documentation are Task 7.
- **Placeholders:** None. Task 6 names the current source owners and requires recording verified symbol/file/line anchors before editing because those fields cross host and backend code.
- **Type consistency:** Shared snapshot and entry-point signatures are defined above; Task 2 settings APIs and Task 3 provider/log/input APIs use those shared types. Renderer tasks consume `ImDrawData` only inside backend code.
- **Review focus coverage:** All five Review Focus items have explicit tests or smoke checks in their owning tasks.
