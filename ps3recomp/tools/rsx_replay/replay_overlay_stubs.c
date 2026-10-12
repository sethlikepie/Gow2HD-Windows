/* replay_overlay_stubs.c -- the runtime overlay (console shell) surface the
 * Metal backend references, stubbed for the offline replay host.
 *
 * The overlay is host UI: it must never draw into a replayed frame, so the
 * core reports "never initialized" (render inactive, display never stale, no
 * quit request) and the Metal UI renderer is never created -- the backend
 * already treats a NULL renderer as "overlay unavailable". Pointer arguments
 * that are Objective-C/opaque types in the real headers are declared as
 * void* here; only the C symbol names matter to the linker. */
#include <stddef.h>
#include <stdint.h>

#include "rsx_overlay.h"
#include "rsx_overlay_log.h"
#include "rsx_overlay_assets.h"
#include "rsx_overlay_packet.h"

int  rsx_overlay_assets_build_apple(rsx_overlay_assets* out, float scale) { (void)out; (void)scale; return 0; }
void rsx_overlay_build_draw_data(void) {}
void rsx_overlay_disable(const char* reason) { (void)reason; }
int  rsx_overlay_display_stale(void) { return 0; }
void rsx_overlay_invalidate_display(void) {}
void rsx_overlay_focus_lost(void) {}
int  rsx_overlay_get_graphics_settings(rsx_overlay_graphics_settings* out) { (void)out; return 0; }
int  rsx_overlay_handle_key(int key, int down, unsigned modifiers) { (void)key; (void)down; (void)modifiers; return 0; }
void rsx_overlay_log_write(int level, const char* message) { (void)level; (void)message; }
int  rsx_overlay_persist_settings(void) { return 0; }
void rsx_overlay_publish_backend_metrics(const rsx_overlay_backend_metrics* m) { (void)m; }
void rsx_overlay_publish_shader_stats(const rsx_overlay_shader_stats* s) { (void)s; }
int  rsx_overlay_render_active(void) { return 0; }
void rsx_overlay_set_asset_builder(rsx_overlay_asset_builder builder) { (void)builder; }
void rsx_overlay_set_backend_capabilities(const rsx_overlay_backend_capabilities* caps) { (void)caps; }
void rsx_overlay_set_display(uint32_t w, uint32_t h, float scale) { (void)w; (void)h; (void)scale; }
void rsx_overlay_set_feedback_sink(rsx_overlay_feedback_sink sink, void* userdata) { (void)sink; (void)userdata; }
int  rsx_overlay_take_quit_request(void) { return 0; }

/* rsx_overlay_feedback_apple.h / cellpad_gamecontroller.h (Apple-only). */
void rsx_overlay_feedback_apple_play(int event, int sounds, int haptics, void* userdata)
{ (void)event; (void)sounds; (void)haptics; (void)userdata; }
void rsx_overlay_feedback_apple_set_haptic(void (*tick)(float, float)) { (void)tick; }
void rsx_overlay_feedback_apple_shutdown(void) {}
void cpgc_ui_tick(float intensity, float sharpness) { (void)intensity; (void)sharpness; }

/* rsx_overlay_metal.h: never created in replay (id<MTLDevice>/id<MTLTexture>
 * arguments are plain object pointers at the ABI level). */
void* rsx_overlay_metal_renderer_create(void* device, unsigned long color_format, const void* source)
{ (void)device; (void)color_format; (void)source; return NULL; }
void rsx_overlay_metal_renderer_destroy(void* renderer) { (void)renderer; }
int  rsx_overlay_metal_renderer_encode(void* renderer, void* command_buffer, void* target,
                                      rsx_overlay_viewport viewport)
{ (void)renderer; (void)command_buffer; (void)target; (void)viewport; return 0; }
