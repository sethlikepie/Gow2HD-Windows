# Dynamic Native Prompt Textures Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace God of War II's native RSX prompt atlas dynamically for keyboard, PlayStation, Xbox and Nintendo input, without a screen-space overlay.

**Architecture:** A pure-C prompt module identifies one measured UI atlas by canonical content identity and rasterizes a BGRA8 variant for the active input family. `metal_bind_texture` selects that variant in the original texture slot; unknown content, cache misses and unsupported formats keep the game's original texture. A gated menu-window probe creates the local evidence needed to establish the immutable atlas manifest before the replacement is enabled.

**Tech Stack:** C11 unit tests, Objective-C/Metal texture upload/cache, macOS GameController input, GoW2 local test harness.

**Spec:** `docs/superpowers/specs/2026-09-18-dynamic-native-prompt-textures-design.md`

## Global Constraints

- Never modify or commit EBOOT, WAD, PSARC, extracted game data, captures or other copyrighted assets.
- Do not use a screen-space overlay, fixed screen coordinate, black panel, shader bypass, or guest-memory/WAD mutation.
- All discovery logging and image dumps are environment-gated and OFF by default.
- The original guest texture is the fallback for every unknown identity, malformed manifest, unsupported format or allocation error.
- Input-family choice is `host_prompts_current()` and changes without restarting the game.
- macOS/Metal is the shipping target; shared C code must not depend on Metal or GameController.

---

### Task 1: Remove the rejected compositor and introduce prompt identity types

**Files:**

- Create: `libs/input/rsx_prompt_texture.h`
- Create: `libs/input/rsx_prompt_texture.c`
- Create: `libs/input/tests/test_rsx_prompt_texture.c`
- Modify: `libs/input/host_prompts.c`, `host_prompts.h`, `host_prompt_overlay.c`, `host_prompt_overlay.h`, `tests/test_host_prompts.c`
- Modify: `libs/video/rsx_metal_backend.m`, `CMakeLists.txt`

**Interfaces:**

- Produces `rsx_prompt_texture_id`, `rsx_prompt_texture_make_id()`, `rsx_prompt_texture_id_equal()`, and `rsx_prompt_variant_key()`.
- Produces `rsx_prompt_texture_is_menu_draw_count(unsigned draws)` for the 180–300 draw discovery window.
- Removes `prompt_overlay_render()` and all fullscreen/lower-left compositor resources from Metal.

- [ ] **Step 1: Write the failing identity test**

```c
rsx_prompt_texture_id a = rsx_prompt_texture_make_id(0x1234, 256, 128, 0x85, 0, 0xAAE4);
rsx_prompt_texture_id b = rsx_prompt_texture_make_id(0x1234, 256, 128, 0x85, 0, 0xAAE4);
CHECK(rsx_prompt_texture_id_equal(&a, &b));
CHECK(!rsx_prompt_texture_id_equal(
    &a, &rsx_prompt_texture_make_id(0x1235, 256, 128, 0x85, 0, 0xAAE4)));
CHECK(rsx_prompt_texture_is_menu_draw_count(227));
CHECK(!rsx_prompt_texture_is_menu_draw_count(594));
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build-macos --target test_rsx_prompt_texture && ./build-macos/test_rsx_prompt_texture`

Expected: target/function missing.

- [ ] **Step 3: Implement the minimal pure identity module**

```c
typedef struct {
    uint64_t content_hash;
    uint32_t width, height, rsx_format, swizzled, remap;
} rsx_prompt_texture_id;

uint64_t rsx_prompt_variant_key(const rsx_prompt_texture_id* id,
                                host_prompt_set set);
```

Compare every field exactly; do not use guest EA. Variants include all identity fields plus the prompt set. The menu predicate is true only for inclusive 180–300 draws.

- [ ] **Step 4: Remove the rejected compositor**

Delete post-present prompt rendering, its Metal resources and panel/keycap functions. `PS3_HOST_PROMPTS` remains device-selection only and no longer emits pixels by itself.

- [ ] **Step 5: Run tests**

Run: `cmake --build build-macos --target test_rsx_prompt_texture test_host_prompts -j4 && ctest --test-dir build-macos -R 'test_rsx_prompt_texture|test_host_prompts' --output-on-failure`

