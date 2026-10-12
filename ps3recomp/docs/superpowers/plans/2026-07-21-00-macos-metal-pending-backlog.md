# Backlog — o que falta implementar / portar para Metal (macOS)

> Inventário **completo** do gap vs D3D12/Windows + planos de fecho.
> Política: `Claude.md` § Port Apple. Índice de execução:
> [`2026-07-21-00-macos-metal-stack-INDEX.md`](2026-07-21-00-macos-metal-stack-INDEX.md).

**Fonte da verdade (vtable):** `libs/video/rsx_commands.h` (`rsx_backend`, ~315–346).  
**Referência Win:** `libs/video/rsx_d3d12_backend.c` (vtable ~2539–2553 + extras).  
**Mac actual:** `libs/video/rsx_metal_backend.m` (vtable ~240–254).

---

## 1. Matriz `rsx_backend` (vtable)

| Callback | D3D12 | Metal hoje | Plano |
|----------|:-----:|:----------:|-------|
| `init` / `shutdown` | ✅ | ✅ | — |
| `begin_frame` / `end_frame` | ✅ | ✅ (fraco) | M1 melhora |
| `present` | ✅ | ✅ movie + draws | M0/M1/M4 |
| `clear` | ✅ | ✅ cor | M5 depth/stencil clear |
| `set_render_target` | ✅ | ✅ surface cache | **M7 GREEN** |
| `set_viewport` | ✅ | 🟡 parcial | M1/M5 |
| `set_blend` | ✅ | ✅ PSO rebuild | **M5 GREEN** |
| `set_depth_stencil` | ✅ | ✅ DS cache | **M5 GREEN** |
| `set_color_mask` | ✅* | ✅ writeMask | **M5 GREEN** |
| `set_alpha_test` | ✅* | 🟡 log only | **M5** (FS subset later) |
| `set_shader` | ✅ HLSL PSO | ✅ MSL PSO cache | **M2 GREEN** |
| `set_vertex_attribs` | ✅ | ✅ snapshot+upload | **M6 GREEN** |
| `draw_arrays` | ✅ | ✅ M1+M2 | **M1 GREEN** |
| `draw_indexed` | ✅ | ✅ M1+M2 | **M1 GREEN** |
| `bind_texture` | ✅ | ✅ deswizzle+upload | **M3 GREEN** |

\* D3D12 implementa via state sujo no draw path; Metal precisa dos hooks ou de ler `rsx_state` no draw.

---

## 2. Extras fora da vtable (só Win / host)

| Feature | Onde (Win) | Metal | Plano |
|---------|------------|-------|-------|
| Movie overlay HLE | `movie_hle.c` + `rsx_d3d12_movie_*` | stubs POSIX | **M0** (+ **M4** zero-copy) |
| Content hold HOSTRES / legal | `rsx_d3d12_content_hold` | ✅ metal + host | **M8 GREEN** |
| `rsx_d3d12_force_bind_rgba` | d3d12 | ✅ `rsx_metal_force_bind_rgba` | **M8 GREEN** |
| cellVdec → frames no ecrã | decode host + guest buffer; present via RSX | decode existe; **present fraco** | **M9** |
| `rsx_live_draw` (capture-replay) | D3D12 only | N/A Mac | **adiado** (não bloquear M*) |
| FP/VP validate tests | `D3DCompile` | sem `metallib` CI | **M2** task testes Apple |
| Default backend Mac | env `sdl` em `rodar_gow2.sh` | ✅ metal default | **M10 GREEN** |

---

## 3. Decompilers / IR

| Peça | Hoje | Metal precisa | Plano |
|------|------|---------------|-------|
| `rsx_fp_decompiler` | → **HLSL** | → **MSL** (`_msl`) | **M2 GREEN** |
| `rsx_vp_decompiler` | → **HLSL** | → **MSL** (`_msl`) | **M2 GREEN** |
| PSO cache por hash bytecode | D3D12 | Metal | **M2 GREEN** |

---

## 4. Media / áudio (não-RSX mas host visual)

| Peça | Win | Mac alvo | Plano |
|------|-----|----------|-------|
| Overlay `.m2v` | ffmpeg pipe | VideoToolbox | **M0** |
| Áudio `.wav` intro | winmm PlaySound | AudioQueue | **M0** |
| Zero-copy frames | n/a | IOSurface | **M4** |
| EOS `overlay_done` | real | 0 stub | **M0** |

---

## 5. Ordem recomendada (pendente)

```
Já planeado:  M0 VT overlay │ M1 draw │ M2 MSL │ M3 tex │ M4 zero-copy
Novo:         M5 state (blend/depth/scissor/mask)
              M6 vertex attribs
              M7 render targets / depth surface
              M8 content hold HOSTRES
              M9 cellVdec YUV → Metal (pixels caminho natural)
              M10 default metal + smoke matrix Mac
```

**Paralelo:** M0 ∥ M1; M5/M6 com M1; M7 após M1; M8 após M0 ou M1 present; M9 após M1+M7 (ou blit simples); M2/M3 após M1; M4 após M0.

**Não é port Metal (continua noutros planos):** vdec Open handle=0, FORCE, WAD guest, ICGLdr typemap, lifter fallthrough.

---

## 6. Tabela rápida “o que cada plano desbloqueia”

| Plano | Utilizador vê | Sistema ganha |
|-------|---------------|---------------|
| M0 | Logo intro | EOS real, sem ffmpeg |
| M1 | Geometria / cores | Draws deixam de ser no-op |
| M2 | Materiais/shaders jogo | PSO decompilados |
| M3 | Texturas | bind real |
| M4 | Logo mais barato CPU | Zero-copy |
| M5 | Alpha/depth correctos | Menos lixo visual |
| M6 | Malhas correctas | Atributos guest |
| M7 | RTs / profundidade | Multpass RSX |
| M8 | Legal/HOSTRES sticky | Paridade content hold |
| M9 | Frames pós-vdec | Pixels caminho natural |
| M10 | Menos fricção | metal default + smokes |

---

## 7. Links dos planos

| ID | Ficheiro |
|----|----------|
| INDEX | `2026-07-21-00-macos-metal-stack-INDEX.md` |
| M0 | `2026-07-21-macos-videotoolbox-metal-overlay.md` |
| M1 | `2026-07-21-macos-metal-rsx-draw-path.md` |
| M2 | `2026-07-21-macos-metal-shader-msl.md` |
| M3 | `2026-07-21-macos-metal-bind-texture.md` |
| M4 | `2026-07-21-macos-metal-iosurface-zerocopy.md` |
| M5 | `2026-07-21-macos-metal-pipeline-state.md` |
| M6 | `2026-07-21-macos-metal-vertex-attribs.md` |
| M7 | `2026-07-21-macos-metal-render-targets.md` |
| M8 | `2026-07-21-macos-metal-content-hold.md` |
| M9 | `2026-07-21-macos-metal-vdec-present.md` |
| M10 | `2026-07-21-macos-metal-default-ops.md` |
