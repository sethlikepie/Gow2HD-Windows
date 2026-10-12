/*
 * Unit test for rsx_texture_deswizzle.h
 *
 * Strategy:
 *   1. Build a known linear RGBA (4 bpp) pattern.
 *   2. Swizzle it into a Morton-ordered buffer using rsx_morton_offset
 *      (the forward permutation).
 *   3. Deswizzle it back with rsx_deswizzle_texels.
 *   4. memcmp against the original linear pattern -> must be identical.
 *
 * Also verifies rsx_morton_offset is a bijection over the padded index
 * space (no collisions, in range) for several pow2 and non-pow2 sizes.
 * That catches addressing bugs independently of the round-trip.
 *
 * Build: gcc -O0 -I libs/video -I include libs/video/tests/test_texture_deswizzle.c -o test_deswz.exe
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "rsx_texture_deswizzle.h"
#include "rsx_texture_formats.h"

/* Fix 1c: unit asserts for the swizzle detector + base-format extraction,
 * with format words assembled by hand. Proves the detector reads the LN flag
 * in the FORMAT byte (bits 8-15), independent of any texture flowing in-boot.
 * Swizzled == LN clear; LN=0x20 lives in the format byte. */
static int test_format_detector(void)
{
    int ok = 1;
#define CHECK(cond) do { if (!(cond)) { printf("  [FAIL] detector: %s\n", #cond); ok = 0; } } while (0)

    /* A8R8G8B8 (0x85): no LN -> swizzled; LN set -> linear. */
    CHECK(rsx_texture_is_swizzled((u32)RSX_TEXTURE_A8R8G8B8 << 8) == 1);
    CHECK(rsx_texture_is_swizzled(((u32)RSX_TEXTURE_A8R8G8B8 | RSX_TEXTURE_LN) << 8) == 0);

    /* BC/DXT1 (0x86): same LN semantics. */
    CHECK(rsx_texture_is_swizzled((u32)RSX_TEXTURE_COMPRESSED_DXT1 << 8) == 1);
    CHECK(rsx_texture_is_swizzled(((u32)RSX_TEXTURE_COMPRESSED_DXT1 | RSX_TEXTURE_LN) << 8) == 0);

    /* 16bpp R5G6B5 (0x84). */
    CHECK(rsx_texture_is_swizzled((u32)RSX_TEXTURE_R5G6B5 << 8) == 1);
    CHECK(rsx_texture_is_swizzled(((u32)RSX_TEXTURE_R5G6B5 | RSX_TEXTURE_LN) << 8) == 0);

    /* The old-bug bit: dimension 2D (0x20 in the LOW byte) must NOT flip the
     * detector — a swizzled A8R8G8B8 stays swizzled regardless of dimension. */
    CHECK(rsx_texture_is_swizzled(((u32)RSX_TEXTURE_A8R8G8B8 << 8) | 0x20u) == 1);

    /* Base-format extraction strips LN/UN so bpp/DXGI lookup still hits. */
    CHECK(rsx_texture_get_format((u32)RSX_TEXTURE_A8R8G8B8 << 8) == RSX_TEXTURE_A8R8G8B8);
    CHECK(rsx_texture_get_format(((u32)RSX_TEXTURE_A8R8G8B8 | RSX_TEXTURE_LN) << 8) == RSX_TEXTURE_A8R8G8B8);
    CHECK(rsx_texture_get_format(((u32)RSX_TEXTURE_A8R8G8B8 | RSX_TEXTURE_LN | RSX_TEXTURE_UN) << 8) == RSX_TEXTURE_A8R8G8B8);
    /* Linear texture must still map to DXGI (regression the bug would cause). */
    CHECK(rsx_to_dxgi_texture_format(rsx_texture_get_format(((u32)RSX_TEXTURE_A8R8G8B8 | RSX_TEXTURE_LN) << 8)) != 0);

    /* Bind/upload sizing — same helpers Metal bind_texture uses. */
    CHECK(rsx_texture_is_bc(RSX_TEXTURE_COMPRESSED_DXT1) == 1);
    CHECK(rsx_texture_is_bc(RSX_TEXTURE_A8R8G8B8) == 0);
    CHECK(rsx_texture_bpp(RSX_TEXTURE_COMPRESSED_DXT1) == 4);
    CHECK(rsx_to_dxgi_texture_format(RSX_TEXTURE_COMPRESSED_DXT1) == DXGI_FORMAT_BC1_UNORM);
    /* 8x8 DXT1: 2x2 blocks * 8 bytes = 32. */
    CHECK(rsx_texture_row_pitch(RSX_TEXTURE_COMPRESSED_DXT1, 8) == 16);
    CHECK(rsx_texture_src_rows(RSX_TEXTURE_COMPRESSED_DXT1, 8) == 2);
    CHECK(rsx_texture_row_pitch(RSX_TEXTURE_COMPRESSED_DXT1, 8) *
          rsx_texture_src_rows(RSX_TEXTURE_COMPRESSED_DXT1, 8) == 32);
    /* 128x128 RGBA8 linear pitch. */
    CHECK(rsx_texture_row_pitch(RSX_TEXTURE_A8R8G8B8, 128) == 512);
    CHECK(rsx_texture_src_rows(RSX_TEXTURE_A8R8G8B8, 128) == 128);

#undef CHECK
    printf("  format detector asserts: %s\n", ok ? "ok" : "BROKEN");
    return ok;
}

