# Vulkan RSX backend — piece A (feature + performance parity with Metal, validated on macOS/MoltenVK)

Date: 2026-09-23 · Status: design approved in chat, awaiting written-spec review

## 1. Context and goal

The user wants GoW2 to run on Linux, Steam Deck, Android, iOS, Windows and macOS. That program was
decomposed into independent pieces, each with its own spec → plan → implementation cycle:

| Piece | Scope | This spec? |
|---|---|---|
| **A** | Vulkan RSX backend at feature + performance parity with Metal, developed on macOS via MoltenVK | **yes** |
| B | Runtime port to Linux / Steam Deck (build, audio, video decode — VideoToolbox is Apple-only, input) | no |
| C | Windows via Vulkan (option beside D3D12) | no (mostly falls out of A) |
| D | Android (NDK, fixed guest VM reservation, 7 GB data, input) | no |
| E | iOS (VM entitlement, sandbox, distribution; graphics stays **Metal**) | no |

Platform policy (CLAUDE.md "Port Apple"): on macOS/iOS the default backend stays **Metal**; Vulkan is
an opt-in (`PS3_RSX_BACKEND=vulkan`) there. MoltenVK is our dev/validation vehicle for A, not the
Apple shipping path.

### What exists today (measured 2026-09-23)

- `libs/video/rsx_vulkan_backend.c` — 990 lines: SDL2 window, instance/device/swapchain, one render
  pass, RSX clear colour, frame dump readback. `draw_arrays`/`draw_indexed` print
  `TODO: no pipeline yet`. Single frame in flight.
- Wiring is done: `gow2-recomp/boot_macos.cpp:223,242` selects it with `PS3_RSX_BACKEND=vulkan`;
  `CMakeLists.txt:165-173` builds it when `find_package(Vulkan)` succeeds. MoltenVK and
  `vulkan-loader` 1.4.350 are installed via Homebrew on the dev machine.
- `libs/video/rsx_metal_backend.m` — 11,662 lines, renders GoW2 correctly **only in record/replay
  mode** (`PS3_METAL_PER_DRAW_RT=1`, set by `gow2-recomp/env_gow2.sh:88`; code default is off at
  `rsx_metal_backend.m:649-661`). Rough split: ~6,200 core rendering, ~2,100 GoW2/host features
  (movie, prompts, content hold, window), ~3,300 diagnostics (122 `PS3_*` gates).
- `rsx_d3d12_backend.c` (2,759 lines) is **not** a reference: no surface cache, MRT, feedback,
  record/replay, applied blend/depth state. It fell behind because it is a separate copy — the
  failure mode this design avoids.
- Shader decompilers have no IR: `rsx_fp_decompile`/`rsx_vp_decompile` emit HLSL; `*_msl` rewrite it
  by string substitution ("dialect convert") and rebuild prologue/epilogue by hand
  (`rsx_fp_decompiler.c:786-935`, `rsx_vp_decompiler.c:391-549`).

## 2. Requirements

1. **Visual parity** with Metal on macOS (via MoltenVK) at the standard shot points: legal/logo,
   intro movie, main menu, gameplay through the Colossus fight (shoulder, button prompts, particles).
2. **Performance parity**: same gameplay scene (Colossus), same resolution, vsync off —
   Vulkan mean FPS **≥ 85 % of Metal**, p99 frame time **≤ 1.5×** Metal. Never measured on the intro.
   A bottleneck proven (profile) to be inside MoltenVK is recorded as a platform limit with evidence.
3. **Metal must not regress.** Every change to Metal is proven by shot A/B.
4. Windows (D3D12, Win32) must not regress; Vulkan code stays portable C (no Apple-only calls in
   the Vulkan path).
5. New dependencies come from **system packages** (Homebrew / apt / pacman / vcpkg or Vulkan SDK),
   found by CMake: **shaderc** (GLSL→SPIR-V), **VMA** (header), optional **SPIRV-Tools**. Missing
   optional deps degrade features, missing required deps disable the Vulkan backend (as today).
6. No cosmetic "fixes" (CLAUDE.md rule 5): a failing shader/draw is skipped and counted, never
   replaced by a stub or debug colour.

Out of scope for A: host button-prompt replacement, MetalFX/upscaling, content hold, the Metal
diagnostic suite (only frame/surface dump are ported), deswizzle on GPU compute (later, if profiled).

## 3. Architecture

```
 rsx_commands.c (NV4097 → rsx_state, unchanged)
        │  rsx_backend vtable (rsx_commands.h:405, unchanged)
   ┌────┴──────────────────────────────┐
 Metal (.m, thin GPU layer)       Vulkan (.c, thin GPU layer)
   └────┬──────────────────────────────┘
        │ direct calls
 rsx_core_* (portable C, new, shared, unit-tested)
```

### 3.1 Shared core (`libs/video/rsx_core_*.{h,c}`) — extracted from the Metal backend

