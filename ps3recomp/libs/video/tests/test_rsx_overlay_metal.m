/*
 * Headless test of the Metal overlay renderer (rsx_overlay_metal.m).
 *
 * Renders known packets into an offscreen BGRA8 MTLTexture through the real
 * renderer and reads the pixels back: alpha blend over a known background,
 * scissor clipping (in a letterboxed viewport), font texture sampling, the
 * hidden no-op, the busy-slot guard, and one frame from the real overlay
 * core. Also covers the pure SDL key translation used by the Metal event pump.
 * Run it once with MTL_DEBUG_LAYER=1 for Metal API validation.
 */
#import <Metal/Metal.h>
#import <Foundation/Foundation.h>
#import <QuartzCore/QuartzCore.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cellPad.h"
#include "rsx_overlay.h"
#include "rsx_overlay_assets.h"
#include "rsx_overlay_metal.h"
#include "rsx_overlay_packet.h"
#include "rsx_overlay_sdl_input.h"
#include "rsx_overlay_settings.h"
#include "rsx_overlay_shell.h"
#include "rsx_metal_draw_stats.h"

/* The core pushes the host button map into cellPad; not linked here. */
void cellPad_set_host_mapping(const uint16_t map[CELL_PAD_HOST_BUTTON_COUNT],
                              uint8_t deadzone)
{
    (void)map;
    (void)deadzone;
}
void cellPad_set_host_vibration(int enabled) { (void)enabled; }

static int s_failures;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__,   \
                    #cond);                                                    \
            s_failures++;                                                      \
            return 0;                                                          \
        }                                                                      \
    } while (0)

enum { FB_W = 128, FB_H = 96 };
/* 16:9 canvas letterboxed into a 4:3 target: bars of 12 rows top and bottom. */
static const rsx_overlay_viewport kViewport = { 0, 12, 128, 72 };

/* ---- known-packet source ------------------------------------------------ */

typedef struct test_source {
    int visible;
    rsx_overlay_draw_packet* packet;
    int font_copies;
} test_source;

/* 2x2 RGBA atlas: (0,0) opaque white, (1,0) transparent white,
 * (0,1) opaque green, (1,1) opaque white. */
static const uint8_t kFont[16] = {
    255, 255, 255, 255,   255, 255, 255, 0,
    0, 255, 0, 255,       255, 255, 255, 255,
};

static int src_visible(void* ud) { return ((test_source*)ud)->visible; }

static int src_copy_packet(rsx_overlay_draw_packet* out, void* ud)
{
    test_source* s = (test_source*)ud;
    memcpy(out, s->packet, sizeof(*out));
    return 1;
}

static int src_copy_font(uint8_t* rgba, size_t capacity, uint32_t* w, uint32_t* h, void* ud)
{
    ((test_source*)ud)->font_copies++;
    if (rgba) {
        if (capacity < sizeof(kFont))
            return 0;
        memcpy(rgba, kFont, sizeof(kFont));
    }
    *w = 2;
    *h = 2;
    return 1;
}

/* Appends a canvas-space quad (two triangles) and its command. The command's
 * index_offset is whatever the packet has so far, so it is deliberately not
 * 4-byte aligned for the second and later quads (6 indices = 12 bytes). */
static void add_quad(rsx_overlay_draw_packet* p, float x0, float y0, float x1, float y1,
                     float u, float v, uint32_t rgba, const float clip[4],
                     uintptr_t texture_id)
{
    const uint32_t base_vertex = p->vertex_count;
    const float xs[4] = { x0, x1, x1, x0 };
    const float ys[4] = { y0, y0, y1, y1 };
    for (int i = 0; i < 4; ++i) {
        rsx_overlay_draw_vertex* vx = &p->vertices[p->vertex_count++];
        vx->position[0] = xs[i];
        vx->position[1] = ys[i];
        vx->uv[0] = u;
        vx->uv[1] = v;
        vx->color = rgba;
    }
    /* Pad one index so offsets are odd multiples of 2 bytes too. */
    if (p->command_count == 1)
        p->indices[p->index_count++] = 0;
    const uint32_t first = p->index_count;
    static const uint16_t tri[6] = { 0, 1, 2, 0, 2, 3 };
    for (int i = 0; i < 6; ++i)
        p->indices[p->index_count++] = tri[i];
    rsx_overlay_draw_command* c = &p->commands[p->command_count++];
    c->element_count = 6;
    c->index_offset = first;
    c->vertex_offset = base_vertex;
    memcpy(c->clip_rect, clip, sizeof(c->clip_rect));
    c->texture_id = texture_id;
}

/* ---- Metal helpers ------------------------------------------------------ */

static id<MTLDevice> s_device;
static id<MTLCommandQueue> s_queue;

static id<MTLTexture> make_target(void)
{
    MTLTextureDescriptor* td =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                           width:FB_W
                                                          height:FB_H
                                                       mipmapped:NO];
    td.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    td.storageMode = MTLStorageModePrivate;
    return [s_device newTextureWithDescriptor:td];
}

