/*
 * ps3recomp - RSX live-draw geometry correctness tests (P1 fixes, Area 2)
 *
 * Covers three P1 RSX rendering bugs found by cross-referencing RPCS3 (see
 * gow2-recomp/notes/2026-07-21-rpcs3-improvement-backlog.md, Area 2):
 *
 *   1. fetch_attr() vertex component decode (rsx_live_draw.c:817-839) --
 *      only RSX_VTX_TYPE_FLOAT and RSX_VTX_TYPE_UNORM8 decoded correctly;
 *      SNORM16/HALF/SINT16/CMP32/UINT8 fell into an `else` that reinterpreted
 *      raw bytes as a BE float, at the wrong stride. Fixed by two new
 *      portable helpers in rsx_vertex_formats.h -- rsx_vertex_decode_component()
 *      (per-component decode, sized via the already-correct
 *      rsx_vertex_type_size()) and rsx_vertex_decode_cmp32() (the packed
 *      11-11-10 attribute-wide dword) -- which fetch_attr() now calls.
 *
 *   2. sink_end() TRIANGLE_STRIP winding (rsx_live_draw.c:900-916) -- emitted
 *      (v[i],v[i+1],v[i+2]) for every i with no alternation, so every odd
 *      triangle in a strip has inverted winding vs. native GPU strip
 *      semantics. Fix: alternate to (v[i+1],v[i],v[i+2]) on odd i.
 *
 *   3. d3d12_draw_arrays()/d3d12_draw_indexed() (rsx_d3d12_backend.c:~1774-1826)
 *      -- rsx_to_d3d12_topology() maps QUADS/TRIANGLE_FAN to
 *      D3D_TOPOLOGY_TRIANGLELIST (not UNDEFINED), so the "skip primitives
 *      that need index conversion" guard never fires for them, and the raw
 *      vertex/index range is submitted as if already triangulated. Fix:
 *      wire in the existing, already-correct rsx_prim_needs_conversion() +
 *      rsx_convert_quads()/rsx_convert_triangle_fan() (rsx_primitives.h) to
 *      build a real index buffer before upload.
 *
 * Platform note: rsx_live_draw.c is `#if !defined(_WIN32) ... #else ...`
 * and rsx_d3d12_backend.c is `#ifdef _WIN32` -- both pull in <windows.h> and
 * <d3d12.h> and cannot be built on this host (macOS, no Windows SDK/mingw).
 * This matches the project's existing split of a Windows D3D12 harness
 * (replay_main.c / build_replay.cmd) vs. a macOS/Metal harness
 * (replay_mac.m / build_replay_mac.sh) -- there is no cross-platform build
 * of the D3D12 backend itself anywhere in this tree.
 *
 * So this test suite:
 *   (a) links and calls the REAL, portable, shared helpers those two files
 *       now call -- rsx_vertex_decode_component/rsx_vertex_decode_cmp32
 *       (rsx_vertex_formats.h) and rsx_prim_needs_conversion/
 *       rsx_convert_quads/rsx_convert_triangle_fan (rsx_primitives.h, both
 *       pre-existing and already correct -- zero callers was the bug);
 *   (b) mirrors the small amount of platform-specific wiring glue that can
 *       only physically live inside the _WIN32-gated files (triangle-strip
 *       winding selection in sink_end(); index-position -> index-value
 *       remap in d3d12_draw_indexed()) with a test-local copy kept
 *       identical in structure to what was hand-applied there, so the
 *       algorithm is genuinely red/green tested before being transcribed.
 *
 * Build:
 *   clang -std=c11 -O0 -Wall -I../../../include -I.. test_rsx_geom.c -o test_rsx_geom
 */

#include "rsx_vertex_formats.h"
#include "rsx_primitives.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (cond) { printf("[PASS] %s\n", msg); } \
    else { printf("[FAIL] %s\n", msg); failures++; } } while (0)

/* ---------------------------------------------------------------------------
 * Guest-style big-endian byte buffer builders (test input only)
 * -----------------------------------------------------------------------*/
static void put_be16(u8* p, u16 v) { p[0] = (u8)(v >> 8); p[1] = (u8)v; }
static void put_be32(u8* p, u32 v) { p[0] = (u8)(v>>24); p[1] = (u8)(v>>16); p[2] = (u8)(v>>8); p[3] = (u8)v; }
static void put_be_f32(u8* p, float f) { u32 v; memcpy(&v, &f, 4); put_be32(p, v); }

