#ifndef PS3RECOMP_RSX_OVERLAY_H
#define PS3RECOMP_RSX_OVERLAY_H

#include <stddef.h>
#include <stdint.h>

#include "rsx_overlay_feedback.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct rsx_overlay_settings rsx_overlay_settings;

typedef struct rsx_overlay_snapshot {
    double fps;
    double frame_ms;
    uint64_t guest_draws_frame;
    uint64_t guest_draws_total;
    int wad_state_available;
    char wad_state[96];
    int shader_stats_available;
    uint32_t shader_hits;
    uint32_t shader_misses;
    uint32_t shader_compiles;
    uint32_t shader_failures;
} rsx_overlay_snapshot;

/* Backend shader-cache counters, cumulative for the session. hits/misses count
 * shader-cache lookups; compiles counts shader programs the backend compiled;
 * failures counts decompile or compile attempts that fell back. */
typedef struct rsx_overlay_shader_stats {
    uint32_t hits;
    uint32_t misses;
    uint32_t compiles;
    uint32_t failures;
} rsx_overlay_shader_stats;

typedef struct rsx_overlay_backend_metrics {
    double fps;
    double frame_ms;
    uint64_t guest_draws_frame;
    uint64_t guest_draws_total;
} rsx_overlay_backend_metrics;

/* Largest drawable side (pixels) a packet may describe. Positions and clip
 * rectangles are drawable pixels of display_width x display_height. */
#define RSX_OVERLAY_DISPLAY_MAX 16384u

#define RSX_OVERLAY_DRAW_VERTEX_CAPACITY 32768u
#define RSX_OVERLAY_DRAW_INDEX_CAPACITY 65536u
#define RSX_OVERLAY_DRAW_COMMAND_CAPACITY 2048u

typedef struct rsx_overlay_draw_vertex {
    float position[2];
    float uv[2];
    uint32_t color;
} rsx_overlay_draw_vertex;

typedef struct rsx_overlay_draw_command {
    uint32_t element_count;
    uint32_t index_offset;
    uint32_t vertex_offset;
    float clip_rect[4];
    uintptr_t texture_id;
} rsx_overlay_draw_command;

typedef struct rsx_overlay_draw_packet {
    uint64_t generation;
    uint32_t display_width;   /* drawable pixels the UI was laid out for */
    uint32_t display_height;
    float backing_scale;      /* drawable pixels per point */
    float open_progress;      /* 0 closed .. 1 fully open */
    float backdrop_dim;       /* informative: the core draws the dim as its first command */
    uint32_t backdrop_blur;   /* 1: renderer may blur the frame behind the UI (Metal only) */
    uint32_t vertex_count;
    uint32_t index_count;
    uint32_t command_count;
    rsx_overlay_draw_vertex vertices[RSX_OVERLAY_DRAW_VERTEX_CAPACITY];
    uint16_t indices[RSX_OVERLAY_DRAW_INDEX_CAPACITY];
    rsx_overlay_draw_command commands[RSX_OVERLAY_DRAW_COMMAND_CAPACITY];
} rsx_overlay_draw_packet;

enum {
    RSX_OVERLAY_KEY_F1 = 0x70,
    RSX_OVERLAY_KEY_F2 = 0x71,
    RSX_OVERLAY_KEY_UP = 0x26,
    RSX_OVERLAY_KEY_DOWN = 0x28,
    RSX_OVERLAY_KEY_LEFT = 0x25,
    RSX_OVERLAY_KEY_RIGHT = 0x27,
    RSX_OVERLAY_KEY_ENTER = 0x0d,
    RSX_OVERLAY_KEY_ESCAPE = 0x1b
};

/* Modifier mask passed to rsx_overlay_handle_key. */
enum {
    RSX_OVERLAY_MOD_SHIFT = 1u << 0,
    RSX_OVERLAY_MOD_CTRL = 1u << 1,
    RSX_OVERLAY_MOD_ALT = 1u << 2
};

/* How the active backend consumes a graphics setting. */
typedef enum rsx_overlay_setting_support {
    RSX_OVERLAY_SETTING_UNAVAILABLE = 0,
    RSX_OVERLAY_SETTING_LIVE = 1,
    RSX_OVERLAY_SETTING_RESTART_REQUIRED = 2
} rsx_overlay_setting_support;

