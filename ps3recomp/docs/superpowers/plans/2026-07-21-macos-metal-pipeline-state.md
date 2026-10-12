# Mac Metal — pipeline state RSX: blend / depth / stencil / scissor / masks (M5)

> **For agentic workers:** `subagent-driven-development` ou `executing-plans`.  
> **Depende de:** M1 (draw path com encoder). **Índice:** `2026-07-21-00-macos-metal-stack-INDEX.md`.

**Goal:** Preencher os callbacks `set_blend`, `set_depth_stencil`, e aplicar no draw/clear **color mask**, **alpha test** (emulado em FS se preciso) e **scissor** a partir de `rsx_state`, para o Mac não ignorar estado que o D3D12 já honra.

**Architecture:** Guardar snapshot no userdata Metal quando os hooks disparam (ou ler `rsx_state*` no draw, como partes do D3D12). Traduzir enums RSX → `MTLRenderPipelineDescriptor` / `MTLDepthStencilDescriptor` / `MTLScissorRect`. PSO variants: cache por (blend×depth×mask) **ou** dynamic state onde Metal permitir.

**Tech Stack:** Metal; `rsx_state` em `rsx_commands.h`; referência `d3d12_set_blend` / `d3d12_set_depth_stencil` (~2473+).

## Global Constraints

- Política Apple: nativo Metal, sem software blend.
- Não inventar defaults silenciosos sem log em `PS3_TRACE_RSX_STATE=1`.
- Windows intacto.

---

### Task 1–3: Implementado 2026-07-21

- [x] Hooks `set_blend` / `set_depth_stencil` / `set_color_mask` / `set_alpha_test`
- [x] Scissor + depth texture + DS cache no present; blend/mask no PSO
- [x] Clear depth/stencil flags; `PS3_TRACE_RSX_STATE` (cap 200)
- [x] Smoke: hooks + `draw_arrays` + PSO rebuild on blend change

---

## Aceite M5

| ID | Critério |
|----|----------|
| P1 | `set_blend` e `set_depth_stencil` ≠ NULL |
| P2 | Scissor aplicado quando `rsx_state` define rect ≠ full |
| P3 | Clear com depth não é no-op se flag depth set |
| P4 | Sem regressão M1 draws |

## Fora de escopo

MRT completo (M7); dual-source blend exótico; alpha test exact NV40 em todos os modes (emular subset).