/* Minimal IEEE754 half encoder (host -> half), used only to build test
 * input for the HALF decode path -- independent of the decoder under test
 * (no subnormal/inf handling needed for these test values). */
static u16 f32_to_half_for_test(float f)
{
    u32 x; memcpy(&x, &f, 4);
    u32 sign = (x >> 16) & 0x8000u;
    s32 exp  = (s32)((x >> 23) & 0xFF) - 127 + 15;
    u32 mant = x & 0x7FFFFFu;
    if (exp <= 0)   return (u16)sign;
    if (exp >= 0x1F) return (u16)(sign | 0x7C00u);
    return (u16)(sign | ((u32)exp << 10) | (mant >> 13));
}

/* ---------------------------------------------------------------------------
 * Bug 1: fetch_attr() vertex component decode
 * -----------------------------------------------------------------------*/
static void test_decode_float(void)
{
    u8 buf[4]; put_be_f32(buf, -2.5f);
    float v = rsx_vertex_decode_component(RSX_VERTEX_TYPE_F, buf);
    CHECK(fabsf(v - (-2.5f)) < 1e-6f, "FLOAT: decodes BE f32 exactly (sanity -- already worked)");
}

static void test_decode_unorm8(void)
{
    u8 buf[1] = { 128 };
    float v = rsx_vertex_decode_component(RSX_VERTEX_TYPE_UB, buf);
    CHECK(fabsf(v - (128 / 255.0f)) < 1e-6f, "UNORM8: byte/255 (sanity -- already worked)");
}

static void test_decode_uint8_not_normalized(void)
{
    u8 buf[1] = { 200 };
    float v = rsx_vertex_decode_component(RSX_VERTEX_TYPE_UB256, buf);
    CHECK(v == 200.0f, "UINT8: raw byte value, NOT divided by 255 (was misread as BE float)");
}

static void test_decode_snorm16(void)
{
    u8 buf[2]; put_be16(buf, (u16)(s16)(-16384));
    float v = rsx_vertex_decode_component(RSX_VERTEX_TYPE_S1, buf);
    CHECK(fabsf(v - (-16384 / 32767.0f)) < 1e-5f, "SNORM16: signed BE u16 / 32767 (was misread as BE float)");

    u8 bmax[2]; put_be16(bmax, 32767);
    CHECK(fabsf(rsx_vertex_decode_component(RSX_VERTEX_TYPE_S1, bmax) - 1.0f) < 1e-5f,
          "SNORM16: max positive raw value -> +1.0");
}

static void test_decode_sint16(void)
{
    u8 buf[2]; put_be16(buf, (u16)(s16)(-1234));
    float v = rsx_vertex_decode_component(RSX_VERTEX_TYPE_S32K, buf);
    CHECK(v == -1234.0f, "SINT16: raw signed int16, NOT normalized (was misread as BE float)");
}

static void test_decode_half(void)
{
    u8 buf[2]; put_be16(buf, f32_to_half_for_test(1.5f));
    float v = rsx_vertex_decode_component(RSX_VERTEX_TYPE_SF, buf);
    CHECK(fabsf(v - 1.5f) < 1e-3f, "HALF: unpacks BE float16 (was misread as BE float32)");

    u8 negbuf[2]; put_be16(negbuf, f32_to_half_for_test(-3.25f));
    CHECK(fabsf(rsx_vertex_decode_component(RSX_VERTEX_TYPE_SF, negbuf) - (-3.25f)) < 1e-3f,
          "HALF: negative value round-trips");
}

static void test_decode_cmp32(void)
{
    /* Packed dword, D3D9 DEC3N-compatible layout used across the NV40-era
     * GPU family RSX derives from: bits [0:10]=X (11b signed), [11:21]=Y
     * (11b signed), [22:31]=Z (10b signed), each normalized by its max
     * positive value. Build X=+1023 (-> +1.0), Y=-1024 (11-bit min -> most
     * negative), Z=0. */
    u32 ux = 1023u & 0x7FFu;
    u32 uy = (u32)(-1024) & 0x7FFu;
    u32 uz = 0u & 0x3FFu;
    u32 w = (uz << 22) | (uy << 11) | ux;
    u8 buf[4]; put_be32(buf, w);

    float xyz[3];
    rsx_vertex_decode_cmp32(buf, xyz);
    CHECK(fabsf(xyz[0] - 1.0f) < 1e-5f, "CMP32: X = max 11-bit -> +1.0");
    CHECK(xyz[1] < -0.99f && xyz[1] >= -1.02f, "CMP32: Y = min 11-bit -> ~-1.0");
    CHECK(fabsf(xyz[2] - 0.0f) < 1e-5f, "CMP32: Z = 0 -> 0.0");
}

