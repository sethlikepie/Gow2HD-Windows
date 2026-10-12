# GoW2 Native Autosave Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add one durable autosave slot that mirrors the last successfully completed native GoW2 save, offers launcher continuation, and exposes a gated original-menu resume action.

**Architecture:** A new portable runtime module owns autosave paths, manifest validation, hashing, atomic directory replacement, and restore staging. `cellSaveData` invokes it only after an existing save completes successfully and redirects a normal AutoLoad to a validated staged mirror only when `PS3_AUTOSAVE_RESUME=1`. The GoW2 launcher chooses that environment mode; a separately measured, gated guest patch requests it from the original menu.

**Tech Stack:** C11/POSIX filesystem APIs with `_WIN32` branches; existing CMake/CTest; Python 3/Tkinter launcher; idempotent GoW2 generated-code patch scripts.

**Spec:** `docs/superpowers/specs/2026-09-21-gow2-native-autosave-design.md`

## Global Constraints

- The feature saves only normal CellSaveData files, never VM, CPU, SPU, FIOS, VDEC, or RSX state.
- Capture runs only after a `CELL_OK` save completion; failures preserve the last valid autosave.
- One slot only; no timer; no simulated guest save calls.
- Autosave input/output paths reject absolute paths, `.`/`..`, and separators in save-directory names.
- Manifest validation verifies title identity, relative paths, file sizes, and SHA-256 before any restore.
- Capture uses a sibling temporary directory and atomic replacement; the normal save directory is read-only to this feature.
- macOS uses native POSIX operations; Windows support remains behind `_WIN32` without regressing the existing runtime.
- Generated GoW2 code remains untracked; every guest hook is represented by an idempotent committed patch script, gated OFF by default.
- Never add game assets or use `git add -A` / `git add .`; commits are pt-BR and list files explicitly.

---

### Task 1: Implement and unit-test the isolated autosave store

**Files:**
- Create: `libs/system/ps3_autosave.h`
- Create: `libs/system/ps3_autosave.c`
- Create: `libs/system/tests/test_ps3_autosave.c`
- Modify: `CMakeLists.txt:414-445`

**Interfaces:**
- Produces `ps3_autosave_capture_completed_save(title_id, save_root, save_dir)` returning `PS3_AUTOSAVE_OK`, `PS3_AUTOSAVE_NOT_FOUND`, `PS3_AUTOSAVE_INVALID`, or `PS3_AUTOSAVE_IO_ERROR`.
- Produces `ps3_autosave_prepare_resume(title_id, save_root, save_dir, out_path, out_path_size)`; on success `out_path` is a private staged directory containing the validated mirrored save.
- Produces `ps3_autosave_exists(title_id, save_root)` and `ps3_autosave_capture_last_completed()` for normal shutdown.
- Consumes only host paths passed by `cellSaveData`; it must not include guest-memory headers or call guest code.

- [ ] **Step 1: Write failing tests for valid capture and restore staging**

Create a temporary `savedata/GOW2SAVE/` directory containing `PARAM.SFO` and a binary payload. Assert capture succeeds, `ps3_autosave_exists` is true, `prepare_resume` succeeds, and the staged files byte-compare equal to the source.

```c
assert(ps3_autosave_capture_completed_save("NPUA80491", root, "GOW2SAVE") == PS3_AUTOSAVE_OK);
assert(ps3_autosave_exists("NPUA80491", root));
assert(ps3_autosave_prepare_resume("NPUA80491", root, "GOW2SAVE", staged, sizeof(staged)) == PS3_AUTOSAVE_OK);
assert_file_equal(source_param, join(staged, "PARAM.SFO"));
assert_file_equal(source_payload, join(staged, "DATA.BIN"));
```

- [ ] **Step 2: Run the focused test and verify it fails because the API is absent**

Run: `cmake -S . -B build-autosave -G Ninja -DBUILD_TESTING=ON && cmake --build build-autosave --target test_ps3_autosave && ctest --test-dir build-autosave -R '^test_ps3_autosave$' --output-on-failure`

Expected: compilation failure mentioning `ps3_autosave_capture_completed_save` or a missing header.

- [ ] **Step 3: Add the public contract and minimal capture implementation**

Define the result enum and functions in `ps3_autosave.h`. In `ps3_autosave.c`, validate `save_dir`, recursively enumerate regular files without following symlinks, hash each file with an in-tree SHA-256 implementation or a small local implementation, write `manifest.json` after all data files to `<autosave-root>/.tmp-<pid>/`, fsync, and rename it over `<autosave-root>/current/` only after a complete capture.

