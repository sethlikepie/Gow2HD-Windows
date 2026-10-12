#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdatomic.h>
#include <sys/stat.h>

#include "cellPad.h"
#include "rsx_overlay.h"
#include "rsx_overlay_feedback.h"
#include "rsx_overlay_log.h"
#include "rsx_overlay_settings.h"
#include "rsx_overlay_shell.h"

int ps3_audio_runtime_muted(void);
void ps3_audio_set_runtime_mute(int muted);

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "[FAIL] %s:%d: %s\n", __func__, __LINE__, #expr); \
        return 0; \
    } \
} while (0)

void cellPad_set_host_mapping(const uint16_t map[CELL_PAD_HOST_BUTTON_COUNT],
                              uint8_t deadzone)
{
    (void)map;
    (void)deadzone;
}

static uint8_t s_last_vibration = 1;
void cellPad_set_host_vibration(int enabled) { s_last_vibration = (uint8_t)(enabled != 0); }

static double s_now = 1000.0;
static void tick(double dt) { s_now += dt; rsx_overlay_test_set_clock(s_now); }
static void settle(void) { tick(1.0); rsx_overlay_build_draw_data(); }

static void game_provider(rsx_overlay_snapshot* out, void* userdata)
{
    int* calls = (int*)userdata;
    ++*calls;
    out->fps = 1.0;
    out->frame_ms = 999.0;
    out->guest_draws_frame = 1;
    out->guest_draws_total = 2;
    out->wad_state_available = 1;
    strcpy(out->wad_state, "loading R_PermA");
    out->shader_stats_available = 1;
    out->shader_hits = 9;
    out->shader_misses = 3;
    out->shader_compiles = 2;
    out->shader_failures = 1;
}

typedef struct provider_lifetime {
    uint64_t cookie;
    atomic_int* entered;
} provider_lifetime;

static void lifetime_provider(rsx_overlay_snapshot* out, void* opaque)
{
    provider_lifetime* data = (provider_lifetime*)opaque;
    atomic_store(data->entered, 1);
    usleep(5000);
    out->shader_hits = (uint32_t)data->cookie;
    out->shader_stats_available = 1;
}

static void* provider_snapshot_thread(void* opaque)
{
    rsx_overlay_snapshot* snapshot = (rsx_overlay_snapshot*)opaque;
    rsx_overlay_snapshot_current(snapshot);
    return NULL;
}

static rsx_overlay_backend_metrics metric_a = { 60.0, 16.0, 100, 1000 };
static rsx_overlay_backend_metrics metric_b = { 30.0, 33.0, 200, 2000 };
static void* metric_writer_thread(void* unused)
{
    unsigned i;
    (void)unused;
    for (i = 0; i < 40000; ++i)
        rsx_overlay_publish_backend_metrics((i & 1) ? &metric_a : &metric_b);
    return NULL;
}

static int metrics_are_known(const rsx_overlay_snapshot* snapshot)
{
    return (snapshot->fps == metric_a.fps && snapshot->frame_ms == metric_a.frame_ms &&
            snapshot->guest_draws_frame == metric_a.guest_draws_frame &&
            snapshot->guest_draws_total == metric_a.guest_draws_total) ||
           (snapshot->fps == metric_b.fps && snapshot->frame_ms == metric_b.frame_ms &&
            snapshot->guest_draws_frame == metric_b.guest_draws_frame &&
            snapshot->guest_draws_total == metric_b.guest_draws_total);
}

static int test_f1_edges_toggle_once(void)
{
    CHECK(rsx_overlay_visible() == 0);
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_F1, 1, 0) == 1);
    CHECK(rsx_overlay_visible() == 1);
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_F1, 1, 0) == 1);
    CHECK(rsx_overlay_visible() == 1);
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_F1, 0, 0) == 1);
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_F1, 1, 0) == 1);
    CHECK(rsx_overlay_visible() == 0);
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_F1, 0, 0) == 1);
    return 1;
}

static int test_pressure_obeys_overlay_capture(void)
{
    CHECK(cellPad_pressure_after_capture(CELL_PAD_CTRL_CROSS, 0,
          CELL_PAD_CTRL_CROSS, 191, 1) == 0);
    CHECK(cellPad_pressure_after_capture(CELL_PAD_CTRL_CROSS,
          CELL_PAD_CTRL_CROSS, CELL_PAD_CTRL_CROSS, 191, 1) == 191);
    CHECK(cellPad_pressure_after_capture(0, CELL_PAD_CTRL_CROSS,
          CELL_PAD_CTRL_CROSS, 0, 1) == 255);
    return 1;
}

static int test_focus_lost_releases_imgui_keys(void)
{
    if (!rsx_overlay_visible()) {
        CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_F1, 1, 0));
        CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_F1, 0, 0));
    }
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_UP, 1, 0) == 1);
    rsx_overlay_build_draw_data();
    CHECK(rsx_overlay_test_input_down(0x26));
    { uint16_t buttons = CELL_PAD_CTRL_UP;
      rsx_overlay_handle_pad(buttons, &buttons); }
    rsx_overlay_build_draw_data();
    CHECK(rsx_overlay_test_input_down(CELL_PAD_CTRL_UP));
    rsx_overlay_focus_lost();
    rsx_overlay_build_draw_data();
    CHECK(!rsx_overlay_test_input_down(0x26));
    CHECK(!rsx_overlay_test_input_down(CELL_PAD_CTRL_UP));
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_F1, 1, 0));
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_F1, 0, 0));
    return 1;
}

static int test_toggle_key_and_visibility_persistence(void)
{
    char path[] = "/tmp/rsx-overlay-settings-XXXXXX";
    int fd = mkstemp(path);
    rsx_overlay_settings settings;
    rsx_overlay_settings loaded;
    CHECK(fd >= 0);
    close(fd);
    rsx_overlay_settings_defaults(&settings);
    CHECK(settings.toggle_key == RSX_OVERLAY_TOGGLE_KEY_F1);
    settings.toggle_key = RSX_OVERLAY_TOGGLE_KEY_F2;
    settings.overlay_visible = 0;
    CHECK(rsx_overlay_settings_save(&settings, path));
    rsx_overlay_shutdown();
    CHECK(rsx_overlay_init(path));
    CHECK(rsx_overlay_visible() == 0);
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_F2, 1, 0));
    CHECK(rsx_overlay_visible() == 1);
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_F2, 1, 0));
    CHECK(rsx_overlay_visible() == 1);
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_F2, 0, 0));
    /* Killed/closed with the menu open: nothing makes the next launch open. */
    rsx_overlay_shutdown();
    CHECK(rsx_overlay_settings_load(&loaded, path));
    CHECK(loaded.overlay_visible == 0);
    CHECK(loaded.toggle_key == RSX_OVERLAY_TOGGLE_KEY_F2);

    /* A file that says visible (older build, or written elsewhere) still
     * starts closed; its other settings are kept. */
    settings.overlay_visible = 1;
    CHECK(rsx_overlay_settings_save(&settings, path));
    CHECK(rsx_overlay_init(path));
    CHECK(rsx_overlay_visible() == 0);
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_F2, 1, 0));
    CHECK(rsx_overlay_visible() == 1);
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_F2, 0, 0));
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_F2, 1, 0));
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_F2, 0, 0));
    CHECK(rsx_overlay_visible() == 0);
    rsx_overlay_shutdown();
    unlink(path);
    return 1;
}

