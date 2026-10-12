# Notice: what the licences cover, and what they do not

**The MIT licence in [LICENSE](LICENSE) covers only the original code and documentation of the Windows
port** (`winport/`, `build.py`, `tools/`, and the Windows changes to the imported sources). The imported
upstream projects keep their own copyright notices and licences, in their folders:

| Component | Folder | Licence | Copyright |
|---|---|---|---|
| ps3recomp engine (snapshot 305dd109, with the pinned lifter revisions 5b004fc7, 5f36a40e, 11a1c3c5) | `ps3recomp/` | MIT, [ps3recomp/LICENSE](ps3recomp/LICENSE) | Ned Heller / sp00nznet and contributors |
| gow2-recomp game recompilation kit (ec2ba46, release kit-macos-20260923) | `gow2-recomp/` | MIT, [gow2-recomp/LICENSE](gow2-recomp/LICENSE), [gow2-recomp/NOTICE.md](gow2-recomp/NOTICE.md) | Ned Heller / sp00nznet; André Brum Cortez Ferreira / andrebrumdev |
| Direct3D 12 renderer of upstream ps3recomp (795ac341, 2026-09-27), adapted | `winport/d3d12/` | MIT (as ps3recomp) | Ned Heller / sp00nznet and contributors; Windows-port changes sethlikepie |
| Dear ImGui | `ps3recomp/third_party/imgui/` | MIT | Omar Cornut |
| Inter typeface | `ps3recomp/third_party/inter/` | SIL OFL 1.1 | The Inter Project Authors |
| stb_image / stb_truetype | `ps3recomp/libs/` | MIT or public domain | Sean Barrett |

Tools downloaded by the build, not part of this repository (see `python build.py --print-tools`):

| Tool | Used for | Licence |
|---|---|---|
| llvm-mingw 20260922 (Martin Storsjö) | compiler, linker, C/C++ runtime linked into the exe | Apache-2.0 WITH LLVM-exception; mingw-w64 runtime: ZPL-2.1 and others |
| FFmpeg (BtbN Windows build, LGPL variant, shared) | decodes the prerendered movies at run time (`ffmpeg.exe`, separate process) | LGPL-2.1-or-later; its licence text is copied to `<install>/licenses/` |

## What is not covered at all

* **God of War II** itself (its code, data, images, audio, video) belongs to Sony Interactive
  Entertainment. This project is not affiliated with or endorsed by Sony.
* **Everything the build generates from your copy of the game** (the decrypted `EBOOT.ELF`, the lifted
  PPU/SPU sources and SCREAM jobs, the movie cache, the exe compiled from them) carries material from the
  game. It is made on your PC from your own copy, for your own use with that copy. It is not part of this
  repository, is not covered by its licence, and must not be redistributed.

No game data, game code or licence file (`.rap`) is distributed here. `tools/check_no_game_data.py` guards
every commit.