/* Golden vector: hard-coded Morton offsets independently derived by hand from
 * the documented NV40 rule (X-first bit interleave; leftover high bits of the
 * larger axis appended linearly on top). This is NOT computed from
 * rsx_morton_offset, so it catches a wrong permutation that the round-trip
 * (which uses the same function forward and inverse) cannot.
 *
 * 4x4 (log2w=log2h=2): offset = x0 | (y0<<1) | (x1<<2) | (y1<<3).
 * Row-major by y, 0..3 each:
 *   y=0: 0  1  4  5
 *   y=1: 2  3  6  7
 *   y=2: 8  9 12 13
 *   y=3:10 11 14 15   (a permutation of 0..15). */
static int test_golden_square(void)
{
    static const uint32_t expect4[4][4] = {
        {  0,  1,  4,  5 },
        {  2,  3,  6,  7 },
        {  8,  9, 12, 13 },
        { 10, 11, 14, 15 },
    };
    int ok = 1;
    for (uint32_t y = 0; y < 4; y++) {
        for (uint32_t x = 0; x < 4; x++) {
            uint32_t got = rsx_morton_offset(x, y, 2, 2);
            if (got != expect4[y][x]) {
                printf("  [FAIL] golden 4x4 (%u,%u): got %u want %u\n", x, y, got, expect4[y][x]);
                ok = 0;
            }
        }
    }
    printf("  golden 4x4: %s\n", ok ? "ok" : "BROKEN");
    return ok;
}

/* Non-square golden: 8x4 (log2w=3, log2h=2).
 * offset = x0 | (y0<<1) | (x1<<2) | (y1<<3) | (x2<<4).
 * The larger axis (X) keeps its high bit (x2) linear at bit 4 (weight 16),
 * above all the interleaved low bits — the "leftover high bits appended
 * linearly" rule. Points hand-derived and verified: */
static int test_golden_nonsquare(void)
{
    struct { uint32_t x, y, off; } g[] = {
        { 0, 0,  0 }, { 1, 0,  1 }, { 2, 0,  4 }, { 3, 0,  5 },
        { 4, 0, 16 }, { 5, 0, 17 }, { 6, 0, 20 }, { 7, 0, 21 },
        { 0, 1,  2 }, { 0, 2,  8 }, { 0, 3, 10 },
        { 4, 3, 26 }, { 7, 3, 31 },
    };
    int ok = 1;
    for (size_t i = 0; i < sizeof(g) / sizeof(g[0]); i++) {
        uint32_t got = rsx_morton_offset(g[i].x, g[i].y, 3, 2);
        if (got != g[i].off) {
            printf("  [FAIL] golden 8x4 (%u,%u): got %u want %u\n", g[i].x, g[i].y, got, g[i].off);
            ok = 0;
        }
    }
    printf("  golden 8x4 (non-square): %s\n", ok ? "ok" : "BROKEN");
    return ok;
}

static int test_bijection(uint32_t w, uint32_t h)
{
    uint32_t log2w = rsx_log2_ceil(w);
    uint32_t log2h = rsx_log2_ceil(h);
    uint32_t span  = (1u << log2w) * (1u << log2h);
    uint8_t* seen  = (uint8_t*)calloc(span, 1);
    int ok = 1;
    for (uint32_t y = 0; y < h && ok; y++) {
        for (uint32_t x = 0; x < w && ok; x++) {
            uint32_t o = rsx_morton_offset(x, y, log2w, log2h);
            if (o >= span) { printf("  [FAIL] offset %u out of span %u at (%u,%u)\n", o, span, x, y); ok = 0; break; }
            if (seen[o])   { printf("  [FAIL] collision at offset %u (%u,%u)\n", o, x, y); ok = 0; break; }
            seen[o] = 1;
        }
    }
    free(seen);
    printf("  bijection %ux%u: %s\n", w, h, ok ? "ok" : "BROKEN");
    return ok;
}