static int test_backend_metrics_reset_after_reinit(void)
{
    char path[] = "/tmp/rsx-overlay-metrics-XXXXXX";
    int fd = mkstemp(path);
    rsx_overlay_settings settings;
    rsx_overlay_snapshot snapshot;
    rsx_overlay_backend_metrics metrics = { 71.0, 14.0, 77, 7700 };
    CHECK(fd >= 0);
    close(fd);
    rsx_overlay_settings_defaults(&settings);
    CHECK(rsx_overlay_settings_save(&settings, path));
    rsx_overlay_shutdown();
    CHECK(rsx_overlay_init(path));
    rsx_overlay_publish_backend_metrics(&metrics);
    rsx_overlay_snapshot_current(&snapshot);
    CHECK(snapshot.fps == 71.0 && snapshot.guest_draws_total == 7700);
    rsx_overlay_shutdown();
    CHECK(rsx_overlay_init(path));
    rsx_overlay_build_draw_data();
    rsx_overlay_snapshot_current(&snapshot);
    CHECK(!rsx_overlay_test_backend_metrics_available());
    CHECK(snapshot.fps == 0.0);
    CHECK(snapshot.frame_ms == 0.0);
    CHECK(snapshot.guest_draws_frame == 0);
    CHECK(snapshot.guest_draws_total == 0);
    rsx_overlay_shutdown();
    unlink(path);
    return 1;
}

static atomic_int reentrant_calls;
static void replacement_provider(rsx_overlay_snapshot* out, void* userdata)
{
    (void)userdata;
    out->shader_stats_available = 1;
    out->shader_hits = 0xBBBB;
}

static void reentrant_provider(rsx_overlay_snapshot* out, void* userdata)
{
    (void)userdata;
    if (atomic_fetch_add(&reentrant_calls, 1) == 0)
        rsx_overlay_set_game_provider(replacement_provider, NULL);
    out->shader_stats_available = 1;
    out->shader_hits = 0xAAAA;
}

typedef struct provider_call_result {
    atomic_int done;
    rsx_overlay_snapshot snapshot;
} provider_call_result;

static void* reentrant_snapshot_thread(void* opaque)
{
    provider_call_result* result = (provider_call_result*)opaque;
    rsx_overlay_snapshot_current(&result->snapshot);
    atomic_store(&result->done, 1);
    return NULL;
}

static int test_provider_registration_from_callback_is_rejected(void)
{
    provider_call_result result = {};
    pthread_t thread;
    unsigned waits = 0;
    rsx_overlay_set_game_provider(reentrant_provider, NULL);
    CHECK(pthread_create(&thread, NULL, reentrant_snapshot_thread, &result) == 0);
    while (!atomic_load(&result.done) && waits++ < 100)
        usleep(5000);
    CHECK(atomic_load(&result.done));
    CHECK(pthread_join(thread, NULL) == 0);
    CHECK(result.snapshot.shader_hits == 0xAAAA);
    rsx_overlay_snapshot_current(&result.snapshot);
    CHECK(result.snapshot.shader_hits == 0xAAAA);
    rsx_overlay_set_game_provider(NULL, NULL);
    return 1;
}

#define CHORD (CELL_PAD_CTRL_SELECT | CELL_PAD_CTRL_START)

/* Controller toggle is the Select+Start chord. A lone Start (GoW2 pause) or
 * Select reaches the guest untouched while the menu is closed. Ends with the
 * menu open (the next test relies on it). */
static int test_start_edges_and_focus_reset(void)
{
    uint16_t guest_buttons;

    CHECK(rsx_overlay_visible() == 0);
    /* Lone Start, several held frames: no toggle, START reaches the guest. */
    for (int frame = 0; frame < 3; ++frame) {
        guest_buttons = CELL_PAD_CTRL_START;
        CHECK(rsx_overlay_handle_pad(CELL_PAD_CTRL_START, &guest_buttons) == 0);
        CHECK(guest_buttons == CELL_PAD_CTRL_START);
        CHECK(rsx_overlay_visible() == 0);
    }
    guest_buttons = 0;
    CHECK(rsx_overlay_handle_pad(0, &guest_buttons) == 0);
    /* Lone Select likewise. */
    guest_buttons = CELL_PAD_CTRL_SELECT;
    CHECK(rsx_overlay_handle_pad(CELL_PAD_CTRL_SELECT, &guest_buttons) == 0);
    CHECK(guest_buttons == CELL_PAD_CTRL_SELECT);
    CHECK(rsx_overlay_visible() == 0);
    guest_buttons = 0;
    CHECK(rsx_overlay_handle_pad(0, &guest_buttons) == 0);

    /* Chord rising edge: exactly one toggle, both buttons withheld. */
    guest_buttons = CHORD | CELL_PAD_CTRL_CROSS;
    CHECK(rsx_overlay_handle_pad(CHORD | CELL_PAD_CTRL_CROSS, &guest_buttons) == 1);
    CHECK(rsx_overlay_visible() == 1);
    CHECK((guest_buttons & CHORD) == 0);
    /* Held chord: no repeat toggle, still withheld. */
    for (int frame = 0; frame < 3; ++frame) {
        guest_buttons = CHORD;
        CHECK(rsx_overlay_handle_pad(CHORD, &guest_buttons) == 1);
        CHECK(rsx_overlay_visible() == 1);
        CHECK((guest_buttons & CHORD) == 0);
    }
    /* Releasing Select first must not leak a lone Start. */
    guest_buttons = CELL_PAD_CTRL_START;
    CHECK(rsx_overlay_handle_pad(CELL_PAD_CTRL_START, &guest_buttons) == 1);
    CHECK((guest_buttons & CHORD) == 0);
    CHECK(rsx_overlay_visible() == 1);
    guest_buttons = 0;
    CHECK(rsx_overlay_handle_pad(0, &guest_buttons) == 0);

    /* Chord while open closes it; the closing press is not gameplay input. */
    guest_buttons = CHORD | CELL_PAD_CTRL_CROSS;
    CHECK(rsx_overlay_handle_pad(CHORD | CELL_PAD_CTRL_CROSS, &guest_buttons) == 1);
    CHECK(rsx_overlay_visible() == 0);
    CHECK(guest_buttons == 0);
    /* Still held after closing: withheld, no re-toggle. */
    guest_buttons = CHORD;
    CHECK(rsx_overlay_handle_pad(CHORD, &guest_buttons) == 1);
    CHECK(guest_buttons == 0);
    CHECK(rsx_overlay_visible() == 0);
    guest_buttons = 0;
    CHECK(rsx_overlay_handle_pad(0, &guest_buttons) == 0);

    /* Sequential Start then Select: Start reaches the guest while alone, the
     * chord completes and toggles exactly once. */
    guest_buttons = CELL_PAD_CTRL_START;
    CHECK(rsx_overlay_handle_pad(CELL_PAD_CTRL_START, &guest_buttons) == 0);
    CHECK(guest_buttons == CELL_PAD_CTRL_START);
    CHECK(rsx_overlay_visible() == 0);
    guest_buttons = CHORD;
    CHECK(rsx_overlay_handle_pad(CHORD, &guest_buttons) == 1);
    CHECK(rsx_overlay_visible() == 1);
    CHECK((guest_buttons & CHORD) == 0);
    guest_buttons = CHORD;
    CHECK(rsx_overlay_handle_pad(CHORD, &guest_buttons) == 1);
    CHECK(rsx_overlay_visible() == 1);

    /* Focus loss mid-chord resets: the still-held chord counts as a new edge. */
    rsx_overlay_focus_lost();
    guest_buttons = CHORD;
    CHECK(rsx_overlay_handle_pad(CHORD, &guest_buttons) == 1);
    CHECK(rsx_overlay_visible() == 0);
    CHECK(guest_buttons == 0);
    guest_buttons = 0;
    CHECK(rsx_overlay_handle_pad(0, &guest_buttons) == 0);
    /* Lone Start after all that is plain guest input again. */
    guest_buttons = CELL_PAD_CTRL_START;
    CHECK(rsx_overlay_handle_pad(CELL_PAD_CTRL_START, &guest_buttons) == 0);
    CHECK(guest_buttons == CELL_PAD_CTRL_START);
    guest_buttons = 0;
    CHECK(rsx_overlay_handle_pad(0, &guest_buttons) == 0);

    /* Reopen for the next test. */
    guest_buttons = CHORD;
    CHECK(rsx_overlay_handle_pad(CHORD, &guest_buttons) == 1);
    CHECK(rsx_overlay_visible() == 1);
    guest_buttons = 0;
    CHECK(rsx_overlay_handle_pad(0, &guest_buttons) == 0);
    rsx_overlay_focus_lost();
    return 1;
}

