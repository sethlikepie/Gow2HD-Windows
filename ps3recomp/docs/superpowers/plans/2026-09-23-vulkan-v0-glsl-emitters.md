# Vulkan V0 — GLSL emitters + shaderc (offline) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Every GoW2 fragment/vertex program our runtime meets decompiles to GLSL 450 and compiles to SPIR-V with shaderc, proven offline on a real corpus — milestone V0 of the Vulkan backend.

**Architecture:** The NV40 decompilers keep emitting their HLSL dialect (the single source of truth that already feeds D3D12 and, via string rewrite, Metal). A new `rsx_glsl_dialect` module turns that dialect into GLSL with a *compat prelude* (`#define float4 vec4`, overloads for `saturate`/`frac`/`lerp`/`rsqrt`/`asfloat`/…) plus three textual rewrites (casts, vector compares, texture calls). `rsx_fp_decompile_glsl` / `rsx_vp_decompile_glsl` wrap the bodies with the Vulkan interface (fixed locations, set 0 bindings, spec constant). A gated probe dumps raw ucode from real runs; a corpus tool compiles every program with shaderc.

**Tech Stack:** C11, CMake/ctest, shaderc (Homebrew `shaderc`, C API `shaderc/shaderc.h`), GLSL 450 targeting Vulkan 1.2.

**Spec:** `docs/superpowers/specs/2026-09-23-vulkan-backend-a-design.md` (§3.2 shader emitters, §5 shader failure, §7 V0).

This is the first of six plans (V0..V5). V1 (`rsx_core_*` extraction) gets its own plan after V0 lands, with line anchors re-verified then.

## Global Constraints

- Commit messages in **pt-BR**, **no** `Co-Authored-By` line (CLAUDE.md rule 3 overrides the harness default).
- `git add` explicit paths only — never `git add -A` / `git add .` (CLAUDE.md rule 1). The file `tools/rpcs3_probe/oracle_bp.py` is someone else's working tree: never stage it.
- **No game data in git:** raw ucode dumps are game code → they live in `../gow2-recomp/shader_corpus/` (gitignored), never in this repo.
- Probes gated by env var, **OFF by default** (CLAUDE.md rule 6): `PS3_RSX_DUMP_UCODE` must be a no-op when unset.
- Metal must not regress: `test_msl_decompiler` must keep passing after every task; any refactor inside `rsx_fp_decompile_msl`/`rsx_vp_decompile_msl` is behaviour-identical (byte-identical MSL output).
- New dependency from **system packages**: `brew install shaderc`; CMake finds it; without shaderc the GLSL tests are skipped with a STATUS message, the runtime still builds.
- Code identifiers/comments in English; user-facing text pt-BR.
- Vulkan interface contract (all tasks and later plans rely on it, copy verbatim):
  - Varyings: `location 0 col0, 1 col1, 2 fog, 3..10 tc0..tc7`; position via `gl_Position` / `gl_FragCoord`.
  - VS inputs: `location 0 vec3 in_v0`, `location 1 vec4 in_v3` (mirror of Metal `VSIn` attribute 0/1).
  - Set 0: `binding 0` `sampler2D rsx_tex[16]` (FS) · `binding 1` std430 `rsx_fc[]` (FS, const-buffer mode) · `binding 2` std140 `uvec4 rsx_at` (FS) · `binding 3` std430 `vp_c[]` (VS) · `binding 4` std430 VAMap `{uint stride, base, vid_base, pad; int slot[16]; vec4 cur[16]}` (VS; byte layout == Metal `MetalVAMap`) · `binding 5` std430 `va[]` float4 ring (VS).
  - Alpha test: `layout(constant_id = 0) const bool rsx_at_on = false;`
  - FS colour outputs: `layout(location = c) out vec4 rsx_c<c>` for `c < rsx_fp_set_mrt_count()` value; depth export writes `gl_FragDepth = r[1].z`.
  - `invariant gl_Position;` in every VS.

## Review Focus

1. **A body with more than one `return`** (early-out FP): the GLSL emitter must fail loudly (return -1), never emit a shader that silently skips the colour write — pinned in Task 3 test `fp_rejects_early_return` (via `rsx_fp_glsl_from_hlsl`).
2. **Reserved GLSL words in the HLSL dialect** (`input` is reserved in GLSL): every `input.x` must become `rsx_in.x`, and an identifier that merely ends in `input` (e.g. `myinput.x`) must be left alone — pinned in Task 2 test `rewrite_input_boundary`.
3. **Nested parentheses inside compare operands** (`(float4)(((r[0]).x) < (abs(r[1]).x))`): the compare rewrite must keep the operands byte-for-byte — pinned in Task 2 test `rewrite_compare_nested`.
4. **Output buffer overflow** (huge programs near the 96 KiB bodies): rewrite and emitters return -1, no truncated GLSL — pinned in Task 2 test `rewrite_overflow` and Task 3 test `fp_tiny_out_buffer`.
5. **Dump probe with the env var unset or pointing to a missing dir**: must write nothing and not crash — pinned in Task 5 tests `dump_off_is_noop` / `dump_bad_dir`.

---

### Task 1: shaderc dependency + CMake wiring

**Files:**
- Modify: `CMakeLists.txt` (after the Vulkan block at lines 165-173; tests section near line 546)
- Create: `libs/video/tests/test_shaderc_smoke.c`

**Interfaces:**
- Produces: CMake variable `PS3RECOMP_HAVE_SHADERC` (ON/OFF) and imported target `PkgConfig::SHADERC` when found. Later tasks add GLSL tests only `if(PS3RECOMP_HAVE_SHADERC)`.

- [ ] **Step 1: Install shaderc and confirm pkg-config sees it**

```bash
brew install shaderc
pkg-config --modversion shaderc && pkg-config --cflags --libs shaderc
```
Expected: a version (e.g. `2026.4`) and `-I/opt/homebrew/include ... -lshaderc_shared`. If `shaderc` is not a pkg-config name, try `shaderc_combined` and use that name in Step 3.

- [ ] **Step 2: Write the failing smoke test**

`libs/video/tests/test_shaderc_smoke.c`:
```c
/* shaderc links and compiles a trivial Vulkan GLSL fragment shader. */
#include <shaderc/shaderc.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    const char* src =
        "#version 450\n"
        "layout(location = 0) out vec4 c;\n"
        "void main() { c = vec4(1.0); }\n";
    shaderc_compiler_t cc = shaderc_compiler_initialize();
    shaderc_compile_options_t op = shaderc_compile_options_initialize();
    shaderc_compile_options_set_target_env(op, shaderc_target_env_vulkan,
                                           shaderc_env_version_vulkan_1_2);
    shaderc_compilation_result_t r = shaderc_compile_into_spv(
        cc, src, strlen(src), shaderc_fragment_shader, "smoke.frag", "main", op);
    int ok = shaderc_result_get_compilation_status(r) == shaderc_compilation_status_success;
    if (!ok) printf("FAIL: %s\n", shaderc_result_get_error_message(r));
    else printf("[PASS] shaderc smoke: %zu bytes SPIR-V\n", shaderc_result_get_length(r));
    shaderc_result_release(r);
    shaderc_compile_options_release(op);
    shaderc_compiler_release(cc);
    return ok ? 0 : 1;
}
```

- [ ] **Step 3: Wire CMake**

After the `find_package(Vulkan QUIET)` block (ends at the `endif()` near line 173), add:
```cmake
# shaderc: GLSL -> SPIR-V for the Vulkan RSX backend (system package:
# brew install shaderc / apt install libshaderc-dev). Optional: without it the
# GLSL tests are skipped and the Vulkan backend stays clear-only.
find_package(PkgConfig QUIET)
set(PS3RECOMP_HAVE_SHADERC OFF)
if(PkgConfig_FOUND)
    pkg_check_modules(SHADERC QUIET IMPORTED_TARGET shaderc)
    if(SHADERC_FOUND)
        set(PS3RECOMP_HAVE_SHADERC ON)
        message(STATUS "ps3recomp: shaderc ${SHADERC_VERSION} found")
    endif()
endif()
if(NOT PS3RECOMP_HAVE_SHADERC)
    message(STATUS "ps3recomp: shaderc not found, GLSL->SPIR-V tests disabled")
endif()
```
In the tests section, right after `add_test(NAME test_msl_decompiler ...)` (line ~560), add:
```cmake
    if(PS3RECOMP_HAVE_SHADERC)
        add_executable(test_shaderc_smoke libs/video/tests/test_shaderc_smoke.c)
        target_link_libraries(test_shaderc_smoke PRIVATE PkgConfig::SHADERC)
        add_test(NAME test_shaderc_smoke COMMAND test_shaderc_smoke)
    endif()
```

- [ ] **Step 4: Configure, build, run**

```bash
cmake -B build-tests -G Ninja -DPS3RECOMP_BUILD_TESTS=ON .
cmake --build build-tests --target test_shaderc_smoke test_msl_decompiler
ctest --test-dir build-tests -R "test_shaderc_smoke|test_msl_decompiler" --output-on-failure
```
Expected: configure prints `shaderc ... found`; both tests PASS.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt libs/video/tests/test_shaderc_smoke.c
git commit -m "build(vulkan): acha o shaderc pelo pkg-config e adiciona smoke de GLSL->SPIR-V"
```

---

### Task 2: `rsx_glsl_dialect` — prelude + body rewrite

**Files:**
- Create: `libs/video/rsx_glsl_dialect.h`, `libs/video/rsx_glsl_dialect.c`
- Create: `libs/video/tests/test_glsl_dialect.c`
- Modify: `CMakeLists.txt` (tests section, next to Task 1's block)

**Interfaces:**
- Produces:
  - `extern const char RSX_GLSL_PRELUDE[];` — goes right after `#version 450\n`.
  - `int rsx_glsl_rewrite_body(const char* in, char* out, u32 out_size);` — 0 ok, -1 overflow/null.
- The runtime library picks the new `.c` up automatically (`file(GLOB_RECURSE HLE_SOURCES` at `CMakeLists.txt:99`) after a re-configure.

- [ ] **Step 1: Write the failing tests**

