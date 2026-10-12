# Gow2HD-Windows launcher: notes

The launcher is a single `Gow2HD-Windows.exe` that players download from GitHub Releases. It finds the player's own copy of
God of War II HD (NPUA80491), downloads verified tools, runs `build.py` to build the port on the player's PC, and then
launches it. It is written in C++ Win32 with GDI only, and statically linked with llvm-mingw. It has no runtime
dependencies, needs no installer, and is about 7.1 MB including the bundled build sources (a 6.7 MB deterministic zip
of `build.py` and the repo sources it compiles; the launcher code itself is about 400 KB). It contains no game assets
or game-derived data. The icon is drawn by `tools/make_icon.py` from simple shapes.

## Layout

| Path | What |
|---|---|
| `src/config.h` | **All constants**: version, `GITHUB_REPO` ("owner/name", the one place to rename the repo), title id, Python URL + sha256, disk estimate (7.5 GB) |
| `src/main.cpp` | Window, pages (setup, build, home, settings, about), command line |
| `src/pipeline.cpp` | Build pipeline (scripts, then Python, then tools, then build.py), finding and launching the built exe |
| `src/net.cpp` | WinHTTP downloads (resume and verify), child processes in a job object, extraction, update check |
| `src/util.cpp` | Strings, files, key=value ini, PARAM.SFO reader, RPCS3/game/.rap detection, SHA-256 (BCrypt), small JSON parser |
| `res/` | `app.rc`, `app.manifest` (PerMonitorV2, asInvoker), `app.ico`, `icon-256.png` (our own) |
| `build.cmd` | Build script (CRLF) |
| `test/` | Stub `build.py`, stub game, fake RPCS3 layout maker, `run_tests.ps1`, `shots.ps1`, Windows Sandbox test |
| `tools/` | `pack_scripts.py` (deterministic zip of the build sources), `make_icon.py`, `shot.ps1` (PrintWindow capture), `scan_private.py` (release check for personal paths) |
| `shots/` | Review screenshots. **Git-ignored**: they show local paths. |

## Building the exe

```
build.cmd [release|stub] [bundled|download]
```

- `release` (the default) packs the **real** build sources of this repository with `tools\pack_scripts.py`: the files
  git tracks under `build.py`, `LICENSE`, `NOTICE.md`, `ps3recomp/`, `gow2-recomp/` and `winport/`, minus what no
  build step reads (`docs`, `notes`, planning/agent folders, `assets`, `*.md` other than licences, macOS `.m`/`.mm`,
  pictures). Entries are sorted with fixed timestamps and attributes, so the same commit gives the same zip (and the
  same sha256). About 1640 files, 21 MB raw, 6.7 MB zipped. The full real build was verified from this bundle.
- `stub` packs `test\stub_scripts` (the stub `build.py` for `test\run_tests.ps1`).
- `bundled` (the default) puts the zip into the exe as an RCDATA resource. One download, works offline, and the
  scripts are locked to the exe version. `download` makes the exe fetch `Gow2HD-Windows-scripts-src.zip` from the
  matching release tag (`SCRIPTS_URL` in config.h) and pin its sha256. Upload that zip next to the exe. Quiver ignores
  it because of the `-src.zip` name.
- `LLVM_MINGW` sets the toolchain path. The default is `..\tools-download\llvm-mingw` (the folder layout
  `build.py --print-tools` / `tools\fetch_tools.py tools-download` produce). `PYTHON` sets the Python used for packing.
- Output: `build\Gow2HD-Windows.exe` and `build\Gow2HD-Windows.exe.sha256`.
- Before a release, run `python tools\scan_private.py build\Gow2HD-Windows.exe build\Gow2HD-Windows-scripts-src.zip`.

## Contract with build.py (for the builder agent)

The launcher runs `tools\python\python.exe -u -X utf8 <scripts>\build.py ...`. The working directory is the scripts
folder, and these environment variables are set: `PYTHONUNBUFFERED=1`, `PYTHONIOENCODING=utf-8`, `PYTHONNOUSERSITE=1`,
`GOW2_LAUNCHER_VERSION`.