static int test_capture_is_limited_to_open_menu(void)
{
    uint16_t guest_buttons = CELL_PAD_CTRL_CROSS | CELL_PAD_CTRL_UP;

    CHECK(rsx_overlay_visible() == 1);
    CHECK(rsx_overlay_handle_pad(guest_buttons, &guest_buttons) == 1);
    CHECK(guest_buttons == 0);
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_F1, 1, 0) == 1);
    CHECK(rsx_overlay_visible() == 0);
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_F1, 0, 0) == 1);
    guest_buttons = CELL_PAD_CTRL_CROSS | CELL_PAD_CTRL_UP;
    CHECK(rsx_overlay_handle_pad(guest_buttons, &guest_buttons) == 0);
    CHECK(guest_buttons == (CELL_PAD_CTRL_CROSS | CELL_PAD_CTRL_UP));
    return 1;
}

static int test_backend_and_game_metrics_merge(void)
{
    rsx_overlay_backend_metrics backend = { 59.5, 16.8, 43, 1001 };
    rsx_overlay_snapshot snapshot;
    int calls = 0;

    rsx_overlay_publish_backend_metrics(&backend);
    rsx_overlay_set_game_provider(game_provider, &calls);
    memset(&snapshot, 0, sizeof(snapshot));
    rsx_overlay_snapshot_current(&snapshot);
    CHECK(calls == 1);
    CHECK(snapshot.fps == 59.5);
    CHECK(snapshot.frame_ms == 16.8);
    CHECK(snapshot.guest_draws_frame == 43);
    CHECK(snapshot.guest_draws_total == 1001);
    CHECK(snapshot.wad_state_available == 1);
    CHECK(strcmp(snapshot.wad_state, "loading R_PermA") == 0);
    CHECK(snapshot.shader_stats_available == 1);
    CHECK(snapshot.shader_hits == 9);
    CHECK(snapshot.shader_misses == 3);
    CHECK(snapshot.shader_compiles == 2);
    CHECK(snapshot.shader_failures == 1);
    rsx_overlay_set_game_provider(0, 0);
    return 1;
}

static int test_coherent_metric_publication(void)
{
    pthread_t writer;
    rsx_overlay_snapshot snapshot;
    unsigned i;
    rsx_overlay_publish_backend_metrics(&metric_a);
    CHECK(pthread_create(&writer, NULL, metric_writer_thread, NULL) == 0);
    for (i = 0; i < 40000; ++i) {
        rsx_overlay_snapshot_current(&snapshot);
        CHECK(metrics_are_known(&snapshot));
    }
    CHECK(pthread_join(writer, NULL) == 0);
    return 1;
}

static int test_provider_pair_and_lifetime(void)
{
    pthread_t snapshot_thread;
    rsx_overlay_snapshot snapshot;
    atomic_int entered = 0;
    provider_lifetime* old_data = (provider_lifetime*)malloc(sizeof(*old_data));
    provider_lifetime replacement = { 0x72, &entered };
    CHECK(old_data != NULL);
    old_data->cookie = 0x71;
    old_data->entered = &entered;
    rsx_overlay_set_game_provider(lifetime_provider, old_data);
    CHECK(pthread_create(&snapshot_thread, NULL, provider_snapshot_thread, &snapshot) == 0);
    while (!atomic_load(&entered)) usleep(100);
    rsx_overlay_set_game_provider(lifetime_provider, &replacement);
    free(old_data); /* Setter guarantees the old callback has completed. */
    CHECK(pthread_join(snapshot_thread, NULL) == 0);
    CHECK(snapshot.shader_stats_available == 1);
    CHECK(snapshot.shader_hits == 0x71);
    rsx_overlay_set_game_provider(NULL, NULL);
    return 1;
}

static int test_draw_packet_is_a_bounded_copy(void)
{
    rsx_overlay_draw_packet first;
    rsx_overlay_draw_packet second;

    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_F1, 1, 0) == 1);
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_F1, 0, 0) == 1);
    CHECK(rsx_overlay_visible() == 1);
    tick(0.02);
    rsx_overlay_build_draw_data();
    settle();
    CHECK(rsx_overlay_copy_draw_packet(&first) == 1);
    CHECK(first.vertex_count > 0);
    CHECK(first.vertex_count <= RSX_OVERLAY_DRAW_VERTEX_CAPACITY);
    CHECK(first.index_count <= RSX_OVERLAY_DRAW_INDEX_CAPACITY);
    CHECK(first.command_count <= RSX_OVERLAY_DRAW_COMMAND_CAPACITY);
    rsx_overlay_build_draw_data();
    CHECK(rsx_overlay_copy_draw_packet(&second) == 1);
    CHECK(second.generation > first.generation);
    CHECK(first.vertex_count > 0);
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_F1, 1, 0) == 1);
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_F1, 0, 0) == 1);
    CHECK(rsx_overlay_visible() == 0);
    return 1;
}

static int test_host_mapping_helpers(void)
{
    uint16_t map[CELL_PAD_HOST_BUTTON_COUNT];
    unsigned i;

    for (i = 0; i < CELL_PAD_HOST_BUTTON_COUNT; ++i)
        map[i] = (uint16_t)(1u << i);
    map[14] = CELL_PAD_CTRL_CIRCLE;
    map[13] = CELL_PAD_CTRL_CROSS;
    CHECK(cellPad_translate_host_buttons(CELL_PAD_CTRL_CROSS | CELL_PAD_CTRL_CIRCLE, map)
          == (CELL_PAD_CTRL_CROSS | CELL_PAD_CTRL_CIRCLE));
    CHECK(cellPad_translate_host_buttons(CELL_PAD_CTRL_CROSS, map) == CELL_PAD_CTRL_CIRCLE);
    CHECK(cellPad_apply_deadzone_axis(146, 15) == 128);
    CHECK(cellPad_apply_deadzone_axis(148, 15) > 128);
    CHECK(cellPad_apply_deadzone_axis(108, 15) < 128);
    CHECK(cellPad_apply_deadzone_axis(0, 15) == 0);
    CHECK(cellPad_apply_deadzone_axis(255, 15) == 255);
    return 1;
}

static int init_with_default_settings(char* path)
{
    int fd = mkstemp(path);
    rsx_overlay_settings settings;

    if (fd < 0)
        return 0;
    close(fd);
    rsx_overlay_settings_defaults(&settings);
    if (!rsx_overlay_settings_save(&settings, path))
        return 0;
    rsx_overlay_shutdown();
    return rsx_overlay_init(path);
}

static int test_font_atlas_is_an_owned_copy(void)
{
    char path[] = "/tmp/rsx-overlay-font-XXXXXX";
    uint32_t width = 0;
    uint32_t height = 0;
    uint8_t* pixels;
    size_t size;
    size_t i;
    int any_alpha = 0;

    CHECK(init_with_default_settings(path));
    /* Size query without a destination. */
    CHECK(rsx_overlay_copy_font_atlas(NULL, 0, &width, &height) == 1);
    CHECK(width > 0 && height > 0);
    size = (size_t)width * height * 4u;
    pixels = (uint8_t*)calloc(1, size);
    CHECK(pixels != NULL);
    /* A destination that is too small is refused, not truncated. */
    CHECK(rsx_overlay_copy_font_atlas(pixels, size - 1u, &width, &height) == 0);
    CHECK(rsx_overlay_copy_font_atlas(pixels, size, &width, &height) == 1);
    for (i = 3; i < size; i += 4)
        any_alpha |= pixels[i] != 0;
    free(pixels);
    CHECK(any_alpha);
    rsx_overlay_shutdown();
    unlink(path);
    /* After shutdown nothing stale is handed to a renderer. */
    CHECK(rsx_overlay_copy_font_atlas(NULL, 0, &width, &height) == 0);
    return 1;
}

