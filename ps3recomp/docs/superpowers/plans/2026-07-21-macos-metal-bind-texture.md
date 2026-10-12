# Mac Metal — bind_texture + deswizzle (M3) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: `superpowers:subagent-driven-development` ou `superpowers:executing-plans`.

**Goal:** Implementar `bind_texture` no backend Metal com upload de dados guest (deswizzle Morton/DXT quando aplicável) para `MTLTexture`, de forma que draws M1/M2 amostram texturas reais (WAD / GCM) — paridade com o path D3D12 + `rsx_texture_deswizzle`.

**Architecture:** `rsx_backend.bind_texture` hoje é `NULL` no Metal. Preencher com criação/atualização de `id<MTLTexture>` por unit, formato mapeado de `rsx_texture_formats`, CPU deswizzle → RGBA8 ou BC* se Metal suportar o formato comprimido. Sampler state básico (clamp/wrap, filter).

**Tech Stack:** `rsx_texture_deswizzle.h`, `rsx_texture_formats.h`, `rsx_metal_backend.m`, referência `rsx_d3d12_backend.c` textures[].

## Global Constraints

- Depende de **M1** (draw); **M2** opcional mas desejável (shader que sample `[[texture(0)]]`).
- Dados de textura **nunca** commitados; dumps PNG só locais/`/tmp`.
- Preferir **uma cópia** host: deswizzle directo para buffer shared mapeável; evitar staging triplo.
- Formatos: MVP **A8R8G8B8 / RGBA8 e DXT1** (legal screen); outros formats log + fallback 1×1 white.

---

### Task 1–4: Implementado 2026-07-21

- [x] `metal_format_from_rsx` (A8R8G8B8, DXT1/3/5, 16-bit expand, B8/G8B8)
- [x] `metal_bind_texture` + 16 slots + dummy fallback no present
- [x] Deswizzle Morton via `rsx_deswizzle_texels` (não-BC)
- [x] Sampler linear/clamp; `PS3_METAL_TEX_DEMO` checkerboard + frame dump ≠ solid

---

## Aceite M3

| ID | Critério |
|----|----------|
| T1 | `bind_texture != NULL` e é chamado sem crash |
| T2 | Dump/frame com conteúdo ≠ cor sólida do clear (in-boot ou sintético documentado) |
| T3 | DXT1 ou RGBA path medido |
| T4 | Win D3D12 intacto |

## Dependências externas

- Conteúdo WAD real: plano **4.EOS+** / **04-content**. M3 pode GREEN com textura sintética; promover a “conteúdo jogo” só com WAD.
