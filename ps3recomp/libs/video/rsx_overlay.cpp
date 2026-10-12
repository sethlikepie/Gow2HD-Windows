#include "rsx_overlay.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>
#include <vector>

#include "imgui.h"
#include "imgui_internal.h" /* ImDrawListSharedData: the shell's white texel */

#include "cellPad.h"
#include "rsx_overlay_assets.h"
#include "rsx_overlay_feedback.h"
#include "rsx_overlay_log.h"
#include "rsx_overlay_settings.h"
#include "rsx_overlay_shell.h"
#include "rsx_overlay_shell_draw.h"

extern "C" void ps3_audio_set_runtime_mute(int muted); /* libs/audio/audio_mute.c */

typedef void (*rsx_overlay_provider)(rsx_overlay_snapshot* out, void* userdata);

static rsx_overlay_settings s_settings;
static char s_settings_path[1024];
static ImGuiContext* s_imgui_context;
static std::atomic<int> s_initialized(0);
static std::atomic<int> s_visible(0);
static std::atomic<int> s_toggle_down(0);
/* Controller toggle is the Select+Start chord: GoW2 pauses on a lone Start,
 * which must keep reaching the game. */
static std::atomic<int> s_chord_down(0);
static std::atomic<int> s_chord_captured(0);
static std::atomic<uint16_t> s_ui_pad_buttons(0);
/* PS/Home on the controller: host-only, toggles the menu on its rising edge
 * and is never folded into a guest bit (unlike the Select+Start chord). */
static std::atomic<int> s_home_down(0);
/* Controller status for the top strip, published by the pad backend at most
 * once a second. Battery stays -1 (unknown) until the controller reports one.
 * One word (bit 0 connected, bits 8..15 battery + 1) so a reader never pairs
 * a fresh "connected" with a stale battery. */
static std::atomic<int> s_controller_status(0);
/* Set when the backend's renderer failed: the core then ignores input. */
static std::atomic<int> s_disabled(0);
/* The Escape press that closed the menu is swallowed until its key-up, so
 * its auto-repeat never reaches the host's own Escape handling. */
static std::atomic<int> s_escape_captured(0);
/* Escape physically down (overlay-visible presses only): its auto-repeat is
 * not a new press, so holding Escape on an open combo closes the combo but
 * not also the menu. */
static std::atomic<int> s_escape_held(0);
/* Bumped each time the menu opens (from any thread); the ImGui thread
 * compares it with the last value it saw to release stale input. */
static std::atomic<uint64_t> s_open_count(0);
static uint64_t s_frame_open_count; /* ImGui thread only */
static std::atomic<uint64_t> s_backend_sequence(0);
static std::atomic<int> s_backend_published(0);
static std::atomic<double> s_backend_fps(0.0);
static std::atomic<double> s_backend_frame_ms(0.0);
static std::atomic<uint64_t> s_backend_draws_frame(0);
static std::atomic<uint64_t> s_backend_draws_total(0);
static std::atomic<uint64_t> s_shader_sequence(0);
static std::atomic<int> s_shader_published(0);
static std::atomic<uint32_t> s_shader_hits(0);
static std::atomic<uint32_t> s_shader_misses(0);
static std::atomic<uint32_t> s_shader_compiles(0);
static std::atomic<uint32_t> s_shader_failures(0);
static rsx_overlay_provider s_game_provider;
static void* s_game_userdata;
static unsigned s_provider_calls;
static int s_provider_changing;
static std::mutex s_provider_mutex;
static std::condition_variable s_provider_condition;
static thread_local unsigned s_provider_callback_depth;
static int s_settings_dirty;
static rsx_overlay_draw_packet s_draw_packet;
static std::mutex s_draw_packet_mutex;
/* Font atlas pixels captured on the ImGui thread at init; renderers copy them
 * through rsx_overlay_copy_font_atlas instead of reading ImGui state. */
static std::mutex s_font_mutex;
static std::vector<uint8_t> s_font_rgba;
static uint32_t s_font_width;
static uint32_t s_font_height;
static std::atomic<uint64_t> s_atlas_generation(0);
/* Guards the graphics fields of s_settings (read by render threads) and the
 * backend capability registration. */
static std::mutex s_graphics_mutex;
static int s_graphics_ready;
static int s_capabilities_registered;
static rsx_overlay_backend_capabilities s_capabilities;

/* Drawable the UI is laid out on (event thread writes, anyone reads). */
static std::atomic<uint32_t> s_display_width(1280u);
static std::atomic<uint32_t> s_display_height(720u);
static std::atomic<float> s_display_scale(1.0f);
/* 1: pumps should publish the drawable size (see rsx_overlay_display_stale). */
static std::atomic<int> s_display_stale(1);

static rsx_shell s_shell;                 /* event thread */
static rsx_overlay_assets s_assets;       /* event thread; pixels mirrored in s_font_rgba */
static float s_assets_scale;              /* event thread */
static std::atomic<rsx_overlay_asset_builder> s_asset_builder(rsx_overlay_assets_build_embedded);
static std::atomic<int> s_assets_dirty(0);
static std::atomic<int> s_render_active(0);
static std::atomic<int> s_quit_requested(0);
static std::atomic<uint16_t> s_release_guard(0);
/* Host bit just bound by a remap capture: withheld (menu open or not) until
 * released, since it is usually outside the navigation mask. */
static std::atomic<uint16_t> s_capture_hold(0);
static std::atomic<int> s_capture_all(0);
/* Pad bits of the keyboard keys held when a key closed the menu. The pad
 * backends fold those keys into the port-0 mask on their own channel
 * (GCKeyboard on macOS), which may report them after the first guest poll
 * that follows the close; until seen they stay pending, then join the
 * release guard. Dropped at the deadline if they never show up. */
static std::atomic<uint16_t> s_key_close_pending(0);
static std::atomic<double> s_key_close_deadline(0.0);
static const double kKeyClosePendingSeconds = 0.25;
static uint64_t s_key_epoch;    /* open count the keyboard state belongs to (event thread) */
static uint32_t s_key_held;     /* RSX_SHELL_IN_* held on the keyboard (event thread) */
static uint32_t s_key_latched;  /* pressed since the last build (event thread) */
static uint16_t s_prev_pad;     /* pad buttons the last build saw */
static uint16_t s_capture_pending; /* Select/Start pressed during a remap capture */
static uint32_t s_frame_keys;   /* test view of the last build */
static uint32_t s_build_keys;   /* keyboard keys held or pressed in this build (event thread) */
static uint16_t s_frame_pad;
static std::mutex s_feedback_mutex;
static rsx_overlay_feedback_sink s_feedback_sink;
static void* s_feedback_userdata;
static char s_title[64] = "Jogo";
#ifdef RSX_OVERLAY_TESTING
static double s_test_clock = -1.0;
static void (*s_test_close_hook)(void);
#define OVERLAY_TEST_CLOSE_HOOK() do { if (s_test_close_hook) s_test_close_hook(); } while (0)
#else
#define OVERLAY_TEST_CLOSE_HOOK() do { } while (0)
#endif

static void overlay_rebuild_assets(float scale);

