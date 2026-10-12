/*
 * d3d12_fork_shim.c -- entry points the gow2 fork's runtime calls on its OWN D3D12 backend
 * that upstream's renderer (vendored next to this file) does not have. Linked only when the
 * Windows build uses the upstream renderer (build_windows.sh, D3D12=upstream).
 *
 * First pass: inert. Consequences, all cosmetic for a first "does the game draw" test:
 *   - movie_hle.c's host FMV frames (intro logo) are not shown (the game still runs the movie
 *     and its sound; the guest's own display buffer is what the renderer presents);
 *   - rsx_host_content.c's legal-screen hold / forced texture bind do nothing;
 *   - the runtime overlay's shader counters read 0 ("unavailable" in the hub).
 * TODO(winport): present movie RGBA frames through the upstream renderer's swap chain.
 */
#include <stdint.h>

/* rsx_d3d12_movie_frame / _playback_begin / _playback_end / _stop: implemented in
 * rsx_d3d12_backend.c (winport-fix 13, host movie overlay). */
void rsx_d3d12_content_hold(const void* rgba, unsigned w, unsigned h) { (void)rgba; (void)w; (void)h; }
void rsx_d3d12_force_bind_rgba(unsigned unit, const void* rgba, unsigned w, unsigned h)
{ (void)unit; (void)rgba; (void)w; (void)h; }
uint32_t rsx_d3d12_fp_cache_hits(void)     { return 0; }
uint32_t rsx_d3d12_fp_cache_misses(void)   { return 0; }
uint32_t rsx_d3d12_fp_decompile_ok(void)   { return 0; }
uint32_t rsx_d3d12_fp_decompile_fail(void) { return 0; }
