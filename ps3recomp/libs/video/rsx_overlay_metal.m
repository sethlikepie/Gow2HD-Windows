/*
 * ps3recomp - Metal renderer for the runtime overlay (macOS / Apple silicon).
 *
 * Objective-C with ARC (see PS3RECOMP_OBJC_ARC_SOURCES in CMakeLists.txt). It
 * never reads live Dear ImGui or SDL state: geometry comes from
 * rsx_overlay_copy_draw_packet() and the font from
 * rsx_overlay_copy_font_atlas(), both immutable copies made by the overlay
 * core on its event thread. That is what lets the movie decode thread present
 * the last UI frame safely.
 *
 * Upload: each slot is one MTLResourceStorageModeShared buffer laid out as a
 * whole rsx_overlay_draw_packet, and the core copies the packet straight into
 * it -- one copy on unified memory, no staging. Vertices and indices are then
 * bound at their offsets inside that same buffer.
 */
#import <Metal/Metal.h>
#import <MetalPerformanceShaders/MetalPerformanceShaders.h>
#import <Foundation/Foundation.h>

#include <os/lock.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rsx_overlay.h"
#include "rsx_overlay_log.h"
#include "rsx_overlay_metal.h"
#include "rsx_overlay_packet.h"

_Static_assert(offsetof(rsx_overlay_draw_packet, vertices) % 4u == 0u,
               "vertex buffer offset must be 4-byte aligned");
_Static_assert(offsetof(rsx_overlay_draw_packet, indices) % 4u == 0u,
               "index buffer offset must be 4-byte aligned");
_Static_assert(sizeof(rsx_overlay_draw_vertex) == 20u,
               "vertex descriptor below assumes the packed ImGui vertex");

static NSString* const kOverlayShaderSource =
    @"#include <metal_stdlib>\n"
     "using namespace metal;\n"
     "struct ov_in { float2 pos [[attribute(0)]]; float2 uv [[attribute(1)]];"
     " float4 col [[attribute(2)]]; };\n"
     "struct ov_out { float4 pos [[position]]; float2 uv; float4 col; };\n"
     "vertex ov_out ov_vs(ov_in v [[stage_in]], constant float4& xf [[buffer(1)]]) {\n"
     "  ov_out o; o.pos = float4(v.pos * xf.xy + xf.zw, 0.0, 1.0);\n"
     "  o.uv = v.uv; o.col = v.col; return o; }\n"
     "fragment float4 ov_fs(ov_out i [[stage_in]], texture2d<float> font [[texture(0)]],\n"
     "                      sampler smp [[sampler(0)]]) {\n"
     "  return i.col * font.sample(smp, i.uv); }\n"
     "struct bd_out { float4 pos [[position]]; float2 uv; };\n"
     "vertex bd_out bd_vs(uint vid [[vertex_id]]) {\n"
     "  float2 p = float2((vid << 1) & 2, vid & 2); bd_out o;\n"
     "  o.pos = float4(p * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0); o.uv = p; return o; }\n"
     "fragment float4 bd_fs(bd_out i [[stage_in]], texture2d<float> tex [[texture(0)]],\n"
     "                      sampler smp [[sampler(0)]], constant float& alpha [[buffer(0)]]) {\n"
     "  return float4(tex.sample(smp, i.uv).rgb, alpha); }\n";

static int overlay_core_visible(void* userdata)
{
    (void)userdata;
    return rsx_overlay_render_active(); /* the close animation still draws */
}

static int overlay_core_copy_packet(rsx_overlay_draw_packet* out, void* userdata)
{
    (void)userdata;
    return rsx_overlay_copy_draw_packet(out);
}

static int overlay_core_copy_font(uint8_t* rgba, size_t capacity, uint32_t* width,
                                  uint32_t* height, void* userdata)
{
    (void)userdata;
    return rsx_overlay_copy_font_atlas(rgba, capacity, width, height);
}

static uint64_t overlay_core_atlas_generation(void* userdata)
{
    (void)userdata;
    return rsx_overlay_font_atlas_generation();
}