void rsx_overlay_set_display(uint32_t width_px, uint32_t height_px, float backing_scale)
{
    s_display_stale.store(0);
    if (width_px == 0 || height_px == 0 || width_px > RSX_OVERLAY_DISPLAY_MAX ||
        height_px > RSX_OVERLAY_DISPLAY_MAX || !(backing_scale >= 0.5f && backing_scale <= 4.0f))
        return;
    s_display_width.store(width_px);
    s_display_height.store(height_px);
    s_display_scale.store(backing_scale);
    /* The atlas follows the backing scale: rebuilt once per change (or once
     * after a builder change), never when only the size changes. */
    if (s_initialized.load() && (backing_scale != s_assets_scale || s_assets_dirty.load()))
        overlay_rebuild_assets(backing_scale);
}

void rsx_overlay_invalidate_display(void)
{
    s_display_stale.store(1);
}

int rsx_overlay_display_stale(void)
{
    return s_display_stale.load();
}

void rsx_overlay_get_display(uint32_t* width_px, uint32_t* height_px, float* backing_scale)
{
    if (width_px != nullptr) *width_px = s_display_width.load();
    if (height_px != nullptr) *height_px = s_display_height.load();
    if (backing_scale != nullptr) *backing_scale = s_display_scale.load();
}

static void rsx_overlay_reset_backend_metrics(void)
{
    /* Initialization runs before backend publication begins. Publish the reset
     * as one sequence so snapshots cannot combine old and zeroed fields. */
    s_backend_sequence.fetch_add(1, std::memory_order_seq_cst);
    s_backend_fps.store(0.0, std::memory_order_relaxed);
    s_backend_frame_ms.store(0.0, std::memory_order_relaxed);
    s_backend_draws_frame.store(0, std::memory_order_relaxed);
    s_backend_draws_total.store(0, std::memory_order_relaxed);
    s_backend_published.store(0, std::memory_order_relaxed);
    s_backend_sequence.fetch_add(1, std::memory_order_seq_cst);
}

static void rsx_overlay_reset_shader_stats(void)
{
    s_shader_sequence.fetch_add(1, std::memory_order_seq_cst);
    s_shader_hits.store(0, std::memory_order_relaxed);
    s_shader_misses.store(0, std::memory_order_relaxed);
    s_shader_compiles.store(0, std::memory_order_relaxed);
    s_shader_failures.store(0, std::memory_order_relaxed);
    s_shader_published.store(0, std::memory_order_relaxed);
    s_shader_sequence.fetch_add(1, std::memory_order_seq_cst);
}

static uint16_t rsx_overlay_navigation_mask(void)
{
    return CELL_PAD_CTRL_SELECT | CELL_PAD_CTRL_START | CELL_PAD_CTRL_UP |
           CELL_PAD_CTRL_RIGHT | CELL_PAD_CTRL_DOWN | CELL_PAD_CTRL_LEFT |
           CELL_PAD_CTRL_L1 | CELL_PAD_CTRL_R1 | CELL_PAD_CTRL_L2 |
           CELL_PAD_CTRL_R2 | CELL_PAD_CTRL_TRIANGLE | CELL_PAD_CTRL_CIRCLE |
           CELL_PAD_CTRL_CROSS | CELL_PAD_CTRL_SQUARE;
}

uint16_t rsx_overlay_pad_navigation_mask(void)
{
    return rsx_overlay_navigation_mask();
}

static int rsx_overlay_navigation_button(uint16_t buttons)
{
    return (buttons & rsx_overlay_navigation_mask()) != 0;
}

static uint32_t overlay_key_to_shell(int key)
{
    switch (key) {
    case RSX_OVERLAY_KEY_UP: return RSX_SHELL_IN_UP;
    case RSX_OVERLAY_KEY_DOWN: return RSX_SHELL_IN_DOWN;
    case RSX_OVERLAY_KEY_LEFT: return RSX_SHELL_IN_LEFT;
    case RSX_OVERLAY_KEY_RIGHT: return RSX_SHELL_IN_RIGHT;
    case RSX_OVERLAY_KEY_ENTER: return RSX_SHELL_IN_CONFIRM;
    case RSX_OVERLAY_KEY_ESCAPE: return RSX_SHELL_IN_BACK;
    default: return 0;
    }
}

/* D-pad navigates, Cross confirms, Circle backs out (physical buttons, before remapping). */
static uint32_t overlay_pad_to_shell(uint16_t pad)
{
    uint32_t in = 0;
    if (pad & CELL_PAD_CTRL_UP) in |= RSX_SHELL_IN_UP;
    if (pad & CELL_PAD_CTRL_DOWN) in |= RSX_SHELL_IN_DOWN;
    if (pad & CELL_PAD_CTRL_LEFT) in |= RSX_SHELL_IN_LEFT;
    if (pad & CELL_PAD_CTRL_RIGHT) in |= RSX_SHELL_IN_RIGHT;
    if (pad & CELL_PAD_CTRL_CROSS) in |= RSX_SHELL_IN_CONFIRM;
    if (pad & CELL_PAD_CTRL_CIRCLE) in |= RSX_SHELL_IN_BACK;
    return in;
}

/* Keyboard keys the shell uses -> the pad bits cellPad folds them into
 * (Return -> START everywhere; arrows -> D-pad via GCKeyboard on macOS; on
 * SDL/Win32 they drive the right stick, so those bits just time out). */
static uint16_t overlay_keys_to_pad(uint32_t keys)
{
    uint16_t pad = 0;
    if (keys & RSX_SHELL_IN_CONFIRM) pad |= CELL_PAD_CTRL_START;
    if (keys & RSX_SHELL_IN_UP) pad |= CELL_PAD_CTRL_UP;
    if (keys & RSX_SHELL_IN_DOWN) pad |= CELL_PAD_CTRL_DOWN;
    if (keys & RSX_SHELL_IN_LEFT) pad |= CELL_PAD_CTRL_LEFT;
    if (keys & RSX_SHELL_IN_RIGHT) pad |= CELL_PAD_CTRL_RIGHT;
    return pad;
}

