# Vulkan V1a — `rsx_core_*` pure extraction from the Metal backend

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move the backend-agnostic pure logic of `libs/video/rsx_metal_backend.m` (decoders, address/geometry rules, pipeline-state keys and neutral enum maps) into portable C modules `libs/video/rsx_core_*.{h,c}`, with the Metal backend calling them and its behaviour bit-for-bit unchanged.

**Architecture:** Each extracted function keeps its exact body; state it used to read from Metal file statics (`s_pipe`, `s_draw`, env gates) is passed in as parameters, and the Metal file keeps thin wrappers with the old names so call sites barely move. Equivalence is proven **exhaustively** by unit tests that embed a verbatim "oracle" copy of the old Metal code and compare against the new core function over the full input domain (all 65 536 u16 values, all enum inputs, randomized structs), before any in-game shot check.

**Tech Stack:** C17 (`CMAKE_C_STANDARD 17`), Objective-C (ARC) for the Metal file, CMake/ctest.

**Spec:** `docs/superpowers/specs/2026-09-23-vulkan-backend-a-design.md` (§3.1 shared core, §6 Metal protection, §7 V1).

**Scope split (decided 2026-09-23):** spec §7 V1 lists six modules. This plan (V1a) takes the pure/medium pieces that can be proven equivalent offline. The stateful pieces — vertex-array fill + VA cache + ring slots (`metal_va_fill_body`, `metal_vtx_ea`, `metal_fetch_attrib_f4`), the draw record (`MetalDrawRec`, `capture_state/fc/vc`), the surface table/eviction and the texture cache — move in **V1b**, planned together with V2/V3 when the Vulkan replay consumes them (extraction driven by the consumer; less risk on the backend the user plays on).

## Global Constraints

- Metal must not regress: every moved function keeps its body byte-for-byte except the explicit parameterisation named in its task; default cases of every switch stay identical (blend factor → One, op → Add, compare → LessEqual, stencil → Keep, wrap → ClampToEdge).
- **Do NOT dedupe the half decoder onto `rsx_vertex_formats.h`**: that header's subnormal half path differs from Metal's (2^-25 vs 2^-24 for mant=1). Metal's decoder moves as-is.
- Core modules are portable C: no ObjC, no Metal types, no `__fp16` without a guard, no NEON without a guard, no Apple-only APIs (`clock_gettime_nsec_np`), no GNU-only libc (`memmem`) — every `libs/video/*.c` is globbed into the runtime on all platforms (`CMakeLists.txt:99-112`).
- Core code owns **no mutable global state**: tables/caches stay in the backend and are passed by pointer. Env gates stay read-once in the Metal file and are passed as `int` flags (names `PS3_METAL_*` unchanged).
- Commits in pt-BR, **no** `Co-Authored-By` (CLAUDE.md rule 3); `git add` explicit paths only; never stage `build-macos-wt/`, `build-tests/`.
- Work in worktree `.worktrees/vulkan-v0` (branch `vulkan-v0`); no `git stash` of any kind (shared stack).
- Tests: `cmake -B build-tests -G Ninja -DPS3RECOMP_BUILD_TESTS=ON .` then `ctest --test-dir build-tests -R "core|glsl|msl|ucode|shaderc" --output-on-failure`; the runtime must still build: `cmake --build build-macos-wt --target ps3recomp_runtime`. Line anchors below are from HEAD `918fd6f0`; re-verify each with `grep -n` before editing.

## Review Focus

1. **Half-float edge values** (subnormals, ±0, Inf, NaN payloads) through the moved decoder and the alpha-ref path: bit-exact vs the old code — pinned by exhaustive 65 536-value oracle tests in Task 1 and Task 2.
2. **Blend factor high half = 0** (real "alpha factor ZERO", GoW2 scenery ONE/ONE_MINUS_SRC_ALPHA): the RGB/alpha split must not fall back to the RGB factor — pinned in Task 3 `blend_split_zero_alpha`.
3. **Colour mask 0** (stencil-only volumes) must stay "no channels", while `0x01010101` / `0xFFFFFFFF` stay "all" — pinned in Task 3 `color_mask_table`.
4. **Surface clip garbage** (`w > 1920`, `w*h > 1920*1080`, zeros) must pick the viewport/default dims exactly as before — pinned in Task 2 `rt_dims_oracle` over randomized states.
5. **Scissor fully outside the target** with the fix gate on vs off (skip vs clamp-to-0) — pinned in Task 2 `scissor_oracle`.

---

### Task 1: `rsx_core_decode` — BE decoders, 16-bit expanders, hashes

