/*
 * ps3recomp - Metal RSX backend (macOS / Apple Silicon)
 *
 * Phase F5 skeleton from docs/MACOS_PORT_PLAN.md: MTLDevice, command queue and
 * a CAMetalLayer swapchain, presenting the RSX clear colour each flip. This is
 * the Metal counterpart of the D3D12 phase-1 path.
 *
 * What is NOT here yet, in the order the D3D12 backend grew them:
 *   - vertex upload with big-endian byte swap
 *   - pipeline state objects and the vertex-colour pass
 *   - NV40 fragment/vertex program translation to MSL
 *   - textures
 *
 * Windowing comes from SDL2 (SDL_Metal_CreateView), so this backend does not
 * open its own NSWindow and stays consistent with the SDL input/audio paths.
 */

#ifndef PS3RECOMP_RSX_METAL_BACKEND_H
#define PS3RECOMP_RSX_METAL_BACKEND_H

#include "rsx_commands.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Create the window, Metal device and layer, and register the backend.
 * Returns 0 on success, -1 if Metal or the window is unavailable. */
int rsx_metal_backend_init(u32 width, u32 height, const char* title);

/* Tear down the swapchain and window, and unregister the backend. */
void rsx_metal_backend_shutdown(void);

/* Drain window events. Returns 0 normally, -1 once the window is closed. */
int rsx_metal_backend_pump_messages(void);

/* -------------------------------------------------------------------------
 * Host movie overlay (paridade com rsx_d3d12_movie_*).
 *
 * Contrato de pixel: **BGRA8888** packed, row-major, top-down (byte order
 * B,G,R,A) — o mesmo que AVAssetReader/VideoToolbox entrega com
 * kCVPixelFormatType_32BGRA e o pixelFormat da CAMetalLayer (BGRA8Unorm).
 * NAO e o RGBA do pipe ffmpeg no Windows: o caller Mac (movie_vt_metal) deve
 * pedir BGRA ao VT ou swizzle antes. Thread-safe: pode ser chamado da thread
 * de decode; o blit corre em metal_present (thread do pump).
 *
 * M4 zero-copy: prefer `rsx_metal_movie_frame_cvpixel` (CVPixelBuffer /
 * IOSurface → CVMetalTextureCache). Falls back to CPU BGRA via
 * `rsx_metal_movie_frame` if cache create fails or PS3_MOVIE_FORCE_RGBA=1.
 * `cv_pixel_buffer` is a CVPixelBufferRef (void* so non-ObjC TUs can link).
 * -----------------------------------------------------------------------*/
void rsx_metal_movie_frame(const void* bgra, unsigned w, unsigned h);
/* Stage BGRA for next present without submitting a drawable (safer from
 * worker threads than full presentDrawable). */
void rsx_metal_movie_frame_stage(const void* bgra, unsigned w, unsigned h);
void rsx_metal_movie_frame_cvpixel(void* cv_pixel_buffer);
void rsx_metal_movie_stop(void);
void rsx_metal_movie_playback_begin(void);
void rsx_metal_movie_playback_end(void);
/* Present one overlay frame immediately (host movie thread; not only GCM flip). */
void rsx_metal_movie_present(void);

/* -------------------------------------------------------------------------
 * Content hold (M8) — sticky HOSTRES / legal-screen RGBA fullscreen.
 * Paused while movie HLE plays (parity with rsx_d3d12_content_hold).
 * Input is **RGBA8888** host order (same as Win); converted to BGRA for Metal.
 * -----------------------------------------------------------------------*/
void rsx_metal_content_hold(const void* rgba, unsigned w, unsigned h);
void rsx_metal_content_hold_clear(void);
/* Optional: force-bind host RGBA into Metal texture unit (M3 path). */
void rsx_metal_force_bind_rgba(unsigned unit, const void* rgba, unsigned w, unsigned h);

/* 1 while movie HLE overlay is actively presenting (M0/M4). cellVdec uses this
 * so PS3_VDEC_METAL_PRESENT does not fight the logo overlay. */
int rsx_metal_movie_overlay_active(void);

/* -------------------------------------------------------------------------
 * V2.0 RSX capture/replay (Task 3) -- native-size surface dump.
 * -----------------------------------------------------------------------*/
/* Commits and waits for in-flight GPU work, then writes <prefix>_display.bmp
 * (the surface the last present showed) and <prefix>_c<loc>_<offset:08X>_
 * <w>x<h>.bmp for every initialised colour surface (and <prefix>_z<loc>_...
 * for depth surfaces already converted to a dumpable colour view). Returns
 * the number of files written. Registered with rsx_capture_set_dump_hook()
 * (rsx_capture.h) at the end of a successful rsx_metal_backend_init(); also
 * called directly by the Task 4 replay host. See rsx_metal_backend.m for the
 * thread-safety reasoning (it relies on the present choke point's own
 * serialisation, adding no new locking). */
int rsx_metal_dump_surfaces(const char* prefix);

#ifdef __cplusplus
}
#endif
#endif
