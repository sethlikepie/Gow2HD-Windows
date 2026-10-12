# Plan 05 — RSX shaders & real pixels (W17–W23) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: subagent-driven-development or executing-plans.

**Goal:** Frames do **jogo** (não só clear color / `PS3_SHADER_DEMO`) via pipeline GCM→RSX→backend host (D3D12 Win; Metal/Vulkan Mac), com testes de decompiler e deswizzle verdes, e smoke de frame dump.

**Architecture:** `cellGcmSys` + `rsx_commands.c` (FIFO NV47xx) → vtable backend (`rsx_d3d12` / `rsx_vulkan` / `rsx_metal` / SDL null). Shaders: `rsx_fp_decompiler` / `rsx_vp_decompiler` → HLSL/SPIR-V/MSL. Texturas: `rsx_texture_deswizzle.h` + bind path. Plano irmão: `2026-07-14-gow2-rsx-content-vp-tex-spu.md`.

**Tech Stack:** D3D12, Vulkan/MoltenVK, Metal, SDL2, `libs/video/tests/*`, `PS3_FRAME_DUMP`, `PS3_TRACE_RSX_SHADERS`.

**Depends on:** Plano 04 (jogo emite `SET_TEXTURE` / VP de verdade; senão só demos sintéticos).  
**Unblocks:** menu jogável / polish (W24+).

## Global Constraints

- Distinguir sempre: **in-boot jogo** vs **sintético** vs **unit offline**.
- Não afirmar “pixels do jogo” se `vp_load=0` e `texture_binds=0`.
- Mac: aceitar D3D12 Win como referência visual até Metal/VK paridade.

---

### Task 1: Contadores de pipeline (TDD de observabilidade)

**Files:**
- `libs/video/rsx_commands.c`, backends
- Create/extend: counters `draw_calls`, `tex_binds`, `vp_load`, `fp_load`, `flip_count`

- [ ] **Step 1: Vermelho — baseline boot atual**

```bash
# 30s run com PS3_TRACE_RSX=1 ou dump no shutdown
# expected no wall de conteúdo antigo: vp_load=0, tex_binds=0, flips>0 (clear)
```

- [ ] **Step 2: Teste de regressão**

```bash
# smoke_rsx_counters.sh
# FAIL se flip_count==0 (pipeline morto)
# WARN se tex_binds==0 (ainda sem conteúdo — Plano 04)
```

- [ ] **Step 3: Verde — contadores impressos no shutdown**

```c
fprintf(stderr, "[RSX] flips=%u draws=%u tex=%u vp=%u fp=%u\n", ...);
```

- [ ] **Step 4: Commit**

---

### Task 2: Deswizzle unit tests (já existem — trancar)

**Files:**
- `libs/video/tests/test_texture_deswizzle.c`
- `libs/video/rsx_texture_deswizzle.h`

- [ ] **Step 1: Vermelho se alguém regredir**

```bash
clang -std=c11 -O1 -I libs/video tests/test_texture_deswizzle.c -o /tmp/tdesw
/tmp/tdesw
# expected: PASS bijection + golden
```

- [ ] **Step 2: Integrar no `scripts/smoke_macos.sh` ou CMake test target**
- [ ] **Step 3: Commit** se wiring CI

---

### Task 3: VP→shader compile offline (TDD)

**Files:**
- `libs/video/rsx_vp_decompiler.c`
- `libs/video/tests/test_vp_ps_link.c` (Win D3DCompile)

- [ ] **Step 1: Vermelho em Mac sem D3DCompile**

Separar:

| Host | Assert |
|------|--------|
| Win | VP→HLSL→`D3DCompile` S_OK + reflect |
| Mac | VP→MSL ou SPIR-V text non-empty + optional `spirv-as` |

- [ ] **Step 2: Fixture NV40 mínima no repo (sem assets de jogo)**

```c
// bytes de vertex program sintético
// rsx_vp_decompile(...)
// assert output contains "main" / entry
```

- [ ] **Step 3: Verde**
- [ ] **Step 4: Commit**

---

### Task 4: FP decompiler + cache (TDD)

**Files:**
- `libs/video/rsx_fp_decompiler.c`
- Backend cache (`fp_cache_hits/misses`)

