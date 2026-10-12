# GoW2 Overlay — Console Shell (PS5-style control center)

Status: approved in brainstorming 2026-09-23 (user). Supersedes the visual layer of
`2026-09-23-gow2-runtime-overlay-design.md`; that spec's plumbing (settings, input capture,
immutable draw packet, metrics/provider split, save-on-change, menu starts closed) stays binding.

## Goal

Replace the debug-tool look of the runtime overlay (stock Dear ImGui tabs on a fixed 1280x720
canvas) with a premium, console-grade control center modelled on the PlayStation 5 control
center: live-blurred game behind, a bottom row of cards, a floating panel above the focused card,
system typography and icons, spring motion, UI sounds and haptics. Success: the player opens it
with the PS button and it feels like a console, not a debugger.

## Decisions (from the user)

| Topic | Decision |
|---|---|
| Reference | PS5 control center (layout B: card row + floating panel above the focused card) |
| Platform | Mac first (full visual incl. live blur on Metal). Windows keeps working with the same layout and navigation, dim-only background, no blur (phase 2). No Windows regression. |
| Content | Player-first cards; diagnostics behind a "Desenvolvedor" card |
| Open/close | Controller PS/Home button (primary); Select+Start chord and keyboard F1 remain |
| Feel | Spring animations + synthesized UI sounds + light haptic ticks, each with an off switch |
| Approach | Keep Dear ImGui underneath only as geometry/input engine; all visible widgets custom-drawn |

Assumptions accepted by the user: the game keeps running behind the open shell (input withheld,
as today); UI sounds are synthesized by us (no Sony assets); on macOS text uses the system SF Pro
and icons use SF Symbols, rasterized by the OS at runtime; closed shell ⇒ no behaviour change.

## Visual language

- **Backdrop:** on open, the game frame is Gaussian-blurred (radius ≈ 24 px at drawable
  resolution) and dimmed ≈ 35 %, faded in over 180 ms, updated live every frame while open.
- **Palette:** the God of War palette of the launcher (`games/gow2/launcher/macos/Theme.swift`),
  kept as named constants in one place (`rsx_overlay_shell_draw.cpp`): stone `#0E0D0C` /
  `#171513` / `#221F1C` / `#2E2A26`, parchment `#EDE6DC`, ash `#A89F93`, blood `#9B1B12` /
  `#B8261A`, gold `#C9A15A` / `#E3C27E`, amber `#D0A23C`.
- **Bottom band:** vertical gradient transparent → ≈ 92 % stone-black (`#0E0D0C`) holding the card row.
- **Cards:** rounded squares (radius ≈ 14 pt), translucent warm stone glass (`#2E2A26`), SF Symbol
  icon + parchment label below. Focused card scales ≈ 1.18 with a spring, 2 pt gold border and soft
  gold glow; unfocused cards at ≈ 75 % opacity. "Sair" sits alone at the right end.
- **Floating panel:** warm stone glass (`#171513`, ≈ 86 %) with drop shadow, anchored above the
  focused card; rows are label left / control right; the focused row has a soft gold rounded
  highlight; switches ON use blood `#B8261A` (slider fill `#9B1B12`), OFF a warm stone track; titles
  and primary text parchment, secondary text ash; error logs a lightened blood, warnings amber.
- **Top strip:** parchment text; left "God of War II HD"; right local time, controller glyph and battery level when
  the controller reports it (hidden otherwise — never a fake value).
- **Typography:** SF Pro — panel title 17 pt semibold, rows 15 pt regular, card labels 11 pt medium.
- **Icons:** SF Symbols `play.fill`, `display`, `gamecontroller`, `speaker.wave.2`, `hammer`, `power`.
- **Motion:** open = row rises with a spring (≈ 220 ms); focus moves with a spring; panel content
  cross-fades 120 ms; close reverses open.
- **Resolution:** everything is laid out and rasterized at the real drawable size and backing
  scale (Retina). The fixed 1280x720 logical canvas is removed.

## Cards and panels

1. **Voltar ao jogo** — panel shows "Voltar ao jogo" and the button hint; confirm closes.
2. **Tela** — Tela cheia (switch), VSync (switch), Tamanho da janela (list: 1280×720, 1600×900,
   1920×1080, 2560×1440). Rows the backend cannot apply live show "requer reiniciar".
3. **Controles** — Zona morta do analógico (slider 0–30 %), Vibração (switch; gates game rumble),
   Remapear botões (sub-list "botão do controle → botão do PS3", new binding captured by pressing
   it), Restaurar padrão.
4. **Som** — Som do jogo (on/off), Sons da interface (switch), Vibração da interface (switch).
   No game-volume slider in this delivery.
5. **Desenvolvedor** — wider two-column panel: FPS and frame time, guest draws, WAD state, shader
   hits/misses/compiles/failures, and a "Logs" row opening the newest-first log list. Unmeasured
   values render "indisponível".
6. **Sair** — confirmation dialog "Sair do God of War II HD?" (Sair / Cancelar). Settings are
   already persisted on change.