static void test_stride_fallback_uses_attrib_size(void)
{
    /* The old fetch_attr() stride fallback was `a.stride ? a.stride :
     * a.size*4` -- always assumed 4 bytes/component. rsx_vertex_attrib_size()
     * (pre-existing, already correct) is what the fixed fetch_attr() must
     * use instead. Sanity-check it here against the values the fix relies
     * on, since a wrong stride silently corrupts every vertex after #0. */
    CHECK(rsx_vertex_attrib_size(RSX_VERTEX_TYPE_UB256, 4) == 4,
          "stride fallback: UINT8 x4 comps = 4 bytes/vertex (old code assumed 16)");
    CHECK(rsx_vertex_attrib_size(RSX_VERTEX_TYPE_S1, 3) == 6,
          "stride fallback: SNORM16 x3 comps = 6 bytes/vertex (old code assumed 12)");
    CHECK(rsx_vertex_attrib_size(RSX_VERTEX_TYPE_CMP, 3) == 4,
          "stride fallback: CMP32 always 4 bytes/vertex regardless of size (old code assumed 12)");
}

/* ---------------------------------------------------------------------------
 * Bug 2: sink_end() TRIANGLE_STRIP winding
 *
 * The production function lives inside rsx_live_draw.c's _WIN32-only TU
 * (see file header). This mirrors it index-for-index: same condition
 * (i & 1), same swap. Kept identical to the hand-applied fix.
 * -----------------------------------------------------------------------*/
static u32 strip_to_triangles_mirror(const u32* verts, u32 n_verts, u32* out /* (n_verts-2)*3 */)
{
    u32 n_out = 0;
    for (u32 i = 0; i + 2 < n_verts; i++) {
        if (i & 1) {
            out[i*3+0] = verts[i+1]; out[i*3+1] = verts[i]; out[i*3+2] = verts[i+2];
        } else {
            out[i*3+0] = verts[i];   out[i*3+1] = verts[i+1]; out[i*3+2] = verts[i+2];
        }
        n_out += 3;
    }
    return n_out;
}

static void test_strip_winding_alternates(void)
{
    u32 verts[5] = { 10, 11, 12, 13, 14 };
    u32 tri[9];
    u32 n = strip_to_triangles_mirror(verts, 5, tri);
    CHECK(n == 9, "strip of 5 verts -> 3 triangles (9 indices)");
    CHECK(tri[0]==10 && tri[1]==11 && tri[2]==12, "triangle 0 (even i): (v0,v1,v2) unchanged");
    CHECK(tri[3]==12 && tri[4]==11 && tri[5]==13, "triangle 1 (odd i): (v[i+1],v[i],v[i+2]) = (v2,v1,v3)");
    CHECK(tri[6]==12 && tri[7]==13 && tri[8]==14, "triangle 2 (even i): (v2,v3,v4) unchanged");
}

/* ---------------------------------------------------------------------------
 * Bug 3: QUADS / TRIANGLE_FAN index generation for the D3D12 backend
 * -----------------------------------------------------------------------*/
static void test_quads_fan_map_to_trianglelist_not_undefined(void)
{
    /* This is *why* the old `if (topo == D3D_TOPOLOGY_UNDEFINED) skip;`
     * guard never caught these two primitives. */
    CHECK(rsx_prim_needs_conversion(RSX_PRIMITIVE_QUADS) != 0, "QUADS needs index conversion");
    CHECK(rsx_prim_needs_conversion(RSX_PRIMITIVE_TRIANGLE_FAN) != 0, "TRIANGLE_FAN needs index conversion");
    CHECK(rsx_to_d3d12_topology(RSX_PRIMITIVE_QUADS) == D3D_TOPOLOGY_TRIANGLELIST,
          "QUADS maps to TRIANGLELIST, NOT UNDEFINED -- the skip guard was dead for it");
    CHECK(rsx_to_d3d12_topology(RSX_PRIMITIVE_TRIANGLE_FAN) == D3D_TOPOLOGY_TRIANGLELIST,
          "TRIANGLE_FAN maps to TRIANGLELIST, NOT UNDEFINED -- ditto");
}