static double overlay_now(void)
{
#ifdef RSX_OVERLAY_TESTING
    if (s_test_clock >= 0.0)
        return s_test_clock;
#endif
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

/* Any thread, atomics only. `render_active` is raised after `visible` so a
 * build that is retiring a finished close animation on the event thread can
 * never leave an open shell unrendered (see rsx_overlay_build_draw_data). */
static void overlay_request_open(void)
{
    s_release_guard.store(0);
    s_key_close_pending.store(0);
    s_open_count.fetch_add(1);
    s_display_stale.store(1); /* lay out on the current drawable */
    s_visible.store(1);
    s_render_active.store(1);
}

/* Any thread, atomics only. Arms the release guard with every navigation
 * button: the first pad poll after the close (rsx_overlay_release_guard)
 * intersects it with what is actually held -- keyboard bits included -- so
 * buttons not held are released at once and held ones stay withheld until
 * released. Nothing depends on which thread saw the buttons last: a keyboard
 * Enter that closes from the event thread before any pad poll saw Return
 * (SDL reports it only after the pump) is still withheld.
 * `keyboard_pad` (0 for pad-sourced closes): pad bits of the keyboard keys
 * held at a keyboard close, withheld whenever the pad path reports them. */
static void overlay_request_close(uint16_t keyboard_pad)
{
    if (!s_visible.load())
        return;
    s_key_close_deadline.store(overlay_now() + kKeyClosePendingSeconds);
    s_key_close_pending.store(keyboard_pad);
    /* Arm BEFORE lowering `visible`: a guest poll landing between the two
     * steps still sees the menu open (buttons withheld), never an unarmed
     * closed menu. release_guard does not narrow the guard while visible. */
    s_release_guard.store(rsx_overlay_navigation_mask());
    OVERLAY_TEST_CLOSE_HOOK();
    if (s_visible.exchange(0) == 0)
        return;
    s_ui_pad_buttons.store(0);
    s_capture_all.store(0);
}

/* Shared by the PS/Home rising edge, the Select+Start chord and F1: atomics
 * only, so it is safe from any guest or backend thread. Every close goes
 * through overlay_request_close, which arms the release guard. */
static void rsx_overlay_toggle_atomic(uint16_t keyboard_pad)
{
    if (s_visible.load())
        overlay_request_close(keyboard_pad);
    else
        overlay_request_open();
}

/* Event thread. Key-ups that arrive while the menu is closed never reach the
 * shell, so keyboard state is dropped once per open -- at the first key event
 * or build after it, whichever comes first (a key pressed right after F1, in
 * the same event batch, is kept). */
static void overlay_sync_key_epoch(void)
{
    const uint64_t opens = s_open_count.load();
    if (opens != s_key_epoch) {
        s_key_epoch = opens;
        s_key_held = 0;
        s_key_latched = 0;
    }
}

/* Event thread: is the shell at its top level (Esc closes)? */
static int overlay_shell_at_top_level(void)
{
    if (s_shell.phase != RSX_SHELL_OPEN && s_shell.phase != RSX_SHELL_OPENING)
        return 1;
    return s_shell.level == RSX_SHELL_LEVEL_CARDS;
}

static void rsx_overlay_apply_input_settings(void)
{
    cellPad_set_host_mapping(s_settings.host_button_to_guest_button, s_settings.analog_deadzone);
    cellPad_set_host_vibration(s_settings.vibration);
}

/* Event thread. Rebuilds the atlas at `scale` and publishes it as texture 0. */
static void overlay_rebuild_assets(float scale)
{
    rsx_overlay_assets fresh;
    rsx_overlay_asset_builder builder = s_asset_builder.load();
    if (!builder(&fresh, scale) && !rsx_overlay_assets_build_embedded(&fresh, scale)) {
        rsx_overlay_log_write(RSX_OVERLAY_LOG_ERROR, "overlay: glyph atlas unavailable");
        return;
    }
    rsx_overlay_assets_free(&s_assets);
    s_assets = fresh;
    s_assets_scale = scale;
    s_assets_dirty.store(0);
    {
        std::lock_guard<std::mutex> lock(s_font_mutex);
        s_font_rgba.assign(s_assets.rgba, s_assets.rgba + (size_t)s_assets.width * s_assets.height * 4u);
        s_font_width = s_assets.width;
        s_font_height = s_assets.height;
    }
    s_atlas_generation.fetch_add(1);
}

void rsx_overlay_set_asset_builder(rsx_overlay_asset_builder builder)
{
    s_asset_builder.store(builder != nullptr ? builder : rsx_overlay_assets_build_embedded);
    s_assets_dirty.store(1);
    s_display_stale.store(1); /* the next publication rebuilds with it */
}

static ImVec2 overlay_white_uv(void)
{
    if (s_assets.width == 0)
        return ImVec2(0.0f, 0.0f);
    return ImVec2(((float)s_assets.white_x + 2.0f) / (float)s_assets.width,
                  ((float)s_assets.white_y + 2.0f) / (float)s_assets.height);
}

int rsx_overlay_init(const char* settings_path)
{
    if (s_initialized.exchange(1) != 0)
        return 1;

    {
        rsx_overlay_settings loaded;
        rsx_overlay_settings_defaults(&loaded);
        (void)rsx_overlay_settings_load(&loaded, settings_path);
        std::lock_guard<std::mutex> lock(s_graphics_mutex);
        s_settings = loaded;
        s_graphics_ready = 1;
    }
    if (settings_path != nullptr) {
        std::strncpy(s_settings_path, settings_path, sizeof(s_settings_path) - 1);
        s_settings_path[sizeof(s_settings_path) - 1] = '\0';
    } else {
        s_settings_path[0] = '\0';
    }
    rsx_overlay_apply_input_settings();
    ps3_audio_set_runtime_mute(s_settings.game_mute);
    /* The menu always starts closed. A persisted "visible" (older files, or a
     * process killed with the menu open) is ignored and written back as 0:
     * reopening on launch would withhold pad input from the game. */
    {
        std::lock_guard<std::mutex> lock(s_graphics_mutex);
        s_settings.overlay_visible = 0;
    }
    s_visible.store(0);
    s_toggle_down.store(0);
    s_chord_down.store(0);
    s_home_down.store(0);
    s_chord_captured.store(0);
    s_ui_pad_buttons.store(0);
    s_escape_captured.store(0);
    s_escape_held.store(0);
    s_render_active.store(0);
    s_quit_requested.store(0);
    s_release_guard.store(0);
    s_capture_hold.store(0);
    s_key_close_pending.store(0);
    s_capture_all.store(0);
    s_key_held = 0;
    s_key_latched = 0;
    s_prev_pad = 0;
    s_capture_pending = 0;
    s_display_stale.store(1);
    s_frame_open_count = s_open_count.load();
    s_key_epoch = s_frame_open_count;
    s_settings_dirty = 0;

    s_imgui_context = ImGui::CreateContext();
    if (s_imgui_context == nullptr) {
        {
            std::lock_guard<std::mutex> lock(s_graphics_mutex);
            s_graphics_ready = 0;
        }
        s_initialized.store(0);
        rsx_overlay_log_write(RSX_OVERLAY_LOG_ERROR, "overlay ImGui context unavailable");
        return 0;
    }
    ImGui::SetCurrentContext(s_imgui_context);
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::GetStyle().AntiAliasedLinesUseTex = false; /* shapes use only the white texel */
    {
        /* ImGui still needs its own (unused) atlas built for NewFrame. */
        unsigned char* font_pixels;
        int font_width, font_height;
        ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&font_pixels, &font_width, &font_height);
    }
    rsx_shell_init(&s_shell);
    overlay_rebuild_assets(s_display_scale.load());
    rsx_overlay_reset_backend_metrics();
    rsx_overlay_reset_shader_stats();
    return 1;
}

void rsx_overlay_shutdown(void)
{
    if (s_initialized.exchange(0) == 0)
        return;
    if (s_settings_dirty && !rsx_overlay_settings_save(
            &s_settings, s_settings_path[0] == '\0' ? nullptr : s_settings_path)) {
        rsx_overlay_log_write(RSX_OVERLAY_LOG_WARN, "overlay settings were not saved");
    }
    rsx_overlay_assets_free(&s_assets);
    s_assets_scale = 0.0f;
    s_render_active.store(0);
    s_capture_all.store(0);
    s_release_guard.store(0);
    s_capture_hold.store(0);
    s_key_close_pending.store(0);
    {
        std::lock_guard<std::mutex> lock(s_font_mutex);
        s_font_rgba.clear();
        s_font_rgba.shrink_to_fit();
        s_font_width = 0;
        s_font_height = 0;
    }
    {
        std::lock_guard<std::mutex> lock(s_graphics_mutex);
        s_graphics_ready = 0;
    }
    if (s_imgui_context != nullptr) {
        ImGui::DestroyContext(s_imgui_context);
        s_imgui_context = nullptr;
    }
    s_visible.store(0);
    s_toggle_down.store(0);
    s_chord_down.store(0);
    s_home_down.store(0);
    s_chord_captured.store(0);
    s_ui_pad_buttons.store(0);
    s_escape_captured.store(0);
    s_escape_held.store(0);
    s_disabled.store(0);
}