`libs/video/tests/test_glsl_dialect.c`:
```c
/* HLSL-dialect -> GLSL rewrite: exact-string cases taken from what the NV40
 * decompilers emit (rsx_fp_decompiler.c / rsx_vp_decompiler.c). */
#include "rsx_glsl_dialect.h"
#include <stdio.h>
#include <string.h>

static int fails = 0;
static void expect_rw(const char* name, const char* in, const char* want)
{
    char out[4096];
    int rc = rsx_glsl_rewrite_body(in, out, sizeof out);
    if (rc != 0 || strcmp(out, want) != 0) {
        printf("FAIL %s\n  in:   %s\n  want: %s\n  got:  %s (rc=%d)\n", name, in, want,
               rc == 0 ? out : "", rc);
        fails++;
    } else {
        printf("[PASS] %s\n", name);
    }
}

int main(void)
{
    expect_rw("rewrite_unroll_and_zero_cast",
        "    [unroll] for (int _i = 0; _i < 48; _i++) { r[_i] = (float4)0; }\n",
        "    for (int _i = 0; _i < 48; _i++) { r[_i] = float4(0); }\n");
    expect_rw("rewrite_compare_fp_spacing",
        "r[0] = (float4)((r[1].xyzw) <  (r[2].xyzw));",
        "r[0] = rsx_slt((r[1].xyzw), (r[2].xyzw));");
    expect_rw("rewrite_compare_vp_nospace",
        "r[3] = (float4)((v[0].xyzw)>=(vp_c[4].xyzw));",
        "r[3] = rsx_sge((v[0].xyzw), (vp_c[4].xyzw));");
    expect_rw("rewrite_compare_all_ops",
        "(float4)((a)<=(b)) (float4)((a)>(b)) (float4)((a)==(b)) (float4)((a)!=(b))",
        "rsx_sle((a), (b)) rsx_sgt((a), (b)) rsx_seq((a), (b)) rsx_sne((a), (b))");
    expect_rw("rewrite_compare_nested",
        "(float4)(((r[0]).x) < (abs(r[1]).x))",
        "rsx_slt(((r[0]).x), (abs(r[1]).x))");
    expect_rw("rewrite_tex_sample",
        "rsx_tex[3].Sample(rsx_samp[3], (r[1]).xy)",
        "texture(rsx_tex[3], (r[1]).xy)");
    expect_rw("rewrite_tex_level_bias_grad",
        "rsx_tex[12].SampleLevel(rsx_samp[12], u, l) "
        "rsx_tex[0].SampleBias(rsx_samp[0], u, b) "
        "rsx_tex[1].SampleGrad(rsx_samp[1], u, dx, dy)",
        "textureLod(rsx_tex[12], u, l) texture(rsx_tex[0], u, b) textureGrad(rsx_tex[1], u, dx, dy)");
    expect_rw("rewrite_kil",
        "    if (any((r[0]).xyzw < 0.0)) discard;\n",
        "    if (any(lessThan(r[0], vec4(0.0)))) discard;\n");
    expect_rw("rewrite_input_boundary",
        "r[0] = input.col0 + myinput.x + input.tc3;",
        "r[0] = rsx_in.col0 + myinput.x + rsx_in.tc3;");
    expect_rw("rewrite_passthrough",
        "{ float _s = dot((r[0]).xyz,(r[1]).xyz); r[2].x = _s; }",
        "{ float _s = dot((r[0]).xyz,(r[1]).xyz); r[2].x = _s; }");

    /* rewrite_overflow: output too small -> -1, never a truncated string */
    {
        char tiny[8];
        int rc = rsx_glsl_rewrite_body("r[0] = (float4)0;", tiny, sizeof tiny);
        if (rc != -1) { printf("FAIL rewrite_overflow rc=%d\n", rc); fails++; }
        else printf("[PASS] rewrite_overflow\n");
    }
    /* prelude sanity: the macros and the compare helpers exist */
    if (!strstr(RSX_GLSL_PRELUDE, "#define float4 vec4") ||
        !strstr(RSX_GLSL_PRELUDE, "vec4 rsx_slt(vec4 a, vec4 b)") ||
        !strstr(RSX_GLSL_PRELUDE, "saturate(vec4 x)")) {
        printf("FAIL prelude_shape\n"); fails++;
    } else printf("[PASS] prelude_shape\n");

    printf(fails ? "%d FAILED\n" : "ALL PASS\n", fails);
    return fails ? 1 : 0;
}
```
Add to `CMakeLists.txt` tests section (outside the shaderc `if`, it needs no shaderc):
```cmake
    add_executable(test_glsl_dialect
        libs/video/tests/test_glsl_dialect.c
        libs/video/rsx_glsl_dialect.c)
    target_include_directories(test_glsl_dialect PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/libs/video"
        "${CMAKE_CURRENT_SOURCE_DIR}/include")
    add_test(NAME test_glsl_dialect COMMAND test_glsl_dialect)
```

- [ ] **Step 2: Run to confirm it fails**

```bash
cmake -B build-tests -G Ninja -DPS3RECOMP_BUILD_TESTS=ON . && cmake --build build-tests --target test_glsl_dialect
```
Expected: build FAILS (`rsx_glsl_dialect.h` not found).

- [ ] **Step 3: Implement**

`libs/video/rsx_glsl_dialect.h`:
```c
/*
 * ps3recomp - HLSL dialect -> Vulkan GLSL 450
 *
 * The NV40 decompilers emit one HLSL dialect (D3D12 consumes it as is, Metal
 * through a string rewrite). For Vulkan the dialect is kept and made legal
 * GLSL by a compat prelude (type macros + intrinsic overloads) plus three
 * textual rewrites the prelude cannot express: `(float4)0` casts, vector
 * compares `(float4)((A) op (B))`, and `rsx_tex[N].Sample*(rsx_samp[N], ...)`.
 * `input.` becomes `rsx_in.` because `input` is a reserved word in GLSL.
 */
#ifndef PS3RECOMP_RSX_GLSL_DIALECT_H
#define PS3RECOMP_RSX_GLSL_DIALECT_H

#include "ps3emu/ps3types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Place right after "#version 450\n". */
extern const char RSX_GLSL_PRELUDE[];

/* Rewrite a decompiler body. 0 on success, -1 on null args or overflow
 * (never leaves a truncated string that looks valid: out[0] = '\0'). */
int rsx_glsl_rewrite_body(const char* in, char* out, u32 out_size);

#ifdef __cplusplus
}
#endif
#endif /* PS3RECOMP_RSX_GLSL_DIALECT_H */
```

`libs/video/rsx_glsl_dialect.c`:
```c
/*
 * ps3recomp - HLSL dialect -> Vulkan GLSL 450 (see rsx_glsl_dialect.h)
 */
#include "rsx_glsl_dialect.h"

#include <stdio.h>
#include <string.h>

const char RSX_GLSL_PRELUDE[] =
    "#define float2 vec2\n"
    "#define float3 vec3\n"
    "#define float4 vec4\n"
    "#define int2 ivec2\n"
    "#define int3 ivec3\n"
    "#define int4 ivec4\n"
    "#define uint2 uvec2\n"
    "#define uint3 uvec3\n"
    "#define uint4 uvec4\n"
    "float saturate(float x) { return clamp(x, 0.0, 1.0); }\n"
    "vec2 saturate(vec2 x) { return clamp(x, 0.0, 1.0); }\n"
    "vec3 saturate(vec3 x) { return clamp(x, 0.0, 1.0); }\n"
    "vec4 saturate(vec4 x) { return clamp(x, 0.0, 1.0); }\n"
    "float frac(float x) { return fract(x); }\n"
    "vec2 frac(vec2 x) { return fract(x); }\n"
    "vec3 frac(vec3 x) { return fract(x); }\n"
    "vec4 frac(vec4 x) { return fract(x); }\n"
    "float lerp(float a, float b, float t) { return mix(a, b, t); }\n"
    "vec2 lerp(vec2 a, vec2 b, vec2 t) { return mix(a, b, t); }\n"
    "vec3 lerp(vec3 a, vec3 b, vec3 t) { return mix(a, b, t); }\n"
    "vec4 lerp(vec4 a, vec4 b, vec4 t) { return mix(a, b, t); }\n"
    "float rsqrt(float x) { return inversesqrt(x); }\n"
    "vec2 rsqrt(vec2 x) { return inversesqrt(x); }\n"
    "vec3 rsqrt(vec3 x) { return inversesqrt(x); }\n"
    "vec4 rsqrt(vec4 x) { return inversesqrt(x); }\n"
    "float ddx(float x) { return dFdx(x); }\n"
    "vec2 ddx(vec2 x) { return dFdx(x); }\n"
    "vec3 ddx(vec3 x) { return dFdx(x); }\n"
    "vec4 ddx(vec4 x) { return dFdx(x); }\n"
    "float ddy(float x) { return dFdy(x); }\n"
    "vec2 ddy(vec2 x) { return dFdy(x); }\n"
    "vec3 ddy(vec3 x) { return dFdy(x); }\n"
    "vec4 ddy(vec4 x) { return dFdy(x); }\n"
    "float asfloat(uint x) { return uintBitsToFloat(x); }\n"
    "float asfloat(int x) { return intBitsToFloat(x); }\n"
    "float asfloat(float x) { return x; }\n"
    "uint asuint(float x) { return floatBitsToUint(x); }\n"
    "uint asuint(uint x) { return x; }\n"
    "int asint(float x) { return floatBitsToInt(x); }\n"
    "uint f32tof16(float x) { return packHalf2x16(vec2(x, 0.0)) & 0xFFFFu; }\n"
    "float f16tof32(uint x) { return unpackHalf2x16(x & 0xFFFFu).x; }\n"
    "vec4 lit(float ndl, float nds, float m) {\n"
    "  float d = max(ndl, 0.0);\n"
    "  float s = (ndl > 0.0) ? pow(max(nds, 0.0), m) : 0.0;\n"
    "  return vec4(1.0, d, s, 1.0);\n"
    "}\n"
    "vec4 rsx_slt(vec4 a, vec4 b) { return vec4(lessThan(a, b)); }\n"
    "vec4 rsx_sle(vec4 a, vec4 b) { return vec4(lessThanEqual(a, b)); }\n"
    "vec4 rsx_sgt(vec4 a, vec4 b) { return vec4(greaterThan(a, b)); }\n"
    "vec4 rsx_sge(vec4 a, vec4 b) { return vec4(greaterThanEqual(a, b)); }\n"
    "vec4 rsx_seq(vec4 a, vec4 b) { return vec4(equal(a, b)); }\n"
    "vec4 rsx_sne(vec4 a, vec4 b) { return vec4(notEqual(a, b)); }\n"
    "float rsx_slt(float a, float b) { return float(a < b); }\n"
    "float rsx_sle(float a, float b) { return float(a <= b); }\n"
    "float rsx_sgt(float a, float b) { return float(a > b); }\n"
    "float rsx_sge(float a, float b) { return float(a >= b); }\n"
    "float rsx_seq(float a, float b) { return float(a == b); }\n"
    "float rsx_sne(float a, float b) { return float(a != b); }\n";

typedef struct { char* p; u32 cap; u32 len; int bad; } GBuf;

static void g_putn(GBuf* b, const char* s, size_t n)
{
    if (b->bad) return;
    if ((size_t)b->len + n + 1u > b->cap) { b->bad = 1; return; }
    memcpy(b->p + b->len, s, n);
    b->len += (u32)n;
    b->p[b->len] = '\0';
}
static void g_puts(GBuf* b, const char* s) { g_putn(b, s, strlen(s)); }

static int starts(const char* p, const char* lit) { return strncmp(p, lit, strlen(lit)) == 0; }

static int is_ident(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

/* p at '(' -> its matching ')', or NULL. */
static const char* match_paren(const char* p)
{
    int d = 0;
    for (; *p; p++) {
        if (*p == '(') d++;
        else if (*p == ')' && --d == 0) return p;
    }
    return NULL;
}

/* "(float4)(" G1 op G2 ")" with G1/G2 parenthesised -> "rsx_sXX(G1, G2)". */
static const char* try_vec_compare(const char* p, GBuf* b)
{
    if (!starts(p, "(float4)((")) return NULL;
    const char* g1 = p + 9;                 /* '(' of the first operand */
    const char* g1e = match_paren(g1);
    if (!g1e) return NULL;
    const char* q = g1e + 1;
    while (*q == ' ') q++;
    const char* fn;
    size_t oplen = 2;
    if (starts(q, "<=")) fn = "rsx_sle";
    else if (starts(q, ">=")) fn = "rsx_sge";
    else if (starts(q, "==")) fn = "rsx_seq";
    else if (starts(q, "!=")) fn = "rsx_sne";
    else if (*q == '<') { fn = "rsx_slt"; oplen = 1; }
    else if (*q == '>') { fn = "rsx_sgt"; oplen = 1; }
    else return NULL;
    q += oplen;
    while (*q == ' ') q++;
    if (*q != '(') return NULL;
    const char* g2e = match_paren(q);
    if (!g2e || g2e[1] != ')') return NULL;
    g_puts(b, fn);
    g_puts(b, "(");
    g_putn(b, g1, (size_t)(g1e - g1 + 1));
    g_puts(b, ", ");
    g_putn(b, q, (size_t)(g2e - q + 1));
    g_puts(b, ")");
    return g2e + 2;                         /* past the cast's closing ')' */
}

/* "rsx_tex[N].Sample*(rsx_samp[N], " -> "texture*(rsx_tex[N], " */
static const char* try_texture(const char* p, GBuf* b)
{
    static const struct { const char* m; const char* fn; } k[] = {
        { ".SampleLevel(", "textureLod(" },
        { ".SampleBias(",  "texture(" },    /* GLSL 3rd arg of texture() is the bias */
        { ".SampleGrad(",  "textureGrad(" },
        { ".Sample(",      "texture(" },
    };
    if (!starts(p, "rsx_tex[")) return NULL;
    const char* q = p + 8;
    unsigned unit = 0;
    int nd = 0;
    while (*q >= '0' && *q <= '9') { unit = unit * 10u + (unsigned)(*q - '0'); q++; nd++; }
    if (!nd || *q != ']') return NULL;
    q++;
    for (size_t i = 0; i < sizeof k / sizeof k[0]; i++) {
        if (!starts(q, k[i].m)) continue;
        const char* s = q + strlen(k[i].m);
        char samp[32];
        int sl = snprintf(samp, sizeof samp, "rsx_samp[%u], ", unit);
        if (strncmp(s, samp, (size_t)sl) != 0) return NULL;
        char head[48];
        snprintf(head, sizeof head, "%srsx_tex[%u], ", k[i].fn, unit);
        g_puts(b, head);
        return s + sl;
    }
    return NULL;
}

int rsx_glsl_rewrite_body(const char* in, char* out, u32 out_size)
{
    if (!in || !out || out_size == 0) return -1;
    GBuf b = { out, out_size, 0, 0 };
    out[0] = '\0';
    const char* p = in;
    while (*p && !b.bad) {
        const char* n;
        if ((n = try_vec_compare(p, &b)) != NULL) { p = n; continue; }
        if ((n = try_texture(p, &b)) != NULL) { p = n; continue; }
        if (starts(p, "[unroll] ")) { p += strlen("[unroll] "); continue; }
        if (starts(p, "(float4)0")) { g_puts(&b, "float4(0)"); p += strlen("(float4)0"); continue; }
        if (starts(p, "any((r[0]).xyzw < 0.0)")) {
            g_puts(&b, "any(lessThan(r[0], vec4(0.0)))");
            p += strlen("any((r[0]).xyzw < 0.0)");
            continue;
        }
        if (starts(p, "input.") && (p == in || !is_ident(p[-1]))) {
            g_puts(&b, "rsx_in.");
            p += strlen("input.");
            continue;
        }
        g_putn(&b, p, 1);
        p++;
    }
    if (b.bad) { out[0] = '\0'; return -1; }
    return 0;
}
```

