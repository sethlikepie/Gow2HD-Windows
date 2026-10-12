/* rsx_core_decode: portable C decoders/hashes shared by every RSX backend
 * (Metal today, Vulkan later). Moved verbatim out of rsx_metal_backend.m --
 * see .superpowers/sdd/2026-09-23-vulkan-v1a-rsx-core-pure/task-1-brief.md.
 *
 * No ObjC, no Metal types, no mutable global state: every function here is
 * pure (input -> output), so it can be exhaustively oracle-tested against the
 * old Metal bodies and reused by any future backend.
 *
 * The per-vertex / per-texel decoders (bswap32, the BE readers, the 16-bit
 * expanders, va_fp_fast) are `static inline` here, not out-of-line in the
 * .c: the build has no LTO, and these run on GoW2's CPU-bound hot paths
 * (VA_LOOP per vertex component, texture upload per texel) -- fix round 1
 * (2026-09-23) found the out-of-line version silently dropped the
 * inlining the old Metal statics had. attrib_sig/tex_fingerprint/
 * texc_bucket run once per draw/bind, not per element, so they stay
 * out-of-line in rsx_core_decode.c. */
#ifndef PS3RECOMP_RSX_CORE_DECODE_H
#define PS3RECOMP_RSX_CORE_DECODE_H

#include "ps3emu/ps3types.h"
#include "rsx_commands.h"

#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline u32 rsx_core_bswap32(u32 v)
{
    return ((v >> 24) & 0xFFu) | ((v >> 8) & 0xFF00u) |
           ((v << 8) & 0xFF0000u) | ((v << 24) & 0xFF000000u);
}

/* IEEE754 half (BE in guest) → float. */
static inline float rsx_core_half_be_to_float(const u8* p)
{
    u16 h = (u16)((p[0] << 8) | p[1]);
    u32 sign = (h >> 15) & 1u;
    u32 exp  = (h >> 10) & 0x1Fu;
    u32 mant = h & 0x3FFu;
    u32 f;
    if (exp == 0) {
        if (mant == 0) f = sign << 31;
        else {
            exp = 127 - 15 + 1;
            while ((mant & 0x400u) == 0) { mant <<= 1; exp--; }
            mant &= 0x3FFu;
            f = (sign << 31) | (exp << 23) | (mant << 13);
        }
    } else if (exp == 31) {
        f = (sign << 31) | 0x7F800000u | (mant << 13);
    } else {
        f = (sign << 31) | ((exp + (127 - 15)) << 23) | (mant << 13);
    }
    float out;
    memcpy(&out, &f, 4);
    return out;
}

static inline float rsx_core_read_f32_be(const u8* p)
{
    u32 w;
    memcpy(&w, p, 4);
    w = rsx_core_bswap32(w);
    float f;
    memcpy(&f, &w, 4);
    return f;
}

static inline float rsx_core_snorm16_be(const u8* p)
{
    s16 v = (s16)((p[0] << 8) | p[1]);
    if (v == -32768) return -1.f;
    return (float)v / 32767.f;
}

/* Impressao do conteudo de um atributo para o cache: 16 palavras de 8 bytes
 * espalhadas pelo intervalo, custo FIXO. Ver rsx_metal_backend.m para a nota
 * de medicao completa (NEON vs FNV) que motivou esta forma. */
static inline u64 rsx_core_va_fp_fast(const u8* p, u32 n)
{
    u64 h = 0x9E3779B97F4A7C15ull ^ n;
    if (n < 8u) {
        for (u32 i = 0; i < n; i++) h = (h ^ p[i]) * 0x100000001B3ull;
        return h;
    }
    const u32 last = n - 8u;
    for (u32 k = 0; k < 16u; k++) {
        const u32 off = (u32)(((u64)last * k) / 15u);
        u64 w; memcpy(&w, p + off, 8);
        h ^= w; h *= 0x9E3779B97F4A7C15ull; h ^= h >> 29;
    }
    return h;
}

/* Expand 16-bit RSX packed colours to BGRA8 host (after host-endian u16). */
static inline void rsx_core_expand_r5g6b5_to_bgra(u16 t, u8* bgra)
{
    u32 r = (t >> 11) & 0x1F;
    u32 g = (t >> 5) & 0x3F;
    u32 b = t & 0x1F;
    bgra[0] = (u8)((b * 255 + 15) / 31);
    bgra[1] = (u8)((g * 255 + 31) / 63);
    bgra[2] = (u8)((r * 255 + 15) / 31);
    bgra[3] = 255;
}

static inline void rsx_core_expand_a1r5g5b5_to_bgra(u16 t, u8* bgra)
{
    u32 a = (t >> 15) & 1;
    u32 r = (t >> 10) & 0x1F;
    u32 g = (t >> 5) & 0x1F;
    u32 b = t & 0x1F;
    bgra[0] = (u8)((b * 255 + 15) / 31);
    bgra[1] = (u8)((g * 255 + 15) / 31);
    bgra[2] = (u8)((r * 255 + 15) / 31);
    bgra[3] = a ? 255 : 0;
}

static inline void rsx_core_expand_a4r4g4b4_to_bgra(u16 t, u8* bgra)
{
    u32 a = (t >> 12) & 0xF;
    u32 r = (t >> 8) & 0xF;
    u32 g = (t >> 4) & 0xF;
    u32 b = t & 0xF;
    bgra[0] = (u8)(b * 17);
    bgra[1] = (u8)(g * 17);
    bgra[2] = (u8)(r * 17);
    bgra[3] = (u8)(a * 17);
}

/* Out-of-line (rsx_core_decode.c): once-per-draw / once-per-bind, not on the
 * per-vertex/per-texel hot path. */
u32   rsx_core_attrib_sig(const rsx_vertex_attrib* a16);            /* FNV over enabled attribs */
u64   rsx_core_tex_fingerprint(const u8* p, u32 n, u32 win);        /* win: sampling window (Metal passes its PS3_METAL_TEX_FPWIN value) */
u32   rsx_core_texc_bucket(u32 ea, u32 fmt, u32 w, u32 h, u32 nbuckets_pow2);

#ifdef __cplusplus
}
#endif

#endif /* PS3RECOMP_RSX_CORE_DECODE_H */
