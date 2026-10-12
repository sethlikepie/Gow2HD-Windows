# GoW2 RSX content + VP + textures + SPU2/3 — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fechar o pipeline gráfico real do GoW2 (WAD → shaders → VP+FP no D3D12 → texturas) e registrar SPUs 2/3 sem regressão de crash, reutilizando o que já existe no tree.

**Architecture:** O jogo carrega WADs via FIOS/`movie_hle`; o loader de shaders (`ICGLdrShader`) popula o container guest; o FIFO RSX empurra FP/VP/texturas para `rsx_commands` → backend D3D12. O FP já é decompilado e cacheado; o VP existe offline e precisa do mesmo wire. Texturas lineares já sobem; falta deswizzle + EA local/main. SPUs liftados já compilam; spu1 roda; spu2/3 ficam atrás de hardening LS/DMA e registro opt-in → default.

**Tech Stack:** ps3recomp C/C++ runtime, D3D12 + D3DCompile, `rsx_fp_decompiler` / `rsx_vp_decompiler`, `spu_workload` + imagens liftadas em `gow2_work/spu_lifted/`, env gates (`PS3_DUMP_SHADERS`, `PS3_SPU1`, `PS3_SPU_ALL`, `PS3_TRACE_SPUDMA`).

## Global Constraints

- Não regredir o loop ~40fps estável: SPU2/3 só no default depois de hard gate + crash isolation.
- Preferir wire do que já existe a reescrever decompiladores.
- Toda mudança de backend RSX deve ser verificável com unit tests em `libs/video/tests/` + boot GoW2 com dump/trace.
- Encoding: VP words já estão em host order em `rsx_state.transform_program` (como `rsx_vp_decompile` espera em LE host).
- Guest VM: qualquer leitura de EA passa por `ppu_guest_range_committed` antes de memcpy (padrão spu-dma / set_shader).

## Estado atual (âncoras no código)

| Área | O que já existe | Gap |
|------|-----------------|-----|
| Conteúdo / shaders | `movie_hle` serve `.wad_ps3`; log `[SHREG] REGISTRA ICGLdrShader`; FP cache em `d3d12_set_shader` | Confirmar cadeia WAD→container→FIFO com shaders reais; métricas de hit |
| VP | `rsx_vp_decompiler.{c,h}` + tests; `transform_program[]` capturado no FIFO | D3D12 usa só `s_vs_passthrough_hlsl` |
| Texturas | `d3d12_bind_texture` linear + BE swap + BC; `rsx_texture_formats.h` | Swizzle skip; location local/main frágil; pouco exercitado com dados reais |
| SPU | spu0 registrado default; spu1 via `PS3_SPU1`/`PS3_SPU_ALL` (user: spu1 estável); DMA com skip de EA inválida | spu2/3 não default; LS/DMA/task ABI ainda crasham se forçados |

### Ordem de dependência

```
Fase A  Conteúdo (WAD → ICGLdrShader → FP real no cache)
   │
   ├─► Fase B  VP no D3D12 (PSO = hash(FP) × hash(VP))
   │
   └─► Fase C  Texturas reais (deswizzle + EA + SRV)
          │
          └─► Fase D  SPU2/3 (LS/DMA harden → register default)
```

A e D são semi-independentes; B depende de A para corpus real; C depende de B só para “frame com UV certo”, mas o upload de textura pode começar em paralelo com B.

---

## Mapa de arquivos

| Arquivo | Responsabilidade |
|---------|------------------|
| `libs/video/movie_hle.c` | Cache host de `.wad_ps3` / `.m2v` |
| `gow2_work/recomp_mid_v2/*` (hooks SHREG, boot) | Instrumentação GoW2: ICGLdrShader, force-fixes |
| `libs/video/rsx_commands.c` / `.h` | FIFO state: FP addr, VP words, textures, dirty flags |
| `libs/video/rsx_fp_decompiler.c` | FP NV40 → HLSL (já wired) |
| `libs/video/rsx_vp_decompiler.c` | VP NV40 → HLSL (offline only) |
| `libs/video/rsx_d3d12_backend.c` | set_shader, bind_texture, PSO cache, draw |
| `libs/video/rsx_texture_formats.h` | Format/DXGI/swizzle bits |
| `libs/video/tests/validate_vp_*.c`, `test_fp_*.c` | Corpus offline |
| `tools/shadertest/harness.c` | fxc validation FP/VP |
| `runtime/spu/spu_dma.h` | MFC GET/PUT + bounds |
| `runtime/spu/spu_workload.c` | Dispatch fingerprint → lifted entry |
| `gow2_work/recomp_mid_v2/gow2_spu_register.c` | Registro spu0–3 + env gates |

---

