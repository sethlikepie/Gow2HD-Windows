# Mac Metal RSX — draw path mínimo (M1) — ✅ GREEN 2026-07-21 — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: `superpowers:subagent-driven-development` ou `superpowers:executing-plans`. Steps com checkbox `- [ ]`.

**Goal:** O backend Metal deixa de ser “só clear color”: implementar `draw_arrays` / `draw_indexed` com **PSO fixo** (VS/FS embutidos em MSL), upload de vértices, e **frame dump** que prove geometria (não só cor sólida). Base para shaders decompilados (M2) e texturas (M3).

**Architecture:** Espelhar o contrato `rsx_backend` já usado por D3D12 (`rsx_commands.c` → vtable). Metal: um `MTLRenderPipelineState` hardcoded (passthrough pos+color ou pos+uv), dynamic VB/IB em `MTLBuffer` shared (Apple Silicon unified memory), record no `draw_*`, flush no `present`/`end_frame`. Sem MoltenVK neste plano.

**Tech Stack:** Metal / ObjC (`rsx_metal_backend.m`), MSL inline ou `.metal` compilado offline, `PS3_FRAME_DUMP`, CMake `build-macos`.

## Global Constraints

- Port Apple: **Metal nativo**, baixo nível; sem OpenGL; sem “software raster” como aceite.
- Unified memory: preferir `MTLResourceStorageModeShared` em buffers de upload (evitar copies inúteis).
- Probes gated; sem dados de jogo no git; commits PT; Windows intacto.
- Não exigir WAD/shaders guest para o aceite M1 (pode usar draw sintético / FIFO de teste).

---

## Estado actual (âncoras)

| Site | Hoje |
|------|------|
| `rsx_metal_backend.m:218–236` | `draw_arrays` / `draw_indexed` → log `TODO: no PSO yet` |
| `bind_texture` | `NULL` |
| `metal_present` | clear + present drawable |
| D3D12 referência | `rsx_d3d12_backend.c` draw records + PSO cache |

---

### Task 1: PSO fixo MSL + buffers

**Files:**
- Modify: `ps3recomp/libs/video/rsx_metal_backend.m`
- Optional create: `ps3recomp/libs/video/shaders/rsx_metal_basic.metal` (ou source string)

**Interfaces:**
- Produz: pipeline “pos.xy + color.rgba” ou NDC fullscreen-capable

- [ ] **Step 1: Escrever VS/FS MSL mínimos**

```metal
#include <metal_stdlib>
using namespace metal;
struct VIn  { float2 pos [[attribute(0)]]; float4 col [[attribute(1)]]; };
struct VOut { float4 pos [[position]]; float4 col; };
vertex VOut vs_main(VIn in [[stage_in]]) {
  VOut o; o.pos = float4(in.pos, 0, 1); o.col = in.col; return o;
}
fragment float4 fs_main(VOut in [[stage_in]]) { return in.col; }
```

- [ ] **Step 2: Compilar em runtime (`newLibraryWithSource`) ou build-time `metallib`**

Preferir **source embutido** no MVP (menos infra). Log erro de compile.

- [ ] **Step 3: Criar `MTLRenderPipelineState` + vertex descriptor (float2+float4)**

- [ ] **Step 4: Build**

```bash
cmake --build build-macos -j"$(sysctl -n hw.ncpu)"
# 0 erros
```

- [ ] **Step 5: Commit**

```bash
git add libs/video/rsx_metal_backend.m
git commit -m "feat(metal): PSO fixo MSL para draw path RSX"
```

---

### Task 2: Upload VB/IB + implementar draw_arrays / draw_indexed

**Files:**
- Modify: `rsx_metal_backend.m` (`metal_draw_arrays`, `metal_draw_indexed`, state)

**Interfaces:**
- Consome: vértices do estado RSX / path já usado no D3D12 (ler como o D3D12 obtém pos)
- Produz: draws no command encoder do frame

- [ ] **Step 1: Buffers shared reutilizáveis (ring ou grow)**

```c
// MTLBuffer *vb, *ib; size capacity; offset cursor per frame
```

- [ ] **Step 2: `metal_draw_arrays`** — copiar verts → `setVertexBuffer` → `drawPrimitives`

- [ ] **Step 3: `metal_draw_indexed`** — idem + index buffer

- [ ] **Step 4: Encoder no begin_frame/present**

Garantir que draws do frame actual são encoded **antes** do `presentDrawable` (não só clear).

- [ ] **Step 5: Remover/gate o log TODO** (só se path activo)

- [ ] **Step 6: Commit**

```bash
git commit -m "feat(metal): draw_arrays/indexed com upload de VB/IB"
```

---

### Task 3: Frame dump discriminador + smoke

**Files:**
- Modify: `metal_dump_frame` / env `PS3_FRAME_DUMP`
- Create: `gow2-recomp/smoke_metal_draw_mac.sh` (opcional se já houver smoke RSX)

**Interfaces:**
- Aceite: dump com **mais de uma cor** ou edge detectável vs clear sólido

- [ ] **Step 1: Smoke headless ou com janela**

```bash
# Gerar draws: preferir path de teste existente no runtime (PS3_SHADER_DEMO / probe)
# ou boot curto com backend metal + env que force um draw de teste
export PS3_RSX_BACKEND=metal
export PS3_FRAME_DUMP=1
# ... boot 15s PID-kill ...
# Verificar PNG/dump: não é cor única 100%
```

- [ ] **Step 2: Documentar comando na note `notes/2026-07-21-macos-metal-draw.md` (gow2)**

- [ ] **Step 3: Commit**

```bash
git commit -m "test(metal): smoke/frame dump prova draw path"
```

---

## Aceite M1

| ID | Critério |
|----|----------|
| D1 | `draw_arrays`/`draw_indexed` não são no-op |
| D2 | Frame dump ou observação visual ≠ clear-only |
| D3 | Build Mac verde; sem regressão Win |
| D4 | Unified memory / shared buffers (sem double-copy óbvio) |

## Fora de escopo M1

Decompiler MSL (M2), `bind_texture` (M3), movie VT (M0).
