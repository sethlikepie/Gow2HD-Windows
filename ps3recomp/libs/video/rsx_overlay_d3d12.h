#ifndef PS3RECOMP_RSX_OVERLAY_D3D12_H
#define PS3RECOMP_RSX_OVERLAY_D3D12_H

/* D3D12 renderer for the runtime overlay. It consumes only the immutable draw
 * packet and font atlas copied out of the overlay core (rsx_overlay.h) and
 * owns every GPU object it uses (root signature, PSO, SRV heap, font texture,
 * per-frame upload buffers): no guest shader, texture or descriptor is reused
 * or modified. Windows-only; the caller includes <d3d12.h> first. */

#ifdef _WIN32

#include <d3d12.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Per-frame upload slots. The caller's frame index must be below this and
 * each slot must only be reused after the frame fence of its previous use. */
#define RSX_OVERLAY_D3D12_FRAME_SLOTS 2u

typedef struct rsx_overlay_d3d12_renderer rsx_overlay_d3d12_renderer;

/* rtv_format/dsv_format must match the targets bound when render() records;
 * dsv_format may be DXGI_FORMAT_UNKNOWN when no depth target is bound. */
rsx_overlay_d3d12_renderer* rsx_overlay_d3d12_renderer_create(ID3D12Device* device,
    DXGI_FORMAT rtv_format, DXGI_FORMAT dsv_format);

/* The caller must have waited for the GPU to finish every recorded frame. */
void rsx_overlay_d3d12_renderer_destroy(rsx_overlay_d3d12_renderer* renderer);

/* Records the UI pass into an open command list whose render target is bound
 * and in RENDER_TARGET state. Sets its own viewport, scissor, root signature,
 * PSO and descriptor heap. Returns 1 when UI draws were recorded, 0 when the
 * overlay is hidden, has nothing to draw, or is not ready. */
int rsx_overlay_d3d12_renderer_render(rsx_overlay_d3d12_renderer* renderer,
    ID3D12GraphicsCommandList* command_list, uint32_t frame_slot,
    uint32_t framebuffer_width, uint32_t framebuffer_height);

#ifdef __cplusplus
}
#endif

#endif /* _WIN32 */
#endif