**Files:**
- Create: `libs/video/rsx_core_decode.h`, `libs/video/rsx_core_decode.c`
- Create: `libs/video/tests/test_rsx_core_decode.c`
- Modify: `libs/video/rsx_metal_backend.m` (bodies of `metal_bswap32` ~3038, `metal_half_be_to_float` ~3189, `metal_read_f32_be` ~3214, `metal_snorm16_be` ~3224, `metal_attrib_sig` ~3339, `metal_va_fp_fast` ~3456, `metal_expand_*` ~1545-1578, `metal_tex_fingerprint` ~1684, `metal_texc_bucket` ~1709)
- Modify: `CMakeLists.txt` (tests section, next to `test_glsl_dialect`)

**Interfaces (produced):**
```c
u32   rsx_core_bswap32(u32 v);
float rsx_core_half_be_to_float(const u8* p);   /* IEEE half, big-endian bytes */
float rsx_core_read_f32_be(const u8* p);
float rsx_core_snorm16_be(const u8* p);
u32   rsx_core_attrib_sig(const rsx_vertex_attrib* a16);            /* FNV over enabled attribs */
u64   rsx_core_va_fp_fast(const u8* p, u32 n);
void  rsx_core_expand_r5g6b5_to_bgra(u16 t, u8* bgra);
void  rsx_core_expand_a1r5g5b5_to_bgra(u16 t, u8* bgra);
void  rsx_core_expand_a4r4g4b4_to_bgra(u16 t, u8* bgra);
u64   rsx_core_tex_fingerprint(const u8* p, u32 n, u32 win);        /* win: sampling window (Metal passes its PS3_METAL_TEX_FPWIN value) */
u32   rsx_core_texc_bucket(u32 ea, u32 fmt, u32 w, u32 h, u32 nbuckets_pow2);
```

