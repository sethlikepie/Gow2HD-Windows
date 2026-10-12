/* rsx_core_rules: bodies moved verbatim from rsx_metal_backend.m
 * (metal_resolve_fp_ea, metal_resolve_tex_ea, the per-draw viewport block,
 * the per-draw scissor block, the alpha-ref block, metal_clamp_dim, the
 * render-target dims block and the rtt/cloc block of metal_set_render_target).
 * See .superpowers/sdd/2026-09-23-vulkan-v1a-rsx-core-pure/task-2-brief.md
 * for the anchors and the oracle test that proves bit-for-bit equivalence. */
#include "rsx_core_rules.h"
#include "rsx_core_decode.h"

#include <string.h>

/* SET_SHADER_PROGRAM carries the DMA context in [1:0] as libgcm writes it:
 * location + 1, i.e. raw 1 = RSX local, raw 2 = main (via the IO map) -- the
 * same convention as rsx_dispatch.h and RPCS3 (`(value & 3) - 1`). Raw 0
 * keeps the old local mapping (see rsx_metal_backend.m for the E432 note on
 * why the old code's raw-0-only local mapping was wrong for GoW2). */
u32 rsx_core_resolve_fp_ea(const rsx_state* s, rsx_core_io2ea_fn io2ea, int* unresolved)
{
    if (unresolved) *unresolved = 0;
    u32 loc = s->shader_program & 0x3u;
    u32 off = s->fragment_program_addr;
    if (loc == 1 || loc == 0) return 0xC0000000u + off;
    if (loc == 2) {
        u32 ea = 0;
        if (io2ea && io2ea(off, &ea)) return ea;
    }
    if (unresolved) *unresolved = 1;
    return off;
}

/* Same DMA-context convention as rsx_core_resolve_fp_ea: raw 1 = local,
 * raw 2 = main (translate through the IO map instead of using the offset as
 * a flat EA). */
u32 rsx_core_resolve_tex_ea(const rsx_texture_state* t, rsx_core_io2ea_fn io2ea)
{
    u32 loc = t->format & 0x3u;
    if (loc == 1) return 0xC0000000u + t->offset;
    if (loc == 2) {
        u32 ea = 0;
        if (io2ea && io2ea(t->offset, &ea)) return ea;
    }
    return t->offset;
}

/* RSX maps clip to window with VIEWPORT_SCALE/OFFSET, not the viewport rect
 * (RPCS3 fill_scale_offset_data): window = ndc * scale + offset.
 * cellGcmSetViewport puts +0.5 in the offset, so pixel i samples UV i/w --
 * GoW2's one-tap bloom blur (671F7750) alternates offsets 0 and +1 texel
 * around that and, with the rect alone, never blurred and drifted a texel
 * up-left per pass (ghost of the bloom, 2026-09-16). Top-left origin
 * (scale.y < 0) only; so_on=0 (PS3_METAL_VIEWPORT_SO=0) = rect. */
void rsx_core_viewport(int has_vp, const float vp[4], u32 tw, u32 th,
                       const float vso[8], u32 vso_seen, int so_on, double out[4])
{
    double x, y, w, h;
    if (has_vp && vp[2] > 0.0f && vp[3] > 0.0f) {
        x = vp[0]; y = vp[1]; w = vp[2]; h = vp[3];
    } else {
        x = 0.0; y = 0.0; w = (double)tw; h = (double)th;
    }
    const float sx = vso[0], sy = vso[1], ox = vso[4], oy = vso[5];
    if (so_on && vso_seen == 3u && sx > 0.0f && sy < 0.0f) {
        x = (double)ox - (double)sx;
        y = (double)oy + (double)sy;
        w = 2.0 * (double)sx;
        h = -2.0 * (double)sy;
    }
    out[0] = x; out[1] = y; out[2] = w; out[3] = h;
}

int rsx_core_scissor_per_draw(const u32 sc[4], u32 tw, u32 th, int scissor_fix, u32 out[4])
{
    u32 sx = sc[0], sy = sc[1];
    u32 sw = sc[2] ? sc[2] : tw, sh = sc[3] ? sc[3] : th;
    if (sx >= tw || sy >= th) {
        /* Fora do alvo: interseccao vazia -> o draw nao pinta nada. */
        if (scissor_fix) return 1;
        if (sx >= tw) sx = 0;
        if (sy >= th) sy = 0;
    }
    if (sx + sw > tw) sw = tw - sx;
    if (sy + sh > th) sh = th - sy;
    if (sw == 0 || sh == 0) return 2;
    out[0] = sx; out[1] = sy; out[2] = sw; out[3] = sh;
    return 0;
}