### Task 1: Fase A — Observabilidade e aceite do carregamento de conteúdo

**Files:**
- Modify: `libs/video/rsx_d3d12_backend.c` (`d3d12_set_shader` — contadores)
- Modify: `libs/video/movie_hle.c` (log de WAD open/close se ainda sparse)
- Optional instrument: hooks SHREG no build GoW2 (`gow2_work/recomp_mid_v2`)
- Test: boot harness + contagem em stderr

**Interfaces:**
- Consumes: `PS3_DUMP_SHADERS`, `movie_hle` cache path, `d3d12_set_shader` path existente
- Produces: contadores estáveis `g_rsx_fp_cache_hits` / `g_rsx_fp_cache_misses` / `g_rsx_fp_real_programs`; dump dir populado com `fp_*.bin` / `vp_*.bin` de jogo

- [x] **Step 1: Confirmar baseline de boot com WAD**

Run from `gow2_work/recomp_mid_v2` (ou o cwd do boot atual):

```bash
# PowerShell / Git Bash — ajustar path do exe se diferente
PS3_DUMP_SHADERS=../shadertest/gow2_dump \
  ./gow2_boot.exe 2>&1 | tee content_a1.log
```

Expected in log (já visto em `movie.log` / `shreg.log`):
- `[movieio] open '...wad_ps3'`
- `[SHREG] ... REGISTRA ICGLdrShader (tipo 0xF85F9B1E)`
- eventualmente `[D3D12] set_shader: fp_addr=...` e/ou `dumped shader fp=...`

Se WAD não abre: checar `movie_cache/` e `PS3_MOVIE_CACHE` / path relativo em `movie_hle.c` (~linhas 204–350).

- [x] **Step 2: Adicionar contadores de FP real no backend**

Em `rsx_d3d12_backend.c`, junto do cache de shader (~linha 1810+):

```c
/* Diagnostics — content pipeline health */
static u32 s_fp_cache_hits;
static u32 s_fp_cache_misses;
static u32 s_fp_decompile_ok;
static u32 s_fp_decompile_fail;

/* export for tests / end-of-run summary */
u32 rsx_d3d12_fp_cache_hits(void)    { return s_fp_cache_hits; }
u32 rsx_d3d12_fp_cache_misses(void)  { return s_fp_cache_misses; }
u32 rsx_d3d12_fp_decompile_ok(void)  { return s_fp_decompile_ok; }
```

Incrementar:
- hit no loop `shader_cache[i].hash == hash` → `s_fp_cache_hits++`
- miss antes de decompile → `s_fp_cache_misses++`
- após `rsx_fp_decompile` + `D3DCompile` ok → `s_fp_decompile_ok++`
- falhas → `s_fp_decompile_fail++`

No shutdown / a cada N frames (gate `PS3_TRACE_RSX_SHADERS=1`):

```c
fprintf(stderr,
  "[RSX-SH] hits=%u misses=%u ok=%u fail=%u cache=%u\n",
  s_fp_cache_hits, s_fp_cache_misses, s_fp_decompile_ok,
  s_fp_decompile_fail, s_d3d.shader_cache_count);
```

- [x] **Step 3: Critério de aceite Fase A**

Após boot até cena pós-logo (ou 30s estáveis):

| Métrica | Aceite |
|---------|--------|
| WAD opens (`R_PermA` ou equivalente) | ≥ 1 |
| `ICGLdrShader` register | ≥ 1 |
| `s_fp_decompile_ok` | ≥ 1 (shader de jogo, não só fallback hardcoded) |
| `PS3_DUMP_SHADERS` | ≥ 1 `fp_*.bin` com size plausível (> 16 B, PROGRAM_END presente) |
| Crash | 0 |

Se `fp_addr` nunca committed: diagnosticar se ICGLdrShader escreve em local RSX (`0xC0000000+`) vs main e se `resolve_fragment_program_ea` está alinhado (já documentado 2026-07-02).

- [x] **Step 4: Commit** — feito como `95c21f9` "feat(rsx): contadores de FP shader cache hit/miss para pipeline de conteúdo" (revisado: spec ✅, D3D12 headless sobe, `[RSX-SH]` cresce sem crash, gate confirmado).

```bash
git add libs/video/rsx_d3d12_backend.c libs/video/rsx_d3d12_backend.h
git commit -m "feat(rsx): FP shader cache hit/miss counters for content pipeline"
```

---

### Task 2: Fase B — Ligar `rsx_vp_decompile` ao D3D12