/* Background: opaque blue (BGRA 255,0,0,255). */
static void encode_clear(id<MTLCommandBuffer> cb, id<MTLTexture> target)
{
    MTLRenderPassDescriptor* p = [MTLRenderPassDescriptor renderPassDescriptor];
    p.colorAttachments[0].texture = target;
    p.colorAttachments[0].loadAction = MTLLoadActionClear;
    p.colorAttachments[0].storeAction = MTLStoreActionStore;
    p.colorAttachments[0].clearColor = MTLClearColorMake(0.0, 0.0, 1.0, 1.0);
    id<MTLRenderCommandEncoder> e = [cb renderCommandEncoderWithDescriptor:p];
    [e endEncoding];
}

static id<MTLBuffer> encode_readback(id<MTLCommandBuffer> cb, id<MTLTexture> target)
{
    id<MTLBuffer> out = [s_device newBufferWithLength:FB_W * FB_H * 4
                                              options:MTLResourceStorageModeShared];
    id<MTLBlitCommandEncoder> b = [cb blitCommandEncoder];
    [b copyFromTexture:target sourceSlice:0 sourceLevel:0
          sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(FB_W, FB_H, 1)
              toBuffer:out destinationOffset:0 destinationBytesPerRow:FB_W * 4
    destinationBytesPerImage:FB_W * FB_H * 4];
    [b endEncoding];
    return out;
}

static const uint8_t* px(id<MTLBuffer> buf, int x, int y)
{
    return (const uint8_t*)buf.contents + ((size_t)y * FB_W + (size_t)x) * 4u;
}

static int near(int a, int b) { return abs(a - b) <= 2; }

/* BGRA expectation. */
static int pixel_is(id<MTLBuffer> buf, int x, int y, int b, int g, int r)
{
    const uint8_t* p = px(buf, x, y);
    if (near(p[0], b) && near(p[1], g) && near(p[2], r))
        return 1;
    fprintf(stderr, "  pixel(%d,%d) = B%u G%u R%u, expected B%d G%d R%d\n",
            x, y, p[0], p[1], p[2], b, g, r);
    return 0;
}

static int is_background(id<MTLBuffer> buf, int x, int y)
{
    return pixel_is(buf, x, y, 255, 0, 0);
}

/* ---- tests -------------------------------------------------------------- */

/* "Guest draws" counts primitive records only: with PS3_METAL_PER_DRAW_RT
 * the record list also holds clears and blits (prim 0xFFFFFFFF). Mirrors the
 * backend's record layout: an MTLPrimitiveType among other fields. */
typedef struct {
    uint32_t geom_id;
    MTLPrimitiveType prim;
    int is_blit;
    uint32_t clear_flags;
    float pad[7];
} fake_draw_rec;

static int test_guest_draw_count_skips_clears_and_blits(void)
{
    fake_draw_rec recs[6];

    memset(recs, 0, sizeof(recs));
    recs[0].prim = MTLPrimitiveTypeTriangle;
    recs[1].prim = (MTLPrimitiveType)0xFFFFFFFFu; recs[1].clear_flags = 0xF0; /* clear */
    recs[2].prim = MTLPrimitiveTypeTriangleStrip;
    recs[3].prim = (MTLPrimitiveType)0xFFFFFFFFu; recs[3].is_blit = 1;        /* blit */
    recs[4].prim = MTLPrimitiveTypePoint; /* 0: a real primitive */
    recs[5].prim = (MTLPrimitiveType)0xFFFFFFFFu; /* unmapped prim, skipped */
    _Static_assert(sizeof(MTLPrimitiveType) == sizeof(unsigned long),
                   "helper reads prim as unsigned long");
    CHECK(rsx_metal_count_geometry_records(&recs[0].prim, sizeof(recs[0]), 6) == 3);
    CHECK(rsx_metal_count_geometry_records(&recs[0].prim, sizeof(recs[0]), 0) == 0);
    CHECK(rsx_metal_count_geometry_records(NULL, sizeof(recs[0]), 6) == 0);
    return 1;
}

static int test_sdl_key_translation(void)
{
    CHECK(rsx_overlay_sdl_key(SDLK_F1, 0) == RSX_OVERLAY_KEY_F1);
    CHECK(rsx_overlay_sdl_key(SDLK_F2, 0) == RSX_OVERLAY_KEY_F2);
    CHECK(rsx_overlay_sdl_key(SDLK_UP, 0) == RSX_OVERLAY_KEY_UP);
    CHECK(rsx_overlay_sdl_key(SDLK_DOWN, 0) == RSX_OVERLAY_KEY_DOWN);
    CHECK(rsx_overlay_sdl_key(SDLK_LEFT, 0) == RSX_OVERLAY_KEY_LEFT);
    CHECK(rsx_overlay_sdl_key(SDLK_RIGHT, 0) == RSX_OVERLAY_KEY_RIGHT);
    CHECK(rsx_overlay_sdl_key(SDLK_RETURN, 0) == RSX_OVERLAY_KEY_ENTER);
    CHECK(rsx_overlay_sdl_key(SDLK_KP_ENTER, KMOD_LSHIFT) == RSX_OVERLAY_KEY_ENTER);
    CHECK(rsx_overlay_sdl_key(SDLK_ESCAPE, 0) == RSX_OVERLAY_KEY_ESCAPE);
    /* Fullscreen shortcuts never reach the overlay. */
    CHECK(rsx_overlay_sdl_key(SDLK_RETURN, KMOD_LGUI) == 0);
    CHECK(rsx_overlay_sdl_key(SDLK_RETURN, KMOD_RALT) == 0);
    CHECK(rsx_overlay_sdl_key(SDLK_F11, 0) == 0);
    CHECK(rsx_overlay_sdl_key(SDLK_q, KMOD_LGUI) == 0);
    CHECK(rsx_overlay_sdl_key(SDLK_w, 0) == 0);
    CHECK(rsx_overlay_sdl_modifiers(0) == 0);
    CHECK(rsx_overlay_sdl_modifiers(KMOD_LSHIFT | KMOD_RCTRL | KMOD_LALT) ==
          (RSX_OVERLAY_MOD_SHIFT | RSX_OVERLAY_MOD_CTRL | RSX_OVERLAY_MOD_ALT));
    CHECK(rsx_overlay_sdl_modifiers(KMOD_LGUI) == 0);
    return 1;
}