- [ ] **Step 4: Run tests**

```bash
cmake -B build-tests -G Ninja -DPS3RECOMP_BUILD_TESTS=ON . && cmake --build build-tests --target test_glsl_dialect
ctest --test-dir build-tests -R test_glsl_dialect --output-on-failure
```
Expected: `ALL PASS` (12 lines `[PASS]`).

- [ ] **Step 5: Commit**

```bash
git add libs/video/rsx_glsl_dialect.h libs/video/rsx_glsl_dialect.c libs/video/tests/test_glsl_dialect.c CMakeLists.txt
git commit -m "feat(rsx): dialeto HLSL->GLSL 450 (preludio de compatibilidade + reescritas)"
```

---

### Task 3: `rsx_fp_decompile_glsl`

**Files:**
- Modify: `libs/video/rsx_fp_decompiler.h` (declare after `rsx_fp_decompile_msl`, line ~49)
- Modify: `libs/video/rsx_fp_decompiler.c` (factor body extraction out of `rsx_fp_decompile_msl` at lines 790-812; add the GLSL function after `rsx_fp_decompile_msl`, which ends at line 935)
- Create: `libs/video/tests/test_glsl_decompiler.c`
- Modify: `CMakeLists.txt` (tests, inside `if(PS3RECOMP_HAVE_SHADERC)`)

**Interfaces:**
- Consumes: `RSX_GLSL_PRELUDE`, `rsx_glsl_rewrite_body` (Task 2); existing `rsx_fp_set_const_buffer_mode`, `rsx_fp_set_color_export`, `rsx_fp_set_depth_export`, `rsx_fp_set_mrt_count`.
- Produces:
  - `int rsx_fp_decompile_glsl(const u8* ucode, u32 max_bytes, char* out, u32 out_size);` — instruction count or -1. Honours the same setters as the MSL path.
  - `int rsx_fp_glsl_from_hlsl(const char* hlsl, int n_instr, char* out, u32 out_size);` — the wrapper stage on its own (HLSL text from `rsx_fp_decompile` in, GLSL out); returns `n_instr` or -1. Exposed so tests can feed hand-written HLSL (e.g. two returns).

- [ ] **Step 1: Write the failing test**

`libs/video/tests/test_glsl_decompiler.c`:
```c
/* NV40 FP/VP -> Vulkan GLSL 450, compiled to SPIR-V with shaderc. */
#include "rsx_fp_decompiler.h"
#include "rsx_vp_decompiler.h"
#include <shaderc/shaderc.h>
#include <stdio.h>
#include <string.h>

static int fails = 0;

static int compiles(const char* src, shaderc_shader_kind kind, const char* name)
{
    shaderc_compiler_t cc = shaderc_compiler_initialize();
    shaderc_compile_options_t op = shaderc_compile_options_initialize();
    shaderc_compile_options_set_target_env(op, shaderc_target_env_vulkan,
                                           shaderc_env_version_vulkan_1_2);
    shaderc_compilation_result_t r =
        shaderc_compile_into_spv(cc, src, strlen(src), kind, name, "main", op);
    int ok = shaderc_result_get_compilation_status(r) == shaderc_compilation_status_success;
    if (!ok) printf("shaderc (%s):\n%s\n--- source ---\n%s\n", name,
                    shaderc_result_get_error_message(r), src);
    shaderc_result_release(r);
    shaderc_compile_options_release(op);
    shaderc_compiler_release(cc);
    return ok;
}

static void check(const char* name, int cond)
{
    if (!cond) { printf("FAIL %s\n", name); fails++; }
    else printf("[PASS] %s\n", name);
}

/* FP words: big-endian with half-word swap, as the RSX reads them. */
static void fp_word(unsigned char* p, u32 h)
{
    u32 be = (h << 16) | (h >> 16);
    p[0] = (unsigned char)(be >> 24); p[1] = (unsigned char)(be >> 16);
    p[2] = (unsigned char)(be >> 8);  p[3] = (unsigned char)be;
}
#define FP_OPC(x)      ((u32)(x) << 24)
#define FP_OUTMASK_ALL (0xFu << 9)
#define FP_END         (1u << 0)
#define FP_INSRC(x)    ((u32)(x) << 13)
#define FP_SWZ_IDENT   ((0u<<9)|(1u<<11)|(2u<<13)|(3u<<15))
#define FP_T_INPUT     1u

static void fp_mov_r0_col0(unsigned char prog[16])
{
    fp_word(prog + 0, FP_OPC(0x01) | FP_OUTMASK_ALL | FP_INSRC(1) | FP_END);
    fp_word(prog + 4, FP_T_INPUT | FP_SWZ_IDENT);
    fp_word(prog + 8, 0);
    fp_word(prog + 12, 0);
}

int main(void)
{
    static char glsl[128 * 1024];
    unsigned char fp[16];
    fp_mov_r0_col0(fp);

    /* fp_basic: MOV r0, COL0 */
    int n = rsx_fp_decompile_glsl(fp, sizeof fp, glsl, sizeof glsl);
    check("fp_basic_decompiles", n == 1);
    check("fp_basic_shape",
          strstr(glsl, "#version 450") && strstr(glsl, "rsx_in.col0") &&
          strstr(glsl, "layout(location = 0) out vec4 rsx_c0") &&
          strstr(glsl, "layout(constant_id = 0) const bool rsx_at_on") &&
          !strstr(glsl, "SV_TARGET") && !strstr(glsl, "input."));
    check("fp_basic_compiles", compiles(glsl, shaderc_fragment_shader, "fp_basic.frag"));

    /* fp_mrt4_depth: 4 colour outputs + depth export compile */
    rsx_fp_set_mrt_count(4);
    rsx_fp_set_depth_export(1);
    n = rsx_fp_decompile_glsl(fp, sizeof fp, glsl, sizeof glsl);
    rsx_fp_set_mrt_count(1);
    rsx_fp_set_depth_export(0);
    check("fp_mrt4_outputs", n == 1 && strstr(glsl, "out vec4 rsx_c3") && strstr(glsl, "rsx_c3 = r[4]"));
    check("fp_mrt4_compiles", compiles(glsl, shaderc_fragment_shader, "fp_mrt4.frag"));

    /* fp_const_buffer: const-buffer mode declares the rsx_fc SSBO */
    rsx_fp_set_const_buffer_mode(1);
    n = rsx_fp_decompile_glsl(fp, sizeof fp, glsl, sizeof glsl);
    rsx_fp_set_const_buffer_mode(0);
    check("fp_const_buffer_decl", n == 1 && strstr(glsl, "binding = 1) readonly buffer RsxFc"));
    check("fp_const_buffer_compiles", compiles(glsl, shaderc_fragment_shader, "fp_fc.frag"));

    /* fp_tiny_out_buffer: overflow -> -1 */
    {
        char tiny[300];
        check("fp_tiny_out_buffer", rsx_fp_decompile_glsl(fp, sizeof fp, tiny, sizeof tiny) == -1);
    }

    printf(fails ? "%d FAILED\n" : "ALL PASS\n", fails);
    return fails ? 1 : 0;
}
```
(`fp_rejects_early_return` is added in Step 4, once `rsx_fp_glsl_from_hlsl` exists.)

