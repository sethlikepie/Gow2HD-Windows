# Mac Metal — IOSurface zero-copy movie (M4) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: `superpowers:subagent-driven-development` ou `superpowers:executing-plans`.

**Goal:** Depois do overlay VT+Metal (M0) estável com cópia CPU RGBA, eliminar a cópia pesada: **VideoToolbox → CVPixelBuffer/IOSurface → `MTLTexture`** sem `memcpy` full-frame por frame, para intro 720p@30 barata em Apple Silicon.

**Architecture:** M0 entrega frames via `rsx_metal_movie_frame(rgba,…)`. M4 adiciona path preferencial:

```
VT decode → CVPixelFormat (NV12 ou BGRA) em IOSurface
         → CVMetalTextureCacheCreateTextureFromImage
         → blit/sample no metal_present
```

Fallback: path M0 (RGBA CPU) se IOSurface/cache falhar.

**Tech Stack:** VideoToolbox, CoreVideo (`CVMetalTextureCache`), IOSurface, Metal; `movie_vt_metal.m`, `rsx_metal_backend.m`.

## Global Constraints

- **Só após M0 GREEN** (logo visível + EOS).
- Sem ffmpeg; sem reencode.
- Manter `movie_hle_overlay_done` / pacing / wav.
- Medir CPU antes/depois (opcional `PS3_PERF_MOVIE=1`: ms/frame, frames dropped).

---

### Task 1–3: Implementado 2026-07-21

- [x] `rsx_metal_movie_frame_cvpixel` + `CVMetalTextureCache`; present sample + completed-handler retain
- [x] `movie_vt_metal` default iosurface; `PS3_MOVIE_FORCE_RGBA=1` CPU fallback
- [x] Smoke: `path=iosurface frames=330 avg_ms=4.10` vs rgba-cpu `4.65`; note em `docs/notes/2026-07-21-macos-metal-iosurface.md`

---

## Aceite M4

| ID | Critério |
|----|----------|
| Z1 | Path IOSurface/CV activo no log em run normal |
| Z2 | Logo continua correcto visualmente |
| Z3 | Fallback RGBA ainda funciona se forçar env `PS3_MOVIE_FORCE_RGBA=1` |
| Z4 | CPU/frame ≤ path M0 (ou documentar se igual em 720p trivial) |

## Fora de escopo

HDR, 4K, decode multi-stream, AudioQueue low-latency redesign.