int rsx_overlay_visible(void)
{
    return s_visible.load();
}

int rsx_overlay_handle_key(int key, int down, unsigned modifiers)
{
    const int toggle_key = s_settings.toggle_key == RSX_OVERLAY_TOGGLE_KEY_F2
        ? RSX_OVERLAY_KEY_F2 : RSX_OVERLAY_KEY_F1;
    uint32_t bit;
    (void)modifiers;

    if (!s_initialized.load() || s_disabled.load())
        return 0;
    if (key == toggle_key) {
        const int was_down = s_toggle_down.exchange(down != 0);
        if (down && !was_down && s_settings.overlay_enabled) {
            uint16_t keyboard_pad = 0;
            if (s_visible.load()) {
                overlay_sync_key_epoch();
                keyboard_pad = overlay_keys_to_pad(s_key_held);
            }
            rsx_overlay_toggle_atomic(keyboard_pad);
        }
        return s_settings.overlay_enabled || s_visible.load();
    }
    if (key == RSX_OVERLAY_KEY_ESCAPE && s_escape_captured.load()) {
        if (!down)
            s_escape_captured.store(0);
        return 1;
    }
    if (!s_visible.load()) {
        if (key == RSX_OVERLAY_KEY_ESCAPE && !down)
            s_escape_held.store(0);
        return 0;
    }
    bit = overlay_key_to_shell(key);
    if (bit == 0)
        return 0;
    overlay_sync_key_epoch();
    if (key == RSX_OVERLAY_KEY_ESCAPE) {
        const int was_held = s_escape_held.exchange(down != 0);
        if (down && was_held)
            return 1; /* auto-repeat is never another Back */
        if (down && overlay_shell_at_top_level()) {
            s_escape_held.store(0);
            s_escape_captured.store(1);
            overlay_request_close(overlay_keys_to_pad(s_key_held));
            return 1;
        }
    }
    if (down) {
        if ((s_key_held & bit) == 0)
            s_key_latched |= bit;
        s_key_held |= bit;
    } else {
        s_key_held &= ~bit;
    }
    return 1;
}

int rsx_overlay_handle_pad(uint16_t host_buttons, uint16_t* guest_buttons)
{
    const uint16_t chord = CELL_PAD_CTRL_SELECT | CELL_PAD_CTRL_START;
    const int chord_down = (host_buttons & chord) == chord;
    int chord_was_down;
    int toggled = 0;
    int consume;

    if (!s_initialized.load() || s_disabled.load() || guest_buttons == nullptr)
        return 0;
    /* Toggle on the rising edge of Select+Start held together; a held chord
     * never re-toggles. From the toggle until BOTH buttons are released,
     * Select and Start are withheld from the guest, so letting go of one
     * first cannot leak a lone Start (GoW2 pause) or Select to the game. A
     * lone Start/Select with the menu closed reaches the guest untouched. */
    chord_was_down = s_chord_down.exchange(chord_down);
    if ((host_buttons & chord) == 0)
        s_chord_captured.store(0);
    if (chord_down && !chord_was_down && s_settings.overlay_enabled) {
        rsx_overlay_toggle_atomic(0);
        s_chord_captured.store(1);
        toggled = 1;
    }
    if (!s_visible.load()) {
        s_ui_pad_buttons.store(0);
        if (s_chord_captured.load()) {
            /* The closing press itself is not gameplay input. */
            if (toggled)
                *guest_buttons &= (uint16_t)~rsx_overlay_navigation_mask();
            else
                *guest_buttons &= (uint16_t)~chord;
            return 1;
        }
        return 0;
    }
    /* cellPadGetData may run on a guest thread. It only publishes state here;
     * build_draw_data consumes it on the sole event thread. The chord that
     * opened or will close the menu is not menu input. */
    s_ui_pad_buttons.store(s_chord_captured.load()
        ? (uint16_t)(host_buttons & (uint16_t)~chord) : host_buttons);
    if (s_capture_all.load()) {
        /* Remap capture: every button is menu input, none reaches the game. */
        *guest_buttons = 0;
        return 1;
    }
    consume = rsx_overlay_navigation_button(host_buttons);
    if (consume)
        *guest_buttons &= (uint16_t)~rsx_overlay_navigation_mask();
    return consume;
}

void rsx_overlay_focus_lost(void)
{
    s_toggle_down.store(0);
    s_chord_down.store(0);
    s_home_down.store(0);
    s_chord_captured.store(0);
    s_ui_pad_buttons.store(0);
    s_escape_captured.store(0);
    s_escape_held.store(0);
    s_key_held = 0;
    s_key_latched = 0;
}

void rsx_overlay_disable(const char* reason)
{
    /* Atomics only: backends call this from their render/init thread. */
    s_visible.store(0);
    s_render_active.store(0);
    s_capture_all.store(0);
    s_chord_down.store(0);
    s_home_down.store(0);
    s_chord_captured.store(0);
    s_ui_pad_buttons.store(0);
    s_escape_captured.store(0);
    s_escape_held.store(0);
    if (s_disabled.exchange(1) == 0) {
        char line[160];
        std::snprintf(line, sizeof(line), "overlay disabled: %s",
                      reason != nullptr ? reason : "renderer unavailable");
        rsx_overlay_log_write(RSX_OVERLAY_LOG_ERROR, line);
        std::fprintf(stderr, "[overlay] %s\n", line);
    }
}

int rsx_overlay_disabled(void)
{
    return s_disabled.load();
}

void rsx_overlay_handle_home(int down)
{
    if (!s_initialized.load() || s_disabled.load())
        return;
    const int was_down = s_home_down.exchange(down != 0);
    if (down && !was_down && s_settings.overlay_enabled)
        rsx_overlay_toggle_atomic(0);
}

void rsx_overlay_publish_controller_status(int connected, int battery_percent)
{
    const int battery = connected && battery_percent >= 0 && battery_percent <= 100
                        ? battery_percent : -1;
    s_controller_status.store(connected ? (1 | ((battery + 1) << 8)) : 0);
}

void rsx_overlay_get_controller_status(int* connected, int* battery_percent)
{
    const int status = s_controller_status.load();
    const int on = status & 1;
    if (connected != nullptr) *connected = on;
    if (battery_percent != nullptr) *battery_percent = on ? ((status >> 8) & 0xFF) - 1 : -1;
}

void rsx_overlay_set_game_provider(rsx_overlay_provider provider, void* userdata)
{
    /* Provider callbacks must not register a provider recursively. The callback
     * itself runs outside the mutex; replacement waits for all callers to drain. */
    if (s_provider_callback_depth != 0) {
        rsx_overlay_log_write(RSX_OVERLAY_LOG_WARN,
                              "provider registration rejected from provider callback");
        return;
    }
    std::unique_lock<std::mutex> lock(s_provider_mutex);
    s_provider_condition.wait(lock, [] { return !s_provider_changing; });
    s_provider_changing = 1;
    s_provider_condition.wait(lock, [] { return s_provider_calls == 0; });
    s_game_provider = provider;
    s_game_userdata = userdata;
    s_provider_changing = 0;
    lock.unlock();
    s_provider_condition.notify_all();
}

