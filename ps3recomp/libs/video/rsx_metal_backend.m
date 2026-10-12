/*
 * ps3recomp - Metal RSX backend (macOS / Apple Silicon)
 *
 * See rsx_metal_backend.h for scope. This file is Objective-C because Metal
 * has no C API; it is the only non-C translation unit in the runtime and is
 * compiled only on Apple platforms.
 */

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <CoreGraphics/CoreGraphics.h>
#import <CoreVideo/CoreVideo.h>
#import <CoreVideo/CVMetalTextureCache.h>

#include <errno.h>
#include <stdarg.h>
#include <sys/resource.h>   /* getrusage: trabalho de CPU por frame (E478) */
#include <mach/mach_time.h>
#include "rsx_metal_backend.h"
#include "rsx_capture.h"
#include <os/lock.h>
static double s_perf_present_ms_sum = 0.0, s_perf_present_ms_max = 0.0;   /* PS3_TRACE_FPS */
/* Where a gameplay frame goes, for PS3_TRACE_FPS: the GPU time Metal reports for the last
 * present, and the guest FIFO decode time the GCM walker accumulates (gcm_perf_take_dec_ms).
 * A perf branch measured only on the intro could not see either. */
static double s_perf_gpu_ms_last = 0.0;
static os_unfair_lock s_perf_lock = OS_UNFAIR_LOCK_INIT;
/* Window title set by a present that ran off the main thread (the GCM RSX wake thread
 * presents too): AppKit only allows it on the main thread, so the event pump applies it. */
static os_unfair_lock s_title_lock = OS_UNFAIR_LOCK_INIT;
static char s_title_pending[256];
/* Window title base: "<game title> — Metal"; the per-second FPS title keeps it
 * as its prefix so the graphics API in use is always visible. */
static char s_base_title[160];
static int s_title_has_pending;   /* under s_title_lock */
#include "rsx_host_content.h"
#include "spu_put_map.h"
#include "rsx_frame_dump.h"
#include "rsx_primitives.h"
#include "rsx_fp_decompiler.h"
#include "rsx_vp_decompiler.h"
#include "rsx_depth_debug.h"
#include "rsx_commands.h"
#include "rsx_core_decode.h"
#include "rsx_core_rules.h"
#include "rsx_core_pipe.h"
#include "rsx_dispatch.h"
#include "rsx_texture_formats.h"
#include "rsx_texture_deswizzle.h"
#include "rsx_metal_deswizzle.h"   /* GPU (compute) deswizzle, gated -- byte-exato vs CPU */
#include "rsx_metal_upscale.h"     /* MetalFX spatial upscale (present), gated PS3_METALFX */
#include "rsx_game_identity.h"     /* nome/icone da janela a partir do PARAM.SFO/ICON0.PNG */
#include "rsx_vertex_formats.h"
#include "rsx_index.h"
#include "rsx_rt_feedback.h"
#include "rsx_ucode_dump.h"
#include "host_prompt_overlay.h"
#include "host_prompt_mesh.h"
#include "host_prompts.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_metal.h>

/* Runtime overlay: shared core API, the Metal UI renderer and the pure
 * SDL-input / frame-timing helpers. */
#include "rsx_overlay.h"
#include "rsx_overlay_settings.h"
#include "rsx_overlay_metal.h"
#include "rsx_overlay_frame_timer.h"
#include "rsx_overlay_sdl_input.h"
#include "rsx_metal_draw_stats.h"
#include "rsx_overlay_log.h"
#include "rsx_overlay_assets.h"
#include "rsx_overlay_feedback_apple.h"
#include "cellpad_gamecontroller.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <unistd.h>

extern uint8_t* vm_base;

/* Runtime overlay UI pass (NULL = unavailable; the game renders as before).
 * The renderer only reads the core's immutable packet, so both the guest flip
 * and the movie decode thread may composite it. */
static rsx_overlay_metal_renderer* s_overlay_ui;
/* Guest-flip timing for the overlay's backend metrics; written only by
 * metal_end_frame (the single metrics publisher). */
static rsx_overlay_frame_timer s_overlay_timer;
static uint64_t s_overlay_guest_draws_total;
/* Last graphics settings applied to the window/layer (event thread only). */
static rsx_overlay_graphics_settings s_overlay_graphics;
static int s_overlay_graphics_known;
extern int ppu_guest_range_committed(uint32_t addr, uint32_t n);

/* Fixed-function vertex (matches D3D12 BasicVertex: 28 bytes). */
typedef struct { float x, y, z, r, g, b, a; } MetalBasicVertex;

/* E444: a GoW2 3D frame records more than 256 draws; everything past the cap
 * was dropped silently, including the frame's composite, which comes last.
 * s_draw_dropped counts what still does not fit (per frame, [PERDRAW]).
 * E456: a 3D frame needs more than 262144 vertices (442 draws dropped at
 * f=37500, the vertex buffer full). */
#define METAL_MAX_VERTS   1048576u
#define METAL_MAX_INDICES 3145728u  /* E486: 3x -- pico medido 491 558/frame, 3 regioes */
#define METAL_MAX_DRAWS   4096u
/* E486: regioes do ring por frame; ver o bloco junto a METAL_VA_RING_F4. */
#define METAL_FRAMES_IN_FLIGHT 3u
static u32 s_draw_dropped;
/* Framebuffer-feedback draws in the frame (a draw sampling its own colour target),
 * reset in metal_begin_frame; PS3_TRACE_METAL_FEEDBACK=<n> prints the first n. */
static u64 s_feedback_draws;
/* Draws replayed with the fixed fallback PSO instead of the pair compiled from the title's
 * own vertex/fragment programs: those come out with the wrong shading. Per frame. */
static u64 s_fallback_pso_draws;
/* Distinct guest colour write masks in the frame, with how many draws used each. */
unsigned long long g_va_skip_n, g_va_done_n;   /* E482 */
static u32 s_cmask_seen[8], s_cmask_cnt[8], s_cmask_n;

/* E463: um scissor que NAO intersecta o render target deve descartar o draw.
 * O codigo fazia "if (sx >= tw) sx = 0", que MOVE o rectangulo para a origem --
 * o jogo pede para nao pintar nada e nos pintamos o alvo inteiro. A segunda
 * copia era pior: "if (sw == 0) sw = rtw" transformava um scissor VAZIO em
 * largura total. PS3_METAL_SCISSOR_FIX=0 repoe o comportamento antigo (A/B). */
static u32 s_scissor_reject;
static int metal_scissor_fix(void)
{
    static int on = -1;
    if (on < 0) { const char* e = getenv("PS3_METAL_SCISSOR_FIX"); on = (e && e[0] == '0') ? 0 : 1; }
    return on;
}
u32 rsx_metal_scissor_rejects(void) { return s_scissor_reject; }
static u32 s_feedback_log_n;
/* Per-frame capacity counters (PS3_TRACE_METAL_PASSES): draws whose attribute
 * fill hit METAL_VA_RING_F4, vertex uploads truncated at METAL_MAX_VERTS. */
static u32 s_va_ovf_draws, s_vb_trunc_draws;
/* Draws that had to reuse another draw's vertex-constant snapshot (ring full). */
static u32 s_vc_ring_full_draws;
/* Per-frame geometry draws (arrays+indexed). Reset in metal_begin_frame.
 * PS3_METAL_MAX_DRAWS=N keeps the first N; PS3_METAL_DUMP_DRAW=N logs that id. */
static u32 s_frame_geom;
static int metal_max_draws(void);
static u32 metal_dump_draw_id(void);

/* E458 A/B gates (diagnostic, OFF = current behaviour): the M6 binaries take a
 * different intro path than E455's (AU #1 128 B instead of 19 401 B, VT err=8,
 * st620 1->10) even with PS3_METAL_VA=0. PS3_METAL_MAXV_OLD=1 restores the
 * 262 144-vertex buffer, PS3_METAL_VA_NOBIND=1 skips the buffer(2)/(3) binds. */
static int metal_env_on(const char* name)
{
    /* `getenv` percorre o ambiente inteiro sob um lock (`__findenv_locked`), e
     * isto e' chamado por DRAW -- e nalguns sitios por quad. Memoriza por
     * nome; os nomes sao literais, por isso o ponteiro basta quase sempre.
     * Duas threads a inserir a mesma entrada escrevem o mesmo valor. */
    enum { METAL_ENV_CACHE_N = 64 };
    static struct { const char* name; int on; } cache[METAL_ENV_CACHE_N];
    static unsigned cached;
    const unsigned n = cached;
    for (unsigned i = 0; i < n; i++)
        if (cache[i].name == name || strcmp(cache[i].name, name) == 0)
            return cache[i].on;
    const char* e = getenv(name);
    const int on = e && e[0] == '1';
    if (n < METAL_ENV_CACHE_N) {
        cache[n].name = name;
        cache[n].on = on;
        cached = n + 1u;
    }
    return on;
}
/* Unset or empty -> def; "0" off; anything else on. Cached by name pointer. */
static int metal_env_default(const char* name, int def)
{
    enum { N = 32 };
    static struct { const char* name; int on; } cache[N];
    static unsigned cached;
    for (unsigned i = 0; i < cached; i++)
        if (cache[i].name == name)
            return cache[i].on;
    const char* e = getenv(name);
    const int on = (!e || !e[0]) ? def : (e[0] != '0');
    if (cached < N) {
        cache[cached].name = name;
        cache[cached].on = on;
        cached++;
    }
    return on;
}
static u32 metal_max_verts(void)
{
    static u32 v = 0;
    if (!v) v = metal_env_on("PS3_METAL_MAXV_OLD") ? 262144u : METAL_MAX_VERTS;
    return v;
}

/* ---------------------------------------------------------------------------
 * State
 * -----------------------------------------------------------------------*/

typedef struct {
    SDL_Window*   window;
    SDL_MetalView view;

    /* __unsafe_unretained on purpose: ownership is manual via CFRetain in init
     * and CFRelease in shutdown. Leaving these __strong would make ARC release
     * them again on assignment, over-releasing every object here, and would
     * make the memset below skip ARC's bookkeeping entirely. */
    __unsafe_unretained CAMetalLayer*       layer;
    __unsafe_unretained id<MTLDevice>       device;
    __unsafe_unretained id<MTLCommandQueue> queue;

    u32 width;
    u32 height;

    double clear_r, clear_g, clear_b;
    double clear_a;
    float  clear_depth;
    u8     clear_stencil;
    u32    clear_flags;   /* RSX_CLEAR_* pending for next present */

    u64 frame_count;
    u64 last_fps_ticks;
    u64 last_fps_frame;
    u32 fps;

    int window_closed;
    int dump_frames;
    int force_dump_once; /* H-M1: dump next present regardless of stride */
} MetalBackendState;

static MetalBackendState s_state;

typedef struct {
    __unsafe_unretained id<MTLTexture> tex;
    __unsafe_unretained id<MTLRenderPipelineState> pso;
    __unsafe_unretained id<MTLSamplerState> sampler;
    u32 w, h;
    int ready;
} MetalPromptOverlay;
static MetalPromptOverlay s_prompt_overlay;

/* Timeline probe (Priority 1, 2026-07-22): correlate frame index with
 * CONTENT / set_shader / bind / movie FSM. Gated PS3_TRACE_TIMELINE. */
static int metal_tl_on(void)
{
    static int v = -1;
    if (v < 0) {
        const char* e = getenv("PS3_TRACE_TIMELINE");
        v = (e && e[0] && e[0] != '0') ? 1 : 0;
    }
    return v;
}
#define METAL_TL(...) do { if (metal_tl_on()) { \
    fprintf(stderr, "[TL] f=%llu ", (unsigned long long)s_state.frame_count); \
    fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); fflush(stderr); \
} } while (0)

/* M5 — RSX pipeline state snapshot applied on present/draw. Struct moved
 * verbatim to rsx_core_pipe (rsx_core_pipe); same field order/types kept
 * here under the old name so every other caller in this file compiles
 * unchanged. */
typedef rsx_core_pipe MetalPipeState;
static MetalPipeState s_pipe;

#define METAL_MAX_DS_CACHE 16u
/* Everything a MTLDepthStencilState is built from. Compared whole (memcmp):
 * the old 32-bit XOR key could alias two states once back-face stencil joined.
 * Struct moved verbatim to rsx_core_pipe (rsx_core_ds_desc). */
typedef rsx_core_ds_desc MetalDsDesc;
typedef struct {
    MetalDsDesc desc;
    __unsafe_unretained id<MTLDepthStencilState> state;
} MetalDsCacheEntry;

static MetalDsCacheEntry s_ds_cache[METAL_MAX_DS_CACHE];
static u32 s_ds_cache_count;
static __unsafe_unretained id<MTLTexture> s_depth_tex;
static u32 s_depth_tex_w, s_depth_tex_h;

/* M7 — offscreen color/depth surfaces keyed by (loc, offset). */
/* 16 filled up on GoW2's front-end (12 distinct surfaces logged in E439), and an
 * eviction hands a recorded draw's index to another surface. */
#define METAL_MAX_SURFACES 64u
typedef struct {
    u32 loc;     /* 0 = RSX local, 1 = main (key only for MVP) */
    u32 offset;
    u32 w, h;
    int is_depth;
    int inited;  /* per-draw mode: first pass on a fresh texture clears it */
    int recreated; /* tex allocated since last TRACE_FLIP consume of this slot */
    u32 guest_fmt; /* NV4097_SET_SURFACE_FORMAT last used with this colour target */
    __unsafe_unretained id<MTLTexture> tex;
    /* Depth surface sampled as a colour texture (metal_depth_color_view): BGRA8
     * copy holding the D24 bytes, converted when wserial (passes that attached
     * this depth) has moved since conv_serial. */
    __unsafe_unretained id<MTLTexture> conv;
    u32 wserial, conv_serial;
} MetalSurface;
static MetalSurface s_surfaces[METAL_MAX_SURFACES];
static u32 s_n_surfaces;
static int s_color_rt_idx = -1; /* index into s_surfaces, -1 → default */
/* MRT (SET_SURFACE_COLOR_TARGET): alvos de cor ligados, como o get_rtt_indexes
 * do RPCS3. s_mrt_idx[0] == s_color_rt_idx; [1..n-1] sao os extras. O GoW2
 * desenha a cena do menu em A+B (0x13) e a composicao final le' B -- so' com o
 * alvo A o B ficava por escrever e a composicao lia zeros (a piscada do menu).
 * PS3_METAL_MRT=0 repoe o comportamento antigo (so' o alvo A). */
static int s_mrt_n = 1;
static int s_mrt_idx[4] = { -1, -1, -1, -1 };
static int metal_mrt_on(void)
{
    static int on = -1;
    if (on < 0) { const char* e = getenv("PS3_METAL_MRT"); on = (e && e[0] == '0') ? 0 : 1; }
    return on;
}
static int s_rt_req[16], s_rt_req_n = 0; static u32 s_rt_req_calls = 0;   /* PS3_TRACE_METAL_SHOW */
static int s_depth_rt_idx = -1;
static u32 s_rt_selects;
static u32 s_rt_creates;

static int metal_trace_state(void)
{
    static int v = -1;
    if (v < 0) {
        const char* e = getenv("PS3_TRACE_RSX_STATE");
        v = (e && e[0] && e[0] != '0') ? 1 : 0;
    }
    return v;
}

static int metal_debug_disable_blend(void)
{
    static int on = -1;
    if (on < 0) {
        const char* e = getenv("PS3_METAL_DEBUG_DISABLE_BLEND");
        on = (e && *e && *e != '0') ? 1 : 0;
    }
    return on;
}

static int metal_debug_force_depth_write(void)
{
    static int on = -1;
    if (on < 0) {
        const char* e = getenv("PS3_METAL_DEBUG_FORCE_DEPTH_WRITE");
        on = (e && *e && *e != '0') ? 1 : 0;
    }
    return on;
}

/* Body moved to rsx_core_pipe.c (rsx_core_blend_key); forwarder kept under
 * the old name for its many callers in this file. */
static u32 metal_pipe_blend_key(void)
{
    return rsx_core_blend_key(&s_pipe, metal_debug_disable_blend());
}

/* RSX stencil: with TWO_SIDED_STENCIL_TEST_ENABLE off both faces use the front
 * parameters; on, back faces use the BACK_STENCIL_* set (z-fail shadow volumes
 * increment on one face and decrement on the other). Body moved to
 * rsx_core_pipe.c (rsx_core_pipe_ds_desc). */
static void metal_pipe_ds_desc(MetalDsDesc* o)
{
    rsx_core_pipe_ds_desc(&s_pipe, metal_debug_force_depth_write(), (u32)rsx_depth_debug_mode(), o);
}

/* Body moved to rsx_core_pipe.c (rsx_core_ds_key). */
static u32 metal_pipe_ds_key(void)
{
    MetalDsDesc d;
    metal_pipe_ds_desc(&d);
    return rsx_core_ds_key(&d);
}

/* GL/NV compare enums used by GCM (0x0200..). Neutral mapping moved to
 * rsx_core_pipe.c (rsx_core_map_cmp); this is now a thin switch back to the
 * MTL type. */
static MTLCompareFunction metal_map_cmp(u32 f)
{
    switch (rsx_core_map_cmp(f)) {
    case RSX_CMP_NEVER:    return MTLCompareFunctionNever;
    case RSX_CMP_LESS:     return MTLCompareFunctionLess;
    case RSX_CMP_EQUAL:    return MTLCompareFunctionEqual;
    case RSX_CMP_LEQUAL:   return MTLCompareFunctionLessEqual;
    case RSX_CMP_GREATER:  return MTLCompareFunctionGreater;
    case RSX_CMP_NOTEQUAL: return MTLCompareFunctionNotEqual;
    case RSX_CMP_GEQUAL:   return MTLCompareFunctionGreaterEqual;
    case RSX_CMP_ALWAYS:   return MTLCompareFunctionAlways;
    default:               return MTLCompareFunctionLessEqual;
    }
}

static MTLBlendFactor metal_map_blend_factor(u32 f, int alpha)
{
    switch (rsx_core_map_blend_factor(f, alpha)) {
    case RSX_BF_ZERO:                  return MTLBlendFactorZero;
    case RSX_BF_ONE:                   return MTLBlendFactorOne;
    case RSX_BF_SRC_COLOR:             return MTLBlendFactorSourceColor;
    case RSX_BF_ONE_MINUS_SRC_COLOR:   return MTLBlendFactorOneMinusSourceColor;
    case RSX_BF_SRC_ALPHA:             return MTLBlendFactorSourceAlpha;
    case RSX_BF_ONE_MINUS_SRC_ALPHA:   return MTLBlendFactorOneMinusSourceAlpha;
    case RSX_BF_DST_ALPHA:             return MTLBlendFactorDestinationAlpha;
    case RSX_BF_ONE_MINUS_DST_ALPHA:   return MTLBlendFactorOneMinusDestinationAlpha;
    case RSX_BF_DST_COLOR:             return MTLBlendFactorDestinationColor;
    case RSX_BF_ONE_MINUS_DST_COLOR:   return MTLBlendFactorOneMinusDestinationColor;
    case RSX_BF_SRC_ALPHA_SAT:         return MTLBlendFactorSourceAlphaSaturated;
    case RSX_BF_CONST_COLOR:           return MTLBlendFactorBlendColor;
    case RSX_BF_ONE_MINUS_CONST_COLOR: return MTLBlendFactorOneMinusBlendColor;
    default:                           return MTLBlendFactorOne;
    }
}

static MTLBlendOperation metal_map_blend_op(u32 e)
{
    switch (rsx_core_map_blend_op(e)) {
    case RSX_BOP_ADD:     return MTLBlendOperationAdd;
    case RSX_BOP_MIN:     return MTLBlendOperationMin;
    case RSX_BOP_MAX:     return MTLBlendOperationMax;
    case RSX_BOP_SUB:     return MTLBlendOperationSubtract;
    case RSX_BOP_REV_SUB: return MTLBlendOperationReverseSubtract;
    default:              return MTLBlendOperationAdd;
    }
}

static MTLStencilOperation metal_map_stencil_op(u32 op)
{
    /* CELL_GCM_* values (RPCS3 gcm_enums.h). The old table was shifted by one
     * (REPLACE->Zero, INCR->Replace, DECR->IncrementClamp, INVERT at 0x1505). */
    switch (rsx_core_map_stencil_op(op)) {
    case RSX_SOP_ZERO:       return MTLStencilOperationZero;       /* CELL_GCM_ZERO */
    case RSX_SOP_KEEP:       return MTLStencilOperationKeep;
    case RSX_SOP_REPLACE:    return MTLStencilOperationReplace;
    case RSX_SOP_INCR_CLAMP: return MTLStencilOperationIncrementClamp;
    case RSX_SOP_DECR_CLAMP: return MTLStencilOperationDecrementClamp;
    case RSX_SOP_INVERT:     return MTLStencilOperationInvert;
    case RSX_SOP_INCR_WRAP:  return MTLStencilOperationIncrementWrap;
    case RSX_SOP_DECR_WRAP:  return MTLStencilOperationDecrementWrap;
    default:                 return MTLStencilOperationKeep;
    }
}

/* color_mask: bit0 B, bit8 G, bit16 R, bit24 A. Rules moved to
 * rsx_core_pipe.c (rsx_core_map_color_mask); this just OR's the RSX_CM_*
 * bits back into MTLColorWriteMask. */
static MTLColorWriteMask metal_map_color_mask(u32 m)
{
    u32 c = rsx_core_map_color_mask(m);
    MTLColorWriteMask w = MTLColorWriteMaskNone;
    if (c & RSX_CM_B) w |= MTLColorWriteMaskBlue;
    if (c & RSX_CM_G) w |= MTLColorWriteMaskGreen;
    if (c & RSX_CM_R) w |= MTLColorWriteMaskRed;
    if (c & RSX_CM_A) w |= MTLColorWriteMaskAlpha;
    return w;
}

static void metal_set_encoder_blend_color(id<MTLRenderCommandEncoder> enc)
{
    if (!enc) return;
    u32 c = s_pipe.blend_color;
    [enc setBlendColorRed:((c >> 16) & 0xFFu) / 255.0f
                    green:((c >>  8) & 0xFFu) / 255.0f
                     blue:( c        & 0xFFu) / 255.0f
                    alpha:((c >> 24) & 0xFFu) / 255.0f];
}

static void metal_apply_blend_to_attachment(MTLRenderPipelineColorAttachmentDescriptor* att)
{
    if (!att) return;
    att.pixelFormat = MTLPixelFormatBGRA8Unorm;
    /* A zero mask is a real guest state (GoW2's stencil-only shadow/light volumes,
     * FP BFA6689B, blend ONE/ONE): turning it into "all channels" drew the
     * extruded volumes as translucent white slabs around the characters' feet and
     * light beams with stepped edges (2026-09-16). rsx_state and s_pipe both reset
     * to 0x01010101, so 0 only comes from SET_COLOR_MASK. */
    att.writeMask = metal_map_color_mask(s_pipe.color_mask);
    if (metal_debug_disable_blend()) {
        att.blendingEnabled = NO;
        return;
    }
    if (s_pipe.blend_enable) {
        u32 sf = s_pipe.blend_sfactor;
        u32 df = s_pipe.blend_dfactor;
        u32 eq = s_pipe.blend_equation;
        /* NV30_3D_BLEND_FUNC_{SRC,DST}: RGB in bits 0..15, ALPHA in 16..31
         * (cellGcmSetBlendFunc packs (alpha << 16) | colour). CELL_GCM_ZERO is
         * 0, so an empty high half is a REAL "alpha factor = ZERO", not an
         * unset field -- the old `?:` fallback copied the RGB factor there and
         * turned GoW2's ONE/ONE_MINUS_SRC_ALPHA scenery draws (sf=0x00000001,
         * df=0x00000303) into dst.a = 1, flooding the bright-pass mask.
         * The blend EQUATION has no valid 0, and metal_map_blend_op already
         * falls back to ADD, so it needs no special case either. */
        rsx_core_blend_split sp;
        rsx_core_split_blend(sf, df, eq, &sp);
        att.blendingEnabled = YES;
        att.sourceRGBBlendFactor = metal_map_blend_factor(sp.sf_rgb, 0);
        att.destinationRGBBlendFactor = metal_map_blend_factor(sp.df_rgb, 0);
        att.rgbBlendOperation = metal_map_blend_op(sp.eq_rgb);
        att.sourceAlphaBlendFactor = metal_map_blend_factor(sp.sf_a, 1);
        att.destinationAlphaBlendFactor = metal_map_blend_factor(sp.df_a, 1);
        att.alphaBlendOperation = metal_map_blend_op(sp.eq_a);
    } else {
        att.blendingEnabled = NO;
    }
}

static void metal_invalidate_blend_psos(void); /* after s_draw */

static int metal_ensure_depth_tex(u32 w, u32 h)
{
    if (w == 0 || h == 0 || !s_state.device) return 0;
    if (s_depth_tex && s_depth_tex_w == w && s_depth_tex_h == h) return 1;
    if (s_depth_tex) {
        CFRelease((__bridge CFTypeRef)s_depth_tex);
        s_depth_tex = nil;
    }
    MTLTextureDescriptor* td =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float_Stencil8
                                                           width:w height:h mipmapped:NO];
    td.usage = MTLTextureUsageRenderTarget;
    td.storageMode = MTLStorageModePrivate;
    id<MTLTexture> t = [s_state.device newTextureWithDescriptor:td];
    if (!t) {
        printf("[RSX metal] depth texture alloc failed %ux%u\n", w, h);
        return 0;
    }
    s_depth_tex = (__bridge id<MTLTexture>)CFBridgingRetain(t);
    s_depth_tex_w = w;
    s_depth_tex_h = h;
    return 1;
}

static id<MTLDepthStencilState> metal_depth_stencil_state(void)
{
    MetalDsDesc want;
    metal_pipe_ds_desc(&want);
    for (u32 i = 0; i < s_ds_cache_count; i++) {
        if (s_ds_cache[i].state && memcmp(&s_ds_cache[i].desc, &want, sizeof(want)) == 0)
            return s_ds_cache[i].state;
    }
    if (!s_state.device) return nil;

    MTLDepthStencilDescriptor* d = [MTLDepthStencilDescriptor new];
    int dmode = rsx_depth_debug_mode();
    u32 gcm_cmp = rsx_depth_effective_func(s_pipe.depth_func,
                                          s_pipe.depth_test_enable, dmode);
    d.depthCompareFunction = metal_map_cmp(gcm_cmp);
    d.depthWriteEnabled = rsx_depth_effective_write(s_pipe.depth_mask,
                                                    s_pipe.depth_test_enable,
                                                    dmode) ? YES : NO;
    if (metal_debug_force_depth_write())
        d.depthWriteEnabled = YES;
    if (want.stencil_en) {
        MTLStencilDescriptor* fs = [MTLStencilDescriptor new];
        fs.stencilCompareFunction = metal_map_cmp(want.f_func ? want.f_func : 0x0207);
        fs.stencilFailureOperation = metal_map_stencil_op(want.f_fail);
        fs.depthFailureOperation = metal_map_stencil_op(want.f_zfail);
        fs.depthStencilPassOperation = metal_map_stencil_op(want.f_zpass);
        fs.readMask = want.f_rmask;
        fs.writeMask = want.f_wmask;
        MTLStencilDescriptor* bs = [MTLStencilDescriptor new];
        bs.stencilCompareFunction = metal_map_cmp(want.b_func ? want.b_func : 0x0207);
        bs.stencilFailureOperation = metal_map_stencil_op(want.b_fail);
        bs.depthFailureOperation = metal_map_stencil_op(want.b_zfail);
        bs.depthStencilPassOperation = metal_map_stencil_op(want.b_zpass);
        bs.readMask = want.b_rmask;
        bs.writeMask = want.b_wmask;
        d.frontFaceStencil = fs;
        d.backFaceStencil = bs;
    }
    id<MTLDepthStencilState> st =
        [s_state.device newDepthStencilStateWithDescriptor:d];
    if (!st) return nil;

    if (s_ds_cache_count >= METAL_MAX_DS_CACHE) {
        if (s_ds_cache[0].state) CFRelease((__bridge CFTypeRef)s_ds_cache[0].state);
        memmove(&s_ds_cache[0], &s_ds_cache[1],
                (METAL_MAX_DS_CACHE - 1) * sizeof(MetalDsCacheEntry));
        s_ds_cache_count--;
    }
    MetalDsCacheEntry* slot = &s_ds_cache[s_ds_cache_count++];
    slot->desc = want;
    slot->state = (__bridge id<MTLDepthStencilState>)CFBridgingRetain(st);
    s_pipe.ds_key = metal_pipe_ds_key();
    return slot->state;
}

static void metal_pipe_init_defaults(void)
{
    memset(&s_pipe, 0, sizeof(s_pipe));
    s_pipe.blend_enable = 0;
    /* Packed (alpha << 16) | rgb, like the FIFO register. */
    s_pipe.blend_sfactor = 0x00010001;  /* ONE  / ONE  */
    s_pipe.blend_dfactor = 0x00000000;  /* ZERO / ZERO */
    s_pipe.blend_equation = 0x80068006; /* ADD  / ADD  */
    s_pipe.depth_test_enable = 0;
    s_pipe.depth_func = 0x0203; /* LEQUAL */
    s_pipe.depth_mask = 1;
    s_pipe.color_mask = 0x01010101u;
    s_pipe.scissor_w = 4096;
    s_pipe.scissor_h = 4096;
    s_pipe.blend_key = metal_pipe_blend_key();
    s_pipe.ds_key = metal_pipe_ds_key();
}

/* Guest clip can be garbage before init; keep display-sized RTs.
 * Body moved to rsx_core_rules.c (rsx_core_clamp_dim); forwarder kept under
 * the old name for its remaining caller in metal_surface_get. */
static u32 metal_clamp_dim(u32 v, u32 def) { return rsx_core_clamp_dim(v, def); }

static int metal_surface_find(u32 loc, u32 offset, int is_depth)
{
    for (u32 i = 0; i < s_n_surfaces; i++) {
        if (s_surfaces[i].loc == loc && s_surfaces[i].offset == offset &&
            s_surfaces[i].is_depth == is_depth)
            return (int)i;
    }
    return -1;
}

/* Get or create a Metal RT/DS texture for an RSX surface offset. Returns index. */
/* PS3_METAL_PER_DRAW_RT=1 (OFF by default until measured): replay each draw into
 * the surface it targeted, with its own viewport/scissor/textures/depth state,
 * instead of the single-pass path that folds every pass of a frame onto one
 * target (E439: shadow/downsample passes landed on the drawable). */
static int metal_per_draw_rt(void)
{
    static int on = -1;
    if (on < 0) {
        const char* e = getenv("PS3_METAL_PER_DRAW_RT");
        on = (e && *e && *e != '0') ? 1 : 0;
    }
    return on;
}

static int metal_surface_get(u32 loc, u32 offset, u32 w, u32 h, int is_depth)
{
    u32 def_w = s_state.width ? s_state.width : 1280u;
    u32 def_h = s_state.height ? s_state.height : 720u;
    w = metal_clamp_dim(w, def_w);
    h = metal_clamp_dim(h, def_h);
    if (!s_state.device) return -1;

    int idx = metal_surface_find(loc, offset, is_depth);
    if (idx >= 0 && s_surfaces[idx].tex &&
        s_surfaces[idx].w == w && s_surfaces[idx].h == h)
        return idx;
    /* Per-draw mode: a surface is guest memory, so a smaller pass on the same
     * offset draws into the top-left of the existing texture, as on the RSX,
     * instead of destroying it. Only ever grow. */
    if (metal_per_draw_rt() && idx >= 0 && s_surfaces[idx].tex) {
        if (s_surfaces[idx].w >= w && s_surfaces[idx].h >= h) return idx;
        if (s_surfaces[idx].w > w) w = s_surfaces[idx].w;
        if (s_surfaces[idx].h > h) h = s_surfaces[idx].h;
    }

    if (idx < 0) {
        if (s_n_surfaces >= METAL_MAX_SURFACES) {
            /* Overwrite oldest non-current entry. */
            idx = 0;
            for (u32 i = 0; i < s_n_surfaces; i++) {
                if ((int)i != s_color_rt_idx && (int)i != s_depth_rt_idx) {
                    idx = (int)i;
                    break;
                }
            }
            if (s_surfaces[idx].tex) {
                CFRelease((__bridge CFTypeRef)s_surfaces[idx].tex);
                s_surfaces[idx].tex = nil;
            }
            if (s_surfaces[idx].conv) {
                CFRelease((__bridge CFTypeRef)s_surfaces[idx].conv);
                s_surfaces[idx].conv = nil;
            }
        } else {
            idx = (int)s_n_surfaces++;
        }
        memset(&s_surfaces[idx], 0, sizeof(s_surfaces[idx]));
        s_surfaces[idx].loc = loc;
        s_surfaces[idx].offset = offset;
        s_surfaces[idx].is_depth = is_depth;
    } else if (s_surfaces[idx].tex) {
        CFRelease((__bridge CFTypeRef)s_surfaces[idx].tex);
        s_surfaces[idx].tex = nil;
        if (s_surfaces[idx].conv) {
            CFRelease((__bridge CFTypeRef)s_surfaces[idx].conv);
            s_surfaces[idx].conv = nil;
        }
    }

    s_surfaces[idx].w = w;
    s_surfaces[idx].h = h;

    MTLPixelFormat fmt = is_depth ? MTLPixelFormatDepth32Float_Stencil8
                                  : MTLPixelFormatBGRA8Unorm;
    MTLTextureDescriptor* td =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:fmt
                                                           width:w height:h mipmapped:NO];
    td.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    td.storageMode = MTLStorageModePrivate;
    id<MTLTexture> t = [s_state.device newTextureWithDescriptor:td];
    if (!t) {
        printf("[RSX metal] surface create failed %s loc=%u off=0x%08X %ux%u\n",
               is_depth ? "depth" : "color", loc, offset, w, h);
        return -1;
    }
    s_surfaces[idx].tex = (__bridge id<MTLTexture>)CFBridgingRetain(t);
    s_surfaces[idx].inited = 0;
    s_surfaces[idx].recreated = 1;
    s_rt_creates++;
    static int clog = 0;
    if (clog++ < 12) {
        printf("[RSX metal] surface %s loc=%u off=0x%08X %ux%u idx=%d (n=%u)\n",
               is_depth ? "depth" : "color", loc, offset, w, h, idx, s_n_surfaces);
        fflush(stdout);
    }
    return idx;
}

static MetalSurface* metal_color_rt(void)
{
    if (s_color_rt_idx >= 0 && (u32)s_color_rt_idx < s_n_surfaces &&
        s_surfaces[s_color_rt_idx].tex)
        return &s_surfaces[s_color_rt_idx];
    int idx = metal_surface_get(0, 0u,
                                s_state.width ? s_state.width : 1280u,
                                s_state.height ? s_state.height : 720u, 0);
    if (idx < 0) return NULL;
    s_color_rt_idx = idx;
    return &s_surfaces[idx];
}

static MetalSurface* metal_depth_rt_opt(void)
{
    if (s_depth_rt_idx >= 0 && (u32)s_depth_rt_idx < s_n_surfaces &&
        s_surfaces[s_depth_rt_idx].tex)
        return &s_surfaces[s_depth_rt_idx];
    return NULL;
}

/* Host movie overlay (M0 CPU BGRA + M4 CVPixelBuffer/IOSurface zero-copy). */
typedef struct {
    pthread_mutex_t mu;
    int             mu_init;
    volatile int    active;   /* overlay ligado (playback) */
    int             pending;  /* staging tem frame novo desde o ultimo upload */
    u8*             staging;  /* BGRA8888 host copy (M0 fallback) */
    u32             w, h;
    /* GPU objects — CFBridgingRetain ownership, like device/queue */
    __unsafe_unretained id<MTLTexture>             tex;
    __unsafe_unretained id<MTLRenderPipelineState> pso;
    __unsafe_unretained id<MTLSamplerState>        sampler;
    u32             tex_w, tex_h;
    int             pso_ready;
    /* M4: CVMetalTextureCache + current CV frame (CFRetain ownership) */
    CVMetalTextureCacheRef tex_cache;
    CVPixelBufferRef       cv_buf;
    CVMetalTextureRef      cv_mtl;
    int                    use_cv;   /* present samples CV texture */
    u64                    frames_cv;
    u64                    frames_rgba;
    u64                    cv_fail;
    int                    hle_source; /* 1 = M0/M4 movie HLE; 0 = cellVdec stage */
} MetalMovieOverlay;
static MetalMovieOverlay s_movie;

/* M8 — sticky content hold (HOSTRES legal screen etc.), RGBA→BGRA. */
typedef struct {
    u8*  rgba;       /* original RGBA for re-arm */
    u8*  bgra;       /* Metal upload */
    u32  w, h;
    int  hold;       /* content stored */
    int  paused;     /* movie HLE owns screen */
    int  movie_hle;  /* playback active */
    int  logged;
    int  pending;    /* need texture upload */
    __unsafe_unretained id<MTLTexture> tex;
    u32  tex_w, tex_h;
} MetalContentHold;
static MetalContentHold s_content;

static void movie_mu_init_once(void)
{
    if (!s_movie.mu_init) {
        pthread_mutex_init(&s_movie.mu, NULL);
        s_movie.mu_init = 1;
    }
}

/* H-M1 present ownership:
 * Default OFF (decode thread presents each frame). Guest GCM flip nearly
 * stops during logo HLE (~50 flips / ~1s) so guest-only present only ever
 * showed the black fade-in. Dual nextDrawable is avoided by skipping guest
 * present while HLE movie is active (see metal_present).
 * Opt-in guest-only: PS3_MOVIE_PRESENT_ON_GUEST=1. */
static int movie_present_on_guest(void)
{
    static int v = -1;
    if (v < 0) {
        const char* e = getenv("PS3_MOVIE_PRESENT_ON_GUEST");
        v = (e && e[0] == '1') ? 1 : 0;
        fprintf(stderr, "[movie-metal] PRESENT_ON_GUEST=%d (%s)\n", v,
                e ? e : "default=0 decode-presents");
        fflush(stderr);
    }
    return v;
}

static int movie_ensure_pso(void)
{
    if (s_movie.pso_ready && s_movie.pso) return 1;
    if (!s_state.device) return 0;

    NSError* err = nil;
    NSString* src = @
        "#include <metal_stdlib>\n"
        "using namespace metal;\n"
        "struct VOut { float4 pos [[position]]; float2 uv; };\n"
        "vertex VOut movie_vs(uint vid [[vertex_id]]) {\n"
        "  float2 p[3] = { float2(-1,-1), float2(3,-1), float2(-1,3) };\n"
        "  float2 u[3] = { float2(0, 1), float2(2, 1), float2(0,-1) };\n"
        "  VOut o; o.pos = float4(p[vid], 0, 1); o.uv = u[vid]; return o;\n"
        "}\n"
        "fragment float4 movie_fs(VOut in [[stage_in]],\n"
        "    texture2d<float> tex [[texture(0)]],\n"
        "    sampler smp [[sampler(0)]]) {\n"
        "  return tex.sample(smp, in.uv);\n"
        "}\n";

    id<MTLLibrary> lib = [s_state.device newLibraryWithSource:src options:nil error:&err];
    if (!lib) {
        printf("[RSX metal] movie PSO library failed: %s\n",
               err ? [[err localizedDescription] UTF8String] : "?");
        return 0;
    }
    id<MTLFunction> vs = [lib newFunctionWithName:@"movie_vs"];
    id<MTLFunction> fs = [lib newFunctionWithName:@"movie_fs"];
    if (!vs || !fs) {
        printf("[RSX metal] movie PSO functions missing\n");
        return 0;
    }
    MTLRenderPipelineDescriptor* desc = [MTLRenderPipelineDescriptor new];
    desc.vertexFunction   = vs;
    desc.fragmentFunction = fs;
    desc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;

    id<MTLRenderPipelineState> pso =
        [s_state.device newRenderPipelineStateWithDescriptor:desc error:&err];
    if (!pso) {
        printf("[RSX metal] movie PSO create failed: %s\n",
               err ? [[err localizedDescription] UTF8String] : "?");
        return 0;
    }
    s_movie.pso = (__bridge id<MTLRenderPipelineState>)CFBridgingRetain(pso);

    MTLSamplerDescriptor* sd = [MTLSamplerDescriptor new];
    sd.minFilter = MTLSamplerMinMagFilterLinear;
    sd.magFilter = MTLSamplerMinMagFilterLinear;
    sd.sAddressMode = MTLSamplerAddressModeClampToEdge;
    sd.tAddressMode = MTLSamplerAddressModeClampToEdge;
    id<MTLSamplerState> smp = [s_state.device newSamplerStateWithDescriptor:sd];
    s_movie.sampler = (__bridge id<MTLSamplerState>)CFBridgingRetain(smp);

    s_movie.pso_ready = 1;
    return 1;
}

static int prompt_overlay_ensure(void)
{
    if (s_prompt_overlay.ready && s_prompt_overlay.pso && s_prompt_overlay.sampler)
        return 1;
    if (!s_state.device) return 0;

    NSError* err = nil;
    NSString* src = @
        "#include <metal_stdlib>\n"
        "using namespace metal;\n"
        "struct VOut { float4 pos [[position]]; float2 uv; };\n"
        "vertex VOut prompt_vs(uint vid [[vertex_id]]) {\n"
        "  float2 p[3] = { float2(-1,-1), float2(3,-1), float2(-1,3) };\n"
        "  float2 u[3] = { float2(0,1), float2(2,1), float2(0,-1) };\n"
        "  VOut o; o.pos=float4(p[vid],0,1); o.uv=u[vid]; return o;\n"
        "}\n"
        "fragment float4 prompt_fs(VOut in [[stage_in]], texture2d<float> tex [[texture(0)]],\n"
        "    sampler smp [[sampler(0)]]) { return tex.sample(smp, in.uv); }\n";
    id<MTLLibrary> lib = [s_state.device newLibraryWithSource:src options:nil error:&err];
    if (!lib) return 0;
    id<MTLFunction> vs = [lib newFunctionWithName:@"prompt_vs"];
    id<MTLFunction> fs = [lib newFunctionWithName:@"prompt_fs"];
    if (!vs || !fs) return 0;

    MTLRenderPipelineDescriptor* desc = [MTLRenderPipelineDescriptor new];
    desc.vertexFunction = vs;
    desc.fragmentFunction = fs;
    desc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    desc.colorAttachments[0].blendingEnabled = YES;
    desc.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
    desc.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
    desc.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
    desc.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    desc.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
    desc.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    id<MTLRenderPipelineState> pso =
        [s_state.device newRenderPipelineStateWithDescriptor:desc error:&err];
    if (!pso) return 0;

    MTLSamplerDescriptor* sd = [MTLSamplerDescriptor new];
    sd.minFilter = MTLSamplerMinMagFilterNearest;
    sd.magFilter = MTLSamplerMinMagFilterNearest;
    sd.sAddressMode = MTLSamplerAddressModeClampToEdge;
    sd.tAddressMode = MTLSamplerAddressModeClampToEdge;
    id<MTLSamplerState> sampler = [s_state.device newSamplerStateWithDescriptor:sd];
    if (!sampler) return 0;
    s_prompt_overlay.pso = (__bridge id<MTLRenderPipelineState>)CFBridgingRetain(pso);
    s_prompt_overlay.sampler = (__bridge id<MTLSamplerState>)CFBridgingRetain(sampler);
    s_prompt_overlay.ready = 1;
    return 1;
}

static int prompt_overlay_ensure_tex(u32 w, u32 h)
{
    if (s_prompt_overlay.tex && s_prompt_overlay.w == w && s_prompt_overlay.h == h)
        return 1;
    if (!s_state.device || !w || !h) return 0;
    if (s_prompt_overlay.tex) {
        CFRelease((__bridge CFTypeRef)s_prompt_overlay.tex);
        s_prompt_overlay.tex = nil;
    }
    MTLTextureDescriptor* td =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                           width:w height:h mipmapped:NO];
    td.usage = MTLTextureUsageShaderRead;
    td.storageMode = MTLStorageModeShared;
    id<MTLTexture> tex = [s_state.device newTextureWithDescriptor:td];
    if (!tex) return 0;
    s_prompt_overlay.tex = (__bridge id<MTLTexture>)CFBridgingRetain(tex);
    s_prompt_overlay.w = w;
    s_prompt_overlay.h = h;
    return 1;
}

static void prompt_overlay_render(id<MTLCommandBuffer> cb, id<MTLTexture> target)
{
    if (!host_prompts_enabled() || !target || !prompt_overlay_ensure()) return;
    unsigned w = 0, h = 0;
    const uint8_t* bgra = host_prompt_overlay_frame(&w, &h);
    if (!bgra || !prompt_overlay_ensure_tex(w, h)) return;
    MTLRegion region = MTLRegionMake2D(0, 0, w, h);
    [s_prompt_overlay.tex replaceRegion:region mipmapLevel:0 withBytes:bgra bytesPerRow:w * 4u];

    MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = target;
    pass.colorAttachments[0].loadAction = MTLLoadActionLoad;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:pass];
    if (!enc) return;
    [enc setViewport:(MTLViewport){0, 0, (double)target.width, (double)target.height, 0, 1}];
    [enc setRenderPipelineState:s_prompt_overlay.pso];
    [enc setFragmentTexture:s_prompt_overlay.tex atIndex:0];
    [enc setFragmentSamplerState:s_prompt_overlay.sampler atIndex:0];
    [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    [enc endEncoding];
}

static int movie_ensure_tex(u32 w, u32 h)
{
    if (s_movie.tex && s_movie.tex_w == w && s_movie.tex_h == h) return 1;
    if (!s_state.device || w == 0 || h == 0) return 0;

    if (s_movie.tex) {
        CFRelease((__bridge CFTypeRef)s_movie.tex);
        s_movie.tex = nil;
    }
    MTLTextureDescriptor* td =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                           width:w
                                                          height:h
                                                       mipmapped:NO];
    td.usage = MTLTextureUsageShaderRead;
    td.storageMode = MTLStorageModeShared;
    id<MTLTexture> tex = [s_state.device newTextureWithDescriptor:td];
    if (!tex) {
        printf("[RSX metal] movie texture alloc failed %ux%u\n", w, h);
        return 0;
    }
    s_movie.tex   = (__bridge id<MTLTexture>)CFBridgingRetain(tex);
    s_movie.tex_w = w;
    s_movie.tex_h = h;
    return 1;
}

static void movie_upload_locked(void)
{
    if (!s_movie.staging || !s_movie.pending || s_movie.w == 0) return;
    if (!movie_ensure_tex(s_movie.w, s_movie.h) || !s_movie.tex) return;

    MTLRegion region = MTLRegionMake2D(0, 0, s_movie.w, s_movie.h);
    [s_movie.tex replaceRegion:region
                   mipmapLevel:0
                     withBytes:s_movie.staging
                   bytesPerRow:(NSUInteger)s_movie.w * 4];
    s_movie.pending = 0;
}

static void movie_release_cv_locked(void)
{
    if (s_movie.cv_mtl) {
        CFRelease(s_movie.cv_mtl);
        s_movie.cv_mtl = NULL;
    }
    if (s_movie.cv_buf) {
        CFRelease(s_movie.cv_buf);
        s_movie.cv_buf = NULL;
    }
    s_movie.use_cv = 0;
}

static int movie_ensure_cv_cache(void)
{
    if (s_movie.tex_cache) return 1;
    if (!s_state.device) return 0;
    id<MTLDevice> dev = s_state.device;
    CVReturn cr = CVMetalTextureCacheCreate(
        kCFAllocatorDefault, NULL, dev, NULL, &s_movie.tex_cache);
    if (cr != kCVReturnSuccess || !s_movie.tex_cache) {
        static int once = 0;
        if (!once) {
            once = 1;
            printf("[RSX metal] CVMetalTextureCacheCreate failed (%d)\n", (int)cr);
        }
        return 0;
    }
    printf("[RSX metal] movie path=iosurface (CVMetalTextureCache ready)\n");
    fflush(stdout);
    return 1;
}

static void content_rgba_to_bgra(const u8* rgba, u8* bgra, u32 n_pix)
{
    for (u32 i = 0; i < n_pix; i++) {
        bgra[i * 4 + 0] = rgba[i * 4 + 2]; /* B */
        bgra[i * 4 + 1] = rgba[i * 4 + 1]; /* G */
        bgra[i * 4 + 2] = rgba[i * 4 + 0]; /* R */
        bgra[i * 4 + 3] = rgba[i * 4 + 3]; /* A */
    }
}

/* Forward — defined with public API below. */
static void content_hold_demo_maybe(void);

static int content_ensure_tex(void)
{
    if (!s_content.hold || !s_content.bgra || !s_state.device) return 0;
    if (s_content.tex && s_content.tex_w == s_content.w &&
        s_content.tex_h == s_content.h && !s_content.pending)
        return 1;
    if (s_content.tex && (s_content.tex_w != s_content.w || s_content.tex_h != s_content.h)) {
        CFRelease((__bridge CFTypeRef)s_content.tex);
        s_content.tex = nil;
    }
    if (!s_content.tex) {
        MTLTextureDescriptor* td =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                               width:s_content.w
                                                              height:s_content.h
                                                           mipmapped:NO];
        td.usage = MTLTextureUsageShaderRead;
        td.storageMode = MTLStorageModeShared;
        id<MTLTexture> t = [s_state.device newTextureWithDescriptor:td];
        if (!t) return 0;
        s_content.tex = (__bridge id<MTLTexture>)CFBridgingRetain(t);
        s_content.tex_w = s_content.w;
        s_content.tex_h = s_content.h;
    }
    MTLRegion region = MTLRegionMake2D(0, 0, s_content.w, s_content.h);
    [s_content.tex replaceRegion:region
                     mipmapLevel:0
                       withBytes:s_content.bgra
                     bytesPerRow:(NSUInteger)s_content.w * 4];
    s_content.pending = 0;
    return 1;
}

/* CPU fallback used when CV cache fails or format is not BGRA. */
static void movie_frame_bgra_from_cv(CVPixelBufferRef buf)
{
    if (!buf) return;
    CVPixelBufferLockBaseAddress(buf, kCVPixelBufferLock_ReadOnly);
    const void* base = CVPixelBufferGetBaseAddress(buf);
    size_t w = CVPixelBufferGetWidth(buf);
    size_t h = CVPixelBufferGetHeight(buf);
    size_t stride = CVPixelBufferGetBytesPerRow(buf);
    if (base && w > 0 && h > 0) {
        if (stride == w * 4) {
            rsx_metal_movie_frame(base, (unsigned)w, (unsigned)h);
        } else {
            size_t tight = w * 4;
            uint8_t* tmp = (uint8_t*)malloc(tight * h);
            if (tmp) {
                for (size_t y = 0; y < h; y++) {
                    memcpy(tmp + y * tight, (const uint8_t*)base + y * stride, tight);
                }
                rsx_metal_movie_frame(tmp, (unsigned)w, (unsigned)h);
                free(tmp);
            }
        }
    }
    CVPixelBufferUnlockBaseAddress(buf, kCVPixelBufferLock_ReadOnly);
}

/* ---------------------------------------------------------------------------
 * M1 draw path — fixed PSO + shared VB/IB, record then replay on present
 * M2 — decompiled FP/VP → MSL → PSO cache (keyed by fp_hash/vp_hash)
 * M3 — bind_texture → MTLTexture slots + deswizzle/upload
 * -----------------------------------------------------------------------*/
/* 32 thrashed on GoW2's first level: >256 evictions between two frames (E428),
 * i.e. a recompile per set_shader. Lookup is a linear scan and eviction a FIFO
 * memmove, both cheap. 1024 since the key also carries the blend/mask state
 * (one entry per variant, E440). */
#define METAL_MAX_SHADER_CACHE 1024u
/* Inline FP constants kept per draw (M2 constant-buffer mode). */
#define METAL_FC_MAX 64u

typedef struct {
    /* Diagnostic-only monotonically increasing geometry ordinal for this frame. */
    u32 geom_id;
    u32 vb_byte_offset;
    u32 vertex_count;   /* verts for non-indexed; index count when indexed */
    u32 is_indexed;
    u32 ib_byte_offset;
    MTLPrimitiveType prim;
    /* Snapshot of pipeline at record time (M1 fixed or M2 decompiled). */
    __unsafe_unretained id<MTLRenderPipelineState> pso;
    /* M2 inline FP constants of this draw in s_draw.fc_buf (rsx_fc[k]). */
    u32 fc_byte_offset;
    u32 fc_count;
    /* M2 vertex-program constants of this draw in s_draw.vc_buf (vp_c[k]). */
    u32 vc_byte_offset;
    /* M5/M7 per-draw state (PS3_METAL_PER_DRAW_RT): the surfaces this draw
     * targets and the viewport/scissor/textures/depth state in force when it
     * was issued. tex[] and dss are retained here and released in
     * metal_begin_frame. A record with clear_flags != 0 clears its target. */
    int   has_state;
    u32   fp_hash;      /* FP of the shader in force (PASS log) */
    u32   vp_hash;      /* VP key of the shader in force (PASS log, PS3_METAL_SKIP_VP) */
    u32   cmask;        /* guest colour mask at record time (PASS log) */
    int   color_idx, depth_idx;
    int   has_vp;
    float vp[4];
    u32   sc[4];
    __unsafe_unretained id<MTLTexture> tex[RSX_MAX_TEXTURES];
    u32   tex_addr[RSX_MAX_TEXTURES];   /* TEXTURE_ADDRESS por unidade */
    u32   tex_filt[RSX_MAX_TEXTURES];   /* TEXTURE_FILTER por unidade */
    __unsafe_unretained id<MTLDepthStencilState> dss;
    int   depth_clamp;  /* SET_ZMIN_MAX_CONTROL: clamp bit or clip bit clear */
    int   poly_offset;  /* SET_POLY_OFFSET_FILL_ENABLE */
    float poly_scale, poly_bias;
    int   stencil_en;   /* stencil test on: replay sets the refs below */
    u32   stencil_ref_front, stencil_ref_back;
    MetalDsDesc ds_desc; /* depth/stencil descriptor behind dss (PASS log) */
    u32   blend[4];      /* enable, sfactor, dfactor, equation at record time (PASS log) */
    u32   tex0_reg[4];   /* unit 0 FORMAT, ADDRESS, FILTER, CONTROL1 at record time (PASS log) */
    u32   tex_off[4];    /* guest texture offsets unit 0..3 (feedback vs dest) */
    float vso[8];        /* guest VIEWPORT_SCALE xyzw, OFFSET xyzw at record time */
    u32   vso_seen;
    u32   clip_wh[2];    /* guest SURFACE_CLIP width/height */
    int   cull_mode;    /* RSX_CULL_NONE/FRONT/BACK */
    int   winding_ccw;  /* guest FRONT_FACE */
    u32   clear_flags;
    float clear_rgba[4];
    float clear_depth;
    u32   clear_stencil;
    /* NV3089 blit record (metal_blit_scaled): color_idx is the destination. */
    int   is_blit;
    int   blit_src;
    float blit_uv[4];    /* u0, v0, u1, v1 normalised in the source surface */
    u32   blit_rect[4];  /* x, y, w, h in the destination surface */
    /* M6 decoded attributes of this draw in s_draw.va_buf (metal_va_fill):
     * va_stride float4 per vertex from float4 va_base; 0 = none. */
    u32   va_base;
    u32   va_stride;
    signed char va_slot[16];
    float va_cur[16][4];   /* VERTEX_DATA4F register at record time */
    u32   pos_ea;          /* guest address of vertex 0's position (diagnostic) */
    u32   pos_gstride;
    /* MRT: alvos de cor deste draw (mrt_idx[0] == color_idx) e com quantos
     * attachments o seu PSO foi construido -- o replay so' pode usar um PSO cujos
     * attachments batem com os do passe. */
    int   mrt_n;
    int   mrt_idx[4];
    int   pso_mrt_n;
    /* PS3_RSX_SPLIT: PSO da metade direita (a chave ligada); o replay desenha
     * este draw duas vezes, uma em cada metade. Retido como o pso. */
    __unsafe_unretained id<MTLRenderPipelineState> pso_b;
    u32   at[4];   /* alpha test: enable, func & 7, ref (float bits), 0 -> FS buffer(1) */
} MetalDrawRec;

/* A recorded draw OWNS a reference to its PSO (CFRetain here, CFRelease in
 * metal_begin_frame when the records are discarded): the 32-entry shader_cache
 * can evict and release a PSO many times per frame (E428: >256 evictions
 * between two frames), and the replay in metal_present must never see a freed
 * pipeline state. */
static void metal_draw_rec_note_pso_mrt(MetalDrawRec* dr);   /* depois de s_draw */
static void metal_draw_rec_set_pso(MetalDrawRec* dr, id<MTLRenderPipelineState> p)
{
    dr->pso = p;
    if (p) CFRetain((__bridge CFTypeRef)p);
    metal_draw_rec_note_pso_mrt(dr);
}

typedef struct {
    u32 fp_hash;
    u32 vp_hash;    /* the ORIGINAL vp hash, even when the VS fell back */
    u32 state_key;  /* blend/mask state baked into the PSO (metal_pso_state_key) */
    int vs_va;      /* built from a decompiled VP (MetalShaderFns.vs_va) */
    u16 used_inputs; /* E482: bit N = o VS le' o atributo de vertice N. */
    u8  mrt_n;       /* alvos de cor com que o PSO foi construido */
    __unsafe_unretained id<MTLRenderPipelineState> pso;
    __unsafe_unretained id<MTLRenderPipelineState> pso_b;   /* PS3_RSX_SPLIT (metade direita) */
} MetalShaderPso;

/* Per-unit bound guest texture (M3). invalid → present uses dummy white. */
typedef struct {
    __unsafe_unretained id<MTLTexture> tex;
    u32 w, h;
    u32 rsx_fmt;
    u32 addr;       /* NV4097_SET_TEXTURE_ADDRESS: wrap S/T/R */
    u32 filt;       /* NV4097_SET_TEXTURE_FILTER: min/mag/mip */
    int valid;
} MetalTexSlot;

typedef struct {
    __unsafe_unretained id<MTLBuffer>              vb;
    __unsafe_unretained id<MTLBuffer>              ib;
    __unsafe_unretained id<MTLRenderPipelineState> pso;
    int                         pso_ready;
    MetalBasicVertex*           vb_mapped; /* shared buffer contents */
    u32*                        ib_mapped;
    u32                         vb_offset; /* bytes written this frame */
    u32                         ib_offset;
    /* Per-draw FP constants (M2 constant-buffer mode): fragment buffer(0). */
    __unsafe_unretained id<MTLBuffer> fc_buf;
    float*                      fc_mapped;
    u32                         fc_offset;  /* bytes written this frame */
    u32                         cur_fp_ea;  /* FP of the current shader */
    u32                         cur_fp_bytes;
    MetalDrawRec                draws[METAL_MAX_DRAWS];
    u32                         draw_count;
    /* PSOs evicted from shader_cache while draws recorded this frame may still
     * point at them (MetalDrawRec.pso is __unsafe_unretained): released only
     * after the frame's draws are encoded (metal_pso_retired_drain). */
    CFTypeRef                   pso_retired[256];
    u32                         pso_retired_count;
    const rsx_state*            current_rsx;
    u64                         total_draws;
    double                      vp_x, vp_y, vp_w, vp_h;
    int                         has_vp;
    /* M2 shader cache */
    MetalShaderPso              shader_cache[METAL_MAX_SHADER_CACHE];
    u32                         shader_cache_count;
    MetalShaderPso*             current_shader; /* NULL → fixed M1 PSO */
    __unsafe_unretained id<MTLBuffer> vp_const_buf; /* 1024 float4 zeros */
    /* Per-draw vertex-program constant snapshots (metal_draw_rec_capture_vc). */
    __unsafe_unretained id<MTLBuffer> vc_buf;
    u8*                         vc_mapped;
    u32                         vc_offset;   /* bytes written this frame */
    u32                         vc_last;     /* offset of the latest snapshot */
    int                         vc_have;     /* a snapshot exists this frame */
    /* M6 decoded vertex attributes (metal_va_fill), vertex buffer(3). */
    __unsafe_unretained id<MTLBuffer> va_buf;
    float*                      va_mapped;
    u32                         va_offset;   /* float4 written this frame */
    u32                         va_last_base, va_last_stride;
    /* Guest address of attribute 0 (position) for this upload: the only way,
     * from a recorded draw, to ask WHO wrote these vertices. Diagnostic. */
    u32                         va_last_pos_ea, va_last_pos_gstride;
    u32                         va_last_count;   /* vertices in the last upload (PS3_TRACE_VC_VP) */
    signed char                 va_last_slot[16];
    float                       va_last_cur[16][4];
    int                         va_last_valid;
    __unsafe_unretained id<MTLTexture>            dummy_tex;
    __unsafe_unretained id<MTLSamplerState>       dummy_samp;
    /* M3 textures */
    MetalTexSlot                tex_slots[RSX_MAX_TEXTURES];
    u32                         bind_ok, bind_skip, bind_deswizzle;
    /* Textured demo PSO (pos+uv → sample unit 0) for PS3_METAL_TEX_DEMO */
    __unsafe_unretained id<MTLRenderPipelineState> tex_demo_pso;
    int                         tex_demo_pso_ready;
    u32 fp_cache_hits, fp_cache_misses;
    u32 fp_decompile_ok, fp_decompile_fail;
    u32 vp_decompile_ok, vp_decompile_fail;
    /* M6 vertex attribs snapshot */
    rsx_vertex_attrib attribs[RSX_MAX_VERTEX_ATTRIBS];
    int               attribs_valid;
    u32               attrib_sig;      /* hash of enabled slots */
    int               layout_ok;       /* usable pos path */
    int               layout_unknown;  /* saw unsupported type */
    u32               upload_ok, upload_fallback, upload_skip;
} MetalDrawPath;
static MetalDrawPath s_draw;

/* Everything a PSO bakes in besides its shaders: blend and colour write mask.
 * Do NOT fold SET_SHADER_CONTROL in here: at present current_rsx is often
 * NULL so the key flipped 0x40↔0x80 every draw (texture flicker). */
/* RSX alpha test in the fragment shader (RPCS3 RSXROPEpilogue.glsl). Default ON
 * since 2026-09-21: it was only logged, so alpha-tested fragments wrote depth --
 * the GoW2 Colossus' reflection pass then hid its bronze pass (LEQUAL) over the
 * shoulder. PS3_METAL_ALPHA_TEST=0 = old behaviour (A/B). */
static int metal_atest_on(void)
{
    static int on = -1;
    if (on < 0) { const char* e = getenv("PS3_METAL_ALPHA_TEST"); on = (e && e[0] == '0') ? 0 : 1; }
    return on;
}
static u32 metal_pso_state_key(void)
{
    return rsx_core_pso_state_key(metal_pipe_blend_key(), &s_pipe, s_mrt_n, metal_atest_on());
}

/* A blend/mask change used to flush the whole shader cache, so every decompiled
 * PSO was recompiled from MSL source at the next set_shader (E440: ~2 fps once
 * GoW2 draws its 3D scene). PSOs are now keyed by (fp, vp, state key) and other
 * states are built from cached functions (metal_draw_pso_now); only the fixed
 * pipeline is re-selected here. */
static void metal_invalidate_blend_psos(void)
{
    if (s_draw.pso) {
        CFRelease((__bridge CFTypeRef)s_draw.pso);
        s_draw.pso = nil;
    }
    s_draw.pso_ready = 0;
}

/* Forward decls (texture path sits above draw buffer helpers). */
static u32 metal_bswap32(u32 v);
static MTLVertexDescriptor* metal_basic_vertex_desc(void);
static int draw_ensure_buffers(void);

static u32 metal_fnv1a(const u8* data, u32 len)
{
    u32 h = 2166136261u;
    for (u32 i = 0; i < len; i++) { h ^= data[i]; h *= 16777619u; }
    return h;
}

extern int gcm_io_offset_to_ea(u32 ioOffset, u32* out_ea);

/* SET_SHADER_PROGRAM carries the DMA context in [1:0] as libgcm writes it:
 * location + 1, i.e. raw 1 = RSX local, raw 2 = main (via the IO map) — the
 * same convention as rsx_dispatch.h and RPCS3 (`(value & 3) - 1`). The old code
 * took raw 0 as local and returned every other value as a flat EA, so GoW2's
 * raw-1 FPs (a local-memory ring at offset ~0x3F7xxx) were read from game data
 * at EA 0x3F7xxx: a different "program" on every draw (E432: 299 distinct
 * fp_hash in 300 misses, 2 vp_hash). Raw 0 keeps the old local mapping. */
/* Body moved to rsx_core_rules.c (rsx_core_resolve_fp_ea); the warn++<5
 * printf (diagnostic, kept as-is) fires on the `unresolved` flag the core
 * function reports back. */
static u32 metal_resolve_fp_ea(const rsx_state* state)
{
    int unresolved = 0;
    u32 ea = rsx_core_resolve_fp_ea(state, gcm_io_offset_to_ea, &unresolved);
    if (unresolved) {
        static int warn = 0;
        if (warn++ < 5)
            printf("[RSX metal] fp location raw=%u off=0x%08X unresolved -> flat offset\n",
                   state->shader_program & 0x3u, state->fragment_program_addr);
    }
    return ea;
}

/* Passthrough VS when no NV40 VP (or VP compile failed). Links with PSIn. */
static const char s_metal_vs_passthrough_msl[] =
    "#include <metal_stdlib>\n"
    "using namespace metal;\n"
    "struct VSIn {\n"
    "  float3 pos [[attribute(0)]];\n"
    "  float4 col [[attribute(1)]];\n"
    "};\n"
    "struct VSOut {\n"
    "  float4 position [[position]];\n"
    "  float4 col0; float4 col1; float4 fog;\n"
    "  float4 tc0; float4 tc1; float4 tc2; float4 tc3;\n"
    "  float4 tc4; float4 tc5; float4 tc6; float4 tc7;\n"
    "};\n"
    "vertex VSOut vs_main(VSIn i [[stage_in]]) {\n"
    "  VSOut o;\n"
    "  o.position = float4(i.pos.xy, 0.0, 1.0);\n"
    "  o.col0 = i.col; o.col1 = i.col; o.fog = float4(0);\n"
    "  /* Host packs UV into attr1 (col.xy) for 2D UI when no diffuse. */\n"
    "  o.tc0 = float4(i.col.xy, 0.0, 1.0);\n"
    "  o.tc1=o.tc2=o.tc3=o.tc4=o.tc5=o.tc6=o.tc7=float4(0);\n"
    "  return o;\n"
    "}\n";

static id<MTLRenderPipelineState> metal_shader_variant_now(void);

/* Os PSOs fixos (fallback, demo) tem um attachment so'. Os descompilados sao
 * construidos com s_mrt_n na chave (metal_pso_state_key). */
static void metal_draw_rec_note_pso_mrt(MetalDrawRec* dr)
{
    id<MTLRenderPipelineState> p = dr->pso;
    dr->pso_b = nil;
    if (p && s_draw.current_shader && p == s_draw.current_shader->pso && s_draw.current_shader->pso_b) {
        dr->pso_b = s_draw.current_shader->pso_b;
        CFRetain((__bridge CFTypeRef)dr->pso_b);
    }
    if (!p || p == s_draw.pso || p == s_draw.tex_demo_pso) dr->pso_mrt_n = 1;
    else if (s_draw.current_shader && p == s_draw.current_shader->pso)
        dr->pso_mrt_n = s_draw.current_shader->mrt_n ? s_draw.current_shader->mrt_n : 1;
    else dr->pso_mrt_n = s_mrt_n;
}

static id<MTLRenderPipelineState> metal_draw_pso_now(void)
{
    if (s_draw.current_shader && s_draw.current_shader->pso) {
        if (s_draw.current_shader->state_key == metal_pso_state_key())
            return s_draw.current_shader->pso;
        /* Blend/mask changed since set_shader: same shaders, other variant. */
        id<MTLRenderPipelineState> v = metal_shader_variant_now();
        if (v) return v;
        if (s_draw.current_shader && s_draw.current_shader->pso)
            return s_draw.current_shader->pso;
    }
    return s_draw.pso;
}

static int metal_ensure_dummy_tex(void)
{
    if (s_draw.dummy_tex && s_draw.dummy_samp) return 1;
    if (!s_state.device) return 0;
    if (!s_draw.dummy_tex) {
        MTLTextureDescriptor* td =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                               width:1 height:1 mipmapped:NO];
        td.usage = MTLTextureUsageShaderRead;
        td.storageMode = MTLStorageModeShared;
        id<MTLTexture> t = [s_state.device newTextureWithDescriptor:td];
        if (!t) return 0;
        u8 white[4] = { 255, 255, 255, 255 };
        [t replaceRegion:MTLRegionMake2D(0, 0, 1, 1) mipmapLevel:0 withBytes:white bytesPerRow:4];
        s_draw.dummy_tex = (__bridge id<MTLTexture>)CFBridgingRetain(t);
    }
    if (!s_draw.dummy_samp) {
        MTLSamplerDescriptor* sd = [MTLSamplerDescriptor new];
        sd.minFilter = MTLSamplerMinMagFilterLinear;
        sd.magFilter = MTLSamplerMinMagFilterLinear;
        sd.sAddressMode = MTLSamplerAddressModeClampToEdge;
        sd.tAddressMode = MTLSamplerAddressModeClampToEdge;
        id<MTLSamplerState> s = [s_state.device newSamplerStateWithDescriptor:sd];
        if (!s) return 0;
        s_draw.dummy_samp = (__bridge id<MTLSamplerState>)CFBridgingRetain(s);
    }
    return 1;
}

/* Map RSX base format → Metal. *is_bc=1 for DXT/BC. Returns 0 if unsupported. */
static int metal_format_from_rsx(u32 rsx_fmt, MTLPixelFormat* out_fmt, int* is_bc)
{
    if (is_bc) *is_bc = 0;
    if (!out_fmt) return 0;
    switch (rsx_fmt) {
    case RSX_TEXTURE_A8R8G8B8:
    case RSX_TEXTURE_D8R8G8B8:
        *out_fmt = MTLPixelFormatBGRA8Unorm;
        return 1;
    case RSX_TEXTURE_B8:
        *out_fmt = MTLPixelFormatR8Unorm;
        return 1;
    case RSX_TEXTURE_G8B8:
        *out_fmt = MTLPixelFormatRG8Unorm;
        return 1;
    case RSX_TEXTURE_R5G6B5:
        /* Expand to BGRA on upload; no direct R5G6B5 on all Apple GPUs as shader-read. */
        *out_fmt = MTLPixelFormatBGRA8Unorm;
        return 1;
    case RSX_TEXTURE_A1R5G5B5:
    case RSX_TEXTURE_R5G5B5A1:
    case RSX_TEXTURE_A4R4G4B4:
        *out_fmt = MTLPixelFormatBGRA8Unorm;
        return 1;
    case RSX_TEXTURE_COMPRESSED_DXT1:
        *out_fmt = MTLPixelFormatBC1_RGBA;
        if (is_bc) *is_bc = 1;
        return 1;
    case RSX_TEXTURE_COMPRESSED_DXT23:
        *out_fmt = MTLPixelFormatBC2_RGBA;
        if (is_bc) *is_bc = 1;
        return 1;
    case RSX_TEXTURE_COMPRESSED_DXT45:
        *out_fmt = MTLPixelFormatBC3_RGBA;
        if (is_bc) *is_bc = 1;
        return 1;
    default:
        return 0;
    }
}

/* Body moved to rsx_core_rules.c (rsx_core_resolve_tex_ea): same DMA-context
 * convention as metal_resolve_fp_ea. */
static u32 metal_resolve_tex_ea(const rsx_texture_state* tex)
{
    return rsx_core_resolve_tex_ea(tex, gcm_io_offset_to_ea);
}

/* Expand 16-bit RSX packed colours to BGRA8 host (after host-endian u16).
 * Bodies moved to rsx_core_decode.h as `static inline` (per-texel hot path
 * in texture upload; no LTO in this build, see the note at
 * metal_va_fp_fast); these stay as one-line forwarders under the old names
 * for every call site. */
static void metal_expand_r5g6b5_to_bgra(u16 t, u8* bgra) { rsx_core_expand_r5g6b5_to_bgra(t, bgra); }
static void metal_expand_a1r5g5b5_to_bgra(u16 t, u8* bgra) { rsx_core_expand_a1r5g5b5_to_bgra(t, bgra); }
static void metal_expand_a4r4g4b4_to_bgra(u16 t, u8* bgra) { rsx_core_expand_a4r4g4b4_to_bgra(t, bgra); }

static void metal_dump_texture_to(id<MTLTexture> tex, const char* path);
/* Upload tightly-packed host pixels into (or recreate) slot.unit texture. */
static int metal_tex_upload_slot(u32 unit, MTLPixelFormat mtl_fmt, u32 width, u32 height,
                                 const void* pixels, NSUInteger bytes_per_row, u32 rsx_fmt)
{
    if (!s_state.device || unit >= RSX_MAX_TEXTURES || !pixels || width == 0 || height == 0)
        return 0;

    MetalTexSlot* slot = &s_draw.tex_slots[unit];
    /* A slot left pointing at a render surface (RTT, Private storage) cannot
     * take replaceRegion: give the unit its own texture again. */
    int need_new = !slot->tex || slot->w != width || slot->h != height ||
                   (slot->valid && slot->tex.pixelFormat != mtl_fmt) ||
                   slot->tex.storageMode == MTLStorageModePrivate;
    if (need_new) {
        if (slot->tex) {
            CFRelease((__bridge CFTypeRef)slot->tex);
            slot->tex = nil;
        }
        MTLTextureDescriptor* td =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:mtl_fmt
                                                               width:width
                                                              height:height
                                                           mipmapped:NO];
        td.usage = MTLTextureUsageShaderRead;
        td.storageMode = MTLStorageModeShared;
        id<MTLTexture> t = [s_state.device newTextureWithDescriptor:td];
        if (!t) {
            printf("[RSX metal] bind_texture: MTLTexture alloc failed unit=%u %ux%u\n",
                   unit, width, height);
            slot->valid = 0;
            return 0;
        }
        slot->tex = (__bridge id<MTLTexture>)CFBridgingRetain(t);
        slot->w = width;
        slot->h = height;
    }
    MTLRegion region = MTLRegionMake2D(0, 0, width, height);
    [slot->tex replaceRegion:region
                 mipmapLevel:0
                   withBytes:pixels
                 bytesPerRow:bytes_per_row];
    slot->rsx_fmt = rsx_fmt;
    slot->valid = 1;
    return 1;
}

/* Texture cache (ON by default in per-draw mode; PS3_METAL_TEX_CACHE=0/1 forces):
 * one MTLTexture per distinct guest texture, keyed by ea/format/size/swizzle
 * and checked against a sampled fingerprint of the guest bytes. It replaces the
 * deswizzle + upload on every bind (E442 host profile: ~30-35 % of a thread in
 * rsx_deswizzle_texels) and the replaceRegion on the unit's single texture,
 * which made every draw of a frame sample the last texture uploaded to its unit
 * (draws are replayed at present). Changed content gets a NEW texture; records
 * already holding the old one keep it. 256 buckets x 4 ways, LRU per bucket. */
#define METAL_TEXC_BUCKETS 256u
#define METAL_TEXC_WAYS    4u
typedef struct {
    u32 ea, fmt, w, h, swz;
    u32 remap;      /* TEXTURE_CONTROL1 crossbar baked into the texture's swizzle */
    u64 fp;
    u64 fp_full;    /* PS3_METAL_TEXC_AUDIT: hash COMPLETO do conteudo no upload */
    u64 last_frame;
    __unsafe_unretained id<MTLTexture> tex;
} MetalTexCacheEnt;
static MetalTexCacheEnt s_texc[METAL_TEXC_BUCKETS][METAL_TEXC_WAYS];
static u32 s_texc_hits, s_texc_miss, s_texc_changed;

static int metal_tex_cache_on(void)
{
    static int on = -1;
    if (on < 0) {
        const char* e = getenv("PS3_METAL_TEX_CACHE");
        on = e ? ((*e && *e != '0') ? 1 : 0) : metal_per_draw_rt();
    }
    return on;
}

/* FNV-1a sobre janelas de 16 bytes, UMA POR PAGINA de 4 KB (o buffer inteiro se
 * for pequeno).
 *
 * Era um numero FIXO de 65 janelas = 1 040 bytes, independentemente do tamanho:
 * numa textura de 512x512 A8R8G8B8 (1 MB) isso cobre 0,1% do conteudo. Uma
 * janela por pagina de 4 KB mantem a COBERTURA constante por area em vez de a
 * diluir com o tamanho (1 MB passa de 65 para 256 janelas) pelos mesmos ~0,4%
 * dos bytes. PS3_METAL_TEX_FPWIN ajusta a granularidade (default 4096).
 *
 * RETRATADO -- a medicao que motivou isto estava errada. O PS3_METAL_TEXC_AUDIT
 * comparava o hash completo com o ultimo hash do BUCKET, nao da ENTRADA: como
 * 256 buckets x 4 vias partilham chave, duas texturas diferentes no mesmo bucket
 * contavam como "conteudo diferente". Dai os 3,89% que atribui a staleness, e
 * dai a amostragem maior nao os mexer (3,94%). Com o audit corrigido (hash
 * completo guardado em tce->fp_full no upload, comparado no acerto):
 * **0,00% em 76 000 acertos**. A fingerprint amostrada NAO esta' a deixar passar
 * conteudo alterado, e o cache de textura NAO e' a causa do Colossus/particulas.
 * A alteracao para 4 KB fica por ser estritamente melhor, nao por corrigir bug.
 *
 * Numeros fixos que sobram aqui, com o efeito de cada um:
 *   - wins > 4096: acima de ~16 MB o passo volta a crescer e a cobertura por
 *     area volta a diluir-se (nenhuma textura do GoW2 chega la').
 *   - n <= 1040: limiar herdado do tamanho das 65 janelas antigas; abaixo dele
 *     hasheia tudo. Arbitrario, sem defeito conhecido.
 *   - step = (n-16)/wins e' divisao inteira: a cauda entre o ultimo passo e
 *     n-16 fica coberta pela janela k==wins, que e' exactamente n-16. */
/* Lazy s_win read: PS3_METAL_TEX_FPWIN, [256, 262144], default 4096. Now
 * read (and cached in the static) on the first call regardless of n -- the
 * old code only reached this read past the n <= 1040 fast path, but the
 * resolved value is the same either way, so this is harmless. Body itself
 * now lives in rsx_core_decode.c. */
static u64 metal_tex_fingerprint(const u8* p, u32 n)
{
    static u32 s_win = 0;
    if (!s_win) {
        const char* e = getenv("PS3_METAL_TEX_FPWIN");
        long v = (e && *e) ? strtol(e, NULL, 0) : 4096;
        if (v < 256 || v > 262144) v = 4096;
        s_win = (u32)v;
    }
    return rsx_core_tex_fingerprint(p, n, s_win);
}

static u32 metal_texc_bucket(u32 ea, u32 fmt, u32 w, u32 h)
{
    return rsx_core_texc_bucket(ea, fmt, w, h, METAL_TEXC_BUCKETS);
}

/* PS3_METAL_TEXC_OFF=1 (OFF, diagnostico): desliga o cache de textura, forcando
 * re-upload em cada bind.
 *
 * Porque existe: a chave do cache e' (ea, fmt, w, h, swz, remap) -- o ENDERECO,
 * nunca o CONTEUDO -- e nao ha' invalidacao nenhuma (sem hook de pagina suja,
 * sem hash). Se o titulo reescrever a textura no mesmo endereco (streaming), o
 * cache devolve a versao VELHA para sempre. Isso explicaria de uma vez a cabeca
 * branca do Kratos, o Colossus borrado e as particulas com efeito errado -- e
 * explica porque os dumps parecem correctos: PS3_METAL_DUMP_TEX escreve no
 * momento da INSERCAO, ou seja a primeira versao.
 *
 * Com a flag ligada o jogo fica lento (re-upload por draw), mas o VISUAL diz se
 * a hipotese esta' certa. */
static int metal_texc_off(void)
{
    static int on = -1;
    if (on < 0) { const char* e = getenv("PS3_METAL_TEXC_OFF"); on = (e && e[0] == '1') ? 1 : 0; }
    return on;
}

static MetalTexCacheEnt* metal_texc_find(u32 ea, u32 fmt, u32 w, u32 h, u32 swz, u32 remap)
{
    if (metal_texc_off()) return NULL;
    MetalTexCacheEnt* b = s_texc[metal_texc_bucket(ea, fmt, w, h)];
    for (u32 i = 0; i < METAL_TEXC_WAYS; i++)
        if (b[i].tex && b[i].ea == ea && b[i].fmt == fmt && b[i].w == w && b[i].h == h &&
            b[i].swz == swz && b[i].remap == remap)
            return &b[i];
    return NULL;
}

/* NV4097 TEXTURE_CONTROL1 component crossbar -> Metal texture swizzle (E461: the
 * Metal path ignored it). RSX rule as upstream HEAD's rsx_texture_component_remap:
 * for the outputs A,R,G,B (fields LSB first) a 2-bit source {A,R,G,B} in bits
 * 0-7 and a 2-bit op in bits 8-15 (0 ZERO, 1 ONE, else REMAP); 0 = unset =
 * identity 0xAAE4. The uploaded texture holds R,G,B,A in components 0..3 (32-bit
 * ARGB byte-swapped to BGRA, BC and the 16-bit expansions decode to RGBA). One-
 * and two-channel textures (B8 as R8, G8B8 as RG8) keep the identity for now. */
static MTLTextureSwizzleChannels metal_remap_swizzle(u32 remap, u32 rsx_fmt, MTLPixelFormat mf)
{
    if (mf == MTLPixelFormatR8Unorm || mf == MTLPixelFormatRG8Unorm)
        return MTLTextureSwizzleChannelsMake(MTLTextureSwizzleRed, MTLTextureSwizzleGreen,
                                             MTLTextureSwizzleBlue, MTLTextureSwizzleAlpha);
    /* Shared crossbar decode (rsx_texture_formats.h, unit-tested): it also
     * forces A = ONE on D8R8G8B8 / D1R5G5B5 whose A still points at the unused
     * D lane -- identity remap there left alpha = the D byte and SRC_ALPHA
     * blending made characters translucent. */
    int rgba[4];
    rsx_texture_remap_rgba(remap, rsx_fmt, rgba);
    static const MTLTextureSwizzle to_mtl[6] = {
        MTLTextureSwizzleZero, MTLTextureSwizzleOne, MTLTextureSwizzleRed,
        MTLTextureSwizzleGreen, MTLTextureSwizzleBlue, MTLTextureSwizzleAlpha };
    for (int i = 0; i < 4; i++)   /* RSX_SWZ_* is 0..5; anything else reads ZERO */
        if (rgba[i] < RSX_SWZ_ZERO || rgba[i] > RSX_SWZ_ALPHA) rgba[i] = RSX_SWZ_ZERO;
    return MTLTextureSwizzleChannelsMake(to_mtl[rgba[0]], to_mtl[rgba[1]],
                                         to_mtl[rgba[2]], to_mtl[rgba[3]]);
}

/* A free way in the key's bucket, else its least recently used one (released). */
static MetalTexCacheEnt* metal_texc_victim(u32 ea, u32 fmt, u32 w, u32 h)
{
    MetalTexCacheEnt* b = s_texc[metal_texc_bucket(ea, fmt, w, h)];
    MetalTexCacheEnt* v = &b[0];
    for (u32 i = 0; i < METAL_TEXC_WAYS; i++) {
        if (!b[i].tex) return &b[i];
        if (b[i].last_frame < v->last_frame) v = &b[i];
    }
    CFRelease((__bridge CFTypeRef)v->tex);
    v->tex = nil;
    return v;
}

/* Point a unit at a texture; the slot holds its own reference. */
static void metal_tex_slot_set(u32 unit, id<MTLTexture> t, u32 w, u32 h, u32 rsx_fmt)
{
    MetalTexSlot* slot = &s_draw.tex_slots[unit];
    if (slot->tex != t) {
        if (slot->tex) CFRelease((__bridge CFTypeRef)slot->tex);
        CFRetain((__bridge CFTypeRef)t);
        slot->tex = t;
    }
    slot->w = w;
    slot->h = h;
    slot->rsx_fmt = rsx_fmt;
    slot->valid = 1;
}

/* A new shared texture holding these pixels; the caller owns one reference. */
static id<MTLTexture> metal_tex_new(MTLPixelFormat fmt, u32 w, u32 h, const void* px,
                                    NSUInteger bytes_per_row, MTLTextureSwizzleChannels sw)
{
    if (!s_state.device || !px || !w || !h) return nil;
    MTLTextureDescriptor* td =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:fmt width:w height:h
                                                       mipmapped:NO];
    td.usage = MTLTextureUsageShaderRead;
    td.storageMode = MTLStorageModeShared;
    td.swizzle = sw;
    id<MTLTexture> t = [s_state.device newTextureWithDescriptor:td];
    if (!t) return nil;
    [t replaceRegion:MTLRegionMake2D(0, 0, w, h) mipmapLevel:0 withBytes:px
         bytesPerRow:bytes_per_row];
    return (__bridge id<MTLTexture>)CFBridgingRetain(t);
}

/* Compute-write target. Apple GPUs reject ShaderWrite+Shared (Metal aborts
 * in -[MTLTextureDescriptorInternal validateWithDevice:]). Private only.
 * Swizzle is sample-time: identity on the writeable texture; caller may wrap
 * a view. Returns the WRITEABLE base (caller CFRelease). */
static id<MTLTexture> metal_tex_new_rw(MTLPixelFormat fmt, u32 w, u32 h)
{
    if (!s_state.device || !w || !h) return nil;
    if (fmt != MTLPixelFormatBGRA8Unorm && fmt != MTLPixelFormatRGBA8Unorm)
        return nil;
    MTLTextureDescriptor* td =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:fmt width:w height:h
                                                       mipmapped:NO];
    td.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
    td.storageMode = MTLStorageModePrivate;
    id<MTLTexture> t = [s_state.device newTextureWithDescriptor:td];
    if (!t) return nil;
    return (__bridge id<MTLTexture>)CFBridgingRetain(t);
}

static rmd_ctx   s_rmd;
static rmd_queue s_rmd_q;
static int       s_rmd_ready;

static int metal_rmd_ready(void)
{
    if (s_rmd_ready)
        return s_rmd.pso_tex != nil;
    s_rmd_ready = 1;
    rmd_queue_init(&s_rmd_q);
    if (!s_state.device || rmd_init(&s_rmd, s_state.device) != 0)
        return 0;
    return s_rmd.pso_tex != nil;
}

/* Install a synthetic BGRA checkerboard on unit 0 (PS3_METAL_TEX_DEMO smoke). */
static void metal_tex_install_checkerboard(u32 unit, u32 w, u32 h)
{
    if (!s_state.device || w == 0 || h == 0) return;
    u32 n = w * h * 4u;
    u8* px = (u8*)malloc(n);
    if (!px) return;
    for (u32 y = 0; y < h; y++) {
        for (u32 x = 0; x < w; x++) {
            int on = ((x / 8) ^ (y / 8)) & 1;
            u8* p = px + (y * w + x) * 4u;
            /* BGRA: magenta/cyan so dump ≠ clear */
            if (on) { p[0] = 255; p[1] = 0;   p[2] = 255; p[3] = 255; }
            else    { p[0] = 255; p[1] = 255; p[2] = 0;   p[3] = 255; }
        }
    }
    if (metal_tex_upload_slot(unit, MTLPixelFormatBGRA8Unorm, w, h, px, w * 4u,
                              RSX_TEXTURE_A8R8G8B8)) {
        static int once = 0;
        if (!once) {
            once = 1;
            printf("[RSX metal] TEX_DEMO checkerboard unit=%u %ux%u uploaded\n", unit, w, h);
        }
    }
    free(px);
}

static int metal_ensure_tex_demo_pso(void)
{
    if (s_draw.tex_demo_pso_ready && s_draw.tex_demo_pso) return 1;
    if (!s_state.device) return 0;
    NSError* err = nil;
    NSString* src = @
        "#include <metal_stdlib>\n"
        "using namespace metal;\n"
        "struct VIn { float3 pos [[attribute(0)]]; float4 col [[attribute(1)]]; };\n"
        "struct VOut { float4 pos [[position]]; float2 uv; };\n"
        "vertex VOut tex_vs(VIn in [[stage_in]]) {\n"
        "  VOut o; o.pos = float4(in.pos.xy, 0, 1);\n"
        "  o.uv = float2(in.col.r, in.col.g); return o;\n"
        "}\n"
        "fragment float4 tex_fs(VOut in [[stage_in]],\n"
        "    texture2d<float> tex [[texture(0)]],\n"
        "    sampler smp [[sampler(0)]]) {\n"
        "  return tex.sample(smp, in.uv);\n"
        "}\n";
    id<MTLLibrary> lib = [s_state.device newLibraryWithSource:src options:nil error:&err];
    if (!lib) {
        printf("[RSX metal] tex demo library failed: %s\n",
               err ? [[err localizedDescription] UTF8String] : "?");
        return 0;
    }
    MTLRenderPipelineDescriptor* desc = [MTLRenderPipelineDescriptor new];
    desc.vertexFunction   = [lib newFunctionWithName:@"tex_vs"];
    desc.fragmentFunction = [lib newFunctionWithName:@"tex_fs"];
    desc.vertexDescriptor = metal_basic_vertex_desc();
    metal_apply_blend_to_attachment(desc.colorAttachments[0]);
    desc.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float_Stencil8;
    desc.stencilAttachmentPixelFormat = MTLPixelFormatDepth32Float_Stencil8;
    id<MTLRenderPipelineState> pso =
        [s_state.device newRenderPipelineStateWithDescriptor:desc error:&err];
    if (!pso) {
        printf("[RSX metal] tex demo PSO failed: %s\n",
               err ? [[err localizedDescription] UTF8String] : "?");
        return 0;
    }
    s_draw.tex_demo_pso = (__bridge id<MTLRenderPipelineState>)CFBridgingRetain(pso);
    s_draw.tex_demo_pso_ready = 1;
    return 1;
}

static void metal_record_tex_demo_triangle(void)
{
    if (!draw_ensure_buffers() || !metal_ensure_tex_demo_pso()) return;
    if (s_draw.draw_count >= METAL_MAX_DRAWS) { s_draw_dropped++; return; }
    if (!s_draw.tex_slots[0].valid)
        metal_tex_install_checkerboard(0, 64, 64);
    u32 off = s_draw.vb_offset;
    MetalBasicVertex* v =
        (MetalBasicVertex*)((u8*)s_draw.vb_mapped + off);
    /* NDC triangle; pack UV in color.rg */
    v[0] = (MetalBasicVertex){  0.f,  0.7f, 0, 0.5f, 0.f, 0, 1 };
    v[1] = (MetalBasicVertex){ -0.7f,-0.7f, 0, 0.f,  1.f, 0, 1 };
    v[2] = (MetalBasicVertex){  0.7f,-0.7f, 0, 1.f,  1.f, 0, 1 };
    s_draw.vb_offset += 3 * (u32)sizeof(MetalBasicVertex);
    MetalDrawRec* dr = &s_draw.draws[s_draw.draw_count++];
    dr->vb_byte_offset = off;
    dr->vertex_count   = 3;
    dr->is_indexed     = 0;
    dr->prim           = MTLPrimitiveTypeTriangle;
    metal_draw_rec_set_pso(dr, s_draw.tex_demo_pso);
}

/* PS3_TRACE_TEX_BIND=1 (OFF, diagnostic): each distinct (exit line, format, size,
 * location) at which bind_texture leaves the unit on the white dummy texture,
 * printed once (cap 256). A draw sampling such a unit reads white -- Kratos renders
 * pure white on the e435 lift with the frame clear on (2026-09-16). */
static void metal_tex_skip_note(u32 unit, const rsx_texture_state* tex, int line)
{
    static int on = -1;
    if (on < 0) on = metal_env_on("PS3_TRACE_TEX_BIND");
    if (!on || !tex) return;
    const u32 w = (tex->image_rect >> 16) & 0xFFFFu, h = tex->image_rect & 0xFFFFu;
    const u32 fmt = rsx_texture_get_format(tex->format);
    const u64 key = ((u64)(u32)line << 48) ^ ((u64)fmt << 40) ^ ((u64)w << 24) ^
                    ((u64)h << 8) ^ (u64)(tex->format & 3u);
    static u64 seen[256];
    static int ns = 0;
    for (int i = 0; i < ns; i++)
        if (seen[i] == key) return;
    if (ns >= 256) return;
    seen[ns++] = key;
    printf("[TEXSKIP] line=%d unit=%u fmt=0x%02X raw=0x%08X %ux%u loc=%u off=0x%08X ctrl1=0x%04X\n",
           line, unit, fmt, tex->format, w, h, tex->format & 3u, tex->offset,
           tex->control1 & 0xFFFFu);
}

static int metal_depth_as_color_on(void);

/* NV4097_SET_TEXTURE_ADDRESS / _FILTER -> MTLSamplerState.
 *
 * Mesma descodificacao que o caminho D3D12 ja' usa (rsx_live_draw.c:325-356):
 * wrap S/T/R nos nibbles 0/8/16 do address; min em filter>>16 & 7, mag em
 * filter>>24 & 7. Os modos do GCM:
 *   1 WRAP  2 MIRROR  3 CLAMP_TO_EDGE  4 BORDER  5 CLAMP
 *   6/7/8 MIRROR_ONCE (variantes)
 * O Metal nao tem MIRROR_ONCE em todas as familias; MirrorClampToEdge existe em
 * macOS, e e' o equivalente. */
static MTLSamplerAddressMode metal_wrap_from_gcm(u32 w)
{
    switch (rsx_core_wrap_from_gcm(w)) {
    case RSX_WRAP_REPEAT:            return MTLSamplerAddressModeRepeat;
    case RSX_WRAP_MIRROR_REPEAT:     return MTLSamplerAddressModeMirrorRepeat;
    case RSX_WRAP_CLAMP_EDGE:        return MTLSamplerAddressModeClampToEdge;
    case RSX_WRAP_CLAMP_BORDER:      return MTLSamplerAddressModeClampToBorderColor;
    case RSX_WRAP_MIRROR_CLAMP_EDGE: return MTLSamplerAddressModeMirrorClampToEdge;
    default:                         return MTLSamplerAddressModeClampToEdge;
    }
}

/* PS3_METAL_SAMPLER=0 (A/B): repoe o sampler fixo de antes do E462. */
static int metal_sampler_real(void)
{
    static int on = -1;
    if (on < 0) { const char* e = getenv("PS3_METAL_SAMPLER"); on = (e && e[0] == '0') ? 0 : 1; }
    return on;
}

/* 64 filled up once the GoW2 Colossus level loaded its own address/filter mixes:
 * every sampler after that fell back to dummy_samp (clamp-to-edge), which turned
 * the Colossus body's wrapped UVs (V 1.07..1.99) into its texture's last row. */
#define METAL_SAMP_CACHE 512u
static struct { u32 key; __unsafe_unretained id<MTLSamplerState> s; } s_samp_cache[METAL_SAMP_CACHE];
static u32 s_samp_count;

static id<MTLSamplerState> metal_sampler_for(u32 addr, u32 filt)
{
    if (!s_state.device) return nil;
    /* So' os campos que usamos entram na chave -- assim o cache nao explode com
     * bits que nao mudam o sampler (bias, gamma, signed remap). */
    const u32 key = rsx_core_sampler_key(addr, filt);
    for (u32 i = 0; i < s_samp_count; i++)
        if (s_samp_cache[i].key == key) return s_samp_cache[i].s;
    if (s_samp_count >= METAL_SAMP_CACHE) {
        static int warned = 0;
        if (!warned++) {
            fprintf(stderr, "[SAMP] cache cheio (%u): samplers novos caem no clamp fixo\n",
                    (unsigned)METAL_SAMP_CACHE);
            fflush(stderr);
        }
        return s_draw.dummy_samp;
    }

    const u32 minf = (filt >> 16) & 0x7u;
    const u32 magf = (filt >> 24) & 0x7u;
    /* GCM min: 1 NEAREST, 2 LINEAR, 3 NEAREST_MIPMAP_NEAREST, 4 LINEAR_MIPMAP_NEAREST,
     *          5 NEAREST_MIPMAP_LINEAR, 6 LINEAR_MIPMAP_LINEAR, 7 CONVOLUTION_MIN.
     *     mag: 1 NEAREST, 2 LINEAR, 4 CONVOLUTION_MAG.
     * O 7 e o 4 faltavam na tabela do D3D12 (rsx_live_draw.c:343) e o GoW2 usa
     * os dois -- medido [SAMP] #3 min=7 mag=4. Ambos sao filtros de convolucao,
     * que no Metal so' temos como linear. */
    MTLSamplerDescriptor* sd = [MTLSamplerDescriptor new];
    sd.minFilter = rsx_core_min_filter_linear(filt) ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
    sd.magFilter = rsx_core_mag_filter_linear(filt) ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
    /* Nao subimos mips (metal_tex_upload_slot cria mipmapped:NO), portanto o
     * filtro de mip fica NotMipmapped -- pedir Linear com um so' nivel nao muda
     * nada e evita um sampler invalido. */
    sd.mipFilter = MTLSamplerMipFilterNotMipmapped;
    sd.sAddressMode = metal_wrap_from_gcm(addr);
    sd.tAddressMode = metal_wrap_from_gcm(addr >> 8);
    sd.rAddressMode = metal_wrap_from_gcm(addr >> 16);
    sd.borderColor  = MTLSamplerBorderColorTransparentBlack;
    id<MTLSamplerState> st = [s_state.device newSamplerStateWithDescriptor:sd];
    if (!st) return s_draw.dummy_samp;
    s_samp_cache[s_samp_count].key = key;
    s_samp_cache[s_samp_count].s = (__bridge id<MTLSamplerState>)CFBridgingRetain(st);
    s_samp_count++;
    {   /* PS3_METAL_TRACE_SAMP=1 (OFF): um linha por sampler NOVO -- diz se o
         * jogo pede mesmo repeat/mirror ou se era tudo clamp (fix sem efeito). */
        static int tr = -1;
        if (tr < 0) { const char* e = getenv("PS3_METAL_TRACE_SAMP"); tr = (e && e[0] == '1') ? 1 : 0; }
        if (tr) {
            static const char* nm[9] = {"?0","REPEAT","MIRROR","CLAMP_EDGE","BORDER",
                                        "CLAMP","MIRR1","MIRR1","MIRR1"};
            u32 ws = addr & 0xFu, wt = (addr >> 8) & 0xFu;
            fprintf(stderr, "[SAMP] #%u addr=0x%08X filt=0x%08X wrapS=%s wrapT=%s min=%u mag=%u\n",
                    s_samp_count, addr, filt, nm[ws > 8 ? 0 : ws], nm[wt > 8 ? 0 : wt], minf, magf);
            fflush(stderr);
        }
    }
    return s_samp_cache[s_samp_count - 1].s;
}

static void metal_bind_texture(void* ud, u32 unit, const rsx_texture_state* tex)
{
    (void)ud;
    if (!tex || unit >= RSX_MAX_TEXTURES) return;

    u32 width  = (tex->image_rect >> 16) & 0xFFFF;
    u32 height = tex->image_rect & 0xFFFF;
    u32 format = rsx_texture_get_format(tex->format);
    u32 ea     = metal_resolve_tex_ea(tex);
    int is_swizzled = rsx_texture_is_swizzled(tex->format);

    /* E462: o sampler tem de sair dos registadores do RSX. O caminho Metal
     * usava UM sampler fixo (Linear/Linear, ClampToEdge em S e T) para todas as
     * unidades -- o D3D12 ja' traduzia (rsx_live_draw.c:325 gcm_wrap). Com
     * clamp no lugar de repeat, qualquer UV fora de [0,1] estica o texel da
     * borda por toda a superficie: e' assim que um modelo fica com um bloco
     * chapado de uma cor so' e uma particula tiled vira uma mancha. */
    s_draw.tex_slots[unit].addr = tex->address;
    s_draw.tex_slots[unit].filt = tex->filter;

    {   /* PS3_TRACE_TEX_AT=<offset hex> (diagnostico, OFF): cada bind dessa
         * textura -- localizacao (1 local / 2 memoria principal), tamanho, EA,
         * se existe superficie nossa nesse offset (RTT) e quem escreveu por
         * ultimo o EA segundo o mapa de PUT do SPU (PS3_TRACE_SPU_PUT=1). Existe
         * para a piscada do menu: a composicao final le' 0x00200000 e isso nao
         * e' render target nosso, logo vem da memoria do guest -- quem a enche? */
        static int parsed = 0; static u32 want = 0;
        if (!parsed) { const char* e = getenv("PS3_TRACE_TEX_AT"); want = e ? (u32)strtoul(e, NULL, 16) : 0u; parsed = 1; }
        if (want && tex->offset == want) {
            static u32 nlog;
            static u32 last_sig = 0xFFFFFFFFu;
            /* So' quando o conteudo muda (assinatura dos primeiros 64 bytes). */
            u32 sig = 2166136261u;
            if (ea && ppu_guest_range_committed(ea, 64))
                for (int b = 0; b < 64; b++) sig = (sig ^ vm_base[ea + b]) * 16777619u;
            const int changed = sig != last_sig;
            last_sig = sig;
            if (changed && nlog++ < 300) {
                char hx[140]; int hp = 0;
                for (int b = 0; b < 64 && ea && hp < 130; b += 4) {
                    u32 w; memcpy(&w, vm_base + ea + b, 4);
                    hp += snprintf(hx + hp, sizeof hx - (size_t)hp, "%08X ", __builtin_bswap32(w));
                }
                fprintf(stderr, "[TEXAT] mudou f=%llu fmt=0x%02X %ux%u bytes=%s\n",
                        (unsigned long long)s_state.frame_count, format, width, height, hx);
                const int sc = metal_surface_find((tex->format & 3u) == 2u ? 1u : 0u, tex->offset, 0);
                const int sd = metal_surface_find(0, tex->offset, 1);
                spu_put_rec r; memset(&r, 0, sizeof r);
                const int who = ea ? spu_put_owner(ea, &r) : 0;
                u32 w0 = 0; if (ea) { memcpy(&w0, vm_base + ea, 4); w0 = __builtin_bswap32(w0); }
                fprintf(stderr, "[TEXAT] f=%llu unit=%u off=0x%08X loc=%u %ux%u fmt=0x%02X ea=0x%08X "
                        "surfCor=S%d surfZ=S%d primeira_palavra=0x%08X spu=%s img=%d pc=0x%05X seq=%llu\n",
                        (unsigned long long)s_state.frame_count, unit, tex->offset, tex->format & 3u,
                        width, height, format, ea, sc, sd, w0, who ? "SIM" : "nao",
                        who ? r.image_id : -1, who ? r.pc : 0u, who ? r.seq : 0ull);
            }
        }
    }

    {
        static int log_hd = 0, log_other = 0;
        int is_hd = (width == 1280 && height == 720);
        if ((!is_hd && log_other < 64) || (is_hd && log_hd < 8)) {
            printf("[RSX metal] bind_texture(unit=%u, off=0x%X, ea=0x%08X, fmt=0x%02X, "
                   "%ux%u, swz=%d)%s\n",
                   unit, tex->offset, ea, format, width, height, is_swizzled,
                   is_hd ? "" : " [CONTENT?]");
            if (is_hd) log_hd++; else log_other++;
        }
        {
            static int tl_n = 0;
            if (metal_tl_on() && tl_n++ < 80)
                METAL_TL("bind unit=%u ea=0x%08X fmt=0x%02X %ux%u swz=%d",
                         unit, ea, format, width, height, is_swizzled);
        }
    }

    if (!vm_base || !s_state.device || width == 0 || height == 0) {
        s_draw.bind_skip++; metal_tex_skip_note(unit, tex, __LINE__);
        return;
    }

    /* Render-to-texture (per-draw mode): a unit pointed at a local colour
     * surface reads what the GPU rendered there, not guest memory -- surfaces
     * live only in Metal (E440: the frame composite sampled zeros and the
     * screen was black). Same idea as upstream 247251b0 for D3D12. */
    /* Localizacao da textura: 1 = VRAM local, 2 = memoria principal. Um render
     * target desenhado em memoria principal (alvo B do MRT do menu do GoW2) so'
     * existe no Metal -- ler os bytes do guest dava zeros. O RPCS3 procura a
     * superficie pelo endereco absoluto, nas duas localizacoes. */
    const u32 tex_loc = tex->format & 0x3u;
    if (metal_per_draw_rt() && (tex_loc == 1u || (tex_loc == 2u && metal_mrt_on()))) {
        int k = metal_surface_find(tex_loc == 2u ? 1u : 0u, tex->offset, 0);
        if (k >= 0 && s_surfaces[k].tex) {
            MetalTexSlot* slot = &s_draw.tex_slots[unit];
            if (slot->tex != s_surfaces[k].tex) {
                if (slot->tex) CFRelease((__bridge CFTypeRef)slot->tex);
                CFRetain((__bridge CFTypeRef)s_surfaces[k].tex);
                slot->tex = s_surfaces[k].tex;
            }
            slot->w = (u32)s_surfaces[k].tex.width;
            slot->h = (u32)s_surfaces[k].tex.height;
            slot->rsx_fmt = format;
            slot->valid = 1;
            s_draw.bind_ok++;
            static int rtt_log = 0;
            if (rtt_log++ < 16)
                printf("[RSX metal] bind_texture unit=%u -> surface[%d] off=0x%08X "
                       "tex %ux%u surf %lux%lu (RTT)\n", unit, k, tex->offset, width, height,
                       (unsigned long)s_surfaces[k].tex.width,
                       (unsigned long)s_surfaces[k].tex.height);
            return;
        }
        /* A depth surface read through a colour format (see metal_depth_as_color_on):
         * the slot holds the depth texture; replay binds its BGRA8 conversion. */
        int kd = metal_depth_as_color_on() ? metal_surface_find(0, tex->offset, 1) : -1;
        if (kd >= 0 && s_surfaces[kd].tex) {
            MetalTexSlot* slot = &s_draw.tex_slots[unit];
            if (slot->tex != s_surfaces[kd].tex) {
                if (slot->tex) CFRelease((__bridge CFTypeRef)slot->tex);
                CFRetain((__bridge CFTypeRef)s_surfaces[kd].tex);
                slot->tex = s_surfaces[kd].tex;
            }
            slot->w = (u32)s_surfaces[kd].tex.width;
            slot->h = (u32)s_surfaces[kd].tex.height;
            slot->rsx_fmt = format;
            slot->valid = 1;
            s_draw.bind_ok++;
            static int dlog = 0;
            if (dlog < 8) {
                dlog++;
                printf("[RSX metal] bind_texture unit=%u -> depth surface[%d] off=0x%08X fmt=0x%02X "
                       "remap=0x%04X %ux%u (depth as colour)\n", unit, kd, tex->offset, format,
                       (unsigned)(tex->control1 & 0xFFFFu), width, height);
            }
            return;
        }
    }

    /* A1/menu diag: sample guest texels (gated). Shows whether UI/WAD data
     * ever lands in VRAM-facing guest memory vs only HOSTRES hold path. */
    {
        static int texpx = -1;
        if (texpx < 0) {
            const char* e = getenv("PS3_TRACE_TEX_PIXELS");
            texpx = (e && e[0] && e[0] != '0') ? 1 : 0;
        }
        if (texpx && ppu_guest_range_committed(ea, 256)) {
            static u32 last_ea = 0;
            static u64 nlog = 0;
            int first = (ea != last_ea);
            last_ea = ea;
            if (first || (nlog++ % 120ull) == 0ull) {
                const u8* p = vm_base + ea;
                u32 nz = 0, mx = 0;
                /* fmt 0x86=DXT1: sample compressed bytes, not RGB layout. */
                u32 nbytes = (format == 0x86u || format == 0x87u || format == 0x88u)
                    ? ((width + 3) / 4) * ((height + 3) / 4) * 8u
                    : width * height * 4u;
                if (nbytes > 8192u) nbytes = 8192u;
                for (u32 i = 0; i < nbytes; i++) {
                    if (p[i] > mx) mx = p[i];
                    if (p[i]) nz++;
                }
                fprintf(stderr,
                        "[TEX-PX] ea=0x%08X %ux%u fmt=0x%02X nonzero_bytes=%u/%u max=%u%s\n",
                        ea, width, height, format, nz, nbytes, mx,
                        first ? " NEW" : "");
                fflush(stderr);
            }
        }
    }

    MTLPixelFormat mtl_fmt = MTLPixelFormatBGRA8Unorm;
    int is_bc = 0;
    if (!metal_format_from_rsx(format, &mtl_fmt, &is_bc)) {
        static int fmt_log = 0;
        if (fmt_log++ < 5)
            printf("[RSX metal] bind_texture: unit=%u fmt=0x%02X unsupported, keep dummy\n",
                   unit, format);
        s_draw.bind_skip++; metal_tex_skip_note(unit, tex, __LINE__);
        return;
    }
    const u32 remap = (tex->control1 & 0xFFFFu) ? (tex->control1 & 0xFFFFu) : 0xAAE4u;
    const MTLTextureSwizzleChannels tsw = metal_remap_swizzle(remap, format, mtl_fmt);
    {
        static int tr = -1, ns = 0;
        static u32 seen[16];
        if (tr < 0) tr = getenv("PS3_TRACE_METAL_PERDRAW") != NULL;
        u32 key = (format << 16) | remap;
        int f = 0;
        for (int i = 0; i < ns; i++) if (seen[i] == key) f = 1;
        if (tr && !f && ns < 16) {
            seen[ns++] = key;
            printf("[RSX metal] tex remap fmt=0x%02X control1=0x%04X -> r=%d g=%d b=%d a=%d\n",
                   format, remap, (int)tsw.red, (int)tsw.green, (int)tsw.blue, (int)tsw.alpha);
        }
    }

    u32 bpp = rsx_texture_bpp(format);
    u32 row_bytes = rsx_texture_row_pitch(format, width);
    u32 src_rows  = rsx_texture_src_rows(format, height);
    int do_deswizzle = is_swizzled && !is_bc;
    /* RSX 2D textures are at most 4096 per side. Sized in 64 bits: in u32 the
     * padded Morton area wraps to 0 for sides >= 32768, so the committed-range
     * guard below passed with 0 bytes and the deswizzle read up to 4 GB
     * (E432: SIGBUS in _platform_memmove <- rsx_deswizzle_texels). */
    if (width > 4096u || height > 4096u) {
        static int big = 0;
        if (big++ < 5)
            printf("[RSX metal] bind_texture: unit=%u ea=0x%08X %ux%u exceeds 4096 -> skip\n",
                   unit, ea, width, height);
        s_draw.bind_skip++; metal_tex_skip_note(unit, tex, __LINE__);
        return;
    }
    const unsigned long long guest_bytes64 = do_deswizzle
        ? (unsigned long long)rsx_morton_padded_texels(width, height) * (bpp / 8u)
        : (unsigned long long)row_bytes * src_rows;
    if (guest_bytes64 == 0 || guest_bytes64 > 0x10000000ull) {
        s_draw.bind_skip++; metal_tex_skip_note(unit, tex, __LINE__);
        return;
    }
    u32 guest_bytes = (u32)guest_bytes64;
    if (!ppu_guest_range_committed(ea, guest_bytes)) {
        static int nc = 0;
        if (nc++ < 5)
            printf("[RSX metal] bind_texture: unit=%u ea=0x%08X (%u B) not committed\n",
                   unit, ea, guest_bytes);
        s_draw.bind_skip++; metal_tex_skip_note(unit, tex, __LINE__);
        return;
    }

    /* Texture cache hit: same key and same sampled content -> no deswizzle,
     * no upload. */
    const int use_texc = metal_tex_cache_on();
    u64 tfp = 0;
    MetalTexCacheEnt* tce = NULL;
    if (use_texc) {
        tce = metal_texc_find(ea, format, width, height, (u32)do_deswizzle, remap);
        /* O hash da textura era 16% do tempo do decode do FIFO, e o mesmo
         * atlas e' ligado dezenas de vezes no MESMO frame. Uma entrada ja'
         * validada neste frame nao e' re-hasheada: o conteudo so' pode ter
         * mudado desde o ultimo bind, e entre o hash e o draw a janela ja'
         * existia. PS3_METAL_TEXC_REHASH=1 repoe o hash por bind (A/B). */
        static int rehash = -1;
        if (rehash < 0) rehash = metal_env_on("PS3_METAL_TEXC_REHASH");
        if (tce && !rehash && tce->last_frame == s_state.frame_count)
            tfp = tce->fp;
        else
            tfp = metal_tex_fingerprint(vm_base + ea, guest_bytes);
        if (tce && tce->fp == tfp) {
            /* PS3_METAL_TEXC_AUDIT=1 (OFF): a fingerprint amostrada pode nao
             * ver uma alteracao fora das janelas, e entao o cache serve a
             * textura VELHA. Este audit calcula o hash COMPLETO e compara com o
             * hash completo GUARDADO NESTA ENTRADA no upload.
             *
             * A primeira versao deste audit comparava com o ultimo hash do
             * BUCKET, nao da entrada -- como 4 texturas partilham um bucket,
             * media aliasing e nao staleness, e dava 3,9% de falsos positivos
             * que nao mexiam quando a amostragem aumentava. */
            { static int au = -1;
              if (au < 0) { const char* e = getenv("PS3_METAL_TEXC_AUDIT"); au = (e && *e == '1') ? 1 : 0; }
              if (au) {
                  u64 full = 1469598103934665603ull ^ (u64)guest_bytes;
                  const u8* q = vm_base + ea;
                  for (u32 i = 0; i < guest_bytes; i++) { full ^= q[i]; full *= 1099511628211ull; }
                  static u64 s_audit_n, s_audit_bad;
                  s_audit_n++;
                  if (tce->fp_full && tce->fp_full != full) s_audit_bad++;
                  if ((s_audit_n % 2000ull) == 0ull)
                      fprintf(stderr, "[TEXAUDIT] acertos=%llu com conteudo DIFERENTE=%llu (%.2f%%)\n",
                              (unsigned long long)s_audit_n, (unsigned long long)s_audit_bad,
                              100.0 * (double)s_audit_bad / (double)s_audit_n);
              } }
            tce->last_frame = s_state.frame_count;
            metal_tex_slot_set(unit, tce->tex, width, height, format);
            s_draw.bind_ok++;
            s_texc_hits++;
            return;
        }
    }

    /* 32bpp Morton: queue a compute write into the MTLTexture; present encodes
     * the burst on the same command buffer as the draws (no per-bind wait). */
    if (do_deswizzle && bpp == 32 && use_texc &&
        metal_env_default("PS3_METAL_GPU_DESWIZZLE", 1) && metal_rmd_ready()) {
        id<MTLTexture> write = metal_tex_new_rw(mtl_fmt, width, height);
        if (write && rmd_queue_push(&s_rmd_q, s_state.device, vm_base + ea,
                                    guest_bytes, write, width, height) == 0) {
            id<MTLTexture> sampled = write;
            const int ident =
                tsw.red == MTLTextureSwizzleRed && tsw.green == MTLTextureSwizzleGreen &&
                tsw.blue == MTLTextureSwizzleBlue && tsw.alpha == MTLTextureSwizzleAlpha;
            if (!ident) {
                id<MTLTexture> view =
                    [write newTextureViewWithPixelFormat:mtl_fmt
                                             textureType:MTLTextureType2D
                                                  levels:NSMakeRange(0, 1)
                                                  slices:NSMakeRange(0, 1)
                                                 swizzle:tsw];
                if (view) {
                    CFBridgingRetain(view);
                    CFRelease((__bridge CFTypeRef)write);
                    sampled = view;
                }
            }
            if (tce) {
                s_texc_changed++;
                CFRelease((__bridge CFTypeRef)tce->tex);
                tce->tex = nil;
            } else {
                tce = metal_texc_victim(ea, format, width, height);
            }
            tce->ea = ea;
            tce->fmt = format;
            tce->w = width;
            tce->h = height;
            tce->swz = (u32)do_deswizzle;
            tce->remap = remap;
            tce->fp = tfp;
            tce->last_frame = s_state.frame_count;
            tce->tex = sampled;
            metal_tex_slot_set(unit, sampled, width, height, format);
            s_draw.bind_ok++;
            s_draw.bind_deswizzle++;
            return;
        }
        if (write)
            CFRelease((__bridge CFTypeRef)write);
    }

    u8* linear_tmp = NULL;
    const u8* pixel_src = vm_base + ea;
    if (do_deswizzle) {
        u32 bppb = bpp / 8u;
        u32 lin_bytes = width * height * bppb;
        linear_tmp = (u8*)malloc(lin_bytes);
        if (!linear_tmp) {
            s_draw.bind_skip++; metal_tex_skip_note(unit, tex, __LINE__);
            return;
        }
        rsx_deswizzle_texels(vm_base + ea, linear_tmp, width, height, bppb);
        pixel_src = linear_tmp;
        s_draw.bind_deswizzle++;
    }

    /* Host upload buffer: BC/DXT and R8 copy; 16-bit expand or swap; 32-bit bswap→BGRA. */
    int expand16 = (!is_bc && bpp == 16 &&
                    (format == RSX_TEXTURE_R5G6B5 || format == RSX_TEXTURE_A1R5G5B5 ||
                     format == RSX_TEXTURE_R5G5B5A1 || format == RSX_TEXTURE_A4R4G4B4));
    u32 upload_row = is_bc ? row_bytes : (expand16 || bpp == 32 || format == RSX_TEXTURE_A8R8G8B8 ||
                                          format == RSX_TEXTURE_D8R8G8B8)
                         ? (expand16 ? width * 4u : row_bytes)
                         : row_bytes;
    if (!is_bc && bpp == 32) upload_row = width * 4u;
    if (expand16) {
        upload_row = width * 4u;
        mtl_fmt = MTLPixelFormatBGRA8Unorm;
    }
    u32 upload_rows = is_bc ? src_rows : height;
    u32 upload_bytes = upload_row * upload_rows;
    u8* upload = (u8*)malloc(upload_bytes);
    if (!upload) {
        free(linear_tmp);
        s_draw.bind_skip++; metal_tex_skip_note(unit, tex, __LINE__);
        return;
    }

    for (u32 y = 0; y < upload_rows; y++) {
        u8* dst_row = upload + (size_t)y * upload_row;
        const u8* src_row = pixel_src + (size_t)y * row_bytes;
        if (is_bc || bpp == 8) {
            memcpy(dst_row, src_row, row_bytes);
        } else if (expand16) {
            for (u32 x = 0; x < width; x++) {
                u16 t;
                memcpy(&t, src_row + (size_t)x * 2, 2);
                t = (u16)((t << 8) | (t >> 8)); /* BE → host */
                u8* p = dst_row + (size_t)x * 4;
                if (format == RSX_TEXTURE_R5G6B5)
                    metal_expand_r5g6b5_to_bgra(t, p);
                else if (format == RSX_TEXTURE_A4R4G4B4)
                    metal_expand_a4r4g4b4_to_bgra(t, p);
                else
                    metal_expand_a1r5g5b5_to_bgra(t, p);
            }
        } else if (bpp == 16) {
            for (u32 x = 0; x < width; x++) {
                u16 t;
                memcpy(&t, src_row + (size_t)x * 2, 2);
                t = (u16)((t << 8) | (t >> 8));
                memcpy(dst_row + (size_t)x * 2, &t, 2);
            }
        } else { /* 32-bit ARGB BE → BGRA LE */
            for (u32 x = 0; x < width; x++) {
                u32 t;
                memcpy(&t, src_row + (size_t)x * 4, 4);
                t = metal_bswap32(t);
                memcpy(dst_row + (size_t)x * 4, &t, 4);
            }
        }
    }

    int ok;
    if (use_texc) {
        id<MTLTexture> nt = metal_tex_new(mtl_fmt, width, height, upload, upload_row, tsw);
        ok = (nt != nil);
        if (nt) {
            if (tce) {
                /* Same key, new content: replace (draw records keep theirs). */
                s_texc_changed++;
                CFRelease((__bridge CFTypeRef)tce->tex);
                tce->tex = nil;
            } else {
                tce = metal_texc_victim(ea, format, width, height);
            }
            tce->ea = ea;
            tce->fmt = format;
            tce->w = width;
            tce->h = height;
            tce->swz = (u32)do_deswizzle;
            tce->remap = remap;
            tce->fp = tfp;
            { /* PS3_METAL_TEXC_AUDIT=1 (OFF): hash completo do que acabamos de subir,
               * para o audit no cache-hit saber se o conteudo mudou sem a fingerprint ver. */
              static int au = -1;
              if (au < 0) { const char* e = getenv("PS3_METAL_TEXC_AUDIT"); au = (e && *e == '1') ? 1 : 0; }
              if (au) { u64 f2 = 1469598103934665603ull ^ (u64)guest_bytes;
                        const u8* q2 = vm_base + ea;
                        for (u32 i = 0; i < guest_bytes; i++) { f2 ^= q2[i]; f2 *= 1099511628211ull; }
                        tce->fp_full = f2; } }
            tce->last_frame = s_state.frame_count;
            tce->tex = nt;   /* owns metal_tex_new's reference */
            metal_tex_slot_set(unit, nt, width, height, format);
            {   /* PS3_METAL_DUMP_TEX=<n> (OFF, diagnostic): write the first n guest
                 * textures the cache uploads, to tell a bad texture from a bad shader --
                 * the Colossus and Kratos both show half the model flat, which is what a
                 * texture whose second half never decoded looks like. */
                static long lim = -1; static u32 ndump = 0;
                if (lim < 0) {
                    const char* e = getenv("PS3_METAL_DUMP_TEX");
                    char* end2 = NULL;
                    long v = (e && *e) ? strtol(e, &end2, 10) : 0;
                    lim = (e && *e && end2 && *end2 == '\0' && v > 0 && v <= 500) ? v : 0;
                }
                /* PS3_METAL_DUMP_TEX_EA=<hex>[,<hex>...] (OFF): dump exactly these
                 * guest textures, whenever uploaded (the Colossus drape's matcap and
                 * bronze mask load long after the first 500). */
                static int ea_n = -1;
                static u32 ea_want[8];
                if (ea_n < 0) {
                    ea_n = 0;
                    const char* e = getenv("PS3_METAL_DUMP_TEX_EA");
                    while (e && *e && ea_n < 8) {
                        char* end3 = NULL;
                        const unsigned long v = strtoul(e, &end3, 16);
                        if (end3 == e) break;
                        ea_want[ea_n++] = (u32)v;
                        e = (*end3 == ',') ? end3 + 1 : end3;
                    }
                }
                int ea_hit = 0;
                for (int k = 0; k < ea_n; k++) if (ea_want[k] == ea) ea_hit = 1;
                if (ea_hit && nt.pixelFormat == MTLPixelFormatBGRA8Unorm) {
                    static u32 nea = 0;
                    char tp[112];
                    snprintf(tp, sizeof(tp), "gtexea_%03u_ea%08X_%ux%u_f%02X_r%08X.bmp", nea++, ea,
                             width, height, (unsigned)(format & 0xFFu), tex->control1);
                    metal_dump_texture_to(nt, tp);
                    fprintf(stderr, "[GTEX-EA] %s addr=0x%08X filt=0x%08X ctl0=0x%08X ctl1=0x%08X swz=%d\n",
                            tp, tex->address, tex->filter, tex->control0, tex->control1, do_deswizzle);
                }
                if (ndump < (u32)lim && width >= 64u && height >= 64u &&
                    nt.pixelFormat == MTLPixelFormatBGRA8Unorm) {
                    ndump++;
                    char tp[96];
                    snprintf(tp, sizeof(tp), "gtex_%03u_ea%08X_%ux%u_f%02X.bmp",
                             ndump, ea, width, height, (unsigned)(format & 0xFFu));
                    metal_dump_texture_to(nt, tp);
                    fprintf(stderr, "[GTEX] #%u ea=0x%08X %ux%u fmt=0x%02X swz=%d\n",
                            ndump, ea, width, height, (unsigned)(format & 0xFFu), do_deswizzle);
                }
            }
            if ((++s_texc_miss % 200u) == 0u)
                printf("[RSX metal] tex cache: hits=%u miss=%u changed=%u\n",
                       s_texc_hits, s_texc_miss, s_texc_changed);
        }
    } else {
        ok = metal_tex_upload_slot(unit, mtl_fmt, width, height, upload, upload_row, format);
    }
    free(upload);
    free(linear_tmp);
    if (ok) {
        s_draw.bind_ok++;
        static int detail = 0;
        if (detail < 10) {
            printf("[RSX metal] texture uploaded: unit=%u %ux%u swz=%d fmt=0x%02X ea=0x%08X\n",
                   unit, width, height, do_deswizzle, format, ea);
            detail++;
        }
    } else {
        s_draw.bind_skip++; metal_tex_skip_note(unit, tex, __LINE__);
    }
}

static int metal_ensure_vp_const_buf(void)
{
    if (s_draw.vp_const_buf) return 1;
    if (!s_state.device) return 0;
    NSUInteger bytes = 1024u * sizeof(float) * 4u;
    id<MTLBuffer> b = [s_state.device newBufferWithLength:bytes
                                                  options:MTLResourceStorageModeShared];
    if (!b) return 0;
    memset(b.contents, 0, bytes);
    s_draw.vp_const_buf = (__bridge id<MTLBuffer>)CFBridgingRetain(b);
    return 1;
}

static MTLVertexDescriptor* metal_basic_vertex_desc(void)
{
    MTLVertexDescriptor* vd = [MTLVertexDescriptor vertexDescriptor];
    vd.attributes[0].format = MTLVertexFormatFloat3;
    vd.attributes[0].offset = 0;
    vd.attributes[0].bufferIndex = 0;
    vd.attributes[1].format = MTLVertexFormatFloat4;
    vd.attributes[1].offset = 12;
    vd.attributes[1].bufferIndex = 0;
    vd.layouts[0].stride = sizeof(MetalBasicVertex);
    vd.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;
    return vd;
}

/* Compiled MSL functions per (fp_hash, original vp_hash): PSO variants for other
 * blend/mask states are built from these without recompiling source. */
#define METAL_MAX_FN_CACHE 512u
typedef struct {
    u32 fp_hash, vp_key;
    int vs_va;      /* vs is the decompiled VP: it reads the per-draw attribute array */
    u16 used_inputs; /* E482 */
    __unsafe_unretained id<MTLFunction> vs;
    __unsafe_unretained id<MTLFunction> fs;
    __unsafe_unretained id<MTLFunction> vs_b;   /* PS3_RSX_SPLIT: VS com a chave ligada */
} MetalShaderFns;
static MetalShaderFns s_fns[METAL_MAX_FN_CACHE];
/* fs -> its library, to specialise the alpha-test variant (function constant 0). */
static struct { __unsafe_unretained id<MTLFunction> fs, fs_at; __unsafe_unretained id<MTLLibrary> lib; }
    s_fs_at[METAL_MAX_FN_CACHE * 2u];
static u32 s_fs_at_n;
static void metal_fs_lib_note(id<MTLFunction> fs, id<MTLLibrary> lib)
{
    if (!fs || !lib || s_fs_at_n >= METAL_MAX_FN_CACHE * 2u) return;
    s_fs_at[s_fs_at_n].fs = (__bridge id<MTLFunction>)CFBridgingRetain(fs);
    s_fs_at[s_fs_at_n].lib = (__bridge id<MTLLibrary>)CFBridgingRetain(lib);
    s_fs_at[s_fs_at_n].fs_at = nil;
    s_fs_at_n++;
}
static id<MTLFunction> metal_fs_alpha_test_variant(id<MTLFunction> fs)
{
    for (u32 i = 0; i < s_fs_at_n; i++) {
        if (s_fs_at[i].fs != fs) continue;
        if (!s_fs_at[i].fs_at) {
            MTLFunctionConstantValues* cv = [MTLFunctionConstantValues new];
            const bool on = true;
            [cv setConstantValue:&on type:MTLDataTypeBool atIndex:0];
            NSError* err = nil;
            id<MTLFunction> f = [s_fs_at[i].lib newFunctionWithName:@"fs_main" constantValues:cv error:&err];
            if (!f) {
                static int n = 0;
                if (n++ < 5) printf("[RSX metal] alpha-test FS variant failed: %s\n",
                                    err ? [[err localizedDescription] UTF8String] : "?");
                return fs;
            }
            s_fs_at[i].fs_at = (__bridge id<MTLFunction>)CFBridgingRetain(f);
        }
        return s_fs_at[i].fs_at;
    }
    return fs;   /* not a decompiled FS (fixed/debug): no variant */
}
static u32 s_fns_count;
static u32 s_fn_compiles, s_pso_builds;
/* VS/FS library compiles that failed (the draw falls back). Overlay counter. */
static u32 s_fn_compile_fail;

/* Shader failures also reach the overlay Logs tab. The ring never blocks
 * (a contended message is dropped), and callers keep their own rate limits. */
static void metal_overlay_shader_log(const char* fmt, ...)
{
    char line[192];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    rsx_overlay_log_write(RSX_OVERLAY_LOG_WARN, line);
}

static MetalShaderFns* metal_fns_find(u32 fp_hash, u32 vp_key)
{
    for (u32 i = 0; i < s_fns_count; i++)
        if (s_fns[i].fp_hash == fp_hash && s_fns[i].vp_key == vp_key) return &s_fns[i];
    return NULL;
}

/* Compile vs_src/fs_src and store the functions under (fp_hash, vp_key),
 * replacing an entry with the same key. The pointer is valid until the next
 * insert. */
/* PS3_RSX_SPLIT=N (diagnostico, OFF): compara uma chave PS3_RSX_FIX_* no MESMO
 * quadro -- metade esquerda da tela SEM ela, metade direita COM ela. N segue o
 * testar_fix.sh: 1 OUT_W1, 2 NO_TC0_HACK, 3 NO_UV_SEED, 4 FOG_X. Cada draw e'
 * desenhado duas vezes (fica mais lento). */
static int metal_split_bit(void)
{
    static int b = -1;
    if (b < 0) {
        const char* e = getenv("PS3_RSX_SPLIT");
        const int n = e ? atoi(e) : 0;
        b = (n == 1) ? 1 : (n == 2) ? 2 : (n == 3) ? 4 : (n == 4) ? 8 : 0;
        if (b) fprintf(stderr, "[SPLIT] metade esquerda SEM / direita COM a chave %d\n", n);
    }
    return b;
}
/* Options for the decompiled VS: preserveInvariance makes the VP's
 * `[[position, invariant]]` binding, so two VPs with the same position math
 * give bit-identical depth (multi-pass on one mesh, LEQUAL without depth
 * write -- the GoW2 Colossus). PS3_METAL_INVARIANT=0 = old compile (A/B). */
static MTLCompileOptions* metal_vs_compile_options(void)
{
    static MTLCompileOptions* opt = nil;
    static int init = 0;
    if (!init) {
        init = 1;
        const char* e = getenv("PS3_METAL_INVARIANT");
        if (!(e && e[0] == '0')) {
            opt = [MTLCompileOptions new];
            if (@available(macOS 11.0, *)) opt.preserveInvariance = YES;
        }
    }
    return opt;
}
static id<MTLFunction> metal_compile_vs(const char* src)
{
    NSError* err = nil;
    id<MTLLibrary> lib = [s_state.device newLibraryWithSource:@(src)
                                                      options:metal_vs_compile_options()
                                                        error:&err];
    if (!lib) {
        static int n = 0;
        s_fn_compile_fail++;
        if (n++ < 5) {
            printf("[RSX metal] VS (split) library failed: %s\n",
                   err ? [[err localizedDescription] UTF8String] : "?");
            metal_overlay_shader_log("Metal VS (split) compile failed");
        }
        return nil;
    }
    return [lib newFunctionWithName:@"vs_main"];
}

static MetalShaderFns* metal_fns_compile_insert(u32 fp_hash, u32 vp_key,
                                                const char* vs_src, const char* fs_src)
{
    if (!s_state.device || !vs_src || !fs_src) return NULL;
    NSError* err = nil;
    id<MTLLibrary> vs_lib = [s_state.device newLibraryWithSource:@(vs_src)
                                                         options:metal_vs_compile_options()
                                                           error:&err];
    if (!vs_lib) {
        static int n = 0;
        s_fn_compile_fail++;
        if (n++ < 5) {
            printf("[RSX metal] VS library failed: %s\n",
                   err ? [[err localizedDescription] UTF8String] : "?");
            metal_overlay_shader_log("Metal VS compile failed fp=%08X vp=%08X",
                                     fp_hash, vp_key);
        }
        return NULL;
    }
    err = nil;
    id<MTLLibrary> fs_lib = [s_state.device newLibraryWithSource:@(fs_src)
                                                         options:nil
                                                           error:&err];
    if (!fs_lib) {
        static int n = 0;
        s_fn_compile_fail++;
        if (n++ < 5) {
            printf("[RSX metal] FS library failed: %s\n",
                   err ? [[err localizedDescription] UTF8String] : "?");
            metal_overlay_shader_log("Metal FS compile failed fp=%08X vp=%08X",
                                     fp_hash, vp_key);
        }
        return NULL;
    }
    id<MTLFunction> vs = [vs_lib newFunctionWithName:@"vs_main"];
    /* A function with function constants must be specialised even when off. */
    MTLFunctionConstantValues* cv_off = [MTLFunctionConstantValues new];
    const bool at_off = false;
    [cv_off setConstantValue:&at_off type:MTLDataTypeBool atIndex:0];
    id<MTLFunction> fs = [fs_lib newFunctionWithName:@"fs_main" constantValues:cv_off error:&err];
    metal_fs_lib_note(fs, fs_lib);
    if (!vs || !fs) {
        static int n = 0;
        s_fn_compile_fail++;
        if (n++ < 5) {
            printf("[RSX metal] vs_main/fs_main missing after decompile\n");
            metal_overlay_shader_log("Metal vs_main/fs_main missing fp=%08X", fp_hash);
        }
        return NULL;
    }
    MetalShaderFns* e = metal_fns_find(fp_hash, vp_key);
    if (e) {
        if (e->vs) CFRelease((__bridge CFTypeRef)e->vs);
        if (e->fs) CFRelease((__bridge CFTypeRef)e->fs);
    } else {
        if (s_fns_count >= METAL_MAX_FN_CACHE) {
            if (s_fns[0].vs) CFRelease((__bridge CFTypeRef)s_fns[0].vs);
            if (s_fns[0].fs) CFRelease((__bridge CFTypeRef)s_fns[0].fs);
            memmove(&s_fns[0], &s_fns[1], (METAL_MAX_FN_CACHE - 1) * sizeof(MetalShaderFns));
            s_fns_count--;
        }
        e = &s_fns[s_fns_count++];
    }
    e->fp_hash = fp_hash;
    e->vp_key  = vp_key;
    e->vs = (__bridge id<MTLFunction>)CFBridgingRetain(vs);
    e->fs = (__bridge id<MTLFunction>)CFBridgingRetain(fs);
    if ((++s_fn_compiles % 50u) == 0u)
        printf("[RSX metal] shader stats: fn_compiles=%u pso_builds=%u fns=%u\n",
               s_fn_compiles, s_pso_builds, s_fns_count);
    return e;
}

/* PS3_METAL_DEBUG_FILL=1 (OFF, diagnostic): every PSO gets a constant-magenta
 * fragment function, to tell "nothing is rasterized" (vertices, viewport,
 * scissor, depth) from "the fragment shader outputs black" (E447: every
 * surface black with all draws recorded).
 * E461: PS3_METAL_DEBUG_FILL=uv paints fract(TEXCOORD0) (+0.25 blue, so a UV of
 * 0 still shows coverage) and =tex paints texture unit 0 sampled at TEXCOORD0,
 * on decompiled-VS PSOs; the fixed pipeline keeps the constant magenta.
 * E464: =blend keeps the game's REAL fragment programs and only forces the colour
 * mask on and blending off -- "the FP computes 0" vs "the blend/mask hides it".
 * 2026-09-16: =tc1 paints TEXCOORD1.rgb and =tc1a its alpha. The character FPs
 * output texture x TEXCOORD1 (the skinned VP's lighting term), and Kratos comes
 * out dark on the throne, white in gameplay, with alpha ~1 saturating the bloom.
 * Returns 0 off, 1 magenta, 2 uv, 3 tex, 4 blend, 5 tc1, 6 tc1a. */
static int metal_debug_fill(void)
{
    static int on = -1;
    if (on < 0) {
        const char* e = getenv("PS3_METAL_DEBUG_FILL");
        if (!e || !*e || *e == '0')        on = 0;
        else if (strcmp(e, "uv") == 0)     on = 2;
        else if (strcmp(e, "tex") == 0)    on = 3;
        else if (strcmp(e, "blend") == 0)  on = 4;
        else if (strcmp(e, "tc1") == 0)    on = 5;
        else if (strcmp(e, "tc1a") == 0)   on = 6;
        else if (strcmp(e, "fpid") == 0)   on = 7;
        else if (strcmp(e, "vpid") == 0)   on = 8;
        else                               on = 1;
    }
    return on;
}

/* A debug mode that replaces the fragment function (4 keeps the game's). */
static int metal_debug_swaps_fs(void)
{
    int m = metal_debug_fill();
    return m >= 1 && m != 4;
}

static id<MTLFunction> metal_debug_fill_fs(void)
{
    static __unsafe_unretained id<MTLFunction> fs = nil;
    static int tried = 0;
    if (fs || tried || !s_state.device) return fs;
    tried = 1;
    NSError* err = nil;
    id<MTLLibrary> lib = [s_state.device
        newLibraryWithSource:@"#include <metal_stdlib>\nusing namespace metal;\n"
                              "fragment float4 dbg_fs() { return float4(1.0, 0.0, 1.0, 1.0); }\n"
                     options:nil error:&err];
    id<MTLFunction> f = lib ? [lib newFunctionWithName:@"dbg_fs"] : nil;
    if (f) fs = (__bridge id<MTLFunction>)CFBridgingRetain(f);
    printf("[RSX metal] debug fill fragment function %s\n", fs ? "ready" : "FAILED");
    return fs;
}

/* Debug fragment for decompiled-VS PSOs: the uv/tex modes read the varyings,
 * so PSIn repeats rsx_fp_decompile_msl's members (Metal links them by name). */
static id<MTLFunction> metal_debug_fill_fs_dec(void)
{
    int mode = metal_debug_fill();
    if (mode < 2 || mode == 4) return metal_debug_fill_fs();
    static __unsafe_unretained id<MTLFunction> fs = nil;
    static int tried = 0;
    if (fs || tried || !s_state.device) return fs ? fs : metal_debug_fill_fs();
    tried = 1;
    NSError* err = nil;
    id<MTLLibrary> lib = [s_state.device
        newLibraryWithSource:@"#include <metal_stdlib>\nusing namespace metal;\n"
            "struct PSIn { float4 position [[position]]; float4 col0; float4 col1; float4 fog;\n"
            "  float4 tc0; float4 tc1; float4 tc2; float4 tc3; float4 tc4; float4 tc5; float4 tc6; float4 tc7; };\n"
            "fragment float4 dbg_uv(PSIn in [[stage_in]]) {\n"
            "  return float4(fract(in.tc0.x), fract(in.tc0.y), 0.25, 1.0); }\n"
            "fragment float4 dbg_tex(PSIn in [[stage_in]],\n"
            "    array<texture2d<float>, 16> rsx_tex [[texture(0)]],\n"
            "    array<sampler, 16> rsx_samp [[sampler(0)]]) {\n"
            "  float4 c = rsx_tex[0].sample(rsx_samp[0], in.tc0.xy);\n"
            "  return float4(c.rgb, 1.0); }\n"
            "fragment float4 dbg_tc1(PSIn in [[stage_in]]) {\n"
            "  return float4(saturate(in.tc1.rgb), 1.0); }\n"
            "fragment float4 dbg_tc1a(PSIn in [[stage_in]]) {\n"
            "  float a = saturate(in.tc1.w); return float4(a, a, a, 1.0); }\n"
                     options:nil error:&err];
    NSString* fname = mode == 2 ? @"dbg_uv" : mode == 3 ? @"dbg_tex" : mode == 5 ? @"dbg_tc1" : @"dbg_tc1a";
    id<MTLFunction> f = lib ? [lib newFunctionWithName:fname] : nil;
    if (f) fs = (__bridge id<MTLFunction>)CFBridgingRetain(f);
    printf("[RSX metal] debug fill (%s) fragment function %s %s\n", [fname UTF8String],
           fs ? "ready" : "FAILED", (!fs && err) ? [[err localizedDescription] UTF8String] : "");
    return fs ? fs : metal_debug_fill_fs();
}

/* A PSO for already-compiled functions in the current blend/mask state. */
/* PS3_METAL_DEBUG_FILL_FP=HEX[,HEX...] (up to 16): apply PS3_METAL_DEBUG_FILL only
 * to PSOs of these fragment programs, keeping the real shaders elsewhere, so the
 * characters (FPs CEFEF8CE, F91F0216, F949090C) can be inspected inside the normal
 * frame. Unset = every PSO (the old behaviour). */
static int metal_debug_fp_selected(u32 fp_hash)
{
    static int parsed = 0, n = 0;
    static u32 list[16];
    if (!parsed) {
        parsed = 1;
        const char* e = getenv("PS3_METAL_DEBUG_FILL_FP");
        while (e && *e && n < 16) {
            char* end = NULL;
            errno = 0;
            unsigned long v = strtoul(e, &end, 16);
            if (end == e || errno == ERANGE || v > 0xFFFFFFFFul) break;
            list[n++] = (u32)v;
            e = (*end == ',') ? end + 1 : end;
            if (*end != ',') break;
        }
        if (n) printf("[RSX metal] debug fill restricted to %d fragment program(s)\n", n);
    }
    if (n == 0) return 1;
    for (int i = 0; i < n; i++)
        if (list[i] == fp_hash) return 1;
    return 0;
}

/* PS3_METAL_DEBUG_FILL_VP=HEX[,HEX...] (OFF): restrict the debug fill to these
 * vertex programs (the key passed to metal_pso_from_fns). The FP CEFEF8CE draws
 * scene, soldiers and Kratos alike; only the skinning VP singles Kratos out, and
 * vpid colours were unreadable under additive passes (2026-09-16). */
static int metal_debug_vp_selected(u32 vp_key)
{
    static int parsed = 0, n = 0;
    static u32 list[16];
    if (!parsed) {
        parsed = 1;
        const char* e = getenv("PS3_METAL_DEBUG_FILL_VP");
        while (e && *e && n < 16) {
            char* end = NULL;
            errno = 0;
            unsigned long v = strtoul(e, &end, 16);
            if (end == e || errno == ERANGE || v > 0xFFFFFFFFul) break;
            list[n++] = (u32)v;
            e = (*end == ',') ? end + 1 : end;
            if (*end != ',') break;
        }
        if (n) printf("[RSX metal] debug fill restricted to %d vertex program(s)\n", n);
    }
    if (n == 0) return 1;
    for (int i = 0; i < n; i++)
        if (list[i] == vp_key) return 1;
    return 0;
}

/* PS3_METAL_SKIP_FP / PS3_METAL_SKIP_VP = HEX[,HEX...] (OFF, diagnostic): the
 * per-draw replay drops draws whose FP (and, when both lists are set, VP) match --
 * removes one effect to see which draw paints an artefact (2026-09-16: blocky
 * white slabs only in the post target S8). */
static int metal_hex_list_parse(const char* name, u32* list, int cap)
{
    int n = 0;
    const char* e = getenv(name);
    while (e && *e && n < cap) {
        char* end = NULL;
        errno = 0;
        unsigned long v = strtoul(e, &end, 16);
        if (end == e || errno == ERANGE || v > 0xFFFFFFFFul) break;
        list[n++] = (u32)v;
        if (*end != ',') break;
        e = end + 1;
    }
    return n;
}

static int metal_debug_skip_draw(u32 fp_hash, u32 vp_hash)
{
    static int parsed = 0, nfp = 0, nvp = 0;
    static u32 fps[16], vps[16];
    if (!parsed) {
        parsed = 1;
        nfp = metal_hex_list_parse("PS3_METAL_SKIP_FP", fps, 16);
        nvp = metal_hex_list_parse("PS3_METAL_SKIP_VP", vps, 16);
        if (nfp || nvp) printf("[RSX metal] debug skip: %d FP, %d VP\n", nfp, nvp);
    }
    if (!nfp && !nvp) return 0;
    int fm = !nfp, vm = !nvp;
    for (int i = 0; i < nfp && !fm; i++) fm = (fps[i] == fp_hash);
    for (int i = 0; i < nvp && !vm; i++) vm = (vps[i] == vp_hash);
    return fm && vm;
}

/* PS3_METAL_DEBUG_FILL=fpid: every fragment program paints one flat colour derived
 * from its hash, with a "[FPID] fp=... rgb=..." legend line per program, so a
 * single scene dump tells which FP draws which object (2026-09-16: Kratos's skin
 * stays white and trial-by-FP cost one run per guess). Cached per hash. */
static id<MTLFunction> metal_fpid_fs(u32 fp_hash, const char* tag)
{
    static u32 keys[128];
    static __unsafe_unretained id<MTLFunction> fns[128];
    static int n = 0;
    for (int i = 0; i < n; i++)
        if (keys[i] == fp_hash) return fns[i];
    if (n >= 128 || !s_state.device) return nil;
    u32 h = fp_hash * 2654435761u;
    const u32 r8 = 64u + ((h >> 24) & 191u), g8 = 64u + ((h >> 12) & 191u), b8 = 64u + ((h >> 4) & 191u);
    char src[256];
    snprintf(src, sizeof(src),
             "#include <metal_stdlib>\nusing namespace metal;\n"
             "fragment float4 dbg_fpid() { return float4(%.4f, %.4f, %.4f, 1.0); }\n",
             r8 / 255.0, g8 / 255.0, b8 / 255.0);
    NSError* err = nil;
    id<MTLLibrary> lib = [s_state.device newLibraryWithSource:@(src) options:nil error:&err];
    id<MTLFunction> f = lib ? [lib newFunctionWithName:@"dbg_fpid"] : nil;
    if (!f) return nil;
    keys[n] = fp_hash;
    fns[n] = (__bridge id<MTLFunction>)CFBridgingRetain(f);
    n++;
    fprintf(stderr, "[%s] hash=%08X rgb=(%u,%u,%u)\n", tag, fp_hash, r8, g8, b8);
    return fns[n - 1];
}

static id<MTLRenderPipelineState> metal_pso_from_fns(id<MTLFunction> vs, id<MTLFunction> fs,
                                                     u32 fp_hash, u32 vp_key)
{
    if (!s_state.device || !vs || !fs) return nil;
    const int dbg = metal_debug_fill() && metal_debug_fp_selected(fp_hash) &&
                    metal_debug_vp_selected(vp_key);
    MTLRenderPipelineDescriptor* desc = [MTLRenderPipelineDescriptor new];
    desc.vertexFunction   = vs;
    /* vpid colours by vertex program: the FP CEFEF8CE draws scene, soldiers and
     * Kratos alike, so only the VP (skinning variant) can single Kratos out. */
    id<MTLFunction> idf = !dbg ? nil
                        : metal_debug_fill() == 7 ? metal_fpid_fs(fp_hash, "FPID")
                        : metal_debug_fill() == 8 ? metal_fpid_fs(vp_key, "VPID")
                        : nil;
    if (s_pipe.alpha_test_enable && metal_atest_on()) fs = metal_fs_alpha_test_variant(fs);
    desc.fragmentFunction = idf ? idf
                          : (dbg && metal_debug_fill() < 7 && metal_debug_swaps_fs() &&
                             metal_debug_fill_fs_dec())
                                ? metal_debug_fill_fs_dec() : fs;
    desc.vertexDescriptor = metal_basic_vertex_desc();
    metal_apply_blend_to_attachment(desc.colorAttachments[0]);
    /* MRT: os alvos extra recebem o mesmo blend e mascara (o RSX tem mascaras e
     * enable de blend por alvo -- SET_COLOR_MASK_MRT / SET_BLEND_ENABLE_MRT --
     * ainda nao seguidos; o GoW2 do menu nao os distingue). */
    for (int mi = 1; mi < s_mrt_n && mi < 4; mi++)
        metal_apply_blend_to_attachment(desc.colorAttachments[mi]);
    /* ID modes (7/8) keep the game's blend and mask and output alpha 0: opaque
     * draws show their ID colour, SRC_ALPHA overlays vanish. Forcing blend off
     * let full-screen overlay passes paint the whole scene target flat. */
    if (dbg && metal_debug_fill() < 7) {
        /* A constant FS alone is not enough: the game's colour mask (a depth
         * pre-pass writes no colour) or a multiplicative blend onto a black
         * target still hides it (E453/E454: scene target 0 % magenta). */
        desc.colorAttachments[0].writeMask = MTLColorWriteMaskAll;
        desc.colorAttachments[0].blendingEnabled = NO;
    }
    desc.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float_Stencil8;
    desc.stencilAttachmentPixelFormat = MTLPixelFormatDepth32Float_Stencil8;
    NSError* err = nil;
    id<MTLRenderPipelineState> pso =
        [s_state.device newRenderPipelineStateWithDescriptor:desc error:&err];
    if (!pso) {
        static int n = 0;
        if (n++ < 5)
            printf("[RSX metal] decompiled PSO create failed: %s\n",
                   err ? [[err localizedDescription] UTF8String] : "?");
        return nil;
    }
    s_pso_builds++;
    return pso;
}

/* Body moved to rsx_core_decode.h as `static inline` (per-texel/per-vertex
 * hot path; no LTO in this build -- see the note at metal_va_fp_fast). */
static u32 metal_bswap32(u32 v) { return rsx_core_bswap32(v); }

static MTLPrimitiveType metal_map_prim(u32 rsx_prim)
{
    switch (rsx_prim) {
    case RSX_PRIMITIVE_POINTS:         return MTLPrimitiveTypePoint;
    case RSX_PRIMITIVE_LINES:          return MTLPrimitiveTypeLine;
    case RSX_PRIMITIVE_LINE_STRIP:     return MTLPrimitiveTypeLineStrip;
    case RSX_PRIMITIVE_LINE_LOOP:      return MTLPrimitiveTypeLineStrip;
    case RSX_PRIMITIVE_TRIANGLES:      return MTLPrimitiveTypeTriangle;
    case RSX_PRIMITIVE_TRIANGLE_STRIP: return MTLPrimitiveTypeTriangleStrip;
    /* fan/quads: draw as triangle list only if caller expands; skip raw */
    default:                           return (MTLPrimitiveType)0xFFFFFFFFu;
    }
}

static int draw_ensure_pso(void)
{
    u32 want_key = metal_pipe_blend_key();
    if (s_draw.pso_ready && s_draw.pso && s_pipe.blend_key == want_key) return 1;
    if (s_draw.pso && s_pipe.blend_key != want_key) {
        CFRelease((__bridge CFTypeRef)s_draw.pso);
        s_draw.pso = nil;
        s_draw.pso_ready = 0;
    }
    if (!s_state.device) return 0;

    /* The fixed pipeline is one shader in a few blend/mask variants: compile its
     * source once and keep the variants (E440: rebuilding it from source on
     * every blend change was 4 266 compiles in ~1000 s). */
    static __unsafe_unretained id<MTLFunction> fixed_vs = nil;
    static __unsafe_unretained id<MTLFunction> fixed_fs = nil;
    static struct { u32 key; __unsafe_unretained id<MTLRenderPipelineState> pso; } var[16];
    static u32 nvar = 0;
    const u32 skey = metal_pso_state_key();
    for (u32 i = 0; i < nvar; i++) {
        if (var[i].key == skey && var[i].pso) {
            if (s_draw.pso) CFRelease((__bridge CFTypeRef)s_draw.pso);
            CFRetain((__bridge CFTypeRef)var[i].pso);
            s_draw.pso = var[i].pso;
            s_draw.pso_ready = 1;
            s_pipe.blend_key = want_key;
            return 1;
        }
    }

    NSError* err = nil;
    if (!fixed_vs || !fixed_fs) {
    NSString* src = @
        "#include <metal_stdlib>\n"
        "using namespace metal;\n"
        "struct VIn {\n"
        "  float3 pos [[attribute(0)]];\n"
        "  float4 col [[attribute(1)]];\n"
        "};\n"
        "struct VOut { float4 pos [[position]]; float4 col; };\n"
        "vertex VOut rsx_vs(VIn in [[stage_in]]) {\n"
        "  VOut o; o.pos = float4(in.pos.xy, 0, 1); o.col = in.col; return o;\n"
        "}\n"
        "fragment float4 rsx_fs(VOut in [[stage_in]]) { return in.col; }\n";

    id<MTLLibrary> lib = [s_state.device newLibraryWithSource:src options:nil error:&err];
    if (!lib) {
        printf("[RSX metal] draw PSO library failed: %s\n",
               err ? [[err localizedDescription] UTF8String] : "?");
        return 0;
    }
    id<MTLFunction> vs = [lib newFunctionWithName:@"rsx_vs"];
    id<MTLFunction> fs = [lib newFunctionWithName:@"rsx_fs"];
    if (!vs || !fs) return 0;
    fixed_vs = (__bridge id<MTLFunction>)CFBridgingRetain(vs);
    fixed_fs = (__bridge id<MTLFunction>)CFBridgingRetain(fs);
    }

    MTLRenderPipelineDescriptor* desc = [MTLRenderPipelineDescriptor new];
    desc.vertexFunction   = fixed_vs;
    /* The fixed pipeline has no guest programs: a FP/VP-restricted fill leaves it alone. */
    const int fixed_dbg = metal_debug_fill() && metal_debug_fp_selected(0u) && metal_debug_vp_selected(0u);
    desc.fragmentFunction = (fixed_dbg && metal_debug_swaps_fs() && metal_debug_fill_fs())
                                ? metal_debug_fill_fs() : fixed_fs;
    desc.vertexDescriptor = metal_basic_vertex_desc();
    metal_apply_blend_to_attachment(desc.colorAttachments[0]);
    if (fixed_dbg) {
        desc.colorAttachments[0].writeMask = MTLColorWriteMaskAll;
        desc.colorAttachments[0].blendingEnabled = NO;
    }
    desc.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float_Stencil8;
    desc.stencilAttachmentPixelFormat = MTLPixelFormatDepth32Float_Stencil8;

    id<MTLRenderPipelineState> pso =
        [s_state.device newRenderPipelineStateWithDescriptor:desc error:&err];
    if (!pso) {
        printf("[RSX metal] draw PSO create failed: %s\n",
               err ? [[err localizedDescription] UTF8String] : "?");
        return 0;
    }
    if (s_draw.pso) CFRelease((__bridge CFTypeRef)s_draw.pso);
    s_draw.pso = (__bridge id<MTLRenderPipelineState>)CFBridgingRetain(pso);
    s_draw.pso_ready = 1;
    s_pipe.blend_key = want_key;
    if (nvar >= 16u) {
        if (var[0].pso) CFRelease((__bridge CFTypeRef)var[0].pso);
        memmove(&var[0], &var[1], 15u * sizeof(var[0]));
        nvar = 15u;
    }
    CFRetain((__bridge CFTypeRef)s_draw.pso);
    var[nvar].key = skey;
    var[nvar].pso = s_draw.pso;
    nvar++;
    printf("[RSX metal] draw path PSO ready (blend=%d mask=0x%08X)\n",
           s_pipe.blend_enable, s_pipe.color_mask);
    return 1;
}

static int draw_ensure_buffers(void)
{
    if (!s_draw.fc_buf && s_state.device) {
        id<MTLBuffer> fc = [s_state.device newBufferWithLength:(NSUInteger)METAL_MAX_DRAWS * METAL_FC_MAX * 16u * METAL_FRAMES_IN_FLIGHT
                                                       options:MTLResourceStorageModeShared];
        if (fc) {
            s_draw.fc_buf = (__bridge id<MTLBuffer>)CFBridgingRetain(fc);
            s_draw.fc_mapped = (float*)s_draw.fc_buf.contents;
        }
    }
    if (s_draw.vb && s_draw.ib && s_draw.vb_mapped && s_draw.ib_mapped) return 1;
    if (!s_state.device) return 0;

    NSUInteger vb_bytes = (NSUInteger)metal_max_verts() * sizeof(MetalBasicVertex);
    NSUInteger ib_bytes = (NSUInteger)METAL_MAX_INDICES * sizeof(u32);
    id<MTLBuffer> vb = [s_state.device newBufferWithLength:vb_bytes
                                                   options:MTLResourceStorageModeShared];
    id<MTLBuffer> ib = [s_state.device newBufferWithLength:ib_bytes
                                                   options:MTLResourceStorageModeShared];
    if (!vb || !ib) {
        printf("[RSX metal] VB/IB alloc failed\n");
        return 0;
    }
    if (s_draw.vb) CFRelease((__bridge CFTypeRef)s_draw.vb);
    if (s_draw.ib) CFRelease((__bridge CFTypeRef)s_draw.ib);
    s_draw.vb = (__bridge id<MTLBuffer>)CFBridgingRetain(vb);
    s_draw.ib = (__bridge id<MTLBuffer>)CFBridgingRetain(ib);
    s_draw.vb_mapped = (MetalBasicVertex*)s_draw.vb.contents;
    s_draw.ib_mapped = (u32*)s_draw.ib.contents;
    return 1;
}

/* IEEE754 half (BE in guest) → float. Bodies moved to rsx_core_decode.h as
 * `static inline` (per-vertex-component hot path in VA_LOOP; no LTO in this
 * build -- see the note at metal_va_fp_fast). */
static float metal_half_be_to_float(const u8* p) { return rsx_core_half_be_to_float(p); }
static float metal_read_f32_be(const u8* p) { return rsx_core_read_f32_be(p); }
static float metal_snorm16_be(const u8* p) { return rsx_core_snorm16_be(p); }

/* Resolve guest EA for an RSX vertex array offset word.
 * NV40 VTXBUF_OFFSET: bit31 = location (0=local, 1=main), [30:0] = offset.
 * Local → 0xC0000000 + offset. Main → IO offset via cellGcm map table. */
extern int gcm_io_offset_to_ea(u32 ioOffset, u32* out_ea);
/* PS3_RSX_VERTEX_BASE=0 ignores VERTEX_DATA_BASE_OFFSET/BASE_INDEX (A/B). */
static int metal_vertex_base_on(void)
{
    static int on = -1;
    if (on < 0) { const char* e = getenv("PS3_RSX_VERTEX_BASE"); on = (e && e[0] == '0') ? 0 : 1; }
    return on;
}

static void metal_vertex_base_note(const rsx_state* st)
{
    static int trace = -1;
    static u32 n = 0;
    if (trace < 0) trace = metal_env_on("PS3_TRACE_METAL_PASSES");
    if (!trace || !st || n >= 12u) return;
    if (st->vertex_data_base_offset == 0u && st->vertex_data_base_index == 0u) return;
    n++;
    fprintf(stderr, "[VBASE] base_offset=0x%08X base_index=%u frame=%llu\n",
            st->vertex_data_base_offset, st->vertex_data_base_index,
            (unsigned long long)s_state.frame_count);
}

static u32 metal_vtx_ea(u32 offset_word, u32 first, u32 i, u32 stride)
{
    u32 loc = (offset_word >> 31) & 1u;
    u32 off = offset_word & 0x7FFFFFFFu;
    if (metal_vertex_base_on() && s_draw.current_rsx) {
        /* RPCS3 get_vertex_offset_from_base: (base + offset) & 0x0FFFFFFF. */
        off = (s_draw.current_rsx->vertex_data_base_offset + off) & 0x0FFFFFFFu;
        metal_vertex_base_note(s_draw.current_rsx);
    }
    u32 base;
    if (loc == 0) {
        base = 0xC0000000u + off;
    } else {
        u32 ea = 0;
        if (gcm_io_offset_to_ea(off, &ea))
            base = ea;
        else
            base = off; /* last-resort: treat as absolute guest EA */
    }
    return base + (first + i) * stride;
}

/* Convert one guest attribute to float4 (xyz + w=1 for pos). */
static int metal_fetch_attrib_f4(const rsx_vertex_attrib* a, u32 first, u32 i,
                                 float out[4], int is_color)
{
    out[0] = out[1] = out[2] = 0.f;
    out[3] = is_color ? 1.f : 1.f;
    if (!a || !a->enabled || !vm_base) return 0;
    u32 stride = a->stride ? a->stride : rsx_vertex_attrib_size(a->type, a->size);
    if (stride == 0) return 0;
    u32 addr = metal_vtx_ea(a->offset, first, i, stride);
    if (!ppu_guest_range_committed(addr, stride < 16 ? 16 : stride)) return 0;
    const u8* src = vm_base + addr;
    u32 n = a->size ? a->size : 4;
    if (n > 4) n = 4;

    switch (a->type) {
    case RSX_VERTEX_TYPE_F:
        for (u32 c = 0; c < n; c++)
            out[c] = metal_read_f32_be(src + c * 4);
        if (!is_color && n < 3) out[2] = 0.f;
        if (is_color && n < 4) out[3] = 1.f;
        return 1;
    case RSX_VERTEX_TYPE_SF:
        for (u32 c = 0; c < n && c < 4; c++)
            out[c] = metal_half_be_to_float(src + c * 2);
        if (!is_color && n < 3) out[2] = 0.f;
        if (is_color && n < 4) out[3] = 1.f;
        return 1;
    case RSX_VERTEX_TYPE_UB: /* normalized */
        for (u32 c = 0; c < n && c < 4; c++)
            out[c] = src[c] / 255.f;
        if (n < 4) out[3] = 1.f;
        return 1;
    case RSX_VERTEX_TYPE_UB256: /* raw 0-255 as float 0-255 → normalize for color */
        for (u32 c = 0; c < n && c < 4; c++)
            out[c] = is_color ? (src[c] / 255.f) : (float)src[c];
        if (is_color && n < 4) out[3] = 1.f;
        return 1;
    case RSX_VERTEX_TYPE_S1:
        for (u32 c = 0; c < n && c < 4; c++)
            out[c] = metal_snorm16_be(src + c * 2);
        if (!is_color && n < 3) out[2] = 0.f;
        if (is_color && n < 4) out[3] = 1.f;
        return 1;
    case RSX_VERTEX_TYPE_S32K: /* signed 16-bit, not normalized */
        for (u32 c = 0; c < n && c < 4; c++)
            out[c] = (float)(int16_t)(u16)((src[c * 2] << 8) | src[c * 2 + 1]);
        return 1;
    case RSX_VERTEX_TYPE_CMP: { /* one BE word: x 11, y 11, z 10 bits, signed normalized */
        u32 w = ((u32)src[0] << 24) | ((u32)src[1] << 16) | ((u32)src[2] << 8) | src[3];
        out[0] = (float)((int32_t)(w << 21) >> 21) / 1023.f;
        out[1] = (float)((int32_t)(w << 10) >> 21) / 1023.f;
        out[2] = (float)((int32_t)w >> 22) / 511.f;
        out[3] = 1.f;
        return 1;
    }
    default:
        return 0;
    }
}

/* Body moved to rsx_core_decode.c (FNV over enabled attribs). */
static u32 metal_attrib_sig(const rsx_vertex_attrib* a16) { return rsx_core_attrib_sig(a16); }

/* M6 vertex attributes. The decompiled VPs read v[0..15], but MetalBasicVertex
 * carries only the position and one float4 (colour or UV), so every other input
 * the game's vertex programs read -- TEXCOORD0 v[8] (read by all 220 VS, E453),
 * v[7], v[2] -- was 0. Each enabled RSX attribute of a draw is decoded to float4
 * into s_draw.va_buf (a vertex = the enabled attributes, in float4) and the VS
 * reads it by vertex_id through a per-draw slot map (buffer(2) map, buffer(3)
 * data). Default ON with PS3_METAL_PER_DRAW_RT; PS3_METAL_VA=0/1 overrides. */
#define METAL_VA_RING_F4 (4u * 1024u * 1024u)   /* float4 per frame: 64 MB */

/* ---------------------------------------------------------------------------
 * E486 -- sincronizacao CPU/GPU dos ring buffers por frame (PS3_METAL_FRAME_FENCE).
 *
 * O defeito: todos estes rings sao UM buffer partilhado que `metal_begin_frame`
 * poe a zero em todo o frame, e o present normal e' `presentDrawable`+`commit`
 * sem espera. MEDIDO com [VARACE]: em 100,0% dos frames de gameplay a CPU
 * comeca a reescrever o ring com a GPU ainda a ler o frame anterior. O que se
 * ve' e' um triangulo gigante a piscar durante um frame -- metade de um quad de
 * ecra inteiro com vertices de outro draw.
 *
 * A correccao e' o padrao do Metal: N regioes, e antes de escrever na regiao k
 * esperar pelo command buffer que a leu ha' N frames. Sem semaforo de proposito
 * -- guardar o proprio command buffer nao pode dessincronizar (um frame que nao
 * chegue a apresentar deixa a entrada a nil e simplesmente nao se espera),
 * enquanto um par wait/signal com o `goto metal_present_after_submit` pelo meio
 * trancava o jogo. Esperar por um buffer ja' terminado devolve logo.
 *
 * Dimensionamento MEDIDO (150 s de gameplay, 828 draws de pico), consumo por frame:
 *   VA 741 366 / 4 194 304 = 17,7%   -> 1/3 do ring chega, folga de 1,9x
 *   VB 183 038 / 1 048 576 = 17,5%   -> idem
 *   IB 491 558 / 1 048 576 = 46,9%   -> NAO cabe em 1/3; o ring de indices
 *                                       triplica (1M -> 3M, +8 MB)
 * O FC tem um slot por draw e draw_count vai ate' METAL_MAX_DRAWS, logo a sua
 * regiao tem de manter a capacidade inteira: o buffer triplica em vez de se
 * dividir. O VC divide-se (1 365 slots/frame para 828 draws de pico) e tem
 * contador proprio de esgotamento, `s_vc_ring_full_draws`, impresso pelo
 * PS3_TRACE_METAL_PASSES -- e' esse o controlo de que dividir o ring nao trocou
 * o artefacto por draws descartados em silencio. */
static int metal_frame_fence_on(void)
{
    static int on = -1;
    /* E486: frame fence defaults safe: PS3_METAL_FRAME_FENCE=0 keeps the unsafe A/B path. */
    if (on < 0) { const char* e = getenv("PS3_METAL_FRAME_FENCE"); on = !(e && e[0] == '0'); }
    return on;
}
/* 1 regiao com o portao desligado = o comportamento de hoje, byte a byte. */
static u32 metal_ring_slots(void) { return metal_frame_fence_on() ? METAL_FRAMES_IN_FLIGHT : 1u; }
static u32 s_frame_slot;
static u32 metal_va_region_f4(void) { return METAL_VA_RING_F4 / metal_ring_slots(); }
static u32 metal_va_base_f4(void)   { return s_frame_slot * metal_va_region_f4(); }
static u32 metal_va_limit_f4(void)  { return metal_va_base_f4() + metal_va_region_f4(); }
static u32 metal_ib_region_idx(void) { return METAL_MAX_INDICES / metal_ring_slots(); }
static u32 metal_ib_base_idx(void)   { return s_frame_slot * metal_ib_region_idx(); }
static u32 metal_ib_limit_idx(void)  { return metal_ib_base_idx() + metal_ib_region_idx(); }
static u32 metal_vb_region_verts(void) { return metal_max_verts() / metal_ring_slots(); }
static u32 metal_vb_base_bytes(void)
{ return s_frame_slot * metal_vb_region_verts() * (u32)sizeof(MetalBasicVertex); }
static u32 metal_vb_limit_bytes(void)
{ return metal_vb_base_bytes() + metal_vb_region_verts() * (u32)sizeof(MetalBasicVertex); }
static u32 metal_fc_region_bytes(void) { return METAL_MAX_DRAWS * METAL_FC_MAX * 16u; }
static u32 metal_fc_base_bytes(void)   { return s_frame_slot * metal_fc_region_bytes(); }
/* O command buffer que leu cada regiao. Esperar por ele antes de reescrever a
 * regiao e' a correccao inteira. nil = essa regiao nunca foi apresentada. */
static id<MTLCommandBuffer> s_frame_cb[METAL_FRAMES_IN_FLIGHT];

typedef struct {
    u32 stride;     /* float4 per vertex; 0 = no decoded data (stage_in only) */
    u32 base;       /* first float4 of the draw in va_buf */
    u32 vid_base;   /* vertex_id of the draw's first vertex */
    u32 pad;
    int slot[16];   /* float4 of attribute k inside a vertex; -1 = not enabled */
    float cur[16][4]; /* VERTEX_DATA4F register: the value of a disabled attribute */
} MetalVAMap;

static int metal_va_on(void)
{
    static int v = -1;
    if (v < 0) {
        const char* e = getenv("PS3_METAL_VA");
        v = e ? (e[0] == '1') : metal_per_draw_rt();
    }
    return v;
}

/* Cache de vertices por frame (PS3_METAL_VA_CACHE, default ON; =0 desliga).
 *
 * MEDIDO (PS3_TRACE_VA_DUP, gameplay, 2026-09-21): ~48% dos vertices que esta
 * funcao descodifica por frame sao REPETICAO EXACTA -- mesma chave e mesmo
 * conteudo no guest (53 mil de ~100 mil chamadas por 120 frames; das repetidas
 * so' 1 em ~54 mil tinha conteudo diferente). O GoW2 desenha a mesma malha em
 * varios passes do mesmo frame. Um acerto reutiliza a regiao ja' descodificada
 * do anel deste frame em vez de a refazer.
 *
 * A chave cobre tudo o que muda a saida: first, count, o layout (atributos
 * ligados e usados, n) e, por atributo usado, endereco, stride, tipo, tamanho e
 * os 4 componentes do registo constante. O conteudo do guest entra por uma
 * impressao amostrada (a mesma do cache de texturas), por isso uma escrita do
 * jogo entre dois draws do mesmo frame invalida o acerto. As entradas valem so'
 * para a regiao do anel deste frame: a epoca muda quando o anel recomeca. */
/* Impressao do conteudo de um atributo para o cache: 16 palavras de 8 bytes
 * espalhadas pelo intervalo, custo FIXO. A primeira versao usava a impressao
 * das texturas (FNV byte a byte) e MEDIU-SE mais cara do que descodificar com
 * NEON: 21,7 contra 13,3 ns por vertice, A/B intercalado na mesma corrida.
 * Um intervalo ate' 128 bytes fica coberto por inteiro (amostras sobrepostas). */
/* Body moved to rsx_core_decode.h as `static inline` (fix round 1,
 * 2026-09-23: this build has no LTO, so an out-of-line body in
 * rsx_core_decode.c would have been a real cross-TU call on this per-vertex
 * hot path -- inline in the header keeps it in this translation unit, same
 * call-site cost as before). */
static inline u64 metal_va_fp_fast(const u8* p, u32 n) { return rsx_core_va_fp_fast(p, n); }

enum { METAL_VAC_N = 4096 };
typedef struct { u64 key, fp, epoch; u32 base, n, pos_ea, pos_gstride; } MetalVaCacheEnt;
static MetalVaCacheEnt s_vac[METAL_VAC_N];
static u64 s_vac_epoch = 1;
static u64 s_vac_hits, s_vac_miss, s_vac_verify_bad, s_vac_verify_n;

static void metal_va_fill_body(u32 first, u32 count);
/* Com PS3_METAL_VA_CACHE=alt, tempo gasto aqui por paridade de frame: a
 * medicao do cache dentro da MESMA cena. */
static void metal_va_fill(u32 first, u32 count)
{
    static int alt = -1;
    if (alt < 0) { const char* e = getenv("PS3_METAL_VA_CACHE"); alt = (e && e[0] == 'a') ? 1 : 0; }
    if (!alt) { metal_va_fill_body(first, count); return; }
    static u64 ns[2], verts[2], calls[2], last_frame, frames;
    const int par = (int)(s_state.frame_count & 1u);
    const u64 t0 = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
    metal_va_fill_body(first, count);
    ns[par] += clock_gettime_nsec_np(CLOCK_UPTIME_RAW) - t0;
    verts[par] += count; calls[par]++;
    if (s_state.frame_count != last_frame) {
        last_frame = s_state.frame_count;
        if (++frames % 240u == 0u && calls[0] && calls[1]) {
            fprintf(stderr, "[VAALT] 120+120 frames | com cache: %.2f ms/frame %.1f ns/vertice | "
                    "sem cache: %.2f ms/frame %.1f ns/vertice | acertos=%llu falhas=%llu\n",
                    (double)ns[0] / 1e6 / 120.0, (double)ns[0] / (double)verts[0],
                    (double)ns[1] / 1e6 / 120.0, (double)ns[1] / (double)verts[1],
                    s_vac_hits, s_vac_miss);
            fflush(stderr);
            ns[0] = ns[1] = verts[0] = verts[1] = calls[0] = calls[1] = 0;
        }
    }
}

static void metal_va_fill_body(u32 first, u32 count)
{
    s_draw.va_last_valid = 0;
    if (!metal_va_on() || !s_state.device || !vm_base || count == 0) return;
    const rsx_vertex_attrib* a16 = s_draw.attribs_valid ? s_draw.attribs
        : (s_draw.current_rsx ? s_draw.current_rsx->vertex_attribs : NULL);
    if (!a16) return;
    signed char slot[16];
    u32 n = 0;
    {   /* PS3_TRACE_METAL_SKIN=f: the format of every enabled attribute, once. A skinned
         * mesh reads bone indices as raw bytes (UB256 = 7); decoded as normalized UB (4)
         * every index would floor() to 0 and the whole mesh would ride one bone. */
        static int on = -1, done = 0;
        if (on < 0) { const char* e = getenv("PS3_TRACE_METAL_SKIN"); on = (e && e[0] == 'f') ? 1 : 0; }
        if (on && done < 6 && a16[7].enabled && a16[7].size) {   /* skinned draws only */
            done++;
            for (u32 k = 0; k < 16; k++)
                if (a16[k].enabled && a16[k].size)
                    fprintf(stderr, "[VAFMT] attr%u type=%u size=%u stride=%u\n",
                            k, a16[k].type, a16[k].size, a16[k].stride);
            fflush(stderr);
        }
    }
    /* An array with size 0 is disabled on RSX even when its type field is set
     * (E457: GoW2 leaves type F / size 0 on the unused 13 of 16). Those keep the
     * stage_in / zero inputs; the constant attribute register (VERTEX_DATA4F)
     * the RSX feeds them is not tracked yet. */
    for (int k = 0; k < 16; k++)
        slot[k] = (a16[k].enabled && a16[k].type != RSX_VERTEX_TYPE_NONE && a16[k].size != 0)
                  ? (signed char)n++ : (signed char)-1;
    if (n == 0) return;
    if (!s_draw.va_buf) {
        id<MTLBuffer> b = [s_state.device newBufferWithLength:(NSUInteger)METAL_VA_RING_F4 * 16u
                                                      options:MTLResourceStorageModeShared];
        if (!b) return;
        s_draw.va_buf = (__bridge id<MTLBuffer>)CFBridgingRetain(b);
        s_draw.va_mapped = (float*)s_draw.va_buf.contents;
        s_draw.va_offset = 0;
    }
    if ((u64)s_draw.va_offset + (u64)count * n > metal_va_limit_f4()) { s_va_ovf_draws++; return; }
    /* Components an array does not supply (size < 4) keep the VERTEX_DATA4F
     * register's value, like a disabled attribute (upstream rsx_fetch_attrib). */
    static const float def4[16][4] = {
        {0,0,0,1},{0,0,0,1},{0,0,0,1},{0,0,0,1},{0,0,0,1},{0,0,0,1},{0,0,0,1},{0,0,0,1},
        {0,0,0,1},{0,0,0,1},{0,0,0,1},{0,0,0,1},{0,0,0,1},{0,0,0,1},{0,0,0,1},{0,0,0,1} };
    const float (*cur)[4] = s_draw.current_rsx ? s_draw.current_rsx->vertex_data4f : def4;
    float* dst = s_draw.va_mapped + (size_t)s_draw.va_offset * 4u;
    /* Per attribute, not per vertex: the base address and the committed-range check
     * are computed once for the whole span and each type decodes in its own tight
     * loop. The per-vertex metal_fetch_attrib_f4 path (an address translation, a
     * range lookup and a type switch per component) was ~15% of the main thread in
     * gameplay (2026-09-16 sample). A span that does not validate as one committed
     * range falls back to that exact per-vertex path, so results are identical. */
    /* E482: nao descodificar atributos que o programa de vertice nunca le'.
     * MEDIDO: poupa 0,1-0,2%. Ver o comentario do contador [VASKIP]. */
    u16 used_mask = 0xFFFFu;
    {
        static int sk = -1, vv2 = -1;
        if (sk < 0)  { const char* e = getenv("PS3_METAL_VA_SKIP");   sk = (e && e[0] == '0') ? 0 : 1; }
        if (vv2 < 0) { const char* e = getenv("PS3_METAL_VA_VERIFY"); vv2 = (e && e[0] == '1') ? 1 : 0; }
        if (sk && !vv2 && s_draw.current_shader && s_draw.current_shader->used_inputs)
            used_mask = s_draw.current_shader->used_inputs;
    }
    /* Consulta do cache de vertices (ver s_vac). */
    static int vac_on = -1, vac_verify = -1;
    if (vac_on < 0) {
        /* "alt" (diagnostico): cache so' nos frames PARES. Com o autostart a cena
         * muda com a velocidade, e comparar duas corridas mede a sala, nao o
         * cache; intercalar frames na MESMA corrida compara a mesma cena. */
        const char* e = getenv("PS3_METAL_VA_CACHE");
        /* OFF por default: a versao com hash FNV mediu-se MAIS LENTA do que
         * descodificar (21,7 vs 13,3 ns/vertice, A/B intercalado). So' liga com
         * PS3_METAL_VA_CACHE=1 ate' o hash barato ser medido. */
        vac_on = (e && e[0] == '1') ? 1 : (e && e[0] == 'a') ? 2 : 0;
    }
    if (vac_verify < 0) vac_verify = metal_env_on("PS3_METAL_VA_CACHE_VERIFY");
    u64 vac_key = 0, vac_fp = 0;
    int vac_hit = -1;
    const int vac_now = vac_on == 2 ? (int)((s_state.frame_count & 1u) == 0u) : vac_on;
    if (vac_now) {
        u64 key = 1469598103934665603ull ^ ((u64)first << 32 | count);
        u64 fp = 1469598103934665603ull;
        key = (key ^ ((u64)used_mask << 32 | n)) * 1099511628211ull;
        for (int k = 0; k < 16; k++) {
            if (slot[k] < 0) continue;
            key = (key ^ ((u64)k << 8 | (u64)(u8)slot[k])) * 1099511628211ull;
            if (!((used_mask >> k) & 1u)) continue;
            const rsx_vertex_attrib* a = &a16[k];
            key = (key ^ ((u64)k << 56 | (u64)a->offset)) * 1099511628211ull;
            key = (key ^ ((u64)a->stride << 16 | (u64)a->type << 8 | a->size)) * 1099511628211ull;
            u32 cb[4]; memcpy(cb, cur[k], sizeof cb);
            key = (key ^ ((u64)cb[0] << 32 | cb[1])) * 1099511628211ull;
            key = (key ^ ((u64)cb[2] << 32 | cb[3])) * 1099511628211ull;
            const u32 stride = a->stride ? a->stride : rsx_vertex_attrib_size(a->type, a->size);
            const u32 ea0 = metal_vtx_ea(a->offset, first, 0, stride);
            const u64 span = (u64)count * stride;
            if (stride && span && span <= 0x1000000ull && ppu_guest_range_committed(ea0, (u32)span))
                fp = (fp ^ metal_va_fp_fast(vm_base + ea0, (u32)span)) * 1099511628211ull;
            else
                fp = (fp ^ 0xDEADull) * 1099511628211ull;   /* sem conteudo legivel: nao reutilizar */
        }
        vac_key = key ? key : 1u;
        vac_fp = fp;
        u32 h = (u32)(vac_key ^ (vac_key >> 29)) & (METAL_VAC_N - 1u);
        for (u32 probe = 0; probe < 8u; probe++, h = (h + 1u) & (METAL_VAC_N - 1u)) {
            const MetalVaCacheEnt* e = &s_vac[h];
            if (e->epoch != s_vac_epoch) break;
            if (e->key == vac_key) {
                if (e->fp == vac_fp && e->n == n &&
                    (u64)e->base + (u64)count * n <= (u64)s_draw.va_offset)
                    vac_hit = (int)h;
                break;
            }
        }
        if (vac_hit >= 0 && !vac_verify) {
            const MetalVaCacheEnt* e = &s_vac[vac_hit];
            s_vac_hits++;
            s_draw.va_last_base = e->base;
            s_draw.va_last_stride = n;
            s_draw.va_last_pos_ea = e->pos_ea;
            s_draw.va_last_pos_gstride = e->pos_gstride;
            memcpy(s_draw.va_last_slot, slot, sizeof slot);
            memcpy(s_draw.va_last_cur, cur, sizeof s_draw.va_last_cur);
            s_draw.va_last_valid = 1;
            return;
        }
        s_vac_miss++;
    }

    u32 va_skipped = 0, va_done = 0;
    for (int k = 0; k < 16; k++) {
        if (slot[k] < 0) continue;
        if (!((used_mask >> k) & 1u)) { va_skipped++; continue; }
        va_done++;
        const rsx_vertex_attrib* a = &a16[k];
        const size_t sk = (size_t)(u32)slot[k];
        const float c0 = cur[k][0], c1 = cur[k][1], c2 = cur[k][2], c3 = cur[k][3];
        const u32 stride = a->stride ? a->stride : rsx_vertex_attrib_size(a->type, a->size);
        const u32 nsz = a->size ? (a->size > 4 ? 4u : a->size) : 4u;
        const u32 nc = a->type == RSX_VERTEX_TYPE_CMP ? 3u : (a->size > 4 ? 4u : a->size);
        const u8* src0 = NULL;
        if (a->enabled && stride && vm_base) {
            const u32 ea0 = metal_vtx_ea(a->offset, first, 0, stride);
            const u64 span = (u64)count * stride + 16u;
            if (span <= 0xFFFFFFFFull && ppu_guest_range_committed(ea0, (u32)span))
                src0 = vm_base + ea0;
        }
        if (k == 0) {
            s_draw.va_last_pos_ea = src0 ? (u32)(size_t)(src0 - vm_base) : 0u;
            s_draw.va_last_pos_gstride = stride;
        }
        const int known = a->type == RSX_VERTEX_TYPE_F || a->type == RSX_VERTEX_TYPE_SF ||
                          a->type == RSX_VERTEX_TYPE_UB || a->type == RSX_VERTEX_TYPE_UB256 ||
                          a->type == RSX_VERTEX_TYPE_S1 || a->type == RSX_VERTEX_TYPE_S32K ||
                          a->type == RSX_VERTEX_TYPE_CMP;
        if (!src0 || !known) {
            for (u32 i = 0; i < count; i++) {
                float* o = dst + ((size_t)i * n + sk) * 4u;
                float t[4];
                o[0] = c0; o[1] = c1; o[2] = c2; o[3] = c3;
                if (metal_fetch_attrib_f4(a, first, i, t, 0))
                    for (u32 c = 0; c < nc; c++) o[c] = t[c];
            }
            continue;
        }
        /* E473: o switch do TIPO saiu de dentro do laco por vertice. O tipo e'
         * constante por atributo, portanto era um desvio por vertice -- e sao
         * ~450 000 vertices por frame. metal_va_fill estava a 10,2% do tempo
         * proprio da thread do PPU no perfil do gameplay.
         *
         * O caso FLOAT (posicoes) ganha ainda NEON: os quatro componentes big
         * endian viram um vld1q + vrev32q + vst1q, em vez de quatro pares
         * memcpy/bswap. A leitura de 16 bytes no ultimo vertice e' segura
         * porque o teste de intervalo acima ja' reserva +16 bytes
         * (span = count*stride + 16). */
        const u32 ncc = (nc < nsz) ? nc : nsz;
        switch (a->type) {
#if defined(__aarch64__) && defined(__ARM_NEON)
        case RSX_VERTEX_TYPE_F:
            if (ncc == 4u) {
                for (u32 i = 0; i < count; i++)
                    vst1q_u8((uint8_t*)(dst + ((size_t)i * n + sk) * 4u),
                             vrev32q_u8(vld1q_u8(src0 + (size_t)i * stride)));
                break;
            }
            if (ncc == 3u) {
                for (u32 i = 0; i < count; i++) {
                    float* o = dst + ((size_t)i * n + sk) * 4u;
                    float t[4];
                    vst1q_u8((uint8_t*)t, vrev32q_u8(vld1q_u8(src0 + (size_t)i * stride)));
                    o[0] = t[0]; o[1] = t[1]; o[2] = t[2]; o[3] = c3;
                }
                break;
            }
            for (u32 i = 0; i < count; i++) {
                float* o = dst + ((size_t)i * n + sk) * 4u;
                const u8* sp = src0 + (size_t)i * stride;
                o[0] = c0; o[1] = c1; o[2] = c2; o[3] = c3;
                for (u32 c = 0; c < ncc; c++) o[c] = metal_read_f32_be(sp + c * 4);
            }
            break;
#else
        case RSX_VERTEX_TYPE_F:
            for (u32 i = 0; i < count; i++) {
                float* o = dst + ((size_t)i * n + sk) * 4u;
                const u8* sp = src0 + (size_t)i * stride;
                o[0] = c0; o[1] = c1; o[2] = c2; o[3] = c3;
                for (u32 c = 0; c < ncc; c++) o[c] = metal_read_f32_be(sp + c * 4);
            }
            break;
#endif
#define VA_LOOP(TIPO, EXPR)                                                       \
        case TIPO:                                                                \
            for (u32 i = 0; i < count; i++) {                                     \
                float* o = dst + ((size_t)i * n + sk) * 4u;                       \
                const u8* sp = src0 + (size_t)i * stride;                         \
                o[0] = c0; o[1] = c1; o[2] = c2; o[3] = c3;                       \
                for (u32 c = 0; c < ncc; c++) o[c] = (EXPR);                      \
            }                                                                     \
            break;
        VA_LOOP(RSX_VERTEX_TYPE_SF,    metal_half_be_to_float(sp + c * 2))
        VA_LOOP(RSX_VERTEX_TYPE_UB,    sp[c] / 255.f)
        VA_LOOP(RSX_VERTEX_TYPE_UB256, (float)sp[c])
        VA_LOOP(RSX_VERTEX_TYPE_S1,    metal_snorm16_be(sp + c * 2))
        VA_LOOP(RSX_VERTEX_TYPE_S32K,
                (float)(int16_t)(u16)((sp[c * 2] << 8) | sp[c * 2 + 1]))
#undef VA_LOOP
        case RSX_VERTEX_TYPE_CMP:
            for (u32 i = 0; i < count; i++) {
                float* o = dst + ((size_t)i * n + sk) * 4u;
                const u8* sp = src0 + (size_t)i * stride;
                const u32 w = ((u32)sp[0] << 24) | ((u32)sp[1] << 16) |
                              ((u32)sp[2] << 8) | sp[3];
                o[0] = (float)((int32_t)(w << 21) >> 21) / 1023.f;
                o[1] = (float)((int32_t)(w << 10) >> 21) / 1023.f;
                o[2] = (float)((int32_t)w >> 22) / 511.f;
                o[3] = c3;
            }
            break;
        default:
            for (u32 i = 0; i < count; i++) {
                float* o = dst + ((size_t)i * n + sk) * 4u;
                o[0] = c0; o[1] = c1; o[2] = c2; o[3] = c3;
            }
            break;
        }

        {   /* PS3_METAL_VA_VERIFY=1 (OFF): oraculo in-situ do caminho vectorizado.
             * Refaz a descodificacao pelo caminho ESCALAR sobre os MESMOS dados e
             * compara bit a bit. Existe porque isto e' dado de VERTICE: um erro
             * aqui nao da' erro de compilacao nem crash, da' geometria destruida
             * -- e testar com dados sinteticos nao cobre os formatos e strides
             * reais que o titulo usa. Caro; e' para uma corrida de validacao. */
            static int vv = -1;
            if (vv < 0) { const char* e = getenv("PS3_METAL_VA_VERIFY"); vv = (e && e[0] == '1') ? 1 : 0; }
            if (vv) {
                static unsigned long long chk, bad;
                for (u32 i = 0; i < count; i++) {
                    const float* got = dst + ((size_t)i * n + sk) * 4u;
                    const u8* sp = src0 + (size_t)i * stride;
                    float want[4] = { c0, c1, c2, c3 };
                    switch (a->type) {
                    case RSX_VERTEX_TYPE_F:
                        for (u32 c = 0; c < ncc; c++) want[c] = metal_read_f32_be(sp + c * 4);
                        break;
                    case RSX_VERTEX_TYPE_SF:
                        for (u32 c = 0; c < ncc; c++) want[c] = metal_half_be_to_float(sp + c * 2);
                        break;
                    case RSX_VERTEX_TYPE_UB:
                        for (u32 c = 0; c < ncc; c++) want[c] = sp[c] / 255.f;
                        break;
                    case RSX_VERTEX_TYPE_UB256:
                        for (u32 c = 0; c < ncc; c++) want[c] = (float)sp[c];
                        break;
                    case RSX_VERTEX_TYPE_S1:
                        for (u32 c = 0; c < ncc; c++) want[c] = metal_snorm16_be(sp + c * 2);
                        break;
                    case RSX_VERTEX_TYPE_S32K:
                        for (u32 c = 0; c < ncc; c++)
                            want[c] = (float)(int16_t)(u16)((sp[c*2] << 8) | sp[c*2 + 1]);
                        break;
                    case RSX_VERTEX_TYPE_CMP: {
                        const u32 w = ((u32)sp[0] << 24) | ((u32)sp[1] << 16) |
                                      ((u32)sp[2] << 8) | sp[3];
                        want[0] = (float)((int32_t)(w << 21) >> 21) / 1023.f;
                        want[1] = (float)((int32_t)(w << 10) >> 21) / 1023.f;
                        want[2] = (float)((int32_t)w >> 22) / 511.f;
                        break; }
                    default: break;
                    }
                    chk++;
                    if (memcmp(got, want, sizeof want) != 0) {
                        bad++;
                        if (bad <= 6)
                            fprintf(stderr, "[VAVERIFY] tipo=%u ncc=%u stride=%u v%u "
                                    "got=(%g,%g,%g,%g) want=(%g,%g,%g,%g)\n",
                                    (unsigned)a->type, ncc, stride, i,
                                    got[0], got[1], got[2], got[3],
                                    want[0], want[1], want[2], want[3]);
                    }
                }
                static unsigned long long tick;
                if ((++tick % 20000ull) == 0ull) {
                    fprintf(stderr, "[VAVERIFY] vertices=%llu divergentes=%llu\n", chk, bad);
                    fflush(stderr);
                }
            }
        }
    }
    {
        static int tr = -1, shown = 0;
        static u32 seen_layout[32];
        if (tr < 0) tr = getenv("PS3_TRACE_METAL_PERDRAW") != NULL;
        /* Distinct layouts (enabled slot, type, size), up to 32: the first 12 calls
         * were all intro quads, and the character normals' format is what matters
         * (2026-09-16: CMP 11/11/10 decode unverified). */
        u32 lkey = 2166136261u;
        for (int k = 0; k < 16; k++)
            if (slot[k] >= 0) {
                lkey ^= (u32)k * 0x9E37u + a16[k].type * 0x85EBu + a16[k].size;
                lkey *= 16777619u;
            }
        int lnew = 1;
        for (int z = 0; z < shown && z < 32; z++)
            if (seen_layout[z] == lkey) lnew = 0;
        if (tr && lnew && shown < 32) {
            seen_layout[shown] = lkey;
            char s[256];
            int p = 0;
            for (int k = 0; k < 16 && p < 230; k++)
                if (slot[k] >= 0)
                    p += snprintf(s + p, sizeof s - (size_t)p, " v%d:t%u/s%u/st%u", k,
                                  (unsigned)a16[k].type, (unsigned)a16[k].size,
                                  (unsigned)a16[k].stride);
            s[p < 255 ? p : 255] = '\0';
            printf("[RSX metal] va n=%u count=%u:%s\n", n, count, s);
            shown++;
        }
    }
    g_va_skip_n += (unsigned long long)va_skipped * count;
    g_va_done_n += (unsigned long long)va_done * count;
    /* PS3_TRACE_VA_DUP=1 (diagnostico, OFF): quantas descodificacoes por frame
     * repetem a MESMA chave (first/count/formato/enderecos/registo constante) e,
     * dessas, quantas com o MESMO conteudo no guest. E' o tecto de um cache de
     * vertices por frame: se a repeticao for rara, o cache nao vale o risco de
     * servir geometria velha. Conteudo amostrado como no hash das texturas. */
    {
        static int on = -1;
        if (on < 0) on = metal_env_on("PS3_TRACE_VA_DUP");
        if (on) {
            enum { VD_N = 4096 };
            static u64 vd_key[VD_N], vd_fp[VD_N];
            static u64 vd_frame = ~0ull;
            static u64 calls, dups, dups_same, verts, dup_verts, same_verts, frames;
            if (vd_frame != s_state.frame_count) {
                vd_frame = s_state.frame_count;
                memset(vd_key, 0, sizeof vd_key);
                if (++frames % 120u == 0u && calls) {
                    fprintf(stderr, "[VADUP] 120 frames: chamadas=%llu repetidas=%llu (%.1f%%) "
                            "mesmo_conteudo=%llu | vertices=%llu repetidos=%.1f%% mesmo_conteudo=%.1f%%\n",
                            calls, dups, 100.0 * (double)dups / (double)calls, dups_same, verts,
                            100.0 * (double)dup_verts / (double)verts,
                            100.0 * (double)same_verts / (double)verts);
                    fflush(stderr);
                    calls = dups = dups_same = verts = dup_verts = same_verts = 0;
                }
            }
            u64 key = 1469598103934665603ull ^ ((u64)first << 32 | count);
            u64 fp = 1469598103934665603ull;
            key = (key ^ used_mask) * 1099511628211ull;
            for (int k = 0; k < 16; k++) {
                if (slot[k] < 0 || !((used_mask >> k) & 1u)) continue;
                const rsx_vertex_attrib* a = &a16[k];
                key = (key ^ ((u64)k << 56 | (u64)a->offset)) * 1099511628211ull;
                key = (key ^ ((u64)a->stride << 16 | (u64)a->type << 8 | a->size)) * 1099511628211ull;
                u32 cb[4]; memcpy(cb, cur[k], sizeof cb);
                key = (key ^ ((u64)cb[0] << 32 | cb[3])) * 1099511628211ull;
                const u32 stride = a->stride ? a->stride : rsx_vertex_attrib_size(a->type, a->size);
                const u32 ea0 = metal_vtx_ea(a->offset, first, 0, stride);
                const u64 span = (u64)count * stride;
                if (stride && span && span <= 0x1000000ull && ppu_guest_range_committed(ea0, (u32)span))
                    fp = (fp ^ metal_tex_fingerprint(vm_base + ea0, (u32)span)) * 1099511628211ull;
            }
            if (!key) key = 1;
            calls++; verts += count;
            u32 h = (u32)(key ^ (key >> 29)) & (VD_N - 1u);
            for (u32 probe = 0; probe < 16u; probe++, h = (h + 1u) & (VD_N - 1u)) {
                if (vd_key[h] == 0) { vd_key[h] = key; vd_fp[h] = fp; break; }
                if (vd_key[h] == key) {
                    dups++; dup_verts += count;
                    if (vd_fp[h] == fp) { dups_same++; same_verts += count; }
                    vd_fp[h] = fp;
                    break;
                }
            }
        }
    }
    if (vac_now) {
        /* PS3_METAL_VA_CACHE_VERIFY=1 (diagnostico, OFF): num acerto descodifica
         * na mesma e compara, componente a componente dos atributos USADOS, com a
         * regiao que o cache teria servido. Oraculo in-situ do cache. */
        if (vac_hit >= 0 && vac_verify) {
            const float* was = s_draw.va_mapped + (size_t)s_vac[vac_hit].base * 4u;
            const float* now = dst;
            int bad = 0;
            for (u32 i = 0; i < count && !bad; i++)
                for (int k = 0; k < 16 && !bad; k++) {
                    if (slot[k] < 0 || !((used_mask >> k) & 1u)) continue;
                    const size_t off = ((size_t)i * n + (size_t)(u32)slot[k]) * 4u;
                    if (memcmp(was + off, now + off, 16) != 0) bad = 1;
                }
            s_vac_verify_n++;
            if (bad) s_vac_verify_bad++;
            if ((s_vac_verify_n % 20000ull) == 0ull) {
                fprintf(stderr, "[VACACHE] verificacao: acertos=%llu com dados DIFERENTES=%llu\n",
                        s_vac_verify_n, s_vac_verify_bad);
                fflush(stderr);
            }
        }
        u32 h = (u32)(vac_key ^ (vac_key >> 29)) & (METAL_VAC_N - 1u);
        for (u32 probe = 0; probe < 8u; probe++, h = (h + 1u) & (METAL_VAC_N - 1u)) {
            MetalVaCacheEnt* e = &s_vac[h];
            if (e->epoch != s_vac_epoch || e->key == vac_key) {
                e->key = vac_key; e->fp = vac_fp; e->epoch = s_vac_epoch;
                e->base = s_draw.va_offset; e->n = n;
                e->pos_ea = (slot[0] >= 0) ? s_draw.va_last_pos_ea : 0u;
                e->pos_gstride = (slot[0] >= 0) ? s_draw.va_last_pos_gstride : 0u;
                break;
            }
        }
    }
    s_draw.va_last_base = s_draw.va_offset;
    s_draw.va_last_stride = n;
    s_draw.va_last_count = count;
    if (!(slot[0] >= 0)) { s_draw.va_last_pos_ea = 0; s_draw.va_last_pos_gstride = 0; }
    memcpy(s_draw.va_last_slot, slot, sizeof slot);
    memcpy(s_draw.va_last_cur, cur, sizeof s_draw.va_last_cur);
    s_draw.va_last_valid = 1;
    s_draw.va_offset += count * n;
}

/*
 * Guest RSX arrays → interleaved BasicVertex (pos float3 + color float4).
 * Uses snapshot from set_vertex_attribs when available, else live current_rsx.
 * Unknown position types → placeholder geometry (no crash, V3).
 */
static u32 upload_vertices_from_rsx(u32 first, u32 count)
{
    if (!draw_ensure_buffers() || !s_draw.vb_mapped) return 0;
    u32 max_verts = (metal_vb_limit_bytes() - s_draw.vb_offset) /
                    (u32)sizeof(MetalBasicVertex);
    if (count > max_verts) { count = max_verts; s_vb_trunc_draws++; }
    if (count == 0) return 0;

    MetalBasicVertex* verts =
        (MetalBasicVertex*)((u8*)s_draw.vb_mapped + s_draw.vb_offset);

    const rsx_vertex_attrib* pos = NULL;
    const rsx_vertex_attrib* col = NULL;
    const rsx_vertex_attrib* uv  = NULL;
    if (s_draw.attribs_valid) {
        pos = &s_draw.attribs[0];
        col = &s_draw.attribs[3];
        uv  = &s_draw.attribs[8];
    } else if (s_draw.current_rsx) {
        pos = &s_draw.current_rsx->vertex_attribs[0];
        col = &s_draw.current_rsx->vertex_attribs[3];
        uv  = &s_draw.current_rsx->vertex_attribs[8];
    }

    int has_position = pos && rsx_vertex_attrib_is_position_ok(pos);
    int has_color = col && rsx_vertex_attrib_is_color_ok(col);
    /* GoW2 legal/UI: pos@0 + UV@8 (no diffuse). Pack UV into the second
     * BasicVertex float4 so the decompiled VP can put it in v[8] via v3.xy. */
    int has_uv = uv && uv->enabled && uv->size >= 2 &&
                 (uv->type == RSX_VERTEX_TYPE_F || uv->type == RSX_VERTEX_TYPE_SF);

    if (pos && pos->enabled && !has_position) {
        static int once = 0;
        if (once++ < 8) {
            printf("[RSX metal] vertex layout: attrib0 type=%u size=%u unsupported "
                   "— placeholder positions\n", pos->type, pos->size);
        }
        s_draw.layout_unknown = 1;
        s_draw.upload_skip++;
    }

    /* The decompiled vertex shader reads every attribute from the per-draw array
     * (vam.stride != 0 overrides the stage_in v0/v3/v8), so when that array was
     * filled for a draw with a decompiled program the BasicVertex decode below is
     * dead work -- a second per-vertex fetch of position/colour/UV. Zero the span
     * (deterministic if a fixed-PSO fallback ever samples it) and skip it. */
    metal_va_fill(first, count);
    if (s_draw.va_last_valid && s_draw.current_shader && s_draw.current_shader->vs_va) {
        memset(verts, 0, (size_t)count * sizeof(MetalBasicVertex));
        s_draw.upload_ok += count;
        s_draw.vb_offset += count * (u32)sizeof(MetalBasicVertex);
        return count;
    }
    for (u32 i = 0; i < count; i++) {
        float p[4] = {0, 0, 0, 1};
        float c[4] = {1, 1, 1, 1};
        float t[4] = {0, 0, 0, 1};
        int got_pos = 0, got_col = 0, got_uv = 0;

        if (has_position)
            got_pos = metal_fetch_attrib_f4(pos, first, i, p, 0);
        if (has_color)
            got_col = metal_fetch_attrib_f4(col, first, i, c, 1);
        if (has_uv)
            got_uv = metal_fetch_attrib_f4(uv, first, i, t, 0);

        if (got_pos) {
            verts[i].x = p[0]; verts[i].y = p[1]; verts[i].z = p[2];
            s_draw.upload_ok++;
        } else {
            float tt = (float)(first + i) / 100.f;
            verts[i].x = sinf(tt * 6.28f) * 0.5f;
            verts[i].y = cosf(tt * 6.28f) * 0.5f;
            verts[i].z = 0.f;
            s_draw.upload_fallback++;
        }

        if (got_col) {
            verts[i].r = c[0]; verts[i].g = c[1];
            verts[i].b = c[2]; verts[i].a = c[3];
        } else if (got_uv) {
            /* UV → attr1 so VP decompiler (v[3]=input.v3) can forward as tc0.
             * Guest VP that reads v[8] needs the decompiler stub below. */
            verts[i].r = t[0]; verts[i].g = t[1];
            verts[i].b = 0.f; verts[i].a = 1.f;
        } else {
            verts[i].r = verts[i].g = verts[i].b = verts[i].a = 1.f;
        }
    }
    metal_va_fill(first, count);
    s_draw.vb_offset += count * (u32)sizeof(MetalBasicVertex);
    return count;
}

static void metal_record_demo_triangle(void)
{
    if (!draw_ensure_buffers() || !draw_ensure_pso()) return;
    if (s_draw.draw_count >= METAL_MAX_DRAWS) { s_draw_dropped++; return; }
    u32 off = s_draw.vb_offset;
    MetalBasicVertex* v =
        (MetalBasicVertex*)((u8*)s_draw.vb_mapped + off);
    /* NDC triangle — cyan/magenta/yellow so dump != solid clear.
     * Exercises M6 float3 position + float4 color layout (attribute 0/1). */
    v[0] = (MetalBasicVertex){  0.f,  0.6f, 0, 0, 1, 1, 1 };
    v[1] = (MetalBasicVertex){ -0.6f,-0.6f, 0, 1, 0, 1, 1 };
    v[2] = (MetalBasicVertex){  0.6f,-0.6f, 0, 1, 1, 0, 1 };
    s_draw.vb_offset += 3 * (u32)sizeof(MetalBasicVertex);
    MetalDrawRec* dr = &s_draw.draws[s_draw.draw_count++];
    dr->vb_byte_offset = off;
    dr->vertex_count   = 3;
    dr->is_indexed     = 0;
    dr->prim           = MTLPrimitiveTypeTriangle;
    metal_draw_rec_set_pso(dr, s_draw.pso); /* demo always uses fixed M1 PSO */
    {
        static int once = 0;
        if (!once) {
            once = 1;
            printf("[RSX metal] M6 float3 position path (demo BasicVertex "
                   "stride=%zu attr0=float3 attr1=float4)\n",
                   sizeof(MetalBasicVertex));
            fflush(stdout);
        }
    }
}

/* ---------------------------------------------------------------------------
 * Backend callbacks
 * -----------------------------------------------------------------------*/

static int metal_init_cb(void* ud, u32 width, u32 height)
{
    (void)ud;
    printf("[RSX metal] init(%ux%u)\n", width, height);
    return 0;
}

static void metal_shutdown_cb(void* ud)
{
    (void)ud;
    printf("[RSX metal] shutdown\n");
}

/* Release the PSOs evicted from shader_cache during the previous frame. Called
 * when the draw records are discarded (metal_begin_frame): no MetalDrawRec
 * points at them any more, and the frame that used them has been committed
 * (a command buffer keeps its own references to what it encoded). */
/* M2 constant-buffer mode (default on; PS3_METAL_FP_LITERAL_CONSTS=1 = off). */
static int metal_fp_const_buffer_on(void)
{
    static int on = -1;
    if (on < 0) {
        const char* e = getenv("PS3_METAL_FP_LITERAL_CONSTS");
        on = (e && e[0] && e[0] != '0') ? 0 : 1;
    }
    return on;
}

/* Copy the current FP's inline constants (as they are in guest memory at record
 * time) into fc_buf for this draw; present binds them at fragment buffer(0). */
static void metal_draw_rec_release_state(MetalDrawRec* dr)
{
    for (int i = 0; i < (int)RSX_MAX_TEXTURES; i++) {
        if (dr->tex[i]) CFRelease((__bridge CFTypeRef)dr->tex[i]);
        dr->tex[i] = nil;
    }
    if (dr->dss) CFRelease((__bridge CFTypeRef)dr->dss);
    dr->dss = nil;
    dr->has_state = 0;
    dr->clear_flags = 0;
    dr->is_blit = 0;
}

/* Snapshot the target surfaces, viewport, scissor, textures and depth state of
 * the draw being recorded (per-draw mode only; a no-op otherwise). */
static void metal_draw_rec_capture_state(MetalDrawRec* dr)
{
    dr->has_state = 0;
    dr->fp_hash = s_draw.current_shader ? s_draw.current_shader->fp_hash : 0u;
    dr->vp_hash = s_draw.current_shader ? s_draw.current_shader->vp_hash : 0u;
    dr->cmask = s_pipe.color_mask;
    dr->clear_flags = 0;
    dr->is_blit = 0;
    for (int i = 0; i < (int)RSX_MAX_TEXTURES; i++) dr->tex[i] = nil;
    dr->dss = nil;
    dr->stencil_en = 0;
    dr->depth_clamp = 0;
    dr->cull_mode = 0;
    dr->winding_ccw = 0;
    if (!metal_per_draw_rt()) return;
    dr->has_state = 1;
    dr->color_idx = s_color_rt_idx;
    dr->depth_idx = s_depth_rt_idx;
    dr->mrt_n = (s_mrt_n > 1 && s_mrt_idx[0] == s_color_rt_idx) ? s_mrt_n : 1;
    for (int k = 0; k < 4; k++) dr->mrt_idx[k] = (k < dr->mrt_n) ? s_mrt_idx[k] : -1;
    dr->mrt_idx[0] = s_color_rt_idx;
    dr->has_vp = s_draw.has_vp;
    dr->vp[0] = (float)s_draw.vp_x;
    dr->vp[1] = (float)s_draw.vp_y;
    dr->vp[2] = (float)s_draw.vp_w;
    dr->vp[3] = (float)s_draw.vp_h;
    dr->sc[0] = s_pipe.has_scissor ? s_pipe.scissor_x : 0u;
    dr->sc[1] = s_pipe.has_scissor ? s_pipe.scissor_y : 0u;
    dr->sc[2] = s_pipe.has_scissor ? s_pipe.scissor_w : 0u;
    dr->sc[3] = s_pipe.has_scissor ? s_pipe.scissor_h : 0u;
    for (int i = 0; i < (int)RSX_MAX_TEXTURES; i++) {
        id<MTLTexture> t = (s_draw.tex_slots[i].valid && s_draw.tex_slots[i].tex)
            ? s_draw.tex_slots[i].tex : nil;
        if (t) {
            CFRetain((__bridge CFTypeRef)t);
            dr->tex[i] = t;
        }
        dr->tex_addr[i] = s_draw.tex_slots[i].addr;
        dr->tex_filt[i] = s_draw.tex_slots[i].filt;
    }
    id<MTLDepthStencilState> d = metal_depth_stencil_state();
    if (d) {
        CFRetain((__bridge CFTypeRef)d);
        dr->dss = d;
    }
    dr->stencil_en = s_pipe.stencil_test_enable ? 1 : 0;
    metal_pipe_ds_desc(&dr->ds_desc);
    {   /* RPCS3 VKGSRender: depth clamp = clamp_enabled || !clip_enabled. z-fail
         * shadow volumes extrude past the far plane and rely on it. */
        const rsx_state* zs = s_draw.current_rsx;
        const u32 zc = zs ? zs->zminmax_control : 0x1u;
        dr->poly_offset = (zs && zs->poly_offset_fill_enable) ? 1 : 0;
        dr->poly_scale = zs ? zs->poly_offset_scale : 0.0f;
        dr->poly_bias = zs ? zs->poly_offset_bias : 0.0f;
        dr->depth_clamp = ((zc & 0xF0u) != 0u || (zc & 0x0Fu) == 0u) ? 1 : 0;
        static int zlog = -1;
        static u32 zlast = 0x1u;
        if (zlog < 0) zlog = metal_env_on("PS3_TRACE_METAL_PASSES") ? 0 : 99;
        if (zc != zlast && zlog < 8) {
            zlog++;
            zlast = zc;
            fprintf(stderr, "[ZMINMAX] control=0x%X clamp=%d frame=%llu\n", zc, dr->depth_clamp,
                    (unsigned long long)s_state.frame_count);
        }
    }
    dr->blend[0] = s_pipe.blend_enable ? 1u : 0u;
    dr->blend[1] = s_pipe.blend_sfactor;
    dr->blend[2] = s_pipe.blend_dfactor;
    dr->blend[3] = s_pipe.blend_equation;
    if (s_draw.current_rsx) {   /* unit 0 guest texture registers (PASS log) */
        const rsx_texture_state* t0 = &s_draw.current_rsx->textures[0];
        dr->tex0_reg[0] = t0->format;
        dr->tex0_reg[1] = t0->address;
        dr->tex0_reg[2] = t0->filter;
        dr->tex0_reg[3] = t0->control1;
        for (int u = 0; u < 4; u++)
            dr->tex_off[u] = s_draw.current_rsx->textures[u].offset;
    } else {
        dr->tex_off[0] = dr->tex_off[1] = dr->tex_off[2] = dr->tex_off[3] = 0;
    }
    {
        const rsx_state* vs = s_draw.current_rsx;
        dr->vso_seen = vs ? vs->viewport_so_seen : 0u;
        for (int i = 0; i < 4; i++) {
            dr->vso[i] = vs ? vs->viewport_scale[i] : 0.0f;
            dr->vso[4 + i] = vs ? vs->viewport_offset[i] : 0.0f;
        }
        dr->clip_wh[0] = vs ? vs->surface_clip_w : 0u;
        dr->clip_wh[1] = vs ? vs->surface_clip_h : 0u;
    }
    dr->stencil_ref_front = s_pipe.stencil_ref & 0xFFu;
    dr->stencil_ref_back = (s_pipe.two_sided_stencil ? s_pipe.back_stencil_ref
                                                     : s_pipe.stencil_ref) & 0xFFu;
    {   /* Face culling state of this draw (tested helpers, rsx_depth_debug.c). */
        const rsx_state* st = s_draw.current_rsx;
        dr->cull_mode = st ? rsx_cull_mode(st->cull_face_enable, st->cull_face) : 0;
        /* ALPHA_REF by surface colour format, as RPCS3 rsx_methods.h
         * alpha_ref(). Body moved to rsx_core_rules.c (rsx_core_alpha_ref);
         * 8u is the "no state" sentinel (8u & 0x1Fu == 8u falls into the
         * plain 0..255 branch, matching the old st==NULL default). */
        dr->at[0] = (st && st->alpha_test_enable && metal_atest_on()) ? 1u : 0u;
        dr->at[1] = st ? (st->alpha_func & 7u) : 7u;
        {
            const float ref = rsx_core_alpha_ref(st ? st->surface_format : 8u, st ? st->alpha_ref : 0u);
            memcpy(&dr->at[2], &ref, 4);
        }
        dr->at[3] = 0u;
        dr->winding_ccw = st ? rsx_metal_winding_is_ccw(st->front_face) : 0;
    }
}

/* M5 face culling: the Metal path drew both faces, so back faces overwrote front
 * ones (E466 frames: inside-out, broken-looking models). Guest CW stays Metal CW
 * -- flipping it for Y-down culled the title/UI quads and the menu never came
 * (noted in rsx_depth_debug.c). PS3_METAL_CULL=0 disables, PS3_METAL_CULL_FLIP=1
 * inverts. Only per-draw mode captures the state; the legacy path is unchanged. */
static void metal_apply_cull(id<MTLRenderCommandEncoder> enc, const MetalDrawRec* dr)
{
    static int off = -1, flip = -1;
    if (off < 0) { const char* e = getenv("PS3_METAL_CULL"); off = (e && e[0] == '0') ? 1 : 0; }
    if (flip < 0) flip = metal_env_on("PS3_METAL_CULL_FLIP");
    if (off || !dr->has_state) { [enc setCullMode:MTLCullModeNone]; return; }
    const int ccw = dr->winding_ccw ^ (flip ? 1 : 0);
    [enc setFrontFacingWinding:ccw ? MTLWindingCounterClockwise : MTLWindingClockwise];
    [enc setCullMode:(dr->cull_mode == RSX_CULL_FRONT) ? MTLCullModeFront
                   : (dr->cull_mode == RSX_CULL_BACK)  ? MTLCullModeBack
                                                       : MTLCullModeNone];
}

static void metal_draw_rec_capture_fc(MetalDrawRec* dr)
{
    dr->fc_byte_offset = 0;
    dr->fc_count = 0;
    if (!metal_fp_const_buffer_on() || !s_draw.fc_mapped || !vm_base || !s_draw.current_shader ||
        !s_draw.cur_fp_ea || !s_draw.cur_fp_bytes)
        return;
    const u32 cap_bytes = metal_fc_base_bytes() + metal_fc_region_bytes();
    if (s_draw.fc_offset + METAL_FC_MAX * 16u > cap_bytes) return;
    float* dst = s_draw.fc_mapped + s_draw.fc_offset / 4u;
    u32 n = 0;
    (void)rsx_fp_hash_and_consts(vm_base + s_draw.cur_fp_ea, s_draw.cur_fp_bytes,
                                 dst, METAL_FC_MAX, &n);
    /* PS3_METAL_TRACE_FC=1 (OFF, diagnostic): how many inline constants each FP
     * really got, with the ucode window they were read from. A program whose
     * MSL references rsx_fc[k] for k >= n samples an uninitialised ring slot. */
    {
        static int trace = -1;
        static u32 shown = 0;
        if (trace < 0) { const char* e = getenv("PS3_METAL_TRACE_FC"); trace = (e && *e != '0') ? 1 : 0; }
        const u32 fph = s_draw.current_shader->fp_hash;
        const int want = (fph == 0x654864DAu || fph == 0x671F7750u || fph == 0x17B2047Du ||
                          fph == 0x09BA583Du || fph == 0x6B2E71EAu);
        if (trace && want && shown < 400u) {
            shown++;
            fprintf(stderr, "[FC] fp=%08X ea=0x%08X bytes=%u n=%u k0=(%g,%g,%g,%g) k1=(%g,%g,%g,%g)\n",
                    fph, s_draw.cur_fp_ea, s_draw.cur_fp_bytes, n,
                    dst[0], dst[1], dst[2], dst[3],
                    n > 1 ? dst[4] : 0.0f, n > 1 ? dst[5] : 0.0f,
                    n > 1 ? dst[6] : 0.0f, n > 1 ? dst[7] : 0.0f);
        }
    }
    dr->fc_byte_offset = s_draw.fc_offset;
    dr->fc_count = n < METAL_FC_MAX ? n : METAL_FC_MAX;
    s_draw.fc_offset += METAL_FC_MAX * 16u;
}

/* M2 vertex-program constants. The decompiled VPs read vp_c[] at buffer(1),
 * which was a single all-zero buffer: every transform matrix was 0, so all game
 * geometry collapsed (E445: every surface black, scene target included). The
 * RSX constant file (512 float4, NV4097_SET_TRANSFORM_CONSTANT, kept in
 * rsx_state.vertex_constants) is snapshotted per draw when it changed since the
 * last snapshot, and each record binds its own. */
#define METAL_VC_SLOT_BYTES ((u32)RSX_MAX_VERTEX_CONSTANTS * 16u)
#define METAL_VC_RING_BYTES (4096u * METAL_VC_SLOT_BYTES)
/* E486: regiao por frame, alinhada ao slot para nao partir um slot ao meio. */
static u32 metal_vc_region_bytes(void)
{ return (METAL_VC_RING_BYTES / metal_ring_slots()) / METAL_VC_SLOT_BYTES * METAL_VC_SLOT_BYTES; }
static u32 metal_vc_base_bytes(void) { return s_frame_slot * metal_vc_region_bytes(); }
static u32 metal_vc_limit_bytes(void) { return metal_vc_base_bytes() + metal_vc_region_bytes(); }
static void metal_draw_rec_capture_vc(MetalDrawRec* dr)
{
    /* Diagnostic A/B: disable the per-draw vertex-constant snapshot reuse with
     * PS3_METAL_VC_CACHE=0.  This is intentionally OFF only for experiments;
     * the normal path keeps the content-addressed reuse.  For skinned meshes
     * these constants include the bone palette, so a stale snapshot is a
     * plausible source of pose-dependent vertex explosions. */
    static int vc_cache = -1;
    if (vc_cache < 0) vc_cache = metal_env_default("PS3_METAL_VC_CACHE", 1);
    dr->vc_byte_offset = 0;
    const rsx_state* st = s_draw.current_rsx;
    if (!st || !s_state.device) return;
    if (!s_draw.vc_buf) {
        id<MTLBuffer> b = [s_state.device newBufferWithLength:METAL_VC_RING_BYTES
                                                      options:MTLResourceStorageModeShared];
        if (!b) return;
        s_draw.vc_buf = (__bridge id<MTLBuffer>)CFBridgingRetain(b);
        s_draw.vc_mapped = (u8*)s_draw.vc_buf.contents;
        s_draw.vc_offset = 0;
        s_draw.vc_have = 0;
    }
    const void* src = st->vertex_constants;
    {   /* PS3_METAL_TRACE_SKIN_BOUNDS=<VPHEX|all> (OFF): input-side skinning probe.
         * The decoded position/weights/indices are captured before Metal runs
         * the vertex program; this deliberately does not pretend to be a
         * post-skinning measurement. */
        static long want_skin = -2;
        if (want_skin == -2) {
            const char* e = getenv("PS3_METAL_TRACE_SKIN_BOUNDS");
            want_skin = (e && *e && strcmp(e, "all") != 0) ? strtol(e, NULL, 16) :
                        (e && *e) ? -1 : -2;
        }
        if (want_skin != -2 && (want_skin < 0 || dr->vp_hash == (u32)want_skin) &&
            s_draw.va_last_valid && s_draw.va_mapped && s_draw.va_last_stride) {
            const int ps = s_draw.va_last_slot[0], ws = s_draw.va_last_slot[2];
            const int bs = s_draw.va_last_slot[7];
            if (ps >= 0) {
                float pmin[3] = { INFINITY, INFINITY, INFINITY };
                float pmax[3] = { -INFINITY, -INFINITY, -INFINITY };
                float wmin = INFINITY, wmax = -INFINITY;
                float bmin = INFINITY, bmax = -INFINITY;
                u32 bad = 0;
                u32 n = dr->vertex_count < 256u ? dr->vertex_count : 256u;
                for (u32 v = 0; v < n; v++) {
                    const float* p = s_draw.va_mapped +
                        ((size_t)s_draw.va_last_base + (size_t)v * s_draw.va_last_stride + (u32)ps) * 4u;
                    for (int c = 0; c < 3; c++) {
                        if (!isfinite(p[c])) bad++;
                        if (p[c] < pmin[c]) pmin[c] = p[c];
                        if (p[c] > pmax[c]) pmax[c] = p[c];
                    }
                    if (ws >= 0) {
                        const float* w = s_draw.va_mapped +
                            ((size_t)s_draw.va_last_base + (size_t)v * s_draw.va_last_stride + (u32)ws) * 4u;
                        for (int c = 0; c < 4; c++) {
                            if (!isfinite(w[c])) bad++;
                            if (w[c] < wmin) wmin = w[c];
                            if (w[c] > wmax) wmax = w[c];
                        }
                    }
                    if (bs >= 0) {
                        const float* b = s_draw.va_mapped +
                            ((size_t)s_draw.va_last_base + (size_t)v * s_draw.va_last_stride + (u32)bs) * 4u;
                        for (int c = 0; c < 4; c++) {
                            if (!isfinite(b[c])) bad++;
                            if (b[c] < bmin) bmin = b[c];
                            if (b[c] > bmax) bmax = b[c];
                        }
                    }
                }
                static u32 skin_logs;
                if (skin_logs++ < 256u || bad) {
                    fprintf(stderr, "[SKIN-IN] frame=%llu draw=%u fp=%08X vp=%08X n=%u "
                            "posEA=%08X stride=%u pos=[%.3g..%.3g,%.3g..%.3g,%.3g..%.3g] "
                            "weights=[%.3g..%.3g] bones=[%.3g..%.3g] bad=%u\n",
                            (unsigned long long)s_state.frame_count, dr->geom_id,
                            dr->fp_hash, dr->vp_hash, n, dr->pos_ea, dr->pos_gstride,
                            pmin[0], pmax[0], pmin[1], pmax[1], pmin[2], pmax[2],
                            (wmin == INFINITY ? 0.f : wmin), (wmax == -INFINITY ? 0.f : wmax),
                            (bmin == INFINITY ? 0.f : bmin), (bmax == -INFINITY ? 0.f : bmax),
                            bad);
                    fflush(stderr);
                }
            }
        }
    }
    {   /* PS3_METAL_TRACE_VC=<FPHEX> (OFF, diagnostico): as constantes do VP que
         * o draw desse fragment program le'. Para as particulas (fp=59864B76) o
         * alpha sai de tc1 = min(v3 * c467.x, c467.y) -- se c467 vier errado a
         * particula nunca desvanece e a nuvem satura o ecra'. */
        static long want = -2;
        if (want == -2) { const char* e = getenv("PS3_METAL_TRACE_VC");
                          want = (e && *e) ? strtol(e, NULL, 16) : -1; }
        if (want >= 0 && dr->fp_hash == (u32)want) {
            static u32 nlog = 0;
            if ((nlog++ % 200u) == 0u) {
                const float (*vc)[4] = st->vertex_constants;
                /* Alpha por vertice que o host decodificou para o slot 3 (a cor
                 * DIFFUSE): e' de la' que sai o fade da particula. */
                float amin = 9e9f, amax = -9e9f; int sl3 = s_draw.va_last_slot[3];
                if (s_draw.va_last_valid && sl3 >= 0 && s_draw.va_mapped) {
                    const u32 nstr = s_draw.va_last_stride;
                    for (u32 vtx = 0; vtx < dr->vertex_count && vtx < 64u; vtx++) {
                        float a = s_draw.va_mapped[((size_t)(s_draw.va_last_base +
                                   (size_t)vtx * nstr + (u32)sl3)) * 4u + 3u];
                        if (a < amin) amin = a;
                        if (a > amax) amax = a;
                    }
                }
                fprintf(stderr, "[VC] fp=%08X vp=%08X n=%u slot3=%d v3.w=[%g..%g] "
                        "c467=%g,%g,%g,%g\n",
                        dr->fp_hash, dr->vp_hash, dr->vertex_count, sl3,
                        (amin > 8e9f ? -1.f : amin), (amax < -8e9f ? -1.f : amax),
                        vc[467][0], vc[467][1], vc[467][2], vc[467][3]);
                fflush(stderr);
            }
        }
    }
    if (vc_cache && s_draw.vc_have &&
        memcmp(s_draw.vc_mapped + s_draw.vc_last, src, METAL_VC_SLOT_BYTES) == 0) {
        dr->vc_byte_offset = s_draw.vc_last;
        return;
    }
    if (s_draw.vc_offset + METAL_VC_SLOT_BYTES > metal_vc_limit_bytes()) {
        /* Ring full: this draw would render with ANOTHER draw's vertex constants -- for a
         * skinned mesh that is the wrong bone palette, i.e. a deformed model. Counted and
         * reported in [CAP] instead of dropping silently. */
        s_vc_ring_full_draws++;
        dr->vc_byte_offset = s_draw.vc_last;
        return;
    }
    memcpy(s_draw.vc_mapped + s_draw.vc_offset, src, METAL_VC_SLOT_BYTES);
    s_draw.vc_last = s_draw.vc_offset;
    s_draw.vc_offset += METAL_VC_SLOT_BYTES;
    s_draw.vc_have = 1;
    dr->vc_byte_offset = s_draw.vc_last;
}

/* PS3_METAL_PICK=1 (diagnostic, OFF): F9 picks the pixel under the mouse. The
 * frame of the key press is shot (frame_gp<k>_*.bmp + PASS log, as
 * PS3_FRAME_DUMP_GAMEPLAY); the NEXT frame is replayed with every draw clipped
 * to a 3x3 box around that pixel and a Metal visibility counter per draw, and
 * [PICK] lists each draw whose fragments passed depth/stencil there: shaders,
 * blend, textures and the guest vertex-attribute formats with the decoded
 * value range of each slot. That frame is thrown away on screen (1 frame).
 * Made for the Colossus' shoulder in GoW2 (whole triangles of the drape lit
 * yellow, 2026-09-21): which draw, which VP, which attribute. */
static int s_pick_on = -1, s_pick_phase = 0, s_pick_key_was = 0, s_pick_n = 0;
static float s_pick_mx = -1.0f, s_pick_my = -1.0f;   /* drawable pixels */
static int s_pick_norm = 0;   /* PS3_METAL_PICK_AUTO: mx/my already normalised u/v */
static u32 s_pick_fmt[METAL_MAX_DRAWS][16];          /* type | size<<4 | stride<<8 | on<<31 */
static char* s_pick_info[METAL_MAX_DRAWS];
static id<MTLBuffer> s_pick_vis;
/* Textures of the picked draws, retained until dumped on a later present. */
static __unsafe_unretained id<MTLTexture> s_pick_tex[METAL_MAX_DRAWS][4];
static volatile int s_pick_dump_ready = 0;
static unsigned char s_pick_reached[METAL_MAX_DRAWS];   /* draw got to the draw call */
/* Pick frame: colour + depth at the picked pixel read back after each covering
 * draw (the encoder is split there), to see which draw set the depth that
 * rejects a later LEQUAL pass and which one painted the final colour. */
enum { PICK_RB_MAX = 1024 };
static id<MTLBuffer> s_pick_rb;
static u32 s_pick_rb_n, s_pick_rb_draw[PICK_RB_MAX], s_pick_rb_fmt[PICK_RB_MAX];
static int s_pick_rb_ci[PICK_RB_MAX];
/* F10 (with PS3_METAL_PICK): cycle "skip one VP" over the VPs the last pick saw
 * -- the title says which one is off, to tell live which pass paints a defect. */
static u32 s_pick_vps[32];
static volatile int s_pick_nvps = 0;
static int s_live_skip_i = 0, s_f10_was = 0;
static u32 s_live_skip_vp = 0;
/* F8 (with PS3_METAL_PICK): depth test ALWAYS (no write) for the GoW2 bronze
 * pass (FP F949090C), to tell a LEQUAL failure from a shading one live. */
static int s_live_zalways = 0, s_f11_was = 0;
/* F7: small depth bias toward the camera on the bronze pass -- fixes it only if
 * the two passes' depth differs by rounding. PS3_BRONZE_BIAS=<units> (default 4). */
static int s_live_zbias = 0, s_f7_was = 0;
/* PS3_BRONZE_AUDIT=<fp hex> (OFF): every 300 frames, each draw of that FP is also
 * drawn under ALWAYS / LEQUAL / EQUAL / LEQUAL+big bias depth tests (no depth
 * write) with a visibility counter each -- how many of its fragments the depth
 * test keeps, and whether the rejects are near misses. The frame's image is not
 * meaningful (the draw blends 4 extra times). */
static u32 s_audit_fp = 0xFFFFFFFFu;
static int s_audit_on = 0;
enum { AUDIT_MAX = 64 };
static u32 s_audit_n, s_audit_draw[AUDIT_MAX];
static id<MTLDepthStencilState> s_audit_dss[4];

/* M6: the record takes the attributes the upload just decoded (once). */
/* PS3_TRACE_VC_VP=<hex>[,<hex>...] (OFF): for draws of these VPs, the vertex
 * constants the draw will run with -- the material block c456..c467 and a hash
 * of the bone palette c256..c319 -- once every 120 frames. Two passes over one
 * skinned mesh (GoW2 Colossus 07C9D580 / E05A608D) must agree on the palette. */
static void metal_trace_vc_vp(const MetalDrawRec* dr)
{
    static int n = -1;
    static u32 want[8];
    if (n < 0) {
        n = 0;
        const char* e = getenv("PS3_TRACE_VC_VP");
        while (e && *e && n < 8) {
            char* end = NULL;
            const unsigned long v = strtoul(e, &end, 16);
            if (end == e) break;
            want[n++] = (u32)v;
            e = (*end == ',') ? end + 1 : end;
        }
    }
    if (!n || !s_draw.vc_mapped || (s_state.frame_count % 120ull) != 0ull) return;
    int hit = 0;
    for (int k = 0; k < n; k++) if (want[k] == dr->vp_hash) hit = 1;
    if (!hit) return;
    const float (*vc)[4] = (const float (*)[4])(s_draw.vc_mapped + dr->vc_byte_offset);
    u32 h = 2166136261u;
    const u8* pb = (const u8*)&vc[256][0];
    for (u32 k = 0; k < 16u * 64u; k++) h = (h ^ pb[k]) * 16777619u;
    u32 hp = 2166136261u, h7 = 2166136261u, nv = 0;
    if (s_draw.va_last_valid && s_draw.va_mapped && s_draw.va_last_stride) {
        nv = s_draw.va_last_count;
        const int s0 = s_draw.va_last_slot[0], s7 = s_draw.va_last_slot[7];
        for (u32 v = 0; v < nv; v++) {
            const float* row = s_draw.va_mapped + ((size_t)s_draw.va_last_base + (size_t)v * s_draw.va_last_stride) * 4u;
            if (s0 >= 0) { const u8* q = (const u8*)(row + (u32)s0 * 4u); for (int k = 0; k < 16; k++) hp = (hp ^ q[k]) * 16777619u; }
            if (s7 >= 0) { const u8* q = (const u8*)(row + (u32)s7 * 4u); for (int k = 0; k < 16; k++) h7 = (h7 ^ q[k]) * 16777619u; }
        }
    }
    u32 hib = 2166136261u;
    if (dr->is_indexed && s_draw.ib_mapped)
        for (u32 k = 0; k < dr->vertex_count; k++) {
            const u32 x = s_draw.ib_mapped[dr->ib_byte_offset / 4u + k];
            hib = (hib ^ x) * 16777619u;
        }
    const rsx_state* sti = s_draw.current_rsx;
    if (s_draw.current_rsx)
        fprintf(stderr, "[VCBE] d=%u vp=%08X n=%u be=%u c459x=%g\n", (u32)(dr - s_draw.draws), dr->vp_hash,
                dr->vertex_count, s_draw.current_rsx->begin_end_id, vc[459][0]);
    fprintf(stderr, "[VCIB] d=%u vp=%08X n=%u ib=%08X ib_addr=0x%08X ib_loc=%u idx16=%d ib_off=%u\n",
            (u32)(dr - s_draw.draws), dr->vp_hash, dr->vertex_count, hib,
            sti ? sti->index_array_addr : 0u, sti ? sti->index_location : 0u,
            sti ? sti->index_is_16bit : -1, dr->ib_byte_offset);
    char b[1400];
    int p = snprintf(b, sizeof b, "[VCVP] f=%llu d=%u vp=%08X fp=%08X n=%u nv=%u pos_ea=0x%08X vpos=%08X vbone=%08X vc_off=%u pal=%08X",
                     (unsigned long long)s_state.frame_count, (u32)(dr - s_draw.draws), dr->vp_hash,
                     dr->fp_hash, dr->vertex_count, nv, s_draw.va_last_pos_ea, hp, h7, dr->vc_byte_offset, h);
    for (int c = 456; c <= 467 && p < (int)sizeof b - 64; c++)
        p += snprintf(b + p, sizeof b - (size_t)p, " c%d=(%g,%g,%g,%g)", c, vc[c][0], vc[c][1], vc[c][2], vc[c][3]);
    {
        float ar; memcpy(&ar, &dr->at[2], 4);
        p += snprintf(b + p, sizeof b - (size_t)p, " atest=%u func=%u ref=%g", dr->at[0], dr->at[1], ar);
        p += snprintf(b + p, sizeof b - (size_t)p, " t0fmt=%08X t0addr=%08X t0filt=%08X t0ctl1=%08X t0off=%08X",
                      dr->tex0_reg[0], dr->tex0_reg[1], dr->tex0_reg[2], dr->tex0_reg[3], dr->tex_off[0]);
    }
    for (int c = 256; c <= 263 && p < (int)sizeof b - 64; c++)
        p += snprintf(b + p, sizeof b - (size_t)p, " c%d=(%.4g,%.4g,%.4g,%.4g)", c, vc[c][0], vc[c][1], vc[c][2], vc[c][3]);
    fprintf(stderr, "%s\n", b);
    /* CPU skinning audit (the 07C9D580/E05A608D microcode, instr 11-42):
     * a0 = floor(floor(v7.xy) * c459.x) [c457.x for E05A608D];
     * bone(k) = x*c[260+k] + y*c[261+k] + z*c[262+k] + c[263+k];
     * skin = w*(bone(a0.x) - bone(a0.y)) + bone(a0.y), w = v0.w;
     * clip = skin.x*c256 + skin.y*c257 + skin.z*c258 + skin.w*c259. */
    if (dr->is_indexed && s_draw.ib_mapped && s_draw.va_last_valid && s_draw.va_mapped &&
        s_draw.va_last_slot[0] >= 0 && s_draw.va_last_slot[7] >= 0) {
        const float scale = (dr->vp_hash == 0xE05A608Du) ? vc[457][0] : vc[459][0];
        const int s0 = s_draw.va_last_slot[0], s7 = s_draw.va_last_slot[7];
        const u32 nv = s_draw.va_last_count;
        float* sk = (float*)malloc(sizeof(float) * 4u * (nv ? nv : 1u));
        float* cl = (float*)malloc(sizeof(float) * 4u * (nv ? nv : 1u));
        if (sk && cl) {
            u32 bad_idx = 0, nanv = 0, wbad = 0, wlow = 0; float maxb = 0, minw = 1e9f, maxabs = 0;
            for (u32 v = 0; v < nv; v++) {
                const float* row = s_draw.va_mapped + ((size_t)s_draw.va_last_base + (size_t)v * s_draw.va_last_stride) * 4u;
                const float* P = row + (u32)s0 * 4u; const float* B = row + (u32)s7 * 4u;
                const int ax = (int)floorf(floorf(B[0]) * scale), ay = (int)floorf(floorf(B[1]) * scale);
                if (ax < 0 || ay < 0 || 263 + ax > 511 || 263 + ay > 511) { bad_idx++; ax < 0 ? 0 : 0; }
                if (B[0] > maxb) maxb = B[0];
                if (B[1] > maxb) maxb = B[1];
                if (P[3] < 0.0f || P[3] > 1.0f) wbad++;
                if (P[3] < minw) minw = P[3];
                float A4[4], C4[4];
                for (int c = 0; c < 4; c++) {
                    const int kx = (260 + ax) & 511, ky = (260 + ay) & 511;
                    A4[c] = P[0] * vc[kx][c] + P[1] * vc[(kx + 1) & 511][c] + P[2] * vc[(kx + 2) & 511][c] + vc[(kx + 3) & 511][c];
                    C4[c] = P[0] * vc[ky][c] + P[1] * vc[(ky + 1) & 511][c] + P[2] * vc[(ky + 2) & 511][c] + vc[(ky + 3) & 511][c];
                }
                for (int c = 0; c < 4; c++) sk[v * 4 + c] = P[3] * (A4[c] - C4[c]) + C4[c];
                for (int c = 0; c < 4; c++)
                    cl[v * 4 + c] = sk[v*4+0] * vc[256][c] + sk[v*4+1] * vc[257][c] + sk[v*4+2] * vc[258][c] + sk[v*4+3] * vc[259][c];
                for (int c = 0; c < 4; c++) {
                    if (!isfinite(sk[v*4+c]) || !isfinite(cl[v*4+c])) nanv++;
                    if (fabsf(sk[v*4+c]) > maxabs) maxabs = fabsf(sk[v*4+c]);
                }
                if (fabsf(cl[v*4+3]) < 1e-3f) wlow++;
            }
            /* triangles: longest edge pre-skin vs post-skin; worst ratio */
            const u32* ib = s_draw.ib_mapped + dr->ib_byte_offset / 4u;
            float worst = 0; u32 wt = 0, nt = 0, spikes = 0;
            for (u32 t = 0; t + 2 < dr->vertex_count; t += 3) {
                u32 q[3]; int ok = 1;
                for (int c = 0; c < 3; c++) { q[c] = ib[t + (u32)c]; if (q[c] >= nv) ok = 0; }
                if (!ok) continue;
                float e0 = 0, e1 = 0;
                for (int c = 0; c < 3; c++) {
                    const u32 a = q[c], bb = q[(c + 1) % 3];
                    const float* pa = s_draw.va_mapped + ((size_t)s_draw.va_last_base + (size_t)a * s_draw.va_last_stride + (u32)s0) * 4u;
                    const float* pb = s_draw.va_mapped + ((size_t)s_draw.va_last_base + (size_t)bb * s_draw.va_last_stride + (u32)s0) * 4u;
                    float d0 = 0, d1 = 0;
                    for (int k = 0; k < 3; k++) {
                        d0 += (pa[k] - pb[k]) * (pa[k] - pb[k]);
                        d1 += (sk[a*4+k] - sk[bb*4+k]) * (sk[a*4+k] - sk[bb*4+k]);
                    }
                    if (d0 > e0) e0 = d0;
                    if (d1 > e1) e1 = d1;
                }
                nt++;
                const float r = sqrtf(e1) / (sqrtf(e0) + 1e-3f);
                if (r > 3.0f) spikes++;
                if (r > worst) { worst = r; wt = t; }
            }
            if (s_draw.current_rsx) {   /* when was each bone's palette entry last written? */
                char bb[512]; int bp = 0;
                const u32 be = s_draw.current_rsx->begin_end_id;
                for (int k = 0; k <= (int)maxb && k < 16 && bp < (int)sizeof bb - 32; k++) {
                    const u32 sl = 260u + (u32)((int)floorf((float)k * scale));
                    bp += snprintf(bb + bp, sizeof bb - (size_t)bp, " b%d@c%u:-%u", k, sl,
                                   be - rsx_vc_last_write_be(sl));
                }
                fprintf(stderr, "[SKINPAL] vp=%08X n=%u be=%u (draws ago of last write)%s\n",
                        dr->vp_hash, dr->vertex_count, be, bb);
                /* Is each bone a rigid transform? The VP uses rows c[k..k+2].xyz as a
                 * 3x3 (columns x,y,z of the input) -- report column lengths and the
                 * largest off-diagonal dot. Animation output, not our decode. */
                char ob[1024]; int op = 0;
                for (int k = 0; k <= (int)maxb && k < 16 && op < (int)sizeof ob - 48; k++) {
                    const u32 sl = 260u + (u32)((int)floorf((float)k * scale));
                    float col[3][3];
                    for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) col[r][c] = vc[(sl + (u32)r) & 511][c];
                    float ln[3], od = 0;
                    for (int r = 0; r < 3; r++) ln[r] = sqrtf(col[r][0]*col[r][0] + col[r][1]*col[r][1] + col[r][2]*col[r][2]);
                    for (int a = 0; a < 3; a++) for (int b2 = a + 1; b2 < 3; b2++) {
                        const float d = fabsf(col[a][0]*col[b2][0] + col[a][1]*col[b2][1] + col[a][2]*col[b2][2]) / (ln[a]*ln[b2] + 1e-9f);
                        if (d > od) od = d;
                    }
                    op += snprintf(ob + op, sizeof ob - (size_t)op, " b%d=|%.2f,%.2f,%.2f|skew%.2f", k, ln[0], ln[1], ln[2], od);
                }
                fprintf(stderr, "[SKINORTHO] n=%u%s\n", dr->vertex_count, ob);
            }
            fprintf(stderr, "[SKIN] vp=%08X n=%u nv=%u tri=%u stretch>3x=%u worst=%.2fx bad_bone_idx=%u max_bone=%g "
                    "w_out_of_0_1=%u min_w=%g nan=%u clip_w~0=%u max|skin|=%g\n",
                    dr->vp_hash, dr->vertex_count, nv, nt, spikes, worst, bad_idx, maxb, wbad, minw, nanv, wlow, maxabs);
            if (worst > 3.0f)
                for (int c = 0; c < 3; c++) {
                    const u32 v = ib[wt + (u32)c];
                    const float* row = s_draw.va_mapped + ((size_t)s_draw.va_last_base + (size_t)v * s_draw.va_last_stride) * 4u;
                    const float* P = row + (u32)s0 * 4u; const float* B = row + (u32)s7 * 4u;
                    fprintf(stderr, "[SKIN]   v%u in=(%g,%g,%g) w=%g bones=(%g,%g,%g,%g) skin=(%g,%g,%g,%g) clip=(%g,%g,%g,%g)\n",
                            v, P[0], P[1], P[2], P[3], B[0], B[1], B[2], B[3],
                            sk[v*4], sk[v*4+1], sk[v*4+2], sk[v*4+3], cl[v*4], cl[v*4+1], cl[v*4+2], cl[v*4+3]);
                }
        }
        free(sk); free(cl);
    }
    fflush(stderr);
}

static void metal_draw_rec_capture_va(MetalDrawRec* dr)
{
    metal_trace_vc_vp(dr);
    if (s_pick_phase == 1 && s_draw.current_rsx) {
        const size_t di = (size_t)(dr - s_draw.draws);
        if (di < METAL_MAX_DRAWS)
            for (int k = 0; k < 16; k++) {
                const rsx_vertex_attrib* a = &s_draw.current_rsx->vertex_attribs[k];
                s_pick_fmt[di][k] = a->enabled
                    ? ((a->type & 0xFu) | ((a->size & 0xFu) << 4) | ((a->stride & 0xFFu) << 8) | (1u << 31))
                    : 0u;
            }
    }
    dr->va_stride = 0;
    if (!s_draw.va_last_valid) return;
    dr->va_base = s_draw.va_last_base;
    dr->va_stride = s_draw.va_last_stride;
    dr->pos_ea = s_draw.va_last_pos_ea;
    dr->pos_gstride = s_draw.va_last_pos_gstride;
    memcpy(dr->va_slot, s_draw.va_last_slot, sizeof dr->va_slot);
    memcpy(dr->va_cur, s_draw.va_last_cur, sizeof dr->va_cur);
    s_draw.va_last_valid = 0;
}

/* Bind buffer(2) slot map + buffer(3) data for a decompiled VS; always bound
 * because the VS declares them (stride 0 = keep the stage_in inputs). */
static void metal_bind_va(id<MTLRenderCommandEncoder> enc, const MetalDrawRec* dr)
{
    static int nobind = -1;
    if (nobind < 0) nobind = metal_env_on("PS3_METAL_VA_NOBIND");
    if (nobind) return;
    MetalVAMap m;
    memset(&m, 0, sizeof m);
    for (int k = 0; k < 16; k++) m.slot[k] = -1;
    if (dr->va_stride && s_draw.va_buf) {
        m.stride = dr->va_stride;
        m.base = dr->va_base;
        m.vid_base = dr->is_indexed ? 0u : dr->vb_byte_offset / (u32)sizeof(MetalBasicVertex);
        for (int k = 0; k < 16; k++) m.slot[k] = dr->va_slot[k];
        memcpy(m.cur, dr->va_cur, sizeof m.cur);
    }
    [enc setVertexBytes:&m length:sizeof m atIndex:2];
    id<MTLBuffer> b = s_draw.va_buf ? s_draw.va_buf : s_draw.vb;
    if (b) [enc setVertexBuffer:b offset:0 atIndex:3];
}

/*
 * The title menu's XButton is a solid FLP mesh, so replacement happens in the
 * same VA/IB draw -- never in a post-present overlay. Coordinates are
 * normalized to their source bounds so an FLP translation does not change the
 * source identity.
 */
static u32 metal_prompt_fnv(u32 h, u32 v) { return (h ^ v) * 16777619u; }

/*
 * Order-free, scale-free shape signature: each triangle's three corners are
 * quantized to 1024 steps of the draw's own bounding box, sorted, hashed, and
 * the hashes are SUMMED (commutative), then mixed with the triangle count.
 * The game rebuilds FLP strips into triangle lists in its own order, so this
 * is what lets tools/ compute the identity of every FLP_HUDA element offline
 * from the asset's mesh parts -- it equals the in-boot value by construction.
 */
static u32 metal_prompt_shape_signature(const MetalDrawRec* dr, float bounds[4])
{
    if (!dr || !bounds || !dr->va_stride ||
        dr->va_slot[0] < 0 || !s_draw.va_mapped || !s_draw.ib_mapped ||
        dr->vertex_count < 3u || dr->vertex_count % 3u != 0u)
        return 0;

    const u32 ib_base = dr->ib_byte_offset / 4u;
    float xmin = INFINITY, ymin = INFINITY, xmax = -INFINITY, ymax = -INFINITY;
    for (u32 i = 0; i < dr->vertex_count; i++) {
        /* Non-indexed draws (the big QTE buttons) read vertices in order. */
        const u32 vi = dr->is_indexed ? s_draw.ib_mapped[ib_base + i] : i;
        if (dr->is_indexed && rsx_index_is_restart(vi)) return 0;
        const u64 at = (u64)dr->va_base + (u64)vi * dr->va_stride +
                       (u32)dr->va_slot[0];
        if (at >= s_draw.va_offset) return 0;
        const float* p = s_draw.va_mapped + (size_t)at * 4u;
        if (!isfinite(p[0]) || !isfinite(p[1])) return 0;
        if (p[0] < xmin) xmin = p[0];
        if (p[0] > xmax) xmax = p[0];
        if (p[1] < ymin) ymin = p[1];
        if (p[1] > ymax) ymax = p[1];
    }
    if (!isfinite(xmin) || xmax <= xmin || ymax <= ymin) return 0;

    const float sx = 1024.0f / (xmax - xmin), sy = 1024.0f / (ymax - ymin);
    u32 acc = 0;
    for (u32 t = 0; t < dr->vertex_count; t += 3u) {
        u32 q[3][2];
        for (u32 k = 0; k < 3u; k++) {
            const u32 vi = dr->is_indexed ? s_draw.ib_mapped[ib_base + t + k] : t + k;
            const float* p = s_draw.va_mapped +
                ((size_t)dr->va_base + (size_t)vi * dr->va_stride + (u32)dr->va_slot[0]) * 4u;
            q[k][0] = (u32)lrintf((p[0] - xmin) * sx);
            q[k][1] = (u32)lrintf((p[1] - ymin) * sy);
        }
        for (u32 i = 0; i < 2u; i++)            /* sort corners by (x, y) */
            for (u32 j = 0; j + 1u < 3u - i; j++)
                if (q[j][0] > q[j + 1][0] ||
                    (q[j][0] == q[j + 1][0] && q[j][1] > q[j + 1][1])) {
                    const u32 x = q[j][0], y = q[j][1];
                    q[j][0] = q[j + 1][0]; q[j][1] = q[j + 1][1];
                    q[j + 1][0] = x; q[j + 1][1] = y;
                }
        u32 h = 2166136261u;
        for (u32 k = 0; k < 3u; k++) {
            h = metal_prompt_fnv(h, q[k][0]);
            h = metal_prompt_fnv(h, q[k][1]);
        }
        acc += h;
    }
    bounds[0] = xmin; bounds[1] = ymin; bounds[2] = xmax; bounds[3] = ymax;
    return metal_prompt_fnv(metal_prompt_fnv(2166136261u, acc), dr->vertex_count / 3u);
}

/*
 * Replaceable native prompt draws. Every field must match: the FLP vector
 * material (fp; the VP varies per screen), index count, VA stride, the aspect of the local bounds
 * and the scale-free shape signature (width/height below are the measured
 * title-menu size, used only for the aspect). Signatures were measured on the real title and
 * difficulty menus (PS3_TRACE_PROMPT_MATCH / PS3_TRACE_PROMPT_SHAPES).
 * `backing` marks the round button drawn right before a face glyph: it is
 * shared by every face, so its keycap shape is chosen once the glyph that
 * follows it identifies the key.
 */
typedef struct {
    host_prompt_face face;
    int backing;
    u32 index_count, va_stride, signature;
    float width, height;
    /* Animated buttons (FLP_HUDA FlashSprite: XBtn, CircleBtn/Smash/Static,
     * SquareBtn, TriangleBtn) draw their glyph as three stacked layers, in
     * the same order for all four: 0 = bright colour (outermost), 1 = black,
     * 2 = dark colour (innermost). The family glyph keeps that structure:
     * layer 0 strokes +0.075 (coloured rim), 1 +0.04 (black outline), 2 plain.
     * Static glyphs of menus/tutorials are one layer: METAL_PROMPT_SINGLE. */
    int layer;
} MetalPromptSlot;
#define METAL_PROMPT_SINGLE 3

static const MetalPromptSlot k_metal_prompt_slots[] = {
    /* Glyphs of FLP_HUDA XButton/TriangleButton/CircleButton/SquareButton
     * (mesh parts 395..398), computed offline from the asset with
     * tools/flp_prompt_signatures.py and equal to the in-boot dumps. The same
     * meshes are used by menus, tutorials and the big QTE buttons, at any size,
     * so va_stride 0 = any vertex layout. */
    { HOST_PROMPT_FACE_CROSS,    0, 30u,  0u, 0xB4F0D78Fu, 32.1875f, 32.0f, METAL_PROMPT_SINGLE },
    { HOST_PROMPT_FACE_TRIANGLE, 0, 27u,  0u, 0x84FE9B9Du, 41.5625f, 36.0f, METAL_PROMPT_SINGLE },
    { HOST_PROMPT_FACE_CIRCLE,   0, 288u, 0u, 0x383002B6u, 41.625f, 41.625f, METAL_PROMPT_SINGLE },
    { HOST_PROMPT_FACE_SQUARE,   0, 36u,  0u, 0x5C9649A7u, 34.875f, 34.8125f, METAL_PROMPT_SINGLE },
    /* Round button backing behind a face glyph (menus and gameplay HUD). */
    { HOST_PROMPT_FACE_CROSS,    1, 90u,  2u, 0x1EAC54B1u, 53.75f,   53.75f, METAL_PROMPT_SINGLE },
    /* Animated buttons of FlashSprite (magic "mash Circle", QTEs): the three
     * layers of mesh parts 356..359, per FLP object (tools/). */
    { HOST_PROMPT_FACE_CROSS,    0, 87u,  0u, 0x9E58A487u, 67.0f,    65.0f,    0 }, /* XBtn p356.o0 */
    { HOST_PROMPT_FACE_CROSS,    0, 51u,  0u, 0xA717305Fu, 67.0625f, 65.0625f, 1 }, /* XBtn p356.o1 */
    { HOST_PROMPT_FACE_CROSS,    0, 48u,  0u, 0x15291A2Bu, 65.9375f, 64.75f,   2 }, /* XBtn p356.o2 */
    { HOST_PROMPT_FACE_CIRCLE,   0, 204u, 0u, 0x0ABE9F97u, 68.75f,   68.875f,  0 }, /* CircleBtn p358.o0 */
    { HOST_PROMPT_FACE_CIRCLE,   0, 228u, 0u, 0x4534A59Cu, 63.5f,    65.875f,  1 }, /* CircleBtn p358.o1 */
    { HOST_PROMPT_FACE_CIRCLE,   0, 207u, 0u, 0xAF59CF04u, 63.0f,    66.25f,   2 }, /* CircleBtn p358.o2 */
    { HOST_PROMPT_FACE_SQUARE,   0, 36u,  0u, 0xD87ADCC2u, 71.3125f, 68.4375f, 0 }, /* SquareBtn p359.o0 */
    { HOST_PROMPT_FACE_SQUARE,   0, 24u,  0u, 0xD5E95A6Au, 71.3125f, 68.4375f, 1 }, /* SquareBtn p359.o1 */
    { HOST_PROMPT_FACE_SQUARE,   0, 24u,  0u, 0xAAA86208u, 70.25f,   67.8125f, 2 }, /* SquareBtn p359.o2 */
    { HOST_PROMPT_FACE_TRIANGLE, 0, 36u,  0u, 0xB0FEA45Cu, 85.1875f, 67.3125f, 0 }, /* TriangleBtn p357.o0 */
    { HOST_PROMPT_FACE_TRIANGLE, 0, 36u,  0u, 0x98D46A4Fu, 74.75f,   67.3125f, 1 }, /* TriangleBtn p357.o1 */
    { HOST_PROMPT_FACE_TRIANGLE, 0, 33u,  0u, 0x8BBD917Bu, 72.4375f, 66.1875f, 2 }, /* TriangleBtn p357.o2 */
};

/* PS3_PROMPT_DUMP_ON_MATCH=1 (diagnostic, OFF by default): with PS3_FRAME_DUMP,
 * dump the frame 20 frames after each face is first replaced, so short-lived
 * gameplay prompts are captured for visual acceptance. */
static u64 s_prompt_dump_at;

/* Backings seen this frame whose glyph has not been drawn yet. */
typedef struct {
    u32 draw_index;
    float bounds[4];
} MetalPromptPending;
static MetalPromptPending s_prompt_pending[4];
static u32 s_prompt_pending_n;

static void metal_prompt_shape_trace(const MetalDrawRec* dr)
{
    /* 1 = prompt material only; 2 = any material, small shapes only (finds
     * prompts drawn with another shader). */
    /* After the PS3_PROMPT_DUMP_SIG shape, log the next draws of any kind:
     * the prompt's label may be font text drawn right after its backing. */
    static u32 follow_left;
    if (follow_left && dr) {
        follow_left--;
        float fb[4] = { 0, 0, 0, 0 };
        const u32 fsig = dr->is_indexed == 1u ? metal_prompt_shape_signature(dr, fb) : 0u;
        fprintf(stderr, "[PROMPT-FOLLOW] g=%u indexed=%u n=%u prim=%u stride=%u fp=%08X "
                "vp=%08X tex0=%08X sig=%08X bbox=(%g,%g)-(%g,%g) slots=%d,%d,%d,%d v0=",
                dr->geom_id, dr->is_indexed, dr->vertex_count, (unsigned)dr->prim,
                dr->va_stride, dr->fp_hash, dr->vp_hash, dr->tex_off[0], fsig,
                fb[0], fb[1], fb[2], fb[3], dr->va_slot[0], dr->va_slot[1],
                dr->va_slot[2], dr->va_slot[3]);
        if (dr->va_stride && s_draw.va_mapped && dr->is_indexed == 1u) {
            const u32 ib0 = dr->ib_byte_offset / 4u;
            for (u32 v = 0; v < 4u && v < dr->vertex_count; v++) {
                const float* q = s_draw.va_mapped + ((size_t)dr->va_base +
                    (size_t)s_draw.ib_mapped[ib0 + v] * dr->va_stride) * 4u;
                for (u32 c = 0; c < dr->va_stride * 4u && c < 8u; c++)
                    fprintf(stderr, "%g%s", q[c], c + 1u < dr->va_stride * 4u && c < 7u ? "," : "");
                fputc(';', stderr);
            }
        }
        fputc('\n', stderr);
    }
    static int on = -1;
    if (on < 0) {
        const char* e = getenv("PS3_TRACE_PROMPT_SHAPES");
        on = (e && e[0] == '2') ? 2 : metal_env_on("PS3_TRACE_PROMPT_SHAPES");
    }
    if (!on || !dr || dr->is_indexed != 1u || dr->prim != MTLPrimitiveTypeTriangle ||
        dr->va_stride == 0u || dr->va_slot[0] < 0 || dr->vertex_count > 600u)
        return;
    const int prompt_material = dr->fp_hash == 0x59864B76u;
    if (on == 1 && !prompt_material) return;
    if (on == 2 && !prompt_material && dr->vertex_count > 120u) return;
    float bounds[4];
    const u32 sig = metal_prompt_shape_signature(dr, bounds);
    if (!sig) return;
    if (on == 2 && !prompt_material &&
        (bounds[2] - bounds[0] > 96.f || bounds[3] - bounds[1] > 96.f)) return;
    /* Every quad has the same shape signature, so a textured sprite prompt is
     * told apart by material, texture and size as well. */
    u32 key = sig;
    key = (key ^ dr->fp_hash) * 16777619u;
    key = (key ^ dr->vp_hash) * 16777619u;
    key = (key ^ dr->tex_off[0]) * 16777619u;
    key = (key ^ (u32)lrintf((bounds[2] - bounds[0]) * 16.f)) * 16777619u;
    key = (key ^ (u32)lrintf((bounds[3] - bounds[1]) * 16.f)) * 16777619u;
    /* PS3_PROMPT_DUMP_SIG=<hex>: with PS3_FRAME_DUMP, capture the screen
     * where that shape first shows up, to identify an unknown prompt. */
    static int want_parsed = 0;
    static u32 want_sig = 0;
    if (!want_parsed) {
        const char* e = getenv("PS3_PROMPT_DUMP_SIG");
        want_sig = e ? (u32)strtoul(e, NULL, 16) : 0u;
        want_parsed = 1;
    }
    if (want_sig && sig == want_sig && !s_prompt_dump_at) {
        s_prompt_dump_at = s_state.frame_count + 20u;
        follow_left = 8u;
        want_sig = 0;
        fprintf(stderr, "[PROMPT-DUMP] sig=%08X seen at frame %llu\n",
                sig, (unsigned long long)s_state.frame_count);
    }
    static u32 seen[1024];
    static u32 nseen = 0;
    for (u32 i = 0; i < nseen; i++)
        if (seen[i] == key) return;
    if (nseen >= 1024u) return;
    seen[nseen++] = key;
    const u32 ib_base = dr->ib_byte_offset / 4u;
    fprintf(stderr, "[PROMPT-SHAPE] frame=%llu sig=%08X n=%u g=%u stride=%u fp=%08X vp=%08X "
            "tex0=%08X bbox=(%g,%g)-(%g,%g) pts=",
            (unsigned long long)s_state.frame_count,
            sig, dr->vertex_count, dr->geom_id, dr->va_stride, dr->fp_hash, dr->vp_hash,
            dr->tex_off[0],
            bounds[0], bounds[1], bounds[2], bounds[3]);
    for (u32 i = 0; i < dr->vertex_count; i++) {
        const u32 vi = s_draw.ib_mapped[ib_base + i];
        if (rsx_index_is_restart(vi)) { fputs("R;", stderr); continue; }
        const float* p = s_draw.va_mapped + ((size_t)dr->va_base +
                         (size_t)vi * dr->va_stride + (u32)dr->va_slot[0]) * 4u;
        fprintf(stderr, "%.3f,%.3f;", p[0], p[1]);
    }
    fputc('\n', stderr);
    fflush(stderr);
}

/*
 * Shoulder/stick prompts ("Press [L1] to block", the "R1" door prompt): a
 * rounded pill (sig DC45B9FD, 63 indices) followed by its label as font quads,
 * drawn twice (shadow + text). The label is read from the font-atlas cells it
 * samples; the cells come from FLP_HUDA's own font (CharNumberToSymbolIdMap ->
 * MeshesRefs -> mesh-part UVs) and 'L'/'1' were also measured in-boot.
 */
#define METAL_PROMPT_PILL_SIGNATURE 0xDC45B9FDu
typedef struct {
    int active;
    u32 pill_index;
    float pill_bounds[4];
    u32 n;
    u32 label_index[4];
    float label_bounds[4][4];
    float label_u0[4], label_v0[4];
} MetalPromptShoulder;
static MetalPromptShoulder s_prompt_shoulder;

static void metal_prompt_apply_mesh(MetalDrawRec* dr, const float bounds[4],
                                    const host_prompt_mesh* mesh);
static void metal_prompt_apply_text(MetalDrawRec* dr, const host_prompt_text* t);

/* Lowest texcoord (second float4 of the vertex) a label quad samples. */
static int metal_prompt_label_uv(const MetalDrawRec* dr, float* u0, float* v0)
{
    if (dr->va_stride != 2u || !s_draw.va_mapped || !s_draw.ib_mapped) return 0;
    const u32 ib0 = dr->ib_byte_offset / 4u;
    float umin = INFINITY, vmin = INFINITY;
    for (u32 i = 0; i < dr->vertex_count; i++) {
        const u64 at = (u64)dr->va_base + (u64)s_draw.ib_mapped[ib0 + i] * 2u + 1u;
        if (at >= s_draw.va_offset) return 0;
        const float* q = s_draw.va_mapped + (size_t)at * 4u;
        if (q[0] < umin) umin = q[0];
        if (q[1] < vmin) vmin = q[1];
    }
    *u0 = umin; *v0 = vmin;
    return isfinite(umin) && isfinite(vmin);
}

static int metal_prompt_near(float a, float b) { return fabsf(a - b) <= 0.002f; }

/* Font-atlas cell (lowest u, v) of each character a shoulder label uses. */
static char metal_prompt_label_char(float u0, float v0)
{
    static const struct { char c; float u, v; } cells[] = {
        { 'L', 0.091797f, 0.771484f }, { 'R', 0.287109f, 0.224609f },
        { '1', 0.837891f, 0.771484f }, { '2', 0.626953f, 0.771484f },
        { '3', 0.501953f, 0.517578f },
    };
    for (u32 i = 0; i < sizeof cells / sizeof cells[0]; i++)
        if (metal_prompt_near(u0, cells[i].u) && metal_prompt_near(v0, cells[i].v))
            return cells[i].c;
    return 0;
}

/* Decides what the collected label says and rewrites pill + label. */
static void metal_prompt_shoulder_finish(void)
{
    MetalPromptShoulder* sh = &s_prompt_shoulder;
    if (!sh->active) return;
    sh->active = 0;
    if ((sh->n != 2u && sh->n != 4u) || sh->pill_index >= s_draw.draw_count) return;
    /* Two characters, repeated for the shadow pass when there are four. */
    char text[2];
    for (u32 i = 0; i < sh->n; i++) {
        const char c = metal_prompt_label_char(sh->label_u0[i], sh->label_v0[i]);
        if (!c) return;                          /* unknown label: keep native */
        if (i < 2u) text[i] = c;
        else if (text[i - 2u] != c) return;
    }
    host_prompt_face face;
    if (text[0] == 'L' && text[1] == '1')      face = HOST_PROMPT_FACE_L1;
    else if (text[0] == 'R' && text[1] == '1') face = HOST_PROMPT_FACE_R1;
    else if (text[0] == 'L' && text[1] == '2') face = HOST_PROMPT_FACE_L2;
    else if (text[0] == 'R' && text[1] == '2') face = HOST_PROMPT_FACE_R2;
    else if (text[0] == 'L' && text[1] == '3') face = HOST_PROMPT_FACE_L3;
    else if (text[0] == 'R' && text[1] == '3') face = HOST_PROMPT_FACE_R3;
    else return;

    if (metal_env_on("PS3_TRACE_PROMPT_MATCH")) {
        static u32 seen_faces;
        if (!(seen_faces & (1u << face))) {
            seen_faces |= 1u << face;
            fprintf(stderr, "[PROMPT-SHOULDER] label=%c%c frame=%llu\n", text[0], text[1],
                    (unsigned long long)s_state.frame_count);
            if (!s_prompt_dump_at && metal_env_on("PS3_PROMPT_DUMP_ON_MATCH"))
                s_prompt_dump_at = s_state.frame_count + 20u;
        }
    }
    /* Redraw the label with the family's text in the title's own font: the
     * first glyph draw of each pass (text and shadow) carries the whole new
     * label, the second glyph draw of each pass is emptied. The pill stays
     * native, so contrast is the game's on any background. */
    host_prompt_text text_mesh;
    const float native_advance = host_prompt_font_advance(text[0]) +
                                 host_prompt_font_advance(text[1]);
    if (!host_prompt_shoulder_text(face, host_prompts_current(), native_advance, &text_mesh))
        return; /* this family keeps the native label */
    for (u32 i = 0; i < sh->n; i++) {
        if (sh->label_index[i] >= s_draw.draw_count) continue;
        MetalDrawRec* ld = &s_draw.draws[sh->label_index[i]];
        if (i % 2u == 0u) {
            metal_prompt_apply_text(ld, &text_mesh);
        } else {
            host_prompt_mesh empty;
            memset(&empty, 0, sizeof empty);
            empty.count = 3u;                    /* degenerate: draws nothing */
            metal_prompt_apply_mesh(ld, sh->label_bounds[i], &empty);
        }
    }
}

/* Rewrites one recorded draw with `mesh`, placed in `bounds`, in fresh VA/IB
 * ring space; the guest's own vertices and indices stay untouched. */
static void metal_prompt_apply_mesh(MetalDrawRec* dr, const float bounds[4],
                                    const host_prompt_mesh* mesh)
{
    if (mesh->count == 0u || mesh->count % 3u != 0u) return;
    if ((u64)s_draw.va_offset + (u64)mesh->count * dr->va_stride > metal_va_limit_f4())
        return;
    const u32 ib_off = s_draw.ib_offset;
    if (ib_off / 4u + mesh->count > metal_ib_limit_idx()) return;
    const u64 template_at = (u64)dr->va_base + (u32)dr->va_slot[0];
    if (template_at >= s_draw.va_offset) return;

    const u32 replacement_base = s_draw.va_offset;
    const float* template_vertex = s_draw.va_mapped + (size_t)dr->va_base * 4u;
    for (u32 i = 0; i < mesh->count; i++) {
        float* dst = s_draw.va_mapped +
            ((size_t)replacement_base + (size_t)i * dr->va_stride) * 4u;
        memcpy(dst, template_vertex, (size_t)dr->va_stride * 4u * sizeof(float));
        float* pos = dst + (u32)dr->va_slot[0] * 4u;
        pos[0] = bounds[0] + (bounds[2] - bounds[0]) * mesh->xy[i][0];
        pos[1] = bounds[1] + (bounds[3] - bounds[1]) * mesh->xy[i][1];
        s_draw.ib_mapped[ib_off / 4u + i] = i;
    }
    s_draw.va_offset += mesh->count * dr->va_stride;
    s_draw.ib_offset += mesh->count * 4u;
    dr->va_base = replacement_base;
    dr->ib_byte_offset = ib_off;
    dr->vertex_count = mesh->count;
    dr->is_indexed = 1;     /* a non-indexed QTE button now draws our indices */
}

/* Rewrites a font-quad draw with `t` (label-local positions + atlas UVs); the
 * UV is the vertex's second float4, as the native glyph quads carry it. */
static void metal_prompt_apply_text(MetalDrawRec* dr, const host_prompt_text* t)
{
    if (t->count == 0u || t->count % 3u != 0u || dr->va_stride != 2u) return;
    if ((u64)s_draw.va_offset + (u64)t->count * 2u > METAL_VA_RING_F4) return;
    const u32 ib_off = s_draw.ib_offset;
    if (ib_off / 4u + t->count > METAL_MAX_INDICES) return;
    if ((u64)dr->va_base + 2u > s_draw.va_offset) return;

    const u32 replacement_base = s_draw.va_offset;
    const float* template_vertex = s_draw.va_mapped + (size_t)dr->va_base * 4u;
    for (u32 i = 0; i < t->count; i++) {
        float* dst = s_draw.va_mapped + ((size_t)replacement_base + (size_t)i * 2u) * 4u;
        memcpy(dst, template_vertex, 8u * sizeof(float));
        dst[0] = t->xy[i][0];
        dst[1] = t->xy[i][1];
        dst[4] = t->uv[i][0];
        dst[5] = t->uv[i][1];
        s_draw.ib_mapped[ib_off / 4u + i] = i;
    }
    s_draw.va_offset += t->count * 2u;
    s_draw.ib_offset += t->count * 4u;
    dr->va_base = replacement_base;
    dr->ib_byte_offset = ib_off;
    dr->vertex_count = t->count;
}

static void metal_prompt_cross_replace(MetalDrawRec* dr)
{
    metal_prompt_shape_trace(dr);
    if (!host_prompts_enabled() || !dr) return;
    if (dr->is_indexed != 1u && metal_env_on("PS3_TRACE_PROMPT_MATCH") &&
        (dr->vertex_count == 180u || dr->vertex_count == 288u ||
         dr->vertex_count == 30u || dr->vertex_count == 36u || dr->vertex_count == 27u)) {
        /* Non-indexed draw with a button's vertex count (QTE buttons?). */
        static u32 seen[64];
        static u32 nseen = 0;
        const u32 key = dr->vertex_count ^ dr->fp_hash ^ (dr->vp_hash * 31u);
        int fresh = 1;
        for (u32 k = 0; k < nseen; k++) if (seen[k] == key) fresh = 0;
        if (fresh && nseen < 64u) {
            seen[nseen++] = key;
            fprintf(stderr, "[PROMPT-NONIDX] n=%u prim=%u stride=%u fp=%08X vp=%08X frame=%llu\n",
                    dr->vertex_count, (unsigned)dr->prim, dr->va_stride, dr->fp_hash,
                    dr->vp_hash, (unsigned long long)s_state.frame_count);
        }
    }
    if (s_prompt_shoulder.active) {
        const u32 here = (u32)(dr - s_draw.draws);
        float lb[4], u0, v0;
        if (dr->is_indexed == 1u && dr->vertex_count == 6u && dr->va_stride == 2u &&
            dr->fp_hash == 0x59864B76u && dr->tex_off[0] != 0u &&
            here > s_prompt_shoulder.pill_index &&
            here - s_prompt_shoulder.pill_index <= 5u && s_prompt_shoulder.n < 4u &&
            metal_prompt_shape_signature(dr, lb) && metal_prompt_label_uv(dr, &u0, &v0)) {
            const u32 k = s_prompt_shoulder.n++;
            {   /* PS3_PROMPT_DUMP_FONT=1 (diagnostic): the label's font atlas,
                 * to map the cells of R/2 next to the measured L/1. */
                static int font_dump = -1;
                if (font_dump < 0) font_dump = metal_env_on("PS3_PROMPT_DUMP_FONT");
                if (font_dump == 1 && dr->tex[0]) {
                    font_dump = 2;
                    metal_dump_texture_to(dr->tex[0], "prompt_font.bmp");
                    fprintf(stderr, "[PROMPT-FONT] tex0=%08X %lux%lu dumped\n", dr->tex_off[0],
                            (unsigned long)dr->tex[0].width, (unsigned long)dr->tex[0].height);
                }
            }
            s_prompt_shoulder.label_index[k] = here;
            memcpy(s_prompt_shoulder.label_bounds[k], lb, sizeof lb);
            s_prompt_shoulder.label_u0[k] = u0;
            s_prompt_shoulder.label_v0[k] = v0;
            return;
        }
        metal_prompt_shoulder_finish();
    }
    if (dr->prim != MTLPrimitiveTypeTriangle || dr->va_slot[0] < 0)
        return;
    /* The prompt material is required for backings and pills; face glyphs are
     * identified by exact order-free shape + index count alone, because the
     * big QTE buttons tint their glyph (orange Circle) with another shader. */
    const int prompt_material = dr->fp_hash == 0x59864B76u;
    if (prompt_material && dr->is_indexed == 1u && dr->vertex_count == 63u && dr->va_stride == 2u) {
        float pb[4];
        /* Measured 68.75 x 39.875 in the tutorial; other screens may stretch
         * the pill, and the decoded label is the identity that decides. */
        if (metal_prompt_shape_signature(dr, pb) == METAL_PROMPT_PILL_SIGNATURE &&
            pb[3] - pb[1] >= 1.0f &&
            (pb[2] - pb[0]) / (pb[3] - pb[1]) >= 1.2f &&
            (pb[2] - pb[0]) / (pb[3] - pb[1]) <= 2.6f) {
            memset(&s_prompt_shoulder, 0, sizeof s_prompt_shoulder);
            s_prompt_shoulder.active = 1;
            s_prompt_shoulder.pill_index = (u32)(dr - s_draw.draws);
            memcpy(s_prompt_shoulder.pill_bounds, pb, sizeof pb);
        }
        return;
    }
    /* The VP key is NOT part of the identity: the same FLP glyph comes with a
     * different VP per screen (title 03BEAB3D, "no save data" D24D7CF2 and
     * 4A7AD35D), while material, topology and the scale-free shape stay. */

    int candidate = 0;
    for (u32 k = 0; k < sizeof k_metal_prompt_slots / sizeof k_metal_prompt_slots[0]; k++)
        if (dr->vertex_count == k_metal_prompt_slots[k].index_count &&
            (!k_metal_prompt_slots[k].va_stride ||
             dr->va_stride == k_metal_prompt_slots[k].va_stride))
            candidate = 1;
    if (!candidate) return;

    float bounds[4];
    const u32 signature = metal_prompt_shape_signature(dr, bounds);
    const float width = bounds[2] - bounds[0];
    const float height = bounds[3] - bounds[1];
    if (!signature || height < 1.0f) return;
    /* Any size of the same glyph; its proportions must match exactly. */
    const MetalPromptSlot* slot = NULL;
    for (u32 k = 0; k < sizeof k_metal_prompt_slots / sizeof k_metal_prompt_slots[0]; k++) {
        const MetalPromptSlot* c = &k_metal_prompt_slots[k];
        if (dr->vertex_count == c->index_count && (prompt_material || !c->backing) &&
            (!c->va_stride || dr->va_stride == c->va_stride) &&
            signature == c->signature &&
            fabsf(width / height - c->width / c->height) <= 0.01f)
            slot = c;
    }

    if (slot && metal_env_on("PS3_TRACE_PROMPT_MATCH")) {
        /* One line per (face, VP) beyond the first 64 matches: the VP changes
         * with the screen/transform, so e.g. the QTE Circle gets its own line. */
        static u32 pair_seen[128];
        static u32 npairs = 0;
        const u32 pair = dr->vp_hash ^ ((u32)slot->face * 0x9E3779B1u) ^ (u32)slot->backing;
        int fresh = 1;
        for (u32 k = 0; k < npairs; k++) if (pair_seen[k] == pair) fresh = 0;
        if (fresh && npairs < 128u) pair_seen[npairs++] = pair;
        if (fresh && !s_prompt_dump_at && metal_env_on("PS3_PROMPT_DUMP_ON_MATCH"))
            s_prompt_dump_at = s_state.frame_count + 20u;
        static u32 logged = 0;
        if (logged++ < 64u || fresh) {
            fprintf(stderr,
                    "[PROMPT-MATCH] face=%d backing=%d g=%u fp=%08X vp=%08X sig=%08X "
                    "bbox=(%g,%g)-(%g,%g)\n",
                    (int)slot->face, slot->backing, dr->geom_id, dr->fp_hash,
                    dr->vp_hash, signature, bounds[0], bounds[1], bounds[2], bounds[3]);
            fflush(stderr);
        }
    }

    if (!slot) {
        /* PS3_TRACE_PROMPT_MATCH: a draw with a known button's index count
         * that did not match (another shape, aspect or material). */
        if (metal_env_on("PS3_TRACE_PROMPT_MATCH")) {
            static u32 miss_seen[64];
            static u32 nmiss = 0;
            int fresh = 1;
            for (u32 k = 0; k < nmiss; k++) if (miss_seen[k] == signature) fresh = 0;
            if (fresh && nmiss < 64u) {
                miss_seen[nmiss++] = signature;
                fprintf(stderr, "[PROMPT-MISS] n=%u stride=%u fp=%08X vp=%08X sig=%08X "
                        "bbox=(%g,%g)-(%g,%g) frame=%llu\n",
                        dr->vertex_count, dr->va_stride, dr->fp_hash, dr->vp_hash, signature,
                        bounds[0], bounds[1], bounds[2], bounds[3],
                        (unsigned long long)s_state.frame_count);
            }
        }
        return;
    }
    const host_prompt_set set = host_prompts_current();
    {   /* One line whenever the glyph family in use changes: which device the
         * prompts are following, without having to read them off the screen. */
        static int last_set = -1;
        if ((int)set != last_set) {
            static const char* const names[] = { "playstation", "xbox", "switch", "teclado" };
            last_set = (int)set;
            fprintf(stderr, "[PROMPT] icones: %s\n",
                    (set <= HOST_PROMPT_SET_KB) ? names[set] : "?");
            fflush(stderr);
        }
    }
    host_prompt_mesh mesh;

    if (slot->backing) {
        /* Deferred until the glyph that follows names the key. */
        if (s_prompt_pending_n == sizeof s_prompt_pending / sizeof s_prompt_pending[0]) {
            memmove(&s_prompt_pending[0], &s_prompt_pending[1],
                    sizeof s_prompt_pending - sizeof s_prompt_pending[0]);
            s_prompt_pending_n--;
        }
        MetalPromptPending* p = &s_prompt_pending[s_prompt_pending_n++];
        p->draw_index = (u32)(dr - s_draw.draws);
        memcpy(p->bounds, bounds, sizeof p->bounds);
        return;
    }

    /* Only the backing(s) drawn just before this glyph (shadow + face, 2 draws
     * in the menus measured) belong to it; an older one is the backing of a
     * button we do not replace yet and keeps its round shape. */
    const u32 here = (u32)(dr - s_draw.draws);
    u32 mine = 0;
    for (u32 i = 0; i < s_prompt_pending_n; i++) {
        const u32 at = s_prompt_pending[i].draw_index;
        if (at < here && here - at <= 3u) mine++;
    }
    /* Two backing draws (shadow, then face) become the keycap's base and its
     * raised top; a lone backing is the top. */
    for (u32 i = 0, k = 0; i < s_prompt_pending_n; i++) {
        const u32 at = s_prompt_pending[i].draw_index;
        if (!(at < here && here - at <= 3u)) continue;
        const int part = (mine >= 2u && k == 0u) ? 0 : 1;
        k++;
        if (host_prompt_backing_mesh_part(slot->face, set, part, &mesh))
            metal_prompt_apply_mesh(&s_draw.draws[at], s_prompt_pending[i].bounds, &mesh);
    }
    s_prompt_pending_n = 0;

    /* Rim / outline layers thicken the glyph's strokes in place. */
    const float grow = slot->layer == 0 ? 0.09f : slot->layer == 1 ? 0.045f : 0.0f;
    /* On the keyboard the animated button's three layers become a keycap in
     * relief (coloured base, dark top, label) instead of an outlined letter. */
    const int keycap = set == HOST_PROMPT_SET_KB && slot->layer != METAL_PROMPT_SINGLE;
    if (keycap ? host_prompt_keycap_layer(slot->face, set, slot->layer, width / height, &mesh)
               : host_prompt_face_mesh_grown(slot->face, set, width / height, grow, &mesh)) {
        metal_prompt_apply_mesh(dr, bounds, &mesh);
        static int dump_on = -1;
        static u32 dumped_faces;
        if (dump_on < 0) dump_on = metal_env_on("PS3_PROMPT_DUMP_ON_MATCH");
        if (dump_on && !(dumped_faces & (1u << slot->face)) && !s_prompt_dump_at) {
            dumped_faces |= 1u << slot->face;
            s_prompt_dump_at = s_state.frame_count + 20u;
            fprintf(stderr, "[PROMPT-DUMP] face=%d first replaced at frame %llu\n",
                    (int)slot->face, (unsigned long long)s_state.frame_count);
        }
    }
}

static void metal_pso_retired_drain(void)
{
    for (u32 i = 0; i < s_draw.pso_retired_count; i++)
        if (s_draw.pso_retired[i]) CFRelease(s_draw.pso_retired[i]);
    s_draw.pso_retired_count = 0;
}

/* E486: o ring de vertices (`s_draw.va_*`, 64 MB) e' UM buffer partilhado que
 * `metal_begin_frame` poe a zero em todo o frame, e o caminho normal de
 * apresentacao e' `presentDrawable` + `commit` -- sem espera e sem fence. Ou
 * seja, a CPU comeca a escrever os vertices do frame N+1 por cima dos do frame
 * N enquanto a GPU ainda os le'.
 *
 * Sintoma que trouxe aqui: na tela de dificuldade aparece, durante UM frame, um
 * triangulo vermelho gigante com hipotenusa limpa de canto a canto (metade de
 * um quad de ecra inteiro a apanhar vertices de outro draw), e a linha de
 * prompts corrompe-se no mesmo frame. Medido no video do utilizador: 446
 * frames, mediana de diferenca entre frames consecutivos 0,16, e cinco picos
 * isolados de 3 a 21 (ate' 133x a mediana), cada um com 1 frame de duracao.
 *
 * Este contador MEDE a corrida sem depender de pixels: no inicio do frame,
 * antes de reusar o ring, pergunta ao command buffer anterior se ja' acabou.
 * `MTLCommandBufferStatusCompleted` = a GPU largou o buffer, reusar e' seguro;
 * qualquer outro estado = estamos a escrever por cima de dados em uso.
 *
 * PS3_TRACE_VARACE=1 imprime por segundo. Diagnostico: nao muda comportamento. */
static unsigned long long s_va_race_n, s_va_race_total;
static int metal_va_race_trace(void)
{
    static int on = -1;
    if (on < 0) on = metal_env_on("PS3_TRACE_VARACE");
    return on;
}
static void metal_va_race_check(u32 next_slot)
{
    if (!metal_va_race_trace()) return;
    s_va_race_total++;
    /* A pergunta certa nao e' "o frame anterior acabou?" -- com N frames em voo
     * a resposta e' "nao" POR PROJECTO, e foi assim que a primeira medicao
     * desta correccao se deu por falhada quando media outra coisa. A pergunta
     * e': ja' acabou o command buffer que leu A REGIAO que estou prestes a
     * reescrever? Com o portao desligado ha' uma regiao so' e isto degenera
     * exactamente na pergunta antiga, o que mantem os dois lados do A/B
     * comparaveis. */
    if (s_frame_cb[next_slot] && s_frame_cb[next_slot].status != MTLCommandBufferStatusCompleted)
        s_va_race_n++;
    /* Por lotes de 120 frames: a razao nao depende do fps. Os picos sao o
     * CONSUMO do frame (offset menos a base da regiao) -- e' esse numero que
     * dimensiona as regioes, e sem ele dividir o ring trocaria o artefacto por
     * draws descartados em silencio. */
    {
        static u32 va_hi, vb_hi, ib_hi;
        /* E486: guard VARACE region underflow: the first diagnostic can run before begin_frame has
         * initialized offsets for the active region; do not wrap unsigned usage. */
        const u32 va_base = metal_va_base_f4();
        const u32 vb_base = metal_vb_base_bytes();
        const u32 ib_base = metal_ib_base_idx() * 4u;
        const u32 va_u = s_draw.va_offset >= va_base ? s_draw.va_offset - va_base : 0u;
        const u32 vb_u = s_draw.vb_offset >= vb_base ? s_draw.vb_offset - vb_base : 0u;
        const u32 ib_u = s_draw.ib_offset >= ib_base ? s_draw.ib_offset - ib_base : 0u;
        if (va_u > va_hi) va_hi = va_u;
        if (vb_u > vb_hi) vb_hi = vb_u;
        if (ib_u > ib_hi) ib_hi = ib_u;
        if (s_va_race_total >= 120ull) {
            fprintf(stderr,
                    "[VARACE] frames=%llu reuso_com_gpu_a_ler=%llu (%.1f%%) | pico va=%u/%u (%.1f%%) "
                    "vb=%u/%u ib=%u/%u\n",
                    s_va_race_total, s_va_race_n,
                    100.0 * (double)s_va_race_n / (double)s_va_race_total,
                    va_hi, metal_va_region_f4(), 100.0 * (double)va_hi / (double)metal_va_region_f4(),
                    vb_hi / (u32)sizeof(MetalBasicVertex), metal_vb_region_verts(),
                    ib_hi / 4u, metal_ib_region_idx());
            s_va_race_total = 0; s_va_race_n = 0;
            va_hi = vb_hi = ib_hi = 0;
        }
    }
}

/* E487: taxa de particulas INFLADAS (sangue/poeira, fp=59864B76).
 *
 * O defeito conhecido: ~12% dos quads chegam com posicao E cor enormes, mas
 * com topologia e UV INTEGRAS -- lixo ESTRUTURADO, nao lixo aleatorio. Num
 * mesmo frame, sa' col=(18,4; 33,6; 69,5; 0) contra quebrada
 * col=(39,3; 21,0; 12,7; 586,9). O FP faz alpha = min(w/128, 2), logo w >= 256
 * satura o alpha em 2,0 e o ecra' enche de vermelho. E' esse o limiar usado
 * aqui: as sadias medem 18 a 69, as quebradas 586 -- ha' uma ordem de grandeza
 * de separacao, portanto o limiar nao e' ajustavel a gosto.
 *
 * Ja' ELIMINADOS por medicao (nao repetir): cache de textura, blend, fragment
 * program, remap/swizzle, alpha da textura, base de UV no atlas, e "quads
 * grandes demais" em geral. A hipotese que sobrou e nunca foi testada e' uma
 * CORRIDA DE LEITURA: `metal_va_fill` descodifica o vertex buffer do guest no
 * momento em que regista o draw, e o guest pode ainda estar a escrever.
 *
 * Isto e' agora testavel de graca, porque [FIFOSRC] mostrou que o walker corre
 * MESMO fora da thread do guest (wake = 58 a 92 kicks/s). PS3_RSX_WAKE=0 poe
 * todo o FIFO na thread do guest; se a taxa cair, a corrida esta' provada.
 *
 * Contado no inicio do frame seguinte, onde os draws e o ring ainda estao
 * intactos. PS3_TRACE_FXBAD=1; diagnostico, nao muda comportamento. */
static unsigned long long s_fx_total, s_fx_bad, s_fx_badpos, s_fx_badboth, s_fx_frames;
static unsigned long long s_fx_badw, s_fx_badcov;
/* Distribuicao dos raios dos sprites (PS3_TRACE_FXSIZE), por frame. */
static u32 s_fx_rad_n, s_fx_rad_gt200, s_fx_rad_gt1k, s_fx_rad_gt5k, s_fx_rad_hist_n;
static float s_fx_rad_min = 9e9f, s_fx_rad_max, s_fx_rad_hist[64];
static u32 s_fx_pos_n;
static u32 s_fx_pair_n, s_fx_pair_bad, s_fx_pair_ok;
static int metal_fx_trace(void)
{
    static int on = -1;
    if (on < 0) on = metal_env_on("PS3_TRACE_FXBAD");
    return on;
}
static void metal_fx_inflated_count(void)
{
    if (!metal_fx_trace() || !s_draw.va_mapped) return;
    for (u32 i = 0; i < s_draw.draw_count && i < METAL_MAX_DRAWS; i++) {
        const MetalDrawRec* q = &s_draw.draws[i];
        if (q->fp_hash != 0x59864B76u || !q->va_stride || q->va_slot[3] < 0) continue;
        float maxw = 0.0f, maxp = 0.0f;
        u32 seen = 0;
        for (u32 v = 0; v < q->vertex_count && v < 6u; v++) {
            if ((u64)q->va_base + (u64)(v + 1u) * q->va_stride > (u64)s_draw.va_offset) break;
            const float* vb = s_draw.va_mapped + ((size_t)q->va_base + (size_t)v * q->va_stride) * 4u;
            const float w = vb[q->va_slot[3] * 4 + 3];
            if (w > maxw) maxw = w;
            /* CORRECCAO DE CRITERIO: o defeito registado era posicao E alpha
             * inflados JUNTOS, e a primeira versao disto filtrava so' pelo
             * alpha -- por isso capturou quads de posicao perfeitamente normal
             * e eu conclui que "nao ha' corrupcao". Media metade do fenomeno.
             * Agora os dois criterios sao independentes e a correlacao entre
             * eles e' que diz se sao um fenomeno ou dois. */
            if (q->va_slot[0] >= 0) {
                const float* pp = vb + q->va_slot[0] * 4;
                for (int k = 0; k < 3; k++) {
                    const float m = pp[k] < 0.0f ? -pp[k] : pp[k];
                    if (m > maxp) maxp = m;
                }
            }
            seen++;
        }
        if (!seen) continue;          /* nada lido = nao e' amostra, nao contar */
        /* PS3_TRACE_FXSIZE=1 (diagnostico): o RAIO do sprite em espaco de
         * objeto. A quad estragada tem topologia de billboard, UVs e cor
         * intactas e so' o raio inflado, por isso e' a distribuicao dos raios
         * -- e nao a imagem -- que separa "nasce gigante" de "cresce". */
        if (metal_env_on("PS3_TRACE_FXSIZE") && q->va_slot[0] >= 0) {
            {
                float cx = 0, cy = 0, cz = 0, rad = 0; u32 nv = 0;
                for (u32 v = 0; v < q->vertex_count && v < 6u; v++) {
                    if ((u64)q->va_base + (u64)(v + 1u) * q->va_stride > (u64)s_draw.va_offset) break;
                    const float* pp = s_draw.va_mapped +
                        ((size_t)q->va_base + (size_t)v * q->va_stride) * 4u + q->va_slot[0] * 4;
                    cx += pp[0]; cy += pp[1]; cz += pp[2]; nv++;
                }
                if (nv) {
                    cx /= (float)nv; cy /= (float)nv; cz /= (float)nv;
                    for (u32 v = 0; v < q->vertex_count && v < 6u; v++) {
                        if ((u64)q->va_base + (u64)(v + 1u) * q->va_stride > (u64)s_draw.va_offset) break;
                        const float* pp = s_draw.va_mapped +
                            ((size_t)q->va_base + (size_t)v * q->va_stride) * 4u + q->va_slot[0] * 4;
                        const float dx = pp[0] - cx, dy = pp[1] - cy, dz = pp[2] - cz;
                        const float d = sqrtf(dx*dx + dy*dy + dz*dz);
                        if (d > rad) rad = d;
                    }
                    /* Um resumo POR FRAME em vez de uma linha por quad: o que
                     * decide entre "particula nasce gigante" (distribuicao com
                     * dois picos) e "particula cresce enquanto vive" (um
                     * continuo) e' a distribuicao dos raios do MESMO frame. */
                    s_fx_rad_n++;
                    if (rad < s_fx_rad_min) s_fx_rad_min = rad;
                    if (rad > s_fx_rad_max) s_fx_rad_max = rad;
                    if (rad > 200.0f)  s_fx_rad_gt200++;
                    if (rad > 1000.0f) s_fx_rad_gt1k++;
                    if (rad > 5000.0f) s_fx_rad_gt5k++;
                    if (s_fx_rad_hist_n < 64u) s_fx_rad_hist[s_fx_rad_hist_n++] = rad;
                }
            }
        }
        /* CRITERIO OBJECTIVO, em espaco de CLIP. Os dois anteriores -- alpha e
         * posicao em espaco de objeto -- dependem da cena: no gameplay dao
         * alpha 378 com posicao 15, no menu dao posicao 7 000 com alpha 23, e
         * a correlacao entre eles e' ZERO. Nenhum dos dois define "partícula
         * estragada"; sao proxies de cenas diferentes.
         *
         * O que define e' o que chega ao rasterizador:
         *   w <= 0  -> vertice ATRAS da camera. O quad envolve o ecra e o
         *              rasterizador pinta tudo. E' este o ecra todo vermelho.
         *   cobertura -> uma gota de sangue que ocupa meio ecra esta' errada
         *              mesmo com w positivo.
         * Ambos sao independentes de cena, e e' por isso que substituem os
         * proxies em vez de se somarem a eles. */
        int bad_w = 0; float cxmin = 9e9f, cxmax = -9e9f, cymin = 9e9f, cymax = -9e9f; u32 cnv = 0;
        if (q->va_slot[0] >= 0 && s_draw.vc_mapped && RSX_MAX_VERTEX_CONSTANTS > 467 &&
            (u64)q->vc_byte_offset + (u64)METAL_VC_SLOT_BYTES <= (u64)METAL_VC_RING_BYTES) {
            const float* cc = (const float*)(s_draw.vc_mapped + q->vc_byte_offset);
            for (u32 v = 0; v < q->vertex_count && v < 6u; v++) {
                if ((u64)q->va_base + (u64)(v + 1u) * q->va_stride > (u64)s_draw.va_offset) break;
                const float* pv = s_draw.va_mapped +
                    ((size_t)q->va_base + (size_t)v * q->va_stride) * 4u + q->va_slot[0] * 4;
                float r0[4], o0[4];
                for (int k = 0; k < 4; k++)
                    r0[k] = pv[0]*cc[260*4+k] + pv[1]*cc[261*4+k] + pv[2]*cc[262*4+k] + pv[3]*cc[263*4+k];
                for (int k = 0; k < 4; k++)
                    o0[k] = r0[0]*cc[256*4+k] + r0[1]*cc[257*4+k] + r0[2]*cc[258*4+k] + r0[3]*cc[259*4+k];
                if (!(o0[3] > 1e-6f)) { bad_w = 1; continue; }
                const float nx = o0[0]/o0[3], ny = o0[1]/o0[3];
                if (nx < cxmin) cxmin = nx; if (nx > cxmax) cxmax = nx;
                if (ny < cymin) cymin = ny; if (ny > cymax) cymax = ny;
                cnv++;
            }
        }
        const int bad_cov = cnv >= 3u && ((cxmax - cxmin) > 1.0f || (cymax - cymin) > 1.0f);
        const int bad_a = maxw >= 256.0f, bad_p = maxp >= 1000.0f;
        if (bad_w) s_fx_badw++;
        if (bad_cov) s_fx_badcov++;
        s_fx_total++;
        if (bad_a) s_fx_bad++;
        if (bad_p) s_fx_badpos++;
        if (bad_a && bad_p) s_fx_badboth++;
        if ((bad_w || bad_cov) && s_fx_pos_n < 4u) {
            s_fx_pos_n++;
            fprintf(stderr, "[FXPOS] draw=%u %s%smaxpos=%g maxw=%g ndc=(%.2f x %.2f) nv=%u/%u\n",
                    i, bad_w ? "W<=0 " : "", bad_cov ? "COBRE " : "", maxp, maxw,
                    cnv ? cxmax-cxmin : 0.f, cnv ? cymax-cymin : 0.f, cnv, q->vertex_count);
            for (u32 v = 0; v < q->vertex_count && v < 6u; v++) {
                if ((u64)q->va_base + (u64)(v + 1u) * q->va_stride > (u64)s_draw.va_offset) break;
                const float* vb = s_draw.va_mapped + ((size_t)q->va_base + (size_t)v * q->va_stride) * 4u;
                const float* pp = vb + q->va_slot[0] * 4;
                const float* pc = vb + q->va_slot[3] * 4;
                const float* pu = q->va_slot[8] >= 0 ? vb + q->va_slot[8]*4 : NULL;
                fprintf(stderr, "[FXPOS]   v%u pos=(%g,%g,%g,%g) col=(%g,%g,%g,%g) uv=(%g,%g)\n", v,
                        pp[0], pp[1], pp[2], pp[3], pc[0], pc[1], pc[2], pc[3],
                        pu?pu[0]:0.f, pu?pu[1]:0.f);
            }
        }
        /* Uma inflada e uma sadia DO MESMO FRAME, lado a lado. E' o que separa
         * as duas explicacoes que sobraram, e nenhum dump isolado separa:
         *   - constante errada  -> c467 ou a matriz diferem entre as duas
         *   - vertice corrompido -> c467 igual, so' os dados diferem
         * Sem o par no mesmo frame nao da' para distinguir, porque c467 varia
         * entre frames de qualquer maneira. */
        if (s_fx_pair_n < 6u && s_draw.vc_mapped && RSX_MAX_VERTEX_CONSTANTS > 467 &&
            (u64)q->vc_byte_offset + (u64)METAL_VC_SLOT_BYTES <= (u64)METAL_VC_RING_BYTES) {
            /* CRITERIO DO PAR: w<=0 em espaco de clip, o mesmo que a
             * contagem usa. O anterior (alpha>=256) era um proxy de cena --
             * emparelhava duas particulas sadias e nao separava nada. */
            const int bad = bad_w;
            if ((bad && !s_fx_pair_bad) || (!bad && !s_fx_pair_ok)) {
                if (bad) s_fx_pair_bad = 1; else s_fx_pair_ok = 1;
                const float* c = (const float*)(s_draw.vc_mapped + q->vc_byte_offset);
                fprintf(stderr, "[FXPAIR] %s draw=%u stride=%u slots(p=%d c=%d uv=%d) maxw=%g "
                        "c467=(%g,%g,%g,%g) vc_off=%u\n",
                        bad ? "INFLADA" : "sadia  ", i, q->va_stride,
                        (int)q->va_slot[0], (int)q->va_slot[3], (int)q->va_slot[8], maxw,
                        c[467*4], c[467*4+1], c[467*4+2], c[467*4+3], q->vc_byte_offset);
                {   /* Quem escreveu estes vertices (PS3_TRACE_SPU_PUT=1): a
                     * sadia e a estragada virem do MESMO job separa "job que
                     * falha" de "particula que falha dentro do job". */
                    spu_put_rec w;
                    if (spu_put_owner(q->pos_ea, &w))
                        fprintf(stderr, "[FXPAIR]   pos_ea=%08X gstride=%u <- SPU img=%d "
                                "pc=0x%05X lr=0x%05X dma=[%08X+%u] seq=%llu/%llu\n",
                                q->pos_ea, q->pos_gstride, w.image_id, w.pc, w.lr,
                                w.ea, w.size, w.seq, spu_put_seq());
                    else
                        fprintf(stderr, "[FXPAIR]   pos_ea=%08X gstride=%u <- sem PUT de SPU%s\n",
                                q->pos_ea, q->pos_gstride,
                                spu_put_map_on() ? "" : " (PS3_TRACE_SPU_PUT desligado)");
                }
                /* E488: a unica hipotese que sobrou por testar -- CORRIDA DE
                 * LEITURA. `metal_va_fill` descodifica o buffer do guest no
                 * momento do registo do draw; se o SPU ainda estava a escrever,
                 * a nossa copia e' de meio caminho. Reler AGORA (um frame
                 * depois, com o job ja' terminado) responde sem ambiguidade:
                 *   iguais    -> o SPU escreveu mesmo isto, o defeito e' o job
                 *   diferentes-> lemos cedo demais, o defeito e' nosso
                 * So' le e imprime; nao muda um unico byte do render. */
                if (q->pos_ea && q->pos_gstride && vm_base) {
                    for (u32 v = 0; v < q->vertex_count && v < 6u; v++) {
                        const u32 ea = q->pos_ea + v * q->pos_gstride;
                        if (!ppu_guest_range_committed(ea, 12)) break;
                        const u8* g = vm_base + ea;
                        fprintf(stderr, "[FXRELE]   v%u guest_agora=(%g,%g,%g) ea=%08X\n", v,
                                metal_read_f32_be(g), metal_read_f32_be(g + 4),
                                metal_read_f32_be(g + 8), ea);
                    }
                }
                /* A pergunta aberta: o w<=0 vem da MATRIZ ou da POSICAO? As
                 * duas matrizes do caminho (c260..263 objeto->mundo, c256..259
                 * mundo->clip) lado a lado no mesmo frame respondem: iguais
                 * entre sadia e estragada = sao os dados do vertice. */
                for (int r = 256; r <= 263; r++)
                    fprintf(stderr, "[FXPAIR]   c%d=(%g,%g,%g,%g)\n", r,
                            c[r*4], c[r*4+1], c[r*4+2], c[r*4+3]);
                for (u32 v = 0; v < q->vertex_count && v < 6u; v++) {
                    if ((u64)q->va_base + (u64)(v + 1u) * q->va_stride > (u64)s_draw.va_offset) break;
                    const float* vb = s_draw.va_mapped + ((size_t)q->va_base + (size_t)v * q->va_stride) * 4u;
                    const float* pp = q->va_slot[0] >= 0 ? vb + q->va_slot[0]*4 : NULL;
                    const float* pc = vb + q->va_slot[3]*4;
                    const float* pu = q->va_slot[8] >= 0 ? vb + q->va_slot[8]*4 : NULL;
                    float r0[4] = {0,0,0,0}, o0[4] = {0,0,0,0};
                    if (pp) {
                        for (int k = 0; k < 4; k++)
                            r0[k] = pp[0]*c[260*4+k] + pp[1]*c[261*4+k] +
                                    pp[2]*c[262*4+k] + pp[3]*c[263*4+k];
                        for (int k = 0; k < 4; k++)
                            o0[k] = r0[0]*c[256*4+k] + r0[1]*c[257*4+k] +
                                    r0[2]*c[258*4+k] + r0[3]*c[259*4+k];
                    }
                    fprintf(stderr, "[FXPAIR]   v%u pos=(%g,%g,%g,%g) col=(%g,%g,%g,%g) uv=(%g,%g)"
                            " mundo=(%g,%g,%g,%g) clip=(%g,%g,%g,%g)\n", v,
                            pp?pp[0]:0.f, pp?pp[1]:0.f, pp?pp[2]:0.f, pp?pp[3]:0.f,
                            pc[0], pc[1], pc[2], pc[3], pu?pu[0]:0.f, pu?pu[1]:0.f,
                            r0[0], r0[1], r0[2], r0[3], o0[0], o0[1], o0[2], o0[3]);
                }
                if (s_fx_pair_bad && s_fx_pair_ok) {
                    s_fx_pair_n++; s_fx_pair_bad = 0; s_fx_pair_ok = 0;
                    fprintf(stderr, "[FXPAIR] --- fim do par %u ---\n", s_fx_pair_n);
                }
            }
        }
    }
    if (s_fx_rad_n) {
        /* Ordena a amostra do frame e imprime os quartis: um continuo entre o
         * minimo e o maximo = as particulas CRESCEM; dois grupos separados =
         * nascem estragadas. */
        for (u32 a = 1; a < s_fx_rad_hist_n; a++) {
            const float v = s_fx_rad_hist[a];
            u32 b = a;
            while (b && s_fx_rad_hist[b - 1] > v) { s_fx_rad_hist[b] = s_fx_rad_hist[b - 1]; b--; }
            s_fx_rad_hist[b] = v;
        }
        fprintf(stderr, "[FXDIST] frame=%llu n=%u min=%.1f q1=%.1f med=%.1f q3=%.1f max=%.1f "
                ">200:%u >1k:%u >5k:%u\n",
                (unsigned long long)s_state.frame_count, s_fx_rad_n, (double)s_fx_rad_min,
                (double)s_fx_rad_hist[s_fx_rad_hist_n / 4u],
                (double)s_fx_rad_hist[s_fx_rad_hist_n / 2u],
                (double)s_fx_rad_hist[(s_fx_rad_hist_n * 3u) / 4u],
                (double)s_fx_rad_max, s_fx_rad_gt200, s_fx_rad_gt1k, s_fx_rad_gt5k);
        s_fx_rad_n = s_fx_rad_gt200 = s_fx_rad_gt1k = s_fx_rad_gt5k = s_fx_rad_hist_n = 0;
        s_fx_rad_min = 9e9f; s_fx_rad_max = 0.0f;
    }
    if (++s_fx_frames >= 120ull) {
        spu_put_map_summary();
        if (s_fx_total)
            fprintf(stderr, "[FXBAD] quads=%llu | w<=0:%llu (%.2f%%) cobre>meio-ecra:%llu (%.2f%%) "
                    "| proxies alpha:%llu pos:%llu ambos:%llu\n",
                    s_fx_total,
                    s_fx_badw,   100.0 * (double)s_fx_badw   / (double)s_fx_total,
                    s_fx_badcov, 100.0 * (double)s_fx_badcov / (double)s_fx_total,
                    s_fx_bad, s_fx_badpos, s_fx_badboth);
        s_fx_frames = 0; s_fx_total = 0; s_fx_bad = 0; s_fx_badpos = 0; s_fx_badboth = 0;
        s_fx_badw = 0; s_fx_badcov = 0;
    }
}

static void metal_begin_frame(void* ud)
{
    (void)ud;
    const u32 next_slot = metal_frame_fence_on() ? (s_frame_slot + 1u) % METAL_FRAMES_IN_FLIGHT : 0u;
    metal_va_race_check(next_slot);
    metal_fx_inflated_count();
    s_prompt_pending_n = 0;
    s_prompt_shoulder.active = 0;
    /* Drop the references the previous frame's draw records held (see
     * metal_draw_rec_set_pso); that frame has been committed. */
    for (u32 i = 0; i < s_draw.draw_count && i < METAL_MAX_DRAWS; i++) {
        if (s_draw.draws[i].pso) CFRelease((__bridge CFTypeRef)s_draw.draws[i].pso);
        if (s_draw.draws[i].pso_b) CFRelease((__bridge CFTypeRef)s_draw.draws[i].pso_b);
        s_draw.draws[i].pso_b = nil;
        s_draw.draws[i].pso = nil;
        metal_draw_rec_release_state(&s_draw.draws[i]);
        s_draw.draws[i].va_stride = 0;
    }
    metal_pso_retired_drain();
    s_draw_dropped    = 0;
    s_feedback_draws  = 0;
    s_fallback_pso_draws = 0;
    s_feedback_log_n  = 0;
    /* E486: roda a regiao e ESPERA pelo command buffer que a leu ha' N frames
     * antes de lhe escrever por cima. Portao desligado = uma regiao so',
     * s_frame_slot fica em 0, nada disto corre. */
    if (metal_frame_fence_on()) {
        s_frame_slot = next_slot;
        if (s_frame_cb[s_frame_slot]) {
            [s_frame_cb[s_frame_slot] waitUntilCompleted];
            s_frame_cb[s_frame_slot] = nil;
        }
    }
    s_draw.va_offset  = metal_va_base_f4();
    s_vac_epoch++;   /* entradas do cache de vertices apontam para o anel antigo */
    s_draw.va_last_valid = 0;
    s_draw.vc_offset  = metal_vc_base_bytes();
    s_draw.vc_have    = 0;
    s_draw.fc_offset  = metal_fc_base_bytes();
    s_draw.vb_offset  = metal_vb_base_bytes();
    s_draw.ib_offset  = metal_ib_base_idx() * 4u;
    s_draw.draw_count = 0;
    s_frame_geom      = 0;
    rsx_stats_begin_frame();
    (void)draw_ensure_buffers();
    (void)draw_ensure_pso();
}

static void metal_end_frame(void* ud)
{
    (void)ud;
    if (host_prompts_enabled())
        metal_prompt_shoulder_finish();   /* a label that ended the frame */
    {
        static int tr = -1;
        static u32 nlog = 0;
        if (tr < 0) tr = metal_env_on("PS3_TRACE_METAL_PASSES");
        if (tr && (s_va_ovf_draws || s_vb_trunc_draws || s_draw_dropped || s_vc_ring_full_draws) &&
            nlog < 40u) {
            nlog++;
            fprintf(stderr, "[CAP] frame=%llu draws=%u va_used=%u/%u va_ovf_draws=%u vb_used=%u/%u "
                    "vb_trunc_draws=%u dropped=%u vc_ring_full=%u\n",
                    (unsigned long long)s_state.frame_count, s_draw.draw_count,
                    s_draw.va_offset, (u32)METAL_VA_RING_F4, s_va_ovf_draws,
                    s_draw.vb_offset / (u32)sizeof(MetalBasicVertex), metal_max_verts(),
                    s_vb_trunc_draws, s_draw_dropped, s_vc_ring_full_draws);
        }
        s_va_ovf_draws = 0;
        s_vb_trunc_draws = 0;
        s_vc_ring_full_draws = 0;
    }
    s_state.frame_count++;
    if (metal_max_draws() || s_frame_geom >= 200u) {
        static u32 last_cap_log;
        if (s_state.frame_count <= 5ull ||
            (s_state.frame_count - last_cap_log) >= 30ull) {
            last_cap_log = (u32)s_state.frame_count;
            fprintf(stderr, "[METAL-DRAWCAP] frame=%llu geom=%u cap=%d rec=%u "
                    "logical=%u packets=%u idx=%u tri_rem=%u\n",
                    (unsigned long long)s_state.frame_count, s_frame_geom,
                    metal_max_draws(), s_draw.draw_count,
                    rsx_stats_logical(), rsx_stats_packets(),
                    rsx_stats_indices(), rsx_stats_tri_rem_warns());
            fflush(stderr);
        }
    }
    if (metal_tl_on() && (s_state.frame_count <= 8 ||
                          (s_state.frame_count % 25ull) == 0ull)) {
        METAL_TL("present draws_frame=%u total_draws=%llu",
                 s_draw.draw_count,
                 (unsigned long long)s_draw.total_draws);
    }

    /* Politica do caminho rapido de memoria (ppu_vm_fast_policy): o acessor do
     * PPU so' pode largar as verificacoes DEPOIS de o jogo entrar em gameplay,
     * e quem sabe disso e' o contador de draws do frame. Sem esta chamada o
     * modulo de politica era codigo morto -- nenhum chamador. */
    { extern void ps3_vm_note_gameplay_draws(unsigned draws) __attribute__((weak));
      if (ps3_vm_note_gameplay_draws) ps3_vm_note_gameplay_draws(s_draw.draw_count); }
    {   /* E465: o mesmo sinal alimenta a rotacao de botoes do PS3_PAD_AUTOSTART.
         * Sem isto o autostart ficava em CROSS para sempre -- que no GoW2 e'
         * saltar -- e nenhuma corrida automatica chegava a exercitar combate. */
        extern void pad_autoplay_note_gameplay(void);
        static unsigned armed_draws;
        if (!armed_draws && s_draw.draw_count >= 400u) {
            armed_draws = s_draw.draw_count;
            pad_autoplay_note_gameplay();
        }
    }

    /* Overlay backend metrics: guest-flip cadence (the same frames [FPS]
     * counts) and this frame's recorded guest draws. Sampled here, before
     * the present that may encode the UI pass: UI draws never enter s_draw,
     * so they can never be counted as guest work. Published only once a
     * whole FPS window was measured; the [FPS] log below is untouched. */
    {
        /* Real primitive records only: in per-draw-RT mode the list also holds
         * clears and blits (prim 0xFFFFFFFF), which are not guest draws. */
        const u32 recs = s_draw.draw_count < METAL_MAX_DRAWS ? s_draw.draw_count
                                                             : (u32)METAL_MAX_DRAWS;
        const u32 guest_draws = metal_per_draw_rt()
            ? rsx_metal_count_geometry_records(&s_draw.draws[0].prim,
                                               sizeof(s_draw.draws[0]), recs)
            : recs;
        rsx_overlay_frame_timer_tick(&s_overlay_timer,
                                     clock_gettime_nsec_np(CLOCK_UPTIME_RAW));
        s_overlay_guest_draws_total += guest_draws;
        if (s_overlay_timer.fps_valid) {
            rsx_overlay_backend_metrics metrics;
            metrics.fps = s_overlay_timer.fps;
            metrics.frame_ms = s_overlay_timer.frame_ms;
            metrics.guest_draws_frame = guest_draws;
            metrics.guest_draws_total = s_overlay_guest_draws_total;
            rsx_overlay_publish_backend_metrics(&metrics);
        }
        /* Shader counters, same thread that bumps them in set_shader.
         * compiles = VS+FS function pairs built; failures = FP/VP decompile
         * failures plus VS/FS library compile failures. */
        {
            rsx_overlay_shader_stats shaders;
            shaders.hits = s_draw.fp_cache_hits;
            shaders.misses = s_draw.fp_cache_misses;
            shaders.compiles = s_fn_compiles;
            shaders.failures = s_draw.fp_decompile_fail + s_draw.vp_decompile_fail +
                               s_fn_compile_fail;
            rsx_overlay_publish_shader_stats(&shaders);
        }
    }

    u64 now = SDL_GetTicks64();
    if (now - s_state.last_fps_ticks >= 1000) {
        u64 delta_frames = s_state.frame_count - s_state.last_fps_frame;
        u64 delta_ticks  = now - s_state.last_fps_ticks;
        s_state.fps = (u32)((delta_frames * 1000) / (delta_ticks ? delta_ticks : 1));
        s_state.last_fps_ticks = now;
        s_state.last_fps_frame = s_state.frame_count;

        char title[sizeof s_title_pending];
        /* PS3_WINDOW_TAG: prefixo no titulo, para distinguir duas janelas lado a
         * lado (testar_fix.sh poe "FIX N"). */
        static const char* wtag = (const char*)1;
        if (wtag == (const char*)1) { wtag = getenv("PS3_WINDOW_TAG"); if (wtag && !*wtag) wtag = NULL; }
        char skt[48];
        const char* tagp = wtag;
        if (s_live_skip_vp) {
            snprintf(skt, sizeof skt, "F10 %d/%d SEM VP %08X", s_live_skip_i, s_pick_nvps, s_live_skip_vp);
            tagp = skt;
        } else if (s_live_zalways) {
            snprintf(skt, sizeof skt, "F8 BRONZE Z-ALWAYS");
            tagp = skt;
        } else if (s_live_zbias) {
            snprintf(skt, sizeof skt, "F7 BRONZE BIAS");
            tagp = skt;
        }
        snprintf(title, sizeof(title),
                 "%s%s%s | %u FPS | logical=%u packets=%u metal=%u idx=%u",
                 tagp ? tagp : "", tagp ? " | " : "", s_base_title,
                 s_state.fps, rsx_stats_logical(), rsx_stats_packets(),
                 s_draw.draw_count, rsx_stats_indices());
                {   /* E462: censo do ficheiro de atributos constantes. Antes so' a
             * codificacao 4F era aceite; se 2F/2S/4UB/4S/SCALED4S tiverem
             * contagem > 0, o titulo usa-as e elas eram descartadas. */
            static int vd = -1;
            if (vd < 0) { const char* e = getenv("PS3_RSX_TRACE_VDATA"); vd = (e && e[0] == '1') ? 1 : 0; }
            if (vd) {
                static const char* nm[6] = { "4F", "2F", "2S", "4UB", "4S", "SCALED4S" };
                char b2[192]; int p2 = 0;
                for (u32 k = 0; k < 6u && p2 < 160; k++)
                    p2 += snprintf(b2 + p2, sizeof b2 - (size_t)p2, " %s=%u",
                                   nm[k], rsx_stats_vdata_writes(k));
                fprintf(stderr, "[VDATA]%s | controlo vconst=%u\n", b2,
                        rsx_stats_vconst_writes_total());
            }
        }
        {   /* E482: quanto a mascara de atributos poupa. MEDIDO no palacio:
             * 0,1-0,2%. A hipotese era que se descodificavam atributos que o VP
             * nunca le'; 99,8% sao lidos. Fica o contador, que e' o que fecha a
             * questao. */
            static unsigned long long p_sk, p_dn;
            const unsigned long long dsk = g_va_skip_n - p_sk, ddn = g_va_done_n - p_dn;
            p_sk = g_va_skip_n; p_dn = g_va_done_n;
            if (dsk + ddn)
                fprintf(stderr, "[VASKIP] saltados=%llu descodificados=%llu (%.1f%% poupado)\n",
                        dsk, ddn, 100.0 * (double)dsk / (double)(dsk + ddn));
        }
        {   /* E483: reparticao do trabalho de FIFO entre a thread do guest
             * (sincrono, dentro das chamadas GCM) e a thread de wake
             * (assincrona, poll de 1 ms). Decide se o item "mover o FIFO para a
             * thread dedicada" e' mover trabalho real ou mexer no que ja' esta'
             * assincrono. */
            extern void gcm_fifo_kick_take(unsigned long long*, unsigned long long*);
            unsigned long long ks = 0, kw = 0;
            static unsigned long long pks, pkw;
            gcm_fifo_kick_take(&ks, &kw);
            const unsigned long long dks = ks - pks, dkw = kw - pkw;
            pks = ks; pkw = kw;
            if (dks + dkw)
                fprintf(stderr, "[FIFOKICK] guest=%llu wake=%llu (%.1f%% sincrono)\n",
                        dks, dkw, 100.0 * (double)dks / (double)(dks + dkw));
        }
        {   /* E481: DETECTOR DE RECONFIGURACAO DE APRESENTACAO.
             *
             * Em vez de snapshots manuais "antes/depois" -- que obrigam a
             * acertar o instante em que se carrega no botao -- compara o estado
             * a cada frame e imprime ANTES -> DEPOIS sozinho, campo a campo, so'
             * quando algo muda. Nao depende de timing e apanha qualquer toggle.
             *
             * Cobre o que distingue as tres hipoteses para o Full Frame:
             *   so' viewport/scissor muda  -> nao ha' reconfiguracao de
             *                                 framebuffer, e' enquadramento
             *   surface/pitch/formato muda -> o jogo reconfigura a superficie
             *   RT muda e o flip nao       -> renderizamos num buffer e
             *                                 apresentamos outro (a origem
             *                                 classica de frame partido/verde)
             *
             * Os outros toggles do menu (Hints, volumes, Controller Display,
             * Flight Controls) servem de CONTROLO NEGATIVO: se nenhum deles
             * imprimir nada e o Full Frame imprimir, a correlacao fecha.
             *
             * Sempre ligado: so' escreve quando ha' mudanca, e uma mudanca de
             * apresentacao em runtime e' rara por definicao. */
            const rsx_state* st = s_draw.current_rsx;
            struct DispSt {
                u32 c_off, c_pitch, z_off, z_pitch, fmt, ctarget;
                u32 clip_x, clip_y, clip_w, clip_h;
                u32 vp_x, vp_y, vp_w, vp_h;
                u32 sc_x, sc_y, sc_w, sc_h;
                u32 rt_w, rt_h, dw, dh;
                int c_idx, z_idx;
            };
            struct DispSt cur;
            memset(&cur, 0, sizeof cur);
            if (st) {
                cur.c_off = st->surface_color_offset[0];
                cur.c_pitch = st->surface_color_pitch[0];
                cur.z_off = st->surface_zeta_offset;
                cur.z_pitch = st->surface_zeta_pitch;
                cur.fmt = st->surface_format;
                cur.ctarget = st->color_target;
                cur.clip_x = st->surface_clip_x; cur.clip_y = st->surface_clip_y;
                cur.clip_w = st->surface_clip_w; cur.clip_h = st->surface_clip_h;
                cur.vp_x = st->viewport_x; cur.vp_y = st->viewport_y;
                cur.vp_w = st->viewport_w; cur.vp_h = st->viewport_h;
            }
            cur.sc_x = s_pipe.scissor_x; cur.sc_y = s_pipe.scissor_y;
            cur.sc_w = s_pipe.scissor_w; cur.sc_h = s_pipe.scissor_h;
            cur.c_idx = s_color_rt_idx; cur.z_idx = s_depth_rt_idx;
            if (s_color_rt_idx >= 0 && (u32)s_color_rt_idx < s_n_surfaces &&
                s_surfaces[s_color_rt_idx].tex) {
                cur.rt_w = (u32)s_surfaces[s_color_rt_idx].tex.width;
                cur.rt_h = (u32)s_surfaces[s_color_rt_idx].tex.height;
            }
            if (s_state.layer) {
                const CGSize dsz = s_state.layer.drawableSize;
                cur.dw = (u32)dsz.width; cur.dh = (u32)dsz.height;
            }
            static struct DispSt prev;
            static int have_prev;
            /* A rotacao normal de triple buffer muda color_off e rt_color_idx a
             * cada flip -- 60 mudancas so' no arranque. Se fossem reportadas, a
             * reconfiguracao que procuramos ficava afogada no ruido. Por isso a
             * comparacao IGNORA esses dois campos; eles sao impressos na mesma,
             * mas so' quando algo ESTRUTURAL muda junto, onde servem de
             * contexto (em especial para o caso "o RT mudou e o flip nao"). */
            struct DispSt a_cmp = prev, b_cmp = cur;
            a_cmp.c_off = b_cmp.c_off = 0;
            a_cmp.c_idx = b_cmp.c_idx = 0;
            if (!have_prev) { prev = cur; have_prev = 1; }
            else if (memcmp(&a_cmp, &b_cmp, sizeof a_cmp) != 0) {
                fprintf(stderr, "[DISPCHG] f=%llu  (campo: antes -> depois)\n",
                        (unsigned long long)s_state.frame_count);
#define DCHG_U(campo, nome) \
                if (prev.campo != cur.campo) \
                    fprintf(stderr, "[DISPCHG]   %-12s %10u -> %10u\n", nome, prev.campo, cur.campo)
                DCHG_U(c_off,   "color_off");   DCHG_U(c_pitch, "color_pitch");
                DCHG_U(z_off,   "depth_off");   DCHG_U(z_pitch, "depth_pitch");
                DCHG_U(fmt,     "surf_format"); DCHG_U(ctarget, "color_target");
                DCHG_U(clip_x,  "clip_x");      DCHG_U(clip_y,  "clip_y");
                DCHG_U(clip_w,  "clip_w");      DCHG_U(clip_h,  "clip_h");
                DCHG_U(vp_x,    "viewport_x");  DCHG_U(vp_y,    "viewport_y");
                DCHG_U(vp_w,    "viewport_w");  DCHG_U(vp_h,    "viewport_h");
                DCHG_U(sc_x,    "scissor_x");   DCHG_U(sc_y,    "scissor_y");
                DCHG_U(sc_w,    "scissor_w");   DCHG_U(sc_h,    "scissor_h");
                DCHG_U(rt_w,    "rt_w");        DCHG_U(rt_h,    "rt_h");
                DCHG_U(dw,      "drawable_w");  DCHG_U(dh,      "drawable_h");
#undef DCHG_U
                if (prev.c_idx != cur.c_idx)
                    fprintf(stderr, "[DISPCHG]   %-12s %10d -> %10d\n", "rt_color_idx", prev.c_idx, cur.c_idx);
                if (prev.z_idx != cur.z_idx)
                    fprintf(stderr, "[DISPCHG]   %-12s %10d -> %10d\n", "rt_depth_idx", prev.z_idx, cur.z_idx);
                fflush(stderr);
                prev = cur;
            }
        }
        {   /* PS3_TRACE_CHAR=1 (OFF): censo de submeshes de PERSONAGEM por segundo.
             *
             * Passo 1 da investigacao "membro do Kratos desaparece na cutscene":
             * antes de suspeitar de skinning, provar se o DRAW do submesh sequer
             * e' emitido. Um personagem nao e' uma mesh -- corpo, braco, perna,
             * armadura e arma sao draws separados -- portanto "a perna sumiu"
             * pode ser so' um draw que nao saiu.
             *
             * Agrupado por (vp, fp), que e' o par que identifica um submesh. Um
             * censo por draw seria 600+ linhas por frame e nao se leria; e o
             * TOTAL tambem nao serve, porque quando um membro desaparece o total
             * mal se mexe. O que se ve' e' um GRUPO que estava la' e vai a zero.
             *
             * Layout de personagem = atributo 3 (cor) E atributo 7 (indices de
             * osso), a mesma deteccao que o bloco [PASS] fx ja' usa.
             *
             * Feito para ser ligado A JOGAR: a corrida automatica so' chega ao
             * palacio e a cutscene precisa de alguem a jogar ate' la'. */
            static int tc = -1;
            if (tc < 0) { const char* e = getenv("PS3_TRACE_CHAR"); tc = (e && e[0] == '1') ? 1 : 0; }
            if (tc) {
                struct CharGrp { u32 vp, fp, n, v; float ext; };
                struct CharGrp g[24];
                u32 ng = 0, extra = 0;
                for (u32 di = 0; di < s_draw.draw_count && di < METAL_MAX_DRAWS; di++) {
                    const MetalDrawRec* q = &s_draw.draws[di];
                    if (!q->has_state || q->va_stride == 0u) continue;
                    if (q->va_slot[3] < 0 || q->va_slot[7] < 0) continue;
                    u32 k = 0;
                    while (k < ng && !(g[k].vp == q->vp_hash && g[k].fp == q->fp_hash)) k++;
                    if (k == ng) {
                        if (ng >= 24u) { extra++; continue; }
                        g[ng].vp = q->vp_hash; g[ng].fp = q->fp_hash;
                        g[ng].n = 0; g[ng].v = 0; g[ng].ext = 0.0f; ng++;
                    }
                    g[k].n++; g[k].v += q->vertex_count;
                    /* E480: extensao POR GRUPO. So' o maximo do frame nao
                     * distingue "um membro esticado" de "uma malha de mundo
                     * legitimamente grande" -- e' preciso ver a extensao ao lado
                     * da contagem de vertices do mesmo submesh. */
                    if (s_draw.va_mapped && q->va_slot[0] >= 0) {
                        float lo3[3] = { 1e30f, 1e30f, 1e30f }, hi3[3] = { -1e30f, -1e30f, -1e30f };
                        u32 nv3 = 0;
                        for (u32 v = 0; v < q->vertex_count && v < 32u; v++) {
                            if ((u64)q->va_base + (u64)(v + 1u) * q->va_stride > (u64)s_draw.va_offset) break;
                            const float* pv = s_draw.va_mapped +
                                ((size_t)q->va_base + (size_t)v * q->va_stride) * 4u +
                                (size_t)q->va_slot[0] * 4u;
                            for (int c = 0; c < 3; c++) {
                                if (pv[c] < lo3[c]) lo3[c] = pv[c];
                                if (pv[c] > hi3[c]) hi3[c] = pv[c];
                            }
                            nv3++;
                        }
                        if (nv3 >= 4u) {
                            const float dx3 = hi3[0]-lo3[0], dy3 = hi3[1]-lo3[1], dz3 = hi3[2]-lo3[2];
                            const float e3 = dx3 > dy3 ? (dx3 > dz3 ? dx3 : dz3) : (dy3 > dz3 ? dy3 : dz3);
                            if (e3 > g[k].ext) g[k].ext = e3;
                        }
                    }
                }
                for (u32 x = 0; x < ng; x++)
                    for (u32 y = x + 1u; y < ng; y++)
                        if (g[y].v > g[x].v) {
                            struct CharGrp t = g[x]; g[x] = g[y]; g[y] = t;
                        }
                /* E479: alem de contar, medir se algum submesh esta' ESTICADO.
                 * O sintoma reportado na cutscene -- braco e coxa com bandas
                 * alongadas e mao deslocada -- e' o que se ve' quando o skinning
                 * puxa vertices para longe: o membro nao desaparece, estica.
                 *
                 * Mede em ESPACO DE OBJECTO, que e' o que o host tem antes do
                 * vertex shader: a maior distancia entre dois vertices do draw.
                 * Um submesh de membro e' compacto; se a extensao saltar uma
                 * ordem de grandeza entre frames, foi puxado. Nao prova que o
                 * culpado e' o skinning -- prova que os vertices que ENTRAM ja'
                 * estao dispersos, ou que nao estao (e ai' o culpado e' o VP). */
                float ext_max = 0.0f; u32 ext_vp = 0, ext_fp = 0;
                if (s_draw.va_mapped) {
                    for (u32 di = 0; di < s_draw.draw_count && di < METAL_MAX_DRAWS; di++) {
                        const MetalDrawRec* q = &s_draw.draws[di];
                        if (!q->has_state || q->va_stride == 0u) continue;
                        if (q->va_slot[3] < 0 || q->va_slot[7] < 0 || q->va_slot[0] < 0) continue;
                        float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
                        u32 nv = 0;
                        for (u32 v = 0; v < q->vertex_count && v < 64u; v++) {
                            if ((u64)q->va_base + (u64)(v + 1u) * q->va_stride > (u64)s_draw.va_offset) break;
                            const float* pv = s_draw.va_mapped +
                                ((size_t)q->va_base + (size_t)v * q->va_stride) * 4u +
                                (size_t)q->va_slot[0] * 4u;
                            for (int c = 0; c < 3; c++) {
                                if (pv[c] < lo[c]) lo[c] = pv[c];
                                if (pv[c] > hi[c]) hi[c] = pv[c];
                            }
                            nv++;
                        }
                        if (nv < 4u) continue;
                        const float dx = hi[0] - lo[0], dy = hi[1] - lo[1], dz = hi[2] - lo[2];
                        const float ext = (dx > dy ? (dx > dz ? dx : dz) : (dy > dz ? dy : dz));
                        if (ext > ext_max) { ext_max = ext; ext_vp = q->vp_hash; ext_fp = q->fp_hash; }
                    }
                }
                char b[420]; int pz = 0;
                for (u32 x = 0; x < ng && x < 8u && pz < 370; x++)
                    pz += snprintf(b + pz, sizeof b - (size_t)pz, " %08X:%ux%u/e%.0f",
                                   g[x].vp, g[x].n, g[x].v, g[x].ext);
                b[pz < 419 ? pz : 419] = '\0';
                fprintf(stderr, "[CHAR] f=%llu grupos=%u maior_extensao=%.1f(%08X/%08X)%s%s\n",
                        (unsigned long long)s_state.frame_count, ng,
                        ext_max, ext_vp, ext_fp, b,
                        extra ? " (+extra)" : "");
                fflush(stderr);
            }
        }
        {   /* E464: abortos de job SPU por segundo. Os logs desta arvore tem
             * 5 482 683 abortos, todos no alvo 0x0391C da imagem 0, e a correlacao
             * medida (567 amostras, 9 corridas, MESMOS ~753 draws) e' brutal:
             *   < 0,25 jobs/frame -> 17,5 ms (57 fps)
             *   >= 2  jobs/frame -> 46,9 ms (21 fps)
             * Sem este contador na linha [FPS] a causalidade nao se prova: so' se
             * ve' a correlacao entre corridas. Sempre ligado -- e' um contador. */
            extern unsigned long long g_spu_abort_unknown __attribute__((weak));
            static unsigned long long prev_ab;
            unsigned long long ab = (&g_spu_abort_unknown) ? g_spu_abort_unknown : 0ull;
            fprintf(stderr, "[SPUABORT] no_segundo=%llu total=%llu\n",
                    (unsigned long long)(ab - prev_ab), ab);
            prev_ab = ab;
        }
        {   /* PS3_METAL_TRACE_SCISSOR=1 (OFF): quantos draws o scissor rejeitou por
             * nao intersectar o alvo. Se ficar 0 o fix do E463 nao e' exercitado
             * pelo GoW2 e nao se pode dizer que corrigiu nada aqui. */
            static int ts = -1;
            if (ts < 0) { const char* e = getenv("PS3_METAL_TRACE_SCISSOR"); ts = (e && e[0] == '1') ? 1 : 0; }
            if (ts) fprintf(stderr, "[SCISSOR] rejeitados=%u\n", s_scissor_reject);
        }
        {   /* PS3_TRACE_FPS=1 (OFF, benchmark): one line per second with the
             * presented FPS and the host CPU time of present (replay encode) */
            static int tf = -1;
            if (tf < 0) tf = metal_env_on("PS3_TRACE_FPS");
            os_unfair_lock_lock(&s_perf_lock);
            const double psum = s_perf_present_ms_sum, pmax = s_perf_present_ms_max;
            const double gpu_ms = s_perf_gpu_ms_last;
            s_perf_present_ms_sum = 0.0;
            s_perf_present_ms_max = 0.0;
            os_unfair_lock_unlock(&s_perf_lock);
            if (tf) {
                extern double gcm_perf_take_dec_ms(void);   /* FIFO decode since the last call */
                const double dec_ms = gcm_perf_take_dec_ms();
                /* Giant-lock accounting lives in ppu_loader.cpp, which CMake keeps
                 * out of this library (boot links it separately) -- weak so the
                 * standalone lib and the unit tests still link. Needs
                 * PS3_TRACE_GIANTSTAT=1; without it the counters stay at 0. */
                extern void ppu_giant_stat_take(double*, double*, long long*) __attribute__((weak));
                double gwait = 0.0, ghold = 0.0; long long gacq = 0;
                if (ppu_giant_stat_take) ppu_giant_stat_take(&gwait, &ghold, &gacq);
                /* PS3_TRACE_VMCOUNT=1: acessos de memoria guest por segundo.
                 * Decide se o custo esta' no por-acesso ou no volume. */
                extern void ppu_vm_count_take(long long*, long long*) __attribute__((weak));
                long long vrd = 0, vwr = 0;
                if (ppu_vm_count_take) ppu_vm_count_take(&vrd, &vwr);
                if (vrd || vwr)
                    fprintf(stderr, "[VMCOUNT] reads=%lld writes=%lld por segundo "
                            "(= %.2f M/frame a %u fps)\n", vrd, vwr,
                            s_state.fps ? (double)(vrd + vwr) / (double)s_state.fps / 1e6 : 0.0,
                            s_state.fps);
                {   /* E478: TRABALHO DE CPU POR FRAME -- metrica que nao se move com
                     * a carga. fps e' tempo de PAREDE e nesta maquina ha' outra
                     * sessao a correr o jogo (carga de 2,5 a 12,8 hoje). Sob
                     * contencao o processo recebe menos CPU por segundo, mas o
                     * TRABALHO por frame nao muda -- logo cpu_ms/frame isola a
                     * optimizacao do ruido da maquina. getrusage soma todas as
                     * threads, que e' o que interessa (PPU + dois SPU). */
                    struct rusage ru;
                    static double prev_cpu_ms;
                    if (getrusage(RUSAGE_SELF, &ru) == 0) {
                        const double cpu_ms =
                            (double)ru.ru_utime.tv_sec * 1000.0 + (double)ru.ru_utime.tv_usec / 1000.0 +
                            (double)ru.ru_stime.tv_sec * 1000.0 + (double)ru.ru_stime.tv_usec / 1000.0;
                        const double d = cpu_ms - prev_cpu_ms;
                        prev_cpu_ms = cpu_ms;
                        if (delta_frames)
                            fprintf(stderr, "[CPUWORK] cpu_ms_frame=%.2f cpu_ms_s=%.0f frames=%llu draws=%u\n",
                                    d / (double)delta_frames, d,
                                    (unsigned long long)delta_frames, s_draw.draw_count);
                    }
                }
                fprintf(stderr, "[FPS] fps=%u draws=%u present_ms_avg=%.2f present_ms_max=%.2f "
                        "gpu_ms=%.2f decode_ms_avg=%.2f giant_wait_ms=%.1f giant_hold_ms=%.1f "
                        "giant_acq=%lld\n",
                        s_state.fps, s_draw.draw_count,
                        delta_frames ? psum / (double)delta_frames : 0.0, pmax,
                        gpu_ms,
                        delta_frames ? dec_ms / (double)delta_frames : 0.0,
                        gwait, ghold, gacq);
                fflush(stderr);
            }
            {   /* PS3_TRACE_RSX_SHADERS=1: the decompiler's score once a second -- the
                 * summary at shutdown never prints, runs end with a kill. */
                static int ts = -1;
                if (ts < 0) ts = metal_env_on("PS3_TRACE_RSX_SHADERS");
                if (ts) {
                    fprintf(stderr, "[SHSTAT] fp_ok=%u fp_fail=%u vp_ok=%u vp_fail=%u cache=%u "
                            "fallback_pso_draws=%llu vconst_w=%u vconst_max=%u vconst_dropped=%u "
                            "tex_ok=%u tex_skip=%u\n",
                            s_draw.fp_decompile_ok, s_draw.fp_decompile_fail,
                            s_draw.vp_decompile_ok, s_draw.vp_decompile_fail,
                            s_draw.shader_cache_count,
                            (unsigned long long)s_fallback_pso_draws,
                            rsx_stats_vconst_writes(), rsx_stats_vconst_max(),
                            rsx_stats_vconst_dropped(), s_draw.bind_ok, s_draw.bind_skip);
                    for (u32 k = 0; k < s_cmask_n; k++)
                        fprintf(stderr, "[CMASK] 0x%08X draws=%u\n", s_cmask_seen[k], s_cmask_cnt[k]);
                    s_cmask_n = 0;   /* per second, not per frame */
                    fflush(stderr);
                    fflush(stderr);
                }
            }
        }
        if (s_state.window) {
            if (pthread_main_np()) {
                SDL_SetWindowTitle(s_state.window, title);
            } else {
                os_unfair_lock_lock(&s_title_lock);
                snprintf(s_title_pending, sizeof s_title_pending, "%s", title);
                s_title_has_pending = 1;
                os_unfair_lock_unlock(&s_title_lock);
            }
        }
    }
}

/*
 * Copy the just-rendered drawable into a shared buffer and write it out.
 *
 * The blit is a separate command buffer that we wait on, which is exactly the
 * kind of pipeline stall you never want in a real frame -- acceptable because
 * this only runs under PS3_FRAME_DUMP, which is a debugging and CI aid.
 */
/* Copy a BGRA8 texture to a shared buffer on its own command buffer (queued
 * after the frame's work) and write it as a BMP. Debug only. */
static void metal_dump_texture_to(id<MTLTexture> tex, const char* path)
{
    const NSUInteger w = tex.width, h = tex.height, stride = w * 4, size = stride * h;
    id<MTLBuffer> staging = [s_state.device newBufferWithLength:size
                                                        options:MTLResourceStorageModeShared];
    if (!staging) return;
    id<MTLCommandBuffer> cb = [s_state.queue commandBuffer];
    id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
    [blit copyFromTexture:tex sourceSlice:0 sourceLevel:0
             sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(w, h, 1)
                 toBuffer:staging destinationOffset:0
   destinationBytesPerRow:stride destinationBytesPerImage:size];
    [blit endEncoding];
    [cb commit];
    [cb waitUntilCompleted];
    rsx_write_bmp_bgra32(path, (const u8*)staging.contents, (u32)w, (u32)h);
    fprintf(stderr, "[RSX metal] dumped %s (%ux%u)\n", path, (u32)w, (u32)h);
    /* PS3_METAL_SURF_DUMP_ALPHA=1 (OFF, diagnostic): the alpha channel as a grey
     * <path>_a.bmp plus its mean and share above 0.5. GoW2's bloom bright-pass
     * (FP 654864DA) scales by the scene's alpha and the 304x171 chain comes out
     * white (2026-09-16); the BGRA BMP above drops alpha. */
    static int da = -1;
    if (da < 0) da = metal_env_on("PS3_METAL_SURF_DUMP_ALPHA");
    if (da) {
        const u8* px = (const u8*)staging.contents;
        u8* g = (u8*)malloc(size);
        if (g) {
            u64 sum = 0, hi = 0;
            for (NSUInteger i = 0; i < size; i += 4) {
                const u8 a = px[i + 3];
                g[i] = a; g[i + 1] = a; g[i + 2] = a; g[i + 3] = 255u;
                sum += a;
                if (a > 127u) hi++;
            }
            const size_t pl = strlen(path);
            char ap[128];
            snprintf(ap, sizeof(ap), "%.*s_a.bmp", (int)(pl > 4 ? pl - 4 : pl), path);
            rsx_write_bmp_bgra32(ap, g, (u32)w, (u32)h);
            const u64 n = (u64)w * (u64)h;
            fprintf(stderr, "[RSX metal] alpha %s mean=%.3f above_half=%.1f%%\n", path,
                    n ? (double)sum / (255.0 * (double)n) : 0.0,
                    n ? 100.0 * (double)hi / (double)n : 0.0);
            free(g);
        }
    }
    fflush(stderr);
}

/* PS3_METAL_SURF_DUMP=<frame> (OFF; per-draw mode): write every BGRA8 colour
 * surface as surf_f<frame>_s<idx>_<offset>.bmp at that frame and the next, to
 * see where a frame's pixels stop (scene target, post chain, display buffer). */
static long long s_surf_dump_at = -1;   /* frame the surface dump fired on (PASS log) */
static int s_last_show = -1, s_last_show_by_last = 0;   /* surface presented (shot log) */
static void metal_surf_dump_maybe(void)
{
    static long long at = -2;
    static u32 minrec = 500u;
    static u32 delay = 0u;
    if (at == -2) {
        const char* e = getenv("PS3_METAL_SURF_DUMP");
        /* "auto": the first frame with >= 500 records (the 3D scene) and the
         * next one -- the intro's frame count varies between runs (E452: the
         * first game frame was ~33 000, so a fixed 30500 fell in the intro). */
        at = (e && *e) ? (strcmp(e, "auto") == 0 ? -3 : atoll(e)) : -1;
        /* PS3_METAL_SURF_DUMP_MINREC: record threshold for "auto". The front
         * end's 3D portrait already has ~550 records; gameplay frames have
         * 1 700-3 300 (2026-09-16), so 1500 lands on gameplay. Clamped. */
        const char* m = getenv("PS3_METAL_SURF_DUMP_MINREC");
        if (m && *m) {
            errno = 0;
            unsigned long v = strtoul(m, NULL, 10);
            if (errno == ERANGE) v = 0ul;
            if (v >= 1ul && v <= (unsigned long)METAL_MAX_DRAWS) minrec = (u32)v;
        }
        /* PS3_METAL_SURF_DUMP_DELAY: frames after the threshold before dumping
         * (the first gameplay frame is often a shot without the characters). */
        const char* dl = getenv("PS3_METAL_SURF_DUMP_DELAY");
        if (dl && *dl) {
            errno = 0;
            unsigned long v = strtoul(dl, NULL, 10);
            if (errno == ERANGE) v = 100001ul;
            if (v <= 100000ul) delay = (u32)v;
        }
    }
    if (at == -3 && s_draw.draw_count >= minrec)
        at = (long long)s_state.frame_count + (long long)delay;
    if (at < 0) return;
    s_surf_dump_at = at;
    const unsigned long long f = s_state.frame_count;
    if (f != (unsigned long long)at && f != (unsigned long long)at + 1ull) return;
    for (u32 k = 0; k < s_n_surfaces; k++) {
        if (s_surfaces[k].is_depth || !s_surfaces[k].tex) continue;
        if (s_surfaces[k].tex.pixelFormat != MTLPixelFormatBGRA8Unorm) continue;
        char path[96];
        snprintf(path, sizeof(path), "surf_f%llu_s%u_%08X.bmp", f, k, s_surfaces[k].offset);
        metal_dump_texture_to(s_surfaces[k].tex, path);
    }
}

static void metal_dump_frame(id<MTLTexture> tex)
{
    const NSUInteger w      = tex.width;
    const NSUInteger h      = tex.height;
    const NSUInteger stride = w * 4;
    const NSUInteger size   = stride * h;

    id<MTLBuffer> staging = [s_state.device newBufferWithLength:size
                                                        options:MTLResourceStorageModeShared];
    if (!staging) {
        printf("[RSX metal] frame dump: staging buffer allocation failed\n");
        return;
    }

    id<MTLCommandBuffer> cb = [s_state.queue commandBuffer];
    id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
    [blit copyFromTexture:tex
              sourceSlice:0
              sourceLevel:0
             sourceOrigin:MTLOriginMake(0, 0, 0)
               sourceSize:MTLSizeMake(w, h, 1)
                 toBuffer:staging
        destinationOffset:0
   destinationBytesPerRow:stride
 destinationBytesPerImage:size];
    [blit endEncoding];
    [cb commit];
    [cb waitUntilCompleted];

    char path[64];
    /* Decode-thread movie present does not bump frame_count — use dump_idx. */
    static u64 dump_idx = 0;
    dump_idx++;
    int movie_dump = s_movie.active && s_movie.hle_source;
    if (movie_dump)
        snprintf(path, sizeof(path), "frame_movie_%llu.bmp",
                 (unsigned long long)dump_idx);
    else
        snprintf(path, sizeof(path), "frame_%llu.bmp",
                 (unsigned long long)s_state.frame_count);

    /* BGRA8Unorm is byte order B,G,R,A in memory, matching BMP directly. */
    rsx_write_bmp_bgra32(path, (const u8*)staging.contents, (u32)w, (u32)h);
    fprintf(stderr, "[RSX metal] dumped %s (%ux%u)\n", path, (u32)w, (u32)h);
    fflush(stderr);
}

/* MetalFX (Port Apple, servico de HOST de present): lazy-init + gate unico.
 * Devolve o ctx do upscaler quando PS3_METALFX esta' ligado E o MetalFX esta'
 * disponivel neste device/OS; caso contrario NULL (baseline no-op). Partilhado
 * pelo ramo guest (RT 3D) e pelo ramo overlay (content-hold / logos). */
static rmu_ctx  s_mfx;
static int      s_mfx_env    = -1;
static int      s_mfx_inited = 0;
static rmu_ctx* metal_metalfx(void)
{
    if (s_mfx_env < 0)
        s_mfx_env = metal_env_default("PS3_METALFX", 1);
    if (!s_mfx_env || !s_state.device) return NULL;
    if (!s_mfx_inited)
        s_mfx_inited = (rmu_init(&s_mfx, s_state.device) == 0);
    return (s_mfx_inited && rmu_available(&s_mfx)) ? &s_mfx : NULL;
}

/* Letterbox (padrao de jogo): maior rect com o aspect do guest (aw:ah) que cabe
 * no drawable (dw x dh), centrado -> barras pretas quando os aspects diferem
 * (ex.: 16:9 num ecra 16:10 fullscreen). Quando os aspects batem (janela 2x) o
 * rect e' o drawable inteiro => no-op. PS3_STRETCH=1 forca preencher (sem
 * barras, estica). */
typedef struct { u32 x, y, w, h; } MetalRect;

static int metal_stretch_on(void)
{
    static int v = -1;
    if (v < 0) { const char* e = getenv("PS3_STRETCH"); v = (e && e[0] && e[0] != '0') ? 1 : 0; }
    return v;
}

static MetalRect metal_letterbox(u32 dw, u32 dh, u32 aw, u32 ah)
{
    MetalRect r = { 0, 0, dw, dh };
    if (metal_stretch_on() || aw == 0 || ah == 0 || dw == 0 || dh == 0) return r;
    /* Compara aspects por produto cruzado (sem divisao/float). */
    unsigned long long lhs = (unsigned long long)aw * dh;   /* aw/ah vs dw/dh */
    unsigned long long rhs = (unsigned long long)ah * dw;
    if (lhs == rhs) return r;                                /* aspects iguais */
    if (lhs > rhs) {                                         /* guest mais largo -> ajusta por largura */
        u32 h = (u32)(((unsigned long long)dw * ah) / aw);
        r.w = dw; r.h = h; r.x = 0; r.y = (dh - h) / 2;
    } else {                                                 /* guest mais alto -> ajusta por altura */
        u32 w = (u32)(((unsigned long long)dh * aw) / ah);
        r.w = w; r.h = dh; r.x = (dw - w) / 2; r.y = 0;
    }
    return r;
}

/* Scaled blit between surfaces (NV3089 in per-draw mode): a quad over the
 * destination rect sampling a source sub-rect, linear filter. */
static __unsafe_unretained id<MTLRenderPipelineState> s_blit_pso = nil;
static __unsafe_unretained id<MTLSamplerState> s_blit_samp = nil;

/* Depth surface sampled as a colour texture. On the RSX a D24S8 buffer read
 * through a colour texture format returns its bytes; RPCS3
 * RSXFragmentTextureDepthConversion.glsl (verified on the GOW collection) gives
 * the texel as (g, b, stencil, r) / 255 of the 24-bit depth value. GoW2's
 * scene copy 6B2E71EA rebuilds the scene depth from those bytes (.xyw) for the
 * post pass, and its particles/light shafts read the same unit; without this
 * the unit held stale guest memory and the effects drew as huge red/white
 * planes (2026-09-16 gameplay shots). PS3_METAL_DEPTH_AS_COLOR=0 disables. */
static int metal_depth_as_color_on(void)
{
    static int on = -1;
    if (on < 0) { const char* e = getenv("PS3_METAL_DEPTH_AS_COLOR"); on = (e && e[0] == '0') ? 0 : 1; }
    return on;
}

static id<MTLSamplerState> metal_nearest_samp(void)
{
    static __unsafe_unretained id<MTLSamplerState> ns = nil;
    if (!ns && s_state.device) {
        MTLSamplerDescriptor* sd = [MTLSamplerDescriptor new];
        sd.minFilter = MTLSamplerMinMagFilterNearest;
        sd.magFilter = MTLSamplerMinMagFilterNearest;
        sd.sAddressMode = MTLSamplerAddressModeClampToEdge;
        sd.tAddressMode = MTLSamplerAddressModeClampToEdge;
        id<MTLSamplerState> st = [s_state.device newSamplerStateWithDescriptor:sd];
        if (st) ns = (__bridge id<MTLSamplerState>)CFBridgingRetain(st);
    }
    return ns;
}

/* Encode surface k's depth into its BGRA8 conv texture if it changed since the
 * last conversion. Must run between render encoders of cb. */
static void metal_depth_color_convert(id<MTLCommandBuffer> cb, int k)
{
    if (k < 0 || (u32)k >= s_n_surfaces || !s_surfaces[k].is_depth || !s_surfaces[k].tex || !cb) return;
    MetalSurface* sf = &s_surfaces[k];
    const NSUInteger w = sf->tex.width, h = sf->tex.height;
    if (sf->conv && sf->conv.width == w && sf->conv.height == h && sf->conv_serial == sf->wserial) return;
    static __unsafe_unretained id<MTLRenderPipelineState> pso = nil;
    static int failed = 0;
    if (!pso && !failed) {
        NSString* src = @
            "#include <metal_stdlib>\n"
            "using namespace metal;\n"
            "struct ZO { float4 pos [[position]]; };\n"
            "vertex ZO z2c_vs(uint vid [[vertex_id]]) {\n"
            "  ZO o; o.pos = float4(vid == 1u ? 3.0 : -1.0, vid == 2u ? -3.0 : 1.0, 0.0, 1.0); return o;\n"
            "}\n"
            "fragment float4 z2c_fs(ZO i [[stage_in]], depth2d<float> z [[texture(0)]]) {\n"
            "  float d = clamp(z.read(uint2(i.pos.xy)), 0.0, 1.0);\n"
            "  uint v = uint(d * 16777215.0);\n"
            "  float r = float((v >> 16) & 0xFFu), g = float((v >> 8) & 0xFFu), b = float(v & 0xFFu);\n"
            "  return float4(g, b, 255.0, r) / 255.0;\n"
            "}\n";
        NSError* err = nil;
        id<MTLLibrary> lib = [s_state.device newLibraryWithSource:src options:nil error:&err];
        id<MTLFunction> vs = lib ? [lib newFunctionWithName:@"z2c_vs"] : nil;
        id<MTLFunction> fs = lib ? [lib newFunctionWithName:@"z2c_fs"] : nil;
        MTLRenderPipelineDescriptor* desc = [MTLRenderPipelineDescriptor new];
        desc.vertexFunction = vs;
        desc.fragmentFunction = fs;
        desc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
        id<MTLRenderPipelineState> p = (vs && fs)
            ? [s_state.device newRenderPipelineStateWithDescriptor:desc error:&err] : nil;
        if (!p) {
            failed = 1;
            printf("[RSX metal] depth->colour PSO failed: %s\n",
                   err ? [[err localizedDescription] UTF8String] : "?");
            return;
        }
        pso = (__bridge id<MTLRenderPipelineState>)CFBridgingRetain(p);
    }
    if (!pso) return;
    if (!sf->conv || sf->conv.width != w || sf->conv.height != h) {
        if (sf->conv) { CFRelease((__bridge CFTypeRef)sf->conv); sf->conv = nil; }
        MTLTextureDescriptor* td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                                        width:w height:h mipmapped:NO];
        td.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        td.storageMode = MTLStorageModePrivate;
        id<MTLTexture> t = [s_state.device newTextureWithDescriptor:td];
        if (!t) return;
        sf->conv = (__bridge id<MTLTexture>)CFBridgingRetain(t);
    }
    MTLRenderPassDescriptor* rp = [MTLRenderPassDescriptor renderPassDescriptor];
    rp.colorAttachments[0].texture = sf->conv;
    rp.colorAttachments[0].loadAction = MTLLoadActionDontCare;
    rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    id<MTLRenderCommandEncoder> e = [cb renderCommandEncoderWithDescriptor:rp];
    if (!e) return;
    [e setRenderPipelineState:pso];
    [e setFragmentTexture:sf->tex atIndex:0];
    [e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    [e endEncoding];
    sf->conv_serial = sf->wserial;
    static u32 nconv = 0;
    if (metal_env_on("PS3_TRACE_METAL_PASSES") && nconv < 8u) {
        nconv++;
        fprintf(stderr, "[PASS] depth->colour surf=%d off=0x%08X %lux%lu serial=%u\n", k, sf->offset,
                (unsigned long)w, (unsigned long)h, sf->wserial);
    }
}

static int metal_blit_pso_ready(void)
{
    if (s_blit_pso && s_blit_samp) return 1;
    if (!s_state.device) return 0;
    static int failed = 0;
    if (failed) return 0;
    NSString* src = @
        "#include <metal_stdlib>\n"
        "using namespace metal;\n"
        "struct BO { float4 pos [[position]]; float2 uv; };\n"
        "vertex BO blit_vs(uint vid [[vertex_id]], constant float4& r [[buffer(0)]]) {\n"
        "  float2 p = float2((vid & 1u) ? 1.0 : 0.0, (vid & 2u) ? 1.0 : 0.0);\n"
        "  BO o; o.pos = float4(p.x * 2.0 - 1.0, 1.0 - p.y * 2.0, 0.0, 1.0);\n"
        "  o.uv = float2(mix(r.x, r.z, p.x), mix(r.y, r.w, p.y)); return o;\n"
        "}\n"
        "fragment float4 blit_fs(BO i [[stage_in]], texture2d<float> t [[texture(0)]],\n"
        "                        sampler s [[sampler(0)]]) { return t.sample(s, i.uv); }\n";
    NSError* err = nil;
    id<MTLLibrary> lib = [s_state.device newLibraryWithSource:src options:nil error:&err];
    id<MTLFunction> vs = lib ? [lib newFunctionWithName:@"blit_vs"] : nil;
    id<MTLFunction> fs = lib ? [lib newFunctionWithName:@"blit_fs"] : nil;
    if (!vs || !fs) {
        failed = 1;
        printf("[RSX metal] blit PSO library failed: %s\n",
               err ? [[err localizedDescription] UTF8String] : "?");
        return 0;
    }
    MTLRenderPipelineDescriptor* desc = [MTLRenderPipelineDescriptor new];
    desc.vertexFunction   = vs;
    desc.fragmentFunction = fs;
    desc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    id<MTLRenderPipelineState> pso = [s_state.device newRenderPipelineStateWithDescriptor:desc
                                                                                   error:&err];
    if (!pso) {
        failed = 1;
        printf("[RSX metal] blit PSO create failed: %s\n",
               err ? [[err localizedDescription] UTF8String] : "?");
        return 0;
    }
    MTLSamplerDescriptor* sd = [MTLSamplerDescriptor new];
    sd.minFilter = MTLSamplerMinMagFilterLinear;
    sd.magFilter = MTLSamplerMinMagFilterLinear;
    sd.sAddressMode = MTLSamplerAddressModeClampToEdge;
    sd.tAddressMode = MTLSamplerAddressModeClampToEdge;
    id<MTLSamplerState> samp = [s_state.device newSamplerStateWithDescriptor:sd];
    if (!samp) { failed = 1; return 0; }
    s_blit_pso  = (__bridge id<MTLRenderPipelineState>)CFBridgingRetain(pso);
    s_blit_samp = (__bridge id<MTLSamplerState>)CFBridgingRetain(samp);
    return 1;
}

/* In-encoder RSX clear: same target, no store/load round-trip. */
static __unsafe_unretained id<MTLRenderPipelineState> s_clear_pso = nil;
static __unsafe_unretained id<MTLRenderPipelineState> s_clear_pso_z = nil;
static __unsafe_unretained id<MTLDepthStencilState>   s_clear_ds[4];

static int metal_clear_pso_ready(void)
{
    if (s_clear_pso && s_clear_pso_z && s_clear_ds[3])
        return 1;
    if (!s_state.device)
        return 0;
    static int failed = 0;
    if (failed)
        return 0;
    NSString* src = @
        "#include <metal_stdlib>\n"
        "using namespace metal;\n"
        "struct CO { float4 pos [[position]]; };\n"
        "vertex CO clear_vs(uint vid [[vertex_id]], constant float& z [[buffer(1)]]) {\n"
        "  float2 p[3] = { float2(-1,-1), float2(3,-1), float2(-1,3) };\n"
        "  CO o; o.pos = float4(p[vid], z, 1); return o;\n"
        "}\n"
        "fragment float4 clear_fs(constant float4& c [[buffer(0)]]) { return c; }\n";
    NSError* err = nil;
    id<MTLLibrary> lib = [s_state.device newLibraryWithSource:src options:nil error:&err];
    id<MTLFunction> vs = lib ? [lib newFunctionWithName:@"clear_vs"] : nil;
    id<MTLFunction> fs = lib ? [lib newFunctionWithName:@"clear_fs"] : nil;
    if (!vs || !fs) {
        failed = 1;
        return 0;
    }
    for (int zonly = 0; zonly < 2; zonly++) {
        MTLRenderPipelineDescriptor* desc = [MTLRenderPipelineDescriptor new];
        desc.vertexFunction = vs;
        desc.fragmentFunction = fs;
        desc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
        desc.colorAttachments[0].writeMask =
            zonly ? MTLColorWriteMaskNone : MTLColorWriteMaskAll;
        desc.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float_Stencil8;
        desc.stencilAttachmentPixelFormat = MTLPixelFormatDepth32Float_Stencil8;
        id<MTLRenderPipelineState> pso =
            [s_state.device newRenderPipelineStateWithDescriptor:desc error:&err];
        if (!pso) {
            failed = 1;
            return 0;
        }
        if (zonly)
            s_clear_pso_z = (__bridge id<MTLRenderPipelineState>)CFBridgingRetain(pso);
        else
            s_clear_pso = (__bridge id<MTLRenderPipelineState>)CFBridgingRetain(pso);
    }
    for (int i = 0; i < 4; i++) {
        MTLDepthStencilDescriptor* dd = [MTLDepthStencilDescriptor new];
        const int dw = (i & 1) != 0;
        const int sw = (i & 2) != 0;
        dd.depthWriteEnabled = dw ? YES : NO;
        dd.depthCompareFunction = MTLCompareFunctionAlways;
        if (sw) {
            MTLStencilDescriptor* st = [MTLStencilDescriptor new];
            st.stencilCompareFunction = MTLCompareFunctionAlways;
            st.depthStencilPassOperation = MTLStencilOperationReplace;
            st.readMask = 0xFF;
            st.writeMask = 0xFF;
            dd.frontFaceStencil = st;
            dd.backFaceStencil = st;
        }
        id<MTLDepthStencilState> ds = [s_state.device newDepthStencilStateWithDescriptor:dd];
        if (!ds) {
            failed = 1;
            return 0;
        }
        s_clear_ds[i] = (__bridge id<MTLDepthStencilState>)CFBridgingRetain(ds);
    }
    return 1;
}

static void metal_encode_clear(id<MTLRenderCommandEncoder> enc, const MetalDrawRec* dr,
                               u32 tw, u32 th)
{
    if (!enc || !dr || !metal_clear_pso_ready())
        return;
    const int cc = (dr->clear_flags & (RSX_CLEAR_COLOR_R | RSX_CLEAR_COLOR_G |
                                       RSX_CLEAR_COLOR_B | RSX_CLEAR_COLOR_A)) != 0;
    const int cd = (dr->clear_flags & RSX_CLEAR_DEPTH) != 0;
    const int cs = (dr->clear_flags & RSX_CLEAR_STENCIL) != 0;
    float col[4] = { dr->clear_rgba[0], dr->clear_rgba[1],
                     dr->clear_rgba[2], dr->clear_rgba[3] };
    float z = cd ? dr->clear_depth : 0.0f;
    [enc setRenderPipelineState:(cc ? s_clear_pso : s_clear_pso_z)];
    [enc setDepthStencilState:s_clear_ds[(cd ? 1 : 0) | (cs ? 2 : 0)]];
    if (cs)
        [enc setStencilReferenceValue:dr->clear_stencil];
    MTLViewport vp = { 0, 0, (double)tw, (double)th, 0.0, 1.0 };
    [enc setViewport:vp];
    MTLScissorRect sc = { 0, 0, tw, th };
    [enc setScissorRect:sc];
    [enc setFragmentBytes:col length:sizeof col atIndex:0];
    [enc setVertexBytes:&z length:sizeof z atIndex:1];
    [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
}

/* NV3089 blit hook (rsx_backend.blit_scaled). Per-draw mode only: when the
 * source is a GPU surface (what the game rendered never reaches guest memory),
 * record the copy in order with the draws; the destination becomes a surface
 * too, so a later texture read of it finds the pixels (RTT). Otherwise decline
 * and let cellGcmSys copy guest memory. */
static int metal_blit_scaled(void* ud, const rsx_blit_desc* d)
{
    (void)ud;
    if (!metal_per_draw_rt() || !d || !s_state.device) return 0;
    if (!d->src_local || !d->dst_local || !d->out_w || !d->out_h) return 0;
    int k = metal_surface_find(0, d->src_offset, 0);
    if (k < 0 || !s_surfaces[k].tex) return 0;
    if (s_draw.draw_count >= METAL_MAX_DRAWS) return 0;
    u32 need_w = d->dst_pitch ? d->dst_pitch / 4u : 0u;
    if (need_w < d->out_x + d->out_w) need_w = d->out_x + d->out_w;
    u32 need_h = d->out_y + d->out_h;
    int di = metal_surface_find(0, d->dst_offset, 0);
    if (di < 0 || !s_surfaces[di].tex) di = metal_surface_get(0, d->dst_offset, need_w, need_h, 0);
    k = metal_surface_find(0, d->src_offset, 0);   /* indices may move on eviction */
    if (di < 0 || k < 0 || di == k || !s_surfaces[di].tex || !s_surfaces[k].tex) return 0;

    const double sw = (double)s_surfaces[k].tex.width, sh = (double)s_surfaces[k].tex.height;
    const double u0 = d->u0 / 16.0, v0 = d->v0 / 16.0;
    const double u1 = u0 + (double)d->out_w * (double)d->ds_dx / 1048576.0;
    const double v1 = v0 + (double)d->out_h * (double)d->dt_dy / 1048576.0;
    MetalDrawRec* dr = &s_draw.draws[s_draw.draw_count++];
    memset(dr, 0, sizeof(*dr));
    dr->prim = (MTLPrimitiveType)0xFFFFFFFFu;   /* never drawn as geometry */
    dr->has_state = 1;
    dr->color_idx = di;
    dr->depth_idx = -1;
    dr->is_blit = 1;
    dr->blit_src = k;
    dr->blit_uv[0] = (float)(u0 / sw);
    dr->blit_uv[1] = (float)(v0 / sh);
    dr->blit_uv[2] = (float)(u1 / sw);
    dr->blit_uv[3] = (float)(v1 / sh);
    dr->blit_rect[0] = d->out_x;
    dr->blit_rect[1] = d->out_y;
    dr->blit_rect[2] = d->out_w;
    dr->blit_rect[3] = d->out_h;
    static int blog = 0;
    if (blog++ < 16)
        printf("[RSX metal] blit surf[%d] off=0x%08X uv(%.3f,%.3f)-(%.3f,%.3f) -> surf[%d] "
               "off=0x%08X rect %u,%u %ux%u\n", k, d->src_offset, dr->blit_uv[0],
               dr->blit_uv[1], dr->blit_uv[2], dr->blit_uv[3], di, d->dst_offset,
               d->out_x, d->out_y, d->out_w, d->out_h);
    return 1;
}

/* Guest display buffers (cellGcmSetDisplayBuffer), kept host-side by
 * cellGcmSys.c; offset is into RSX local memory, like a local colour surface. */
typedef struct { u32 offset, pitch, width, height; } MetalGcmDisplayInfo;
extern void* cellGcmGetDisplayInfo(u32 index);
extern u32 cellGcmGetCurrentDisplayBufferId(void);

/* PS3_TRACE_FLIP (OFF). Predictions before the first run — do not rewrite after:
 * P1 wrong flip buffer: tagged frames have by_last=1, or srcDraw=0 while last_c
 *    has draws, or show offset != disp_off
 * P2 display reconfig: tagged frames coincide with [DISPLAY RECONFIG] /
 *    DISPLAY_CHANGED
 * P3 unwritten drawable: PRESENT_WITHOUT_WRITE (swapchain cleared, sample skipped)
 * P4 RT recreate: RT_RECREATED on the shown surface this present
 * P5 drawable miss: DRAWABLE_MISS (usually keeps the last frame, not a black flash)
 * SHOW_NO_DRAWS alone is not a bug: flipping the previous display buffer is legal. */
static int metal_trace_flip_on(void)
{
    static int on = -1;
    if (on < 0) {
        const char* e = getenv("PS3_TRACE_FLIP");
        on = (e && e[0] && e[0] != '0') ? 1 : 0;
    }
    return on;
}
static long metal_trace_flip_cap(void)
{
    static long cap = -1;
    if (cap < 0) {
        const char* e = getenv("PS3_TRACE_FLIP");
        char* end = NULL;
        long v = (e && *e) ? strtol(e, &end, 10) : 0;
        if (e && *e && end && *end == '\0' && v > 1 && v <= 1000000)
            cap = v;
        else
            cap = 20000;
    }
    return cap;
}

static u32 metal_feedback_trace_lim(void)
{
    static long lim = -1;
    if (lim < 0) {
        const char* e = getenv("PS3_TRACE_METAL_FEEDBACK");
        char* end = NULL;
        long v = (e && *e) ? strtol(e, &end, 10) : 0;
        lim = (e && *e && end && *end == '\0' && v > 0 && v <= 100000) ? v : 0;
    }
    return (u32)lim;
}

/* Skip every draw that samples its colour target. */
static int metal_skip_feedback_on(void)
{
    static int on = -1;
    if (on < 0) on = metal_env_on("PS3_METAL_DEBUG_SKIP_FEEDBACK");
    return on;
}

/* Blood/distortion overlay: skip only those feedback draws that also blend. */
static int metal_skip_blood_fx_on(void)
{
    static int on = -1;
    if (on < 0) on = metal_env_on("PS3_METAL_DEBUG_SKIP_BLOOD_FX");
    return on;
}

/* SceneColor → copy texture, then sample the copy (Metal-legal). Default ON
 * (2026-09-21): sampling the colour attachment being rendered is undefined on
 * Apple's tile-based GPUs -- the GoW2 health-orb smoke (FP 75102F33, target S8
 * = its own t0) came out as blocky tiles. RPCS3 also draws feedback loops from
 * a copy. One copy per run of feedback draws (664d3eb0). PS3_METAL_FEEDBACK_COPY=0
 * = old behaviour (A/B). */
static int metal_feedback_copy_on(void)
{
    static int on = -1;
    if (on < 0) on = metal_env_default("PS3_METAL_FEEDBACK_COPY", 1);
    return on;
}

static __unsafe_unretained id<MTLTexture> s_fb_copy;
static u32 s_fb_copy_w, s_fb_copy_h;
static MTLPixelFormat s_fb_copy_fmt;

static void metal_feedback_shot_request(void);   /* defined with the gameplay-shot state */
static id<MTLTexture> metal_feedback_blit_copy(id<MTLCommandBuffer> cb, id<MTLTexture> src)
{
    if (!cb || !src || !s_state.device)
        return nil;
    u32 w = (u32)src.width, h = (u32)src.height;
    MTLPixelFormat fmt = src.pixelFormat;
    if (!s_fb_copy || s_fb_copy_w < w || s_fb_copy_h < h || s_fb_copy_fmt != fmt) {
        if (s_fb_copy)
            CFRelease((__bridge CFTypeRef)s_fb_copy);
        s_fb_copy = nil;
        MTLTextureDescriptor* td =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:fmt
                                                               width:w
                                                              height:h
                                                           mipmapped:NO];
        td.usage = MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget;
        td.storageMode = MTLStorageModePrivate;
        id<MTLTexture> nt = [s_state.device newTextureWithDescriptor:td];
        if (!nt)
            return nil;
        s_fb_copy = (__bridge id<MTLTexture>)CFBridgingRetain(nt);
        s_fb_copy_w = w;
        s_fb_copy_h = h;
        s_fb_copy_fmt = fmt;
    }
    id<MTLBlitCommandEncoder> be = [cb blitCommandEncoder];
    if (!be)
        return nil;
    [be copyFromTexture:src
            sourceSlice:0
            sourceLevel:0
           sourceOrigin:MTLOriginMake(0, 0, 0)
             sourceSize:MTLSizeMake(w, h, 1)
              toTexture:s_fb_copy
       destinationSlice:0
       destinationLevel:0
      destinationOrigin:MTLOriginMake(0, 0, 0)];
    [be endEncoding];
    return s_fb_copy;
}
static int metal_perdraw_trace(void)
{
    static int on = -1;
    if (on < 0) {
        const char* e = getenv("PS3_TRACE_METAL_PERDRAW");
        on = (e && *e && *e != '0') ? 1 : 0;
    }
    return on;
}

/* PS3_METAL_DEBUG_NODEPTH=1 (OFF, diagnostic): per-draw replay with the depth
 * and stencil tests off, to rule them out when nothing reaches a surface. */
static id<MTLDepthStencilState> metal_debug_nodepth_dss(void)
{
    static __unsafe_unretained id<MTLDepthStencilState> d = nil;
    static int on = -1;
    if (on < 0) {
        const char* e = getenv("PS3_METAL_DEBUG_NODEPTH");
        on = (e && *e && *e != '0') ? 1 : 0;
    }
    if (!on) return nil;
    if (!d && s_state.device) {
        MTLDepthStencilDescriptor* dd = [MTLDepthStencilDescriptor new];
        dd.depthCompareFunction = MTLCompareFunctionAlways;
        dd.depthWriteEnabled = NO;
        id<MTLDepthStencilState> s = [s_state.device newDepthStencilStateWithDescriptor:dd];
        if (s) d = (__bridge id<MTLDepthStencilState>)CFBridgingRetain(s);
    }
    return d;
}

/* Always-pass, no-write depth state for one live A/B (F11), whatever the env. */
static id<MTLDepthStencilState> metal_debug_nodepth_any(void)
{
    static __unsafe_unretained id<MTLDepthStencilState> d = nil;
    if (!d && s_state.device) {
        MTLDepthStencilDescriptor* dd = [MTLDepthStencilDescriptor new];
        dd.depthCompareFunction = MTLCompareFunctionAlways;
        dd.depthWriteEnabled = NO;
        id<MTLDepthStencilState> st = [s_state.device newDepthStencilStateWithDescriptor:dd];
        if (st) d = (__bridge id<MTLDepthStencilState>)CFBridgingRetain(st);
    }
    return d;
}

/* Depth for a colour target the game drew without a zeta surface: the draw PSOs
 * carry a depth attachment format, so every pass needs one. One per size. */
static id<MTLTexture> metal_perdraw_fallback_depth(u32 w, u32 h)
{
    static struct { u32 w, h; __unsafe_unretained id<MTLTexture> t; } cache[8];
    for (int i = 0; i < 8; i++)
        if (cache[i].t && cache[i].w == w && cache[i].h == h) return cache[i].t;
    int slot = -1;
    for (int i = 0; i < 8; i++)
        if (!cache[i].t) { slot = i; break; }
    if (slot < 0) {
        slot = 0; /* command buffers retain what they reference */
        CFRelease((__bridge CFTypeRef)cache[0].t);
        cache[0].t = nil;
    }
    MTLTextureDescriptor* td =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float_Stencil8
                                                           width:w height:h mipmapped:NO];
    td.usage = MTLTextureUsageRenderTarget;
    td.storageMode = MTLStorageModePrivate;
    id<MTLTexture> t = [s_state.device newTextureWithDescriptor:td];
    if (!t) return nil;
    cache[slot].t = (__bridge id<MTLTexture>)CFBridgingRetain(t);
    cache[slot].w = w;
    cache[slot].h = h;
    return cache[slot].t;
}

/* PS3_METAL_PER_DRAW_RT present: replay the frame's records in order, one render
 * pass per run of draws that share a target (a clear record opens a new pass on
 * its target), each draw with its own viewport/scissor/textures/depth state;
 * then sample the surface the flip displays (cellGcmGetDisplayInfo(buffer_id))
 * onto the drawable. Known gaps: render-to-texture (a texture unit reading a
 * surface still reads guest memory), per-draw stencil ref, MRT B..D. */
/* PS3_TRACE_GREEN=2: depois do commit, espera o GPU e, se o quadro saiu verde,
 * despeja TODAS as superficies -- ainda guardam o conteudo desse quadro, porque
 * nada mais correu. Mostra que entrada da composicao tem o verde. Lento; so'
 * para uma corrida de diagnostico. */
__unsafe_unretained id<MTLBuffer> g_green_sync_buf;
u64 g_green_sync_frame;
static void metal_green_sync_check(id<MTLCommandBuffer> cb)
{
    static int mode = -1;
    if (mode < 0) { const char* e = getenv("PS3_TRACE_GREEN"); mode = (e && e[0] == '2') ? 2 : 0; }
    if (mode != 2 || !cb || !g_green_sync_buf) return;
    [cb waitUntilCompleted];
    const u8* q = (const u8*)g_green_sync_buf.contents;
    int what = 0;
    for (int k = 0; k < 5; k++) {
        u32 sr = 0, sg = 0, sb = 0;
        for (int t = 0; t < 64; t++) { sb += q[k*256+t*4]; sg += q[k*256+t*4+1]; sr += q[k*256+t*4+2]; }
        sr /= 64u; sg /= 64u; sb /= 64u;
        if (sg >= 90u && sb * 4u < sg && sr < sg) what |= 1 << k;
    }
    g_green_sync_buf = nil;
    static int ndump;
    if (!what || ndump >= 2) return;
    ndump++;
    fprintf(stderr, "[GREEN-SYNC] quadro %llu verde (blocos=0x%02X): despejando %u superficies\n",
            (unsigned long long)g_green_sync_frame, what, s_n_surfaces);
    for (u32 k = 0; k < s_n_surfaces; k++) {
        if (!s_surfaces[k].tex || s_surfaces[k].is_depth) continue;
        char path[128];
        snprintf(path, sizeof path, "gsync_f%llu_S%u_%08X_%ux%u.bmp",
                 (unsigned long long)g_green_sync_frame, k, s_surfaces[k].offset,
                 (unsigned)s_surfaces[k].tex.width, (unsigned)s_surfaces[k].tex.height);
        metal_dump_texture_to(s_surfaces[k].tex, path);
    }
    fflush(stderr);
}

/* PS3_METAL_PICK: one line per draw that covers the picked box, printed later
 * with its visibility count. Attribute ranges come from the decoded VAMap data
 * (the float4s the VS reads), over the vertices this draw references. */
static void metal_pick_describe(u32 i, const MetalDrawRec* dr, int ci, u32 tw, u32 th,
                                u32 px, u32 py)
{
    if (i >= METAL_MAX_DRAWS) return;
    enum { PB = 4096 };
    char* b = (char*)malloc(PB);
    if (!b) return;
    int n = snprintf(b, PB, "#%u S%d(%ux%u) px=(%u,%u) vp=%08X fp=%08X blend=%u/%X/%X/%X "
                     "cmask=%08X cull=%d prim=%u n=%u idx=%u vastride=%u",
                     i, ci, tw, th, px, py, dr->vp_hash, dr->fp_hash, dr->blend[0], dr->blend[1],
                     dr->blend[2], dr->blend[3], dr->cmask, dr->cull_mode, (unsigned)dr->prim,
                     dr->vertex_count, dr->is_indexed, dr->va_stride);
    for (int u = 0; u < 4 && n < PB - 96; u++) {
        id<MTLTexture> t = dr->tex[u];
        if (!t) continue;
        n += snprintf(b + n, (size_t)(PB - n), " t%d=0x%08X/%lux%lu/pf%lu", u, dr->tex_off[u],
                      (unsigned long)t.width, (unsigned long)t.height, (unsigned long)t.pixelFormat);
    }
    if (n < PB - 96) n += snprintf(b + n, (size_t)(PB - n), " tex0fmt=%08X addr0=%08X filt0=%08X nsamp=%u",
                                   dr->tex0_reg[0], dr->tex_addr[0], dr->tex_filt[0], s_samp_count);
    if (n < PB - 160)   /* depth/stencil and face state of this draw */
        n += snprintf(b + n, (size_t)(PB - n), " z=S%d dt=%u dw=%u df=%X dmode=%u st=%u ccw=%d clamp=%d po=%d",
                      dr->depth_idx, dr->ds_desc.depth_test, dr->ds_desc.depth_write_mask,
                      dr->ds_desc.depth_func, dr->ds_desc.dmode, dr->ds_desc.stencil_en,
                      dr->winding_ccw, dr->depth_clamp, dr->poly_offset);
    for (int k = 0; k < 16 && n < PB - 200; k++) {
        const u32 f = s_pick_fmt[i][k];
        const int sl = dr->va_slot[k];
        if (!f && sl < 0) continue;
        n += snprintf(b + n, (size_t)(PB - n), " | v%d t%u s%u st%u", k, f & 0xFu, (f >> 4) & 0xFu,
                      (f >> 8) & 0xFFu);
        if (sl < 0 || !dr->va_stride || !s_draw.va_mapped) {
            n += snprintf(b + n, (size_t)(PB - n), " cur(%g,%g,%g,%g)", dr->va_cur[k][0],
                          dr->va_cur[k][1], dr->va_cur[k][2], dr->va_cur[k][3]);
            continue;
        }
        float lo[4] = { INFINITY, INFINITY, INFINITY, INFINITY };
        float hi[4] = { -INFINITY, -INFINITY, -INFINITY, -INFINITY };
        u32 nan = 0;
        const u32 cnt = dr->vertex_count < 4096u ? dr->vertex_count : 4096u;
        for (u32 j = 0; j < cnt; j++) {
            u32 v = j;
            if (dr->is_indexed) {
                if (!s_draw.ib_mapped) break;
                v = s_draw.ib_mapped[dr->ib_byte_offset / 4u + j];
                if (v > 0x100000u) continue;
            }
            const float* p = s_draw.va_mapped +
                ((size_t)dr->va_base + (size_t)v * dr->va_stride + (u32)sl) * 4u;
            for (int c = 0; c < 4; c++) {
                if (!isfinite(p[c])) { nan++; continue; }
                if (p[c] < lo[c]) lo[c] = p[c];
                if (p[c] > hi[c]) hi[c] = p[c];
            }
        }
        n += snprintf(b + n, (size_t)(PB - n), " [%.3g..%.3g %.3g..%.3g %.3g..%.3g %.3g..%.3g]%s",
                      lo[0], hi[0], lo[1], hi[1], lo[2], hi[2], lo[3], hi[3], nan ? " NaN" : "");
    }
    /* Pre-skin geometry check (v0 as decoded, before the VP): triangles whose
     * longest edge is > 8x the median. Spikes HERE = the vertex data in guest
     * memory is already wrong (SPU/DMA writer); none here but spikes on screen =
     * the VP (bones/constants). Indexed triangle lists only. */
    if (n < PB - 300 && dr->is_indexed && dr->prim == MTLPrimitiveTypeTriangle &&
        dr->va_stride && dr->va_slot[0] >= 0 && s_draw.va_mapped && s_draw.ib_mapped) {
        const u32 nt = (dr->vertex_count < 12288u ? dr->vertex_count : 12288u) / 3u;
        static float el[4096];
        const u32* ib = s_draw.ib_mapped + dr->ib_byte_offset / 4u;
        const int s0 = dr->va_slot[0], s7 = dr->va_slot[7];
        u32 m = 0, worst = 0;
        float wmax = 0.0f;
        for (u32 t = 0; t < nt; t++) {
            const float* q[3];
            int ok = 1;
            for (int c = 0; c < 3; c++) {
                const u32 v = ib[t * 3u + (u32)c];
                if (v > 0x100000u) { ok = 0; break; }
                q[c] = s_draw.va_mapped + ((size_t)dr->va_base + (size_t)v * dr->va_stride + (u32)s0) * 4u;
            }
            if (!ok) continue;
            float e = 0.0f;
            for (int c = 0; c < 3; c++) {
                const float* a = q[c]; const float* bb = q[(c + 1) % 3];
                const float dx = a[0] - bb[0], dy = a[1] - bb[1], dz = a[2] - bb[2];
                const float d = sqrtf(dx * dx + dy * dy + dz * dz);
                if (d > e) e = d;
            }
            if (m < 4096u) el[m++] = e;
            if (e > wmax) { wmax = e; worst = t; }
        }
        if (m) {
            float med = 0.0f;   /* median by partial selection over the sample */
            {
                float tmp[4096];
                memcpy(tmp, el, m * sizeof(float));
                const u32 k = m / 2u;
                for (u32 a = 0; a <= k; a++) {
                    u32 mi = a;
                    for (u32 c = a + 1u; c < m; c++) if (tmp[c] < tmp[mi]) mi = c;
                    const float sw = tmp[a]; tmp[a] = tmp[mi]; tmp[mi] = sw;
                }
                med = tmp[k];
            }
            u32 spikes = 0;
            for (u32 t = 0; t < m; t++) if (med > 0.0f && el[t] > 8.0f * med) spikes++;
            n += snprintf(b + n, (size_t)(PB - n), " || preskin tri=%u med=%.3g max=%.3g spikes(>8x)=%u",
                          m, med, wmax, spikes);
            for (int c = 0; c < 3 && n < PB - 120; c++) {
                const u32 v = ib[worst * 3u + (u32)c];
                const float* p0 = s_draw.va_mapped + ((size_t)dr->va_base + (size_t)v * dr->va_stride + (u32)s0) * 4u;
                n += snprintf(b + n, (size_t)(PB - n), " w%d:v%u(%.4g,%.4g,%.4g,%.3g)", c, v,
                              p0[0], p0[1], p0[2], p0[3]);
                if (s7 >= 0) {
                    const float* p7 = s_draw.va_mapped + ((size_t)dr->va_base + (size_t)v * dr->va_stride + (u32)s7) * 4u;
                    n += snprintf(b + n, (size_t)(PB - n), "b(%g,%g)", p7[0], p7[1]);
                }
            }
        }
    }
    if (n < PB - 80)
        n += snprintf(b + n, (size_t)(PB - n), " pos_ea=0x%08X gstride=%u", dr->pos_ea, dr->pos_gstride);
    if (s_draw.vc_mapped && n < PB - 700) {   /* bone palette + material constants of this draw */
        const float (*vc)[4] = (const float (*)[4])(s_draw.vc_mapped + dr->vc_byte_offset);
        u32 h = 2166136261u;
        const u8* pb = (const u8*)&vc[256][0];
        for (u32 k = 0; k < 16u * 64u; k++) h = (h ^ pb[k]) * 16777619u;   /* c256..c319 */
        n += snprintf(b + n, (size_t)(PB - n), " vc256_319=%08X", h);
        for (int c = 456; c <= 467 && n < PB - 64; c++)
            n += snprintf(b + n, (size_t)(PB - n), " c%d=(%g,%g,%g,%g)", c, vc[c][0], vc[c][1], vc[c][2], vc[c][3]);
        for (int c = 260; c <= 267 && n < PB - 64; c++)
            n += snprintf(b + n, (size_t)(PB - n), " c%d=(%.4g,%.4g,%.4g,%.4g)", c, vc[c][0], vc[c][1], vc[c][2], vc[c][3]);
    }
    for (int u = 0; u < 4; u++) {
        id<MTLTexture> t = dr->tex[u];
        if (!t || t.width > 2048u || t.height > 2048u) continue;
        s_pick_tex[i][u] = (__bridge id<MTLTexture>)CFBridgingRetain(t);
    }
    free(s_pick_info[i]);
    s_pick_info[i] = b;
    s_pick_n++;
}

static void metal_present_per_draw(id<MTLCommandBuffer> cb, id<MTLTexture> dtex,
                                   MetalRect lb, u32 buffer_id)
{
    int pick = (s_pick_phase == 2);
    float pick_u = -1.0f, pick_v = -1.0f;
    {
        if (s_audit_fp == 0xFFFFFFFFu) {
            const char* e = getenv("PS3_BRONZE_AUDIT");
            s_audit_fp = (e && *e) ? (u32)strtoul(e, NULL, 16) : 0u;
        }
        s_audit_on = s_audit_fp && !pick && (s_state.frame_count % 300ull) == 0ull && s_state.frame_count > 0;
        if (s_audit_on) {
            if (!s_pick_vis)
                s_pick_vis = [s_state.device newBufferWithLength:METAL_MAX_DRAWS * 8u
                                                         options:MTLResourceStorageModeShared];
            if (!s_audit_dss[0]) {
                const MTLCompareFunction f[4] = { MTLCompareFunctionAlways, MTLCompareFunctionLessEqual,
                                                  MTLCompareFunctionEqual, MTLCompareFunctionLessEqual };
                for (int k = 0; k < 4; k++) {
                    MTLDepthStencilDescriptor* dd = [MTLDepthStencilDescriptor new];
                    dd.depthCompareFunction = f[k];
                    dd.depthWriteEnabled = NO;
                    s_audit_dss[k] = [s_state.device newDepthStencilStateWithDescriptor:dd];
                }
            }
            memset(s_pick_vis.contents, 0, METAL_MAX_DRAWS * 8u);
            s_audit_n = 0;
        }
    }
    if (pick && s_draw.draw_count == 0) {   /* repeated flip, nothing drawn: try the next one */
        s_pick_phase = 1;
        pick = 0;
    }
    if (pick) {
        s_pick_phase = 0;
        if (s_pick_norm) {
            pick_u = s_pick_mx;
            pick_v = s_pick_my;
        } else if (lb.w && lb.h) {
            pick_u = (s_pick_mx - (float)lb.x) / (float)lb.w;
            pick_v = (s_pick_my - (float)lb.y) / (float)lb.h;
        }
        if (!s_pick_vis)
            s_pick_vis = [s_state.device newBufferWithLength:METAL_MAX_DRAWS * 8u
                                                     options:MTLResourceStorageModeShared];
        if (!s_pick_vis || pick_u < 0.0f || pick_u >= 1.0f || pick_v < 0.0f || pick_v >= 1.0f) {
            fprintf(stderr, "[PICK] mouse fora da imagem (u=%.3f v=%.3f): solte o rato com Escape "
                    "e aponte para o jogo\n", pick_u, pick_v);
            pick = 0;
        } else {
            memset(s_pick_vis.contents, 0, METAL_MAX_DRAWS * 8u);
            memset(s_pick_reached, 0, sizeof s_pick_reached);
            s_pick_rb_n = 0;
            for (u32 k = 0; k < METAL_MAX_DRAWS; k++) {
                free(s_pick_info[k]); s_pick_info[k] = NULL;
                for (int u = 0; u < 4; u++)
                    if (s_pick_tex[k][u]) { CFRelease((__bridge CFTypeRef)s_pick_tex[k][u]); s_pick_tex[k][u] = nil; }
            }
            s_pick_n = 0;
            fprintf(stderr, "[PICK] quadro de pick f=%llu u=%.4f v=%.4f draws=%u\n",
                    (unsigned long long)s_state.frame_count, pick_u, pick_v, s_draw.draw_count);
        }
    }
    id<MTLRenderCommandEncoder> enc = nil;
    int cur_c = -2, cur_d = -2, last_c = -1;
    u32 tw = 0, th = 0;
    /* MRT: assinatura dos alvos extra do passe aberto e quantos attachments ele
     * tem de facto (um alvo extra em falta encolhe o passe). */
    u64 cur_mrt = 0;
    int cur_pass_mrt = 1;
    u32 n_pass = 0, n_draw = 0, n_skip = 0, n_clear = 0, n_blit = 0;
    int fb_have_copy = 0, fb_copy_target = -1;   /* snapshot valid for the current feedback run */
    /* Trace only: target and draw count of each pass, and the first draw of
     * the last pass (what the final composite samples). */
    int pass_tgt[16];
    u32 pass_draws[16];
    int last_pass_first = -1;
    int pl_first[64], pl_tgt[64];
    u32 pl_draws[64], pl_n = 0;
    (void)metal_ensure_dummy_tex();
    (void)metal_ensure_vp_const_buf();

    /* Main color: GCM flip buffer if this frame 3D-drew into it, else the
     * last 3D color surface (what the present samples). One clear only. */
    int main_c = -1;
    int forced_clear = 0;
    if (rsx_debug_clear_frame_on()) {
        int disp = -1, last_3d = -1, hits_disp = 0;
        const MetalGcmDisplayInfo* gdi0 =
            (const MetalGcmDisplayInfo*)cellGcmGetDisplayInfo(buffer_id);
        if (gdi0)
            disp = metal_surface_find(0, gdi0->offset, 0);
        for (u32 i = 0; i < s_draw.draw_count && i < METAL_MAX_DRAWS; i++) {
            const MetalDrawRec* dr = &s_draw.draws[i];
            int ci = dr->color_idx;
            if (!dr->has_state || dr->is_blit || dr->clear_flags)
                continue;
            if (ci < 0 || (u32)ci >= s_n_surfaces || !s_surfaces[ci].tex ||
                s_surfaces[ci].is_depth)
                continue;
            last_3d = ci;
            if (disp >= 0 && ci == disp)
                hits_disp = 1;
        }
        main_c = hits_disp ? disp : last_3d;
    }

    for (u32 i = 0; i < s_draw.draw_count && i < METAL_MAX_DRAWS; i++) {
        MetalDrawRec* dr = &s_draw.draws[i];
        if (!dr->has_state) { n_skip++; continue; }
        int ci = dr->color_idx;
        if (ci < 0 || (u32)ci >= s_n_surfaces || !s_surfaces[ci].tex ||
            s_surfaces[ci].is_depth) { n_skip++; continue; }
        id<MTLTexture> ctex = s_surfaces[ci].tex;
        if (dr->is_blit) {
            /* A blit is its own pass (no depth, blit PSO); the next record
             * opens a fresh one. */
            if (enc) { [enc endEncoding]; enc = nil; }
            cur_c = -2;
            cur_d = -2;
            int si = dr->blit_src;
            if (si < 0 || (u32)si >= s_n_surfaces || !s_surfaces[si].tex || si == ci ||
                !metal_blit_pso_ready()) { n_skip++; continue; }
            MTLRenderPassDescriptor* bp = [MTLRenderPassDescriptor renderPassDescriptor];
            bp.colorAttachments[0].texture     = ctex;
            bp.colorAttachments[0].loadAction  =
                s_surfaces[ci].inited ? MTLLoadActionLoad : MTLLoadActionClear;
            bp.colorAttachments[0].clearColor  = MTLClearColorMake(0, 0, 0, 1);
            bp.colorAttachments[0].storeAction = MTLStoreActionStore;
            s_surfaces[ci].inited = 1;
            id<MTLRenderCommandEncoder> be = [cb renderCommandEncoderWithDescriptor:bp];
            if (be) {
                u32 bw = (u32)ctex.width, bh = (u32)ctex.height;
                u32 rx = dr->blit_rect[0] < bw ? dr->blit_rect[0] : 0u;
                u32 ry = dr->blit_rect[1] < bh ? dr->blit_rect[1] : 0u;
                u32 rw = dr->blit_rect[2], rh = dr->blit_rect[3];
                if (rx + rw > bw) rw = bw - rx;
                if (ry + rh > bh) rh = bh - ry;
                MTLViewport bvp = { (double)rx, (double)ry, (double)rw, (double)rh, 0.0, 1.0 };
                [be setViewport:bvp];
                [be setRenderPipelineState:s_blit_pso];
                [be setVertexBytes:dr->blit_uv length:sizeof(dr->blit_uv) atIndex:0];
                [be setFragmentTexture:s_surfaces[si].tex atIndex:0];
                [be setFragmentSamplerState:s_blit_samp atIndex:0];
                [be drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
                [be endEncoding];
            }
            last_c = ci;
            n_blit++;
            continue;
        }
        int di = dr->depth_idx;
        id<MTLTexture> ztex = nil;
        if (di >= 0 && (u32)di < s_n_surfaces && s_surfaces[di].tex &&
            s_surfaces[di].is_depth &&
            s_surfaces[di].tex.width == ctex.width &&
            s_surfaces[di].tex.height == ctex.height)
            ztex = s_surfaces[di].tex;
        else
            di = -1;
        const int is_clear = dr->clear_flags != 0;
        int samples_self = 0;
        if (!is_clear) {
            for (int ti = 0; ti < (int)RSX_MAX_TEXTURES; ti++)
                if (dr->tex[ti] && dr->tex[ti] == ctex) {
                    samples_self = 1;
                    break;
                }
        }
        /* One snapshot per run of consecutive feedback draws, not one per draw: the copy
         * is a full-surface blit plus a pass break, and GoW2 issues the overlay as dozens
         * of quads in a row (measured 2026-09-17: per-draw copies cost 18-21 -> 13-15 fps).
         * Any ordinary draw on the way invalidates it, because it changed the surface. */
        if (samples_self && metal_feedback_copy_on() &&
            (!fb_have_copy || fb_copy_target != ci)) {
            if (enc) {
                [enc endEncoding];
                enc = nil;
            }
            cur_c = -2;
            cur_d = -2;
            if (metal_feedback_blit_copy(cb, ctex)) {
                fb_have_copy = 1;
                fb_copy_target = ci;
            }
        }
        if (samples_self) metal_feedback_shot_request();

        const int dmrt = (dr->mrt_n > 1) ? dr->mrt_n : 1;
        u64 msig = 0;
        for (int k = 1; k < dmrt; k++)
            msig = (msig << 16) ^ (u64)(u32)(dr->mrt_idx[k] + 1) ^ ((u64)k << 60);
        const int merge_clear =
            is_clear && enc && ci == cur_c && di == cur_d && msig == cur_mrt && cur_pass_mrt == 1 &&
            metal_env_default("PS3_METAL_PASS_MERGE", 1) && metal_clear_pso_ready();
        if (merge_clear) {
            metal_encode_clear(enc, dr, tw, th);
            n_clear++;
            continue;
        }

        if (is_clear || ci != cur_c || di != cur_d || msig != cur_mrt || !enc) {
            if (enc) { [enc endEncoding]; enc = nil; }
            if (metal_depth_as_color_on()) {
                /* Depth surfaces this pass samples as colour: convert them now, with
                 * the content earlier passes left (encoders run in cb order). */
                for (u32 j = i; j < s_draw.draw_count && j < METAL_MAX_DRAWS; j++) {
                    const MetalDrawRec* q = &s_draw.draws[j];
                    if (!q->has_state || q->is_blit) break;
                    if (j > i && (q->clear_flags || q->color_idx != dr->color_idx ||
                                  q->depth_idx != dr->depth_idx)) break;
                    for (int tj = 0; tj < (int)RSX_MAX_TEXTURES; tj++) {
                        id<MTLTexture> t = q->tex[tj];
                        if (!t || t.pixelFormat != MTLPixelFormatDepth32Float_Stencil8) continue;
                        for (u32 k = 0; k < s_n_surfaces; k++)
                            if (s_surfaces[k].tex == t && s_surfaces[k].is_depth) {
                                metal_depth_color_convert(cb, (int)k);
                                break;
                            }
                    }
                }
            }
            MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
            pass.colorAttachments[0].texture = ctex;
            int cc = is_clear && (dr->clear_flags & (RSX_CLEAR_COLOR_R | RSX_CLEAR_COLOR_G |
                                                     RSX_CLEAR_COLOR_B | RSX_CLEAR_COLOR_A));
            /* Synthetic clears (first use of a surface, PS3_METAL_DEBUG_CLEAR_FRAME)
             * use alpha 0, the RSX clear-value reset. GoW2 draws its scene with
             * colour mask RGB only (0x00010101) and its bloom bright-pass
             * (FP 654864DA) scales by the scene's alpha: a synthetic alpha of 1
             * turned the whole 304x171 bloom chain white and the composite black
             * with blotches (2026-09-16 PASS log). PS3_METAL_CLEAR_ALPHA1=1 = old. */
            static int ca1 = -1;
            if (ca1 < 0) ca1 = metal_env_on("PS3_METAL_CLEAR_ALPHA1");
            const double synth_a = ca1 ? 1.0 : 0.0;
            MTLClearColor ccol = cc ? MTLClearColorMake(dr->clear_rgba[0], dr->clear_rgba[1],
                                                        dr->clear_rgba[2], dr->clear_rgba[3])
                                    : MTLClearColorMake(0, 0, 0, synth_a);
            if (!s_surfaces[ci].inited) cc = 1;
            s_surfaces[ci].inited = 1;
            if (rsx_debug_clear_frame_on() &&
                rsx_debug_clear_frame_take(ci, main_c, &forced_clear)) {
                cc = 1;
                ccol = MTLClearColorMake(0, 0, 0, synth_a);
                static u32 ncf;
                if (ncf++ < 8 || (s_state.frame_count % 60ull) == 0ull) {
                    fprintf(stderr,
                            "[CLEAR-FRAME] f=%llu surf=%d off=0x%08X "
                            "(main=%d black, 3D pass)\n",
                            (unsigned long long)s_state.frame_count, ci,
                            s_surfaces[ci].offset, main_c);
                    fflush(stderr);
                }
            }
            pass.colorAttachments[0].loadAction  = cc ? MTLLoadActionClear : MTLLoadActionLoad;
            pass.colorAttachments[0].storeAction = MTLStoreActionStore;
            pass.colorAttachments[0].clearColor  = ccol;
            /* MRT: os alvos extra entram como attachments 1..n-1 com o mesmo tamanho;
             * um clear do RSX limpa todos os alvos ligados. */
            int pass_mrt = 1;
            for (int k = 1; k < dmrt; k++) {
                const int ek = dr->mrt_idx[k];
                if (ek < 0 || (u32)ek >= s_n_surfaces || !s_surfaces[ek].tex ||
                    s_surfaces[ek].tex.width != ctex.width || s_surfaces[ek].tex.height != ctex.height)
                    break;
                const int ecc = (is_clear && cc) || !s_surfaces[ek].inited;
                s_surfaces[ek].inited = 1;
                pass.colorAttachments[k].texture     = s_surfaces[ek].tex;
                pass.colorAttachments[k].loadAction  = ecc ? MTLLoadActionClear : MTLLoadActionLoad;
                pass.colorAttachments[k].storeAction = MTLStoreActionStore;
                pass.colorAttachments[k].clearColor  = ccol;
                s_surfaces[ek].wserial++;
                pass_mrt = k + 1;
            }

            int cd = is_clear && (dr->clear_flags & RSX_CLEAR_DEPTH);
            int cs = is_clear && (dr->clear_flags & RSX_CLEAR_STENCIL);
            if (ztex) {
                if (!s_surfaces[di].inited) cd = cs = 1;
                s_surfaces[di].inited = 1;
            } else {
                ztex = metal_perdraw_fallback_depth((u32)ctex.width, (u32)ctex.height);
                cd = cs = 1;
            }
            if (ztex) {
                pass.depthAttachment.texture       = ztex;
                pass.depthAttachment.loadAction    = cd ? MTLLoadActionClear : MTLLoadActionLoad;
                pass.depthAttachment.storeAction   = MTLStoreActionStore;
                pass.depthAttachment.clearDepth    = cd && is_clear ? dr->clear_depth : 1.0;
                pass.stencilAttachment.texture     = ztex;
                pass.stencilAttachment.loadAction  = cs ? MTLLoadActionClear : MTLLoadActionLoad;
                pass.stencilAttachment.storeAction = MTLStoreActionStore;
                pass.stencilAttachment.clearStencil = cs && is_clear ? dr->clear_stencil : 0;
            }
            {   /* PASS log frames: how each pass starts its depth/stencil (the stencil
                 * light volumes need S7 cleared or rebuilt each frame, 2026-09-16). */
                static int pt = -1;
                if (pt < 0) pt = metal_env_on("PS3_TRACE_METAL_PASSES");
                const long long fa = s_surf_dump_at;
                const unsigned long long fcn = s_state.frame_count;
                if (pt && fa >= 0 && (fcn == (unsigned long long)fa || fcn == (unsigned long long)fa + 1ull))
                    fprintf(stderr, "[PASS] start rec=%u c=S%d z=S%d clear=%d flags=0x%X clr_rgba=(%g,%g,%g,%g) zload=%s sload=%s zclr=%g sclr=%u\n",
                            i, ci, di, is_clear, dr->clear_flags,
                            dr->clear_rgba[0], dr->clear_rgba[1], dr->clear_rgba[2], dr->clear_rgba[3],
                            ztex ? (cd ? "CLEAR" : "LOAD") : "-", ztex ? (cs ? "CLEAR" : "LOAD") : "-",
                            cd && is_clear ? dr->clear_depth : 1.0, cs && is_clear ? dr->clear_stencil : 0u);
            }
            if (pick || s_audit_on) pass.visibilityResultBuffer = s_pick_vis;
            enc = [cb renderCommandEncoderWithDescriptor:pass];
            if (!enc) break;
            metal_set_encoder_blend_color(enc);
            if (di >= 0) s_surfaces[di].wserial++;
            cur_c = ci;
            cur_d = di;
            cur_mrt = msig;
            cur_pass_mrt = pass_mrt;
            tw = (u32)ctex.width;
            th = (u32)ctex.height;
            if (n_pass < 16u) { pass_tgt[n_pass] = ci; pass_draws[n_pass] = 0; }
            if (pl_n < 64u) { pl_tgt[pl_n] = ci; pl_draws[pl_n] = 0; pl_first[pl_n] = -1; pl_n++; }
            last_pass_first = -1;
            n_pass++;
            if (s_draw.vp_const_buf)
                [enc setVertexBuffer:s_draw.vp_const_buf offset:0 atIndex:1];
            if (s_draw.vb)
                [enc setVertexBuffer:s_draw.vb offset:0 atIndex:0];
        }
        last_c = ci;
        if (is_clear) { n_clear++; continue; }
        if (dr->prim == (MTLPrimitiveType)0xFFFFFFFFu) { n_skip++; continue; }
        if (metal_debug_skip_draw(dr->fp_hash, dr->vp_hash)) { n_skip++; continue; }
        if (s_live_skip_vp && dr->vp_hash == s_live_skip_vp) { n_skip++; continue; }
        if (samples_self &&
            (metal_skip_feedback_on() ||
             (metal_skip_blood_fx_on() && dr->blend[0]))) {
            n_skip++;
            continue;
        }
        id<MTLRenderPipelineState> use = dr->pso ? dr->pso : s_draw.pso;
        /* O PSO tem de ter os mesmos attachments do passe (senao o Metal rejeita o
         * draw): um draw MRT com o shader por compilar (PSO fixo, 1 attachment)
         * nao tem como escrever nos alvos extra -- salta-se, como o RPCS3 nao
         * desenha sem o shader. */
        if ((use == s_draw.pso ? 1 : (dr->pso_mrt_n ? dr->pso_mrt_n : 1)) != cur_pass_mrt) {
            static u32 s_mrt_skip;
            if (s_mrt_skip++ < 4)
                fprintf(stderr, "[MRT] draw %u saltado: PSO com %d alvos, passe com %d\n",
                        i, dr->pso_mrt_n, cur_pass_mrt);
            n_skip++;
            continue;
        }
        if (!dr->pso) s_fallback_pso_draws++;
        {   /* Colour write masks used in this frame: GoW2 draws the scene RGB-only on the
             * console (alpha stays what the clear left, and the bloom bright-pass scales by
             * it). Ours measured alpha = 1 over the whole scene target, which floods the
             * bloom -- so first check WHICH masks the title actually asks for. */
            u32 k = 0;
            while (k < s_cmask_n && s_cmask_seen[k] != dr->cmask) k++;
            if (k == s_cmask_n && s_cmask_n < 8u) {
                s_cmask_seen[s_cmask_n] = dr->cmask;
                s_cmask_cnt[s_cmask_n] = 0;
                s_cmask_n++;
            }
            if (k < 8u) s_cmask_cnt[k]++;
        }
        {   /* PS3_TRACE_METAL_SKIN=<vp hash> (OFF): for the draws of one vertex program,
             * the decoded attribute map. A skinning VP reads the bone index from
             * attribute 7 and the weights from 0.w/2: if those slots are -1 the shader
             * falls back to the VERTEX_DATA4F register and every vertex ends up on the
             * same bone -- the mesh deforms exactly while the animation moves. */
            static long want = -2; static u32 nlog = 0;
            if (want == -2) {
                const char* e = getenv("PS3_TRACE_METAL_SKIN");
                char* end = NULL;
                long v = (e && *e) ? ((e[0] == 'b') ? -3 : strtol(e, &end, 16)) : -1;
                want = (e && *e && (e[0] == 'b' || (end && *end == '\0'))) ? v : -1;
            }
            const int skin_bad = (dr->va_stride == 0u || dr->va_slot[7] < 0 || dr->va_slot[2] < 0);
            if (want == -3 && skin_bad && nlog < 24u) {   /* -3 = only the broken ones, any VP */
                nlog++;
                fprintf(stderr, "[SKIN-BAD] f=%llu rec=%u vp=%08X fp=%08X stride=%u v0=%d v2=%d v7=%d\n",
                        (unsigned long long)s_state.frame_count, i, dr->vp_hash, dr->fp_hash,
                        dr->va_stride, (int)dr->va_slot[0], (int)dr->va_slot[2], (int)dr->va_slot[7]);
                fflush(stderr);
            }
            if (want >= 0 && (u32)want == dr->vp_hash && nlog < 24u) {
                nlog++;
                fprintf(stderr, "[SKIN] f=%llu rec=%u vp=%08X fp=%08X stride=%u slots:"
                        " v0=%d v1=%d v2=%d v3=%d v7=%d v8=%d cur7=(%g,%g,%g,%g)\n",
                        (unsigned long long)s_state.frame_count, i, dr->vp_hash, dr->fp_hash,
                        dr->va_stride, (int)dr->va_slot[0], (int)dr->va_slot[1],
                        (int)dr->va_slot[2], (int)dr->va_slot[3], (int)dr->va_slot[7],
                        (int)dr->va_slot[8], dr->va_cur[7][0], dr->va_cur[7][1],
                        dr->va_cur[7][2], dr->va_cur[7][3]);
                fflush(stderr);
            }
        }
        if (!use || !s_draw.vb) { n_skip++; continue; }

        /* RSX maps clip to window with VIEWPORT_SCALE/OFFSET, not the viewport
         * rect (RPCS3 fill_scale_offset_data): window = ndc * scale + offset.
         * cellGcmSetViewport puts +0.5 in the offset, so pixel i samples UV
         * i/w -- GoW2's one-tap bloom blur (671F7750) alternates offsets 0 and
         * +1 texel around that and, with the rect alone, never blurred and
         * drifted a texel up-left per pass (ghost of the bloom, 2026-09-16).
         * Top-left origin (scale.y < 0) only; PS3_METAL_VIEWPORT_SO=0 = rect.
         * Body moved to rsx_core_rules.c (rsx_core_viewport). */
        double vpd[4];
        {   static int so_off = -1;
            if (so_off < 0) { const char* e = getenv("PS3_METAL_VIEWPORT_SO"); so_off = (e && e[0] == '0') ? 1 : 0; }
            rsx_core_viewport(dr->has_vp, dr->vp, tw, th, dr->vso, dr->vso_seen, !so_off, vpd); }
        MTLViewport vp = (MTLViewport){ vpd[0], vpd[1], vpd[2], vpd[3], 0.0, 1.0 };
        [enc setViewport:vp];
        /* Body moved to rsx_core_rules.c (rsx_core_scissor_per_draw). sx/sy/sw/sh
         * stay named locals: the PICK and PS3_RSX_SPLIT paths below reuse the
         * clamped scissor rect. */
        u32 scr[4];
        const int sres = rsx_core_scissor_per_draw(dr->sc, tw, th, metal_scissor_fix(), scr);
        if (sres == 1) { s_scissor_reject++; n_skip++; continue; }
        if (sres == 2) { n_skip++; continue; }
        const u32 sx = scr[0], sy = scr[1], sw = scr[2], sh = scr[3];
        [enc setScissorRect:(MTLScissorRect){ sx, sy, sw, sh }];
        id<MTLDepthStencilState> ndd = metal_debug_nodepth_dss();
        if (!ndd && s_live_zalways && dr->fp_hash == 0xF949090Cu) ndd = metal_debug_nodepth_any();
        if (ndd) {
            [enc setDepthStencilState:ndd];
        } else if (dr->dss) {
            [enc setDepthStencilState:dr->dss];
            if (dr->stencil_en)
                [enc setStencilFrontReferenceValue:dr->stencil_ref_front
                                backReferenceValue:dr->stencil_ref_back];
        }
        /* dest colour texture == a bound fragment texture. */
        if (samples_self) {
            s_feedback_draws++;
            if (s_feedback_log_n < metal_feedback_trace_lim()) {
                s_feedback_log_n++;
                u32 dst_off = s_surfaces[ci].offset;
                int hz = 0;
                for (int u = 0; u < 4; u++)
                    if (rsx_rt_is_feedback(dst_off, dr->tex_off[u]))
                        hz = 1;
                fprintf(stderr, "[FEEDBACK] f=%llu rec=%u target=S%d dest_off=0x%08X %ux%u "
                        "fp=%08X vp=%08X blend=%u sf=0x%X df=0x%X eq=0x%X cmask=0x%X "
                        "vp_rect=(%g,%g,%g,%g) scissor=(%u,%u,%u,%u) "
                        "tex_off=0x%08X,0x%08X,0x%08X,0x%08X%s\n",
                        (unsigned long long)s_state.frame_count, i, ci, dst_off,
                        (unsigned)ctex.width, (unsigned)ctex.height, dr->fp_hash, dr->vp_hash,
                        dr->blend[0], dr->blend[1], dr->blend[2], dr->blend[3], dr->cmask,
                        dr->has_vp ? dr->vp[0] : 0.0f, dr->has_vp ? dr->vp[1] : 0.0f,
                        dr->has_vp ? dr->vp[2] : 0.0f, dr->has_vp ? dr->vp[3] : 0.0f,
                        dr->sc[0], dr->sc[1], dr->sc[2], dr->sc[3],
                        dr->tex_off[0], dr->tex_off[1], dr->tex_off[2], dr->tex_off[3],
                        hz ? " HAZARD" : "");
                fflush(stderr);
            }
        }
        for (int ti = 0; ti < (int)RSX_MAX_TEXTURES; ti++) {
            id<MTLTexture> t = dr->tex[ti] ? dr->tex[ti] : s_draw.dummy_tex;
            if (t && t == ctex && metal_feedback_copy_on() && s_fb_copy)
                t = s_fb_copy;
            id<MTLSamplerState> smp = s_draw.dummy_samp;
            if (metal_sampler_real() && dr->tex[ti]) {
                id<MTLSamplerState> rs = metal_sampler_for(dr->tex_addr[ti], dr->tex_filt[ti]);
                if (rs) smp = rs;
            }
            if (t && t.pixelFormat == MTLPixelFormatDepth32Float_Stencil8) {
                /* Never bind the depth texture itself (it may be this pass's attachment). */
                id<MTLTexture> cv = nil;
                for (u32 k = 0; k < s_n_surfaces; k++)
                    if (s_surfaces[k].tex == t) { cv = s_surfaces[k].conv; break; }
                t = cv ? cv : s_draw.dummy_tex;
                if (metal_nearest_samp()) smp = metal_nearest_samp();
            }
            if (t) [enc setFragmentTexture:t atIndex:ti];
            if (smp) [enc setFragmentSamplerState:smp atIndex:ti];
        }
        metal_apply_cull(enc, dr);
        {   /* PS3_METAL_DEPTH_CLAMP=0 ignores SET_ZMIN_MAX_CONTROL (A/B). */
            static int dc_off = -1;
            if (dc_off < 0) { const char* e = getenv("PS3_METAL_DEPTH_CLAMP"); dc_off = (e && e[0] == '0') ? 1 : 0; }
            [enc setDepthClipMode:(!dc_off && dr->depth_clamp) ? MTLDepthClipModeClamp : MTLDepthClipModeClip];
            /* RPCS3 VKDraw: vkCmdSetDepthBias(bias, 0, scale) on float depth when
             * POLY_OFFSET_FILL is on. PS3_METAL_POLY_OFFSET=0 ignores it (A/B). */
            static int po_off = -1;
            if (po_off < 0) { const char* e = getenv("PS3_METAL_POLY_OFFSET"); po_off = (e && e[0] == '0') ? 1 : 0; }
            static float zb = -1.0f;
            if (zb < 0.0f) { const char* e = getenv("PS3_BRONZE_BIAS"); zb = (e && *e) ? (float)atof(e) : 4.0f; }
            if (s_live_zbias && dr->fp_hash == 0xF949090Cu)
                [enc setDepthBias:-zb slopeScale:-1.0f clamp:0.0f];
            else if (!po_off && dr->poly_offset)
                [enc setDepthBias:dr->poly_bias slopeScale:dr->poly_scale clamp:0.0f];
            else
                [enc setDepthBias:0.0f slopeScale:0.0f clamp:0.0f];
        }
        [enc setRenderPipelineState:use];
        if (s_draw.fc_buf)
            [enc setFragmentBuffer:s_draw.fc_buf offset:dr->fc_byte_offset atIndex:0];
        [enc setFragmentBytes:dr->at length:sizeof dr->at atIndex:1];
        if (s_draw.vc_buf)
            [enc setVertexBuffer:s_draw.vc_buf offset:dr->vc_byte_offset atIndex:1];
        metal_bind_va(enc, dr);
        if (pick && i < METAL_MAX_DRAWS) s_pick_reached[i] = 1;
        if (pick) {   /* PS3_METAL_PICK: only the 3x3 box, one counter per draw */
            const u32 px = (u32)(pick_u * (float)tw), py = (u32)(pick_v * (float)th);
            u32 x0 = px > 0u ? px - 1u : 0u, y0 = py > 0u ? py - 1u : 0u;
            u32 x1 = px + 2u < tw ? px + 2u : tw, y1 = py + 2u < th ? py + 2u : th;
            if (x0 < sx) x0 = sx;
            if (y0 < sy) y0 = sy;
            if (x1 > sx + sw) x1 = sx + sw;
            if (y1 > sy + sh) y1 = sy + sh;
            if (x1 <= x0 || y1 <= y0) { n_skip++; continue; }
            [enc setScissorRect:(MTLScissorRect){ x0, y0, x1 - x0, y1 - y0 }];
            [enc setVisibilityResultMode:MTLVisibilityResultModeCounting offset:(NSUInteger)i * 8u];
            metal_pick_describe(i, dr, ci, tw, th, px, py);
        }
        /* PS3_RSX_SPLIT: metade esquerda com `use` (chave desligada), metade
         * direita com pso_b (chave ligada), cada uma recortada pelo scissor. */
        if (s_audit_on && dr->fp_hash == s_audit_fp && s_audit_n < AUDIT_MAX &&
            (s_audit_n + 1u) * 4u * 8u <= METAL_MAX_DRAWS * 8u) {
            const u32 slot = s_audit_n++;
            s_audit_draw[slot] = i;
            for (int k = 0; k < 4; k++) {
                [enc setDepthStencilState:s_audit_dss[k]];
                if (k == 3) [enc setDepthBias:-256.0f slopeScale:-4.0f clamp:0.0f];
                [enc setVisibilityResultMode:MTLVisibilityResultModeCounting offset:(slot * 4u + (u32)k) * 8u];
                if (dr->is_indexed && s_draw.ib) {
                    [enc setVertexBufferOffset:dr->vb_byte_offset atIndex:0];
                    [enc drawIndexedPrimitives:dr->prim indexCount:dr->vertex_count indexType:MTLIndexTypeUInt32
                                   indexBuffer:s_draw.ib indexBufferOffset:dr->ib_byte_offset];
                } else {
                    [enc setVertexBufferOffset:0 atIndex:0];
                    [enc drawPrimitives:dr->prim vertexStart:dr->vb_byte_offset / (u32)sizeof(MetalBasicVertex)
                            vertexCount:dr->vertex_count];
                }
            }
            [enc setVisibilityResultMode:MTLVisibilityResultModeDisabled offset:0];
            [enc setDepthBias:0.0f slopeScale:0.0f clamp:0.0f];
            if (dr->dss) [enc setDepthStencilState:dr->dss];
        }
        const int split2 = !pick && dr->pso_b && use == dr->pso && metal_split_bit();
        for (int half = 0; half < (split2 ? 2 : 1); half++) {
            if (split2) {
                const u32 mid = tw / 2u;
                const u32 h0 = half ? mid : 0u, h1 = half ? tw : mid;
                const u32 a0 = sx > h0 ? sx : h0;
                const u32 a1 = (sx + sw) < h1 ? (sx + sw) : h1;
                if (a1 <= a0) continue;
                [enc setScissorRect:(MTLScissorRect){ a0, sy, a1 - a0, sh }];
                [enc setRenderPipelineState:half ? dr->pso_b : use];
            }
            if (dr->is_indexed && s_draw.ib) {
                [enc setVertexBufferOffset:dr->vb_byte_offset atIndex:0];
                [enc drawIndexedPrimitives:dr->prim
                                indexCount:dr->vertex_count
                                 indexType:MTLIndexTypeUInt32
                               indexBuffer:s_draw.ib
                         indexBufferOffset:dr->ib_byte_offset];
            } else {
                [enc setVertexBufferOffset:0 atIndex:0];
                [enc drawPrimitives:dr->prim
                        vertexStart:dr->vb_byte_offset / (u32)sizeof(MetalBasicVertex)
                        vertexCount:dr->vertex_count];
            }
        }
        if (pick)   /* nothing else drawn in this encoder may add to this draw's counter */
            [enc setVisibilityResultMode:MTLVisibilityResultModeDisabled offset:0];
        if (pick && s_pick_info[i] && s_pick_rb_n < PICK_RB_MAX) {
            if (!s_pick_rb)
                s_pick_rb = [s_state.device newBufferWithLength:PICK_RB_MAX * 32u
                                                        options:MTLResourceStorageModeShared];
            const u32 px = (u32)(pick_u * (float)tw), py = (u32)(pick_v * (float)th);
            if (s_pick_rb && px < tw && py < th) {
                [enc endEncoding];
                enc = nil;
                cur_c = -2;
                cur_d = -2;
                const u32 slot = s_pick_rb_n++;
                s_pick_rb_draw[slot] = i;
                s_pick_rb_fmt[slot] = (u32)ctex.pixelFormat;
                s_pick_rb_ci[slot] = ci;
                memset((u8*)s_pick_rb.contents + slot * 32u, 0, 32);
                id<MTLBlitCommandEncoder> be = [cb blitCommandEncoder];
                const NSUInteger cbpp = ctex.pixelFormat == MTLPixelFormatBGRA8Unorm ? 4u
                                      : ctex.pixelFormat == MTLPixelFormatRGBA16Float ? 8u : 0u;
                if (cbpp)
                    [be copyFromTexture:ctex sourceSlice:0 sourceLevel:0
                           sourceOrigin:MTLOriginMake(px, py, 0) sourceSize:MTLSizeMake(1, 1, 1)
                               toBuffer:s_pick_rb destinationOffset:slot * 32u
                  destinationBytesPerRow:cbpp destinationBytesPerImage:cbpp];
                id<MTLTexture> zt = (di >= 0 && (u32)di < s_n_surfaces) ? s_surfaces[di].tex : nil;
                if (zt && zt.pixelFormat == MTLPixelFormatDepth32Float_Stencil8 &&
                    zt.width == ctex.width && zt.height == ctex.height)
                    [be copyFromTexture:zt sourceSlice:0 sourceLevel:0
                           sourceOrigin:MTLOriginMake(px, py, 0) sourceSize:MTLSizeMake(1, 1, 1)
                               toBuffer:s_pick_rb destinationOffset:slot * 32u + 16u
                  destinationBytesPerRow:4 destinationBytesPerImage:4
                                options:MTLBlitOptionDepthFromDepthStencil];
                [be endEncoding];
            }
        }
        if (split2) {
            [enc setScissorRect:(MTLScissorRect){ sx, sy, sw, sh }];
            static u64 s_split_n, s_split_last, s_split_novam;
            s_split_n++;
            /* A §3 so' age sem VAMap (vam.stride == 0): com ele o VS sobrescreve os
             * 16 atributos antes de os usar e as duas metades saem iguais. */
            if (!dr->va_stride) s_split_novam++;
            if (s_state.frame_count >= s_split_last + 300u) {
                s_split_last = s_state.frame_count;
                fprintf(stderr, "[SPLIT] quadro %llu: %llu draws desenhados nas duas metades, "
                        "%llu deles SEM VAMap (onde a correcao 3 pode mudar algo)\n",
                        (unsigned long long)s_state.frame_count, (unsigned long long)s_split_n,
                        (unsigned long long)s_split_novam);
                fflush(stderr);
            }
        }
        n_draw++;
        if (!samples_self) fb_have_copy = 0;   /* this draw changed the surface */
        if (n_pass >= 1u && n_pass <= 16u) pass_draws[n_pass - 1u]++;
        if (pl_n >= 1u) { pl_draws[pl_n - 1u]++; if (pl_first[pl_n - 1u] < 0) pl_first[pl_n - 1u] = (int)i; }
        if (last_pass_first < 0) last_pass_first = (int)i;
    }
    if (enc) [enc endEncoding];
    if (s_audit_on && s_audit_n) {
        const u32 na = s_audit_n;
        u32* dd = (u32*)malloc(sizeof(u32) * 2u * na);
        if (!dd) goto audit_done;
        u32* nn = dd + na;
        for (u32 k = 0; k < na; k++) { dd[k] = s_audit_draw[k]; nn[k] = s_draw.draws[dd[k]].vertex_count; }
        const unsigned long long af = (unsigned long long)s_state.frame_count;
        [cb addCompletedHandler:^(id<MTLCommandBuffer> done) {
            (void)done;
            const uint64_t* v = (const uint64_t*)s_pick_vis.contents;
            for (u32 k = 0; k < na; k++)
                fprintf(stderr, "[BRONZE] f=%llu d=%u n=%u always=%llu lequal=%llu equal=%llu lequal_bias=%llu\n",
                        af, dd[k], nn[k], (unsigned long long)v[k * 4 + 0], (unsigned long long)v[k * 4 + 1],
                        (unsigned long long)v[k * 4 + 2], (unsigned long long)v[k * 4 + 3]);
            fflush(stderr);
            free(dd);
        }];
    }
audit_done:;
    if (pick) {   /* draws recorded this frame that the replay skipped before drawing */
        u32 ns = 0;
        for (u32 k = 0; k < s_draw.draw_count && k < METAL_MAX_DRAWS; k++) {
            const MetalDrawRec* q = &s_draw.draws[k];
            if (s_pick_reached[k] || q->clear_flags || q->is_blit) continue;
            if (ns++ < 64u)
                fprintf(stderr, "[PICK] saltado #%u S%d vp=%08X fp=%08X n=%u idx=%u prim=%u pso=%d has_state=%d "
                        "sc=%u,%u,%u,%u mrt=%d/%d\n", k, q->color_idx, q->vp_hash, q->fp_hash,
                        q->vertex_count, q->is_indexed, (unsigned)q->prim, q->pso ? 1 : 0, q->has_state,
                        q->sc[0], q->sc[1], q->sc[2], q->sc[3], q->pso_mrt_n, q->mrt_n);
        }
        fprintf(stderr, "[PICK] %u draws saltados no replay deste quadro\n", ns);
    }
    if (pick) {
        const u32 ndraw = s_draw.draw_count;
        const unsigned long long pf = (unsigned long long)s_state.frame_count;
        [cb addCompletedHandler:^(id<MTLCommandBuffer> done) {
            (void)done;
            const uint64_t* vis = (const uint64_t*)s_pick_vis.contents;
            u32 hit = 0;
            for (u32 k = 0; k < ndraw && k < METAL_MAX_DRAWS; k++) {
                if (!s_pick_info[k] || !vis[k]) continue;
                hit++;
                fprintf(stderr, "[PICK] %s samples=%llu\n", s_pick_info[k], (unsigned long long)vis[k]);
            }
            for (u32 k = 0; k < METAL_MAX_DRAWS; k++) {
                if (k < ndraw && vis[k]) continue;
                for (int u = 0; u < 4; u++)
                    if (s_pick_tex[k][u]) { CFRelease((__bridge CFTypeRef)s_pick_tex[k][u]); s_pick_tex[k][u] = nil; }
            }
            {
                int nv = 0;
                for (u32 k = 0; k < ndraw && k < METAL_MAX_DRAWS && nv < 32; k++) {
                    if (!s_pick_info[k] || !vis[k]) continue;
                    const char* q = strstr(s_pick_info[k], " vp=");
                    if (!q) continue;
                    const u32 h = (u32)strtoul(q + 4, NULL, 16);
                    int dup = 0;
                    for (int j = 0; j < nv; j++) if (s_pick_vps[j] == h) dup = 1;
                    if (!dup) s_pick_vps[nv++] = h;
                }
                s_pick_nvps = nv;
                s_live_skip_i = 0;
                s_live_skip_vp = 0;
            }
            s_pick_dump_ready = 1;
            for (u32 k = 0; k < s_pick_rb_n && s_pick_rb; k++) {
                const u8* q = (const u8*)s_pick_rb.contents + k * 32u;
                float z; memcpy(&z, q + 16, 4);
                const u32 d = s_pick_rb_draw[k];
                int prev = -1;   /* previous readback of the same target */
                for (int j = (int)k - 1; j >= 0; j--)
                    if (s_pick_rb_ci[j] == s_pick_rb_ci[k]) { prev = j; break; }
                if (prev >= 0 && memcmp(q, (const u8*)s_pick_rb.contents + (u32)prev * 32u, 32) == 0)
                    continue;   /* this draw changed nothing at the pixel */
                if (s_pick_rb_fmt[k] == (u32)MTLPixelFormatBGRA8Unorm)
                    fprintf(stderr, "[PICK] depois #%u S%d: cor RGBA=(%u,%u,%u,%u) z=%.8f samples=%llu\n",
                            d, s_pick_rb_ci[k], q[2], q[1], q[0], q[3], z, (unsigned long long)vis[d]);
                else
                    fprintf(stderr, "[PICK] depois #%u: fmt=%u z=%.8f samples=%llu\n",
                            d, s_pick_rb_fmt[k], z, (unsigned long long)vis[d]);
            }
            fprintf(stderr, "[PICK] fim f=%llu: %d draws cobrem a caixa, %u com fragmentos visiveis "
                    "(o ultimo opaco no alvo da cena e' quem pinta)\n", pf, s_pick_n, hit);
            fflush(stderr);
        }];
    }

    /* The surface the flip displays; fall back to the frame's last target. */
    int show = -1, by_last = 0;
    u32 disp_off = 0xFFFFFFFFu;
    const MetalGcmDisplayInfo* gdi = (const MetalGcmDisplayInfo*)cellGcmGetDisplayInfo(buffer_id);
    if (gdi) {
        disp_off = gdi->offset;
        int k = metal_surface_find(0, disp_off, 0);
        if (k >= 0 && s_surfaces[k].tex) show = k;
    }
    if (show < 0 && last_c >= 0) { show = last_c; by_last = 1; }
    {   /* PS3_METAL_DEBUG_NO_POSTFX=1 (OFF, diagnostic): present the 3D scene target
         * (the colour target with the most draws this frame, S4 in gameplay) instead
         * of the flip surface, skipping the whole post chain (6B2E71EA scene process,
         * bloom 304x171/76x42, 17B2047D composite, 09BA583D final). Separates the
         * remaining dark blurred regions into "3D renderer" vs "post-FX" in one run. */
        static int nopfx = -1;
        if (nopfx < 0) nopfx = metal_env_on("PS3_METAL_DEBUG_NO_POSTFX");
        if (nopfx) {
            u32 best = 0;
            int bi = -1;
            for (u32 pp = 0; pp < pl_n; pp++)
                if (pl_draws[pp] > best) { best = pl_draws[pp]; bi = pl_tgt[pp]; }
            if (bi >= 0 && (u32)bi < s_n_surfaces && s_surfaces[bi].tex) { show = bi; by_last = 0; }
        }
    }

    s_last_show = show;
    s_last_show_by_last = by_last;
    /* PS3_RSX_SPLIT: linha amarela de 2 px no meio da imagem final, para ver
     * onde acaba a metade SEM e comeca a metade COM a correcao. */
    if (metal_split_bit() && show >= 0 && (u32)show < s_n_surfaces && s_surfaces[show].tex && cb) {
        static id<MTLRenderPipelineState> line_pso;
        static int line_failed;
        if (!line_pso && !line_failed) {
            NSError* err = nil;
            id<MTLLibrary> lib = [s_state.device newLibraryWithSource:
                @"#include <metal_stdlib>\nusing namespace metal;\n"
                 "struct LO { float4 p [[position]]; };\n"
                 "vertex LO line_vs(uint v [[vertex_id]]) { LO o;\n"
                 "  float2 t = float2((v << 1) & 2, v & 2); o.p = float4(t * 2.0 - 1.0, 0, 1); return o; }\n"
                 "fragment float4 line_fs() { return float4(1.0, 0.85, 0.0, 1.0); }\n"
                                                              options:nil error:&err];
            MTLRenderPipelineDescriptor* d = [MTLRenderPipelineDescriptor new];
            d.vertexFunction = lib ? [lib newFunctionWithName:@"line_vs"] : nil;
            d.fragmentFunction = lib ? [lib newFunctionWithName:@"line_fs"] : nil;
            d.colorAttachments[0].pixelFormat = s_surfaces[show].tex.pixelFormat;
            id<MTLRenderPipelineState> ps = (d.vertexFunction && d.fragmentFunction)
                ? [s_state.device newRenderPipelineStateWithDescriptor:d error:&err] : nil;
            if (ps) line_pso = (__bridge id<MTLRenderPipelineState>)CFBridgingRetain(ps);
            else line_failed = 1;
        }
        if (line_pso) {
            id<MTLTexture> st = s_surfaces[show].tex;
            MTLRenderPassDescriptor* lp = [MTLRenderPassDescriptor renderPassDescriptor];
            lp.colorAttachments[0].texture = st;
            lp.colorAttachments[0].loadAction = MTLLoadActionLoad;
            lp.colorAttachments[0].storeAction = MTLStoreActionStore;
            id<MTLRenderCommandEncoder> le = [cb renderCommandEncoderWithDescriptor:lp];
            if (le) {
                const NSUInteger W = st.width, H = st.height;
                [le setRenderPipelineState:line_pso];
                [le setScissorRect:(MTLScissorRect){ W / 2 - 1, 0, 2, H }];
                [le drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
                [le endEncoding];
            }
        }
    }
    /* (modo 2, ver metal_green_sync_check) */
    extern __unsafe_unretained id<MTLBuffer> g_green_sync_buf;
    extern u64 g_green_sync_frame;
    /* PS3_TRACE_GREEN=1 (diagnostico, OFF): o menu de Power Up do GoW2 pisca
     * verde (1 quadro com a metade direita verde chapado, ou o fundo inteiro
     * verde-oliva). Amostra 5 blocos 8x8 da superficie mostrada por blit; quando
     * o GPU acaba, um bloco verde marca o quadro, e no present seguinte sai o
     * resumo dos draws desse quadro e do anterior (alvo, fp, texturas) -- a
     * diferenca entre os dois diz quem pinta o verde. */
    {
        static int on = -1;
        if (on < 0) { const char* e = getenv("PS3_TRACE_GREEN"); on = (e && (e[0] == '1' || e[0] == '2')) ? 1 : 0; }
        enum { GR_N = 8, GR_SUM = 65536, GR_BLK = 5 };
        static char ring[GR_N][GR_SUM];
        static u64 ring_f[GR_N];
        static int ring_show[GR_N];
        static u64 green_f[4];
        static int green_what[4];
        static u32 green_n;               /* escrito pelo completion handler */
        static __unsafe_unretained id<MTLBuffer> gbuf[GR_N];
        static u32 nreport;
        if (on && show >= 0 && (u32)show < s_n_surfaces && s_surfaces[show].tex && cb) {
            const u64 fnow = s_state.frame_count;
            /* 1) relatar quadros verdes detectados */
            const u32 gn = __atomic_exchange_n(&green_n, 0u, __ATOMIC_ACQ_REL);
            for (u32 gi = 0; gi < gn && gi < 4u && nreport < 12u; gi++) {
                const u64 gf = green_f[gi];
                nreport++;
                fprintf(stderr, "[GREEN] quadro %llu verde (blocos=0x%02X)\n",
                        (unsigned long long)gf, green_what[gi]);
                /* A superficie mostrada nesse quadro ainda tem o conteudo (triplo
                 * buffer; o command buffer deste present ainda nao foi submetido,
                 * e o do dump entra na fila antes dele). */
                for (int r = 0; r < GR_N; r++)
                    if (ring_f[r] == gf && ring_show[r] >= 0 && (u32)ring_show[r] < s_n_surfaces &&
                        s_surfaces[ring_show[r]].tex && nreport <= 6u) {
                        char path[96];
                        snprintf(path, sizeof path, "green_f%llu_S%d.bmp", (unsigned long long)gf, ring_show[r]);
                        metal_dump_texture_to(s_surfaces[ring_show[r]].tex, path);
                        fprintf(stderr, "[GREEN] dump %s\n", path);
                    }
                for (u64 want = gf - 1; want <= gf; want++)
                    for (int r = 0; r < GR_N; r++)
                        if (ring_f[r] == want)
                            fprintf(stderr, "[GREEN] %s f=%llu: %s\n", want == gf ? "VERDE " : "ANTES ",
                                    (unsigned long long)want, ring[r]);
                fflush(stderr);
            }
            /* 2) resumo dos draws deste quadro */
            const int slot = (int)(fnow % GR_N);
            char* sb = ring[slot];
            int sp = snprintf(sb, GR_SUM, "show=S%d", show);
            for (u32 i = 0; i < s_draw.draw_count && i < METAL_MAX_DRAWS && sp < GR_SUM - 160; i++) {
                const MetalDrawRec* dr = &s_draw.draws[i];
                if (!dr->has_state) continue;
                const int ci = dr->color_idx;
                sp += snprintf(sb + sp, (size_t)(GR_SUM - sp), " |%u:S%d%s", i, ci,
                               dr->is_blit ? "blit" : dr->clear_flags ? "clr" : "");
                if (dr->clear_flags)
                    sp += snprintf(sb + sp, (size_t)(GR_SUM - sp), "(%.2f,%.2f,%.2f,%.2f)",
                                   dr->clear_rgba[0], dr->clear_rgba[1], dr->clear_rgba[2], dr->clear_rgba[3]);
                else if (!dr->is_blit) {
                    sp += snprintf(sb + sp, (size_t)(GR_SUM - sp), " fp=%08X%s", dr->fp_hash,
                                   dr->pso ? "" : "(semPSO)");
                    /* viewport, scissor, blend e as constantes (fc/vc) deste draw:
                     * shader, texturas e geometria sairam IGUAIS entre o quadro
                     * verde e o anterior, por isso a diferenca esta' aqui. */
                    sp += snprintf(sb + sp, (size_t)(GR_SUM - sp),
                                   " vp=%.0f,%.0f,%.0f,%.0f sc=%u,%u,%u,%u bl=%X/%X/%X",
                                   dr->vp[0], dr->vp[1], dr->vp[2], dr->vp[3],
                                   dr->sc[0], dr->sc[1], dr->sc[2], dr->sc[3],
                                   dr->blend[0], dr->blend[1], dr->blend[2]);
                    if (s_draw.fc_mapped && dr->fc_count) {
                        u32 h = 2166136261u;
                        const u8* fb = (const u8*)s_draw.fc_mapped + dr->fc_byte_offset;
                        for (u32 b = 0; b < dr->fc_count * 16u; b++) h = (h ^ fb[b]) * 16777619u;
                        const float* f0 = (const float*)fb;
                        sp += snprintf(sb + sp, (size_t)(GR_SUM - sp), " fc#%u=%08X(%.2f,%.2f,%.2f,%.2f)",
                                       dr->fc_count, h, f0[0], f0[1], f0[2], f0[3]);
                    }
                    if (s_draw.vc_mapped) {
                        u32 h = 2166136261u;
                        const u8* vb = s_draw.vc_mapped + dr->vc_byte_offset;
                        for (u32 b = 0; b < 512u; b++) h = (h ^ vb[b]) * 16777619u;   /* c0..c31 */
                        sp += snprintf(sb + sp, (size_t)(GR_SUM - sp), " vc=%08X", h);
                    }
                    /* Geometria descodificada: caixa das posicoes (atributo 0) e o
                     * primeiro vertice do atributo 3 (COL0 no RSX). */
                    if (dr->va_stride && s_draw.va_mapped && dr->va_slot[0] >= 0) {
                        float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
                        const u32 nv = dr->vertex_count < 64u ? dr->vertex_count : 64u;
                        for (u32 v = 0; v < nv; v++) {
                            if ((u64)dr->va_base + (u64)(v + 1u) * dr->va_stride > (u64)s_draw.va_offset) break;
                            const float* pp = s_draw.va_mapped + ((size_t)dr->va_base + (size_t)v * dr->va_stride) * 4u
                                              + (size_t)dr->va_slot[0] * 4u;
                            if (pp[0] < x0) x0 = pp[0]; if (pp[0] > x1) x1 = pp[0];
                            if (pp[1] < y0) y0 = pp[1]; if (pp[1] > y1) y1 = pp[1];
                        }
                        sp += snprintf(sb + sp, (size_t)(GR_SUM - sp), " n=%u bb=(%.1f,%.1f)-(%.1f,%.1f)",
                                       dr->vertex_count, x0, y0, x1, y1);
                        if (dr->va_slot[3] >= 0) {
                            const float* cc = s_draw.va_mapped + (size_t)dr->va_base * 4u + (size_t)dr->va_slot[3] * 4u;
                            sp += snprintf(sb + sp, (size_t)(GR_SUM - sp), " c=(%.2f,%.2f,%.2f,%.2f)",
                                           cc[0], cc[1], cc[2], cc[3]);
                        }
                    }
                    for (int u = 0; u < 4; u++)
                        if (dr->tex[u])
                            sp += snprintf(sb + sp, (size_t)(GR_SUM - sp), " t%d=%08X/%lux%lu", u,
                                           dr->tex_off[u], (unsigned long)dr->tex[u].width,
                                           (unsigned long)dr->tex[u].height);
                }
            }
            ring_f[slot] = fnow;
            ring_show[slot] = show;
            /* 3) amostras da superficie mostrada */
            id<MTLTexture> st = s_surfaces[show].tex;
            if (!gbuf[slot]) {
                id<MTLBuffer> b = [s_state.device newBufferWithLength:GR_BLK * 8 * 8 * 4
                                                              options:MTLResourceStorageModeShared];
                gbuf[slot] = (__bridge id<MTLBuffer>)CFBridgingRetain(b);
            }
            id<MTLBuffer> gb = gbuf[slot];
            const u32 W = (u32)st.width, H = (u32)st.height;
            const u32 px[GR_BLK][2] = { { W * 70 / 100, H * 40 / 100 }, { W / 2, H / 2 },
                                        { W * 15 / 100, H * 40 / 100 }, { W * 80 / 100, H * 80 / 100 },
                                        { W * 25 / 100, H * 85 / 100 } };
            id<MTLBlitCommandEncoder> be = [cb blitCommandEncoder];
            for (int k = 0; k < GR_BLK; k++)
                [be copyFromTexture:st sourceSlice:0 sourceLevel:0
                       sourceOrigin:MTLOriginMake(px[k][0], px[k][1], 0)
                         sourceSize:MTLSizeMake(8, 8, 1)
                           toBuffer:gb destinationOffset:(NSUInteger)k * 256u
                  destinationBytesPerRow:32 destinationBytesPerImage:256];
            [be endEncoding];
            g_green_sync_buf = gb;
            g_green_sync_frame = fnow;
            [cb addCompletedHandler:^(id<MTLCommandBuffer> c) {
                (void)c;
                const u8* q = (const u8*)gb.contents;
                int what = 0;
                for (int k = 0; k < GR_BLK; k++) {
                    u32 sr = 0, sg = 0, sbb = 0;
                    for (int t = 0; t < 64; t++) {
                        sbb += q[k * 256 + t * 4 + 0]; sg += q[k * 256 + t * 4 + 1]; sr += q[k * 256 + t * 4 + 2];
                    }
                    sr /= 64u; sg /= 64u; sbb /= 64u;   /* BGRA8 */
                    if (sg >= 90u && sbb * 4u < sg && sr < sg) what |= 1 << k;
                }
                if (what) {
                    const u32 n = __atomic_load_n(&green_n, __ATOMIC_ACQUIRE);
                    if (n < 4u) {
                        green_f[n] = fnow; green_what[n] = what;
                        __atomic_store_n(&green_n, n + 1u, __ATOMIC_RELEASE);
                    }
                }
            }];
        }
    }
    {   /* PS3_TRACE_METAL_SHOW=1 (OFF, diagnostic): per present with >= 300 records, the
         * flip id, the surface shown and the NV3089 blit destinations recorded in this
         * frame (first 120 such presents) -- which display buffer a frame writes vs the
         * one the flip shows. */
        static long ts = -1;   /* value = minimum records (1 = 300) */
        if (ts < 0) {
            const char* e = getenv("PS3_TRACE_METAL_SHOW");
            char* end = NULL;
            long v = (e && *e) ? strtol(e, &end, 10) : 0;
            ts = (e && *e && end && *end == '\0' && v > 0 && v <= 100000) ? (v == 1 ? 300 : v) : 0;
        }
        static int tn = 0, started = 0;
        if (ts && s_draw.draw_count >= (u32)ts) started = 1;   /* then every present, any size */
        if (ts && started && tn < 3000) {
            char bl[160];
            int bn = 0;
            bl[0] = '\0';
            for (u32 i = 0; i < s_draw.draw_count; i++) {
                if (!s_draw.draws[i].is_blit) continue;
                const size_t room = sizeof(bl) - (size_t)bn;
                const int r = snprintf(bl + bn, room, " S%d<-S%d@%u",
                                       s_draw.draws[i].color_idx, s_draw.draws[i].blit_src, i);
                if (r < 0 || (size_t)r >= room) break;   /* would truncate: stop here */
                bn += r;
            }
            /* records per colour target, in first-use order (up to 8 targets) */
            int tg_idx[8], tg_cnt[8], tg_n = 0;
            for (u32 i = 0; i < s_draw.draw_count; i++) {
                const int c = s_draw.draws[i].color_idx;
                int j = 0;
                while (j < tg_n && tg_idx[j] != c) j++;
                if (j == tg_n) { if (tg_n == 8) continue; tg_idx[tg_n] = c; tg_cnt[tg_n] = 0; tg_n++; }
                tg_cnt[j]++;
            }
            char tg[160];
            int tn2 = 0;
            tg[0] = '\0';
            for (int j = 0; j < tg_n; j++) {
                const size_t room = sizeof(tg) - (size_t)tn2;
                const int r = snprintf(tg + tn2, room, " S%d:%d", tg_idx[j], tg_cnt[j]);
                if (r < 0 || (size_t)r >= room) break;
                tn2 += r;
            }
            char rq[96];
            int rn = 0;
            rq[0] = '\0';
            for (int j = 0; j < s_rt_req_n; j++) {
                const size_t room = sizeof(rq) - (size_t)rn;
                const int r = snprintf(rq + rn, room, " S%d", s_rt_req[j]);
                if (r < 0 || (size_t)r >= room) break;
                rn += r;
            }
            fprintf(stderr, "[SHOW] frame=%llu id=%u disp_off=0x%08X show=S%d by_last=%d recs=%u tg:%s rt_calls=%u rt:%s aoff=%u blits:%s\n",
                    (unsigned long long)s_state.frame_count, buffer_id, disp_off, show, by_last,
                    s_draw.draw_count, tg, s_rt_req_calls, rq, rsx_stats_take_aoffset(), bl);
            tn++;
        }
        s_rt_req_n = 0;
        s_rt_req_calls = 0;
    }
    MTLRenderPassDescriptor* p2 = [MTLRenderPassDescriptor renderPassDescriptor];
    p2.colorAttachments[0].texture     = dtex;
    p2.colorAttachments[0].loadAction  = MTLLoadActionClear;
    p2.colorAttachments[0].clearColor  = MTLClearColorMake(0, 0, 0, 1);
    p2.colorAttachments[0].storeAction = MTLStoreActionStore;
    id<MTLRenderCommandEncoder> e2 = [cb renderCommandEncoderWithDescriptor:p2];
    int drawable_written = 0;
    if (e2 && show >= 0 && movie_ensure_pso() && s_movie.pso && s_movie.sampler) {
        MTLViewport vp = { (double)lb.x, (double)lb.y, (double)lb.w, (double)lb.h, 0.0, 1.0 };
        [e2 setViewport:vp];
        [e2 setRenderPipelineState:s_movie.pso];
        [e2 setFragmentTexture:s_surfaces[show].tex atIndex:0];
        [e2 setFragmentSamplerState:s_movie.sampler atIndex:0];
        [e2 drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        drawable_written = 1;
    }
    if (e2) [e2 endEncoding];

    if (metal_trace_flip_on()) {
        /* Compact one-line present summary. Tags only on the unusual cases. */
        static u32 nlog, disp_gen;
        static int prev_db_set;
        static MetalGcmDisplayInfo prev_db[2];
        static u32 prev_dw, prev_dh;
        static int prev_show = -2;
        static u32 prev_disp_off = 0xFFFFFFFFu;
        u32 n_src_draw = 0, n_src_clear = 0, n_src_blit = 0, n_last_draw = 0;
        for (u32 i = 0; i < s_draw.draw_count && i < METAL_MAX_DRAWS; i++) {
            const MetalDrawRec* dr = &s_draw.draws[i];
            if (!dr->has_state) continue;
            if (show >= 0 && dr->color_idx == show) {
                if (dr->is_blit) n_src_blit++;
                else if (dr->clear_flags) n_src_clear++;
                else n_src_draw++;
                /* PS3_TRACE_COMPOSITE=1 (diagnostico, OFF): o que o draw de
                 * composicao final (o que escreve no buffer de tela) amostra.
                 * A piscada do menu mostra a CENA CRUA dentro do buffer de tela
                 * certo, com a composicao presente em todos os frames -- entao a
                 * suspeita e' a textura: o endereco que a composicao le' pode
                 * estar ligado a uma superficie diferente (aliasing de VRAM ou
                 * recriacao por tamanho). Por unidade: endereco guest, tamanho do
                 * objeto gravado, que superficie o tem, e se ainda e' a superficie
                 * actual desse endereco. */
                static int comp = -1;
                if (comp < 0) comp = metal_env_on("PS3_TRACE_COMPOSITE");
                if (comp && !dr->is_blit && !dr->clear_flags) {
                    char b[400]; int p2 = 0;
                    for (int u = 0; u < 4 && p2 < 360; u++) {
                        id<MTLTexture> t = dr->tex[u];
                        if (!t && !dr->tex_off[u]) continue;
                        int owner = -1;
                        for (u32 k = 0; k < s_n_surfaces; k++)
                            if (t && s_surfaces[k].tex == t) { owner = (int)k; break; }
                        /* tex_off = offset da textura em VRAM (tex_addr e' o registo de
                         * MODO de enderecamento, nao serve para isto). */
                        const int now = metal_surface_find(0, dr->tex_off[u], 0);
                        p2 += snprintf(b + p2, sizeof b - (size_t)p2,
                                       " u%d=0x%08X/%lux%lu/S%d/atual=S%d%s", u, dr->tex_off[u],
                                       t ? (unsigned long)t.width : 0ul, t ? (unsigned long)t.height : 0ul,
                                       owner, now, (owner != now) ? "!DIF" : "");
                    }
                    fprintf(stderr, "[COMP] f=%llu show=S%d draw=%u fp=%08X%s\n",
                            (unsigned long long)s_state.frame_count, show, i, dr->fp_hash, b);
                }
            }
            if (last_c >= 0 && dr->color_idx == last_c &&
                !dr->is_blit && !dr->clear_flags)
                n_last_draw++;
        }
        const MetalGcmDisplayInfo* db0 =
            (const MetalGcmDisplayInfo*)cellGcmGetDisplayInfo(0);
        const MetalGcmDisplayInfo* db1 =
            (const MetalGcmDisplayInfo*)cellGcmGetDisplayInfo(1);
        const MetalGcmDisplayInfo* db2 =
            (const MetalGcmDisplayInfo*)cellGcmGetDisplayInfo(2);
        const u32 cur_id = cellGcmGetCurrentDisplayBufferId();
        const u32 dw = dtex ? (u32)dtex.width : 0u;
        const u32 dh = dtex ? (u32)dtex.height : 0u;
        int display_changed = 0;
        if (db0 && db1 && db2) {
            static MetalGcmDisplayInfo prev_db2;
            if (!prev_db_set ||
                prev_db[0].offset != db0->offset || prev_db[0].pitch != db0->pitch ||
                prev_db[0].width != db0->width || prev_db[0].height != db0->height ||
                prev_db[1].offset != db1->offset || prev_db[1].pitch != db1->pitch ||
                prev_db[1].width != db1->width || prev_db[1].height != db1->height ||
                prev_db2.offset != db2->offset || prev_db2.pitch != db2->pitch ||
                prev_db2.width != db2->width || prev_db2.height != db2->height) {
                if (prev_db_set) {
                    display_changed = 1;
                    disp_gen++;
                }
                prev_db[0] = *db0;
                prev_db[1] = *db1;
                prev_db2 = *db2;
                prev_db_set = 1;
            }
        }
        const int src_ok = (show >= 0 && (u32)show < s_n_surfaces && s_surfaces[show].tex);
        const int src_written = (n_src_draw + n_src_blit) > 0u;
        const int rt_recreated = src_ok && s_surfaces[show].recreated;
        const int flip_unknown = (gdi == NULL) || (show < 0) || by_last;
        const int present_without_write = !drawable_written;
        const int drawable_size_changed = (prev_dw && (prev_dw != dw || prev_dh != dh));
        const int source_eq_dest = src_ok && dtex && s_surfaces[show].tex == dtex;
        const int show_changed = (prev_show != -2 && prev_show != show);
        char tags[192];
        int tn = 0;
        tags[0] = '\0';
#define FLIP_TAG(s) do { \
            if (tn < (int)sizeof(tags) - 24) { \
                tn += snprintf(tags + tn, sizeof(tags) - (size_t)tn, "%s%s", \
                               tn ? "|" : "", (s)); \
            } \
        } while (0)
        int hot = 0;
        if (present_without_write) { FLIP_TAG("PRESENT_WITHOUT_WRITE"); hot = 1; }
        if (flip_unknown) { FLIP_TAG("FLIP_UNKNOWN_BUFFER"); hot = 1; }
        if (display_changed) { FLIP_TAG("DISPLAY_CHANGED"); hot = 1; }
        if (rt_recreated) { FLIP_TAG("RT_RECREATED"); hot = 1; }
        if (drawable_size_changed) { FLIP_TAG("DRAWABLE_CHANGED"); hot = 1; }
        if (source_eq_dest) { FLIP_TAG("SOURCE_EQ_DEST"); hot = 1; }
        if (src_ok && gdi && s_surfaces[show].offset != disp_off) {
            FLIP_TAG("SHOW_OFF_NE_FLIP"); hot = 1;
        }
        if (!e2) { FLIP_TAG("NO_PRESENT_ENCODER"); hot = 1; }
        /* Informational — legal on ping-pong / offscreen composite. Not cap-bypass. */
        if (show_changed) FLIP_TAG("SHOW_CHANGED");
        if (src_ok && !src_written) FLIP_TAG("SHOW_NO_DRAWS");
        if (src_ok && n_src_clear > 0u && n_src_draw == 0u && n_src_blit == 0u) {
            FLIP_TAG("SHOW_CLEAR_NO_DRAW"); hot = 1;
        }
        if (show >= 0 && last_c >= 0 && show != last_c && n_last_draw > 0u)
            FLIP_TAG("FLIP_NE_LAST_RT");
#undef FLIP_TAG
        const int tagged = hot;
        const int over_cap = (long)nlog >= metal_trace_flip_cap();
        if (!over_cap || tagged) {
            const u32 rt_off = (s_color_rt_idx >= 0 && (u32)s_color_rt_idx < s_n_surfaces)
                ? s_surfaces[s_color_rt_idx].offset : 0xFFFFFFFFu;
            const u32 rt_w = (s_color_rt_idx >= 0 && (u32)s_color_rt_idx < s_n_surfaces)
                ? s_surfaces[s_color_rt_idx].w : 0u;
            const u32 rt_h = (s_color_rt_idx >= 0 && (u32)s_color_rt_idx < s_n_surfaces)
                ? s_surfaces[s_color_rt_idx].h : 0u;
            fprintf(stderr,
                    "[FLIP] f=%llu gen=%u buf=%u cur=%u disp_off=0x%08X "
                    "show=S%d by_last=%d last_c=S%d colorRT=S%d off=0x%08X %ux%u "
                    "db0=0x%08X/%u/%ux%u db1=0x%08X/%u/%ux%u db2=0x%08X/%u/%ux%u "
                    "src=%ux%u pitch=%u gfmt=0x%08X inited=%d rec=%d "
                    "passes=%u metalDraws=%u clears=%u blits=%u skip=%u recs=%u "
                    "srcDraw=%u srcClear=%u srcBlit=%u srcWritten=%s "
                    "drawableWritten=%s present=YES drawable=%ux%u "
                    "srcTex=%p dstTex=%p tags=%s\n",
                    (unsigned long long)s_state.frame_count, disp_gen,
                    buffer_id, cur_id, disp_off,
                    show, by_last, last_c, s_color_rt_idx, rt_off, rt_w, rt_h,
                    db0 ? db0->offset : 0u, db0 ? db0->pitch : 0u,
                    db0 ? db0->width : 0u, db0 ? db0->height : 0u,
                    db1 ? db1->offset : 0u, db1 ? db1->pitch : 0u,
                    db1 ? db1->width : 0u, db1 ? db1->height : 0u,
                    db2 ? db2->offset : 0u, db2 ? db2->pitch : 0u,
                    db2 ? db2->width : 0u, db2 ? db2->height : 0u,
                    src_ok ? s_surfaces[show].w : 0u,
                    src_ok ? s_surfaces[show].h : 0u,
                    gdi ? gdi->pitch : 0u,
                    src_ok ? s_surfaces[show].guest_fmt : 0u,
                    src_ok ? s_surfaces[show].inited : 0,
                    src_ok ? s_surfaces[show].recreated : 0,
                    n_pass, n_draw, n_clear, n_blit, n_skip, s_draw.draw_count,
                    n_src_draw, n_src_clear, n_src_blit,
                    src_written ? "YES" : "NO",
                    drawable_written ? "YES" : "NO",
                    dw, dh,
                    src_ok ? (__bridge void*)s_surfaces[show].tex : NULL,
                    dtex ? (__bridge void*)dtex : NULL,
                    tags[0] ? tags : "-");
            fflush(stderr);
            nlog++;
        }
        if (src_ok) s_surfaces[show].recreated = 0;
        prev_show = show;
        prev_disp_off = disp_off;
        prev_dw = dw;
        prev_dh = dh;
    }

    {   /* PS3_TRACE_METAL_PASSES=1 (OFF, diagnostic): on the surface-dump frames,
         * each pass's target, size, draw count, the FP hash of its first draw and
         * that draw's texture sources -- to find which pass writes the white
         * 304x171 buffers of the post chain (2026-09-16 gameplay dump). */
        static int on = -1;
        if (on < 0) on = metal_env_on("PS3_TRACE_METAL_PASSES");
        const long long fa = s_surf_dump_at;
        const unsigned long long fc = s_state.frame_count;
        if (on && fa >= 0 && (fc == (unsigned long long)fa || fc == (unsigned long long)fa + 1ull)) {
            fprintf(stderr, "[PASS] f=%llu passes=%u show=%d\n", fc, pl_n, show);
            for (u32 pp = 0; pp < pl_n; pp++) {
                const int ti = pl_tgt[pp];
                const int ok = (ti >= 0 && (u32)ti < s_n_surfaces);
                char tb2[64];
                int tw2 = 0;
                u32 fph = 0;
                tb2[0] = 0;
                if (pl_first[pp] >= 0) {
                    const MetalDrawRec* fd = &s_draw.draws[pl_first[pp]];
                    fph = fd->fp_hash;
                    for (int tj = 0; tj < 4 && tw2 < (int)sizeof(tb2) - 8; tj++) {
                        int sk = -1;
                        for (u32 k = 0; fd->tex[tj] && k < s_n_surfaces; k++)
                            if (s_surfaces[k].tex == fd->tex[tj]) { sk = (int)k; break; }
                        if (sk >= 0)
                            tw2 += snprintf(tb2 + tw2, sizeof(tb2) - (size_t)tw2, " S%d", sk);
                        else
                            tw2 += snprintf(tb2 + tw2, sizeof(tb2) - (size_t)tw2, "%s",
                                            fd->tex[tj] ? " T" : " -");
                    }
                }
                fprintf(stderr, "[PASS] #%u tgt=S%d %ux%u gfmt=0x%08X draws=%u fp=%08X tex:%s\n", pp, ti,
                        ok ? s_surfaces[ti].w : 0u, ok ? s_surfaces[ti].h : 0u,
                        ok ? s_surfaces[ti].guest_fmt : 0u, pl_draws[pp], fph, tb2);
                {   /* Draws of this pass that write alpha (colour mask A bit) and
                     * their FPs: the scene target's alpha is ~1 everywhere and
                     * drives the bloom bright-pass (2026-09-16 alpha dump). */
                    u32 aw = 0, nd = 0, afp[4] = {0, 0, 0, 0}, nafp = 0;
                    for (u32 di2 = 0; di2 < s_draw.draw_count; di2++) {
                        const MetalDrawRec* q = &s_draw.draws[di2];
                        if (!q->has_state || q->clear_flags || q->is_blit || q->color_idx != ti) continue;
                        if (pp + 1u < pl_n && pl_first[pp + 1u] >= 0 && (int)di2 >= pl_first[pp + 1u]) break;
                        if (pl_first[pp] >= 0 && (int)di2 < pl_first[pp]) continue;
                        nd++;
                        const int wa = (q->cmask & 0x01000000u) || q->cmask == 0u;
                        if (!wa) continue;
                        aw++;
                        int seen = 0;
                        for (u32 z = 0; z < nafp; z++) if (afp[z] == q->fp_hash) seen = 1;
                        if (!seen && nafp < 4u) afp[nafp++] = q->fp_hash;
                    }
                    fprintf(stderr, "[PASS]    alpha_writers=%u/%u fps=%08X %08X %08X %08X\n",
                            aw, nd, afp[0], afp[1], afp[2], afp[3]);
                    {   /* The material alpha is min(c464.w, c457.z) (VP writes o[8] =
                         * min(r3, c457.yyyz) with r3.w = c464.w). Print it for the first
                         * alpha writers of this pass: characters measured 0, so the ones
                         * filling the scene target with 1 are these. */
                        u32 shown = 0, hi_alpha = 0, lo_alpha = 0;
                        for (u32 di3 = 0; di3 < s_draw.draw_count; di3++) {
                            const MetalDrawRec* q = &s_draw.draws[di3];
                            if (!q->has_state || q->clear_flags || q->is_blit || q->color_idx != ti) continue;
                            if (pl_first[pp] >= 0 && (int)di3 < pl_first[pp]) continue;
                            if (pp + 1u < pl_n && pl_first[pp + 1u] >= 0 && (int)di3 >= pl_first[pp + 1u]) break;
                            if (!((q->cmask & 0x01000000u) || q->cmask == 0u)) continue;
                            if (!s_draw.vc_mapped || RSX_MAX_VERTEX_CONSTANTS <= 464) continue;
                            if ((u64)q->vc_byte_offset + (u64)METAL_VC_SLOT_BYTES > (u64)METAL_VC_RING_BYTES) continue;
                            const float* cc = (const float*)(s_draw.vc_mapped + q->vc_byte_offset);
                            const float a = cc[464 * 4 + 3] < cc[457 * 4 + 2] ? cc[464 * 4 + 3] : cc[457 * 4 + 2];
                            if (a >= 0.5f) hi_alpha++; else lo_alpha++;
                            if (a >= 0.5f && shown < 14u) {
                                shown++;
                                fprintf(stderr, "[ALPHA] rec=%u fp=%08X vp=%08X cmask=%08X n=%u blend=%u sf=%X df=%X c457.z=%g c464.w=%g -> tc1.w=%g\n",
                                        di3, q->fp_hash, q->vp_hash, q->cmask, q->vertex_count,
                                        q->blend[0], q->blend[1], q->blend[2],
                                        cc[457 * 4 + 2], cc[464 * 4 + 3], a);
                                fprintf(stderr, "[ALPHA]     tex0 fmt=0x%08X addr=0x%08X filter=0x%08X ctl1=0x%08X\n",
                                        q->tex0_reg[0], q->tex0_reg[1], q->tex0_reg[2], q->tex0_reg[3]);
                            }
                        }
                        fprintf(stderr, "[ALPHA] pass tgt=S%d draws com alfa>=0.5: %u, alfa<0.5: %u\n",
                                ti, hi_alpha, lo_alpha);
                    }
                }
                if (pl_first[pp] >= 0 && ti >= 0 && (u32)ti < s_n_surfaces &&
                    s_surfaces[ti].w == 1216u && pl_draws[pp] > 1u && pl_draws[pp] <= 400u &&
                    pp > 0u) {
                    /* Every draw of the full-res post target pass (S8: the scene copy
                     * 6B2E71EA then ~23 draws on top): FP, blend-relevant colour mask
                     * and texture sources. Frames alternate clean/dark with ghosts of
                     * other camera positions, and the copy alone is clean (2026-09-16). */
                    u32 dshown = 0;
                    for (u32 di5 = (u32)pl_first[pp]; di5 < s_draw.draw_count && dshown < pl_draws[pp]; di5++) {
                        const MetalDrawRec* q = &s_draw.draws[di5];
                        if (!q->has_state || q->clear_flags || q->is_blit || q->color_idx != ti) continue;
                        char tb3[64];
                        int tw3 = 0;
                        tb3[0] = 0;
                        for (int tj = 0; tj < 4 && tw3 < (int)sizeof(tb3) - 8; tj++) {
                            int sk = -1;
                            for (u32 k = 0; q->tex[tj] && k < s_n_surfaces; k++)
                                if (s_surfaces[k].tex == q->tex[tj]) { sk = (int)k; break; }
                            if (sk >= 0) tw3 += snprintf(tb3 + tw3, sizeof(tb3) - (size_t)tw3, " S%d", sk);
                            else tw3 += snprintf(tb3 + tw3, sizeof(tb3) - (size_t)tw3, "%s", q->tex[tj] ? " T" : " -");
                        }
                        fprintf(stderr, "[PASS]    d%u fp=%08X vp=%08X cmask=%08X n=%u tex:%s z=%d dt=%u dw=%u df=%X cull=%d ccw=%d bl=%u %X/%X/%X prim=%d idx=%d po=%d/%g/%g\n",
                                dshown, q->fp_hash, q->vp_hash, q->cmask, q->vertex_count, tb3, q->depth_idx,
                                q->ds_desc.depth_test, q->ds_desc.depth_write_mask, q->ds_desc.depth_func,
                                q->cull_mode, q->winding_ccw, q->blend[0], q->blend[1], q->blend[2], q->blend[3],
                                (int)q->prim, (int)q->is_indexed, q->poly_offset, q->poly_scale, q->poly_bias);
                        if ((q->fp_hash == 0xBFA6689Bu || q->fp_hash == 0xD59B685Cu ||
                             q->fp_hash == 0x654864DAu || q->fp_hash == 0x671F7750u ||
                             q->fp_hash == 0x17B2047Du || q->fp_hash == 0x09BA583Du ||
                             q->fp_hash == 0x6B2E71EAu) && s_draw.fc_mapped &&
                            (u64)q->fc_byte_offset + 32u <= (u64)METAL_MAX_DRAWS * METAL_FC_MAX * 16u * METAL_FRAMES_IN_FLIGHT) {
                            /* Shadow apply / alpha writer constants (FP returns fc[0].xxxy). */
                            const float* k = s_draw.fc_mapped + q->fc_byte_offset / 4u;
                            fprintf(stderr, "[PASS]       fc n=%u fc0=(%g,%g,%g,%g) fc1=(%g,%g,%g,%g)\n", q->fc_count,
                                    k[0], k[1], k[2], k[3], k[4], k[5], k[6], k[7]);
                        }
                        if ((q->fp_hash == 0xDD5B993Eu || q->fp_hash == 0x59864B76u) && q->va_stride &&
                            s_draw.va_mapped && s_draw.vc_mapped) {
                            /* Light shafts / dust show hard-edged translucent slabs
                             * (2026-09-16): vertex colour alpha and c467 of the first
                             * vertices, to tell a data decode fault from the effect. */
                            static u32 nfx = 0;
                            /* c[467] lives inside the METAL_VC_SLOT_BYTES snapshot (512 constants). */
                            if (nfx < 24u && RSX_MAX_VERTEX_CONSTANTS > 467 &&
                                (u64)q->vc_byte_offset + (u64)METAL_VC_SLOT_BYTES <= (u64)METAL_VC_RING_BYTES) {
                                nfx++;
                                const float* c = (const float*)(s_draw.vc_mapped + q->vc_byte_offset);
                                fprintf(stderr, "[PASS]       fx c467=(%g,%g,%g,%g) slots v0=%d v3=%d v8=%d stride=%u\n",
                                        c[467 * 4], c[467 * 4 + 1], c[467 * 4 + 2], c[467 * 4 + 3],
                                        (int)q->va_slot[0], (int)q->va_slot[3], (int)q->va_slot[8], q->va_stride);
                                if (q->tex[0]) {
                                    MTLTextureSwizzleChannels sw = q->tex[0].swizzle;
                                    fprintf(stderr, "[PASS]       fx tex0 %lux%lu pf=%lu swizzle=%d%d%d%d guest fmt=0x%08X addr=0x%08X filter=0x%08X ctl1=0x%08X\n",
                                            (unsigned long)q->tex[0].width, (unsigned long)q->tex[0].height,
                                            (unsigned long)q->tex[0].pixelFormat,
                                            (int)sw.red, (int)sw.green, (int)sw.blue, (int)sw.alpha,
                                            q->tex0_reg[0], q->tex0_reg[1], q->tex0_reg[2], q->tex0_reg[3]);
                                    if (q->tex[0].pixelFormat == MTLPixelFormatBGRA8Unorm &&
                                        q->tex[0].storageMode != MTLStorageModeMemoryless) {
                                        char tp[64];
                                        snprintf(tp, sizeof(tp), "fxtex_%u_%lux%lu.bmp", nfx,
                                                 (unsigned long)q->tex[0].width, (unsigned long)q->tex[0].height);
                                        metal_dump_texture_to(q->tex[0], tp);
                                    }
                                }
                                for (u32 vi = 0; vi < q->vertex_count && vi < 8u; vi++) {
                                    /* Stay inside this frame's written float4 range. */
                                    if ((u64)q->va_base + (u64)(vi + 1u) * q->va_stride > (u64)s_draw.va_offset) break;
                                    const float* vb = s_draw.va_mapped + ((size_t)q->va_base + (size_t)vi * q->va_stride) * 4u;
                                    const float* p0 = q->va_slot[0] >= 0 ? vb + q->va_slot[0] * 4 : NULL;
                                    const float* p3 = q->va_slot[3] >= 0 ? vb + q->va_slot[3] * 4 : NULL;
                                    const float* p8 = q->va_slot[8] >= 0 ? vb + q->va_slot[8] * 4 : NULL;
                                    fprintf(stderr, "[PASS]       fx v%u pos=(%g,%g,%g,%g) col=(%g,%g,%g,%g) uv=(%g,%g)\n", vi,
                                            p0 ? p0[0] : 0.0f, p0 ? p0[1] : 0.0f, p0 ? p0[2] : 0.0f, p0 ? p0[3] : 0.0f,
                                            p3 ? p3[0] : 0.0f, p3 ? p3[1] : 0.0f, p3 ? p3[2] : 0.0f, p3 ? p3[3] : 0.0f,
                                            p8 ? p8[0] : 0.0f, p8 ? p8[1] : 0.0f);
                                }
                                {   /* Tamanho do quad NA TELA. As particulas do GoW2 sao
                                     * billboards: as 6 posicoes em espaco de objeto diferem
                                     * ~0,013 e e' a matriz de modelo (c260..c263) que as
                                     * expande, depois a view-proj (c256..c259). Sem este
                                     * numero nao se distingue "particulas grandes demais" de
                                     * "particulas a mais" -- as duas enchem o ecra' de
                                     * vermelho. Fracao = metade da largura NDC. */
                                    const float* c = (const float*)(s_draw.vc_mapped + q->vc_byte_offset);
                                    float xmin = 9e9f, xmax = -9e9f, ymin = 9e9f, ymax = -9e9f;
                                    u32 nv = 0;
                                    for (u32 vi = 0; vi < q->vertex_count && vi < 6u; vi++) {
                                        if ((u64)q->va_base + (u64)(vi + 1u) * q->va_stride > (u64)s_draw.va_offset) break;
                                        if (q->va_slot[0] < 0) break;
                                        const float* v0 = s_draw.va_mapped +
                                            ((size_t)q->va_base + (size_t)vi * q->va_stride) * 4u + q->va_slot[0] * 4;
                                        float r0[4], o0[4];
                                        for (int k = 0; k < 4; k++)
                                            r0[k] = v0[0] * c[260 * 4 + k] + v0[1] * c[261 * 4 + k] +
                                                    v0[2] * c[262 * 4 + k] + v0[3] * c[263 * 4 + k];
                                        for (int k = 0; k < 4; k++)
                                            o0[k] = r0[0] * c[256 * 4 + k] + r0[1] * c[257 * 4 + k] +
                                                    r0[2] * c[258 * 4 + k] + r0[3] * c[259 * 4 + k];
                                        if (!(o0[3] > 1e-6f)) continue;
                                        const float nx = o0[0] / o0[3], ny = o0[1] / o0[3];
                                        if (nx < xmin) xmin = nx;
                                        if (nx > xmax) xmax = nx;
                                        if (ny < ymin) ymin = ny;
                                        if (ny > ymax) ymax = ny;
                                        nv++;
                                    }
                                    if (nv >= 3u)
                                        fprintf(stderr, "[PASS]       fx quad NDC=(%.4f x %.4f) "
                                                "= %.1f%% x %.1f%% do ecra (nv=%u)\n",
                                                xmax - xmin, ymax - ymin,
                                                50.0f * (xmax - xmin), 50.0f * (ymax - ymin), nv);
                                    /* Um quad que ocupa mais de 20% do ecra' nao e' uma gota
                                     * de sangue. Imprimir a posicao CRUA em espaco de objeto e
                                     * o w de clip separa "dado corrompido a montante" (NaN,
                                     * valores absurdos) de "particula legitimamente colada a'
                                     * camera" (w pequeno mas posicao plausivel). */
                                    if (nv >= 3u && (xmax - xmin > 0.4f || ymax - ymin > 0.4f ||
                                                     nv < q->vertex_count)) {
                                        for (u32 vi = 0; vi < q->vertex_count && vi < 6u; vi++) {
                                            if ((u64)q->va_base + (u64)(vi + 1u) * q->va_stride > (u64)s_draw.va_offset) break;
                                            if (q->va_slot[0] < 0) break;
                                            const float* v0 = s_draw.va_mapped +
                                                ((size_t)q->va_base + (size_t)vi * q->va_stride) * 4u + q->va_slot[0] * 4;
                                            float r0[4], o0[4];
                                            for (int k = 0; k < 4; k++)
                                                r0[k] = v0[0] * c[260 * 4 + k] + v0[1] * c[261 * 4 + k] +
                                                        v0[2] * c[262 * 4 + k] + v0[3] * c[263 * 4 + k];
                                            for (int k = 0; k < 4; k++)
                                                o0[k] = r0[0] * c[256 * 4 + k] + r0[1] * c[257 * 4 + k] +
                                                        r0[2] * c[258 * 4 + k] + r0[3] * c[259 * 4 + k];
                                            fprintf(stderr, "[PASS]       fx GRANDE v%u obj=(%g,%g,%g,%g) "
                                                    "world=(%g,%g,%g) clip_w=%g\n", vi,
                                                    v0[0], v0[1], v0[2], v0[3], r0[0], r0[1], r0[2], o0[3]);
                                        }
                                    }
                                }
                            }
                        }
                        if (q->stencil_en)
                            fprintf(stderr, "[PASS]       st F:f=%X op=%X/%X/%X rm=%X wm=%X ref=%u"
                                    " B:f=%X op=%X/%X/%X rm=%X wm=%X ref=%u\n",
                                    q->ds_desc.f_func, q->ds_desc.f_fail, q->ds_desc.f_zfail, q->ds_desc.f_zpass,
                                    q->ds_desc.f_rmask, q->ds_desc.f_wmask, q->stencil_ref_front,
                                    q->ds_desc.b_func, q->ds_desc.b_fail, q->ds_desc.b_zfail, q->ds_desc.b_zpass,
                                    q->ds_desc.b_rmask, q->ds_desc.b_wmask, q->stencil_ref_back);
                        dshown++;
                    }
                }
                if (pl_first[pp] >= 0 && pl_draws[pp] > 400u) {
                    /* Big pass (scene): distinct (FP, blend, colour mask, stencil, depth
                     * write) with draw and vertex counts -- the combat frames show huge
                     * translucent red/white planes (2026-09-16 gameplay shots). */
                    typedef struct { u32 fp, cm, bl[4], st, dw, n, v, po; } Grp;
                    Grp g[64];
                    u32 ng = 0;
                    for (u32 di6 = (u32)pl_first[pp]; di6 < s_draw.draw_count; di6++) {
                        const MetalDrawRec* q = &s_draw.draws[di6];
                        if (!q->has_state || q->clear_flags || q->is_blit || q->color_idx != ti) continue;
                        if (pp + 1u < pl_n && pl_first[pp + 1u] >= 0 && (int)di6 >= pl_first[pp + 1u]) break;
                        u32 k = 0;
                        for (; k < ng; k++)
                            if (g[k].fp == q->fp_hash && g[k].cm == q->cmask && g[k].st == (u32)q->stencil_en &&
                                g[k].dw == q->ds_desc.depth_write_mask && memcmp(g[k].bl, q->blend, sizeof(g[k].bl)) == 0)
                                break;
                        if (k == ng) {
                            if (ng >= 64u) continue;
                            memset(&g[k], 0, sizeof(g[k]));
                            g[k].fp = q->fp_hash; g[k].cm = q->cmask; g[k].st = (u32)q->stencil_en;
                            g[k].dw = q->ds_desc.depth_write_mask;
                            memcpy(g[k].bl, q->blend, sizeof(g[k].bl));
                            ng++;
                        }
                        g[k].n++;
                        g[k].v += q->vertex_count;
                        if (q->poly_offset) g[k].po++;
                    }
                    for (u32 k = 0; k < ng; k++)
                        fprintf(stderr, "[PASS]    grp fp=%08X cmask=%08X bl=%u %X/%X/%X st=%u dw=%u draws=%u verts=%u poly_offset=%u\n",
                                g[k].fp, g[k].cm, g[k].bl[0], g[k].bl[1], g[k].bl[2], g[k].bl[3],
                                g[k].st, g[k].dw, g[k].n, g[k].v, g[k].po);
                }
                if (pp == 0u && s_draw.va_mapped && s_draw.vc_mapped) {
                    /* Character draws (layout with v3 colour and v7 bone indices):
                     * the skinned VP sets TEXCOORD1 = min(v3 * c465.zzzy, c465.zzzy)
                     * and the characters render white (2026-09-16 tc1 view). Print
                     * c465..c467 and vertex 0's decoded v0/v3/v7/v8, first 3 draws. */
                    {   /* Every character-layout draw of the pass: TEXCOORD1 of vertex 0
                         * computed on the CPU exactly as the VP does, min(v3*c465.zzzy, c465.zzzy). */
                        u32 nch = 0, sat = 0;
                        float max_scale = 0.0f, max_v3 = 0.0f, max_tc1 = 0.0f;
                        for (u32 di4 = 0; di4 < s_draw.draw_count; di4++) {
                            const MetalDrawRec* q = &s_draw.draws[di4];
                            if (!q->has_state || q->color_idx != ti || q->va_stride == 0u) continue;
                            if (q->va_slot[3] < 0 || q->va_slot[7] < 0) continue;
                            nch++;
                            const float* c = (const float*)(s_draw.vc_mapped + q->vc_byte_offset);
                            const float* p3 = s_draw.va_mapped + (size_t)q->va_base * 4u + (size_t)q->va_slot[3] * 4u;
                            const float sc = c[465 * 4 + 2];
                            float t = 0.0f;
                            for (int ch = 0; ch < 3; ch++) {
                                float x = p3[ch] * sc;
                                if (x > sc) x = sc;
                                if (x > t) t = x;
                                if (p3[ch] > max_v3) max_v3 = p3[ch];
                            }
                            if (sc > max_scale) max_scale = sc;
                            if (t > max_tc1) max_tc1 = t;
                            if (t >= 1.0f) sat++;
                        }
                        fprintf(stderr, "[PASS]    chars=%u tc1_ge_1=%u max_c465z=%g max_v3=%g max_tc1=%g\n",
                                nch, sat, max_scale, max_v3, max_tc1);
                    }
                    u32 shown_ch = 0;
                    for (u32 di3 = 0; di3 < s_draw.draw_count && shown_ch < 3u; di3++) {
                        const MetalDrawRec* q = &s_draw.draws[di3];
                        if (!q->has_state || q->color_idx != ti || q->va_stride == 0u) continue;
                        if (q->va_slot[3] < 0 || q->va_slot[7] < 0) continue;
                        shown_ch++;
                        const float* c = (const float*)(s_draw.vc_mapped + q->vc_byte_offset);
                        const float* vx = s_draw.va_mapped + (size_t)q->va_base * 4u; /* va_base counts float4 */
                        const float* p0 = q->va_slot[0] >= 0 ? vx + q->va_slot[0] * 4 : NULL;
                        const float* p3 = vx + q->va_slot[3] * 4;
                        const float* p7 = vx + q->va_slot[7] * 4;
                        const float* p8 = q->va_slot[8] >= 0 ? vx + q->va_slot[8] * 4 : NULL;
                        /* c457.z and c464.w clamp the per-vertex colour the material FP
                         * multiplies by: tc1.w = min(c464.w, c457.z) is the alpha that ends
                         * up in the scene target and drives the bloom's bright pass. */
                        fprintf(stderr, "[PASS]    char#%u fp=%08X c457=(%g,%g,%g,%g) c464=(%g,%g,%g,%g) "
                                "tc1_w=min(%g,%g)\n", shown_ch, q->fp_hash,
                                c[457 * 4], c[457 * 4 + 1], c[457 * 4 + 2], c[457 * 4 + 3],
                                c[464 * 4], c[464 * 4 + 1], c[464 * 4 + 2], c[464 * 4 + 3],
                                c[464 * 4 + 3], c[457 * 4 + 2]);
                        fprintf(stderr, "[PASS]    char#%u fp=%08X c465=(%g,%g,%g,%g) c466=(%g,%g,%g,%g) "
                                "c467=(%g,%g,%g,%g)\n", shown_ch, q->fp_hash,
                                c[465 * 4], c[465 * 4 + 1], c[465 * 4 + 2], c[465 * 4 + 3],
                                c[466 * 4], c[466 * 4 + 1], c[466 * 4 + 2], c[466 * 4 + 3],
                                c[467 * 4], c[467 * 4 + 1], c[467 * 4 + 2], c[467 * 4 + 3]);
                        fprintf(stderr, "[PASS]    char#%u v0=(%g,%g,%g,%g) v3=(%g,%g,%g,%g) "
                                "v7=(%g,%g,%g,%g) v8=(%g,%g)\n", shown_ch,
                                p0 ? p0[0] : 0.0f, p0 ? p0[1] : 0.0f, p0 ? p0[2] : 0.0f, p0 ? p0[3] : 0.0f,
                                p3[0], p3[1], p3[2], p3[3], p7[0], p7[1], p7[2], p7[3],
                                p8 ? p8[0] : 0.0f, p8 ? p8[1] : 0.0f);
                    }
                }
                if (pl_first[pp] >= 0 && pl_draws[pp] <= 2u) {
                    /* Post-chain geometry (2026-09-16: the 76x42 bloom stage comes out
                     * shrunk ~0.8 toward the top-left with clamped edges): viewport
                     * rect, guest VIEWPORT_SCALE/OFFSET, surface clip, source texture
                     * size and the quad's v0/v8 per vertex. */
                    const MetalDrawRec* fd = &s_draw.draws[pl_first[pp]];
                    fprintf(stderr, "[PASS]    geo vp=(%g,%g,%g,%g) sc=(%u,%u,%u,%u) so_seen=%u "
                            "scale=(%g,%g,%g,%g) offset=(%g,%g,%g,%g) clip=%ux%u tex0=%lux%lu n=%u\n",
                            fd->vp[0], fd->vp[1], fd->vp[2], fd->vp[3],
                            fd->sc[0], fd->sc[1], fd->sc[2], fd->sc[3], fd->vso_seen,
                            fd->vso[0], fd->vso[1], fd->vso[2], fd->vso[3],
                            fd->vso[4], fd->vso[5], fd->vso[6], fd->vso[7],
                            fd->clip_wh[0], fd->clip_wh[1],
                            fd->tex[0] ? (unsigned long)fd->tex[0].width : 0ul,
                            fd->tex[0] ? (unsigned long)fd->tex[0].height : 0ul, fd->vertex_count);
                    if (fd->va_stride && s_draw.va_mapped) {
                        for (u32 vi = 0; vi < fd->vertex_count && vi < 4u; vi++) {
                            const float* vb = s_draw.va_mapped + ((size_t)fd->va_base + (size_t)vi * fd->va_stride) * 4u;
                            const float* p0 = fd->va_slot[0] >= 0 ? vb + fd->va_slot[0] * 4 : NULL;
                            const float* p8 = fd->va_slot[8] >= 0 ? vb + fd->va_slot[8] * 4 : NULL;
                            fprintf(stderr, "[PASS]       v%u pos=(%g,%g,%g,%g) uv=(%g,%g,%g,%g)\n", vi,
                                    p0 ? p0[0] : 0.0f, p0 ? p0[1] : 0.0f, p0 ? p0[2] : 0.0f, p0 ? p0[3] : 0.0f,
                                    p8 ? p8[0] : 0.0f, p8 ? p8[1] : 0.0f, p8 ? p8[2] : 0.0f, p8 ? p8[3] : 0.0f);
                        }
                    }
                    if (s_draw.vc_mapped) {
                        const float* c = (const float*)(s_draw.vc_mapped + fd->vc_byte_offset);
                        fprintf(stderr, "[PASS]       c465=(%g,%g,%g,%g) c466=(%g,%g,%g,%g) c467=(%g,%g,%g,%g)\n",
                                c[465 * 4], c[465 * 4 + 1], c[465 * 4 + 2], c[465 * 4 + 3],
                                c[466 * 4], c[466 * 4 + 1], c[466 * 4 + 2], c[466 * 4 + 3],
                                c[467 * 4], c[467 * 4 + 1], c[467 * 4 + 2], c[467 * 4 + 3]);
                    }
                }
                /* The first draw's FP constants (rsx_fc[0..2]): the bloom
                 * bright-pass 654864DA scales by fc0.y - fc0.x and adds fc1.x. */
                if (pl_first[pp] >= 0 && s_draw.fc_mapped) {
                    const MetalDrawRec* fd = &s_draw.draws[pl_first[pp]];
                    const float* k = s_draw.fc_mapped + fd->fc_byte_offset / 4u;
                    fprintf(stderr, "[PASS]    fc_count=%u fc0=(%g,%g,%g,%g) fc1=(%g,%g,%g,%g) "
                            "fc2=(%g,%g,%g,%g)\n", fd->fc_count, k[0], k[1], k[2], k[3],
                            k[4], k[5], k[6], k[7], k[8], k[9], k[10], k[11]);
                }
            }
        }
    }

    if (metal_perdraw_trace()) {
        static u32 n = 0, last_nsurf = 0;
        if (s_n_surfaces != last_nsurf) {
            last_nsurf = s_n_surfaces;
            for (u32 k = 0; k < s_n_surfaces; k++)
                fprintf(stderr, "[PERDRAW] surf[%u] %s off=0x%08X %ux%u\n", k,
                        s_surfaces[k].is_depth ? "depth" : "color", s_surfaces[k].offset,
                        s_surfaces[k].w, s_surfaces[k].h);
        }
        if (n++ < 40 || (s_state.frame_count % 500ull) == 0ull) {
            fprintf(stderr, "[PERDRAW] f=%llu recs=%u passes=%u draws=%u clears=%u blits=%u "
                    "skip=%u dropped=%u feedback=%llu show=%d%s disp_off=0x%08X\n",
                    (unsigned long long)s_state.frame_count, s_draw.draw_count, n_pass,
                    n_draw, n_clear, n_blit, n_skip, s_draw_dropped,
                    (unsigned long long)s_feedback_draws, show,
                    by_last ? "(last)" : "", disp_off);
            char pb[256];
            int pw = 0;
            pb[0] = 0;
            for (u32 p = 0; p < n_pass && p < 16u && pw < (int)sizeof(pb) - 16; p++)
                pw += snprintf(pb + pw, sizeof(pb) - (size_t)pw, " S%d:%u", pass_tgt[p],
                               pass_draws[p]);
            char tb[64];
            int tbw = 0;
            tb[0] = 0;
            if (last_pass_first >= 0) {
                const MetalDrawRec* fd = &s_draw.draws[last_pass_first];
                for (int ti = 0; ti < 4 && tbw < (int)sizeof(tb) - 8; ti++) {
                    int sk = -1;
                    for (u32 k = 0; fd->tex[ti] && k < s_n_surfaces; k++)
                        if (s_surfaces[k].tex == fd->tex[ti]) { sk = (int)k; break; }
                    if (sk >= 0)
                        tbw += snprintf(tb + tbw, sizeof(tb) - (size_t)tbw, " S%d", sk);
                    else
                        tbw += snprintf(tb + tbw, sizeof(tb) - (size_t)tbw, "%s",
                                        fd->tex[ti] ? " T" : " -");
                }
            }
            fprintf(stderr, "[PERDRAW]   passes(tgt:draws)%s | last-pass tex0-3:%s\n", pb, tb);
            /* The frame's first real draw: where it lands and what it feeds the VP. */
            for (u32 qi = 0; qi < s_draw.draw_count && qi < METAL_MAX_DRAWS; qi++) {
                const MetalDrawRec* q = &s_draw.draws[qi];
                if (!q->has_state || q->clear_flags || q->is_blit ||
                    q->prim == (MTLPrimitiveType)0xFFFFFFFFu)
                    continue;
                const MetalBasicVertex* v0 = s_draw.vb_mapped
                    ? (const MetalBasicVertex*)((const u8*)s_draw.vb_mapped + q->vb_byte_offset)
                    : NULL;
                const float* c0 = s_draw.vc_mapped
                    ? (const float*)(s_draw.vc_mapped + q->vc_byte_offset) : NULL;
                fprintf(stderr, "[PERDRAW]   first draw: tgt=S%d vp=(%.0f,%.0f %.0fx%.0f)%s "
                        "sc=(%u,%u %ux%u) n=%u idx=%d pso=%d v0=(%.3g,%.3g,%.3g) "
                        "c0=(%.3g,%.3g,%.3g,%.3g)\n",
                        q->color_idx, q->vp[0], q->vp[1], q->vp[2], q->vp[3],
                        q->has_vp ? "" : "(none)", q->sc[0], q->sc[1], q->sc[2], q->sc[3],
                        q->vertex_count, q->is_indexed, q->pso ? 1 : 0,
                        v0 ? v0->x : 0.0f, v0 ? v0->y : 0.0f, v0 ? v0->z : 0.0f,
                        c0 ? c0[0] : 0.0f, c0 ? c0[1] : 0.0f, c0 ? c0[2] : 0.0f,
                        c0 ? c0[3] : 0.0f);
                /* Which of the 512 VP constants this draw's snapshot actually holds
                 * (E449: with a constant-magenta FS and no depth test, nothing of the
                 * 3D scene rasterizes -- only NDC fullscreen quads do). */
                if (c0) {
                    int nzc = 0, ciw = 0;
                    char ci[128];
                    ci[0] = 0;
                    for (u32 k = 0; k < RSX_MAX_VERTEX_CONSTANTS; k++) {
                        const float* cv = c0 + k * 4u;
                        if (cv[0] != 0.0f || cv[1] != 0.0f || cv[2] != 0.0f || cv[3] != 0.0f) {
                            nzc++;
                            if (ciw < (int)sizeof(ci) - 8)
                                ciw += snprintf(ci + ciw, sizeof(ci) - (size_t)ciw, " %u", k);
                        }
                    }
                    fprintf(stderr, "[PERDRAW]   first draw consts: nonzero=%d/%u idx:%s\n", nzc,
                            (unsigned)RSX_MAX_VERTEX_CONSTANTS, ci);
                    /* c[256..263] -- the game's transform rows/columns (E451/E453:
                     * the post passes rasterize, the scene target stays empty even
                     * with a magenta FS and no depth test) -- to compute where v0
                     * lands in clip space. */
                    for (u32 k = 256; k < 264 && k < RSX_MAX_VERTEX_CONSTANTS; k++) {
                        const float* cv = c0 + k * 4u;
                        fprintf(stderr, "[PERDRAW]   c[%u]=(%.6g,%.6g,%.6g,%.6g)\n", k, cv[0], cv[1],
                                cv[2], cv[3]);
                    }
                    /* c[456..467]: the scene VS copies c[467] into TEX1, and 114 of
                     * the 191 texturing FPs output tex0 * TEX1, premultiplied by its
                     * alpha (E462/E463) -- a zero here is a black scene. */
                    for (u32 k = 456; k < 468 && k < RSX_MAX_VERTEX_CONSTANTS; k++) {
                        const float* cv = c0 + k * 4u;
                        fprintf(stderr, "[PERDRAW]   c[%u]=(%.6g,%.6g,%.6g,%.6g)\n", k, cv[0], cv[1],
                                cv[2], cv[3]);
                    }
                }
                break;
            }
        }
    }
}

/* PS3_FRAME_DUMP_GAMEPLAY=<frames> (OFF, diagnostic): from the first frame with
 * >= PS3_METAL_SURF_DUMP_MINREC records (default 800, the 3D scene), every <frames>
 * frames write the presented image (frame_gp<k>_f<n>.bmp) and the 1216-wide colour
 * surfaces (frame_gp<k>_s<i>.bmp), at most PS3_FRAME_DUMP_GAMEPLAY_MAX (default 6).
 * Decided before the replay so the PASS log (PS3_TRACE_METAL_PASSES) lists the draws
 * of the very frame shot -- the geometry garbage is intermittent (2026-09-16). */
static int s_gp_every = -1, s_gp_max = 6, s_gp_k = 0, s_gp_shoot_now = 0;
static u32 s_gp_minrec = 800u;
/* PS3_METAL_FEEDBACK_SHOT=1 (diagnostic, OFF): a frame with a framebuffer-feedback draw
 * asks for a gameplay shot and the PASS log, so the same frame can be compared with and
 * without PS3_METAL_FEEDBACK_COPY. Needs PS3_FRAME_DUMP_GAMEPLAY set for the shot path. */
static void metal_feedback_shot_request(void)
{
    if (!metal_env_on("PS3_METAL_FEEDBACK_SHOT") || s_gp_k >= s_gp_max) return;
    if (s_draw.draw_count < s_gp_minrec) return;   /* gameplay frames only, not the load fade */
    s_gp_shoot_now = 1;
    s_surf_dump_at = (long long)s_state.frame_count;
}
static long long s_gp_next = -1;
static void metal_gp_decide(void)
{
    s_gp_shoot_now = 0;
    if (s_gp_every < 0) {
        const char* e = getenv("PS3_FRAME_DUMP_GAMEPLAY");
        errno = 0;
        long ev = (e && *e) ? strtol(e, NULL, 10) : 0L;
        if (errno == ERANGE) ev = 0L;
        s_gp_every = (ev >= 1L && ev <= 100000L) ? (int)ev : 0;
        const char* m = getenv("PS3_FRAME_DUMP_GAMEPLAY_MAX");
        errno = 0;
        long mx = (m && *m) ? strtol(m, NULL, 10) : 0L;
        if (errno == ERANGE) mx = 0L;
        if (mx >= 1L && mx <= 100L) s_gp_max = (int)mx;
        const char* r = getenv("PS3_METAL_SURF_DUMP_MINREC");
        if (r && *r) {
            errno = 0;
            unsigned long rv = strtoul(r, NULL, 10);
            if (errno != ERANGE && rv >= 1ul && rv <= (unsigned long)METAL_MAX_DRAWS) s_gp_minrec = (u32)rv;
        }
    }
    if (s_pick_on < 0) s_pick_on = metal_env_on("PS3_METAL_PICK");
    if (s_pick_on && s_pick_dump_ready) {   /* textures of the draws the last pick saw */
        s_pick_dump_ready = 0;
        static u32 pk = 0;
        for (u32 k = 0; k < METAL_MAX_DRAWS; k++)
            for (int u = 0; u < 4; u++) {
                id<MTLTexture> t = s_pick_tex[k][u];
                if (!t) continue;
                if (t.pixelFormat == MTLPixelFormatBGRA8Unorm) {
                    char tp[80];
                    snprintf(tp, sizeof(tp), "pick%u_d%u_t%d_%lux%lu.bmp", pk, k, u,
                             (unsigned long)t.width, (unsigned long)t.height);
                    metal_dump_texture_to(t, tp);
                    fprintf(stderr, "[PICK] textura %s\n", tp);
                }
                CFRelease((__bridge CFTypeRef)t);
                s_pick_tex[k][u] = nil;
            }
        pk++;
        fflush(stderr);
    }
    if (s_pick_on) {   /* PS3_METAL_PICK: F9 = shoot this frame, pick on the next */
        if (s_pick_phase == 1) s_pick_phase = 2;
        const Uint8* ks = SDL_GetKeyboardState(NULL);
        const int down = ks && ks[SDL_SCANCODE_F9];
        /* PS3_METAL_PICK_AUTO=<frame>,<u>,<v>: the same pick without a key, at a
         * normalised point of the image (to test the tool headless). */
        static long long auto_f = -2;
        static float auto_u, auto_v;
        if (auto_f == -2) {
            const char* e = getenv("PS3_METAL_PICK_AUTO");
            long long f = -1; float u = -1.0f, v = -1.0f;
            auto_f = (e && sscanf(e, "%lld,%f,%f", &f, &u, &v) == 3 && f >= 0) ? f : -1;
            auto_u = u; auto_v = v;
        }
        if (auto_f >= 0 && (long long)s_state.frame_count >= auto_f && s_pick_phase == 0) {
            auto_f = -1;
            s_pick_mx = auto_u; s_pick_my = auto_v; s_pick_norm = 1;
            s_pick_phase = 1;
            if (s_gp_max < 99) s_gp_max = 99;
            if (s_gp_k < s_gp_max) { s_gp_shoot_now = 1; s_surf_dump_at = (long long)s_state.frame_count; }
            fprintf(stderr, "[PICK] auto f=%llu u=%.4f v=%.4f shot=gp%d\n",
                    (unsigned long long)s_state.frame_count, auto_u, auto_v, s_gp_k);
        }
        if (down && !s_pick_key_was && s_pick_phase == 0 && s_state.window) {
            int mx = 0, my = 0, ww = 0, wh = 0, dw = 0, dh = 0;
            SDL_GetMouseState(&mx, &my);
            SDL_GetWindowSize(s_state.window, &ww, &wh);
            SDL_Metal_GetDrawableSize(s_state.window, &dw, &dh);
            if (ww > 0 && wh > 0) {
                s_pick_mx = (float)mx * (float)dw / (float)ww;
                s_pick_my = (float)my * (float)dh / (float)wh;
                s_pick_norm = 0;
                s_pick_phase = 1;
                if (s_gp_max < 99) s_gp_max = 99;
                if (s_gp_k < s_gp_max) {
                    s_gp_shoot_now = 1;
                    s_surf_dump_at = (long long)s_state.frame_count;
                }
                fprintf(stderr, "[PICK] F9 f=%llu mouse=(%d,%d) janela=%dx%d drawable=%dx%d shot=gp%d\n",
                        (unsigned long long)s_state.frame_count, mx, my, ww, wh, dw, dh, s_gp_k);
                fflush(stderr);
            }
        }
        s_pick_key_was = down;
        const int f10 = ks && ks[SDL_SCANCODE_F10];
        if (f10 && !s_f10_was && s_pick_nvps > 0) {
            s_live_skip_i = (s_live_skip_i + 1) % (s_pick_nvps + 1);
            s_live_skip_vp = s_live_skip_i ? s_pick_vps[s_live_skip_i - 1] : 0u;
            fprintf(stderr, "[PICK] F10 -> %s %08X (%d/%d)\n", s_live_skip_vp ? "sem VP" : "tudo ligado",
                    s_live_skip_vp, s_live_skip_i, s_pick_nvps);
            fflush(stderr);
        }
        s_f10_was = f10;
        const int f11 = ks && ks[SDL_SCANCODE_F8];
        if (f11 && !s_f11_was) {
            s_live_zalways = !s_live_zalways;
            fprintf(stderr, "[PICK] F8 -> bronze Z-ALWAYS %s\n", s_live_zalways ? "LIGADO" : "desligado");
            fflush(stderr);
        }
        s_f11_was = f11;
        const int f7 = ks && ks[SDL_SCANCODE_F7];
        if (f7 && !s_f7_was) {
            s_live_zbias = !s_live_zbias;
            fprintf(stderr, "[PICK] F7 -> bronze depth bias %s\n", s_live_zbias ? "LIGADO" : "desligado");
            fflush(stderr);
        }
        s_f7_was = f7;
    }
    if (s_gp_every <= 0 || s_gp_k >= s_gp_max) return;
    const long long fc = (long long)s_state.frame_count;
    if (s_gp_next < 0 && s_draw.draw_count >= s_gp_minrec) s_gp_next = fc + s_gp_every;
    if (s_gp_next >= 0 && fc >= s_gp_next) {
        s_gp_next = fc + s_gp_every;
        s_gp_shoot_now = 1;
        s_surf_dump_at = fc;   /* PASS log for this frame */
    }
}

static void metal_present_impl(void* ud, u32 buffer_id);

/* Host CPU time of each present (replay encode + submit), for PS3_TRACE_FPS. */
static void metal_present(void* ud, u32 buffer_id)
{
    static mach_timebase_info_data_t tb;
    if (tb.denom == 0 && mach_timebase_info(&tb) != KERN_SUCCESS) { tb.numer = 1; tb.denom = 1; }
    const uint64_t t0 = mach_absolute_time();
    metal_present_impl(ud, buffer_id);
    const uint64_t t1 = mach_absolute_time();
    const double ms = (double)(t1 - t0) * (double)tb.numer / (double)tb.denom / 1.0e6;
    os_unfair_lock_lock(&s_perf_lock);
    s_perf_present_ms_sum += ms;
    if (ms > s_perf_present_ms_max) s_perf_present_ms_max = ms;
    os_unfair_lock_unlock(&s_perf_lock);
}

static void metal_present_impl(void* ud, u32 buffer_id)
{
    (void)ud;
    (void)buffer_id;

    if (!s_state.layer || !s_state.queue) {
        return;
    }

    /* H-M1: when decode thread owns movie presents, guest GCM flips must not
     * call nextDrawable (dual-present → pure black dumps). Staging still
     * updates; decode's present path composites. */
    if (!movie_present_on_guest()) {
        int hle_movie = 0;
        movie_mu_init_once();
        pthread_mutex_lock(&s_movie.mu);
        hle_movie = s_movie.active && s_movie.hle_source &&
                    (s_movie.use_cv || s_movie.staging != NULL);
        pthread_mutex_unlock(&s_movie.mu);
        /* ud==NULL is the decode-thread call from rsx_metal_movie_present. */
        if (hle_movie && ud != NULL)
            return;
    }

    @autoreleasepool {
        id<CAMetalDrawable> drawable = [s_state.layer nextDrawable];
        if (!drawable) {
            /* The layer hands out a small pool of drawables; a miss means the
             * previous frames are still in flight. Dropping the frame is the
             * right call -- blocking here would stall the guest. */
            if (metal_trace_flip_on()) {
                fprintf(stderr,
                        "[FLIP] f=%llu DRAWABLE_MISS present=NO draws=%u tags=DRAWABLE_MISS\n",
                        (unsigned long long)s_state.frame_count, s_draw.draw_count);
                fflush(stderr);
            }
            return;
        }

        content_hold_demo_maybe();
        /* Advance HOSTRES company-logo queue after intro (scep_e / bluepoint). */
        rsx_host_boot_logo_tick();

        /* Optional synthetic draws: M1 color triangle or M3 textured checkerboard. */
        {
            static int demo = -1, tex_demo = -1;
            if (demo < 0) {
                const char* e = getenv("PS3_METAL_DEMO_DRAW");
                demo = (e && e[0] && e[0] != '0') ? 1 : 0;
            }
            if (tex_demo < 0) {
                const char* e = getenv("PS3_METAL_TEX_DEMO");
                tex_demo = (e && e[0] && e[0] != '0') ? 1 : 0;
            }
            if (s_draw.draw_count == 0) {
                if (tex_demo)
                    metal_record_tex_demo_triangle();
                else if (demo)
                    metal_record_demo_triangle();
            }
        }

        /* Priority: movie HLE > content hold > guest RT (M0/M4/M8/M7). */
        int movie_on = 0;
        int content_on = 0;
        id<MTLTexture> movie_sample_tex = nil;
        id<MTLTexture> content_sample_tex = nil;
        CVPixelBufferRef hold_cv_buf = NULL;
        CVMetalTextureRef hold_cv_mtl = NULL;
        movie_mu_init_once();
        pthread_mutex_lock(&s_movie.mu);
        if (s_movie.active) {
            if (s_movie.use_cv && s_movie.cv_mtl) {
                movie_sample_tex = CVMetalTextureGetTexture(s_movie.cv_mtl);
                if (movie_sample_tex && movie_ensure_pso()) {
                    movie_on = 1;
                    hold_cv_mtl = s_movie.cv_mtl;
                    hold_cv_buf = s_movie.cv_buf;
                    if (hold_cv_mtl) CFRetain(hold_cv_mtl);
                    if (hold_cv_buf) CFRetain(hold_cv_buf);
                }
            } else if (s_movie.staging) {
                movie_upload_locked();
                if (s_movie.tex && movie_ensure_pso()) {
                    movie_on = 1;
                    movie_sample_tex = s_movie.tex;
                    /* H-M1: log staging richness; one-shot dump when content. */
                    {
                        static int gp = 0;
                        static int dumped_rich = 0;
                        u32 st_nb = 0, st_mx = 0;
                        if (s_movie.staging) {
                            u32 n = s_movie.w * s_movie.h;
                            const u8* p = s_movie.staging;
                            for (u32 i = 0; i < n; i += 256) {
                                const u8* q = p + i * 4;
                                if (q[2] > st_mx) st_mx = q[2];
                                if (q[0] > 8 || q[1] > 8 || q[2] > 8)
                                    st_nb++;
                            }
                        }
                        if (metal_tl_on() &&
                            (gp < 12 || (gp % 15) == 0 || st_mx > 20)) {
                            fprintf(stderr,
                                    "[movie-metal] guest_present #%d frames=%llu "
                                    "stage_nb~%u maxR=%u\n",
                                    gp, (unsigned long long)s_movie.frames_rgba,
                                    st_nb, st_mx);
                            fflush(stderr);
                        }
                        /* One-shot dumps only when frame dump / timeline is on. */
                        if (s_state.dump_frames || metal_tl_on()) {
                            if (!dumped_rich && st_mx > 40) {
                                dumped_rich = 1;
                                s_state.dump_frames = 1;
                                s_state.force_dump_once = 1;
                                fprintf(stderr,
                                        "[movie-metal] RICH_FRAME force dump "
                                        "frames=%llu maxR=%u\n",
                                        (unsigned long long)s_movie.frames_rgba,
                                        st_mx);
                                fflush(stderr);
                            } else if (dumped_rich == 1 && st_mx > 200) {
                                dumped_rich = 2;
                                s_state.force_dump_once = 1;
                                fprintf(stderr,
                                        "[movie-metal] RICH_FRAME2 force dump "
                                        "frames=%llu maxR=%u\n",
                                        (unsigned long long)s_movie.frames_rgba,
                                        st_mx);
                                fflush(stderr);
                            } else if (dumped_rich >= 2 && !s_state.force_dump_once) {
                                /* Avoid flooding disk after the two rich captures
                                 * unless user set PS3_FRAME_DUMP explicitly. */
                                if (!rsx_frame_dump_enabled())
                                    s_state.dump_frames = 0;
                            }
                        }
                        gp++;
                    }
                }
            }
        }
        pthread_mutex_unlock(&s_movie.mu);

        if (!movie_on && s_content.hold && !s_content.paused && content_ensure_tex() &&
            s_content.tex && movie_ensure_pso()) {
            content_on = 1;
            content_sample_tex = s_content.tex;
        }

        {
            static int tl_mode = 0;
            if (metal_tl_on() && (tl_mode++ < 48 || movie_on ||
                                  (s_state.frame_count % 50ull) == 0ull)) {
                METAL_TL("present_mode movie=%d content=%d hold=%d paused=%d "
                         "active=%d use_cv=%d draws=%u",
                         movie_on, content_on, s_content.hold, s_content.paused,
                         s_movie.active, s_movie.use_cv, s_draw.draw_count);
            }
        }

        id<MTLCommandBuffer> cb = [s_state.queue commandBuffer];
        rmd_queue_encode(&s_rmd, &s_rmd_q, cb);
        [cb addCompletedHandler:^(id<MTLCommandBuffer> done) {   /* GPU time for PS3_TRACE_FPS */
            const double ms = (done.GPUEndTime - done.GPUStartTime) * 1000.0;
            if (ms > 0.0 && ms < 10000.0) {   /* the handler runs on a Metal thread */
                os_unfair_lock_lock(&s_perf_lock);
                s_perf_gpu_ms_last = ms;
                os_unfair_lock_unlock(&s_perf_lock);
            }
        }];
        u32 dw = (u32)drawable.texture.width;
        u32 dh = (u32)drawable.texture.height;
        /* Rect de apresentacao com o aspect do guest (16:9) -> barras quando o
         * drawable tem outro aspect (fullscreen 16:10). No-op em janela 2x. */
        MetalRect lb = metal_letterbox(dw, dh,
                                       s_state.width ? s_state.width : 1280u,
                                       s_state.height ? s_state.height : 720u);

        if (movie_on || content_on) {
            /* Fullscreen overlay onto swapchain (movie or sticky hold). */
            id<MTLTexture> overlay =
                movie_on ? movie_sample_tex : content_sample_tex;

            /* MetalFX no content-hold (logos / legal screen): upscale espacial
             * edge-aware do texto guest-res -> drawable retina, em vez do sample
             * bilinear do movie PSO. So' para o hold (fonte BGRA com ShaderRead,
             * orientacao top-left igual a' do movie PSO); o filme ao vivo (VT)
             * fica no sample. Gated por PS3_METALFX. */
            rmu_ctx* mfx = metal_metalfx();
            id<MTLTexture> mfx_out = nil;
            if (mfx && content_on && !movie_on && overlay &&
                overlay.width < lb.w) {
                mfx_out = rmu_prepare(mfx, overlay.pixelFormat,
                                      (u32)overlay.width, (u32)overlay.height,
                                      lb.w, lb.h);
                if (mfx_out) {
                    rmu_encode(mfx, cb, overlay);
                    static int mfx_ov_log = 0;
                    if (mfx_ov_log++ < 3)
                        fprintf(stderr,
                                "[RSX metal] MetalFX overlay upscale %lux%lu -> %ux%u "
                                "(letterbox @%u,%u de %ux%u)\n",
                                (unsigned long)overlay.width,
                                (unsigned long)overlay.height,
                                lb.w, lb.h, lb.x, lb.y, dw, dh);
                    /* Barras pretas quando o letterbox nao cobre o drawable. */
                    if (lb.w != dw || lb.h != dh) {
                        MTLRenderPassDescriptor* clr = [MTLRenderPassDescriptor renderPassDescriptor];
                        clr.colorAttachments[0].texture     = drawable.texture;
                        clr.colorAttachments[0].loadAction  = MTLLoadActionClear;
                        clr.colorAttachments[0].storeAction = MTLStoreActionStore;
                        clr.colorAttachments[0].clearColor  = MTLClearColorMake(0, 0, 0, 1);
                        id<MTLRenderCommandEncoder> ce =
                            [cb renderCommandEncoderWithDescriptor:clr];
                        [ce endEncoding];
                    }
                    id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
                    [blit copyFromTexture:mfx_out
                              sourceSlice:0 sourceLevel:0
                             sourceOrigin:MTLOriginMake(0, 0, 0)
                               sourceSize:MTLSizeMake(mfx_out.width, mfx_out.height, 1)
                                toTexture:drawable.texture
                         destinationSlice:0 destinationLevel:0
                        destinationOrigin:MTLOriginMake(lb.x, lb.y, 0)];
                    [blit endEncoding];
                }
            }

            int drew_ov = mfx_out ? 1 : 0;
            if (!mfx_out) {
                MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
                pass.colorAttachments[0].texture     = drawable.texture;
                pass.colorAttachments[0].loadAction  = MTLLoadActionClear;
                pass.colorAttachments[0].storeAction = MTLStoreActionStore;
                pass.colorAttachments[0].clearColor  = MTLClearColorMake(0, 0, 0, 1);
                id<MTLRenderCommandEncoder> enc =
                    [cb renderCommandEncoderWithDescriptor:pass];
                if (s_movie.pso && overlay && s_movie.sampler) {
                    /* Viewport = rect letterbox: o clear pinta as barras, o
                     * triangulo fullscreen so' preenche o rect com o aspect
                     * correto (no-op quando lb == drawable). */
                    if (lb.w != dw || lb.h != dh) {
                        MTLViewport vp = { (double)lb.x, (double)lb.y,
                                           (double)lb.w, (double)lb.h, 0.0, 1.0 };
                        [enc setViewport:vp];
                    }
                    [enc setRenderPipelineState:s_movie.pso];
                    [enc setFragmentTexture:overlay atIndex:0];
                    [enc setFragmentSamplerState:s_movie.sampler atIndex:0];
                    [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
                    drew_ov = 1;
                }
                [enc endEncoding];
            }
            {
                static int tl_ov = 0;
                if (metal_tl_on() && (tl_ov++ < 32 || !drew_ov))
                    METAL_TL("overlay_draw movie=%d content=%d drew=%d pso=%d tex=%d",
                             movie_on, content_on, drew_ov,
                             s_movie.pso ? 1 : 0, overlay ? 1 : 0);
            }
            if (metal_trace_flip_on()) {
                fprintf(stderr,
                        "[FLIP] f=%llu overlay movie=%d content=%d drew=%d "
                        "drawableWritten=%s present=YES drawable=%ux%u tags=%s\n",
                        (unsigned long long)s_state.frame_count, movie_on, content_on,
                        drew_ov, drew_ov ? "YES" : "NO", dw, dh,
                        drew_ov ? "OVERLAY" : "PRESENT_WITHOUT_WRITE|OVERLAY");
                fflush(stderr);
            }
        } else if (metal_per_draw_rt()) {
            metal_gp_decide();
            metal_present_per_draw(cb, drawable.texture, lb, buffer_id);
        } else {
            /* M7: render guest/demo draws into offscreen color RT, then blit to drawable. */
            {
                static int glog = 0;
                if (glog++ < 12 || (s_state.frame_count % 100ull) == 0ull)
                    fprintf(stderr,
                            "[RSX metal] guest_present f=%llu draws=%u has_vb=%d "
                            "has_ib=%d dw=%u dh=%u\n",
                            (unsigned long long)s_state.frame_count,
                            s_draw.draw_count, s_draw.vb ? 1 : 0,
                            s_draw.ib ? 1 : 0, dw, dh);
            }
            MetalSurface* crt = metal_color_rt();
            int have_crt = crt && crt->tex;
            int size_mismatch = have_crt &&
                (crt->tex.width != drawable.texture.width ||
                 crt->tex.height != drawable.texture.height);

            /* MetalFX (Port Apple, servico de HOST): quando ligado e ha' um
             * color RT guest MENOR que o drawable (ecra retina/4K), renderiza a
             * resolucao NATIVA do jogo no RT e faz upscale espacial no present,
             * em vez de rasterizar direto a resolucao do ecra. Gated OFF por
             * default; degrada para o blit/sample se MetalFX indisponivel. */
            rmu_ctx* mfx = metal_metalfx();
            int use_mfx = mfx && have_crt && size_mismatch &&
                          crt->tex.width < drawable.texture.width;

            /* Prefer drawing directly to the drawable when sizes differ (retina
             * 2x). Intermediate 1280x720 RT + sample-blit used movie PSO and
             * often left the drawable black after content-hold (diag: solid
             * green FS still pure black). Direct drawable present is honest.
             * With MetalFX on we DO render into the guest-res RT (crt) to feed
             * the spatial scaler. */
            id<MTLTexture> color_tex =
                use_mfx ? crt->tex
                        : ((size_mismatch || !have_crt) ? drawable.texture
                                                        : crt->tex);
            u32 rtw = (color_tex == drawable.texture) ? dw
                      : (crt ? crt->w : dw);
            u32 rth = (color_tex == drawable.texture) ? dh
                      : (crt ? crt->h : dh);

            MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
            pass.colorAttachments[0].texture     = color_tex;
            int clear_c = (s_state.clear_flags &
                           (RSX_CLEAR_COLOR_R | RSX_CLEAR_COLOR_G |
                            RSX_CLEAR_COLOR_B | RSX_CLEAR_COLOR_A)) != 0;
            static int rt_color_inited = 0;
            pass.colorAttachments[0].loadAction  =
                (clear_c || !rt_color_inited || rsx_debug_clear_frame_on())
                    ? MTLLoadActionClear : MTLLoadActionLoad;
            pass.colorAttachments[0].storeAction = MTLStoreActionStore;
            pass.colorAttachments[0].clearColor  =
                rsx_debug_clear_frame_on()
                    ? MTLClearColorMake(0, 0, 0, 1)
                    : MTLClearColorMake(s_state.clear_r, s_state.clear_g, s_state.clear_b,
                                        s_state.clear_a > 0 ? s_state.clear_a : 1.0);
            rt_color_inited = 1;
            s_state.clear_flags &= (u32)~(RSX_CLEAR_COLOR_R | RSX_CLEAR_COLOR_G |
                                          RSX_CLEAR_COLOR_B | RSX_CLEAR_COLOR_A);

            MetalSurface* drt = metal_depth_rt_opt();
            id<MTLTexture> depth_tex = nil;
            if (drt && drt->tex) {
                depth_tex = drt->tex;
            } else if (s_draw.draw_count > 0 && metal_ensure_depth_tex(rtw, rth)) {
                depth_tex = s_depth_tex;
            }
            if (depth_tex) {
                static int depth_inited = 0;
                int clear_d = (s_state.clear_flags & RSX_CLEAR_DEPTH) != 0 || !depth_inited;
                int clear_s = (s_state.clear_flags & RSX_CLEAR_STENCIL) != 0 || !depth_inited;
                pass.depthAttachment.texture     = depth_tex;
                pass.depthAttachment.loadAction  = clear_d ? MTLLoadActionClear : MTLLoadActionLoad;
                pass.depthAttachment.storeAction = MTLStoreActionStore;
                pass.depthAttachment.clearDepth  = s_state.clear_depth;
                pass.stencilAttachment.texture     = depth_tex;
                pass.stencilAttachment.loadAction  =
                    clear_s ? MTLLoadActionClear : MTLLoadActionLoad;
                pass.stencilAttachment.storeAction = MTLStoreActionStore;
                pass.stencilAttachment.clearStencil = s_state.clear_stencil;
                s_state.clear_flags &= (u32)~(RSX_CLEAR_DEPTH | RSX_CLEAR_STENCIL);
                depth_inited = 1;
            }

            id<MTLRenderCommandEncoder> enc =
                [cb renderCommandEncoderWithDescriptor:pass];
            metal_set_encoder_blend_color(enc);

            if (s_draw.draw_count > 0 && s_draw.vb) {
                /* Guest viewport is typically 1280×720. When we render directly
                 * to a retina drawable (e.g. 2560×1440), scale so NDC fills
                 * the full window — otherwise only the top-left quarter paints. */
                double scale_x = 1.0, scale_y = 1.0;
                if (s_state.width > 0 && rtw > s_state.width)
                    scale_x = (double)rtw / (double)s_state.width;
                if (s_state.height > 0 && rth > s_state.height)
                    scale_y = (double)rth / (double)s_state.height;
                if (s_draw.has_vp) {
                    MTLViewport vp = {
                        s_draw.vp_x * scale_x, s_draw.vp_y * scale_y,
                        s_draw.vp_w * scale_x, s_draw.vp_h * scale_y,
                        0.0, 1.0
                    };
                    [enc setViewport:vp];
                } else {
                    MTLViewport vp = { 0, 0, (double)rtw, (double)rth, 0.0, 1.0 };
                    [enc setViewport:vp];
                }
                int sc_empty = 0;
                {
                    u32 sx = (u32)(s_pipe.scissor_x * scale_x);
                    u32 sy = (u32)(s_pipe.scissor_y * scale_y);
                    u32 sw = s_pipe.scissor_w
                        ? (u32)(s_pipe.scissor_w * scale_x) : rtw;
                    u32 sh = s_pipe.scissor_h
                        ? (u32)(s_pipe.scissor_h * scale_y) : rth;
                    if (sx >= rtw || sy >= rth) {
                        if (metal_scissor_fix()) { s_scissor_reject++; sc_empty = 1; }
                        else { if (sx >= rtw) sx = 0; if (sy >= rth) sy = 0; }
                    }
                    if (sx + sw > rtw) sw = rtw - sx;
                    if (sy + sh > rth) sh = rth - sy;
                    if (sw == 0 || sh == 0) {
                        /* Vazio e' vazio: inflar para o alvo inteiro pintava tudo. */
                        if (metal_scissor_fix()) { s_scissor_reject++; sc_empty = 1; }
                        if (sw == 0) sw = rtw;
                        if (sh == 0) sh = rth;
                    }
                    if (!sc_empty) {
                        MTLScissorRect sc = { sx, sy, sw, sh };
                        [enc setScissorRect:sc];
                    }
                }
                id<MTLDepthStencilState> dss = metal_depth_stencil_state();
                if (dss) {
                    [enc setDepthStencilState:dss];
                    if (s_pipe.stencil_test_enable)
                        [enc setStencilFrontReferenceValue:(uint32_t)(s_pipe.stencil_ref & 0xFF)
                                        backReferenceValue:(uint32_t)((s_pipe.two_sided_stencil
                                            ? s_pipe.back_stencil_ref : s_pipe.stencil_ref) & 0xFF)];
                }
                (void)metal_ensure_dummy_tex();
                (void)metal_ensure_vp_const_buf();
                for (int ti = 0; ti < (int)RSX_MAX_TEXTURES; ti++) {
                    id<MTLTexture> t = (s_draw.tex_slots[ti].valid && s_draw.tex_slots[ti].tex)
                        ? s_draw.tex_slots[ti].tex
                        : s_draw.dummy_tex;
                    id<MTLSamplerState> sm = s_draw.dummy_samp;
                    if (metal_sampler_real() && s_draw.tex_slots[ti].valid) {
                        id<MTLSamplerState> rs = metal_sampler_for(s_draw.tex_slots[ti].addr,
                                                                   s_draw.tex_slots[ti].filt);
                        if (rs) sm = rs;
                    }
                    if (t) [enc setFragmentTexture:t atIndex:ti];
                    if (sm) [enc setFragmentSamplerState:sm atIndex:ti];
                }
                if (s_draw.vp_const_buf)
                    [enc setVertexBuffer:s_draw.vp_const_buf offset:0 atIndex:1];
                [enc setVertexBuffer:s_draw.vb offset:0 atIndex:0];
                for (u32 i = 0; i < s_draw.draw_count; i++) {
                    MetalDrawRec* dr = &s_draw.draws[i];
                    if (dr->prim == (MTLPrimitiveType)0xFFFFFFFFu) continue;
                    id<MTLRenderPipelineState> use =
                        dr->pso ? dr->pso : s_draw.pso;
                    if (!use) continue;
                    metal_apply_cull(enc, dr);
        [enc setRenderPipelineState:use];
                    if (s_draw.fc_buf)
                        [enc setFragmentBuffer:s_draw.fc_buf offset:dr->fc_byte_offset atIndex:0];
                    if (s_draw.vc_buf)
                        [enc setVertexBuffer:s_draw.vc_buf offset:dr->vc_byte_offset atIndex:1];
                    metal_bind_va(enc, dr);
                    if (sc_empty) {
                        /* scissor sem interseccao: o guest nao quer pixels aqui */
                    } else if (dr->is_indexed && s_draw.ib) {
                        [enc setVertexBufferOffset:dr->vb_byte_offset atIndex:0];
                        [enc drawIndexedPrimitives:dr->prim
                                        indexCount:dr->vertex_count
                                         indexType:MTLIndexTypeUInt32
                                       indexBuffer:s_draw.ib
                                 indexBufferOffset:dr->ib_byte_offset];
                    } else {
                        u32 start = dr->vb_byte_offset / (u32)sizeof(MetalBasicVertex);
                        [enc drawPrimitives:dr->prim
                                vertexStart:start
                                vertexCount:dr->vertex_count];
                    }
                }
            }
            [enc endEncoding];

            /* Upscale (MetalFX) / blit / sample color RT → swapchain drawable. */
            if (color_tex != drawable.texture && color_tex) {
                /* MetalFX: upscale guest-res RT → owned output, blit → drawable.
                 * Fica antes do sample bilinear: melhor qualidade que amostragem
                 * simples. Se rmu_prepare falhar (nao devia se available), cai
                 * para o caminho de sample abaixo. */
                id<MTLTexture> mfx_out = nil;
                if (use_mfx) {
                    mfx_out = rmu_prepare(mfx, color_tex.pixelFormat,
                                          (u32)color_tex.width, (u32)color_tex.height,
                                          lb.w, lb.h);
                    if (mfx_out) {
                        rmu_encode(mfx, cb, color_tex);
                        static int mfx_log = 0;
                        if (mfx_log++ < 3)
                            fprintf(stderr,
                                    "[RSX metal] MetalFX upscale %lux%lu -> %ux%u "
                                    "(letterbox @%u,%u de %ux%u)\n",
                                    (unsigned long)color_tex.width,
                                    (unsigned long)color_tex.height,
                                    lb.w, lb.h, lb.x, lb.y, dw, dh);
                    }
                }
                if (mfx_out) {
                    /* Barras pretas quando o letterbox nao cobre o drawable. */
                    if (lb.w != dw || lb.h != dh) {
                        MTLRenderPassDescriptor* clr = [MTLRenderPassDescriptor renderPassDescriptor];
                        clr.colorAttachments[0].texture     = drawable.texture;
                        clr.colorAttachments[0].loadAction  = MTLLoadActionClear;
                        clr.colorAttachments[0].storeAction = MTLStoreActionStore;
                        clr.colorAttachments[0].clearColor  = MTLClearColorMake(0, 0, 0, 1);
                        id<MTLRenderCommandEncoder> ce =
                            [cb renderCommandEncoderWithDescriptor:clr];
                        [ce endEncoding];
                    }
                    id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
                    [blit copyFromTexture:mfx_out
                              sourceSlice:0 sourceLevel:0
                             sourceOrigin:MTLOriginMake(0, 0, 0)
                               sourceSize:MTLSizeMake(mfx_out.width, mfx_out.height, 1)
                                toTexture:drawable.texture
                         destinationSlice:0 destinationLevel:0
                        destinationOrigin:MTLOriginMake(lb.x, lb.y, 0)];
                    [blit endEncoding];
                } else if (color_tex.width == drawable.texture.width &&
                    color_tex.height == drawable.texture.height) {
                    id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
                    [blit copyFromTexture:color_tex
                              sourceSlice:0 sourceLevel:0
                             sourceOrigin:MTLOriginMake(0, 0, 0)
                               sourceSize:MTLSizeMake(color_tex.width, color_tex.height, 1)
                                toTexture:drawable.texture
                         destinationSlice:0 destinationLevel:0
                        destinationOrigin:MTLOriginMake(0, 0, 0)];
                    [blit endEncoding];
                } else if (movie_ensure_pso() && s_movie.pso && s_movie.sampler) {
                    /* Scale via fullscreen sample (reuse movie PSO). Barras: com
                     * letterbox limpa a preto e restringe o viewport; senao
                     * DontCare (cobre tudo). */
                    int lbx = (lb.w != dw || lb.h != dh);
                    MTLRenderPassDescriptor* p2 = [MTLRenderPassDescriptor renderPassDescriptor];
                    p2.colorAttachments[0].texture     = drawable.texture;
                    p2.colorAttachments[0].loadAction  =
                        lbx ? MTLLoadActionClear : MTLLoadActionDontCare;
                    p2.colorAttachments[0].clearColor  = MTLClearColorMake(0, 0, 0, 1);
                    p2.colorAttachments[0].storeAction = MTLStoreActionStore;
                    id<MTLRenderCommandEncoder> e2 =
                        [cb renderCommandEncoderWithDescriptor:p2];
                    if (lbx) {
                        MTLViewport vp = { (double)lb.x, (double)lb.y,
                                           (double)lb.w, (double)lb.h, 0.0, 1.0 };
                        [e2 setViewport:vp];
                    }
                    [e2 setRenderPipelineState:s_movie.pso];
                    [e2 setFragmentTexture:color_tex atIndex:0];
                    [e2 setFragmentSamplerState:s_movie.sampler atIndex:0];
                    [e2 drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
                    [e2 endEncoding];
                }
            }
        }

        /* Runtime overlay UI: the last pass on the drawable this call
         * presents, after whichever layer was chosen above (movie, content
         * hold, per-draw RT or guest). One composite per present, same
         * command buffer, never a second nextDrawable. The UI is laid out at
         * the drawable's own size, so it covers the whole drawable
         * (letterbox bars included). No-op while hidden. */
        if (s_overlay_ui) {
            rsx_overlay_viewport ui_vp = { 0, 0, dw, dh };
            (void)rsx_overlay_metal_renderer_encode(s_overlay_ui, cb, drawable.texture, ui_vp);
        }

        if (hold_cv_mtl || hold_cv_buf) {
            CVMetalTextureRef mtl_rel = hold_cv_mtl;
            CVPixelBufferRef buf_rel = hold_cv_buf;
            [cb addCompletedHandler:^(id<MTLCommandBuffer> _Nonnull completed) {
                (void)completed;
                if (mtl_rel) CFRelease(mtl_rel);
                if (buf_rel) CFRelease(buf_rel);
            }];
        }

        if (s_gp_shoot_now) {
            /* Gameplay shot decided before the replay (metal_gp_decide): the PASS
             * log already printed this frame's draws; now the image and S4/S8. */
            const long long fc = (long long)s_state.frame_count;
            const int gp_k = s_gp_k++;
            s_gp_shoot_now = 0;
            [cb commit];
            [cb waitUntilCompleted];
            id<MTLTexture> t = drawable.texture;
            const NSUInteger w = t.width, h = t.height;
            id<MTLBuffer> st = [s_state.device newBufferWithLength:w * h * 4u
                                                           options:MTLResourceStorageModeShared];
            if (st) {
                id<MTLCommandBuffer> bcb = [s_state.queue commandBuffer];
                id<MTLBlitCommandEncoder> be = [bcb blitCommandEncoder];
                [be copyFromTexture:t sourceSlice:0 sourceLevel:0
                       sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(w, h, 1)
                           toBuffer:st destinationOffset:0 destinationBytesPerRow:w * 4u
            destinationBytesPerImage:w * h * 4u];
                [be endEncoding];
                [bcb commit];
                [bcb waitUntilCompleted];
                char path[64];
                snprintf(path, sizeof(path), "frame_gp%d_f%lld.bmp", gp_k, fc);
                rsx_write_bmp_bgra32(path, (const u8*)st.contents, (u32)w, (u32)h);
                fprintf(stderr, "[RSX metal] gameplay shot %s (%lux%lu draws=%u show=S%d by_last=%d)\n", path,
                        (unsigned long)w, (unsigned long)h, s_draw.draw_count, s_last_show, s_last_show_by_last);
                fflush(stderr);
            }
            for (u32 k = 0; k < s_n_surfaces; k++) {
                if (s_surfaces[k].is_depth || !s_surfaces[k].tex) continue;
                if (s_surfaces[k].tex.pixelFormat != MTLPixelFormatBGRA8Unorm) continue;
                if (s_surfaces[k].tex.width != 1216u && s_surfaces[k].tex.width != 1280u)
                    continue;   /* scene, post and display targets */
                char sp[64];
                snprintf(sp, sizeof(sp), "frame_gp%d_s%u.bmp", gp_k, k);
                metal_dump_texture_to(s_surfaces[k].tex, sp);
            }
            id<MTLCommandBuffer> pcb = [s_state.queue commandBuffer];
            [pcb presentDrawable:drawable];
            [pcb commit];
            goto metal_present_after_submit;
        }
        if (s_state.dump_frames) {
            /* Optional stride (default 1). P1 timeline uses 5–10 to keep
             * disk sane while still classifying legal→menu phases. */
            static int dump_stride = -1;
            if (dump_stride < 0) {
                const char* e = getenv("PS3_FRAME_DUMP_STRIDE");
                dump_stride = (e && e[0]) ? atoi(e) : 1;
                if (dump_stride < 1) dump_stride = 1;
            }
            if (s_prompt_dump_at && s_state.frame_count >= s_prompt_dump_at) {
                s_state.force_dump_once = 1;
                s_prompt_dump_at = 0;
            }
            int do_dump = (dump_stride == 1) ||
                          (s_state.frame_count < 3) ||
                          ((s_state.frame_count % (u64)dump_stride) == 0) ||
                          s_state.force_dump_once;
            if (s_state.force_dump_once)
                s_state.force_dump_once = 0;
            [cb commit];
            [cb waitUntilCompleted];
            if (do_dump)
                metal_dump_frame(drawable.texture);

            id<MTLCommandBuffer> present_cb = [s_state.queue commandBuffer];
            [present_cb presentDrawable:drawable];
            [present_cb commit];
        } else {
            /* PS3_TRACE_PRESENTTIME=1 (diag, OFF): the time each frame really
             * reached the screen (drawable.presentedTime) -- judder shows as
             * frames held 1 or 3 refreshes instead of 2. */
            { static int on = -1;
              if (on < 0) on = metal_env_on("PS3_TRACE_PRESENTTIME") ? 1 : 0;
              if (on) {
                  static int n = 0;
                  [drawable addPresentedHandler:^(id<MTLDrawable> d) {
                      if (__atomic_fetch_add(&n, 1, __ATOMIC_RELAXED) < 3000 && d.presentedTime > 0) {
                          fprintf(stderr, "[PRESENTTIME] %.3f\n", d.presentedTime * 1000.0);
                      }
                  }];
              } }
            [cb presentDrawable:drawable];
            [cb commit];
            metal_green_sync_check(cb);
            /* PS3_METAL_SYNC_PRESENT=1: espera o GPU acabar este quadro antes de o
             * CPU gravar o proximo. Medido no menu do GoW2: a piscada verde (um
             * quadrante verde a cada 143 quadros) some quando o GPU e' esperado,
             * e volta sem isso -- uma corrida entre o quadro em voo e o seguinte.
             * O GPU gasta ~2 ms de um quadro de ~25 ms de CPU, por isso esperar e'
             * quase gratis. */
            {
                static int sp = -1;
                if (sp < 0) sp = metal_env_default("PS3_METAL_SYNC_PRESENT", 0);
                if (sp) [cb waitUntilCompleted];
            }
            /* Guardado so' para o [VARACE] poder perguntar, no inicio do frame
             * seguinte, se a GPU ja' largou o ring antes de o reescrevermos. */
            s_frame_cb[s_frame_slot] = cb;
        }
metal_present_after_submit:
        if (metal_per_draw_rt()) metal_surf_dump_maybe();
    }
}

static void metal_clear(void* ud, u32 flags, u32 color, float depth, u8 stencil)
{
    (void)ud;
    /* Per-draw mode: a clear is ordered with the draws and hits the surface
     * bound when it was issued, not the frame's one target. */
    if (metal_per_draw_rt()) {
        if (s_draw.draw_count < METAL_MAX_DRAWS && flags) {
            MetalDrawRec* dr = &s_draw.draws[s_draw.draw_count++];
            memset(dr, 0, sizeof(*dr));
            dr->prim = (MTLPrimitiveType)0xFFFFFFFFu; /* never drawn */
            dr->has_state = 1;
            dr->color_idx = s_color_rt_idx;
            dr->depth_idx = s_depth_rt_idx;
            dr->clear_flags = flags;
            dr->clear_rgba[0] = ((color >> 16) & 0xFF) / 255.0f;
            dr->clear_rgba[1] = ((color >> 8)  & 0xFF) / 255.0f;
            dr->clear_rgba[2] = ( color        & 0xFF) / 255.0f;
            dr->clear_rgba[3] = ((color >> 24) & 0xFF) / 255.0f;
            dr->clear_depth = depth;
            dr->clear_stencil = stencil;
        }
        return;
    }
    s_state.clear_flags |= flags;
    if (flags & (RSX_CLEAR_COLOR_R | RSX_CLEAR_COLOR_G | RSX_CLEAR_COLOR_B | RSX_CLEAR_COLOR_A)) {
        /* RSX delivers the clear colour as ARGB; Metal wants normalised doubles. */
        s_state.clear_a = ((color >> 24) & 0xFF) / 255.0;
        s_state.clear_r = ((color >> 16) & 0xFF) / 255.0;
        s_state.clear_g = ((color >> 8)  & 0xFF) / 255.0;
        s_state.clear_b = ( color        & 0xFF) / 255.0;
    }
    if (flags & RSX_CLEAR_DEPTH)
        s_state.clear_depth = depth;
    if (flags & RSX_CLEAR_STENCIL)
        s_state.clear_stencil = stencil;
    if (metal_trace_state()) {
        static int n = 0;
        if (n++ < 200)
            printf("[RSX metal] clear flags=0x%X color=0x%08X depth=%f stencil=%u\n",
                   flags, color, depth, (unsigned)stencil);
    }
}

static void metal_set_render_target(void* ud, const rsx_state* state)
{
    (void)ud;
    if (!state) return;
    s_draw.current_rsx = state;
    if (state->scissor_w || state->scissor_h) {
        s_pipe.scissor_x = state->scissor_x;
        s_pipe.scissor_y = state->scissor_y;
        s_pipe.scissor_w = state->scissor_w;
        s_pipe.scissor_h = state->scissor_h;
        s_pipe.has_scissor = 1;
    }

    /* Prefer viewport when clip is nonsense (e.g. height >> 1080). Body
     * moved to rsx_core_rules.c (rsx_core_rt_dims). */
    u32 def_w = s_state.width ? s_state.width : 1280u;
    u32 def_h = s_state.height ? s_state.height : 720u;
    u32 w, h;
    rsx_core_rt_dims(state, def_w, def_h, &w, &h);
    /* PS3_TRACE_MRT=1 (diagnostico, OFF): combinacoes distintas de alvo de cor
     * (SET_SURFACE_COLOR_TARGET, quais de A/B/C/D estao ligados) e os quatro
     * offsets. Este backend so' desenha o alvo A; se o titulo usar MRT, os
     * alvos B-D nunca sao escritos -- suspeito da textura zerada em
     * 0x00200000 que a composicao final do menu le'. */
    {
        static int on = -1;
        if (on < 0) on = metal_env_on("PS3_TRACE_MRT");
        if (on) {
            static u64 seen[64]; static u32 nseen;
            const u64 k = ((u64)state->color_target << 48) ^ ((u64)state->surface_color_offset[1] << 24) ^
                          ((u64)state->surface_color_offset[0]) ^ ((u64)state->surface_color_offset[2] << 8) ^
                          ((u64)state->surface_color_offset[3] << 40);
            int nw = 1;
            for (u32 i = 0; i < nseen; i++) if (seen[i] == k) { nw = 0; break; }
            if (nw && nseen < 64u) {
                seen[nseen++] = k;
                fprintf(stderr, "[MRT] f=%llu target=0x%02X dmaA=%08X dmaB=%08X A=0x%08X B=0x%08X C=0x%08X D=0x%08X Z=0x%08X fmt=0x%08X\n",
                        (unsigned long long)s_state.frame_count, state->color_target,
                        state->surface_color_dma[0], state->surface_color_dma[1],
                        state->surface_color_offset[0], state->surface_color_offset[1],
                        state->surface_color_offset[2], state->surface_color_offset[3],
                        state->surface_zeta_offset, state->surface_format);
                fflush(stderr);
            }
        }
    }
    /* Location: low bits of surface_format often encode type; MVP keys offset. */
    u32 loc = 0;
    /* Alvos ligados (RPCS3 get_rtt_indexes); none (0) mantem o alvo A como
     * antes, para os passes so' de profundidade. Localizacao de cada alvo
     * (SET_CONTEXT_DMA_COLOR_*): 0 VRAM local, 1 memoria principal -- o
     * mesmo offset pode existir nas duas (o RPCS3 resolve por endereco
     * absoluto, get_address(offset, dma)); o alvo B do menu do GoW2 vive em
     * memoria principal. Body moved to rsx_core_rules.c (rsx_core_mrt_targets). */
    int rtt[4]; int nrtt;
    u32 cloc[4];
    rsx_core_mrt_targets(state, metal_mrt_on(), rtt, &nrtt, cloc);
    u32 color_off = state->surface_color_offset[rtt[0]];
    int cidx = metal_surface_get(cloc[rtt[0]], color_off, w, h, 0);
    s_mrt_n = 1;
    for (int k = 1; k < nrtt && cidx >= 0; k++) {
        const u32 off = state->surface_color_offset[rtt[k]];
        const int sk = (off && (off != color_off || cloc[rtt[k]] != cloc[rtt[0]]))
                       ? metal_surface_get(cloc[rtt[k]], off, w, h, 0) : -1;
        if (sk < 0) break;                                  /* incompleto: fica no que ja' ha' */
        s_surfaces[sk].guest_fmt = state->surface_format;
        s_mrt_n = k + 1;
    }
    /* Criar uma superficie pode mover os indices das outras: resolver todos por
     * offset no fim. */
    cidx = metal_surface_find(cloc[rtt[0]], color_off, 0);
    s_mrt_idx[0] = cidx;
    for (int k = 1; k < s_mrt_n; k++) {
        s_mrt_idx[k] = metal_surface_find(cloc[rtt[k]], state->surface_color_offset[rtt[k]], 0);
        if (s_mrt_idx[k] < 0) { s_mrt_n = k; break; }
    }
    if (cidx < 0) s_mrt_n = 1;
    {   /* PS3_TRACE_METAL_SHOW: distinct targets requested this frame (see [SHOW]) */
        int j = 0;
        while (j < s_rt_req_n && s_rt_req[j] != cidx) j++;
        if (j == s_rt_req_n && s_rt_req_n < 16) s_rt_req[s_rt_req_n++] = cidx;
        s_rt_req_calls++;
    }
    if (cidx >= 0) {
        s_color_rt_idx = cidx;
        s_surfaces[cidx].guest_fmt = state->surface_format;
        s_rt_selects++;
    }

    if (state->surface_zeta_offset) {
        int didx = metal_surface_get(loc, state->surface_zeta_offset, w, h, 1);
        if (didx >= 0) s_depth_rt_idx = didx;
    } else {
        s_depth_rt_idx = -1;
    }

    static int s_log = 0;
    if (s_log < 8) {
        printf("[RSX metal] set_render_target(fmt=0x%X %ux%u color_off=0x%08X "
               "zeta_off=0x%08X cidx=%d didx=%d)\n",
               state->surface_format, w, h, color_off, state->surface_zeta_offset,
               s_color_rt_idx, s_depth_rt_idx);
        s_log++;
        fflush(stdout);
    }
}

static void metal_set_viewport(void* ud, const rsx_state* state)
{
    (void)ud;
    if (!state) return;
    s_draw.current_rsx = state;
    s_draw.vp_x = (double)state->viewport_x;
    s_draw.vp_y = (double)state->viewport_y;
    s_draw.vp_w = state->viewport_w ? (double)state->viewport_w : (double)s_state.width;
    s_draw.vp_h = state->viewport_h ? (double)state->viewport_h : (double)s_state.height;
    s_draw.has_vp = 1;
    /* Scissor often co-moves with viewport; keep latest from rsx_state. */
    s_pipe.scissor_x = state->scissor_x;
    s_pipe.scissor_y = state->scissor_y;
    s_pipe.scissor_w = state->scissor_w ? state->scissor_w : state->viewport_w;
    s_pipe.scissor_h = state->scissor_h ? state->scissor_h : state->viewport_h;
    s_pipe.has_scissor = 1;
}

static void metal_set_blend(void* ud, const rsx_state* state)
{
    (void)ud;
    if (!state) return;
    s_draw.current_rsx = state;
    int ch = (s_pipe.blend_enable != state->blend_enable) ||
             (s_pipe.blend_sfactor != state->blend_sfactor) ||
             (s_pipe.blend_dfactor != state->blend_dfactor) ||
             (s_pipe.blend_equation != state->blend_equation);
    s_pipe.blend_enable = state->blend_enable;
    s_pipe.blend_sfactor = state->blend_sfactor;
    s_pipe.blend_dfactor = state->blend_dfactor;
    s_pipe.blend_equation = state->blend_equation;
    s_pipe.blend_color = state->blend_color;
    if (ch) metal_invalidate_blend_psos();
    if (metal_trace_state() && ch) {   /* changes only: the intro's repeats hid the scene (E466) */
        static int n = 0;
        if (n++ < 200)
            printf("[RSX metal] set_blend(enable=%d sf=0x%X df=0x%X eq=0x%X color=0x%08X)\n",
                   state->blend_enable, state->blend_sfactor,
                   state->blend_dfactor, state->blend_equation, state->blend_color);
    }
}

static void metal_set_depth_stencil(void* ud, const rsx_state* state)
{
    (void)ud;
    if (!state) return;
    s_draw.current_rsx = state;
    s_pipe.depth_test_enable = state->depth_test_enable;
    s_pipe.depth_func = state->depth_func;
    s_pipe.depth_mask = state->depth_mask;
    s_pipe.stencil_test_enable = state->stencil_test_enable;
    s_pipe.stencil_func = state->stencil_func;
    s_pipe.stencil_ref = state->stencil_ref;
    s_pipe.stencil_mask = state->stencil_mask;
    s_pipe.stencil_op_fail = state->stencil_op_fail;
    s_pipe.stencil_op_zfail = state->stencil_op_zfail;
    s_pipe.stencil_op_zpass = state->stencil_op_zpass;
    s_pipe.stencil_write_mask = state->stencil_write_mask;
    s_pipe.two_sided_stencil = state->two_sided_stencil_enable;
    s_pipe.back_stencil_func = state->back_stencil_func;
    s_pipe.back_stencil_ref = state->back_stencil_ref;
    s_pipe.back_stencil_mask = state->back_stencil_mask;
    s_pipe.back_stencil_write_mask = state->back_stencil_write_mask;
    s_pipe.back_stencil_op_fail = state->back_stencil_op_fail;
    s_pipe.back_stencil_op_zfail = state->back_stencil_op_zfail;
    s_pipe.back_stencil_op_zpass = state->back_stencil_op_zpass;
    u32 old_ds = s_pipe.ds_key;
    s_pipe.ds_key = metal_pipe_ds_key();
    if (metal_trace_state() && old_ds != s_pipe.ds_key) {
        static int n = 0;
        if (n++ < 200)
            printf("[RSX metal] set_depth_stencil(depth=%d mask=%d func=0x%X stencil=%d)\n",
                   state->depth_test_enable, state->depth_mask,
                   state->depth_func, state->stencil_test_enable);
    }
}

static void metal_set_color_mask(void* ud, const rsx_state* state)
{
    (void)ud;
    if (!state) return;
    s_draw.current_rsx = state;
    int ch = (s_pipe.color_mask != state->color_mask);
    if (ch) {
        s_pipe.color_mask = state->color_mask;
        metal_invalidate_blend_psos();
    }
    if (metal_trace_state() && ch) {
        static int n = 0;
        if (n++ < 200)
            printf("[RSX metal] set_color_mask(0x%08X)\n", state->color_mask);
    }
}

static void metal_set_alpha_test(void* ud, const rsx_state* state)
{
    (void)ud;
    if (!state) return;
    s_draw.current_rsx = state;
    int ch = (s_pipe.alpha_test_enable != state->alpha_test_enable) ||
             (s_pipe.alpha_func != state->alpha_func) || (s_pipe.alpha_ref != state->alpha_ref);
    s_pipe.alpha_test_enable = state->alpha_test_enable;
    s_pipe.alpha_func = state->alpha_func;
    s_pipe.alpha_ref = state->alpha_ref;
    /* Full alpha-test in FS is out of M5 scope; log for diagnostics. */
    if (metal_trace_state() && ch) {
        static int n = 0;
        if (n++ < 200)
            printf("[RSX metal] set_alpha_test(enable=%d func=0x%X ref=%u)\n",
                   state->alpha_test_enable, state->alpha_func, state->alpha_ref);
    }
}

static void metal_set_vertex_attribs(void* ud, const rsx_state* state)
{
    (void)ud;
    if (!state) return;
    s_draw.current_rsx = state;
    /* Scissor refresh (no dedicated hook). */
    s_pipe.scissor_x = state->scissor_x;
    s_pipe.scissor_y = state->scissor_y;
    s_pipe.scissor_w = state->scissor_w;
    s_pipe.scissor_h = state->scissor_h;
    s_pipe.has_scissor = 1;

    memcpy(s_draw.attribs, state->vertex_attribs, sizeof(s_draw.attribs));
    s_draw.attribs_valid = 1;
    s_draw.attrib_sig = metal_attrib_sig(s_draw.attribs);
    s_draw.layout_ok = rsx_vertex_attrib_is_position_ok(&s_draw.attribs[0]);
    s_draw.layout_unknown = 0;
    for (int i = 0; i < RSX_MAX_VERTEX_ATTRIBS; i++) {
        const rsx_vertex_attrib* a = &s_draw.attribs[i];
        if (!a->enabled) continue;
        if (rsx_to_mtl_vertex_format_tag(a->type, a->size) == RSX_MTL_VF_UNKNOWN)
            s_draw.layout_unknown = 1;
    }

    static int log_count = 0;
    int trace = 0;
    {
        const char* e = getenv("PS3_TRACE_RSX_ATTRIBS");
        trace = (e && e[0] && e[0] != '0') ? 1 : 0;
    }
    if (log_count < 8 || trace) {
        printf("[RSX metal] set_vertex_attribs sig=0x%08X layout_ok=%d unknown=%d\n",
               s_draw.attrib_sig, s_draw.layout_ok, s_draw.layout_unknown);
        for (int i = 0; i < RSX_MAX_VERTEX_ATTRIBS; i++) {
            const rsx_vertex_attrib* a = &s_draw.attribs[i];
            if (!a->enabled) continue;
            const char* type_name = "?";
            switch (a->type) {
            case RSX_VERTEX_TYPE_S1:    type_name = "snorm16"; break;
            case RSX_VERTEX_TYPE_F:     type_name = "float32"; break;
            case RSX_VERTEX_TYPE_SF:    type_name = "float16"; break;
            case RSX_VERTEX_TYPE_UB:    type_name = "ubyte_n"; break;
            case RSX_VERTEX_TYPE_S32K:  type_name = "s16"; break;
            case RSX_VERTEX_TYPE_CMP:   type_name = "cmp11_11_10"; break;
            case RSX_VERTEX_TYPE_UB256: type_name = "ubyte"; break;
            }
            u32 tag = rsx_to_mtl_vertex_format_tag(a->type, a->size);
            printf("  attrib[%d]: %s x%u stride=%u off=0x%X mtl_tag=%u%s\n",
                   i, type_name, a->size, a->stride, a->offset, tag,
                   tag == RSX_MTL_VF_UNKNOWN ? " [UNSUPPORTED]" : "");
        }
        if (log_count < 8) log_count++;
        fflush(stdout);
    }
}

/* Insert a PSO under (fp, original vp, state key); becomes current_shader. */
/* E482: que atributos de vertice o VS realmente LE'. Derivada do texto MSL
 * emitido, nao de uma segunda descodificacao do ucode, para nao poder divergir
 * do que o shader faz. Conservadora: apanha tambem as escritas do preambulo
 * (v[0], v[3], v[8]), logo no pior caso descodifica a mais, nunca a menos. */
static u16 metal_vs_used_inputs(const char* msl)
{
    if (!msl) return 0xFFFFu;
    u16 m = 0;
    for (const char* q = msl; (q = strstr(q, "v[")) != NULL; q += 2) {
        const char* d = q + 2;
        if (*d < '0' || *d > '9') continue;      /* v[_a]: indice variavel */
        unsigned n = (unsigned)(*d - '0');
        if (d[1] >= '0' && d[1] <= '9') { n = n * 10u + (unsigned)(d[1] - '0'); d++; }
        if (d[1] != ']') continue;
        if (n < 16u) m |= (u16)(1u << n);
    }
    return m ? m : 0xFFFFu;   /* indice variavel: nao arriscar */
}

static MetalShaderPso* metal_shader_cache_insert(u32 fp_hash, u32 vp_key, u32 skey,
                                                 id<MTLRenderPipelineState> pso)
{
    if (s_draw.shader_cache_count >= METAL_MAX_SHADER_CACHE) {
        MetalShaderPso* victim = &s_draw.shader_cache[0];
        /* PS3_TRACE_METAL_CACHE (OFF): total evictions, every 1000 -- measures
         * cache thrash (a recompile per set_shader when it churns). */
        { static int on = -1; static unsigned long ev = 0;
          if (on < 0) on = getenv("PS3_TRACE_METAL_CACHE") ? 1 : 0;
          if (on && (++ev % 1000u) == 0) {
              printf("[RSX metal] shader_cache evictions=%lu (size %u)\n", ev, (u32)METAL_MAX_SHADER_CACHE);
              fflush(stdout);
          } }
        /* Draws recorded earlier in this frame may still hold victim->pso
         * (MetalDrawRec.pso is __unsafe_unretained); releasing it here made the
         * replay in metal_present retain a freed object (E427 visible run:
         * SIGSEGV in objc_retain <- metal_present). Retire it until the next
         * metal_begin_frame instead. */
        if (victim->pso_b) { CFRelease((__bridge CFTypeRef)victim->pso_b); victim->pso_b = nil; }
        if (victim->pso) {
            if (s_draw.pso_retired_count < 256u) {
                s_draw.pso_retired[s_draw.pso_retired_count++] = (__bridge CFTypeRef)victim->pso;
            } else {
                static int warned = 0;
                if (!warned++)
                    printf("[RSX metal] pso_retired full (256) -- releasing an evicted PSO immediately\n");
                CFRelease((__bridge CFTypeRef)victim->pso);
            }
        }
        memmove(&s_draw.shader_cache[0], &s_draw.shader_cache[1],
                (METAL_MAX_SHADER_CACHE - 1) * sizeof(MetalShaderPso));
        s_draw.shader_cache_count--;
        /* Pointer may be invalidated after memmove; the new slot replaces it. */
        s_draw.current_shader = NULL;
    }

    MetalShaderPso* slot = &s_draw.shader_cache[s_draw.shader_cache_count];
    slot->fp_hash   = fp_hash;
    slot->vp_hash   = vp_key;
    slot->state_key = skey;
    slot->mrt_n     = (u8)s_mrt_n;
    slot->pso_b     = nil;
    {   /* PS3_RSX_SPLIT: o mesmo estado com o VS da metade direita */
        MetalShaderFns* bfe = metal_fns_find(fp_hash, vp_key);
        if (bfe && bfe->vs_b) {
            id<MTLRenderPipelineState> pb = metal_pso_from_fns(bfe->vs_b, bfe->fs, fp_hash, vp_key);
            if (pb) slot->pso_b = (__bridge id<MTLRenderPipelineState>)CFBridgingRetain(pb);
        }
    }
    {   /* set_shader updates fns->vs_va right after compiling, before this insert */
        MetalShaderFns* sfe = metal_fns_find(fp_hash, vp_key);
        slot->vs_va = sfe ? sfe->vs_va : 0;
    }
    slot->pso = (__bridge id<MTLRenderPipelineState>)CFBridgingRetain(pso);
    s_draw.shader_cache_count++;
    s_draw.current_shader = slot;
    return slot;
}

/* The current shaders' PSO for the blend/mask state in force now: a cached
 * variant, or one built from the cached functions. NULL if neither exists. */
static id<MTLRenderPipelineState> metal_shader_variant_now(void)
{
    MetalShaderPso* cs = s_draw.current_shader;
    if (!cs) return nil;
    const u32 fp = cs->fp_hash, vp = cs->vp_hash, key = metal_pso_state_key();
    for (u32 i = 0; i < s_draw.shader_cache_count; i++) {
        if (s_draw.shader_cache[i].fp_hash == fp && s_draw.shader_cache[i].vp_hash == vp &&
            s_draw.shader_cache[i].state_key == key && s_draw.shader_cache[i].pso) {
            s_draw.current_shader = &s_draw.shader_cache[i];
            return s_draw.shader_cache[i].pso;
        }
    }
    MetalShaderFns* fe = metal_fns_find(fp, vp);
    if (!fe) return nil;
    id<MTLRenderPipelineState> p = metal_pso_from_fns(fe->vs, fe->fs, fe->fp_hash, fe->vp_key);
    if (!p) return nil;
    MetalShaderPso* slot = metal_shader_cache_insert(fp, vp, key, p);
    return slot ? slot->pso : nil;
}

static void metal_set_shader(void* ud, const rsx_state* state)
{
    (void)ud;
    if (!state) { s_draw.current_shader = NULL; return; }
    s_draw.current_rsx = state;

    static int log_count = 0;
    if (log_count < 5) {
        printf("[RSX metal] set_shader: fp_addr=0x%08X, vp_load=%u, loc=%u\n",
               state->fragment_program_addr, state->transform_program_load,
               state->shader_program & 0x3u);
        log_count++;
    }

    /* "Sem programa" e' o REGISTO CRU a zero (nunca escrito). O offset 0 e'
     * um endereco valido: o GoW2 tem os fragment programs do texto do menu num
     * anel em VRAM que da' a volta para o offset 0 -- data = 0x1 (local + 1).
     * Testar so' o offset tratava esse draw como sem shader, o pipeline de
     * reserva desenhava a posicao crua e saia um quadrante verde a cada volta
     * do anel (~143 quadros). */
    if (!state->shader_program) {
        s_draw.current_shader = NULL;
        return;
    }
    if (!vm_base || !s_state.device) {
        s_draw.current_shader = NULL;
        return;
    }

    const u32 MAX_FP_BYTES = 16u * 1024u;
    u32 ea = metal_resolve_fp_ea(state);
    {
        static int tl_n = 0;
        if (metal_tl_on() && tl_n++ < 64)
            METAL_TL("set_shader ea=0x%08X fp_raw=0x%08X vp_load=%u",
                     ea, state->fragment_program_addr,
                     state->transform_program_load);
    }
    if (!ppu_guest_range_committed(ea, 512)) {
        static int warn = 0;
        if (warn++ < 5)
            printf("[RSX metal] set_shader: fp ea=0x%08X not committed, fallback PSO\n", ea);
        s_draw.current_shader = NULL;
        return;
    }
    const u8* ucode = vm_base + ea;
    u32 prog_bytes = rsx_fp_program_size(ucode, MAX_FP_BYTES);
    if (!prog_bytes) {
        static int zlog = 0;
        if (zlog++ < 5)
            printf("[RSX metal] set_shader: fp ea=0x%08X size=0, fallback PSO\n", ea);
        s_draw.current_shader = NULL;
        return;
    }

    /* Key on the instruction words only: the title patches inline FP constants
     * per draw, and hashing them made every draw a new PSO (E430: 244 000
     * evictions in 600 s). The constants travel per draw in fc_buf instead.
     * PS3_METAL_FP_LITERAL_CONSTS=1 restores the old literal mode. */
    u32 fp_hash;
    if (metal_fp_const_buffer_on())
        fp_hash = rsx_fp_hash_and_consts(ucode, prog_bytes, NULL, 0, NULL);
    else
        fp_hash = metal_fnv1a(ucode, prog_bytes);
    s_draw.cur_fp_ea    = ea;
    s_draw.cur_fp_bytes = prog_bytes;
    u32 vpw = 0;
    const u32* vpwords = rsx_vp_entry(state, &vpw);
    u32 vp_hash = (vpw >= 4) ? metal_fnv1a((const u8*)vpwords, vpw * 4) : 0u;
    /* PS3_RSX_DUMP_UCODE=<dir> (OFF): raw ucode corpus for the Vulkan GLSL
     * emitter (plan 2026-09-23-vulkan-v0). No-op when unset. */
    rsx_ucode_dump(ucode, prog_bytes, fp_hash, vpwords, vpw, vp_hash);
    /* Key = the ORIGINAL (fp, vp) pair plus the blend/mask state. The VS
     * fallback below zeroes vp_hash; storing that made those shaders miss (and
     * recompile) on every set_shader. */
    /* Depth export changes the FS signature: variants keyed apart (salted vp key). */
    const int fp_depth_export = state->shader_control_valid &&
        (state->shader_control & RSX_FP_SHADER_CONTROL_DEPTH_EXPORT) != 0u;
    const u32 vp_key = vp_hash ^ (fp_depth_export ? 0xDE9E7000u : 0u);
    const u32 skey = metal_pso_state_key();

    for (u32 i = 0; i < s_draw.shader_cache_count; i++) {
        if (s_draw.shader_cache[i].fp_hash == fp_hash &&
            s_draw.shader_cache[i].vp_hash == vp_key &&
            s_draw.shader_cache[i].state_key == skey) {
            s_draw.fp_cache_hits++;
            s_draw.current_shader = &s_draw.shader_cache[i];
            return;
        }
    }
    /* Same shaders in another blend/mask state: build from cached functions. */
    {
        MetalShaderFns* fe = metal_fns_find(fp_hash, vp_key);
        if (fe) {
            id<MTLRenderPipelineState> vpso = metal_pso_from_fns(fe->vs, fe->fs, fe->fp_hash, fe->vp_key);
            if (vpso) {
                s_draw.fp_cache_hits++;
                {   MetalShaderPso* _e = metal_shader_cache_insert(fp_hash, vp_key, skey, vpso);
                    if (_e) _e->used_inputs = fe->used_inputs; }
                return;
            }
        }
    }
    s_draw.fp_cache_misses++;
    /* PS3_TRACE_METAL_MISS (OFF): which half of the key changes on a miss --
     * E431 showed the instruction-only fp_hash did not stop the thrash. */
    { static int on = -1; static int n = 0;
      if (on < 0) on = getenv("PS3_TRACE_METAL_MISS") ? 1 : 0;
      if (on && n < 300) {
          n++;
          printf("[RSX metal] miss #%d fp_hash=0x%08X vp_hash=0x%08X ea=0x%08X fp_bytes=%u vp_words=%u\n",
                 n, fp_hash, vp_hash, ea, prog_bytes, vpw);
          fflush(stdout);
      } }

    static char fs_msl[96 * 1024];
    static char vs_msl[96 * 1024];
    rsx_fp_set_const_buffer_mode(metal_fp_const_buffer_on());
    /* Honor guest SET_SHADER_CONTROL. GoW2 writes 0x02000440 (32_BITS_EXPORTS
     * → r0) on the shaders we logged; forcing AUTO/h0 made the picture worse
     * than the E466 frame that was already on screen. */
    if (state->shader_control_valid)
        rsx_fp_set_color_export((state->shader_control & 0x40u) ? 1 : 0);
    else
        rsx_fp_set_color_export(-1);
    rsx_fp_set_depth_export(fp_depth_export);
    /* Sempre 4 saidas de cor: o PSO liga so' os alvos activos e o Metal descarta
     * as outras (medido: FS com 4 saidas + PSO com 1..4 attachments passa na
     * camada de validacao). Assim a mesma funcao serve a qualquer
     * SET_SURFACE_COLOR_TARGET, sem depender da ordem set_shader/set_surface. */
    rsx_fp_set_mrt_count(metal_mrt_on() ? 4 : 1);
    int n_instr = rsx_fp_decompile_msl(ucode, prog_bytes, fs_msl, sizeof(fs_msl));
    rsx_fp_set_mrt_count(1);
    rsx_fp_set_depth_export(0);
    rsx_fp_set_color_export(-1);
    if (fp_depth_export && metal_env_on("PS3_TRACE_METAL_PASSES")) {
        static int dn = 0;   /* diagnostic: which FPs compile with depth export */
        if (dn++ < 16)
            fprintf(stderr, "[PASS] depth-export FS fp=%08X ctrl=%08X r1_depth=%d\n", fp_hash,
                    state->shader_control, strstr(fs_msl, "[[depth(any)]]") ? 1 : 0);
    }
    rsx_fp_set_const_buffer_mode(0);
    if (n_instr < 1) {
        static int fail_log = 0;
        s_draw.fp_decompile_fail++;
        if (fail_log++ < 5) {
            printf("[RSX metal] set_shader: rsx_fp_decompile_msl failed "
                   "(ea=0x%08X instr=%d), fallback PSO\n", ea, n_instr);
            metal_overlay_shader_log("FP decompile failed ea=0x%08X instr=%d", ea, n_instr);
        }
        s_draw.current_shader = NULL;
        return;
    }

    const char* vs_src = s_metal_vs_passthrough_msl;
    int used_vp = 0;
    /* PS3_METAL_VP_OFF=1: force passthrough VS (UV in col.xy) — diagnoses
     * guest VP clobbering NDC fullscreen quads / TEXCOORD. */
    static int vp_off = -1;
    if (vp_off < 0) {
        const char* e = getenv("PS3_METAL_VP_OFF");
        vp_off = (e && e[0] && e[0] != '0') ? 1 : 0;
    }
    static char vs_msl_b[96 * 1024];
    int have_vs_b = 0;
    if (vp_hash && !vp_off) {
        const int sbit = metal_split_bit();
        if (sbit) rsx_vp_set_fix_override(rsx_vp_fix_env_bits() & ~sbit);   /* esquerda: SEM */
        int vp_instr = rsx_vp_decompile_msl((const u8*)vpwords,
                                            vpw * 4, vs_msl, sizeof(vs_msl));
        if (sbit) {
            rsx_vp_set_fix_override(rsx_vp_fix_env_bits() | sbit);          /* direita: COM */
            have_vs_b = rsx_vp_decompile_msl((const u8*)vpwords,
                                             vpw * 4, vs_msl_b, sizeof(vs_msl_b)) > 0;
            rsx_vp_set_fix_override(-1);
        }
        if (vp_instr > 0) {
            vs_src = vs_msl;
            used_vp = 1;
            s_draw.vp_decompile_ok++;
        } else {
            static int vlog = 0;
            s_draw.vp_decompile_fail++;
            if (vlog++ < 5) {
                printf("[RSX metal] set_shader: rsx_vp_decompile_msl failed "
                       "(vp_hash=0x%08X)\n", vp_hash);
                metal_overlay_shader_log("VP decompile failed vp=0x%08X", vp_hash);
            }
            vp_hash = 0;
        }
    } else if (vp_off) {
        vp_hash = 0;
    }

    /* If decompile emitted a no-op body (return r[0] with no .sample call),
     * substitute a textured passthrough FS so UI/legal quads can paint.
     * Note: preamble always mentions rsx_tex[] — check for .sample( only. */
    {
        /* This passthrough replaced every FS that returned r[0] -- the normal
         * NV40 colour output (H0-only shaders already return h[0]) -- so real
         * sampling shaders of 2-21 instructions were thrown away and every draw
         * copied texture unit 0 (E447: black scene target, UV-gradient bloom
         * surfaces; log `empty/no-sample FS (instr=21 sample=1 noop=1)`). Opt-in
         * for A/B only: PS3_METAL_FS_PASSTHROUGH=1. */
        static int fs_pt = -1;
        if (fs_pt < 0) {
            const char* e = getenv("PS3_METAL_FS_PASSTHROUGH");
            fs_pt = (e && *e && *e != '0') ? 1 : 0;
        }
        int has_sample = strstr(fs_msl, ".sample(") != NULL;
        int noop_ret = strstr(fs_msl, "return r[0];") != NULL;
        if (fs_pt && (!has_sample || noop_ret)) {
            static int flog = 0;
            if (flog++ < 8)
                printf("[RSX metal] set_shader: empty/no-sample FS "
                       "(instr=%d sample=%d noop=%d) → textured passthrough\n",
                       n_instr, has_sample, noop_ret);
            static const char fs_tex[] =
                "#include <metal_stdlib>\n"
                "using namespace metal;\n"
                "struct PSIn {\n"
                "  float4 position [[position]];\n"
                "  float4 col0; float4 col1; float4 fog;\n"
                "  float4 tc0; float4 tc1; float4 tc2; float4 tc3;\n"
                "  float4 tc4; float4 tc5; float4 tc6; float4 tc7;\n"
                "};\n"
                "fragment float4 fs_main(PSIn input [[stage_in]],\n"
                "    texture2d<float> tex0 [[texture(0)]],\n"
                "    sampler samp0 [[sampler(0)]]) {\n"
                "  return tex0.sample(samp0, input.tc0.xy);\n"
                "}\n";
            snprintf(fs_msl, sizeof(fs_msl), "%s", fs_tex);
        }
    }

    /* PS3_METAL_DUMP_MSL=1 (OFF, diagnostic): write the MSL of each new (fp, vp)
     * pair to msl_fp<hash>_vp<hash>.metal in the working directory (first 200),
     * to read what the game's shaders become (E448: real shaders compile and
     * every surface stays 0). */
    {
        static int dump_on = -1;
        static int dumped = 0;
        static int dump_cap = 200;   /* PS3_METAL_DUMP_MSL=<n> > 1 raises it (late levels) */
        if (dump_on < 0) {
            const char* e = getenv("PS3_METAL_DUMP_MSL");
            dump_on = (e && *e && *e != '0') ? 1 : 0;
            const int v = dump_on ? atoi(e) : 0;
            if (v > 1 && v <= 100000) dump_cap = v;
        }
        if (dump_on && dumped < dump_cap) {
            char path[80];
            snprintf(path, sizeof(path), "msl_fp%08X_vp%08X.metal", fp_hash, vp_key);
            FILE* df = fopen(path, "w");
            if (df) {
                /* Raw fragment microcode first: the destination register and write mask of
                 * each instruction are what decide where the colour and the alpha land, and
                 * that is exactly what the bloom's input alpha depends on. */
                fprintf(df, "// ucode %u bytes at 0x%08X\n", prog_bytes, ea);
                for (u32 wo = 0; wo + 16u <= prog_bytes; wo += 16u)
                    fprintf(df, "// i%02u: %08X %08X %08X %08X\n", wo / 16u,
                            rsx_fp_read_word(ucode + wo + 0), rsx_fp_read_word(ucode + wo + 4),
                            rsx_fp_read_word(ucode + wo + 8), rsx_fp_read_word(ucode + wo + 12));
                {   /* Vertex microcode too: 4 words per instruction, the destination and
                     * write mask live in word 3 and decide which output register the
                     * per-vertex colour lands in. */
                    const u32* vw = (const u32*)state->transform_program;
                    const u32 vn = state->transform_program_words;
                    fprintf(df, "// vp_words=%u load=%u start=%u\n", vn, state->transform_program_load,
                            state->transform_program_start);
                    for (u32 vi = 0; vi + 4u <= vn && vi < 512u * 4u; vi += 4u)
                        fprintf(df, "// v%02u: %08X %08X %08X %08X\n", vi / 4u,
                                vw[vi + 0], vw[vi + 1], vw[vi + 2], vw[vi + 3]);
                }
                fprintf(df, "// fp_hash=0x%08X vp_hash=0x%08X fp_ea=0x%08X fp_instr=%d used_vp=%d\n"
                            "// ---- fragment ----\n%s\n// ---- vertex ----\n%s\n",
                        fp_hash, vp_key, ea, n_instr, used_vp, fs_msl, vs_src);
                fclose(df);
                dumped++;
            }
        }
    }
    MetalShaderFns* fe = metal_fns_compile_insert(fp_hash, vp_key, vs_src, fs_msl);
    if (fe && have_vs_b && used_vp && !fe->vs_b) {
        id<MTLFunction> vb = metal_compile_vs(vs_msl_b);
        if (vb) fe->vs_b = (__bridge id<MTLFunction>)CFBridgingRetain(vb);
    }
    if (fe) {
        fe->vs_va = used_vp;
        fe->used_inputs = used_vp ? metal_vs_used_inputs(vs_src) : 0xFFFFu;
    }
    id<MTLRenderPipelineState> pso = fe ? metal_pso_from_fns(fe->vs, fe->fs, fe->fp_hash, fe->vp_key) : nil;
    if (!pso && used_vp) {
        /* Link mismatch / VP bad — retry with passthrough VS. */
        static int rlog = 0;
        if (rlog++ < 5)
            printf("[RSX metal] set_shader: VP PSO failed, retry passthrough "
                   "(vp_hash was set)\n");
        vp_hash = 0;
        used_vp = 0;
        fe = metal_fns_compile_insert(fp_hash, vp_key, s_metal_vs_passthrough_msl, fs_msl);
        if (fe) { fe->vs_va = 0; fe->used_inputs = 0xFFFFu; }
        pso = fe ? metal_pso_from_fns(fe->vs, fe->fs, fe->fp_hash, fe->vp_key) : nil;
    }
    if (!pso) {
        s_draw.fp_decompile_fail++;
        static int plog = 0;
        if (plog++ < 5)
            printf("[RSX metal] set_shader: PSO compile failed "
                   "(ea=0x%08X hash=0x%08X), fallback PSO\n", ea, fp_hash);
        s_draw.current_shader = NULL;
        return;
    }
    s_draw.fp_decompile_ok++;
    {   MetalShaderPso* _e2 = metal_shader_cache_insert(fp_hash, vp_key, skey, pso);
        if (_e2) _e2->used_inputs = fe ? fe->used_inputs : 0xFFFFu; }

    static int compiled_log = 0;
    if (compiled_log++ < 32) {
        printf("[RSX metal] set_shader: compiled PSO (ea=0x%08X, fp=%d instr, "
               "hash=0x%08X, vp_hash=0x%08X %s, cache=%u/%u)\n",
               ea, n_instr, fp_hash, vp_hash, used_vp ? "VP" : "passthrough",
               s_draw.shader_cache_count, (u32)METAL_MAX_SHADER_CACHE);
        if (compiled_log == 1 && getenv("PS3_TRACE_METAL_SHADER")) {
            FILE* df = fopen("/tmp/gow2_first_fs.metal", "w");
            if (df) { fputs(fs_msl, df); fclose(df); }
            FILE* dv = fopen("/tmp/gow2_first_vs.metal", "w");
            if (dv) { fputs(vs_src, dv); fclose(dv); }
            printf("[RSX metal] dumped first VS/FS to /tmp/gow2_first_*.metal\n");
        }
    }
}

static int metal_max_draws(void)
{
    static int n = -2;
    if (n == -2) {
        const char* e = getenv("PS3_METAL_MAX_DRAWS");
        n = (e && *e) ? atoi(e) : 0;
        if (n < 0) n = 0;
    }
    return n;
}

static u32 metal_dump_draw_id(void)
{
    static int n = -2;
    if (n == -2) {
        const char* e = getenv("PS3_METAL_DUMP_DRAW");
        n = (e && *e) ? atoi(e) : 0;
        if (n < 0) n = 0;
    }
    return (u32)n;
}

static int metal_geom_skipped(u32 gid)
{
    static int parsed = 0, one = 0, lo = 0, hi = 0;
    static u32 skip_begin = 0, only_begin = 0;
    if (!parsed) {
        const char* e = getenv("PS3_METAL_SKIP_DRAW");
        if (e && *e) one = atoi(e);
        e = getenv("PS3_METAL_SKIP_DRAWS");
        if (e && *e) {
            unsigned a = 0, b = 0;
            if (sscanf(e, "%u:%u", &a, &b) == 2) { lo = (int)a; hi = (int)b; }
        }
        e = getenv("PS3_METAL_SKIP_BEGIN");
        if (e && *e) skip_begin = (u32)strtoul(e, NULL, 0);
        e = getenv("PS3_METAL_ONLY_BEGIN");
        if (e && *e) only_begin = (u32)strtoul(e, NULL, 0);
        parsed = 1;
    }
    if (one && gid == (u32)one) return 1;
    if (lo && hi && gid >= (u32)lo && gid <= (u32)hi) return 1;
    if (s_draw.current_rsx) {
        u32 bid = s_draw.current_rsx->begin_end_id;
        if (skip_begin && bid == skip_begin) return 1;
        if (only_begin && bid != only_begin) return 1;
    }
    return 0;
}

/* 1-based per-frame geom id, or 0 if over cap / skipped (later ids still issued). */
static u32 metal_next_geom_id(void)
{
    s_frame_geom++;
    int cap = metal_max_draws();
    if (cap && (int)s_frame_geom > cap)
        return 0;
    if (metal_geom_skipped(s_frame_geom))
        return 0;
    return s_frame_geom;
}

static const char* metal_rsx_prim_name(u32 p)
{
    switch (p) {
    case RSX_PRIMITIVE_POINTS:         return "POINTS";
    case RSX_PRIMITIVE_LINES:          return "LINES";
    case RSX_PRIMITIVE_LINE_LOOP:      return "LINE_LOOP";
    case RSX_PRIMITIVE_LINE_STRIP:     return "LINE_STRIP";
    case RSX_PRIMITIVE_TRIANGLES:      return "TRIANGLES";
    case RSX_PRIMITIVE_TRIANGLE_STRIP: return "TRIANGLE_STRIP";
    case RSX_PRIMITIVE_TRIANGLE_FAN:   return "TRIANGLE_FAN";
    case RSX_PRIMITIVE_QUADS:          return "QUADS";
    case RSX_PRIMITIVE_QUAD_STRIP:     return "QUAD_STRIP";
    case RSX_PRIMITIVE_POLYGON:        return "POLYGON";
    default:                           return "?";
    }
}

static const char* metal_vtx_type_name(u32 t)
{
    switch (t) {
    case RSX_VERTEX_TYPE_S1:    return "SHORT_NORM";
    case RSX_VERTEX_TYPE_F:     return "FLOAT";
    case RSX_VERTEX_TYPE_SF:    return "HALF";
    case RSX_VERTEX_TYPE_UB:    return "UNORM8";
    case RSX_VERTEX_TYPE_S32K:  return "SINT16";
    case RSX_VERTEX_TYPE_CMP:   return "CMP32";
    case RSX_VERTEX_TYPE_UB256: return "UINT8";
    default:                    return "?";
    }
}

static void metal_dump_attribs(void)
{
    const rsx_vertex_attrib* a = s_draw.attribs_valid
        ? s_draw.attribs
        : (s_draw.current_rsx ? s_draw.current_rsx->vertex_attribs : NULL);
    if (!a) {
        fprintf(stderr, "attribs: (none)\n");
        return;
    }
    static const char* slotn[16] = {
        "POSITION", "1", "2", "NORMAL/COLOR", "4", "5", "6", "7",
        "UV", "9", "10", "11", "12", "13", "14", "15"
    };
    for (int i = 0; i < (int)RSX_MAX_VERTEX_ATTRIBS; i++) {
        if (!a[i].enabled) continue;
        fprintf(stderr, "%s:\n type: %s\n size: %u\n offset: %u\n stride: %u\n",
                slotn[i], metal_vtx_type_name(a[i].type), a[i].size,
                a[i].offset, a[i].stride);
    }
}

static void metal_dump_positions(u32 vb_off, u32 nvert, u32 draw_id)
{
    if (!s_draw.vb_mapped || nvert == 0) return;
    const MetalBasicVertex* vv =
        (const MetalBasicVertex*)((const u8*)s_draw.vb_mapped + vb_off);
    u32 show = nvert < 8u ? nvert : 8u;
    for (u32 i = 0; i < show; i++) {
        float x = vv[i].x, y = vv[i].y, z = vv[i].z;
        if (!isfinite(x) || !isfinite(y) || !isfinite(z))
            fprintf(stderr, "BAD POSITION draw=%u index=%u p=(%f,%f,%f)\n",
                    draw_id, i, x, y, z);
        else if (fabsf(x) > 1.0e5f || fabsf(y) > 1.0e5f || fabsf(z) > 1.0e5f)
            fprintf(stderr, "HUGE POSITION draw=%u index=%u p=(%f,%f,%f)\n",
                    draw_id, i, x, y, z);
        else
            fprintf(stderr, "pos[%u]=(%.5g, %.5g, %.5g)\n", i, x, y, z);
    }
}

static void metal_dump_geom_header(u32 draw_id, u32 primitive, int indexed,
                                   u32 count, u32 nvert, u32 first_or_off)
{
    fprintf(stderr, "========== [GEOM DRAW %u] frameDraw=%u geomDraw=%u ==========\n",
            draw_id, s_draw.draw_count + 1u, draw_id);
    {
        const rsx_state* st = s_draw.current_rsx;
        u32 vs = 0, cnst = 0, vert = s_draw.attrib_sig, rast = 0, ib = 0;
        if (s_draw.current_shader) {
            vs = s_draw.current_shader->fp_hash ^ s_draw.current_shader->vp_hash;
        }
        if (st) {
            ib = st->index_array_addr;
            rast = (u32)st->blend_enable | ((u32)st->depth_test_enable << 1) |
                   (st->color_mask << 8) | (st->scissor_w << 16);
            u32 lo = st->vertex_constants_lo, hi = st->vertex_constants_hi;
            if (hi < lo || hi >= RSX_MAX_VERTEX_CONSTANTS) { lo = 0; hi = 7; }
            cnst = 2166136261u;
            for (u32 i = lo; i <= hi && i < RSX_MAX_VERTEX_CONSTANTS; i++) {
                for (int k = 0; k < 4; k++) {
                    u32 bits;
                    memcpy(&bits, &st->vertex_constants[i][k], 4);
                    cnst = (cnst * 16777619u) ^ bits;
                }
            }
            fprintf(stderr, "beginEndId: %u\npacket: %u\n",
                    st->begin_end_id, st->begin_end_packet);
            {
                int dm = rsx_depth_debug_mode();
                u32 mf = rsx_depth_effective_func(st->depth_func,
                                                 st->depth_test_enable, dm);
                fprintf(stderr,
                        "depth: rsxEn=%d rsxFunc=0x%X (%s) rsxWrite=%d "
                        "metalCmp=%s metalWrite=%d znear=%g zfar=%g "
                        "clearZ=%g mode=%s\n",
                        st->depth_test_enable, st->depth_func,
                        rsx_depth_func_name(st->depth_func), st->depth_mask,
                        rsx_depth_func_name(mf),
                        rsx_depth_effective_write(st->depth_mask,
                                                 st->depth_test_enable, dm),
                        st->clip_min, st->clip_max,
                        (float)(st->zstencil_clear_value >> 8) / (float)0xFFFFFF,
                        rsx_depth_debug_mode_name(dm));
            }
        }
        fprintf(stderr, "VS=%08X CONST=%08X VERTEX=%08X RASTER=%08X IB=%08X\n",
                vs, cnst, vert, rast, ib);
    }
    fprintf(stderr, "primitive: %s\n", metal_rsx_prim_name(primitive));
    fprintf(stderr, "indexed: %s\n", indexed ? "true" : "false");
    if (indexed) {
        const rsx_state* st = s_draw.current_rsx;
        fprintf(stderr, "indexCount: %u\n", count);
        fprintf(stderr, "indexType: %s\n",
                (st && st->index_is_16bit) ? "UINT16" : "UINT32");
        fprintf(stderr, "indexOffset: 0x%08X\n", first_or_off);
        if (st)
            fprintf(stderr, "indexArray: 0x%08X loc=%u\n",
                    st->index_array_addr, st->index_location);
    } else {
        fprintf(stderr, "firstVertex: %u\n", first_or_off);
        fprintf(stderr, "vertexCount: %u\n", count);
    }
    fprintf(stderr, "uploadedVerts: %u\n", nvert);
    fprintf(stderr, "baseVertex: 0\n");
    metal_dump_attribs();
}

/* PS3_TRACE_DROP=1 (diagnostic, OFF): every guest draw the Metal path throws
 * away, by reason, with its size -- several exits returned silently (a mesh
 * missing on screen, the GoW2 Colossus' shoulder, 2026-09-21, left no trace). */
static void metal_drop_note(const char* why, u32 prim, u32 count, int indexed)
{
    static int on = -1;
    if (on < 0) on = metal_env_on("PS3_TRACE_DROP");
    if (!on) return;
    static u32 n = 0;
    static u64 last_frame = ~0ull;
    static u32 per_frame = 0;
    if (s_state.frame_count != last_frame) { last_frame = s_state.frame_count; per_frame = 0; }
    n++;
    if (per_frame++ < 8u && (n <= 400u || (s_state.frame_count % 300ull) == 0ull || s_pick_phase)) {
        const rsx_state* st = s_draw.current_rsx;
        fprintf(stderr, "[DROP] f=%llu %s prim=%u n=%u indexed=%d idx16=%d vp_load=%u\n",
                (unsigned long long)s_state.frame_count, why, prim, count, indexed,
                st ? st->index_is_16bit : -1, st ? st->transform_program_load : 0u);
        fflush(stderr);
    }
}

static void metal_draw_arrays(void* ud, u32 primitive, u32 first, u32 count)
{
    (void)ud;
    u32 gid = metal_next_geom_id();
    if (!gid) {
        s_draw.total_draws++;
        return;
    }
    s_draw.total_draws++;
    if (metal_tl_on() && (s_draw.total_draws <= 16 || (s_draw.total_draws % 200) == 0))
        METAL_TL("draw_arrays #%llu prim=%u first=%u count=%u",
                 (unsigned long long)s_draw.total_draws, primitive, first, count);
    if (s_draw.total_draws <= 20 || (s_draw.total_draws % 1000) == 0) {
        printf("[RSX metal] draw_arrays #%llu prim=%u first=%u count=%u\n",
               (unsigned long long)s_draw.total_draws, primitive, first, count);
    }
    if (!draw_ensure_pso() || !draw_ensure_buffers()) { metal_drop_note("!draw_ensure_pso() || !draw_ensure_buffers()", primitive, count, 0); return; }
    if (count == 0 || count > metal_vb_region_verts()) { metal_drop_note("count == 0 || count > metal_vb_region_verts()", primitive, count, 0); return; }
    if (s_draw.draw_count >= METAL_MAX_DRAWS) { metal_drop_note("s_draw.draw_count >= METAL_MAX_DRAWS", primitive, count, 0); s_draw_dropped++; return; }

    MTLPrimitiveType prim = metal_map_prim(primitive);
    u32 n_conv = 0;
    u32 ib_off = 0;
    if (rsx_prim_needs_conversion(primitive)) {
        ib_off = s_draw.ib_offset;
        if (ib_off / 4u >= metal_ib_limit_idx()) { metal_drop_note("ib_off / 4u >= metal_ib_limit_idx()", primitive, count, 0); s_draw_dropped++; return; }
        u32 cap = metal_ib_limit_idx() - ib_off / 4u;
        n_conv = rsx_expand_prim_indices(primitive, 0, count,
                                         s_draw.ib_mapped + ib_off / 4u, cap);
        if (!n_conv) { metal_drop_note("!n_conv", primitive, count, 0); s_draw_dropped++; return; }
        s_draw.ib_offset += n_conv * 4u;
        prim = (primitive == RSX_PRIMITIVE_LINE_LOOP)
            ? MTLPrimitiveTypeLineStrip : MTLPrimitiveTypeTriangle;
    } else if (prim == (MTLPrimitiveType)0xFFFFFFFFu) {
        static int sk = 0;
        if (sk < 3) {
            printf("[RSX metal] draw_arrays: skip prim=%u (needs conversion)\n", primitive);
            sk++;
        }
        return;
    }

    u32 record_offset = s_draw.vb_offset;
    u32 actual = upload_vertices_from_rsx(first, count);
    if (actual == 0 || (n_conv && actual < count)) { metal_drop_note("actual == 0 || (n_conv && actual < count)", primitive, count, 0); s_draw_dropped++; return; }

    MetalDrawRec* dr = &s_draw.draws[s_draw.draw_count++];
    dr->geom_id        = gid;
    dr->vb_byte_offset = record_offset;
    dr->vertex_count   = n_conv ? n_conv : actual;
    dr->is_indexed     = n_conv ? 1u : 0u;
    dr->ib_byte_offset = ib_off;
    dr->prim           = prim;
    metal_draw_rec_set_pso(dr, metal_draw_pso_now());
    metal_draw_rec_capture_fc(dr);
    metal_draw_rec_capture_vc(dr);
    metal_draw_rec_capture_va(dr);
    metal_draw_rec_capture_state(dr);
    metal_prompt_cross_replace(dr);
    if (metal_dump_draw_id() == gid) {
        static int dump_left_a = 2;
        if (dump_left_a > 0) {
            dump_left_a--;
            metal_dump_geom_header(gid, primitive, 0, count, actual, first);
            metal_dump_positions(record_offset, actual, gid);
            fflush(stderr);
        }
    }
}

static void metal_draw_indexed(void* ud, u32 primitive, u32 offset, u32 count)
{
    (void)ud;
    u32 gid = metal_next_geom_id();
    if (!gid) {
        s_draw.total_draws++;
        if (metal_geom_skipped(s_frame_geom) && s_draw.current_rsx) {
            fprintf(stderr, "[SKIP GEOM %u] beginEndId=%u packet=%u prim=%u first=0x%X n=%u\n",
                    s_frame_geom, s_draw.current_rsx->begin_end_id,
                    s_draw.current_rsx->begin_end_packet, primitive, offset, count);
            fflush(stderr);
        }
        return;
    }
    s_draw.total_draws++;
    if (gid >= 490u && gid <= 500u) {
        const rsx_state* st = s_draw.current_rsx;
        fprintf(stderr, "[RSX DRAW %u] beginEndId=%u packet=%u prim=%u first=0x%X n=%u IB=0x%08X\n",
                gid, st ? st->begin_end_id : 0, st ? st->begin_end_packet : 0,
                primitive, offset, count, st ? st->index_array_addr : 0);
        fflush(stderr);
    }
    if (metal_tl_on() && (s_draw.total_draws <= 16 || (s_draw.total_draws % 200) == 0))
        METAL_TL("draw_indexed #%llu prim=%u off=%u count=%u",
                 (unsigned long long)s_draw.total_draws, primitive, offset, count);
    if (s_draw.total_draws <= 20) {
        printf("[RSX metal] draw_indexed prim=%u off=%u count=%u\n",
               primitive, offset, count);
    }
    if (!draw_ensure_pso() || !draw_ensure_buffers()) { metal_drop_note("!draw_ensure_pso() || !draw_ensure_buffers()", primitive, count, 1); return; }
    if (count == 0 || count > metal_ib_region_idx()) { metal_drop_note("count == 0 || count > metal_ib_region_idx()", primitive, count, 1); return; }
    if (s_draw.draw_count >= METAL_MAX_DRAWS) { metal_drop_note("s_draw.draw_count >= METAL_MAX_DRAWS", primitive, count, 1); s_draw_dropped++; return; }
    if (!vm_base || !s_draw.current_rsx) { metal_drop_note("!vm_base || !s_draw.current_rsx", primitive, count, 1); return; }

    MTLPrimitiveType prim = metal_map_prim(primitive);
    if (prim == (MTLPrimitiveType)0xFFFFFFFFu && !rsx_prim_needs_conversion(primitive))
        return;

    const rsx_state* state = s_draw.current_rsx;
    u32 elem_size = state->index_is_16bit ? 2u : 4u;
    /* location 0 = local, 1 = main (IO offset → guest EA). */
    u32 loc = state->index_location & 0xFu;
    u32 ea;
    if (loc == 0) {
        ea = 0xC0000000u + state->index_array_addr;
    } else {
        u32 main_ea = 0;
        if (gcm_io_offset_to_ea(state->index_array_addr, &main_ea))
            ea = main_ea;
        else
            ea = state->index_array_addr;
    }
    u32 read_addr = ea + offset * elem_size;
    if (!ppu_guest_range_committed(read_addr, count * elem_size)) { metal_drop_note("!ppu_guest_range_committed(read_addr, count * elem_size)", primitive, count, 1); return; }

    u32 max_index = 0, min_index = 0xFFFFFFFFu;
    const u8* isrc = vm_base + read_addr;
    u32 ib_off = s_draw.ib_offset;
    if (ib_off / 4u + count > metal_ib_limit_idx()) { metal_drop_note("ib_off / 4u + count > metal_ib_limit_idx()", primitive, count, 1); s_draw_dropped++; return; }
    u32 n_restart = 0;
    for (u32 i = 0; i < count; i++) {
        u32 idx = rsx_index_from_guest(isrc + i * elem_size, elem_size == 2);
        s_draw.ib_mapped[ib_off / 4u + i] = idx;
        if (rsx_index_is_restart(idx)) {
            n_restart++;
            continue;
        }
        if (idx > max_index) max_index = idx;
        if (idx < min_index) min_index = idx;
    }
    s_draw.ib_offset += count * 4u;
    if (count > 256u) {
        static int splitlog = 0;
        if (splitlog < 6) {
            splitlog++;
            fprintf(stderr, "[GEOM SPLIT] first_packet=256 rest=%u total=%u "
                    "total%%3=%u\n  last12 of packet0:",
                    count - 256u, count, count % 3u);
            for (u32 i = 244; i < 256 && i < count; i++) {
                u32 idx = rsx_index_from_guest(isrc + i * elem_size, elem_size == 2);
                fprintf(stderr, " %u", idx);
            }
            fprintf(stderr, "\n  first12 of packet1:");
            for (u32 i = 256; i < 268 && i < count; i++) {
                u32 idx = rsx_index_from_guest(isrc + i * elem_size, elem_size == 2);
                fprintf(stderr, " %u", idx);
            }
            fprintf(stderr, "\n  split would group (i253,i254,i255 leftover) vs "
                    "merged (i255,i256,i257)\n");
            fflush(stderr);
        }
    }
    /* Upload only the vertices this draw indexes, [min, max], and rebase the
     * indices: uploading 0..max for every draw re-sent a mesh's whole range per
     * sub-draw and filled the vertex buffer early in a 3D frame (E445: 2-3k
     * draws dropped per frame). Restart (0xFFFFFFFF) is not a vertex. */
    if (min_index > max_index) min_index = 0;
    if (min_index) {
        for (u32 i = 0; i < count; i++) {
            u32* p = &s_draw.ib_mapped[ib_off / 4u + i];
            if (!rsx_index_is_restart(*p))
                *p -= min_index;
        }
    }
    if (rsx_prim_needs_conversion(primitive)) {
        if (n_restart) { metal_drop_note("n_restart", primitive, count, 1); s_draw_dropped++; return; }
        enum { METAL_CONV_SCRATCH = 262144u };
        static u32 pos_scratch[METAL_CONV_SCRATCH];
        u32 dst_off = s_draw.ib_offset;
        if (dst_off / 4u >= metal_ib_limit_idx()) { metal_drop_note("dst_off / 4u >= metal_ib_limit_idx()", primitive, count, 1); s_draw_dropped++; return; }
        u32 cap = metal_ib_limit_idx() - dst_off / 4u;
        u32 n_idx = rsx_expand_indexed_prim(primitive,
                                           s_draw.ib_mapped + ib_off / 4u, count,
                                           pos_scratch, METAL_CONV_SCRATCH,
                                           s_draw.ib_mapped + dst_off / 4u, cap);
        if (!n_idx) { metal_drop_note("!n_idx", primitive, count, 1); s_draw_dropped++; return; }
        s_draw.ib_offset += n_idx * 4u;
        ib_off = dst_off;
        count = n_idx;
        prim = (primitive == RSX_PRIMITIVE_LINE_LOOP)
            ? MTLPrimitiveTypeLineStrip : MTLPrimitiveTypeTriangle;
    }
    {
        static int dlog = 0;
        if (dlog < 8 && getenv("PS3_TRACE_METAL_DRAW")) {
            dlog++;
            printf("[RSX metal] DRAW indexed prim=%u 16bit=%d n=%u restart=%u "
                   "min=%u max=%u idx0=%u idx1=%u idx2=%u\n",
                   primitive, state->index_is_16bit, count, n_restart, min_index,
                   max_index,
                   count > 0 ? s_draw.ib_mapped[ib_off / 4u] : 0,
                   count > 1 ? s_draw.ib_mapped[ib_off / 4u + 1] : 0,
                   count > 2 ? s_draw.ib_mapped[ib_off / 4u + 2] : 0);
        }
    }

    u32 record_offset = s_draw.vb_offset;
    /* RPCS3 upload_vertex_data: indexed draws fetch from (min + base_index) & 0xFFFFF. */
    const u32 vfirst = metal_vertex_base_on()
        ? ((min_index + state->vertex_data_base_index) & 0x000FFFFFu) : min_index;
    u32 nvert = upload_vertices_from_rsx(vfirst, max_index - min_index + 1);
    if (nvert == 0) { metal_drop_note("nvert == 0", primitive, count, 1); s_draw_dropped++; return; }

    {
        static int once = 0;
        if (once++ < 2 && getenv("PS3_TRACE_METAL_DRAW")) {
            MetalBasicVertex* vv =
                (MetalBasicVertex*)((u8*)s_draw.vb_mapped + record_offset);
            fprintf(stderr,
                    "[RSX metal] idx_draw n=%u max_i=%u v0=(%.2f,%.2f) "
                    "uv0=(%.2f,%.2f) idx0=%u\n",
                    count, max_index, vv[0].x, vv[0].y, vv[0].r, vv[0].g,
                    s_draw.ib_mapped[ib_off / 4u]);
            fflush(stderr);
        }
    }

    MetalDrawRec* dr = &s_draw.draws[s_draw.draw_count++];
    dr->geom_id        = gid;
    dr->vb_byte_offset = record_offset;
    dr->vertex_count   = count;
    dr->is_indexed     = 1;
    dr->ib_byte_offset = ib_off;
    dr->prim           = prim;
    metal_draw_rec_set_pso(dr, metal_draw_pso_now());
    metal_draw_rec_capture_fc(dr);
    metal_draw_rec_capture_vc(dr);
    metal_draw_rec_capture_va(dr);
    metal_draw_rec_capture_state(dr);
    metal_prompt_cross_replace(dr);
    if (metal_dump_draw_id() == gid) {
        static int dump_left_i = 2;
        if (dump_left_i <= 0) goto metal_draw_indexed_done;
        dump_left_i--;
        metal_dump_geom_header(gid, primitive, 1, count, nvert, offset);
        fprintf(stderr, "restartEnable: true (u16 0xFFFF -> u32 0xFFFFFFFF)\n");
        fprintf(stderr, "restartIndex: 0xFFFF (guest u16) / 0xFFFFFFFF (host)\n");
        fprintf(stderr, "restartCount: %u\n", n_restart);
        fprintf(stderr, "min_index: %u max_index: %u\n", min_index, max_index);
        fprintf(stderr, "indices:\n");
        u32 show = count < 32u ? count : 32u;
        u32 nvert_range = (max_index >= min_index) ? (max_index - min_index + 1u) : 0u;
        for (u32 i = 0; i < show; i++) {
            u32 raw;
            if (elem_size == 2) {
                u16 v;
                memcpy(&v, isrc + i * 2u, 2);
                raw = (u32)((v >> 8) | ((v & 0xFFu) << 8));
            } else {
                memcpy(&raw, isrc + i * 4u, 4);
                raw = ((raw >> 24) & 0xFFu) | ((raw >> 8) & 0xFF00u) |
                      ((raw << 8) & 0xFF0000u) | ((raw << 24) & 0xFF000000u);
            }
            u32 host = s_draw.ib_mapped[ib_off / 4u + i];
            if (raw == 0xFFFFu || host == RSX_INDEX_RESTART)
                fprintf(stderr, "%u:  %u  host=0x%08X RESTART\n", i, raw, host);
            else {
                fprintf(stderr, "%u:  %u  host=%u\n", i, raw, host);
                if (host >= nvert) {
                    fprintf(stderr,
                            "INVALID INDEX draw=%u i=%u idx=%u vertexCount=%u\n",
                            gid, i, host, nvert);
                }
            }
        }
        if (count > show)
            fprintf(stderr, "... (%u more)\n", count - show);
        metal_dump_positions(record_offset, nvert < nvert_range ? nvert : nvert_range,
                             gid);
        fflush(stderr);
    }
metal_draw_indexed_done:
    ;
}

static rsx_backend s_metal_backend = {
    .userdata          = &s_state,
    .init              = metal_init_cb,
    .shutdown          = metal_shutdown_cb,
    .begin_frame       = metal_begin_frame,
    .end_frame         = metal_end_frame,
    .present           = metal_present,
    .set_render_target = metal_set_render_target,
    .set_viewport      = metal_set_viewport,
    .set_blend         = metal_set_blend,
    .set_depth_stencil = metal_set_depth_stencil,
    .set_color_mask    = metal_set_color_mask,
    .set_alpha_test    = metal_set_alpha_test,
    .set_shader        = metal_set_shader,
    .set_vertex_attribs = metal_set_vertex_attribs,
    .clear             = metal_clear,
    .draw_arrays       = metal_draw_arrays,
    .draw_indexed      = metal_draw_indexed,
    .blit_scaled       = metal_blit_scaled,
    .bind_texture      = metal_bind_texture,
};

/* ---------------------------------------------------------------------------
 * Public API
 * -----------------------------------------------------------------------*/

/* ---------------------------------------------------------------------------
 * E489 -- a janela comporta-se como um jogo nativo do Mac (PS3_NATIVE_WINDOW=0
 * repoe o comportamento antigo).
 *
 * Tres coisas que denunciavam "isto e' uma app SDL", todas pedidas:
 *
 * 1. O CURSOR ficava por cima do jogo. `SDL_SetRelativeMouseMode` esconde-o E
 *    passa a entregar movimento RELATIVO, que e' o que a camera quer: sem isto
 *    o rato bate na borda do ecra e a camera para de virar.
 *
 * 2. O POPUP DE ACENTOS do macOS (segurar uma tecla mostra a' a^ a~ ...)
 *    aparecia por cima do jogo e engolia a tecla. Acontece porque o SDL deixa a
 *    entrada de texto LIGADA por omissao, e ai' a view passa a ser um
 *    NSTextInputClient. `SDL_StopTextInput()` corta pela raiz; o
 *    ApplePressAndHoldEnabled=NO fica como rede, caso algo volte a ligar a
 *    entrada de texto.
 *
 * 3. O protector de ecra podia entrar a meio de uma cena longa.
 *
 * ESCAPE deixa de fechar o jogo QUANDO o rato esta' capturado -- passa a
 * liberta-lo. Sem isso, esconder o cursor deixava o utilizador sem saida. Sem
 * captura, Escape continua a fechar, tal como antes: e' um superconjunto do
 * comportamento antigo, nunca menos. */
static int metal_native_window_on(void)
{
    static int on = -1;
    if (on < 0) { const char* e = getenv("PS3_NATIVE_WINDOW"); on = (e && e[0] == '0') ? 0 : 1; }
    return on;
}
static int s_mouse_grabbed;
static void metal_mouse_grab(int want)
{
    if (!metal_native_window_on() || !s_state.window) return;
    if (want == s_mouse_grabbed) return;
    if (SDL_SetRelativeMouseMode(want ? SDL_TRUE : SDL_FALSE) == 0)
        s_mouse_grabbed = want;
}

/* Task 3 (V2.0 capture/replay) -- native-size surface dump, registered with
 * rsx_capture_set_dump_hook below. Called by the PS3_RSX_CAPTURE recorder
 * (libs/video/rsx_capture.c, off by default) exactly once, synchronously,
 * right after it writes the END record for the last captured frame -- from
 * rsx_capture_on_present(), itself called from gcm_bridge_present_from()
 * (cellGcmSys.c), the documented ONE present choke point for every flip
 * (in-stream GCM_FLIP_COMMAND and the immediate HLE flip both funnel through
 * it). That call happens on the same thread and the same call stack as the
 * frame's own b->present(), right after it returns -- no other thread can be
 * encoding into these surfaces on this path (metal_present_impl's hle_movie
 * guard already keeps the guest and the movie-decode present paths mutually
 * exclusive), and this function itself only reads existing MTLTexture objects
 * (blit + an existing depth->colour convert), so it needs no extra lock: it
 * runs inside the same serialisation present already has. Task 4's replay
 * host calls this function directly after replaying a captured frame, off
 * the same single-threaded harness, for the same reason.
 *
 * Explicit assumption: the two producers that could touch these surfaces --
 * the RSX FIFO walker (wake thread) and the forced-flip path in
 * cellGcmSys.c (guest thread, ~cellGcmSys.c:3018-3042) -- are serialised by
 * the giant lock (ppu_giant_lock_*), and a present only runs with that
 * serialisation held. If that guarantee ever changes (e.g. the walker or a
 * forced flip starts encoding without the giant lock), this dump can race a
 * frame still being encoded and needs its own lock. */
int rsx_metal_dump_surfaces(const char* prefix)
{
    if (!prefix || !prefix[0] || !s_state.device || !s_state.queue) return 0;
    int n = 0;
    int skipped_fmt = 0;
    char path[1100];

    @autoreleasepool {
        /* Same idiom as the gameplay shot (~9291): commit and wait so the GPU
         * has finished all work queued before this call (the queue is FIFO;
         * an empty command buffer is a barrier for everything submitted
         * earlier, without needing a reference to whichever command buffer
         * the last present used). */
        id<MTLCommandBuffer> sync_cb = [s_state.queue commandBuffer];
        [sync_cb commit];
        [sync_cb waitUntilCompleted];
    }

    /* The surface the last present showed: the exact result metal_present_impl
     * already computed (cellGcmGetDisplayInfo(buffer_id)->offset ->
     * metal_surface_find(0, off, 0), falling back to the frame's last colour
     * target) and cached in s_last_show. Reusing the cached outcome instead of
     * re-deriving it is the most faithful reuse of that selection: it is
     * literally that code's answer for the frame just presented. */
    if (s_last_show >= 0 && (u32)s_last_show < s_n_surfaces && s_surfaces[s_last_show].tex) {
        snprintf(path, sizeof path, "%s_display.bmp", prefix);
        metal_dump_texture_to(s_surfaces[s_last_show].tex, path);
        n++;
    }

    for (u32 k = 0; k < s_n_surfaces; k++) {
        MetalSurface* sf = &s_surfaces[k];
        if (sf->is_depth || !sf->tex || !sf->inited) continue;
        if (sf->tex.pixelFormat != MTLPixelFormatBGRA8Unorm) { skipped_fmt++; continue; }
        snprintf(path, sizeof path, "%s_c%u_%08X_%ux%u.bmp", prefix, sf->loc, sf->offset,
                 (unsigned)sf->tex.width, (unsigned)sf->tex.height);
        metal_dump_texture_to(sf->tex, path);
        n++;
    }
    if (skipped_fmt > 0)
        fprintf(stderr, "[RSX metal] rsx_metal_dump_surfaces skipped %d non-BGRA8 colour surface(s)\n", skipped_fmt);

    /* Depth surfaces: reuse metal_depth_color_convert (already used to sample
     * depth as a colour texture, ~line 6262), which encodes a depth->BGRA8
     * pass into MetalSurface.conv -- no new depth conversion code here. */
    {
        int any_depth = 0;
        for (u32 k = 0; k < s_n_surfaces; k++)
            if (s_surfaces[k].is_depth && s_surfaces[k].tex && s_surfaces[k].inited) { any_depth = 1; break; }
        if (any_depth) {
            @autoreleasepool {
                id<MTLCommandBuffer> zcb = [s_state.queue commandBuffer];
                for (u32 k = 0; k < s_n_surfaces; k++)
                    if (s_surfaces[k].is_depth && s_surfaces[k].tex && s_surfaces[k].inited)
                        metal_depth_color_convert(zcb, (int)k);
                [zcb commit];
                [zcb waitUntilCompleted];
            }
            for (u32 k = 0; k < s_n_surfaces; k++) {
                MetalSurface* sf = &s_surfaces[k];
                if (!sf->is_depth || !sf->tex || !sf->inited || !sf->conv) continue;
                snprintf(path, sizeof path, "%s_z%u_%08X_%ux%u.bmp", prefix, sf->loc, sf->offset,
                         (unsigned)sf->tex.width, (unsigned)sf->tex.height);
                metal_dump_texture_to(sf->conv, path);
                n++;
            }
        }
    }

    fprintf(stderr, "[RSX metal] rsx_metal_dump_surfaces prefix=%s files=%d\n", prefix, n);
    return n;
}

int rsx_metal_backend_init(u32 width, u32 height, const char* title)
{
    memset((void*)&s_state, 0, sizeof(s_state));
    memset((void*)&s_draw, 0, sizeof(s_draw));
    memset(s_surfaces, 0, sizeof(s_surfaces));
    s_n_surfaces = 0;
    s_color_rt_idx = s_depth_rt_idx = -1;
    s_rt_selects = s_rt_creates = 0;
    metal_pipe_init_defaults();
    s_state.width  = width;
    s_state.height = height;
    s_state.clear_b = 64.0 / 255.0;   /* dark blue, matching the other backends */
    s_state.clear_a = 1.0;
    s_state.clear_depth = 1.0f;
    s_state.dump_frames = rsx_frame_dump_enabled();

    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
        printf("[RSX metal] ERROR: SDL_InitSubSystem(VIDEO) failed: %s\n",
               SDL_GetError());
        return -1;
    }

    /* PS3_FULLSCREEN=1 -> fullscreen borderless na resolucao do ecra (padrao
     * de jogo). FULLSCREEN_DESKTOP nao troca o modo de video do Mac (amigavel
     * a cmd-tab e ao compositor); ESC sai (ver pump_messages). OFF por default
     * -> janela, para smokes/dev e frame dump. */
    /* RESIZABLE: without it macOS has no maximize (green) button and the window
     * cannot be dragged to another size. The render targets keep the size they
     * were created with; present already scales (and letterboxes) to whatever
     * the drawable is, so a resize costs nothing but the scale. F11 or Cmd+Enter
     * toggle borderless fullscreen (see pump_messages). */
    Uint32 win_flags = SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_METAL |
                       SDL_WINDOW_RESIZABLE;
    {   /* PS3_WINDOW_HIDDEN=1 (diagnostic runs): no visible window, no focus theft */
        const char* hw = getenv("PS3_WINDOW_HIDDEN");
        if (hw && hw[0] == '1') win_flags = (win_flags & ~(Uint32)SDL_WINDOW_SHOWN) | SDL_WINDOW_HIDDEN;
    }
    /* Persisted overlay graphics settings are consumed here when the overlay
     * core was initialized before the backend: window size (points) and
     * fullscreen. PS3_FULLSCREEN, when set, still decides fullscreen. The
     * guest resolution (s_state.width/height) never changes. Without an
     * initialized overlay nothing here differs from before. */
    int win_w = (int)width, win_h = (int)height;
    rsx_overlay_graphics_settings overlay_graphics;
    const int have_overlay_graphics = rsx_overlay_get_graphics_settings(&overlay_graphics);
    s_overlay_graphics_known = 0;
    if (have_overlay_graphics) {
        if (overlay_graphics.window_width > 0 && overlay_graphics.window_height > 0) {
            win_w = (int)overlay_graphics.window_width;
            win_h = (int)overlay_graphics.window_height;
        }
    }
    if (rsx_overlay_resolve_flag(getenv("PS3_FULLSCREEN"), have_overlay_graphics,
                                 have_overlay_graphics ? overlay_graphics.fullscreen : 0, 0))
        win_flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    else
        win_flags &= ~(Uint32)SDL_WINDOW_FULLSCREEN_DESKTOP;
    /* Nome da janela a partir do proprio jogo (PARAM.SFO TITLE); fallback ao
     * title passado pelo host, senao generico. */
    char sfo_title[256];
    const char* wtitle = (title && title[0]) ? title : "ps3recomp";
    if (gid_read_title(sfo_title, sizeof sfo_title) == 0 && sfo_title[0]) {
        wtitle = sfo_title;
        fprintf(stderr, "[identity] window title <- PARAM.SFO: \"%s\"\n", wtitle);
    }
    if (metal_native_window_on()) {
        /* Antes de criar a janela: clicar para focar tambem conta como clique no
         * jogo, e o ecra inteiro usa o fullscreen nativo do macOS. */
        SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
        SDL_SetHint(SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES, "1");
    }
    snprintf(s_base_title, sizeof s_base_title, "%s \xE2\x80\x94 Metal", wtitle);
    s_state.window = SDL_CreateWindow(
        s_base_title,
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        win_w, win_h,
        win_flags);
    if (!s_state.window) {
        printf("[RSX metal] ERROR: SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return -1;
    }

    if (metal_native_window_on()) {
        SDL_StopTextInput();          /* mata o popup de acentos do macOS */
        SDL_DisableScreenSaver();
        [[NSUserDefaults standardUserDefaults] setBool:NO
                                                forKey:@"ApplePressAndHoldEnabled"];
        /* Como o RPCS3: o cursor so' fica ESCONDIDO sobre a janela (fora dela
         * volta ao normal) e o rato nao e' capturado ao abrir -- so' quando o
         * utilizador clica na janela (ver SDL_MOUSEBUTTONDOWN). A camera nao
         * depende da captura: le deltas brutos do GCMouse. */
        SDL_ShowCursor(SDL_DISABLE);
        fprintf(stderr, "[janela] modo nativo: cursor escondido sobre a janela, clique captura, "
                        "entrada de texto desligada (Escape liberta o rato / sai da tela cheia; "
                        "PS3_NATIVE_WINDOW=0 desliga)\n");
    }

    s_state.view = SDL_Metal_CreateView(s_state.window);
    if (!s_state.view) {
        printf("[RSX metal] ERROR: SDL_Metal_CreateView failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(s_state.window);
        s_state.window = NULL;
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return -1;
    }

    /* Take each object through a strong local first, then hand ownership to
     * the struct with CFBridgingRetain (+1). Assigning an ARC-returned object
     * straight into an __unsafe_unretained field would let ARC release it at
     * the end of the statement, leaving a dangling pointer. */
    CAMetalLayer* layer = (__bridge CAMetalLayer*)SDL_Metal_GetLayer(s_state.view);
    if (!layer) {
        printf("[RSX metal] ERROR: SDL_Metal_GetLayer returned nil\n");
        rsx_metal_backend_shutdown();
        return -1;
    }
    s_state.layer = (__bridge CAMetalLayer*)CFBridgingRetain(layer);

    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (!device) {
        printf("[RSX metal] ERROR: no Metal device available\n");
        rsx_metal_backend_shutdown();
        return -1;
    }
    s_state.device = (__bridge id<MTLDevice>)CFBridgingRetain(device);

    id<MTLCommandQueue> queue = [device newCommandQueue];
    if (!queue) {
        printf("[RSX metal] ERROR: newCommandQueue failed\n");
        rsx_metal_backend_shutdown();
        return -1;
    }
    s_state.queue = (__bridge id<MTLCommandQueue>)CFBridgingRetain(queue);

    s_state.layer.device          = s_state.device;
    s_state.layer.pixelFormat     = MTLPixelFormatBGRA8Unorm;
    /* Movie overlay samples a separate texture into the drawable; keep
     * framebufferOnly=NO so present path stays flexible (and frame dump works). */
    s_state.layer.framebufferOnly = NO;
    {   /* Same precedence as the launcher test (rsx_overlay_resolve_flag). */
        const int vsync = rsx_overlay_resolve_flag(getenv("PS3_METAL_VSYNC"), have_overlay_graphics,
                                                   have_overlay_graphics ? overlay_graphics.vsync : 1, 1);
        s_state.layer.displaySyncEnabled = vsync ? YES : NO;
    }
    s_state.layer.maximumDrawableCount = 3;
    if (metal_env_default("PS3_METAL_HDR", 0)) {
        s_state.layer.wantsExtendedDynamicRangeContent = YES;
        CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceExtendedSRGB);
        if (cs) {
            s_state.layer.colorspace = cs;
            CGColorSpaceRelease(cs);
        }
    }

    /* Alinha drawableSize ao backing REAL do ecra (fullscreen ou HIGHDPI) —
     * senao o fullscreen ficaria a apresentar a 1280x720 e so' corrigia num
     * resize. O guest continua a 720p; o present faz upscale (MetalFX/blit)
     * para este tamanho. */
    {
        int dpw = 0, dph = 0;
        SDL_Metal_GetDrawableSize(s_state.window, &dpw, &dph);
        if (dpw > 0 && dph > 0)
            s_state.layer.drawableSize = CGSizeMake(dpw, dph);
    }

    s_state.last_fps_ticks = SDL_GetTicks64();

    /* Runtime overlay. Optional: without the renderer the game renders
     * exactly as before and the UI never appears. Every graphics setting is
     * applied live by the event thread (rsx_metal_backend_pump_messages):
     * VSync on the layer, fullscreen and window size on the SDL window. */
    rsx_overlay_frame_timer_init(&s_overlay_timer);
    s_overlay_guest_draws_total = 0;
    s_overlay_ui = rsx_overlay_metal_renderer_create(s_state.device,
                                                      s_state.layer.pixelFormat, NULL);
    if (!s_overlay_ui) {
        fprintf(stderr, "[RSX metal] overlay renderer unavailable\n");
        /* Nobody could see the menu: stop the core from toggling it and from
         * withholding pad/keyboard input. */
        rsx_overlay_disable("Metal overlay renderer unavailable");
    } else {
        /* Console shell on macOS: SF Pro / SF Symbols atlas, UI sounds on a
         * dedicated AVAudioEngine, haptic ticks on the pad's async queue. */
        rsx_overlay_set_asset_builder(rsx_overlay_assets_build_apple);
        rsx_overlay_feedback_apple_set_haptic(cpgc_ui_tick);
        rsx_overlay_set_feedback_sink(rsx_overlay_feedback_apple_play, NULL);
    }
    {
        rsx_overlay_backend_capabilities caps;
        caps.window_size = RSX_OVERLAY_SETTING_LIVE;
        caps.fullscreen = RSX_OVERLAY_SETTING_LIVE;
        caps.vsync = RSX_OVERLAY_SETTING_LIVE;
        rsx_overlay_set_backend_capabilities(&caps);
    }
    if (have_overlay_graphics) {
        s_overlay_graphics = overlay_graphics;
        s_overlay_graphics_known = 1;
    }

    rsx_set_backend(&s_metal_backend);

    /* Icone do Dock a partir do ICON0.PNG do jogo (SDL ja' criou o NSApp). */
    gid_set_dock_icon();

    printf("[RSX metal] device: %s\n", [s_state.device.name UTF8String]);
    printf("[RSX metal] Window created: %ux%u (drawable %.0fx%.0f)%s%s "
           "MetalFX=%d pass-merge=%d gpu-deswizzle=%d vsync=%d hdr=%d\n",
           width, height,
           s_state.layer.drawableSize.width, s_state.layer.drawableSize.height,
           (SDL_GetWindowFlags(s_state.window) & SDL_WINDOW_FULLSCREEN_DESKTOP)
               ? " [fullscreen]" : "",
           s_state.dump_frames ? " (frame dump on)" : "",
           metal_env_default("PS3_METALFX", 1),
           metal_env_default("PS3_METAL_PASS_MERGE", 1),
           metal_env_default("PS3_METAL_GPU_DESWIZZLE", 1),
           metal_env_default("PS3_METAL_VSYNC", 1),
           metal_env_default("PS3_METAL_HDR", 0));

    /* PS3_RSX_CAPTURE recorder (rsx_capture.c, off by default): register the
     * native-size surface dump so it can produce the "_live" prefix once,
     * after the last captured present. No-op unless that env var is set. */
    rsx_capture_set_dump_hook(rsx_metal_dump_surfaces);

    return 0;
}

void rsx_metal_movie_playback_begin(void)
{
    s_content.movie_hle = 1;
    rsx_host_movie_hle_set(1);
    s_content.paused = 1;
    METAL_TL("movie_begin");
    fprintf(stderr, "[CONTENT] pause hold for movie HLE\n");
    fflush(stderr);
    movie_mu_init_once();
    pthread_mutex_lock(&s_movie.mu);
    s_movie.active = 1;
    s_movie.hle_source = 1;
    pthread_mutex_unlock(&s_movie.mu);
    printf("[RSX metal] movie playback begin\n");
    fflush(stdout);
}

/* Metal content flags. Logo arming is rsx_metal_movie_playback_end, which
 * the unit test calls without this file. Returns 0 when the diagnostic
 * sticky hold owns this end, so the caller must not arm the logo queue. */
static int metal_movie_session_end(void)
{
    int was_hle = s_content.movie_hle;
    s_content.movie_hle = 0;
    s_content.paused = 0;
    METAL_TL("movie_end hold=%d was_hle=%d %ux%u",
             s_content.hold, was_hle, s_content.w, s_content.h);
    const char* v = getenv("PS3_CONTENT_HOLD_AFTER_MOVIE");
    if (v && v[0] && v[0] != '0' && s_content.hold && s_content.rgba) {
        s_content.pending = 1;
        fprintf(stderr, "[CONTENT] re-arm hold after movie %ux%u\n",
                s_content.w, s_content.h);
        fflush(stderr);
        return 0;
    }
    /* Intro (index 0) keeps the hold so the logo queue can replace it.
     * Any other end drops the hold here; playback_end will not arm logos. */
    if (was_hle && !movie_clock_arms_logos())
        s_content.hold = 0;
    return 1;
}

void rsx_metal_content_hold_clear(void)
{
    s_content.hold = 0;
    s_content.paused = 0;
    s_content.pending = 0;
    /* Keep rgba/bgra for optional re-arm; just stop overlay. */
    METAL_TL("content_hold_clear");
    fprintf(stderr, "[CONTENT] hold cleared\n");
    fflush(stderr);
}

void rsx_metal_content_hold(const void* rgba, unsigned w, unsigned h)
{
    if (!rgba || !w || !h) return;
    size_t nbytes = (size_t)w * (size_t)h * 4u;
    free(s_content.rgba);
    free(s_content.bgra);
    s_content.rgba = (u8*)malloc(nbytes);
    s_content.bgra = (u8*)malloc(nbytes);
    if (!s_content.rgba || !s_content.bgra) {
        free(s_content.rgba); s_content.rgba = NULL;
        free(s_content.bgra); s_content.bgra = NULL;
        s_content.hold = 0;
        return;
    }
    memcpy(s_content.rgba, rgba, nbytes);
    content_rgba_to_bgra((const u8*)rgba, s_content.bgra, w * h);
    s_content.w = w;
    s_content.h = h;
    s_content.hold = 1;
    s_content.pending = 1;
    METAL_TL("content_hold %ux%u", w, h);
    if (!s_content.logged) {
        s_content.logged = 1;
        fprintf(stderr, "[CONTENT] hold stored %ux%u (HOSTRES texture)\n", w, h);
        fflush(stderr);
    }
    if (s_content.movie_hle || s_content.paused) {
        fprintf(stderr, "[CONTENT] hold stored but paused (movie HLE active)\n");
        fflush(stderr);
        return;
    }
    /* Immediate present of sticky overlay when no movie. */
    if (s_state.device)
        rsx_metal_movie_present();
}

void rsx_metal_force_bind_rgba(unsigned unit, const void* rgba, unsigned w, unsigned h)
{
    if (!rgba || !w || !h || unit >= RSX_MAX_TEXTURES || !s_state.device) {
        static int once = 0;
        if (!once && (!s_state.device)) {
            once = 1;
            fprintf(stderr, "[FORCE-TEX] metal device not ready unit=%u %ux%u\n",
                    unit, w, h);
            fflush(stderr);
        }
        return;
    }
    size_t nbytes = (size_t)w * (size_t)h * 4u;
    u8* bgra = (u8*)malloc(nbytes);
    if (!bgra) return;
    content_rgba_to_bgra((const u8*)rgba, bgra, w * h);
    if (metal_tex_upload_slot(unit, MTLPixelFormatBGRA8Unorm, w, h, bgra, w * 4u,
                              RSX_TEXTURE_A8R8G8B8)) {
        fprintf(stderr,
                "[FORCE-TEX] metal bind unit=%u %ux%u (native MTLTexture)\n",
                unit, w, h);
        fflush(stderr);
    }
    free(bgra);
}

/* Gated synthetic hold for M8 smoke (no game assets). */
static void content_hold_demo_maybe(void)
{
    static int done = -1;
    if (done >= 0) return;
    const char* e = getenv("PS3_METAL_CONTENT_HOLD_DEMO");
    done = (e && e[0] && e[0] != '0') ? 1 : 0;
    if (!done) return;
    const u32 w = 64, h = 64;
    u8* rgba = (u8*)malloc((size_t)w * h * 4);
    if (!rgba) return;
    for (u32 y = 0; y < h; y++) {
        for (u32 x = 0; x < w; x++) {
            int on = ((x / 8) ^ (y / 8)) & 1;
            u8* p = rgba + (y * w + x) * 4;
            if (on) { p[0] = 0; p[1] = 220; p[2] = 80; p[3] = 255; }
            else    { p[0] = 220; p[1] = 40; p[2] = 180; p[3] = 255; }
        }
    }
    fprintf(stderr, "[CONTENT] DEMO hold checkerboard %ux%u\n", w, h);
    fflush(stderr);
    rsx_metal_content_hold(rgba, w, h);
    free(rgba);
}

void rsx_metal_movie_present(void)
{
    /* Present from the decode thread without SDL_PollEvent — on macOS
     * Cocoa_PumpEvents must run on the main thread (NSInternalInconsistency
     * otherwise). Metal command buffers + presentDrawable are OK off-main. */
    metal_present(NULL, 0);
}

int rsx_metal_movie_overlay_active(void)
{
    /* True only for M0/M4 HLE path — not cellVdec staged frames. */
    int on = 0;
    movie_mu_init_once();
    pthread_mutex_lock(&s_movie.mu);
    on = s_movie.active && s_movie.hle_source &&
         (s_movie.use_cv || s_movie.staging != NULL);
    pthread_mutex_unlock(&s_movie.mu);
    return on;
}

void rsx_metal_movie_frame_stage(const void* bgra, unsigned w, unsigned h)
{
    if (!bgra || w == 0 || h == 0) return;
    movie_mu_init_once();
    pthread_mutex_lock(&s_movie.mu);
    /* CPU path supersedes CV for this frame. */
    movie_release_cv_locked();
    size_t nbytes = (size_t)w * (size_t)h * 4u;
    if (!s_movie.staging || s_movie.w != w || s_movie.h != h) {
        free(s_movie.staging);
        s_movie.staging = (u8*)malloc(nbytes);
        s_movie.w = w;
        s_movie.h = h;
    }
    if (s_movie.staging) {
        memcpy(s_movie.staging, bgra, nbytes);
        s_movie.pending = 1;
        s_movie.active  = 1;
        /* cellVdec stages with hle_source=0 so overlay_active stays false. */
        s_movie.hle_source = 0;
        s_movie.frames_rgba++;
    }
    pthread_mutex_unlock(&s_movie.mu);
}

void rsx_metal_movie_frame(const void* bgra, unsigned w, unsigned h)
{
    rsx_metal_movie_frame_stage(bgra, w, h);
    movie_mu_init_once();
    pthread_mutex_lock(&s_movie.mu);
    s_movie.hle_source = 1; /* HLE / explicit present path */
    /* H-M1: sample staging at start + mid logo (SmLogo fades from black). */
    if (s_movie.staging) {
        u64 fr = s_movie.frames_rgba;
        if (fr <= 3 || fr == 30 || fr == 60 || fr == 120 || fr == 180) {
            u32 nb = 0, mx = 0, n = w * h;
            const u8* p = s_movie.staging;
            for (u32 i = 0; i < n; i += 64) {
                const u8* q = p + i * 4;
                if (q[0] > mx) mx = q[0];
                if (q[1] > mx) mx = q[1];
                if (q[2] > mx) mx = q[2];
                if (q[0] > 8 || q[1] > 8 || q[2] > 8) nb++;
            }
            fprintf(stderr,
                    "[movie-metal] stage #%llu %ux%u nonblack_samples=%u/%u max=%u\n",
                    (unsigned long long)fr, w, h, nb, n / 64 + 1, mx);
            fflush(stderr);
        }
    }
    pthread_mutex_unlock(&s_movie.mu);
    /* H-M1: default guest-only present. Dual-thread nextDrawable (decode +
     * guest flip) painted pure-black frame dumps while legal content-hold
     * (guest-only) stayed nonblack. Opt out: PS3_MOVIE_PRESENT_ON_GUEST=0. */
    if (!movie_present_on_guest())
        rsx_metal_movie_present();
}

void rsx_metal_movie_frame_cvpixel(void* cv_pixel_buffer)
{
    CVPixelBufferRef buf = (CVPixelBufferRef)cv_pixel_buffer;
    if (!buf) return;

    /* H-M1: default CPU BGRA → MTLTexture (same path as content-hold).
     * CVMetalTexture zero-copy still composites pure black on guest present
     * even with alwaysCopiesSampleData=YES and nonblack CPU probes on the
     * same CVPixelBuffer (cv_src #61 max=250). Opt-in zero-copy:
     *   PS3_MOVIE_CV_ZEROCOPY=1
     * Legacy alias: PS3_MOVIE_FORCE_RGBA=1 forces CPU (redundant with default). */
    static int use_cv_zerocopy = -1;
    if (use_cv_zerocopy < 0) {
        const char* zc = getenv("PS3_MOVIE_CV_ZEROCOPY");
        const char* fr = getenv("PS3_MOVIE_FORCE_RGBA");
        /* Default OFF (CPU path). ZC only if explicitly 1 and FORCE_RGBA not set. */
        use_cv_zerocopy = (zc && zc[0] == '1') ? 1 : 0;
        if (fr && fr[0] && fr[0] != '0')
            use_cv_zerocopy = 0;
        fprintf(stderr, "[movie-metal] present_path=%s (CV_ZEROCOPY=%d)\n",
                use_cv_zerocopy ? "iosurface" : "rgba-cpu", use_cv_zerocopy);
        fflush(stderr);
    }
    if (!use_cv_zerocopy) {
        movie_frame_bgra_from_cv(buf);
        return;
    }

    if (!s_state.device || !movie_ensure_cv_cache()) {
        s_movie.cv_fail++;
        movie_frame_bgra_from_cv(buf);
        return;
    }

    size_t w = CVPixelBufferGetWidth(buf);
    size_t h = CVPixelBufferGetHeight(buf);
    OSType fmt = CVPixelBufferGetPixelFormatType(buf);
    if (w == 0 || h == 0 || fmt != kCVPixelFormatType_32BGRA) {
        static int bad = 0;
        if (bad++ < 3)
            printf("[RSX metal] movie cvpixel unsupported fmt=0x%08X %zux%zu → RGBA\n",
                   (unsigned)fmt, w, h);
        s_movie.cv_fail++;
        movie_frame_bgra_from_cv(buf);
        return;
    }

    /* H-M1: sample CV pixels before Metal (proves decode ≠ black). */
    if (s_movie.frames_cv < 3 || s_movie.frames_cv == 60 ||
        s_movie.frames_cv == 120) {
        CVPixelBufferLockBaseAddress(buf, kCVPixelBufferLock_ReadOnly);
        const u8* base = (const u8*)CVPixelBufferGetBaseAddress(buf);
        size_t stride = CVPixelBufferGetBytesPerRow(buf);
        u32 nb = 0, samples = 0, mx = 0;
        if (base) {
            for (size_t y = 0; y < h; y += 16) {
                const u8* row = base + y * stride;
                for (size_t x = 0; x < w; x += 16) {
                    const u8* q = row + x * 4;
                    if (q[0] > mx) mx = q[0];
                    if (q[1] > mx) mx = q[1];
                    if (q[2] > mx) mx = q[2];
                    samples++;
                    if (q[0] > 8 || q[1] > 8 || q[2] > 8) nb++;
                }
            }
        }
        CVPixelBufferUnlockBaseAddress(buf, kCVPixelBufferLock_ReadOnly);
        fprintf(stderr,
                "[movie-metal] cv_src #%llu %zux%zu nonblack=%u/%u max=%u\n",
                (unsigned long long)(s_movie.frames_cv + 1), w, h, nb, samples,
                mx);
        fflush(stderr);
    }

    CVMetalTextureRef cvTex = NULL;
    CVReturn cr = CVMetalTextureCacheCreateTextureFromImage(
        kCFAllocatorDefault, s_movie.tex_cache, buf, NULL,
        MTLPixelFormatBGRA8Unorm, (size_t)w, (size_t)h, 0, &cvTex);
    if (cr != kCVReturnSuccess || !cvTex || !CVMetalTextureGetTexture(cvTex)) {
        static int fail_log = 0;
        if (fail_log++ < 5)
            printf("[RSX metal] CVMetalTextureCacheCreateTextureFromImage failed (%d)\n",
                   (int)cr);
        if (cvTex) CFRelease(cvTex);
        s_movie.cv_fail++;
        movie_frame_bgra_from_cv(buf);
        return;
    }

    movie_mu_init_once();
    pthread_mutex_lock(&s_movie.mu);
    movie_release_cv_locked();
    s_movie.cv_mtl = cvTex; /* ownership from CreateTextureFromImage */
    s_movie.cv_buf = (CVPixelBufferRef)CFRetain(buf);
    s_movie.w = (u32)w;
    s_movie.h = (u32)h;
    s_movie.use_cv = 1;
    s_movie.active = 1;
    s_movie.hle_source = 1;
    s_movie.pending = 0;
    s_movie.frames_cv++;
    if (s_movie.frames_cv == 1 || (s_movie.frames_cv % 120) == 0) {
        printf("[RSX metal] movie frame cv #%llu %ux%u (iosurface)\n",
               (unsigned long long)s_movie.frames_cv, (u32)w, (u32)h);
        fflush(stdout);
    }
    pthread_mutex_unlock(&s_movie.mu);

    if (s_movie.tex_cache)
        CVMetalTextureCacheFlush(s_movie.tex_cache, 0);

    if (!movie_present_on_guest())
        rsx_metal_movie_present();
}

void rsx_metal_movie_stop(void)
{
    /* End-of-movie bookkeeping first (may re-arm content hold). */
    if (metal_movie_session_end())
        rsx_metal_movie_playback_end();
    else
        rsx_host_movie_hle_set(0);
    movie_mu_init_once();
    pthread_mutex_lock(&s_movie.mu);
    /* Keep last frame if we have staging/cv for one more present; clear active
     * when nothing to show. CV path releases below. */
    int had = (s_movie.staging != NULL) || s_movie.use_cv;
    s_movie.active  = 0;
    s_movie.pending = 0;
    free(s_movie.staging);
    s_movie.staging = NULL;
    s_movie.w = s_movie.h = 0;
    movie_release_cv_locked();
    u64 ncv = s_movie.frames_cv, nrgba = s_movie.frames_rgba, nfail = s_movie.cv_fail;
    pthread_mutex_unlock(&s_movie.mu);
    (void)had;
    printf("[RSX metal] movie stop (frames_cv=%llu frames_rgba=%llu cv_fail=%llu)\n",
           (unsigned long long)ncv, (unsigned long long)nrgba, (unsigned long long)nfail);
    fflush(stdout);
}

void rsx_metal_backend_shutdown(void)
{
    rsx_metal_movie_stop();
    rsx_overlay_set_backend_capabilities(NULL);
    rsx_overlay_set_feedback_sink(NULL, NULL);
    rsx_overlay_feedback_apple_set_haptic(NULL);
    rsx_overlay_feedback_apple_shutdown();
    rsx_overlay_set_asset_builder(NULL);
    rsx_overlay_metal_renderer_destroy(s_overlay_ui);
    s_overlay_ui = NULL;
    free(s_content.rgba); s_content.rgba = NULL;
    free(s_content.bgra); s_content.bgra = NULL;
    if (s_content.tex) {
        CFRelease((__bridge CFTypeRef)s_content.tex);
        s_content.tex = nil;
    }
    s_content.hold = s_content.paused = s_content.movie_hle = 0;
    if (s_movie.tex_cache) {
        CFRelease(s_movie.tex_cache);
        s_movie.tex_cache = NULL;
    }
    if (s_movie.tex) {
        CFRelease((__bridge CFTypeRef)s_movie.tex);
        s_movie.tex = nil;
    }
    if (s_movie.pso) {
        CFRelease((__bridge CFTypeRef)s_movie.pso);
        s_movie.pso = nil;
    }
    if (s_movie.sampler) {
        CFRelease((__bridge CFTypeRef)s_movie.sampler);
        s_movie.sampler = nil;
    }
    if (s_prompt_overlay.tex) {
        CFRelease((__bridge CFTypeRef)s_prompt_overlay.tex);
        s_prompt_overlay.tex = nil;
    }
    if (s_prompt_overlay.pso) {
        CFRelease((__bridge CFTypeRef)s_prompt_overlay.pso);
        s_prompt_overlay.pso = nil;
    }
    if (s_prompt_overlay.sampler) {
        CFRelease((__bridge CFTypeRef)s_prompt_overlay.sampler);
        s_prompt_overlay.sampler = nil;
    }
    s_prompt_overlay.ready = 0;
    s_prompt_overlay.w = s_prompt_overlay.h = 0;
    s_movie.pso_ready = 0;
    s_movie.tex_w = s_movie.tex_h = 0;

    if (getenv("PS3_TRACE_RSX_SHADERS")) {
        fprintf(stderr,
                "[RSX-SH metal] hits=%u misses=%u ok=%u fail=%u "
                "vp_ok=%u vp_fail=%u cache=%u\n",
                s_draw.fp_cache_hits, s_draw.fp_cache_misses,
                s_draw.fp_decompile_ok, s_draw.fp_decompile_fail,
                s_draw.vp_decompile_ok, s_draw.vp_decompile_fail,
                s_draw.shader_cache_count);
    }
    fprintf(stderr, "[RSX metal] textures: bind_ok=%u skip=%u deswizzle=%u\n",
            s_draw.bind_ok, s_draw.bind_skip, s_draw.bind_deswizzle);
    fprintf(stderr, "[RSX metal] RT: selects=%u creates=%u surfaces=%u cidx=%d didx=%d\n",
            s_rt_selects, s_rt_creates, s_n_surfaces, s_color_rt_idx, s_depth_rt_idx);
    fprintf(stderr,
            "[RSX metal] attribs: layout_ok=%d unknown=%d upload_ok=%u "
            "fallback=%u skip=%u sig=0x%08X\n",
            s_draw.layout_ok, s_draw.layout_unknown, s_draw.upload_ok,
            s_draw.upload_fallback, s_draw.upload_skip, s_draw.attrib_sig);
    for (u32 i = 0; i < s_draw.shader_cache_count; i++) {
        if (s_draw.shader_cache[i].pso) {
            CFRelease((__bridge CFTypeRef)s_draw.shader_cache[i].pso);
            s_draw.shader_cache[i].pso = nil;
        }
    }
    s_draw.shader_cache_count = 0;
    s_draw.current_shader = NULL;
    if (s_draw.vp_const_buf) {
        CFRelease((__bridge CFTypeRef)s_draw.vp_const_buf);
        s_draw.vp_const_buf = nil;
    }
    for (u32 i = 0; i < RSX_MAX_TEXTURES; i++) {
        if (s_draw.tex_slots[i].tex) {
            CFRelease((__bridge CFTypeRef)s_draw.tex_slots[i].tex);
            s_draw.tex_slots[i].tex = nil;
        }
        s_draw.tex_slots[i].valid = 0;
    }
    if (s_draw.tex_demo_pso) {
        CFRelease((__bridge CFTypeRef)s_draw.tex_demo_pso);
        s_draw.tex_demo_pso = nil;
    }
    s_draw.tex_demo_pso_ready = 0;
    for (u32 i = 0; i < s_ds_cache_count; i++) {
        if (s_ds_cache[i].state) {
            CFRelease((__bridge CFTypeRef)s_ds_cache[i].state);
            s_ds_cache[i].state = nil;
        }
    }
    s_ds_cache_count = 0;
    if (s_depth_tex) {
        CFRelease((__bridge CFTypeRef)s_depth_tex);
        s_depth_tex = nil;
    }
    s_depth_tex_w = s_depth_tex_h = 0;
    for (u32 i = 0; i < s_n_surfaces; i++) {
        if (s_surfaces[i].tex) {
            CFRelease((__bridge CFTypeRef)s_surfaces[i].tex);
            s_surfaces[i].tex = nil;
        }
        if (s_surfaces[i].conv) {
            CFRelease((__bridge CFTypeRef)s_surfaces[i].conv);
            s_surfaces[i].conv = nil;
        }
    }
    s_n_surfaces = 0;
    s_color_rt_idx = s_depth_rt_idx = -1;
    if (s_draw.dummy_tex) {
        CFRelease((__bridge CFTypeRef)s_draw.dummy_tex);
        s_draw.dummy_tex = nil;
    }
    if (s_draw.dummy_samp) {
        CFRelease((__bridge CFTypeRef)s_draw.dummy_samp);
        s_draw.dummy_samp = nil;
    }
    if (s_draw.vb) { CFRelease((__bridge CFTypeRef)s_draw.vb); s_draw.vb = nil; }
    if (s_draw.ib) { CFRelease((__bridge CFTypeRef)s_draw.ib); s_draw.ib = nil; }
    if (s_draw.pso) { CFRelease((__bridge CFTypeRef)s_draw.pso); s_draw.pso = nil; }
    s_draw.vb_mapped = NULL;
    s_draw.ib_mapped = NULL;
    s_draw.pso_ready = 0;

    if (s_state.queue) {
        CFRelease((__bridge CFTypeRef)s_state.queue);
        s_state.queue = nil;
    }
    if (s_state.device) {
        CFRelease((__bridge CFTypeRef)s_state.device);
        s_state.device = nil;
    }
    if (s_state.layer) {
        CFRelease((__bridge CFTypeRef)s_state.layer);
        s_state.layer = nil;
    }
    if (s_state.view) {
        SDL_Metal_DestroyView(s_state.view);
        s_state.view = NULL;
    }
    if (s_state.window) {
        SDL_DestroyWindow(s_state.window);
        s_state.window = NULL;
    }
    SDL_QuitSubSystem(SDL_INIT_VIDEO);

    rsx_set_backend(NULL);
    printf("[RSX metal] Backend shut down after %llu frames\n",
           (unsigned long long)s_state.frame_count);
}

/*
 * The player closed the window (close button or Cmd+Q). Nothing consumed the
 * old window_closed flag, so the game kept running and the window stayed up.
 * Leave at once with _exit: exit() would run atexit/static destructors that
 * can block on the guest giant lock held by other threads. By design this path
 * skips rsx_overlay_shutdown (the host's orderly exit calls it): the pump
 * already persists the overlay settings whenever the shell is closed, and the
 * shell's "Sair" persists them right before calling here. Add no blocking
 * work here.
 */
static void metal_quit_now(const char* why)
{
    fprintf(stderr, "[RSX metal] %s: exiting\n", why);
    fflush(stderr);
    _exit(0);
}

/* Event thread: applies graphics settings the overlay's Graphics tab changed
 * since the last pump. Only a change is applied, so F11 / Cmd+Enter and the
 * env overrides keep working as before; with no initialized overlay this
 * does nothing. The first observation (overlay initialized after the
 * backend) is only recorded. */
static void metal_overlay_apply_graphics(void)
{
    rsx_overlay_graphics_settings g;
    if (!s_state.window || !pthread_main_np() || !rsx_overlay_get_graphics_settings(&g))
        return;
    if (!s_overlay_graphics_known) {
        s_overlay_graphics = g;
        s_overlay_graphics_known = 1;
        return;
    }
    if (g.vsync != s_overlay_graphics.vsync && s_state.layer)
        s_state.layer.displaySyncEnabled = g.vsync ? YES : NO;
    if (g.fullscreen != s_overlay_graphics.fullscreen)
        SDL_SetWindowFullscreen(s_state.window,
                                g.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0u);
    if ((g.window_width != s_overlay_graphics.window_width ||
         g.window_height != s_overlay_graphics.window_height) &&
        g.window_width > 0 && g.window_height > 0 &&
        !(SDL_GetWindowFlags(s_state.window) & SDL_WINDOW_FULLSCREEN_DESKTOP))
        SDL_SetWindowSize(s_state.window, (int)g.window_width, (int)g.window_height);
    s_overlay_graphics = g;
}

int rsx_metal_backend_pump_messages(void)
{
    SDL_Event ev;
    if (s_state.window && pthread_main_np()) {
        char title[sizeof s_title_pending];
        int have = 0;
        os_unfair_lock_lock(&s_title_lock);
        if (s_title_has_pending) {
            snprintf(title, sizeof title, "%s", s_title_pending);
            s_title_has_pending = 0;
            have = 1;
        }
        os_unfair_lock_unlock(&s_title_lock);
        if (have) SDL_SetWindowTitle(s_state.window, title);
    }
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
        case SDL_QUIT:
            metal_quit_now("Cmd+Q / quit");
            break;

        case SDL_WINDOWEVENT:
            if (ev.window.event == SDL_WINDOWEVENT_CLOSE) {
                metal_quit_now("window closed");
            } else if (ev.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                metal_mouse_grab(0);   /* Cmd+Tab devolve o rato ao sistema */
                /* Held keys/buttons must not stay captured across focus loss. */
                rsx_overlay_focus_lost();
            } else if (ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                int dw = 0, dh = 0;
                SDL_Metal_GetDrawableSize(s_state.window, &dw, &dh);
                if (dw > 0 && dh > 0) {
                    s_state.layer.drawableSize = CGSizeMake(dw, dh);
                }
                rsx_overlay_invalidate_display();
            } else if (ev.window.event == SDL_WINDOWEVENT_DISPLAY_CHANGED) {
                /* Dragged to a display with another backing scale: the size in
                 * points may not change, so SIZE_CHANGED alone can miss it. */
                rsx_overlay_invalidate_display();
            }
            break;

        case SDL_KEYDOWN:
        case SDL_KEYUP: {
            /* The overlay sees only the keys it owns and consumes them only
             * while open (plus its toggle key); Cmd/Alt+Enter never reaches
             * it. Everything else keeps the old path, and key-up events
             * still do nothing there. */
            const int overlay_key = rsx_overlay_sdl_key(ev.key.keysym.sym, ev.key.keysym.mod);
            if (overlay_key != 0 &&
                rsx_overlay_handle_key(overlay_key, ev.type == SDL_KEYDOWN,
                                       rsx_overlay_sdl_modifiers(ev.key.keysym.mod)))
                break;
            if (ev.type == SDL_KEYUP)
                break;
            if (ev.key.keysym.sym == SDLK_F11 ||
                (ev.key.keysym.sym == SDLK_RETURN &&
                 (ev.key.keysym.mod & (KMOD_GUI | KMOD_ALT)))) {
                const Uint32 f = SDL_GetWindowFlags(s_state.window);
                SDL_SetWindowFullscreen(s_state.window,
                                        (f & SDL_WINDOW_FULLSCREEN_DESKTOP) ? 0u
                                                                            : SDL_WINDOW_FULLSCREEN_DESKTOP);
                break;
            }
            if (ev.key.keysym.sym == SDLK_ESCAPE) {
                /* Com o rato capturado, Escape LIBERTA em vez de fechar -- senao
                 * esconder o cursor deixava o utilizador sem saida. Sem captura,
                 * fecha como sempre fez. */
                if (s_mouse_grabbed) {
                    metal_mouse_grab(0);
                } else if (metal_native_window_on()) {
                    /* Sem captura o jogo abre assim: Escape nao pode fechar o
                     * jogo por engano. Sai da tela cheia; fechar = Cmd+Q ou o
                     * botao da janela. */
                    if (SDL_GetWindowFlags(s_state.window) & SDL_WINDOW_FULLSCREEN_DESKTOP)
                        SDL_SetWindowFullscreen(s_state.window, 0u);
                } else {
                    s_state.window_closed = 1;
                }
            }
            break;
        }

        case SDL_MOUSEBUTTONDOWN:
            /* Clicar na janela volta a capturar: o padrao dos jogos nativos. */
            if (metal_native_window_on() && !s_mouse_grabbed) metal_mouse_grab(1);
            break;

        default:
            break;
        }
    }

    /* This thread owns the overlay's shell state: it builds the next
     * immutable draw packet here, after this batch of input, and applies any
     * graphics change the menu made. Nothing is built while the shell is
     * closed (render inactive); then only a pending settings change (e.g. the
     * menu just closed) is written, and an idle call does no I/O. The
     * drawable size is published only when the core asks for it (init,
     * resize, display change, each open): a closed shell costs one atomic
     * load here, not a window query per pump. */
    if (rsx_overlay_display_stale() && s_state.window && pthread_main_np()) {
        int dw = 0, dh = 0, ww = 0, wh = 0;
        SDL_Metal_GetDrawableSize(s_state.window, &dw, &dh);
        SDL_GetWindowSize(s_state.window, &ww, &wh);
        rsx_overlay_set_display((uint32_t)(dw > 0 ? dw : 0), (uint32_t)(dh > 0 ? dh : 0),
                                ww > 0 ? (float)dw / (float)ww : 0.0f);
    }
    if (rsx_overlay_render_active())
        rsx_overlay_build_draw_data();
    else
        (void)rsx_overlay_persist_settings();
    metal_overlay_apply_graphics();
    if (rsx_overlay_take_quit_request()) {
        (void)rsx_overlay_persist_settings();
        metal_quit_now("overlay: Sair");
    }

    return s_state.window_closed ? -1 : 0;
}
