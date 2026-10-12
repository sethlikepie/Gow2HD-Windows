#ifndef PS3RECOMP_RSX_OVERLAY_D3D12_PACKET_H
#define PS3RECOMP_RSX_OVERLAY_D3D12_PACKET_H

/* D3D12 names for the host-independent overlay packet helpers. The logic
 * lives in rsx_overlay_packet.h, shared with the Metal renderer; these thin
 * aliases keep the D3D12 renderer and its tests on their own names. */

#include "rsx_overlay_packet.h"

typedef rsx_overlay_scissor rsx_overlay_d3d12_scissor;

static inline int rsx_overlay_d3d12_packet_valid(const rsx_overlay_draw_packet* packet)
{
    return rsx_overlay_packet_valid(packet);
}

static inline int rsx_overlay_d3d12_packet_command_valid(
    const rsx_overlay_draw_packet* packet,
    const rsx_overlay_draw_command* command)
{
    return rsx_overlay_packet_command_valid(packet, command);
}

static inline int rsx_overlay_d3d12_clip_to_scissor(
    const float clip_rect[4], uint32_t display_width, uint32_t display_height,
    uint32_t framebuffer_width, uint32_t framebuffer_height,
    rsx_overlay_d3d12_scissor* out)
{
    return rsx_overlay_clip_to_scissor(clip_rect, display_width, display_height,
                                       framebuffer_width, framebuffer_height, out);
}

static inline void rsx_overlay_d3d12_display_transform(uint32_t display_width,
                                                       uint32_t display_height, float out[4])
{
    rsx_overlay_display_transform(display_width, display_height, out);
}

#endif