static int test_backend_capabilities_gate_graphics_settings(void)
{
    char path[] = "/tmp/rsx-overlay-graphics-XXXXXX";
    rsx_overlay_backend_capabilities caps;
    rsx_overlay_backend_capabilities read_back;
    rsx_overlay_graphics_settings graphics;
    rsx_overlay_graphics_settings request;

    CHECK(init_with_default_settings(path));
    rsx_overlay_set_backend_capabilities(NULL);
    CHECK(rsx_overlay_get_backend_capabilities(&read_back) == 0);
    CHECK(rsx_overlay_get_graphics_settings(&graphics) == 1);
    CHECK(graphics.window_width == 1280u && graphics.window_height == 720u);
    CHECK(graphics.fullscreen == 0 && graphics.vsync == 1);

    /* No capability registered: nothing may change. */
    request = graphics;
    request.vsync = 0;
    request.window_width = 1920u;
    request.window_height = 1080u;
    CHECK(rsx_overlay_test_apply_graphics(&request) == 0);
    CHECK(rsx_overlay_get_graphics_settings(&graphics) == 1);
    CHECK(graphics.vsync == 1 && graphics.window_width == 1280u);

    memset(&caps, 0, sizeof(caps));
    caps.window_size = RSX_OVERLAY_SETTING_RESTART_REQUIRED;
    caps.fullscreen = RSX_OVERLAY_SETTING_UNAVAILABLE;
    caps.vsync = RSX_OVERLAY_SETTING_LIVE;
    rsx_overlay_set_backend_capabilities(&caps);
    CHECK(rsx_overlay_get_backend_capabilities(&read_back) == 1);
    CHECK(read_back.window_size == RSX_OVERLAY_SETTING_RESTART_REQUIRED);
    CHECK(read_back.fullscreen == RSX_OVERLAY_SETTING_UNAVAILABLE);
    CHECK(read_back.vsync == RSX_OVERLAY_SETTING_LIVE);

    /* Supported fields change; the unavailable one is left untouched. */
    request.fullscreen = 1;
    CHECK(rsx_overlay_test_apply_graphics(&request) == 1);
    CHECK(rsx_overlay_get_graphics_settings(&graphics) == 1);
    CHECK(graphics.vsync == 0);
    CHECK(graphics.window_width == 1920u && graphics.window_height == 1080u);
    CHECK(graphics.fullscreen == 0);

    /* Out-of-range values fall back to the validated defaults. */
    request.window_width = 5u;
    CHECK(rsx_overlay_test_apply_graphics(&request) == 1);
    CHECK(rsx_overlay_get_graphics_settings(&graphics) == 1);
    CHECK(graphics.window_width == 1280u);

    request.window_width = 1280u;
    request.window_height = 720u;
    request.vsync = 1;
    CHECK(rsx_overlay_test_apply_graphics(&request) == 1);
    rsx_overlay_set_backend_capabilities(NULL);
    CHECK(rsx_overlay_get_backend_capabilities(&read_back) == 0);
    rsx_overlay_shutdown();
    unlink(path);
    CHECK(rsx_overlay_get_graphics_settings(&graphics) == 0);
    return 1;
}

/* Settings reach disk when they change (the game is normally killed or leaves
 * through _exit, so shutdown is not a save point), and never on an idle call. */
static int test_settings_saved_on_change_not_per_frame(void)
{
    char path[] = "/tmp/rsx-overlay-persist-XXXXXX";
    rsx_overlay_settings loaded;
    rsx_overlay_backend_capabilities caps;
    rsx_overlay_graphics_settings graphics;
    struct stat info;

    CHECK(init_with_default_settings(path));
    CHECK(rsx_overlay_persist_settings() == 0);

    /* Opening/closing the menu is not a setting: no write. */
    CHECK(unlink(path) == 0);
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_F1, 1, 0));
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_F1, 0, 0));
    CHECK(rsx_overlay_visible() == 1);
    CHECK(rsx_overlay_persist_settings() == 0);
    CHECK(stat(path, &info) != 0);

    /* A graphics change made through the menu path is saved without any
     * shutdown, then idle calls never touch the disk again. */
    memset(&caps, 0, sizeof(caps));
    caps.vsync = RSX_OVERLAY_SETTING_LIVE;
    caps.fullscreen = RSX_OVERLAY_SETTING_LIVE;
    rsx_overlay_set_backend_capabilities(&caps);
    CHECK(rsx_overlay_get_graphics_settings(&graphics));
    graphics.vsync = 0;
    CHECK(rsx_overlay_test_apply_graphics(&graphics) == 1);
    CHECK(rsx_overlay_persist_settings() == 1);
    CHECK(rsx_overlay_settings_load(&loaded, path));
    CHECK(loaded.vsync == 0);
    CHECK(loaded.overlay_visible == 0);
    CHECK(unlink(path) == 0);
    CHECK(rsx_overlay_persist_settings() == 0);
    CHECK(rsx_overlay_persist_settings() == 0);
    CHECK(stat(path, &info) != 0);

    /* The menu frame itself saves a pending change. */
    graphics.fullscreen = 1;
    CHECK(rsx_overlay_test_apply_graphics(&graphics) == 1);
    rsx_overlay_build_draw_data();
    CHECK(rsx_overlay_settings_load(&loaded, path));
    CHECK(loaded.fullscreen == 1 && loaded.vsync == 0);
    CHECK(rsx_overlay_persist_settings() == 0);
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_F1, 1, 0));
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_F1, 0, 0));
    CHECK(rsx_overlay_visible() == 0);

    /* Reload as a new process would, without a shutdown save in between. */
    rsx_overlay_set_backend_capabilities(NULL);
    CHECK(unlink(path) == 0);
    rsx_overlay_shutdown();
    CHECK(stat(path, &info) != 0);
    CHECK(rsx_overlay_settings_save(&loaded, path));
    CHECK(rsx_overlay_init(path));
    CHECK(rsx_overlay_get_graphics_settings(&graphics));
    CHECK(graphics.vsync == 0 && graphics.fullscreen == 1);
    CHECK(rsx_overlay_visible() == 0);
    rsx_overlay_shutdown();
    unlink(path);
    return 1;
}

/* An unwritable path fails once and is not retried on every idle call. */
static int test_failed_save_is_not_retried_per_frame(void)
{
    rsx_overlay_backend_capabilities caps;
    rsx_overlay_graphics_settings graphics;

    rsx_overlay_shutdown();
    CHECK(rsx_overlay_init("/dev/null/rsx-overlay/settings"));
    memset(&caps, 0, sizeof(caps));
    caps.vsync = RSX_OVERLAY_SETTING_LIVE;
    rsx_overlay_set_backend_capabilities(&caps);
    CHECK(rsx_overlay_get_graphics_settings(&graphics));
    graphics.vsync = 0;
    CHECK(rsx_overlay_test_apply_graphics(&graphics) == 1);
    CHECK(rsx_overlay_persist_settings() == -1);
    CHECK(rsx_overlay_persist_settings() == 0);
    rsx_overlay_set_backend_capabilities(NULL);
    rsx_overlay_shutdown();
    return 1;
}