- [ ] **Step 4: Add restore validation**

Parse only the manifest format written by this module. Reject malformed JSON, wrong version/title/source directory, duplicate paths, separators or traversal, unexpected file size, and digest mismatches. Copy verified data to `<autosave-root>/.restore-<pid>/`; return its path only after its files are complete.

- [ ] **Step 5: Run the focused test and verify it passes**

Run: `cmake --build build-autosave --target test_ps3_autosave && ctest --test-dir build-autosave -R '^test_ps3_autosave$' --output-on-failure`

Expected: one passing `test_ps3_autosave` test.

- [ ] **Step 6: Commit the isolated store**

```bash
git add libs/system/ps3_autosave.h libs/system/ps3_autosave.c libs/system/tests/test_ps3_autosave.c CMakeLists.txt
git commit -m "feat: adiciona armazenamento de autosave nativo"
```

### Task 2: Prove failure preservation and hostile-input rejection

**Files:**
- Modify: `libs/system/tests/test_ps3_autosave.c`
- Modify: `libs/system/ps3_autosave.c`

**Interfaces:**
- Consumes the Task 1 API.
- Produces an invariant: any capture/restore error leaves the previous valid `current/` slot readable.

- [ ] **Step 1: Write failing preservation and validation tests**

Capture payload `A`, mutate the live save to `B`, make the capture destination deliberately fail (read-only parent or injected test filesystem failure), then assert staging still returns payload `A`. Add independent tests for `../escape`, a forged title ID, a missing listed file, and a one-byte payload change after capture.

```c
assert(ps3_autosave_capture_completed_save("NPUA80491", root, "GOW2SAVE") == PS3_AUTOSAVE_OK);
make_capture_destination_fail(root);
write_file(live_payload, "B", 1);
assert(ps3_autosave_capture_completed_save("NPUA80491", root, "GOW2SAVE") == PS3_AUTOSAVE_IO_ERROR);
assert_restored_payload(root, "A", 1);
assert(ps3_autosave_capture_completed_save("NPUA80491", root, "../escape") == PS3_AUTOSAVE_INVALID);
```

- [ ] **Step 2: Run the focused test and verify the new assertions fail**

Run: `cmake --build build-autosave --target test_ps3_autosave && ctest --test-dir build-autosave -R '^test_ps3_autosave$' --output-on-failure`

Expected: failure in the preservation or malformed-input assertion.

- [ ] **Step 3: Make replacement failure-safe and validation strict**

Keep the old `current/` directory untouched until the new temporary directory and manifest are complete. Remove only validated stale temporary children. Ensure parser and directory traversal reject every test case rather than treating it as an empty autosave.

- [ ] **Step 4: Run the focused test and verify it passes**

Run: `cmake --build build-autosave --target test_ps3_autosave && ctest --test-dir build-autosave -R '^test_ps3_autosave$' --output-on-failure`

Expected: all autosave store assertions pass.

- [ ] **Step 5: Commit validation hardening**

```bash
git add libs/system/ps3_autosave.c libs/system/tests/test_ps3_autosave.c
git commit -m "test: protege autosave contra corrupção"
```

### Task 3: Integrate only completed native saves with CellSaveData

**Files:**
- Modify: `libs/system/cellSaveData.c:578-700`
- Modify: `libs/system/cellSaveData.c:923-1007`
- Modify: `libs/system/tests/test_savedata_auto.c`
- Modify: `CMakeLists.txt:434-445`

**Interfaces:**
- Consumes `ps3_autosave_capture_completed_save` after successful save completion.
- Consumes `PS3_AUTOSAVE_RESUME=1` only on the AutoLoad path.
- Produces one `[autosave] capture` or `[autosave] resume` diagnostic per outcome.

- [ ] **Step 1: Write failing integration tests**

Extend `test_savedata_auto.c` to complete a native AutoSave callback, locate the generated autosave through `PS3_SAVEDATA_ROOT`, and verify its files match the game save. Add a failing guest file callback case and assert that no autosave is created or replaced.

```c
assert(run_auto_save() == CELL_OK);
assert(ps3_autosave_exists("NPUA80491", getenv("PS3_SAVEDATA_ROOT")));
assert(run_auto_save_with_failing_file_callback() == CELL_SAVEDATA_ERROR_CBRESULT);
assert_autosave_matches_previous_complete_save();
```

- [ ] **Step 2: Run the integration test and verify it fails at the missing autosave assertion**

Run: `cmake --build build-autosave --target test_savedata_auto && ctest --test-dir build-autosave -R '^test_savedata_auto$' --output-on-failure`

