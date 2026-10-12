# Host Input and Platform Prompts Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Make keyboard, Xbox, and Nintendo controllers drive the PS3 pad contract correctly and expose an automatic platform-specific prompt descriptor.

**Architecture:** Keep `cellPadGetData` as the guest-facing interface. Add a plain-C host input model for device identity, logical activity, and prompt labels; each backend reports normalized `PadHostState` plus device identity. The existing prompt selector consumes that identity and remains gated, while renderer integration is limited to an existing host overlay path and otherwise exposes a tested descriptor without altering game WAD textures.

**Tech Stack:** C17, Objective-C GameController on macOS, SDL2 on non-Apple desktop targets, XInput on Windows, CTest.

**Spec:** `docs/superpowers/specs/2026-09-18-host-input-prompts-design.md`

## Global Constraints

- Preserve the guest `CELL_PAD_CTRL_*` contract and DualShock default behavior.
- Device changes follow the last device that produced real input, not connection order.
- Probes and forced prompt modes remain off by default.
- Do not touch or commit game data or unrelated dirty files.
- macOS continues to prefer native GameController APIs.
- Tests distinguish unit/offline proof from in-boot device proof.

### Task 1: Define the host device and prompt descriptor contract

**Files:**
- Modify: `libs/input/host_prompts.h`
- Modify: `libs/input/host_prompts.c`
- Modify: `libs/input/tests/test_host_prompts.c`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produce `host_input_device` values for keyboard, PlayStation, Xbox, Nintendo, and unknown devices.
- Produce `host_prompt_view` with stable labels for Cross, Circle, Square, Triangle, Start, Select, shoulders, and stick clicks.
- Preserve `host_prompts_current()` and the `PS3_HOST_PROMPTS` gate for existing callers.

- [x] Add failing tests for device classification, prompt labels, and last-active selection.
- [x] Run the focused build and confirm the new tests fail before implementation.
- [x] Implement the enums, immutable prompt views, and activity helper in plain C.
- [x] Keep `host_prompts_classify(const char*)` as a compatibility wrapper returning the existing enum.
- [x] Run the focused test and then `ctest --test-dir build-macos -R test_host_prompts --output-on-failure`.

### Task 2: Make macOS GameController report accurate device identity and activity

**Files:**
- Modify: `libs/input/cellpad_gamecontroller.m`
- Modify: `libs/input/cellpad_internal.h`
- Modify: `libs/input/cellpad_gamecontroller.h`

**Interfaces:**
- Consume `GCController.productCategory` and controller name metadata.
- Produce `PadHostState.device` and `PadHostState.activity` while retaining the existing normalized PS3 button/axis fields.

- [x] Add a backend-independent test fixture for activity detection from neutral and non-neutral samples.
- [x] Run the fixture before implementation and confirm it fails for the new fields.
- [x] Add native category/name classification for DualShock/DualSense, Xbox, Switch/Joy-Con, and unknown controllers.
- [x] Mark a device active only when a button, trigger, or axis leaves the deadzone.
- [x] Keep the current keyboard/mouse mappings and reset mixed keyboard/controller state per poll.
- [x] Build the Objective-C input objects in the existing macOS target.

### Task 3: Extend SDL2 input with keyboard, identity, Nintendo mapping, and last-active prompts

**Files:**
- Modify: `libs/input/cellPad.c`
- Modify: `libs/input/cellpad_internal.h`
- Modify: `libs/input/host_prompts.c`
- Modify: `libs/input/tests/test_host_prompts.c`

**Interfaces:**
- Consume SDL2 controller names, mappings, keyboard state, and controller axes.
- Produce the same `PadHostState` logical PS3 values on Windows-forced-SDL2 and other non-Apple targets.

- [x] Add tests for Nintendo physical-label prompt selection and logical action mapping independent of SDL runtime state.
- [x] Run the focused tests and confirm the new mapping cases fail.
- [x] Use SDL controller name/mapping metadata to classify Switch Pro/Joy-Con before the Xbox fallback.
- [x] Add SDL keyboard WASD movement and arrow-key camera mapping.
- [x] Record last-active device only after real input, preserving neutral axis values and trigger thresholds.
- [x] Compile the input C source with the configured SDL backend.

### Task 4: Add Windows keyboard and non-XInput identification without regressing XInput

**Files:**
- Modify: `libs/input/cellPad.c`
- Modify: `libs/input/host_prompts.c`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Preserve XInput as the primary Xbox-compatible gameplay path.
- Use SDL2/controller metadata as an optional identity supplement for non-XInput Nintendo devices.

- [x] Add coverage for XInput fallback and Nintendo-name classification through the shared classifier tests.
- [x] Run the tests before changing the Windows path.
- [x] Ensure XInput falls back to Xbox when no stronger USB family is available.
- [x] Ensure keyboard input becomes active only while a mapped key is pressed.
- [ ] Verify the Windows branch with a Windows toolchain; this host cannot execute that target.

### Task 5: Connect prompt views to an existing host presentation hook

**Files:**
- Inspect and modify only the backend hook that already owns the target present/overlay lifecycle, if one exists: `libs/video/rsx_d3d12_backend.c`, `libs/video/rsx_metal_backend.m`, or their public headers.
- Add a focused prompt-view consumer test under `libs/input/tests/` if the selected hook is testable without a window.

**Interfaces:**
- Consume `host_prompts_current_view()`.
- Render only host prompt assets supplied by the backend; never rewrite guest WAD textures.

- [x] Confirm there is no existing generic prompt-capable overlay hook in the current backends.
- [x] Add a host-side procedural prompt compositor to the Metal present path.
- [ ] Add the equivalent D3D12 compositor and exercise it on Windows hardware.
- [x] Leave the renderer untouched and document the missing hook; the input and prompt contract remains complete and tested.
- [x] Keep prompt mode off by default and verify the configured runtime build remains clean.

### Task 6: Verification and handoff

**Files:**
- Modify: `docs/superpowers/specs/2026-09-18-host-input-prompts-design.md` only if verification findings require clarification.
- Add: `docs/re_sessions/2026-09-18-host-input-prompts.md` with measured results.

- [x] Run focused host prompt tests.
- [x] Run the runtime test suite relevant to `libs/input` and the configured macOS build.
- [ ] Run an in-boot smoke with DualShock/default behavior and record evidence.
- [ ] Run available keyboard/Xbox/Nintendo hardware or SDL identity tests and label unavailable hardware as untested.
- [x] Check `git diff --check` and the explicit file list before handoff.
