# M4 — IOSurface / CVPixelBuffer zero-copy movie (2026-07-21)

## Path

```
AVAssetReader (kCVPixelFormatType_32BGRA)
  → rsx_metal_movie_frame_cvpixel
  → CVMetalTextureCacheCreateTextureFromImage
  → sample MTLTexture in metal_present
```

Fallback: `PS3_MOVIE_FORCE_RGBA=1` or cache/create fail → lock base + `rsx_metal_movie_frame` (M0 memcpy).

## Lifetime

Present `CFRetain`s the active `CVMetalTexture`/`CVPixelBuffer` and releases in the command-buffer completed handler so the IOSurface survives GPU use after the decode thread CFReleases the sample buffer.

## Aceite medido (in-boot, SmLogo_v2.m2v)

| ID | Resultado |
|----|-----------|
| Z1 | `[movie-vt] path=iosurface frames=330` + `[RSX metal] movie path=iosurface` |
| Z2 | Decode completed natural EOS 330f (logo path unchanged) |
| Z3 | `PS3_MOVIE_FORCE_RGBA=1` → `path=rgba-cpu frames=330` |
| Z4 | submit avg **iosurface 4.10 ms** vs **rgba-cpu 4.65 ms** (720p@~30, M5 host; includes present) |

```
[movie-vt] path=iosurface frames=330 avg_ms=4.10
[movie-vt] path=rgba-cpu  frames=330 avg_ms=4.65
```

## Env

| Var | Efeito |
|-----|--------|
| (default) | zero-copy CV/IOSurface |
| `PS3_MOVIE_FORCE_RGBA=1` | CPU staging M0 |
| `PS3_PERF_MOVIE=1` | log `PERF_MOVIE path=… avg_ms=` |