CMake, inside `if(PS3RECOMP_HAVE_SHADERC)` from Task 1:
```cmake
        add_executable(test_glsl_decompiler
            libs/video/tests/test_glsl_decompiler.c
            libs/video/rsx_fp_decompiler.c
            libs/video/rsx_vp_decompiler.c
            libs/video/rsx_glsl_dialect.c)
        target_include_directories(test_glsl_decompiler PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/libs/video"
            "${CMAKE_CURRENT_SOURCE_DIR}/include")
        target_link_libraries(test_glsl_decompiler PRIVATE PkgConfig::SHADERC)
        add_test(NAME test_glsl_decompiler COMMAND test_glsl_decompiler)
```

- [ ] **Step 2: Run to confirm it fails**

```bash
cmake -B build-tests -G Ninja -DPS3RECOMP_BUILD_TESTS=ON . && cmake --build build-tests --target test_glsl_decompiler
```
Expected: compile error, `rsx_fp_decompile_glsl` undeclared.

- [ ] **Step 3: Implement**

In `rsx_fp_decompiler.h`, after the `rsx_fp_decompile_msl` declaration:
```c
/* Same as rsx_fp_decompile_msl, but emit Vulkan GLSL 450 (entry `main`).
 * Interface (plan 2026-09-23-vulkan-v0): varyings at locations 0 col0, 1 col1,
 * 2 fog, 3..10 tc0..tc7; set 0 binding 0 sampler2D rsx_tex[16], binding 1
 * std430 rsx_fc[] (const-buffer mode), binding 2 std140 uvec4 rsx_at; alpha
 * test = specialization constant 0; outputs rsx_c0..rsx_c<mrt-1>, depth
 * export -> gl_FragDepth. Honours the same setters as the MSL path. Returns
 * instruction count or -1 (overflow, malformed body, more than one return). */
int rsx_fp_decompile_glsl(const u8* ucode, u32 max_bytes, char* out, u32 out_size);

/* The wrapper stage alone: `hlsl` is rsx_fp_decompile output, `n_instr` its
 * return value (passed through on success). For tests and the corpus tool. */
int rsx_fp_glsl_from_hlsl(const char* hlsl, int n_instr, char* out, u32 out_size);
```

In `rsx_fp_decompiler.c`, add `#include "rsx_glsl_dialect.h"` next to the existing includes. Replace the body-extraction block inside `rsx_fp_decompile_msl` (from `const char* sig = "float4 main(PSInput input) : SV_TARGET {";` through `body_cut[blen] = '\0';`, lines ~795-812) with a call to this new static helper placed just above `rsx_fp_decompile_msl`:
```c
/* Cut the HLSL function body out of rsx_fp_decompile's output (between the
 * entry signature and the final closing brace). Shared by the MSL and GLSL
 * emitters so both see exactly the same instructions. 0 ok / -1. */
static int fp_hlsl_body(const char* hlsl, char* dst, u32 dst_size)
{
    const char* sig = "float4 main(PSInput input) : SV_TARGET {";
    const char* body = strstr(hlsl, sig);
    if (!body) return -1;
    body += strlen(sig);
    size_t blen = strlen(body);
    while (blen > 0 && (body[blen - 1] == '\n' || body[blen - 1] == '\r' ||
                        body[blen - 1] == ' '))
        blen--;
    if (blen > 0 && body[blen - 1] == '}') blen--;
    while (blen > 0 && (body[blen - 1] == '\n' || body[blen - 1] == ' '))
        blen--;
    if (blen + 1u > dst_size) return -1;
    memcpy(dst, body, blen);
    dst[blen] = '\0';
    return 0;
}
```
and in `rsx_fp_decompile_msl` the replaced block becomes:
```c
    static char body_cut[96 * 1024];
    if (fp_hlsl_body(hlsl, body_cut, sizeof(body_cut)) != 0) return -1;
```
(`body_cut` was a 96 KiB stack array; making it `static` matches the other buffers in the function and changes no output.)

Then add after `rsx_fp_decompile_msl`:
```c
int rsx_fp_decompile_glsl(const u8* ucode, u32 max_bytes, char* out, u32 out_size)
{
    static char hlsl[96 * 1024];
    if (!ucode || !out || out_size < 256) return -1;
    out[0] = '\0';
    int n = rsx_fp_decompile(ucode, max_bytes, hlsl, sizeof(hlsl));
    if (n < 0) return -1;
    return rsx_fp_glsl_from_hlsl(hlsl, n, out, out_size);
}

int rsx_fp_glsl_from_hlsl(const char* hlsl, int n, char* out, u32 out_size)
{
    static char body_cut[96 * 1024];
    static char body[96 * 1024];
    if (!hlsl || !out || out_size < 256) return -1;
    out[0] = '\0';
    if (fp_hlsl_body(hlsl, body_cut, sizeof(body_cut)) != 0) return -1;
    if (rsx_glsl_rewrite_body(body_cut, body, sizeof(body)) != 0) return -1;

    /* Exactly one `return X;` (the colour export). An early return would skip
     * the output writes below, so refuse instead of emitting a wrong shader. */
    char* ret = strstr(body, "return ");
    if (!ret || strstr(ret + 1, "return ")) return -1;
    const char* expr = ret + 7;
    const char* semi = strchr(expr, ';');
    if (!semi) return -1;
    char tail[256];
    const size_t elen = (size_t)(semi - expr);
    if (elen + 1u > sizeof(tail)) return -1;
    memcpy(tail, expr, elen);
    tail[elen] = '\0';

    const int depth_out = s_fp_depth_export && s_fp_wrote_r1;
    const int mrt = s_fp_mrt_count < 1 ? 1 : (s_fp_mrt_count > 4 ? 4 : s_fp_mrt_count);

    u32 pos = 0;
    if (msl_append(out, out_size, &pos, "#version 450\n") != 0) goto overflow;
    if (msl_append(out, out_size, &pos, RSX_GLSL_PRELUDE) != 0) goto overflow;
    if (msl_append(out, out_size, &pos,
        "struct PSIn { vec4 position; vec4 col0; vec4 col1; vec4 fog;\n"
        "  vec4 tc0; vec4 tc1; vec4 tc2; vec4 tc3; vec4 tc4; vec4 tc5; vec4 tc6; vec4 tc7; };\n"
        "layout(location = 0) in vec4 v_col0;\n"
        "layout(location = 1) in vec4 v_col1;\n"
        "layout(location = 2) in vec4 v_fog;\n"
        "layout(location = 3) in vec4 v_tc0;\n"
        "layout(location = 4) in vec4 v_tc1;\n"
        "layout(location = 5) in vec4 v_tc2;\n"
        "layout(location = 6) in vec4 v_tc3;\n"
        "layout(location = 7) in vec4 v_tc4;\n"
        "layout(location = 8) in vec4 v_tc5;\n"
        "layout(location = 9) in vec4 v_tc6;\n"
        "layout(location = 10) in vec4 v_tc7;\n"
        "layout(set = 0, binding = 0) uniform sampler2D rsx_tex[16];\n"
        "layout(std140, set = 0, binding = 2) uniform RsxAt { uvec4 rsx_at; };\n"
        "layout(constant_id = 0) const bool rsx_at_on = false;\n"
        /* RSX alpha test (RPCS3 RSXROPEpilogue.glsl), same table as the MSL. */
        "void rsx_atest(vec4 c, uvec4 at) {\n"
        "  if (!rsx_at_on) return;\n"
        "  float a = c.w, b = uintBitsToFloat(at.z); bool p;\n"
        "  switch (at.y & 7u) {\n"
        "  case 0u: p = false; break;\n"
        "  case 1u: p = a < b; break;\n"
        "  case 2u: p = a == b; break;\n"
        "  case 3u: p = a <= b; break;\n"
        "  case 4u: p = a > b; break;\n"
        "  case 5u: p = a != b; break;\n"
        "  case 6u: p = a >= b; break;\n"
        "  default: p = true; break;\n"
        "  }\n"
        "  if (!p) discard;\n"
        "}\n") != 0)
        goto overflow;
    if (s_fp_const_buf_mode &&
        msl_append(out, out_size, &pos,
                   "layout(std430, set = 0, binding = 1) readonly buffer RsxFc { vec4 rsx_fc[]; };\n") != 0)
        goto overflow;
    for (int c = 0; c < mrt; c++) {
        char decl[64];
        snprintf(decl, sizeof decl, "layout(location = %d) out vec4 rsx_c%d;\n", c, c);
        if (msl_append(out, out_size, &pos, decl) != 0) goto overflow;
    }
    if (msl_append(out, out_size, &pos,
        "void main() {\n"
        "    PSIn rsx_in;\n"
        "    rsx_in.position = gl_FragCoord; rsx_in.col0 = v_col0; rsx_in.col1 = v_col1;\n"
        "    rsx_in.fog = v_fog; rsx_in.tc0 = v_tc0; rsx_in.tc1 = v_tc1; rsx_in.tc2 = v_tc2;\n"
        "    rsx_in.tc3 = v_tc3; rsx_in.tc4 = v_tc4; rsx_in.tc5 = v_tc5; rsx_in.tc6 = v_tc6;\n"
        "    rsx_in.tc7 = v_tc7;\n") != 0)
        goto overflow;
    *ret = '\0';                                  /* body up to the return */
    {
        const int bad = msl_append(out, out_size, &pos, body) != 0;
        *ret = 'r';
        if (bad) goto overflow;
    }
    {
        /* c0 is what the program returned; c1..c3 come from the SAME bank's
         * export registers (r0 -> r2,r3,r4; h0 -> h4,h6,h8), as in the MSL. */
        const int bank_h = strstr(tail, "h[0]") != NULL;
        static const int idx_r[4] = { 0, 2, 3, 4 }, idx_h[4] = { 0, 4, 6, 8 };
        char epi[768];
        int ep = snprintf(epi, sizeof epi,
                          "{ vec4 _c0 = %s; rsx_atest(_c0, rsx_at); rsx_c0 = _c0;", tail);
        for (int c = 1; c < mrt; c++)
            ep += snprintf(epi + ep, sizeof epi - (size_t)ep, " rsx_c%d = %s[%d];", c,
                           bank_h ? "h" : "r", bank_h ? idx_h[c] : idx_r[c]);
        if (depth_out)
            ep += snprintf(epi + ep, sizeof epi - (size_t)ep, " gl_FragDepth = r[1].z;");
        snprintf(epi + ep, sizeof epi - (size_t)ep, " return; }%s", semi + 1);
        if (msl_append(out, out_size, &pos, epi) != 0) goto overflow;
    }
    if (msl_append(out, out_size, &pos, "\n}\n") != 0) goto overflow;
    return n;

overflow:
    out[0] = '\0';
    return -1;
}
```

