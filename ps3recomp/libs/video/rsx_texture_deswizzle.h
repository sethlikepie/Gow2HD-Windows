/*
 * ps3recomp - RSX Texture Deswizzle (Morton order -> linear)
 *
 * PS3 RSX stores non-"linear" (LN flag clear) textures in a swizzled,
 * power-of-two Morton layout: the low bits of X and Y are bit-interleaved.
 * D3D12 expects a row-major (linear) layout, so before uploading we must
 * un-permute the texels.
 *
 * Addressing rule (matches Nvidia NV40 / RPCS3 rsx_utils swizzle):
 *   - The swizzled surface is addressed as if its dimensions were padded up
 *     to the next power of two (log2 ceil of width/height).
 *   - Starting from the least-significant bit, one X bit and one Y bit are
 *     emitted alternately (X first). When one axis runs out of bits, the
 *     remaining high bits of the longer axis are appended linearly on top.
 *
 * This applies ONLY to uncompressed linear-swizzled formats (A8R8G8B8,
 * R5G6B5, B8, ...). Block-compressed (DXT/BC) surfaces are NOT Morton
 * swizzled and must be copied verbatim by the caller.
 *
 * Reference: RPCS3 rpcs3/Emu/RSX/Common/TextureUtils.cpp (swizzle transform),
 * nouveau nv40 texture layout.
 */

#ifndef PS3RECOMP_RSX_TEXTURE_DESWIZZLE_H
#define PS3RECOMP_RSX_TEXTURE_DESWIZZLE_H

#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ceil(log2(n)) — number of address bits needed for n texels (pad to pow2).
 * n == 0 / n == 1 return 0. */
static inline uint32_t rsx_log2_ceil(uint32_t n)
{
    uint32_t l = 0;
    while ((1u << l) < n) l++;
    return l;
}

/* Swizzled (Morton) texel index for linear coordinate (x, y), given the
 * power-of-two log2 dimensions of the padded surface. Interleaves X/Y bits
 * (X first); leftover high bits of the larger axis are appended linearly. */
static inline uint32_t rsx_morton_offset(uint32_t x, uint32_t y,
                                         uint32_t log2_w, uint32_t log2_h)
{
    uint32_t offset = 0;
    uint32_t shift  = 0;
    uint32_t xi = 0, yi = 0;
    uint32_t rx = log2_w, ry = log2_h;
    while (rx || ry) {
        if (rx) {
            offset |= ((x >> xi) & 1u) << shift;
            shift++; xi++; rx--;
        }
        if (ry) {
            offset |= ((y >> yi) & 1u) << shift;
            shift++; yi++; ry--;
        }
    }
    return offset;
}

/* Deswizzle a full 2D surface from Morton order (src) to row-major linear
 * (dst). bpp_bytes is bytes-per-texel (1, 2, 4, 8, 16). dst must hold
 * width*height*bpp_bytes bytes (tightly packed). src is addressed with the
 * power-of-two padded extent, so it may span up to
 * (1<<log2w)*(1<<log2h)*bpp_bytes bytes — see rsx_morton_padded_texels(). */
static inline void rsx_deswizzle_texels(const uint8_t* src, uint8_t* dst,
                                        uint32_t width, uint32_t height,
                                        uint32_t bpp_bytes)
{
    uint32_t log2w = rsx_log2_ceil(width);
    uint32_t log2h = rsx_log2_ceil(height);
    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            uint32_t s = rsx_morton_offset(x, y, log2w, log2h) * bpp_bytes;
            uint32_t d = ((y * width) + x) * bpp_bytes;
            memcpy(dst + d, src + s, bpp_bytes);
        }
    }
}

/* Number of texels the swizzled source spans (power-of-two padded area).
 * Callers use this to size the committed-page guard over guest memory. */
static inline uint32_t rsx_morton_padded_texels(uint32_t width, uint32_t height)
{
    return (1u << rsx_log2_ceil(width)) * (1u << rsx_log2_ceil(height));
}

#ifdef __cplusplus
}
#endif
#endif
