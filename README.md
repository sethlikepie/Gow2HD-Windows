# God of War II HD for Windows (PS3 static recompilation)

A native Windows x64 build of **God of War II HD** (PlayStation 3, **NPUA80491 v01.00**, the US PSN release),
made by *static recompilation*: the game's PowerPC (PPU) and SPU code is translated ahead of time into C/C++ and
compiled with clang for x86-64. It runs on a re-implementation of the PS3 system libraries with a Direct3D 12
renderer, WASAPI audio and XInput/keyboard input. It is not an emulator: there is no interpreter or JIT for the
game's main code.

**This repository contains no game code or data.** You bring your own legally obtained copy of the game and your
licence file; the build decrypts, translates and compiles everything **on your PC**, and nothing built from your copy
leaves it.

It is the Windows continuation of [gow2-recomp](https://github.com/andrebrumdev/gow2-recomp) (macOS / Apple
Silicon, by andrebrumdev) on the [ps3recomp](https://github.com/sp00nznet/ps3recomp) engine (by sp00nznet).

## Features

* High frame rates: an own frame limiter (30 to 1000 fps, default 300), with **162 fps** as the G-Sync / FreeSync
  cap for 165 Hz displays, and even frame pacing when the game runs below the cap.
* **Resolution scale** 1x to 4x (default 2x = 2560x1440), borderless or windowed (**F11** switches), 16:9.
* **Port Settings** (**F1**, or L3 outside gameplay): frame-rate cap, display mode, resolution scale, fps counter and
  checkpoints, drawn in the game's own menu style. Saved in `port_settings.ini` next to the game exe.
* **F10** fps counter.
* **Checkpoints**: **F6** saves the game's latest checkpoint with a picture; **F1 > Load Checkpoint** lists them and
  loads one in game.
* The game's **Full Frame** option (game Options) works, prerendered movies included.
* All 45 prerendered movies at full length with picture/sound sync; in a movie SELECT pauses picture and sound,
  holding START skips.
* XInput controllers and the keyboard; input only while the window has focus. Game saves work.

## Requirements

* **Windows 10 or 11, x64**, a CPU with **AVX2** (x86-64-v3: Intel Haswell / AMD Excavator or newer) and a
  **Direct3D 12** GPU.
* **Disk:** about **7.5 GB** when built from an installed game folder (install folder 5.8 GB: movie cache 4.7 GB,
  build intermediates 0.8 GB; 0.95 GB of tools; 0.27 GB of downloads while they are unpacked), about **14 GB** when
  built from the PSN package (its 6.5 GB of game files are extracted into the install folder and stay there, because
  the game reads them while it runs).
* **Your game**, God of War II HD **NPUA80491 v01.00**, either
  * the game folder (with `PARAM.SFO`, `USRDIR/EBOOT.BIN` and `USRDIR/gow2.psarc`), e.g. RPCS3's
    `dev_hdd0/game/NPUA80491` or a dump from your PS3, **or**
  * the PSN package `UP9000-NPUA80491_00-GODOFWARIIHDUS00.pkg` (any file name; the full game, about 7 GB, not an
    update),
* and **your licence** `UP9000-NPUA80491_00-GODOFWARIIHDUS00.rap` (RPCS3: `dev_hdd0/home/<user>/exdata`), or an
  `EBOOT.ELF` you decrypted yourself.
* An internet connection for the first build (about 270 MB of tools). No Visual Studio, MSYS2, CMake or admin rights.

## Quick start: Gow2HD-Windows.exe

1. Download **Gow2HD-Windows.exe** from the [releases](https://github.com/sethlikepie/Gow2HD-Windows/releases) (or
   install it from [Quiver](https://quiverlauncher.com)) and run it. It is not code-signed, so Windows SmartScreen
   asks once: **More info → Run anyway**.
2. **Game:** the launcher looks for your game folder or `.pkg` (RPCS3 folders, Downloads, Desktop, Documents, next
   to the exe). If it does not find it, press **FOLDER** (installed game folder) or **.PKG** (PSN package), or drop
   the folder or file on the window.
3. **Licence:** it looks for the `.rap` the same way; otherwise pick it or drop it on the window.
4. Press **BUILD**. The launcher downloads pinned, SHA-256-checked tools (an embeddable Python, llvm-mingw, FFmpeg),
   runs `build.py` from the sources bundled in the exe and then starts the game. The first build takes about
   **10 minutes** on a 16-thread desktop (about one more minute to unpack a `.pkg`). After that, **PLAY** starts the
   game.

Everything goes to `%LOCALAPPDATA%\GoW2-Windows` (changeable). **REMOVE BUILD** in the launcher deletes the build and
the tools again; your saves, checkpoints and settings stay. Command-line options (`--play`,
`--silent-build --game <dir>|--pkg <file> --rap <file> --out <dir>`, `--remove-build`, `--check-update`, ...):
[launcher/NOTES.md](launcher/NOTES.md).

## Play

The game starts borderless at 2x resolution with a 300 fps cap. No arguments or environment variables are needed;
`gow2hd.exe` in the install folder can also be started directly.

| Key | Action |
|---|---|
| **F1** (L3 outside gameplay) | Port Settings: frame-rate cap (30 / 60 / 120 / 144 / **162** / 165 / 240 / 300 / 1000), display mode, resolution scale, fps counter, checkpoints |
| **F6** | Save Checkpoint |
| **F10** | fps counter on / off |
| **F11** | borderless / windowed |
| SELECT / hold START in a movie | pause / skip (prerendered movies) |

Keyboard as a pad: Enter = Start, Tab = Select, Space = X, E = Circle, J = Square, K = Triangle, WASD = left stick,
arrows = right stick, LShift = L1, Q = R1, LCtrl = L2, R = R2, F = L3, G = R3. Controllers: XInput (Xbox, or
PlayStation pads through Steam Input / DS4Windows).

**Checkpoints:** F6 (or F1 > *Save Checkpoint*) saves the game's latest checkpoint, with a picture of where it was
taken, to `checkpoints\` next to the game exe. F1 > *Load Checkpoint* lists them (newest first): X loads one in game
(during gameplay or the pause menu, any level), Square/Delete moves it to `checkpoints\deleted\`, O goes back. A
checkpoint is the game's own (the last one it passed), not the exact spot where you pressed F6. Checkpoints are
separate from the game's save slots, and REMOVE BUILD keeps them.

**Saves** live in `%APPDATA%\ps3recomp\dev_hdd0\home\00000001\savedata`. The log of the last run is `gow2.log` next
to the game exe (the previous one is `gow2.prev.log`).

## Known issues

* Only **NPUA80491 v01.00** (US PSN) is supported; other regions or versions fail the hash checks.
* An **AVX2** CPU is required (the game exe is built for x86-64-v3).
* `Gow2HD-Windows.exe` is **not code-signed**: SmartScreen warns on the first start, and some antivirus tools distrust a
  new unsigned exe that downloads and runs a compiler.
* The **indexed-draw optimisation** of the renderer is **off** by default: with it on, a QTE (button-prompt) sequence
  can go out of sync with its animation. Combat is somewhat slower without it.
* **Start-up hang recovery:** on some launches Windows stops handing back a swap-chain buffer during the boot logos;
  a watchdog hides and shows the window for a moment (~0.6 s, `[PWD82]` in the log) and the game continues.
* The **WARNING screen** lasts about 30 s (slow level loading through many small reads). **Real-time cutscenes**
  cannot be skipped (prerendered movies can).
* Music: one long session reported the music stopping in a later scene (Colossus) while other sounds kept playing.
  v0.1.0 fixes a runtime lock bug that silenced a music stream in exactly this way in the sister God of War port; if
  it still happens, please report it with the log.
* **Full Frame:** menus, gameplay and prerendered movies are tested with it on; real-time cutscenes in Full Frame
  are not yet.
* **Checkpoints:** tested in the Rhodes levels (other areas show raw level names in the list); loading during a death
  screen, a QTE or a boss fight is untested.
* The **display-mode** toggle in Port Settings has had less testing than the other options.
* The launcher's first-time setup needs mouse and keyboard (file pickers); no gamepad navigation there yet (Quiver
  kiosk users: finish the setup once on the desktop).
* Removing the app in Quiver does not delete the ~7 GB build in `%LOCALAPPDATA%\GoW2-Windows`: use **REMOVE BUILD**
  in the launcher first.
* Harmless upstream oddities: ~40 `[vm] OOB` lines in the log during save I/O; the Colossus bone corruption and some
  placeholder normals are repaired in the renderer.

## Reporting bugs

Open an [issue](https://github.com/sethlikepie/Gow2HD-Windows/issues) with:

* what you did and what happened (a screenshot helps for graphics bugs);
* **`gow2.log`** from the install folder (`%LOCALAPPDATA%\GoW2-Windows` unless you changed it; the run before is
  `gow2.prev.log`);
* for build or launcher problems, **`build.log`** from the same folder (**SHOW LOG** in the launcher opens it);
* your CPU, GPU, Windows version, and whether you built from the game folder or the `.pkg`.

The logs contain file paths of your PC (for example your Windows user name); edit them out if you prefer. Never
attach game files, your `.rap` or anything else from the install folder.

## Build from source (without the launcher)

```
python tools\fetch_tools.py C:\gow2\tools
python build.py --game "D:\RPCS3\dev_hdd0\game\NPUA80491" --rap "D:\UP9000-NPUA80491_00-GODOFWARIIHDUS00.rap" ^
                --tools C:\gow2\tools --out C:\gow2\game
rem or from the PSN package:
python build.py --pkg "D:\Downloads\UP9000-NPUA80491_00-GODOFWARIIHDUS00.pkg" --rap "D:\UP9000-NPUA80491_00-GODOFWARIIHDUS00.rap" ^
                --tools C:\gow2\tools --out C:\gow2\game
```

Python 3.11+ is enough (the Windows *embeddable* CPython zip works; no packages needed). `python build.py
--print-tools` lists the pinned tools (llvm-mingw 20260922 and an FFmpeg LGPL build).

With `--pkg`, `build.py` first checks the package (PS3 package, content id, complete download, the full game v01.00
rather than an update) and extracts `PARAM.SFO`, `ICON0.PNG` and `USRDIR\*` into `<out>\game\NPUA80491`, which is
then the game folder. The extractor (`winport/buildlib/pkg.py`, the package format as RPCS3 and pkg2zip read it) uses
Windows' own AES (CNG), streams, can resume, and checks `EBOOT.BIN` and `gow2.psarc` against the same SHA-256 pins.

`build.py` checks the game files (sizes and SHA-256 of `EBOOT.BIN` and `gow2.psarc`), decrypts `EBOOT.BIN` with your
licence (a Python port of the kit's `tools/unself`, with Windows' AES), runs the kit's pinned PPU lifter, patch
scripts and kit delta and the seven SPU lifts (each checked against the hashes in `gow2-recomp/kit/`), applies the
port's changes to the lifted code (`winport/lift_patches/`), lifts the SCREAM audio mixer jobs (`winport/scream/`),
extracts the movie cache from `gow2.psarc`, compiles the engine runtime and the game, and installs everything into
`--out`. Running it again skips the finished steps (about 2 s when nothing changed). It prints
`PROGRESS <0-100> <message>` lines and, on failure, one `ERROR <message>` line (details in `<out>\.build\build.log`).
Options: `--elf <EBOOT.ELF>` instead of `--rap`, `--jobs N`.

Every behaviour switch of the port is an environment variable with a built-in default (`winport/host/wp_release.c`
lists the defaults); setting one overrides it, which is how the diagnostic modes (frame capture, profiler, test
scripts) are turned on.

## Repository layout

* `ps3recomp/`: the engine (upstream snapshot + Windows changes: pad, audio, movies, saves, perf, GCM).
* `gow2-recomp/`: the upstream game kit: function bounds, lift patch scripts, SPU glue, kit hashes.
* `winport/`: the Windows port: `d3d12/` renderer, `host/` (start-up defaults, Windows helpers), `scream/` (native
  SCREAM mixer), `lift_patches/` (changes to the generated lift), `buildlib/` (build steps), `spu_simd/`.
* `build.py`: the one build entry point. `tools/`: `fetch_tools.py`, `check_no_game_data.py` (pre-commit guard).
* `launcher/`: `Gow2HD-Windows.exe` (C++ Win32, no dependencies): setup wizard, verified downloads, runs `build.py`,
  starts the game. `launcher\build.cmd` bundles the build sources into it; see `launcher/NOTES.md`.
* [CHANGELOG.md](CHANGELOG.md): what changed in each release.

## Credits

* **[ps3recomp](https://github.com/sp00nznet/ps3recomp)** by Ned Heller / sp00nznet and contributors (MIT): the
  static recompilation engine, PPU/SPU lifters and the Direct3D 12 renderer this port adapts.
* **[gow2-recomp](https://github.com/andrebrumdev/gow2-recomp)** by André Brum Cortez Ferreira / andrebrumdev (MIT):
  the God of War II recompilation (macOS), its kit and the reverse engineering behind it.
* **[FFmpeg](https://ffmpeg.org/)** (LGPL-2.1-or-later): decodes the prerendered movies at run time; downloaded by the
  build as a separate LGPL build and run as its own process, not linked into the port.
* **llvm-mingw** by Martin Storsjö: the compiler the build downloads.
* The lineage these projects follow: [N64Recomp](https://github.com/N64Recomp/N64Recomp),
  [XenonRecomp](https://github.com/hedge-dev/XenonRecomp), and [RPCS3](https://rpcs3.net/), used upstream as the
  reference.
* Windows port: sethlikepie. All third-party components and licences: [NOTICE.md](NOTICE.md).

## Legal

God of War II is © Sony Interactive Entertainment. This project is not affiliated with or endorsed by Sony
Interactive Entertainment or Santa Monica Studio. It distributes no copyrighted game code or assets and downloads
none; you must own the game. Everything built from your copy stays on your PC and must not be redistributed. The
port's code is MIT licensed (© 2026 sethlikepie); the engine and the upstream kit are MIT licensed (© sp00nznet and
contributors; © andrebrumdev). See [LICENSE](LICENSE) and [NOTICE.md](NOTICE.md).
