# M5 — Metal pipeline state: blend / depth / scissor / masks (2026-07-21)

## Implementação

| Hook | Comportamento |
|------|----------------|
| `set_blend` | Snapshot + rebuild PSO (blend factors / equation) |
| `set_depth_stencil` | Snapshot → `MTLDepthStencilState` cache |
| `set_color_mask` | writeMask no color attachment; rebuild PSO |
| `set_alpha_test` | Snapshot + log (`PS3_TRACE_RSX_STATE`); FS discard out of scope |
| clear | Cor + depth/stencil flags (`RSX_CLEAR_*`); depth attach no present |
| scissor | `setScissorRect` no encoder (viewport / RT / attribs refresh) |

## Mapa RSX → Metal (resumo)

- Blend factors: `0x0000/1` ZERO/ONE, `0x0300–308` GL src/dst, `0x8001–4` blend color  
- Compare: `0x0200–207` NEVER…ALWAYS  
- Color mask bits: B@0 G@8 R@16 A@24 → `MTLColorWriteMask*`

## Aceite

| ID | Resultado |
|----|-----------|
| P1 | `set_blend` / `set_depth_stencil` / `set_color_mask` / `set_alpha_test` ≠ NULL |
| P2 | Scissor aplicado no present (guest draws) |
| P3 | clear flags depth/stencil honrados no depth attach; log `clear flags=…` |
| P4 | M1/M2 draws OK (`draw_arrays`, `compiled PSO`) |

## Smoke

```bash
PS3_RSX_BACKEND=metal PS3_TRACE_RSX_STATE=1 PS3_METAL_DEMO_DRAW=1 \
  PS3_SHADER_DEMO=1 PS3_NOMOVIES=1 ./boot_gow2 EBOOT.ELF
# expect: set_blend / set_depth_stencil / draw path PSO ready / draw_arrays
```
