# Notice — what the licence covers, and what it does not

**The MIT licence in [LICENSE](LICENSE) covers only the original code and documentation of this
project** (scripts, launcher, kit, glue code, patch scripts, tests and docs written for it).

It grants **no rights** over:

- **God of War II** itself — its data, code, images, audio and video, which belong to Sony Interactive
  Entertainment. This project is not affiliated with or endorsed by Sony.
- **The parts of the game reproduced in the lift you generate.** The kit and the build scripts translate
  the game's code from **your own** copy into C/C++ on **your own** machine; those generated files carry
  material from the game and are yours to use with your copy only. They are not part of this repository
  and are not covered by its licence.
- **The gameplay screenshots** in [docs/img/](docs/img/), which show the game's copyrighted content.

No game data, game code or licence file (`.rap`) is distributed here; see the "Legal" section of
[README.md](README.md#legal).

## Third-party components

They keep their own licences:

| Component | Where it is used | Licence | Details |
|---|---|---|---|
| ps3recomp engine | the runtime the port links | MIT | [andrebrumdev/ps3recomp](https://github.com/andrebrumdev/ps3recomp) (`LICENSE`, `NOTICE.md`) |
| FFmpeg (`libavcodec`, `libavutil`, MPEG-2 decoder only) | iOS app (static) and Android app (shared libraries) | LGPL-2.1 or later | the engine's `third_party/ffmpeg/README.md`; in the iOS app under `licenses/`; in the Android app's licences screen |
| SDL2 | Mac, iOS and Android builds | zlib | the engine's `third_party/sdl2/README.md` |
| CPython (only inside the release kit, when no Python ≥ 3.11 is installed) | kit build tools | PSF licence | [kit/README.md](kit/README.md) |

**FFmpeg in the Android app (LGPL-2.1):** as the [README](README.md#legal) says, the APK for Android is
compiled locally from your own dump and installed only on your own device; it is never distributed. The
FFmpeg libraries inside the app remain under the LGPL-2.1 and can be copied, modified and replaced; the
app carries their licence text, the exact source tarball and the build script and flags that produced
them.