static int test_viewport_scissor(void)
{
    rsx_overlay_scissor sc;
    const float full[4] = { 0.0f, 0.0f, 1280.0f, 720.0f };
    const float left[4] = { 0.0f, 0.0f, 640.0f, 720.0f };
    const float outside[4] = { -100.0f, -100.0f, 2000.0f, 2000.0f };
    const float empty[4] = { 10.0f, 10.0f, 10.0f, 20.0f };
    CHECK(rsx_overlay_clip_to_viewport_scissor(full, 1280u, 720u, &kViewport, FB_W, FB_H, &sc));
    CHECK(sc.left == 0 && sc.top == 12 && sc.right == 128 && sc.bottom == 84);
    CHECK(rsx_overlay_clip_to_viewport_scissor(left, 1280u, 720u, &kViewport, FB_W, FB_H, &sc));
    CHECK(sc.left == 0 && sc.top == 12 && sc.right == 64 && sc.bottom == 84);
    /* Clamped to the viewport, never into the letterbox bars. */
    CHECK(rsx_overlay_clip_to_viewport_scissor(outside, 1280u, 720u, &kViewport, FB_W, FB_H, &sc));
    CHECK(sc.left == 0 && sc.top == 12 && sc.right == 128 && sc.bottom == 84);
    CHECK(!rsx_overlay_clip_to_viewport_scissor(empty, 1280u, 720u, &kViewport, FB_W, FB_H, &sc));
    /* Whole-framebuffer form is the D3D12 behaviour. */
    CHECK(rsx_overlay_clip_to_scissor(left, 1280u, 720u, 1280, 720, &sc));
    CHECK(sc.left == 0 && sc.top == 0 && sc.right == 640 && sc.bottom == 720);
    return 1;
}

/* A packet laid out at the target's own size maps pixel for pixel. */
static int test_display_pixels_map_one_to_one(void)
{
    static const float clip[4] = { 0.0f, 0.0f, (float)FB_W, (float)FB_H };
    rsx_overlay_draw_packet* p = calloc(1, sizeof(*p));
    CHECK(p != NULL);
    p->generation = 1;
    p->display_width = FB_W;
    p->display_height = FB_H;
    p->backing_scale = 1.0f;
    add_quad(p, 10, 20, 30, 40, 0.25f, 0.75f, 0xFFFFFFFFu, clip, 0); /* green texel */
    test_source src = { 1, p, 0 };
    rsx_overlay_metal_source source = { src_visible, src_copy_packet, src_copy_font, &src };
    rsx_overlay_metal_renderer* r =
        rsx_overlay_metal_renderer_create(s_device, MTLPixelFormatBGRA8Unorm, &source);
    CHECK(r != NULL);
    const rsx_overlay_viewport full = { 0, 0, FB_W, FB_H };
    id<MTLTexture> target = make_target();
    id<MTLCommandBuffer> cb = [s_queue commandBuffer];
    encode_clear(cb, target);
    CHECK(rsx_overlay_metal_renderer_encode(r, cb, target, full) == 1);
    id<MTLBuffer> rb = encode_readback(cb, target);
    [cb commit];
    [cb waitUntilCompleted];
    CHECK(pixel_is(rb, 10, 20, 0, 255, 0));
    CHECK(pixel_is(rb, 29, 39, 0, 255, 0));
    CHECK(is_background(rb, 30, 20));
    CHECK(is_background(rb, 10, 40));
    rsx_overlay_metal_renderer_destroy(r);
    free(p);
    return 1;
}

static rsx_overlay_draw_packet* known_packet(void)
{
    static const float left_half[4] = { 0.0f, 0.0f, 640.0f, 720.0f };
    static const float right_top[4] = { 640.0f, 0.0f, 1280.0f, 360.0f };
    static const float right_bottom[4] = { 640.0f, 360.0f, 1280.0f, 720.0f };
    static const float everything[4] = { 0.0f, 0.0f, 1280.0f, 720.0f };
    rsx_overlay_draw_packet* p = calloc(1, sizeof(*p));
    if (!p)
        return NULL;
    p->generation = 1;
    p->display_width = 1280u; p->display_height = 720u; p->backing_scale = 1.0f;
    /* A: full-canvas red at alpha 128 sampling the opaque white texel, clipped
     * to the left half -> blend over blue on the left only. */
    add_quad(p, 0, 0, 1280, 720, 0.25f, 0.25f, 0x800000FFu, left_half, 0);
    /* B: opaque white sampling the transparent texel -> no visible change. */
    add_quad(p, 640, 0, 1280, 360, 0.75f, 0.25f, 0xFFFFFFFFu, right_top, 0);
    /* C: opaque white sampling the green texel -> green. */
    add_quad(p, 640, 360, 1280, 720, 0.25f, 0.75f, 0xFFFFFFFFu, right_bottom, 0);
    /* D: unknown texture id -> skipped (would paint everything white). */
    add_quad(p, 0, 0, 1280, 720, 0.25f, 0.25f, 0xFFFFFFFFu, everything, 7);
    return p;
}

