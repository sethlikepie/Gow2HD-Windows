# Contributing

* **Never commit anything from the game**: no EBOOT/ELF, psarc, movies, WADs, saves, screenshots or frame
  captures, logs, and none of the files the build generates from the game (lifted PPU/SPU sources, SCREAM
  jobs, movie cache). Install the hook once: `copy tools\hooks\pre-commit .git\hooks\pre-commit`
  (it runs `python tools/check_no_game_data.py`).
* Changes to the engine and the renderer are normal source edits. Changes to the *generated* lift go in
  `winport/lift_patches/` as idempotent transforms (anchor that matches exactly once, a TAG marker).
* New behaviour goes behind an environment switch; if it becomes the default, add it to the table in
  `winport/host/wp_release.c`. Diagnostics stay off by default and must not flood the log.
* Test with `build.py` into a scratch `--out`, run the exe muted (`PS3_MUTE=1`), and check `gow2.log`
  for `PSO FAIL`, `0x887A` and `[CRASH]`.
* Keep the upstream licence notices (`ps3recomp/LICENSE`, `gow2-recomp/LICENSE`, `NOTICE.md`).
* Commits: describe the why. By contributing you agree your work is released under the MIT licence.
