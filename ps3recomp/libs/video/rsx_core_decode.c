/* rsx_core_decode: portable C decoders/hashes shared by every RSX backend.
 * Bodies moved verbatim from rsx_metal_backend.m (metal_* statics); see
 * .superpowers/sdd/2026-09-23-vulkan-v1a-rsx-core-pure/task-1-brief.md for the
 * anchors and the oracle test that proves bit-for-bit equivalence.
 *
 * Only the once-per-draw/once-per-bind functions live here out-of-line; the
 * per-vertex/per-texel decoders moved to rsx_core_decode.h as `static
 * inline` in fix round 1 (2026-09-23) -- no LTO in this build, and those ran
 * on GoW2's CPU-bound hot paths. */
#include "rsx_core_decode.h"

u32 rsx_core_attrib_sig(const rsx_vertex_attrib* a16)
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

/* n <= 1040: limiar herdado do tamanho das 65 janelas antigas; abaixo dele
 * hasheia tudo. Arbitrario, sem defeito conhecido.
 * step = (n-16)/wins e' divisao inteira: a cauda entre o ultimo passo e
 * n-16 fica coberta pela janela k==wins, que e' exactamente n-16.
 * win: janela de amostragem (era o `s_win` estatico lido de
 * PS3_METAL_TEX_FPWIN no Metal; aqui o chamador passa o valor ja' resolvido). */
u64 rsx_core_tex_fingerprint(const u8* p, u32 n, u32 win)
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

u32 rsx_core_texc_bucket(u32 ea, u32 fmt, u32 w, u32 h, u32 nbuckets_pow2)
{
    u32 x = ea ^ (fmt * 0x9E3779B1u) ^ (w << 16) ^ h;
    x ^= x >> 15; x *= 0x2C1B3C6Du; x ^= x >> 12;
    return x & (nbuckets_pow2 - 1u);
}