- [ ] **Step 4: Add the early-return guard test, run everything**

Append to `main()` in `test_glsl_decompiler.c`, before the final `printf`:
```c
    /* fp_rejects_early_return: a body with two returns must be refused, never
     * emitted with the colour writes skipped on the early path. */
    {
        const char* two_ret =
            "struct PSInput { float4 position : SV_POSITION; };\n"
            "float4 main(PSInput input) : SV_TARGET {\n"
            "    float4 r[48]; float4 h[48];\n"
            "    if (r[1].x > 0.0) return r[1];\n"
            "    return r[0];\n"
            "}\n";
        check("fp_rejects_early_return",
              rsx_fp_glsl_from_hlsl(two_ret, 2, glsl, sizeof glsl) == -1 && glsl[0] == '\0');
        const char* one_ret =
            "float4 main(PSInput input) : SV_TARGET {\n"
            "    float4 r[48]; float4 h[48];\n"
            "    return r[0];\n"
            "}\n";
        check("fp_single_return_ok", rsx_fp_glsl_from_hlsl(one_ret, 1, glsl, sizeof glsl) == 1);
    }
```
Then:
```bash
cmake --build build-tests --target test_glsl_decompiler test_msl_decompiler
ctest --test-dir build-tests -R "test_glsl_decompiler|test_msl_decompiler|test_glsl_dialect" --output-on-failure
```
Expected: all PASS. If `fp_*_compiles` fails, read the shaderc message printed with the source; the fix goes in the prelude or `rsx_glsl_rewrite_body` **with a new `expect_rw` case in `test_glsl_dialect.c` first**.

- [ ] **Step 5: Prove Metal output is byte-identical**

```bash
git stash push libs/video/rsx_fp_decompiler.c -m v0-fp
cmake --build build-tests --target test_msl_decompiler && ./build-tests/test_msl_decompiler > /tmp/msl_before.txt
git stash pop
cmake --build build-tests --target test_msl_decompiler && ./build-tests/test_msl_decompiler > /tmp/msl_after.txt
diff /tmp/msl_before.txt /tmp/msl_after.txt && echo IDENTICAL
```
Expected: `IDENTICAL`.

- [ ] **Step 6: Commit**

```bash
git add libs/video/rsx_fp_decompiler.h libs/video/rsx_fp_decompiler.c libs/video/tests/test_glsl_decompiler.c CMakeLists.txt
git commit -m "feat(rsx): emissor GLSL 450 do fragment program (interface Vulkan)"
```

---

### Task 4: `rsx_vp_decompile_glsl`

**Files:**
- Modify: `libs/video/rsx_vp_decompiler.h` (declare after `rsx_vp_decompile_msl`, line ~42)
- Modify: `libs/video/rsx_vp_decompiler.c` (factor body extraction + fix-bit resolution out of `rsx_vp_decompile_msl`, lines 386-444; add the GLSL function after it, ends at line 549)
- Modify: `libs/video/tests/test_glsl_decompiler.c`

**Interfaces:**
- Consumes: `RSX_GLSL_PRELUDE`, `rsx_glsl_rewrite_body` (Task 2).
- Produces: `int rsx_vp_decompile_glsl(const u8* ucode, u32 max_bytes, char* out, u32 out_size);` — instruction count or -1. Same `PS3_RSX_FIX_*` / `rsx_vp_set_fix_override` semantics as the MSL path.

- [ ] **Step 1: Write the failing test**

Add to `test_glsl_decompiler.c` (helpers above `main`, calls inside `main` before the final `printf`):
```c
/* VP words are little-endian, 4 per instruction (RPCS3 raw .vp format). */
static void vp_word(unsigned char* p, u32 w)
{
    p[0] = (unsigned char)w; p[1] = (unsigned char)(w >> 8);
    p[2] = (unsigned char)(w >> 16); p[3] = (unsigned char)(w >> 24);
}
/* MOV o[0], v[0] (vector unit, vec_result, full mask), END.
 * src0 = input(reg_type 2) with identity swizzle = 0x1B02:
 *   low 9 bits -> D2[31:23] = 0x102, high 8 bits -> D1[7:0] = 0x0D. */
static void vp_mov_o0_v0(unsigned char prog[16])
{
    u32 d0 = (1u << 30) | (0x3Fu << 15);                 /* vec_result, dst_tmp none */
    u32 d1 = (0x01u << 22) | 0x0Du;                      /* vec MOV, src0 hi */
    u32 d2 = 0x102u << 23;                               /* src0 lo */
    u32 d3 = (1u << 16) | (1u << 15) | (1u << 14) | (1u << 13)   /* vec mask xyzw */
           | (0x3Fu << 7)                                        /* sca_dst_tmp none */
           | (0u << 2)                                           /* dst_out o[0] */
           | 1u;                                                 /* end */
    vp_word(prog + 0, d0); vp_word(prog + 4, d1);
    vp_word(prog + 8, d2); vp_word(prog + 12, d3);
}
```
Inside `main`:
```c
    unsigned char vp[16];
    vp_mov_o0_v0(vp);
    n = rsx_vp_decompile_glsl(vp, sizeof vp, glsl, sizeof glsl);
    check("vp_basic_decompiles", n == 1);
    check("vp_basic_shape",
          strstr(glsl, "invariant gl_Position;") && strstr(glsl, "o[0]") &&
          strstr(glsl, "gl_Position = o[0];") &&
          strstr(glsl, "layout(location = 10) out vec4 v_tc7") &&
          strstr(glsl, "binding = 4) readonly buffer RsxVam") &&
          !strstr(glsl, "[unroll]") && !strstr(glsl, "(float4)0"));
    check("vp_basic_compiles", compiles(glsl, shaderc_vertex_shader, "vp_basic.vert"));

    rsx_vp_set_fix_override(1 | 2 | 8);          /* OUT_W1, NO_TC0_HACK, FOG_X; seed on */
    n = rsx_vp_decompile_glsl(vp, sizeof vp, glsl, sizeof glsl);
    rsx_vp_set_fix_override(-1);
    check("vp_fix_bits_shape", n == 1 && strstr(glsl, "o[_j]=float4(0,0,0,1)") &&
          strstr(glsl, "v_fog = o[5].xxxx") && !strstr(glsl, "o[7] = v[8]"));
    check("vp_fix_bits_compiles", compiles(glsl, shaderc_vertex_shader, "vp_fix.vert"));
```

- [ ] **Step 2: Run to confirm it fails**

```bash
cmake --build build-tests --target test_glsl_decompiler
```
Expected: compile error, `rsx_vp_decompile_glsl` undeclared.

- [ ] **Step 3: Implement**

Header, after `rsx_vp_decompile_msl`:
```c
/* Same as rsx_vp_decompile_msl, but emit Vulkan GLSL 450 (entry `main`).
 * Inputs location 0 vec3 in_v0 / 1 vec4 in_v3 (mirror of Metal VSIn); outputs
 * location 0 col0, 1 col1, 2 fog, 3..10 tc0..tc7, `invariant gl_Position`;
 * set 0 binding 3 std430 vp_c[], binding 4 std430 VAMap (byte layout of
 * MetalVAMap), binding 5 std430 va[] (float4 ring, indexed by gl_VertexIndex).
 * Same PS3_RSX_FIX_* / override semantics as the MSL path. */
int rsx_vp_decompile_glsl(const u8* ucode, u32 max_bytes, char* out, u32 out_size);
```

In `rsx_vp_decompiler.c` add `#include "rsx_glsl_dialect.h"`. Above `rsx_vp_decompile_msl`, add two helpers and make the MSL function use them (the replaced code is the marker-based body cut at lines ~391-402 and the `fx_*` static block + override at lines ~418-444):
```c
/* Instruction body between the o[] zero-init loop and the VSOutput map. */
static int vp_hlsl_body(const char* hlsl, char* dst, u32 dst_size)
{
    const char* start_mark =
        "    [unroll] for (int _j=0;_j<32;_j++) o[_j]=(float4)0;\n";
    const char* end_mark = "    VSOutput Out;\n";
    const char* b0 = strstr(hlsl, start_mark);
    const char* b1 = strstr(hlsl, end_mark);
    if (!b0 || !b1 || b1 <= b0) return -1;
    b0 += strlen(start_mark);
    size_t blen = (size_t)(b1 - b0);
    if (blen + 1u > dst_size) return -1;
    memcpy(dst, b0, blen);
    dst[blen] = '\0';
    return 0;
}

typedef struct { int w1, nohack, noseed, fogx; } VpFix;

/* PS3_RSX_FIX_* A/B switches (read once) with rsx_vp_set_fix_override on top. */
static VpFix vp_fix_resolve(void)
{
    static int fx_w1 = -1, fx_nohack = -1, fx_noseed = -1, fx_fogx = -1;
    if (fx_w1 < 0) {
        const char* e;
        e = getenv("PS3_RSX_FIX_OUT_W1");      fx_w1     = (e && e[0] == '1');
        e = getenv("PS3_RSX_FIX_NO_TC0_HACK"); fx_nohack = (e && e[0] == '1');
        e = getenv("PS3_RSX_FIX_NO_UV_SEED");  fx_noseed = !(e && e[0] == '0');
        e = getenv("PS3_RSX_FIX_FOG_X");       fx_fogx   = (e && e[0] == '1');
        if (fx_w1 || fx_nohack || fx_noseed || fx_fogx)
            fprintf(stderr, "[RSXFIX] VP: out_w1=%d no_tc0_hack=%d no_uv_seed=%d fog_x=%d\n",
                    fx_w1, fx_nohack, fx_noseed, fx_fogx);
    }
    VpFix f = { fx_w1, fx_nohack, fx_noseed, fx_fogx };
    if (s_vp_fix_override >= 0) {
        f.w1 = (s_vp_fix_override >> 0) & 1; f.nohack = (s_vp_fix_override >> 1) & 1;
        f.noseed = (s_vp_fix_override >> 2) & 1; f.fogx = (s_vp_fix_override >> 3) & 1;
    }
    return f;
}
```
In `rsx_vp_decompile_msl`: replace the marker block with `if (vp_hlsl_body(hlsl, body, sizeof(body)) != 0) return -1;`, and replace the whole `static int fx_w1 ...` block through the override `if` with:
```c
    const VpFix fx = vp_fix_resolve();
    int u_w1 = fx.w1, u_nohack = fx.nohack, u_noseed = fx.noseed, u_fogx = fx.fogx;
```
(The existing comments describing the §1/§2/§3/§6 switches move onto `vp_fix_resolve` unchanged.)

