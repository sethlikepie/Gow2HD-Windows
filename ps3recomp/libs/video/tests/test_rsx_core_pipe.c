/* Oracle test: rsx_core_pipe.c vs verbatim old_* copies of the Metal bodies
 * it replaced (rsx_metal_backend.m, HEAD 918fd6f0 + Task 1/2's f3d555f2 /
 * 5f4998d4). Proves bit-for-bit equivalence over large randomized/exhaustive
 * domains instead of a handful of examples. See
 * .superpowers/sdd/2026-09-23-vulkan-v1a-rsx-core-pure/task-3-brief.md. */
#include "rsx_core_pipe.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------------------
 * old_*: verbatim copies of the Metal bodies being replaced, renamed only,
 * parameterised exactly as the task brief's old_* description asks (the
 * global s_pipe replaced by a pointer to an identically-laid-out struct,
 * and the env/debug reads -- metal_debug_disable_blend(),
 * metal_debug_force_depth_write(), rsx_depth_debug_mode(), s_mrt_n,
 * metal_atest_on() -- replaced by the same parameters the new API takes). */

typedef struct {
    int  blend_enable;
    u32  blend_sfactor, blend_dfactor, blend_equation;
    u32  blend_color;
    int  depth_test_enable;
    u32  depth_func;
    int  depth_mask;
    int  stencil_test_enable;
    u32  stencil_func, stencil_ref, stencil_mask;
    u32  stencil_op_fail, stencil_op_zfail, stencil_op_zpass;
    u32  stencil_write_mask;
    int  two_sided_stencil;
    u32  back_stencil_func, back_stencil_ref, back_stencil_mask, back_stencil_write_mask;
    u32  back_stencil_op_fail, back_stencil_op_zfail, back_stencil_op_zpass;
    u32  color_mask;
    int  alpha_test_enable;
    u32  alpha_func, alpha_ref;
    u32  scissor_x, scissor_y, scissor_w, scissor_h;
    int  has_scissor;
    u32  blend_key;
    u32  ds_key;
} OldPipeState;

typedef struct {
    u32 depth_test, depth_write_mask, depth_func, force_write, dmode;
    u32 stencil_en;
    u32 f_func, f_fail, f_zfail, f_zpass, f_rmask, f_wmask;
    u32 b_func, b_fail, b_zfail, b_zpass, b_rmask, b_wmask;
} OldDsDesc;

static u32 old_pipe_blend_key(const OldPipeState* s_pipe, int disable_blend)
{
    if (disable_blend)
        return 0u;
    u32 k = s_pipe->blend_enable ? 1u : 0u;
    k ^= s_pipe->blend_sfactor * 0x9E3779B1u;
    k ^= s_pipe->blend_dfactor * 0x85EBCA6Bu;
    k ^= s_pipe->blend_equation * 0xC2B2AE35u;
    k ^= s_pipe->color_mask;
    return k;
}

static void old_pipe_ds_desc(const OldPipeState* s_pipe, int force_write, u32 dmode, OldDsDesc* o)
{
    memset(o, 0, sizeof(*o));
    o->depth_test = s_pipe->depth_test_enable ? 1u : 0u;
    o->depth_write_mask = s_pipe->depth_mask ? 1u : 0u;
    o->depth_func = s_pipe->depth_func;
    o->force_write = force_write ? 1u : 0u;
    o->dmode = dmode;
    if (!s_pipe->stencil_test_enable) return;
    o->stencil_en = 1u;
    o->f_func = s_pipe->stencil_func;
    o->f_fail = s_pipe->stencil_op_fail;
    o->f_zfail = s_pipe->stencil_op_zfail;
    o->f_zpass = s_pipe->stencil_op_zpass;
    o->f_rmask = s_pipe->stencil_mask & 0xFFu;
    o->f_wmask = s_pipe->stencil_write_mask & 0xFFu;
    if (s_pipe->two_sided_stencil) {
        o->b_func = s_pipe->back_stencil_func;
        o->b_fail = s_pipe->back_stencil_op_fail;
        o->b_zfail = s_pipe->back_stencil_op_zfail;
        o->b_zpass = s_pipe->back_stencil_op_zpass;
        o->b_rmask = s_pipe->back_stencil_mask & 0xFFu;
        o->b_wmask = s_pipe->back_stencil_write_mask & 0xFFu;
    } else {
        o->b_func = o->f_func;   o->b_fail = o->f_fail;
        o->b_zfail = o->f_zfail; o->b_zpass = o->f_zpass;
        o->b_rmask = o->f_rmask; o->b_wmask = o->f_wmask;
    }
}