typedef struct rsx_overlay_backend_capabilities {
    uint8_t window_size; /* rsx_overlay_setting_support */
    uint8_t fullscreen;
    uint8_t vsync;
} rsx_overlay_backend_capabilities;

typedef struct rsx_overlay_graphics_settings {
    uint32_t window_width;
    uint32_t window_height;
    uint8_t fullscreen;
    uint8_t vsync;
} rsx_overlay_graphics_settings;

int rsx_overlay_init(const char* settings_path);
void rsx_overlay_shutdown(void);
int rsx_overlay_visible(void);
int rsx_overlay_handle_key(int key, int down, unsigned modifiers);
int rsx_overlay_handle_pad(uint16_t host_buttons, uint16_t* guest_buttons);
void rsx_overlay_focus_lost(void);
/* The selected backend could not build its overlay renderer (or its font
 * texture): the menu could never be seen, so the core stops acting on input
 * -- no toggle, nothing withheld from the guest, rsx_overlay_visible() 0.
 * Callable from any thread; sticky until rsx_overlay_shutdown(), including
 * when called before rsx_overlay_init(). `reason` goes to the overlay log. */
void rsx_overlay_disable(const char* reason);
int rsx_overlay_disabled(void);
/* PS/Home on the controller: any thread; toggles the menu on the rising edge
 * (mirrors the Select+Start chord) and is never a guest bit -- the pad
 * backend never folds it into CELL_PAD_CTRL_*. A no-op while disabled. */
void rsx_overlay_handle_home(int down);
/* Top-strip controller status. `battery_percent` is -1 (unknown) unless the
 * controller itself reports one; a disconnected pad always reads -1, never a
 * stale value. Publish from the pad backend; read from the render thread. */
void rsx_overlay_publish_controller_status(int connected, int battery_percent);
void rsx_overlay_get_controller_status(int* connected, int* battery_percent);
/* Pad buttons the open menu reserves (never forwarded to the guest while it
 * is visible and driven by them). */
uint16_t rsx_overlay_pad_navigation_mask(void);
/* Provider callbacks must not register/unregister providers. Reentrant updates
 * are rejected and logged; external replacement waits for active callbacks. */
void rsx_overlay_set_game_provider(
    void (*provider)(rsx_overlay_snapshot* out, void* userdata), void* userdata);
void rsx_overlay_set_snapshot_provider(
    void (*provider)(rsx_overlay_snapshot* out, void* userdata), void* userdata);
/* Exactly one active publisher per selected backend. */
void rsx_overlay_publish_backend_metrics(const rsx_overlay_backend_metrics* metrics);
void rsx_overlay_build_draw_data(void);
/* Event thread (the one that builds draw data). Size in pixels of the drawable
 * the UI is laid out on and its backing scale (pixels per point). Zero sizes
 * (minimized window) and scales outside [0.5, 4] keep the previous value. */
void rsx_overlay_set_display(uint32_t width_px, uint32_t height_px, float backing_scale);
void rsx_overlay_get_display(uint32_t* width_px, uint32_t* height_px, float* backing_scale);
/* Display publication is event-driven, never polled by a closed shell: pumps
 * call rsx_overlay_set_display only while rsx_overlay_display_stale() is 1.
 * It is 1 after init, after rsx_overlay_invalidate_display() (window resized
 * or moved to a display with another backing scale), on every open and after
 * an asset-builder change; any rsx_overlay_set_display call clears it. */
void rsx_overlay_invalidate_display(void);
int rsx_overlay_display_stale(void);
/* Shell opening/open/closing: renderers draw, pumps build. */
int rsx_overlay_render_active(void);
/* 1 once after "Sair" was confirmed. */
int rsx_overlay_take_quit_request(void);
/* Before the backend starts; default "Jogo". */
void rsx_overlay_set_title(const char* title);
void rsx_overlay_set_feedback_sink(rsx_overlay_feedback_sink sink, void* userdata);
/* cellpad_overlay_filter: every host bit (keyboard included) of this frame;
 * returns the buttons still withheld because they were held when the menu
 * closed (every navigation button held at the first poll after a close) or
 * because a remap capture just bound them. */