static void test_convert_quads_arrays_wiring(void)
{
    /* draw_arrays wiring: vertices [first,first+count) get uploaded to a
     * fresh VB slice starting at local index 0 (BaseVertexLocation anchors
     * the slice), so the fixed backend calls the helper with first=0 to get
     * indices local to that slice. */
    u32 idx[6];
    u32 n = rsx_convert_quads(0, 4, idx, 6);
    CHECK(n == 6, "one quad (4 verts) -> 6 indices (2 triangles)");
    u32 expect[6] = {0,1,2, 0,2,3};
    CHECK(memcmp(idx, expect, sizeof(expect)) == 0,
          "quad triangulation (0,1,2)+(0,2,3) matches rsx_live_draw.c's inline QUADS expansion winding");
}

static void test_convert_triangle_fan_arrays_wiring(void)
{
    u32 idx[9];
    u32 n = rsx_convert_triangle_fan(0, 5, idx, 9);
    CHECK(n == 9, "5-vertex fan -> 3 triangles (9 indices)");
    u32 expect[9] = {0,1,2, 0,2,3, 0,3,4};
    CHECK(memcmp(idx, expect, sizeof(expect)) == 0,
          "fan triangulation: every triangle keeps vertex 0, matches rsx_live_draw.c's inline FAN expansion");
}

/* draw_indexed wiring: the guest already supplied its own per-vertex index
 * array for the quad/fan sequence. We must re-triangulate SEQUENCE
 * POSITIONS (via the same helpers, first=0) and then remap each generated
 * position through the guest's index values -- position i in the sequence
 * names guest vertex src_indices[i], not vertex i itself. */
static u32 expand_indexed_prim_mirror(u32 rsx_prim, const u32* src_indices, u32 count,
                                       u32* out_indices, u32 max_indices)
{
    u32 pos[64];
    u32 n = 0;
    if (rsx_prim == RSX_PRIMITIVE_TRIANGLE_FAN)
        n = rsx_convert_triangle_fan(0, count, pos, 64);
    else if (rsx_prim == RSX_PRIMITIVE_QUADS)
        n = rsx_convert_quads(0, count, pos, 64);
    if (!n || n > max_indices) return 0;
    for (u32 j = 0; j < n; j++) out_indices[j] = src_indices[pos[j]];
    return n;
}

static void test_convert_quads_indexed_wiring(void)
{
    /* Guest supplies 4 arbitrary (non-sequential) vertex indices for one quad. */
    u32 guest_idx[4] = { 40, 41, 42, 43 };
    u32 out[6];
    u32 n = expand_indexed_prim_mirror(RSX_PRIMITIVE_QUADS, guest_idx, 4, out, 6);
    CHECK(n == 6, "indexed quad: 4 guest indices -> 6 triangle-list indices");
    u32 expect[6] = {40,41,42, 40,42,43};
    CHECK(memcmp(out, expect, sizeof(expect)) == 0,
          "indexed quad: triangle-list indices reference the GUEST's index VALUES, not raw sequence positions");
}

static void test_convert_fan_indexed_wiring(void)
{
    u32 guest_idx[5] = { 7, 3, 9, 1, 5 };
    u32 out[9];
    u32 n = expand_indexed_prim_mirror(RSX_PRIMITIVE_TRIANGLE_FAN, guest_idx, 5, out, 9);
    CHECK(n == 9, "indexed fan: 5 guest indices -> 9 triangle-list indices");
    u32 expect[9] = {7,3,9, 7,9,1, 7,1,5};
    CHECK(memcmp(out, expect, sizeof(expect)) == 0,
          "indexed fan: every triangle keeps the fan's guest-index center (7)");
}

int main(void)
{
    printf("=== rsx_live_draw / rsx_d3d12_backend geometry fixes (P1 Area 2) ===\n\n");

    printf("--- Bug 1: fetch_attr() vertex component decode ---\n");
    test_decode_float();
    test_decode_unorm8();
    test_decode_uint8_not_normalized();
    test_decode_snorm16();
    test_decode_sint16();
    test_decode_half();
    test_decode_cmp32();
    test_stride_fallback_uses_attrib_size();

    printf("\n--- Bug 2: sink_end() TRIANGLE_STRIP winding ---\n");
    test_strip_winding_alternates();

    printf("\n--- Bug 3: QUADS/TRIANGLE_FAN index conversion (D3D12 backend) ---\n");
    test_quads_fan_map_to_trianglelist_not_undefined();
    test_convert_quads_arrays_wiring();
    test_convert_triangle_fan_arrays_wiring();
    test_convert_quads_indexed_wiring();
    test_convert_fan_indexed_wiring();

    printf("\n===========================================\n");
    printf("Results: %s (%d failure(s))\n", failures ? "FAIL" : "OK", failures);
    return failures ? 1 : 0;
}