void rsx_overlay_set_snapshot_provider(rsx_overlay_provider provider, void* userdata)
{
    rsx_overlay_set_game_provider(provider, userdata);
}

void rsx_overlay_publish_backend_metrics(const rsx_overlay_backend_metrics* metrics)
{
    if (metrics == nullptr)
        return;
    /* One active publisher per selected backend; readers use a coherent seqlock. */
    s_backend_sequence.fetch_add(1, std::memory_order_seq_cst);
    s_backend_fps.store(metrics->fps, std::memory_order_relaxed);
    s_backend_frame_ms.store(metrics->frame_ms, std::memory_order_relaxed);
    s_backend_draws_frame.store(metrics->guest_draws_frame, std::memory_order_relaxed);
    s_backend_draws_total.store(metrics->guest_draws_total, std::memory_order_relaxed);
    s_backend_published.store(1, std::memory_order_relaxed);
    s_backend_sequence.fetch_add(1, std::memory_order_seq_cst);
}

void rsx_overlay_publish_shader_stats(const rsx_overlay_shader_stats* stats)
{
    if (stats == nullptr)
        return;
    /* One publisher (the selected backend's render thread); seqlock readers. */
    s_shader_sequence.fetch_add(1, std::memory_order_seq_cst);
    s_shader_hits.store(stats->hits, std::memory_order_relaxed);
    s_shader_misses.store(stats->misses, std::memory_order_relaxed);
    s_shader_compiles.store(stats->compiles, std::memory_order_relaxed);
    s_shader_failures.store(stats->failures, std::memory_order_relaxed);
    s_shader_published.store(1, std::memory_order_relaxed);
    s_shader_sequence.fetch_add(1, std::memory_order_seq_cst);
}

int rsx_overlay_read_shader_stats(rsx_overlay_shader_stats* out)
{
    rsx_overlay_shader_stats copy;
    int published;

    if (out == nullptr)
        return 0;
    for (;;) {
        uint64_t before = s_shader_sequence.load(std::memory_order_seq_cst);
        if (before & 1u)
            continue;
        published = s_shader_published.load(std::memory_order_relaxed);
        copy.hits = s_shader_hits.load(std::memory_order_relaxed);
        copy.misses = s_shader_misses.load(std::memory_order_relaxed);
        copy.compiles = s_shader_compiles.load(std::memory_order_relaxed);
        copy.failures = s_shader_failures.load(std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_seq_cst);
        if (before == s_shader_sequence.load(std::memory_order_seq_cst))
            break;
    }
    if (!published)
        return 0;
    *out = copy;
    return 1;
}

const char* rsx_overlay_settings_path_from_env(void)
{
    const char* path = std::getenv("PS3_OVERLAY_SETTINGS");
    return (path != nullptr && path[0] != '\0') ? path : nullptr;
}

int rsx_overlay_persist_settings(void)
{
    rsx_overlay_settings copy;

    if (!s_initialized.load() || s_imgui_context == nullptr)
        return 0;
    if (!s_settings_dirty)
        return 0;
    /* A held direction on the slider changes the value ~10x/s: save once it is released. */
    if (s_visible.load() && rsx_shell_direction_held(&s_shell))
        return 0;
    {
        std::lock_guard<std::mutex> lock(s_graphics_mutex);
        copy = s_settings;
    }
    /* Cleared before the write: a failure is reported once and retried only
     * after the next change, so an unwritable path never costs a write per
     * frame. */
    s_settings_dirty = 0;
    if (!rsx_overlay_settings_save(&copy, s_settings_path[0] == '\0' ? nullptr : s_settings_path)) {
        rsx_overlay_log_write(RSX_OVERLAY_LOG_WARN, "overlay settings were not saved");
        return -1;
    }
    return 1;
}

void rsx_overlay_snapshot_current(rsx_overlay_snapshot* out)
{
    rsx_overlay_snapshot game = {};
    rsx_overlay_provider provider;
    void* userdata;

    if (out == nullptr)
        return;
    std::memset(out, 0, sizeof(*out));
    for (;;) {
        uint64_t before = s_backend_sequence.load(std::memory_order_seq_cst);
        if (before & 1u)
            continue;
        out->fps = s_backend_fps.load(std::memory_order_relaxed);
        out->frame_ms = s_backend_frame_ms.load(std::memory_order_relaxed);
        out->guest_draws_frame = s_backend_draws_frame.load(std::memory_order_relaxed);
        out->guest_draws_total = s_backend_draws_total.load(std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_seq_cst);
        if (before == s_backend_sequence.load(std::memory_order_seq_cst))
            break;
    }
    {
        std::unique_lock<std::mutex> lock(s_provider_mutex);
        s_provider_condition.wait(lock, [] { return !s_provider_changing; });
        provider = s_game_provider;
        userdata = s_game_userdata;
        if (provider != nullptr)
            ++s_provider_calls;
    }
    if (provider == nullptr)
        return;
    ++s_provider_callback_depth;
    provider(&game, userdata);
    --s_provider_callback_depth;
    {
        std::lock_guard<std::mutex> lock(s_provider_mutex);
        --s_provider_calls;
    }
    s_provider_condition.notify_all();
    out->wad_state_available = game.wad_state_available;
    std::memcpy(out->wad_state, game.wad_state, sizeof(out->wad_state));
    out->wad_state[sizeof(out->wad_state) - 1] = '\0';
    out->shader_stats_available = game.shader_stats_available;
    out->shader_hits = game.shader_hits;
    out->shader_misses = game.shader_misses;
    out->shader_compiles = game.shader_compiles;
    out->shader_failures = game.shader_failures;
}

int rsx_overlay_copy_font_atlas(uint8_t* rgba, size_t capacity,
                                uint32_t* width, uint32_t* height)
{
    std::lock_guard<std::mutex> lock(s_font_mutex);
    size_t size = s_font_rgba.size();

    if (size == 0)
        return 0;
    if (rgba != nullptr) {
        if (capacity < size)
            return 0;
        std::memcpy(rgba, s_font_rgba.data(), size);
    }
    if (width != nullptr)
        *width = s_font_width;
    if (height != nullptr)
        *height = s_font_height;
    return 1;
}

uint64_t rsx_overlay_font_atlas_generation(void)
{
    return s_atlas_generation.load();
}

void rsx_overlay_set_backend_capabilities(const rsx_overlay_backend_capabilities* caps)
{
    std::lock_guard<std::mutex> lock(s_graphics_mutex);
    if (caps == nullptr) {
        s_capabilities_registered = 0;
        std::memset(&s_capabilities, 0, sizeof(s_capabilities));
        return;
    }
    s_capabilities = *caps;
    s_capabilities_registered = 1;
}

int rsx_overlay_get_backend_capabilities(rsx_overlay_backend_capabilities* out)
{
    std::lock_guard<std::mutex> lock(s_graphics_mutex);
    if (out == nullptr || !s_capabilities_registered)
        return 0;
    *out = s_capabilities;
    return 1;
}

int rsx_overlay_get_graphics_settings(rsx_overlay_graphics_settings* out)
{
    std::lock_guard<std::mutex> lock(s_graphics_mutex);
    if (out == nullptr || !s_graphics_ready)
        return 0;
    out->window_width = s_settings.window_width;
    out->window_height = s_settings.window_height;
    out->fullscreen = s_settings.fullscreen;
    out->vsync = s_settings.vsync;
    return 1;
}