static u32 old_pipe_ds_key(const OldDsDesc* d)
{
    u32 h = 2166136261u;
    const unsigned char* b = (const unsigned char*)d;
    for (size_t i = 0; i < sizeof(*d); i++) { h ^= b[i]; h *= 16777619u; }
    return h;
}

static u32 old_pso_state_key(u32 blend_key, const OldPipeState* s_pipe, int mrt_n, int atest_on)
{
    return ((blend_key * 0x9E3779B1u) ^ s_pipe->color_mask) ^
           ((u32)(mrt_n - 1) * 0x51ED270Bu) ^
           ((s_pipe->alpha_test_enable && atest_on) ? 0xA17E57C5u : 0u);
}

static u32 old_sampler_key(u32 addr, u32 filt)
{
    return ((addr & 0xFu)) | (((addr >> 8) & 0xFu) << 4) | (((addr >> 16) & 0xFu) << 8) |
           ((((filt >> 16) & 0x7u)) << 12) | ((((filt >> 24) & 0x7u)) << 15);
}

static int old_min_filter_linear(u32 filt)
{
    u32 minf = (filt >> 16) & 0x7u;
    return (minf == 2u || minf == 4u || minf == 6u || minf == 7u) ? 1 : 0;
}

static int old_mag_filter_linear(u32 filt)
{
    u32 magf = (filt >> 24) & 0x7u;
    return (magf == 2u || magf == 4u) ? 1 : 0;
}

/* ---------------------------------------------------------------------------
 * Test harness. */

static int fails;
#define CHECK(name, cond) do { if (!(cond)) { printf("FAIL %s\n", name); fails++; } else printf("[PASS] %s\n", name); } while (0)

static void fill_pipe_random(OldPipeState* p)
{
    p->blend_enable = rand() & 1;
    p->blend_sfactor = ((u32)rand() << 16) ^ (u32)rand();
    p->blend_dfactor = ((u32)rand() << 16) ^ (u32)rand();
    p->blend_equation = ((u32)rand() << 16) ^ (u32)rand();
    p->blend_color = ((u32)rand() << 16) ^ (u32)rand();
    p->depth_test_enable = rand() & 1;
    p->depth_func = ((u32)rand() << 16) ^ (u32)rand();
    p->depth_mask = rand() & 1;
    p->stencil_test_enable = rand() & 1;
    p->stencil_func = ((u32)rand() << 16) ^ (u32)rand();
    p->stencil_ref = ((u32)rand() << 16) ^ (u32)rand();
    p->stencil_mask = ((u32)rand() << 16) ^ (u32)rand();
    p->stencil_op_fail = ((u32)rand() << 16) ^ (u32)rand();
    p->stencil_op_zfail = ((u32)rand() << 16) ^ (u32)rand();
    p->stencil_op_zpass = ((u32)rand() << 16) ^ (u32)rand();
    p->stencil_write_mask = ((u32)rand() << 16) ^ (u32)rand();
    p->two_sided_stencil = rand() & 1;
    p->back_stencil_func = ((u32)rand() << 16) ^ (u32)rand();
    p->back_stencil_ref = ((u32)rand() << 16) ^ (u32)rand();
    p->back_stencil_mask = ((u32)rand() << 16) ^ (u32)rand();
    p->back_stencil_write_mask = ((u32)rand() << 16) ^ (u32)rand();
    p->back_stencil_op_fail = ((u32)rand() << 16) ^ (u32)rand();
    p->back_stencil_op_zfail = ((u32)rand() << 16) ^ (u32)rand();
    p->back_stencil_op_zpass = ((u32)rand() << 16) ^ (u32)rand();
    switch (rand() % 4) { /* bias toward the real-world values (rsx_state/s_pipe reset, SET_COLOR_MASK). */
    case 0: p->color_mask = 0u; break;
    case 1: p->color_mask = 0x01010101u; break;
    case 2: p->color_mask = 0xFFFFFFFFu; break;
    default: p->color_mask = ((u32)rand() << 16) ^ (u32)rand(); break;
    }
    p->alpha_test_enable = rand() & 1;
    p->alpha_func = ((u32)rand() << 16) ^ (u32)rand();
    p->alpha_ref = ((u32)rand() << 16) ^ (u32)rand();
    p->scissor_x = (u32)rand();
    p->scissor_y = (u32)rand();
    p->scissor_w = (u32)rand();
    p->scissor_h = (u32)rand();
    p->has_scissor = rand() & 1;
    p->blend_key = 0;
    p->ds_key = 0;
}