static int test_known_packet_pixels(void)
{
    test_source src = { 1, known_packet(), 0 };
    rsx_overlay_metal_source source = { src_visible, src_copy_packet, src_copy_font, &src };
    CHECK(src.packet != NULL);
    rsx_overlay_metal_renderer* r =
        rsx_overlay_metal_renderer_create(s_device, MTLPixelFormatBGRA8Unorm, &source);
    CHECK(r != NULL);
    id<MTLTexture> target = make_target();
    id<MTLCommandBuffer> cb = [s_queue commandBuffer];
    encode_clear(cb, target);
    CHECK(rsx_overlay_metal_renderer_encode(r, cb, target, kViewport) == 1);
    id<MTLBuffer> rb = encode_readback(cb, target);
    [cb commit];
    [cb waitUntilCompleted];
    CHECK(cb.status == MTLCommandBufferStatusCompleted);

    /* Letterbox bars are never touched. */
    CHECK(is_background(rb, 10, 5));
    CHECK(is_background(rb, 100, 90));
    CHECK(is_background(rb, 10, 11));
    CHECK(is_background(rb, 10, 84));
    /* Left half: red(128/255) over blue. */
    CHECK(pixel_is(rb, 10, 30, 127, 0, 128));
    CHECK(pixel_is(rb, 63, 66, 127, 0, 128));
    /* Scissor edge: x=64 is outside A's clip. Right-top: transparent texel. */
    CHECK(is_background(rb, 64, 30));
    CHECK(is_background(rb, 120, 20));
    /* Right-bottom: green texel sampled. */
    CHECK(pixel_is(rb, 64, 66, 0, 255, 0));
    CHECK(pixel_is(rb, 127, 83, 0, 255, 0));
    CHECK(src.font_copies >= 1);
    rsx_overlay_metal_renderer_destroy(r);
    free(src.packet);
    return 1;
}

static int test_hidden_is_noop(void)
{
    test_source src = { 0, known_packet(), 0 };
    rsx_overlay_metal_source source = { src_visible, src_copy_packet, src_copy_font, &src };
    rsx_overlay_metal_renderer* r =
        rsx_overlay_metal_renderer_create(s_device, MTLPixelFormatBGRA8Unorm, &source);
    CHECK(r != NULL);
    id<MTLTexture> target = make_target();
    id<MTLCommandBuffer> cb = [s_queue commandBuffer];
    encode_clear(cb, target);
    CHECK(rsx_overlay_metal_renderer_encode(r, cb, target, kViewport) == 0);
    id<MTLBuffer> rb = encode_readback(cb, target);
    [cb commit];
    [cb waitUntilCompleted];
    for (int y = 0; y < FB_H; y += 7)
        for (int x = 0; x < FB_W; x += 7)
            CHECK(is_background(rb, x, y));
    /* Hidden never even reads the font. */
    CHECK(src.font_copies == 0);
    rsx_overlay_metal_renderer_destroy(r);
    free(src.packet);
    return 1;
}

static int test_invalid_packet_draws_nothing(void)
{
    test_source src = { 1, known_packet(), 0 };
    rsx_overlay_metal_source source = { src_visible, src_copy_packet, src_copy_font, &src };
    rsx_overlay_metal_renderer* r =
        rsx_overlay_metal_renderer_create(s_device, MTLPixelFormatBGRA8Unorm, &source);
    CHECK(r != NULL);
    /* Every command's indices point past the vertices. */
    for (uint32_t i = 0; i < src.packet->index_count; ++i)
        src.packet->indices[i] = 60000;
    id<MTLTexture> target = make_target();
    id<MTLCommandBuffer> cb = [s_queue commandBuffer];
    encode_clear(cb, target);
    CHECK(rsx_overlay_metal_renderer_encode(r, cb, target, kViewport) == 0);
    src.packet->command_count = 0;
    CHECK(rsx_overlay_metal_renderer_encode(r, cb, target, kViewport) == 0);
    id<MTLBuffer> rb = encode_readback(cb, target);
    [cb commit];
    [cb waitUntilCompleted];
    CHECK(is_background(rb, 10, 30));
    CHECK(is_background(rb, 64, 66));
    rsx_overlay_metal_renderer_destroy(r);
    free(src.packet);
    return 1;
}

/* A slot is reused only after the command buffer that read it completed: with
 * every slot held by an uncommitted command buffer the next frame is skipped,
 * and after completion encoding resumes. */