uint16_t rsx_overlay_release_guard(uint16_t host_buttons);
/* Any thread, atomics only. 1 when a pad frame without the full Select+Start
 * chord would pass through cellpad_overlay_filter untouched and change no
 * overlay state: uninitialized, or closed with no release guard, capture
 * hold, pending keyboard close, chord state or UI pad bits. cellPadGetData
 * then skips the filter (PS/Home is still routed). */
int rsx_overlay_pad_idle(void);
/* Event/ImGui thread only. Writes the settings file when a setting changed
 * since the last save (menu open/close, a Graphics or Controls change once no
 * direction is held); an idle call does no I/O. Returns 1 saved, 0 nothing to
 * save, -1 the save failed (logged once; retried only after the next change).
 * build_draw_data calls it; pumps call it while the menu is hidden. */
int rsx_overlay_persist_settings(void);
/* Settings path handed over by the launcher (PS3_OVERLAY_SETTINGS), or NULL
 * for the per-user default location. */
const char* rsx_overlay_settings_path_from_env(void);
/* The selected backend publishes its cumulative shader counters (once per
 * frame, from its render thread). Readers get a coherent copy; returns 0 until
 * something was published in this overlay session. */
void rsx_overlay_publish_shader_stats(const rsx_overlay_shader_stats* stats);
int rsx_overlay_read_shader_stats(rsx_overlay_shader_stats* out);
int rsx_overlay_copy_draw_packet(rsx_overlay_draw_packet* out);
/* Copies the shell's glyph atlas (texture 0: RGBA8, straight alpha, white
 * RGB, coverage in A) built on the event thread, so a renderer never touches
 * it. With rgba == NULL only the size is reported.
 * Returns 0 when the overlay is not initialized or capacity is too small. */
int rsx_overlay_copy_font_atlas(uint8_t* rgba, size_t capacity,
                                uint32_t* width, uint32_t* height);
typedef struct rsx_overlay_assets rsx_overlay_assets;
typedef int (*rsx_overlay_asset_builder)(rsx_overlay_assets* out, float scale);
/* NULL: embedded Inter. Takes effect at the next display publication. */
void rsx_overlay_set_asset_builder(rsx_overlay_asset_builder builder);
/* Bumped every time the atlas returned by rsx_overlay_copy_font_atlas changes
 * (init, backing-scale change, builder change). Renderers re-upload on change. */
uint64_t rsx_overlay_font_atlas_generation(void);
/* The selected backend registers how it consumes each graphics setting; NULL
 * clears the registration. Unregistered or unavailable settings are shown as
 * unavailable and never change. */
void rsx_overlay_set_backend_capabilities(const rsx_overlay_backend_capabilities* caps);
int rsx_overlay_get_backend_capabilities(rsx_overlay_backend_capabilities* out);
/* Thread-safe copy of the persisted graphics settings. Returns 0 (and leaves
 * out untouched) when the overlay is not initialized. */
int rsx_overlay_get_graphics_settings(rsx_overlay_graphics_settings* out);
#ifdef RSX_OVERLAY_TESTING
void rsx_overlay_test_set_clock(double seconds);          /* < 0: real clock */
/* Called inside every close, between its two atomic steps (a guest poll racing it). */
void rsx_overlay_test_set_close_hook(void (*hook)(void));
/* RSX_OVERLAY_KEY_* or CELL_PAD_CTRL_* as the last build saw it. */
int rsx_overlay_test_input_down(int code);
void rsx_overlay_test_shell_state(int* phase, int* level, int* card, int* row, int* list_index);
float rsx_overlay_test_atlas_scale(void);
int rsx_overlay_test_backend_metrics_available(void);
/* Applies a graphics change exactly as the Display card does. */
int rsx_overlay_test_apply_graphics(const rsx_overlay_graphics_settings* request);
#endif
void rsx_overlay_snapshot_current(rsx_overlay_snapshot* out);

#ifdef __cplusplus
}
#endif

#endif