- [ ] **Step 1: Write the failing test.** `test_rsx_core_decode.c` contains, verbatim and renamed `old_*`, the current Metal bodies of every function above (copy them from `rsx_metal_backend.m` at the anchors; for `old_tex_fingerprint` replace only the lazy `s_win` env block by a `u32 win` parameter, and for `old_texc_bucket` replace `METAL_TEXC_BUCKETS - 1u` by `255u`). Then:
```c
#include "rsx_core_decode.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int fails;
#define CHECK(name, cond) do { if (!(cond)) { printf("FAIL %s\n", name); fails++; } else printf("[PASS] %s\n", name); } while (0)
static int same_bits(float a, float b) { u32 x, y; memcpy(&x, &a, 4); memcpy(&y, &b, 4); return x == y; }
int main(void)
{
    int ok = 1;
    for (u32 v = 0; v < 65536u; v++) {            /* every half, every snorm16 */
        u8 p[2] = { (u8)(v >> 8), (u8)v };
        if (!same_bits(rsx_core_half_be_to_float(p), old_half_be_to_float(p))) { ok = 0; printf("half %04X\n", v); break; }
        if (!same_bits(rsx_core_snorm16_be(p), old_snorm16_be(p))) { ok = 0; printf("snorm %04X\n", v); break; }
        u8 a[4], b[4];
        rsx_core_expand_r5g6b5_to_bgra((u16)v, a);   old_expand_r5g6b5_to_bgra((u16)v, b);   if (memcmp(a, b, 4)) { ok = 0; break; }
        rsx_core_expand_a1r5g5b5_to_bgra((u16)v, a); old_expand_a1r5g5b5_to_bgra((u16)v, b); if (memcmp(a, b, 4)) { ok = 0; break; }
        rsx_core_expand_a4r4g4b4_to_bgra((u16)v, a); old_expand_a4r4g4b4_to_bgra((u16)v, b); if (memcmp(a, b, 4)) { ok = 0; break; }
    }
    CHECK("u16_domain_exhaustive", ok);
    ok = 1; srand(1);
    for (int i = 0; i < 1000000 && ok; i++) {
        u32 w = ((u32)rand() << 16) ^ (u32)rand();
        u8 p[4] = { (u8)(w >> 24), (u8)(w >> 16), (u8)(w >> 8), (u8)w };
        ok = rsx_core_bswap32(w) == old_bswap32(w) && same_bits(rsx_core_read_f32_be(p), old_read_f32_be(p))
          && rsx_core_texc_bucket(w, w >> 3, w >> 7, w >> 11, 256u) == old_texc_bucket(w, w >> 3, w >> 7, w >> 11);
    }
    CHECK("u32_random_1M", ok);
    {   /* hashes over buffers of every small length and some large ones */
        static u8 buf[1 << 20];
        for (u32 i = 0; i < sizeof buf; i++) buf[i] = (u8)(i * 2654435761u >> 24);
        ok = 1;
        for (u32 n = 0; n < 2100u && ok; n++)
            ok = rsx_core_va_fp_fast(buf, n) == old_va_fp_fast(buf, n)
              && rsx_core_tex_fingerprint(buf, n, 4096u) == old_tex_fingerprint(buf, n, 4096u);
        const u32 big[] = { 4096u, 65536u, 262144u, 1u << 20 };
        const u32 wins[] = { 256u, 4096u, 262144u };
        for (int i = 0; i < 4 && ok; i++) for (int j = 0; j < 3 && ok; j++)
            ok = rsx_core_tex_fingerprint(buf, big[i], wins[j]) == old_tex_fingerprint(buf, big[i], wins[j]);
        CHECK("hashes_oracle", ok);
    }
    {   /* attrib signature over randomized attribute tables */
        ok = 1;
        for (int t = 0; t < 100000 && ok; t++) {
            rsx_vertex_attrib a[RSX_MAX_VERTEX_ATTRIBS]; memset(a, 0, sizeof a);
            for (int k = 0; k < RSX_MAX_VERTEX_ATTRIBS; k++) {
                a[k].enabled = rand() & 1; a[k].type = (u32)(rand() & 7); a[k].size = (u32)(rand() & 7);
                a[k].stride = (u32)(rand() & 255); a[k].offset = ((u32)rand() << 8) ^ (u32)rand();
            }
            ok = rsx_core_attrib_sig(a) == old_attrib_sig(a);
        }
        CHECK("attrib_sig_oracle", ok);
    }
    printf(fails ? "%d FAILED\n" : "ALL PASS\n", fails);
    return fails ? 1 : 0;
}
```
(Check the field types of `rsx_vertex_attrib` in `rsx_commands.h` and adapt the random assignments to them without changing the idea.) CMake (tests section, outside the shaderc `if`):
```cmake
    add_executable(test_rsx_core_decode
        libs/video/tests/test_rsx_core_decode.c
        libs/video/rsx_core_decode.c)
    target_include_directories(test_rsx_core_decode PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/libs/video" "${CMAKE_CURRENT_SOURCE_DIR}/include")
    add_test(NAME test_rsx_core_decode COMMAND test_rsx_core_decode)
```
- [ ] **Step 2: Run to confirm it fails** — `cmake -B build-tests -G Ninja -DPS3RECOMP_BUILD_TESTS=ON . && cmake --build build-tests --target test_rsx_core_decode` → fails: `rsx_core_decode.h` not found.
- [ ] **Step 3: Implement.** Header with the declarations above (include guard `PS3RECOMP_RSX_CORE_DECODE_H`, `extern "C"` block, `#include "ps3emu/ps3types.h"` and `"rsx_commands.h"` for `rsx_vertex_attrib`). The `.c` holds the **verbatim** Metal bodies under the new names (`static` removed; `metal_bswap32` calls become `rsx_core_bswap32`; `tex_fingerprint` uses the `win` parameter instead of the lazy env read; `texc_bucket` masks with `nbuckets_pow2 - 1u`). In `rsx_metal_backend.m`: `#include "rsx_core_decode.h"` next to the other `rsx_*.h` includes (~line 34-54), and replace each Metal body by a one-line forwarder keeping the old name and signature, e.g.:
```objc
static float metal_half_be_to_float(const u8* p) { return rsx_core_half_be_to_float(p); }
static u64 metal_tex_fingerprint(const u8* p, u32 n)
{
    static u32 s_win = 0;   /* PS3_METAL_TEX_FPWIN, read once as before */
    if (!s_win) {
        const char* e = getenv("PS3_METAL_TEX_FPWIN");
        long v = (e && *e) ? strtol(e, NULL, 0) : 4096;
        if (v < 256 || v > 262144) v = 4096;
        s_win = (u32)v;
    }
    return rsx_core_tex_fingerprint(p, n, s_win);
}
static u32 metal_texc_bucket(u32 ea, u32 fmt, u32 w, u32 h) { return rsx_core_texc_bucket(ea, fmt, w, h, METAL_TEXC_BUCKETS); }
```
Keep the lazy `s_win` read exactly where it was relative to the `n <= 1040` fast path semantics: the old code read the env only when `n > 1040`; reading it earlier is harmless (same value) — note it in the commit body. `metal_va_fp_fast` stays `static inline` as a forwarder.
- [ ] **Step 4: Run tests + runtime build** — `ctest --test-dir build-tests -R "core|msl" --output-on-failure` (ALL PASS) and `cmake --build build-macos-wt --target ps3recomp_runtime` (no new warnings).
- [ ] **Step 5: Commit** — `git add libs/video/rsx_core_decode.h libs/video/rsx_core_decode.c libs/video/tests/test_rsx_core_decode.c libs/video/rsx_metal_backend.m CMakeLists.txt && git commit -m "refactor(rsx): decodificadores e hashes do Metal vao para rsx_core_decode (prova exaustiva contra o codigo antigo)"`

