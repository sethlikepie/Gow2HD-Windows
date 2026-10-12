# GoW2 Native Autosave Design

## Goal

Provide one crash-safe autosave slot for the native GoW2 recompilation. The
slot is a private, atomic mirror of the most recent **successfully completed
game-owned CellSaveData save**, and can be selected by the launcher or from a
new `Resume Autosave` entry in the original game menu.

This is deliberately not an emulator savestate. It must never serialize host
threads, guest RAM, PPU/SPU registers, FIOS operations, RSX state, or decoder
state.

## Scope and behavior

- The runtime observes all successful `cellSaveData` save operations.
- Once an operation is complete, it atomically mirrors the resulting game save
  directory to one single autosave slot. A failed operation changes nothing.
- The launcher offers `Continue Autosave` and `Start Normally`. Continue is
  selected by default only when a valid autosave manifest exists.
- A normal process exit performs the same mirror operation only when there is a
  known completed native save. It never tries to make the guest save on exit.
- A game checkpoint or stage transition advances the autosave only when it
  causes the game to finish its own CellSaveData save.
- Selecting `Resume Autosave` in the original menu makes the CellSaveData load
  path read the autosave mirror. All game deserialization remains guest-owned.

## Non-goals

- No periodic timer.
- No invocation or simulation of the game save routine.
- No copying of a directory while callbacks/file writes are in flight.
- No replacement of the user's canonical game save.
- No support for multiple autosave history slots in this phase.
- No assumption that an old autosave is compatible with a different game build
  or title identity.

## Storage contract

The autosave is outside the normal game save directory:

```
<PS3_SAVEDATA_ROOT>/../ps3recomp-autosave/<title-id-or-save-dir>/
  manifest.json
  data/
```

`manifest.json` contains a format version, title identity, source save
directory name, capture timestamp, and an inventory of relative files with
size and SHA-256 digest. A restore candidate is valid only if its manifest is
well-formed, identity matches the current title, each listed file remains
within `data/`, and every size/digest matches.

Capture writes to a sibling temporary directory, fsyncs files/directories where
the platform supports it, writes the manifest last, then atomically replaces
the previous complete slot. A stale temporary directory is ignored and cleaned
only after validating that it is below the autosave root.

## Runtime interface

Add a narrow C interface, independent from game-specific generated code:

```c
typedef enum ps3_autosave_result {
    PS3_AUTOSAVE_OK = 0,
    PS3_AUTOSAVE_NOT_FOUND,
    PS3_AUTOSAVE_INVALID,
    PS3_AUTOSAVE_IO_ERROR,
} ps3_autosave_result;

ps3_autosave_result ps3_autosave_capture_completed_save(
    const char* title_id, const char* save_root, const char* save_dir);
ps3_autosave_result ps3_autosave_prepare_resume(
    const char* title_id, const char* save_root, char* out_save_dir,
    size_t out_save_dir_size);
int ps3_autosave_exists(const char* title_id, const char* save_root);
```

`cellSaveData` calls capture only after its successful completion path. A load
whose mode is `PS3_AUTOSAVE_RESUME=1` calls `prepare_resume`, which validates
the manifest and copies its data to a private restore directory; it then feeds
that directory to the already-existing CellSaveData load callbacks. A failed
validation falls back to normal game loading and reports a diagnostic.

The only game-specific responsibility is a gated, idempotent generated-code
hook that exposes an original-menu item and selects the environment-backed
resume mode. It is OFF by default until its exact guest menu construction and
selection anchors are measured in boot.

## Launcher interface

`gow2_launcher.py` receives a read-only `autosave_available()` helper and two
launch intents. `Continue Autosave` exports `PS3_AUTOSAVE_RESUME=1`; `Start
Normally` unsets it. The launcher never edits game save data itself.

## Error handling

- Source save missing, invalid path components, failed digest, or I/O failure:
  leave the previous autosave intact and return an error.
- An invalid/missing autosave does not block a normal game launch or normal
  CellSaveData load.
- The menu entry is disabled/hidden when no valid autosave is present.
- Logs identify capture, validation, and restore outcomes without exposing game
  contents.

## Testing and acceptance

1. Unit-test capture and manifest validation with a temporary save root.
2. Prove a valid capture survives a second capture failure unchanged.
3. Reject traversal, truncated manifests, wrong title identity, missing files,
   and digest mismatches.
4. Exercise the CellSaveData integration using the existing automatic-save
   harness: successful game save creates an autosave; failed guest callback
   does not.
5. Test launcher intent construction without launching a game.
6. In boot, observe the native game save complete, verify an autosave appears,
   choose `Resume Autosave` from the original menu, and observe the ordinary
   game load callback read the mirrored save.

## Platform constraints

- The core uses portable C/POSIX-style filesystem operations with `_WIN32`
  branches as already used by `cellSaveData.c`; macOS uses native POSIX APIs.
- No game assets or generated game files are committed.
- Any generated-code menu hook is represented by an idempotent patch script in
  `games/gow2/`, gated off by default, and committed separately from assets.