static int test_shader_stats_and_missing_provider(void)
{
    char path[] = "/tmp/rsx-overlay-shaders-XXXXXX";
    rsx_overlay_shader_stats stats = { 10, 4, 3, 1 };
    rsx_overlay_shader_stats read_back;
    rsx_overlay_snapshot snapshot;

    CHECK(init_with_default_settings(path));
    memset(&read_back, 0xA5, sizeof(read_back));
    CHECK(rsx_overlay_read_shader_stats(&read_back) == 0);
    rsx_overlay_publish_shader_stats(&stats);
    CHECK(rsx_overlay_read_shader_stats(&read_back) == 1);
    CHECK(read_back.hits == 10 && read_back.misses == 4);
    CHECK(read_back.compiles == 3 && read_back.failures == 1);
    CHECK(rsx_overlay_read_shader_stats(NULL) == 0);

    /* No game provider: game fields stay unavailable, never zero-as-measured. */
    rsx_overlay_set_game_provider(NULL, NULL);
    memset(&snapshot, 0x5A, sizeof(snapshot));
    rsx_overlay_snapshot_current(&snapshot);
    CHECK(snapshot.wad_state_available == 0);
    CHECK(snapshot.wad_state[0] == '\0');
    CHECK(snapshot.shader_stats_available == 0);

    /* The open menu renders the unavailable state safely. */
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_F1, 1, 0));
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_F1, 0, 0));
    rsx_overlay_build_draw_data();
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_F1, 1, 0));
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_F1, 0, 0));
    rsx_overlay_build_draw_data();

    /* A new session starts with nothing published. */
    rsx_overlay_shutdown();
    CHECK(rsx_overlay_init(path));
    CHECK(rsx_overlay_read_shader_stats(&read_back) == 0);
    rsx_overlay_shutdown();
    unlink(path);
    return 1;
}

static int test_settings_path_from_env(void)
{
    CHECK(setenv("PS3_OVERLAY_SETTINGS", "/tmp/overlay-from-launcher.settings", 1) == 0);
    CHECK(rsx_overlay_settings_path_from_env() != NULL);
    CHECK(strcmp(rsx_overlay_settings_path_from_env(),
                 "/tmp/overlay-from-launcher.settings") == 0);
    CHECK(setenv("PS3_OVERLAY_SETTINGS", "", 1) == 0);
    CHECK(rsx_overlay_settings_path_from_env() == NULL);
    CHECK(unsetenv("PS3_OVERLAY_SETTINGS") == 0);
    CHECK(rsx_overlay_settings_path_from_env() == NULL);
    return 1;
}

/* Fresh core on its own settings file (menu closed). */
static int fresh_init(char* path, size_t size)
{
    rsx_overlay_settings settings;
    int fd;

    rsx_overlay_shutdown();
    snprintf(path, size, "/tmp/rsx-overlay-fix-XXXXXX");
    fd = mkstemp(path);
    if (fd < 0)
        return 0;
    close(fd);
    rsx_overlay_settings_defaults(&settings);
    if (!rsx_overlay_settings_save(&settings, path))
        return 0;
    return rsx_overlay_init(path) && !rsx_overlay_visible();
}

static int open_menu(void)
{
    if (!rsx_overlay_handle_key(RSX_OVERLAY_KEY_F1, 1, 0) ||
        !rsx_overlay_handle_key(RSX_OVERLAY_KEY_F1, 0, 0))
        return 0;
    tick(0.02);
    rsx_overlay_build_draw_data();
    settle();
    return rsx_overlay_visible();
}

static int test_packet_carries_display_size(void)
{
    char path[64];
    uint32_t w, h;
    float scale;
    rsx_overlay_draw_packet* packet = (rsx_overlay_draw_packet*)calloc(1, sizeof(*packet));

    CHECK(packet != NULL);
    CHECK(fresh_init(path, sizeof path));
    rsx_overlay_set_display(2560u, 1440u, 2.0f);
    CHECK(open_menu());
    rsx_overlay_build_draw_data();
    CHECK(rsx_overlay_copy_draw_packet(packet) == 1);
    CHECK(packet->display_width == 2560u && packet->display_height == 1440u);
    CHECK(packet->backing_scale == 2.0f);
    for (uint32_t i = 0; i < packet->command_count; ++i)
        CHECK(packet->commands[i].clip_rect[2] <= 2560.0f &&
              packet->commands[i].clip_rect[3] <= 1440.0f);
    /* A minimized window reports 0x0; bad scales are ignored too. */
    rsx_overlay_set_display(0u, 0u, 2.0f);
    rsx_overlay_set_display(800u, 600u, 0.0f);
    rsx_overlay_get_display(&w, &h, &scale);
    CHECK(w == 2560u && h == 1440u && scale == 2.0f);
    rsx_overlay_set_display(1280u, 720u, 1.0f);
    rsx_overlay_shutdown();
    unlink(path);
    free(packet);
    return 1;
}

static void tap_key(int key)
{
    rsx_overlay_handle_key(key, 1, 0);
    tick(0.02);
    rsx_overlay_build_draw_data();
    rsx_overlay_handle_key(key, 0, 0);
    tick(0.02);
    rsx_overlay_build_draw_data();
}

/* Escape closes the menu when no popup is open, and the whole press
 * (auto-repeat included) stays with the overlay so the host's own Escape
 * (close window / leave fullscreen) never fires from it. */
static int test_escape_closes_menu(void)
{
    char path[64];

    CHECK(fresh_init(path, sizeof path));
    CHECK(open_menu());
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_ESCAPE, 1, 0) == 1);
    CHECK(!rsx_overlay_visible());
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_ESCAPE, 1, 0) == 1); /* repeat */
    CHECK(!rsx_overlay_visible());
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_ESCAPE, 0, 0) == 1);
    /* Menu closed, next Escape press belongs to the host again. */
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_ESCAPE, 1, 0) == 0);
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_ESCAPE, 0, 0) == 0);
    CHECK(!rsx_overlay_visible());
    rsx_overlay_shutdown();
    unlink(path);
    return 1;
}

static int test_arrow_keys_move_card_focus(void)
{
    char path[64];
    int phase, level, card, row, index;

    CHECK(fresh_init(path, sizeof path));
    CHECK(open_menu());
    rsx_overlay_test_shell_state(&phase, &level, &card, &row, &index);
    CHECK(phase == RSX_SHELL_OPEN && level == RSX_SHELL_LEVEL_CARDS && card == RSX_SHELL_CARD_RESUME);
    tap_key(RSX_OVERLAY_KEY_RIGHT);
    tap_key(RSX_OVERLAY_KEY_RIGHT);
    rsx_overlay_test_shell_state(&phase, &level, &card, &row, &index);
    CHECK(card == RSX_SHELL_CARD_CONTROLS);
    tap_key(RSX_OVERLAY_KEY_LEFT);
    rsx_overlay_test_shell_state(&phase, &level, &card, &row, &index);
    CHECK(card == RSX_SHELL_CARD_DISPLAY);
    tap_key(RSX_OVERLAY_KEY_RIGHT);
    tap_key(RSX_OVERLAY_KEY_DOWN);
    rsx_overlay_test_shell_state(&phase, &level, &card, &row, &index);
    CHECK(level == RSX_SHELL_LEVEL_PANEL && row == 0);
    rsx_overlay_shutdown();
    unlink(path);
    return 1;
}

/* Esc backs out one level; at card level a fresh press closes, and the whole
 * press (auto-repeat included) stays with the overlay. */
static int test_escape_backs_out_one_level(void)
{
    char path[64];
    int phase, level, card, row, index;

    CHECK(fresh_init(path, sizeof path));
    CHECK(open_menu());
    tap_key(RSX_OVERLAY_KEY_RIGHT);
    tap_key(RSX_OVERLAY_KEY_RIGHT);
    tap_key(RSX_OVERLAY_KEY_ENTER);
    rsx_overlay_test_shell_state(&phase, &level, &card, &row, &index);
    CHECK(level == RSX_SHELL_LEVEL_PANEL);
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_ESCAPE, 1, 0) == 1);
    tick(0.02);
    rsx_overlay_build_draw_data();
    rsx_overlay_test_shell_state(&phase, &level, &card, &row, &index);
    CHECK(level == RSX_SHELL_LEVEL_CARDS && rsx_overlay_visible());
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_ESCAPE, 1, 0) == 1); /* auto-repeat */
    tick(0.02);
    rsx_overlay_build_draw_data();
    CHECK(rsx_overlay_visible());
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_ESCAPE, 0, 0) == 1);
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_ESCAPE, 1, 0) == 1);
    CHECK(!rsx_overlay_visible());
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_ESCAPE, 1, 0) == 1);
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_ESCAPE, 0, 0) == 1);
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_ESCAPE, 1, 0) == 0);
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_ESCAPE, 0, 0) == 0);
    rsx_overlay_shutdown();
    unlink(path);
    return 1;
}