---

### Task 2: `rsx_core_rules` — addresses, viewport, scissor, alpha ref, RT dims, MRT targets

**Files:**
- Create: `libs/video/rsx_core_rules.h`, `libs/video/rsx_core_rules.c`, `libs/video/tests/test_rsx_core_rules.c`
- Modify: `libs/video/rsx_metal_backend.m` (`metal_resolve_fp_ea` ~1388, `metal_resolve_tex_ea` ~1530, viewport block ~7488-7508, per-draw scissor ~7509-7518, alpha ref ~4222-4237, `metal_clamp_dim` ~635 + dims ~9620-9632, rtt/cloc ~9662-9681 in `metal_set_render_target`)
- Modify: `CMakeLists.txt`

**Interfaces (produced):**
```c
/* gcm_io_offset_to_ea is passed in so core does not link the GCM HLE. */
typedef int (*rsx_core_io2ea_fn)(u32 io_offset, u32* ea);
u32  rsx_core_resolve_fp_ea(const rsx_state* s, rsx_core_io2ea_fn io2ea, int* unresolved);   /* *unresolved=1 when it fell to the flat offset (caller logs) */
u32  rsx_core_resolve_tex_ea(const rsx_texture_state* t, rsx_core_io2ea_fn io2ea);
/* Viewport: rect (has_vp, vp[4]) or full target, then VIEWPORT_SCALE/OFFSET when so_on && seen==3 && sx>0 && sy<0. out = x,y,w,h */
void rsx_core_viewport(int has_vp, const float vp[4], u32 tw, u32 th, const float vso[8], u32 vso_seen, int so_on, double out[4]);
/* Per-draw scissor: returns 0 draw with out[4]; 1 skip AND count a scissor reject; 2 skip without counting. */
int  rsx_core_scissor_per_draw(const u32 sc[4], u32 tw, u32 th, int scissor_fix, u32 out[4]);
float rsx_core_alpha_ref(u32 surface_format, u32 alpha_ref);
u32  rsx_core_clamp_dim(u32 v, u32 def);
void rsx_core_rt_dims(const rsx_state* s, u32 def_w, u32 def_h, u32* w, u32* h);
/* rtt[4] target order (A=0..D=3), *nrtt, cloc[4] 0 local / 1 main, from SET_SURFACE_COLOR_TARGET + DMA. */
void rsx_core_mrt_targets(const rsx_state* s, int mrt_on, int rtt[4], int* nrtt, u32 cloc[4]);
```

- [ ] **Step 1: Write the failing test.** Oracle copies (verbatim, renamed `old_*`, globals replaced by the same parameters as the new API): `old_resolve_fp_ea` / `old_resolve_tex_ea` (with a stub `io2ea` that maps `off → off ^ 0x40000000` and fails for `off & 1`), the viewport block, the per-draw scissor block (returning the same 0/1/2 codes), the alpha-ref block **using `__fp16` inside `#if defined(__aarch64__)`**, `old_clamp_dim`, the dims block, the rtt/cloc block. Tests:
```c
/* fill_state_random: randomize every rsx_state field the functions read:
 * shader_program, fragment_program_addr, surface_clip_w/h, viewport_w/h,
 * surface_format, alpha_ref, color_target (bias 1/4 of draws to 0x02/0x13/0x17/0x1F),
 * surface_color_dma[4] (bias to RSX_CONTEXT_DMA_MAIN), surface_color_offset[4]. */
```
  - `fp_tex_ea_oracle`: 1 M random states/textures, results and the unresolved flag equal.
  - `viewport_oracle`: 1 M random (has_vp, vp, tw, th, vso incl. sx≤0/sy≥0/NaN, seen 0..3, so_on 0/1): `out` equal bit-for-bit.
  - `scissor_oracle`: 1 M random sc (incl. 0s and values ≥ tw/th), fix 0/1: return code and `out` equal.
  - `alpha_ref_exhaustive` (aarch64 only, `#if defined(__aarch64__)`): for formats {0x0B,0x0C,0x0D,0x08,0x03} × all 65 536 low halves × 8 random high halves: bit-equal floats.
  - `rt_dims_oracle`: 1 M random states with dims drawn from {0, 1..1920, 1921..4096, garbage}: equal.
  - `mrt_targets_oracle`: every `color_target` 0..0xFF × mrt_on 0/1 × random DMA: rtt, nrtt, cloc equal.
  CMake: same pattern as Task 1 (`test_rsx_core_rules`, sources: test + `rsx_core_rules.c`).