**Files:**
- Modify: `libs/video/rsx_d3d12_backend.c` (`d3d12_set_shader`, `create_pso_triple`, cache key, CBV de constants)
- Modify: `libs/video/rsx_d3d12_backend.h` se exportar helpers
- Modify: `libs/video/rsx_commands.h` se precisar expor dirty de VP de forma mais clara (já tem `transform_program_dirty`)
- Test: `libs/video/tests/validate_vp_shaders.c`, `tools/shadertest/harness.c`, corpus de `PS3_DUMP_SHADERS`

**Interfaces:**
- Consumes: `int rsx_vp_decompile(const u8* ucode, u32 max_bytes, char* out, u32 out_size)`; `state->transform_program` / `transform_program_words`; `state->vertex_constants[][]`
- Produces: PSO keyed by `(fp_hash, vp_hash)`; VS blob decompilado; CBV `vp_c[]` alimentado

- [x] **Step 1: Teste unitário offline com corpus dumpado** — harness `test_vp_ps_link.c`: VP→vs_5_0 S_OK, 12/12 saídas VS linkam PS inputs (PASS).

Se ainda não houver dumps de jogo, usar synthetic do harness. Com dumps:

```bash
cd tools/shadertest
# já documentado no README do harness
gcc -O0 -I ../../include harness.c \
  ../../libs/video/rsx_fp_decompiler.c \
  ../../libs/video/rsx_vp_decompiler.c -o harness.exe
./harness.exe path/to/vp_XXXXXXXX.bin
```

Expected: `rsx_vp_decompile` retorna `n_instr > 0` e HLSL contém `main(`.

- [x] **Step 2: Estender `ShaderPsoSet` com hash composto** — `fp_hash`+`vp_hash`+`vs_owned`; lookup composto; todos os `.hash` migrados.

Hoje:

```c
/* conceito atual */
typedef struct {
    u32 hash; /* só FP */
    ID3D12PipelineState *tri, *line, *point;
} ShaderPsoSet;
```

Novo:

```c
typedef struct {
    u32 fp_hash;
    u32 vp_hash;   /* 0 = passthrough VS */
    ID3D12PipelineState *tri, *line, *point;
    ID3DBlob* vs_blob; /* owned if vp_hash != 0; shared passthrough otherwise */
} ShaderPsoSet;
```

Lookup: match `fp_hash && vp_hash`.

- [x] **Step 3: Decompile VP a partir de `transform_program`** — `rsx_vp_decompile`→`D3DCompile vs_5_0`; fallback passthrough garantido. (Descoberta: `lib_rsx_vp_decompiler.o` nunca fora linkado; agora está.)

Em `d3d12_set_shader`, após decompilar FP:

```c
u32 vp_hash = 0;
ID3DBlob* vs_blob = s_d3d.vs_passthrough_blob;
ID3DBlob* vs_owned = NULL;

if (state->transform_program_words >= 4) {
    /* transform_program is u32 host-order words; decompiler wants byte stream LE */
    const u8* vp_ucode = (const u8*)state->transform_program;
    u32 vp_bytes = state->transform_program_words * 4u;
    /* cap: 512 instr * 16 B */
    if (vp_bytes > 512u * 16u) vp_bytes = 512u * 16u;

    static char vp_hlsl[96 * 1024];
    int vn = rsx_vp_decompile(vp_ucode, vp_bytes, vp_hlsl, sizeof(vp_hlsl));
    if (vn > 0) {
        vp_hash = fnv1a(vp_ucode, vp_bytes);
        ID3DBlob* err = NULL;
        HRESULT hr = D3DCompile(vp_hlsl, strlen(vp_hlsl), "vp_decompiled",
                                NULL, NULL, "main", "vs_5_0", 0, 0,
                                &vs_owned, &err);
        if (SUCCEEDED(hr) && vs_owned) {
            vs_blob = vs_owned;
        } else {
            static int n; if (n++ < 5)
                printf("[D3D12] VP D3DCompile failed, fallback passthrough: %s\n",
                       err ? (const char*)err->lpVtbl->GetBufferPointer(err) : "?");
            if (err) err->lpVtbl->Release(err);
            if (vs_owned) { vs_owned->lpVtbl->Release(vs_owned); vs_owned = NULL; }
            vp_hash = 0;
            vs_blob = s_d3d.vs_passthrough_blob;
        }
    }
}
/* create_pso_triple(vs_blob, ps_blob, slot); slot->fp_hash=...; slot->vp_hash=vp_hash; */
```

Incluir `#include "rsx_vp_decompiler.h"`.

- [x] **Step 4: Alinhar interface VS↔PS e input layout** — VSOutput do VP (SV_Position/COLOR0/COLOR1/FOG/TEXCOORD0..7) idêntico ao PSInput do FP; retry com passthrough se PSO falhar.

O passthrough atual emite:

```
VSOutput { SV_POSITION, COLOR0, COLOR1, FOG, TEXCOORD0..7 }
VSInput  { POSITION, COLOR }  // layout hardcoded em create_pso_triple
```

`rsx_vp_decompile` emite `VSOutput main(VSInput input)` com attrs `v[n]` e constants `vp_c[n]`.

Ações mínimas (MVP):
1. Garantir que o HLSL do VP declare o **mesmo** `VSOutput` que o FP `PSInput` espera (já documentado em `RSX_FRAGMENT_PROGRAM.md`).
2. Se o decompiler VP usa array `v[]`/`vp_c[]` com cbuffer, o root signature / CBV deve espelhar isso.
3. Expandir `upload_vertices_from_rsx` (ou equivalente) para copiar pelo menos POSITION + COLOR + TEXCOORD0 dos `vertex_attribs` enabled — senão o VP real recebe lixo em UVs.

Se o HLSL emitido pelo VP **não** linkar com o PS (assinatura diferente), preferir wrapper:

```c
/* Após rsx_vp_decompile, se o emitter ainda for "array style",
 * wrap no formato VSOutput que o FP espera — ou ajustar o emitter
 * em rsx_vp_decompiler.c uma vez. Preferir um único lugar (decompiler). */
```

Critério: `D3DCompile` VS+PS e `CreateGraphicsPipelineState` com o mesmo pair → S_OK.

- [~] **Step 5: Upload de vertex constants (CBV)** — DEFERIDO (opcional MVP). VS decompilado declara `cbuffer VPConst:b0` mas nenhuma constante é bindada; inofensivo até haver draw real com VP. Follow-up.

Quando `state->vertex_constants_dirty` (já no state):

```c
/* Map upload heap CBV; copy float4 slots
 * state->vertex_constants[i][0..3] for i in [lo, hi]
 * Bind as b0 matching vp_c / mvp layout. */
```

Fallback: se VP falhar, manter o path atual `mvp_col0..3` do passthrough.

- [x] **Step 6: Testes** — unit harness PASS; boot integração sem crash/regressão (fallback passthrough in-boot, vp_load=0).

```bash
# unit
cd libs/video/tests
# rebuild validate_vp_shaders + validate_fp_shaders (scripts existentes no tree)

# integration: boot com dump e contador
PS3_DUMP_SHADERS=../shadertest/gow2_dump PS3_TRACE_RSX_SHADERS=1 ./gow2_boot.exe
```

Aceite Fase B:
- `vp_hash != 0` em ≥ 1 PSO de jogo
- zero crash no set_shader
- fallback passthrough se decompile/compile falhar (nunca hard-fail o frame)

- [x] **Step 7: Commit** — `320cbc6` "feat(rsx): ligar rsx_vp_decompile ao cache de PSO do D3D12" (revisado: PASSA).

```bash
git add libs/video/rsx_d3d12_backend.c libs/video/rsx_vp_decompiler.c \
        libs/video/rsx_vp_decompiler.h libs/video/tests/
git commit -m "feat(rsx): wire rsx_vp_decompile into D3D12 PSO cache"
```

---

### Task 3: Fase C — Pipeline de texturas com dados reais

**Files:**
- Create: `libs/video/rsx_texture_deswizzle.h` (inline helpers, header-only ok)
- Modify: `libs/video/rsx_d3d12_backend.c` (`d3d12_bind_texture`)
- Modify: `libs/video/rsx_texture_formats.h` se faltar bpp/location helpers
- Test: `libs/video/tests/test_texture_deswizzle.c` (novo)

**Interfaces:**
- Consumes: `rsx_texture_state` (`offset`, `format`, `image_rect`, pitch/location se modelados); `vm_base`
- Produces: `ID3D12Resource` + SRV em `s_d3d.textures[unit]` para swizzled e linear

- [x] **Step 1: Teste unitário de deswizzle Morton (2D)** — `test_texture_deswizzle.c`: round-trip 6 tamanhos + bijeção + golden vector 4x4/8x4 + asserts do detector de formato. [PASS].

`libs/video/tests/test_texture_deswizzle.c`:

```c
#include "../rsx_texture_deswizzle.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

/* Tiny 2x2 A8R8G8B8: linear L[y][x], swizzled Morton order.
 * Build known pattern, swizzle, deswizzle, compare. */
static void morton_encode_ref(u32 x, u32 y, u32* out_x, u32* out_y); /* or linear index */

int main(void)
{
    const u32 W = 8, H = 8, BPP = 4;
    u8 linear[8*8*4];
    u8 swiz[8*8*4];
    u8 out[8*8*4];
    for (u32 i = 0; i < W*H*BPP; i++) linear[i] = (u8)(i & 0xFF);
    rsx_swizzle_texels(linear, swiz, W, H, BPP);   /* or only implement deswizzle */
    rsx_deswizzle_texels(swiz, out, W, H, BPP);
    assert(memcmp(linear, out, sizeof linear) == 0);
    printf("[PASS] deswizzle 8x8\n");
    return 0;
}
```

