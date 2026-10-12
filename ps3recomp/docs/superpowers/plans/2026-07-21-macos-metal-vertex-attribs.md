# Mac Metal — set_vertex_attribs + layouts (M6)

> **For agentic workers:** `subagent-driven-development` ou `executing-plans`.  
> **Depende de:** M1. Idealmente antes ou junto de M2 (shaders com inputs correctos).

**Goal:** Portar `set_vertex_attribs` do D3D12 para Metal: interpretar `rsx_state` vertex attribute array (offsets, strides, types, frequency) e construir `MTLVertexDescriptor` + binds de buffer correctos nos draws.

**Architecture:** D3D12 usa `d3d12_set_vertex_attribs` (~2122) e records por draw. Metal: cache de `MTLVertexDescriptor` keyed por assinatura dos attribs activos; no `draw_*`, `setVertexBuffer` com strides guest. Tipos: float3/float4/u8n common path first.

**Tech Stack:** `rsx_state` vertex fields; `rsx_metal_backend.m`; ref D3D12.

## Global Constraints

- Unified memory: buffers shared.
- Fallback: se layout desconhecido, log + skip draw (não crash).
- Política Apple: nativo Metal.

---

### Task 1–3: Implementado 2026-07-21

- [x] `rsx_to_mtl_vertex_format_tag` + position/color OK helpers
- [x] `metal_set_vertex_attribs` snapshot + sig + TRACE_RSX_ATTRIBS
- [x] Upload multi-type (float/half/ubyte/snorm16) pos@0 color@3 → BasicVertex
- [x] Smoke: demo float3 path + set_vertex_attribs + draw_arrays

---

## Aceite M6

| ID | Critério |
|----|----------|
| V1 | Hook não-NULL |
| V2 | Pelo menos float3 position path medido |
| V3 | Layout desconhecido → log, sem crash |
| V4 | M1 não regrediu |

## Fora de escopo

Instancing completo; todos os packed formats NV40; divisor frequency edge cases.
