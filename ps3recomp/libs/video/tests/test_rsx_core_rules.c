/* Oracle test: rsx_core_rules.c vs verbatim old_* copies of the Metal bodies
 * it replaced (rsx_metal_backend.m, HEAD 918fd6f0 + Task 1's f3d555f2).
 * Proves bit-for-bit equivalence over exhaustive/large random domains
 * instead of a handful of examples. See
 * .superpowers/sdd/2026-09-23-vulkan-v1a-rsx-core-pure/task-2-brief.md. */
#include "rsx_core_rules.h"
#include "rsx_core_decode.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------------------
 * old_*: verbatim copies of the Metal bodies being replaced, renamed only,
 * parameterised exactly as the task brief's old_* description asks (globals
 * such as gcm_io_offset_to_ea / metal_scissor_fix / metal_mrt_on replaced by
 * the same parameters the new API takes). */

static int stub_io2ea(u32 off, u32* ea)
{
    if (off & 1u) return 0;
    *ea = off ^ 0x40000000u;
    return 1;
}

static u32 old_resolve_fp_ea(const rsx_state* state, rsx_core_io2ea_fn io2ea, int* unresolved)
{
    if (unresolved) *unresolved = 0;
    u32 loc = state->shader_program & 0x3u;
    u32 off = state->fragment_program_addr;
    if (loc == 1 || loc == 0) return 0xC0000000u + off;
    if (loc == 2) {
        u32 ea = 0;
        if (io2ea(off, &ea)) return ea;
    }
    if (unresolved) *unresolved = 1;
    return off;
}

static u32 old_resolve_tex_ea(const rsx_texture_state* tex, rsx_core_io2ea_fn io2ea)
{
    u32 loc = tex->format & 0x3u;
    if (loc == 1) return 0xC0000000u + tex->offset;
    if (loc == 2) {
        u32 ea = 0;
        if (io2ea(tex->offset, &ea)) return ea;
    }
    return tex->offset;
}