| Module | Extracted from (Metal) | Responsibility |
|---|---|---|
| `rsx_core_record` | `MetalDrawRec` (~1138), `capture_state` (4122), `capture_fc` (4256), `capture_vc` (4306) | Draw record: state snapshot, FP constants, 468 VP constants in a content-addressed ring |
| `rsx_core_surface` | `metal_surface_find/get` (276-760), MRT decode 0x13/0x1F (9656-9706) | Surface-cache bookkeeping (keys, grow-only, top-left draw into smaller pass), render-to-texture lookup, feedback detection. Holds an **opaque backend handle**, no GPU objects |
| `rsx_core_vertex` | BE decoders, `metal_fetch_attrib_f4`, `metal_va_fill_body` (3502-3948), `metal_attrib_sig`, VA cache | Attribute decode into the float4 ring + VAMap; disabled attribs read `VERTEX_DATA4F`; skinning slots 2/7 |
| `rsx_core_keys` | `metal_pso_state_key`, `metal_pipe_blend_key`, `metal_pipe_ds_desc`, `metal_sampler_for` (2007), `metal_wrap_from_gcm` | API-neutral descriptions + hash keys for PSO, blend (RGB bits 0-15 / alpha 16-31, 0 = ZERO), depth-stencil, sampler (wrap 3/5→clamp-to-edge, 6-8→mirror-clamp-to-edge; min 7 / mag 4 → linear; no mips) |
| `rsx_core_texture` | `metal_expand_*`, texture fingerprint/buckets, `metal_resolve_tex_ea`, `metal_resolve_fp_ea` | Texture key + upload-ready content (uses existing `rsx_texture_deswizzle`/`rsx_texture_formats`) |
| `rsx_core_math` | viewport from scale/offset (7497), alpha ref by surface format (4222), cull (4241) | The GoW2 correctness rules in one place |

Line numbers are anchors as of 2026-09-23 and must be re-verified before each extraction task.

### 3.2 Shader emitters

`rsx_fp_decompile_glsl` / `rsx_vp_decompile_glsl` beside the `*_msl` functions, same dialect-convert
approach (GLSL 450: `float4`→`vec4`, `frac`→`fract`, `lerp`→`mix`, `ddx`→`dFdx`, `select`, `rsx_lit`,
sample calls). Contract:

- Varyings linked by fixed `layout(location=N)`: position, col0, col1, fog, tc0-7 (same set as MSL PSIn).
- 16 combined image samplers, one descriptor set per draw (or push descriptors where available).
- FP constants + alpha-test params + VP constants in one per-slot buffer, bound by dynamic offset.
- Alpha test = specialization constant (was `[[function_constant(0)]]`).
- Always 4 colour outputs (r0,r2,r3,r4 or h0,h4,h6,h8 by SHADER_CONTROL bit 0x40); depth export r1.z
  when SHADER_CONTROL & 0xE.
- VS reads attributes from the float4 ring SSBO via VAMap, indexed by `gl_VertexIndex`;
  `invariant gl_Position` (Colossus drawn twice by two VPs over the same mesh).
- Y: Vulkan NDC Y is down → negative-height viewport; the VP keeps no Y flip; cull keeps guest CW.

### 3.3 Vulkan backend files (replace today's single file)

| File | Responsibility |
|---|---|
| `rsx_vk_device.c` | Instance, device, capability flags, swapchain, 2-3 frames in flight (per-slot command buffer, fence, rings, descriptor pools), VMA |
| `rsx_vk_pipeline.c` | shaderc compile, SPIR-V disk cache by hash, PSO cache, `VkPipelineCache` load/save, dynamic state where supported |
| `rsx_vk_texture.c` | Texture cache by `rsx_core_texture` key, uploads, sampler cache (≥ 512 entries), depth→BGRA8 conversion pass |
| `rsx_vk_replay.c` | Replay of recorded draws at present, pass merge by MRT signature, feedback copy, final copy to swapchain with letterbox, movie overlay |
| `rsx_vulkan_backend.c` | vtable glue only (init/shutdown/pump + callbacks into core) |

## 4. Frame data flow

1. **During the frame (RSX thread):** vtable callbacks → `rsx_core_record` stores each draw (state,
   constants, vertex ring slot decoded by `rsx_core_vertex`). No Vulkan commands are recorded here.
2. **`bind_texture`:** `rsx_core_surface` first answers "is this address a surface?" (render-to-texture
   → record the surface handle). Otherwise `rsx_core_texture` produces key + content; Vulkan uploads
   once per key (CPU deswizzle).
3. **`present` (replay):** walk recorded draws; open/merge render passes by MRT signature (skip a draw
   whose PSO attachment count ≠ pass count); feedback copy once per run of self-sampling draws;
   depth-as-texture via BGRA8 conversion sampled nearest (scene-copy FP 6B2E71EA depends on it);
   first use of a surface clears alpha to 0; PSO + descriptors; draw. Then copy the display surface
   (or the movie frame) to the swapchain.
4. **Frames in flight:** 2-3 slots, each owning its slice of vertex ring, constant ring, descriptor
   pools and command buffer, guarded by its fence (lesson E486: never overwrite what the GPU reads).