Expected: failure because completed saves do not create an autosave yet.

- [ ] **Step 3: Capture strictly after successful save return paths**

Thread a title identity provider through the runtime configuration, or use the GoW2 fixed title ID only in the GoW2 adapter layer; do not hard-code it in generic `cellSaveData`. Call the capture API after the existing `CELL_OK` returns at `savedata_execute` and `savedata_auto_guest`. Do not call it when a callback/file operation failed.

- [ ] **Step 4: Add opt-in load redirection**

Before the AutoLoad stat callback, if `PS3_AUTOSAVE_RESUME=1`, validate/stage the autosave and substitute only the host `save_path` used by the existing guest callback loop. On absence/validation failure log it and retain the normal save path.

- [ ] **Step 5: Run focused integration tests and verify they pass**

Run: `cmake --build build-autosave --target test_ps3_autosave test_savedata_auto && ctest --test-dir build-autosave -R '^(test_ps3_autosave|test_savedata_auto)$' --output-on-failure`

Expected: both test executables pass.

- [ ] **Step 6: Commit CellSaveData integration**

```bash
git add libs/system/cellSaveData.c libs/system/tests/test_savedata_auto.c CMakeLists.txt
git commit -m "feat: espelha saves concluídos no autosave"
```

### Task 4: Add launcher launch intents and test them

**Files:**
- Modify: `games/gow2/gow2_launcher.py:177-216`
- Modify: `games/gow2/gow2_launcher.py:219-260`
- Modify: `games/gow2/gow2_launcher.py:423-517`
- Modify: `../gow2-recomp/gow2_launcher.py` then sync via `git subtree pull`

**Interfaces:**
- `autosave_available(cfg) -> bool` reads only the manifest and data under the configured save root.
- `play(cfg, resume_autosave: bool = False)` exports `PS3_AUTOSAVE_RESUME=1` only for continuation.
- Produces UI buttons `CONTINUAR AUTOSAVE` and `JOGAR NORMALMENTE`.

- [ ] **Step 1: Write failing Python self-tests**

Create a valid autosave fixture under a temporary configured savedata root and assert `autosave_available` is true. Assert the generated launch script contains `PS3_AUTOSAVE_RESUME=1` only when `resume_autosave=True`.

```python
check(autosave_available({"savedata_root": str(root)}), "autosave válido disponível")
check("PS3_AUTOSAVE_RESUME=1" in build_launch_script(cfg, resume_autosave=True), "continuar exporta modo")
check("PS3_AUTOSAVE_RESUME=1" not in build_launch_script(cfg, resume_autosave=False), "jogar normal não exporta modo")
```

- [ ] **Step 2: Run the launcher self-test and verify it fails**

Run: `python3 games/gow2/gow2_launcher.py --self-test`

Expected: failure because helpers and/or launch intent are absent.

- [ ] **Step 3: Add the two launch paths and default selection**

Extract script construction from `play`; preserve existing environment values. Render a continuation button only for a validated autosave and make it the primary/default action; retain a separate normal-launch button that unsets the mode.

- [ ] **Step 4: Run the launcher self-test and verify it passes**

Run: `python3 games/gow2/gow2_launcher.py --self-test`

Expected: `gow2_launcher self-test: PASS`.

- [ ] **Step 5: Commit in the port repository and sync the subtree**

```bash
git -C ../gow2-recomp add gow2_launcher.py
git -C ../gow2-recomp commit -m "feat: adiciona continuar pelo autosave"
git -C ../gow2-recomp push origin main
git subtree pull --prefix=games/gow2 https://github.com/andrebrumdev/gow2-recomp.git main
```

### Task 5: Measure and add the original-menu resume hook

**Files:**
- Create: `games/gow2/recomp_mid_v2/patch_autosave_menu_probe.py`
- Create: `games/gow2/recomp_mid_v2/patch_autosave_menu_hook.py`
- Create: `games/gow2/recomp_mid_v2/test_patch_autosave_menu.py`
- Modify: `games/gow2/build_macos.sh` only if the patch must be applied before its current generated-code compile step
- Modify: `../gow2-recomp/` equivalents, then sync via subtree

**Interfaces:**
- Probe output must include the concrete generated source filename, guest function EA, menu-construction branch, input-selection branch, and the normal load call site.
- The hook is active only with `PS3_AUTOSAVE_MENU=1`; selecting its item sets `PS3_AUTOSAVE_RESUME=1` through an existing host-visible control channel and reuses the normal guest load route.

- [ ] **Step 1: Write failing patch-script fixture tests**

