# M6 — set_vertex_attribs + formats (2026-07-21)

## Implementação

| Peça | Detalhe |
|------|---------|
| `rsx_to_mtl_vertex_format_tag` | float/float2–4, half2/4, uchar4(_norm), short2/4_norm |
| `metal_set_vertex_attribs` | Snapshot 16 slots, `attrib_sig`, `layout_ok` / `layout_unknown` |
| Upload | Guest BE → LE float4; pos@0 + color@3 → `MetalBasicVertex` |
| Fallback | tipo pos desconhecido → placeholder + log (sem crash) |
| Descriptor | M1/M2: attr0=float3, attr1=float4 (interleaved 28 B) |

## Aceite

| ID | Resultado |
|----|-----------|
| V1 | `.set_vertex_attribs = metal_set_vertex_attribs` |
| V2 | Demo: `M6 float3 position path (demo BasicVertex …)` + guest path quando attrib0 float |
| V3 | Unsupported type → log `[UNSUPPORTED]` / placeholder, sem hang |
| V4 | M1 demo + draw_arrays sem regressão |

## Trace

```bash
PS3_TRACE_RSX_ATTRIBS=1
# shutdown: [RSX metal] attribs: layout_ok=… upload_ok=… fallback=…
```

## Smoke

```bash
PS3_RSX_BACKEND=metal PS3_METAL_DEMO_DRAW=1 PS3_TRACE_RSX_ATTRIBS=1 \
  PS3_NOMOVIES=1 ./boot_gow2 EBOOT.ELF
```