Implementação de referência (Morton / RSX linearization — alinhar com RPCS3 `rsx::convert_linear_swizzle` ou Mesa):

```c
/* rsx_texture_deswizzle.h — MVP power-of-two 2D */
static inline u32 rsx_morton_offset(u32 x, u32 y, u32 log2_w)
{
    /* classic bit interleave for power-of-two width; height handled by slice */
    u32 offset = 0;
    for (u32 i = 0; i < log2_w; i++) {
        offset |= ((x >> i) & 1u) << (2u * i);
        offset |= ((y >> i) & 1u) << (2u * i + 1u);
    }
    return offset;
}

static inline void rsx_deswizzle_texels(const u8* src, u8* dst,
                                        u32 w, u32 h, u32 bpp)
{
    /* For non-pow2, pad to next pow2 for addressing (RSX rule) — document */
    u32 lw = 0; while ((1u << lw) < w) lw++;
    for (u32 y = 0; y < h; y++)
        for (u32 x = 0; x < w; x++) {
            u32 si = rsx_morton_offset(x, y, lw) * bpp;
            u32 di = (y * w + x) * bpp;
            memcpy(dst + di, src + si, bpp);
        }
}
```

Validar contra 1 dump de textura real (se disponível) ou golden gerado offline.

- [x] **Step 2: Resolver EA de textura (local vs main)** — `resolve_texture_ea` (loc==1→0xC0000000+off, else main) + guard `ppu_guest_range_committed` substituindo o guard grosseiro.

Hoje:

```c
if (offset >= 0x20000000) return; /* too blunt */
const u8* src = vm_base + offset;
```

Substituir por helper espelhando fragment program:

```c
static u32 resolve_texture_ea(const rsx_texture_state* tex)
{
    /* RSX texture location bits live in format/offset words —
     * confirm field against rsx_commands SET_TEXTURE_OFFSET /
     * RPCS3 gcm_enums. MVP: bit in tex->format or tex->offset. */
    u32 off = tex->offset & ~0xFu;
    u32 loc = /* extract location: 0 local, 1 main */;
    return (loc == 0) ? (0xC0000000u + off) : off;
}
```

Antes do memcpy:

```c
u32 ea = resolve_texture_ea(tex);
u32 nbytes = row_bytes * src_rows;
if (!ppu_guest_range_committed(ea, nbytes)) {
    static int n; if (n++ < 5)
        printf("[D3D12] bind_texture: ea=0x%08X uncommitted, skip\n", ea);
    return;
}
const u8* src = vm_base + ea;
```

- [x] **Step 3: Integrar deswizzle em `d3d12_bind_texture`** — early-return removido; deswizzle→linear_tmp (só não-BC), free em todos os paths. +fix `cfb57eb`: detector de swizzle corrigido (bit LN, não dimension) + get_format mascara LN/UN.

Substituir o early-return swizzled:

```c
u8* linear_tmp = NULL;
const u8* upload_src = src;
if (rsx_texture_is_swizzled(tex->format)) {
    size_t n = (size_t)row_bytes * src_rows;
    linear_tmp = (u8*)malloc(n);
    if (!linear_tmp) return;
    rsx_deswizzle_texels(src, linear_tmp, width, height, bpp / 8);
    upload_src = linear_tmp;
}
/* ... map upload buffer from upload_src with existing BE swap ... */
free(linear_tmp);
```

- [~] **Step 4: Exercitar com dados reais** — NÃO observável in-boot (jogo não emite SET_TEXTURE — parede de shaders). Provado por unit test + boot sem crash/AV. Honesto, nada forjado.

Boot com:

```bash
# first 20 bind_texture logs already exist; add success path log:
# [D3D12] texture uploaded: unit=N ... swizzled=0/1
```

Aceite Fase C:
- ≥ 1 textura **swizzled** com log `texture uploaded` (não “skipping upload”)
- ≥ 1 textura linear uploaded
- FP com `TEX` opcode não falha só por SRV dummy (visual: não é aceite de “bonito”, é “SRV real bound”)
- sem AV em memcpy de textura

- [x] **Step 5: Commit** — `425d8d0` (deswizzle+EA) + `cfb57eb` (fix detector+golden). Revisado: PASSA.

```bash
git add libs/video/rsx_texture_deswizzle.h libs/video/rsx_d3d12_backend.c \
        libs/video/tests/test_texture_deswizzle.c
git commit -m "feat(rsx): deswizzle + EA resolve for real texture upload"
```