/* Applies only the fields the registered backend consumes; the result is
 * validated like a loaded settings file. Returns 0 without a registration. */
static int rsx_overlay_apply_graphics(const rsx_overlay_graphics_settings* request)
{
    rsx_overlay_settings candidate;

    if (request == nullptr)
        return 0;
    {
        std::lock_guard<std::mutex> lock(s_graphics_mutex);
        if (!s_capabilities_registered || !s_graphics_ready)
            return 0;
        candidate = s_settings;
        if (s_capabilities.window_size != RSX_OVERLAY_SETTING_UNAVAILABLE) {
            candidate.window_width = request->window_width;
            candidate.window_height = request->window_height;
        }
        if (s_capabilities.fullscreen != RSX_OVERLAY_SETTING_UNAVAILABLE)
            candidate.fullscreen = request->fullscreen;
        if (s_capabilities.vsync != RSX_OVERLAY_SETTING_UNAVAILABLE)
            candidate.vsync = request->vsync;
        (void)rsx_overlay_settings_validate(&candidate);
        s_settings.window_width = candidate.window_width;
        s_settings.window_height = candidate.window_height;
        s_settings.fullscreen = candidate.fullscreen;
        s_settings.vsync = candidate.vsync;
    }
    s_settings_dirty = 1;
    return 1;
}

static void rsx_overlay_copy_imgui_draw_data(const ImDrawData* draw_data, uint32_t width,
                                             uint32_t height, float scale, float open_progress)
{
    /* ~870 KB: kept off the stack of whichever thread pumps events. Only the
     * single event thread builds draw data, so one scratch copy suffices. */
    static rsx_overlay_draw_packet packet;
    int list_index;

    if (draw_data == nullptr)
        return;
    packet.vertex_count = 0;
    packet.index_count = 0;
    packet.command_count = 0;
    packet.display_width = width;
    packet.display_height = height;
    packet.backing_scale = scale;
    packet.open_progress = open_progress;
    packet.backdrop_dim = RSX_SHELL_BACKDROP_DIM;
    packet.backdrop_blur = 1u;
    for (list_index = 0; list_index < draw_data->CmdListsCount; ++list_index) {
        const ImDrawList* list = draw_data->CmdLists[list_index];
        uint32_t vertex_base = packet.vertex_count;
        uint32_t index_base = packet.index_count;
        int command_index;

        if ((uint64_t)packet.vertex_count + (uint64_t)list->VtxBuffer.Size >
                RSX_OVERLAY_DRAW_VERTEX_CAPACITY ||
            (uint64_t)packet.index_count + (uint64_t)list->IdxBuffer.Size >
                RSX_OVERLAY_DRAW_INDEX_CAPACITY)
            break;
        for (int vertex_index = 0; vertex_index < list->VtxBuffer.Size; ++vertex_index) {
            const ImDrawVert& source = list->VtxBuffer[vertex_index];
            rsx_overlay_draw_vertex* destination =
                &packet.vertices[packet.vertex_count + (uint32_t)vertex_index];
            destination->position[0] = source.pos.x;
            destination->position[1] = source.pos.y;
            destination->uv[0] = source.uv.x;
            destination->uv[1] = source.uv.y;
            destination->color = source.col;
        }
        std::memcpy(packet.indices + packet.index_count, list->IdxBuffer.Data,
                    (size_t)list->IdxBuffer.Size * sizeof(packet.indices[0]));
        packet.vertex_count += (uint32_t)list->VtxBuffer.Size;
        packet.index_count += (uint32_t)list->IdxBuffer.Size;
        for (command_index = 0; command_index < list->CmdBuffer.Size; ++command_index) {
            const ImDrawCmd& source = list->CmdBuffer[command_index];
            rsx_overlay_draw_command* destination;

            if (packet.command_count >= RSX_OVERLAY_DRAW_COMMAND_CAPACITY)
                break;
            if (source.UserCallback != nullptr)
                continue;
            destination = &packet.commands[packet.command_count++];
            destination->element_count = source.ElemCount;
            destination->index_offset = index_base + source.IdxOffset;
            destination->vertex_offset = vertex_base + source.VtxOffset;
            destination->clip_rect[0] = source.ClipRect.x;
            destination->clip_rect[1] = source.ClipRect.y;
            destination->clip_rect[2] = source.ClipRect.z;
            destination->clip_rect[3] = source.ClipRect.w;
            destination->texture_id = (uintptr_t)source.GetTexID();
        }
    }
    std::lock_guard<std::mutex> lock(s_draw_packet_mutex);
    packet.generation = s_draw_packet.generation + 1;
    s_draw_packet = packet;
}

int rsx_overlay_render_active(void) { return s_render_active.load(); }
/* Pumps call this every frame: a plain load while nothing is pending. */
int rsx_overlay_take_quit_request(void)
{
    return s_quit_requested.load() ? s_quit_requested.exchange(0) : 0;
}

void rsx_overlay_set_title(const char* title)
{
    if (title != nullptr && title[0] != '\0')
        std::snprintf(s_title, sizeof(s_title), "%s", title);
}

void rsx_overlay_set_feedback_sink(rsx_overlay_feedback_sink sink, void* userdata)
{
    std::lock_guard<std::mutex> lock(s_feedback_mutex);
    s_feedback_sink = sink;
    s_feedback_userdata = userdata;
}

uint16_t rsx_overlay_release_guard(uint16_t host_buttons)
{
    if (!s_initialized.load())
        return 0;
    /* While the menu is open the guard is left armed as is: a poll just
     * before a close must not drop a key the pad path has not reported yet. */
    if (s_visible.load())
        return (uint16_t)(s_capture_hold.fetch_and(host_buttons) & host_buttons);
    /* Keyboard bits of a keyboard close join the guard the first time the
     * pad path reports them held (it may lag the key event), so they stay
     * withheld until released; unseen ones are dropped at the deadline. */
    const uint16_t pending = s_key_close_pending.load();
    if (pending != 0) {
        const uint16_t seen = (uint16_t)(pending & host_buttons);
        if (seen != 0)
            s_release_guard.fetch_or(seen);
        if (overlay_now() >= s_key_close_deadline.load())
            s_key_close_pending.store(0);
        else if (seen != 0)
            s_key_close_pending.fetch_and((uint16_t)~seen);
    }
    /* One atomic read-modify-write each: an arm (close on the event thread)
     * racing this guest poll is either intersected now or on the next poll,
     * never overwritten. */
    const uint16_t guard = (uint16_t)(s_release_guard.fetch_and(host_buttons) & host_buttons);
    const uint16_t hold = (uint16_t)(s_capture_hold.fetch_and(host_buttons) & host_buttons);
    return (uint16_t)(guard | hold);
}

int rsx_overlay_pad_idle(void)
{
    if (!s_initialized.load())
        return 1;
    /* `visible` first: a close arms the guard before lowering it, so seeing
     * the menu closed here also sees that close's guard. */
    if (s_visible.load())
        return 0;
    return s_release_guard.load() == 0 && s_capture_hold.load() == 0 &&
           s_key_close_pending.load() == 0 && s_chord_down.load() == 0 &&
           s_chord_captured.load() == 0 && s_ui_pad_buttons.load() == 0;
}

