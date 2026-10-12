# Mac Metal — FP/VP decompiler → MSL + PSO cache (M2) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: `superpowers:subagent-driven-development` ou `superpowers:executing-plans`.

**Goal:** Paridade conceptual com o path D3D12 `set_shader` → decompile NV40 → compile → PSO cache, mas com **MSL + `MTLRenderPipelineState`**, para o Mac executar fragment/vertex programs do jogo (quando o guest os enviar).

**Architecture:** Hoje `rsx_fp_decompiler` / `rsx_vp_decompiler` emitem **HLSL**. Opções (escolher na Task 0 e fixar):

| Opção | Prós | Contras |
|-------|------|---------|
| **A. Backend MSL nos decomps** | Nativo, performático, sem extra | Duplicar templates HLSL→MSL |
| **B. HLSL→MSL via SPIRV-Cross** | Reusa HLSL actual | Dep externa; menos “baixo nível” |
| **C. Emitir SPIR-V e Metal via SPIRV-Cross** | Um IR | Mesma dep; mais stack |

**Política Apple (`Claude.md`):** preferir **A** (emitir MSL directo no decompiler ou `rsx_*_decompiler_msl.c`) para menos deps e melhor controlo. B/C só se A explodir o schedule.

**Tech Stack:** `rsx_fp_decompiler.*`, `rsx_vp_decompiler.*`, `rsx_metal_backend.m`, cache PSO como `ShaderPsoSet` no D3D12.

## Global Constraints

- Depende de **M1** (draw path com encoder e set_pipeline).
- Não aceitar `PS3_SHADER_DEMO` como “shaders do jogo”.
- CRC bypass / combination stub **proibidos** como aceite (só diag gated).
- Cache de PSO keyed por hash do bytecode NV40 (como D3D12 `fp_hash`/`vp_hash`).

---

### Task 0: Decisão A/B/C + spike 1 shader

- [x] **Step 1–3:** **MSL_EMIT=A** — HLSL dialect convert, sem SPIRV-Cross. Note: `docs/notes/2026-07-21-macos-metal-msl.md`. Spike MOV+TEX → Metal library/PSO OK offline.

---

### Task 1: Emissor MSL (FP)

- [x] `rsx_fp_decompile_msl` + teste `libs/video/tests/test_msl_decompiler.c`

---

### Task 2: Emissor MSL (VP)

- [x] `rsx_vp_decompile_msl` + passthrough VS no backend se VP ausente/fail

---

### Task 3: PSO cache no Metal backend

- [x] `metal_set_shader` — FNV cache 32, draw snapshot PSO, dummy tex 0..15, `PS3_TRACE_RSX_SHADERS`

---

### Task 4: Smoke in-boot (quando guest enviar shader)

- [x] Fixture `PS3_SHADER_DEMO=1`: `compiled PSO … fp=1 instr hash=0x9ECDEF7F` + `draw_arrays` (2026-07-21)

---

## Aceite M2

| ID | Critério |
|----|----------|
| S1 | Pelo menos 1 FP decompilado → MSL → PSO sem crash |
| S2 | Fallback PSO M1 se decompile falhar (sem silent black forever sem log) |
| S3 | Sem CRC bypass como “fix” |
| S4 | D3D12 HLSL path intacto |

## Fora de escopo

Texturas reais (M3); dual-source blend exótico; precision full NV40.
