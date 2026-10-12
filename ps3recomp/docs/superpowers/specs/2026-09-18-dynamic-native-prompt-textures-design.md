# Dynamic Native Prompt Meshes — Design

## Status and correction

**Status (2026-09-18, in-boot, real captured frames):**

| Screen | Keyboard | Xbox | Switch |
| --- | --- | --- | --- |
| Title / "no save data" (Cross) | `⎵` on a wide keycap | `A` | `B` |
| Difficulty / pause (Triangle = Back) | `K` on a square keycap | not captured | not captured |
| Gameplay tutorial (Square/Triangle/Circle) | `J` / `K` / `E` on square keycaps | `X` / `Y` / `B` | `Y` / `X` / `A` |

PlayStation keeps the native glyphs. The round backing is shared by every
face, so its keycap shape is decided by the glyph drawn right after it (wide:
Space/Shift; square: letters); controllers keep the round button.

Identity: prompt material fp `59864B76` + index count + VA stride + aspect +
**scale-free** shape signature. The VP is not part of it (it changes per
screen: `03BEAB3D` title, `D24D7CF2`/`4A7AD35D` "no save data", `E153EBB9`
gameplay). Circle (`2DBB2FF8`, 288 idx), Square (`9B902E0D`, 36 idx), the
gameplay Triangle (`17093045`) and a backing variant (`4CBDFBBD`) were found
with `PS3_TRACE_PROMPT_SHAPES=2` and identified by rendering the probe's vertex
dump. `PS3_PROMPT_DUMP_ON_MATCH=1` captures the frame 20 frames after each face
is first replaced.

Still open: L1/R1/L2/R2, D-pad and stick prompts (no screen reached so far
drew them), and auto-switch with a physically connected controller. The auto
path is covered by a unit test, an in-boot simulated device
(`PS3_HOST_PROMPTS_SIM="Switch Pro Controller@62"` -> `[PROMPT-SIM] set=2`) and
the user's real keyboard session.

The original premise of this document was wrong: the first prompt cannot be
replaced reliably by patching a texture atlas. The inspected GoW2 UI asset stores
the PlayStation face symbols as **solid FLP mesh parts**, not as TXR pixels. This
document retains the word “textures” in its file name for continuity, but the
implementation must follow the mesh/draw design below.

The feature is a visual mod of the prompts the game itself displays. It must not
draw a generic input panel, a black bar, or a post-present/full-screen overlay.

## Product goal

God of War II continues to own UI layout, animation, layering, blending, text,
and all non-prompt art. At each native prompt location, the visible glyph adapts
to the device that last produced real input:

| Input family | Examples at original PS3 logical locations |
| --- | --- |
| PlayStation / DualShock / DualSense | `X`, `O`, `□`, `△`; `L1/R1/L2/R2`; Start/Select |
| Xbox | `A`, `B`, `X`, `Y`; `LB/RB/LT/RT`; Menu/View |
| Nintendo / Switch | `B`, `A`, `Y`, `X`; `L/R/ZL/ZR`; `+/-` |
| Keyboard | compact keycaps: `SPACE`, `E`, `J`, `K`, `L-SHIFT`, `Q`, `L-CTRL`, `R`, `ENTER`, `TAB`, `WASD`, and arrow keys |

The desired visual language is the user-provided example: a small, legible keycap
or console button at **each action’s existing in-game location**, not a list of
all bindings elsewhere on screen. A keyboard `SPACE` replaces the menu’s visible
Cross glyph in `X = SELECT`; the original `= SELECT`, background, and menu art
remain untouched.

The first acceptance case is the lower-left confirmation prompt in the main menu.
The completed system also covers face buttons, D-pad, shoulders/triggers,
Start/Select, left/right sticks, movement, camera, and context/action prompts.

## Asset evidence (read-only investigation)

No game asset was changed, extracted into the repository, or committed.

The permanent GoW2 WAD used by the running Mac build is outside the repository:

```text
../gow2-recomp/movie_cache/R_PermA.wad_ps3
```