@interface RsxOverlayMetalRenderer : NSObject {
@public
    id<MTLDevice> _device;
    id<MTLRenderPipelineState> _pipeline;
    id<MTLSamplerState> _sampler;
    id<MTLTexture> _font;
    id<MTLBuffer> _slots[RSX_OVERLAY_METAL_FRAME_SLOTS];
    atomic_int _slot_busy[RSX_OVERLAY_METAL_FRAME_SLOTS];
    uint32_t _next_slot;
    rsx_overlay_metal_source _source;
    os_unfair_lock _lock;
    uint32_t _font_failures;
    uint64_t _font_generation;
    /* Per-encode scratch (under _lock): which commands draw, and where. */
    uint8_t _cmd_draw[RSX_OVERLAY_DRAW_COMMAND_CAPACITY];
    rsx_overlay_scissor _cmd_scissor[RSX_OVERLAY_DRAW_COMMAND_CAPACITY];
    /* Backdrop blur (under _lock), allocated on the first open frame only.
     * _blur_src is the owned full-resolution copy of the drawable; _blur_a /
     * _blur_b the half-resolution pair. _test_fail_stage stays 0 outside
     * tests. */
    id<MTLRenderPipelineState> _backdrop_pipeline;
    id<MTLTexture> _blur_src, _blur_a, _blur_b;
    MPSImageBilinearScale* _downscale;
    MPSImageGaussianBlur* _blur;
    int _blur_failed;
    int _test_fail_stage;
}
@end

@implementation RsxOverlayMetalRenderer
@end

static int overlay_create_pipeline(RsxOverlayMetalRenderer* r, MTLPixelFormat color_format)
{
    NSError* error = nil;
    id<MTLLibrary> library = [r->_device newLibraryWithSource:kOverlayShaderSource
                                                      options:nil
                                                        error:&error];
    if (!library) {
        fprintf(stderr, "[OVERLAY-METAL] shader compile failed: %s\n",
                error ? [[error localizedDescription] UTF8String] : "?");
        return 0;
    }
    MTLVertexDescriptor* vd = [MTLVertexDescriptor vertexDescriptor];
    vd.attributes[0].format = MTLVertexFormatFloat2;
    vd.attributes[0].offset = offsetof(rsx_overlay_draw_vertex, position);
    vd.attributes[0].bufferIndex = 0;
    vd.attributes[1].format = MTLVertexFormatFloat2;
    vd.attributes[1].offset = offsetof(rsx_overlay_draw_vertex, uv);
    vd.attributes[1].bufferIndex = 0;
    /* ImGui packs RGBA8 with R in the low byte: UChar4Normalized yields
     * (R,G,B,A) in x,y,z,w. */
    vd.attributes[2].format = MTLVertexFormatUChar4Normalized;
    vd.attributes[2].offset = offsetof(rsx_overlay_draw_vertex, color);
    vd.attributes[2].bufferIndex = 0;
    vd.layouts[0].stride = sizeof(rsx_overlay_draw_vertex);
    vd.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;

    MTLRenderPipelineDescriptor* pd = [[MTLRenderPipelineDescriptor alloc] init];
    pd.label = @"rsx_overlay_ui";
    pd.vertexFunction = [library newFunctionWithName:@"ov_vs"];
    pd.fragmentFunction = [library newFunctionWithName:@"ov_fs"];
    pd.vertexDescriptor = vd;
    pd.colorAttachments[0].pixelFormat = color_format;
    pd.colorAttachments[0].blendingEnabled = YES;
    pd.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
    pd.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
    pd.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
    pd.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    pd.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
    pd.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    /* No depth/stencil attachment: the UI pass never sees guest depth. */
    r->_pipeline = [r->_device newRenderPipelineStateWithDescriptor:pd error:&error];
    if (!r->_pipeline) {
        fprintf(stderr, "[OVERLAY-METAL] pipeline failed: %s\n",
                error ? [[error localizedDescription] UTF8String] : "?");
        return 0;
    }

    /* Backdrop pass (blurred frame fading in). Its failure only disables the
     * blur: the UI and the core's dim still draw. */
    MTLRenderPipelineDescriptor* bd = [[MTLRenderPipelineDescriptor alloc] init];
    bd.label = @"rsx_overlay_backdrop";
    bd.vertexFunction = [library newFunctionWithName:@"bd_vs"];
    bd.fragmentFunction = [library newFunctionWithName:@"bd_fs"];
    bd.colorAttachments[0] = pd.colorAttachments[0]; /* same format and blend */
    r->_backdrop_pipeline = [r->_device newRenderPipelineStateWithDescriptor:bd error:&error];
    if (!r->_backdrop_pipeline)
        fprintf(stderr, "[OVERLAY-METAL] backdrop pipeline failed: %s\n",
                error ? [[error localizedDescription] UTF8String] : "?");

    MTLSamplerDescriptor* sd = [[MTLSamplerDescriptor alloc] init];
    sd.minFilter = MTLSamplerMinMagFilterLinear;
    sd.magFilter = MTLSamplerMinMagFilterLinear;
    sd.mipFilter = MTLSamplerMipFilterNotMipmapped;
    sd.sAddressMode = MTLSamplerAddressModeClampToEdge;
    sd.tAddressMode = MTLSamplerAddressModeClampToEdge;
    r->_sampler = [r->_device newSamplerStateWithDescriptor:sd];
    return r->_sampler != nil;
}