Expected: both pass and `prompt_overlay_render` has no references.

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt libs/input/rsx_prompt_texture.h libs/input/rsx_prompt_texture.c   libs/input/tests/test_rsx_prompt_texture.c libs/input/host_prompts.c   libs/input/host_prompts.h libs/input/host_prompt_overlay.c   libs/input/host_prompt_overlay.h libs/input/tests/test_host_prompts.c   libs/video/rsx_metal_backend.m
git commit -m "refactor(input): remove compositor de prompts"
```

### Task 2: Diagnose the native menu atlas, including cache hits

**Files:**

- Modify: `libs/video/rsx_metal_backend.m`
- Modify: `libs/input/rsx_prompt_texture.h`, `rsx_prompt_texture.c`, `tests/test_rsx_prompt_texture.c`

**Interfaces:**

- Consumes canonical post-deswizzle/upload bytes and `rsx_prompt_texture_id`.
- Produces gated `[PROMPTATLAS]` rows with full hash, metadata, slot, draw count and cache-hit/cache-miss state.
- Produces `rsx_prompt_texture_manifest_match(const rsx_prompt_texture_id*)`.

- [ ] **Step 1: Write failing manifest-guard tests**

```c
CHECK(!rsx_prompt_texture_manifest_match(&unknown));
CHECK(!rsx_prompt_texture_manifest_match(&same_hash_wrong_dimensions));
CHECK(rsx_prompt_texture_manifest_match(&measured_identity));
```

The fixture is metadata only. No game pixels enter the repository.

- [ ] **Step 2: Run and verify failure**

Run: `cmake --build build-macos --target test_rsx_prompt_texture && ./build-macos/test_rsx_prompt_texture`

Expected: matcher absent or measured identity unregistered.

- [ ] **Step 3: Instrument both Metal bind paths**

Behind `PS3_TRACE_PROMPT_ATLAS=1`, calculate the canonical full hash after the upload conversion and retain it in `MetalTexCacheEnt`. On both a cache hit and a new upload, emit a row only for 180–300 draw frames. Pixel dump stays separately gated.

- [ ] **Step 4: Measure and freeze the manifest**

Boot the unchanged menu with `PS3_HOST_PROMPTS=0 PS3_TRACE_PROMPT_ATLAS=1`. Inspect only local artifacts, identify the exact `X = SELECT` atlas and its rectangles, then add hash guards, dimensions, format and all prompt rectangles to the manifest. If it is compressed, decode to BGRA8 before patching; never write BC/DXT blocks.

- [ ] **Step 5: Verify unknown safety**

A second boot with tracing off must produce no prompt rows, match no unrelated gameplay texture and show the original menu unchanged.

- [ ] **Step 6: Commit**

```bash
git add libs/video/rsx_metal_backend.m libs/input/rsx_prompt_texture.h   libs/input/rsx_prompt_texture.c libs/input/tests/test_rsx_prompt_texture.c
git commit -m "diag(metal): identifica atlas nativo de prompts"
```

### Task 3: Rasterize and cache native variants for every input family

**Files:**

- Create: `libs/input/rsx_prompt_raster.h`
- Create: `libs/input/rsx_prompt_raster.c`
- Create: `libs/input/tests/test_rsx_prompt_raster.c`
- Modify: `libs/input/rsx_prompt_texture.h`, `rsx_prompt_texture.c`, `libs/video/rsx_metal_backend.m`, `CMakeLists.txt`

**Interfaces:**

- Consumes `uint8_t* bgra`, dimensions, stride, measured manifest rectangles and `host_prompt_set`.
- Produces `int rsx_prompt_raster_apply(...)`, which returns 1 only after a known atlas is patched and otherwise leaves input byte-identical.
- Metal variants are keyed by `rsx_prompt_variant_key()`.

- [ ] **Step 1: Write failing raster tests with synthetic BGRA fixtures**

```c
make_synthetic_prompt_atlas(before, 64, 64);
memcpy(after, before, sizeof after);
CHECK(rsx_prompt_raster_apply(after, 64, 64, 64 * 4,
                              HOST_PROMPT_SET_KB, &fixture));
