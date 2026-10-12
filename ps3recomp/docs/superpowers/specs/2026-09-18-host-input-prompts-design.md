# Host Input Detection and Platform Prompts

## Goal

Allow the recompilation to be controlled with a keyboard, Xbox controller, or Nintendo controller in addition to DualShock/DualSense, while keeping the guest's PS3 input contract unchanged. The visible button prompts should follow the device that most recently produced real input.

## Scope

The feature has four layers:

1. A host-device descriptor and prompt-set decision in plain C.
2. Platform backends that populate the descriptor and translate host controls to `CELL_PAD_CTRL_*` values.
3. Keyboard mappings on every supported desktop target, with mouse-look where the host API supports it.
4. A renderer-facing prompt provider that exposes platform-specific labels/icons without modifying guest WAD assets or claiming that an overlay is a game-pixel acceptance.

The existing PS3 pad API remains the only interface consumed by the guest. DualShock behavior and the current PS3 default remain unchanged when prompt integration is disabled or device identity is unavailable.

## Device model

The host layer will report:

- `HOST_INPUT_KEYBOARD`;
- `HOST_INPUT_PLAYSTATION`;
- `HOST_INPUT_XBOX`;
- `HOST_INPUT_NINTENDO`;
- `HOST_INPUT_UNKNOWN`.

Each active device has a prompt set and a stable logical mapping to PS3 actions. The prompt set is selected from the last device with an actual button, axis, trigger, or keyboard transition, not merely from a connected device. This prevents an idle controller from overriding the keyboard in a mixed setup.

## Backend policy

- macOS keeps the native GameController backend and uses its product/category metadata for DualShock, Xbox, Switch, and unknown controllers.
- Windows keeps XInput for Xbox-compatible devices and adds a device-name/SDL2 path for identifying non-XInput Nintendo devices and keyboard input. XInput remains the fallback for reliable Xbox gameplay.
- Other desktop targets use SDL2 GameController plus SDL keyboard events.

All backends normalize stick direction, trigger ranges, dead zones, and face-button semantics into the existing PS3 representation. Nintendo's physical A/B and X/Y labels are preserved in the prompt set while the logical action mapping remains correct for the player.

## Prompt integration

The prompt service will expose a small immutable view containing the current set and labels for Cross, Circle, Square, Triangle, Start, Select, shoulders, and stick clicks. The renderer/backend consumes that view when drawing host prompts. The original title textures are not rewritten. If no prompt renderer is available on a given backend, the service remains harmless and the original DualShock prompts remain visible.

Prompt modes remain gated:

- unset or `0`: no host prompt overlay;
- `auto`: follow the last active device;
- explicit `ps`, `xbox`, `switch`, or `kb`: force a set for testing and accessibility.

The default remains off until the renderer integration is proven on the target build.

## Verification

Unit tests will cover device-name classification, logical button translation, analog/trigger normalization, last-active-device selection, forced prompt modes, and unknown-device fallback. Build verification will cover the macOS native target and the Windows/non-Apple input compilation paths where toolchains are available. End-to-end acceptance will distinguish:

- offline/unit proof of mappings and detection;
- in-boot proof that a physical device controls the game;
- in-boot proof that the displayed prompt set changes after real input.

No test will use game data as a repository artifact, and no diagnostic override will be enabled by default.