/* Called under _lock. Each atlas generation is copied once into a texture
 * the GPU reads directly (shared on unified memory); a new generation (init,
 * backing-scale change) replaces it. */
static int overlay_ensure_font(RsxOverlayMetalRenderer* r)
{
    uint32_t width = 0, height = 0;
    size_t size;
    uint8_t* pixels;

    const uint64_t generation = r->_source.atlas_generation
        ? r->_source.atlas_generation(r->_source.userdata) : 0u;
    if (r->_font && generation == r->_font_generation)
        return 1;
    if (r->_font) {
        /* New atlas (backing scale changed): in-flight command buffers keep
         * the old texture alive; the next frames sample the new one. */
        r->_font = nil;
        r->_font_failures = 0;
    }
    if (r->_font_failures >= 8u)
        return 0;
    if (!r->_source.copy_font(NULL, 0, &width, &height, r->_source.userdata) ||
        width == 0 || height == 0 || width > 16384u || height > 16384u)
        return 0;   /* overlay not initialized yet: try again next frame */
    size = (size_t)width * height * 4u;
    pixels = (uint8_t*)malloc(size);
    if (!pixels)
        return 0;
    if (!r->_source.copy_font(pixels, size, &width, &height, r->_source.userdata) ||
        (size_t)width * height * 4u != size) {
        free(pixels);
        return 0;   /* re-initialized between the two calls */
    }
    MTLTextureDescriptor* td =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                           width:width
                                                          height:height
                                                       mipmapped:NO];
    td.usage = MTLTextureUsageShaderRead;
    /* Shared on unified memory (one CPU copy, read in place by the GPU).
     * Discrete GPUs keep the default storage; replaceRegion uploads it. */
    if (r->_device.hasUnifiedMemory)
        td.storageMode = MTLStorageModeShared;
    id<MTLTexture> tex = [r->_device newTextureWithDescriptor:td];
    if (!tex) {
        free(pixels);
        if (++r->_font_failures == 8u) {
            fprintf(stderr, "[OVERLAY-METAL] font texture unavailable\n");
            /* Only the core-backed renderer owns the core's input state. */
            if (r->_source.copy_font == overlay_core_copy_font)
                rsx_overlay_disable("Metal overlay font texture unavailable");
        }
        return 0;
    }
    tex.label = @"rsx_overlay_font";
    [tex replaceRegion:MTLRegionMake2D(0, 0, width, height)
           mipmapLevel:0
             withBytes:pixels
           bytesPerRow:(NSUInteger)width * 4u];
    free(pixels);
    r->_font = tex;
    r->_font_generation = generation;
    return 1;
}

static void overlay_blur_fail(RsxOverlayMetalRenderer* r, const char* why)
{
    if (r->_blur_failed)
        return;
    r->_blur_failed = 1;
    char line[160];
    snprintf(line, sizeof line, "overlay: backdrop blur unavailable (%s), dim only", why);
    rsx_overlay_log_write(RSX_OVERLAY_LOG_WARN, line);
    fprintf(stderr, "[OVERLAY-METAL] %s\n", line);
}

/* Called under _lock. Owned full-resolution copy of the drawable
 * (ShaderRead) plus the half-resolution pair; recreated only when the
 * drawable size or format changes. Never called while the shell is closed. */