CHECK(memcmp(before + outside, after + outside, 4) == 0);
CHECK(memcmp(before + fixture.cross_rect, after + fixture.cross_rect, 4) != 0);
```

Repeat for PS, Xbox and Nintendo. Assert the logical Cross patches are distinct: `Space`, `X`, `A`, and `B`.

- [ ] **Step 2: Run and verify failure**

Run: `cmake --build build-macos --target test_rsx_prompt_raster && ./build-macos/test_rsx_prompt_raster`

Expected: target/function missing.

- [ ] **Step 3: Implement deterministic rasterization**

Render rounded keycaps and circular controller buttons only inside manifest rectangles. Clip every primitive to its rectangle and alpha-composite over source BGRA. Use the approved mappings: Space/E/J/K, L-Shift/Q/L-Ctrl/R, Enter/Tab, WASD/arrows; PS X/O/□/△; Xbox A/B/X/Y; Nintendo B/A/Y/X.

- [ ] **Step 4: Integrate the Metal variant cache**

On a manifest match, preserve the original cache texture and build/reuse an immutable BGRA8 `MTLTexture` variant selected by `host_prompts_current()`. On every mismatch, allocation error or unsupported format bind the original `MetalTexCacheEnt.tex`.

- [ ] **Step 5: Run unit and runtime builds**

Run: `cmake --build build-macos --target test_rsx_prompt_texture test_rsx_prompt_raster ps3recomp_runtime -j4 && ctest --test-dir build-macos -R 'test_rsx_prompt_(texture|raster)' --output-on-failure`

Expected: all pass and `libps3recomp_runtime.a` links.

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt libs/input/rsx_prompt_texture.h libs/input/rsx_prompt_texture.c   libs/input/rsx_prompt_raster.h libs/input/rsx_prompt_raster.c   libs/input/tests/test_rsx_prompt_texture.c libs/input/tests/test_rsx_prompt_raster.c   libs/video/rsx_metal_backend.m
git commit -m "feat(metal): troca atlas nativo de prompts por dispositivo"
```

### Task 4: Run visual emulation for all four input families

**Files:**

- Create: `../gow2-recomp/claude_runs/run_prompt_variants.sh` (commit only in the GoW2 port repository if it contains no assets)
- Modify: `docs/re_sessions/2026-09-18-E468-reproducao-gameplay-claude.md`

**Interfaces:**

- Consumes SHA-validated `boot_gow2` and `PS3_HOST_PROMPTS=kb|ps|xbox|switch`.
- Produces local menu captures and `[PROMPTATLAS] variant=` rows.

- [ ] **Step 1: Write the failing harness assertion**

```bash
grep -q "\[PROMPTATLAS\].*variant=$family" "$log" || exit 1
test -s "$capture" || exit 1
```

- [ ] **Step 2: Verify it fails before Task 3**

Run: `bash ../gow2-recomp/claude_runs/run_prompt_variants.sh kb`

Expected: no native variant log before integration.

- [ ] **Step 3: Implement the isolated harness**

Copy a SHA-validated binary to `mktemp -d`, run it through `env -i` with one forced family, capture a 180–300-draw menu frame, wait for the matching variant row, then kill only that PID. Keep images under the temporary directory and never stage them.

- [ ] **Step 4: Run and inspect emulated variants**

```bash
for family in kb ps xbox switch; do
  bash ../gow2-recomp/claude_runs/run_prompt_variants.sh "$family"
done
```

Acceptance: the native `X = SELECT` location contains Space, PlayStation X, Xbox A and Nintendo B; equals/text remain guest pixels; no panel appears; each log identifies the same atlas and requested variant.

- [ ] **Step 5: Verify gameplay is untouched**

Run the gameplay reproduction with `PS3_HOST_PROMPTS=kb`. No prompt-atlas variant may be selected for an unrelated texture and no fixed screen glyph/panel may appear.

- [ ] **Step 6: Document results and commit**

Record hashes, metadata, frames, variants and observed visual results. Stage only script/docs, never images/assets.

```bash
git add docs/re_sessions/2026-09-18-E468-reproducao-gameplay-claude.md
git commit -m "docs(gow2): valida prompts nativos por plataforma"
```