/* ALPHA_REF by surface colour format, as RPCS3 rsx_methods.h alpha_ref().
 * surface_format is the raw rsx_state field (masked with 0x1F here, same as
 * the old `cf` local); pass 8u for a "no state" sentinel (8u & 0x1Fu == 8u
 * falls into the plain 0..255 branch, matching the old code's st==NULL
 * default). */
float rsx_core_alpha_ref(u32 surface_format, u32 alpha_ref)
{
    const u32 cf = surface_format & 0x1Fu;
    const u32 rr = alpha_ref;
    float ref;
    if (cf == 0x0Cu || cf == 0x0Du) {
        memcpy(&ref, &rr, 4);
    } else if (cf == 0x0Bu) {
        const u16 hbits = (u16)(rr & 0xFFFFu);
#if defined(__aarch64__) || defined(__ARM_FP16_FORMAT_IEEE)
        const __fp16 hv = *(const __fp16*)&hbits;
        ref = (float)hv;
#else
        /* Portable fallback: hbits is a host-order (little-endian on every
         * platform this build targets) IEEE754 half bit pattern -- the same
         * bytes __fp16 reads on arm64. Re-order to big-endian and reuse the
         * exhaustively-proven decoder; both paths agree bit-for-bit except that
         * sNaN inputs (0x7C01-0x7DFF, 0xFC01-0xFDFF) are not quieted by the
         * portable branch (1022 of 65536 halves differ: __fp16 quiets to 0x7FC02000,
         * portable keeps signalling payload at 0x7F802000, but either way fails
         * alpha test comparisons identically). */
        const u8 be[2] = { (u8)(hbits >> 8), (u8)hbits };
        ref = rsx_core_half_be_to_float(be);
#endif
    } else {
        ref = (float)(rr & 0xFFu) / 255.0f;
    }
    return ref;
}

/* Guest clip can be garbage before init; keep display-sized RTs. */
u32 rsx_core_clamp_dim(u32 v, u32 def)
{
    if (v == 0 || v > 1920u) return def;
    return v;
}

/* Prefer viewport when clip is nonsense (e.g. height >> 1080). */
void rsx_core_rt_dims(const rsx_state* s, u32 def_w, u32 def_h, u32* w, u32* h)
{
    u32 ww = s->surface_clip_w ? s->surface_clip_w
                                : (s->viewport_w ? s->viewport_w : def_w);
    u32 hh = s->surface_clip_h ? s->surface_clip_h
                                : (s->viewport_h ? s->viewport_h : def_h);
    if (ww > 1920u || hh > 1080u || (u64)ww * (u64)hh > (u64)1920 * 1080) {
        ww = s->viewport_w ? s->viewport_w : def_w;
        hh = s->viewport_h ? s->viewport_h : def_h;
        ww = rsx_core_clamp_dim(ww, def_w);
        hh = rsx_core_clamp_dim(hh, def_h);
    }
    *w = ww; *h = hh;
}

/* Alvos ligados (RPCS3 get_rtt_indexes). none (0) mantem o alvo A como
 * antes, para os passes so' de profundidade.
 * Localizacao de cada alvo (SET_CONTEXT_DMA_COLOR_*): 0 VRAM local, 1
 * memoria principal. O mesmo offset pode existir nas duas (o RPCS3 resolve
 * por endereco absoluto, get_address(offset, dma)). */
void rsx_core_mrt_targets(const rsx_state* s, int mrt_on, int rtt[4], int* nrtt, u32 cloc[4])
{
    rtt[0] = 0; rtt[1] = -1; rtt[2] = -1; rtt[3] = -1;
    *nrtt = 1;
    if (mrt_on) {
        switch (s->color_target) {
        case 0x02: rtt[0] = 1; break;                                      /* so' B */
        case 0x13: rtt[1] = 1; *nrtt = 2; break;                           /* A,B */
        case 0x17: rtt[1] = 1; rtt[2] = 2; *nrtt = 3; break;               /* A,B,C */
        case 0x1F: rtt[1] = 1; rtt[2] = 2; rtt[3] = 3; *nrtt = 4; break;   /* A,B,C,D */
        default: break;
        }
    }
    cloc[0] = 0; cloc[1] = 0; cloc[2] = 0; cloc[3] = 0;
    if (mrt_on)
        for (int k = 0; k < 4; k++)
            cloc[k] = (s->surface_color_dma[k] == RSX_CONTEXT_DMA_MAIN) ? 1u : 0u;
}