static int overlay_prepare_blur(RsxOverlayMetalRenderer* r, id<MTLTexture> target)
{
    if (r->_blur_failed)
        return 0;
    if (target.framebufferOnly) {
        overlay_blur_fail(r, "drawable is framebufferOnly");
        return 0;
    }
    if (!r->_blur) {
        if (r->_test_fail_stage == 1 || !r->_backdrop_pipeline || !MPSSupportsMTLDevice(r->_device)) {
            overlay_blur_fail(r, "MPS unsupported");
            return 0;
        }
        r->_downscale = [[MPSImageBilinearScale alloc] initWithDevice:r->_device];
        r->_blur = [[MPSImageGaussianBlur alloc] initWithDevice:r->_device
                                                         sigma:RSX_OVERLAY_METAL_BLUR_SIGMA];
        if (!r->_downscale || !r->_blur) {
            r->_downscale = nil;
            r->_blur = nil;
            overlay_blur_fail(r, "MPS kernels unavailable");
            return 0;
        }
        r->_blur.edgeMode = MPSImageEdgeModeClamp;
    }
    const NSUInteger w = MAX((NSUInteger)1, target.width / 2);
    const NSUInteger h = MAX((NSUInteger)1, target.height / 2);
    if (!r->_blur_src || r->_blur_src.width != target.width ||
        r->_blur_src.height != target.height || r->_blur_src.pixelFormat != target.pixelFormat) {
        MTLTextureDescriptor* full =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:target.pixelFormat
                                                               width:target.width
                                                              height:target.height
                                                           mipmapped:NO];
        full.usage = MTLTextureUsageShaderRead;
        full.storageMode = MTLStorageModePrivate;
        MTLTextureDescriptor* half =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:target.pixelFormat
                                                               width:w
                                                              height:h
                                                           mipmapped:NO];
        half.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
        half.storageMode = MTLStorageModePrivate;
        r->_blur_src = r->_blur_a = r->_blur_b = nil;
        if (r->_test_fail_stage != 2) {
            r->_blur_src = [r->_device newTextureWithDescriptor:full];
            r->_blur_a = [r->_device newTextureWithDescriptor:half];
            r->_blur_b = [r->_device newTextureWithDescriptor:half];
        }
        if (!r->_blur_src || !r->_blur_a || !r->_blur_b) {
            r->_blur_src = r->_blur_a = r->_blur_b = nil;
            overlay_blur_fail(r, "scratch textures unavailable");
            return 0;
        }
        r->_blur_src.label = @"rsx_overlay_blur_src";
        r->_blur_a.label = @"rsx_overlay_blur_a";
        r->_blur_b.label = @"rsx_overlay_blur_b";
    }
    return 1;
}

rsx_overlay_metal_renderer* rsx_overlay_metal_renderer_create(
    id<MTLDevice> device, MTLPixelFormat color_format,
    const rsx_overlay_metal_source* source)
{
    if (!device)
        return NULL;
    RsxOverlayMetalRenderer* r = [[RsxOverlayMetalRenderer alloc] init];
    if (!r)
        return NULL;
    r->_device = device;
    r->_lock = OS_UNFAIR_LOCK_INIT;
    if (source) {
        r->_source = *source;
    } else {
        r->_source.visible = overlay_core_visible;
        r->_source.copy_packet = overlay_core_copy_packet;
        r->_source.copy_font = overlay_core_copy_font;
        r->_source.userdata = NULL;
        r->_source.atlas_generation = overlay_core_atlas_generation;
    }
    if (!r->_source.visible || !r->_source.copy_packet || !r->_source.copy_font)
        return NULL;
    if (!overlay_create_pipeline(r, color_format))
        return NULL;
    for (uint32_t i = 0; i < RSX_OVERLAY_METAL_FRAME_SLOTS; ++i) {
        r->_slots[i] = [device newBufferWithLength:sizeof(rsx_overlay_draw_packet)
                                           options:MTLResourceStorageModeShared];
        if (!r->_slots[i])
            return NULL;
        r->_slots[i].label = @"rsx_overlay_packet";
        atomic_init(&r->_slot_busy[i], 0);
    }
    /* +1 owned by the caller; released in destroy(). */
    return (rsx_overlay_metal_renderer*)CFBridgingRetain(r);
}