---

### Task 4: Fase D — Hardening LS/DMA e registro de SPU2/3

> **Verificado 2026-07-14 (árvore real):** premissa OK.
> - `gow2_work/recomp_mid_v2/gow2_spu_register.c` existe, está **trackeado** no git de `gow2_work` (`git ls-files` lista o arquivo).
> - `.gitignore` de `gow2_work` ignora `recomp_mid_v2/*.{cpp,h,o,bin}` e `spu_images/` (ELFs de jogo), mas **não** ignora `*.c` em `recomp_mid_v2/` nem `spu_lifted/`.
> - Liftados commitados: `spu_lifted/spu{0..3}/` + `spu_lifted/spu{0..3}_v2/` (16 arquivos `.c/.h`).
> - `spu_images/*.elf` é **local-only** (gitignore) — regenerável; fingerprints no register batem com FNV do ELF.
> - Registro default: **só spu0**. spu1 atrás de `PS3_SPU1|PS3_SPU_ALL`. spu2/3 **só** `PS3_SPU_ALL`.
> - Gap real de crash: `spu_workload.c` VEH (`spu_job_veh`) loga e retorna `EXCEPTION_CONTINUE_SEARCH` → processo morre. Isolação SEH no thunk é o pré-requisito para habilitar spu2/3 com segurança.

**Files:**
- Modify: `ps3recomp/runtime/spu/spu_dma.h` (já tem skip em GET/PUT; completar list path se crash)
- Modify: `ps3recomp/runtime/spu/spu_workload.c` (**primário** — SEH isolation em `spu_job_thunk` / `spu_job_veh`)
- Modify: `gow2_work/recomp_mid_v2/gow2_spu_register.c` (gates finos `PS3_SPU2`/`PS3_SPU3`; depois default)
- Optional: `ps3recomp/runtime/spu/spu_channels.c` se mailbox/stop incompleto
- Test: `ps3recomp/runtime/spu/tests/test_dma*`; smoke com `PS3_SPU_ALL=1`
- **Não commitar:** `spu_images/`, dumps, `recomp_mid_v2/*.cpp` gerados

**Interfaces:**
- Consumes fingerprints (já no register):
  - spu0 `0xDE6DC3A5EA2BE487` → `spu0_spu_func_00003070` (default)
  - spu1 `0x2A5C4E67A14505B8` → `spu1_spu_func_00003050` (`PS3_SPU1|ALL`)
  - spu2 `0xABCD0BA4D18DED49` → `spu2_spu_func_00004080` (`PS3_SPU_ALL` only hoje)
  - spu3 `0xED6A0C318DEB46C6` → `spu3_spu_func_00004080` (`PS3_SPU_ALL` only hoje)
- Consumes: `spu_workload_register` / `spu_workload_dispatch` (`ps3recomp/runtime/spu/`)
- Produces: spu2/spu3 no default **somente** após aceite; env `PS3_SPU2`, `PS3_SPU3` finos antes do `PS3_SPU_ALL`

- [~] **Step 1: Reproduzir crash sob gate** — spu2/3 NÃO são despachados na janela de boot (sobem no gameplay), então crash in-game não reproduzido. Mecanismo provado por teste determinístico + smokes standalone limpos. Honesto.

```bash
PS3_SPU_ALL=1 PS3_TRACE_SPUDMA=1 ./gow2_boot.exe 2>&1 | tee spu23_crash.log
```

Capturar:
- `[SPUCRASH] code=... rip=... fault=...` (VEH em `spu_workload.c`)
- últimas linhas `[SPUDMA]`
- se é “indirect branch to unknown LS” vs AV em EA

Mapear RIP com `nm` no objeto liftado (`spu2_spu_func_*`).

- [x] **Step 2: Suite mínima de DMA** — test_dma verde (bounds guard: size0/OOB/low-EA/uncommitted/list todos exigem -1). DMA já estava hardened (spu_dma.h não tocado).

Garantir que estes casos **não** abortam o processo:

| Caso | Esperado |
|------|----------|
| GET size 0 | -1, log skip |
| GET ea uncommitted | -1, log skip |
| GET ea < 0x10000 | -1 |
| GET size > 16K | -1 |
| PUT list com elemento OOB | skip elemento / -1, sem AV |
| GET válido 64 B | LS bate com guest |

Se list path ainda memcpy cego, espelhar o bounds-check de `mfc_do_transfer` em `mfc_do_list_transfer`.

- [x] **Step 3: Isolamento de falha por job** — thread dedicada + VEH+ExitThread gated por `__thread` (`__try/__except` não compila no mingw). Provado: AV real → `[SPUJOB] aborted by SEH` → host sobrevive → job limpo seguinte roda. VEH NÃO mascara main thread (checa flag na 1ª linha).