/* 1M randomized pipes (every field random, booleans biased 50/50) x
 * disable_blend 0/1 x force_write 0/1 x dmode 0..3 x mrt_n 1..4 x atest 0/1:
 * keys and memcmp of the desc equal. OldPipeState/rsx_core_pipe have the
 * identical field layout the task requires, so a raw memcpy from one into
 * the other is exactly the cross-struct read the brief asks the oracle to
 * prove (verified once via the sizeof check below, then exercised by every
 * key/desc comparison that follows). */
static int pipe_oracle(void)
{
    if (sizeof(OldPipeState) != sizeof(rsx_core_pipe)) {
        printf("pipe_oracle: sizeof(OldPipeState)=%zu != sizeof(rsx_core_pipe)=%zu\n",
               sizeof(OldPipeState), sizeof(rsx_core_pipe));
        return 0;
    }
    if (sizeof(OldDsDesc) != sizeof(rsx_core_ds_desc)) {
        printf("pipe_oracle: sizeof(OldDsDesc)=%zu != sizeof(rsx_core_ds_desc)=%zu\n",
               sizeof(OldDsDesc), sizeof(rsx_core_ds_desc));
        return 0;
    }

    srand(99);
    for (int i = 0; i < 1000000; i++) {
        OldPipeState op;
        fill_pipe_random(&op);
        rsx_core_pipe np;
        memcpy(&np, &op, sizeof np);

        int disable_blend = rand() & 1;
        int force_write = rand() & 1;
        u32 dmode = (u32)(rand() % 4);
        int mrt_n = 1 + (int)(rand() % 4);
        int atest_on = rand() & 1;

        u32 bk_old = old_pipe_blend_key(&op, disable_blend);
        u32 bk_new = rsx_core_blend_key(&np, disable_blend);
        if (bk_old != bk_new) { printf("blend_key mismatch i=%d old=0x%08X new=0x%08X\n", i, bk_old, bk_new); return 0; }

        OldDsDesc do_desc;
        rsx_core_ds_desc dn_desc;
        old_pipe_ds_desc(&op, force_write, dmode, &do_desc);
        rsx_core_pipe_ds_desc(&np, force_write, dmode, &dn_desc);
        if (memcmp(&do_desc, &dn_desc, sizeof(do_desc)) != 0) {
            printf("ds_desc mismatch i=%d\n", i);
            return 0;
        }

        u32 dk_old = old_pipe_ds_key(&do_desc);
        u32 dk_new = rsx_core_ds_key(&dn_desc);
        if (dk_old != dk_new) { printf("ds_key mismatch i=%d old=0x%08X new=0x%08X\n", i, dk_old, dk_new); return 0; }

        u32 pk_old = old_pso_state_key(bk_old, &op, mrt_n, atest_on);
        u32 pk_new = rsx_core_pso_state_key(bk_new, &np, mrt_n, atest_on);
        if (pk_old != pk_new) { printf("pso_state_key mismatch i=%d old=0x%08X new=0x%08X\n", i, pk_old, pk_new); return 0; }
    }
    return 1;
}