- [ ] **Step 2: Run to confirm it fails** (header missing).
- [ ] **Step 3: Implement.** Bodies verbatim, parameterised as the interface says. `rsx_core_alpha_ref`: keep `__fp16` under `#if defined(__aarch64__) || defined(__ARM_FP16_FORMAT_IEEE)` (so Metal on arm64 is bit-identical), and in the `#else` branch convert the host-order u16 with the same algorithm as `rsx_core_half_be_to_float` (build a 2-byte BE buffer and call it). In the Metal file replace each site by a call: e.g. the viewport block becomes
```objc
        double vpd[4];
        {   static int so_off = -1;
            if (so_off < 0) { const char* e = getenv("PS3_METAL_VIEWPORT_SO"); so_off = (e && e[0] == '0') ? 1 : 0; }
            rsx_core_viewport(dr->has_vp, dr->vp, tw, th, dr->vso, dr->vso_seen, !so_off, vpd); }
        MTLViewport vp = (MTLViewport){ vpd[0], vpd[1], vpd[2], vpd[3], 0.0, 1.0 };
```
  and the scissor block becomes
```objc
        u32 scr[4];
        const int sres = rsx_core_scissor_per_draw(dr->sc, tw, th, metal_scissor_fix(), scr);
        if (sres == 1) { s_scissor_reject++; n_skip++; continue; }
        if (sres == 2) { n_skip++; continue; }
        [enc setScissorRect:(MTLScissorRect){ scr[0], scr[1], scr[2], scr[3] }];
```
  Keep the explanatory comments (move the long ones into the core function). `metal_resolve_fp_ea` keeps its `warn++ < 5` printf using the `unresolved` flag. Keep `metal_clamp_dim` as a forwarder (other callers exist — grep).
- [ ] **Step 4: Run tests + runtime build** as in Task 1.
- [ ] **Step 5: Commit** — `git add libs/video/rsx_core_rules.h libs/video/rsx_core_rules.c libs/video/tests/test_rsx_core_rules.c libs/video/rsx_metal_backend.m CMakeLists.txt && git commit -m "refactor(rsx): regras de viewport/scissor/alpha ref/alvos do Metal vao para rsx_core_rules (oraculo)"`

---

### Task 3: `rsx_core_pipe` — pipeline state, keys, neutral enum maps

**Files:**
- Create: `libs/video/rsx_core_pipe.h`, `libs/video/rsx_core_pipe.c`, `libs/video/tests/test_rsx_core_pipe.c`, `libs/video/tests/test_rsx_core_pipe_metal.m` (Apple only)
- Modify: `libs/video/rsx_metal_backend.m` (`MetalPipeState` ~232-255, `MetalDsDesc` ~260-265, `metal_pipe_blend_key` ~346, `metal_pipe_ds_desc` ~361, `metal_pipe_ds_key` ~391, `metal_pso_state_key` ~1346, maps ~402-483, blend split in `metal_apply_blend_to_attachment` ~521-530, `metal_wrap_from_gcm` ~1983, sampler key/filter ~2016-2042)
- Modify: `CMakeLists.txt`