static void overlay_collect_input(rsx_shell_input* in)
{
    const uint16_t pad = s_ui_pad_buttons.load();
    const uint16_t chord = CELL_PAD_CTRL_SELECT | CELL_PAD_CTRL_START;
    uint16_t pad_pressed = (uint16_t)(pad & (uint16_t)~s_prev_pad);
    s_prev_pad = pad;
    /* Remap capture vs the Select+Start chord: Select or Start pressed during
     * a capture is bound only once released on its own, so pressing one and
     * then the other still forms the chord (the pad path closes the shell)
     * instead of remapping the first. Any other button binds on its press. */
    if (s_visible.load() && s_shell.level == RSX_SHELL_LEVEL_CAPTURE) {
        s_capture_pending |= (uint16_t)(pad_pressed & chord);
        pad_pressed &= (uint16_t)~chord;
        const uint16_t released = (uint16_t)(s_capture_pending & (uint16_t)~pad);
        pad_pressed |= released;
        s_capture_pending &= (uint16_t)~released;
    } else {
        s_capture_pending = 0;
    }
    std::memset(in, 0, sizeof(*in));
    in->held = s_key_held | overlay_pad_to_shell(pad);
    in->key_pressed = s_key_latched;
    in->pressed = s_key_latched | overlay_pad_to_shell(pad_pressed);
    in->pad_pressed = pad_pressed;
    s_build_keys = s_key_held | s_key_latched;
    s_key_latched = 0;
    s_frame_keys = s_key_held;
    s_frame_pad = pad;
}

static void overlay_fill_model(rsx_shell_model* m, uint32_t log_lines)
{
    rsx_overlay_backend_capabilities caps;
    rsx_overlay_graphics_settings g;
    std::memset(m, 0, sizeof(*m));
    if (rsx_overlay_get_backend_capabilities(&caps)) {
        m->support_fullscreen = caps.fullscreen;
        m->support_vsync = caps.vsync;
        m->support_window_size = caps.window_size;
    }
    m->window_size_index = 255;
    if (rsx_overlay_get_graphics_settings(&g))
        for (int i = 0; i < RSX_SHELL_WINDOW_SIZE_COUNT; ++i)
            if (rsx_shell_window_sizes[i][0] == g.window_width && rsx_shell_window_sizes[i][1] == g.window_height)
                m->window_size_index = (uint8_t)i;
    m->log_lines = log_lines;
    m->list_visible_rows = 1;
}

/* Newest first; `buf` is split in place. */
static int overlay_log_lines(char* buf, size_t cap, const char** lines, int max)
{
    const char* tmp[RSX_OVERLAY_LOG_ENTRY_COUNT];
    int n = 0, count = 0;
    char* p = buf;
    (void)rsx_overlay_log_snapshot(buf, cap);
    while (*p != '\0' && n < (int)RSX_OVERLAY_LOG_ENTRY_COUNT) {
        char* nl = std::strchr(p, '\n');
        if (nl != nullptr) *nl = '\0';
        if (*p != '\0') tmp[n++] = p;
        if (nl == nullptr) break;
        p = nl + 1;
    }
    for (int i = n - 1; i >= 0 && count < max; --i)
        lines[count++] = tmp[i];
    return count;
}

static void overlay_apply_actions(const rsx_shell_output* out)
{
    for (int i = 0; i < out->count; ++i) {
        const rsx_shell_action* a = &out->actions[i];
        rsx_overlay_graphics_settings g;
        switch (a->kind) {
        case RSX_SHELL_ACT_CLOSE:
            overlay_request_close(overlay_keys_to_pad(s_build_keys));
            break;
        case RSX_SHELL_ACT_QUIT:
            s_quit_requested.store(1);
            overlay_request_close(overlay_keys_to_pad(s_build_keys));
            break;
        case RSX_SHELL_ACT_TOGGLE_FULLSCREEN:
            if (rsx_overlay_get_graphics_settings(&g)) { g.fullscreen = g.fullscreen ? 0 : 1; (void)rsx_overlay_apply_graphics(&g); }
            break;
        case RSX_SHELL_ACT_TOGGLE_VSYNC:
            if (rsx_overlay_get_graphics_settings(&g)) { g.vsync = g.vsync ? 0 : 1; (void)rsx_overlay_apply_graphics(&g); }
            break;
        case RSX_SHELL_ACT_SET_WINDOW_SIZE:
            if (a->a >= 0 && a->a < RSX_SHELL_WINDOW_SIZE_COUNT && rsx_overlay_get_graphics_settings(&g)) {
                g.window_width = rsx_shell_window_sizes[a->a][0];
                g.window_height = rsx_shell_window_sizes[a->a][1];
                (void)rsx_overlay_apply_graphics(&g);
            }
            break;
        case RSX_SHELL_ACT_DEADZONE_STEP: {
            const int dz = s_settings.analog_deadzone;
            /* The slider spans 0..30 %; an older, larger value only moves down. */
            const int next = a->a < 0 ? (dz > 0 ? dz - 1 : 0) : (dz < RSX_SHELL_DEADZONE_MAX ? dz + 1 : dz);
            if (next != dz) {
                s_settings.analog_deadzone = (uint8_t)next;
                rsx_overlay_apply_input_settings();
                s_settings_dirty = 1;
            }
            break;
        }
        case RSX_SHELL_ACT_TOGGLE_VIBRATION:
            s_settings.vibration = s_settings.vibration ? 0 : 1;
            rsx_overlay_apply_input_settings();
            s_settings_dirty = 1;
            break;
        case RSX_SHELL_ACT_REMAP:
            /* remap_assign keeps a bijection only if it starts from one: load
             * already validated the file; re-check (it may also repair graphics
             * fields, which render threads read under this mutex). */
            {
                std::lock_guard<std::mutex> lock(s_graphics_mutex);
                (void)rsx_overlay_settings_validate(&s_settings);
            }
            rsx_shell_remap_assign(s_settings.host_button_to_guest_button, (unsigned)a->a, (unsigned)a->b);
            if (a->b >= 0 && a->b < RSX_SHELL_REMAP_COUNT)
                s_capture_hold.fetch_or((uint16_t)(1u << (unsigned)a->b));
            rsx_overlay_apply_input_settings();
            s_settings_dirty = 1;
            break;
        case RSX_SHELL_ACT_RESTORE_CONTROLS: {
            rsx_overlay_settings d;
            rsx_overlay_settings_defaults(&d);
            std::memcpy(s_settings.host_button_to_guest_button, d.host_button_to_guest_button,
                        sizeof(d.host_button_to_guest_button));
            s_settings.analog_deadzone = d.analog_deadzone;
            s_settings.vibration = d.vibration;
            rsx_overlay_apply_input_settings();
            s_settings_dirty = 1;
            break;
        }
        case RSX_SHELL_ACT_TOGGLE_GAME_SOUND:
            s_settings.game_mute = s_settings.game_mute ? 0 : 1;
            ps3_audio_set_runtime_mute(s_settings.game_mute);
            s_settings_dirty = 1;
            break;
        case RSX_SHELL_ACT_TOGGLE_UI_SOUNDS:
            s_settings.ui_sounds = s_settings.ui_sounds ? 0 : 1;
            s_settings_dirty = 1;
            break;
        case RSX_SHELL_ACT_TOGGLE_UI_HAPTICS:
            s_settings.ui_haptics = s_settings.ui_haptics ? 0 : 1;
            s_settings_dirty = 1;
            break;
        default:
            break;
        }
    }
}

