# GoW2 Overlay Console Shell Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the stock Dear ImGui tab UI of the runtime overlay with a PS5-style control center (blurred live backdrop, card row, floating panel, system type and icons, springs, UI sounds and haptics), opened by the controller PS/Home button, Select+Start or F1.

**Architecture:** A pure C-ABI shell module (`rsx_overlay_shell`) owns the state machine, focus, springs and layout and emits actions; the existing overlay core (`rsx_overlay.cpp`) feeds it input, applies its actions to the persisted settings and consumers, and draws it through `ImDrawList` into the immutable draw packet (v2: real drawable size, backing scale, backdrop fields). Text and icons come from a runtime glyph atlas (CoreText + SF Symbols on Apple, embedded Inter elsewhere) that replaces the ImGui font as texture 0. Metal adds a half-resolution MPS Gaussian backdrop inside the same UI pass; D3D12 keeps the same layout with dim only.

**Tech Stack:** C11/C++20, Dear ImGui 1.92.5 (geometry only), Objective-C ARC (Metal, MetalPerformanceShaders, CoreText, AppKit, AVFoundation, GameController, CoreHaptics), stb_truetype (vendored with ImGui), D3D12 (C COM), SwiftUI launcher, CMake/CTest + Ninja.

**Spec:** `docs/superpowers/specs/2026-09-23-gow2-overlay-console-shell-design.md` (visual layer). Plumbing still binding from `docs/superpowers/specs/2026-09-23-gow2-runtime-overlay-design.md`.

## Global Constraints

- Backdrop: Gaussian blur radius ≈ 24 px at drawable resolution, dim ≈ 35 %, faded in over 180 ms (driven by `open_progress`), live every frame while open.
- Bottom band: vertical gradient transparent → ≈ 92 % near-black.
- Cards: radius ≈ 14 pt, translucent dark glass, focused scale ≈ 1.18 with a spring, 2 pt white border + soft white glow, unfocused ≈ 75 % opacity; "Sair" alone at the right end.
- Panel: dark glass ≈ 86 % with drop shadow, anchored above the focused card; label left / control right; focused row has a light rounded highlight; switches blue `#2E7BF6`.
- Top strip: left "God of War II HD"; right local time, controller glyph and battery only when the controller reports it (never a fake value).
- Typography: SF Pro — panel title 17 pt semibold, rows 15 pt regular, card labels 11 pt medium. Icons: SF Symbols `play.fill`, `display`, `gamecontroller`, `speaker.wave.2`, `hammer`, `power`.
- Motion: open ≈ 220 ms spring rise; focus spring; panel content cross-fade 120 ms; close reverses open.
- Everything laid out and rasterized at the real drawable size and backing scale; the fixed 1280x720 canvas is removed.
- Window sizes offered: 1280×720, 1600×900, 1920×1080, 2560×1440. Dead-zone slider 0–30 %. No game-volume slider.
- Shell always opens on "Voltar ao jogo". Left/right = cards; down = into panel; up/down = rows; Cross/Enter = confirm; Circle/Esc = back one level, closes at card level; PS/Home, Select+Start and F1 close from anywhere.
- Closed shell ⇒ no per-frame cost beyond today's, guest input and render unchanged. The game keeps running behind the open shell with its input withheld.
- UI sounds synthesized in code (no Sony assets), dedicated `AVAudioEngine`; haptics through the existing asynchronous haptics queue; neither blocks the game or UI thread. Each has an off switch.
- Windows: same layout and navigation, dim-only backdrop, no blur (phase 2); no Windows regression. D3D12/Win32 code stays out of the macOS target; Metal/Apple code stays out of Windows.
- Settings v2 fields `ui_sounds`, `ui_haptics`, `game_mute`, `vibration`; v1 files load with defaults for new fields; invalid values fall back per field.
- Project rules (CLAUDE.md): internal artifacts in English; commits in pt-BR **without** co-author lines; never `git add -A`/`git add .` (explicit paths only); never stage game data; probes gated OFF; never claim in-boot results without observation. Overlay visibility is never evidence of natural game pixels.
- In-boot validation is the user's UAT (Task 10). No task claims it.

## Review Focus

1. **A button still held when the shell closes from inside** (Cross on "Voltar ao jogo", Circle at card level, keyboard Enter/Space mapped to START/CROSS) must not reach the game as a press — Kratos must not jump nor the game pause. Pinned by `test_closing_from_menu_withholds_held_buttons` (Task 7).
2. **Tiny, ultrawide and 16:10 Retina drawables** (640×480 @1x, 3440×1440 @2x, 2880×1800 @2x) must keep cards non-overlapping, on screen, and the panel between the top strip and the focused card, with long lists scrolling. Pinned by `test_layout_extremes_fit` (Task 3).
3. **Backing-scale change mid-session** (window dragged between a Retina and a 1x display) must rebuild the atlas once, bump its generation and make the Metal renderer reload it — no stale UVs, no rebuild when the scale is unchanged. Pinned by `test_scale_change_rebuilds_atlas` (Task 7) and `test_atlas_generation_reloads_texture` (Task 4).
4. **Rapid toggles** (PS pressed twice fast, chord during the closing animation, F1 right after open) must keep phase/visibility consistent: input captured only while opening/open, rendering until the close animation ends, reopen continues from the current animation value. Pinned by `test_reopen_during_close_resumes` (Task 3) and `test_closed_shell_costs_nothing` (Task 7).
5. **Densest screen** (64 max-length log entries, Logs list at 3840×2160 @2x, every card at card level) must fit the fixed packet capacities. Pinned by `test_draw_budget_worst_case` (Task 7).

---

## File Structure

| File | Responsibility | Task |
|---|---|---|
| `libs/video/rsx_overlay.h` | Core API: packet v2, display size, render-active, quit request, title, controller status, asset builder, feedback sink, release guard, Home button, test hooks | 1,4,6,7 |
| `libs/video/rsx_overlay_packet.h`, `rsx_overlay_d3d12_packet.h` | Packet validation and display→framebuffer scissor/transform helpers | 1 |
| `libs/video/rsx_overlay.cpp` | Core: input capture, shell glue, action application, atlas ownership, packet build | 1,4,6,7 |
| `libs/video/rsx_overlay_settings.{h,c}` | Settings v2 + v1 upgrade | 2 |
| `libs/video/rsx_overlay_feedback.h` (new) | Feedback event enum and sink type shared by shell, core and players | 3 |
| `libs/video/rsx_overlay_shell.{h,cpp}` (new) | Pure shell logic: state machine, focus, springs, layout, remap assignment | 3 |
| `libs/video/rsx_overlay_assets.{h,c}` (new) | Glyph set, UTF-8, packer, text width, embedded Inter atlas builder | 4 |
| `libs/video/rsx_overlay_assets_apple.m` (new) | CoreText SF Pro + SF Symbols atlas builder (falls back to Inter) | 4 |
| `third_party/inter/` (new) | Inter 4.1 Regular/SemiBold TTF + OFL license + pin note | 4 |
| `cmake/embed_file.cmake` (new) | Turns a binary file into a C array at build time | 4 |
| `libs/video/rsx_overlay_metal.{h,m}` | Metal UI pass: packet v2, atlas generation reload, MPS backdrop | 1,4,8 |
| `libs/video/rsx_overlay_d3d12.c` | D3D12 UI pass: packet v2, render-active gate | 1,7 |
| `libs/video/rsx_overlay_ui_sound.{h,c}` (new) | Deterministic UI sound synthesis | 5 |
| `libs/video/rsx_overlay_feedback_apple.{h,m}` (new) | AVAudioEngine player + haptic hook | 5 |
| `libs/audio/audio_mute.c`, `cellAudio.h`, `cellAudio.c` | Runtime mute flag + per-block gate | 5 |
| `libs/input/cellPad.{h,c}`, `cellpad_internal.h`, `cellpad_overlay.h`, `cellpad_gamecontroller.{h,m}` | Vibration gate, UI haptic tick, PS/Home host bit, battery, release guard | 5,6,7 |
| `libs/video/rsx_overlay_shell_draw.{h,cpp}` (new) | ImDrawList drawing of the whole shell | 7 |
| `libs/video/rsx_metal_backend.m`, `rsx_d3d12_backend.c` | set_display, render-active gating, quit, installs (Apple builder, feedback) | 1,7 |
| `games/gow2/boot_macos.cpp`, `games/gow2/build_macos.sh` | Title, frameworks | 4,7,8 |
| `games/gow2/launcher/macos/*.swift`, `build_app.sh`, `tests/launch_check/` | Launcher ↔ overlay settings handoff | 9 |
| `docs/UPSTREAM_DELTA.tsv` | Re-anchor line 37 | 9 |
| `games/gow2/README.md`, `docs/superpowers/plans/2026-09-23-gow2-overlay-console-shell-uat.md` | Docs + pt-BR UAT | 10 |
| `CMakeLists.txt` | Sources, frameworks, tests | all |

**Build/test commands used by every task** (worktree root; `build-macos` is already configured with Ninja and tests on; CMake edits re-run configure automatically):

```bash
cmake --build build-macos --target <target>
ctest --test-dir build-macos -R '^<test>$' --output-on-failure
```

MinGW syntax check (Windows must keep compiling):

```bash
MW="-Ilibs/video -Ilibs/input -Ilibs/audio -Iinclude -Ithird_party/imgui"
x86_64-w64-mingw32-gcc -fsyntax-only -std=c11 $MW <file.c>
x86_64-w64-mingw32-g++ -fsyntax-only -std=c++20 $MW <file.cpp>
```

---

### Task 1: Draw packet v2 and real drawable size

**Files:**
- Modify: `libs/video/rsx_overlay.h:44-48` (canvas defines), `:68-76` (packet), declarations near `:140`
- Modify: `libs/video/rsx_overlay_packet.h` (whole helper section `:32-153`)
- Modify: `libs/video/rsx_overlay_d3d12_packet.h:24-37`
- Modify: `libs/video/rsx_overlay.cpp:792-851` (`rsx_overlay_copy_imgui_draw_data`), `:853-927` (`rsx_overlay_build_draw_data`)
- Modify: `libs/video/rsx_overlay_metal.m:284-315` (encode), `libs/video/rsx_overlay_d3d12.c:472-503` (render)
- Modify: `libs/video/rsx_metal_backend.m:9551-9559` (UI hook), `:11866-11881` (pump tail); `libs/video/rsx_d3d12_backend.c:2888-2905` (pump)
- Test: `libs/video/tests/test_rsx_overlay.c`, `test_rsx_overlay_d3d12.c`, `test_rsx_overlay_metal.m`

**Interfaces:**
- Produces:

```c
#define RSX_OVERLAY_DISPLAY_MAX 16384u
typedef struct rsx_overlay_draw_packet {
    uint64_t generation;
    uint32_t display_width;   /* drawable pixels the UI was laid out for */
    uint32_t display_height;
    float backing_scale;      /* drawable pixels per point */
    float open_progress;      /* 0 closed .. 1 fully open */
    float backdrop_dim;       /* informative: the core draws the dim as its first command */
    uint32_t backdrop_blur;   /* 1: renderer may blur the frame behind the UI (Metal only) */
    uint32_t vertex_count, index_count, command_count;
    /* vertices/indices/commands unchanged */
} rsx_overlay_draw_packet;
void rsx_overlay_set_display(uint32_t width_px, uint32_t height_px, float backing_scale);
void rsx_overlay_get_display(uint32_t* width_px, uint32_t* height_px, float* backing_scale);
int  rsx_overlay_clip_to_viewport_scissor(const float clip[4], uint32_t display_w, uint32_t display_h,
         const rsx_overlay_viewport* vp, uint32_t fb_w, uint32_t fb_h, rsx_overlay_scissor* out);
int  rsx_overlay_clip_to_scissor(const float clip[4], uint32_t display_w, uint32_t display_h,
         uint32_t fb_w, uint32_t fb_h, rsx_overlay_scissor* out);
void rsx_overlay_display_transform(uint32_t display_w, uint32_t display_h, float out[4]);
/* d3d12 aliases: rsx_overlay_d3d12_clip_to_scissor(clip, dw, dh, fbw, fbh, out),
 *                rsx_overlay_d3d12_display_transform(dw, dh, out) */
```

- [ ] **Step 0: Preflight — branch base and anchors**

The spec requires this branch to contain `macos-arm64-port-f0-f2`. It was merged at `af042d04`, but at plan time the port branch had moved **12 commits ahead** (tip `b036f4c2`, including `env_gow2.sh` and sound/lwmutex changes). Bring it in before touching code:

```bash
git fetch origin
if git merge-base --is-ancestor origin/macos-arm64-port-f0-f2 HEAD; then
  echo "port branch already contained"
else
  git merge --no-ff origin/macos-arm64-port-f0-f2 \
    -m "merge: traz macos-arm64-port-f0-f2 atualizado para a branch do overlay"
fi
git merge-base --is-ancestor origin/macos-arm64-port-f0-f2 HEAD && echo OK
```

Expected: `OK`. Resolve conflicts with the `integrar-upstream` file policy (ours for overlay files, theirs for the rest) and never `git add -A`.

Then re-validate every anchor this plan cites. Tasks name each anchor by **function or comment text plus line**; after the merge, trust the text and record corrected line numbers in the SDD ledger (not in this plan):

```bash
grep -n "static void rsx_overlay_copy_imgui_draw_data\|^void rsx_overlay_build_draw_data\|^int rsx_overlay_handle_key\|^int rsx_overlay_handle_pad\|^int rsx_overlay_persist_settings\|^int rsx_overlay_init" libs/video/rsx_overlay.cpp
grep -n "Runtime overlay UI: the last pass\|framebufferOnly = NO\|^int rsx_metal_backend_pump_messages\|^void rsx_metal_backend_shutdown\|rsx_overlay_metal_renderer_create(s_state.device\|PS3_METAL_VSYNC, when set\|const char\* fs = getenv(\"PS3_FULLSCREEN\")" libs/video/rsx_metal_backend.m
grep -n "^int rsx_d3d12_backend_pump_messages\|rsx_overlay_d3d12_renderer_render(s_d3d.overlay" libs/video/rsx_d3d12_backend.c
grep -n "cellpad_overlay_filter(host_buttons\|^s32 cellPadSetActDirect\|s_host_analog_deadzone =\|PS3_PAD_AUTOSTART on a windowed Mac" libs/input/cellPad.c
grep -n "ps3_audio_muted()" libs/audio/cellAudio.c
grep -n "PS3_FULLSCREEN\|PS3_METAL_VSYNC" games/gow2/launcher/macos/Settings.swift games/gow2/env_gow2.sh
sed -n 37p docs/UPSTREAM_DELTA.tsv
```

Expected: every pattern is found once (the env_gow2/Settings grep may list several lines). A missing pattern means the merge changed that code: stop and re-plan that step with the reviewer before continuing.

- [ ] **Step 0b: Record the test baseline**

Run: `cmake --build build-macos && ctest --test-dir build-macos -j8 2>&1 | tail -30`
Expected: note every failing test in the SDD ledger (`.superpowers/sdd/progress.md`). `py.test_upstream_delta` is a known baseline failure (fixed in Task 9). Later tasks compare against this list.

- [ ] **Step 1: Write the failing tests**

In `libs/video/tests/test_rsx_overlay_d3d12.c` replace `test_clip_rect_maps_canvas_to_framebuffer` and `test_canvas_transform_maps_corners_to_ndc` with:

```c
static int test_clip_rect_maps_display_to_framebuffer(void)
{
    const float half[4] = { -5.0f, 10.0f, 1300.0f, 730.0f };
    const float inner[4] = { 100.25f, 50.5f, 200.5f, 60.25f };
    const float empty[4] = { 9.0f, 9.0f, 9.0f, 10.0f };
    const float nan_rect[4] = { 0.0f, 0.0f, NAN, 10.0f };
    rsx_overlay_d3d12_scissor scissor;

    /* A 1280x720 layout presented on 640x360 scales by 0.5. */
    CHECK(rsx_overlay_d3d12_clip_to_scissor(half, 1280u, 720u, 640u, 360u, &scissor));
    CHECK(scissor.left == 0 && scissor.top == 5 && scissor.right == 640 && scissor.bottom == 360);
    CHECK(rsx_overlay_d3d12_clip_to_scissor(inner, 1280u, 720u, 1920u, 1080u, &scissor));
    CHECK(scissor.left == 150 && scissor.top == 75 && scissor.right == 301 && scissor.bottom == 91);
    /* Laid out at the framebuffer's own size (the shell's normal case): 1:1. */
    CHECK(rsx_overlay_d3d12_clip_to_scissor(inner, 1920u, 1080u, 1920u, 1080u, &scissor));
    CHECK(scissor.left == 100 && scissor.top == 50 && scissor.right == 201 && scissor.bottom == 61);
    CHECK(!rsx_overlay_d3d12_clip_to_scissor(empty, 1280u, 720u, 640u, 360u, &scissor));
    CHECK(!rsx_overlay_d3d12_clip_to_scissor(nan_rect, 1280u, 720u, 640u, 360u, &scissor));
    CHECK(!rsx_overlay_d3d12_clip_to_scissor(inner, 1280u, 720u, 0u, 360u, &scissor));
    CHECK(!rsx_overlay_d3d12_clip_to_scissor(inner, 0u, 720u, 640u, 360u, &scissor));
    return 1;
}

static int test_display_transform_maps_corners_to_ndc(void)
{
    float t[4];

    rsx_overlay_d3d12_display_transform(2560u, 1440u, t);
    CHECK(fabsf(0.0f * t[0] + t[2] + 1.0f) < 1e-6f);
    CHECK(fabsf(0.0f * t[1] + t[3] - 1.0f) < 1e-6f);
    CHECK(fabsf(2560.0f * t[0] + t[2] - 1.0f) < 1e-6f);
    CHECK(fabsf(1440.0f * t[1] + t[3] + 1.0f) < 1e-6f);
    return 1;
}
```

Update `main` to call the two new names. In `test_packet_counts_are_bounded`, right after `s_packet.command_count = 1;` insert:

```c
    CHECK(!rsx_overlay_d3d12_packet_valid(&s_packet)); /* no display size yet */
    s_packet.display_width = 1280u;
    s_packet.display_height = 720u;
```

and at the end of that test (before `return 1;`):

```c
    s_packet.command_count = 1;
    s_packet.display_width = RSX_OVERLAY_DISPLAY_MAX + 1u;
    CHECK(!rsx_overlay_d3d12_packet_valid(&s_packet));
```

In `libs/video/tests/test_rsx_overlay_metal.m`: in `known_packet()` after `p->generation = 1;` add `p->display_width = 1280u; p->display_height = 720u; p->backing_scale = 1.0f;`. Replace every `rsx_overlay_clip_to_viewport_scissor(X, &kViewport, FB_W, FB_H, &sc)` in `test_viewport_scissor` with `rsx_overlay_clip_to_viewport_scissor(X, 1280u, 720u, &kViewport, FB_W, FB_H, &sc)` and the last call with `rsx_overlay_clip_to_scissor(left, 1280u, 720u, 1280, 720, &sc)`. Add:

```objc
/* A packet laid out at the target's own size maps pixel for pixel. */
static int test_display_pixels_map_one_to_one(void)
{
    static const float clip[4] = { 0.0f, 0.0f, (float)FB_W, (float)FB_H };
    rsx_overlay_draw_packet* p = calloc(1, sizeof(*p));
    CHECK(p != NULL);
    p->generation = 1;
    p->display_width = FB_W;
    p->display_height = FB_H;
    p->backing_scale = 1.0f;
    add_quad(p, 10, 20, 30, 40, 0.25f, 0.75f, 0xFFFFFFFFu, clip, 0); /* green texel */
    test_source src = { 1, p, 0 };
    rsx_overlay_metal_source source = { src_visible, src_copy_packet, src_copy_font, &src };
    rsx_overlay_metal_renderer* r =
        rsx_overlay_metal_renderer_create(s_device, MTLPixelFormatBGRA8Unorm, &source);
    CHECK(r != NULL);
    const rsx_overlay_viewport full = { 0, 0, FB_W, FB_H };
    id<MTLTexture> target = make_target();
    id<MTLCommandBuffer> cb = [s_queue commandBuffer];
    encode_clear(cb, target);
    CHECK(rsx_overlay_metal_renderer_encode(r, cb, target, full) == 1);
    id<MTLBuffer> rb = encode_readback(cb, target);
    [cb commit];
    [cb waitUntilCompleted];
    CHECK(pixel_is(rb, 10, 20, 0, 255, 0));
    CHECK(pixel_is(rb, 29, 39, 0, 255, 0));
    CHECK(is_background(rb, 30, 20));
    CHECK(is_background(rb, 10, 40));
    rsx_overlay_metal_renderer_destroy(r);
    free(p);
    return 1;
}
```

and call it from `main` after `test_viewport_scissor()`.

In `libs/video/tests/test_rsx_overlay.c`, after the `open_menu()` helper, add and call it from `main` just before `test_disabled_core_is_inert()`:

```c
static int test_packet_carries_display_size(void)
{
    char path[64];
    uint32_t w, h;
    float scale;
    rsx_overlay_draw_packet* packet = (rsx_overlay_draw_packet*)calloc(1, sizeof(*packet));

    CHECK(packet != NULL);
    CHECK(fresh_init(path, sizeof path));
    rsx_overlay_set_display(2560u, 1440u, 2.0f);
    CHECK(open_menu());
    rsx_overlay_build_draw_data();
    CHECK(rsx_overlay_copy_draw_packet(packet) == 1);
    CHECK(packet->display_width == 2560u && packet->display_height == 1440u);
    CHECK(packet->backing_scale == 2.0f);
    for (uint32_t i = 0; i < packet->command_count; ++i)
        CHECK(packet->commands[i].clip_rect[2] <= 2560.0f &&
              packet->commands[i].clip_rect[3] <= 1440.0f);
    /* A minimized window reports 0x0; bad scales are ignored too. */
    rsx_overlay_set_display(0u, 0u, 2.0f);
    rsx_overlay_set_display(800u, 600u, 0.0f);
    rsx_overlay_get_display(&w, &h, &scale);
    CHECK(w == 2560u && h == 1440u && scale == 2.0f);
    rsx_overlay_set_display(1280u, 720u, 1.0f);
    rsx_overlay_shutdown();
    unlink(path);
    free(packet);
    return 1;
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake --build build-macos --target test_rsx_overlay test_rsx_overlay_d3d12 test_rsx_overlay_metal`
Expected: compile errors — `rsx_overlay_set_display`, `display_width`, `rsx_overlay_d3d12_display_transform` undeclared.

- [ ] **Step 3: Implement packet v2 and helpers**

`rsx_overlay.h`: replace the `RSX_OVERLAY_CANVAS_WIDTH/HEIGHT` block (`:44-48`) with

```c
/* Largest drawable side (pixels) a packet may describe. Positions and clip
 * rectangles are drawable pixels of display_width x display_height. */
#define RSX_OVERLAY_DISPLAY_MAX 16384u
```

replace the packet struct with the one in **Interfaces**, and declare after `rsx_overlay_build_draw_data`:

```c
/* Event thread (the one that builds draw data). Size in pixels of the drawable
 * the UI is laid out on and its backing scale (pixels per point). Zero sizes
 * (minimized window) and scales outside [0.5, 4] keep the previous value. */
void rsx_overlay_set_display(uint32_t width_px, uint32_t height_px, float backing_scale);
void rsx_overlay_get_display(uint32_t* width_px, uint32_t* height_px, float* backing_scale);
```

`rsx_overlay_packet.h`: replace `rsx_overlay_packet_valid`, `rsx_overlay_clip_to_viewport_scissor`, `rsx_overlay_clip_to_scissor`, `rsx_overlay_canvas_transform` with:

```c
static inline int rsx_overlay_packet_valid(const rsx_overlay_draw_packet* packet)
{
    return packet != NULL &&
           packet->display_width != 0 && packet->display_height != 0 &&
           packet->display_width <= RSX_OVERLAY_DISPLAY_MAX &&
           packet->display_height <= RSX_OVERLAY_DISPLAY_MAX &&
           packet->vertex_count != 0 && packet->index_count != 0 &&
           packet->command_count != 0 &&
           packet->vertex_count <= RSX_OVERLAY_DRAW_VERTEX_CAPACITY &&
           packet->index_count <= RSX_OVERLAY_DRAW_INDEX_CAPACITY &&
           packet->command_count <= RSX_OVERLAY_DRAW_COMMAND_CAPACITY;
}

/* Clip rectangles are in the packet's display pixels. They are scaled onto
 * the viewport (normally the whole drawable), offset by its origin and clamped
 * to both the viewport and the framebuffer. Returns 0 when nothing is left. */
static inline int rsx_overlay_clip_to_viewport_scissor(
    const float clip_rect[4], uint32_t display_width, uint32_t display_height,
    const rsx_overlay_viewport* viewport,
    uint32_t framebuffer_width, uint32_t framebuffer_height,
    rsx_overlay_scissor* out)
{
    float scale_x, scale_y, left, top, right, bottom, min_x, min_y, max_x, max_y;

    if (clip_rect == NULL || viewport == NULL || out == NULL ||
        display_width == 0 || display_height == 0 ||
        framebuffer_width == 0 || framebuffer_height == 0 ||
        viewport->width == 0 || viewport->height == 0)
        return 0;
    if (!(clip_rect[2] > clip_rect[0]) || !(clip_rect[3] > clip_rect[1]))
        return 0;
    scale_x = (float)viewport->width / (float)display_width;
    scale_y = (float)viewport->height / (float)display_height;
    left = floorf(clip_rect[0] * scale_x) + (float)viewport->x;
    top = floorf(clip_rect[1] * scale_y) + (float)viewport->y;
    right = ceilf(clip_rect[2] * scale_x) + (float)viewport->x;
    bottom = ceilf(clip_rect[3] * scale_y) + (float)viewport->y;
    if (!(left == left) || !(top == top) || !(right == right) || !(bottom == bottom))
        return 0;
    min_x = (float)viewport->x;
    min_y = (float)viewport->y;
    max_x = (float)viewport->x + (float)viewport->width;
    max_y = (float)viewport->y + (float)viewport->height;
    if (max_x > (float)framebuffer_width) max_x = (float)framebuffer_width;
    if (max_y > (float)framebuffer_height) max_y = (float)framebuffer_height;
    if (left < min_x) left = min_x;
    if (top < min_y) top = min_y;
    if (right > max_x) right = max_x;
    if (bottom > max_y) bottom = max_y;
    if (!(right > left) || !(bottom > top))
        return 0;
    out->left = (int32_t)left;
    out->top = (int32_t)top;
    out->right = (int32_t)right;
    out->bottom = (int32_t)bottom;
    return 1;
}

static inline int rsx_overlay_clip_to_scissor(
    const float clip_rect[4], uint32_t display_width, uint32_t display_height,
    uint32_t framebuffer_width, uint32_t framebuffer_height, rsx_overlay_scissor* out)
{
    rsx_overlay_viewport viewport;

    viewport.x = 0;
    viewport.y = 0;
    viewport.width = framebuffer_width;
    viewport.height = framebuffer_height;
    return rsx_overlay_clip_to_viewport_scissor(clip_rect, display_width, display_height,
        &viewport, framebuffer_width, framebuffer_height, out);
}

/* {scale.xy, translate.xy}: display pixels to clip space, (0,0) top-left. */
static inline void rsx_overlay_display_transform(uint32_t display_width,
                                                 uint32_t display_height, float out[4])
{
    out[0] = 2.0f / (float)(display_width ? display_width : 1u);
    out[1] = -2.0f / (float)(display_height ? display_height : 1u);
    out[2] = -1.0f;
    out[3] = 1.0f;
}
```

`rsx_overlay_d3d12_packet.h`: replace the last two aliases with

```c
static inline int rsx_overlay_d3d12_clip_to_scissor(
    const float clip_rect[4], uint32_t display_width, uint32_t display_height,
    uint32_t framebuffer_width, uint32_t framebuffer_height,
    rsx_overlay_d3d12_scissor* out)
{
    return rsx_overlay_clip_to_scissor(clip_rect, display_width, display_height,
                                       framebuffer_width, framebuffer_height, out);
}

static inline void rsx_overlay_d3d12_display_transform(uint32_t display_width,
                                                       uint32_t display_height, float out[4])
{
    rsx_overlay_display_transform(display_width, display_height, out);
}
```

`rsx_overlay.cpp`: add after the `s_capabilities` statics:

```cpp
/* Drawable the UI is laid out on (event thread writes, anyone reads). */
static std::atomic<uint32_t> s_display_width(1280u);
static std::atomic<uint32_t> s_display_height(720u);
static std::atomic<float> s_display_scale(1.0f);

void rsx_overlay_set_display(uint32_t width_px, uint32_t height_px, float backing_scale)
{
    if (width_px == 0 || height_px == 0 || width_px > RSX_OVERLAY_DISPLAY_MAX ||
        height_px > RSX_OVERLAY_DISPLAY_MAX || !(backing_scale >= 0.5f && backing_scale <= 4.0f))
        return;
    s_display_width.store(width_px);
    s_display_height.store(height_px);
    s_display_scale.store(backing_scale);
}

void rsx_overlay_get_display(uint32_t* width_px, uint32_t* height_px, float* backing_scale)
{
    if (width_px != nullptr) *width_px = s_display_width.load();
    if (height_px != nullptr) *height_px = s_display_height.load();
    if (backing_scale != nullptr) *backing_scale = s_display_scale.load();
}
```

Change `rsx_overlay_copy_imgui_draw_data` to `static void rsx_overlay_copy_imgui_draw_data(const ImDrawData* draw_data, uint32_t width, uint32_t height, float scale, float open_progress)` and, right after `packet.command_count = 0;`, set:

```cpp
    packet.display_width = width;
    packet.display_height = height;
    packet.backing_scale = scale;
    packet.open_progress = open_progress;
    packet.backdrop_dim = 0.0f;
    packet.backdrop_blur = 0u;
```

In `rsx_overlay_build_draw_data`, replace `io.DisplaySize = ImVec2(RSX_OVERLAY_CANVAS_WIDTH, RSX_OVERLAY_CANVAS_HEIGHT);` with

```cpp
    uint32_t width, height;
    float scale;
    rsx_overlay_get_display(&width, &height, &scale);
    io.DisplaySize = ImVec2((float)width, (float)height);
```

and the copy call with `rsx_overlay_copy_imgui_draw_data(ImGui::GetDrawData(), width, height, scale, s_visible.load() ? 1.0f : 0.0f);`. (The interim tab UI lays out in drawable pixels; Task 7 replaces it.)

`rsx_overlay_metal.m` encode: pass `packet->display_width, packet->display_height` as the 2nd/3rd arguments of `rsx_overlay_clip_to_viewport_scissor`, and replace `rsx_overlay_canvas_transform(xf);` with `rsx_overlay_display_transform(packet->display_width, packet->display_height, xf);`. Update the header comment of `rsx_overlay_metal_renderer_encode` in `rsx_overlay_metal.h` ("canvas" → "the packet's display").

`rsx_overlay_d3d12.c` render: replace `rsx_overlay_d3d12_canvas_transform(transform);` with `rsx_overlay_d3d12_display_transform(packet->display_width, packet->display_height, transform);` and the scissor call with `rsx_overlay_d3d12_clip_to_scissor(command->clip_rect, packet->display_width, packet->display_height, framebuffer_width, framebuffer_height, &scissor)`.

`rsx_metal_backend.m` UI hook (`:9551-9559`): replace with

```objc
        /* Runtime overlay UI: the last pass on the drawable this call
         * presents, after whichever layer was chosen above (movie, content
         * hold, per-draw RT or guest). One composite per present, same
         * command buffer, never a second nextDrawable. The UI is laid out at
         * the drawable's own size, so it covers the whole drawable
         * (letterbox bars included). No-op while hidden. */
        if (s_overlay_ui) {
            rsx_overlay_viewport ui_vp = { 0, 0, dw, dh };
            (void)rsx_overlay_metal_renderer_encode(s_overlay_ui, cb, drawable.texture, ui_vp);
        }
```

In `rsx_metal_backend_pump_messages`, just before `if (rsx_overlay_visible())` at the end, insert:

```objc
    if (s_state.window && pthread_main_np()) {
        int dw = 0, dh = 0, ww = 0, wh = 0;
        SDL_Metal_GetDrawableSize(s_state.window, &dw, &dh);
        SDL_GetWindowSize(s_state.window, &ww, &wh);
        if (dw > 0 && dh > 0 && ww > 0)
            rsx_overlay_set_display((uint32_t)dw, (uint32_t)dh, (float)dw / (float)ww);
    }
```

`rsx_d3d12_backend.c` pump: before `if (rsx_overlay_visible())` insert `rsx_overlay_set_display(s_d3d.width, s_d3d.height, 1.0f);`.

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cmake --build build-macos --target test_rsx_overlay test_rsx_overlay_d3d12 test_rsx_overlay_metal test_pad_overlay_route test_gow2_overlay_provider ps3recomp_runtime && ctest --test-dir build-macos -R 'overlay' --output-on-failure`
Expected: all overlay tests PASS.
Then: `for f in libs/video/rsx_overlay_d3d12.c libs/video/rsx_d3d12_backend.c; do x86_64-w64-mingw32-gcc -fsyntax-only -std=c11 $MW $f || echo FAIL $f; done; x86_64-w64-mingw32-g++ -fsyntax-only -std=c++20 $MW libs/video/rsx_overlay.cpp` — expected: no output.

- [ ] **Step 5: Commit**

```bash
git add libs/video/rsx_overlay.h libs/video/rsx_overlay_packet.h libs/video/rsx_overlay_d3d12_packet.h \
  libs/video/rsx_overlay.cpp libs/video/rsx_overlay_metal.h libs/video/rsx_overlay_metal.m \
  libs/video/rsx_overlay_d3d12.c libs/video/rsx_metal_backend.m libs/video/rsx_d3d12_backend.c \
  libs/video/tests/test_rsx_overlay.c libs/video/tests/test_rsx_overlay_d3d12.c libs/video/tests/test_rsx_overlay_metal.m
git commit -m "feat(overlay): pacote v2 com tamanho real do drawable e escala de backing"
```

---

### Task 2: Settings v2 with v1 upgrade

**Files:**
- Modify: `libs/video/rsx_overlay_settings.h:10,20-39`, `libs/video/rsx_overlay_settings.c:54-131,257-307,309-373,402-470`
- Test: `libs/video/tests/test_rsx_overlay_settings.c` (`settings_match` near `:103`, `test_defaults :116`, `test_unknown_version_uses_defaults :330`)

**Interfaces:**
- Produces: `RSX_OVERLAY_SETTINGS_VERSION 2u`, `RSX_OVERLAY_SETTINGS_MIN_VERSION 1u`; new fields in `rsx_overlay_settings` (after `analog_deadzone`): `uint8_t ui_sounds; uint8_t ui_haptics; uint8_t game_mute; uint8_t vibration;` (defaults 1, 1, 0, 1).

- [ ] **Step 1: Write the failing tests**

In `test_rsx_overlay_settings.c`: extend `settings_match` with `a->ui_sounds == b->ui_sounds && a->ui_haptics == b->ui_haptics && a->game_mute == b->game_mute && a->vibration == b->vibration &&`. In `test_defaults` add

```c
    CHECK(settings.ui_sounds == 1);
    CHECK(settings.ui_haptics == 1);
    CHECK(settings.game_mute == 0);
    CHECK(settings.vibration == 1);
```

In `test_valid_round_trip`, before saving `written`, set `written.ui_sounds = 0; written.ui_haptics = 0; written.game_mute = 1; written.vibration = 0;`. In `test_unknown_version_uses_defaults` change `"version=2\noverlay_enabled=0\n"` to `"version=3\noverlay_enabled=0\n"`. Add two tests and call them from `main` next to the others:

```c
static int test_version1_file_upgrades(const char* dir)
{
    char path[1024];
    rsx_overlay_settings loaded;

    CHECK(path_join(path, sizeof(path), dir, "v1.settings"));
    CHECK(write_text(path, "version=1\nvsync=0\nanalog_deadzone=12\n"));
    CHECK(rsx_overlay_settings_load(&loaded, path) == 1);
    CHECK(loaded.version == RSX_OVERLAY_SETTINGS_VERSION);
    CHECK(loaded.vsync == 0 && loaded.analog_deadzone == 12);
    CHECK(loaded.ui_sounds == 1 && loaded.ui_haptics == 1);
    CHECK(loaded.game_mute == 0 && loaded.vibration == 1);
    /* Saving writes the current version. */
    CHECK(rsx_overlay_settings_save(&loaded, path) == 1);
    CHECK(rsx_overlay_settings_load(&loaded, path) == 1);
    CHECK(loaded.version == RSX_OVERLAY_SETTINGS_VERSION);
    remove(path);
    return 1;
}

static int test_invalid_v2_fields_fall_back_per_field(const char* dir)
{
    char path[1024];
    rsx_overlay_settings loaded;

    CHECK(path_join(path, sizeof(path), dir, "v2-bad.settings"));
    CHECK(write_text(path, "version=2\nui_sounds=7\nui_haptics=0\ngame_mute=x\nvibration=0\n"));
    CHECK(rsx_overlay_settings_load(&loaded, path) == 1);
    CHECK(loaded.ui_sounds == 1);   /* invalid: default */
    CHECK(loaded.ui_haptics == 0);  /* valid: kept */
    CHECK(loaded.game_mute == 0);   /* invalid: default */
    CHECK(loaded.vibration == 0);   /* valid: kept */
    loaded.vibration = 9;
    CHECK(rsx_overlay_settings_validate(&loaded) == 0);
    CHECK(loaded.vibration == 1);
    remove(path);
    return 1;
}
```

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build build-macos --target test_rsx_overlay_settings`
Expected: compile error `no member named 'ui_sounds'`.

- [ ] **Step 3: Implement**

`rsx_overlay_settings.h`: `#define RSX_OVERLAY_SETTINGS_VERSION 2u` plus `#define RSX_OVERLAY_SETTINGS_MIN_VERSION 1u /* older files load; missing keys take defaults */`; after `uint8_t analog_deadzone;` add

```c
    /* v2: console-shell switches. */
    uint8_t ui_sounds;   /* synthesized UI sounds */
    uint8_t ui_haptics;  /* controller tick on UI navigation */
    uint8_t game_mute;   /* runtime mute of game audio (cellAudio) */
    uint8_t vibration;   /* game rumble reaches the controller */
```

`rsx_overlay_settings.c`:
- `rsx_overlay_settings_defaults`: add `out->ui_sounds = 1; out->ui_haptics = 1; out->game_mute = 0; out->vibration = 1;`
- `rsx_overlay_settings_validate`: before the map check add

```c
    if (settings->ui_sounds > 1) { settings->ui_sounds = defaults.ui_sounds; valid = 0; }
    if (settings->ui_haptics > 1) { settings->ui_haptics = defaults.ui_haptics; valid = 0; }
    if (settings->game_mute > 1) { settings->game_mute = defaults.game_mute; valid = 0; }
    if (settings->vibration > 1) { settings->vibration = defaults.vibration; valid = 0; }
```

- `rsx_overlay_settings_parse_line`: version branch becomes

```c
        if (!rsx_overlay_settings_parse_u32(equals, &value) ||
            value < RSX_OVERLAY_SETTINGS_MIN_VERSION || value > RSX_OVERLAY_SETTINGS_VERSION)
            *version_supported = 0;
```

and before the `host_button_to_guest_button` branch add

```c
    } else if (strcmp(line, "ui_sounds") == 0) {
        if (rsx_overlay_settings_parse_bool(equals, &boolean))
            settings->ui_sounds = boolean;
    } else if (strcmp(line, "ui_haptics") == 0) {
        if (rsx_overlay_settings_parse_bool(equals, &boolean))
            settings->ui_haptics = boolean;
    } else if (strcmp(line, "game_mute") == 0) {
        if (rsx_overlay_settings_parse_bool(equals, &boolean))
            settings->game_mute = boolean;
    } else if (strcmp(line, "vibration") == 0) {
        if (rsx_overlay_settings_parse_bool(equals, &boolean))
            settings->vibration = boolean;
```

- `rsx_overlay_settings_load`: after `if (!saw_version || !version_supported) return 0;` add `candidate.version = RSX_OVERLAY_SETTINGS_VERSION; /* v1 upgrades in memory */`.
- `rsx_overlay_settings_save`: extend the format with `"ui_sounds=%u\n" "ui_haptics=%u\n" "game_mute=%u\n" "vibration=%u\n"` after `analog_deadzone=%u\n` and the matching `candidate.ui_sounds, candidate.ui_haptics, candidate.game_mute, candidate.vibration` arguments.

- [ ] **Step 4: Run to verify pass**

Run: `cmake --build build-macos --target test_rsx_overlay_settings test_rsx_overlay && ctest --test-dir build-macos -R '^test_rsx_overlay(_settings)?$' --output-on-failure`
Expected: PASS. Also `x86_64-w64-mingw32-gcc -fsyntax-only -std=c11 $MW libs/video/rsx_overlay_settings.c` — no output.

- [ ] **Step 5: Commit**

```bash
git add libs/video/rsx_overlay_settings.h libs/video/rsx_overlay_settings.c libs/video/tests/test_rsx_overlay_settings.c
git commit -m "feat(overlay): configurações v2 (sons, vibração, mudo) com upgrade da v1"
```

---

### Task 3: Shell pure logic (state machine, focus, springs, layout)

**Files:**
- Create: `libs/video/rsx_overlay_feedback.h`, `libs/video/rsx_overlay_shell.h`, `libs/video/rsx_overlay_shell.cpp`
- Create: `libs/video/tests/test_rsx_overlay_shell.c`
- Modify: `CMakeLists.txt:143` (append source), test block next to `test_rsx_overlay` (`:624`)

**Interfaces:**
- Consumes: `rsx_overlay_setting_support` values from `rsx_overlay.h`.
- Produces (all `extern "C"`): the full contents of `rsx_overlay_feedback.h` and `rsx_overlay_shell.h` below. Later tasks use exactly these names.

**Boundary contract (shell ↔ core).** The shell is pure navigation: it never reads or writes settings, never clamps values and never calls platform code. Everything it knows about the world comes in through `rsx_shell_model` (capability support, current window-size index, list sizes) and `rsx_shell_input`; everything it wants done goes out as `rsx_shell_action`s plus a feedback mask. The **core** (Task 7, `overlay_apply_actions`) owns all setting semantics: toggles, the 0–30 % dead-zone clamp, applying graphics through capabilities, persisting, and calling consumers. The only shared helper is `rsx_shell_remap_assign`, a pure function on a map array that the core calls on its own settings. Consequently Task 3's tests assert *which action is emitted* and *which level/row/card is focused*; Task 7's tests assert *what the action does to settings and consumers*. A reviewer can reject either side independently.

- [ ] **Step 1: Write the headers (interfaces only, no behavior)**

`libs/video/rsx_overlay_feedback.h`:

```c
#ifndef PS3RECOMP_RSX_OVERLAY_FEEDBACK_H
#define PS3RECOMP_RSX_OVERLAY_FEEDBACK_H

/* UI feedback events shared by the shell (which emits them), the overlay core
 * (which forwards them to a sink) and the platform players (sound, haptics). */
typedef enum rsx_overlay_feedback_event {
    RSX_OVERLAY_FEEDBACK_OPEN = 0,
    RSX_OVERLAY_FEEDBACK_CLOSE,
    RSX_OVERLAY_FEEDBACK_MOVE,
    RSX_OVERLAY_FEEDBACK_CONFIRM,
    RSX_OVERLAY_FEEDBACK_BACK,
    RSX_OVERLAY_FEEDBACK_ERROR,
    RSX_OVERLAY_FEEDBACK_COUNT
} rsx_overlay_feedback_event;

#define RSX_OVERLAY_FEEDBACK_BIT(e) (1u << (unsigned)(e))

/* Called on the overlay's event thread; must not block. `sounds`/`haptics`
 * are the user's switches at the time of the event. */
typedef void (*rsx_overlay_feedback_sink)(int event, int sounds, int haptics, void* userdata);

#endif
```

`libs/video/rsx_overlay_shell.h`:

```c
#ifndef PS3RECOMP_RSX_OVERLAY_SHELL_H
#define PS3RECOMP_RSX_OVERLAY_SHELL_H

/* Console shell: pure logic, no ImGui, no platform. Time is passed in
 * (seconds, monotonic), so every value is deterministic in tests. */

#include <stdint.h>

#include "rsx_overlay.h"
#include "rsx_overlay_feedback.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum rsx_shell_phase {
    RSX_SHELL_CLOSED = 0, RSX_SHELL_OPENING, RSX_SHELL_OPEN, RSX_SHELL_CLOSING
} rsx_shell_phase;

typedef enum rsx_shell_card {
    RSX_SHELL_CARD_RESUME = 0, RSX_SHELL_CARD_DISPLAY, RSX_SHELL_CARD_CONTROLS,
    RSX_SHELL_CARD_SOUND, RSX_SHELL_CARD_DEVELOPER, RSX_SHELL_CARD_QUIT,
    RSX_SHELL_CARD_COUNT
} rsx_shell_card;

typedef enum rsx_shell_level {
    RSX_SHELL_LEVEL_CARDS = 0, RSX_SHELL_LEVEL_PANEL, RSX_SHELL_LEVEL_LIST,
    RSX_SHELL_LEVEL_CAPTURE, RSX_SHELL_LEVEL_DIALOG
} rsx_shell_level;

typedef enum rsx_shell_list {
    RSX_SHELL_LIST_NONE = 0, RSX_SHELL_LIST_WINDOW_SIZE, RSX_SHELL_LIST_REMAP, RSX_SHELL_LIST_LOGS
} rsx_shell_list;

typedef enum rsx_shell_row {
    RSX_SHELL_ROW_FULLSCREEN = 0, RSX_SHELL_ROW_VSYNC, RSX_SHELL_ROW_WINDOW_SIZE,
    RSX_SHELL_ROW_DEADZONE, RSX_SHELL_ROW_VIBRATION, RSX_SHELL_ROW_REMAP, RSX_SHELL_ROW_RESTORE,
    RSX_SHELL_ROW_GAME_SOUND, RSX_SHELL_ROW_UI_SOUNDS, RSX_SHELL_ROW_UI_HAPTICS,
    RSX_SHELL_ROW_LOGS, RSX_SHELL_ROW_COUNT
} rsx_shell_row;

enum {
    RSX_SHELL_IN_UP = 1u << 0, RSX_SHELL_IN_DOWN = 1u << 1, RSX_SHELL_IN_LEFT = 1u << 2,
    RSX_SHELL_IN_RIGHT = 1u << 3, RSX_SHELL_IN_CONFIRM = 1u << 4, RSX_SHELL_IN_BACK = 1u << 5
};

typedef struct rsx_shell_input {
    uint32_t held;        /* RSX_SHELL_IN_* down now (keyboard + pad) */
    uint32_t pressed;     /* RSX_SHELL_IN_* pressed since the last update */
    uint32_t key_pressed; /* subset of `pressed` that came from the keyboard */
    uint16_t pad_pressed; /* raw host pad bits (CELL_PAD_CTRL_*) newly pressed */
} rsx_shell_input;

typedef enum rsx_shell_action_kind {
    RSX_SHELL_ACT_NONE = 0, RSX_SHELL_ACT_CLOSE, RSX_SHELL_ACT_QUIT,
    RSX_SHELL_ACT_TOGGLE_FULLSCREEN, RSX_SHELL_ACT_TOGGLE_VSYNC, RSX_SHELL_ACT_SET_WINDOW_SIZE,
    RSX_SHELL_ACT_DEADZONE_STEP, RSX_SHELL_ACT_TOGGLE_VIBRATION, RSX_SHELL_ACT_REMAP,
    RSX_SHELL_ACT_RESTORE_CONTROLS, RSX_SHELL_ACT_TOGGLE_GAME_SOUND,
    RSX_SHELL_ACT_TOGGLE_UI_SOUNDS, RSX_SHELL_ACT_TOGGLE_UI_HAPTICS
} rsx_shell_action_kind;

/* SET_WINDOW_SIZE: a = index; DEADZONE_STEP: a = -1/+1; REMAP: a = guest bit, b = host bit. */
typedef struct rsx_shell_action { int kind; int a; int b; } rsx_shell_action;

#define RSX_SHELL_MAX_ACTIONS 8
typedef struct rsx_shell_output {
    rsx_shell_action actions[RSX_SHELL_MAX_ACTIONS];
    int count;
    uint32_t feedback; /* RSX_OVERLAY_FEEDBACK_BIT(...) mask */
} rsx_shell_output;

typedef struct rsx_shell_model {
    uint8_t support_fullscreen;  /* rsx_overlay_setting_support */
    uint8_t support_vsync;
    uint8_t support_window_size;
    uint8_t window_size_index;   /* 0..3, 255 when the size is not in the list */
    uint32_t log_lines;          /* entries in the logs list */
    uint32_t list_visible_rows;  /* rows the panel shows (from the layout), >= 1 */
} rsx_shell_model;

typedef struct rsx_spring { float value; float velocity; float target; } rsx_spring;

#define RSX_SHELL_WINDOW_SIZE_COUNT 4
#define RSX_SHELL_REMAP_COUNT 16
#define RSX_SHELL_DEADZONE_MAX 30
#define RSX_SHELL_CAPTURE_TIMEOUT_S 5.0
#define RSX_SHELL_OPEN_OMEGA 22.0f  /* ~95 % at 220 ms, critically damped */
#define RSX_SHELL_CARD_OMEGA 28.0f
#define RSX_SHELL_FOCUS_SCALE 1.18f
#define RSX_SHELL_FADE_S 0.12
#define RSX_SHELL_REPEAT_DELAY_S 0.40
#define RSX_SHELL_REPEAT_INTERVAL_S 0.10
#define RSX_SHELL_BACKDROP_DIM 0.35f

/* Layout, in points (multiplied by the layout's px_per_pt). */
#define RSX_SHELL_CARD_PT 96.0f
#define RSX_SHELL_CARD_GAP_PT 20.0f
#define RSX_SHELL_MARGIN_PT 48.0f
#define RSX_SHELL_CARD_BOTTOM_PT 72.0f
#define RSX_SHELL_RISE_PT 40.0f
#define RSX_SHELL_STRIP_PT 44.0f
#define RSX_SHELL_BAND_PT 260.0f
#define RSX_SHELL_PANEL_W_PT 380.0f
#define RSX_SHELL_PANEL_WIDE_W_PT 600.0f
#define RSX_SHELL_PANEL_HEADER_PT 52.0f
#define RSX_SHELL_PANEL_PAD_PT 12.0f
#define RSX_SHELL_PANEL_GAP_PT 20.0f
#define RSX_SHELL_ROW_H_PT 44.0f
#define RSX_SHELL_CARD_RADIUS_PT 14.0f
#define RSX_SHELL_NEED_W_PT 792.0f /* 2 margins + 6 cards + 4 gaps + 2 gaps before Sair */
#define RSX_SHELL_NEED_H_PT 480.0f

typedef struct rsx_shell {
    rsx_shell_phase phase;
    rsx_shell_level level;
    int card;
    int row;               /* index into rsx_shell_panel_rows(card) */
    rsx_shell_list list;
    int list_index;
    int list_scroll;
    int capture_guest;
    double capture_started;
    int dialog_choice;     /* 0 = Sair, 1 = Cancelar */
    double last_time;
    double panel_switch_time;
    double repeat_next;
    uint32_t repeat_dir;
    uint32_t pending_feedback;
    rsx_spring open;
    rsx_spring card_scale[RSX_SHELL_CARD_COUNT];
} rsx_shell;

typedef struct rsx_shell_rect { float x, y, w, h; } rsx_shell_rect;

typedef struct rsx_shell_layout {
    float px_per_pt;
    float width, height;
    rsx_shell_rect top_strip;
    rsx_shell_rect band;
    rsx_shell_rect cards[RSX_SHELL_CARD_COUNT]; /* unscaled rects, risen by open progress */
    rsx_shell_rect panel;
    int panel_visible_rows;
} rsx_shell_layout;

extern const uint32_t rsx_shell_window_sizes[RSX_SHELL_WINDOW_SIZE_COUNT][2];
/* pt-BR names of the 16 pad buttons in CELL_PAD_CTRL_* bit order. */
extern const char* const rsx_shell_button_names[RSX_SHELL_REMAP_COUNT];

void rsx_spring_step(rsx_spring* spring, float omega, float dt);
void rsx_shell_init(rsx_shell* shell);
void rsx_shell_open(rsx_shell* shell, double now);
void rsx_shell_close(rsx_shell* shell, double now);
void rsx_shell_update(rsx_shell* shell, const rsx_shell_input* input,
                      const rsx_shell_model* model, double now, rsx_shell_output* out);
/* Interactive rows of a card's panel; rows == NULL returns the count. */
int rsx_shell_panel_rows(int card, rsx_shell_row* rows, int capacity);
int rsx_shell_row_enabled(rsx_shell_row row, const rsx_shell_model* model);
/* Rows the panel displays (for the layout) and whether it is the wide one. */
int rsx_shell_panel_display_rows(const rsx_shell* shell, const rsx_shell_model* model);
int rsx_shell_panel_wide(const rsx_shell* shell);
float rsx_shell_panel_fade(const rsx_shell* shell, double now);
/* 1 while a direction is held (a slider may still be moving). */
int rsx_shell_direction_held(const rsx_shell* shell);
/* Binds host bit `host_index` to guest bit `guest_index`, swapping with the
 * previous owner so the map stays a bijection. */
void rsx_shell_remap_assign(uint16_t map[RSX_SHELL_REMAP_COUNT], unsigned guest_index,
                            unsigned host_index);
void rsx_shell_compute_layout(float width_px, float height_px, float backing_scale,
                              int focused_card, int panel_rows, int wide_panel,
                              float open_progress, rsx_shell_layout* out);

#ifdef __cplusplus
}
#endif

#endif
```

- [ ] **Step 2: Write the failing tests**

`libs/video/tests/test_rsx_overlay_shell.c`:

```c
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "cellPad.h"
#include "rsx_overlay_shell.h"

#define CHECK(expr) do { \
    if (!(expr)) { fprintf(stderr, "[FAIL] %s:%d: %s\n", __func__, __LINE__, #expr); return 0; } \
} while (0)
#define NEAR(a, b) (fabs((double)(a) - (double)(b)) < 0.01)

static rsx_shell_model live_model(void)
{
    rsx_shell_model m;
    memset(&m, 0, sizeof m);
    m.support_fullscreen = m.support_vsync = m.support_window_size = RSX_OVERLAY_SETTING_LIVE;
    m.window_size_index = 0;
    m.log_lines = 10;
    m.list_visible_rows = 4;
    return m;
}

static rsx_shell_output step(rsx_shell* s, const rsx_shell_model* m, double t,
                             uint32_t pressed, uint32_t held, uint16_t pad)
{
    rsx_shell_input in;
    rsx_shell_output out;
    memset(&in, 0, sizeof in);
    in.pressed = pressed;
    in.key_pressed = pad ? 0 : pressed;
    in.held = held | pressed;
    in.pad_pressed = pad;
    rsx_shell_update(s, &in, m, t, &out);
    return out;
}

/* Press and release one key; returns the press frame's output. */
static rsx_shell_output tap(rsx_shell* s, const rsx_shell_model* m, double* t, uint32_t bit)
{
    rsx_shell_output o = step(s, m, *t, bit, 0, 0);
    *t += 0.05;
    step(s, m, *t, 0, 0, 0);
    *t += 0.05;
    return o;
}

static int has_action(const rsx_shell_output* o, int kind, int a, int b)
{
    for (int i = 0; i < o->count; ++i)
        if (o->actions[i].kind == kind && o->actions[i].a == a && o->actions[i].b == b)
            return 1;
    return 0;
}

static void settle_open(rsx_shell* s, const rsx_shell_model* m, double* t)
{
    rsx_shell_open(s, *t);
    for (int i = 0; i < 40; ++i) { step(s, m, *t, 0, 0, 0); *t += 1.0 / 60.0; }
}

static int test_spring_is_analytic_and_deterministic(void)
{
    rsx_spring a = { 0.0f, 0.0f, 1.0f }, b = { 0.0f, 0.0f, 1.0f };
    const float w = RSX_SHELL_OPEN_OMEGA;
    rsx_spring_step(&a, w, 0.22f);
    for (int i = 0; i < 22; ++i) rsx_spring_step(&b, w, 0.01f);
    CHECK(fabsf(a.value - b.value) < 1e-4f);
    CHECK(fabsf(a.value - (1.0f - (1.0f + w * 0.22f) * expf(-w * 0.22f))) < 1e-5f);
    CHECK(a.value > 0.94f && a.value < 0.97f); /* ~95 % at 220 ms */
    /* Critically damped from rest: never overshoots. */
    rsx_spring c = { 0.0f, 0.0f, 1.0f };
    for (int i = 0; i < 200; ++i) { rsx_spring_step(&c, w, 0.005f); CHECK(c.value <= 1.0f + 1e-6f); }
    rsx_spring_step(&c, w, 0.0f); /* dt 0: no change */
    return 1;
}

static int test_open_settles_and_close_returns_to_closed(void)
{
    rsx_shell s; rsx_shell_model m = live_model(); double t = 10.0;
    rsx_shell_init(&s);
    CHECK(s.phase == RSX_SHELL_CLOSED);
    rsx_shell_open(&s, t);
    rsx_shell_output o = step(&s, &m, t, 0, 0, 0);
    CHECK(o.feedback & RSX_OVERLAY_FEEDBACK_BIT(RSX_OVERLAY_FEEDBACK_OPEN));
    CHECK(s.phase == RSX_SHELL_OPENING && s.card == RSX_SHELL_CARD_RESUME);
    for (int i = 0; i < 30; ++i) { t += 1.0 / 60.0; step(&s, &m, t, 0, 0, 0); }
    CHECK(s.phase == RSX_SHELL_OPEN && s.open.value == 1.0f);
    CHECK(NEAR(s.card_scale[RSX_SHELL_CARD_RESUME].value, RSX_SHELL_FOCUS_SCALE));
    rsx_shell_close(&s, t);
    o = step(&s, &m, t, 0, 0, 0);
    CHECK(o.feedback & RSX_OVERLAY_FEEDBACK_BIT(RSX_OVERLAY_FEEDBACK_CLOSE));
    CHECK(s.phase == RSX_SHELL_CLOSING);
    for (int i = 0; i < 40; ++i) { t += 1.0 / 60.0; step(&s, &m, t, 0, 0, 0); }
    CHECK(s.phase == RSX_SHELL_CLOSED && s.open.value == 0.0f && s.level == RSX_SHELL_LEVEL_CARDS);
    return 1;
}

static int test_reopen_during_close_resumes(void)
{
    rsx_shell s; rsx_shell_model m = live_model(); double t = 0.0;
    rsx_shell_init(&s);
    settle_open(&s, &m, &t);
    tap(&s, &m, &t, RSX_SHELL_IN_RIGHT);
    rsx_shell_close(&s, t);
    step(&s, &m, t, 0, 0, 0);
    t += 0.05;
    step(&s, &m, t, 0, 0, 0);
    const float mid = s.open.value;
    CHECK(mid > 0.0f && mid < 1.0f);
    /* Input is ignored while closing. */
    rsx_shell_output o = step(&s, &m, t, RSX_SHELL_IN_RIGHT, 0, 0);
    CHECK(o.count == 0 && s.card == RSX_SHELL_CARD_DISPLAY);
    rsx_shell_open(&s, t);
    CHECK(s.phase == RSX_SHELL_OPENING);
    CHECK(fabsf(s.open.value - mid) < 0.05f);
    CHECK(s.card == RSX_SHELL_CARD_RESUME && s.level == RSX_SHELL_LEVEL_CARDS);
    return 1;
}

static int test_card_navigation_clamps_and_repeats(void)
{
    rsx_shell s; rsx_shell_model m = live_model(); double t = 0.0;
    rsx_shell_init(&s);
    settle_open(&s, &m, &t);
    rsx_shell_output o = step(&s, &m, t, RSX_SHELL_IN_RIGHT, 0, 0);
    CHECK(s.card == 1 && (o.feedback & RSX_OVERLAY_FEEDBACK_BIT(RSX_OVERLAY_FEEDBACK_MOVE)));
    const double t0 = t;
    step(&s, &m, t0 + 0.39, 0, RSX_SHELL_IN_RIGHT, 0);
    CHECK(s.card == 1);
    step(&s, &m, t0 + 0.41, 0, RSX_SHELL_IN_RIGHT, 0);
    CHECK(s.card == 2);
    step(&s, &m, t0 + 0.52, 0, RSX_SHELL_IN_RIGHT, 0);
    CHECK(s.card == 3);
    step(&s, &m, t0 + 0.60, 0, 0, 0);
    step(&s, &m, t0 + 2.00, 0, 0, 0);
    CHECK(s.card == 3 && !rsx_shell_direction_held(&s));
    t = t0 + 2.1;
    for (int i = 0; i < 5; ++i) tap(&s, &m, &t, RSX_SHELL_IN_LEFT);
    CHECK(s.card == 0);
    o = tap(&s, &m, &t, RSX_SHELL_IN_LEFT);
    CHECK(s.card == 0 && !(o.feedback & RSX_OVERLAY_FEEDBACK_BIT(RSX_OVERLAY_FEEDBACK_MOVE)));
    for (int i = 0; i < 8; ++i) tap(&s, &m, &t, RSX_SHELL_IN_RIGHT);
    CHECK(s.card == RSX_SHELL_CARD_QUIT);
    return 1;
}

static int test_resume_confirm_and_back_close(void)
{
    rsx_shell s; rsx_shell_model m = live_model(); double t = 0.0;
    rsx_shell_init(&s);
    settle_open(&s, &m, &t);
    rsx_shell_output o = tap(&s, &m, &t, RSX_SHELL_IN_CONFIRM);
    CHECK(has_action(&o, RSX_SHELL_ACT_CLOSE, 0, 0) && s.phase == RSX_SHELL_CLOSING);
    for (int i = 0; i < 40; ++i) { t += 1.0 / 60.0; step(&s, &m, t, 0, 0, 0); }
    settle_open(&s, &m, &t);
    tap(&s, &m, &t, RSX_SHELL_IN_RIGHT);
    o = tap(&s, &m, &t, RSX_SHELL_IN_BACK);
    CHECK(has_action(&o, RSX_SHELL_ACT_CLOSE, 0, 0));
    return 1;
}

static int test_panel_skips_unavailable_rows(void)
{
    rsx_shell s; rsx_shell_model m = live_model(); double t = 0.0;
    m.support_fullscreen = RSX_OVERLAY_SETTING_UNAVAILABLE;
    rsx_shell_init(&s);
    settle_open(&s, &m, &t);
    tap(&s, &m, &t, RSX_SHELL_IN_RIGHT);
    tap(&s, &m, &t, RSX_SHELL_IN_CONFIRM);
    CHECK(s.level == RSX_SHELL_LEVEL_PANEL && s.row == 1); /* VSync */
    tap(&s, &m, &t, RSX_SHELL_IN_DOWN);
    CHECK(s.row == 2);
    tap(&s, &m, &t, RSX_SHELL_IN_DOWN);
    CHECK(s.row == 2);
    tap(&s, &m, &t, RSX_SHELL_IN_UP);
    CHECK(s.row == 1);
    tap(&s, &m, &t, RSX_SHELL_IN_UP);
    CHECK(s.level == RSX_SHELL_LEVEL_CARDS);
    tap(&s, &m, &t, RSX_SHELL_IN_DOWN);
    CHECK(s.level == RSX_SHELL_LEVEL_PANEL && s.row == 1);
    rsx_shell_output o = tap(&s, &m, &t, RSX_SHELL_IN_CONFIRM);
    CHECK(has_action(&o, RSX_SHELL_ACT_TOGGLE_VSYNC, 0, 0));
    tap(&s, &m, &t, RSX_SHELL_IN_BACK);
    CHECK(s.level == RSX_SHELL_LEVEL_CARDS && s.phase == RSX_SHELL_OPEN);
    /* A card whose rows are all unavailable cannot be entered. */
    m.support_vsync = m.support_window_size = RSX_OVERLAY_SETTING_UNAVAILABLE;
    o = tap(&s, &m, &t, RSX_SHELL_IN_CONFIRM);
    CHECK(s.level == RSX_SHELL_LEVEL_CARDS &&
          (o.feedback & RSX_OVERLAY_FEEDBACK_BIT(RSX_OVERLAY_FEEDBACK_ERROR)));
    return 1;
}

static int test_window_size_list(void)
{
    rsx_shell s; rsx_shell_model m = live_model(); double t = 0.0;
    m.window_size_index = 2;
    rsx_shell_init(&s);
    settle_open(&s, &m, &t);
    tap(&s, &m, &t, RSX_SHELL_IN_RIGHT);
    tap(&s, &m, &t, RSX_SHELL_IN_CONFIRM);
    tap(&s, &m, &t, RSX_SHELL_IN_DOWN);
    tap(&s, &m, &t, RSX_SHELL_IN_DOWN);
    tap(&s, &m, &t, RSX_SHELL_IN_CONFIRM);
    CHECK(s.level == RSX_SHELL_LEVEL_LIST && s.list == RSX_SHELL_LIST_WINDOW_SIZE && s.list_index == 2);
    tap(&s, &m, &t, RSX_SHELL_IN_DOWN);
    tap(&s, &m, &t, RSX_SHELL_IN_DOWN);
    CHECK(s.list_index == 3);
    rsx_shell_output o = tap(&s, &m, &t, RSX_SHELL_IN_CONFIRM);
    CHECK(has_action(&o, RSX_SHELL_ACT_SET_WINDOW_SIZE, 3, 0) && s.level == RSX_SHELL_LEVEL_PANEL);
    CHECK(rsx_shell_window_sizes[3][0] == 2560u && rsx_shell_window_sizes[3][1] == 1440u);
    return 1;
}

static int test_deadzone_slider_steps(void)
{
    rsx_shell s; rsx_shell_model m = live_model(); double t = 0.0;
    rsx_shell_init(&s);
    settle_open(&s, &m, &t);
    tap(&s, &m, &t, RSX_SHELL_IN_RIGHT);
    tap(&s, &m, &t, RSX_SHELL_IN_RIGHT);
    tap(&s, &m, &t, RSX_SHELL_IN_CONFIRM);
    CHECK(s.level == RSX_SHELL_LEVEL_PANEL && s.row == 0);
    rsx_shell_output o = tap(&s, &m, &t, RSX_SHELL_IN_RIGHT);
    CHECK(has_action(&o, RSX_SHELL_ACT_DEADZONE_STEP, 1, 0));
    o = tap(&s, &m, &t, RSX_SHELL_IN_LEFT);
    CHECK(has_action(&o, RSX_SHELL_ACT_DEADZONE_STEP, -1, 0));
    return 1;
}

static int test_remap_capture(void)
{
    rsx_shell s; rsx_shell_model m = live_model(); double t = 0.0;
    rsx_shell_init(&s);
    settle_open(&s, &m, &t);
    tap(&s, &m, &t, RSX_SHELL_IN_RIGHT);
    tap(&s, &m, &t, RSX_SHELL_IN_RIGHT);
    tap(&s, &m, &t, RSX_SHELL_IN_CONFIRM);
    tap(&s, &m, &t, RSX_SHELL_IN_DOWN);
    tap(&s, &m, &t, RSX_SHELL_IN_DOWN);
    tap(&s, &m, &t, RSX_SHELL_IN_CONFIRM);
    CHECK(s.level == RSX_SHELL_LEVEL_LIST && s.list == RSX_SHELL_LIST_REMAP);
    for (int i = 0; i < 14; ++i) tap(&s, &m, &t, RSX_SHELL_IN_DOWN);
    CHECK(s.list_index == 14 && s.list_scroll == 11); /* 4 visible rows: 11..14 */
    tap(&s, &m, &t, RSX_SHELL_IN_CONFIRM);
    CHECK(s.level == RSX_SHELL_LEVEL_CAPTURE && s.capture_guest == 14);
    /* Pad Circle is captured, not treated as Back. */
    rsx_shell_output o = step(&s, &m, t, RSX_SHELL_IN_BACK, 0, CELL_PAD_CTRL_CIRCLE);
    CHECK(has_action(&o, RSX_SHELL_ACT_REMAP, 14, 13) && s.level == RSX_SHELL_LEVEL_LIST);
    t += 0.1;
    tap(&s, &m, &t, RSX_SHELL_IN_CONFIRM);
    CHECK(s.level == RSX_SHELL_LEVEL_CAPTURE);
    o = tap(&s, &m, &t, RSX_SHELL_IN_BACK); /* keyboard Esc cancels */
    CHECK(o.count == 0 && s.level == RSX_SHELL_LEVEL_LIST);
    tap(&s, &m, &t, RSX_SHELL_IN_CONFIRM);
    o = step(&s, &m, t + RSX_SHELL_CAPTURE_TIMEOUT_S + 0.1, 0, 0, 0);
    CHECK(o.count == 0 && s.level == RSX_SHELL_LEVEL_LIST &&
          (o.feedback & RSX_OVERLAY_FEEDBACK_BIT(RSX_OVERLAY_FEEDBACK_ERROR)));
    return 1;
}

static int test_remap_assign_keeps_bijection(void)
{
    uint16_t map[16];
    uint32_t seen = 0;
    for (unsigned i = 0; i < 16; ++i) map[i] = (uint16_t)(1u << i);
    rsx_shell_remap_assign(map, 14, 1); /* Cruz <- host L3 */
    CHECK(map[1] == CELL_PAD_CTRL_CROSS && map[14] == CELL_PAD_CTRL_L3);
    rsx_shell_remap_assign(map, 14, 1);
    CHECK(map[1] == CELL_PAD_CTRL_CROSS && map[14] == CELL_PAD_CTRL_L3);
    rsx_shell_remap_assign(map, 99, 1);  /* out of range: ignored */
    rsx_shell_remap_assign(map, 1, 99);
    for (unsigned i = 0; i < 16; ++i) { CHECK((map[i] & (map[i] - 1u)) == 0 && !(seen & map[i])); seen |= map[i]; }
    CHECK(seen == 0xFFFFu);
    return 1;
}

static int test_quit_dialog(void)
{
    rsx_shell s; rsx_shell_model m = live_model(); double t = 0.0;
    rsx_shell_init(&s);
    settle_open(&s, &m, &t);
    for (int i = 0; i < 5; ++i) tap(&s, &m, &t, RSX_SHELL_IN_RIGHT);
    rsx_shell_output o = tap(&s, &m, &t, RSX_SHELL_IN_CONFIRM);
    CHECK(s.level == RSX_SHELL_LEVEL_DIALOG && s.dialog_choice == 1 && o.count == 0);
    o = tap(&s, &m, &t, RSX_SHELL_IN_CONFIRM); /* Cancelar */
    CHECK(s.level == RSX_SHELL_LEVEL_CARDS && o.count == 0);
    tap(&s, &m, &t, RSX_SHELL_IN_CONFIRM);
    tap(&s, &m, &t, RSX_SHELL_IN_LEFT);
    CHECK(s.dialog_choice == 0);
    o = tap(&s, &m, &t, RSX_SHELL_IN_CONFIRM);
    CHECK(has_action(&o, RSX_SHELL_ACT_QUIT, 0, 0) && s.phase == RSX_SHELL_CLOSING);
    return 1;
}

static int test_logs_list_scrolls(void)
{
    rsx_shell s; rsx_shell_model m = live_model(); double t = 0.0;
    m.log_lines = 64;
    m.list_visible_rows = 6;
    rsx_shell_init(&s);
    settle_open(&s, &m, &t);
    for (int i = 0; i < 4; ++i) tap(&s, &m, &t, RSX_SHELL_IN_RIGHT);
    tap(&s, &m, &t, RSX_SHELL_IN_CONFIRM);
    CHECK(s.level == RSX_SHELL_LEVEL_PANEL);
    tap(&s, &m, &t, RSX_SHELL_IN_CONFIRM);
    CHECK(s.level == RSX_SHELL_LEVEL_LIST && s.list == RSX_SHELL_LIST_LOGS);
    for (int i = 0; i < 10; ++i) tap(&s, &m, &t, RSX_SHELL_IN_DOWN);
    CHECK(s.list_index == 10 && s.list_scroll == 5);
    for (int i = 0; i < 10; ++i) tap(&s, &m, &t, RSX_SHELL_IN_UP);
    CHECK(s.list_index == 0 && s.list_scroll == 0);
    CHECK(rsx_shell_panel_display_rows(&s, &m) == 64 && rsx_shell_panel_wide(&s));
    return 1;
}

static int test_panel_fade(void)
{
    rsx_shell s; rsx_shell_model m = live_model(); double t = 0.0;
    rsx_shell_init(&s);
    settle_open(&s, &m, &t);
    step(&s, &m, t, RSX_SHELL_IN_RIGHT, 0, 0);
    CHECK(rsx_shell_panel_fade(&s, t) < 0.01f);
    CHECK(NEAR(rsx_shell_panel_fade(&s, t + 0.06), 0.5f));
    CHECK(rsx_shell_panel_fade(&s, t + 0.2) == 1.0f);
    return 1;
}

static int test_layout_reference_1280x720(void)
{
    rsx_shell_layout L;
    rsx_shell_compute_layout(1280.0f, 720.0f, 1.0f, RSX_SHELL_CARD_DISPLAY, 3, 0, 1.0f, &L);
    CHECK(NEAR(L.px_per_pt, 1.0f));
    CHECK(NEAR(L.cards[0].x, 48) && NEAR(L.cards[1].x, 164) && NEAR(L.cards[4].x, 512));
    CHECK(NEAR(L.cards[RSX_SHELL_CARD_QUIT].x, 1136) && NEAR(L.cards[0].y, 552) && NEAR(L.cards[0].w, 96));
    CHECK(NEAR(L.top_strip.h, 44) && NEAR(L.band.y, 460));
    CHECK(NEAR(L.panel.x, 48) && NEAR(L.panel.w, 380) && NEAR(L.panel.h, 208));
    CHECK(NEAR(L.panel.y, 315.36) && L.panel_visible_rows == 3);
    rsx_shell_compute_layout(1280.0f, 720.0f, 1.0f, 0, 3, 0, 0.0f, &L);
    CHECK(NEAR(L.cards[0].y, 592)); /* closed: sunk by the rise */
    return 1;
}

static int test_layout_retina_doubles(void)
{
    rsx_shell_layout a, b;
    rsx_shell_compute_layout(1280.0f, 720.0f, 1.0f, 2, 4, 0, 1.0f, &a);
    rsx_shell_compute_layout(2560.0f, 1440.0f, 2.0f, 2, 4, 0, 1.0f, &b);
    CHECK(NEAR(b.px_per_pt, 2.0f));
    for (int i = 0; i < RSX_SHELL_CARD_COUNT; ++i)
        CHECK(NEAR(b.cards[i].x, 2 * a.cards[i].x) && NEAR(b.cards[i].y, 2 * a.cards[i].y));
    CHECK(NEAR(b.panel.x, 2 * a.panel.x) && NEAR(b.panel.y, 2 * a.panel.y) && NEAR(b.panel.h, 2 * a.panel.h));
    return 1;
}

static int test_layout_extremes_fit(void)
{
    static const float sizes[][3] = { { 640, 480, 1 }, { 3440 * 2, 1440 * 2, 2 }, { 2880, 1800, 2 }, { 1280, 720, 2 } };
    for (unsigned k = 0; k < sizeof sizes / sizeof sizes[0]; ++k) {
        for (int card = 0; card < RSX_SHELL_CARD_COUNT; ++card) {
            rsx_shell_layout L;
            const float W = sizes[k][0], H = sizes[k][1];
            rsx_shell_compute_layout(W, H, sizes[k][2], card, 16, card == RSX_SHELL_CARD_DEVELOPER, 1.0f, &L);
            for (int i = 0; i < RSX_SHELL_CARD_COUNT; ++i) {
                CHECK(L.cards[i].x >= 0 && L.cards[i].x + L.cards[i].w <= W);
                CHECK(L.cards[i].y >= 0 && L.cards[i].y + L.cards[i].h <= H);
                if (i > 0)
                    CHECK(L.cards[i].x > L.cards[i - 1].x + L.cards[i - 1].w);
            }
            CHECK(L.panel.x >= 0 && L.panel.x + L.panel.w <= W + 0.01f);
            CHECK(L.panel.y >= L.top_strip.h - 0.01f);
            CHECK(L.panel.y + L.panel.h <= L.cards[card].y + 0.01f);
            CHECK(L.panel_visible_rows >= 1 && L.panel_visible_rows <= 16);
        }
    }
    rsx_shell_layout small;
    rsx_shell_compute_layout(640.0f, 480.0f, 1.0f, RSX_SHELL_CARD_CONTROLS, 16, 0, 1.0f, &small);
    CHECK(small.panel_visible_rows < 16);
    return 1;
}

int main(void)
{
    int ok = 1;
    ok &= test_spring_is_analytic_and_deterministic();
    ok &= test_open_settles_and_close_returns_to_closed();
    ok &= test_reopen_during_close_resumes();
    ok &= test_card_navigation_clamps_and_repeats();
    ok &= test_resume_confirm_and_back_close();
    ok &= test_panel_skips_unavailable_rows();
    ok &= test_window_size_list();
    ok &= test_deadzone_slider_steps();
    ok &= test_remap_capture();
    ok &= test_remap_assign_keeps_bijection();
    ok &= test_quit_dialog();
    ok &= test_logs_list_scrolls();
    ok &= test_panel_fade();
    ok &= test_layout_reference_1280x720();
    ok &= test_layout_retina_doubles();
    ok &= test_layout_extremes_fit();
    printf("test_rsx_overlay_shell: %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
```

CMake — after the `test_rsx_overlay` block (`CMakeLists.txt:636`):

```cmake
    add_executable(test_rsx_overlay_shell
        libs/video/tests/test_rsx_overlay_shell.c
        libs/video/rsx_overlay_shell.cpp)
    target_include_directories(test_rsx_overlay_shell PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/libs/video"
        "${CMAKE_CURRENT_SOURCE_DIR}/libs/input"
        "${CMAKE_CURRENT_SOURCE_DIR}/include")
    set_target_properties(test_rsx_overlay_shell PROPERTIES LINKER_LANGUAGE CXX)
    add_test(NAME test_rsx_overlay_shell COMMAND test_rsx_overlay_shell)
```

and after `CMakeLists.txt:143` add `list(APPEND HLE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/libs/video/rsx_overlay_shell.cpp")`.

- [ ] **Step 3: Run to verify failure**

Run: `cmake --build build-macos --target test_rsx_overlay_shell`
Expected: link errors (undefined `rsx_shell_*`) — `rsx_overlay_shell.cpp` does not exist yet (create it empty first if CMake refuses to configure: `: > libs/video/rsx_overlay_shell.cpp`).

- [ ] **Step 4: Implement `libs/video/rsx_overlay_shell.cpp`**

```cpp
#include "rsx_overlay_shell.h"

#include <cmath>
#include <cstring>

const uint32_t rsx_shell_window_sizes[RSX_SHELL_WINDOW_SIZE_COUNT][2] = {
    { 1280u, 720u }, { 1600u, 900u }, { 1920u, 1080u }, { 2560u, 1440u }
};

const char* const rsx_shell_button_names[RSX_SHELL_REMAP_COUNT] = {
    "Select", "L3", "R3", "Start", "Cima", "Direita", "Baixo", "Esquerda",
    "L2", "R2", "L1", "R1", "Tri\xC3\xA2ngulo", "C\xC3\xADrculo", "Cruz", "Quadrado"
};

static const uint32_t kDirs = RSX_SHELL_IN_UP | RSX_SHELL_IN_DOWN | RSX_SHELL_IN_LEFT | RSX_SHELL_IN_RIGHT;

static float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

void rsx_spring_step(rsx_spring* s, float omega, float dt)
{
    if (s == nullptr || !(dt > 0.0f))
        return;
    /* Critically damped closed form: exact for any dt, so the result depends
     * only on elapsed time, never on the frame rate. */
    const float x0 = s->value - s->target;
    const float v0 = s->velocity;
    const float e = std::exp(-omega * dt);
    const float c = v0 + omega * x0;
    s->value = s->target + (x0 + c * dt) * e;
    s->velocity = (v0 - omega * c * dt) * e;
}

void rsx_shell_init(rsx_shell* s)
{
    if (s == nullptr)
        return;
    std::memset(s, 0, sizeof(*s));
    s->phase = RSX_SHELL_CLOSED;
    s->dialog_choice = 1;
    for (int i = 0; i < RSX_SHELL_CARD_COUNT; ++i) {
        s->card_scale[i].value = 1.0f;
        s->card_scale[i].target = 1.0f;
    }
}

int rsx_shell_panel_rows(int card, rsx_shell_row* rows, int capacity)
{
    static const rsx_shell_row display[] = { RSX_SHELL_ROW_FULLSCREEN, RSX_SHELL_ROW_VSYNC, RSX_SHELL_ROW_WINDOW_SIZE };
    static const rsx_shell_row controls[] = { RSX_SHELL_ROW_DEADZONE, RSX_SHELL_ROW_VIBRATION, RSX_SHELL_ROW_REMAP, RSX_SHELL_ROW_RESTORE };
    static const rsx_shell_row sound[] = { RSX_SHELL_ROW_GAME_SOUND, RSX_SHELL_ROW_UI_SOUNDS, RSX_SHELL_ROW_UI_HAPTICS };
    static const rsx_shell_row developer[] = { RSX_SHELL_ROW_LOGS };
    const rsx_shell_row* src = nullptr;
    int n = 0;
    switch (card) {
    case RSX_SHELL_CARD_DISPLAY: src = display; n = 3; break;
    case RSX_SHELL_CARD_CONTROLS: src = controls; n = 4; break;
    case RSX_SHELL_CARD_SOUND: src = sound; n = 3; break;
    case RSX_SHELL_CARD_DEVELOPER: src = developer; n = 1; break;
    default: return 0;
    }
    if (rows != nullptr)
        for (int i = 0; i < n && i < capacity; ++i)
            rows[i] = src[i];
    return n;
}

int rsx_shell_row_enabled(rsx_shell_row row, const rsx_shell_model* m)
{
    if (m == nullptr)
        return 1;
    switch (row) {
    case RSX_SHELL_ROW_FULLSCREEN: return m->support_fullscreen != RSX_OVERLAY_SETTING_UNAVAILABLE;
    case RSX_SHELL_ROW_VSYNC: return m->support_vsync != RSX_OVERLAY_SETTING_UNAVAILABLE;
    case RSX_SHELL_ROW_WINDOW_SIZE: return m->support_window_size != RSX_OVERLAY_SETTING_UNAVAILABLE;
    default: return 1;
    }
}

static int list_count(rsx_shell_list list, const rsx_shell_model* m)
{
    switch (list) {
    case RSX_SHELL_LIST_WINDOW_SIZE: return RSX_SHELL_WINDOW_SIZE_COUNT;
    case RSX_SHELL_LIST_REMAP: return RSX_SHELL_REMAP_COUNT;
    case RSX_SHELL_LIST_LOGS: return m != nullptr ? (int)m->log_lines : 0;
    default: return 0;
    }
}

int rsx_shell_panel_display_rows(const rsx_shell* s, const rsx_shell_model* m)
{
    if (s->level == RSX_SHELL_LEVEL_LIST || s->level == RSX_SHELL_LEVEL_CAPTURE) {
        const int n = list_count(s->list, m);
        return n < 1 ? 1 : n;
    }
    switch (s->card) {
    case RSX_SHELL_CARD_RESUME:
    case RSX_SHELL_CARD_QUIT: return 1;
    case RSX_SHELL_CARD_DEVELOPER: return 6; /* five metrics + Logs */
    default: return rsx_shell_panel_rows(s->card, nullptr, 0);
    }
}

int rsx_shell_panel_wide(const rsx_shell* s) { return s->card == RSX_SHELL_CARD_DEVELOPER; }

float rsx_shell_panel_fade(const rsx_shell* s, double now)
{
    return clamp01((float)((now - s->panel_switch_time) / RSX_SHELL_FADE_S));
}

int rsx_shell_direction_held(const rsx_shell* s) { return s->repeat_dir != 0; }

void rsx_shell_remap_assign(uint16_t map[RSX_SHELL_REMAP_COUNT], unsigned guest_index, unsigned host_index)
{
    if (map == nullptr || guest_index >= RSX_SHELL_REMAP_COUNT || host_index >= RSX_SHELL_REMAP_COUNT)
        return;
    const uint16_t guest = (uint16_t)(1u << guest_index);
    for (unsigned owner = 0; owner < RSX_SHELL_REMAP_COUNT; ++owner) {
        if (map[owner] != guest)
            continue;
        if (owner != host_index) {
            map[owner] = map[host_index];
            map[host_index] = guest;
        }
        return;
    }
}

void rsx_shell_open(rsx_shell* s, double now)
{
    if (s->phase == RSX_SHELL_OPEN || s->phase == RSX_SHELL_OPENING)
        return;
    if (s->phase == RSX_SHELL_CLOSED)
        s->last_time = now;
    s->phase = RSX_SHELL_OPENING;
    s->level = RSX_SHELL_LEVEL_CARDS;
    s->card = RSX_SHELL_CARD_RESUME;
    s->row = 0;
    s->list = RSX_SHELL_LIST_NONE;
    s->list_index = 0;
    s->list_scroll = 0;
    s->dialog_choice = 1;
    s->repeat_dir = 0;
    s->panel_switch_time = now;
    s->open.target = 1.0f;
    s->pending_feedback |= RSX_OVERLAY_FEEDBACK_BIT(RSX_OVERLAY_FEEDBACK_OPEN);
}

void rsx_shell_close(rsx_shell* s, double now)
{
    (void)now;
    if (s->phase != RSX_SHELL_OPEN && s->phase != RSX_SHELL_OPENING)
        return;
    s->phase = RSX_SHELL_CLOSING;
    s->open.target = 0.0f;
    s->repeat_dir = 0;
    s->pending_feedback |= RSX_OVERLAY_FEEDBACK_BIT(RSX_OVERLAY_FEEDBACK_CLOSE);
}

static void emit(rsx_shell_output* out, int kind, int a, int b)
{
    if (out->count < RSX_SHELL_MAX_ACTIONS) {
        out->actions[out->count].kind = kind;
        out->actions[out->count].a = a;
        out->actions[out->count].b = b;
        ++out->count;
    }
}

static void fb(rsx_shell_output* out, int event) { out->feedback |= RSX_OVERLAY_FEEDBACK_BIT(event); }

static void begin_close(rsx_shell* s, rsx_shell_output* out, int kind)
{
    emit(out, kind, 0, 0);
    s->phase = RSX_SHELL_CLOSING;
    s->open.target = 0.0f;
    s->repeat_dir = 0;
    fb(out, RSX_OVERLAY_FEEDBACK_CLOSE);
}

static uint32_t dir_events(rsx_shell* s, const rsx_shell_input* in, double now)
{
    uint32_t ev = in->pressed & kDirs;
    const uint32_t held = in->held & kDirs;
    if (ev != 0) {
        s->repeat_dir = ev & (~ev + 1u); /* lowest pressed direction repeats */
        s->repeat_next = now + RSX_SHELL_REPEAT_DELAY_S;
    } else if (s->repeat_dir != 0 && (held & s->repeat_dir) != 0) {
        if (now >= s->repeat_next) {
            ev = s->repeat_dir;
            s->repeat_next = now + RSX_SHELL_REPEAT_INTERVAL_S;
        }
    } else {
        s->repeat_dir = 0;
    }
    return ev;
}

static int step_row(const rsx_shell* s, const rsx_shell_model* m, int from, int delta)
{
    rsx_shell_row rows[8];
    const int n = rsx_shell_panel_rows(s->card, rows, 8);
    for (int i = from + delta; i >= 0 && i < n; i += delta)
        if (rsx_shell_row_enabled(rows[i], m))
            return i;
    return -1;
}

static void fix_scroll(rsx_shell* s, const rsx_shell_model* m)
{
    const int visible = m->list_visible_rows < 1 ? 1 : (int)m->list_visible_rows;
    if (s->list_index < s->list_scroll) s->list_scroll = s->list_index;
    if (s->list_index >= s->list_scroll + visible) s->list_scroll = s->list_index - visible + 1;
    if (s->list_scroll < 0) s->list_scroll = 0;
}

static void open_list(rsx_shell* s, rsx_shell_list list, int index, double now)
{
    s->level = RSX_SHELL_LEVEL_LIST;
    s->list = list;
    s->list_index = index;
    s->list_scroll = 0;
    s->panel_switch_time = now;
}

static void confirm_row(rsx_shell* s, const rsx_shell_model* m, double now, rsx_shell_output* out)
{
    rsx_shell_row rows[8];
    const int n = rsx_shell_panel_rows(s->card, rows, 8);
    if (s->row < 0 || s->row >= n)
        return;
    fb(out, RSX_OVERLAY_FEEDBACK_CONFIRM);
    switch (rows[s->row]) {
    case RSX_SHELL_ROW_FULLSCREEN: emit(out, RSX_SHELL_ACT_TOGGLE_FULLSCREEN, 0, 0); break;
    case RSX_SHELL_ROW_VSYNC: emit(out, RSX_SHELL_ACT_TOGGLE_VSYNC, 0, 0); break;
    case RSX_SHELL_ROW_WINDOW_SIZE:
        open_list(s, RSX_SHELL_LIST_WINDOW_SIZE,
                  m->window_size_index < RSX_SHELL_WINDOW_SIZE_COUNT ? m->window_size_index : 0, now);
        fix_scroll(s, m);
        break;
    case RSX_SHELL_ROW_VIBRATION: emit(out, RSX_SHELL_ACT_TOGGLE_VIBRATION, 0, 0); break;
    case RSX_SHELL_ROW_REMAP: open_list(s, RSX_SHELL_LIST_REMAP, 0, now); break;
    case RSX_SHELL_ROW_RESTORE: emit(out, RSX_SHELL_ACT_RESTORE_CONTROLS, 0, 0); break;
    case RSX_SHELL_ROW_GAME_SOUND: emit(out, RSX_SHELL_ACT_TOGGLE_GAME_SOUND, 0, 0); break;
    case RSX_SHELL_ROW_UI_SOUNDS: emit(out, RSX_SHELL_ACT_TOGGLE_UI_SOUNDS, 0, 0); break;
    case RSX_SHELL_ROW_UI_HAPTICS: emit(out, RSX_SHELL_ACT_TOGGLE_UI_HAPTICS, 0, 0); break;
    case RSX_SHELL_ROW_LOGS: open_list(s, RSX_SHELL_LIST_LOGS, 0, now); break;
    default: break; /* DEADZONE: left/right only */
    }
}

static void handle_input(rsx_shell* s, const rsx_shell_input* in, const rsx_shell_model* m,
                         double now, rsx_shell_output* out)
{
    const uint32_t dir = dir_events(s, in, now);
    const uint32_t btn = in->pressed & (RSX_SHELL_IN_CONFIRM | RSX_SHELL_IN_BACK);

    switch (s->level) {
    case RSX_SHELL_LEVEL_CARDS: {
        int next = s->card + ((dir & RSX_SHELL_IN_RIGHT) ? 1 : 0) - ((dir & RSX_SHELL_IN_LEFT) ? 1 : 0);
        if (next < 0) next = 0;
        if (next >= RSX_SHELL_CARD_COUNT) next = RSX_SHELL_CARD_COUNT - 1;
        if (next != s->card) {
            s->card = next;
            s->panel_switch_time = now;
            fb(out, RSX_OVERLAY_FEEDBACK_MOVE);
        }
        if (btn & RSX_SHELL_IN_CONFIRM) {
            if (s->card == RSX_SHELL_CARD_RESUME) {
                begin_close(s, out, RSX_SHELL_ACT_CLOSE);
                return;
            }
            if (s->card == RSX_SHELL_CARD_QUIT) {
                s->level = RSX_SHELL_LEVEL_DIALOG;
                s->dialog_choice = 1;
                fb(out, RSX_OVERLAY_FEEDBACK_CONFIRM);
                return;
            }
        }
        if ((btn & RSX_SHELL_IN_CONFIRM) || (dir & RSX_SHELL_IN_DOWN)) {
            if (s->card == RSX_SHELL_CARD_RESUME || s->card == RSX_SHELL_CARD_QUIT)
                return;
            const int first = step_row(s, m, -1, 1);
            if (first < 0) {
                fb(out, RSX_OVERLAY_FEEDBACK_ERROR);
                return;
            }
            s->level = RSX_SHELL_LEVEL_PANEL;
            s->row = first;
            fb(out, (btn & RSX_SHELL_IN_CONFIRM) ? RSX_OVERLAY_FEEDBACK_CONFIRM : RSX_OVERLAY_FEEDBACK_MOVE);
            return;
        }
        if (btn & RSX_SHELL_IN_BACK)
            begin_close(s, out, RSX_SHELL_ACT_CLOSE);
        return;
    }
    case RSX_SHELL_LEVEL_PANEL: {
        rsx_shell_row rows[8];
        const int n = rsx_shell_panel_rows(s->card, rows, 8);
        if (dir & RSX_SHELL_IN_UP) {
            const int prev = step_row(s, m, s->row, -1);
            if (prev < 0) s->level = RSX_SHELL_LEVEL_CARDS; else s->row = prev;
            fb(out, RSX_OVERLAY_FEEDBACK_MOVE);
            return;
        }
        if (dir & RSX_SHELL_IN_DOWN) {
            const int nxt = step_row(s, m, s->row, 1);
            if (nxt >= 0) { s->row = nxt; fb(out, RSX_OVERLAY_FEEDBACK_MOVE); }
            return;
        }
        if ((dir & (RSX_SHELL_IN_LEFT | RSX_SHELL_IN_RIGHT)) && s->row < n &&
            rows[s->row] == RSX_SHELL_ROW_DEADZONE) {
            emit(out, RSX_SHELL_ACT_DEADZONE_STEP, (dir & RSX_SHELL_IN_RIGHT) ? 1 : -1, 0);
            fb(out, RSX_OVERLAY_FEEDBACK_MOVE);
            return;
        }
        if (btn & RSX_SHELL_IN_CONFIRM) { confirm_row(s, m, now, out); return; }
        if (btn & RSX_SHELL_IN_BACK) { s->level = RSX_SHELL_LEVEL_CARDS; fb(out, RSX_OVERLAY_FEEDBACK_BACK); }
        return;
    }
    case RSX_SHELL_LEVEL_LIST: {
        const int count = list_count(s->list, m);
        const int before = s->list_index;
        if ((dir & RSX_SHELL_IN_UP) && s->list_index > 0) --s->list_index;
        if ((dir & RSX_SHELL_IN_DOWN) && s->list_index + 1 < count) ++s->list_index;
        if (s->list_index != before) { fix_scroll(s, m); fb(out, RSX_OVERLAY_FEEDBACK_MOVE); }
        if (btn & RSX_SHELL_IN_CONFIRM) {
            if (s->list == RSX_SHELL_LIST_WINDOW_SIZE) {
                emit(out, RSX_SHELL_ACT_SET_WINDOW_SIZE, s->list_index, 0);
                s->level = RSX_SHELL_LEVEL_PANEL;
                s->list = RSX_SHELL_LIST_NONE;
                fb(out, RSX_OVERLAY_FEEDBACK_CONFIRM);
            } else if (s->list == RSX_SHELL_LIST_REMAP) {
                s->level = RSX_SHELL_LEVEL_CAPTURE;
                s->capture_guest = s->list_index;
                s->capture_started = now;
                fb(out, RSX_OVERLAY_FEEDBACK_CONFIRM);
            }
            return;
        }
        if (btn & RSX_SHELL_IN_BACK) {
            s->level = RSX_SHELL_LEVEL_PANEL;
            s->list = RSX_SHELL_LIST_NONE;
            s->panel_switch_time = now;
            fb(out, RSX_OVERLAY_FEEDBACK_BACK);
        }
        return;
    }
    case RSX_SHELL_LEVEL_CAPTURE:
        if (in->key_pressed & RSX_SHELL_IN_BACK) {
            s->level = RSX_SHELL_LEVEL_LIST;
            fb(out, RSX_OVERLAY_FEEDBACK_BACK);
        } else if (in->pad_pressed != 0) {
            unsigned host = 0;
            while (((in->pad_pressed >> host) & 1u) == 0)
                ++host;
            emit(out, RSX_SHELL_ACT_REMAP, s->capture_guest, (int)host);
            s->level = RSX_SHELL_LEVEL_LIST;
            fb(out, RSX_OVERLAY_FEEDBACK_CONFIRM);
        } else if (now - s->capture_started >= RSX_SHELL_CAPTURE_TIMEOUT_S) {
            s->level = RSX_SHELL_LEVEL_LIST;
            fb(out, RSX_OVERLAY_FEEDBACK_ERROR);
        }
        return;
    case RSX_SHELL_LEVEL_DIALOG:
        if (dir & (RSX_SHELL_IN_LEFT | RSX_SHELL_IN_RIGHT)) {
            s->dialog_choice ^= 1;
            fb(out, RSX_OVERLAY_FEEDBACK_MOVE);
        }
        if (btn & RSX_SHELL_IN_CONFIRM) {
            if (s->dialog_choice == 0) {
                fb(out, RSX_OVERLAY_FEEDBACK_CONFIRM);
                begin_close(s, out, RSX_SHELL_ACT_QUIT);
            } else {
                s->level = RSX_SHELL_LEVEL_CARDS;
                fb(out, RSX_OVERLAY_FEEDBACK_BACK);
            }
        } else if (btn & RSX_SHELL_IN_BACK) {
            s->level = RSX_SHELL_LEVEL_CARDS;
            fb(out, RSX_OVERLAY_FEEDBACK_BACK);
        }
        return;
    }
}

void rsx_shell_update(rsx_shell* s, const rsx_shell_input* in, const rsx_shell_model* m,
                      double now, rsx_shell_output* out)
{
    std::memset(out, 0, sizeof(*out));
    out->feedback = s->pending_feedback;
    s->pending_feedback = 0;
    float dt = (float)(now - s->last_time);
    if (!(dt > 0.0f)) dt = 0.0f;
    if (dt > 0.1f) dt = 0.1f;
    s->last_time = now;
    if (s->phase == RSX_SHELL_CLOSED)
        return;
    if ((s->phase == RSX_SHELL_OPENING || s->phase == RSX_SHELL_OPEN) && in != nullptr && m != nullptr)
        handle_input(s, in, m, now, out);
    rsx_spring_step(&s->open, RSX_SHELL_OPEN_OMEGA, dt);
    for (int i = 0; i < RSX_SHELL_CARD_COUNT; ++i) {
        s->card_scale[i].target = (i == s->card && s->phase != RSX_SHELL_CLOSING) ? RSX_SHELL_FOCUS_SCALE : 1.0f;
        rsx_spring_step(&s->card_scale[i], RSX_SHELL_CARD_OMEGA, dt);
    }
    if (s->phase == RSX_SHELL_OPENING && s->open.value >= 0.995f) {
        s->phase = RSX_SHELL_OPEN;
        s->open.value = 1.0f;
        s->open.velocity = 0.0f;
    } else if (s->phase == RSX_SHELL_CLOSING && s->open.value <= 0.005f) {
        s->phase = RSX_SHELL_CLOSED;
        s->open.value = 0.0f;
        s->open.velocity = 0.0f;
        s->level = RSX_SHELL_LEVEL_CARDS;
        s->list = RSX_SHELL_LIST_NONE;
    }
}

void rsx_shell_compute_layout(float W, float H, float scale, int card, int rows, int wide,
                              float open, rsx_shell_layout* L)
{
    std::memset(L, 0, sizeof(*L));
    if (!(scale > 0.0f)) scale = 1.0f;
    if (!(W > 0.0f) || !(H > 0.0f)) return;
    const float wpt = W / scale, hpt = H / scale;
    float fit = 1.0f;
    if (wpt < RSX_SHELL_NEED_W_PT) fit = wpt / RSX_SHELL_NEED_W_PT;
    if (hpt < RSX_SHELL_NEED_H_PT && hpt / RSX_SHELL_NEED_H_PT < fit) fit = hpt / RSX_SHELL_NEED_H_PT;
    const float u = scale * fit;
    L->px_per_pt = u;
    L->width = W;
    L->height = H;
    L->top_strip = { 0.0f, 0.0f, W, RSX_SHELL_STRIP_PT * u };
    L->band = { 0.0f, H - RSX_SHELL_BAND_PT * u, W, RSX_SHELL_BAND_PT * u };
    if (L->band.y < 0.0f) { L->band.y = 0.0f; L->band.h = H; }
    const float top = H - (RSX_SHELL_CARD_BOTTOM_PT + RSX_SHELL_CARD_PT) * u +
                      (1.0f - clamp01(open)) * RSX_SHELL_RISE_PT * u;
    for (int i = 0; i < RSX_SHELL_CARD_QUIT; ++i)
        L->cards[i] = { (RSX_SHELL_MARGIN_PT + i * (RSX_SHELL_CARD_PT + RSX_SHELL_CARD_GAP_PT)) * u,
                        top, RSX_SHELL_CARD_PT * u, RSX_SHELL_CARD_PT * u };
    L->cards[RSX_SHELL_CARD_QUIT] = { W - (RSX_SHELL_MARGIN_PT + RSX_SHELL_CARD_PT) * u, top,
                                      RSX_SHELL_CARD_PT * u, RSX_SHELL_CARD_PT * u };
    if (card < 0 || card >= RSX_SHELL_CARD_COUNT) card = 0;
    float pw = (wide ? RSX_SHELL_PANEL_WIDE_W_PT : RSX_SHELL_PANEL_W_PT) * u;
    const float max_w = W - 2.0f * RSX_SHELL_MARGIN_PT * u;
    if (pw > max_w) pw = max_w;
    const rsx_shell_rect& c = L->cards[card];
    const float card_top = c.y - c.h * (RSX_SHELL_FOCUS_SCALE - 1.0f) * 0.5f;
    const float bottom = card_top - RSX_SHELL_PANEL_GAP_PT * u;
    const float min_y = L->top_strip.h + 8.0f * u;
    const float chrome = (RSX_SHELL_PANEL_HEADER_PT + 2.0f * RSX_SHELL_PANEL_PAD_PT) * u;
    int fit_rows = (int)std::floor((bottom - min_y - chrome) / (RSX_SHELL_ROW_H_PT * u));
    if (fit_rows < 1) fit_rows = 1;
    int visible = rows < 1 ? 1 : rows;
    if (visible > fit_rows) visible = fit_rows;
    const float ph = chrome + (float)visible * RSX_SHELL_ROW_H_PT * u;
    float px = c.x + c.w * 0.5f - pw * 0.5f;
    if (px < RSX_SHELL_MARGIN_PT * u) px = RSX_SHELL_MARGIN_PT * u;
    if (px + pw > W - RSX_SHELL_MARGIN_PT * u) px = W - RSX_SHELL_MARGIN_PT * u - pw;
    float py = bottom - ph;
    if (py < min_y) py = min_y;
    L->panel = { px, py, pw, ph };
    L->panel_visible_rows = visible;
}
```

- [ ] **Step 5: Run to verify pass**

Run: `cmake --build build-macos --target test_rsx_overlay_shell && ctest --test-dir build-macos -R '^test_rsx_overlay_shell$' --output-on-failure`
Expected: `test_rsx_overlay_shell: PASS`. Then `x86_64-w64-mingw32-g++ -fsyntax-only -std=c++20 $MW libs/video/rsx_overlay_shell.cpp` — no output.

- [ ] **Step 6: Commit**

```bash
git add libs/video/rsx_overlay_feedback.h libs/video/rsx_overlay_shell.h libs/video/rsx_overlay_shell.cpp \
  libs/video/tests/test_rsx_overlay_shell.c CMakeLists.txt
git commit -m "feat(overlay): lógica pura da central (estados, foco, molas, layout)"
```

---
### Task 4: Platform assets (SF Pro / SF Symbols atlas, embedded Inter fallback)

**Files:**
- Create: `third_party/inter/Inter-Regular.ttf`, `third_party/inter/Inter-SemiBold.ttf`, `third_party/inter/LICENSE.txt`, `third_party/inter/README.ps3recomp.md`
- Create: `cmake/embed_file.cmake`
- Create: `libs/video/rsx_overlay_assets.h`, `libs/video/rsx_overlay_assets.c`, `libs/video/rsx_overlay_assets_apple.m`
- Create: `libs/video/tests/test_rsx_overlay_assets.c`, `libs/video/tests/test_rsx_overlay_assets_apple.m`
- Modify: `libs/video/rsx_overlay.h` (asset builder typedef + generation), `libs/video/rsx_overlay.cpp:214-226` (init font capture), `libs/video/rsx_overlay_metal.h:34-40` (source), `libs/video/rsx_overlay_metal.m:69-84,149-200,213-220` (generation reload)
- Modify: `CMakeLists.txt` (fonts object library before `add_library(ps3recomp_runtime` `:234`; Apple sources `:162-167`; ARC list `:241-246`; frameworks `:255-264`; tests), `games/gow2/build_macos.sh:573-577`
- Test: `libs/video/tests/test_rsx_overlay_metal.m` (generation reload)

**Interfaces:**
- Produces (`rsx_overlay_assets.h`):

```c
typedef enum rsx_overlay_text_style {
    RSX_OVERLAY_TEXT_TITLE = 0,  /* 17 pt semibold */
    RSX_OVERLAY_TEXT_ROW,        /* 15 pt regular */
    RSX_OVERLAY_TEXT_LABEL,      /* 11 pt medium (Inter: semibold) */
    RSX_OVERLAY_TEXT_CAPTION,    /* 13 pt regular */
    RSX_OVERLAY_TEXT_STYLE_COUNT
} rsx_overlay_text_style;
typedef enum rsx_overlay_icon {
    RSX_OVERLAY_ICON_PLAY = 0, RSX_OVERLAY_ICON_DISPLAY, RSX_OVERLAY_ICON_GAMEPAD,
    RSX_OVERLAY_ICON_SPEAKER, RSX_OVERLAY_ICON_HAMMER, RSX_OVERLAY_ICON_POWER, RSX_OVERLAY_ICON_COUNT
} rsx_overlay_icon;
#define RSX_OVERLAY_GLYPH_COUNT 194   /* U+0020..007E, U+00A0..00FF, U+2026, U+2013, U+2014 */
#define RSX_OVERLAY_ICON_POINT_SIZE 30.0f
typedef struct rsx_overlay_glyph { uint16_t x, y, w, h; int16_t left, top; float advance; uint8_t present; } rsx_overlay_glyph;
typedef struct rsx_overlay_assets { /* see Step 3 */ } rsx_overlay_assets;
int      rsx_overlay_glyph_index(uint32_t codepoint);
uint32_t rsx_overlay_glyph_codepoint(int index);
uint32_t rsx_overlay_utf8_next(const char** text);
float    rsx_overlay_text_point_size(int style);
float    rsx_overlay_text_width(const rsx_overlay_assets* a, int style, const char* utf8, float k);
int      rsx_overlay_assets_build_embedded(rsx_overlay_assets* out, float scale);
int      rsx_overlay_assets_build_apple(rsx_overlay_assets* out, float scale); /* Apple only */
void     rsx_overlay_assets_free(rsx_overlay_assets* a);
```

- Produces (`rsx_overlay.h`): `typedef struct rsx_overlay_assets rsx_overlay_assets; typedef int (*rsx_overlay_asset_builder)(rsx_overlay_assets* out, float scale); uint64_t rsx_overlay_font_atlas_generation(void);`
- Produces (`rsx_overlay_metal.h`): new last member of `rsx_overlay_metal_source`: `uint64_t (*atlas_generation)(void* userdata); /* NULL: atlas never changes */`.

- [ ] **Step 1: Vendor Inter and the embed script**

```bash
S=$(mktemp -d)
curl -sSL -o "$S/inter.zip" https://github.com/rsms/inter/releases/download/v4.1/Inter-4.1.zip
echo "9883fdd4a49d4fb66bd8177ba6625ef9a64aa45899767dde3d36aa425756b11e  $S/inter.zip" | shasum -a 256 -c
unzip -q -o "$S/inter.zip" extras/ttf/Inter-Regular.ttf extras/ttf/Inter-SemiBold.ttf LICENSE.txt -d "$S/x"
mkdir -p third_party/inter
cp "$S/x/extras/ttf/Inter-Regular.ttf" "$S/x/extras/ttf/Inter-SemiBold.ttf" "$S/x/LICENSE.txt" third_party/inter/
shasum -a 256 third_party/inter/*
```

Expected hashes: Regular `40d692fce188e4471e2b3cba937be967878f631ad3ebbbdcd587687c7ebe0c82`, SemiBold `78a843fade9d4612a5567302fb595b56976eb5fcebf4fea5a5912d638bafcde3`, LICENSE `262481e844521b326f5ecd053e59b98c8b2da78c8ee1bdbb6e8174305e54935a`.

`third_party/inter/README.ps3recomp.md`:

```markdown
# Inter font pin

Fallback typeface of the runtime overlay shell (used when SF Pro is not
available, and always on Windows/Linux).

- Upstream: <https://github.com/rsms/inter>, release `v4.1` (`Inter-4.1.zip`,
  SHA-256 `9883fdd4a49d4fb66bd8177ba6625ef9a64aa45899767dde3d36aa425756b11e`).
- Files: `extras/ttf/Inter-Regular.ttf`, `extras/ttf/Inter-SemiBold.ttf`, `LICENSE.txt`.
- License: SIL Open Font License 1.1 (`LICENSE.txt`, unmodified).
- Embedded at build time by `cmake/embed_file.cmake` into the
  `ps3recomp_overlay_fonts` object library; nothing is modified.
```

`cmake/embed_file.cmake`:

```cmake
# Usage: cmake -DINPUT=<file> -DOUTPUT=<file.c> -DSYMBOL=<name> -P embed_file.cmake
# Emits `const unsigned char SYMBOL[]` and `const size_t SYMBOL_size`.
file(READ "${INPUT}" hex HEX)
string(LENGTH "${hex}" hex_len)
math(EXPR size "${hex_len} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
file(WRITE "${OUTPUT}"
    "/* Generated from ${INPUT} by cmake/embed_file.cmake. Do not edit. */\n"
    "#include <stddef.h>\n"
    "const unsigned char ${SYMBOL}[] = {${bytes}};\n"
    "const size_t ${SYMBOL}_size = ${size};\n")
```

- [ ] **Step 2: Write the failing tests**

`libs/video/tests/test_rsx_overlay_assets.c`:

```c
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "rsx_overlay_assets.h"

#define CHECK(expr) do { \
    if (!(expr)) { fprintf(stderr, "[FAIL] %s:%d: %s\n", __func__, __LINE__, #expr); return 0; } \
} while (0)

static int test_glyph_set_and_utf8(void)
{
    const char* s = "a\xC3\xA3\xE2\x80\xA6\xE4\xB8\x80\xFF";
    CHECK(rsx_overlay_glyph_index(' ') == 0);
    CHECK(rsx_overlay_glyph_index('~') == 94);
    CHECK(rsx_overlay_glyph_index(0xA0) == 95 && rsx_overlay_glyph_index(0xFF) == 190);
    CHECK(rsx_overlay_glyph_index(0x2026) == 191 && rsx_overlay_glyph_index(0x2014) == 193);
    CHECK(rsx_overlay_glyph_index(0x7F) == -1 && rsx_overlay_glyph_index(0x4E00) == -1);
    for (int i = 0; i < RSX_OVERLAY_GLYPH_COUNT; ++i)
        CHECK(rsx_overlay_glyph_index(rsx_overlay_glyph_codepoint(i)) == i);
    CHECK(rsx_overlay_utf8_next(&s) == 'a');
    CHECK(rsx_overlay_utf8_next(&s) == 0xE3);
    CHECK(rsx_overlay_utf8_next(&s) == 0x2026);
    CHECK(rsx_overlay_utf8_next(&s) == 0x4E00);
    CHECK(rsx_overlay_utf8_next(&s) == 0xFFFD && *s == '\0'); /* invalid byte: one step */
    return 1;
}

static int test_embedded_atlas(float scale, rsx_overlay_assets* out)
{
    CHECK(rsx_overlay_assets_build_embedded(out, scale) == 1);
    CHECK(out->rgba != NULL && out->width >= 1024 && out->width <= 4096 && out->height == out->width);
    CHECK(out->scale == scale && out->system_font == 0 && out->system_icons == 0);
    /* The white block is opaque: the shell's solid shapes sample it. */
    CHECK(out->rgba[((size_t)(out->white_y + 1) * out->width + out->white_x + 1) * 4 + 3] == 255);
    const int A = rsx_overlay_glyph_index('A'), atilde = rsx_overlay_glyph_index(0xE3);
    for (int st = 0; st < RSX_OVERLAY_TEXT_STYLE_COUNT; ++st) {
        CHECK(out->glyphs[st][A].present && out->glyphs[st][A].w > 0 && out->glyphs[st][A].h > 0);
        CHECK(out->glyphs[st][atilde].present);
        CHECK(out->glyphs[st][rsx_overlay_glyph_index(' ')].advance > 0.0f);
        CHECK(out->ascent[st] > 0.0f && out->descent[st] > 0.0f);
    }
    CHECK(out->line_height[RSX_OVERLAY_TEXT_TITLE] > out->line_height[RSX_OVERLAY_TEXT_LABEL]);
    return 1;
}

static int test_embedded_scales_with_backing(void)
{
    rsx_overlay_assets one, two;
    CHECK(test_embedded_atlas(1.0f, &one));
    CHECK(test_embedded_atlas(2.0f, &two));
    const int H = rsx_overlay_glyph_index('H');
    const float r = (float)two.glyphs[RSX_OVERLAY_TEXT_ROW][H].h / (float)one.glyphs[RSX_OVERLAY_TEXT_ROW][H].h;
    CHECK(r > 1.8f && r < 2.2f);
    const float w1 = rsx_overlay_text_width(&one, RSX_OVERLAY_TEXT_ROW, "Tela cheia", 1.0f);
    const float w2 = rsx_overlay_text_width(&two, RSX_OVERLAY_TEXT_ROW, "Tela cheia", 1.0f);
    CHECK(w1 > 0.0f && fabsf(w2 - 2.0f * w1) < 3.0f);
    /* k scales measurement; unknown code points measure as '?'. */
    CHECK(fabsf(rsx_overlay_text_width(&two, RSX_OVERLAY_TEXT_ROW, "Tela cheia", 0.5f) - 0.5f * w2) < 0.01f);
    CHECK(rsx_overlay_text_width(&one, RSX_OVERLAY_TEXT_ROW, "\xE4\xB8\x80", 1.0f) ==
          rsx_overlay_text_width(&one, RSX_OVERLAY_TEXT_ROW, "?", 1.0f));
    rsx_overlay_assets_free(&one);
    rsx_overlay_assets_free(&two);
    CHECK(one.rgba == NULL);
    CHECK(rsx_overlay_assets_build_embedded(&one, 0.1f) == 0); /* out of range */
    return 1;
}

int main(void)
{
    int ok = 1;
    ok &= test_glyph_set_and_utf8();
    ok &= test_embedded_scales_with_backing();
    printf("test_rsx_overlay_assets: %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
```

`libs/video/tests/test_rsx_overlay_assets_apple.m`:

```objc
#import <Foundation/Foundation.h>
#include <stdio.h>
#include "rsx_overlay_assets.h"

#define CHECK(expr) do { \
    if (!(expr)) { fprintf(stderr, "[FAIL] %s:%d: %s\n", __func__, __LINE__, #expr); return 1; } \
} while (0)

int main(void)
{
    @autoreleasepool {
        rsx_overlay_assets one, two;
        CHECK(rsx_overlay_assets_build_apple(&one, 1.0f) == 1);
        CHECK(rsx_overlay_assets_build_apple(&two, 2.0f) == 1);
        CHECK(one.system_font == 1 && two.system_font == 1);   /* SF Pro via CoreText */
        CHECK(two.system_icons == 1);                          /* SF Symbols rasterized */
        for (int i = 0; i < RSX_OVERLAY_ICON_COUNT; ++i)
            CHECK(two.icons[i].w > 0 && two.icons[i].h > 0 && two.icons[i].present);
        const int H = rsx_overlay_glyph_index('H');
        const float r = (float)two.glyphs[RSX_OVERLAY_TEXT_TITLE][H].h / (float)one.glyphs[RSX_OVERLAY_TEXT_TITLE][H].h;
        CHECK(r > 1.8f && r < 2.2f);
        CHECK(two.ascent[RSX_OVERLAY_TEXT_ROW] > 1.8f * one.ascent[RSX_OVERLAY_TEXT_ROW]);
        CHECK(two.glyphs[RSX_OVERLAY_TEXT_ROW][rsx_overlay_glyph_index(0xE7)].present); /* ç */
        rsx_overlay_assets_free(&one);
        rsx_overlay_assets_free(&two);
        printf("test_rsx_overlay_assets_apple: PASS\n");
    }
    return 0;
}
```

In `libs/video/tests/test_rsx_overlay_metal.m` add a generation-aware source and test (call from `main` after `test_busy_slots_skip_instead_of_overwrite()`):

```objc
static uint64_t s_gen = 1;
static uint8_t s_font2[16];
static uint64_t src_generation(void* ud) { (void)ud; return s_gen; }
static int src_copy_font2(uint8_t* rgba, size_t capacity, uint32_t* w, uint32_t* h, void* ud)
{
    ((test_source*)ud)->font_copies++;
    if (rgba) { if (capacity < sizeof(s_font2)) return 0; memcpy(rgba, s_font2, sizeof(s_font2)); }
    *w = 2; *h = 2;
    return 1;
}

/* A new atlas generation (backing scale changed) is re-uploaded; the same
 * generation is never copied again. */
static int test_atlas_generation_reloads_texture(void)
{
    memcpy(s_font2, kFont, sizeof(s_font2));
    s_gen = 1;
    test_source src = { 1, known_packet(), 0 };
    rsx_overlay_metal_source source = { src_visible, src_copy_packet, src_copy_font2, &src, src_generation };
    rsx_overlay_metal_renderer* r =
        rsx_overlay_metal_renderer_create(s_device, MTLPixelFormatBGRA8Unorm, &source);
    CHECK(r != NULL);
    id<MTLTexture> target = make_target();
    for (int pass = 0; pass < 3; ++pass) {
        if (pass == 2) {
            /* Region C samples texel (0,1): make it opaque red, bump generation. */
            s_font2[8] = 255; s_font2[9] = 0; s_font2[10] = 0; s_font2[11] = 255;
            s_gen = 2;
        }
        const int copies_before = src.font_copies;
        id<MTLCommandBuffer> cb = [s_queue commandBuffer];
        encode_clear(cb, target);
        CHECK(rsx_overlay_metal_renderer_encode(r, cb, target, kViewport) == 1);
        id<MTLBuffer> rb = encode_readback(cb, target);
        [cb commit];
        [cb waitUntilCompleted];
        if (pass == 1) CHECK(src.font_copies == copies_before);        /* cached */
        if (pass < 2) CHECK(pixel_is(rb, 64, 66, 0, 255, 0));            /* green */
        else CHECK(pixel_is(rb, 64, 66, 0, 0, 255));                     /* red: reloaded */
    }
    rsx_overlay_metal_renderer_destroy(r);
    free(src.packet);
    return 1;
}
```

Existing initializers `{ src_visible, src_copy_packet, src_copy_font, &src }` stay valid (the new member is zero-initialized).

- [ ] **Step 3: Run to verify failure**

Run: `cmake --build build-macos --target test_rsx_overlay_metal`
Expected: compile error — too many initializers / no member `atlas_generation`. (The assets test targets do not exist yet.)

- [ ] **Step 4: Implement the assets module**

`libs/video/rsx_overlay_assets.h`:

```c
#ifndef PS3RECOMP_RSX_OVERLAY_ASSETS_H
#define PS3RECOMP_RSX_OVERLAY_ASSETS_H

/* Glyph/icon atlas of the console shell: white RGB, coverage in A, rasterized
 * at the drawable's backing scale. It becomes the overlay's texture 0. */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum rsx_overlay_text_style {
    RSX_OVERLAY_TEXT_TITLE = 0,
    RSX_OVERLAY_TEXT_ROW,
    RSX_OVERLAY_TEXT_LABEL,
    RSX_OVERLAY_TEXT_CAPTION,
    RSX_OVERLAY_TEXT_STYLE_COUNT
} rsx_overlay_text_style;

typedef enum rsx_overlay_icon {
    RSX_OVERLAY_ICON_PLAY = 0, RSX_OVERLAY_ICON_DISPLAY, RSX_OVERLAY_ICON_GAMEPAD,
    RSX_OVERLAY_ICON_SPEAKER, RSX_OVERLAY_ICON_HAMMER, RSX_OVERLAY_ICON_POWER,
    RSX_OVERLAY_ICON_COUNT
} rsx_overlay_icon;

#define RSX_OVERLAY_GLYPH_COUNT 194
#define RSX_OVERLAY_ICON_POINT_SIZE 30.0f

/* Atlas rectangle plus pen metrics in atlas pixels: the glyph's top-left is
 * (pen + left, baseline - top). */
typedef struct rsx_overlay_glyph {
    uint16_t x, y, w, h;
    int16_t left, top;
    float advance;
    uint8_t present;
} rsx_overlay_glyph;

typedef struct rsx_overlay_assets {
    uint32_t width, height;
    uint8_t* rgba;
    float scale;             /* atlas pixels per point */
    uint8_t system_font;     /* 1: SF Pro (CoreText); 0: embedded Inter */
    uint8_t system_icons;    /* 1: `icons` holds SF Symbols; 0: draw geometric icons */
    uint16_t white_x, white_y; /* top-left of an opaque 4x4 block */
    float ascent[RSX_OVERLAY_TEXT_STYLE_COUNT];
    float descent[RSX_OVERLAY_TEXT_STYLE_COUNT];  /* positive, below the baseline */
    float line_height[RSX_OVERLAY_TEXT_STYLE_COUNT];
    rsx_overlay_glyph glyphs[RSX_OVERLAY_TEXT_STYLE_COUNT][RSX_OVERLAY_GLYPH_COUNT];
    rsx_overlay_glyph icons[RSX_OVERLAY_ICON_COUNT];
} rsx_overlay_assets;

typedef struct rsx_overlay_packer { uint32_t width, height, x, y, row_h; } rsx_overlay_packer;

int rsx_overlay_glyph_index(uint32_t codepoint);
uint32_t rsx_overlay_glyph_codepoint(int index);
uint32_t rsx_overlay_utf8_next(const char** text);
float rsx_overlay_text_point_size(int style);
float rsx_overlay_text_width(const rsx_overlay_assets* a, int style, const char* utf8, float k);

/* Shared by the builders. */
int rsx_overlay_assets_begin(rsx_overlay_assets* a, uint32_t w, uint32_t h, float scale,
                             rsx_overlay_packer* p);
int rsx_overlay_packer_add(rsx_overlay_packer* p, uint32_t w, uint32_t h, uint16_t* x, uint16_t* y);
void rsx_overlay_assets_put_alpha(rsx_overlay_assets* a, uint32_t x, uint32_t y, uint32_t w,
                                  uint32_t h, const uint8_t* alpha, uint32_t stride);

int rsx_overlay_assets_build_embedded(rsx_overlay_assets* out, float scale);
#if defined(__APPLE__)
int rsx_overlay_assets_build_apple(rsx_overlay_assets* out, float scale);
#endif
void rsx_overlay_assets_free(rsx_overlay_assets* a);

#ifdef __cplusplus
}
#endif

#endif
```

`libs/video/rsx_overlay_assets.c`:

```c
#include "rsx_overlay_assets.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "imstb_truetype.h"

extern const unsigned char rsx_overlay_inter_regular[];
extern const size_t rsx_overlay_inter_regular_size;
extern const unsigned char rsx_overlay_inter_semibold[];
extern const size_t rsx_overlay_inter_semibold_size;

static const float k_point_size[RSX_OVERLAY_TEXT_STYLE_COUNT] = { 17.0f, 15.0f, 11.0f, 13.0f };
static const int k_inter_semibold[RSX_OVERLAY_TEXT_STYLE_COUNT] = { 1, 0, 1, 0 };

float rsx_overlay_text_point_size(int style)
{
    return (style >= 0 && style < RSX_OVERLAY_TEXT_STYLE_COUNT) ? k_point_size[style] : 15.0f;
}

int rsx_overlay_glyph_index(uint32_t cp)
{
    if (cp >= 0x20 && cp <= 0x7E) return (int)(cp - 0x20);
    if (cp >= 0xA0 && cp <= 0xFF) return 95 + (int)(cp - 0xA0);
    if (cp == 0x2026) return 191;
    if (cp == 0x2013) return 192;
    if (cp == 0x2014) return 193;
    return -1;
}

uint32_t rsx_overlay_glyph_codepoint(int index)
{
    if (index >= 0 && index < 95) return 0x20u + (uint32_t)index;
    if (index >= 95 && index < 191) return 0xA0u + (uint32_t)(index - 95);
    if (index == 191) return 0x2026u;
    if (index == 192) return 0x2013u;
    if (index == 193) return 0x2014u;
    return 0;
}

uint32_t rsx_overlay_utf8_next(const char** text)
{
    const unsigned char* s = (const unsigned char*)*text;
    uint32_t cp;
    int n;
    if (s[0] < 0x80) { cp = s[0]; n = 1; }
    else if ((s[0] & 0xE0) == 0xC0 && (s[1] & 0xC0) == 0x80) {
        cp = ((uint32_t)(s[0] & 0x1F) << 6) | (s[1] & 0x3F); n = 2;
    } else if ((s[0] & 0xF0) == 0xE0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80) {
        cp = ((uint32_t)(s[0] & 0x0F) << 12) | ((uint32_t)(s[1] & 0x3F) << 6) | (s[2] & 0x3F); n = 3;
    } else if ((s[0] & 0xF8) == 0xF0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80 &&
               (s[3] & 0xC0) == 0x80) {
        cp = ((uint32_t)(s[0] & 0x07) << 18) | ((uint32_t)(s[1] & 0x3F) << 12) |
             ((uint32_t)(s[2] & 0x3F) << 6) | (s[3] & 0x3F); n = 4;
    } else { cp = 0xFFFDu; n = 1; }
    *text += n;
    return cp;
}

float rsx_overlay_text_width(const rsx_overlay_assets* a, int style, const char* text, float k)
{
    const int q = rsx_overlay_glyph_index('?');
    float w = 0.0f;
    if (a == NULL || text == NULL || style < 0 || style >= RSX_OVERLAY_TEXT_STYLE_COUNT)
        return 0.0f;
    while (*text) {
        int gi = rsx_overlay_glyph_index(rsx_overlay_utf8_next(&text));
        if (gi < 0 || !a->glyphs[style][gi].present)
            gi = q;
        w += a->glyphs[style][gi].advance * k;
    }
    return w;
}

int rsx_overlay_packer_add(rsx_overlay_packer* p, uint32_t w, uint32_t h, uint16_t* ox, uint16_t* oy)
{
    if (w + 2u > p->width)
        return 0;
    if (p->x + w + 1u > p->width) {
        p->x = 1u;
        p->y += p->row_h + 1u;
        p->row_h = 0u;
    }
    if (p->y + h + 1u > p->height)
        return 0;
    *ox = (uint16_t)p->x;
    *oy = (uint16_t)p->y;
    p->x += w + 1u;
    if (h > p->row_h)
        p->row_h = h;
    return 1;
}

void rsx_overlay_assets_put_alpha(rsx_overlay_assets* a, uint32_t x, uint32_t y, uint32_t w,
                                  uint32_t h, const uint8_t* alpha, uint32_t stride)
{
    for (uint32_t row = 0; row < h; ++row)
        for (uint32_t col = 0; col < w; ++col)
            a->rgba[(((size_t)(y + row) * a->width) + x + col) * 4u + 3u] = alpha[(size_t)row * stride + col];
}

int rsx_overlay_assets_begin(rsx_overlay_assets* a, uint32_t w, uint32_t h, float scale,
                             rsx_overlay_packer* p)
{
    static const uint8_t opaque[16] = { 255, 255, 255, 255, 255, 255, 255, 255,
                                        255, 255, 255, 255, 255, 255, 255, 255 };
    uint16_t x, y;
    memset(a, 0, sizeof(*a));
    a->rgba = (uint8_t*)malloc((size_t)w * h * 4u);
    if (a->rgba == NULL)
        return 0;
    for (size_t i = 0; i < (size_t)w * h; ++i) {
        a->rgba[i * 4u + 0u] = 255;
        a->rgba[i * 4u + 1u] = 255;
        a->rgba[i * 4u + 2u] = 255;
        a->rgba[i * 4u + 3u] = 0;
    }
    a->width = w;
    a->height = h;
    a->scale = scale;
    p->width = w; p->height = h; p->x = 1u; p->y = 1u; p->row_h = 0u;
    if (!rsx_overlay_packer_add(p, 4u, 4u, &x, &y)) {
        rsx_overlay_assets_free(a);
        return 0;
    }
    rsx_overlay_assets_put_alpha(a, x, y, 4u, 4u, opaque, 4u);
    a->white_x = x;
    a->white_y = y;
    return 1;
}

void rsx_overlay_assets_free(rsx_overlay_assets* a)
{
    if (a == NULL)
        return;
    free(a->rgba);
    a->rgba = NULL;
    a->width = a->height = 0;
}

static int bake_style(rsx_overlay_assets* a, rsx_overlay_packer* p, int style,
                      const stbtt_fontinfo* font, float scale)
{
    const float sc = stbtt_ScaleForMappingEmToPixels(font, rsx_overlay_text_point_size(style) * scale);
    int ascent, descent, gap;
    stbtt_GetFontVMetrics(font, &ascent, &descent, &gap);
    a->ascent[style] = (float)ascent * sc;
    a->descent[style] = (float)-descent * sc;
    a->line_height[style] = (float)(ascent - descent + gap) * sc;
    for (int i = 0; i < RSX_OVERLAY_GLYPH_COUNT; ++i) {
        rsx_overlay_glyph* out = &a->glyphs[style][i];
        const int g = stbtt_FindGlyphIndex(font, (int)rsx_overlay_glyph_codepoint(i));
        int adv, lsb, x0, y0, x1, y1;
        uint16_t ox, oy;
        uint8_t* tmp;
        memset(out, 0, sizeof(*out));
        if (g == 0)
            continue;
        stbtt_GetGlyphHMetrics(font, g, &adv, &lsb);
        out->advance = (float)adv * sc;
        out->present = 1;
        stbtt_GetGlyphBitmapBox(font, g, sc, sc, &x0, &y0, &x1, &y1);
        if (x1 <= x0 || y1 <= y0)
            continue;
        if (!rsx_overlay_packer_add(p, (uint32_t)(x1 - x0), (uint32_t)(y1 - y0), &ox, &oy))
            return 0;
        tmp = (uint8_t*)malloc((size_t)(x1 - x0) * (size_t)(y1 - y0));
        if (tmp == NULL)
            return 0;
        stbtt_MakeGlyphBitmap(font, tmp, x1 - x0, y1 - y0, x1 - x0, sc, sc, g);
        rsx_overlay_assets_put_alpha(a, ox, oy, (uint32_t)(x1 - x0), (uint32_t)(y1 - y0), tmp,
                                     (uint32_t)(x1 - x0));
        free(tmp);
        out->x = ox; out->y = oy;
        out->w = (uint16_t)(x1 - x0); out->h = (uint16_t)(y1 - y0);
        out->left = (int16_t)x0; out->top = (int16_t)-y0;
    }
    return 1;
}

int rsx_overlay_assets_build_embedded(rsx_overlay_assets* out, float scale)
{
    static const uint32_t sizes[] = { 1024u, 2048u, 4096u };
    stbtt_fontinfo regular, semibold;
    if (out == NULL || !(scale >= 0.5f && scale <= 4.0f))
        return 0;
    if (!stbtt_InitFont(&regular, rsx_overlay_inter_regular,
                        stbtt_GetFontOffsetForIndex(rsx_overlay_inter_regular, 0)) ||
        !stbtt_InitFont(&semibold, rsx_overlay_inter_semibold,
                        stbtt_GetFontOffsetForIndex(rsx_overlay_inter_semibold, 0)))
        return 0;
    for (size_t attempt = 0; attempt < sizeof sizes / sizeof sizes[0]; ++attempt) {
        rsx_overlay_packer p;
        int ok = 1;
        if (!rsx_overlay_assets_begin(out, sizes[attempt], sizes[attempt], scale, &p))
            return 0;
        for (int st = 0; st < RSX_OVERLAY_TEXT_STYLE_COUNT && ok; ++st)
            ok = bake_style(out, &p, st, k_inter_semibold[st] ? &semibold : &regular, scale);
        if (ok) {
            out->system_font = 0;
            out->system_icons = 0;
            return 1;
        }
        rsx_overlay_assets_free(out);
    }
    return 0;
}
```

`libs/video/rsx_overlay_assets_apple.m` (ARC):

```objc
/* CoreText SF Pro + SF Symbols atlas for the console shell (macOS). Falls back
 * to the embedded Inter builder when the system font is unavailable; icons
 * fall back to geometric drawing (system_icons = 0). */
#import <AppKit/AppKit.h>
#import <CoreText/CoreText.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "rsx_overlay_assets.h"
#include "rsx_overlay_log.h"

static NSFontWeight apple_weight(int style)
{
    switch (style) {
    case RSX_OVERLAY_TEXT_TITLE: return NSFontWeightSemibold;
    case RSX_OVERLAY_TEXT_LABEL: return NSFontWeightMedium;
    default: return NSFontWeightRegular;
    }
}

static int apple_bake_style(rsx_overlay_assets* a, rsx_overlay_packer* p, int style, CTFontRef font)
{
    a->ascent[style] = (float)CTFontGetAscent(font);
    a->descent[style] = (float)CTFontGetDescent(font);
    a->line_height[style] = (float)(CTFontGetAscent(font) + CTFontGetDescent(font) + CTFontGetLeading(font));
    for (int i = 0; i < RSX_OVERLAY_GLYPH_COUNT; ++i) {
        rsx_overlay_glyph* out = &a->glyphs[style][i];
        UniChar ch = (UniChar)rsx_overlay_glyph_codepoint(i);
        CGGlyph g;
        CGSize adv;
        memset(out, 0, sizeof(*out));
        if (!CTFontGetGlyphsForCharacters(font, &ch, &g, 1))
            continue;
        CTFontGetAdvancesForGlyphs(font, kCTFontOrientationHorizontal, &g, &adv, 1);
        out->advance = (float)adv.width;
        out->present = 1;
        const CGRect bb = CTFontGetBoundingRectsForGlyphs(font, kCTFontOrientationHorizontal, &g, NULL, 1);
        if (bb.size.width <= 0.0 || bb.size.height <= 0.0)
            continue;
        const int x0 = (int)floor(bb.origin.x) - 1, y0 = (int)floor(bb.origin.y) - 1;
        const int x1 = (int)ceil(bb.origin.x + bb.size.width) + 1;
        const int y1 = (int)ceil(bb.origin.y + bb.size.height) + 1;
        const int w = x1 - x0, h = y1 - y0;
        uint16_t ox, oy;
        uint8_t* buf = (uint8_t*)calloc((size_t)w * (size_t)h, 1);
        if (buf == NULL)
            return 0;
        CGContextRef ctx = CGBitmapContextCreate(buf, (size_t)w, (size_t)h, 8, (size_t)w, NULL,
                                                 (CGBitmapInfo)kCGImageAlphaOnly);
        if (ctx == NULL) { free(buf); return 0; }
        CGContextSetAllowsAntialiasing(ctx, true);
        CGContextSetShouldAntialias(ctx, true);
        CGContextSetAllowsFontSmoothing(ctx, false);
        const CGPoint pos = CGPointMake(-x0, -y0);
        CTFontDrawGlyphs(font, &g, &pos, 1, ctx);
        CGContextRelease(ctx);
        /* CGBitmapContext memory row 0 is the top row: same as the atlas. */
        if (!rsx_overlay_packer_add(p, (uint32_t)w, (uint32_t)h, &ox, &oy)) { free(buf); return 0; }
        rsx_overlay_assets_put_alpha(a, ox, oy, (uint32_t)w, (uint32_t)h, buf, (uint32_t)w);
        free(buf);
        out->x = ox; out->y = oy; out->w = (uint16_t)w; out->h = (uint16_t)h;
        out->left = (int16_t)x0; out->top = (int16_t)y1;
    }
    return 1;
}

static int apple_bake_icons(rsx_overlay_assets* a, rsx_overlay_packer* p, float scale)
{
    static NSString* const names[RSX_OVERLAY_ICON_COUNT] = {
        @"play.fill", @"display", @"gamecontroller", @"speaker.wave.2", @"hammer", @"power"
    };
    if (@available(macOS 11.0, *)) {
        NSImageSymbolConfiguration* cfg =
            [NSImageSymbolConfiguration configurationWithPointSize:RSX_OVERLAY_ICON_POINT_SIZE * scale
                                                            weight:NSFontWeightRegular];
        for (int i = 0; i < RSX_OVERLAY_ICON_COUNT; ++i) {
            NSImage* img = [[NSImage imageWithSystemSymbolName:names[i] accessibilityDescription:nil]
                               imageWithSymbolConfiguration:cfg];
            if (img == nil)
                return 0;
            const NSSize sz = img.size;
            const int w = (int)ceil(sz.width) + 2, h = (int)ceil(sz.height) + 2;
            uint8_t* rgba = (uint8_t*)calloc((size_t)w * (size_t)h, 4);
            uint8_t* alpha = (uint8_t*)malloc((size_t)w * (size_t)h);
            unsigned coverage = 0;
            uint16_t ox, oy;
            if (rgba == NULL || alpha == NULL) { free(rgba); free(alpha); return 0; }
            CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
            CGContextRef ctx = CGBitmapContextCreate(rgba, (size_t)w, (size_t)h, 8, (size_t)w * 4u, cs,
                                                     (CGBitmapInfo)kCGImageAlphaPremultipliedLast);
            CGColorSpaceRelease(cs);
            if (ctx == NULL) { free(rgba); free(alpha); return 0; }
            NSGraphicsContext* gc = [NSGraphicsContext graphicsContextWithCGContext:ctx flipped:NO];
            [NSGraphicsContext saveGraphicsState];
            [NSGraphicsContext setCurrentContext:gc];
            [img drawInRect:NSMakeRect(1, 1, sz.width, sz.height) fromRect:NSZeroRect
                  operation:NSCompositingOperationSourceOver fraction:1.0];
            [NSGraphicsContext restoreGraphicsState];
            CGContextRelease(ctx);
            for (int k = 0; k < w * h; ++k) { alpha[k] = rgba[k * 4 + 3]; coverage += alpha[k] != 0; }
            free(rgba);
            if (coverage == 0 || !rsx_overlay_packer_add(p, (uint32_t)w, (uint32_t)h, &ox, &oy)) {
                free(alpha);
                return 0;
            }
            rsx_overlay_assets_put_alpha(a, ox, oy, (uint32_t)w, (uint32_t)h, alpha, (uint32_t)w);
            free(alpha);
            a->icons[i].x = ox; a->icons[i].y = oy;
            a->icons[i].w = (uint16_t)w; a->icons[i].h = (uint16_t)h;
            a->icons[i].present = 1;
        }
        return 1;
    }
    return 0;
}

int rsx_overlay_assets_build_apple(rsx_overlay_assets* out, float scale)
{
    static const uint32_t sizes[] = { 1024u, 2048u, 4096u };
    if (out == NULL || !(scale >= 0.5f && scale <= 4.0f))
        return 0;
    @autoreleasepool {
        for (size_t attempt = 0; attempt < sizeof sizes / sizeof sizes[0]; ++attempt) {
            rsx_overlay_packer p;
            int ok = 1;
            if (!rsx_overlay_assets_begin(out, sizes[attempt], sizes[attempt], scale, &p))
                return 0;
            for (int st = 0; st < RSX_OVERLAY_TEXT_STYLE_COUNT && ok; ++st) {
                NSFont* font = [NSFont systemFontOfSize:rsx_overlay_text_point_size(st) * scale
                                                 weight:apple_weight(st)];
                if (font == nil) {
                    rsx_overlay_assets_free(out);
                    rsx_overlay_log_write(RSX_OVERLAY_LOG_WARN, "overlay: SF Pro unavailable, using Inter");
                    return rsx_overlay_assets_build_embedded(out, scale);
                }
                ok = apple_bake_style(out, &p, st, (__bridge CTFontRef)font);
            }
            if (ok) {
                out->system_font = 1;
                out->system_icons = (uint8_t)apple_bake_icons(out, &p, scale);
                if (!out->system_icons) {
                    memset(out->icons, 0, sizeof(out->icons));
                    rsx_overlay_log_write(RSX_OVERLAY_LOG_WARN, "overlay: SF Symbols unavailable, geometric icons");
                }
                return 1;
            }
            rsx_overlay_assets_free(out);
        }
    }
    return rsx_overlay_assets_build_embedded(out, scale);
}
```

- [ ] **Step 5: Core generation + Metal reload**

`rsx_overlay.h` (after `rsx_overlay_copy_font_atlas`):

```c
typedef struct rsx_overlay_assets rsx_overlay_assets;
typedef int (*rsx_overlay_asset_builder)(rsx_overlay_assets* out, float scale);
/* Bumped every time the atlas returned by rsx_overlay_copy_font_atlas changes
 * (init, backing-scale change, builder change). Renderers re-upload on change. */
uint64_t rsx_overlay_font_atlas_generation(void);
```

`rsx_overlay.cpp`: add `static std::atomic<uint64_t> s_atlas_generation(0);` next to the font statics; in `rsx_overlay_init`, inside the font-capture block after `s_font_height = (uint32_t)font_height;` add `s_atlas_generation.fetch_add(1);`; and

```cpp
uint64_t rsx_overlay_font_atlas_generation(void)
{
    return s_atlas_generation.load();
}
```

`rsx_overlay_metal.h`: add `uint64_t (*atlas_generation)(void* userdata);` as the last member of `rsx_overlay_metal_source` (comment: "NULL: the atlas never changes").

`rsx_overlay_metal.m`: add

```objc
static uint64_t overlay_core_atlas_generation(void* userdata)
{
    (void)userdata;
    return rsx_overlay_font_atlas_generation();
}
```

set `r->_source.atlas_generation = overlay_core_atlas_generation;` in the default-source branch of `create`; add ivar `uint64_t _font_generation;`; at the start of `overlay_ensure_font` replace `if (r->_font) return 1;` with

```objc
    const uint64_t generation = r->_source.atlas_generation
        ? r->_source.atlas_generation(r->_source.userdata) : 0u;
    if (r->_font && generation == r->_font_generation)
        return 1;
    if (r->_font) {
        /* New atlas (backing scale changed): in-flight command buffers keep
         * the old texture alive; the next frames sample the new one. */
        r->_font = nil;
        r->_font_failures = 0;
    }
```

and after `r->_font = tex;` add `r->_font_generation = generation;`.

- [ ] **Step 6: CMake and link wiring**

Before `add_library(ps3recomp_runtime STATIC` (`CMakeLists.txt:234`):

```cmake
# Inter (third_party/inter, OFL) embedded as C arrays: the console shell's
# fallback typeface. Object library so the runtime archive and the tests share
# one generated copy.
set(PS3RECOMP_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(PS3RECOMP_INTER_SOURCES)
foreach(pair "Inter-Regular.ttf|rsx_overlay_inter_regular" "Inter-SemiBold.ttf|rsx_overlay_inter_semibold")
    string(REPLACE "|" ";" parts "${pair}")
    list(GET parts 0 font_file)
    list(GET parts 1 font_symbol)
    add_custom_command(
        OUTPUT "${PS3RECOMP_GEN_DIR}/${font_symbol}.c"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${PS3RECOMP_GEN_DIR}"
        COMMAND ${CMAKE_COMMAND}
            -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/third_party/inter/${font_file}
            -DOUTPUT=${PS3RECOMP_GEN_DIR}/${font_symbol}.c
            -DSYMBOL=${font_symbol}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_file.cmake"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/inter/${font_file}"
                "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_file.cmake"
        VERBATIM)
    list(APPEND PS3RECOMP_INTER_SOURCES "${PS3RECOMP_GEN_DIR}/${font_symbol}.c")
endforeach()
add_library(ps3recomp_overlay_fonts OBJECT ${PS3RECOMP_INTER_SOURCES})
```

Change `target_link_libraries(ps3recomp_runtime PRIVATE ps3recomp_imgui)` to `target_link_libraries(ps3recomp_runtime PRIVATE ps3recomp_imgui ps3recomp_overlay_fonts)`. After `CMakeLists.txt:143` add `list(APPEND HLE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/libs/video/rsx_overlay_assets.c")`. In the `if(APPLE)` source block (`:162-167`) append `"${CMAKE_CURRENT_SOURCE_DIR}/libs/video/rsx_overlay_assets_apple.m"` to `HLE_SOURCES` and to `PS3RECOMP_OBJC_ARC_SOURCES`; add `"-framework CoreText"` to the Apple `target_link_libraries(ps3recomp_runtime PUBLIC ...)` list and to `test_runtime_overlay_link`. In `games/gow2/build_macos.sh:574` change `-framework Cocoa \` to `-framework Cocoa -framework CoreText \`.

Tests (next to `test_rsx_overlay_shell`):

```cmake
    add_executable(test_rsx_overlay_assets
        libs/video/tests/test_rsx_overlay_assets.c
        libs/video/rsx_overlay_assets.c)
    target_include_directories(test_rsx_overlay_assets PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/libs/video"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui")
    target_link_libraries(test_rsx_overlay_assets PRIVATE ps3recomp_overlay_fonts m)
    add_test(NAME test_rsx_overlay_assets COMMAND test_rsx_overlay_assets)

    if(APPLE)
        add_executable(test_rsx_overlay_assets_apple
            libs/video/tests/test_rsx_overlay_assets_apple.m
            libs/video/rsx_overlay_assets_apple.m
            libs/video/rsx_overlay_assets.c
            libs/video/rsx_overlay_log.c)
        set_source_files_properties(
            libs/video/tests/test_rsx_overlay_assets_apple.m libs/video/rsx_overlay_assets_apple.m
            PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
        target_include_directories(test_rsx_overlay_assets_apple PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/libs/video"
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui")
        target_link_libraries(test_rsx_overlay_assets_apple PRIVATE ps3recomp_overlay_fonts
            Threads::Threads "-framework AppKit" "-framework CoreText" "-framework Foundation")
        add_test(NAME test_rsx_overlay_assets_apple COMMAND test_rsx_overlay_assets_apple)
    endif()
```

(`Threads::Threads` is found later at `:707`; if CMake complains it is unknown here, add `find_package(Threads REQUIRED)` right before this block.)

- [ ] **Step 7: Run to verify pass**

Run: `cmake --build build-macos --target test_rsx_overlay_assets test_rsx_overlay_assets_apple test_rsx_overlay_metal ps3recomp_runtime test_runtime_overlay_link && ctest --test-dir build-macos -R 'overlay' --output-on-failure`
Expected: all PASS, including `test_atlas_generation_reloads_texture` and `test_runtime_overlay_link`.
Then: `x86_64-w64-mingw32-gcc -fsyntax-only -std=c11 $MW libs/video/rsx_overlay_assets.c` — no output.

- [ ] **Step 8: Commit**

```bash
git add third_party/inter/Inter-Regular.ttf third_party/inter/Inter-SemiBold.ttf third_party/inter/LICENSE.txt \
  third_party/inter/README.ps3recomp.md cmake/embed_file.cmake libs/video/rsx_overlay_assets.h \
  libs/video/rsx_overlay_assets.c libs/video/rsx_overlay_assets_apple.m libs/video/rsx_overlay.h \
  libs/video/rsx_overlay.cpp libs/video/rsx_overlay_metal.h libs/video/rsx_overlay_metal.m \
  libs/video/tests/test_rsx_overlay_assets.c libs/video/tests/test_rsx_overlay_assets_apple.m \
  libs/video/tests/test_rsx_overlay_metal.m CMakeLists.txt games/gow2/build_macos.sh
git commit -m "feat(overlay): atlas de glifos (SF Pro/SF Symbols no Mac, Inter embutida) com geração"
```

---

### Task 5: Consumers — runtime mute, vibration gate, UI sounds and haptics

**Files:**
- Modify: `libs/audio/audio_mute.c`, `libs/audio/cellAudio.h:118`, `libs/audio/cellAudio.c:193-205` (SDL), `:321-327` (CoreAudio), `:469-476` (WASAPI)
- Modify: `libs/input/cellPad.h:255-257` (declarations + inline gate), `libs/input/cellPad.c:96` (flag), `:121-148` (setter area), `:1134-1137` (SetActDirect)
- Modify: `libs/input/cellpad_gamecontroller.h`, `libs/input/cellpad_gamecontroller.m` (append `cpgc_ui_tick`)
- Create: `libs/video/rsx_overlay_ui_sound.{h,c}`, `libs/video/rsx_overlay_feedback_apple.{h,m}`
- Create: `libs/video/tests/test_rsx_overlay_ui_sound.c`, `libs/video/tests/test_rsx_overlay_feedback_apple.m`
- Modify: `libs/audio/tests/test_audio_muted.c`, `libs/input/tests/test_pad_overlay_route.c`, `CMakeLists.txt`

**Interfaces:**
- Produces:

```c
/* cellAudio.h */
void ps3_audio_set_runtime_mute(int muted);          /* any thread */
int  ps3_audio_runtime_muted(void);
int  ps3_audio_output_silenced(void);                /* PS3_MUTE || runtime mute */
const float* ps3_audio_gate_block(const float* mixed, float* scratch, size_t floats);
/* cellPad.h */
void cellPad_set_host_vibration(int enabled);
static inline void cellPad_gate_actuators(u8* motor, unsigned count, int enabled);
/* cellpad_gamecontroller.h */
void cpgc_ui_tick(float intensity, float sharpness);
/* rsx_overlay_ui_sound.h */
#define RSX_OVERLAY_UI_SOUND_RATE 48000
size_t rsx_overlay_ui_sound_length(int event);
size_t rsx_overlay_ui_sound_render(int event, float* out, size_t capacity);
/* rsx_overlay_feedback_apple.h (Apple) */
void rsx_overlay_feedback_apple_set_haptic(void (*tick)(float intensity, float sharpness));
void rsx_overlay_feedback_apple_play(int event, int sounds, int haptics, void* userdata);
int  rsx_overlay_feedback_apple_state(void);  /* 0 idle, 1 engine running, -1 failed */
void rsx_overlay_feedback_apple_shutdown(void);
```

- [ ] **Step 1: Write the failing tests**

`libs/audio/tests/test_audio_muted.c`: add declarations `void ps3_audio_set_runtime_mute(int); int ps3_audio_output_silenced(void); const float* ps3_audio_gate_block(const float*, float*, size_t);` and replace the child branch (`if (argc > 1) { ... }`) with

```c
    if (argc > 1) {                 /* filho: um caso, um processo */
        if (strcmp(argv[1], "runtime") == 0) {
            float mixed[4] = { 0.5f, -0.5f, 0.25f, -0.25f };
            float scratch[4] = { 9.0f, 9.0f, 9.0f, 9.0f };
            const int env_muted = atoi(argv[2]);
            if (ps3_audio_output_silenced() != env_muted) return 1;
            if (ps3_audio_gate_block(mixed, scratch, 4) != (env_muted ? scratch : mixed)) return 2;
            ps3_audio_set_runtime_mute(1);
            if (!ps3_audio_output_silenced()) return 3;
            const float* out = ps3_audio_gate_block(mixed, scratch, 4);
            if (out != scratch) return 4;
            for (int i = 0; i < 4; ++i) if (out[i] != 0.0f) return 5;
            ps3_audio_set_runtime_mute(0);
            if (ps3_audio_output_silenced() != env_muted) return 6; /* PS3_MUTE still wins */
            return 0;
        }
        const int want = atoi(argv[2]);
        const int got = ps3_audio_muted();
        if (got != want) {
            fprintf(stderr, "FALHOU: PS3_MUTE=%s -> mudo=%d, esperado %d\n",
                    getenv("PS3_MUTE") ? getenv("PS3_MUTE") : "(nao definida)", got, want);
            return 1;
        }
        return 0;
    }
```

and, after the existing loop, add

```c
    static const struct { const char* value; const char* muted; } runtime_cases[] = {
        { NULL, "0" }, { "1", "1" },
    };
    for (unsigned i = 0; i < sizeof runtime_cases / sizeof runtime_cases[0]; i++) {
        if (runtime_cases[i].value) setenv("PS3_MUTE", runtime_cases[i].value, 1);
        else                        unsetenv("PS3_MUTE");
        const pid_t pid = fork();
        assert(pid >= 0);
        if (pid == 0) {
            char* args[] = { argv[0], (char*)"runtime", (char*)runtime_cases[i].muted, NULL };
            execv(argv[0], args);
            _exit(127);
        }
        int st = 0;
        while (waitpid(pid, &st, 0) < 0) { }
        if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) fails++;
    }
```

`libs/video/tests/test_rsx_overlay_ui_sound.c`:

```c
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "rsx_overlay_feedback.h"
#include "rsx_overlay_ui_sound.h"

#define CHECK(expr) do { \
    if (!(expr)) { fprintf(stderr, "[FAIL] %s:%d: %s\n", __func__, __LINE__, #expr); return 0; } \
} while (0)

static float buf[RSX_OVERLAY_UI_SOUND_RATE];

static int test_every_event_is_short_quiet_and_click_free(void)
{
    for (int e = 0; e < RSX_OVERLAY_FEEDBACK_COUNT; ++e) {
        const size_t n = rsx_overlay_ui_sound_length(e);
        CHECK(n > 0 && n <= RSX_OVERLAY_UI_SOUND_RATE / 4); /* <= 250 ms */
        CHECK(rsx_overlay_ui_sound_render(e, buf, sizeof buf / sizeof buf[0]) == n);
        float peak = 0.0f;
        for (size_t i = 0; i < n; ++i) peak = fmaxf(peak, fabsf(buf[i]));
        CHECK(peak > 0.05f && peak <= 0.30f);
        CHECK(fabsf(buf[0]) < 1e-3f && fabsf(buf[n - 1]) < 1e-3f);
    }
    CHECK(rsx_overlay_ui_sound_length(RSX_OVERLAY_FEEDBACK_OPEN) == 8640);  /* 180 ms */
    CHECK(rsx_overlay_ui_sound_length(RSX_OVERLAY_FEEDBACK_MOVE) == 1344);  /* 28 ms */
    CHECK(rsx_overlay_ui_sound_length(-1) == 0 && rsx_overlay_ui_sound_length(RSX_OVERLAY_FEEDBACK_COUNT) == 0);
    return 1;
}

static int test_deterministic_distinct_and_bounded(void)
{
    static float a[RSX_OVERLAY_UI_SOUND_RATE], b[RSX_OVERLAY_UI_SOUND_RATE];
    const size_t n = rsx_overlay_ui_sound_render(RSX_OVERLAY_FEEDBACK_OPEN, a, RSX_OVERLAY_UI_SOUND_RATE);
    CHECK(rsx_overlay_ui_sound_render(RSX_OVERLAY_FEEDBACK_OPEN, b, RSX_OVERLAY_UI_SOUND_RATE) == n);
    CHECK(memcmp(a, b, n * sizeof(float)) == 0);
    rsx_overlay_ui_sound_render(RSX_OVERLAY_FEEDBACK_CLOSE, b, RSX_OVERLAY_UI_SOUND_RATE);
    CHECK(memcmp(a, b, 256 * sizeof(float)) != 0);
    CHECK(rsx_overlay_ui_sound_render(RSX_OVERLAY_FEEDBACK_OPEN, a, 100) == 100);
    CHECK(rsx_overlay_ui_sound_render(RSX_OVERLAY_FEEDBACK_OPEN, NULL, 0) == n);
    return 1;
}

int main(void)
{
    int ok = test_every_event_is_short_quiet_and_click_free() & test_deterministic_distinct_and_bounded();
    printf("test_rsx_overlay_ui_sound: %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
```

`libs/video/tests/test_rsx_overlay_feedback_apple.m`:

```objc
#import <Foundation/Foundation.h>
#include <mach/mach_time.h>
#include <stdio.h>
#include <unistd.h>

#include "rsx_overlay_feedback.h"
#include "rsx_overlay_feedback_apple.h"

static volatile int s_ticks;
static void fake_tick(float intensity, float sharpness)
{
    (void)intensity; (void)sharpness;
    __sync_fetch_and_add(&s_ticks, 1);
}

int main(void)
{
    @autoreleasepool {
        mach_timebase_info_data_t tb;
        mach_timebase_info(&tb);
        rsx_overlay_feedback_apple_set_haptic(fake_tick);
        const uint64_t t0 = mach_absolute_time();
        for (int i = 0; i < 200; ++i)
            rsx_overlay_feedback_apple_play(RSX_OVERLAY_FEEDBACK_MOVE, 1, 1, NULL);
        const double ms = (double)(mach_absolute_time() - t0) * tb.numer / tb.denom / 1e6;
        if (ms > 50.0) { fprintf(stderr, "[FAIL] play blocked for %.1f ms\n", ms); return 1; }
        if (s_ticks != 200) { fprintf(stderr, "[FAIL] ticks %d\n", s_ticks); return 1; }
        rsx_overlay_feedback_apple_play(RSX_OVERLAY_FEEDBACK_OPEN, 0, 0, NULL);
        rsx_overlay_feedback_apple_play(99, 1, 1, NULL);  /* out of range: ignored */
        if (s_ticks != 200) { fprintf(stderr, "[FAIL] switches off still ticked\n"); return 1; }
        /* The engine starts or fails cleanly (no audio device on CI): never hangs. */
        for (int i = 0; i < 200 && rsx_overlay_feedback_apple_state() == 0; ++i) usleep(10000);
        const int state = rsx_overlay_feedback_apple_state();
        if (state != 1 && state != -1) { fprintf(stderr, "[FAIL] state %d\n", state); return 1; }
        rsx_overlay_feedback_apple_shutdown();
        printf("test_rsx_overlay_feedback_apple: PASS (engine %s)\n", state == 1 ? "running" : "unavailable");
    }
    return 0;
}
```

`libs/input/tests/test_pad_overlay_route.c`: add and call from `main`

```c
static int test_vibration_gate_zeroes_motors(void)
{
    u8 motor[CELL_PAD_ACTUATOR_MAX] = { 1, 200 };
    cellPad_gate_actuators(motor, CELL_PAD_ACTUATOR_MAX, 1);
    CHECK(motor[0] == 1 && motor[1] == 200);
    cellPad_gate_actuators(motor, CELL_PAD_ACTUATOR_MAX, 0);
    CHECK(motor[0] == 0 && motor[1] == 0);
    return 1;
}
```

CMake: next to `test_rsx_overlay_assets` add

```cmake
    add_executable(test_rsx_overlay_ui_sound
        libs/video/tests/test_rsx_overlay_ui_sound.c
        libs/video/rsx_overlay_ui_sound.c)
    target_include_directories(test_rsx_overlay_ui_sound PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/libs/video")
    target_link_libraries(test_rsx_overlay_ui_sound PRIVATE m)
    add_test(NAME test_rsx_overlay_ui_sound COMMAND test_rsx_overlay_ui_sound)
    if(APPLE)
        add_executable(test_rsx_overlay_feedback_apple
            libs/video/tests/test_rsx_overlay_feedback_apple.m
            libs/video/rsx_overlay_feedback_apple.m
            libs/video/rsx_overlay_ui_sound.c
            libs/video/rsx_overlay_log.c)
        set_source_files_properties(
            libs/video/tests/test_rsx_overlay_feedback_apple.m libs/video/rsx_overlay_feedback_apple.m
            PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
        target_include_directories(test_rsx_overlay_feedback_apple PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/libs/video")
        target_link_libraries(test_rsx_overlay_feedback_apple PRIVATE Threads::Threads
            "-framework AVFoundation" "-framework Foundation")
        add_test(NAME test_rsx_overlay_feedback_apple COMMAND test_rsx_overlay_feedback_apple)
    endif()
```

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build build-macos --target test_audio_muted test_rsx_overlay_ui_sound test_pad_overlay_route`
Expected: undefined `ps3_audio_set_runtime_mute`, missing `rsx_overlay_ui_sound.h`, undeclared `cellPad_gate_actuators`.

- [ ] **Step 3: Implement audio**

Append to `libs/audio/audio_mute.c` (add `#include <stdatomic.h>` and `#include <string.h>` at the top):

```c
/* Runtime mute (overlay "Som do jogo"): independent of PS3_MUTE, read per
 * block. Both only silence the samples, never the device, so every host
 * thread keeps its timing (see the CoreAudio note in cellAudio.c). */
static atomic_int s_runtime_mute;

void ps3_audio_set_runtime_mute(int muted)
{
    atomic_store_explicit(&s_runtime_mute, muted ? 1 : 0, memory_order_relaxed);
}

int ps3_audio_runtime_muted(void)
{
    return atomic_load_explicit(&s_runtime_mute, memory_order_relaxed);
}

int ps3_audio_output_silenced(void)
{
    return ps3_audio_muted() || ps3_audio_runtime_muted();
}

const float* ps3_audio_gate_block(const float* mixed, float* scratch, size_t floats)
{
    if (!ps3_audio_output_silenced())
        return mixed;
    memset(scratch, 0, floats * sizeof(float));
    return scratch;
}
```

`cellAudio.h` after `int ps3_audio_muted(void);`:

```c
void ps3_audio_set_runtime_mute(int muted);
int ps3_audio_runtime_muted(void);
int ps3_audio_output_silenced(void);
/* Returns `mixed`, or `scratch` zeroed when output is silenced. */
const float* ps3_audio_gate_block(const float* mixed, float* scratch, size_t floats);
```

`cellAudio.c`: SDL submit body becomes

```c
    if (!s_sdl_audio_dev) return;
    /* PS3_MUTE / overlay mute: queue silence, not skip the device (mirrors the
     * CoreAudio/WASAPI gates) -- keeps SDL's own queued-size pacing unchanged. */
    float scratch[num_samples * 2];
    SDL_QueueAudio(s_sdl_audio_dev, ps3_audio_gate_block(stereo_samples, scratch, num_samples * 2),
                   num_samples * 2 * sizeof(float));
```

CoreAudio: replace the `if (ps3_audio_muted()) { ... return; } ca_ring_write(&s_ring, stereo_samples, num_samples * 2);` block (keep the long comment above it) with

```c
    float scratch[num_samples * 2];
    ca_ring_write(&s_ring, ps3_audio_gate_block(stereo_samples, scratch, num_samples * 2),
                  num_samples * 2);
```

WASAPI: replace the `static int muted ... memcpy(...)` lines with

```c
        /* Checked per block: the overlay can mute at runtime. */
        const float* src = ps3_audio_gate_block(stereo_samples, (float*)buf, num_samples * 2);
        if (src != (const float*)buf)
            memcpy(buf, src, num_samples * 2 * sizeof(float));
```

Also update the diagnostic print at `cellAudio.c:663` to use `ps3_audio_output_silenced()`.

- [ ] **Step 4: Implement the vibration gate and UI haptic tick**

`cellPad.h` after the `cellPad_set_host_mapping` declaration:

```c
/* Overlay "Vibração": 0 stops game rumble reaching the controller. */
void cellPad_set_host_vibration(int enabled);

static inline void cellPad_gate_actuators(u8* motor, unsigned count, int enabled)
{
    unsigned i;
    if (enabled)
        return;
    for (i = 0; i < count; ++i)
        motor[i] = 0; /* zero, not skip: an ongoing rumble is stopped */
}
```

`cellPad.c`: after `s_host_analog_deadzone` add `static atomic_int s_host_vibration = 1;`; after `cellPad_set_host_mapping` add

```c
void cellPad_set_host_vibration(int enabled)
{
    atomic_store_explicit(&s_host_vibration, enabled ? 1 : 0, memory_order_relaxed);
}
```

and in `cellPadSetActDirect`, right after the `local.motor[i] = vm_read8(...)` loop:

```c
    cellPad_gate_actuators(local.motor, CELL_PAD_ACTUATOR_MAX,
                           atomic_load_explicit(&s_host_vibration, memory_order_relaxed));
```

`cellpad_gamecontroller.h`: declare `void cpgc_ui_tick(float intensity, float sharpness); /* UI haptic transient on port 0, async */`. Append to `cellpad_gamecontroller.m`:

```objc
/* UI haptics (overlay): one transient on port 0, on the same serial queue as
 * rumble -- CoreHaptics player creation is a synchronous XPC round trip and
 * must never run on the UI or guest thread. Ticks coalesce while one is queued. */
static volatile int s_ui_tick_queued;

static void ui_tick_apply(float intensity, float sharpness) API_AVAILABLE(macos(11.0)) {
    NSArray<GCController*>* ctrls = GCController.controllers;
    if (ctrls.count == 0) return;
    GCController* c = ctrls[0];
    if (!c.haptics) return;
    if (!s_haptic_engine[0]) {
        s_haptic_engine[0] = [c.haptics createEngineWithLocality:GCHapticsLocalityDefault];
        [s_haptic_engine[0] startAndReturnError:nil];
    }
    CHHapticEngine* eng = s_haptic_engine[0];
    if (!eng) return;
    CHHapticEventParameter* ip = [[CHHapticEventParameter alloc]
        initWithParameterID:CHHapticEventParameterIDHapticIntensity value:intensity];
    CHHapticEventParameter* sp = [[CHHapticEventParameter alloc]
        initWithParameterID:CHHapticEventParameterIDHapticSharpness value:sharpness];
    CHHapticEvent* ev = [[CHHapticEvent alloc] initWithEventType:CHHapticEventTypeHapticTransient
                                                      parameters:@[ip, sp] relativeTime:0];
    NSError* err = nil;
    CHHapticPattern* pat = [[CHHapticPattern alloc] initWithEvents:@[ev] parameters:@[] error:&err];
    if (!pat) return;
    id<CHHapticPatternPlayer> pl = [eng createPlayerWithPattern:pat error:&err];
    [pl startAtTime:0 error:nil];
}

void cpgc_ui_tick(float intensity, float sharpness) {
    if (@available(macOS 11.0, *)) {} else return;
    if (__sync_lock_test_and_set(&s_ui_tick_queued, 1)) return;
    dispatch_async(rumble_queue(), ^{
        __sync_lock_release(&s_ui_tick_queued);
        if (@available(macOS 11.0, *)) ui_tick_apply(intensity, sharpness);
    });
}
```

- [ ] **Step 5: Implement UI sound synthesis and the Apple player**

`libs/video/rsx_overlay_ui_sound.h`:

```c
#ifndef PS3RECOMP_RSX_OVERLAY_UI_SOUND_H
#define PS3RECOMP_RSX_OVERLAY_UI_SOUND_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Deterministic mono UI sounds (our own synthesis, no external assets). */
#define RSX_OVERLAY_UI_SOUND_RATE 48000
size_t rsx_overlay_ui_sound_length(int event);
/* Writes min(length, capacity) samples; out == NULL returns the length. */
size_t rsx_overlay_ui_sound_render(int event, float* out, size_t capacity);
#ifdef __cplusplus
}
#endif
#endif
```

`libs/video/rsx_overlay_ui_sound.c`:

```c
#include "rsx_overlay_ui_sound.h"

#include <math.h>

#include "rsx_overlay_feedback.h"

typedef struct { float f0, f1, start, dur; } ui_tone;
typedef struct { float total; int count; ui_tone tone[2]; } ui_sound;

static const ui_sound k_sounds[RSX_OVERLAY_FEEDBACK_COUNT] = {
    /* OPEN    */ { 0.180f, 2, { { 660.0f, 660.0f, 0.000f, 0.090f }, { 990.0f, 990.0f, 0.060f, 0.120f } } },
    /* CLOSE   */ { 0.160f, 2, { { 990.0f, 990.0f, 0.000f, 0.080f }, { 660.0f, 660.0f, 0.050f, 0.110f } } },
    /* MOVE    */ { 0.028f, 1, { { 1760.0f, 1760.0f, 0.000f, 0.028f }, { 0, 0, 0, 0 } } },
    /* CONFIRM */ { 0.110f, 2, { { 880.0f, 880.0f, 0.000f, 0.060f }, { 1320.0f, 1320.0f, 0.040f, 0.070f } } },
    /* BACK    */ { 0.080f, 1, { { 740.0f, 520.0f, 0.000f, 0.080f }, { 0, 0, 0, 0 } } },
    /* ERROR   */ { 0.160f, 2, { { 220.0f, 220.0f, 0.000f, 0.060f }, { 220.0f, 220.0f, 0.090f, 0.070f } } },
};

#define UI_PEAK 0.18f
#define UI_LIMIT 0.30f

static float envelope(float t, float dur)
{
    const float attack = 0.003f, release = 0.004f;
    float a, r;
    if (t < 0.0f || t >= dur) return 0.0f;
    a = t < attack ? t / attack : 1.0f;
    r = (dur - t) < release ? (dur - t) / release : 1.0f;
    return a * r * expf(-t / (dur * 0.25f));
}

size_t rsx_overlay_ui_sound_length(int event)
{
    if (event < 0 || event >= RSX_OVERLAY_FEEDBACK_COUNT) return 0;
    return (size_t)lroundf(k_sounds[event].total * (float)RSX_OVERLAY_UI_SOUND_RATE);
}

size_t rsx_overlay_ui_sound_render(int event, float* out, size_t capacity)
{
    const float two_pi = 6.28318530718f;
    size_t n = rsx_overlay_ui_sound_length(event);
    if (out == NULL || n == 0) return n;
    if (n > capacity) n = capacity;
    for (size_t i = 0; i < n; ++i) {
        const float t = (float)i / (float)RSX_OVERLAY_UI_SOUND_RATE;
        float s = 0.0f;
        for (int k = 0; k < k_sounds[event].count; ++k) {
            const ui_tone* tone = &k_sounds[event].tone[k];
            const float lt = t - tone->start;
            if (lt < 0.0f || lt >= tone->dur) continue;
            /* Linear glide f0 -> f1: phase is the integral of the frequency. */
            const float phase = two_pi * (tone->f0 * lt + (tone->f1 - tone->f0) * lt * lt / (2.0f * tone->dur));
            s += envelope(lt, tone->dur) * sinf(phase);
        }
        s *= UI_PEAK;
        out[i] = s > UI_LIMIT ? UI_LIMIT : (s < -UI_LIMIT ? -UI_LIMIT : s);
    }
    return n;
}
```

`libs/video/rsx_overlay_feedback_apple.h`:

```c
#ifndef PS3RECOMP_RSX_OVERLAY_FEEDBACK_APPLE_H
#define PS3RECOMP_RSX_OVERLAY_FEEDBACK_APPLE_H
#ifdef __cplusplus
extern "C" {
#endif
/* The haptic hook (cpgc_ui_tick in the runtime); NULL disables haptics. */
void rsx_overlay_feedback_apple_set_haptic(void (*tick)(float intensity, float sharpness));
/* rsx_overlay_feedback_sink: never blocks the caller. */
void rsx_overlay_feedback_apple_play(int event, int sounds, int haptics, void* userdata);
int rsx_overlay_feedback_apple_state(void);
void rsx_overlay_feedback_apple_shutdown(void);
#ifdef __cplusplus
}
#endif
#endif
```

`libs/video/rsx_overlay_feedback_apple.m` (ARC):

```objc
/* UI sounds on a dedicated AVAudioEngine (independent of the game's
 * AudioUnit) and UI haptics through the runtime's async haptics queue.
 * All engine work runs on one serial queue; play() only enqueues. */
#import <AVFoundation/AVFoundation.h>

#include <stdatomic.h>

#include "rsx_overlay_feedback.h"
#include "rsx_overlay_feedback_apple.h"
#include "rsx_overlay_log.h"
#include "rsx_overlay_ui_sound.h"

static void (*_Atomic s_haptic)(float, float);
static atomic_int s_state;           /* 0 idle, 1 running, -1 failed */
static AVAudioEngine* s_engine;      /* ui_queue only */
static AVAudioPlayerNode* s_player;
static AVAudioPCMBuffer* s_buffers[RSX_OVERLAY_FEEDBACK_COUNT];

static const float k_haptic[RSX_OVERLAY_FEEDBACK_COUNT][2] = {
    { 0.5f, 0.3f }, { 0.5f, 0.3f }, { 0.35f, 0.8f }, { 0.6f, 0.6f }, { 0.4f, 0.4f }, { 0.8f, 1.0f }
};

static dispatch_queue_t ui_queue(void)
{
    static dispatch_queue_t q;
    static dispatch_once_t once;
    dispatch_once(&once, ^{ q = dispatch_queue_create("ps3recomp.ui-sound", DISPATCH_QUEUE_SERIAL); });
    return q;
}

static int ui_audio_start(void) /* ui_queue */
{
    const int state = atomic_load(&s_state);
    if (state != 0) return state > 0;
    AVAudioFormat* fmt = [[AVAudioFormat alloc] initStandardFormatWithSampleRate:RSX_OVERLAY_UI_SOUND_RATE
                                                                        channels:1];
    s_engine = [[AVAudioEngine alloc] init];
    s_player = [[AVAudioPlayerNode alloc] init];
    [s_engine attachNode:s_player];
    [s_engine connect:s_player to:s_engine.mainMixerNode format:fmt];
    for (int e = 0; e < RSX_OVERLAY_FEEDBACK_COUNT; ++e) {
        const AVAudioFrameCount frames = (AVAudioFrameCount)rsx_overlay_ui_sound_length(e);
        AVAudioPCMBuffer* b = [[AVAudioPCMBuffer alloc] initWithPCMFormat:fmt frameCapacity:frames];
        b.frameLength = (AVAudioFrameCount)rsx_overlay_ui_sound_render(e, b.floatChannelData[0], frames);
        s_buffers[e] = b;
    }
    NSError* err = nil;
    if (![s_engine startAndReturnError:&err]) {
        char line[160];
        snprintf(line, sizeof line, "overlay: UI sounds unavailable (%s)",
                 err ? err.localizedDescription.UTF8String : "engine start failed");
        rsx_overlay_log_write(RSX_OVERLAY_LOG_WARN, line);
        s_engine = nil;
        s_player = nil;
        atomic_store(&s_state, -1);
        return 0;
    }
    [s_player play];
    atomic_store(&s_state, 1);
    return 1;
}

void rsx_overlay_feedback_apple_set_haptic(void (*tick)(float, float))
{
    atomic_store(&s_haptic, tick);
}

void rsx_overlay_feedback_apple_play(int event, int sounds, int haptics, void* userdata)
{
    (void)userdata;
    if (event < 0 || event >= RSX_OVERLAY_FEEDBACK_COUNT)
        return;
    if (haptics) {
        void (*tick)(float, float) = atomic_load(&s_haptic);
        if (tick) tick(k_haptic[event][0], k_haptic[event][1]);
    }
    if (!sounds || atomic_load(&s_state) < 0)
        return;
    dispatch_async(ui_queue(), ^{
        if (!ui_audio_start()) return;
        [s_player scheduleBuffer:s_buffers[event] atTime:nil
                         options:AVAudioPlayerNodeBufferInterrupts completionHandler:nil];
        if (!s_player.playing) [s_player play];
    });
}

int rsx_overlay_feedback_apple_state(void)
{
    return atomic_load(&s_state);
}

void rsx_overlay_feedback_apple_shutdown(void)
{
    dispatch_sync(ui_queue(), ^{
        [s_player stop];
        [s_engine stop];
        s_player = nil;
        s_engine = nil;
        for (int e = 0; e < RSX_OVERLAY_FEEDBACK_COUNT; ++e) s_buffers[e] = nil;
        atomic_store(&s_state, 0);
    });
}
```

CMake: append `libs/video/rsx_overlay_ui_sound.c` explicitly to `HLE_SOURCES` (after the assets line); in the `if(APPLE)` block append `rsx_overlay_feedback_apple.m` to `HLE_SOURCES` and `PS3RECOMP_OBJC_ARC_SOURCES`.

Note (tests): the four targets that compile `rsx_overlay.cpp` do not change in this task — the core does not call the new consumers until Task 7.

- [ ] **Step 6: Run to verify pass**

Run: `cmake --build build-macos --target test_audio_muted test_rsx_overlay_ui_sound test_rsx_overlay_feedback_apple test_pad_overlay_route ps3recomp_runtime test_runtime_overlay_link && ctest --test-dir build-macos -R 'audio_muted|ui_sound|feedback_apple|pad_overlay_route|runtime_overlay_link' --output-on-failure`
Expected: all PASS.
Then: `for f in libs/audio/cellAudio.c libs/audio/audio_mute.c libs/input/cellPad.c libs/video/rsx_overlay_ui_sound.c; do x86_64-w64-mingw32-gcc -fsyntax-only -std=c11 $MW $f || echo FAIL $f; done` — no output.

- [ ] **Step 7: Commit**

```bash
git add libs/audio/audio_mute.c libs/audio/cellAudio.h libs/audio/cellAudio.c libs/audio/tests/test_audio_muted.c \
  libs/input/cellPad.h libs/input/cellPad.c libs/input/cellpad_gamecontroller.h libs/input/cellpad_gamecontroller.m \
  libs/input/tests/test_pad_overlay_route.c libs/video/rsx_overlay_ui_sound.h libs/video/rsx_overlay_ui_sound.c \
  libs/video/rsx_overlay_feedback_apple.h libs/video/rsx_overlay_feedback_apple.m \
  libs/video/tests/test_rsx_overlay_ui_sound.c libs/video/tests/test_rsx_overlay_feedback_apple.m CMakeLists.txt
git commit -m "feat(overlay): mudo em runtime, trava de vibração, sons e háptica da interface"
```

---

### Task 6: PS/Home button as a host-only bit, controller status

**Files:**
- Modify: `libs/input/cellpad_internal.h:42-44` (field), `libs/input/cellpad_gamecontroller.m:149-186` (`poll_controller`), `:188-230` (`cpgc_poll`)
- Modify: `libs/input/cellpad_overlay.h` (Home helper), `libs/input/cellPad.c:~912` (call before autostart)
- Modify: `libs/video/rsx_overlay.h`, `libs/video/rsx_overlay.cpp` (Home edge, controller status)
- Test: `libs/input/tests/test_pad_overlay_route.c`, `libs/video/tests/test_rsx_overlay.c`

**Interfaces:**
- Produces:

```c
/* rsx_overlay.h */
void rsx_overlay_handle_home(int down);   /* any thread; toggles on the rising edge; never a guest bit */
void rsx_overlay_publish_controller_status(int connected, int battery_percent); /* -1: unknown */
void rsx_overlay_get_controller_status(int* connected, int* battery_percent);
/* cellpad_overlay.h */
static inline void cellpad_overlay_home(int down);
/* PadHostState */ u8 host_home; /* PS/Home pressed (GameController); never reaches the guest */
/* cellpad_overlay.h: one port's host -> guest frame exactly as cellPadGetData builds it
 * (autostart aside). Consumes PS/Home on port 0; outputs the filtered host bits (for
 * pressure), the translated guest bits and the packed axes. */
static inline void cellpad_overlay_port_frame(unsigned port, const PadHostState* hs,
    const u16 map[CELL_PAD_HOST_BUTTON_COUNT], u8 deadzone,
    u16* host_filtered, u16* guest_buttons, u32* axes);
```

- [ ] **Step 1: Write the failing tests**

`test_pad_overlay_route.c` (call from `main`):

```c
/* PS/Home is a host-only bit: it toggles the menu on its rising edge and is
 * never a CELL_PAD_CTRL_* bit, so the game cannot see it. */
static int test_home_button_toggles_and_never_reaches_guest(void)
{
    CHECK(init_closed());
    cellpad_overlay_home(1);
    CHECK(rsx_overlay_visible());
    cellpad_overlay_home(1);                 /* held: no repeat toggle */
    CHECK(rsx_overlay_visible());
    CHECK(cellpad_overlay_filter(CELL_PAD_CTRL_CROSS, CELL_PAD_CTRL_CROSS) == 0);
    CHECK(cellpad_overlay_filter(0, 0) == 0);
    cellpad_overlay_home(0);
    cellpad_overlay_home(1);
    CHECK(!rsx_overlay_visible());
    cellpad_overlay_home(0);
    CHECK(cellpad_overlay_filter(CELL_PAD_CTRL_START, CELL_PAD_CTRL_START) == CELL_PAD_CTRL_START);
    CHECK(cellpad_overlay_filter(0, 0) == 0);
    /* Focus loss forgets a held Home: the next report counts as a press. */
    cellpad_overlay_home(1);
    CHECK(rsx_overlay_visible());
    rsx_overlay_focus_lost();
    cellpad_overlay_home(1);
    CHECK(!rsx_overlay_visible());
    cellpad_overlay_home(0);
    /* A disabled overlay ignores Home. */
    rsx_overlay_disable("test: renderer unavailable");
    cellpad_overlay_home(1);
    CHECK(!rsx_overlay_visible());
    done();
    return 1;
}
```

Guest isolation through the real per-port frame (the code `cellPadGetData` runs; call from `main`):

```c
/* PS/Home toggles the shell but the guest frame never carries it: with R3
 * held (outside the menu's reserved set) and neutral sticks, every frame gives
 * the game exactly R3 and centered axes -- through press, hold, release,
 * a second toggle and a disconnect with a stale Home bit. */
static int test_home_never_reaches_guest_frames(void)
{
    static const uint8_t home[7] = { 0, 1, 1, 0, 1, 0, 0 };
    static const int visible[7] = { 0, 1, 1, 1, 0, 0, 0 };
    u16 map[CELL_PAD_HOST_BUTTON_COUNT], host, guest;
    u32 axes;
    PadHostState hs;

    for (unsigned i = 0; i < CELL_PAD_HOST_BUTTON_COUNT; ++i)
        map[i] = (u16)(1u << i);
    memset(&hs, 0, sizeof hs);
    hs.connected = 1;
    hs.analog_lx = hs.analog_ly = hs.analog_rx = hs.analog_ry = 128;
    hs.controller_axes = CELL_PAD_HOST_AXIS_ALL;
    hs.buttons = hs.controller_buttons = CELL_PAD_CTRL_R3;
    CHECK(init_closed());
    for (int i = 0; i < 7; ++i) {
        hs.host_home = home[i];
        cellpad_overlay_port_frame(0, &hs, map, 0, &host, &guest, &axes);
        CHECK(guest == CELL_PAD_CTRL_R3);
        CHECK(axes == 0x80808080u);
        CHECK(rsx_overlay_visible() == visible[i]);
    }
    /* Disconnected pad with a stale Home bit: no toggle, nothing for the game. */
    hs.connected = 0;
    hs.host_home = 1;
    hs.buttons = hs.controller_buttons = 0;
    cellpad_overlay_port_frame(0, &hs, map, 0, &host, &guest, &axes);
    CHECK(guest == 0 && axes == 0x80808080u && !rsx_overlay_visible());
    /* Other ports never consume Home. */
    hs.connected = 1;
    cellpad_overlay_port_frame(1, &hs, map, 0, &host, &guest, &axes);
    CHECK(!rsx_overlay_visible());
    done();
    return 1;
}
```

(`test_pad_overlay_route.c` needs `#include <string.h>` and `#include "cellpad_internal.h"`.)

`test_rsx_overlay.c` (call from `main` before `test_disabled_core_is_inert`):

```c
static int test_controller_status_is_never_invented(void)
{
    int connected = -2, battery = -2;
    rsx_overlay_get_controller_status(&connected, &battery);
    CHECK(connected == 0 && battery == -1);   /* nothing published yet */
    rsx_overlay_publish_controller_status(1, 57);
    rsx_overlay_get_controller_status(&connected, &battery);
    CHECK(connected == 1 && battery == 57);
    rsx_overlay_publish_controller_status(1, 150);  /* out of range: unknown */
    rsx_overlay_get_controller_status(&connected, &battery);
    CHECK(battery == -1);
    rsx_overlay_publish_controller_status(0, 80);   /* no controller: no battery */
    rsx_overlay_get_controller_status(&connected, &battery);
    CHECK(connected == 0 && battery == -1);
    return 1;
}
```

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build build-macos --target test_pad_overlay_route test_rsx_overlay`
Expected: undeclared `cellpad_overlay_home`, `rsx_overlay_publish_controller_status`.

- [ ] **Step 3: Implement**

`rsx_overlay.h` declarations as in **Interfaces**. `rsx_overlay.cpp`:

```cpp
static std::atomic<int> s_home_down(0);
static std::atomic<int> s_controller_connected(0);
static std::atomic<int> s_battery_percent(-1);

/* Any thread, atomics only (cellPadGetData runs on guest threads). */
static void rsx_overlay_toggle_atomic(void)
{
    if (s_visible.load()) {
        s_visible.store(0);
        s_ui_pad_buttons.store(0);
    } else {
        s_open_count.fetch_add(1);
        s_visible.store(1);
    }
}

void rsx_overlay_handle_home(int down)
{
    if (!s_initialized.load() || s_disabled.load())
        return;
    const int was_down = s_home_down.exchange(down != 0);
    if (down && !was_down && s_settings.overlay_enabled)
        rsx_overlay_toggle_atomic();
}

void rsx_overlay_publish_controller_status(int connected, int battery_percent)
{
    s_controller_connected.store(connected ? 1 : 0);
    s_battery_percent.store(connected && battery_percent >= 0 && battery_percent <= 100
                            ? battery_percent : -1);
}

void rsx_overlay_get_controller_status(int* connected, int* battery_percent)
{
    if (connected != nullptr) *connected = s_controller_connected.load();
    if (battery_percent != nullptr) *battery_percent = s_battery_percent.load();
}
```

Add `s_home_down.store(0);` to `rsx_overlay_focus_lost`, `rsx_overlay_disable`, `rsx_overlay_init` (next to `s_chord_down.store(0)`) and `rsx_overlay_shutdown`.

`cellpad_internal.h` after `u16 controller_buttons;`:

```c
    /* PS/Home on the controller (GameController buttonHome). Host-only: it
     * toggles the runtime overlay and is never translated into a guest bit. */
    u8   host_home;
```

`cellpad_overlay.h`: add `#include "cellpad_internal.h"` next to `#include "cellPad.h"`, and after `cellpad_overlay_axes`:

```c
/* PS/Home: host-only, toggles the overlay; nothing is added to guest buttons. */
static inline void cellpad_overlay_home(int down)
{
    rsx_overlay_handle_home(down);
}

/* One port's host -> guest frame, exactly as cellPadGetData builds it when
 * autostart does not own the port. PS/Home is consumed here (port 0, only
 * from a connected pad) and is never translated into a guest bit. */
static inline void cellpad_overlay_port_frame(unsigned port, const PadHostState* hs,
    const u16 map[CELL_PAD_HOST_BUTTON_COUNT], u8 deadzone,
    u16* host_filtered, u16* guest_buttons, u32* axes)
{
    u16 host = hs->buttons;
    if (port == 0) {
        cellpad_overlay_home(hs->connected && hs->host_home);
        host = cellpad_overlay_filter(host, hs->controller_buttons);
    }
    *host_filtered = host;
    *guest_buttons = cellPad_translate_host_buttons(host, map);
    *axes = cellpad_overlay_axes(port, hs->analog_rx, hs->analog_ry, hs->analog_lx,
                                 hs->analog_ly, hs->controller_axes, deadzone);
}
```

`cellPad.c` (`cellPadGetData`): make the real path use that helper. Replace

```c
    if (!autostart_own) {
        if (port_no == 0)
            host_buttons = cellpad_overlay_filter(host_buttons, hs->controller_buttons);
        btns = cellPad_translate_host_buttons(host_buttons, guest_button_map);
    }
```

with

```c
    u32 frame_axes = 0x80808080u;
    if (autostart_own) {
        if (port_no == 0)
            cellpad_overlay_home(hs->connected && hs->host_home); /* PS/Home still opens the menu */
    } else {
        cellpad_overlay_port_frame(port_no, hs, guest_button_map, analog_deadzone,
                                   &host_buttons, &btns, &frame_axes);
    }
```

and in the report-length block replace the `cellpad_overlay_axes(port_no, ...)` call with `frame_axes` (keeping `autostart_own ? 0x80808080u :`). `host_buttons` keeps feeding `pad_set_host_pressure` as before.

`cellpad_gamecontroller.m`: add `#include "rsx_overlay.h"`; in `poll_controller` before `st->buttons = b;`:

```objc
    if (@available(macOS 11.0, *)) {
        GCControllerButtonInput* home = gp.buttonHome;
        if (home) {
            st->host_home = home.pressed ? 1 : 0;
            /* Ask macOS not to act on the PS/Home press while we run (Launchpad
             * / Game Center overlay); set once per element. */
            static __weak GCControllerElement* s_claimed;
            if (s_claimed != home) {
                home.preferredSystemGestureState = GCSystemGestureStateDisabled;
                s_claimed = home;
            }
        }
    }
```

and at the end of `cpgc_poll`:

```objc
    /* Top-strip status for the overlay, at most once a second. The battery is
     * only reported when the controller knows it (never a made-up value). */
    static double s_status_t;
    const double now = CFAbsoluteTimeGetCurrent();
    if (now - s_status_t >= 1.0) {
        s_status_t = now;
        int pct = -1;
        if (ctrls.count > 0) {
            if (@available(macOS 11.0, *)) {
                GCDeviceBattery* bat = ctrls[0].battery;
                if (bat && bat.batteryState != GCDeviceBatteryStateUnknown)
                    pct = (int)lroundf(bat.batteryLevel * 100.0f);
            }
        }
        rsx_overlay_publish_controller_status(ctrls.count > 0, pct);
    }
```

- [ ] **Step 4: Run to verify pass**

Run: `cmake --build build-macos --target test_pad_overlay_route test_rsx_overlay ps3recomp_runtime && ctest --test-dir build-macos -R 'pad_overlay_route|^test_rsx_overlay$' --output-on-failure`
Expected: PASS, including `test_home_never_reaches_guest_frames`. Then `x86_64-w64-mingw32-gcc -fsyntax-only -std=c11 $MW libs/input/cellPad.c` and `x86_64-w64-mingw32-g++ -fsyntax-only -std=c++20 $MW libs/video/rsx_overlay.cpp` — no output (XInput/SDL leave `host_home` 0; Windows keeps Select+Start and F1).

- [ ] **Step 5: Commit**

```bash
git add libs/input/cellpad_internal.h libs/input/cellpad_gamecontroller.m libs/input/cellpad_overlay.h \
  libs/input/cellPad.c libs/video/rsx_overlay.h libs/video/rsx_overlay.cpp \
  libs/input/tests/test_pad_overlay_route.c libs/video/tests/test_rsx_overlay.c
git commit -m "feat(pad): botão PS/Home abre a central sem chegar ao jogo; bateria do controle"
```

---
### Task 7: Shell drawing and core wiring (replaces the ImGui tabs)

**Files:**
- Create: `libs/video/rsx_overlay_shell_draw.h`, `libs/video/rsx_overlay_shell_draw.cpp`
- Modify: `libs/video/rsx_overlay.h` (new API, test hooks: remove `rsx_overlay_test_imgui_key_down`, `rsx_overlay_test_nav_id`, `rsx_overlay_test_popup_open`)
- Modify: `libs/video/rsx_overlay.cpp` — replace `rsx_overlay_navigation_mask`'s neighbours `rsx_overlay_update_imgui_pad`/`rsx_overlay_update_imgui_key` (`:124-154`), `rsx_overlay_apply_input_settings` (`:156-160`), init/shutdown (`:162-263`), `rsx_overlay_release_imgui_input`/`rsx_overlay_popup_open`/`rsx_overlay_hide_from_keyboard` (`:270-299`), `rsx_overlay_handle_key` (`:301-347`), `rsx_overlay_handle_pad` (`:349-395`), `rsx_overlay_focus_lost` (`:397-406`), `rsx_overlay_disable` (`:408-424`), `rsx_overlay_persist_settings` (`:516-544`), delete `rsx_overlay_render_controls` (`:595-635`) and `rsx_overlay_render_graphics` + `rsx_overlay_support_suffix` (`:721-790`), replace `rsx_overlay_build_draw_data` (`:853-927`), test hooks (`:938-986`); Task 6's `rsx_overlay_toggle_atomic` body
- Modify: `libs/input/cellpad_overlay.h` (release guard), `libs/input/cellPad.h` (nothing new)
- Modify: `libs/video/rsx_overlay_d3d12.c:432-434` (gate), `libs/video/rsx_overlay_metal.m:50-54` (core visible → render active)
- Modify: `libs/video/rsx_metal_backend.m` (init `:11181-11205`, shutdown `:11591-11593`, pump tail), `libs/video/rsx_d3d12_backend.c` (pump `:2888-2905`)
- Modify: `games/gow2/boot_macos.cpp:428-430` (title)
- Modify: `CMakeLists.txt` (sources; the four core-linking tests)
- Test: `libs/video/tests/test_rsx_overlay.c`, `libs/input/tests/test_pad_overlay_route.c`, `libs/video/tests/test_rsx_overlay_metal.m`, `games/gow2/tests/test_gow2_overlay_provider.c`

**Interfaces:**
- Consumes: Task 1 (`rsx_overlay_get_display`), Task 2 (v2 settings), Task 3 (whole shell API), Task 4 (`rsx_overlay_assets*`, `rsx_overlay_asset_builder`), Task 5 (`ps3_audio_set_runtime_mute`, `cellPad_set_host_vibration`, `rsx_overlay_feedback_apple_*`, `cpgc_ui_tick`), Task 6 (`rsx_overlay_handle_home`, controller status).
- Produces (`rsx_overlay.h`):

```c
int  rsx_overlay_render_active(void);          /* shell opening/open/closing: renderers draw, pumps build */
int  rsx_overlay_take_quit_request(void);      /* 1 once after "Sair" was confirmed */
void rsx_overlay_set_title(const char* title); /* before the backend starts; default "Jogo" */
void rsx_overlay_set_feedback_sink(rsx_overlay_feedback_sink sink, void* userdata);
void rsx_overlay_set_asset_builder(rsx_overlay_asset_builder builder); /* NULL: embedded Inter */
/* cellpad_overlay_filter: every host bit (keyboard included) of this frame; returns
 * the buttons still withheld because they were held when the menu closed. */
uint16_t rsx_overlay_release_guard(uint16_t host_buttons);
#ifdef RSX_OVERLAY_TESTING
void  rsx_overlay_test_set_clock(double seconds);          /* < 0: real clock */
int   rsx_overlay_test_input_down(int code);               /* RSX_OVERLAY_KEY_* or CELL_PAD_CTRL_* as the last build saw it */
void  rsx_overlay_test_shell_state(int* phase, int* level, int* card, int* row, int* list_index);
float rsx_overlay_test_atlas_scale(void);
#endif
```

- **Atlas contract** (replaces the ImGui font as texture 0; both renderers rely on exactly this):
  1. Every draw command uses `texture_id == 0`; renderers draw nothing else (`rsx_overlay_command_uses_atlas`, below).
  2. Texture 0 is `rsx_overlay_copy_font_atlas()`: RGBA8 unorm, **straight** (non-premultiplied) alpha, RGB = 255 everywhere, coverage in A. Metal uploads it as `MTLPixelFormatRGBA8Unorm`, D3D12 as `DXGI_FORMAT_R8G8B8A8_UNORM`; both blend `SrcAlpha / OneMinusSrcAlpha` (already true in `rsx_overlay_metal.m` `overlay_create_pipeline` and `rsx_overlay_d3d12.c` `overlay_create_pipeline`).
  3. The opaque 4×4 block at (`white_x`, `white_y`) is the only texel solid shapes sample; its UV is the block centre `((white_x + 2) / w, (white_y + 2) / h)`, so bilinear filtering never reaches a transparent texel.
  4. `rsx_overlay_font_atlas_generation()` changes whenever the pixels change; renderers re-upload on change (Metal) or never see a change (D3D12: scale fixed at 1.0).
  Pinned by `test_atlas_contract` (core), `test_real_core_frame`'s dim-pixel check (Metal) and `test_command_atlas_contract` (D3D12 helper).
- Produces (`rsx_overlay_shell_draw.h`): `rsx_shell_view` and `void rsx_shell_draw(ImDrawList*, const rsx_shell*, const rsx_shell_layout*, const rsx_overlay_assets*, const rsx_shell_view*, double now);`

- [ ] **Step 1: Update the existing tests to the shell and add the new ones**

Shared changes in `libs/video/tests/test_rsx_overlay.c`:
- includes: add `#include "rsx_overlay_feedback.h"` and `#include "rsx_overlay_shell.h"`; declare `int ps3_audio_runtime_muted(void); void ps3_audio_set_runtime_mute(int muted);`
- next to the `cellPad_set_host_mapping` stub:

```c
static uint8_t s_last_vibration = 1;
void cellPad_set_host_vibration(int enabled) { s_last_vibration = (uint8_t)(enabled != 0); }

static double s_now = 1000.0;
static void tick(double dt) { s_now += dt; rsx_overlay_test_set_clock(s_now); }
static void settle(void) { tick(1.0); rsx_overlay_build_draw_data(); }
```

- `main`: first statement `rsx_overlay_test_set_clock(s_now);`.
- Rename every `rsx_overlay_test_imgui_key_down(` to `rsx_overlay_test_input_down(`.
- `open_menu()` becomes

```c
static int open_menu(void)
{
    if (!rsx_overlay_handle_key(RSX_OVERLAY_KEY_F1, 1, 0) ||
        !rsx_overlay_handle_key(RSX_OVERLAY_KEY_F1, 0, 0))
        return 0;
    tick(0.02);
    rsx_overlay_build_draw_data();
    settle();
    return rsx_overlay_visible();
}
```

- `tap_key` becomes `static void tap_key(int key)` pressing, `tick(0.02)`, building, releasing, `tick(0.02)`, building (no return value).
- `test_draw_packet_is_a_bounded_copy`: replace its two consecutive `rsx_overlay_build_draw_data();` with `tick(0.02); rsx_overlay_build_draw_data(); settle();`, and after the final assertions close the menu again with an F1 press/release so the next test starts as before.
- `test_reopen_releases_held_keys`: replace both uses of `RSX_OVERLAY_KEY_ENTER` with `RSX_OVERLAY_KEY_LEFT` (Enter on "Voltar ao jogo" now closes the menu).
- Delete `test_arrow_keys_move_nav_focus` and `test_escape_with_popup_closes_popup_only`; in `main` call the replacements below instead.

New tests (after `open_menu`/`tap_key`; add each to `main` before `test_disabled_core_is_inert()`):

```c
static int test_arrow_keys_move_card_focus(void)
{
    char path[64];
    int phase, level, card, row, index;

    CHECK(fresh_init(path, sizeof path));
    CHECK(open_menu());
    rsx_overlay_test_shell_state(&phase, &level, &card, &row, &index);
    CHECK(phase == RSX_SHELL_OPEN && level == RSX_SHELL_LEVEL_CARDS && card == RSX_SHELL_CARD_RESUME);
    tap_key(RSX_OVERLAY_KEY_RIGHT);
    tap_key(RSX_OVERLAY_KEY_RIGHT);
    rsx_overlay_test_shell_state(&phase, &level, &card, &row, &index);
    CHECK(card == RSX_SHELL_CARD_CONTROLS);
    tap_key(RSX_OVERLAY_KEY_LEFT);
    rsx_overlay_test_shell_state(&phase, &level, &card, &row, &index);
    CHECK(card == RSX_SHELL_CARD_DISPLAY);
    tap_key(RSX_OVERLAY_KEY_RIGHT);
    tap_key(RSX_OVERLAY_KEY_DOWN);
    rsx_overlay_test_shell_state(&phase, &level, &card, &row, &index);
    CHECK(level == RSX_SHELL_LEVEL_PANEL && row == 0);
    rsx_overlay_shutdown();
    unlink(path);
    return 1;
}

/* Esc backs out one level; at card level a fresh press closes, and the whole
 * press (auto-repeat included) stays with the overlay. */
static int test_escape_backs_out_one_level(void)
{
    char path[64];
    int phase, level, card, row, index;

    CHECK(fresh_init(path, sizeof path));
    CHECK(open_menu());
    tap_key(RSX_OVERLAY_KEY_RIGHT);
    tap_key(RSX_OVERLAY_KEY_RIGHT);
    tap_key(RSX_OVERLAY_KEY_ENTER);
    rsx_overlay_test_shell_state(&phase, &level, &card, &row, &index);
    CHECK(level == RSX_SHELL_LEVEL_PANEL);
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_ESCAPE, 1, 0) == 1);
    tick(0.02);
    rsx_overlay_build_draw_data();
    rsx_overlay_test_shell_state(&phase, &level, &card, &row, &index);
    CHECK(level == RSX_SHELL_LEVEL_CARDS && rsx_overlay_visible());
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_ESCAPE, 1, 0) == 1); /* auto-repeat */
    tick(0.02);
    rsx_overlay_build_draw_data();
    CHECK(rsx_overlay_visible());
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_ESCAPE, 0, 0) == 1);
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_ESCAPE, 1, 0) == 1);
    CHECK(!rsx_overlay_visible());
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_ESCAPE, 1, 0) == 1);
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_ESCAPE, 0, 0) == 1);
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_ESCAPE, 1, 0) == 0);
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_ESCAPE, 0, 0) == 0);
    rsx_overlay_shutdown();
    unlink(path);
    return 1;
}

static int test_closed_shell_costs_nothing(void)
{
    char path[64];
    uint16_t guest;
    rsx_overlay_draw_packet* packet = (rsx_overlay_draw_packet*)calloc(1, sizeof(*packet));

    CHECK(packet != NULL);
    CHECK(fresh_init(path, sizeof path));
    CHECK(!rsx_overlay_render_active());
    CHECK(open_menu());
    CHECK(rsx_overlay_render_active());
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_F1, 1, 0));
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_F1, 0, 0));
    CHECK(!rsx_overlay_visible());
    /* The close animation still renders; input already belongs to the game. */
    CHECK(rsx_overlay_render_active());
    guest = CELL_PAD_CTRL_CROSS;
    CHECK(rsx_overlay_handle_pad(CELL_PAD_CTRL_CROSS, &guest) == 0 && guest == CELL_PAD_CTRL_CROSS);
    /* F1 again mid-animation reopens; F1 once more closes. */
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_F1, 1, 0));
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_F1, 0, 0));
    tick(0.02);
    rsx_overlay_build_draw_data();
    CHECK(rsx_overlay_visible() && rsx_overlay_render_active());
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_F1, 1, 0));
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_F1, 0, 0));
    tick(0.02);
    rsx_overlay_build_draw_data();
    settle();
    CHECK(!rsx_overlay_render_active());
    rsx_overlay_build_draw_data();
    CHECK(rsx_overlay_copy_draw_packet(packet) == 1);
    CHECK(packet->vertex_count == 0 && packet->open_progress == 0.0f);
    for (uint32_t bits = 0; bits <= 0xFFFFu; ++bits) {
        if ((bits & CHORD) == CHORD)
            continue;
        guest = (uint16_t)bits;
        CHECK(rsx_overlay_handle_pad((uint16_t)bits, &guest) == 0);
        CHECK(guest == (uint16_t)bits);
    }
    rsx_overlay_shutdown();
    unlink(path);
    free(packet);
    return 1;
}

static int test_draw_budget_worst_case(void)
{
    char path[64];
    char line[RSX_OVERLAY_LOG_MESSAGE_CAPACITY];
    int phase, level, card, row, index;
    rsx_overlay_draw_packet* packet = (rsx_overlay_draw_packet*)calloc(1, sizeof(*packet));

    CHECK(packet != NULL);
    memset(line, 'W', sizeof line - 1u);
    line[sizeof line - 1u] = '\0';
    for (unsigned i = 0; i < RSX_OVERLAY_LOG_ENTRY_COUNT; ++i)
        rsx_overlay_log_write(RSX_OVERLAY_LOG_ERROR, line);
    CHECK(fresh_init(path, sizeof path));
    rsx_overlay_set_display(3840u, 2160u, 2.0f);
    CHECK(open_menu());
    for (int c = 0; c < RSX_SHELL_CARD_COUNT; ++c) {
        rsx_overlay_build_draw_data();
        CHECK(rsx_overlay_copy_draw_packet(packet) == 1);
        CHECK(packet->vertex_count > 0 && packet->vertex_count < RSX_OVERLAY_DRAW_VERTEX_CAPACITY);
        CHECK(packet->index_count < RSX_OVERLAY_DRAW_INDEX_CAPACITY);
        CHECK(packet->command_count > 0 && packet->command_count < RSX_OVERLAY_DRAW_COMMAND_CAPACITY);
        tap_key(RSX_OVERLAY_KEY_RIGHT);
    }
    tap_key(RSX_OVERLAY_KEY_LEFT);   /* Desenvolvedor */
    tap_key(RSX_OVERLAY_KEY_ENTER);  /* panel, Logs row */
    tap_key(RSX_OVERLAY_KEY_ENTER);  /* Logs list */
    rsx_overlay_test_shell_state(&phase, &level, &card, &row, &index);
    CHECK(card == RSX_SHELL_CARD_DEVELOPER && level == RSX_SHELL_LEVEL_LIST);
    rsx_overlay_build_draw_data();
    CHECK(rsx_overlay_copy_draw_packet(packet) == 1);
    CHECK(packet->vertex_count > 0 && packet->vertex_count < RSX_OVERLAY_DRAW_VERTEX_CAPACITY);
    CHECK(packet->index_count < RSX_OVERLAY_DRAW_INDEX_CAPACITY);
    rsx_overlay_set_display(1280u, 720u, 1.0f);
    rsx_overlay_shutdown();
    unlink(path);
    free(packet);
    return 1;
}

static int test_scale_change_rebuilds_atlas(void)
{
    char path[64];
    uint64_t g0;

    CHECK(fresh_init(path, sizeof path));
    rsx_overlay_set_display(1280u, 720u, 1.0f);
    g0 = rsx_overlay_font_atlas_generation();
    CHECK(g0 > 0 && rsx_overlay_test_atlas_scale() == 1.0f);
    rsx_overlay_set_display(1600u, 900u, 1.0f);
    CHECK(rsx_overlay_font_atlas_generation() == g0);   /* same scale: no work */
    rsx_overlay_set_display(2560u, 1440u, 2.0f);
    CHECK(rsx_overlay_font_atlas_generation() > g0);
    CHECK(rsx_overlay_test_atlas_scale() == 2.0f);
    CHECK(rsx_overlay_copy_font_atlas(NULL, 0, NULL, NULL) == 1);
    rsx_overlay_set_display(1280u, 720u, 1.0f);
    rsx_overlay_shutdown();
    unlink(path);
    return 1;
}

static int test_quit_dialog(void)
{
    char path[64];
    int phase, level, card, row, index;

    CHECK(fresh_init(path, sizeof path));
    CHECK(open_menu());
    for (int i = 0; i < 5; ++i)
        tap_key(RSX_OVERLAY_KEY_RIGHT);
    tap_key(RSX_OVERLAY_KEY_ENTER);
    rsx_overlay_test_shell_state(&phase, &level, &card, &row, &index);
    CHECK(level == RSX_SHELL_LEVEL_DIALOG);
    tap_key(RSX_OVERLAY_KEY_ENTER);                    /* default: Cancelar */
    CHECK(rsx_overlay_take_quit_request() == 0 && rsx_overlay_visible());
    tap_key(RSX_OVERLAY_KEY_ENTER);
    tap_key(RSX_OVERLAY_KEY_LEFT);                     /* Sair */
    tap_key(RSX_OVERLAY_KEY_ENTER);
    CHECK(!rsx_overlay_visible());
    CHECK(rsx_overlay_take_quit_request() == 1);
    CHECK(rsx_overlay_take_quit_request() == 0);
    rsx_overlay_shutdown();
    unlink(path);
    return 1;
}

static int s_fb_count;
static int s_fb_event[128], s_fb_sounds[128], s_fb_haptics[128];
static void record_feedback(int event, int sounds, int haptics, void* userdata)
{
    (void)userdata;
    if (s_fb_count < 128) {
        s_fb_event[s_fb_count] = event;
        s_fb_sounds[s_fb_count] = sounds;
        s_fb_haptics[s_fb_count] = haptics;
        ++s_fb_count;
    }
}

static int test_feedback_follows_ui_switches(void)
{
    char path[64];
    int before;

    CHECK(fresh_init(path, sizeof path));
    s_fb_count = 0;
    rsx_overlay_set_feedback_sink(record_feedback, NULL);
    CHECK(open_menu());
    CHECK(s_fb_count >= 1 && s_fb_event[0] == RSX_OVERLAY_FEEDBACK_OPEN);
    CHECK(s_fb_sounds[0] == 1 && s_fb_haptics[0] == 1);
    tap_key(RSX_OVERLAY_KEY_RIGHT);
    CHECK(s_fb_event[s_fb_count - 1] == RSX_OVERLAY_FEEDBACK_MOVE);
    tap_key(RSX_OVERLAY_KEY_RIGHT);
    tap_key(RSX_OVERLAY_KEY_RIGHT);                    /* Som */
    tap_key(RSX_OVERLAY_KEY_ENTER);                    /* Som do jogo */
    tap_key(RSX_OVERLAY_KEY_DOWN);                     /* Sons da interface */
    before = s_fb_count;
    tap_key(RSX_OVERLAY_KEY_ENTER);                    /* off */
    CHECK(s_fb_count > before);
    CHECK(s_fb_sounds[s_fb_count - 1] == 0 && s_fb_haptics[s_fb_count - 1] == 1);
    tap_key(RSX_OVERLAY_KEY_DOWN);
    tap_key(RSX_OVERLAY_KEY_ENTER);                    /* Vibração da interface off */
    before = s_fb_count;
    tap_key(RSX_OVERLAY_KEY_UP);
    CHECK(s_fb_count == before);                       /* both off: silent and still */
    rsx_overlay_set_feedback_sink(NULL, NULL);
    rsx_overlay_shutdown();
    unlink(path);
    return 1;
}

static int test_sound_and_vibration_reach_consumers(void)
{
    char path[64];

    CHECK(fresh_init(path, sizeof path));
    CHECK(ps3_audio_runtime_muted() == 0 && s_last_vibration == 1);
    CHECK(open_menu());
    tap_key(RSX_OVERLAY_KEY_RIGHT);
    tap_key(RSX_OVERLAY_KEY_RIGHT);
    tap_key(RSX_OVERLAY_KEY_RIGHT);                    /* Som */
    tap_key(RSX_OVERLAY_KEY_ENTER);
    tap_key(RSX_OVERLAY_KEY_ENTER);                    /* Som do jogo: off */
    CHECK(ps3_audio_runtime_muted() == 1);
    tap_key(RSX_OVERLAY_KEY_ESCAPE);
    tap_key(RSX_OVERLAY_KEY_LEFT);                     /* Controles */
    tap_key(RSX_OVERLAY_KEY_ENTER);
    tap_key(RSX_OVERLAY_KEY_DOWN);                     /* Vibração */
    tap_key(RSX_OVERLAY_KEY_ENTER);
    CHECK(s_last_vibration == 0);
    rsx_overlay_shutdown();                            /* saves */
    ps3_audio_set_runtime_mute(0);
    s_last_vibration = 1;
    CHECK(rsx_overlay_init(path));                     /* reload applies both */
    CHECK(ps3_audio_runtime_muted() == 1 && s_last_vibration == 0);
    rsx_overlay_shutdown();
    ps3_audio_set_runtime_mute(0);
    s_last_vibration = 1;
    unlink(path);
    return 1;
}

static int test_slider_saves_after_release(void)
{
    char path[64];
    rsx_overlay_settings loaded;

    CHECK(fresh_init(path, sizeof path));
    CHECK(open_menu());
    tap_key(RSX_OVERLAY_KEY_RIGHT);
    tap_key(RSX_OVERLAY_KEY_RIGHT);
    tap_key(RSX_OVERLAY_KEY_ENTER);                    /* Zona morta */
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_RIGHT, 1, 0) == 1);
    for (int i = 0; i < 12; ++i) {
        tick(0.1);
        rsx_overlay_build_draw_data();
    }
    CHECK(rsx_overlay_settings_load(&loaded, path) == 1);
    CHECK(loaded.analog_deadzone == 0);                /* nothing written while held */
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_RIGHT, 0, 0) == 1);
    tick(0.02);
    rsx_overlay_build_draw_data();
    CHECK(rsx_overlay_settings_load(&loaded, path) == 1);
    CHECK(loaded.analog_deadzone >= 8 && loaded.analog_deadzone <= 9);
    rsx_overlay_shutdown();
    unlink(path);
    return 1;
}
```

`test_rsx_overlay.c` needs `#include "rsx_overlay_log.h"` for `test_draw_budget_worst_case`.

Atlas contract, core side (add to `test_rsx_overlay.c`, call from `main`):

```c
static int test_atlas_contract(void)
{
    char path[64];
    uint32_t w = 0, h = 0;
    rsx_overlay_draw_packet* p = (rsx_overlay_draw_packet*)calloc(1, sizeof(*p));
    uint8_t* px;

    CHECK(p != NULL);
    CHECK(fresh_init(path, sizeof path));
    CHECK(open_menu());
    rsx_overlay_build_draw_data();
    CHECK(rsx_overlay_copy_draw_packet(p) == 1 && p->command_count > 0);
    CHECK(rsx_overlay_copy_font_atlas(NULL, 0, &w, &h) == 1);
    px = (uint8_t*)malloc((size_t)w * h * 4u);
    CHECK(px != NULL && rsx_overlay_copy_font_atlas(px, (size_t)w * h * 4u, &w, &h) == 1);
    for (size_t i = 0; i < (size_t)w * h; ++i)                      /* straight alpha, white RGB */
        CHECK(px[i * 4u] == 255 && px[i * 4u + 1u] == 255 && px[i * 4u + 2u] == 255);
    for (uint32_t c = 0; c < p->command_count; ++c)
        CHECK(p->commands[c].texture_id == 0);
    /* The first command is the dim rectangle: it samples the white block
     * centre, and every bilinear neighbour of that UV is opaque. */
    const rsx_overlay_draw_vertex* v0 =
        &p->vertices[p->commands[0].vertex_offset + p->indices[p->commands[0].index_offset]];
    const int tx = (int)(v0->uv[0] * (float)w), ty = (int)(v0->uv[1] * (float)h);
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
            CHECK(px[((size_t)(ty + dy) * w + (size_t)(tx + dx)) * 4u + 3u] == 255);
    /* Dim alpha follows open progress (fully open here). */
    CHECK(p->open_progress == 1.0f);
    CHECK((v0->color >> 24) == (uint32_t)(RSX_SHELL_BACKDROP_DIM * 255.0f + 0.5f));
    free(px);
    free(p);
    rsx_overlay_shutdown();
    unlink(path);
    return 1;
}
```

Atlas contract, renderer side: in `rsx_overlay_packet.h` add

```c
/* Atlas contract: texture 0 is the shell atlas (RGBA8, straight alpha, white
 * RGB); no other texture exists, so any other id is skipped. */
static inline int rsx_overlay_command_uses_atlas(const rsx_overlay_draw_command* command)
{
    return command != NULL && command->texture_id == 0;
}
```

use it in `rsx_overlay_metal.m` (replace `cmd->texture_id == 0 &&` with `rsx_overlay_command_uses_atlas(cmd) &&`) and in `rsx_overlay_d3d12.c` (replace `command->texture_id != 0 ||` with `!rsx_overlay_command_uses_atlas(command) ||`), and add to `test_rsx_overlay_d3d12.c` (count it in `main`, `total` + 1):

```c
static int test_command_atlas_contract(void)
{
    rsx_overlay_draw_command command;
    memset(&command, 0, sizeof(command));
    CHECK(rsx_overlay_command_uses_atlas(&command));
    command.texture_id = 1;
    CHECK(!rsx_overlay_command_uses_atlas(&command));
    CHECK(!rsx_overlay_command_uses_atlas(NULL));
    return 1;
}
```

In `test_rsx_overlay_metal.m` `test_real_core_frame`, after the `changed_inside > 0` check, add the pixel proof that solid shapes sample an opaque texel through the real atlas: display point (1000, 200) is clear of strip, cards and panel at 1280×720 @1x, and maps to target pixel (100, 32) in `kViewport`; the background is solid blue, so blur leaves it blue and the dim leaves 65 %:

```objc
    CHECK(pixel_is(rb, 100, 32, 166, 0, 0));
```

`libs/input/tests/test_pad_overlay_route.c`:
- add `#define RSX_OVERLAY_TESTING`-dependent helpers (target already defines it): the same `s_now`/`tick`/`settle` helpers, a `tap_key(int key)` (press, `tick(0.02)`, build, release, `tick(0.02)`, build), `rsx_overlay_test_set_clock(s_now);` as the first statement of `main`;
- stubs: replace the `cellPad_set_host_mapping` stub with one that records the map, and add the vibration stub:

```c
static uint16_t s_last_map[CELL_PAD_HOST_BUTTON_COUNT];
void cellPad_set_host_mapping(const uint16_t map[CELL_PAD_HOST_BUTTON_COUNT], uint8_t deadzone)
{
    (void)deadzone;
    memcpy(s_last_map, map, sizeof s_last_map);
}
void cellPad_set_host_vibration(int enabled) { (void)enabled; }
```

  (add `#include <string.h>`);
- `open_with_f1()`: replace its single `rsx_overlay_build_draw_data();` with `tick(0.02); rsx_overlay_build_draw_data(); settle();`;
- rename `rsx_overlay_test_imgui_key_down(` → `rsx_overlay_test_input_down(`;
- in `test_open_keyboard_enter_acts_once`, insert `tap_key(RSX_OVERLAY_KEY_RIGHT); /* on "Tela": Enter must not close */` right after `CHECK(open_with_f1());`, and put `tick(0.02);` before each `rsx_overlay_build_draw_data();` in that test;
- new tests (call from `main`):

```c
/* A button still held when the menu closes from inside is not a game press. */
static int test_closing_from_menu_withholds_held_buttons(void)
{
    CHECK(init_closed());
    CHECK(open_with_f1());
    CHECK(cellpad_overlay_filter(CELL_PAD_CTRL_CROSS, CELL_PAD_CTRL_CROSS) == 0);
    tick(0.02);
    rsx_overlay_build_draw_data();                     /* Cross on "Voltar ao jogo" */
    CHECK(!rsx_overlay_visible());
    for (int frame = 0; frame < 3; ++frame)
        CHECK(cellpad_overlay_filter(CELL_PAD_CTRL_CROSS, CELL_PAD_CTRL_CROSS) == 0);
    CHECK(cellpad_overlay_filter(0, 0) == 0);
    CHECK(cellpad_overlay_filter(CELL_PAD_CTRL_CROSS, CELL_PAD_CTRL_CROSS) == CELL_PAD_CTRL_CROSS);
    CHECK(cellpad_overlay_filter(0, 0) == 0);
    settle();
    /* Keyboard Enter (START on the pad path) likewise never pauses the game. */
    CHECK(open_with_f1());
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_ENTER, 1, 0) == 1);
    CHECK(keyboard_frame(CELL_PAD_CTRL_START) == 0);
    tick(0.02);
    rsx_overlay_build_draw_data();
    CHECK(!rsx_overlay_visible());
    CHECK(keyboard_frame(CELL_PAD_CTRL_START) == 0);
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_ENTER, 0, 0) == 0);
    CHECK(keyboard_frame(0) == 0);
    CHECK(keyboard_frame(CELL_PAD_CTRL_START) == CELL_PAD_CTRL_START);
    done();
    return 1;
}

/* Remap capture takes any controller button and nothing leaks to the game. */
static int test_remap_capture_takes_any_button(void)
{
    CHECK(init_closed());
    CHECK(open_with_f1());
    tap_key(RSX_OVERLAY_KEY_RIGHT);
    tap_key(RSX_OVERLAY_KEY_RIGHT);
    tap_key(RSX_OVERLAY_KEY_ENTER);                    /* Controles */
    tap_key(RSX_OVERLAY_KEY_DOWN);
    tap_key(RSX_OVERLAY_KEY_DOWN);
    tap_key(RSX_OVERLAY_KEY_ENTER);                    /* Remapear botões */
    for (int i = 0; i < 14; ++i)
        tap_key(RSX_OVERLAY_KEY_DOWN);                 /* Cruz */
    tap_key(RSX_OVERLAY_KEY_ENTER);                    /* capture */
    CHECK(keyboard_frame(CELL_PAD_CTRL_R3) == 0);      /* even non-menu keys are withheld */
    CHECK(cellpad_overlay_filter(CELL_PAD_CTRL_L3, CELL_PAD_CTRL_L3) == 0);
    tick(0.02);
    rsx_overlay_build_draw_data();
    CHECK(s_last_map[1] == CELL_PAD_CTRL_CROSS);       /* host L3 drives Cruz */
    CHECK(s_last_map[14] == CELL_PAD_CTRL_L3);         /* old owner swapped: bijection kept */
    CHECK(cellpad_overlay_filter(0, 0) == 0);
    CHECK(cellpad_overlay_filter(CELL_PAD_CTRL_L3, CELL_PAD_CTRL_L3) == CELL_PAD_CTRL_L3);
    done();
    return 1;
}
```

`libs/video/tests/test_rsx_overlay_metal.m`: add the vibration stub `void cellPad_set_host_vibration(int enabled) { (void)enabled; }`; in `main` call `rsx_overlay_test_set_clock(1000.0);` first; in `test_real_core_frame` replace the `for (int i = 0; i < 3; ++i) rsx_overlay_build_draw_data();` loop with `rsx_overlay_test_set_clock(1000.1); rsx_overlay_build_draw_data(); rsx_overlay_test_set_clock(1001.1); rsx_overlay_build_draw_data();`, and after the closing F1 add `rsx_overlay_test_set_clock(1002.1); rsx_overlay_build_draw_data();` before the final `encode == 0` check.

`games/gow2/tests/test_gow2_overlay_provider.c`: add `void cellPad_set_host_vibration(int enabled) { (void)enabled; }` next to its mapping stub.

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build build-macos --target test_rsx_overlay test_pad_overlay_route`
Expected: compile errors (`rsx_overlay_test_set_clock`, `rsx_overlay_render_active`, `rsx_overlay_release_guard` undeclared).

- [ ] **Step 3: Write the drawing module**

`libs/video/rsx_overlay_shell_draw.h`:

```cpp
#ifndef PS3RECOMP_RSX_OVERLAY_SHELL_DRAW_H
#define PS3RECOMP_RSX_OVERLAY_SHELL_DRAW_H

/* Draws the console shell with ImDrawList primitives only (no ImGui widgets).
 * Solid shapes sample the atlas white texel (the core points ImGui's shared
 * white UV at it); text and SF Symbols are atlas quads. Event thread only. */

#include "imgui.h"
#include "rsx_overlay.h"
#include "rsx_overlay_assets.h"
#include "rsx_overlay_shell.h"

typedef struct rsx_shell_view {
    const rsx_overlay_snapshot* snapshot;
    int metrics_available;
    int caps_registered;
    rsx_overlay_backend_capabilities caps;
    rsx_overlay_graphics_settings graphics;
    uint8_t analog_deadzone, vibration, game_mute, ui_sounds, ui_haptics;
    uint16_t map[RSX_SHELL_REMAP_COUNT];
    const char* title;
    char clock[8];
    int controller_connected;
    int battery_percent;          /* -1: hidden */
    const char* const* log_lines; /* newest first */
    int log_count;
} rsx_shell_view;

void rsx_shell_draw(ImDrawList* dl, const rsx_shell* shell, const rsx_shell_layout* layout,
                    const rsx_overlay_assets* assets, const rsx_shell_view* view, double now);

#endif
```

`libs/video/rsx_overlay_shell_draw.cpp`:

```cpp
#include "rsx_overlay_shell_draw.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

struct Ctx {
    ImDrawList* dl;
    const rsx_overlay_assets* a;
    float u;  /* drawable pixels per layout point */
    float k;  /* drawable pixels per atlas pixel */
};

const char* const kCardLabels[RSX_SHELL_CARD_COUNT] = {
    "Voltar ao jogo", "Tela", "Controles", "Som", "Desenvolvedor", "Sair"
};
const int kCardIcons[RSX_SHELL_CARD_COUNT] = {
    RSX_OVERLAY_ICON_PLAY, RSX_OVERLAY_ICON_DISPLAY, RSX_OVERLAY_ICON_GAMEPAD,
    RSX_OVERLAY_ICON_SPEAKER, RSX_OVERLAY_ICON_HAMMER, RSX_OVERLAY_ICON_POWER
};
const char* const kRowLabels[RSX_SHELL_ROW_COUNT] = {
    "Tela cheia", "VSync", "Tamanho da janela",
    "Zona morta do anal\xC3\xB3gico", "Vibra\xC3\xA7\xC3\xA3o", "Remapear bot\xC3\xB5" "es", "Restaurar padr\xC3\xA3o",
    "Som do jogo", "Sons da interface", "Vibra\xC3\xA7\xC3\xA3o da interface", "Logs"
};
const char kUnavailable[] = "indispon\xC3\xADvel";

float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
ImU32 rgba(int r, int g, int b, float a) { return IM_COL32(r, g, b, (int)(clamp01(a) * 255.0f + 0.5f)); }
ImU32 white(float a) { return rgba(255, 255, 255, a); }
ImU32 blue(float a) { return rgba(0x2E, 0x7B, 0xF6, a); }

int glyph_for(const rsx_overlay_assets* a, int style, uint32_t cp)
{
    int gi = rsx_overlay_glyph_index(cp);
    if (gi < 0 || !a->glyphs[style][gi].present)
        gi = rsx_overlay_glyph_index('?');
    return gi;
}

float text_w(const Ctx& c, int style, const char* s) { return rsx_overlay_text_width(c.a, style, s, c.k); }

float text(const Ctx& c, int style, float x, float baseline, const char* s, ImU32 col)
{
    const float iw = 1.0f / (float)c.a->width, ih = 1.0f / (float)c.a->height;
    float pen = x;
    if ((col & IM_COL32_A_MASK) == 0)
        return text_w(c, style, s);
    while (*s) {
        const rsx_overlay_glyph& g = c.a->glyphs[style][glyph_for(c.a, style, rsx_overlay_utf8_next(&s))];
        if (g.w != 0 && g.h != 0) {
            const ImVec2 p0(pen + (float)g.left * c.k, baseline - (float)g.top * c.k);
            const ImVec2 p1(p0.x + (float)g.w * c.k, p0.y + (float)g.h * c.k);
            c.dl->PrimReserve(6, 4);
            c.dl->PrimRectUV(p0, p1, ImVec2((float)g.x * iw, (float)g.y * ih),
                             ImVec2((float)(g.x + g.w) * iw, (float)(g.y + g.h) * ih), col);
        }
        pen += g.advance * c.k;
    }
    return pen - x;
}

float baseline_at(const Ctx& c, int style, float center_y)
{
    return center_y + (c.a->ascent[style] - c.a->descent[style]) * 0.5f * c.k;
}

void text_centered(const Ctx& c, int style, float cx, float baseline, const char* s, ImU32 col)
{
    text(c, style, cx - text_w(c, style, s) * 0.5f, baseline, s, col);
}

void text_right(const Ctx& c, int style, float right, float baseline, const char* s, ImU32 col)
{
    text(c, style, right - text_w(c, style, s), baseline, s, col);
}

/* Truncates with an ellipsis to fit max_w. */
const char* fit(const Ctx& c, int style, const char* s, float max_w, char* buf, size_t cap)
{
    if (text_w(c, style, s) <= max_w || cap < 8)
        return s;
    const float ell = text_w(c, style, "\xE2\x80\xA6");
    float w = 0.0f;
    const char* p = s;
    const char* last = s;
    while (*p) {
        const char* next = p;
        const rsx_overlay_glyph& g = c.a->glyphs[style][glyph_for(c.a, style, rsx_overlay_utf8_next(&next))];
        if (w + g.advance * c.k + ell > max_w)
            break;
        w += g.advance * c.k;
        p = next;
        last = p;
    }
    size_t n = (size_t)(last - s);
    if (n + 4 > cap)
        n = cap - 4;
    std::memcpy(buf, s, n);
    std::memcpy(buf + n, "\xE2\x80\xA6", 4); /* includes the NUL */
    return buf;
}

void shadow(const Ctx& c, ImVec2 p0, ImVec2 p1, float rounding, float alpha)
{
    static const float grow[4] = { 2.0f, 4.0f, 7.0f, 11.0f };
    static const float a4[4] = { 0.10f, 0.07f, 0.045f, 0.025f };
    for (int i = 0; i < 4; ++i) {
        const float g = grow[i] * c.u, dy = 4.0f * c.u;
        c.dl->AddRectFilled(ImVec2(p0.x - g, p0.y - g + dy), ImVec2(p1.x + g, p1.y + g + dy),
                            rgba(0, 0, 0, a4[i] * alpha), rounding + g);
    }
}

void glow(const Ctx& c, ImVec2 p0, ImVec2 p1, float rounding, float alpha)
{
    static const float grow[3] = { 3.0f, 6.0f, 9.0f };
    static const float a3[3] = { 0.12f, 0.07f, 0.04f };
    for (int i = 0; i < 3; ++i) {
        const float g = grow[i] * c.u;
        c.dl->AddRectFilled(ImVec2(p0.x - g, p0.y - g), ImVec2(p1.x + g, p1.y + g),
                            white(a3[i] * alpha), rounding + g);
    }
}

void geometric_icon(const Ctx& c, int id, ImVec2 m, float s, ImU32 col)
{
    ImDrawList* dl = c.dl;
    const float h = s * 0.5f, t = std::fmax(1.5f, s * 0.07f);
    switch (id) {
    case RSX_OVERLAY_ICON_PLAY:
        dl->AddTriangleFilled(ImVec2(m.x - h * 0.55f, m.y - h * 0.7f), ImVec2(m.x + h * 0.75f, m.y),
                              ImVec2(m.x - h * 0.55f, m.y + h * 0.7f), col);
        break;
    case RSX_OVERLAY_ICON_DISPLAY:
        dl->AddRect(ImVec2(m.x - h, m.y - h * 0.7f), ImVec2(m.x + h, m.y + h * 0.45f), col, s * 0.08f, 0, t);
        dl->AddLine(ImVec2(m.x - h * 0.4f, m.y + h * 0.75f), ImVec2(m.x + h * 0.4f, m.y + h * 0.75f), col, t);
        break;
    case RSX_OVERLAY_ICON_GAMEPAD:
        dl->AddRect(ImVec2(m.x - h, m.y - h * 0.45f), ImVec2(m.x + h, m.y + h * 0.45f), col, h * 0.45f, 0, t);
        dl->AddLine(ImVec2(m.x - h * 0.62f, m.y), ImVec2(m.x - h * 0.22f, m.y), col, t);
        dl->AddLine(ImVec2(m.x - h * 0.42f, m.y - h * 0.2f), ImVec2(m.x - h * 0.42f, m.y + h * 0.2f), col, t);
        dl->AddCircleFilled(ImVec2(m.x + h * 0.35f, m.y - h * 0.1f), t, col);
        dl->AddCircleFilled(ImVec2(m.x + h * 0.58f, m.y + h * 0.12f), t, col);
        break;
    case RSX_OVERLAY_ICON_SPEAKER:
        dl->AddRectFilled(ImVec2(m.x - h * 0.9f, m.y - h * 0.25f), ImVec2(m.x - h * 0.55f, m.y + h * 0.25f), col);
        dl->AddQuadFilled(ImVec2(m.x - h * 0.55f, m.y - h * 0.25f), ImVec2(m.x - h * 0.05f, m.y - h * 0.7f),
                          ImVec2(m.x - h * 0.05f, m.y + h * 0.7f), ImVec2(m.x - h * 0.55f, m.y + h * 0.25f), col);
        dl->PathArcTo(ImVec2(m.x - h * 0.05f, m.y), h * 0.45f, -0.9f, 0.9f);
        dl->PathStroke(col, 0, t);
        dl->PathArcTo(ImVec2(m.x - h * 0.05f, m.y), h * 0.8f, -0.9f, 0.9f);
        dl->PathStroke(col, 0, t);
        break;
    case RSX_OVERLAY_ICON_HAMMER:
        dl->AddLine(ImVec2(m.x - h * 0.6f, m.y + h * 0.8f), ImVec2(m.x + h * 0.25f, m.y - h * 0.05f), col, t * 1.4f);
        dl->AddQuadFilled(ImVec2(m.x + h * 0.05f, m.y - h * 0.55f), ImVec2(m.x + h * 0.55f, m.y - h * 0.05f),
                          ImVec2(m.x + h * 0.35f, m.y + h * 0.15f), ImVec2(m.x - h * 0.15f, m.y - h * 0.35f), col);
        break;
    case RSX_OVERLAY_ICON_POWER:
        dl->PathArcTo(m, h * 0.75f, -IM_PI * 0.5f + 0.6f, IM_PI * 1.5f - 0.6f);
        dl->PathStroke(col, 0, t);
        dl->AddLine(ImVec2(m.x, m.y - h * 0.95f), ImVec2(m.x, m.y - h * 0.1f), col, t);
        break;
    default:
        break;
    }
}

void icon(const Ctx& c, int id, ImVec2 center, float size_px, ImU32 col)
{
    const rsx_overlay_assets* a = c.a;
    if (a->system_icons && a->icons[id].present) {
        const rsx_overlay_glyph& g = a->icons[id];
        const float s = size_px / (RSX_OVERLAY_ICON_POINT_SIZE * a->scale);
        const float w = (float)g.w * s, h = (float)g.h * s;
        const float iw = 1.0f / (float)a->width, ih = 1.0f / (float)a->height;
        const ImVec2 p0(center.x - w * 0.5f, center.y - h * 0.5f);
        c.dl->PrimReserve(6, 4);
        c.dl->PrimRectUV(p0, ImVec2(p0.x + w, p0.y + h), ImVec2((float)g.x * iw, (float)g.y * ih),
                         ImVec2((float)(g.x + g.w) * iw, (float)(g.y + g.h) * ih), col);
        return;
    }
    geometric_icon(c, id, center, size_px, col);
}

void chevron(const Ctx& c, float right, float cy, ImU32 col)
{
    const float s = 5.0f * c.u;
    c.dl->AddLine(ImVec2(right - s, cy - s), ImVec2(right, cy), col, 1.6f * c.u);
    c.dl->AddLine(ImVec2(right, cy), ImVec2(right - s, cy + s), col, 1.6f * c.u);
}

void checkmark(const Ctx& c, float right, float cy, ImU32 col)
{
    const float s = 6.0f * c.u;
    c.dl->AddLine(ImVec2(right - 2.0f * s, cy), ImVec2(right - 1.2f * s, cy + 0.8f * s), col, 2.0f * c.u);
    c.dl->AddLine(ImVec2(right - 1.2f * s, cy + 0.8f * s), ImVec2(right, cy - s), col, 2.0f * c.u);
}

void toggle(const Ctx& c, float right, float cy, bool on, float fade)
{
    const float w = 44.0f * c.u, h = 26.0f * c.u;
    const ImVec2 p0(right - w, cy - h * 0.5f), p1(right, cy + h * 0.5f);
    c.dl->AddRectFilled(p0, p1, on ? blue(fade) : rgba(120, 120, 128, 0.55f * fade), h * 0.5f);
    const float kx = on ? p1.x - h * 0.5f : p0.x + h * 0.5f;
    c.dl->AddCircleFilled(ImVec2(kx, cy), h * 0.5f - 2.5f * c.u, white(fade), 24);
}

void slider(const Ctx& c, float right, float cy, int value, int max, float fade)
{
    char t[16];
    std::snprintf(t, sizeof t, "%d%%", value);
    const float tw = text_w(c, RSX_OVERLAY_TEXT_ROW, t);
    text(c, RSX_OVERLAY_TEXT_ROW, right - tw, baseline_at(c, RSX_OVERLAY_TEXT_ROW, cy), t, white(0.9f * fade));
    const float track = 120.0f * c.u, x1 = right - tw - 12.0f * c.u, x0 = x1 - track;
    const float f = value >= max ? 1.0f : (float)value / (float)max;
    c.dl->AddRectFilled(ImVec2(x0, cy - 2.0f * c.u), ImVec2(x1, cy + 2.0f * c.u), rgba(120, 120, 128, 0.55f * fade), 2.0f * c.u);
    c.dl->AddRectFilled(ImVec2(x0, cy - 2.0f * c.u), ImVec2(x0 + track * f, cy + 2.0f * c.u), blue(fade), 2.0f * c.u);
    c.dl->AddCircleFilled(ImVec2(x0 + track * f, cy), 7.0f * c.u, white(fade), 20);
}

uint8_t row_support(int row, const rsx_shell_view* v)
{
    if (row != RSX_SHELL_ROW_FULLSCREEN && row != RSX_SHELL_ROW_VSYNC && row != RSX_SHELL_ROW_WINDOW_SIZE)
        return RSX_OVERLAY_SETTING_LIVE;
    if (!v->caps_registered)
        return RSX_OVERLAY_SETTING_UNAVAILABLE;
    return row == RSX_SHELL_ROW_FULLSCREEN ? v->caps.fullscreen
         : row == RSX_SHELL_ROW_VSYNC ? v->caps.vsync : v->caps.window_size;
}

void row_highlight(const Ctx& c, float x, float top, float w, float h, float fade)
{
    c.dl->AddRectFilled(ImVec2(x, top + 2.0f * c.u), ImVec2(x + w, top + h - 2.0f * c.u), white(0.14f * fade), 10.0f * c.u);
}

void draw_rows(const Ctx& c, const rsx_shell* s, const rsx_shell_view* v, float x, float y, float w, float fade)
{
    rsx_shell_row rows[8];
    const int n = rsx_shell_panel_rows(s->card, rows, 8);
    const float row_h = RSX_SHELL_ROW_H_PT * c.u;
    for (int i = 0; i < n; ++i) {
        const float top = y + (float)i * row_h, cy = top + row_h * 0.5f, right = x + w - 12.0f * c.u;
        const uint8_t support = row_support(rows[i], v);
        const bool enabled = support != RSX_OVERLAY_SETTING_UNAVAILABLE;
        if (s->level == RSX_SHELL_LEVEL_PANEL && s->row == i)
            row_highlight(c, x, top, w, row_h, fade);
        const ImU32 label = white((enabled ? 0.95f : 0.4f) * fade);
        if (support == RSX_OVERLAY_SETTING_RESTART_REQUIRED) {
            const float b = cy - 1.0f * c.u;
            text(c, RSX_OVERLAY_TEXT_ROW, x + 12.0f * c.u, b, kRowLabels[rows[i]], label);
            text(c, RSX_OVERLAY_TEXT_CAPTION, x + 12.0f * c.u,
                 b + c.a->ascent[RSX_OVERLAY_TEXT_CAPTION] * c.k + 3.0f * c.u, "requer reiniciar", white(0.55f * fade));
        } else {
            text(c, RSX_OVERLAY_TEXT_ROW, x + 12.0f * c.u, baseline_at(c, RSX_OVERLAY_TEXT_ROW, cy), kRowLabels[rows[i]], label);
        }
        if (!enabled) {
            text_right(c, RSX_OVERLAY_TEXT_CAPTION, right, baseline_at(c, RSX_OVERLAY_TEXT_CAPTION, cy), kUnavailable, white(0.45f * fade));
            continue;
        }
        switch (rows[i]) {
        case RSX_SHELL_ROW_FULLSCREEN: toggle(c, right, cy, v->graphics.fullscreen != 0, fade); break;
        case RSX_SHELL_ROW_VSYNC: toggle(c, right, cy, v->graphics.vsync != 0, fade); break;
        case RSX_SHELL_ROW_WINDOW_SIZE: {
            char t[32];
            std::snprintf(t, sizeof t, "%u\xC3\x97%u", v->graphics.window_width, v->graphics.window_height);
            chevron(c, right, cy, white(0.7f * fade));
            text_right(c, RSX_OVERLAY_TEXT_ROW, right - 14.0f * c.u, baseline_at(c, RSX_OVERLAY_TEXT_ROW, cy), t, white(0.7f * fade));
            break;
        }
        case RSX_SHELL_ROW_DEADZONE: slider(c, right, cy, v->analog_deadzone, RSX_SHELL_DEADZONE_MAX, fade); break;
        case RSX_SHELL_ROW_VIBRATION: toggle(c, right, cy, v->vibration != 0, fade); break;
        case RSX_SHELL_ROW_REMAP: chevron(c, right, cy, white(0.7f * fade)); break;
        case RSX_SHELL_ROW_GAME_SOUND: toggle(c, right, cy, v->game_mute == 0, fade); break;
        case RSX_SHELL_ROW_UI_SOUNDS: toggle(c, right, cy, v->ui_sounds != 0, fade); break;
        case RSX_SHELL_ROW_UI_HAPTICS: toggle(c, right, cy, v->ui_haptics != 0, fade); break;
        case RSX_SHELL_ROW_LOGS: {
            char t[16];
            std::snprintf(t, sizeof t, "%d", v->log_count);
            chevron(c, right, cy, white(0.7f * fade));
            text_right(c, RSX_OVERLAY_TEXT_ROW, right - 14.0f * c.u, baseline_at(c, RSX_OVERLAY_TEXT_ROW, cy), t, white(0.6f * fade));
            break;
        }
        default: break;
        }
    }
}

void draw_developer(const Ctx& c, const rsx_shell* s, const rsx_shell_view* v, float x, float y, float w, float fade)
{
    static const char* const labels[5] = {
        "FPS", "Draws do jogo", "Estado do WAD", "Shaders (cache)", "Compila\xC3\xA7\xC3\xB5" "es"
    };
    const rsx_overlay_snapshot* m = v->snapshot;
    const float row_h = RSX_SHELL_ROW_H_PT * c.u, vx = x + w * 0.42f;
    char values[5][128];
    char buf[160];
    if (v->metrics_available) {
        std::snprintf(values[0], sizeof values[0], "%.1f  (%.2f ms)", m->fps, m->frame_ms);
        std::snprintf(values[1], sizeof values[1], "%llu por quadro", (unsigned long long)m->guest_draws_frame);
    } else {
        std::snprintf(values[0], sizeof values[0], "%s", kUnavailable);
        std::snprintf(values[1], sizeof values[1], "%s", kUnavailable);
    }
    std::snprintf(values[2], sizeof values[2], "%s", m->wad_state_available ? m->wad_state : kUnavailable);
    if (m->shader_stats_available) {
        std::snprintf(values[3], sizeof values[3], "%u acertos \xC2\xB7 %u falhas", m->shader_hits, m->shader_misses);
        std::snprintf(values[4], sizeof values[4], "%u compilados \xC2\xB7 %u falhas", m->shader_compiles, m->shader_failures);
    } else {
        std::snprintf(values[3], sizeof values[3], "%s", kUnavailable);
        std::snprintf(values[4], sizeof values[4], "%s", kUnavailable);
    }
    for (int i = 0; i < 5; ++i) {
        const float cy = y + ((float)i + 0.5f) * row_h;
        text(c, RSX_OVERLAY_TEXT_ROW, x + 12.0f * c.u, baseline_at(c, RSX_OVERLAY_TEXT_ROW, cy), labels[i], white(0.6f * fade));
        text(c, RSX_OVERLAY_TEXT_ROW, vx, baseline_at(c, RSX_OVERLAY_TEXT_ROW, cy),
             fit(c, RSX_OVERLAY_TEXT_ROW, values[i], x + w - 12.0f * c.u - vx, buf, sizeof buf), white(0.95f * fade));
    }
    /* Logs: the only interactive row. */
    draw_rows(c, s, v, x, y + 5.0f * row_h, w, fade);
}

void draw_list(const Ctx& c, const rsx_shell* s, const rsx_shell_layout* L, const rsx_shell_view* v,
               float x, float y, float w, float fade)
{
    const float row_h = RSX_SHELL_ROW_H_PT * c.u, right = x + w - 12.0f * c.u;
    const int count = s->list == RSX_SHELL_LIST_WINDOW_SIZE ? RSX_SHELL_WINDOW_SIZE_COUNT
                    : s->list == RSX_SHELL_LIST_REMAP ? RSX_SHELL_REMAP_COUNT : v->log_count;
    char buf[256];
    if (s->list == RSX_SHELL_LIST_LOGS && count == 0) {
        text(c, RSX_OVERLAY_TEXT_ROW, x + 12.0f * c.u, baseline_at(c, RSX_OVERLAY_TEXT_ROW, y + row_h * 0.5f),
             "Nenhum log recente", white(0.5f * fade));
        return;
    }
    int last = s->list_scroll + L->panel_visible_rows;
    if (last > count) last = count;
    for (int i = s->list_scroll; i < last; ++i) {
        const float top = y + (float)(i - s->list_scroll) * row_h, cy = top + row_h * 0.5f;
        const float b = baseline_at(c, RSX_OVERLAY_TEXT_ROW, cy);
        if (s->list_index == i)
            row_highlight(c, x, top, w, row_h, fade);
        if (s->list == RSX_SHELL_LIST_WINDOW_SIZE) {
            std::snprintf(buf, sizeof buf, "%u\xC3\x97%u", rsx_shell_window_sizes[i][0], rsx_shell_window_sizes[i][1]);
            text(c, RSX_OVERLAY_TEXT_ROW, x + 12.0f * c.u, b, buf, white(0.95f * fade));
            if (v->graphics.window_width == rsx_shell_window_sizes[i][0] &&
                v->graphics.window_height == rsx_shell_window_sizes[i][1])
                checkmark(c, right, cy, blue(fade));
        } else if (s->list == RSX_SHELL_LIST_REMAP) {
            const char* host = "\xE2\x80\x94";
            for (int h = 0; h < RSX_SHELL_REMAP_COUNT; ++h)
                if (v->map[h] == (uint16_t)(1u << i))
                    host = rsx_shell_button_names[h];
            text(c, RSX_OVERLAY_TEXT_ROW, x + 12.0f * c.u, b, rsx_shell_button_names[i], white(0.95f * fade));
            if (s->level == RSX_SHELL_LEVEL_CAPTURE && s->capture_guest == i)
                text_right(c, RSX_OVERLAY_TEXT_ROW, right, b, "Pressione um bot\xC3\xA3o\xE2\x80\xA6", blue(fade));
            else
                text_right(c, RSX_OVERLAY_TEXT_ROW, right, b, host, white(0.7f * fade));
        } else {
            const char* line = v->log_lines[i];
            const ImU32 col = std::strncmp(line, "[ERROR]", 7) == 0 ? rgba(255, 120, 110, fade)
                            : std::strncmp(line, "[WARN]", 6) == 0 ? rgba(255, 200, 90, fade) : white(0.85f * fade);
            text(c, RSX_OVERLAY_TEXT_CAPTION, x + 12.0f * c.u, baseline_at(c, RSX_OVERLAY_TEXT_CAPTION, cy),
                 fit(c, RSX_OVERLAY_TEXT_CAPTION, line, w - 24.0f * c.u, buf, sizeof buf), col);
        }
    }
}

const char* panel_title(const rsx_shell* s)
{
    if (s->level == RSX_SHELL_LEVEL_LIST || s->level == RSX_SHELL_LEVEL_CAPTURE) {
        switch (s->list) {
        case RSX_SHELL_LIST_WINDOW_SIZE: return "Tamanho da janela";
        case RSX_SHELL_LIST_REMAP: return "Remapear bot\xC3\xB5" "es";
        case RSX_SHELL_LIST_LOGS: return "Logs";
        default: break;
        }
    }
    return kCardLabels[s->card];
}

void draw_panel(const Ctx& c, const rsx_shell* s, const rsx_shell_layout* L, const rsx_shell_view* v,
                float open, double now)
{
    const rsx_shell_rect& P = L->panel;
    if (P.w <= 0.0f || P.h <= 0.0f)
        return;
    const float fade = open * rsx_shell_panel_fade(s, now);
    const ImVec2 p0(P.x, P.y), p1(P.x + P.w, P.y + P.h);
    const float rounding = 16.0f * c.u, pad = RSX_SHELL_PANEL_PAD_PT * c.u;
    shadow(c, p0, p1, rounding, open);
    c.dl->AddRectFilled(p0, p1, rgba(22, 22, 26, 0.86f * open), rounding);
    text(c, RSX_OVERLAY_TEXT_TITLE, P.x + pad + 8.0f * c.u,
         P.y + pad + c.a->ascent[RSX_OVERLAY_TEXT_TITLE] * c.k + 8.0f * c.u, panel_title(s), white(fade));
    const float x = P.x + pad, y = P.y + pad + RSX_SHELL_PANEL_HEADER_PT * c.u, w = P.w - 2.0f * pad;
    const float cy = y + RSX_SHELL_ROW_H_PT * c.u * 0.5f;
    if (s->level == RSX_SHELL_LEVEL_LIST || s->level == RSX_SHELL_LEVEL_CAPTURE) {
        draw_list(c, s, L, v, x, y, w, fade);
    } else if (s->card == RSX_SHELL_CARD_RESUME) {
        text(c, RSX_OVERLAY_TEXT_ROW, x + 12.0f * c.u, baseline_at(c, RSX_OVERLAY_TEXT_ROW, cy),
             "Pressione X ou Enter para continuar", white(0.8f * fade));
    } else if (s->card == RSX_SHELL_CARD_QUIT) {
        char t[128];
        std::snprintf(t, sizeof t, "Fecha o %s", v->title);
        text(c, RSX_OVERLAY_TEXT_ROW, x + 12.0f * c.u, baseline_at(c, RSX_OVERLAY_TEXT_ROW, cy), t, white(0.8f * fade));
    } else if (s->card == RSX_SHELL_CARD_DEVELOPER) {
        draw_developer(c, s, v, x, y, w, fade);
    } else {
        draw_rows(c, s, v, x, y, w, fade);
    }
}

void draw_top_strip(const Ctx& c, const rsx_shell_layout* L, const rsx_shell_view* v, float open)
{
    const float cy = L->top_strip.y + L->top_strip.h * 0.5f;
    const ImU32 col = white(0.92f * open);
    float x = L->top_strip.x + L->top_strip.w - 24.0f * c.u;
    text(c, RSX_OVERLAY_TEXT_TITLE, L->top_strip.x + 24.0f * c.u,
         baseline_at(c, RSX_OVERLAY_TEXT_TITLE, cy), v->title, col);
    if (v->controller_connected && v->battery_percent >= 0) {
        char pct[8];
        std::snprintf(pct, sizeof pct, "%d%%", v->battery_percent);
        x -= text_w(c, RSX_OVERLAY_TEXT_CAPTION, pct);
        text(c, RSX_OVERLAY_TEXT_CAPTION, x, baseline_at(c, RSX_OVERLAY_TEXT_CAPTION, cy), pct, col);
        x -= 6.0f * c.u;
        const float bw = 22.0f * c.u, bh = 11.0f * c.u;
        const ImVec2 b0(x - bw, cy - bh * 0.5f), b1(x, cy + bh * 0.5f);
        c.dl->AddRect(b0, b1, col, 2.0f * c.u, 0, 1.2f * c.u);
        c.dl->AddRectFilled(ImVec2(b1.x + 1.0f * c.u, cy - bh * 0.2f), ImVec2(b1.x + 2.5f * c.u, cy + bh * 0.2f), col);
        c.dl->AddRectFilled(ImVec2(b0.x + 2.0f * c.u, b0.y + 2.0f * c.u),
                            ImVec2(b0.x + 2.0f * c.u + (bw - 4.0f * c.u) * (float)v->battery_percent / 100.0f,
                                   b1.y - 2.0f * c.u), col);
        x = b0.x - 12.0f * c.u;
    }
    if (v->controller_connected) {
        icon(c, RSX_OVERLAY_ICON_GAMEPAD, ImVec2(x - 12.0f * c.u, cy), 20.0f * c.u, col);
        x -= 32.0f * c.u;
    }
    text_right(c, RSX_OVERLAY_TEXT_ROW, x, baseline_at(c, RSX_OVERLAY_TEXT_ROW, cy), v->clock, col);
}

void draw_cards(const Ctx& c, const rsx_shell* s, const rsx_shell_layout* L, float open)
{
    for (int i = 0; i < RSX_SHELL_CARD_COUNT; ++i) {
        const rsx_shell_rect& r = L->cards[i];
        const float sc = s->card_scale[i].value;
        const float cx = r.x + r.w * 0.5f, cy = r.y + r.h * 0.5f;
        const ImVec2 p0(cx - r.w * 0.5f * sc, cy - r.h * 0.5f * sc), p1(cx + r.w * 0.5f * sc, cy + r.h * 0.5f * sc);
        const bool focused = i == s->card;
        const float alpha = open * (focused ? 1.0f : 0.75f);
        const float rounding = RSX_SHELL_CARD_RADIUS_PT * c.u * sc;
        shadow(c, p0, p1, rounding, alpha);
        if (focused)
            glow(c, p0, p1, rounding, open);
        c.dl->AddRectFilled(p0, p1, rgba(44, 44, 52, 0.82f * alpha), rounding);
        if (focused)
            c.dl->AddRect(p0, p1, white(open), rounding, 0, 2.0f * c.u);
        icon(c, kCardIcons[i], ImVec2(cx, cy), 34.0f * c.u * sc, white(alpha));
        text_centered(c, RSX_OVERLAY_TEXT_LABEL, cx,
                      p1.y + 8.0f * c.u + c.a->ascent[RSX_OVERLAY_TEXT_LABEL] * c.k, kCardLabels[i], white(alpha));
    }
}

void draw_dialog(const Ctx& c, const rsx_shell* s, const rsx_shell_layout* L, const rsx_shell_view* v, float open)
{
    char q[128];
    c.dl->AddRectFilled(ImVec2(0.0f, 0.0f), ImVec2(L->width, L->height), rgba(0, 0, 0, 0.40f * open));
    const float w = 440.0f * c.u, h = 190.0f * c.u;
    const float x = (L->width - w) * 0.5f, y = (L->height - h) * 0.5f;
    const ImVec2 p0(x, y), p1(x + w, y + h);
    shadow(c, p0, p1, 18.0f * c.u, open);
    c.dl->AddRectFilled(p0, p1, rgba(26, 26, 30, 0.94f * open), 18.0f * c.u);
    std::snprintf(q, sizeof q, "Sair do %s?", v->title);
    text_centered(c, RSX_OVERLAY_TEXT_TITLE, x + w * 0.5f, y + 50.0f * c.u, q, white(open));
    text_centered(c, RSX_OVERLAY_TEXT_CAPTION, x + w * 0.5f, y + 78.0f * c.u,
                  "As configura\xC3\xA7\xC3\xB5" "es j\xC3\xA1 est\xC3\xA3o salvas.", white(0.6f * open));
    static const char* const buttons[2] = { "Sair", "Cancelar" };
    const float bw = 150.0f * c.u, bh = 44.0f * c.u, gap = 20.0f * c.u, by = y + h - 64.0f * c.u;
    const float bx0 = x + (w - 2.0f * bw - gap) * 0.5f;
    for (int i = 0; i < 2; ++i) {
        const ImVec2 b0(bx0 + (float)i * (bw + gap), by), b1(b0.x + bw, by + bh);
        const bool focused = s->dialog_choice == i;
        c.dl->AddRectFilled(b0, b1, focused ? white(open) : white(0.12f * open), bh * 0.5f);
        text_centered(c, RSX_OVERLAY_TEXT_ROW, b0.x + bw * 0.5f, baseline_at(c, RSX_OVERLAY_TEXT_ROW, by + bh * 0.5f),
                      buttons[i], focused ? rgba(16, 16, 20, open) : white(open));
    }
}

} // namespace

void rsx_shell_draw(ImDrawList* dl, const rsx_shell* s, const rsx_shell_layout* L,
                    const rsx_overlay_assets* a, const rsx_shell_view* v, double now)
{
    if (dl == nullptr || s == nullptr || L == nullptr || a == nullptr || a->rgba == nullptr ||
        v == nullptr || s->phase == RSX_SHELL_CLOSED || L->px_per_pt <= 0.0f)
        return;
    const Ctx c = { dl, a, L->px_per_pt, L->px_per_pt / a->scale };
    const float open = clamp01(s->open.value);
    /* 1. Dim: always the first command, so a renderer without blur (D3D12)
     *    still gets the dimmed backdrop. */
    dl->AddRectFilled(ImVec2(0.0f, 0.0f), ImVec2(L->width, L->height), rgba(0, 0, 0, RSX_SHELL_BACKDROP_DIM * open));
    /* 2. Bottom band: transparent -> ~92 % near-black. */
    dl->AddRectFilledMultiColor(ImVec2(L->band.x, L->band.y), ImVec2(L->band.x + L->band.w, L->band.y + L->band.h),
                                rgba(8, 8, 10, 0.0f), rgba(8, 8, 10, 0.0f),
                                rgba(8, 8, 10, 0.92f * open), rgba(8, 8, 10, 0.92f * open));
    draw_top_strip(c, L, v, open);
    draw_cards(c, s, L, open);
    draw_panel(c, s, L, v, open, now);
    if (s->level == RSX_SHELL_LEVEL_DIALOG)
        draw_dialog(c, s, L, v, open);
}
```

- [ ] **Step 4: Wire the core**

`rsx_overlay.h`: add the declarations of **Interfaces** (include `"rsx_overlay_feedback.h"` at the top of the header for the sink type); replace the three removed test hooks with the four new ones; update the `rsx_overlay_persist_settings` comment ("…once no direction is held" instead of "once the widget is released").

`rsx_overlay.cpp`, in order:

1. Includes and state (after the existing includes/statics; `imgui_internal.h` now unconditional):

```cpp
#include <chrono>
#include <ctime>

#include "imgui_internal.h" /* ImDrawListSharedData: the shell's white texel */
#include "rsx_overlay_assets.h"
#include "rsx_overlay_feedback.h"
#include "rsx_overlay_shell.h"
#include "rsx_overlay_shell_draw.h"

extern "C" void ps3_audio_set_runtime_mute(int muted); /* libs/audio/audio_mute.c */

static rsx_shell s_shell;                 /* event thread */
static rsx_overlay_assets s_assets;       /* event thread; pixels mirrored in s_font_rgba */
static float s_assets_scale;              /* event thread */
static std::atomic<rsx_overlay_asset_builder> s_asset_builder(rsx_overlay_assets_build_embedded);
static std::atomic<int> s_assets_dirty(0);
static std::atomic<int> s_render_active(0);
static std::atomic<int> s_quit_requested(0);
static std::atomic<uint16_t> s_release_guard(0);
static std::atomic<uint16_t> s_last_host_buttons(0);
static std::atomic<int> s_capture_all(0);
static uint32_t s_key_held;     /* RSX_SHELL_IN_* held on the keyboard (event thread) */
static uint32_t s_key_latched;  /* pressed since the last build (event thread) */
static uint16_t s_prev_pad;     /* pad buttons the last build saw */
static uint32_t s_frame_keys;   /* test view of the last build */
static uint16_t s_frame_pad;
static std::mutex s_feedback_mutex;
static rsx_overlay_feedback_sink s_feedback_sink;
static void* s_feedback_userdata;
static char s_title[64] = "Jogo";
#ifdef RSX_OVERLAY_TESTING
static double s_test_clock = -1.0;
#endif
```

2. Delete `rsx_overlay_update_imgui_pad`, `rsx_overlay_update_imgui_key`, `rsx_overlay_release_imgui_input`, `rsx_overlay_popup_open`, `rsx_overlay_hide_from_keyboard`, `rsx_overlay_render_controls`, `rsx_overlay_support_suffix`, `rsx_overlay_render_graphics`. Add:

```cpp
static uint32_t overlay_key_to_shell(int key)
{
    switch (key) {
    case RSX_OVERLAY_KEY_UP: return RSX_SHELL_IN_UP;
    case RSX_OVERLAY_KEY_DOWN: return RSX_SHELL_IN_DOWN;
    case RSX_OVERLAY_KEY_LEFT: return RSX_SHELL_IN_LEFT;
    case RSX_OVERLAY_KEY_RIGHT: return RSX_SHELL_IN_RIGHT;
    case RSX_OVERLAY_KEY_ENTER: return RSX_SHELL_IN_CONFIRM;
    case RSX_OVERLAY_KEY_ESCAPE: return RSX_SHELL_IN_BACK;
    default: return 0;
    }
}

/* D-pad navigates, Cross confirms, Circle backs out (physical buttons, before remapping). */
static uint32_t overlay_pad_to_shell(uint16_t pad)
{
    uint32_t in = 0;
    if (pad & CELL_PAD_CTRL_UP) in |= RSX_SHELL_IN_UP;
    if (pad & CELL_PAD_CTRL_DOWN) in |= RSX_SHELL_IN_DOWN;
    if (pad & CELL_PAD_CTRL_LEFT) in |= RSX_SHELL_IN_LEFT;
    if (pad & CELL_PAD_CTRL_RIGHT) in |= RSX_SHELL_IN_RIGHT;
    if (pad & CELL_PAD_CTRL_CROSS) in |= RSX_SHELL_IN_CONFIRM;
    if (pad & CELL_PAD_CTRL_CIRCLE) in |= RSX_SHELL_IN_BACK;
    return in;
}

static double overlay_now(void)
{
#ifdef RSX_OVERLAY_TESTING
    if (s_test_clock >= 0.0)
        return s_test_clock;
#endif
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

/* Any thread, atomics only. */
static void overlay_request_open(void)
{
    s_release_guard.store(0);
    s_open_count.fetch_add(1);
    s_render_active.store(1);
    s_visible.store(1);
}

/* Any thread, atomics only. Buttons held right now (keyboard bits included,
 * as last seen by the pad path) stay withheld from the game until released. */
static void overlay_request_close(void)
{
    if (s_visible.exchange(0) == 0)
        return;
    s_release_guard.store((uint16_t)(s_last_host_buttons.load() & rsx_overlay_navigation_mask()));
    s_ui_pad_buttons.store(0);
    s_capture_all.store(0);
}

/* Event thread: is the shell at its top level (Esc closes)? */
static int overlay_shell_at_top_level(void)
{
    if (s_shell.phase != RSX_SHELL_OPEN && s_shell.phase != RSX_SHELL_OPENING)
        return 1;
    return s_shell.level == RSX_SHELL_LEVEL_CARDS;
}
```

Replace Task 6's `rsx_overlay_toggle_atomic` body with `if (s_visible.load()) overlay_request_close(); else overlay_request_open();`.

3. `rsx_overlay_apply_input_settings` becomes

```cpp
static void rsx_overlay_apply_input_settings(void)
{
    cellPad_set_host_mapping(s_settings.host_button_to_guest_button, s_settings.analog_deadzone);
    cellPad_set_host_vibration(s_settings.vibration);
}
```

4. Assets:

```cpp
/* Event thread. Rebuilds the atlas at `scale` and publishes it as texture 0. */
static void overlay_rebuild_assets(float scale)
{
    rsx_overlay_assets fresh;
    rsx_overlay_asset_builder builder = s_asset_builder.load();
    if (!builder(&fresh, scale) && !rsx_overlay_assets_build_embedded(&fresh, scale)) {
        rsx_overlay_log_write(RSX_OVERLAY_LOG_ERROR, "overlay: glyph atlas unavailable");
        return;
    }
    rsx_overlay_assets_free(&s_assets);
    s_assets = fresh;
    s_assets_scale = scale;
    s_assets_dirty.store(0);
    {
        std::lock_guard<std::mutex> lock(s_font_mutex);
        s_font_rgba.assign(s_assets.rgba, s_assets.rgba + (size_t)s_assets.width * s_assets.height * 4u);
        s_font_width = s_assets.width;
        s_font_height = s_assets.height;
    }
    s_atlas_generation.fetch_add(1);
}

void rsx_overlay_set_asset_builder(rsx_overlay_asset_builder builder)
{
    s_asset_builder.store(builder != nullptr ? builder : rsx_overlay_assets_build_embedded);
    s_assets_dirty.store(1);
}

static ImVec2 overlay_white_uv(void)
{
    if (s_assets.width == 0)
        return ImVec2(0.0f, 0.0f);
    return ImVec2(((float)s_assets.white_x + 2.0f) / (float)s_assets.width,
                  ((float)s_assets.white_y + 2.0f) / (float)s_assets.height);
}
```

In `rsx_overlay_set_display`, after storing the values add:

```cpp
    if (s_initialized.load() && (backing_scale != s_assets_scale || s_assets_dirty.load()))
        overlay_rebuild_assets(backing_scale);
```

5. `rsx_overlay_init`: replace the font-capture block with

```cpp
    ImGui::GetStyle().AntiAliasedLinesUseTex = false; /* shapes use only the white texel */
    {
        /* ImGui still needs its own (unused) atlas built for NewFrame. */
        unsigned char* font_pixels;
        int font_width, font_height;
        ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&font_pixels, &font_width, &font_height);
    }
    rsx_shell_init(&s_shell);
    overlay_rebuild_assets(s_display_scale.load());
```

and next to the other resets add `s_render_active.store(0); s_quit_requested.store(0); s_release_guard.store(0); s_capture_all.store(0); s_key_held = 0; s_key_latched = 0; s_prev_pad = 0;` and `ps3_audio_set_runtime_mute(s_settings.game_mute);` right after `rsx_overlay_apply_input_settings();`.

`rsx_overlay_shutdown`: add `rsx_overlay_assets_free(&s_assets); s_assets_scale = 0.0f; s_render_active.store(0); s_capture_all.store(0);`.

6. `rsx_overlay_handle_key`:

```cpp
int rsx_overlay_handle_key(int key, int down, unsigned modifiers)
{
    const int toggle_key = s_settings.toggle_key == RSX_OVERLAY_TOGGLE_KEY_F2
        ? RSX_OVERLAY_KEY_F2 : RSX_OVERLAY_KEY_F1;
    uint32_t bit;
    (void)modifiers;

    if (!s_initialized.load() || s_disabled.load())
        return 0;
    if (key == toggle_key) {
        const int was_down = s_toggle_down.exchange(down != 0);
        if (down && !was_down && s_settings.overlay_enabled) {
            if (s_visible.load()) overlay_request_close();
            else overlay_request_open();
        }
        return s_settings.overlay_enabled || s_visible.load();
    }
    if (key == RSX_OVERLAY_KEY_ESCAPE && s_escape_captured.load()) {
        if (!down)
            s_escape_captured.store(0);
        return 1;
    }
    if (!s_visible.load()) {
        if (key == RSX_OVERLAY_KEY_ESCAPE && !down)
            s_escape_held.store(0);
        return 0;
    }
    bit = overlay_key_to_shell(key);
    if (bit == 0)
        return 0;
    if (key == RSX_OVERLAY_KEY_ESCAPE) {
        const int was_held = s_escape_held.exchange(down != 0);
        if (down && was_held)
            return 1; /* auto-repeat is never another Back */
        if (down && overlay_shell_at_top_level()) {
            s_escape_held.store(0);
            s_escape_captured.store(1);
            overlay_request_close();
            return 1;
        }
    }
    if (down) {
        if ((s_key_held & bit) == 0)
            s_key_latched |= bit;
        s_key_held |= bit;
    } else {
        s_key_held &= ~bit;
    }
    return 1;
}
```

7. `rsx_overlay_handle_pad`: keep the body but (a) the toggle becomes `if (s_visible.load()) overlay_request_close(); else overlay_request_open();` (drop the manual `s_open_count`/`s_visible` stores), (b) before `consume = rsx_overlay_navigation_button(host_buttons);` insert

```cpp
    if (s_capture_all.load()) {
        /* Remap capture: every button is menu input, none reaches the game. */
        *guest_buttons = 0;
        return 1;
    }
```

8. Add the guard:

```cpp
uint16_t rsx_overlay_release_guard(uint16_t host_buttons)
{
    if (!s_initialized.load())
        return 0;
    s_last_host_buttons.store(host_buttons);
    const uint16_t guard = (uint16_t)(s_release_guard.load() & host_buttons);
    s_release_guard.store(guard);
    return s_visible.load() ? 0 : guard;
}
```

and in `libs/input/cellpad_overlay.h`, `cellpad_overlay_filter` becomes

```c
static inline uint16_t cellpad_overlay_filter(uint16_t host_buttons,
                                              uint16_t controller_buttons)
{
    uint16_t out = host_buttons;
    const uint16_t keyboard_only = (uint16_t)(host_buttons & (uint16_t)~controller_buttons);

    rsx_overlay_handle_pad((uint16_t)(controller_buttons & host_buttons), &out);
    if (rsx_overlay_visible())
        out &= (uint16_t)~(keyboard_only & rsx_overlay_pad_navigation_mask());
    /* Buttons held when the menu closed from inside stay the menu's until released. */
    out &= (uint16_t)~rsx_overlay_release_guard(host_buttons);
    return out;
}
```

9. `rsx_overlay_focus_lost`: replace `rsx_overlay_release_imgui_input();` with `s_key_held = 0; s_key_latched = 0;`. `rsx_overlay_disable`: add `s_render_active.store(0); s_capture_all.store(0);`.

10. `rsx_overlay_persist_settings`: replace the `if (s_visible.load()) { ... IsAnyItemActive ... }` block with

```cpp
    /* A held direction on the slider changes the value ~10x/s: save once it is released. */
    if (s_visible.load() && rsx_shell_direction_held(&s_shell))
        return 0;
```

11. Public helpers:

```cpp
int rsx_overlay_render_active(void) { return s_render_active.load(); }
int rsx_overlay_take_quit_request(void) { return s_quit_requested.exchange(0); }

void rsx_overlay_set_title(const char* title)
{
    if (title != nullptr && title[0] != '\0')
        std::snprintf(s_title, sizeof(s_title), "%s", title);
}

void rsx_overlay_set_feedback_sink(rsx_overlay_feedback_sink sink, void* userdata)
{
    std::lock_guard<std::mutex> lock(s_feedback_mutex);
    s_feedback_sink = sink;
    s_feedback_userdata = userdata;
}
```

12. Shell glue (before `rsx_overlay_build_draw_data`):

```cpp
static void overlay_collect_input(rsx_shell_input* in)
{
    const uint16_t pad = s_ui_pad_buttons.load();
    const uint16_t pad_pressed = (uint16_t)(pad & (uint16_t)~s_prev_pad);
    s_prev_pad = pad;
    std::memset(in, 0, sizeof(*in));
    in->held = s_key_held | overlay_pad_to_shell(pad);
    in->key_pressed = s_key_latched;
    in->pressed = s_key_latched | overlay_pad_to_shell(pad_pressed);
    in->pad_pressed = pad_pressed;
    s_key_latched = 0;
    s_frame_keys = s_key_held;
    s_frame_pad = pad;
}

static void overlay_fill_model(rsx_shell_model* m, uint32_t log_lines)
{
    rsx_overlay_backend_capabilities caps;
    rsx_overlay_graphics_settings g;
    std::memset(m, 0, sizeof(*m));
    if (rsx_overlay_get_backend_capabilities(&caps)) {
        m->support_fullscreen = caps.fullscreen;
        m->support_vsync = caps.vsync;
        m->support_window_size = caps.window_size;
    }
    m->window_size_index = 255;
    if (rsx_overlay_get_graphics_settings(&g))
        for (int i = 0; i < RSX_SHELL_WINDOW_SIZE_COUNT; ++i)
            if (rsx_shell_window_sizes[i][0] == g.window_width && rsx_shell_window_sizes[i][1] == g.window_height)
                m->window_size_index = (uint8_t)i;
    m->log_lines = log_lines;
    m->list_visible_rows = 1;
}

/* Newest first; `buf` is split in place. */
static int overlay_log_lines(char* buf, size_t cap, const char** lines, int max)
{
    const char* tmp[RSX_OVERLAY_LOG_ENTRY_COUNT];
    int n = 0, count = 0;
    char* p = buf;
    (void)rsx_overlay_log_snapshot(buf, cap);
    while (*p != '\0' && n < (int)RSX_OVERLAY_LOG_ENTRY_COUNT) {
        char* nl = std::strchr(p, '\n');
        if (nl != nullptr) *nl = '\0';
        if (*p != '\0') tmp[n++] = p;
        if (nl == nullptr) break;
        p = nl + 1;
    }
    for (int i = n - 1; i >= 0 && count < max; --i)
        lines[count++] = tmp[i];
    return count;
}

static void overlay_apply_actions(const rsx_shell_output* out)
{
    for (int i = 0; i < out->count; ++i) {
        const rsx_shell_action* a = &out->actions[i];
        rsx_overlay_graphics_settings g;
        switch (a->kind) {
        case RSX_SHELL_ACT_CLOSE:
            overlay_request_close();
            break;
        case RSX_SHELL_ACT_QUIT:
            s_quit_requested.store(1);
            overlay_request_close();
            break;
        case RSX_SHELL_ACT_TOGGLE_FULLSCREEN:
            if (rsx_overlay_get_graphics_settings(&g)) { g.fullscreen = g.fullscreen ? 0 : 1; (void)rsx_overlay_apply_graphics(&g); }
            break;
        case RSX_SHELL_ACT_TOGGLE_VSYNC:
            if (rsx_overlay_get_graphics_settings(&g)) { g.vsync = g.vsync ? 0 : 1; (void)rsx_overlay_apply_graphics(&g); }
            break;
        case RSX_SHELL_ACT_SET_WINDOW_SIZE:
            if (a->a >= 0 && a->a < RSX_SHELL_WINDOW_SIZE_COUNT && rsx_overlay_get_graphics_settings(&g)) {
                g.window_width = rsx_shell_window_sizes[a->a][0];
                g.window_height = rsx_shell_window_sizes[a->a][1];
                (void)rsx_overlay_apply_graphics(&g);
            }
            break;
        case RSX_SHELL_ACT_DEADZONE_STEP: {
            const int dz = s_settings.analog_deadzone;
            /* The slider spans 0..30 %; an older, larger value only moves down. */
            const int next = a->a < 0 ? (dz > 0 ? dz - 1 : 0) : (dz < RSX_SHELL_DEADZONE_MAX ? dz + 1 : dz);
            if (next != dz) {
                s_settings.analog_deadzone = (uint8_t)next;
                rsx_overlay_apply_input_settings();
                s_settings_dirty = 1;
            }
            break;
        }
        case RSX_SHELL_ACT_TOGGLE_VIBRATION:
            s_settings.vibration = s_settings.vibration ? 0 : 1;
            rsx_overlay_apply_input_settings();
            s_settings_dirty = 1;
            break;
        case RSX_SHELL_ACT_REMAP:
            rsx_shell_remap_assign(s_settings.host_button_to_guest_button, (unsigned)a->a, (unsigned)a->b);
            rsx_overlay_apply_input_settings();
            s_settings_dirty = 1;
            break;
        case RSX_SHELL_ACT_RESTORE_CONTROLS: {
            rsx_overlay_settings d;
            rsx_overlay_settings_defaults(&d);
            std::memcpy(s_settings.host_button_to_guest_button, d.host_button_to_guest_button,
                        sizeof(d.host_button_to_guest_button));
            s_settings.analog_deadzone = d.analog_deadzone;
            s_settings.vibration = d.vibration;
            rsx_overlay_apply_input_settings();
            s_settings_dirty = 1;
            break;
        }
        case RSX_SHELL_ACT_TOGGLE_GAME_SOUND:
            s_settings.game_mute = s_settings.game_mute ? 0 : 1;
            ps3_audio_set_runtime_mute(s_settings.game_mute);
            s_settings_dirty = 1;
            break;
        case RSX_SHELL_ACT_TOGGLE_UI_SOUNDS:
            s_settings.ui_sounds = s_settings.ui_sounds ? 0 : 1;
            s_settings_dirty = 1;
            break;
        case RSX_SHELL_ACT_TOGGLE_UI_HAPTICS:
            s_settings.ui_haptics = s_settings.ui_haptics ? 0 : 1;
            s_settings_dirty = 1;
            break;
        default:
            break;
        }
    }
}

static void overlay_emit_feedback(uint32_t mask)
{
    rsx_overlay_feedback_sink sink;
    void* userdata;
    if (mask == 0 || (!s_settings.ui_sounds && !s_settings.ui_haptics))
        return;
    {
        std::lock_guard<std::mutex> lock(s_feedback_mutex);
        sink = s_feedback_sink;
        userdata = s_feedback_userdata;
    }
    if (sink == nullptr)
        return;
    for (int e = 0; e < RSX_OVERLAY_FEEDBACK_COUNT; ++e)
        if (mask & RSX_OVERLAY_FEEDBACK_BIT(e))
            sink(e, s_settings.ui_sounds, s_settings.ui_haptics, userdata);
}

static void overlay_fill_view(rsx_shell_view* v, const rsx_overlay_snapshot* snapshot,
                              const char* const* lines, int count)
{
    std::time_t now = std::time(nullptr);
    std::tm local;
    std::memset(v, 0, sizeof(*v));
    v->snapshot = snapshot;
    v->metrics_available = s_backend_published.load(std::memory_order_relaxed);
    v->caps_registered = rsx_overlay_get_backend_capabilities(&v->caps);
    (void)rsx_overlay_get_graphics_settings(&v->graphics);
    v->analog_deadzone = s_settings.analog_deadzone;
    v->vibration = s_settings.vibration;
    v->game_mute = s_settings.game_mute;
    v->ui_sounds = s_settings.ui_sounds;
    v->ui_haptics = s_settings.ui_haptics;
    std::memcpy(v->map, s_settings.host_button_to_guest_button, sizeof(v->map));
    v->title = s_title;
#if defined(_WIN32)
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    std::snprintf(v->clock, sizeof(v->clock), "%02d:%02d", local.tm_hour, local.tm_min);
    rsx_overlay_get_controller_status(&v->controller_connected, &v->battery_percent);
    v->log_lines = lines;
    v->log_count = count;
}
```

13. `rsx_overlay_copy_imgui_draw_data`: set `packet.backdrop_dim = RSX_SHELL_BACKDROP_DIM; packet.backdrop_blur = 1u;` (replacing Task 1's zeros).

14. Replace `rsx_overlay_build_draw_data`:

```cpp
void rsx_overlay_build_draw_data(void)
{
    static char logs[RSX_OVERLAY_LOG_SNAPSHOT_CAPACITY]; /* event thread only */
    static const char* log_lines[RSX_OVERLAY_LOG_ENTRY_COUNT];
    rsx_shell_input input;
    rsx_shell_model model;
    rsx_shell_output output;
    rsx_shell_layout layout;
    uint32_t width, height;
    float scale;
    int log_count = 0;

    if (!s_initialized.load() || s_imgui_context == nullptr)
        return;
    ImGui::SetCurrentContext(s_imgui_context);
    const double now = overlay_now();
    rsx_overlay_get_display(&width, &height, &scale);
    {
        const uint64_t opens = s_open_count.load();
        if (opens != s_frame_open_count) {
            /* Keys released while closed never reached us; a pad button held
             * at open is not a press. */
            s_frame_open_count = opens;
            s_key_held = 0;
            s_key_latched = 0;
            s_prev_pad = s_ui_pad_buttons.load();
            rsx_shell_open(&s_shell, now);
        }
    }
    if (!s_visible.load())
        rsx_shell_close(&s_shell, now);
    overlay_collect_input(&input);
    if (s_shell.phase != RSX_SHELL_CLOSED && s_shell.card == RSX_SHELL_CARD_DEVELOPER)
        log_count = overlay_log_lines(logs, sizeof(logs), log_lines, (int)RSX_OVERLAY_LOG_ENTRY_COUNT);
    overlay_fill_model(&model, (uint32_t)log_count);
    rsx_shell_compute_layout((float)width, (float)height, scale, s_shell.card,
                             rsx_shell_panel_display_rows(&s_shell, &model),
                             rsx_shell_panel_wide(&s_shell), s_shell.open.value, &layout);
    model.list_visible_rows = (uint32_t)layout.panel_visible_rows;
    rsx_shell_update(&s_shell, &input, &model, now, &output);
    overlay_apply_actions(&output);
    overlay_emit_feedback(output.feedback);
    s_capture_all.store(s_visible.load() && s_shell.level == RSX_SHELL_LEVEL_CAPTURE);
    if (s_shell.phase == RSX_SHELL_CLOSED && !s_visible.load())
        s_render_active.store(0);

    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)width, (float)height);
    io.DeltaTime = 1.0f / 60.0f;
    ImGui::NewFrame();
    ImGui::GetDrawListSharedData()->TexUvWhitePixel = overlay_white_uv();
    if (s_shell.phase != RSX_SHELL_CLOSED) {
        rsx_overlay_snapshot snapshot;
        rsx_shell_view view;
        rsx_overlay_snapshot_current(&snapshot);
        overlay_fill_view(&view, &snapshot, log_lines, log_count);
        rsx_shell_compute_layout((float)width, (float)height, scale, s_shell.card,
                                 rsx_shell_panel_display_rows(&s_shell, &model),
                                 rsx_shell_panel_wide(&s_shell), s_shell.open.value, &layout);
        rsx_shell_draw(ImGui::GetForegroundDrawList(), &s_shell, &layout, &s_assets, &view, now);
    }
    ImGui::Render();
    rsx_overlay_copy_imgui_draw_data(ImGui::GetDrawData(), width, height, scale,
        s_shell.phase == RSX_SHELL_CLOSED ? 0.0f : std::fmin(1.0f, std::fmax(0.0f, s_shell.open.value)));
    (void)rsx_overlay_persist_settings();
}
```

(add `#include <cmath>`).

15. Test hooks (replace the old `#ifdef RSX_OVERLAY_TESTING` block, keeping `rsx_overlay_test_backend_metrics_available` and `rsx_overlay_test_apply_graphics`):

```cpp
void rsx_overlay_test_set_clock(double seconds) { s_test_clock = seconds; }

int rsx_overlay_test_input_down(int code)
{
    const uint32_t bit = overlay_key_to_shell(code);
    if (bit != 0)
        return (s_frame_keys & bit) != 0;
    return (s_frame_pad & (uint16_t)code) != 0;
}

void rsx_overlay_test_shell_state(int* phase, int* level, int* card, int* row, int* list_index)
{
    if (phase) *phase = s_shell.phase;
    if (level) *level = s_shell.level;
    if (card) *card = s_shell.card;
    if (row) *row = s_shell.row;
    if (list_index) *list_index = s_shell.list_index;
}

float rsx_overlay_test_atlas_scale(void) { return s_assets_scale; }
```

- [ ] **Step 5: Renderers, backends, host**

`rsx_overlay_metal.m`: `overlay_core_visible` returns `rsx_overlay_render_active()`. `rsx_overlay_d3d12.c:433`: replace `!rsx_overlay_visible()` with `!rsx_overlay_render_active()`.

`rsx_metal_backend.m`: add includes `"rsx_overlay_assets.h"`, `"rsx_overlay_feedback_apple.h"`, `"cellpad_gamecontroller.h"`. In init, inside the `else` of `if (!s_overlay_ui)` (create a braces block if needed), after success:

```objc
        /* Console shell on macOS: SF Pro / SF Symbols atlas, UI sounds on a
         * dedicated AVAudioEngine, haptic ticks on the pad's async queue. */
        rsx_overlay_set_asset_builder(rsx_overlay_assets_build_apple);
        rsx_overlay_feedback_apple_set_haptic(cpgc_ui_tick);
        rsx_overlay_set_feedback_sink(rsx_overlay_feedback_apple_play, NULL);
```

In `rsx_metal_backend_shutdown` before `rsx_overlay_metal_renderer_destroy(s_overlay_ui);`:

```objc
    rsx_overlay_set_feedback_sink(NULL, NULL);
    rsx_overlay_feedback_apple_set_haptic(NULL);
    rsx_overlay_feedback_apple_shutdown();
    rsx_overlay_set_asset_builder(NULL);
```

Pump tail becomes

```objc
    if (rsx_overlay_render_active())
        rsx_overlay_build_draw_data();
    else
        (void)rsx_overlay_persist_settings();
    metal_overlay_apply_graphics();
    if (rsx_overlay_take_quit_request()) {
        (void)rsx_overlay_persist_settings();
        metal_quit_now("overlay: Sair");
    }
```

Update the comment above it: "Nothing is built while the shell is closed (render inactive)".

`rsx_d3d12_backend.c` pump tail:

```c
    if (rsx_overlay_render_active())
        rsx_overlay_build_draw_data();
    else
        (void)rsx_overlay_persist_settings();
    if (rsx_overlay_take_quit_request()) {
        (void)rsx_overlay_persist_settings();
        s_d3d.window_closed = 1;
    }
```

`games/gow2/boot_macos.cpp` right after the `rsx_overlay_init(...)` failure check: `rsx_overlay_set_title("God of War II HD");`. (The same one-line change belongs in `../gow2-recomp/boot_macos.cpp` via the usual `git subtree push`; do not edit the sibling checkout from this task.)

`CMakeLists.txt`:
- after the `rsx_overlay_shell.cpp` append: `list(APPEND HLE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/libs/video/rsx_overlay_shell_draw.cpp")`;
- before the first overlay test target define

```cmake
    set(PS3RECOMP_OVERLAY_CORE_TEST_SOURCES
        libs/video/rsx_overlay.cpp
        libs/video/rsx_overlay_log.c
        libs/video/rsx_overlay_settings.c
        libs/video/rsx_overlay_shell.cpp
        libs/video/rsx_overlay_shell_draw.cpp
        libs/video/rsx_overlay_assets.c
        libs/audio/audio_mute.c)
```

- in `test_rsx_overlay`, `test_gow2_overlay_provider`, `test_rsx_overlay_metal`, `test_pad_overlay_route`: replace their `rsx_overlay.cpp`/`rsx_overlay_log.c`/`rsx_overlay_settings.c` source lines with `${PS3RECOMP_OVERLAY_CORE_TEST_SOURCES}`, add `ps3recomp_overlay_fonts` to `target_link_libraries`, and add `target_compile_definitions(test_rsx_overlay_metal PRIVATE RSX_OVERLAY_TESTING=1)`.

- [ ] **Step 6: Run to verify pass**

Run: `cmake --build build-macos && ctest --test-dir build-macos -R 'overlay|pad_|audio' --output-on-failure`
Expected: every listed test PASS, including `test_closing_from_menu_withholds_held_buttons`, `test_remap_capture_takes_any_button`, `test_closed_shell_costs_nothing`, `test_draw_budget_worst_case`, `test_scale_change_rebuilds_atlas`, `test_runtime_overlay_link`.
Then:

```bash
for f in libs/video/rsx_overlay.cpp libs/video/rsx_overlay_shell_draw.cpp; do x86_64-w64-mingw32-g++ -fsyntax-only -std=c++20 $MW $f || echo FAIL $f; done
for f in libs/video/rsx_overlay_d3d12.c libs/video/rsx_d3d12_backend.c libs/input/cellPad.c; do x86_64-w64-mingw32-gcc -fsyntax-only -std=c11 $MW $f || echo FAIL $f; done
grep -n "IsAnyItemActive\|BeginTabBar\|RSX_OVERLAY_CANVAS" libs/video/*.cpp libs/video/*.h libs/video/*.m
```

Expected: no output from any of them.

- [ ] **Step 7: Commit**

```bash
git add libs/video/rsx_overlay_shell_draw.h libs/video/rsx_overlay_shell_draw.cpp libs/video/rsx_overlay.h \
  libs/video/rsx_overlay.cpp libs/input/cellpad_overlay.h libs/video/rsx_overlay_metal.m \
  libs/video/rsx_overlay_d3d12.c libs/video/rsx_metal_backend.m libs/video/rsx_d3d12_backend.c \
  games/gow2/boot_macos.cpp CMakeLists.txt libs/video/tests/test_rsx_overlay.c \
  libs/input/tests/test_pad_overlay_route.c libs/video/tests/test_rsx_overlay_metal.m \
  games/gow2/tests/test_gow2_overlay_provider.c
git commit -m "feat(overlay): central estilo console substitui as abas do ImGui"
```

---
### Task 8: Metal backdrop blur (MPS, half resolution) + headless 2x shell test

**Files:**
- Modify: `libs/video/rsx_overlay_metal.h` (test hook), `libs/video/rsx_overlay_metal.m` (shader source `:37-48`, ivars `:69-84`, create `:202-235`, encode `:247-354`)
- Modify: `CMakeLists.txt` (runtime frameworks, `test_rsx_overlay_metal` sources/frameworks/env, `test_runtime_overlay_link`), `games/gow2/build_macos.sh:573`
- Test: `libs/video/tests/test_rsx_overlay_metal.m`

**Interfaces:**
- Consumes: packet v2 `backdrop_blur`, `open_progress` (Task 1/7); `rsx_overlay_set_asset_builder`, `rsx_overlay_assets_build_apple`, `rsx_shell_compute_layout`, `rsx_overlay_test_set_clock` (Tasks 3/4/7).
- Produces: `#define RSX_OVERLAY_METAL_BLUR_SIGMA 6.0f` (half-res sigma ≈ 24 px radius at full res) and, under `RSX_OVERLAY_TESTING`, `int rsx_overlay_metal_renderer_test_blur_state(rsx_overlay_metal_renderer* r); /* bit0: blur textures allocated, bit1: blur failed (dim only) */` plus `void rsx_overlay_metal_renderer_test_fail_blur(rsx_overlay_metal_renderer* r, int stage); /* 1: MPS unsupported, 2: scratch allocation fails */`.

Rules this task must keep (from `rsx_metal_backend.m:9551-9559` and the decode-thread present at `:9010-9021`): the blur reads the drawable of the **same** present call, encodes into the **same** command buffer before the UI pass, never calls `nextDrawable`, and allocates nothing while `open_progress == 0` (closed shell = zero cost).

**Why the blur copies the drawable first (verified).** The backend sets `s_state.layer.framebufferOnly = NO` (`rsx_metal_backend.m:11150`, comment "keep framebufferOnly=NO so present path stays flexible (and frame dump works)"); the present path already blits *from* `drawable.texture` for frame dumps (`metal_dump_frame`, gameplay shots at `:9580-9590`). Apple documents that with `framebufferOnly = NO` the drawable textures may be used for more than rendering, but the exact `usage` bits a `CAMetalLayer` hands out are not a contract we control. So the blur never samples the drawable directly: it **blits** the drawable into an owned full-resolution texture created with `ShaderRead`, then runs MPS from that copy. A blit only needs `!texture.framebufferOnly`, which the backend guarantees and the renderer checks. The fallback is explicit and tested: `framebufferOnly` drawable, MPS unsupported, or scratch allocation failure → one log line, dim-only frame (the core's dim command still draws), never an invisible UI.

- [ ] **Step 1: Write the failing tests**

Add to `test_rsx_overlay_metal.m` (the target defines `RSX_OVERLAY_TESTING` since Task 7):

```objc
#include "rsx_overlay_assets.h"
#include "rsx_overlay_shell.h"

/* Readable target of any size, filled with a black/white checkerboard. */
static id<MTLTexture> make_checker(uint32_t w, uint32_t h, uint32_t cell, MTLTextureUsage usage)
{
    MTLTextureDescriptor* td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                                  width:w height:h mipmapped:NO];
    td.usage = usage;
    td.storageMode = MTLStorageModePrivate;
    id<MTLTexture> t = [s_device newTextureWithDescriptor:td];
    id<MTLBuffer> src = [s_device newBufferWithLength:(NSUInteger)w * h * 4 options:MTLResourceStorageModeShared];
    uint8_t* p = (uint8_t*)src.contents;
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x) {
            const uint8_t v = (((x / cell) + (y / cell)) & 1u) ? 255 : 0;
            uint8_t* q = p + ((size_t)y * w + x) * 4u;
            q[0] = q[1] = q[2] = v;
            q[3] = 255;
        }
    id<MTLCommandBuffer> cb = [s_queue commandBuffer];
    id<MTLBlitCommandEncoder> b = [cb blitCommandEncoder];
    [b copyFromBuffer:src sourceOffset:0 sourceBytesPerRow:(NSUInteger)w * 4 sourceBytesPerImage:(NSUInteger)w * h * 4
           sourceSize:MTLSizeMake(w, h, 1) toTexture:t destinationSlice:0 destinationLevel:0
    destinationOrigin:MTLOriginMake(0, 0, 0)];
    [b endEncoding];
    [cb commit];
    [cb waitUntilCompleted];
    return t;
}

static id<MTLBuffer> readback_any(id<MTLCommandBuffer> cb, id<MTLTexture> t)
{
    const NSUInteger w = t.width, h = t.height;
    id<MTLBuffer> out = [s_device newBufferWithLength:w * h * 4 options:MTLResourceStorageModeShared];
    id<MTLBlitCommandEncoder> b = [cb blitCommandEncoder];
    [b copyFromTexture:t sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0)
            sourceSize:MTLSizeMake(w, h, 1) toBuffer:out destinationOffset:0
destinationBytesPerRow:w * 4 destinationBytesPerImage:w * h * 4];
    [b endEncoding];
    return out;
}

/* Max-min of the green channel and its mean over a square region. */
static void region_stats(id<MTLBuffer> buf, uint32_t stride_px, uint32_t x0, uint32_t y0, uint32_t size,
                         int* range, double* mean)
{
    int lo = 255, hi = 0;
    double sum = 0.0;
    for (uint32_t y = y0; y < y0 + size; ++y)
        for (uint32_t x = x0; x < x0 + size; ++x) {
            const int g = ((const uint8_t*)buf.contents)[((size_t)y * stride_px + x) * 4u + 1u];
            lo = g < lo ? g : lo;
            hi = g > hi ? g : hi;
            sum += g;
        }
    *range = hi - lo;
    *mean = sum / (double)(size * size);
}

/* Like the core's packet: first command is the full-screen dim (black at
 * 0.35 * open, sampling the opaque white texel), then one invisible quad. */
static rsx_overlay_draw_packet* blur_packet(uint32_t w, uint32_t h, float open)
{
    const float clip[4] = { 0.0f, 0.0f, (float)w, (float)h };
    const uint32_t dim_alpha = (uint32_t)(RSX_SHELL_BACKDROP_DIM * open * 255.0f + 0.5f);
    rsx_overlay_draw_packet* p = calloc(1, sizeof(*p));
    p->generation = 1;
    p->display_width = w;
    p->display_height = h;
    p->backing_scale = 2.0f;
    p->backdrop_blur = 1u;
    p->backdrop_dim = RSX_SHELL_BACKDROP_DIM;
    p->open_progress = open;
    add_quad(p, 0, 0, (float)w, (float)h, 0.25f, 0.25f, dim_alpha << 24, clip, 0);
    add_quad(p, 0, 0, 1, 1, 0.75f, 0.25f, 0xFFFFFFFFu, clip, 0); /* transparent texel */
    return p;
}

/* Checkerboard through the renderer; returns the green range and mean of a
 * centre region and the renderer's blur state. */
static int render_checker(float open, int fail_stage, MTLTextureUsage usage,
                          int* range, double* mean, int* blur_state)
{
    test_source src = { 1, blur_packet(256, 192, open), 0 };
    rsx_overlay_metal_source source = { src_visible, src_copy_packet, src_copy_font, &src };
    rsx_overlay_metal_renderer* r = rsx_overlay_metal_renderer_create(s_device, MTLPixelFormatBGRA8Unorm, &source);
    CHECK(r != NULL);
    if (fail_stage)
        rsx_overlay_metal_renderer_test_fail_blur(r, fail_stage);
    id<MTLTexture> target = make_checker(256, 192, 8, usage);
    const rsx_overlay_viewport full = { 0, 0, 256, 192 };
    id<MTLCommandBuffer> cb = [s_queue commandBuffer];
    CHECK(rsx_overlay_metal_renderer_encode(r, cb, target, full) == 1);
    id<MTLBuffer> rb = readback_any(cb, target);
    [cb commit];
    [cb waitUntilCompleted];
    CHECK(cb.status == MTLCommandBufferStatusCompleted);
    region_stats(rb, 256, 112, 80, 32, range, mean);
    *blur_state = rsx_overlay_metal_renderer_test_blur_state(r);
    rsx_overlay_metal_renderer_destroy(r);
    free(src.packet);
    return 1;
}

/* Expected values: checker 0/255; dim at full open leaves 65 % (255 -> 166);
 * a blurred 8 px checker at sigma 6 (half res) is ~127 everywhere. */
static int test_blur_and_dim_pixels(void)
{
    const MTLTextureUsage rw = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    int range, state;
    double mean;

    /* Closed: nothing blurred, nothing dimmed, nothing allocated. */
    CHECK(render_checker(0.0f, 0, rw, &range, &mean, &state));
    CHECK(range >= 250 && (state & 1) == 0);
    /* Fully open: blurred and dimmed. */
    CHECK(render_checker(1.0f, 0, rw, &range, &mean, &state));
    CHECK(range < 40 && mean > 68.0 && mean < 98.0 && state == 1);
    /* Half open: backdrop at 50 %, dim at 17.5 % -> white ~158, black ~52. */
    CHECK(render_checker(0.5f, 0, rw, &range, &mean, &state));
    CHECK(range > 80 && range < 140);
    return 1;
}

/* The drawable is never sampled directly: a render-target-only texture (no
 * ShaderRead, but not framebufferOnly -- what CAMetalLayer hands out with
 * framebufferOnly = NO) still blurs, through the owned copy. */
static int test_render_target_only_drawable_still_blurs(void)
{
    int range, state;
    double mean;
    CHECK(render_checker(1.0f, 0, MTLTextureUsageRenderTarget, &range, &mean, &state));
    CHECK(range < 40 && state == 1);
    return 1;
}

/* Injected MPS / allocation failure: dim only, pixels exact, flag set. */
static int test_blur_failure_falls_back_to_dim(void)
{
    const MTLTextureUsage rw = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    for (int stage = 1; stage <= 2; ++stage) {
        int range, state;
        double mean;
        CHECK(render_checker(1.0f, stage, rw, &range, &mean, &state));
        CHECK(range >= 162 && range <= 170);   /* 255 * 0.65 against 0: sharp, dimmed */
        CHECK(mean > 78.0 && mean < 88.0);     /* half white, half black, dimmed */
        CHECK((state & 2) != 0 && (state & 1) == 0);
    }
    return 1;
}

/* A real CAMetalLayer drawable configured like the backend
 * (rsx_metal_backend.m: framebufferOnly = NO). Skips, without failing, when
 * the headless session cannot vend a drawable. */
static int test_cametallayer_drawable_blurs(void)
{
    CAMetalLayer* layer = [CAMetalLayer layer];
    layer.device = s_device;
    layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    layer.framebufferOnly = NO;
    layer.drawableSize = CGSizeMake(256, 192);
    id<CAMetalDrawable> drawable = [layer nextDrawable];
    if (!drawable) {
        printf("test_cametallayer_drawable_blurs: SKIP (no drawable headless)\n");
        return 1;
    }
    CHECK(!drawable.texture.framebufferOnly);
    id<MTLTexture> checker = make_checker(256, 192, 8, MTLTextureUsageShaderRead);
    id<MTLCommandBuffer> fill = [s_queue commandBuffer];
    id<MTLBlitCommandEncoder> b = [fill blitCommandEncoder];
    [b copyFromTexture:checker toTexture:drawable.texture];
    [b endEncoding];
    [fill commit];
    [fill waitUntilCompleted];
    test_source src = { 1, blur_packet(256, 192, 1.0f), 0 };
    rsx_overlay_metal_source source = { src_visible, src_copy_packet, src_copy_font, &src };
    rsx_overlay_metal_renderer* r = rsx_overlay_metal_renderer_create(s_device, MTLPixelFormatBGRA8Unorm, &source);
    const rsx_overlay_viewport full = { 0, 0, 256, 192 };
    id<MTLCommandBuffer> cb = [s_queue commandBuffer];
    CHECK(rsx_overlay_metal_renderer_encode(r, cb, drawable.texture, full) == 1);
    id<MTLBuffer> rb = readback_any(cb, drawable.texture);
    [cb commit];
    [cb waitUntilCompleted];
    int range;
    double mean;
    region_stats(rb, 256, 112, 80, 32, &range, &mean);
    CHECK(range < 40 && rsx_overlay_metal_renderer_test_blur_state(r) == 1);
    rsx_overlay_metal_renderer_destroy(r);
    free(src.packet);
    return 1;
}

/* The real shell at 2x on a checkerboard "game": blur + dim behind, rounded
 * card corners, label text present. */
static int test_real_shell_frame_at_2x(void)
{
    char path[] = "/tmp/rsx-overlay-shell2x-XXXXXX";
    int fd = mkstemp(path);
    rsx_overlay_settings settings;
    const uint32_t W = 1280, H = 720;
    CHECK(fd >= 0);
    close(fd);
    rsx_overlay_settings_defaults(&settings);
    CHECK(rsx_overlay_settings_save(&settings, path));
    CHECK(rsx_overlay_init(path));
    rsx_overlay_set_asset_builder(rsx_overlay_assets_build_apple);
    rsx_overlay_set_display(W, H, 2.0f);
    rsx_overlay_metal_renderer* r = rsx_overlay_metal_renderer_create(s_device, MTLPixelFormatBGRA8Unorm, NULL);
    CHECK(r != NULL);
    rsx_overlay_test_set_clock(2000.0);
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_F1, 1, 0));
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_F1, 0, 0));
    rsx_overlay_build_draw_data();
    rsx_overlay_test_set_clock(2001.0);
    rsx_overlay_build_draw_data();

    id<MTLTexture> target = make_checker(W, H, 16, MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead);
    const rsx_overlay_viewport full = { 0, 0, W, H };
    id<MTLCommandBuffer> cb = [s_queue commandBuffer];
    CHECK(rsx_overlay_metal_renderer_encode(r, cb, target, full) == 1);
    id<MTLBuffer> rb = readback_any(cb, target);
    [cb commit];
    [cb waitUntilCompleted];
    CHECK(cb.status == MTLCommandBufferStatusCompleted);

    /* Backdrop region clear of strip, panel and cards: blurred and dimmed. */
    int range; double mean;
    region_stats(rb, W, 900, 120, 48, &range, &mean);
    CHECK(range < 60);
    CHECK(mean < 0.8 * 127.5);

    /* Focused card (Voltar ao jogo): the exact corner is outside the rounded
     * shape, the edge midpoint is on the white border. */
    rsx_shell_layout L;
    rsx_shell_compute_layout((float)W, (float)H, 2.0f, RSX_SHELL_CARD_RESUME, 1, 0, 1.0f, &L);
    const rsx_shell_rect c = L.cards[RSX_SHELL_CARD_RESUME];
    const float cx = c.x + c.w * 0.5f, cy = c.y + c.h * 0.5f, hw = c.w * 0.5f * RSX_SHELL_FOCUS_SCALE;
    const uint8_t* corner = (const uint8_t*)rb.contents + (((size_t)(cy - hw + 1.0f)) * W + (size_t)(cx - hw + 1.0f)) * 4u;
    const uint8_t* edge = (const uint8_t*)rb.contents + (((size_t)cy) * W + (size_t)(cx - hw + 1.0f)) * 4u;
    CHECK(edge[1] > 200);
    CHECK(corner[1] < 170);

    /* Label under the focused card: bright text pixels exist. */
    int bright = 0;
    const uint32_t ly0 = (uint32_t)(cy + hw), ly1 = ly0 + (uint32_t)(24.0f * L.px_per_pt);
    for (uint32_t y = ly0; y < ly1 && y < H; ++y)
        for (uint32_t x = (uint32_t)(cx - hw); x < (uint32_t)(cx + hw); ++x)
            bright += ((const uint8_t*)rb.contents)[((size_t)y * W + x) * 4u + 1u] > 200;
    CHECK(bright > 20);

    rsx_overlay_metal_renderer_destroy(r);
    rsx_overlay_set_asset_builder(NULL);
    rsx_overlay_set_display(1280u, 720u, 1.0f);
    rsx_overlay_shutdown();
    unlink(path);
    return 1;
}
```

Call `test_blur_and_dim_pixels`, `test_render_target_only_drawable_still_blurs`, `test_blur_failure_falls_back_to_dim`, `test_cametallayer_drawable_blurs` and `test_real_shell_frame_at_2x` from `main` after `test_real_core_frame()`; add `#import <QuartzCore/QuartzCore.h>` to the test.

CMake for `test_rsx_overlay_metal`: add sources `libs/video/rsx_overlay_assets_apple.m` (with `-fobjc-arc` in its `set_source_files_properties`), frameworks `"-framework MetalPerformanceShaders" "-framework AppKit" "-framework CoreText" "-framework QuartzCore"`, and after `add_test`:

```cmake
        set_tests_properties(test_rsx_overlay_metal PROPERTIES ENVIRONMENT "MTL_DEBUG_LAYER=1")
```

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build build-macos --target test_rsx_overlay_metal`
Expected: `rsx_overlay_metal_renderer_test_blur_state` / `rsx_overlay_metal_renderer_test_fail_blur` undeclared.

- [ ] **Step 3: Implement the blur in `rsx_overlay_metal.m`**

- `#import <MetalPerformanceShaders/MetalPerformanceShaders.h>` and `#include "rsx_overlay_log.h"`.
- Append to `kOverlayShaderSource`:

```objc
     "struct bd_out { float4 pos [[position]]; float2 uv; };\n"
     "vertex bd_out bd_vs(uint vid [[vertex_id]]) {\n"
     "  float2 p = float2((vid << 1) & 2, vid & 2); bd_out o;\n"
     "  o.pos = float4(p * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0); o.uv = p; return o; }\n"
     "fragment float4 bd_fs(bd_out i [[stage_in]], texture2d<float> tex [[texture(0)]],\n"
     "                      sampler smp [[sampler(0)]], constant float& alpha [[buffer(0)]]) {\n"
     "  return float4(tex.sample(smp, i.uv).rgb, alpha); }\n";
```

- ivars: `id<MTLRenderPipelineState> _backdrop_pipeline; id<MTLTexture> _blur_src, _blur_a, _blur_b; MPSImageBilinearScale* _downscale; MPSImageGaussianBlur* _blur; int _blur_failed; int _test_fail_stage;` (`_blur_src` is the owned full-resolution copy of the drawable; `_test_fail_stage` stays 0 outside tests).
- In `overlay_create_pipeline`, after `_pipeline` succeeds, build the backdrop pipeline from the same library (failure only disables blur):

```objc
    MTLRenderPipelineDescriptor* bd = [[MTLRenderPipelineDescriptor alloc] init];
    bd.label = @"rsx_overlay_backdrop";
    bd.vertexFunction = [library newFunctionWithName:@"bd_vs"];
    bd.fragmentFunction = [library newFunctionWithName:@"bd_fs"];
    bd.colorAttachments[0] = pd.colorAttachments[0]; /* same format and blend */
    r->_backdrop_pipeline = [r->_device newRenderPipelineStateWithDescriptor:bd error:&error];
```

(If assigning `colorAttachments[0]` is rejected by the compiler, copy `pixelFormat`, `blendingEnabled` and the six blend fields one by one.)

- Helpers (under `_lock`):

```objc
static void overlay_blur_fail(RsxOverlayMetalRenderer* r, const char* why)
{
    if (r->_blur_failed)
        return;
    r->_blur_failed = 1;
    char line[160];
    snprintf(line, sizeof line, "overlay: backdrop blur unavailable (%s), dim only", why);
    rsx_overlay_log_write(RSX_OVERLAY_LOG_WARN, line);
    fprintf(stderr, "[OVERLAY-METAL] %s\n", line);
}

/* Owned full-resolution copy of the drawable (ShaderRead) plus the
 * half-resolution pair; recreated only when the drawable size or format
 * changes. Never called while the shell is closed. */
static int overlay_prepare_blur(RsxOverlayMetalRenderer* r, id<MTLTexture> target)
{
    if (r->_blur_failed)
        return 0;
    if (target.framebufferOnly) {
        overlay_blur_fail(r, "drawable is framebufferOnly");
        return 0;
    }
    if (!r->_blur) {
        if (r->_test_fail_stage == 1 || !r->_backdrop_pipeline || !MPSSupportsMTLDevice(r->_device)) {
            overlay_blur_fail(r, "MPS unsupported");
            return 0;
        }
        r->_downscale = [[MPSImageBilinearScale alloc] initWithDevice:r->_device];
        r->_blur = [[MPSImageGaussianBlur alloc] initWithDevice:r->_device sigma:RSX_OVERLAY_METAL_BLUR_SIGMA];
        if (!r->_downscale || !r->_blur) {
            r->_downscale = nil;
            r->_blur = nil;
            overlay_blur_fail(r, "MPS kernels unavailable");
            return 0;
        }
        r->_blur.edgeMode = MPSImageEdgeModeClamp;
    }
    const NSUInteger w = MAX((NSUInteger)1, target.width / 2), h = MAX((NSUInteger)1, target.height / 2);
    if (!r->_blur_src || r->_blur_src.width != target.width || r->_blur_src.height != target.height ||
        r->_blur_src.pixelFormat != target.pixelFormat) {
        MTLTextureDescriptor* full = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:target.pixelFormat
                                                                                         width:target.width
                                                                                        height:target.height
                                                                                     mipmapped:NO];
        full.usage = MTLTextureUsageShaderRead;
        full.storageMode = MTLStorageModePrivate;
        MTLTextureDescriptor* half = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:target.pixelFormat
                                                                                         width:w height:h mipmapped:NO];
        half.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
        half.storageMode = MTLStorageModePrivate;
        if (r->_test_fail_stage != 2) {
            r->_blur_src = [r->_device newTextureWithDescriptor:full];
            r->_blur_a = [r->_device newTextureWithDescriptor:half];
            r->_blur_b = [r->_device newTextureWithDescriptor:half];
        }
        if (!r->_blur_src || !r->_blur_a || !r->_blur_b) {
            r->_blur_src = r->_blur_a = r->_blur_b = nil;
            overlay_blur_fail(r, "scratch textures unavailable");
            return 0;
        }
    }
    return 1;
}
```

- In `rsx_overlay_metal_renderer_encode`, after `if (draw_commands == 0) break;` and before creating the render pass:

```objc
        /* Backdrop: only while the shell is (partly) open. Same drawable,
         * same command buffer, before the UI pass; never a second present. */
        const float open = packet->open_progress < 0.0f ? 0.0f : (packet->open_progress > 1.0f ? 1.0f : packet->open_progress);
        int blurred = 0;
        if (packet->backdrop_blur && open > 0.001f && overlay_prepare_blur(r, target)) {
            /* Blit (needs only !framebufferOnly) into a texture we own with
             * ShaderRead; the drawable's own usage bits are never relied on. */
            id<MTLBlitCommandEncoder> copy = [command_buffer blitCommandEncoder];
            [copy copyFromTexture:target toTexture:r->_blur_src];
            [copy endEncoding];
            [r->_downscale encodeToCommandBuffer:command_buffer sourceTexture:r->_blur_src destinationTexture:r->_blur_a];
            [r->_blur encodeToCommandBuffer:command_buffer sourceTexture:r->_blur_a destinationTexture:r->_blur_b];
            blurred = 1;
        }
```

  and right after `enc.label = @"rsx_overlay_ui";`:

```objc
        if (blurred) {
            /* The blurred frame fades in with the shell; the core's first UI
             * command then dims it (the same dim D3D12 gets without blur). */
            const MTLViewport whole = { 0.0, 0.0, (double)fb_w, (double)fb_h, 0.0, 1.0 };
            [enc setViewport:whole];
            [enc setRenderPipelineState:r->_backdrop_pipeline];
            [enc setFragmentTexture:r->_blur_b atIndex:0];
            [enc setFragmentSamplerState:r->_sampler atIndex:0];
            [enc setFragmentBytes:&open length:sizeof(open) atIndex:0];
            [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        }
```

  (the existing code then sets the UI viewport and pipeline, unchanged).
- Test hook and header:

```objc
#ifdef RSX_OVERLAY_TESTING
int rsx_overlay_metal_renderer_test_blur_state(rsx_overlay_metal_renderer* renderer)
{
    RsxOverlayMetalRenderer* r = (__bridge RsxOverlayMetalRenderer*)renderer;
    os_unfair_lock_lock(&r->_lock);
    const int state = (r->_blur_a ? 1 : 0) | (r->_blur_failed ? 2 : 0);
    os_unfair_lock_unlock(&r->_lock);
    return state;
}

void rsx_overlay_metal_renderer_test_fail_blur(rsx_overlay_metal_renderer* renderer, int stage)
{
    RsxOverlayMetalRenderer* r = (__bridge RsxOverlayMetalRenderer*)renderer;
    os_unfair_lock_lock(&r->_lock);
    r->_test_fail_stage = stage;
    os_unfair_lock_unlock(&r->_lock);
}
#endif
```

  `rsx_overlay_metal.h`: `#define RSX_OVERLAY_METAL_BLUR_SIGMA 6.0f /* half-res; ~24 px radius at drawable res */` and the `RSX_OVERLAY_TESTING` declaration from **Interfaces**.

- Link: add `"-framework MetalPerformanceShaders"` to the Apple `ps3recomp_runtime` PUBLIC list and `test_runtime_overlay_link`; in `games/gow2/build_macos.sh:573` change `-framework Metal -framework MetalFX` to `-framework Metal -framework MetalFX -framework MetalPerformanceShaders`.

- [ ] **Step 4: Run to verify pass**

Run: `cmake --build build-macos --target test_rsx_overlay_metal test_runtime_overlay_link ps3recomp_runtime && ctest --test-dir build-macos -R 'rsx_overlay_metal|runtime_overlay_link' --output-on-failure`
Expected: PASS with Metal API validation on (no validation errors in the output); `test_cametallayer_drawable_blurs` either passes or prints `SKIP` — record which in the ledger (a SKIP leaves the real-drawable path to the UAT "desfocado" item).

- [ ] **Step 5: Commit**

```bash
git add libs/video/rsx_overlay_metal.h libs/video/rsx_overlay_metal.m libs/video/tests/test_rsx_overlay_metal.m \
  CMakeLists.txt games/gow2/build_macos.sh
git commit -m "feat(metal): desfoque do jogo atrás da central (MPS em meia resolução)"
```

---

### Task 9: Launcher handoff of the overlay settings + UPSTREAM_DELTA re-anchor

**Precedence (ruling):** fullscreen and VSync have **one** source of truth — the runtime overlay settings file (`PS3_OVERLAY_SETTINGS`, else `user_config.json` `overlay_settings`, else `~/Library/Application Support/ps3recomp/gow2/runtime-overlay.settings`). The SwiftUI launcher's two toggles read and write that file; it never exports `PS3_FULLSCREEN`/`PS3_METAL_VSYNC`. An explicit value typed by a tester in "Avançado" (or set in the environment that started the launcher) still wins, exactly like `gow2_launcher.py` (`games/gow2/gow2_launcher.py:328-349`). On first run of the new launcher, a file without `fullscreen`/`vsync` keys is seeded from the old `@AppStorage` values (default `true`), so nobody's current behavior changes silently.

The runtime side of the same rule is made a pure function so it can be tested end to end: `rsx_overlay_resolve_flag(env, settings_loaded, saved, backend_default)` in `rsx_overlay_settings.h`, used by `rsx_metal_backend_init` for fullscreen and VSync, and by a tiny probe the launcher test runs as the "game".

**Files:**
- Modify: `libs/video/rsx_overlay_settings.h` (resolver), `libs/video/rsx_metal_backend.m` (init: the `PS3_FULLSCREEN` block after the overlay-graphics block, and the `PS3_METAL_VSYNC, when set, wins` block)
- Create: `games/gow2/launcher/macos/tests/launch_check/overlay_env_probe.c`
- Create: `games/gow2/launcher/macos/OverlaySettingsFile.swift`
- Modify: `games/gow2/launcher/macos/Settings.swift` (whole class), `Backend.swift:30-37` (config), `:75-87` (load), `:216-238` (launch script), `:252-257` (repo URL), `Views.swift:377-385`, `build_app.sh:15-19`
- Create: `games/gow2/launcher/macos/tests/launch_check/main.swift`, `games/gow2/launcher/macos/tests/launch_check/run.sh`
- Modify: `CMakeLists.txt` (swift test), `docs/UPSTREAM_DELTA.tsv:37`

**Interfaces:**
- Produces (C): `static inline int rsx_overlay_resolve_flag(const char* env_value, int settings_loaded, int saved_value, int backend_default);` — explicit env (non-empty: `"0"` off, else on) > loaded settings > backend default (fullscreen 0, VSync 1).
- Produces (Swift): `struct OverlaySettingsFile { let url: URL; func read() -> [String: String]; func update(_ changes: [String: String]) throws; static func flag(_ v: String?) -> Bool? }`; `LauncherCore.repoURL() -> URL`; `LauncherCore.overlaySettingsURL(repo: URL) -> URL`; `GameSettings.init(overlayFile:legacy:)`, `GameSettings.reloadFromFile()`.

- [ ] **Step 1: Write the failing check**

`games/gow2/launcher/macos/tests/launch_check/main.swift`:

```swift
import Foundation

// Launcher -> runtime handoff: one settings file, no forced env overrides.
var fails = 0
func check(_ c: Bool, _ m: String) { if !c { fails += 1; print("FAIL:", m) } }

let tmp = URL(fileURLWithPath: NSTemporaryDirectory()).appendingPathComponent("launch_check-\(getpid())")
try? FileManager.default.removeItem(at: tmp)
try! FileManager.default.createDirectory(at: tmp, withIntermediateDirectories: true)

// 1. update() keeps unrelated lines and order, replaces known keys, appends new ones.
let file = OverlaySettingsFile(url: tmp.appendingPathComponent("s/runtime-overlay.settings"))
try! file.update(["fullscreen": "0"])
check(file.read()["version"] == "2" && file.read()["fullscreen"] == "0", "created with version")
try! "version=1\nvsync=0\nanalog_deadzone=12\n".write(to: file.url, atomically: true, encoding: .utf8)
try! file.update(["vsync": "1", "fullscreen": "1"])
let text = try! String(contentsOf: file.url, encoding: .utf8)
check(text == "version=1\nvsync=1\nanalog_deadzone=12\nfullscreen=1\n", "preserve+replace+append: \(text)")
let mode = (try! FileManager.default.attributesOfItem(atPath: file.url.path)[.posixPermissions] as! NSNumber).intValue
check(mode == 0o600, "mode 0600, got \(String(mode, radix: 8))")

MainActor.assumeIsolated {
    // 2. Seeding from the old AppStorage values when the file lacks the keys.
    let defaults = UserDefaults(suiteName: "launch_check-\(getpid())")!
    defaults.set(false, forKey: "fullscreen")
    let seeded = OverlaySettingsFile(url: tmp.appendingPathComponent("seed.settings"))
    let s = GameSettings(overlayFile: seeded, legacy: defaults)
    check(s.fullscreen == false && s.vsync == true, "seeded from legacy")
    check(seeded.read()["fullscreen"] == "0" && seeded.read()["vsync"] == "1", "seed written")
    // 3. Toggling writes the file; the environment carries the path, never the flags.
    s.fullscreen = true
    check(seeded.read()["fullscreen"] == "1", "toggle persisted")
    let env = s.environment
    check(env["PS3_FULLSCREEN"] == nil && env["PS3_METAL_VSYNC"] == nil, "no forced overrides: \(env)")
    check(env["PS3_OVERLAY_SETTINGS"] == seeded.url.path, "settings path handed over")
    // 4. A file edited by the in-game menu wins on reload.
    try! seeded.update(["fullscreen": "0"])
    s.reloadFromFile()
    check(s.fullscreen == false, "reload from file")
    defaults.removePersistentDomain(forName: "launch_check-\(getpid())")
}

// 5. End to end: generated launch script -> env -> the runtime's own
//    precedence (overlay_env_probe links rsx_overlay_settings.c and prints what
//    rsx_metal_backend_init would pick). Three cases: caller env > saved file >
//    backend default, with env_gow2.sh's own VSync default in the way.
let probe = CommandLine.arguments[1]
let repo = tmp.appendingPathComponent("repo")
try! FileManager.default.createDirectory(at: repo, withIntermediateDirectories: true)
try! ": \"${PS3_METAL_VSYNC:=1}\"; export PS3_METAL_VSYNC\n".write(
    to: repo.appendingPathComponent("env_gow2.sh"), atomically: true, encoding: .utf8)
var cfg = LauncherConfig()
cfg.mods_dir = tmp.appendingPathComponent("mods").path
let script = LauncherCore.launchScript(repo: repo, c: cfg, binary: probe, resume: false)
func run(settings: URL, _ extra: [String: String]) -> String {
    let p = Process()
    p.executableURL = URL(fileURLWithPath: "/bin/bash")
    p.arguments = ["-c", script]
    var env = ProcessInfo.processInfo.environment
    env.removeValue(forKey: "PS3_FULLSCREEN"); env.removeValue(forKey: "PS3_METAL_VSYNC")
    env["PS3_OVERLAY_SETTINGS"] = settings.path
    for (k, v) in extra { env[k] = v }
    p.environment = env
    let pipe = Pipe(); p.standardOutput = pipe
    try! p.run(); p.waitUntilExit()
    return String(decoding: pipe.fileHandleForReading.readDataToEndOfFile(), as: UTF8.self)
        .trimmingCharacters(in: .whitespacesAndNewlines)
}
MainActor.assumeIsolated {
    // A. Caller env wins over the saved file.
    let a = OverlaySettingsFile(url: tmp.appendingPathComponent("a.settings"))
    try! a.update(["fullscreen": "0", "vsync": "1"])
    check(run(settings: a.url, ["PS3_FULLSCREEN": "1", "PS3_METAL_VSYNC": "0"]) == "fullscreen=1 vsync=0",
          "A: caller env wins")
    // B. Saved file (written by the launcher's own toggles) wins over script defaults.
    let bDefaults = UserDefaults(suiteName: "launch_check-b-\(getpid())")!
    let b = OverlaySettingsFile(url: tmp.appendingPathComponent("b.settings"))
    let gs = GameSettings(overlayFile: b, legacy: bDefaults)
    gs.fullscreen = true
    gs.vsync = false
    check(run(settings: b.url, [:]) == "fullscreen=1 vsync=0", "B: saved settings win over env_gow2.sh default")
    bDefaults.removePersistentDomain(forName: "launch_check-b-\(getpid())")
    // C. Nothing saved, nothing in env: backend defaults.
    let c = tmp.appendingPathComponent("absent.settings")
    check(run(settings: c, [:]) == "fullscreen=0 vsync=1", "C: backend defaults")
}

try? FileManager.default.removeItem(at: tmp)
print(fails == 0 ? "PASS" : "FAIL \(fails)")
exit(fails == 0 ? 0 : 1)
```

`games/gow2/launcher/macos/tests/launch_check/run.sh`:

```bash
#!/bin/bash
# Builds and runs the launcher handoff check. Usage: run.sh [out_dir]
set -euo pipefail
SRC="$(cd "$(dirname "$0")/../.." && pwd)"
ROOT="$(cd "$SRC/../../../.." && pwd)"
OUT="${1:-${TMPDIR:-/tmp}/launch_check}"
mkdir -p "$OUT"
cc -std=c11 -I"$ROOT/libs/video" "$SRC"/tests/launch_check/overlay_env_probe.c \
    "$ROOT"/libs/video/rsx_overlay_settings.c -o "$OUT/overlay_env_probe"
swiftc -swift-version 5 -target arm64-apple-macos14.0 \
    "$SRC"/Backend.swift "$SRC"/Settings.swift "$SRC"/OverlaySettingsFile.swift \
    "$SRC"/tests/launch_check/main.swift -o "$OUT/launch_check"
"$OUT/launch_check" "$OUT/overlay_env_probe"
```

CMake (next to `test_pad_overlay_route`):

```cmake
    find_program(PS3RECOMP_SWIFTC swiftc)
    if(APPLE AND PS3RECOMP_SWIFTC)
        add_test(NAME swift.launcher_launch_check
            COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/games/gow2/launcher/macos/tests/launch_check/run.sh"
                         "${CMAKE_CURRENT_BINARY_DIR}/launch_check")
    endif()
```

`games/gow2/launcher/macos/tests/launch_check/overlay_env_probe.c`:

```c
/* Stand-in for the game in the launcher test: loads PS3_OVERLAY_SETTINGS and
 * prints the fullscreen/VSync rsx_metal_backend_init would choose. */
#include <stdio.h>
#include <stdlib.h>

#include "rsx_overlay_settings.h"

int main(void)
{
    rsx_overlay_settings s;
    const int loaded = rsx_overlay_settings_load(&s, getenv("PS3_OVERLAY_SETTINGS"));
    printf("fullscreen=%d vsync=%d\n",
           rsx_overlay_resolve_flag(getenv("PS3_FULLSCREEN"), loaded, s.fullscreen, 0),
           rsx_overlay_resolve_flag(getenv("PS3_METAL_VSYNC"), loaded, s.vsync, 1));
    return 0;
}
```

- [ ] **Step 2: Run to verify failure**

Run: `chmod +x games/gow2/launcher/macos/tests/launch_check/run.sh && games/gow2/launcher/macos/tests/launch_check/run.sh`
Expected: compile error `rsx_overlay_resolve_flag` undeclared (probe) — then, once it exists, `cannot find 'OverlaySettingsFile' in scope`.

- [ ] **Step 3: Implement**

`libs/video/rsx_overlay_settings.h` (before the closing `extern "C"`):

```c
/* Launch-time precedence for fullscreen/VSync: an explicit environment value
 * wins ("0" off, any other non-empty value on), then a loaded settings file,
 * then the backend default. Mirrors metal_env_default's parsing. */
static inline int rsx_overlay_resolve_flag(const char* env_value, int settings_loaded,
                                           int saved_value, int backend_default)
{
    if (env_value != NULL && env_value[0] != '\0')
        return env_value[0] != '0';
    if (settings_loaded)
        return saved_value != 0;
    return backend_default != 0;
}
```

(add `#include <stddef.h>`). `rsx_metal_backend_init`: replace the `PS3_FULLSCREEN` block (the one right after `if (have_overlay_graphics) { ... }`) and drop the `if (overlay_graphics.fullscreen) win_flags |= ...` line inside that block, with

```objc
    if (rsx_overlay_resolve_flag(getenv("PS3_FULLSCREEN"), have_overlay_graphics,
                                 have_overlay_graphics ? overlay_graphics.fullscreen : 0, 0))
        win_flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    else
        win_flags &= ~(Uint32)SDL_WINDOW_FULLSCREEN_DESKTOP;
```

and the VSync block with

```objc
    {   /* Same precedence as the launcher test (rsx_overlay_resolve_flag). */
        const int vsync = rsx_overlay_resolve_flag(getenv("PS3_METAL_VSYNC"), have_overlay_graphics,
                                                   have_overlay_graphics ? overlay_graphics.vsync : 1, 1);
        s_state.layer.displaySyncEnabled = vsync ? YES : NO;
    }
```

(behavior is unchanged: same env parsing as `metal_env_default`, same defaults).

`games/gow2/launcher/macos/OverlaySettingsFile.swift`:

```swift
import Foundation

/// The runtime overlay settings file (libs/video/rsx_overlay_settings.c):
/// `key=value` lines. The launcher and the in-game menu edit the same file.
struct OverlaySettingsFile {
    let url: URL

    static func defaultURL(home: URL = FileManager.default.homeDirectoryForCurrentUser) -> URL {
        home.appendingPathComponent("Library/Application Support/ps3recomp/gow2/runtime-overlay.settings")
    }

    static func flag(_ value: String?) -> Bool? {
        switch value { case "1": return true; case "0": return false; default: return nil }
    }

    func read() -> [String: String] {
        guard let text = try? String(contentsOf: url, encoding: .utf8) else { return [:] }
        var out: [String: String] = [:]
        for line in text.split(separator: "\n") {
            let parts = line.split(separator: "=", maxSplits: 1, omittingEmptySubsequences: false)
            if parts.count == 2 { out[String(parts[0])] = String(parts[1]) }
        }
        return out
    }

    /// Replaces the given keys in place, appends missing ones, keeps every
    /// other line; creates the file (with `version=2`) when absent.
    func update(_ changes: [String: String]) throws {
        var lines = (try? String(contentsOf: url, encoding: .utf8))?
            .components(separatedBy: "\n") ?? []
        if lines.last == "" { lines.removeLast() }
        var pending = changes
        lines = lines.map { line in
            let parts = line.split(separator: "=", maxSplits: 1, omittingEmptySubsequences: false)
            guard parts.count == 2, let value = pending.removeValue(forKey: String(parts[0])) else { return line }
            return "\(parts[0])=\(value)"
        }
        if !lines.contains(where: { $0.hasPrefix("version=") }) { lines.insert("version=2", at: 0) }
        for key in pending.keys.sorted() { lines.append("\(key)=\(pending[key]!)") }
        try FileManager.default.createDirectory(at: url.deletingLastPathComponent(),
                                                withIntermediateDirectories: true)
        try (lines.joined(separator: "\n") + "\n").write(to: url, atomically: true, encoding: .utf8)
        try FileManager.default.setAttributes([.posixPermissions: 0o600], ofItemAtPath: url.path)
    }
}
```

`Backend.swift`:
- `LauncherConfig`: add `var overlay_settings: String? = nil`; in `load(repo:)` add `if let v = obj["overlay_settings"] as? String, !v.isEmpty { c.overlay_settings = v }`.
- In `enum LauncherCore` add

```swift
    /// The checkout the launcher drives: GOW2_REPO, else the path stamped by build_app.sh.
    static func repoURL() -> URL {
        let env = ProcessInfo.processInfo.environment["GOW2_REPO"]
        let stamped = Bundle.main.object(forInfoDictionaryKey: "GoW2RepoPath") as? String
        return URL(fileURLWithPath: env ?? stamped ?? FileManager.default.currentDirectoryPath)
    }

    /// Same order as gow2_launcher.py: caller env, user_config.json, per-user default.
    static func overlaySettingsURL(repo: URL) -> URL {
        if let e = ProcessInfo.processInfo.environment["PS3_OVERLAY_SETTINGS"], !e.isEmpty {
            return URL(fileURLWithPath: e)
        }
        if let c = load(repo: repo).overlay_settings {
            return URL(fileURLWithPath: (c as NSString).expandingTildeInPath)
        }
        return OverlaySettingsFile.defaultURL()
    }
```

  and in `Backend.init` replace the three repo lines with `repo = LauncherCore.repoURL()`.
- `launchScript`: the returned script becomes

```swift
        return """
        set -euo pipefail
        cd \(shq(repo.path))
        G2_FULLSCREEN="${PS3_FULLSCREEN-}"
        G2_VSYNC="${PS3_METAL_VSYNC-}"
        set -a
        . ./env_gow2.sh   # ${VAR:=default}: the player's settings, exported before, win
        set +a
        export PS3_VFS_ROOT=\(shq(c.vfs_root))
        export PS3_MOVIE_CACHE=\(shq(c.movie_cache))
        export PS3_MODS_DIR=\(shq(c.mods_dir))
        export PS3_MODS_ENABLED=\(shq(enabled.joined(separator: ",")))
        export PS3_MOVIE_DONE_MS="${GOW2_MOVIE_DONE_MS:-auto}"
        # Fullscreen and VSync come from the overlay settings file; only an
        # explicit caller value overrides it (env_gow2.sh's default must not).
        if [ -n "$G2_FULLSCREEN" ]; then export PS3_FULLSCREEN="$G2_FULLSCREEN"; else unset PS3_FULLSCREEN; fi
        if [ -n "$G2_VSYNC" ]; then export PS3_METAL_VSYNC="$G2_VSYNC"; else unset PS3_METAL_VSYNC; fi
        \(resumeEnv)unset PS3_NO_RSX
        exec \(shq(binary)) \(shq(c.elf))
        """
```

`Settings.swift` — replace the class:

```swift
import SwiftUI

/// Player-facing options. Every one maps to something the runtime already
/// reads; nothing here invents behaviour. Fullscreen and VSync live in the
/// runtime overlay settings file -- the same file the in-game menu edits.
@MainActor
final class GameSettings: ObservableObject {
    @Published var fullscreen: Bool { didSet { if fullscreen != oldValue { write("fullscreen", fullscreen) } } }
    @Published var vsync: Bool { didSet { if vsync != oldValue { write("vsync", vsync) } } }
    @AppStorage("metalFX") var metalFX = true
    @AppStorage("hdr") var hdr = false
    @AppStorage("frameFence") var frameFence = true
    @AppStorage("soundOn") var soundOn = true
    @AppStorage("movieSkip") var movieSkip = true
    @AppStorage("autostart") var autostart = false
    @AppStorage("extraEnv") var extraEnv = ""

    let overlayFile: OverlaySettingsFile

    init(overlayFile: OverlaySettingsFile = OverlaySettingsFile(
            url: LauncherCore.overlaySettingsURL(repo: LauncherCore.repoURL())),
         legacy: UserDefaults = .standard) {
        self.overlayFile = overlayFile
        let saved = overlayFile.read()
        // First run of this launcher: carry over the old toggles (default on).
        let fs = OverlaySettingsFile.flag(saved["fullscreen"]) ?? (legacy.object(forKey: "fullscreen") as? Bool ?? true)
        let vs = OverlaySettingsFile.flag(saved["vsync"]) ?? (legacy.object(forKey: "vsync") as? Bool ?? true)
        fullscreen = fs
        vsync = vs
        var seed: [String: String] = [:]
        if saved["fullscreen"] == nil { seed["fullscreen"] = fs ? "1" : "0" }
        if saved["vsync"] == nil { seed["vsync"] = vs ? "1" : "0" }
        if !seed.isEmpty { try? overlayFile.update(seed) }
    }

    /// The in-game menu may have changed the file since the last read.
    func reloadFromFile() {
        let saved = overlayFile.read()
        if let f = OverlaySettingsFile.flag(saved["fullscreen"]), f != fullscreen { fullscreen = f }
        if let v = OverlaySettingsFile.flag(saved["vsync"]), v != vsync { vsync = v }
    }

    private func write(_ key: String, _ on: Bool) { try? overlayFile.update([key: on ? "1" : "0"]) }
    private func flag(_ on: Bool) -> String { on ? "1" : "0" }

    var environment: [String: String] {
        var env: [String: String] = [
            "PS3_OVERLAY_SETTINGS": overlayFile.url.path, // fullscreen/VSync + in-game menu
            "PS3_METALFX": flag(metalFX),            // MetalFX spatial upscale
            "PS3_METAL_HDR": flag(hdr),
            "PS3_METAL_FRAME_FENCE": flag(frameFence), // no 1-frame giant triangle
            "PS3_MUTE": flag(!soundOn),              // audio_mute.c
            "PS3_MOVIE_SKIP": flag(movieSkip),       // cellPad.c: START/CROSS skips
            "PS3_PAD_AUTOSTART": flag(autostart),    // presses Start through the intro
        ]
        // Advanced: KEY=VALUE per line, for testers (PS3_FULLSCREEN=... still wins).
        for line in extraEnv.split(separator: "\n") {
            let parts = line.split(separator: "=", maxSplits: 1).map {
                $0.trimmingCharacters(in: .whitespaces)
            }
            if parts.count == 2, parts[0].hasPrefix("PS3_") { env[parts[0]] = parts[1] }
        }
        return env
    }
}
```

`Views.swift` `GraphicsView`: change the caption under "Tela cheia ao abrir" to `Text("Também muda no menu do jogo (botão PS, Select+Start ou F1). F11 ou Cmd+Enter alternam durante o jogo; Esc solta o mouse.")` and add `.onAppear { settings.reloadFromFile() }` to the `Form`.

`build_app.sh`: add `"$SRC"/OverlaySettingsFile.swift` to the `swiftc` source list.

`docs/UPSTREAM_DELTA.tsv:37`: the anchored README line 3 now reads ``**God of War II HD** (PS3, `NPUA80491`) running…``. Re-anchor by hand (data lines are hand-maintained; `#` lines are not touched):

```bash
sed -i '' '37s/God of War II HD (NPUA80491)/(PS3, `NPUA80491`)/' docs/UPSTREAM_DELTA.tsv
```

- [ ] **Step 4: Run to verify pass**

Run: `games/gow2/launcher/macos/tests/launch_check/run.sh && games/gow2/launcher/macos/build_app.sh >/dev/null && python3 tools/upstream_delta.py validate --tsv docs/UPSTREAM_DELTA.tsv && ctest --test-dir build-macos -R 'swift.launcher|py.test_upstream_delta' --output-on-failure`
Expected: `PASS`, the app builds (`built: …`; remove the produced `GoW2 Recomp.app` if it lands inside the worktree — it is not versioned), the validator prints no `FAIL`, and both CTests pass.

- [ ] **Step 5: Commit**

```bash
git add libs/video/rsx_overlay_settings.h libs/video/rsx_metal_backend.m \
  games/gow2/launcher/macos/tests/launch_check/overlay_env_probe.c \
  games/gow2/launcher/macos/OverlaySettingsFile.swift games/gow2/launcher/macos/Settings.swift \
  games/gow2/launcher/macos/Backend.swift games/gow2/launcher/macos/Views.swift games/gow2/launcher/macos/build_app.sh \
  games/gow2/launcher/macos/tests/launch_check/main.swift games/gow2/launcher/macos/tests/launch_check/run.sh \
  CMakeLists.txt docs/UPSTREAM_DELTA.tsv
git commit -m "fix(launcher): tela cheia e VSync vêm do arquivo do overlay; reâncora do UPSTREAM_DELTA"
```

---

### Task 10: Documentation and pt-BR UAT checklist

**Files:**
- Modify: `games/gow2/README.md:181-230` ("Overlay de runtime (menu F1) e o launcher" — Controles and Precedência subsections)
- Create: `docs/superpowers/plans/2026-09-23-gow2-overlay-console-shell-uat.md`

**Interfaces:** none (documentation). Facts to document come from Tasks 1–9; do not document anything not implemented.

- [ ] **Step 1: Rewrite the README "Controles" subsection**

Replace the bullet list under `### Controles` with:

```markdown
- **Abrir/fechar a central:** botão **PS/Home** do controle (macOS, via
  GameController), **segure Select e aperte Start**, ou **F1** no teclado
  (`toggle_key=2` no arquivo troca para F2). A central sempre abre em
  **"Voltar ao jogo"** e o jogo continua rodando atrás dela, sem receber
  entrada. No Windows o botão PS/Home não é lido (XInput não o expõe):
  use Select+Start ou F1.
- **Navegação:** esquerda/direita troca de cartão; baixo (ou X/Enter) entra
  no painel; cima/baixo percorre as linhas; **X/Enter** confirma; **O/Esc**
  volta um nível e, nos cartões, fecha. PS/Home, Select+Start e F1 fecham
  de qualquer lugar. Um botão que ainda estiver pressionado quando a central
  fecha só chega ao jogo depois de solto (o X que confirmou "Voltar ao jogo"
  não vira pulo; o Enter não vira pausa).
- **Cartões:** Voltar ao jogo · Tela (tela cheia, VSync, tamanho da janela) ·
  Controles (zona morta 0–30 %, vibração do jogo, remapear botões, restaurar
  padrão) · Som (som do jogo, sons da interface, vibração da interface) ·
  Desenvolvedor (FPS/tempo de quadro, draws do jogo, estado do WAD, shaders,
  Logs) · Sair (pede confirmação; as configurações já estão salvas).
- **Remapear:** escolha o botão do PS3 na lista e pressione o botão do
  controle que deve acioná-lo; o antigo dono troca de lugar (o mapa nunca
  fica com botões repetidos). Esc cancela; sem resposta em 5 s, cancela.
- **Visual:** no macOS o jogo fica desfocado e escurecido atrás da central
  (Metal + MetalPerformanceShaders); no Windows só escurece. Texto em SF Pro
  e ícones SF Symbols no macOS; Inter (OFL, `third_party/inter`) e ícones
  geométricos no resto — ou se a fonte do sistema faltar.
- **Som do jogo** (menu) e **"Som ligado"** (launcher, `PS3_MUTE`) somam-se:
  o jogo só toca com os dois ligados.
- Valores não medidos aparecem como **"indisponível"** — nunca como zero.
- **O menu sempre inicia fechado.**
```

Replace the `### Precedência` subsection body with:

```markdown
**Tela cheia e VSync têm uma fonte só: o arquivo de configurações do
overlay.** O menu do jogo e o launcher SwiftUI (`GoW2 Recomp.app`) editam o
mesmo arquivo; o launcher não exporta mais `PS3_FULLSCREEN`/`PS3_METAL_VSYNC`
e passa o caminho em `PS3_OVERLAY_SETTINGS`. Na primeira execução do launcher
novo, um arquivo sem essas chaves recebe os valores que o launcher tinha
(padrão: ligados).

Um valor **explícito** ainda vence o arquivo: `PS3_FULLSCREEN`/
`PS3_METAL_VSYNC` definidos no ambiente de quem chamou (ou em "Avançado" no
launcher). Os scripts `env_gow2.sh`/`rodar_gow2.sh` têm padrões próprios
(`${PS3_METAL_VSYNC:=1}`, `${PS3_FULLSCREEN:-1}`); o `gow2_launcher.py` e o
launcher SwiftUI guardam o valor do chamador antes de carregar o
`env_gow2.sh` e removem a variável se ele não definiu nada — o padrão do
script não mascara o arquivo.
```

Keep the rest of the section ("Onde fica o arquivo…", the "Importante" paragraph) as is, adding to the settings list the v2 keys: `ui_sounds`, `ui_haptics`, `game_mute`, `vibration` (arquivos da versão 1 carregam com os padrões dessas chaves).

- [ ] **Step 2: Write the UAT checklist**

`docs/superpowers/plans/2026-09-23-gow2-overlay-console-shell-uat.md`:

```markdown
# UAT — Central estilo PS5 do overlay (GoW2, macOS)

Quem roda: o usuário, no próprio Mac, com `./jogar_g2.sh` ou o `GoW2 Recomp.app`
(o agente não abre instâncias com o usuário jogando). Marque cada item com o
que **viu**; "não testado" é resposta válida. Nada aqui prova pixels naturais
do jogo — a central é UI do host.

Build testado: `git rev-parse --short HEAD` = ________  Data: ________

## Abrir e fechar
- [ ] Botão **PS/Home** abre a central; o macOS **não** abre Launchpad/Game Center junto.
- [ ] PS/Home de novo fecha; segurar PS/Home não fica abrindo/fechando.
- [ ] **Select+Start** (segurar Select, apertar Start) abre e fecha; Start sozinho continua pausando o jogo.
- [ ] **F1** abre e fecha.
- [ ] Abre sempre em **"Voltar ao jogo"**.
- [ ] X em "Voltar ao jogo" fecha e o Kratos **não** pula; Enter fecha e o jogo **não** pausa.

## Visual
- [ ] Jogo atrás **desfocado e escurecido**, e continua se mexendo (desfoque ao vivo).
- [ ] Faixa escura em degradê embaixo; cartões arredondados; cartão focado maior, com borda branca e brilho.
- [ ] "Sair" fica sozinho à direita.
- [ ] Painel flutuante acima do cartão focado, com sombra; linha focada destacada; chaves azuis.
- [ ] Topo: "God of War II HD" à esquerda; hora à direita; ícone do controle e bateria **só** com controle que informa bateria.
- [ ] Texto nítido em tela Retina e em tela cheia (sem serrilhado nem borrão).
- [ ] Animação: a fileira sobe com mola ao abrir; o foco desliza; painel troca com fade curto; fechar faz o inverso.
- [ ] Janela pequena (1280×720) e tela cheia: nada se sobrepõe, nada sai da tela.

## Cartões
- [ ] **Tela**: Tela cheia e VSync mudam na hora; Tamanho da janela abre a lista e aplica; linha "requer reiniciar" aparece só se o backend pedir.
- [ ] **Controles**: zona morta anda de 1 em 1 (0–30 %) segurando esquerda/direita; Vibração desliga o tremor do jogo; Restaurar padrão volta o mapa.
- [ ] **Remapear**: escolher "Cruz", apertar L3 → L3 passa a pular no jogo; o botão antigo troca de lugar; Esc cancela; 5 s sem apertar cancela; nada do que é apertado durante a captura chega ao jogo.
- [ ] **Som**: Som do jogo desliga/religa o áudio do jogo na hora; Sons da interface e Vibração da interface desligam cada um.
- [ ] **Desenvolvedor**: FPS/ms, draws, estado do WAD, shaders — ou "indisponível"; Logs abre a lista (mais recente primeiro) e rola.
- [ ] **Sair**: pede "Sair do God of War II HD?"; Cancelar volta; Sair fecha o jogo.

## Sons e háptica
- [ ] Sons curtos ao abrir, mover, confirmar, voltar, fechar; nenhum estalo.
- [ ] Tique no controle ao navegar (com "Vibração da interface" ligada).
- [ ] Nada disso engasga o jogo.

## Persistência e launcher
- [ ] Mudar tela cheia/VSync no jogo, fechar, abrir pelo launcher: o launcher mostra o valor novo e o jogo abre assim.
- [ ] Mudar no launcher: o jogo abre com o valor do launcher.
- [ ] Mudanças da central continuam depois de reiniciar o jogo.

## Com a central fechada
- [ ] Jogo idêntico a antes (FPS, controles, som, vibração).
- [ ] Cutscene in-game e intro tocam normalmente, com e sem abrir a central durante elas.

Observações / prints: ______________________________________________
```

- [ ] **Step 3: Verify**

Run: `git diff --check && grep -n "F1 abre/fecha o menu (tecla padrão)" games/gow2/README.md`
Expected: no whitespace errors; the grep prints nothing (old text replaced).

- [ ] **Step 4: Commit**

```bash
git add games/gow2/README.md docs/superpowers/plans/2026-09-23-gow2-overlay-console-shell-uat.md
git commit -m "docs(gow2): central estilo PS5 no README e checklist de UAT"
```

- [ ] **Step 5: Branch-level verification (no in-boot claims)**

Run: `cmake --build build-macos && ctest --test-dir build-macos -j8 2>&1 | tail -30`
Expected: every test that passed in Task 1 Step 0 still passes, plus `py.test_upstream_delta` and the new tests. Record the result in the SDD ledger as **offline/unit** evidence; the in-boot acceptance is the user's UAT above.

---

## Self-Review

**Spec coverage.**
- Visual language (blur, dim, band, cards, panel, top strip, typography, icons, motion, real resolution): Tasks 1 (real size), 3 (motion constants/springs/layout), 4 (SF Pro/SF Symbols/Inter), 7 (drawing), 8 (blur).
- Six cards and panels, navigation rules, open on "Voltar ao jogo", remap capture, quit confirmation, "indisponível", "requer reiniciar": Tasks 3 and 7.
- Architecture 1 (shell module): Task 3 (+7 drawing). 2 (packet v2): Tasks 1 and 7. 3 (assets): Task 4. 4 (Metal blur): Task 8. 5 (PS/Home): Task 6. 6 (sounds/haptics): Task 5 (+7 sink). 7 (settings v2): Task 2. 8 (consumers game_mute/vibration): Task 5 (+7 wiring).
- Error handling: blur unavailable → Task 8 (`test_blur_failure_falls_back_to_dim`, injected MPS/allocation failure, pixel-checked; `framebufferOnly` drawable → same path); SF unavailable → Task 4 fallback; PS/Home unavailable → chord/F1 unchanged (Task 6 tests, docs); audio engine failure → Task 5 (state −1, silent); renderer failure → existing `rsx_overlay_disable` kept (Task 7 clears render-active).
- Testing section: state machine/focus/springs (Task 3), packet v2 (Task 1), settings v2 (Task 2), headless Metal 2x (Task 8), pad PS/Home (Task 6), cellAudio mute (Task 5), pt-BR UAT (Task 10).
- Extra requirements: launcher handoff + delta re-anchor (Task 9); MinGW checks in Tasks 1, 2, 3, 4, 5, 6, 7.

**Rulings on spec gaps/ambiguities** (all recorded in the tasks above):
1. Panel is visible for the focused card at card level (rows unfocused); "Voltar ao jogo" and "Sair" act on Confirm at card level, Down does nothing for them.
2. Dialog focus defaults to **Cancelar**.
3. Remap list = the 16 PS3 buttons; capture binds the first newly pressed controller button and swaps with the previous owner; keyboard Esc cancels (pad Circle is capturable); 5 s timeout; every button is withheld from the game during capture.
4. Dead-zone slider: 0–30 %, step 1; stored values above 30 (older UI) are kept and only move down.
5. Dim is always drawn by the core as the first command (so D3D12 gets it); Metal adds the blurred frame underneath, alpha = `open_progress`. `backdrop_dim` in the packet is informative.
6. Menu navigation uses the D-pad and Cross/Circle only (sticks stay centered, as today).
7. The ImGui font atlas is replaced as texture 0 by the shell atlas; ImGui's shared white UV is pointed at it — no second texture for renderers. The atlas contract (texture id, RGBA8 straight alpha, white block and UV, generation) is written in Task 7 **Interfaces** and pinned in the core, Metal and D3D12 tests.
12. The Metal blur never samples the drawable: it blits it into an owned `ShaderRead` texture first (backend guarantees `framebufferOnly = NO`).
13. Preflight (Task 1 Step 0) merges the moved `macos-arm64-port-f0-f2` and re-validates anchors by text before any code change.
8. Platform pieces are installed by the Metal backend (asset builder, feedback sink) so the core and its tests stay platform-free.
9. Any close path withholds buttons held at that moment until released (generalizes the chord rule).
10. Launcher "Som ligado" (`PS3_MUTE`) stays as is; it and the in-game "Som do jogo" combine (documented).
11. Windows: no PS/Home (XInput has no public Guide bit); backing scale fixed at 1.0.

**Placeholder scan:** no TBD/TODO; every code step has code; the only conditional instructions are compiler-dependent fallbacks with the exact alternative spelled out (Task 4 `Threads::Threads`, Task 8 `colorAttachments` copy).

**Type consistency:** names checked across tasks — `rsx_overlay_set_display/get_display`, `rsx_overlay_display_transform`, `rsx_overlay_clip_to_(viewport_)scissor(clip, dw, dh, …)`, `rsx_shell_*` (Task 3 header is the single definition), `rsx_overlay_assets_build_embedded/apple`, `rsx_overlay_asset_builder`, `rsx_overlay_font_atlas_generation`, `rsx_overlay_feedback_sink(int,int,int,void*)`, `rsx_overlay_feedback_apple_play` (matches the sink), `cpgc_ui_tick(float,float)` (matches `set_haptic`), `rsx_overlay_release_guard`, `rsx_overlay_test_input_down`, `RSX_SHELL_BACKDROP_DIM`, `RSX_OVERLAY_METAL_BLUR_SIGMA`.

**Review Focus coverage:** item 1 → Task 7 `test_closing_from_menu_withholds_held_buttons`; item 2 → Task 3 `test_layout_extremes_fit`; item 3 → Task 7 `test_scale_change_rebuilds_atlas` + Task 4 `test_atlas_generation_reloads_texture`; item 4 → Task 3 `test_reopen_during_close_resumes` + Task 7 `test_closed_shell_costs_nothing`; item 5 → Task 7 `test_draw_budget_worst_case`.
