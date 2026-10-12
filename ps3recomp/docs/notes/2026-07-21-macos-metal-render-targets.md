# M7 — Render targets + depth surface (2026-07-21)

## Implementação

| Peça | Detalhe |
|------|---------|
| `MetalSurface` cache | key `(loc, offset, is_depth)`, max 16 |
| `metal_set_render_target` | cria/selecciona color RT + zeta opcional |
| Dimensões | clip w/h; clamp se >1920×1080 (lixo guest pré-init) |
| Present | draws → RT offscreen → blit/sample → drawable |
| Movie | continua path directo no drawable (M0/M4) |

## Aceite

| ID | Resultado |
|----|-----------|
| R1 | `set_render_target` cria surfaces + log `surface color/depth …` |
| R2 | `PS3_METAL_DEMO_DRAW=1` + dump: multi-cor (triângulo na RT) |
| R3 | depth surface / fallback `s_depth_tex` sem crash |
| R4 | M1 demo + M5 blend rebuild OK |

## Smoke

```bash
PS3_RSX_BACKEND=metal PS3_METAL_DEMO_DRAW=1 PS3_MOVIE_HLE=0 \
  PS3_FRAME_DUMP=1 ./boot_gow2 EBOOT.ELF
# log: surface color loc=0 off=0x0 1280x720
# frame_*.bmp com cores ≠ clear sólido
```

## Shutdown counters

`[RSX metal] RT: selects=… creates=… surfaces=… cidx=… didx=…`