Navigation: left/right = cards; down = into panel; up/down = rows; Cross/Enter = confirm;
Circle/Esc = back one level, and at card level closes; PS/Home, Select+Start and F1 close from
anywhere. The shell always opens focused on "Voltar ao jogo".

## Architecture

Unchanged and reused: input capture/withholding, settings persistence (save-on-change),
event-thread UI build, immutable draw packet consumed by renderers, backend-metrics / game-provider
split, renderer-failure disable path.

1. **Shell module — `libs/video/rsx_overlay_shell.{h,cpp}` (new).** Owns the state machine
   (closed → opening → open → closing), its own focus model (card / row / sub-list / dialog),
   spring animation (deterministic, driven by elapsed time passed in), and draws every element
   with ImDrawList primitives (rounded rects, shadows and glow as gradient quads). ImGui's stock
   widgets and nav are not used for the shell. `rsx_overlay.cpp` calls the shell instead of tabs.
   Pure logic (state machine, focus, springs, layout math) is separable from drawing for tests.
2. **Draw packet v2.** Adds drawable width/height, backing scale, and backdrop fields
   `backdrop_blur` (0/1), `backdrop_dim` (0..1) and `open_progress` (0..1). Clip rects are in
   drawable pixels. This is the ABI extension anticipated by the Task 4 ruling; D3D12 consumes the
   new size fields and ignores blur.
3. **Platform assets — `libs/video/rsx_overlay_assets.h`.**
   - macOS (`rsx_overlay_assets_apple.m`): rasterizes SF Pro through CoreText and SF Symbols
     through AppKit into an atlas at the current backing scale, at init on the UI thread;
     regenerated when the window's backing scale changes.
   - Windows/other: embedded Inter (SIL OFL, license shipped in `third_party/`) and simple
     geometric icon shapes.
4. **Metal backdrop blur (`rsx_overlay_metal.m`).** Only while `open_progress > 0`: before the UI
   pass, copy the presented drawable (the layer already sets `framebufferOnly = NO`,
   `rsx_metal_backend.m:~11142`) into a half-resolution texture, run `MPSImageGaussianBlur`,
   composite full-screen with dim proportional to `open_progress`, then draw the UI. Zero cost
   when closed. Must respect the existing single-drawable / decode-thread present rules.
5. **PS/Home button (`libs/input/cellpad_gamecontroller.m`).** Read `buttonHome` of the extended
   gamepad; request exclusive use while focused (`preferredSystemGestureState = .disabled` on the
   home button when available). Exposed as a host-only bit that toggles the shell and never
   reaches the guest.
6. **UI sounds and haptics.** Short clips (open, move, confirm, back) synthesized in code at init
   and played through a dedicated `AVAudioEngine` independent of game audio; haptic transient
   through the existing asynchronous haptics queue. Both never block the game or UI thread.
7. **Settings v2 (`rsx_overlay_settings`).** New fields `ui_sounds`, `ui_haptics`, `game_mute`,
   `vibration`. Version-1 files load with defaults for new fields; invalid values fall back per field.
8. **New consumers.** `game_mute` sets a runtime flag `cellAudio` reads per block (today mute is
   env-only via `ps3_audio_muted()`); `vibration` gates guest rumble.

Isolation: closed shell adds no per-frame cost beyond today's, and input/render are unchanged.

## Error handling

- Blur unavailable (drawable unreadable, MPS failure): dim-only backdrop plus one log line.
- SF Pro / SF Symbol unavailable: fall back to embedded Inter and geometric icons.
- PS/Home not available or not grantable: Select+Start and F1 still work.
- UI audio engine fails to start: shell is silent; game audio unaffected.
- Renderer failure: core disables itself (existing behaviour) — never an invisible input trap.

## Testing

Offline (no game launch):
- Shell state machine and focus: open/close, card moves, enter/leave panel and sub-list, remap
  capture, Circle/Esc per level, dialog.
- Springs/animation: deterministic values for given elapsed times.
- Packet v2: real size, backing scale, backdrop fields; clip rects in drawable pixels.
- Settings v2: v1 file upgrades with defaults; invalid values fall back.
- Headless Metal: render the shell into an offscreen texture at 2x — assert blur/dim applied,
  rounded corners, text present — with Metal API validation on.
- Pad: PS/Home bit toggles the shell and never reaches the guest; Select+Start/F1 unchanged.
- cellAudio: `game_mute` flag zeroes the mixed block.

In-boot: a pt-BR UAT checklist for the user (PS button, Select+Start, F1, visuals, blur, sounds,
haptics, each panel, quit confirmation, game intact with the shell closed).

## Constraints

- Implementation starts from `feat/gow2-runtime-overlay` brought up to date with
  `macos-arm64-port-f0-f2` (it is ≥ 28 commits behind, including audio and pad changes this work
  touches); resolve that merge before Task 1.
- Project rules apply: never forge results; probes gated OFF; commits pt-BR without co-authorship;
  never stage game data; D3D12/Win32 code out of the macOS target; Metal code out of Windows.
- Overlay visibility is never evidence of natural game pixels.

## Out of scope (this delivery)

Windows backdrop blur (phase 2), game-volume slider, FPS counter outside the menu, trophies/
notifications/any PS5 feature beyond the control-center shell.