Then after `rsx_vp_decompile_msl`:
```c
int rsx_vp_decompile_glsl(const u8* ucode, u32 max_bytes, char* out, u32 out_size)
{
    static char hlsl[96 * 1024];
    static char body_cut[96 * 1024];
    static char body[96 * 1024];
    if (!ucode || !out || out_size < 256) return -1;
    out[0] = '\0';

    int n = rsx_vp_decompile(ucode, max_bytes, hlsl, sizeof(hlsl));
    if (n < 0) return -1;
    if (vp_hlsl_body(hlsl, body_cut, sizeof(body_cut)) != 0) return -1;
    if (rsx_glsl_rewrite_body(body_cut, body, sizeof(body)) != 0) return -1;
    const VpFix fx = vp_fix_resolve();

    u32 pos = 0;
#define VP_PUT(s) do { if (vp_msl_append(out, out_size, &pos, (s)) != 0) goto overflow; } while (0)
    VP_PUT("#version 450\n");
    VP_PUT(RSX_GLSL_PRELUDE);
    VP_PUT("layout(location = 0) in vec3 in_v0;\n"
           "layout(location = 1) in vec4 in_v3;\n"
           "layout(location = 0) out vec4 v_col0;\n"
           "layout(location = 1) out vec4 v_col1;\n"
           "layout(location = 2) out vec4 v_fog;\n"
           "layout(location = 3) out vec4 v_tc0;\n"
           "layout(location = 4) out vec4 v_tc1;\n"
           "layout(location = 5) out vec4 v_tc2;\n"
           "layout(location = 6) out vec4 v_tc3;\n"
           "layout(location = 7) out vec4 v_tc4;\n"
           "layout(location = 8) out vec4 v_tc5;\n"
           "layout(location = 9) out vec4 v_tc6;\n"
           "layout(location = 10) out vec4 v_tc7;\n"
           /* RPCS3 `invariant gl_Position`: the Colossus is drawn twice over the
            * same mesh by two VPs; see rsx_vp_decompile_msl. */
           "invariant gl_Position;\n"
           "layout(std430, set = 0, binding = 3) readonly buffer RsxVpc { vec4 vp_c[]; };\n"
           /* std430 gives the byte layout of MetalVAMap (16 + 64 + 256 bytes). */
           "layout(std430, set = 0, binding = 4) readonly buffer RsxVam {\n"
           "  uint stride; uint base; uint vid_base; uint pad; int slot[16]; vec4 cur[16];\n"
           "} vam;\n"
           "layout(std430, set = 0, binding = 5) readonly buffer RsxVa { vec4 va[]; };\n"
           "void main() {\n"
           "    vec4 v[16];\n"
           "    for (int _k=0;_k<16;_k++) v[_k]=vec4(0);\n"
           "    v[0]=vec4(in_v0,1); v[3]=in_v3;\n");
    if (!fx.noseed)
        VP_PUT("    v[8]=vec4(in_v3.xy, 0.0, 1.0);\n");
    else
        VP_PUT("    if (vam.stride == 0u) { for (int _a=1;_a<16;_a++) v[_a] = vam.cur[_a]; }\n");
    VP_PUT("    if (vam.stride != 0u) {\n"
           "      uint _vb = vam.base + (uint(gl_VertexIndex) - vam.vid_base) * vam.stride;\n"
           "      for (int _a=0;_a<16;_a++)\n"
           "        v[_a] = (vam.slot[_a] >= 0) ? va[_vb + uint(vam.slot[_a])] : vam.cur[_a];\n"
           "    }\n"
           "    vec4 r[64]; vec4 o[32];\n"
           "    ivec4 a0 = ivec4(0,0,0,0); ivec4 a1 = ivec4(0,0,0,0);\n"
           "    for (int _i=0;_i<64;_i++) r[_i]=vec4(0);\n");
    VP_PUT(fx.w1 ? "    for (int _j=0;_j<32;_j++) o[_j]=float4(0,0,0,1);\n"
                 : "    for (int _j=0;_j<32;_j++) o[_j]=float4(0);\n");
    VP_PUT(body);
    if (!fx.nohack)
        VP_PUT("    if (all(equal(o[7], vec4(0.0)))) o[7] = v[8];\n");
    VP_PUT("    gl_Position = o[0]; v_col0 = o[1]; v_col1 = o[2];\n");
    VP_PUT(fx.fogx ? "    v_fog = o[5].xxxx;\n" : "    v_fog = o[5];\n");
    VP_PUT("    v_tc0=o[7]; v_tc1=o[8]; v_tc2=o[9]; v_tc3=o[10];\n"
           "    v_tc4=o[11]; v_tc5=o[12]; v_tc6=o[13]; v_tc7=o[14];\n"
           "}\n");
#undef VP_PUT
    return n;

overflow:
    out[0] = '\0';
    return -1;
}
```

- [ ] **Step 4: Run tests + Metal byte-identity**

```bash
cmake --build build-tests --target test_glsl_decompiler test_msl_decompiler
ctest --test-dir build-tests -R "glsl|msl" --output-on-failure
```
Expected: all PASS. Then repeat Task 3 Step 5's stash/diff procedure for `libs/video/rsx_vp_decompiler.c`, but with a VP MSL dump: build a throwaway `/tmp/vp_msl_dump.c` that calls `rsx_vp_decompile_msl` on `vp_mov_o0_v0` and on every `.vp` in `~/Library/Caches/rpcs3/cache/NPUA80491/*/shaders_cache/raw/`, printing the output; run before/after; `diff` must print nothing. The throwaway file is not committed.

- [ ] **Step 5: Commit**

```bash
git add libs/video/rsx_vp_decompiler.h libs/video/rsx_vp_decompiler.c libs/video/tests/test_glsl_decompiler.c
git commit -m "feat(rsx): emissor GLSL 450 do vertex program (VAMap/anel, invariant)"
```

---

### Task 5: gated ucode dump (`PS3_RSX_DUMP_UCODE`)

**Files:**
- Create: `libs/video/rsx_ucode_dump.h`, `libs/video/rsx_ucode_dump.c`
- Create: `libs/video/tests/test_ucode_dump.c`
- Modify: `libs/video/rsx_metal_backend.m` (`metal_set_shader`: one call after `fp_hash`/`vpwords` are known, right after line ~10066 `u32 vp_hash = ...`)
- Modify: `CMakeLists.txt` (tests)

**Interfaces:**
- Produces: `void rsx_ucode_dump(const u8* fp, u32 fp_bytes, u32 fp_hash, const u32* vp_words, u32 vp_nwords, u32 vp_hash);` — no-op unless `PS3_RSX_DUMP_UCODE=<dir>`; writes `<dir>/<fp_hash:08X>.fp` (raw guest bytes, RPCS3 raw format) and `<dir>/<vp_hash:08X>.vp` (little-endian words, RPCS3 raw format) once per hash (skips files that exist). Also `void rsx_ucode_dump_reset_for_test(void);` to re-read the env var in tests.

- [ ] **Step 1: Write the failing test**

`libs/video/tests/test_ucode_dump.c`:
```c
#define _XOPEN_SOURCE 700          /* mkdtemp/setenv under -std=c11 on glibc */
#include "rsx_ucode_dump.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int fails = 0;
static void check(const char* n, int c) { if (!c) { printf("FAIL %s\n", n); fails++; } else printf("[PASS] %s\n", n); }
static long fsize(const char* p) { struct stat st; return stat(p, &st) == 0 ? (long)st.st_size : -1; }

int main(void)
{
    const u8 fp[32] = { 1, 2, 3, 4 };
    const u32 vp[8] = { 0x11223344u, 0, 0, 1u };
    char dir[] = "/tmp/ucode_dump_test_XXXXXX";
    if (!mkdtemp(dir)) { printf("mkdtemp failed\n"); return 1; }
    char pf[256], pv[256];
    snprintf(pf, sizeof pf, "%s/0000ABCD.fp", dir);
    snprintf(pv, sizeof pv, "%s/00001234.vp", dir);

    /* dump_off_is_noop */
    unsetenv("PS3_RSX_DUMP_UCODE");
    rsx_ucode_dump_reset_for_test();
    rsx_ucode_dump(fp, sizeof fp, 0xABCDu, vp, 4, 0x1234u);
    check("dump_off_is_noop", fsize(pf) < 0 && fsize(pv) < 0);

    /* dump_writes_raw */
    setenv("PS3_RSX_DUMP_UCODE", dir, 1);
    rsx_ucode_dump_reset_for_test();
    rsx_ucode_dump(fp, sizeof fp, 0xABCDu, vp, 4, 0x1234u);
    check("dump_writes_fp", fsize(pf) == 32);
    check("dump_writes_vp", fsize(pv) == 16);
    {
        FILE* f = fopen(pv, "rb"); unsigned char b[4] = {0};
        if (f) { fread(b, 1, 4, f); fclose(f); }
        check("dump_vp_little_endian", b[0] == 0x44 && b[3] == 0x11);
    }

    /* dump_bad_dir: missing directory -> nothing, no crash */
    setenv("PS3_RSX_DUMP_UCODE", "/nonexistent/dir/for/ucode", 1);
    rsx_ucode_dump_reset_for_test();
    rsx_ucode_dump(fp, sizeof fp, 0x1u, vp, 4, 0x2u);
    check("dump_bad_dir", 1);

    remove(pf); remove(pv); rmdir(dir);
    printf(fails ? "%d FAILED\n" : "ALL PASS\n", fails);
    return fails ? 1 : 0;
}
```
CMake (outside the shaderc `if`):
```cmake
    add_executable(test_ucode_dump
        libs/video/tests/test_ucode_dump.c
        libs/video/rsx_ucode_dump.c)
    target_include_directories(test_ucode_dump PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/libs/video"
        "${CMAKE_CURRENT_SOURCE_DIR}/include")
    add_test(NAME test_ucode_dump COMMAND test_ucode_dump)
```

- [ ] **Step 2: Run to confirm it fails**

```bash
cmake -B build-tests -G Ninja -DPS3RECOMP_BUILD_TESTS=ON . && cmake --build build-tests --target test_ucode_dump
```
Expected: FAIL, `rsx_ucode_dump.h` not found.

- [ ] **Step 3: Implement**

`libs/video/rsx_ucode_dump.h`:
```c
/*
 * ps3recomp - PS3_RSX_DUMP_UCODE=<dir> (diagnostic, OFF by default)
 *
 * Writes each distinct fragment/vertex program the runtime binds as raw
 * microcode in RPCS3's shaders_cache/raw format: <fp_hash>.fp = the guest
 * bytes as the RSX reads them, <vp_hash>.vp = little-endian instruction
 * words. The files are game code: keep them out of git
 * (../gow2-recomp/shader_corpus/ is gitignored).
 */
#ifndef PS3RECOMP_RSX_UCODE_DUMP_H
#define PS3RECOMP_RSX_UCODE_DUMP_H

#include "ps3emu/ps3types.h"

#ifdef __cplusplus
extern "C" {
#endif

void rsx_ucode_dump(const u8* fp, u32 fp_bytes, u32 fp_hash,
                    const u32* vp_words, u32 vp_nwords, u32 vp_hash);
void rsx_ucode_dump_reset_for_test(void);

#ifdef __cplusplus
}
#endif
#endif /* PS3RECOMP_RSX_UCODE_DUMP_H */
```