static int test_busy_slots_skip_instead_of_overwrite(void)
{
    test_source src = { 1, known_packet(), 0 };
    rsx_overlay_metal_source source = { src_visible, src_copy_packet, src_copy_font, &src };
    rsx_overlay_metal_renderer* r =
        rsx_overlay_metal_renderer_create(s_device, MTLPixelFormatBGRA8Unorm, &source);
    CHECK(r != NULL);
    id<MTLTexture> target = make_target();
    id<MTLCommandBuffer> held[RSX_OVERLAY_METAL_FRAME_SLOTS];
    for (uint32_t i = 0; i < RSX_OVERLAY_METAL_FRAME_SLOTS; ++i) {
        held[i] = [s_queue commandBuffer];
        CHECK(rsx_overlay_metal_renderer_encode(r, held[i], target, kViewport) == 1);
    }
    id<MTLCommandBuffer> extra = [s_queue commandBuffer];
    CHECK(rsx_overlay_metal_renderer_encode(r, extra, target, kViewport) == 0);
    for (uint32_t i = 0; i < RSX_OVERLAY_METAL_FRAME_SLOTS; ++i)
        [held[i] commit];
    [held[RSX_OVERLAY_METAL_FRAME_SLOTS - 1] waitUntilCompleted];
    /* Completed handlers run after completion; give them a moment. */
    int resumed = 0;
    for (int tries = 0; tries < 200 && !resumed; ++tries) {
        id<MTLCommandBuffer> cb = [s_queue commandBuffer];
        resumed = rsx_overlay_metal_renderer_encode(r, cb, target, kViewport);
        [cb commit];
        [cb waitUntilCompleted];
        if (!resumed)
            usleep(1000);
    }
    CHECK(resumed == 1);
    [extra commit];
    rsx_overlay_metal_renderer_destroy(r);
    free(src.packet);
    return 1;
}

/* One frame from the real overlay core: F1 opens it, the event thread builds
 * the packet, the renderer (default source) composites it inside the viewport
 * only; closing it makes encode a no-op again. */
static int test_real_core_frame(void)
{
    char path[] = "/tmp/rsx-overlay-metal-XXXXXX";
    int fd = mkstemp(path);
    rsx_overlay_settings settings;
    CHECK(fd >= 0);
    close(fd);
    rsx_overlay_settings_defaults(&settings);
    settings.overlay_visible = 0;
    CHECK(rsx_overlay_settings_save(&settings, path));
    CHECK(rsx_overlay_init(path));
    rsx_overlay_metal_renderer* r =
        rsx_overlay_metal_renderer_create(s_device, MTLPixelFormatBGRA8Unorm, NULL);
    CHECK(r != NULL);

    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_F1, 1, 0));
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_F1, 0, 0));
    CHECK(rsx_overlay_visible());
    rsx_overlay_test_set_clock(1000.1);
    rsx_overlay_build_draw_data();
    rsx_overlay_test_set_clock(1001.1);
    rsx_overlay_build_draw_data();

    id<MTLTexture> target = make_target();
    id<MTLCommandBuffer> cb = [s_queue commandBuffer];
    encode_clear(cb, target);
    CHECK(rsx_overlay_metal_renderer_encode(r, cb, target, kViewport) == 1);
    id<MTLBuffer> rb = encode_readback(cb, target);
    [cb commit];
    [cb waitUntilCompleted];
    int changed_inside = 0;
    for (int y = 0; y < FB_H; ++y) {
        for (int x = 0; x < FB_W; ++x) {
            const uint8_t* p = px(rb, x, y);
            const int bg = p[0] == 255 && p[1] == 0 && p[2] == 0;
            if (y < 12 || y >= 84)
                CHECK(bg);   /* bars untouched */
            else if (!bg)
                changed_inside++;
        }
    }
    CHECK(changed_inside > 0);
    CHECK(pixel_is(rb, 100, 32, 166, 0, 0));

    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_F1, 1, 0));
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_F1, 0, 0));
    CHECK(!rsx_overlay_visible());
    rsx_overlay_test_set_clock(1002.1);
    rsx_overlay_build_draw_data();
    id<MTLCommandBuffer> cb2 = [s_queue commandBuffer];
    CHECK(rsx_overlay_metal_renderer_encode(r, cb2, target, kViewport) == 0);
    [cb2 commit];
    [cb2 waitUntilCompleted];

    rsx_overlay_metal_renderer_destroy(r);
    rsx_overlay_shutdown();
    unlink(path);
    return 1;
}

static uint64_t s_gen = 1;
static uint8_t s_font2[16];
static uint64_t src_generation(void* ud) { (void)ud; return s_gen; }
static int src_copy_font2(uint8_t* rgba, size_t capacity, uint32_t* w, uint32_t* h, void* ud)
{
    ((test_source*)ud)->font_copies++;
    if (rgba) { if (capacity < sizeof(s_font2)) return 0; memcpy(rgba, s_font2, sizeof(s_font2)); }
    *w = 2; *h = 2;
    return 1;
}

/* A new atlas generation (backing scale changed) is re-uploaded; the same
 * generation is never copied again. */