static int test_closed_shell_costs_nothing(void)
{
    char path[64];
    uint16_t guest;
    rsx_overlay_draw_packet* packet = (rsx_overlay_draw_packet*)calloc(1, sizeof(*packet));

    CHECK(packet != NULL);
    CHECK(fresh_init(path, sizeof path));
    CHECK(!rsx_overlay_render_active());
    /* Display size is published on demand (init, resize, backing-scale
     * change, each open), never polled by a pump while the shell is closed. */
    CHECK(rsx_overlay_display_stale());
    rsx_overlay_set_display(1280u, 720u, 1.0f);
    CHECK(!rsx_overlay_display_stale());
    CHECK(open_menu());
    CHECK(rsx_overlay_render_active());
    CHECK(rsx_overlay_display_stale());
    rsx_overlay_set_display(1280u, 720u, 1.0f);
    CHECK(!rsx_overlay_display_stale());
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_F1, 1, 0));
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_F1, 0, 0));
    CHECK(!rsx_overlay_visible());
    /* The close animation still renders; input already belongs to the game. */
    CHECK(rsx_overlay_render_active());
    guest = CELL_PAD_CTRL_CROSS;
    CHECK(rsx_overlay_handle_pad(CELL_PAD_CTRL_CROSS, &guest) == 0 && guest == CELL_PAD_CTRL_CROSS);
    /* F1 again mid-animation reopens; F1 once more closes. */
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_F1, 1, 0));
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_F1, 0, 0));
    tick(0.02);
    rsx_overlay_build_draw_data();
    CHECK(rsx_overlay_visible() && rsx_overlay_render_active());
    CHECK(rsx_overlay_display_stale());                /* every open asks once */
    rsx_overlay_set_display(1280u, 720u, 1.0f);
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_F1, 1, 0));
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_F1, 0, 0));
    tick(0.02);
    rsx_overlay_build_draw_data();
    settle();
    CHECK(!rsx_overlay_render_active());
    rsx_overlay_build_draw_data();
    CHECK(rsx_overlay_copy_draw_packet(packet) == 1);
    CHECK(packet->vertex_count == 0 && packet->open_progress == 0.0f);
    CHECK(!rsx_overlay_display_stale());
    rsx_overlay_invalidate_display();                  /* SIZE/DISPLAY_CHANGED */
    CHECK(rsx_overlay_display_stale());
    rsx_overlay_set_display(1280u, 720u, 1.0f);
    CHECK(!rsx_overlay_display_stale());
    for (uint32_t bits = 0; bits <= 0xFFFFu; ++bits) {
        if ((bits & CHORD) == CHORD)
            continue;
        guest = (uint16_t)bits;
        CHECK(rsx_overlay_handle_pad((uint16_t)bits, &guest) == 0);
        CHECK(guest == (uint16_t)bits);
    }
    rsx_overlay_shutdown();
    unlink(path);
    free(packet);
    return 1;
}

static int test_draw_budget_worst_case(void)
{
    char path[64];
    char line[RSX_OVERLAY_LOG_MESSAGE_CAPACITY];
    int phase, level, card, row, index;
    rsx_overlay_draw_packet* packet = (rsx_overlay_draw_packet*)calloc(1, sizeof(*packet));

    CHECK(packet != NULL);
    memset(line, 'W', sizeof line - 1u);
    line[sizeof line - 1u] = '\0';
    for (unsigned i = 0; i < RSX_OVERLAY_LOG_ENTRY_COUNT; ++i)
        rsx_overlay_log_write(RSX_OVERLAY_LOG_ERROR, line);
    CHECK(fresh_init(path, sizeof path));
    rsx_overlay_set_display(3840u, 2160u, 2.0f);
    CHECK(open_menu());
    for (int c = 0; c < RSX_SHELL_CARD_COUNT; ++c) {
        rsx_overlay_build_draw_data();
        CHECK(rsx_overlay_copy_draw_packet(packet) == 1);
        CHECK(packet->vertex_count > 0 && packet->vertex_count < RSX_OVERLAY_DRAW_VERTEX_CAPACITY);
        CHECK(packet->index_count < RSX_OVERLAY_DRAW_INDEX_CAPACITY);
        CHECK(packet->command_count > 0 && packet->command_count < RSX_OVERLAY_DRAW_COMMAND_CAPACITY);
        tap_key(RSX_OVERLAY_KEY_RIGHT);
    }
    tap_key(RSX_OVERLAY_KEY_LEFT);   /* Desenvolvedor */
    tap_key(RSX_OVERLAY_KEY_ENTER);  /* panel, Logs row */
    tap_key(RSX_OVERLAY_KEY_ENTER);  /* Logs list */
    rsx_overlay_test_shell_state(&phase, &level, &card, &row, &index);
    CHECK(card == RSX_SHELL_CARD_DEVELOPER && level == RSX_SHELL_LEVEL_LIST);
    rsx_overlay_build_draw_data();
    CHECK(rsx_overlay_copy_draw_packet(packet) == 1);
    CHECK(packet->vertex_count > 0 && packet->vertex_count < RSX_OVERLAY_DRAW_VERTEX_CAPACITY);
    CHECK(packet->index_count < RSX_OVERLAY_DRAW_INDEX_CAPACITY);
    rsx_overlay_set_display(1280u, 720u, 1.0f);
    rsx_overlay_shutdown();
    unlink(path);
    free(packet);
    return 1;
}

static int test_scale_change_rebuilds_atlas(void)
{
    char path[64];
    uint64_t g0;

    CHECK(fresh_init(path, sizeof path));
    rsx_overlay_set_display(1280u, 720u, 1.0f);
    g0 = rsx_overlay_font_atlas_generation();
    CHECK(g0 > 0 && rsx_overlay_test_atlas_scale() == 1.0f);
    rsx_overlay_set_display(1600u, 900u, 1.0f);
    CHECK(rsx_overlay_font_atlas_generation() == g0);   /* same scale: no work */
    rsx_overlay_set_display(2560u, 1440u, 2.0f);
    CHECK(rsx_overlay_font_atlas_generation() == g0 + 1u);  /* exactly one rebuild */
    CHECK(rsx_overlay_test_atlas_scale() == 2.0f);
    rsx_overlay_set_display(2880u, 1800u, 2.0f);
    CHECK(rsx_overlay_font_atlas_generation() == g0 + 1u);
    /* A new builder rebuilds once, at the next publication it asks for. */
    rsx_overlay_set_asset_builder(NULL);
    CHECK(rsx_overlay_display_stale());
    CHECK(rsx_overlay_font_atlas_generation() == g0 + 1u);
    rsx_overlay_set_display(2880u, 1800u, 2.0f);
    CHECK(rsx_overlay_font_atlas_generation() == g0 + 2u);
    CHECK(rsx_overlay_copy_font_atlas(NULL, 0, NULL, NULL) == 1);
    rsx_overlay_set_display(1280u, 720u, 1.0f);
    rsx_overlay_shutdown();
    unlink(path);
    return 1;
}

static int test_quit_dialog(void)
{
    char path[64];
    int phase, level, card, row, index;

    CHECK(fresh_init(path, sizeof path));
    CHECK(open_menu());
    for (int i = 0; i < 5; ++i)
        tap_key(RSX_OVERLAY_KEY_RIGHT);
    tap_key(RSX_OVERLAY_KEY_ENTER);
    rsx_overlay_test_shell_state(&phase, &level, &card, &row, &index);
    CHECK(level == RSX_SHELL_LEVEL_DIALOG);
    tap_key(RSX_OVERLAY_KEY_ENTER);                    /* default: Cancelar */
    CHECK(rsx_overlay_take_quit_request() == 0 && rsx_overlay_visible());
    tap_key(RSX_OVERLAY_KEY_ENTER);
    tap_key(RSX_OVERLAY_KEY_LEFT);                     /* Sair */
    tap_key(RSX_OVERLAY_KEY_ENTER);
    CHECK(!rsx_overlay_visible());
    CHECK(rsx_overlay_take_quit_request() == 1);
    CHECK(rsx_overlay_take_quit_request() == 0);
    rsx_overlay_shutdown();
    unlink(path);
    return 1;
}