5. **Constants:** one large buffer per slot, per-draw dynamic offsets.
6. **Shaders:** first sight of an FP/VP (key = instruction words only; FP at offset 0 is valid; only
   `shader_program == 0` means no shader) → GLSL → shaderc → SPIR-V cached on disk by hash.
7. **Movie overlay:** on macOS VideoToolbox keeps decoding as today; the Vulkan backend receives the
   decoded frame and uploads it to a `VkImage` (one copy per frame — acceptable for video). On Linux
   the frame source is piece B.

## 5. Errors and edge cases

- **Vulkan unavailable / init fails:** `rsx_vulkan_backend_init` returns -1; boot falls back to the
  platform default backend with one log line. Never abort the game.
- **Missing feature/extension** (MoltenVK varies: `depthClamp`, mirror-clamp, dynamic state): probed at
  init into capability flags; each has a declared fallback and a one-time log
  `[RSX vulkan] cap X missing -> Y`.
- **Shader compile failure:** log GLSL + error once per hash, write GLSL to disk, **skip** the draw,
  count `shader_fail` in the end-of-frame line.
- **Invalid draw** (empty scissor intersection, attachment mismatch, no FP): skip + counter (same
  rules as Metal, from `rsx_core`).
- **Ring/pool overflow:** grow on the next frame, never mid-frame; the overflowing draw is skipped and
  counted.
- **Out-of-date swapchain / resize / fullscreen:** recreate on next present.
- **Device lost:** log last submitted draw (FP/VP hash, pass) and exit cleanly; no recovery in A.
- **Validation:** `PS3_VK_VALIDATION=1` (off by default) enables `VK_LAYER_KHRONOS_validation`
  routed to the log. Every new feature must be validation-clean on the path it covers.

## 6. Testing and acceptance

**Metal protection (every extraction):**
- Unit test of the extracted module in ctest (`PS3RECOMP_BUILD_TESTS=ON`) with fixed vectors.
- Metal shot A/B via `gow2-recomp/claude_runs/run_until_shots.sh` (6 shots + S8 + PASS): pixel-identical
  on deterministic shots; where runs vary, first measure Metal-vs-Metal variance and use it as the
  declared tolerance.
- One commit per extraction.

**Vulkan visual acceptance (MoltenVK vs Metal):** same shot points as §2.1; per shot, pixels differing
by > 8/255 on any channel < 0.5 %, and no black/magenta regions where Metal has content. Filtering/
rounding differences become declared tolerances. Zero validation errors in a short menu + gameplay run.

**Performance acceptance:** §2.2, measured with the existing FPS log, runs stopped by event (not fixed
duration). If missed, a profile (Instruments / `sample`) must show the bottleneck.

## 7. Milestones (each proven before the next)

Revised 2026-09-23 after V0/V1a: the GoW2 logos are host overlays (not guest draws), the menu needs MRT + render-to-texture, and no deterministic capture existed, so V2 was split (V2.0-V2.4) and a frame recorder/replayer comes first.


| # | Deliverable | Proof |
|---|---|---|
| V0 | GLSL emitters + shaderc wiring | Offline: every dumped GoW2 FP/VP compiles to SPIR-V |
| V1a | Pure `rsx_core_*` (decode, rules, pipe) extracted; Metal uses it | Exhaustive oracle tests (done 2026-09-23) |
| V2.0 | In-game RSX frame recorder (gated) + offline replay host into the production backends | Same capture replayed twice into Metal → identical pixels; menu feature census |
| V2.1 | Vulkan 1.1 device, frames in flight, swapchain recreation, host-content (logo) overlay dispatched to the active backend | Logos match Metal; validation-clean |
| V2.2 | V1b: stateful extraction (draw record, FC/VC/VA rings, surface table, pipe-from-state) | Capture replay into Metal before/after: pixel-identical |
| V2.3 | Vulkan per-draw replay, single colour target + depth, textures, display copy | Title/menu surfaces vs Metal on the same capture |
| V2.4 | MRT + render-to-texture (location 1/2) — pulled forward from V3: the menu composite samples MRT target B at 0x00200000 | Menu matches Metal on the capture and in-game, no flicker |
| V3 | Feedback, depth-as-texture, remaining gameplay features | Gameplay + Colossus match Metal |
| V4 | Movie overlay via upload | Intro + cutscenes match Metal |
| V5 | Performance: pipeline cache, dynamic state, profiling | ≥ 85 % Metal FPS, p99 ≤ 1.5× |

## 8. Risks

- **Refactoring the backend the user plays on** — mitigated by one extraction per commit with shot A/B.
- **MoltenVK gaps** hide real Vulkan behaviour — capability flags + a later check on native Linux (B).
- **Text-rewrite shader emitter** is fragile — V0 compiles every dumped shader offline before any
  runtime work; an IR is out of scope unless the rewrite proves unmaintainable.
- **Shot determinism** — variance measured first, tolerances declared, never silent.