static int test_atlas_generation_reloads_texture(void)
{
    memcpy(s_font2, kFont, sizeof(s_font2));
    s_gen = 1;
    test_source src = { 1, known_packet(), 0 };
    rsx_overlay_metal_source source = { src_visible, src_copy_packet, src_copy_font2, &src, src_generation };
    rsx_overlay_metal_renderer* r =
        rsx_overlay_metal_renderer_create(s_device, MTLPixelFormatBGRA8Unorm, &source);
    CHECK(r != NULL);
    id<MTLTexture> target = make_target();
    for (int pass = 0; pass < 3; ++pass) {
        if (pass == 2) {
            /* Region C samples texel (0,1): make it opaque red, bump generation. */
            s_font2[8] = 255; s_font2[9] = 0; s_font2[10] = 0; s_font2[11] = 255;
            s_gen = 2;
        }
        const int copies_before = src.font_copies;
        id<MTLCommandBuffer> cb = [s_queue commandBuffer];
        encode_clear(cb, target);
        CHECK(rsx_overlay_metal_renderer_encode(r, cb, target, kViewport) == 1);
        id<MTLBuffer> rb = encode_readback(cb, target);
        [cb commit];
        [cb waitUntilCompleted];
        if (pass == 1) CHECK(src.font_copies == copies_before);        /* cached */
        if (pass < 2) CHECK(pixel_is(rb, 64, 66, 0, 255, 0));            /* green */
        else CHECK(pixel_is(rb, 64, 66, 0, 0, 255));                     /* red: reloaded */
    }
    rsx_overlay_metal_renderer_destroy(r);
    free(src.packet);
    return 1;
}

/* ---- backdrop blur (Task 8) -------------------------------------------- */


/* Readable target of any size, filled with a black/white checkerboard. */
static id<MTLTexture> make_checker(uint32_t w, uint32_t h, uint32_t cell, MTLTextureUsage usage)
{
    MTLTextureDescriptor* td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                                  width:w height:h mipmapped:NO];
    td.usage = usage;
    td.storageMode = MTLStorageModePrivate;
    id<MTLTexture> t = [s_device newTextureWithDescriptor:td];
    id<MTLBuffer> src = [s_device newBufferWithLength:(NSUInteger)w * h * 4 options:MTLResourceStorageModeShared];
    uint8_t* p = (uint8_t*)src.contents;
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x) {
            const uint8_t v = (((x / cell) + (y / cell)) & 1u) ? 255 : 0;
            uint8_t* q = p + ((size_t)y * w + x) * 4u;
            q[0] = q[1] = q[2] = v;
            q[3] = 255;
        }
    id<MTLCommandBuffer> cb = [s_queue commandBuffer];
    id<MTLBlitCommandEncoder> b = [cb blitCommandEncoder];
    [b copyFromBuffer:src sourceOffset:0 sourceBytesPerRow:(NSUInteger)w * 4 sourceBytesPerImage:(NSUInteger)w * h * 4
           sourceSize:MTLSizeMake(w, h, 1) toTexture:t destinationSlice:0 destinationLevel:0
    destinationOrigin:MTLOriginMake(0, 0, 0)];
    [b endEncoding];
    [cb commit];
    [cb waitUntilCompleted];
    return t;
}

static id<MTLBuffer> readback_any(id<MTLCommandBuffer> cb, id<MTLTexture> t)
{
    const NSUInteger w = t.width, h = t.height;
    id<MTLBuffer> out = [s_device newBufferWithLength:w * h * 4 options:MTLResourceStorageModeShared];
    id<MTLBlitCommandEncoder> b = [cb blitCommandEncoder];
    [b copyFromTexture:t sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0)
            sourceSize:MTLSizeMake(w, h, 1) toBuffer:out destinationOffset:0
destinationBytesPerRow:w * 4 destinationBytesPerImage:w * h * 4];
    [b endEncoding];
    return out;
}

/* Max-min of the green channel and its mean over a square region. */
static void region_stats(id<MTLBuffer> buf, uint32_t stride_px, uint32_t x0, uint32_t y0, uint32_t size,
                         int* range, double* mean)
{
    int lo = 255, hi = 0;
    double sum = 0.0;
    for (uint32_t y = y0; y < y0 + size; ++y)
        for (uint32_t x = x0; x < x0 + size; ++x) {
            const int g = ((const uint8_t*)buf.contents)[((size_t)y * stride_px + x) * 4u + 1u];
            lo = g < lo ? g : lo;
            hi = g > hi ? g : hi;
            sum += g;
        }
    *range = hi - lo;
    *mean = sum / (double)(size * size);
}

/* Like the core's packet: first command is the full-screen dim (black at
 * 0.35 * open, sampling the opaque white texel), then one invisible quad. */
static rsx_overlay_draw_packet* blur_packet(uint32_t w, uint32_t h, float open)
{
    const float clip[4] = { 0.0f, 0.0f, (float)w, (float)h };
    const uint32_t dim_alpha = (uint32_t)(RSX_SHELL_BACKDROP_DIM * open * 255.0f + 0.5f);
    rsx_overlay_draw_packet* p = calloc(1, sizeof(*p));
    p->generation = 1;
    p->display_width = w;
    p->display_height = h;
    p->backing_scale = 2.0f;
    p->backdrop_blur = 1u;
    p->backdrop_dim = RSX_SHELL_BACKDROP_DIM;
    p->open_progress = open;
    add_quad(p, 0, 0, (float)w, (float)h, 0.25f, 0.25f, dim_alpha << 24, clip, 0);
    add_quad(p, 0, 0, 1, 1, 0.75f, 0.25f, 0xFFFFFFFFu, clip, 0); /* transparent texel */
    return p;
}

/* Checkerboard through the renderer; returns the green range and mean of a
 * centre region and the renderer's blur state. */
