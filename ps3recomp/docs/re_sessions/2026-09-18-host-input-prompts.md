# Host input and platform prompts — 2026-09-18

## Implemented

- Added a plain-C host input classification contract for keyboard, PlayStation, Xbox, Nintendo, and unknown devices.
- Added immutable prompt views with text labels and icon IDs for face buttons, Start/Select, D-pad, shoulders, triggers, stick clicks, and both sticks.
- Preserved `PS3_HOST_PROMPTS=0` as the default-off gate and kept `auto`, `ps`, `xbox`, `switch`, and `kb` forced modes.
- Added activity detection using button state, 0..255 stick deadzones, and trigger thresholds so an idle connected device does not steal prompt focus.
- Kept the native macOS GameController path and fixed keyboard state reset between polls.
- Added SDL keyboard movement (`WASD`) and camera (`arrow keys`) mapping, plus controller-name classification.
- Added Windows keyboard mapping through `GetAsyncKeyState` and USB VID-family detection as a supplement to XInput. Nintendo VID `057E` and PlayStation VID `054C` are recognized when the raw HID family is unambiguous enough for the XInput fallback.

## Verification

- `test_host_prompts`: PASS.
- `test_pad_autostart`: PASS.
- `cmake --build build-macos --target ps3recomp_runtime test_host_prompts -j4`: PASS.
- Full configured `build-macos`: PASS/no work remaining after the incremental build.
- SDL2-forced compile of `libs/input/cellPad.c`: PASS.
- Physical controller and in-boot keyboard/Xbox/Nintendo runs: not exercised in this session.
- Generated visual base atlas: `assets/input/host_prompt_atlas_base.png` (transparent RGBA, inspected after generation).

## Deliberate limitation

The guest WAD textures remain untouched. The host-side visual mod now has a procedural transparent prompt frame and the Metal present path composites it after the final game frame when prompts are enabled. The generated base atlas is retained as an art asset for future texture-backed rendering. D3D12 still needs an equivalent compositor before the same visual replacement is available on Windows.