static int s_fb_count;
static int s_fb_event[128], s_fb_sounds[128], s_fb_haptics[128];
static void record_feedback(int event, int sounds, int haptics, void* userdata)
{
    (void)userdata;
    if (s_fb_count < 128) {
        s_fb_event[s_fb_count] = event;
        s_fb_sounds[s_fb_count] = sounds;
        s_fb_haptics[s_fb_count] = haptics;
        ++s_fb_count;
    }
}

static int test_feedback_follows_ui_switches(void)
{
    char path[64];
    int before;

    CHECK(fresh_init(path, sizeof path));
    s_fb_count = 0;
    rsx_overlay_set_feedback_sink(record_feedback, NULL);
    CHECK(open_menu());
    CHECK(s_fb_count >= 1 && s_fb_event[0] == RSX_OVERLAY_FEEDBACK_OPEN);
    CHECK(s_fb_sounds[0] == 1 && s_fb_haptics[0] == 1);
    tap_key(RSX_OVERLAY_KEY_RIGHT);
    CHECK(s_fb_event[s_fb_count - 1] == RSX_OVERLAY_FEEDBACK_MOVE);
    tap_key(RSX_OVERLAY_KEY_RIGHT);
    tap_key(RSX_OVERLAY_KEY_RIGHT);                    /* Som */
    tap_key(RSX_OVERLAY_KEY_ENTER);                    /* Som do jogo */
    tap_key(RSX_OVERLAY_KEY_DOWN);                     /* Sons da interface */
    before = s_fb_count;
    tap_key(RSX_OVERLAY_KEY_ENTER);                    /* off */
    CHECK(s_fb_count > before);
    CHECK(s_fb_sounds[s_fb_count - 1] == 0 && s_fb_haptics[s_fb_count - 1] == 1);
    tap_key(RSX_OVERLAY_KEY_DOWN);
    tap_key(RSX_OVERLAY_KEY_ENTER);                    /* Vibração da interface off */
    before = s_fb_count;
    tap_key(RSX_OVERLAY_KEY_UP);
    CHECK(s_fb_count == before);                       /* both off: silent and still */
    rsx_overlay_set_feedback_sink(NULL, NULL);
    rsx_overlay_shutdown();
    unlink(path);
    return 1;
}

static int test_sound_and_vibration_reach_consumers(void)
{
    char path[64];

    CHECK(fresh_init(path, sizeof path));
    CHECK(ps3_audio_runtime_muted() == 0 && s_last_vibration == 1);
    CHECK(open_menu());
    tap_key(RSX_OVERLAY_KEY_RIGHT);
    tap_key(RSX_OVERLAY_KEY_RIGHT);
    tap_key(RSX_OVERLAY_KEY_RIGHT);                    /* Som */
    tap_key(RSX_OVERLAY_KEY_ENTER);
    tap_key(RSX_OVERLAY_KEY_ENTER);                    /* Som do jogo: off */
    CHECK(ps3_audio_runtime_muted() == 1);
    tap_key(RSX_OVERLAY_KEY_ESCAPE);
    tap_key(RSX_OVERLAY_KEY_LEFT);                     /* Controles */
    tap_key(RSX_OVERLAY_KEY_ENTER);
    tap_key(RSX_OVERLAY_KEY_DOWN);                     /* Vibração */
    tap_key(RSX_OVERLAY_KEY_ENTER);
    CHECK(s_last_vibration == 0);
    rsx_overlay_shutdown();                            /* saves */
    ps3_audio_set_runtime_mute(0);
    s_last_vibration = 1;
    CHECK(rsx_overlay_init(path));                     /* reload applies both */
    CHECK(ps3_audio_runtime_muted() == 1 && s_last_vibration == 0);
    rsx_overlay_shutdown();
    ps3_audio_set_runtime_mute(0);
    s_last_vibration = 1;
    unlink(path);
    return 1;
}

static int test_slider_saves_after_release(void)
{
    char path[64];
    rsx_overlay_settings loaded;

    CHECK(fresh_init(path, sizeof path));
    CHECK(open_menu());
    tap_key(RSX_OVERLAY_KEY_RIGHT);
    tap_key(RSX_OVERLAY_KEY_RIGHT);
    tap_key(RSX_OVERLAY_KEY_ENTER);                    /* Zona morta */
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_RIGHT, 1, 0) == 1);
    for (int i = 0; i < 12; ++i) {
        tick(0.1);
        rsx_overlay_build_draw_data();
    }
    CHECK(rsx_overlay_settings_load(&loaded, path) == 1);
    CHECK(loaded.analog_deadzone == 0);                /* nothing written while held */
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_RIGHT, 0, 0) == 1);
    tick(0.02);
    rsx_overlay_build_draw_data();
    CHECK(rsx_overlay_settings_load(&loaded, path) == 1);
    CHECK(loaded.analog_deadzone >= 8 && loaded.analog_deadzone <= 9);
    rsx_overlay_shutdown();
    unlink(path);
    return 1;
}

/* Unavailable rows (Tela with no backend capabilities registered) read as
 * disabled but legible: ash at 75 % (0xBF), not the old 55 % (0x8C). */
static int test_unavailable_rows_legible(void)
{
    char path[64];
    const uint32_t ash75 = 0xBF939FA8u, ash55 = 0x8C939FA8u;   /* IM_COL32 ABGR of #A89F93 */
    rsx_overlay_draw_packet* p = (rsx_overlay_draw_packet*)calloc(1, sizeof(*p));
    uint32_t n75 = 0, n55 = 0;

    CHECK(p != NULL);
    CHECK(fresh_init(path, sizeof path));
    CHECK(open_menu());
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_RIGHT, 1, 0) == 1);
    tick(0.02);
    rsx_overlay_build_draw_data();
    (void)rsx_overlay_handle_key(RSX_OVERLAY_KEY_RIGHT, 0, 0);
    settle();
    settle();
    CHECK(rsx_overlay_copy_draw_packet(p) == 1);
    for (uint32_t i = 0; i < p->vertex_count; ++i) {
        n75 += p->vertices[i].color == ash75;
        n55 += p->vertices[i].color == ash55;
    }
    CHECK(n75 > 0);
    CHECK(n55 == 0);
    free(p);
    rsx_overlay_shutdown();
    unlink(path);
    return 1;
}

static int test_atlas_contract(void)
{
    char path[64];
    uint32_t w = 0, h = 0;
    rsx_overlay_draw_packet* p = (rsx_overlay_draw_packet*)calloc(1, sizeof(*p));
    uint8_t* px;

    CHECK(p != NULL);
    CHECK(fresh_init(path, sizeof path));
    CHECK(open_menu());
    rsx_overlay_build_draw_data();
    CHECK(rsx_overlay_copy_draw_packet(p) == 1 && p->command_count > 0);
    CHECK(rsx_overlay_copy_font_atlas(NULL, 0, &w, &h) == 1);
    px = (uint8_t*)malloc((size_t)w * h * 4u);
    CHECK(px != NULL && rsx_overlay_copy_font_atlas(px, (size_t)w * h * 4u, &w, &h) == 1);
    for (size_t i = 0; i < (size_t)w * h; ++i)                      /* straight alpha, white RGB */
        CHECK(px[i * 4u] == 255 && px[i * 4u + 1u] == 255 && px[i * 4u + 2u] == 255);
    for (uint32_t c = 0; c < p->command_count; ++c)
        CHECK(p->commands[c].texture_id == 0);
    /* The first command is the dim rectangle: it samples the white block
     * centre, and every bilinear neighbour of that UV is opaque. */
    const rsx_overlay_draw_vertex* v0 =
        &p->vertices[p->commands[0].vertex_offset + p->indices[p->commands[0].index_offset]];
    const int tx = (int)(v0->uv[0] * (float)w), ty = (int)(v0->uv[1] * (float)h);
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
            CHECK(px[((size_t)(ty + dy) * w + (size_t)(tx + dx)) * 4u + 3u] == 255);
    /* Dim alpha follows open progress (fully open here). */
    CHECK(p->open_progress == 1.0f);
    CHECK((v0->color >> 24) == (uint32_t)(RSX_SHELL_BACKDROP_DIM * 255.0f + 0.5f));
    free(px);
    free(p);
    rsx_overlay_shutdown();
    unlink(path);
    return 1;
}

