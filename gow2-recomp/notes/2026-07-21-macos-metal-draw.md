# M1 Metal draw path — GREEN 2026-07-21

## Implementacao (ps3recomp)
`rsx_metal_backend.m`:
- PSO fixo MSL pos+color (28-byte vertex)
- VB/IB `MTLResourceStorageModeShared`
- `draw_arrays` / `draw_indexed` record + replay no present
- `set_vertex_attribs` / `set_shader` gravam `current_rsx` (M2 depois)
- `PS3_METAL_DEMO_DRAW=1` injecta triangulo NDC multi-cor se 0 draws guest

## Smoke
```bash
./smoke_metal_draw_mac.sh
# ou
PS3_RSX_BACKEND=metal PS3_METAL_DEMO_DRAW=1 PS3_FRAME_DUMP=1 ./boot_gow2 EBOOT.ELF
```

## Evidencia
- `draw path PSO ready`
- bridge `mode=4 keeping pre-registered backend`
- `frame_*.bmp` 2560x1440: cor clear (0,0,64) + cores do triangulo demo (unique >> 1)
- sem NSException / sem replace por trace

## Proximo
M2 MSL decompiler; M5 blend/depth; guest draws reais quando FIFO emitir.