static int blend_split_zero_alpha(void)
{
    rsx_core_blend_split o;
    memset(&o, 0, sizeof o);
    /* sf=ONE (rgb) / alpha-half ZERO, df=ONE_MINUS_SRC_ALPHA (rgb) / alpha-half
     * ZERO, eq=ADD (rgb) / alpha-half ZERO -- GoW2 scenery ONE/ONE_MINUS_SRC_ALPHA
     * with an unset high half. The old `?:` fallback bug copied the RGB
     * factor into a.; the split must keep the real ZERO. */
    rsx_core_split_blend(0x00000001u, 0x00000303u, 0x00008006u, &o);
    if (o.sf_a != 0u || o.df_a != 0u) {
        printf("blend_split_zero_alpha: sf_a=%u df_a=%u (want 0/0)\n", o.sf_a, o.df_a);
        return 0;
    }
    if (o.sf_rgb != 0x0001u || o.df_rgb != 0x0303u || o.eq_rgb != 0x8006u || o.eq_a != 0u) {
        printf("blend_split_zero_alpha: rgb/eq halves wrong (sf_rgb=0x%X df_rgb=0x%X eq_rgb=0x%X eq_a=%u)\n",
               o.sf_rgb, o.df_rgb, o.eq_rgb, o.eq_a);
        return 0;
    }
    return 1;
}

static int color_mask_table(void)
{
    struct { u32 m; u32 want; } cases[] = {
        { 0x00000000u, 0u },
        { 0x01010101u, (u32)RSX_CM_ALL },
        { 0xFFFFFFFFu, (u32)RSX_CM_ALL },
        { 0x00000001u, (u32)RSX_CM_B },
        { 0x01000000u, (u32)RSX_CM_A },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        u32 got = rsx_core_map_color_mask(cases[i].m);
        if (got != cases[i].want) {
            printf("color_mask_table mismatch m=0x%08X got=%u want=%u\n", cases[i].m, got, cases[i].want);
            return 0;
        }
    }
    return 1;
}

/* sampler_key/filter predicates only read addr's three low nibbles (bits
 * 0-3, 8-11, 16-19) and filt's two 3-bit fields (bits 16-18, 24-26), so an
 * exhaustive sweep of filt's full high halfword (both high bytes, all
 * 65536 values -- "all filt high bytes") crossed with a handful of random
 * addr per value, PLUS an exhaustive sweep of the 4096 addr-nibble
 * combinations (16^3) that actually move the key crossed with a random
 * filt, together cover every input either function can observe without a
 * ~268M-iteration full cross product. */
static int sampler_key_oracle(void)
{
    srand(111);
    for (u32 filt_hi = 0; filt_hi <= 0xFFFFu; filt_hi++) {
        u32 filt = filt_hi << 16;
        for (int k = 0; k < 8; k++) {
            u32 addr = (u32)rand();
            u32 key_new = rsx_core_sampler_key(addr, filt);
            u32 key_old = old_sampler_key(addr, filt);
            if (key_new != key_old) {
                printf("sampler_key mismatch filt=0x%08X addr=0x%08X old=0x%08X new=0x%08X\n",
                       filt, addr, key_old, key_new);
                return 0;
            }
            int minl_new = rsx_core_min_filter_linear(filt), minl_old = old_min_filter_linear(filt);
            int magl_new = rsx_core_mag_filter_linear(filt), magl_old = old_mag_filter_linear(filt);
            if (minl_new != minl_old || magl_new != magl_old) {
                printf("sampler_filter mismatch filt=0x%08X minl(old=%d new=%d) magl(old=%d new=%d)\n",
                       filt, minl_old, minl_new, magl_old, magl_new);
                return 0;
            }
        }
    }
    for (u32 an = 0; an < 4096u; an++) {
        u32 addr = (an & 0xFu) | (((an >> 4) & 0xFu) << 8) | (((an >> 8) & 0xFu) << 16);
        u32 filt = (u32)rand();
        u32 key_new = rsx_core_sampler_key(addr, filt);
        u32 key_old = old_sampler_key(addr, filt);
        if (key_new != key_old) {
            printf("sampler_key(addr) mismatch addr=0x%08X filt=0x%08X old=0x%08X new=0x%08X\n",
                   addr, filt, key_old, key_new);
            return 0;
        }
    }
    return 1;
}

int main(void)
{
    fails = 0;
    CHECK("pipe_oracle", pipe_oracle());
    CHECK("blend_split_zero_alpha", blend_split_zero_alpha());
    CHECK("color_mask_table", color_mask_table());
    CHECK("sampler_key_oracle", sampler_key_oracle());
    if (fails) { printf("%d FAILURES\n", fails); return 1; }
    printf("ALL PASS\n");
    return 0;
}