`tools/wad_dump.py --toc` shows permanent sections including `GFXX_R_Perm`,
`TXRX_R_Perm`, and `FLPX_R_Perm`. The WAD was opened read-only through
[mogaika/god_of_war_browser](https://github.com/mogaika/god_of_war_browser), using
its PS3 / GoW2 parsing mode. The browser is useful here because it exposes FLP
animation and mesh metadata, not merely raster textures.

The browser reports the relevant permanent UI resource as `FLP_HUDA` (resource
id `2972` in this inspected R_PERM/WAD view; that browser-local id is diagnostic
metadata, not a production identity). Its named elements include:

| FLP element | Element id | Mesh-part reference | Mesh-part index | Texture name |
| --- | ---: | ---: | ---: | --- |
| `XButton` | 433 | 262 | 395 | empty |
| `TriangleButton` | 434 | 263 | 396 | empty |
| `CircleButton` | 435 | 264 | 397 | empty |
| `SquareButton` | 436 | 265 | 398 | empty |
| `L1Button`, `L2Button`, `R1Button`, `R2Button` | named FLP elements | varies | varies | mixed/vector-dependent |
| `DpadButton`, `Joystick`, `JoystickL`, `JoystickR` | named FLP elements | varies | varies | mixed/vector-dependent |

The empty texture names on the four face-button mesh parts are decisive evidence:
the visible `X` in the menu is vector geometry. Therefore a TXR hash/atlas-only
solution cannot change the actual Cross glyph shown in the user’s screenshot.

## Visual evidence and controlled runs

### Stable menu capture

The native Metal path reaches the real GoW2 title menu. A frame dump from the
running build shows the original lower-left `X = SELECT` exactly as expected.
The diagnostic run uses the normal game renderer and is only held at the title by
the gated test setting below:

```text
PS3_PAD_AUTOSTART_HOLD_TITLE=1
```

This setting is OFF by default. It keeps the virtual bring-up controller neutral
after the title is ready so it does not press Cross and enter New Game during a
visual test. It does not alter real keyboard or controller input.

### Native draw probe

`PS3_TRACE_PROMPT_MESH=1` is an OFF-by-default Metal diagnostic. It records a
single native frame and reports for each draw:

- frame/draw/geometry identifiers;
- index count and primitive;
- a stable FNV-style geometry/attribute signature;
- bounding box from the actual vertex-attribute buffer used by the vertex shader;
- render target, texture address, shader hashes, viewport/scissor, and attribute layout.

The first version incorrectly inspected the simplified `MetalBasicVertex` buffer;
the menu’s decompiled vertex shader reads `va_buf`, so that buffer contained zeros.
The probe was corrected to inspect `va_buf`. This is an important constraint for
the final replacement: altering only the fallback vertex buffer will not change
the native FLP prompt.

The held real main-menu capture established three adjacent pieces of the prompt:

| Trace geometry | Indexed vertices | Observed result | Role |
| --- | ---: | --- | --- |
| `220` | 90 | suppression leaves the Cross visible | outer ring/shadow component |
| `221` | 90 | paired with `220` | outer ring/shadow component |
| `222` | 30 | suppression removes only the Cross stroke | accepted `XButton` glyph |

The accepted glyph draw has fragment hash `59864B76`, vertex hash `03BEAB3D`,
decoded VA stride `1`, trace geometry signature `EA1F51A3`, and local attribute
bounds approximately `x=14.625..46.812`, `y=-39..-7`. Its source material and
the nearby 90-index ring draws are retained; only the 30-index glyph is the
replacement slot.

A run with `PS3_METAL_SKIP_DRAW=222` and
`PS3_PAD_AUTOSTART_HOLD_TITLE=1` produced a real frame in which the lower-left
circle, `= SELECT`, menu text, and background remained intact while the visible
Cross disappeared. This is **in-boot visual proof**, not merely a log inference.
The trace geometry number is useful discovery evidence only: it is frame-local
and must not be the shipping matcher by itself. The implementation must retain
the complete material/topology/VA fingerprint above and add semantic provenance
from `FLP_HUDA` before it rewrites a draw.

## Rejected approaches

The following are explicitly rejected and must not ship:

1. A full-screen or post-present prompt overlay.
2. A black/translucent bottom panel or a list of bindings unrelated to the
   game’s own prompt positions.
3. Fixed screen coordinates as the product implementation.
4. Substituting a vaguely similar texture by guest address alone.
5. Writing WAD, PSARC, EBOOT, guest VM, or copyrighted assets on disk.
6. Declaring success from a draw count, a texture dump, or a synthetic demo.

The early host overlay prototype is useful only as an isolated keycap rasterizer
reference. Its screen-space presentation path is not part of this feature.

## Correct architecture

```text
R_PermA.wad_ps3 (read-only)
  -> FLP_HUDA semantic inventory
       XButton / CircleButton / SquareButton / TriangleButton / L1... / sticks
  -> native RSX draw fingerprint discovery (gated; one semantic at a time)
  -> guarded prompt-draw resolver
       exact FLP-compatible mesh + shader + topology signature?
         no  -> replay original guest draw byte-for-byte
         yes -> select host_prompts_current()
                -> replay native prompt slot with that family’s button/keycap mesh
                -> retain game transform, layer, scissor, blend, and surrounding UI
```

The renderer-side resolver belongs in the Metal native draw/replay path, not in
post-present composition and not solely in `metal_bind_texture`. It may use a
small runtime-generated texture for a keycap face, but its placement and clipping
must be driven by the matched native FLP draw. A vector glyph/keycap mesh is
preferred for the face-button elements because their originals are vectors.

### Match rule

No individual condition is enough. A native prompt draw is matched only when all
available guards agree:

1. semantic asset provenance from the FLP inventory;
2. primitive/topology and indexed count;
3. canonical vertex-attribute geometry signature;
4. vertex and fragment shader fingerprints;
5. expected render target, viewport/scissor, and material/texture state;
6. optional stable transform relationship for the currently animated FLP element.

Guest EA, a frame-local draw ordinal, or a single shader hash alone are not valid
identities. Any mismatch or ambiguity is a no-op: replay the original game draw.

### Dynamic device selection

`host_prompts_current()` is the shared source of truth. It already supports:

```text
PS3_HOST_PROMPTS=auto   # default: last real keyboard/controller activity
PS3_HOST_PROMPTS=ps
PS3_HOST_PROMPTS=xbox
PS3_HOST_PROMPTS=switch
PS3_HOST_PROMPTS=kb
PS3_HOST_PROMPTS=0      # disable all replacement
```

The input layer recognizes PlayStation, Xbox, Nintendo/Switch and keyboard
product categories. A family change must be visible on the next applicable
native prompt draw without reloading the WAD, restarting the game, or changing
guest memory.

### Keycap/button art contract

Each replacement sprite/mesh is a standalone prompt, sized to the source
element’s native bounds and transformed by the source element’s existing UI
transform. It contains no unrelated text and no backing strip.

For keyboard prompts, use a compact rounded keycap with a subtle dark inset and
high-contrast label, in the style of the supplied reference image. For controller
prompts, use the platform’s familiar button/shoulder outline and logical action
at the original PS3 location. The label is never a replacement for the action:
Xbox `A` occupies the original Cross action position, Switch `B` occupies that
same Cross action position, and keyboard `SPACE` occupies it for confirm.

The approved initial keyboard mapping is:

| Logical PS3 action | Keyboard keycap |
| --- | --- |
| Cross / Circle / Square / Triangle | `SPACE` / `E` / `J` / `K` |
| L1 / R1 / L2 / R2 | `L-SHIFT` / `Q` / `L-CTRL` / `R` |
| Start / Select | `ENTER` / `TAB` |
| D-pad | arrow keys |
| left stick / movement | `WASD` |
| right stick / camera | mouse |
| L3 / R3 | `F` / `G` (R3 also middle mouse) |

## Implementation phases

### Phase 1 — establish a reliable native X target

**Complete as discovery evidence.** The held-menu trace and the `222` A/B above
identify the Cross stroke without conflating it with the ring/shadow. Before
shipping, persist the identity as a guarded fingerprint rather than the trace
ordinal.

### Phase 2 — replace the accepted X target

1. **Complete offline:** implement a pure prompt-mesh builder with no Apple
   frameworks. `test_host_prompt_mesh` proves that PlayStation preserves its
   source mesh and that keyboard, Xbox, and Switch replacements have bounded
   normalized geometry. This does not prove visibility in game.
2. **Done (in-boot):** `metal_prompt_cross_replace` activates only for the
   complete fingerprint (indexed, 30 indices, triangles, fp `59864B76`,
   vp `03BEAB3D`, VA stride 1, 32x32 local bounds, normalized shape signature
   `7DD29075`). The replacement is written into a fresh VA and IB ring range,
   so the guest indices are never overwritten; meshes up to 256 vertices.
3. Replay a `SPACE` keycap for keyboard and `A` / `B` / Cross variants for Xbox /
   Switch / PlayStation in the same native draw position. The keyboard variant
   must include a legible `SPACE` keycap treatment, not merely a generic bar.
4. Confirm the original Cross mesh is not drawn underneath the replacement.
5. **Done for kb/xbox/switch:** one real menu frame each. The `SPACE` label is
   a 3x5 cell font inside the 32-unit glyph bounds: readable at 1440p but small.
   The first Switch `B` lacked its lower bowl and read as `E`; fixed and
   re-captured.

### Phase 3 — semantic coverage

Repeat the Phase 1 evidence chain for each FLP element family, beginning with
the named `TriangleButton`, `CircleButton`, `SquareButton`, `L1Button`,
`R1Button`, `L2Button`, `R2Button`, `DpadButton`, and joystick elements. Add
Start/Select and action-specific gameplay prompts as their FLP/draw sources are
observed. Do not extrapolate a face-button fingerprint to shoulders or sticks.

### Phase 4 — auto-switch and gameplay validation

Use real keyboard, PlayStation, Xbox, and Nintendo activity where attached.
Forced settings are valid coverage of rendering families; they are not proof of
automatic input detection. Verify that non-prompt HUD, scenery, cutscenes, and
gameplay geometry remain unchanged.

## Tests and acceptance matrix

### Unit tests (no game assets)

- Device classification and `host_prompts_current()` selection.
- Logical action -> platform label/keycap mapping for every family.
- Prompt mesh builder: each label has valid geometry/UVs and stays within its
  requested source bounds. `test_host_prompt_mesh` currently covers the first
  Cross-action variants offline.
- Guard matcher: exact signature matches; any changed topology, hash, shader,
  or material causes a no-op.
- Variant-cache behavior: a family switch changes only the prompt variant and
  never mutates cached guest/VM data.
- `PS3_PAD_AUTOSTART_HOLD_TITLE` helper: pre-title navigation remains intact and
  the virtual pad is neutral after title readiness. This test already passes.

### In-boot acceptance

| Case | Required visual proof |
| --- | --- |
| Keyboard menu | `SPACE = SELECT` keycap replaces only the original Cross |
| PlayStation menu | native-style Cross remains in the source location |
| Xbox menu | `A = SELECT` in that same location |
| Nintendo menu | `B = SELECT` in that same location |
| Device switch | the next prompt draw updates without restart |
| Face/shoulder/D-pad/stick prompt | each tested semantic source changes only its own native glyph |
| Non-prompt UI/gameplay | unchanged screenshot and no unexpected substitution log |

Every visual acceptance must be an actual captured game frame. A unit test,
debug draw, raw WAD image, or log line alone does not prove the mod is visible.

The only completed visual A/B at this point is the **negative control** for the
native Cross: suppressing draw `222` removes the Cross and nothing else in the
main-menu prompt. It proves the source slot, not a finished keyboard/Xbox/Switch
replacement. The four positive rows above remain required before this feature is
reported as implemented.

## Safety and repository rules

- Diagnostics are environment-gated and OFF by default.
- Game binaries, WADs, PSARCs, movies, extracted content, and captured images are
  never committed.
- The initial implementation target is Metal/macOS. Shared device/semantic logic
  must remain platform-neutral so D3D12 can consume it later.
- Windows behavior must not regress under Apple-specific code.
- Any failed guard falls back to the untouched native game draw.
- Results are reported as in-boot, offline/unit, or unexercised; never inferred.
