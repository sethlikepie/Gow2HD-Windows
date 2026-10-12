/*
 * ps3recomp - Vulkan RSX backend
 *
 * Phase F5 of docs/MACOS_PORT_PLAN.md, the cross-platform option: one backend
 * that serves Linux natively and macOS through MoltenVK, rather than a second
 * Apple-only path beside D3D12.
 *
 * On Apple Silicon this is Vulkan translated to Metal by MoltenVK, so it sits
 * one layer above the native Metal backend. That is the trade: portability now
 * in exchange for a translation layer between us and the GPU.
 *
 * Current scope matches the Metal skeleton: instance, device, swapchain and
 * the RSX clear colour through a real render pass. Vertex upload, pipeline
 * state objects, NV40 shader translation to SPIR-V and textures are not here.
 *
 * Windowing comes from SDL2 (SDL_Vulkan_CreateSurface), shared with the input
 * and audio paths.
 */

#ifndef PS3RECOMP_RSX_VULKAN_BACKEND_H
#define PS3RECOMP_RSX_VULKAN_BACKEND_H

#include "rsx_commands.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Create the window, Vulkan device and swapchain, and register the backend.
 * Returns 0 on success, -1 if Vulkan or the window is unavailable. */
int rsx_vulkan_backend_init(u32 width, u32 height, const char* title);

/* Tear down the swapchain, device and window, and unregister the backend. */
void rsx_vulkan_backend_shutdown(void);

/* Drain window events. Returns 0 normally, -1 once the window is closed. */
int rsx_vulkan_backend_pump_messages(void);

#ifdef __cplusplus
}
#endif
#endif
