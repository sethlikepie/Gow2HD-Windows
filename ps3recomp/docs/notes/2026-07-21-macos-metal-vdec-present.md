# M9 — cellVdec → Metal present (2026-07-21)

## Path

```
cellVdec decode (ffmpeg YUV420P or DEMO synth)
  → vdec_yuv420p_to_bgra
  → rsx_metal_movie_frame_stage (BGRA)
  → metal present on GCM flip / pump
```

## Env

| Var | Default | Efeito |
|-----|---------|--------|
| `PS3_VDEC_METAL_PRESENT` | **1** on Apple (off if `=0`) | enable present hook |
| `PS3_VDEC_METAL_DEMO=1` | off | autostart after QueryAttrEx when Open wall |
| `PS3_VDEC_METAL_DEMO_SYNTH=1` | off | force synthetic bars (no m2v/ffmpeg) |
| `PS3_MOVIE_HLE=1` | — | **defers** vdec present (HLE wins) |

## Priority

1. Movie HLE overlay (`hle_source=1`)  
2. cellVdec staged frames (`hle_source=0`)  
3. Content hold (M8)  
4. Guest RT (M7)

## Aceite

| ID | Resultado |
|----|-----------|
| C1 | DEMO: `[vdec-metal] frame N … staged` N≥1 (multi-frame proved) |
| C2 | Guest Open still wall: QueryAttrEx only until intro-vdec; DEMO is honest fixture |
| C3 | HLE: `path=iosurface` + no vdec steal when HLE on |

## Smoke

```bash
# Multi-frame present path (fixture)
PS3_RSX_BACKEND=metal PS3_MOVIE_HLE=0 PS3_VDEC_METAL_PRESENT=1 \
  PS3_VDEC_METAL_DEMO=1 PS3_VDEC_METAL_DEMO_SYNTH=1 ./boot_gow2 EBOOT.ELF

# HLE still works
PS3_RSX_BACKEND=metal PS3_MOVIE_HLE=1 ./boot_gow2 EBOOT.ELF
```

## Note

Real guest Open+ffmpeg decode works when Open≠0 and `/opt/homebrew/bin/ffmpeg` is available; M9 hooks the same present path.