static int test_roundtrip(uint32_t w, uint32_t h, uint32_t bpp_bytes)
{
    uint32_t texels = w * h;
    uint8_t* linear   = (uint8_t*)malloc(texels * bpp_bytes);
    /* swizzled buffer must cover the pow2-padded extent */
    uint32_t padded   = rsx_morton_padded_texels(w, h);
    uint8_t* swizzled = (uint8_t*)calloc(padded * bpp_bytes, 1);
    uint8_t* out      = (uint8_t*)malloc(texels * bpp_bytes);

    /* Deterministic pattern: each texel byte derived from its linear index. */
    for (uint32_t i = 0; i < texels * bpp_bytes; i++)
        linear[i] = (uint8_t)((i * 37u + 11u) & 0xFF);

    /* Forward swizzle: place each linear texel at its Morton offset. */
    uint32_t log2w = rsx_log2_ceil(w);
    uint32_t log2h = rsx_log2_ceil(h);
    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            uint32_t s = rsx_morton_offset(x, y, log2w, log2h) * bpp_bytes;
            uint32_t d = ((y * w) + x) * bpp_bytes;
            memcpy(swizzled + s, linear + d, bpp_bytes);
        }
    }

    /* Inverse: deswizzle back to linear. */
    rsx_deswizzle_texels(swizzled, out, w, h, bpp_bytes);

    int ok = (memcmp(linear, out, texels * bpp_bytes) == 0);
    printf("  roundtrip %ux%u bpp=%u: %s\n", w, h, bpp_bytes, ok ? "ok" : "MISMATCH");

    free(linear); free(swizzled); free(out);
    return ok;
}

static int test_d8_remap_alpha_one(void)
{
    int ok = 1;
    int rgba[4];
#define CHECKR(cond) do { if (!(cond)) { printf("  [FAIL] remap: %s\n", #cond); ok = 0; } } while (0)
    rsx_texture_remap_rgba(0xAAE4u, RSX_TEXTURE_A8R8G8B8, rgba);
    CHECKR(rgba[0] == RSX_SWZ_RED && rgba[1] == RSX_SWZ_GREEN &&
           rgba[2] == RSX_SWZ_BLUE && rgba[3] == RSX_SWZ_ALPHA);
    /* D8 identity would keep A=D; product forces A=1 so skin/env aren't translucent. */
    rsx_texture_remap_rgba(0xAAE4u, RSX_TEXTURE_D8R8G8B8, rgba);
    CHECKR(rgba[0] == RSX_SWZ_RED && rgba[1] == RSX_SWZ_GREEN &&
           rgba[2] == RSX_SWZ_BLUE && rgba[3] == RSX_SWZ_ONE);
    rsx_texture_remap_rgba(0, RSX_TEXTURE_D8R8G8B8, rgba);
    CHECKR(rgba[3] == RSX_SWZ_ONE);
    /* Explicit A=ZERO stays ZERO. 0xAAE4 with A op=ZERO: low A src=A, op bits
     * A=00 → 0xAAE4 & ~0x0300 = 0xA8E4. */
    rsx_texture_remap_rgba(0xA8E4u, RSX_TEXTURE_D8R8G8B8, rgba);
    CHECKR(rgba[3] == RSX_SWZ_ZERO);
    /* Guest already set A=ONE (op=01 on A): 0xAAE4 with A op=01 → 0xA9E4. */
    rsx_texture_remap_rgba(0xA9E4u, RSX_TEXTURE_D8R8G8B8, rgba);
    CHECKR(rgba[3] == RSX_SWZ_ONE);
#undef CHECKR
    printf("  d8 remap alpha-one: %s\n", ok ? "ok" : "FAIL");
    return ok;
}

int main(void)
{
    int ok = 1;

    printf("[test] rsx_texture_deswizzle\n");

    /* Detector asserts (Fix 1c) + golden Morton offsets (Fix 2): these are
     * NON-tautological — they compare against hand-derived expected values. */
    ok &= test_format_detector();
    ok &= test_golden_square();
    ok &= test_golden_nonsquare();

    /* Required by task: 8x8x4 known pattern round-trip. */
    ok &= test_roundtrip(8, 8, 4);

    /* Extra coverage: other sizes / bpp incl. non-square, non-pow2. */
    ok &= test_roundtrip(16, 16, 4);
    ok &= test_roundtrip(4, 4, 2);
    ok &= test_roundtrip(32, 8, 4);   /* non-square */
    ok &= test_roundtrip(8, 32, 1);   /* non-square, 1 bpp */
    ok &= test_roundtrip(6, 6, 4);    /* non-pow2 -> padded */

    ok &= test_bijection(8, 8);
    ok &= test_bijection(32, 8);
    ok &= test_bijection(6, 6);
    ok &= test_d8_remap_alpha_one();

    printf("%s\n", ok ? "[PASS]" : "[FAIL]");
    return ok ? 0 : 1;
}