static int render_checker(float open, int fail_stage, MTLTextureUsage usage,
                          int* range, double* mean, int* blur_state)
{
    test_source src = { 1, blur_packet(256, 192, open), 0 };
    rsx_overlay_metal_source source = { src_visible, src_copy_packet, src_copy_font, &src };
    rsx_overlay_metal_renderer* r = rsx_overlay_metal_renderer_create(s_device, MTLPixelFormatBGRA8Unorm, &source);
    CHECK(r != NULL);
    if (fail_stage)
        rsx_overlay_metal_renderer_test_fail_blur(r, fail_stage);
    id<MTLTexture> target = make_checker(256, 192, 8, usage);
    const rsx_overlay_viewport full = { 0, 0, 256, 192 };
    id<MTLCommandBuffer> cb = [s_queue commandBuffer];
    CHECK(rsx_overlay_metal_renderer_encode(r, cb, target, full) == 1);
    id<MTLBuffer> rb = readback_any(cb, target);
    [cb commit];
    [cb waitUntilCompleted];
    CHECK(cb.status == MTLCommandBufferStatusCompleted);
    region_stats(rb, 256, 112, 80, 32, range, mean);
    *blur_state = rsx_overlay_metal_renderer_test_blur_state(r);
    rsx_overlay_metal_renderer_destroy(r);
    free(src.packet);
    return 1;
}

/* Expected values: checker 0/255; dim at full open leaves 65 % (255 -> 166);
 * a blurred 8 px checker at sigma 6 (half res) is ~127 everywhere. */
static int test_blur_and_dim_pixels(void)
{
    const MTLTextureUsage rw = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    int range, state;
    double mean;

    /* Closed: nothing blurred, nothing dimmed, nothing allocated. */
    CHECK(render_checker(0.0f, 0, rw, &range, &mean, &state));
    CHECK(range >= 250 && (state & 1) == 0);
    /* Fully open: blurred and dimmed. */
    CHECK(render_checker(1.0f, 0, rw, &range, &mean, &state));
    CHECK(range < 40 && mean > 68.0 && mean < 98.0 && state == 1);
    /* Half open: backdrop at 50 %, dim at 17.5 % -> white ~158, black ~52. */
    CHECK(render_checker(0.5f, 0, rw, &range, &mean, &state));
    CHECK(range > 80 && range < 140);
    return 1;
}

/* The drawable is never sampled directly: a render-target-only texture (no
 * ShaderRead, but not framebufferOnly -- what CAMetalLayer hands out with
 * framebufferOnly = NO) still blurs, through the owned copy. */
static int test_render_target_only_drawable_still_blurs(void)
{
    int range, state;
    double mean;
    CHECK(render_checker(1.0f, 0, MTLTextureUsageRenderTarget, &range, &mean, &state));
    CHECK(range < 40 && state == 1);
    return 1;
}

/* Injected MPS / allocation failure: dim only, pixels exact, flag set. */
static int test_blur_failure_falls_back_to_dim(void)
{
    const MTLTextureUsage rw = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    for (int stage = 1; stage <= 2; ++stage) {
        int range, state;
        double mean;
        CHECK(render_checker(1.0f, stage, rw, &range, &mean, &state));
        CHECK(range >= 162 && range <= 170);   /* 255 * 0.65 against 0: sharp, dimmed */
        CHECK(mean > 78.0 && mean < 88.0);     /* half white, half black, dimmed */
        CHECK((state & 2) != 0 && (state & 1) == 0);
    }
    return 1;
}

/* A real CAMetalLayer drawable configured like the backend
 * (rsx_metal_backend.m: framebufferOnly = NO). Skips, without failing, when
 * the headless session cannot vend a drawable. */
static int test_cametallayer_drawable_blurs(void)
{
    CAMetalLayer* layer = [CAMetalLayer layer];
    layer.device = s_device;
    layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    layer.framebufferOnly = NO;
    layer.drawableSize = CGSizeMake(256, 192);
    id<CAMetalDrawable> drawable = [layer nextDrawable];
    if (!drawable) {
        printf("test_cametallayer_drawable_blurs: SKIP (no drawable headless)\n");
        return 1;
    }
    CHECK(!drawable.texture.framebufferOnly);
    id<MTLTexture> checker = make_checker(256, 192, 8, MTLTextureUsageShaderRead);
    id<MTLCommandBuffer> fill = [s_queue commandBuffer];
    id<MTLBlitCommandEncoder> b = [fill blitCommandEncoder];
    [b copyFromTexture:checker toTexture:drawable.texture];
    [b endEncoding];
    [fill commit];
    [fill waitUntilCompleted];
    test_source src = { 1, blur_packet(256, 192, 1.0f), 0 };
    rsx_overlay_metal_source source = { src_visible, src_copy_packet, src_copy_font, &src };
    rsx_overlay_metal_renderer* r = rsx_overlay_metal_renderer_create(s_device, MTLPixelFormatBGRA8Unorm, &source);
    const rsx_overlay_viewport full = { 0, 0, 256, 192 };
    id<MTLCommandBuffer> cb = [s_queue commandBuffer];
    CHECK(rsx_overlay_metal_renderer_encode(r, cb, drawable.texture, full) == 1);
    id<MTLBuffer> rb = readback_any(cb, drawable.texture);
    [cb commit];
    [cb waitUntilCompleted];
    int range;
    double mean;
    region_stats(rb, 256, 112, 80, 32, &range, &mean);
    CHECK(range < 40 && rsx_overlay_metal_renderer_test_blur_state(r) == 1);
    rsx_overlay_metal_renderer_destroy(r);
    free(src.packet);
    return 1;
}

/* The real shell at 2x on a checkerboard "game": blur + dim behind, rounded
 * card corners, label text present. */
