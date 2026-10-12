#ifndef PS3RECOMP_RSX_OVERLAY_METAL_H
#define PS3RECOMP_RSX_OVERLAY_METAL_H

/* Metal renderer for the runtime overlay. It consumes only the immutable draw
 * packet and font atlas copied out of the overlay core (rsx_overlay.h) and
 * owns every GPU object it uses (PSO, sampler, font texture, per-frame shared
 * upload buffers): no guest shader, texture, buffer or draw counter is reused
 * or touched. It never calls ImGui or SDL, so it may encode from any thread
 * that presents (the guest flip or the movie decode thread); concurrent calls
 * are serialized internally. Objective-C, Apple only. */

#if defined(__APPLE__) && defined(__OBJC__)

#import <Metal/Metal.h>
#include <stdint.h>

#include "rsx_overlay.h"
#include "rsx_overlay_packet.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Upload slots in flight at once. A slot is reused only after the command
 * buffer that read it completed; with none free the UI skips that frame
 * instead of blocking the present. */
#define RSX_OVERLAY_METAL_FRAME_SLOTS 4u

/* Backdrop Gaussian blur behind the open shell, applied at half resolution. */
#define RSX_OVERLAY_METAL_BLUR_SIGMA 6.0f /* half-res; ~24 px radius at drawable res */

typedef struct rsx_overlay_metal_renderer rsx_overlay_metal_renderer;

/* Where the renderer reads UI state from. NULL in create() selects the
 * overlay core (rsx_overlay_visible / copy_draw_packet / copy_font_atlas);
 * tests pass their own to render a known packet. */
typedef struct rsx_overlay_metal_source {
    int (*visible)(void* userdata);
    int (*copy_packet)(rsx_overlay_draw_packet* out, void* userdata);
    int (*copy_font)(uint8_t* rgba, size_t capacity, uint32_t* width,
                     uint32_t* height, void* userdata);
    void* userdata;
    /* Current atlas generation; a change re-uploads the font texture.
     * NULL: the atlas never changes. */
    uint64_t (*atlas_generation)(void* userdata);
} rsx_overlay_metal_source;

/* color_format must match the target texture passed to encode(). */
rsx_overlay_metal_renderer* rsx_overlay_metal_renderer_create(
    id<MTLDevice> device, MTLPixelFormat color_format,
    const rsx_overlay_metal_source* source);

/* Releases the caller's reference. Command buffers still reading an upload
 * slot keep the renderer alive until they complete, so it never waits. */
void rsx_overlay_metal_renderer_destroy(rsx_overlay_metal_renderer* renderer);

/* Encodes the UI pass as its own render pass (load + store, no depth) into
 * command_buffer, drawing onto target with the packet's display mapped onto viewport.
 * Must be the last pass on target before the caller presents/commits it.
 * Returns 1 when UI draws were encoded; 0 (and encodes nothing) when the
 * overlay is hidden, has nothing to draw, no slot is free, or the renderer is
 * not ready. */
int rsx_overlay_metal_renderer_encode(rsx_overlay_metal_renderer* renderer,
    id<MTLCommandBuffer> command_buffer, id<MTLTexture> target,
    rsx_overlay_viewport viewport);

#ifdef RSX_OVERLAY_TESTING
/* bit0: blur textures allocated, bit1: blur failed (dim only) */
int rsx_overlay_metal_renderer_test_blur_state(rsx_overlay_metal_renderer* r);
/* 1: MPS unsupported, 2: scratch allocation fails */
void rsx_overlay_metal_renderer_test_fail_blur(rsx_overlay_metal_renderer* r, int stage);
#endif

#ifdef __cplusplus
}
#endif

#endif /* __APPLE__ && __OBJC__ */
#endif