**Interfaces (produced):**
```c
typedef struct { /* exactly the MetalPipeState fields, same order and types */ } rsx_core_pipe;
typedef struct { /* exactly the MetalDsDesc fields, same order and types */ } rsx_core_ds_desc;
u32  rsx_core_blend_key(const rsx_core_pipe* p, int disable_blend);
void rsx_core_pipe_ds_desc(const rsx_core_pipe* p, int force_write, u32 dmode, rsx_core_ds_desc* o);
u32  rsx_core_ds_key(const rsx_core_ds_desc* d);
u32  rsx_core_pso_state_key(u32 blend_key, const rsx_core_pipe* p, int mrt_n, int atest_on);

typedef enum { RSX_CMP_NEVER, RSX_CMP_LESS, RSX_CMP_EQUAL, RSX_CMP_LEQUAL, RSX_CMP_GREATER,
               RSX_CMP_NOTEQUAL, RSX_CMP_GEQUAL, RSX_CMP_ALWAYS } rsx_core_cmp;
typedef enum { RSX_BF_ZERO, RSX_BF_ONE, RSX_BF_SRC_COLOR, RSX_BF_ONE_MINUS_SRC_COLOR, RSX_BF_SRC_ALPHA,
               RSX_BF_ONE_MINUS_SRC_ALPHA, RSX_BF_DST_ALPHA, RSX_BF_ONE_MINUS_DST_ALPHA, RSX_BF_DST_COLOR,
               RSX_BF_ONE_MINUS_DST_COLOR, RSX_BF_SRC_ALPHA_SAT, RSX_BF_CONST_COLOR,
               RSX_BF_ONE_MINUS_CONST_COLOR } rsx_core_blend_factor;
typedef enum { RSX_BOP_ADD, RSX_BOP_MIN, RSX_BOP_MAX, RSX_BOP_SUB, RSX_BOP_REV_SUB } rsx_core_blend_op;
typedef enum { RSX_SOP_ZERO, RSX_SOP_KEEP, RSX_SOP_REPLACE, RSX_SOP_INCR_CLAMP, RSX_SOP_DECR_CLAMP,
               RSX_SOP_INVERT, RSX_SOP_INCR_WRAP, RSX_SOP_DECR_WRAP } rsx_core_stencil_op;
typedef enum { RSX_WRAP_REPEAT, RSX_WRAP_MIRROR_REPEAT, RSX_WRAP_CLAMP_EDGE, RSX_WRAP_CLAMP_BORDER,
               RSX_WRAP_MIRROR_CLAMP_EDGE } rsx_core_wrap;
enum { RSX_CM_R = 1, RSX_CM_G = 2, RSX_CM_B = 4, RSX_CM_A = 8, RSX_CM_ALL = 15 };
rsx_core_cmp          rsx_core_map_cmp(u32 f);                 /* default LEQUAL */
rsx_core_blend_factor rsx_core_map_blend_factor(u32 f, int alpha); /* default ONE */
rsx_core_blend_op     rsx_core_map_blend_op(u32 e);            /* default ADD */
rsx_core_stencil_op   rsx_core_map_stencil_op(u32 op);         /* default KEEP */
u32                   rsx_core_map_color_mask(u32 m);          /* RSX_CM_* bits; same rules as metal_map_color_mask */
rsx_core_wrap         rsx_core_wrap_from_gcm(u32 w);           /* default CLAMP_EDGE */
typedef struct { u32 sf_rgb, sf_a, df_rgb, df_a, eq_rgb, eq_a; } rsx_core_blend_split;
void rsx_core_split_blend(u32 sf, u32 df, u32 eq, rsx_core_blend_split* o);  /* RGB bits 0-15, alpha 16-31; 0 alpha = real ZERO */
u32  rsx_core_sampler_key(u32 addr, u32 filt);
int  rsx_core_min_filter_linear(u32 filt);                     /* GCM min 2,4,6,7 */
int  rsx_core_mag_filter_linear(u32 filt);                     /* GCM mag 2,4 */
```

- [ ] **Step 1: Write the failing tests.**
  - `test_rsx_core_pipe.c` (portable): oracle copies of `metal_pipe_blend_key`, `metal_pipe_ds_desc`, `metal_pipe_ds_key`, `metal_pso_state_key` taking the old `MetalPipeState` by pointer and the env/debug values as parameters; 1 M randomized pipes (every field random, booleans biased 50/50) × disable_blend 0/1 × force_write 0/1 × dmode 0..3 × mrt_n 1..4 × atest 0/1: keys and `memcmp` of the desc equal. `blend_split_zero_alpha`: `rsx_core_split_blend(0x00000001, 0x00000303, 0x00008006, &o)` gives `sf_a == 0`, `df_a == 0` (not the RGB factors). `color_mask_table`: `m=0 → 0`, `0x01010101 → RSX_CM_ALL`, `0xFFFFFFFF → RSX_CM_ALL`, `0x00000001 → RSX_CM_B`, `0x01000000 → RSX_CM_A`. `sampler_key_oracle`: all `filt` high bytes × 4096 random `addr`: key and linear flags equal to the old inline expressions.
  - `test_rsx_core_pipe_metal.m` (APPLE only, compiled as ObjC, links Metal): oracle copies of `metal_map_cmp`, `metal_map_blend_factor`, `metal_map_blend_op`, `metal_map_stencil_op`, `metal_map_color_mask`, `metal_wrap_from_gcm` (verbatim, MTL return types) and the **new** thin Metal switches (the exact code Step 3 puts in the backend, copied here); for every input in 0..0xFFFF (and `alpha` 0/1, and for color mask 1 M random u32 plus the special values) `old(x) == metal_from_core(core(x))`. This is the proof that Metal pipelines are unchanged.
  CMake:
```cmake
    add_executable(test_rsx_core_pipe libs/video/tests/test_rsx_core_pipe.c libs/video/rsx_core_pipe.c)
    target_include_directories(test_rsx_core_pipe PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/libs/video" "${CMAKE_CURRENT_SOURCE_DIR}/include")
    add_test(NAME test_rsx_core_pipe COMMAND test_rsx_core_pipe)
    if(APPLE)
        add_executable(test_rsx_core_pipe_metal libs/video/tests/test_rsx_core_pipe_metal.m libs/video/rsx_core_pipe.c)
        set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/libs/video/tests/test_rsx_core_pipe_metal.m" PROPERTIES LANGUAGE OBJC)
        target_include_directories(test_rsx_core_pipe_metal PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/libs/video" "${CMAKE_CURRENT_SOURCE_DIR}/include")
        target_link_libraries(test_rsx_core_pipe_metal PRIVATE "-framework Metal" "-framework Foundation")
        add_test(NAME test_rsx_core_pipe_metal COMMAND test_rsx_core_pipe_metal)
    endif()
```
- [ ] **Step 2: Run to confirm they fail** (header missing).
- [ ] **Step 3: Implement.** `rsx_core_pipe.h/.c`: the structs are moved verbatim (field order/types identical — `memcmp`/FNV over `rsx_core_ds_desc` must hash the same bytes as before), key functions verbatim with `s_pipe.` → `p->` and the debug/env values as parameters; map functions return the neutral enums with the same case tables and defaults; `split_blend` is the three pairs of masks from `metal_apply_blend_to_attachment`. In the Metal file: `typedef rsx_core_pipe MetalPipeState;` and `typedef rsx_core_ds_desc MetalDsDesc;` replace the struct definitions; `metal_pipe_blend_key()` → `return rsx_core_blend_key(&s_pipe, metal_debug_disable_blend());`, `metal_pipe_ds_desc(o)` → `rsx_core_pipe_ds_desc(&s_pipe, metal_debug_force_depth_write(), (u32)rsx_depth_debug_mode(), o);`, `metal_pipe_ds_key()` → desc + `rsx_core_ds_key`, `metal_pso_state_key()` → `rsx_core_pso_state_key(metal_pipe_blend_key(), &s_pipe, s_mrt_n, metal_atest_on())`. Each `metal_map_*` becomes `switch (rsx_core_map_*(x)) { case RSX_...: return MTL...; }` with a `default:` equal to the old default; `metal_apply_blend_to_attachment` uses `rsx_core_split_blend`; `metal_wrap_from_gcm` switches on `rsx_core_wrap_from_gcm`; `metal_sampler_for` uses `rsx_core_sampler_key` and the two linear predicates. The sampler cache, `s_samp_cache` and all MTL objects stay in the Metal file.
- [ ] **Step 4: Run tests + runtime build** as in Task 1 (both pipe tests must pass on macOS).
- [ ] **Step 5: Commit** — `git add libs/video/rsx_core_pipe.h libs/video/rsx_core_pipe.c libs/video/tests/test_rsx_core_pipe.c libs/video/tests/test_rsx_core_pipe_metal.m libs/video/rsx_metal_backend.m CMakeLists.txt && git commit -m "refactor(rsx): estado de pipeline, chaves e mapas neutros saem do Metal para rsx_core_pipe (prova de equivalencia)"`

---

### Task 4: in-game Metal A/B (acceptance of V1a)

**Files:** none committed except the execution record in this plan.

