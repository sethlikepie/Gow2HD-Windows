# Mac Metal — render targets e depth surface (M7)

> **For agentic workers:** `subagent-driven-development` ou `executing-plans`.  
> **Depende de:** M1; M5 (depth state) recomendado.

**Goal:** `set_render_target` deixa de ser stub: criar/gerir **color RT** e **depth/zeta** como `MTLTexture` a partir dos offsets/pitch/format em `rsx_state` (local memory RSX), e o present faz blit da RT resolvida para o drawable (como o D3D12 resolve flip).

**Architecture:** Cache de surfaces keyed por `(location, offset)` — padrão de `rsx_live_draw.c` `surface_get`. Memory: mapear ponteiro guest/host da RSX local (`0xC0000000` commit no boot Mac) se o backend já a expõe; senão texture-only e copies nos EFB→display. MVP: **uma** color RT + depth opcional; present = blit RT0 → swapchain.

**Tech Stack:** Metal textures `private`/`shared`; ref `d3d12_set_render_target`, live_draw surfaces.

## Global Constraints

- Não commitar dumps de framebuffer de jogo.
- Preferir uma resolução de present (1280×720) no MVP.
- Política Apple: Metal nativo.

---

### Task 1–3: Implementado 2026-07-21

- [x] Cache `MetalSurface` (color + depth) por loc/offset
- [x] `set_render_target` selecciona RT; present draws → RT → blit/sample drawable
- [x] Smoke demo multi-cor no dump; clamp de clip absurdos

---

## Aceite M7

| ID | Critério |
|----|----------|
| R1 | `set_render_target` faz mais que log |
| R2 | Present mostra conteúdo da RT (não só clear do drawable sem draws) |
| R3 | Depth texture opcional sem crash |
| R4 | M1/M5 não regredidos |

## Fora de escopo

MRT multi-color completo; MSAA resolve; tiled memory exact; live_draw port.
