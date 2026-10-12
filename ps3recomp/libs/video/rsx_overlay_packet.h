#ifndef PS3RECOMP_RSX_OVERLAY_PACKET_H
#define PS3RECOMP_RSX_OVERLAY_PACKET_H

/* Host-independent helpers shared by the overlay renderers (D3D12, Metal).
 * They only look at the immutable packet copied out of the overlay core, so
 * they are unit-tested on every host (libs/video/tests/test_rsx_overlay_d3d12.c
 * and test_rsx_overlay_metal.m). */

#include <math.h>
#include <stdint.h>
#include <stddef.h>

#include "rsx_overlay.h"

/* Pixel rectangle as [left, right) x [top, bottom). */
typedef struct rsx_overlay_scissor {
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
} rsx_overlay_scissor;

/* Region of the framebuffer the packet's display is mapped onto (normally
 * the whole drawable). */
typedef struct rsx_overlay_viewport {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
} rsx_overlay_viewport;

static inline int rsx_overlay_packet_valid(const rsx_overlay_draw_packet* packet)
{
    return packet != NULL &&
           packet->display_width != 0 && packet->display_height != 0 &&
           packet->display_width <= RSX_OVERLAY_DISPLAY_MAX &&
           packet->display_height <= RSX_OVERLAY_DISPLAY_MAX &&
           packet->vertex_count != 0 && packet->index_count != 0 &&
           packet->command_count != 0 &&
           packet->vertex_count <= RSX_OVERLAY_DRAW_VERTEX_CAPACITY &&
           packet->index_count <= RSX_OVERLAY_DRAW_INDEX_CAPACITY &&
           packet->command_count <= RSX_OVERLAY_DRAW_COMMAND_CAPACITY;
}

/* Every index the command draws, shifted by its base vertex, must land inside
 * the packet's vertices; the GPU never sees an out-of-range fetch. */
static inline int rsx_overlay_packet_command_valid(
    const rsx_overlay_draw_packet* packet,
    const rsx_overlay_draw_command* command)
{
    uint32_t index;
    uint32_t end;
    uint32_t vertices_after_base;

    if (packet == NULL || command == NULL || command->element_count == 0 ||
        packet->vertex_count > RSX_OVERLAY_DRAW_VERTEX_CAPACITY ||
        packet->index_count > RSX_OVERLAY_DRAW_INDEX_CAPACITY ||
        command->index_offset > packet->index_count ||
        command->element_count > packet->index_count - command->index_offset ||
        command->vertex_offset >= packet->vertex_count)
        return 0;
    vertices_after_base = packet->vertex_count - command->vertex_offset;
    end = command->index_offset + command->element_count;
    for (index = command->index_offset; index < end; ++index) {
        if ((uint32_t)packet->indices[index] >= vertices_after_base)
            return 0;
    }
    return 1;
}

/* Clip rectangles are in the packet's display pixels. They are scaled onto
 * the viewport (normally the whole drawable), offset by its origin and clamped
 * to both the viewport and the framebuffer. Returns 0 when nothing is left. */
/* Atlas contract: texture 0 is the shell atlas (RGBA8, straight alpha, white
 * RGB); no other texture exists, so any other id is skipped. */
static inline int rsx_overlay_command_uses_atlas(const rsx_overlay_draw_command* command)
{
    return command != NULL && command->texture_id == 0;
}

static inline int rsx_overlay_clip_to_viewport_scissor(
    const float clip_rect[4], uint32_t display_width, uint32_t display_height,
    const rsx_overlay_viewport* viewport,
    uint32_t framebuffer_width, uint32_t framebuffer_height,
    rsx_overlay_scissor* out)
{
    float scale_x, scale_y, left, top, right, bottom, min_x, min_y, max_x, max_y;

    if (clip_rect == NULL || viewport == NULL || out == NULL ||
        display_width == 0 || display_height == 0 ||
        framebuffer_width == 0 || framebuffer_height == 0 ||
        viewport->width == 0 || viewport->height == 0)
        return 0;
    /* Empty (or NaN) rectangles draw nothing; rounding outwards must not turn
     * them into a one-pixel scissor. */
    if (!(clip_rect[2] > clip_rect[0]) || !(clip_rect[3] > clip_rect[1]))
        return 0;
    scale_x = (float)viewport->width / (float)display_width;
    scale_y = (float)viewport->height / (float)display_height;
    left = floorf(clip_rect[0] * scale_x) + (float)viewport->x;
    top = floorf(clip_rect[1] * scale_y) + (float)viewport->y;
    right = ceilf(clip_rect[2] * scale_x) + (float)viewport->x;
    bottom = ceilf(clip_rect[3] * scale_y) + (float)viewport->y;
    /* NaN fails every ordered comparison: reject before clamping. */
    if (!(left == left) || !(top == top) || !(right == right) || !(bottom == bottom))
        return 0;
    min_x = (float)viewport->x;
    min_y = (float)viewport->y;
    max_x = (float)viewport->x + (float)viewport->width;
    max_y = (float)viewport->y + (float)viewport->height;
    if (max_x > (float)framebuffer_width) max_x = (float)framebuffer_width;
    if (max_y > (float)framebuffer_height) max_y = (float)framebuffer_height;
    if (left < min_x) left = min_x;
    if (top < min_y) top = min_y;
    if (right > max_x) right = max_x;
    if (bottom > max_y) bottom = max_y;
    if (!(right > left) || !(bottom > top))
        return 0;
    out->left = (int32_t)left;
    out->top = (int32_t)top;
    out->right = (int32_t)right;
    out->bottom = (int32_t)bottom;
    return 1;
}

/* Whole-framebuffer form (the display covers the framebuffer). */
static inline int rsx_overlay_clip_to_scissor(
    const float clip_rect[4], uint32_t display_width, uint32_t display_height,
    uint32_t framebuffer_width, uint32_t framebuffer_height, rsx_overlay_scissor* out)
{
    rsx_overlay_viewport viewport;

    viewport.x = 0;
    viewport.y = 0;
    viewport.width = framebuffer_width;
    viewport.height = framebuffer_height;
    return rsx_overlay_clip_to_viewport_scissor(clip_rect, display_width, display_height,
        &viewport, framebuffer_width, framebuffer_height, out);
}

/* {scale.xy, translate.xy}: display pixels to clip space, (0,0) top-left. */
static inline void rsx_overlay_display_transform(uint32_t display_width,
                                                 uint32_t display_height, float out[4])
{
    out[0] = 2.0f / (float)(display_width ? display_width : 1u);
    out[1] = -2.0f / (float)(display_height ? display_height : 1u);
    out[2] = -1.0f;
    out[3] = 1.0f;
}

#endif