Hoje o VEH loga e continua search → processo morre. Para spu2/3:

```c
/* Option A (preferred for bring-up): catch AV in job thread, set
 * ctx status STOPPED, return from thunk without killing process. */
static LONG WINAPI spu_job_veh(EXCEPTION_POINTERS* ep) {
    if (code == EXCEPTION_ACCESS_VIOLATION) {
        fprintf(stderr, "[SPUCRASH] ... (job aborted, process continues)\n");
        /* longjmp to setjmp in spu_job_thunk — or ExitThread after mark */
        return EXCEPTION_EXECUTE_HANDLER; /* only if structured around __try */
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
```

No Windows, preferir `__try/__except` no thunk:

```c
unsigned long __stdcall spu_job_thunk(void* p) {
    struct spu_job_args* a = p;
    __try {
        spu_run_lifted_job_ex(a->fn, a->ls, a->ea, a->r4_hi, a->r4_lo);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        fprintf(stderr, "[SPUJOB] aborted by SEH code=0x%08lX\n",
                GetExceptionCode());
    }
    return 0;
}
```

Aceite: forçar spu2 crash **não** derruba o host; main loop segue.

- [x] **Step 4: Standalone smoke spu2/spu3 (sem SPURS completo)** — spu2/spu3 rodam LIMPOS isoladamente (RETURNED cleanly r=0). Gap in-game é ABI de task/DMA de work-queue, não o lift — rede SEH cobre.

Usar o path já validado nas notes (`gow2-recomp-notes.md`: spu2/3 “✓ clean” standalone):

```bash
# carregar ELF → spu_elf_load_to_ls → entry lifted → return
# sem args de task reais; indiretos desconhecidos são log, não AV
```

Se standalone limpa mas in-game crasha: o gap é **ABI de task / DMA de work queue**, não o lift. Tratar com:
- popular LS window de taskset (já parcialmente em `spu_workload_dispatch_task`)
- ou HLE no-op se job for “math kernel sem input” até o descriptor real existir

- [x] **Step 5: Registro gradual** — gates finos `PS3_SPU2`/`PS3_SPU3` (além de PS3_SPU_ALL). NÃO promovido a default (correto: promoção só após validação da Task 5). Bloqueadores de promoção rastreados no ledger.

Em `gow2_spu_register.c`:

```c
void gow2_register_spu_workloads(void)
{
    spu0_spu_recomp_register();
    spu1_spu_recomp_register();
    spu2_spu_recomp_register();
    spu3_spu_recomp_register();

    spu_workload_register(0xDE6DC3A5EA2BE487ull, spu0_spu_func_00003070, "gow2_spu0");

    /* spu1: MPEG / decoder path — stable when DMA hardened */
    if (getenv("PS3_SPU_ALL") || getenv("PS3_SPU1") || getenv("PS3_SPU_DEFAULT"))
        spu_workload_register(0x2A5C4E67A14505B8ull, spu1_spu_func_00003050, "gow2_spu1");

    /* spu2/3: math kernels — only after SEH isolation + DMA suite green */
    if (getenv("PS3_SPU_ALL") || getenv("PS3_SPU2"))
        spu_workload_register(0xABCD0BA4D18DED49ull, spu2_spu_func_00004080, "gow2_spu2");
    if (getenv("PS3_SPU_ALL") || getenv("PS3_SPU3"))
        spu_workload_register(0xED6A0C318DEB46C6ull, spu3_spu_func_00004080, "gow2_spu3");
}
```

Promoção a default (sem env):

```c
/* After accept criteria: register spu1 always; spu2/3 always with SEH.
 * Keep PS3_SPU_NONE to force all-off for bisection. */
if (!getenv("PS3_SPU_NONE")) {
    spu_workload_register(..., spu1_...);
    spu_workload_register(..., spu2_...);
    spu_workload_register(..., spu3_...);
}
```

Só aplicar o default no **último** commit da fase, depois de:
- 5 min de boot com `PS3_SPU_ALL=1` sem process kill
- zero aumento de hard hang no main thread

- [x] **Step 6: Commit** — DOIS repos: ps3recomp `3d76af9` (spu_workload.c + test_dma + test_seh_isolation) + gow2_work `e01fbee` (gow2_spu_register.c). Revisado: PASSA.

```bash
git add runtime/spu/spu_dma.h runtime/spu/spu_workload.c \
        gow2_work/recomp_mid_v2/gow2_spu_register.c runtime/spu/tests/
git commit -m "feat(spu): harden DMA/SEH and register gow2 spu2/spu3"
```

---