static void old_viewport(int has_vp, const float vp[4], u32 tw, u32 th,
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

static int old_scissor(const u32 sc[4], u32 tw, u32 th, int scissor_fix, u32 out[4])
{
    u32 sx = sc[0], sy = sc[1];
    u32 sw = sc[2] ? sc[2] : tw, sh = sc[3] ? sc[3] : th;
    if (sx >= tw || sy >= th) {
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

#if defined(__aarch64__)
static float old_alpha_ref(u32 surface_format, u32 alpha_ref)
{
    const u32 cf = surface_format & 0x1Fu;
    const u32 rr = alpha_ref;
    float ref;
    if (cf == 0x0Cu || cf == 0x0Du) {
        memcpy(&ref, &rr, 4);
    } else if (cf == 0x0Bu) {
        const u16 hbits = (u16)(rr & 0xFFFFu);
        const __fp16 hv = *(const __fp16*)&hbits;
        ref = (float)hv;
    } else {
        ref = (float)(rr & 0xFFu) / 255.0f;
    }
    return ref;
}
#endif

static u32 old_clamp_dim(u32 v, u32 def)
{
    if (v == 0 || v > 1920u) return def;
    return v;
}

static void old_rt_dims(const rsx_state* s, u32 def_w, u32 def_h, u32* w, u32* h)
{
    u32 ww = s->surface_clip_w ? s->surface_clip_w
                                : (s->viewport_w ? s->viewport_w : def_w);
    u32 hh = s->surface_clip_h ? s->surface_clip_h
                                : (s->viewport_h ? s->viewport_h : def_h);
    if (ww > 1920u || hh > 1080u || (u64)ww * (u64)hh > (u64)1920 * 1080) {
        ww = s->viewport_w ? s->viewport_w : def_w;
        hh = s->viewport_h ? s->viewport_h : def_h;
        ww = old_clamp_dim(ww, def_w);
        hh = old_clamp_dim(hh, def_h);
    }
    *w = ww; *h = hh;
}

static void old_mrt_targets(const rsx_state* s, int mrt_on, int rtt[4], int* nrtt, u32 cloc[4])
{
    rtt[0] = 0; rtt[1] = -1; rtt[2] = -1; rtt[3] = -1;
    *nrtt = 1;
    if (mrt_on) {
        switch (s->color_target) {
        case 0x02: rtt[0] = 1; break;
        case 0x13: rtt[1] = 1; *nrtt = 2; break;
        case 0x17: rtt[1] = 1; rtt[2] = 2; *nrtt = 3; break;
        case 0x1F: rtt[1] = 1; rtt[2] = 2; rtt[3] = 3; *nrtt = 4; break;
        default: break;
        }
    }
    cloc[0] = 0; cloc[1] = 0; cloc[2] = 0; cloc[3] = 0;
    if (mrt_on)
        for (int k = 0; k < 4; k++)
            cloc[k] = (s->surface_color_dma[k] == RSX_CONTEXT_DMA_MAIN) ? 1u : 0u;
}

/* --------------------------------------------------------------------- */

static int fails;
#define CHECK(name, cond) do { if (!(cond)) { printf("FAIL %s\n", name); fails++; } else printf("[PASS] %s\n", name); } while (0)
static int same_bits(float a, float b) { u32 x, y; memcpy(&x, &a, 4); memcpy(&y, &b, 4); return x == y; }
static int same_bits64(double a, double b) { u64 x, y; memcpy(&x, &a, 8); memcpy(&y, &b, 8); return x == y; }

/* fill_state_random: randomize every rsx_state field the functions read. */
static void fill_state_random(rsx_state* s)
{
    memset(s, 0, sizeof *s);
    s->shader_program = ((u32)rand() << 16) ^ (u32)rand();
    s->fragment_program_addr = ((u32)rand() << 16) ^ (u32)rand();
    s->surface_clip_w = (u32)rand();
    s->surface_clip_h = (u32)rand();
    s->viewport_w = (u32)rand();
    s->viewport_h = (u32)rand();
    s->surface_format = (u32)rand();
    s->alpha_ref = ((u32)rand() << 16) ^ (u32)rand();
    if ((rand() & 3) == 0) {
        static const u32 biased[] = { 0x02u, 0x13u, 0x17u, 0x1Fu };
        s->color_target = biased[rand() % 4];
    } else {
        s->color_target = (u32)(rand() & 0xFF);
    }
    for (int i = 0; i < 4; i++) {
        s->surface_color_dma[i] = (rand() & 1) ? RSX_CONTEXT_DMA_MAIN : (u32)rand();
        s->surface_color_offset[i] = ((u32)rand() << 16) ^ (u32)rand();
    }
}

static float rand_float_biased(void)
{
    switch (rand() % 5) {
    case 0: return 0.0f;
    case 1: return -(float)(rand() % 4096) - (float)(rand() % 1000) / 1000.0f;
    case 2: return (float)(rand() % 4096) + (float)(rand() % 1000) / 1000.0f;
    case 3: { u32 bits = 0x7FC00000u ^ ((u32)rand() & 0x3FFFFFu); float f; memcpy(&f, &bits, 4); return f; } /* NaN */
    default: { u32 bits = (u32)rand(); float f; memcpy(&f, &bits, 4); return f; }
    }
}

static u32 rand_sc_component(u32 bound)
{
    switch (rand() % 4) {
    case 0: return 0u;
    case 1: return bound;                                    /* exactly == bound (out-of-target edge) */
    case 2: return bound + 1u + (u32)(rand() % 100);          /* past bound */
    default: return (u32)(rand() % (bound ? bound * 2u + 1u : 100u));
    }
}

static u32 rand_dim_bucket(void)
{
    switch (rand() % 4) {
    case 0: return 0u;
    case 1: return 1u + (u32)(rand() % 1920);
    case 2: return 1921u + (u32)(rand() % (4096 - 1921 + 1));
    default: return (u32)rand();                              /* garbage */
    }
}

static int fp_tex_ea_oracle(void)
{
    srand(11);
    for (int i = 0; i < 1000000; i++) {
        rsx_state s; fill_state_random(&s);
        rsx_texture_state t; memset(&t, 0, sizeof t);
        t.format = (u32)rand();
        t.offset = ((u32)rand() << 16) ^ (u32)rand();

        int un_new = -1, un_old = -1;
        u32 a = rsx_core_resolve_fp_ea(&s, stub_io2ea, &un_new);
        u32 b = old_resolve_fp_ea(&s, stub_io2ea, &un_old);
        if (a != b || un_new != un_old) { printf("fp_ea mismatch i=%d\n", i); return 0; }

        u32 c = rsx_core_resolve_tex_ea(&t, stub_io2ea);
        u32 d = old_resolve_tex_ea(&t, stub_io2ea);
        if (c != d) { printf("tex_ea mismatch i=%d\n", i); return 0; }
    }
    return 1;
}

static int viewport_oracle(void)
{
    srand(22);
    for (int i = 0; i < 1000000; i++) {
        int has_vp = rand() & 1;
        float vp[4] = { rand_float_biased(), rand_float_biased(), rand_float_biased(), rand_float_biased() };
        u32 tw = 1u + (u32)(rand() % 4096);
        u32 th = 1u + (u32)(rand() % 4096);
        float vso[8];
        for (int k = 0; k < 8; k++) vso[k] = rand_float_biased();
        u32 seen = (u32)(rand() % 4);
        int so_on = rand() & 1;
        double out_new[4], out_old[4];
        rsx_core_viewport(has_vp, vp, tw, th, vso, seen, so_on, out_new);
        old_viewport(has_vp, vp, tw, th, vso, seen, so_on, out_old);
        for (int k = 0; k < 4; k++)
            if (!same_bits64(out_new[k], out_old[k])) { printf("viewport mismatch i=%d k=%d\n", i, k); return 0; }
    }
    return 1;
}

static int scissor_oracle(void)
{
    srand(33);
    for (int i = 0; i < 1000000; i++) {
        u32 tw = 1u + (u32)(rand() % 4096);
        u32 th = 1u + (u32)(rand() % 4096);
        u32 sc[4];
        sc[0] = rand_sc_component(tw);
        sc[1] = rand_sc_component(th);
        sc[2] = rand_sc_component(tw);
        sc[3] = rand_sc_component(th);
        int fix = rand() & 1;
        u32 out_new[4] = { 0, 0, 0, 0 }, out_old[4] = { 0, 0, 0, 0 };
        int rn = rsx_core_scissor_per_draw(sc, tw, th, fix, out_new);
        int ro = old_scissor(sc, tw, th, fix, out_old);
        if (rn != ro) { printf("scissor code mismatch i=%d\n", i); return 0; }
        if (rn == 0 && memcmp(out_new, out_old, sizeof out_new)) { printf("scissor out mismatch i=%d\n", i); return 0; }
    }
    return 1;
}

#if defined(__aarch64__)
static int alpha_ref_exhaustive(void)
{
    srand(66);
    const u32 formats[] = { 0x0Bu, 0x0Cu, 0x0Du, 0x08u, 0x03u };
    for (int fi = 0; fi < 5; fi++) {
        u32 sf = formats[fi];
        for (u32 lo = 0; lo < 65536u; lo++) {
            for (int hi = 0; hi < 8; hi++) {
                u32 high = (u32)rand() << 16;
                u32 rr = high | lo;
                float a = rsx_core_alpha_ref(sf, rr);
                float b = old_alpha_ref(sf, rr);
                if (!same_bits(a, b)) {
                    printf("alpha_ref mismatch sf=0x%02X rr=0x%08X\n", sf, rr);
                    return 0;
                }
            }
        }
    }
    return 1;
}
#endif

static int rt_dims_oracle(void)
{
    srand(44);
    for (int i = 0; i < 1000000; i++) {
        rsx_state s; fill_state_random(&s);
        s.surface_clip_w = rand_dim_bucket();
        s.surface_clip_h = rand_dim_bucket();
        s.viewport_w = rand_dim_bucket();
        s.viewport_h = rand_dim_bucket();
        u32 def_w = 1u + (u32)(rand() % 4096);
        u32 def_h = 1u + (u32)(rand() % 4096);
        u32 wn, hn, wo, ho;
        rsx_core_rt_dims(&s, def_w, def_h, &wn, &hn);
        old_rt_dims(&s, def_w, def_h, &wo, &ho);
        if (wn != wo || hn != ho) { printf("rt_dims mismatch i=%d\n", i); return 0; }
    }
    return 1;
}

static int mrt_targets_oracle(void)
{
    srand(55);
    for (u32 ct = 0; ct <= 0xFFu; ct++) {
        for (int mrt_on = 0; mrt_on <= 1; mrt_on++) {
            rsx_state s; memset(&s, 0, sizeof s);
            s.color_target = ct;
            for (int k = 0; k < 4; k++)
                s.surface_color_dma[k] = (rand() & 1) ? RSX_CONTEXT_DMA_MAIN : (u32)rand();
            int rtt_n[4], rtt_o[4], nrtt_n = -1, nrtt_o = -1;
            u32 cloc_n[4] = { 0 }, cloc_o[4] = { 0 };
            rsx_core_mrt_targets(&s, mrt_on, rtt_n, &nrtt_n, cloc_n);
            old_mrt_targets(&s, mrt_on, rtt_o, &nrtt_o, cloc_o);
            if (nrtt_n != nrtt_o || memcmp(rtt_n, rtt_o, sizeof rtt_n) || memcmp(cloc_n, cloc_o, sizeof cloc_n)) {
                printf("mrt mismatch ct=0x%02X mrt_on=%d\n", ct, mrt_on);
                return 0;
            }
        }
    }
    return 1;
}

int main(void)
{
    fails = 0;
    CHECK("fp_tex_ea_oracle", fp_tex_ea_oracle());
    CHECK("viewport_oracle", viewport_oracle());
    CHECK("scissor_oracle", scissor_oracle());
#if defined(__aarch64__)
    CHECK("alpha_ref_exhaustive", alpha_ref_exhaustive());
#endif
    CHECK("rt_dims_oracle", rt_dims_oracle());
    CHECK("mrt_targets_oracle", mrt_targets_oracle());
    if (fails) { printf("%d FAILURES\n", fails); return 1; }
    printf("ALL PASS\n");
    return 0;
}
