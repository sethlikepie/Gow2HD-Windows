/* Oracle test: rsx_core_decode.c vs verbatim old_* copies of the Metal bodies
 * it replaced (rsx_metal_backend.m, HEAD 918fd6f0). Proves bit-for-bit
 * equivalence exhaustively (every u16, 1M random u32s, hash buffers up to
 * 1 MiB, 100k randomized attrib tables) instead of a handful of examples.
 * See .superpowers/sdd/2026-09-23-vulkan-v1a-rsx-core-pure/task-1-brief.md. */
#include "rsx_core_decode.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------------------
 * old_*: verbatim copies of the Metal statics being replaced, renamed only.
 * old_tex_fingerprint takes `win` instead of the lazy PS3_METAL_TEX_FPWIN
 * static; old_texc_bucket masks with 255u instead of METAL_TEXC_BUCKETS - 1u
 * (both per the task brief). Everything else is byte-for-byte the old body. */

static u32 old_bswap32(u32 v)
{
    return ((v >> 24) & 0xFFu) | ((v >> 8) & 0xFF00u) |
           ((v << 8) & 0xFF0000u) | ((v << 24) & 0xFF000000u);
}

static float old_half_be_to_float(const u8* p)
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

static float old_read_f32_be(const u8* p)
{
    u32 w;
    memcpy(&w, p, 4);
    w = old_bswap32(w);
    float f;
    memcpy(&f, &w, 4);
    return f;
}

static float old_snorm16_be(const u8* p)
{
    s16 v = (s16)((p[0] << 8) | p[1]);
    if (v == -32768) return -1.f;
    return (float)v / 32767.f;
}

static u32 old_attrib_sig(const rsx_vertex_attrib* a16)
{
    u32 h = 2166136261u;
    for (int i = 0; i < RSX_MAX_VERTEX_ATTRIBS; i++) {
        if (!a16[i].enabled) continue;
        h ^= (u32)i; h *= 16777619u;
        h ^= a16[i].type; h *= 16777619u;
        h ^= a16[i].size; h *= 16777619u;
        h ^= a16[i].stride; h *= 16777619u;
        h ^= a16[i].offset; h *= 16777619u;
    }
    return h;
}

static inline u64 old_va_fp_fast(const u8* p, u32 n)
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

static void old_expand_r5g6b5_to_bgra(u16 t, u8* bgra)
{
    u32 r = (t >> 11) & 0x1F;
    u32 g = (t >> 5) & 0x3F;
    u32 b = t & 0x1F;
    bgra[0] = (u8)((b * 255 + 15) / 31);
    bgra[1] = (u8)((g * 255 + 31) / 63);
    bgra[2] = (u8)((r * 255 + 15) / 31);
    bgra[3] = 255;
}

static void old_expand_a1r5g5b5_to_bgra(u16 t, u8* bgra)
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

static void old_expand_a4r4g4b4_to_bgra(u16 t, u8* bgra)
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

static u64 old_tex_fingerprint(const u8* p, u32 n, u32 win)
{
    u64 h = 1469598103934665603ull ^ (u64)n;
    if (n <= 1040u) {
        for (u32 i = 0; i < n; i++) { h ^= p[i]; h *= 1099511628211ull; }
        return h;
    }
    u32 wins = n / win;
    if (wins < 64u) wins = 64u;
    if (wins > 4096u) wins = 4096u;
    const u32 step = (n - 16u) / wins;
    for (u32 k = 0; k <= wins; k++) {
        const u32 off = (k == wins) ? n - 16u : k * step;
        for (u32 i = 0; i < 16u; i++) { h ^= p[off + i]; h *= 1099511628211ull; }
    }
    return h;
}

static u32 old_texc_bucket(u32 ea, u32 fmt, u32 w, u32 h)
{
    u32 x = ea ^ (fmt * 0x9E3779B1u) ^ (w << 16) ^ h;
    x ^= x >> 15; x *= 0x2C1B3C6Du; x ^= x >> 12;
    return x & (255u);
}

/* --------------------------------------------------------------------- */

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