### Task 5: Integração ponta a ponta e checklist de release

**Files:**
- Modify: `docs/gow2-recomp-notes.md` (status table)
- Modify: `docs/RSX_FRAGMENT_PROGRAM.md` (marcar VP wired)
- Optional: `docs/RSX_GRAPHICS.md` capability matrix

- [x] **Step 1: Script de smoke único** — `gow2_work/smoke_rsx_spu.sh` (exe/env reais, greps de sanidade, anti-órfão). Rodado 1x: exit 124, RSX-SH cresce, SPUCRASH=0, parede de conteúdo esperada.

`gow2_work/smoke_rsx_spu.sh` (ou `.ps1`):

```bash
#!/usr/bin/env bash
set -e
export PS3_DUMP_SHADERS=../shadertest/gow2_dump
export PS3_TRACE_RSX_SHADERS=1
export PS3_TRACE_SPUDMA=1
export PS3_SPU_ALL=1
timeout 60s ./gow2_boot.exe 2>&1 | tee smoke_e2e.log
grep -q "ICGLdrShader" smoke_e2e.log
grep -q "decompile_ok\|shader_cache\|dumped shader\|RSX-SH" smoke_e2e.log || true
# must NOT be a pure native crash exit without log
test -d "$PS3_DUMP_SHADERS"
```

- [x] **Step 2: Matriz de aceite global** — 9 critérios documentados honestamente em gow2-recomp-notes.md (in-boot / offline / não-exercitado / parcial-pré-existente).

| # | Critério | Fase |
|---|----------|------|
| 1 | WAD carrega + ICGLdrShader registra | A |
| 2 | ≥1 FP de jogo no cache D3D12 | A |
| 3 | ≥1 VP decompilado em PSO (ou dump + compile ok) | B |
| 4 | Fallback passthrough se VP falhar | B |
| 5 | Textura linear upload ok | C |
| 6 | Textura swizzled upload ok | C |
| 7 | spu1 default ou env, sem crash host | D |
| 8 | spu2/3 registráveis; crash isolado | D |
| 9 | Loop 40fps não regride sem `PS3_SPU_ALL` | D |

- [x] **Step 3: Atualizar docs de status** — gow2-recomp-notes.md, RSX_FRAGMENT_PROGRAM.md (VP wired, FP done in-boot), RSX_GRAPHICS.md (texture unit-validated). Sem alegar renderização.

Em `docs/gow2-recomp-notes.md` e `docs/RSX_FRAGMENT_PROGRAM.md`, substituir “VP not translated” / “bind_texture stub” pelo estado real pós-implementação.

- [x] **Step 4: Commit docs** — ps3recomp `44d7f8a` (docs) + gow2_work `0b42e0e` (smoke script).

```bash
git add docs/
git commit -m "docs: GoW2 RSX content/VP/tex/SPU pipeline status"
```

---

## Riscos e mitigação

| Risco | Mitigação |
|-------|-----------|
| HLSL do VP não linka com PS (varyings) | Wrapper de interface único no decompiler; fallback passthrough |
| Deswizzle errado (cor “errada”) | Unit test + comparar 1 textura dumpada com RPCS3 se possível |
| Registrar spu2/3 regride 40fps | SEH isolation + env default off até smoke 5 min |
| EA textura em local RSX não mapeada | `resolve_texture_ea` + `ppu_guest_range_committed` |
| Conteúdo WAD não chega (FIOS) | Não é reimplementar loader — debug path `movie_hle` / mount; ICGLdrShader já dispara em init |

## Fora de escopo (não fazer neste plano)

- SPURS kernel HLE completo / spu0 graphics EDGE
- Vulkan backend
- Branching completo no FP/VP decompiler (IF/LOOP/CAL)
- `draw_indexed` / depth buffer full
- Perfect-hash PPU indiretos (outro track)

## Estimativa relativa

| Fase | Esforço | Paralelo? |
|------|---------|-----------|
| A Conteúdo/métricas | S (0.5–1 d) | sozinha primeiro |
| B VP→D3D12 | M (2–4 d) | após A para corpus |
| C Texturas | M (2–3 d) | paralelo a B após Step C1 |
| D SPU2/3 | M–L (2–5 d) | paralelo a B/C |
| E Integração | S (0.5 d) | no fim |

---

## Self-review

1. **Spec coverage:** (1) conteúdo WAD/ICGLdrShader → Task 1; (2) VP backend → Task 2; (3) texturas → Task 3; (4) SPU2/3 → Task 4; integração → Task 5.
2. **Placeholders:** nenhum TBD operacional; snippets apontam arquivos reais.
3. **Type consistency:** `rsx_vp_decompile` / `spu_workload_register` / fingerprints batem com o tree atual.