/* A key held when the menu closes (its key-up never reaches ImGui) must not
 * stay down -- and auto-repeat navigation -- when the menu reopens. */
static int test_reopen_releases_held_keys(void)
{
    char path[64];
    uint16_t guest;

    CHECK(fresh_init(path, sizeof path));
    CHECK(open_menu());
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_UP, 1, 0) == 1);
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_LEFT, 1, 0) == 1);
    rsx_overlay_build_draw_data();
    CHECK(rsx_overlay_test_input_down(RSX_OVERLAY_KEY_UP));
    /* F1 closes while Up/Enter are held; their key-ups arrive while closed. */
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_F1, 1, 0));
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_F1, 0, 0));
    CHECK(!rsx_overlay_visible());
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_UP, 0, 0) == 0);
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_LEFT, 0, 0) == 0);
    CHECK(open_menu());
    CHECK(!rsx_overlay_test_input_down(RSX_OVERLAY_KEY_UP));
    CHECK(!rsx_overlay_test_input_down(RSX_OVERLAY_KEY_LEFT));
    /* Same through the controller chord (closed from the pad path). */
    guest = CELL_PAD_CTRL_DOWN;
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_DOWN, 1, 0) == 1);
    (void)rsx_overlay_handle_pad(CELL_PAD_CTRL_DOWN, &guest);
    rsx_overlay_build_draw_data();
    CHECK(rsx_overlay_test_input_down(RSX_OVERLAY_KEY_DOWN));
    CHECK(rsx_overlay_test_input_down(CELL_PAD_CTRL_DOWN));
    guest = CELL_PAD_CTRL_SELECT | CELL_PAD_CTRL_START;
    (void)rsx_overlay_handle_pad(guest, &guest);
    CHECK(!rsx_overlay_visible());
    guest = 0;
    (void)rsx_overlay_handle_pad(0, &guest);
    CHECK(open_menu());
    CHECK(!rsx_overlay_test_input_down(RSX_OVERLAY_KEY_DOWN));
    CHECK(!rsx_overlay_test_input_down(CELL_PAD_CTRL_DOWN));
    rsx_overlay_shutdown();
    unlink(path);
    return 1;
}

static int test_controller_status_is_never_invented(void)
{
    int connected = -2, battery = -2;
    rsx_overlay_get_controller_status(&connected, &battery);
    CHECK(connected == 0 && battery == -1);   /* nothing published yet */
    rsx_overlay_publish_controller_status(1, 57);
    rsx_overlay_get_controller_status(&connected, &battery);
    CHECK(connected == 1 && battery == 57);
    rsx_overlay_publish_controller_status(1, 150);  /* out of range: unknown */
    rsx_overlay_get_controller_status(&connected, &battery);
    CHECK(battery == -1);
    rsx_overlay_publish_controller_status(0, 80);   /* no controller: no battery */
    rsx_overlay_get_controller_status(&connected, &battery);
    CHECK(connected == 0 && battery == -1);
    return 1;
}

/* A core whose renderer failed never toggles and never withholds input. */
static int test_disabled_core_is_inert(void)
{
    char path[64];
    uint16_t guest;
    const uint16_t chord = CELL_PAD_CTRL_SELECT | CELL_PAD_CTRL_START;

    CHECK(fresh_init(path, sizeof path));
    CHECK(open_menu());
    rsx_overlay_disable("test: renderer unavailable");
    CHECK(rsx_overlay_disabled());
    CHECK(!rsx_overlay_visible());
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_F1, 1, 0) == 0);
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_F1, 0, 0) == 0);
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_ENTER, 1, 0) == 0);
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_ESCAPE, 1, 0) == 0);
    CHECK(!rsx_overlay_visible());
    for (int frame = 0; frame < 3; ++frame) {
        guest = chord | CELL_PAD_CTRL_CROSS;
        CHECK(rsx_overlay_handle_pad(guest, &guest) == 0);
        CHECK(guest == (chord | CELL_PAD_CTRL_CROSS));
        CHECK(!rsx_overlay_visible());
    }
    guest = CELL_PAD_CTRL_START;
    CHECK(rsx_overlay_handle_pad(guest, &guest) == 0 && guest == CELL_PAD_CTRL_START);
    rsx_overlay_shutdown();
    CHECK(!rsx_overlay_disabled());

    /* Disabled before init (renderer built first): still inert after init. */
    rsx_overlay_disable("test: before init");
    CHECK(rsx_overlay_init(path));
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_F1, 1, 0) == 0);
    CHECK(!rsx_overlay_visible());
    rsx_overlay_shutdown();

    /* shutdown clears it: a later session works normally. */
    CHECK(fresh_init(path, sizeof path));
    CHECK(open_menu());
    rsx_overlay_shutdown();
    unlink(path);
    return 1;
}

int main(void)
{
    int ok = 1;
    rsx_overlay_test_set_clock(s_now);
    char initial_path[] = "/tmp/rsx-overlay-initial-XXXXXX";
    int initial_fd = mkstemp(initial_path);
    rsx_overlay_settings initial_settings;

    ok &= initial_fd >= 0;
    if (initial_fd >= 0) close(initial_fd);
    rsx_overlay_settings_defaults(&initial_settings);
    ok &= rsx_overlay_settings_save(&initial_settings, initial_path);
    ok &= rsx_overlay_init(initial_path) == 1;
    ok &= test_f1_edges_toggle_once();
    ok &= test_pressure_obeys_overlay_capture();
    ok &= test_focus_lost_releases_imgui_keys();
    ok &= test_start_edges_and_focus_reset();
    ok &= test_capture_is_limited_to_open_menu();
    ok &= test_backend_and_game_metrics_merge();
    ok &= test_coherent_metric_publication();
    ok &= test_provider_pair_and_lifetime();
    ok &= test_draw_packet_is_a_bounded_copy();
    ok &= test_host_mapping_helpers();
    ok &= test_toggle_key_and_visibility_persistence();
    ok &= test_backend_metrics_reset_after_reinit();
    ok &= test_provider_registration_from_callback_is_rejected();
    ok &= test_font_atlas_is_an_owned_copy();
    ok &= test_backend_capabilities_gate_graphics_settings();
    ok &= test_settings_saved_on_change_not_per_frame();
    ok &= test_failed_save_is_not_retried_per_frame();
    ok &= test_shader_stats_and_missing_provider();
    ok &= test_settings_path_from_env();
    ok &= test_arrow_keys_move_card_focus();
    ok &= test_escape_closes_menu();
    ok &= test_escape_backs_out_one_level();
    ok &= test_reopen_releases_held_keys();
    ok &= test_packet_carries_display_size();
    ok &= test_controller_status_is_never_invented();
    ok &= test_closed_shell_costs_nothing();
    ok &= test_draw_budget_worst_case();
    ok &= test_scale_change_rebuilds_atlas();
    ok &= test_quit_dialog();
    ok &= test_feedback_follows_ui_switches();
    ok &= test_sound_and_vibration_reach_consumers();
    ok &= test_slider_saves_after_release();
    ok &= test_atlas_contract();
    ok &= test_unavailable_rows_legible();
    ok &= test_disabled_core_is_inert();
    rsx_overlay_shutdown();
    unlink(initial_path);
    printf("test_rsx_overlay: %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