- [ ] **Step 1: Vermelho**

```bash
# unit: fragment program fixture → HLSL/SPIR-V
# fail se decompile_fail++
```

- [ ] **Step 2: Verde path de produção**

```
d3d12_set_shader / vulkan_set_shader → decompile → compile → PSO
```

- [ ] **Step 3: In-boot sintético**

```bash
PS3_SHADER_DEMO=1  # se existir
# expected: fp_cache_hits cresce (já observado no master com demo)
```

- [ ] **Step 4: Commit**

---

### Task 5: Draw path com textura (integração)

**Pré-condição:** Plano 04 fez `tex_binds > 0` **ou** usar capture WAD offline.

**Files:**
- `rsx_d3d12_backend.c` / `rsx_vulkan_backend.c` / `rsx_metal_backend.m`
- `rsx_live_draw.c` se existir

- [ ] **Step 1: Vermelho**

```bash
# frame dump PNG hash == solid clear only
PS3_FRAME_DUMP=1
# compare to known clear color
```

- [ ] **Step 2: Verde mínimo**

Um draw texturizado (quad fullscreen com textura do WAD legal) — pode ser path HLE de prova (`patch_wad_tex_capture`) já usado no Win.

- [ ] **Step 3: Assert**

```bash
# PNG não é cor sólida; SSIM ou histogram variance > threshold
python3 tools/check_frame_not_clear.py frame_000.png
```

- [ ] **Step 4: Commit**

---

### Task 6: Mac backend parity (clear → textured)

**Files:**
- `rsx_metal_backend.m`, `rsx_vulkan_backend.c`, `rsx_null_backend.c` (SDL)

- [ ] **Step 1: Vermelho**

```bash
PS3_RSX_BACKEND=metal ./boot_gow2 ...
# fail se init -1 ou janela não sobe
```

- [ ] **Step 2: Verde — init + clear + present**
- [ ] **Step 3: Verde — mesmo dump texturizado que Task 5 (se SPIR-V/MSL ready)**
- [ ] **Step 4: Commit**

Se Metal completo for longo: **aceite parcial** = Vulkan/MoltenVK textured dump.

---

### Task 7: Smoke visual

**Files:**
- Create: `gow2-recomp/smoke_visual.sh`
- Ref: `bt_visual_combo.sh`, `bt_pixels.sh`

```bash
#!/usr/bin/env bash
# 1) run 20s with FRAME_DUMP
# 2) at least 1 PNG
# 3) not solid clear (script)
# 4) optional: compare to golden legal_screen720.png (tolerance)
```

- [ ] **Step 1: Vermelho sem conteúdo**
- [ ] **Step 2: Verde com path de prova WAD**
- [ ] **Step 3: Commit**

---

### Task 8: Critério “pixels do jogo” (honestidade)

Checklist obrigatório no note de fecho:

```markdown
| Sinal | Valor |
|-------|-------|
| vp_load (jogo) | |
| tex_binds (jogo) | |
| fp decompile_ok | |
| frame source | jogo / HLE capture / SHADER_DEMO |
| backend | d3d12 / metal / vulkan |
```

**PASS “jogo”** só se `vp_load>0` **ou** `tex_binds>0` vindos do FIFO do título (não demo env).

- [ ] **Step 1: Preencher note**
- [ ] **Step 2: Commit note**

---

## Aceite do Plano 05

| Nível | Critério |
|-------|----------|
| **L4a** | Unit deswizzle + VP/FP offline PASS |
| **L4b** | Frame dump ≠ clear (prova visual) |
| **L4c** | Contadores mostram tex/vp do **jogo** |
| **L4d** | Mac ou Win com backend real (não null-only) |

**L4c = meta “pixels do jogo”.** L4b sozinho é progresso, não “jogo renderiza”.

**Done →** planos futuros W24–W28 (áudio, movies reais, pad, fases).

---

## Ordem TDD resumida (este plano)

```
1 counters (red/green)
2 deswizzle lock
3 VP offline
4 FP offline + cache
5 textured draw dump
6 Mac backend
7 smoke_visual
8 honesty note (jogo vs demo)
```