- [ ] **Step 1: Build two binaries** from the same gow2 tree and lift (`recomp_macos_e435`): BEFORE = engine at this plan's base commit (a temporary `git worktree add --detach <scratch>/eng_before <base>`, `cmake -B build-macos -DCMAKE_BUILD_TYPE=Release` + `--target ps3recomp_runtime`), AFTER = worktree HEAD (`build-macos-wt`). Link each with `cd ../gow2-recomp && PY=<main>/.venv/bin/python PS3_ENGINE_ROOT=<engine> RUNTIME_LIB=<engine>/<builddir>/libps3recomp_runtime.a OUT=$PWD/boot_gow2_v1a_<before|after> ./build_macos.sh recomp_macos_e435`.
- [ ] **Step 2: Ask the user before opening windows** (memory: never open test windows while they play; `PS3_RSX_BACKEND=null` still opens a Metal window). With their OK, run `claude_runs/run_until_shots.sh boot_gow2_v1a_before <sha> v1a_before 600 6` then the same for `after`.
- [ ] **Step 3: Compare** the 6 gameplay shots + S8 pairwise: first measure BEFORE-vs-BEFORE variance (run BEFORE twice) and use it as the declared tolerance; AFTER must be within it (pixel-identical on deterministic shots). Record counts per shot.
- [ ] **Step 4: Record** an `## Execution record (V1a acceptance)` section at the end of this plan (numbers, binaries' sha256, tolerance) and commit it (`git add` this plan only). Delete the two binaries and the temporary worktree.

## Execution record (V1a acceptance)

- 2026-09-23. Tasks 1-3 merged on `vulkan-v0`: f3d555f2 + 8b7e185f (rsx_core_decode; hot helpers `static inline` in the header because the build has no LTO), 5f4998d4 (rsx_core_rules), 63b88b98 (rsx_core_pipe; MetalPipeState/MetalDsDesc are typedefs of the core structs).
- Equivalence proof (offline, the binding evidence): oracle tests against verbatim copies of the pre-change Metal code — every u16 (half, snorm16, 3 expanders), 1M random u32, hash lengths 0..2099 + 4K..1M × 3 windows, 100k attrib tables, 1M random states for addresses/viewport/scissor/dims, all colour targets × MRT on/off, alpha ref 5 formats × 65 536 halves × 8 on arm64, 1M random pipes for the keys/DS desc, and every 0..0xFFFF input of the enum maps through the exact Metal switches (ObjC test). All pass.
- In-game Metal A/B (`run_until_shots.sh`, 6 gameplay shots + S1-S9 stages, lift e435): BEFORE = engine ec886ac5 (sha256 b3814190e11d1d93…), AFTER = 63b88b98 (sha256 fa5ba06430019886…). The shots are taken at different game frames in every run (e.g. gp0 at f9842 / f8169 / f7684), so pixel comparison is dominated by content: BEFORE-vs-BEFORE median 25.7 % px > 8/255 (max 93.6 %), BEFORE1-vs-AFTER median 24.6 % (max 93.6 %), BEFORE2-vs-AFTER median 40.2 % — AFTER inside the same-binary variance. **This A/B is not discriminating for subtle changes**; it confirms there is no gross breakage (visual check: gameplay with HUD, characters, lighting and bloom correct in AFTER). Deterministic shot capture is a follow-up (the V2 plan needs it for Vulkan-vs-Metal anyway).
- Final whole-branch review (2026-09-23): **ready to merge**. Every Metal call site hand-checked against the base; hot helpers still inlined (`nm` on the Release object); core `.c` files compile warning-free with MinGW (`-std=c11 -Wall -Wextra`). Comment fixes in ad350c10.

### Carried to the V1b / V2 plan
1. **Precondition:** deterministic shot capture (fixed frame index / dump-at-frame-N) before any stateful extraction (V1b) and for Vulkan-vs-Metal comparison — the V1a A/B could not discriminate.
2. Move the thin Metal enum switches into a small header shared by the backend and `test_rsx_core_pipe_metal.m`, so the test checks the shipped code rather than a hand copy.
3. Faithful enums for Vulkan: `RSX_BF_CONST_ALPHA` / `RSX_BF_ONE_MINUS_CONST_ALPHA` (GCM 0x8003/0x8004) and distinct GL_CLAMP / MIRROR_ONCE wraps; Metal maps them back to today's choices (ObjC test proves Metal unchanged).
4. `rsx_core_pipe` carries Metal PSO-cache fields (`blend_key`, `ds_key`); split neutral state from backend bookkeeping.
5. Add `rsx_core_pipe_from_state()` / `rsx_core_pipe_init_defaults()` (today Metal-only at the s_pipe fill and `metal_pipe_init_defaults`).
6. The legacy (non-per-draw) scissor rule differs from the per-draw one; the core models only per-draw — fine if Vulkan replays per draw.
7. Expose the portable half conversion used by the non-arm64 alpha-ref branch as a named inline so it is testable on arm64; replace `(rand()<<16)^rand()` with a shared xorshift in the core tests (MSVC/MinGW RAND_MAX 0x7FFF).