static void overlay_emit_feedback(uint32_t mask)
{
    rsx_overlay_feedback_sink sink;
    void* userdata;
    if (mask == 0 || (!s_settings.ui_sounds && !s_settings.ui_haptics))
        return;
    {
        std::lock_guard<std::mutex> lock(s_feedback_mutex);
        sink = s_feedback_sink;
        userdata = s_feedback_userdata;
    }
    if (sink == nullptr)
        return;
    for (int e = 0; e < RSX_OVERLAY_FEEDBACK_COUNT; ++e)
        if (mask & RSX_OVERLAY_FEEDBACK_BIT(e))
            sink(e, s_settings.ui_sounds, s_settings.ui_haptics, userdata);
}

static void overlay_fill_view(rsx_shell_view* v, const rsx_overlay_snapshot* snapshot,
                              const char* const* lines, int count)
{
    std::time_t now = std::time(nullptr);
    std::tm local;
    std::memset(v, 0, sizeof(*v));
    v->snapshot = snapshot;
    v->metrics_available = s_backend_published.load(std::memory_order_relaxed);
    v->caps_registered = rsx_overlay_get_backend_capabilities(&v->caps);
    (void)rsx_overlay_get_graphics_settings(&v->graphics);
    v->analog_deadzone = s_settings.analog_deadzone;
    v->vibration = s_settings.vibration;
    v->game_mute = s_settings.game_mute;
    v->ui_sounds = s_settings.ui_sounds;
    v->ui_haptics = s_settings.ui_haptics;
    std::memcpy(v->map, s_settings.host_button_to_guest_button, sizeof(v->map));
    v->title = s_title;
#if defined(_WIN32)
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    std::snprintf(v->clock, sizeof(v->clock), "%02d:%02d", local.tm_hour, local.tm_min);
    rsx_overlay_get_controller_status(&v->controller_connected, &v->battery_percent);
    v->log_lines = lines;
    v->log_count = count;
}

void rsx_overlay_build_draw_data(void)
{
    static char logs[RSX_OVERLAY_LOG_SNAPSHOT_CAPACITY]; /* event thread only */
    static const char* log_lines[RSX_OVERLAY_LOG_ENTRY_COUNT];
    rsx_shell_input input;
    rsx_shell_model model;
    rsx_shell_output output;
    rsx_shell_layout layout;
    uint32_t width, height;
    float scale;
    int log_count = 0;

    if (!s_initialized.load() || s_imgui_context == nullptr)
        return;
    ImGui::SetCurrentContext(s_imgui_context);
    const double now = overlay_now();
    rsx_overlay_get_display(&width, &height, &scale);
    {
        const uint64_t opens = s_open_count.load();
        if (opens != s_frame_open_count) {
            /* A pad button held at open is not a press. */
            s_frame_open_count = opens;
            overlay_sync_key_epoch();
            s_prev_pad = s_ui_pad_buttons.load();
            rsx_shell_open(&s_shell, now);
        }
    }
    if (!s_visible.load())
        rsx_shell_close(&s_shell, now);
    overlay_collect_input(&input);
    if (s_shell.phase != RSX_SHELL_CLOSED && s_shell.card == RSX_SHELL_CARD_DEVELOPER)
        log_count = overlay_log_lines(logs, sizeof(logs), log_lines, (int)RSX_OVERLAY_LOG_ENTRY_COUNT);
    overlay_fill_model(&model, (uint32_t)log_count);
    /* Rows a list can show, from the settled (fully open) layout: the rise
     * animation must not change how far a list scrolls. Refit after it so a
     * resize, a backing-scale change or a shorter log keeps the selection
     * on screen. */
    rsx_shell_compute_layout((float)width, (float)height, scale, s_shell.card,
                             rsx_shell_panel_display_rows(&s_shell, &model),
                             rsx_shell_panel_wide(&s_shell), 1.0f, &layout);
    model.list_visible_rows = (uint32_t)layout.panel_visible_rows;
    rsx_shell_refit_list(&s_shell, &model);
    rsx_shell_update(&s_shell, &input, &model, now, &output);
    overlay_apply_actions(&output);
    overlay_emit_feedback(output.feedback);
    s_capture_all.store(s_visible.load() && s_shell.level == RSX_SHELL_LEVEL_CAPTURE);
    if (s_shell.phase == RSX_SHELL_CLOSED && !s_visible.load()) {
        s_render_active.store(0);
        if (s_visible.load())   /* reopened from the pad thread meanwhile */
            s_render_active.store(1);
    }

    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)width, (float)height);
    io.DeltaTime = 1.0f / 60.0f;
    ImGui::NewFrame();
    ImGui::GetDrawListSharedData()->TexUvWhitePixel = overlay_white_uv();
    if (s_shell.phase != RSX_SHELL_CLOSED) {
        rsx_overlay_snapshot snapshot;
        rsx_shell_view view;
        rsx_overlay_snapshot_current(&snapshot);
        overlay_fill_view(&view, &snapshot, log_lines, log_count);
        rsx_shell_compute_layout((float)width, (float)height, scale, s_shell.card,
                                 rsx_shell_panel_display_rows(&s_shell, &model),
                                 rsx_shell_panel_wide(&s_shell), s_shell.open.value, &layout);
        rsx_shell_draw(ImGui::GetForegroundDrawList(), &s_shell, &layout, &s_assets, &view, now);
    }
    ImGui::Render();
    rsx_overlay_copy_imgui_draw_data(ImGui::GetDrawData(), width, height, scale,
        s_shell.phase == RSX_SHELL_CLOSED ? 0.0f : std::fmin(1.0f, std::fmax(0.0f, s_shell.open.value)));
    (void)rsx_overlay_persist_settings();
}

int rsx_overlay_copy_draw_packet(rsx_overlay_draw_packet* out)
{
    if (out == nullptr)
        return 0;
    std::lock_guard<std::mutex> lock(s_draw_packet_mutex);
    *out = s_draw_packet;
    return 1;
}

#ifdef RSX_OVERLAY_TESTING
void rsx_overlay_test_set_clock(double seconds) { s_test_clock = seconds; }
void rsx_overlay_test_set_close_hook(void (*hook)(void)) { s_test_close_hook = hook; }

int rsx_overlay_test_input_down(int code)
{
    const uint32_t bit = overlay_key_to_shell(code);
    if (bit != 0)
        return (s_frame_keys & bit) != 0;
    return (s_frame_pad & (uint16_t)code) != 0;
}

void rsx_overlay_test_shell_state(int* phase, int* level, int* card, int* row, int* list_index)
{
    if (phase) *phase = s_shell.phase;
    if (level) *level = s_shell.level;
    if (card) *card = s_shell.card;
    if (row) *row = s_shell.row;
    if (list_index) *list_index = s_shell.list_index;
}

float rsx_overlay_test_atlas_scale(void) { return s_assets_scale; }

int rsx_overlay_test_backend_metrics_available(void)
{
    return s_backend_published.load(std::memory_order_relaxed);
}

int rsx_overlay_test_apply_graphics(const rsx_overlay_graphics_settings* request)
{
    return rsx_overlay_apply_graphics(request);
}
#endif