static int test_real_shell_frame_at_2x(void)
{
    char path[] = "/tmp/rsx-overlay-shell2x-XXXXXX";
    int fd = mkstemp(path);
    rsx_overlay_settings settings;
    const uint32_t W = 1280, H = 720;
    CHECK(fd >= 0);
    close(fd);
    rsx_overlay_settings_defaults(&settings);
    CHECK(rsx_overlay_settings_save(&settings, path));
    CHECK(rsx_overlay_init(path));
    rsx_overlay_set_asset_builder(rsx_overlay_assets_build_apple);
    rsx_overlay_set_display(W, H, 2.0f);
    rsx_overlay_metal_renderer* r = rsx_overlay_metal_renderer_create(s_device, MTLPixelFormatBGRA8Unorm, NULL);
    CHECK(r != NULL);
    rsx_overlay_test_set_clock(2000.0);
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_F1, 1, 0));
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_F1, 0, 0));
    rsx_overlay_build_draw_data();
    rsx_overlay_test_set_clock(2001.0);
    rsx_overlay_build_draw_data();

    id<MTLTexture> target = make_checker(W, H, 16, MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead);
    const rsx_overlay_viewport full = { 0, 0, W, H };
    id<MTLCommandBuffer> cb = [s_queue commandBuffer];
    CHECK(rsx_overlay_metal_renderer_encode(r, cb, target, full) == 1);
    id<MTLBuffer> rb = readback_any(cb, target);
    [cb commit];
    [cb waitUntilCompleted];
    CHECK(cb.status == MTLCommandBufferStatusCompleted);

    /* Backdrop region clear of strip, panel and cards: blurred and dimmed. */
    int range; double mean;
    region_stats(rb, W, 900, 120, 48, &range, &mean);
    CHECK(range < 60);
    CHECK(mean < 0.8 * 127.5);

    /* Focused card (Voltar ao jogo): the exact corner is outside the rounded
     * shape, the edge midpoint is on the gold (#C9A15A) border. BGRA readback. */
    rsx_shell_layout L;
    rsx_shell_compute_layout((float)W, (float)H, 2.0f, RSX_SHELL_CARD_RESUME, 1, 0, 1.0f, &L);
    const rsx_shell_rect c = L.cards[RSX_SHELL_CARD_RESUME];
    const float cx = c.x + c.w * 0.5f, cy = c.y + c.h * 0.5f, hw = c.w * 0.5f * RSX_SHELL_FOCUS_SCALE;
    const uint8_t* corner = (const uint8_t*)rb.contents + (((size_t)(cy - hw + 1.0f)) * W + (size_t)(cx - hw + 1.0f)) * 4u;
    const uint8_t* edge = (const uint8_t*)rb.contents + (((size_t)cy) * W + (size_t)(cx - hw + 1.0f)) * 4u;
    CHECK(edge[2] > 170 && edge[2] < 230);   /* R ~0xC9 */
    CHECK(edge[1] > 130 && edge[1] < 190);   /* G ~0xA1 */
    CHECK(edge[0] < 120);                    /* B ~0x5A: gold, not white */
    CHECK(corner[1] < 170);

    /* Label under the focused card: bright text pixels exist. */
    int bright = 0;
    const uint32_t ly0 = (uint32_t)(cy + hw), ly1 = ly0 + (uint32_t)(24.0f * L.px_per_pt);
    for (uint32_t y = ly0; y < ly1 && y < H; ++y)
        for (uint32_t x = (uint32_t)(cx - hw); x < (uint32_t)(cx + hw); ++x)
            bright += ((const uint8_t*)rb.contents)[((size_t)y * W + x) * 4u + 1u] > 200;
    CHECK(bright > 20);

    rsx_overlay_metal_renderer_destroy(r);
    rsx_overlay_set_asset_builder(NULL);
    rsx_overlay_set_display(1280u, 720u, 1.0f);
    rsx_overlay_shutdown();
    unlink(path);
    return 1;
}

int main(void)
{
    @autoreleasepool {
        s_device = MTLCreateSystemDefaultDevice();
        if (!s_device) {
            fprintf(stderr, "no Metal device\n");
            return 1;
        }
        s_queue = [s_device newCommandQueue];
        rsx_overlay_test_set_clock(1000.0);
        int ok = 1;
        ok &= test_guest_draw_count_skips_clears_and_blits();
        ok &= test_sdl_key_translation();
        ok &= test_viewport_scissor();
        ok &= test_display_pixels_map_one_to_one();
        ok &= test_known_packet_pixels();
        ok &= test_hidden_is_noop();
        ok &= test_invalid_packet_draws_nothing();
        ok &= test_busy_slots_skip_instead_of_overwrite();
        ok &= test_atlas_generation_reloads_texture();
        ok &= test_real_core_frame();
        ok &= test_blur_and_dim_pixels();
        ok &= test_render_target_only_drawable_still_blurs();
        ok &= test_blur_failure_falls_back_to_dim();
        ok &= test_cametallayer_drawable_blurs();
        ok &= test_real_shell_frame_at_2x();
        if (!ok || s_failures) {
            fprintf(stderr, "test_rsx_overlay_metal: %d failure(s)\n", s_failures);
            return 1;
        }
        printf("test_rsx_overlay_metal: all passed\n");
    }
    return 0;
}