1. **`build.py --print-tools`** prints JSON: either a list or `{"tools": [...]}` of objects with
   `name, url, sha256, size, extract-to`.
   - `sha256` is **required** (64 hex digits). Tools without it are refused.
   - `extract-to` is relative to `--tools`. It may not contain `..` or a drive letter.
   - Archives (`.zip`, `.tar.*`, `.7z`) are extracted into `<tools>\<extract-to>` with Windows `tar.exe`. A zip that
     fails there is retried with PowerShell `Expand-Archive`. Any other file is copied into that folder.
   - Each tool has a stamp in `tools\.stamps\`. A tool whose sha256 and destination are unchanged is not downloaded again.
     When a tool changes, its folder is deleted and extracted fresh.
   - Nothing else may be printed before the JSON. Leading non-JSON lines are skipped up to the first `[` or `{`.
2. **The build:** `build.py --game <dir> --rap <file> [--elf <file>] --tools <dir> --out <install dir> [--jobs N]`.
   - `--game` is the resolved folder that contains `PARAM.SFO` and `USRDIR`, already checked to be NPUA80491 v01.00.
   - **Or `--pkg <file>`** instead of `--game`, when the player picked the PSN package. The launcher has already read
     its header, decrypted item table and PARAM.SFO (`CheckGamePkg` in `util.cpp`). build.py extracts PARAM.SFO,
     ICON0.PNG and `USRDIR\*` into `<out>\game\NPUA80491` (`winport/buildlib/pkg.py`), and that folder is the game
     folder from then on (`gow2_port.ini` `vfs_root`; `LaunchGame` checks it instead of the .pkg). The extracted files
     stay: the game reads `gow2.psarc` while it runs. build_info.json then lists `game` in `installed_dirs`, so
     **Remove build** deletes the extracted copy (never the .pkg).
   - stdout lines:
     - `PROGRESS <0-100> <msg>` is mapped onto 30..100% of the overall bar.
     - `ERROR <msg>` is shown to the player if the exit code is not 0. The last ERROR line wins.
     - **`EXE <path>`** (absolute, or relative to `--out`) names the game exe.
   - Every line goes to `<install>\build.log`.
   - Exit code 0 means success.
   - Without an `EXE` line, the launcher takes the largest `*.exe` in `<out>`, `<out>\bin`, `<out>\game` or `<out>\port`.
3. **The embeddable Python ignores the script's own folder.** The launcher writes `python312._pth` with the scripts
   folder in it, so sibling imports work. Still, `sys.path.insert(0, os.path.dirname(__file__))` is safer. Only the
   standard library (and the zip it ships) is available: no pip, tkinter or venv. `_ssl`, `hashlib`, `zipfile`,
   `lzma` and `bz2` are present.
4. **Launch environment: none.** The game exe configures itself (`winport/host/wp_release.c`): the tested play
   switches are compiled-in defaults, `gow2_port.ini` next to the exe (written by build.py) names the game's USRDIR and
   the movie cache and may add `[env]` defaults, `port_settings.ini` next to it is read, and the exe makes its own
   folder the working directory. `Play` therefore starts the exe from its folder with **no arguments and no extra
   environment**; the build-only variables (`PYTHON*`, `GOW2_LAUNCHER_VERSION`) are removed from the launcher's
   environment before the game starts. Double-clicking `gow2hd.exe` behaves the same.
5. **Settings** are written to `<exe dir>\port_settings.ini` (that is where the port reads them). With `EXE` at
   `<install>\...exe` this is `<install>\port_settings.ini`. Keys: `fps_cap` (30/60/120/144/162/165/240/300/1000),
   `borderless`, `res_scale` 1-4, `fps_overlay`. Comments and unknown keys are kept. A missing key means the exe's
   default (300 fps, borderless, 2x, counter off); the launcher shows those defaults and writes only keys the player
   changed, so it never pins a value over the exe's defaults.
6. **build_info.json** (written by build.py next to the exe) lists the files it installed (`installed`) and its folders
   (`installed_dirs`). "Remove build" deletes exactly those, plus the launcher's own `tools\`, `scripts\`, `downloads\`.

## Install folder contents

`%LOCALAPPDATA%\GoW2-Windows` by default (stored in `HKCU\Software\GoW2-Windows\InstallDir`; that is the only registry
value):

- `tools\` holds `python\` and the tool folders, plus `.stamps\`.
- `scripts\` holds the extracted bundled scripts and `.bundle.sha256`.
- `downloads\` holds `*.part` files (resumable; deleted after extraction).
- `build.log` and `build.prev.log`.
- `launcher_state.ini` holds game, rap, elf, built, exe, built_at, launcher_version and autoplay.
- The build output (whatever build.py writes to `--out`).

**Remove build** (home menu, or `--remove-build [--install D] [--yes]` on the command line, which is a dry run without
`--yes`) deletes, after a confirmation that lists what stays: `tools\`, `scripts\`, `downloads\`, `.build\`,
`movie_cache\`, `licenses\`, `shader_cache\` (the game's PSO cache), the files in `build_info.json`'s `installed` list (fallback: `gow2hd.exe`, `EBOOT.ELF`,
the FFmpeg exes/DLLs), `build_info.json`, `gow2.log`, `build.log` and their `.prev` copies. It keeps
`port_settings.ini`, `launcher_state.ini` (marked not built; game and licence paths kept) and `gow2_port.ini` when it
has `[env]` entries. **Saves are never touched**: they live in `PS3_SAVEDATA_ROOT` if set (environment or
`gow2_port.ini` `[env]`, relative = to the exe folder), else `%APPDATA%\ps3recomp\dev_hdd0\home\00000001\savedata`
(`ps3recomp/libs/system/cellSaveData.c`). Every candidate save folder is protected: a folder that holds one, or lies
inside one, is skipped (and listed as kept). Junctions are never followed.

## Command line

| Flag | Effect |
|---|---|
| (none) | Opens setup if the port is not built, otherwise the launcher window. If "Start the game directly" is on, the game starts at once; hold Shift to see the window. |
| `--play` | Starts the game if it is built, otherwise opens setup. |
| `--setup` | Opens the setup page even when the port is built. |
| `--silent-build (--game D \| --pkg F) --rap F [--elf F] --out D [--jobs N]` | Runs the whole pipeline with no window. Output goes to stdout (`PROGRESS`, `ERROR`, `OK <exe>`). The exit code is 0 on success. From cmd use `start /wait`, or redirect stdout. |
| `--detect [--game D]` | Prints what auto-detection finds. |
| `--check-update` | Prints the GitHub latest-release check. No release yet (HTTP 404) prints `latest=none` and exits 0. |
| `--remove-build [--yes]` | Prints what "Remove build" would delete and keep; deletes only with `--yes`. |
| `--install D` | Uses another install folder for this run. |
| `--page settings\|about\|build` | Development only. |

Exit codes for `--silent-build`:

| Code | Meaning |
|---|---|
| 2 | Bad arguments, or the install folder can't be written |
| 3 | Game or rap invalid |
| 4 | Scripts |
| 5 | Python |
| 6 | Tool list |
| 7 | Tool download |
| 8 | Python didn't start |
| 9 | Cancelled |
| 10 | build.py failed |
| 11 | No exe found |

## Detection

- **RPCS3 folders:**
  - Uninstall registry entries.
  - `%APPDATA%`, `%LOCALAPPDATA%`, the user profile, Documents, Desktop, Downloads, Program Files (`rpcs3*` folders).
  - scoop.
  - The launcher's own folder and its parents.
  - On every fixed drive: `\RPCS3`, `\Emulators\RPCS3`, `\Emulation\RPCS3`, `\Games\RPCS3` and similar.
  - `config\vfs.yml` is read when `dev_hdd0` was moved.
- **Game:**
  - `<hdd0>\game\NPUA80491`.
  - The launcher's folder and up to 2 parents, the current folder, Downloads, Desktop, Documents. Each candidate is
    checked itself, plus `NPUA80491`/`PS3_GAME` inside it, plus one level of subfolders.
  - Every hit is checked with PARAM.SFO (`TITLE_ID == NPUA80491`, `APP_VER == 01.00`, `USRDIR\EBOOT.BIN` present).
    Other titles and versions get "Only NPUA80491 v01.00 US is supported".
- **PSN package (.pkg)**, when no installed game folder was found: every `*.pkg` (any name) next to the launcher and
  its parent, in the current folder, Downloads, Desktop, Documents and the RPCS3 folders, each with one level of
  subfolders. The file decides, not its name: header magic and type (PS3), content id
  `UP9000-NPUA80491_00-GODOFWARIIHDUS00`, complete size, then the decrypted item table and PARAM.SFO inside
  (`NPUA80491`, `01.00`, not an update/EBOOT-only package, `USRDIR/gow2.psarc` of the v01.00 size). Retail and debug
  packages are read (AES-128-CTR via BCrypt, resp. the SHA-1 keystream). A hit shows "Game package (.pkg) found: God
  of War II HD (NPUA80491)". The setup page has **FOLDER** and **.PKG** buttons on the game row, and a .pkg, game
  folder, .rap or .elf can be dropped onto the window. With a package the disk estimate grows by 6.5 GB (the
  extracted game files, which stay because the game reads them while it runs).
- **.rap:**
  - `<hdd0>\home\*\exdata`, including the hdd0 that holds the picked game.
  - The game folder (or the .pkg's folder) and 2 parents, the launcher folder, Downloads, Desktop.
  - The file must be exactly 16 bytes. A name without NPUA80491 only gives a warning.
- **EBOOT.ELF (optional):** `USRDIR\EBOOT.ELF`, or next to the game.

## Tests (all pass, 2026-10-09)

### Real build, end to end (2026-10-09, launcher 0.1.0 with the bundled real sources)

- **PC** (16 threads, 31 GB, RTX 4070 Ti): `--silent-build` into an empty folder with real downloads (Python, llvm-mingw,
  FFmpeg) and the full build: **496 s** total, exit 0, `EXE` line picked up (`launcher_state.ini exe=`). The built
  `gow2hd.exe`, run hidden and muted with a seed-save copy and `WINPORT_PSET80_INI` on a temp file, reached gameplay
  (state 11) at 42 s; Port Settings panel opened; no CRASH/PSO FAIL/0x887A/FENCE STUCK; no ini of the install changed.
  `--remove-build --yes` then freed 7.1 GB in 5 s and left only `launcher_state.ini` (and port_settings.ini when present).
- **Windows Sandbox** (clean Windows 10.0.26100, no dev tools; game folder, .rap folder and seed mapped read-only):
  `test\sandbox\real_build.ps1` → downloads + build **699 s**, exit 0 (825 s wall time including sandbox boot and a
  90 s game run). The sandbox had GPU-PV (the host's RTX 4070 Ti, not WARP): D3D12 initialised, the boot movie played,
  the game was still running after 90 s, no crash dump. Logs are copied to the results folder; the sandbox shuts
  itself down.

### Stub tests

`test\run_tests.ps1 -Game <dir> -Rap <file>` (after `build.cmd stub`) runs 20 checks against the stub:

- A full silent build. Python and tools are downloaded for real and checked against SHA-256.
- A rebuild reuses tools.
- A wrong sha256 is refused (exit 7).
- A build.py ERROR is reported (exit 10).
- A partial download resumes with HTTP 206.
- A corrupt `.part` is detected, deleted and downloaded again.
- Bad arguments (exit 2) and the wrong game version (exit 3).
- `--play` starts the exe from its folder with `port_settings.ini` next to it and no launcher environment.
- `--check-update` against a repo without releases prints `latest=none` and exits 0.
- `--remove-build`: the dry run deletes nothing; `--yes` deletes the build and tools, keeps port_settings.ini, the
  state and a save folder placed (on purpose) inside `movie_cache\`.
- GUI Cancel kills python and its child.
- Killing the launcher kills the build (job object).

The tests save and restore the registry value.

`test\shots.ps1` captures every page with PrintWindow into `shots\`.

**Windows Sandbox** is enabled on this PC: `WindowsSandbox.exe` and `CmService` are present, and the sandbox VM ran.
`test\sandbox\` holds the `.wsb` template and the script that runs inside it. On a clean Windows 10.0.26100 (no Python,
no VC runtime), the following all worked:

- Detection inside a fake RPCS3 layout, including the exe dropped into the RPCS3 folder.
- A full silent build with real downloads.
- The GUI started and was captured to `shots\09_sandbox_home.png`.

## Design

The palette is warm near-black with bone text, a bronze accent and one crimson colour for the primary action. The title
is set in Palatino caps with letter-spacing, and a single Greek-key rule sits under it. The home page is a vertical text
menu in the style of the game's own menus, with a faint large "II" numeral as the only ornament. There are no
gradients-on-buttons, glows or icons-in-circles. The dark DWM title bar has a matching caption colour on Windows 11.

## Open items / questions

- `GITHUB_REPO` is `sethlikepie/Gow2HD-Windows`; renaming the repo means changing that one constant (and the Quiver entry).
- Gamepad navigation on the home menu (Quiver kiosk / couch use).
- Code signing: an unsigned exe triggers SmartScreen on first run. That is worth it for a public release.
- `--silent-build` is a GUI-subsystem exe, so cmd doesn't wait for it without `start /wait`.
