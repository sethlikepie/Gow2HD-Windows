# GoW2 Runtime Overlay — Design Specification

## Goal

Add an in-game settings and diagnostics overlay to the GoW2 static recompilation host. The overlay opens with F1 or controller Start, can be navigated with keyboard and controller, exposes applicable graphics and input settings, reports measured runtime state, and persists user preferences.

The overlay is a host UI. Its visibility and diagnostics are not evidence that the guest game rendered successfully; game-content acceptance remains based on natural guest draws and textures.

## Current context

- The GoW2 port has native D3D12 and Metal backends selected by its host boot flow.
- Both backends own presentation and expose message pumping. The Metal event pump uses SDL2; the Windows D3D12 path owns Win32 messages.
- The Metal backend has frame and draw accounting. D3D12 has an existing but rough FPS counter, per-frame draw records, and shader-cache health counters.
- Host movie presentation is already composed by both graphics backends, so overlay composition must be ordered after guest/movie content where possible and must not take ownership of movie presentation.
- SDL2-backed controller state is translated into the guest `cellPad` interface. The runtime also has a Tkinter GoW2 setup launcher with a JSON configuration, but that file currently describes launch/setup data rather than runtime graphics preferences.
- The local RPCS3 reference has RSX-thread-owned performance/debug overlay managers (`rpcs3/Emu/RSX/RSXThread.cpp`, `rpcs3/Emu/RSX/Overlays/overlay_perf_metrics.cpp`, and `overlay_debug_overlay.cpp`). Reuse the principle of a presentation-thread overlay manager and measured counters; do not import RPCS3 implementation dependencies.

## User-approved scope

1. An in-game menu toggled by F1 and controller Start.
2. Graphics/performance settings, controller settings and remapping.
3. Diagnostic display for FPS, WAD state, shaders, draws, and logs.
4. Persistent user configuration.
5. Native rendering on Windows D3D12 and macOS Metal.

## Architecture

### Shared overlay core

Add a backend-independent overlay module that owns visibility, navigation, settings model, diagnostics snapshot consumption, and the bounded in-memory log view. Dear ImGui supplies the UI and input/navigation model. The core must not call D3D12, Metal, SDL renderer, or guest APIs directly.

The module receives host input events and a per-frame diagnostic snapshot, emits overlay draw data, and reports setting changes through a small host-facing API. It is initialized and shut down with the selected backend. The overlay is disabled by default only if configuration explicitly disables it; otherwise the F1/Start toggle is available.

### Backend renderers

Each graphics backend renders the shared UI draw data using its existing native device and frame resources:

- D3D12: append an alpha-blended overlay pass to the frame command list before present.
- Metal: encode the UI pass into the active drawable render command before presentation.

The render pass must restore or isolate all GPU state it changes, use host-owned resources, and avoid changing guest RSX state. It must coexist with the host movie layer and content-hold paths. If presentation is occurring on a decode thread, the menu draw and input mutation remain owned by the normal window/presentation thread; queued movie presentation can continue without consuming UI events.

### Input and menu capture

F1 toggles visibility. Start toggles visibility on a rising edge, with the edge tracked by the host overlay so a held button cannot repeatedly toggle. While open, keyboard/controller navigation is consumed by the overlay and must not leak those same UI actions to the guest. When closed, existing guest input behavior remains unchanged.

The existing backend event pump feeds keyboard/mouse events to the overlay. Controller input is read from the host controller layer before guest `cellPad` delivery, with configurable bindings and a clear capture policy. Remapping must update the host-to-guest mapping, not only the displayed labels.

### Settings

The first settings model includes:

- Window mode and resolution, with changes applied live only where the backend safely supports swapchain/layer resize; otherwise mark them as restart-required.
- VSync/present mode, exposed only where the backend has a real runtime control.
- Overlay visibility, diagnostics visibility, and toggle binding.
- Host controller-to-guest button mapping and analog deadzone, connected to the `cellPad` translation path.

No UI control may claim a setting was applied when it is merely stored. Unsupported controls are omitted or clearly marked unavailable until a real backend implementation exists.

### Diagnostics

Expose a snapshot API populated by measured runtime values:

- FPS and frame time from backend presentation accounting.
- Guest draw calls per frame and total, distinguishing guest draws from overlay draws.
- Shader cache/compile counts where the backend exposes them; otherwise show unavailable rather than zero.
- Current WAD/movie-loader state from existing GoW2 diagnostics through a narrow provider hook; unknown states are shown as unavailable.
- Recent runtime logs through a bounded ring buffer. Logging callbacks must not perform UI work or block hot-path threads; the UI reads a snapshot on the presentation thread.

Overlay-specific draws and host movie frames must never be counted as guest game draws.

## Configuration and lifecycle

Use a versioned runtime settings file in the platform's per-user application configuration directory (macOS Application Support, Windows AppData). Load defaults, validate every field and binding, and retain defaults for invalid or unknown data. Save atomically through a temporary sibling file followed by rename. The existing launcher setup JSON remains compatible and may pass an explicit runtime-config path to the game; runtime defaults must not depend on the current working directory.

Initialization order: load/validate settings, initialize the selected backend, initialize overlay resources, then enter guest execution. Shutdown saves dirty settings and releases UI resources before backend teardown. A failure to initialize the optional UI renderer must leave the game backend usable and report the issue through logs.

## Boundaries and non-goals

- No claim of natural game pixels or shader-registry completion from overlay visibility.
- No modification of guest code or RSX command semantics to draw the menu.
- No D3D12 or Win32 dependency in the macOS target; no SDL renderer dependency in the native Metal render path.
- No cosmetic toggles lacking an actual setting consumer.
- No alteration of the existing setup launcher's game-data handling beyond passing runtime settings as needed.

## Delivery sequence

1. Shared settings/diagnostic/input model and persistence, with deterministic unit coverage.
2. Native overlay renderer and F1 input on one backend, then controller Start/navigation and capture.
3. Port the renderer to the other native backend and verify equivalent behavior.
4. Connect WAD/shader/log providers and actual settings consumers; expose unsupported values as unavailable.
5. Run backend smoke checks and a GoW2 in-boot check where the local build/runtime environment permits. Report offline, in-boot, and unexercised results separately.

## Acceptance criteria

- F1 and Start open/close the overlay reliably, including held-button and focus-loss cases.
- Overlay keyboard/controller navigation works and UI actions do not reach the guest while captured.
- Overlay content composites above the current presented frame without corrupting guest RSX state or host movie presentation.
- D3D12 and Metal use native backend resources and each can shut down cleanly.
- FPS/draw counters are real and do not include overlay draws. WAD/shader values are real or explicitly unavailable.
- Recent logs are viewable without blocking guest/render threads.
- Settings survive restart, malformed files fall back safely, and restart-required settings are identified.
- Existing input and rendering behavior remains unchanged with the overlay closed.
- Verification records distinguish build/unit evidence from in-boot observations; overlay appearance is never reported as proof of game rendering.

## Risks to resolve in the implementation plan

- The exact D3D12 frame-list and Metal drawable ownership boundaries must be confirmed before selecting the UI renderer integration points.
- Start-button capture must not race guest polling; prefer the host input sample path and edge detection over reading guest memory.
- The WAD/shader observability currently lives in GoW2/runtime-specific code and needs a narrow provider contract to avoid backend coupling.
- Dear ImGui source/dependency acquisition and license attribution must follow repository dependency policy; do not vendor or fetch it until the plan specifies the approved dependency route.
- Current working trees contain unrelated user changes (`libs/audio/cellAudio.c`, `runtime/spu/spu_workload.c`, and sibling `g2audio`); implementation must preserve them.