`libs/video/rsx_ucode_dump.c`:
```c
#include "rsx_ucode_dump.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int  s_state = -1;          /* -1 unread, 0 off, 1 on */
static char s_dir[512];

void rsx_ucode_dump_reset_for_test(void) { s_state = -1; }

static int dump_on(void)
{
    if (s_state < 0) {
        const char* e = getenv("PS3_RSX_DUMP_UCODE");
        s_state = (e && *e && strlen(e) < sizeof(s_dir) - 32) ? 1 : 0;
        if (s_state) {
            snprintf(s_dir, sizeof s_dir, "%s", e);
            fprintf(stderr, "[RSX] PS3_RSX_DUMP_UCODE -> %s\n", s_dir);
        }
    }
    return s_state;
}

/* Write once per path; an existing file (or an unwritable dir) is left alone. */
static void write_once(const char* path, const void* data, size_t n)
{
    FILE* f = fopen(path, "rb");
    if (f) { fclose(f); return; }
    f = fopen(path, "wb");
    if (!f) return;
    fwrite(data, 1, n, f);
    fclose(f);
}

void rsx_ucode_dump(const u8* fp, u32 fp_bytes, u32 fp_hash,
                    const u32* vp_words, u32 vp_nwords, u32 vp_hash)
{
    if (!dump_on()) return;
    char path[600];
    if (fp && fp_bytes) {
        snprintf(path, sizeof path, "%s/%08X.fp", s_dir, fp_hash);
        write_once(path, fp, fp_bytes);
    }
    if (vp_words && vp_nwords) {
        u8 buf[512 * 16];
        u32 n = vp_nwords * 4u;
        if (n > sizeof buf) n = sizeof buf;
        for (u32 i = 0; i < n / 4u; i++) {
            u32 w = vp_words[i];
            buf[i * 4 + 0] = (u8)w;         buf[i * 4 + 1] = (u8)(w >> 8);
            buf[i * 4 + 2] = (u8)(w >> 16); buf[i * 4 + 3] = (u8)(w >> 24);
        }
        snprintf(path, sizeof path, "%s/%08X.vp", s_dir, vp_hash);
        write_once(path, buf, n);
    }
}
```

In `rsx_metal_backend.m`: add `#include "rsx_ucode_dump.h"` with the other `rsx_*.h` includes, and right after the line `u32 vp_hash = (vpw >= 4) ? metal_fnv1a((const u8*)vpwords, vpw * 4) : 0u;` (≈10066) add:
```objc
    /* PS3_RSX_DUMP_UCODE=<dir> (OFF): raw ucode corpus for the Vulkan GLSL
     * emitter (plan 2026-09-23-vulkan-v0). No-op when unset. */
    rsx_ucode_dump(ucode, prog_bytes, fp_hash, vpwords, vpw, vp_hash);
```

- [ ] **Step 4: Run tests; build the game binary**

```bash
cmake --build build-tests --target test_ucode_dump && ctest --test-dir build-tests -R test_ucode_dump --output-on-failure
cmake --build build-macos
```
Expected: `ALL PASS`; `build-macos` builds (the runtime lib picks up `rsx_ucode_dump.c` via the glob — re-run `cmake -B build-macos` first if it reports an undefined symbol). Then rebuild the game binary as `gow2-recomp` does (`../gow2-recomp/build_macos.sh`, after `cmake --build` per the memory note that `build_macos.sh` does not rebuild the runtime lib).

- [ ] **Step 5: Commit**

```bash
git add libs/video/rsx_ucode_dump.h libs/video/rsx_ucode_dump.c libs/video/tests/test_ucode_dump.c libs/video/rsx_metal_backend.m CMakeLists.txt
git commit -m "diag(rsx): PS3_RSX_DUMP_UCODE grava o microcodigo FP/VP no formato raw do RPCS3"
```

---

### Task 6: corpus validator (`validate_glsl_corpus`)

**Files:**
- Create: `libs/video/tests/validate_glsl_corpus.c`
- Modify: `CMakeLists.txt` (inside `if(PS3RECOMP_HAVE_SHADERC)`, as a plain executable — **not** an `add_test`, it needs game data)

**Interfaces:**
- Consumes: `rsx_fp_decompile_glsl`, `rsx_vp_decompile_glsl` (Tasks 3-4).
- Produces: CLI `validate_glsl_corpus <dir>...` — for every `*.fp` / `*.vp` in the given dirs, decompile + shaderc-compile, both for the default setters and for the Metal gameplay variant (`mrt=4`, const-buffer mode on, colour export from bit 0x40 both ways, depth export on/off). Prints one line per failure with file, variant and the first shaderc error line; writes the failing GLSL to `<file>.<variant>.glsl` next to a `--out <dir>` if given; exit code = number of failing files (0 = all pass). Summary line: `glsl-corpus: fp N/N vp M/M variants V/V`.

- [ ] **Step 1: Write the tool**

`libs/video/tests/validate_glsl_corpus.c`:
```c
/*
 * ps3recomp - V0 gate: every real FP/VP decompiles to GLSL 450 and compiles
 * to SPIR-V with shaderc. Inputs are RPCS3 shaders_cache/raw dirs and/or our
 * own PS3_RSX_DUMP_UCODE dirs (same format). Game data: never commit them.
 *
 *   validate_glsl_corpus [--out <dir>] <raw dir>...
 */
#include "rsx_fp_decompiler.h"
#include "rsx_vp_decompiler.h"
#include <shaderc/shaderc.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static shaderc_compiler_t s_cc;
static shaderc_compile_options_t s_op;
static const char* s_out_dir;
static char s_glsl[256 * 1024];

static int ends_with(const char* s, const char* suf)
{
    size_t a = strlen(s), b = strlen(suf);
    return a >= b && strcmp(s + a - b, suf) == 0;
}

static unsigned char* read_file(const char* path, long* n)
{
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); *n = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char* b = (unsigned char*)malloc((size_t)*n + 16);
    if (b && fread(b, 1, (size_t)*n, f) != (size_t)*n) { free(b); b = NULL; }
    fclose(f);
    return b;
}

static void save_glsl(const char* file, const char* variant)
{
    if (!s_out_dir) return;
    const char* base = strrchr(file, '/'); base = base ? base + 1 : file;
    char path[1024];
    snprintf(path, sizeof path, "%s/%s.%s.glsl", s_out_dir, base, variant);
    FILE* f = fopen(path, "w");
    if (f) { fputs(s_glsl, f); fclose(f); }
}

/* 1 = pass. On failure prints "<file> [variant] <reason>". */
static int compile_one(const char* file, const char* variant, int n, shaderc_shader_kind kind)
{
    if (n < 0) { printf("%s [%s] decompile/emit failed\n", file, variant); save_glsl(file, variant); return 0; }
    shaderc_compilation_result_t r =
        shaderc_compile_into_spv(s_cc, s_glsl, strlen(s_glsl), kind, file, "main", s_op);
    int ok = shaderc_result_get_compilation_status(r) == shaderc_compilation_status_success;
    if (!ok) {
        const char* msg = shaderc_result_get_error_message(r);
        const char* nl = strchr(msg, '\n');
        printf("%s [%s] %.*s\n", file, variant, nl ? (int)(nl - msg) : (int)strlen(msg), msg);
        save_glsl(file, variant);
    }
    shaderc_result_release(r);
    return ok;
}

static int run_fp(const char* path, const unsigned char* b, long n, int* vpass, int* vtotal)
{
    /* default + the Metal gameplay variants the Vulkan backend will request */
    static const struct { const char* name; int mrt, cbuf, cexp, depth; } V[] = {
        { "default", 1, 0, -1, 0 },
        { "mrt4-cbuf-r0", 4, 1, 1, 0 },
        { "mrt4-cbuf-h0", 4, 1, 0, 0 },
        { "mrt4-cbuf-r0-depth", 4, 1, 1, 1 },
    };
    int all = 1;
    for (size_t i = 0; i < sizeof V / sizeof V[0]; i++) {
        rsx_fp_set_mrt_count(V[i].mrt);
        rsx_fp_set_const_buffer_mode(V[i].cbuf);
        rsx_fp_set_color_export(V[i].cexp);
        rsx_fp_set_depth_export(V[i].depth);
        int k = rsx_fp_decompile_glsl(b, (u32)n, s_glsl, sizeof s_glsl);
        int ok = compile_one(path, V[i].name, k, shaderc_fragment_shader);
        *vpass += ok; (*vtotal)++; all &= ok;
    }
    rsx_fp_set_mrt_count(1); rsx_fp_set_const_buffer_mode(0);
    rsx_fp_set_color_export(-1); rsx_fp_set_depth_export(0);
    return all;
}

static int run_vp(const char* path, const unsigned char* b, long n, int* vpass, int* vtotal)
{
    static const struct { const char* name; int fix; } V[] = {
        { "env-default", -1 }, { "fix-all", 1 | 2 | 4 | 8 },
    };
    int all = 1;
    for (size_t i = 0; i < sizeof V / sizeof V[0]; i++) {
        rsx_vp_set_fix_override(V[i].fix);
        int k = rsx_vp_decompile_glsl(b, (u32)n, s_glsl, sizeof s_glsl);
        int ok = compile_one(path, V[i].name, k, shaderc_vertex_shader);
        *vpass += ok; (*vtotal)++; all &= ok;
    }
    rsx_vp_set_fix_override(-1);
    return all;
}

int main(int argc, char** argv)
{
    s_cc = shaderc_compiler_initialize();
    s_op = shaderc_compile_options_initialize();
    shaderc_compile_options_set_target_env(s_op, shaderc_target_env_vulkan,
                                           shaderc_env_version_vulkan_1_2);
    int fp_ok = 0, fp_n = 0, vp_ok = 0, vp_n = 0, v_ok = 0, v_n = 0;
    for (int a = 1; a < argc; a++) {
        if (!strcmp(argv[a], "--out") && a + 1 < argc) { s_out_dir = argv[++a]; continue; }
        DIR* d = opendir(argv[a]);
        if (!d) { printf("cannot open %s\n", argv[a]); continue; }
        struct dirent* e;
        while ((e = readdir(d)) != NULL) {
            int is_fp = ends_with(e->d_name, ".fp"), is_vp = ends_with(e->d_name, ".vp");
            if (!is_fp && !is_vp) continue;
            char path[1024];
            snprintf(path, sizeof path, "%s/%s", argv[a], e->d_name);
            long n = 0;
            unsigned char* b = read_file(path, &n);
            if (!b || n <= 0) { free(b); continue; }
            if (is_fp) { fp_n++; fp_ok += run_fp(path, b, n, &v_ok, &v_n); }
            else       { vp_n++; vp_ok += run_vp(path, b, n, &v_ok, &v_n); }
            free(b);
        }
        closedir(d);
    }
    printf("glsl-corpus: fp %d/%d vp %d/%d variants %d/%d\n", fp_ok, fp_n, vp_ok, vp_n, v_ok, v_n);
    shaderc_compile_options_release(s_op);
    shaderc_compiler_release(s_cc);
    return (fp_n - fp_ok) + (vp_n - vp_ok);
}
```
CMake (inside `if(PS3RECOMP_HAVE_SHADERC)`):
```cmake
        add_executable(validate_glsl_corpus
            libs/video/tests/validate_glsl_corpus.c
            libs/video/rsx_fp_decompiler.c
            libs/video/rsx_vp_decompiler.c
            libs/video/rsx_glsl_dialect.c)
        target_include_directories(validate_glsl_corpus PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/libs/video"
            "${CMAKE_CURRENT_SOURCE_DIR}/include")
        target_link_libraries(validate_glsl_corpus PRIVATE PkgConfig::SHADERC)
```