void rsx_overlay_metal_renderer_destroy(rsx_overlay_metal_renderer* renderer)
{
    if (!renderer)
        return;
    /* Completion handlers of frames still in flight hold their own strong
     * reference, so the slots outlive this release until the GPU is done. */
    RsxOverlayMetalRenderer* r = CFBridgingRelease(renderer);
    (void)r;
}

int rsx_overlay_metal_renderer_encode(rsx_overlay_metal_renderer* renderer,
    id<MTLCommandBuffer> command_buffer, id<MTLTexture> target,
    rsx_overlay_viewport viewport)
{
    if (!renderer || !command_buffer || !target)
        return 0;
    RsxOverlayMetalRenderer* r = (__bridge RsxOverlayMetalRenderer*)renderer;
    const uint32_t fb_w = (uint32_t)target.width;
    const uint32_t fb_h = (uint32_t)target.height;

    /* Hidden overlay: nothing is copied, no pass is created. */
    if (!r->_source.visible(r->_source.userdata) || fb_w == 0 || fb_h == 0 ||
        viewport.width == 0 || viewport.height == 0)
        return 0;

    int drawn = 0;
    os_unfair_lock_lock(&r->_lock);
    do {
        if (!overlay_ensure_font(r))
            break;

        /* Oldest-first ring: command buffers on one queue complete in order,
         * but two presenting threads may interleave, so each slot carries its
         * own busy flag cleared by the command buffer that read it. */
        uint32_t slot = RSX_OVERLAY_METAL_FRAME_SLOTS;
        for (uint32_t k = 0; k < RSX_OVERLAY_METAL_FRAME_SLOTS; ++k) {
            uint32_t s = (r->_next_slot + k) % RSX_OVERLAY_METAL_FRAME_SLOTS;
            if (atomic_load_explicit(&r->_slot_busy[s], memory_order_acquire) == 0) {
                slot = s;
                break;
            }
        }
        if (slot == RSX_OVERLAY_METAL_FRAME_SLOTS)
            break;   /* every slot still read by the GPU: skip, never block */

        id<MTLBuffer> buf = r->_slots[slot];
        rsx_overlay_draw_packet* packet = (rsx_overlay_draw_packet*)buf.contents;
        if (!r->_source.copy_packet(packet, r->_source.userdata) ||
            !rsx_overlay_packet_valid(packet))
            break;

        uint32_t draw_commands = 0;
        for (uint32_t c = 0; c < packet->command_count; ++c) {
            const rsx_overlay_draw_command* cmd = &packet->commands[c];
            /* Texture id 0 is the shell atlas; the core registers no other. */
            r->_cmd_draw[c] = (uint8_t)(rsx_overlay_command_uses_atlas(cmd) &&
                rsx_overlay_packet_command_valid(packet, cmd) &&
                rsx_overlay_clip_to_viewport_scissor(cmd->clip_rect, packet->display_width,
                                                     packet->display_height, &viewport,
                                                     fb_w, fb_h, &r->_cmd_scissor[c]));
            draw_commands += r->_cmd_draw[c];
        }
        if (draw_commands == 0)
            break;

        /* Backdrop: only while the shell is (partly) open. Same drawable,
         * same command buffer, before the UI pass; never a second present. */
        const float open = packet->open_progress < 0.0f
            ? 0.0f : (packet->open_progress > 1.0f ? 1.0f : packet->open_progress);
        int blurred = 0;
        if (packet->backdrop_blur && open > 0.001f && overlay_prepare_blur(r, target)) {
            /* Blit (needs only !framebufferOnly) into a texture we own with
             * ShaderRead; the drawable's own usage bits are never relied on. */
            id<MTLBlitCommandEncoder> copy = [command_buffer blitCommandEncoder];
            copy.label = @"rsx_overlay_backdrop_copy";
            [copy copyFromTexture:target toTexture:r->_blur_src];
            [copy endEncoding];
            [r->_downscale encodeToCommandBuffer:command_buffer
                                   sourceTexture:r->_blur_src
                              destinationTexture:r->_blur_a];
            [r->_blur encodeToCommandBuffer:command_buffer
                              sourceTexture:r->_blur_a
                         destinationTexture:r->_blur_b];
            blurred = 1;
        }

        MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture = target;
        pass.colorAttachments[0].loadAction = MTLLoadActionLoad;
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        id<MTLRenderCommandEncoder> enc =
            [command_buffer renderCommandEncoderWithDescriptor:pass];
        if (!enc)
            break;
        enc.label = @"rsx_overlay_ui";

        if (blurred) {
            /* The blurred frame fades in with the shell; the core's first UI
             * command then dims it (the same dim D3D12 gets without blur). */
            const MTLViewport whole = { 0.0, 0.0, (double)fb_w, (double)fb_h, 0.0, 1.0 };
            [enc setViewport:whole];
            [enc setRenderPipelineState:r->_backdrop_pipeline];
            [enc setFragmentTexture:r->_blur_b atIndex:0];
            [enc setFragmentSamplerState:r->_sampler atIndex:0];
            [enc setFragmentBytes:&open length:sizeof(open) atIndex:0];
            [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        }

        float xf[4];
        rsx_overlay_display_transform(packet->display_width, packet->display_height, xf);
        MTLViewport vp = { (double)viewport.x, (double)viewport.y,
                           (double)viewport.width, (double)viewport.height, 0.0, 1.0 };
        [enc setViewport:vp];
        [enc setRenderPipelineState:r->_pipeline];
        [enc setCullMode:MTLCullModeNone];
        [enc setVertexBuffer:buf offset:offsetof(rsx_overlay_draw_packet, vertices) atIndex:0];
        [enc setVertexBytes:xf length:sizeof(xf) atIndex:1];
        [enc setFragmentTexture:r->_font atIndex:0];
        [enc setFragmentSamplerState:r->_sampler atIndex:0];
        for (uint32_t c = 0; c < packet->command_count; ++c) {
            if (!r->_cmd_draw[c])
                continue;
            const rsx_overlay_draw_command* cmd = &packet->commands[c];
            const rsx_overlay_scissor* sc = &r->_cmd_scissor[c];
            MTLScissorRect rect = { (NSUInteger)sc->left, (NSUInteger)sc->top,
                                    (NSUInteger)(sc->right - sc->left),
                                    (NSUInteger)(sc->bottom - sc->top) };
            [enc setScissorRect:rect];
            [enc drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                            indexCount:cmd->element_count
                             indexType:MTLIndexTypeUInt16
                           indexBuffer:buf
                     indexBufferOffset:offsetof(rsx_overlay_draw_packet, indices) +
                                       (NSUInteger)cmd->index_offset * sizeof(uint16_t)
                         instanceCount:1
                            baseVertex:(NSInteger)cmd->vertex_offset
                          baseInstance:0];
        }
        [enc endEncoding];

        atomic_store_explicit(&r->_slot_busy[slot], 1, memory_order_release);
        r->_next_slot = (slot + 1u) % RSX_OVERLAY_METAL_FRAME_SLOTS;
        const uint32_t done_slot = slot;
        [command_buffer addCompletedHandler:^(id<MTLCommandBuffer> done) {
            (void)done;
            atomic_store_explicit(&r->_slot_busy[done_slot], 0, memory_order_release);
        }];
        drawn = 1;
    } while (0);
    os_unfair_lock_unlock(&r->_lock);
    return drawn;
}

#ifdef RSX_OVERLAY_TESTING
int rsx_overlay_metal_renderer_test_blur_state(rsx_overlay_metal_renderer* renderer)
{
    RsxOverlayMetalRenderer* r = (__bridge RsxOverlayMetalRenderer*)renderer;
    os_unfair_lock_lock(&r->_lock);
    const int state = (r->_blur_a ? 1 : 0) | (r->_blur_failed ? 2 : 0);
    os_unfair_lock_unlock(&r->_lock);
    return state;
}

void rsx_overlay_metal_renderer_test_fail_blur(rsx_overlay_metal_renderer* renderer, int stage)
{
    RsxOverlayMetalRenderer* r = (__bridge RsxOverlayMetalRenderer*)renderer;
    os_unfair_lock_lock(&r->_lock);
    r->_test_fail_stage = stage;
    os_unfair_lock_unlock(&r->_lock);
}
#endif
