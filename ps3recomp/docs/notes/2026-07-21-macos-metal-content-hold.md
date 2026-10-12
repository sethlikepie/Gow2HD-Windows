# M8 — Content hold / HOSTRES sticky (2026-07-21)

## API

| Função | Papel |
|--------|--------|
| `rsx_metal_content_hold(rgba,w,h)` | Guarda RGBA, converte BGRA, sticky overlay |
| `rsx_metal_force_bind_rgba(unit,…)` | Upload MTLTexture unit (M3) |
| `rsx_metal_movie_playback_begin/end` | Pausa / retoma hold |
| `rsx_host_content_hold` / `force_bind` | Dispatch Apple→Metal, Win→D3D12 |

Call sites: `host_res_inflate.c` (legalscreen720), `host_wad_tex.c` (`PS3_WAD_TEX_HOLD`).

## Present priority

```
movie HLE  >  content hold  >  guest RT (M7)  >  clear
```

## Env

| Var | Efeito |
|-----|--------|
| `PS3_METAL_CONTENT_HOLD_DEMO=1` | checkerboard 64×64 sintético (smoke) |
| `PS3_CONTENT_HOLD_AFTER_MOVIE=1` | re-arma hold após EOS (default off) |
| `PS3_WAD_TEX_HOLD=1` | WAD tex arma hold |

## Aceite

| ID | Resultado |
|----|-----------|
| H1 | DEMO hold dump: magenta+verde (unique≥2) |
| H2 | `[CONTENT] pause hold for movie HLE` com movie |
| H3 | Win `rsx_d3d12_*` via `rsx_host_*` em `_WIN32` |

## Smoke

```bash
PS3_RSX_BACKEND=metal PS3_METAL_CONTENT_HOLD_DEMO=1 PS3_MOVIE_HLE=0 \
  PS3_FRAME_DUMP=1 ./boot_gow2 EBOOT.ELF
# [CONTENT] DEMO hold checkerboard 64x64
# [CONTENT] hold stored 64x64
```