Create a minimal generated-source fixture containing the measured unique anchors and assert the hook script inserts exactly one gated menu entry, one selection branch, and one resume-mode handoff. Assert a missing/ambiguous anchor exits nonzero without modifying the fixture.

```python
assert run_patch(fixture) == 0
assert fixture.read_text().count("PS3_AUTOSAVE_MENU") == 1
assert run_patch(ambiguous_fixture) != 0
assert ambiguous_fixture.read_text() == original_text
```

- [ ] **Step 2: Run the patch-script test and verify it fails because the scripts are absent**

Run: `python3 games/gow2/recomp_mid_v2/test_patch_autosave_menu.py`

Expected: failure/import error for `patch_autosave_menu_hook`.

- [ ] **Step 3: Measure guest anchors in a real menu boot before writing the hook**

Run the probe with `PS3_AUTOSAVE_MENU_PROBE=1`, record the exact generated file and EAs in the patch script constants, and store the boot log path plus one screenshot of the original menu in a non-asset session note. Do not proceed if the probe does not identify one construction and one selection site.

- [ ] **Step 4: Implement the idempotent, default-off patch**

Make the script reject source drift, insert only the measured menu item and selection handoff, and leave the normal menu/load path byte-for-byte unchanged when `PS3_AUTOSAVE_MENU` is unset. The hook must not fabricate a save or invoke guest serialization; it only selects the established autosave load mode.

- [ ] **Step 5: Run fixture tests and an in-boot A/B smoke**

Run: `python3 games/gow2/recomp_mid_v2/test_patch_autosave_menu.py && PS3_AUTOSAVE_MENU=0 ../gow2-recomp/smoke_boot_mac.sh && PS3_AUTOSAVE_MENU=1 PS3_AUTOSAVE_RESUME=0 ../gow2-recomp/smoke_boot_mac.sh`

Expected: fixture test passes; OFF boot follows the unchanged normal menu route; ON boot logs exactly one resume-menu registration and remains in the original menu without an autosave selection.

- [ ] **Step 6: Validate actual resume in boot**

Create an autosave via Task 3, boot with the menu hook enabled, select `Resume Autosave` by normal pad input, and capture logs proving the ordinary CellSaveData AutoLoad callback consumed the staged mirror. Record this as in-boot evidence; do not claim gameplay restoration from an offline test.

- [ ] **Step 7: Commit the port hook and sync subtree**

```bash
git -C ../gow2-recomp add recomp_mid_v2/patch_autosave_menu_probe.py recomp_mid_v2/patch_autosave_menu_hook.py recomp_mid_v2/test_patch_autosave_menu.py build_macos.sh
git -C ../gow2-recomp commit -m "feat: adiciona retomar autosave no menu"
git -C ../gow2-recomp push origin main
git subtree pull --prefix=games/gow2 https://github.com/andrebrumdev/gow2-recomp.git main
```

### Task 6: Full regression and native acceptance

**Files:**
- Modify: `docs/superpowers/specs/2026-09-21-gow2-native-autosave-design.md` only to record measured acceptance evidence
- Modify: `docs/superpowers/plans/2026-09-21-gow2-native-autosave.md` by checking completed tasks

**Interfaces:**
- Consumes Tasks 1–5.
- Produces fresh evidence for unit, launcher, build, normal launch, and original-menu resume behavior.

- [ ] **Step 1: Run all relevant automated tests**

Run: `cmake --build build-autosave && ctest --test-dir build-autosave --output-on-failure && python3 games/gow2/gow2_launcher.py --self-test`

Expected: all selected CTest tests and launcher self-test pass.

- [ ] **Step 2: Build native macOS artifacts**

Run: `cmake --build build-macos -j"$(sysctl -n hw.ncpu)" && cd ../gow2-recomp && ./build_macos.sh`

Expected: runtime library and `boot_gow2` link successfully.

- [ ] **Step 3: Run normal and continuation smokes**

Run a normal game save to create the mirror, then run launcher-generated normal and continuation commands. Verify normal launch does not set resume mode; continuation validates the manifest, stages it, and enters the standard AutoLoad callback.

- [ ] **Step 4: Inspect scope and working tree before commit**

Run: `git status -sb && git diff --check && git log --oneline -6`

Expected: no game assets staged; unrelated pre-existing changes remain outside autosave commits.

- [ ] **Step 5: Commit acceptance artifacts only if they contain no game data**

```bash
git add docs/superpowers/specs/2026-09-21-gow2-native-autosave-design.md docs/superpowers/plans/2026-09-21-gow2-native-autosave.md
git commit -m "docs: registra validação do autosave nativo"
```