- [ ] **Step 2: Run on the RPCS3 corpus already on disk**

```bash
cmake --build build-tests --target validate_glsl_corpus
mkdir -p /tmp/glsl_fail
./build-tests/validate_glsl_corpus --out /tmp/glsl_fail \
  ~/Library/Caches/rpcs3/cache/NPUA80491/*/shaders_cache/raw
```
Expected: a summary line; the corpus is ~50 files (20 fp / 30 vp in `ppu-9vyS...-EBOOT.BIN`, plus the intro/skipintro dirs). Record the numbers. Failures are expected at first and are fixed in Task 7 — do not fix here.

- [ ] **Step 3: Commit**

```bash
git add libs/video/tests/validate_glsl_corpus.c CMakeLists.txt
git commit -m "test(rsx): validate_glsl_corpus compila cada FP/VP real para SPIR-V com o shaderc"
```

---

### Task 7: gameplay corpus + drive the corpus to 100 %

**Files:**
- Modify: `../gow2-recomp/.gitignore` (separate repo — add `/shader_corpus/`)
- Modify (as failures demand): `libs/video/rsx_glsl_dialect.c`, `libs/video/tests/test_glsl_dialect.c`, `libs/video/rsx_fp_decompiler.c`, `libs/video/rsx_vp_decompiler.c`
- Modify: `.superpowers/sdd/progress.md` (ledger line with the final numbers)

**Interfaces:**
- Consumes: everything above. Produces the V0 acceptance evidence.

- [ ] **Step 1: Ignore the corpus dir in gow2-recomp and commit that there**

```bash
cd ../gow2-recomp
printf '\n# microcodigo FP/VP dumpado (PS3_RSX_DUMP_UCODE): codigo do jogo, nunca versionar\n/shader_corpus/\n' >> .gitignore
mkdir -p shader_corpus
git check-ignore -v shader_corpus/x.fp
git add .gitignore && git commit -m "chore: ignora shader_corpus (dump de microcodigo do jogo)"
cd ../ps3recomp
```
Expected: `check-ignore` prints the `.gitignore` rule. (The `.gitignore` of gow2-recomp may already carry an uncommitted `/traducao_ptbr/` line from 2026-09-23 — include it in this commit only if the user agrees; otherwise stage with `git add -p` and pick the corpus hunk.)

- [ ] **Step 2: Capture the gameplay corpus**

Ask the user (they play; do not open a test window while they are playing — memory `nao-abrir-instancias-de-teste-com-usuario-jogando`) to run one session to past the Colossus with:
```bash
cd ../gow2-recomp && PS3_RSX_DUMP_UCODE="$PWD/shader_corpus" ./jogar_g2.sh
```
If the user prefers the agent to capture, use `claude_runs/run_until_shots.sh` with the same env var (reaches gameplay shots). Afterwards:
```bash
ls ../gow2-recomp/shader_corpus | wc -l; ls ../gow2-recomp/shader_corpus/*.fp | wc -l; ls ../gow2-recomp/shader_corpus/*.vp | wc -l
```
Record the counts (expected: hundreds of FPs/VPs vs ~50 in the RPCS3 cache).

- [ ] **Step 3: Run the validator on both corpora**

```bash
rm -rf /tmp/glsl_fail && mkdir -p /tmp/glsl_fail
./build-tests/validate_glsl_corpus --out /tmp/glsl_fail \
  ../gow2-recomp/shader_corpus ~/Library/Caches/rpcs3/cache/NPUA80491/*/shaders_cache/raw \
  | tee /tmp/glsl_corpus_run1.txt | tail -1
```
Expected: summary line. Group the failure lines by first error message:
```bash
grep -v '^glsl-corpus' /tmp/glsl_corpus_run1.txt | sed 's/^[^]]*] //' | sort | uniq -c | sort -rn | head -20
```

- [ ] **Step 4: Fix loop — one error class at a time, test first**

For the most frequent error class:
1. Open one failing `/tmp/glsl_fail/<file>.<variant>.glsl`, find the offending construct.
2. Reduce it to the smallest HLSL-dialect snippet the decompiler emits for it and add an `expect_rw(...)` case to `test_glsl_dialect.c` with the GLSL you want (or, if the construct needs a helper, a prelude assertion). Run `ctest -R test_glsl_dialect` → FAIL.
3. Fix `rsx_glsl_rewrite_body` or `RSX_GLSL_PRELUDE` (never the NV40 decoding — if the HLSL itself is wrong for GLSL semantics, change the emitter only in the GLSL wrapper, keeping D3D12/Metal output identical; re-run the byte-identity check of Task 3 Step 5 / Task 4 Step 4 if you touched a decompiler file).
4. `ctest -R "glsl|msl"` → PASS; re-run Step 3; the class count must drop to 0.
5. Commit: `git add <files> && git commit -m "fix(rsx): GLSL — <classe do erro>"`.

Repeat until the summary shows `fp N/N vp M/M` with no failures. Constructs that cannot be expressed without changing semantics are **not** stubbed: stop and report them to the user (CLAUDE.md rule 5).

- [ ] **Step 5: Record the acceptance evidence**

Append to `.superpowers/sdd/progress.md`:
```markdown
## Vulkan V0 (2026-09-xx) — GLSL emitters
- Corpus: gow2 shader_corpus <X> fp / <Y> vp (PS3_RSX_DUMP_UCODE, session to past the Colossus) + RPCS3 raw <Z> files.
- validate_glsl_corpus: fp X/X vp Y/Y variants V/V (0 failures). Output: /tmp/glsl_corpus_final.txt
- Metal MSL byte-identical before/after (Tasks 3-4).
- Error classes fixed: <list with commit hashes>.
- Proven OFFLINE only (shaderc compile); nothing exercised in-boot on Vulkan yet (that is V2).
```
```bash
git add .superpowers/sdd/progress.md
git commit -m "docs(sdd): V0 do Vulkan — corpus GLSL 100% compilando (offline)"
```

## Execution record (V0 acceptance)

Date: 2026-09-23.

- **Corpus:** gow2 capture played through the Colossus with `PS3_RSX_DUMP_UCODE`
  (`gow2-recomp/shader_corpus`, git-ignored): 23 fp / 208 vp. RPCS3 raw cache
  (NPUA80491 `shaders_cache/raw`): 50 fp / 74 vp.
- **Validator (`build-tests/validate_glsl_corpus`):**
  - gow2 only: `glsl-corpus: fp 23/23 vp 208/208 variants 508/508`, exit 0.
  - gow2 + RPCS3: `glsl-corpus: fp 73/73 vp 282/282 variants 856/856`, exit 0.
- **Error classes fixed in the Step 4 loop:** none. Every shader passed on the first run.
- **Defects found during V0, fixed and committed:**
  - Task 3: depth-export FP path (`gl_FragDepth`) was not covered by a test. Test added in `f174bad3`.
  - Task 4: Windows build broke on `memmem`. Fixed by moving `ddx`/`ddy` into a fragment-only prelude in `14fd3812`.
  - Task 6: the validator's exit code was truncated and an empty or missing dir counted as a pass. Fixed in `90bf34a0`.
  - Task 5: the `PS3_RSX_DUMP_UCODE` probe did an `fopen` on every shader bind, which caused an in-game hitch. `58deab2b` replaced it with an in-memory per-hash set. In-game sample: main-thread samples in `rsx_ucode_dump` dropped from 642/1232 to 4/1323, and `__open_nocancel` dropped from 573 to 0.
- **Open watch item:** the FP emitter declares `r[48]`/`h[48]`, but the 6-bit register index can reach 63. No captured shader hit this.
- Proven OFFLINE only (shaderc compile to SPIR-V); nothing exercised in-boot on Vulkan yet — that is V2.

### Final review (2026-09-23) and items carried to the V2 plan

- Final whole-branch review: "With fixes" -> fixed in 8f87a5a4 (compare operands now go through the rewriter; opcode-sweep ctest: FP 0/576 fail with 3 documented skips = RFL+CONST pre-existing HLSL truncation, VP 0/384), 6b2cbc52 (validator counts unreadable/empty files as failures; VP default variant pinned to override 4), abc3b8b1 (dump header: Metal only). Re-review: all addressed. Metal MSL byte-identity independently re-proven by the reviewer (2716 program-variants).
- **Carry to V2 (must be settled before pipeline layouts / the shader cache):**
  1. Contract drift plan vs spec §3.2: spec says "always 4 colour outputs" and "FP consts + alpha test + VP consts in one per-slot buffer, dynamic offsets"; V0 emits `mrt` outputs and separate bindings 1/2/3. Pick one and update spec or emitters.
  2. TXL/TXB: GLSL maps to textureLod/bias correctly, but MSL leaves a 3-arg `.sample(...)` that Metal likely rejects — expect Vulkan to draw where Metal falls back in shot A/B.
  3. shaderc is found only via pkg-config; add a Vulkan SDK / vcpkg fallback before Windows or Linux-SDK builds.
  4. The emitters use global setters (mrt, const-buffer mode, colour/depth export, VP fix override) and ~670 KiB of static buffers; wrap them in an options struct before any background/parallel shader compile.
  5. `rsx_fp_glsl_from_hlsl` reads `s_fp_wrote_r1` from the last `rsx_fp_decompile` call — pass it explicitly.
  6. Pre-existing emitter bugs shared with MSL (separate ticket, change MSL output): RFL with a CONST source truncates the HLSL rhs buffer; FP declares r[48]/h[48] while the register index is 6-bit (0..63).
- Side note: an in-game report of "gray intro (~4 frames) + no audio" with the vkv0 build did NOT reproduce headless (vkv0 and HEAD-without-V0 both: SmLogo frames=330 completed=1, audio present, 120 s runs); the untested condition was Metal + PS3_RSX_DUMP_UCODE on, where the 12:20 build had the per-call fopen stall fixed in 58deab2b.
