# Changelog

## v0.1.0 (2026-10-11)

First public release.

* **Gow2HD-Windows.exe** launcher: finds your game folder or PSN package (`.pkg`) and your licence (`.rap`), downloads
  pinned, SHA-256-checked tools (embeddable Python, llvm-mingw, FFmpeg), builds the port on your PC with `build.py`
  and starts the game. Remove build, update check, Quiver support.
* **Build:** one command (`build.py`) from the game folder or the PSN package; no game data in the repository.
* **Graphics:** Direct3D 12 renderer, resolution scale 1x-4x, borderless/windowed (F11), the game's Full Frame
  option, no haze or dark line at the screen edges at higher resolution scales, Colossus fixes.
* **Performance:** async GPU command processing, faster SPU jobs and guest memory access; own frame limiter up to
  1000 fps with a 162 fps cap for G-Sync/FreeSync at 165 Hz, even frame pacing below the cap (Kratos no longer snaps
  between poses).
* **Port Settings** (F1): frame-rate cap, display mode, resolution scale, fps counter (F10), checkpoints.
* **Checkpoints:** F6 saves the game's latest checkpoint with a picture, F1 lists and loads them in game.
* **Movies:** all 45 prerendered movies, full length, picture/sound sync, SELECT pause, hold-START skip; a movie can
  no longer stall the game.
* **Audio:** native SCREAM mixer; a recursive-lock bug in the runtime that could leave a sound-stream thread blocked
  for good (music silent while other sounds play) is fixed.
* **Fixes:** the first trophy unlock no longer crashes; music keeps playing after a checkpoint load from the pause
  menu; start-up swap-chain hang recovery.
* Known issues: see [README.md](README.md#known-issues).
