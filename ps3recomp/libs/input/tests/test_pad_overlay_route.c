/*
 * One physical key, one overlay action: the keyboard reaches the overlay only
 * through native key events (rsx_overlay_handle_key), while the pad path
 * (cellpad_overlay_filter, used by cellPadGetData) feeds the overlay only
 * controller-sourced bits. Links the real overlay core.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cellPad.h"
#include "cellpad_internal.h"
#include "cellpad_overlay.h"
#include "rsx_overlay.h"
#include "rsx_overlay_settings.h"

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "[FAIL] %s:%d: %s\n", __func__, __LINE__, #expr); \
        return 0; \
    } \
} while (0)

static uint16_t s_last_map[CELL_PAD_HOST_BUTTON_COUNT];
void cellPad_set_host_mapping(const uint16_t map[CELL_PAD_HOST_BUTTON_COUNT], uint8_t deadzone)
{
    (void)deadzone;
    memcpy(s_last_map, map, sizeof s_last_map);
}
void cellPad_set_host_vibration(int enabled) { (void)enabled; }

static double s_now = 1000.0;
static void tick(double dt) { s_now += dt; rsx_overlay_test_set_clock(s_now); }
static void settle(void) { tick(1.0); rsx_overlay_build_draw_data(); }

static char s_path[64];

static int init_closed(void)
{
    rsx_overlay_settings settings;
    int fd;

    snprintf(s_path, sizeof s_path, "/tmp/rsx-pad-route-XXXXXX");
    fd = mkstemp(s_path);
    if (fd < 0)
        return 0;
    close(fd);
    rsx_overlay_settings_defaults(&settings);
    settings.overlay_visible = 0;
    if (!rsx_overlay_settings_save(&settings, s_path))
        return 0;
    rsx_overlay_shutdown();
    return rsx_overlay_init(s_path) && !rsx_overlay_visible();
}

static void done(void)
{
    rsx_overlay_shutdown();
    unlink(s_path);
}

/* A frame where only the keyboard holds `bits` (no controller). */
static uint16_t keyboard_frame(uint16_t bits) { return cellpad_overlay_filter(bits, 0); }

static int open_with_f1(void)
{
    if (!rsx_overlay_handle_key(RSX_OVERLAY_KEY_F1, 1, 0) ||
        !rsx_overlay_handle_key(RSX_OVERLAY_KEY_F1, 0, 0))
        return 0;
    tick(0.02);
    rsx_overlay_build_draw_data();
    settle();
    return rsx_overlay_visible();
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

static int test_uninitialized_is_identity(void)
{
    rsx_overlay_shutdown();
    CHECK(cellpad_overlay_filter(CELL_PAD_CTRL_START, 0) == CELL_PAD_CTRL_START);
    CHECK(cellpad_overlay_filter(CELL_PAD_CTRL_START, CELL_PAD_CTRL_START) == CELL_PAD_CTRL_START);
    CHECK(cellpad_overlay_filter(0xFFFF, 0x00F0) == 0xFFFF);
    return 1;
}

/* Closed menu: keyboard Return is START for the game, exactly as before, and
 * causes no overlay action on either channel. */
static int test_closed_keyboard_return_reaches_guest(void)
{
    CHECK(init_closed());
    /* Native key event (SDL/Win32 path): not consumed while closed. */
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_ENTER, 1, 0) == 0);
    /* Pad path: several held frames of the same press. */
    for (int frame = 0; frame < 4; ++frame) {
        CHECK(keyboard_frame(CELL_PAD_CTRL_START) == CELL_PAD_CTRL_START);
        CHECK(!rsx_overlay_visible());
    }
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_ENTER, 0, 0) == 0);
    CHECK(keyboard_frame(0) == 0);
    CHECK(!rsx_overlay_visible());
    /* Other keyboard-mapped buttons are untouched while closed. */
    CHECK(keyboard_frame(CELL_PAD_CTRL_CROSS | CELL_PAD_CTRL_UP) ==
          (CELL_PAD_CTRL_CROSS | CELL_PAD_CTRL_UP));
    done();
    return 1;
}

/* Open menu: one Enter press is one ImGui Enter (native key event) and does
 * not also toggle the menu through the pad path; START never reaches the game. */
static int test_open_keyboard_enter_acts_once(void)
{
    CHECK(init_closed());
    CHECK(open_with_f1());
    tap_key(RSX_OVERLAY_KEY_RIGHT); /* on "Tela": Enter must not close */
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_ENTER, 1, 0) == 1);
    for (int frame = 0; frame < 4; ++frame) {
        CHECK(keyboard_frame(CELL_PAD_CTRL_START) == 0);
        CHECK(rsx_overlay_visible());
    }
    tick(0.02);
    rsx_overlay_build_draw_data();
    CHECK(rsx_overlay_test_input_down(RSX_OVERLAY_KEY_ENTER));
    CHECK(!rsx_overlay_test_input_down(CELL_PAD_CTRL_START));
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_ENTER, 0, 0) == 1);
    CHECK(keyboard_frame(0) == 0);
    tick(0.02);
    rsx_overlay_build_draw_data();
    CHECK(!rsx_overlay_test_input_down(RSX_OVERLAY_KEY_ENTER));
    CHECK(rsx_overlay_visible());
    done();
    return 1;
}

/* Open menu: keyboard arrows drive ImGui once (key events), not a second time
 * as a pad D-pad, and keyboard gameplay buttons do not leak to the game. */
static int test_open_keyboard_arrows_and_buttons(void)
{
    CHECK(init_closed());
    CHECK(open_with_f1());
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_UP, 1, 0) == 1);
    CHECK(keyboard_frame(CELL_PAD_CTRL_UP) == 0);
    rsx_overlay_build_draw_data();
    CHECK(rsx_overlay_test_input_down(RSX_OVERLAY_KEY_UP));
    CHECK(!rsx_overlay_test_input_down(CELL_PAD_CTRL_UP));
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_UP, 0, 0) == 1);
    CHECK(keyboard_frame(CELL_PAD_CTRL_CROSS) == 0);
    /* L3/R3 are outside the menu's reserved set, as for a controller. */
    CHECK(keyboard_frame(CELL_PAD_CTRL_L3) == CELL_PAD_CTRL_L3);
    done();
    return 1;
}

#define CHORD (CELL_PAD_CTRL_SELECT | CELL_PAD_CTRL_START)

/* Controller: a lone Start (GoW2 pause) reaches the game with the menu closed;
 * the Select+Start chord toggles on its rising edge, a held chord does not
 * re-toggle, and the chord is withheld from the game until fully released. */
static int test_controller_chord_toggles(void)
{
    CHECK(init_closed());
    for (int frame = 0; frame < 3; ++frame) {
        CHECK(cellpad_overlay_filter(CELL_PAD_CTRL_START, CELL_PAD_CTRL_START) ==
              CELL_PAD_CTRL_START);
        CHECK(!rsx_overlay_visible());
    }
    CHECK(cellpad_overlay_filter(0, 0) == 0);
    CHECK(cellpad_overlay_filter(CHORD, CHORD) == 0);
    CHECK(rsx_overlay_visible());
    CHECK(cellpad_overlay_filter(CHORD, CHORD) == 0);
    CHECK(rsx_overlay_visible());
    CHECK(cellpad_overlay_filter(CELL_PAD_CTRL_START, CELL_PAD_CTRL_START) == 0);
    CHECK(cellpad_overlay_filter(0, 0) == 0);
    CHECK(rsx_overlay_visible());
    /* Controller D-pad drives the menu through the pad path. */
    CHECK(cellpad_overlay_filter(CELL_PAD_CTRL_DOWN, CELL_PAD_CTRL_DOWN) == 0);
    rsx_overlay_build_draw_data();
    CHECK(rsx_overlay_test_input_down(CELL_PAD_CTRL_DOWN));
    CHECK(cellpad_overlay_filter(0, 0) == 0);
    /* The chord again closes it; nothing reaches the game until released. */
    CHECK(cellpad_overlay_filter(CHORD, CHORD) == 0);
    CHECK(!rsx_overlay_visible());
    CHECK(cellpad_overlay_filter(CELL_PAD_CTRL_START, CELL_PAD_CTRL_START) == 0);
    CHECK(cellpad_overlay_filter(0, 0) == 0);
    CHECK(!rsx_overlay_visible());
    CHECK(cellpad_overlay_filter(CELL_PAD_CTRL_START, CELL_PAD_CTRL_START) ==
          CELL_PAD_CTRL_START);
    CHECK(cellpad_overlay_filter(0, 0) == 0);
    /* Keyboard Return (START) + Tab (SELECT) is not the controller chord. */
    CHECK(cellpad_overlay_filter(CHORD, 0) == CHORD);
    CHECK(!rsx_overlay_visible());
    CHECK(cellpad_overlay_filter(0, 0) == 0);
    done();
    return 1;
}

static uint32_t splat(unsigned v) { return (uint32_t)v * 0x01010101u; }

/* Defaults: the deadzone is 0 everywhere a default is taken, and every axis
 * value 0..255 reaches the guest unchanged -- controller or keyboard/mouse,
 * overlay uninitialized or initialized and closed (pre-overlay behavior). */
static int test_default_axes_byte_identical(void)
{
    rsx_overlay_settings defaults;

    rsx_overlay_settings_defaults(&defaults);
    CHECK(defaults.analog_deadzone == CELL_PAD_HOST_DEADZONE_DEFAULT);
    CHECK(CELL_PAD_HOST_DEADZONE_DEFAULT == 0);
    for (int pass = 0; pass < 2; ++pass) {
        if (pass == 0)
            rsx_overlay_shutdown();
        else
            CHECK(init_closed());
        for (unsigned v = 0; v < 256; ++v) {
            CHECK(cellpad_overlay_axes(0, (uint8_t)v, (uint8_t)v, (uint8_t)v, (uint8_t)v,
                                       CELL_PAD_HOST_AXIS_ALL,
                                       defaults.analog_deadzone) == splat(v));
            CHECK(cellpad_overlay_axes(0, (uint8_t)v, (uint8_t)v, (uint8_t)v, (uint8_t)v,
                                       0, defaults.analog_deadzone) == splat(v));
        }
    }
    /* Byte order is the report-length packing cellPadGetData used before:
     * rx | ry << 8 | lx << 16 | ly << 24. */
    CHECK(cellpad_overlay_axes(0, 0x11, 0x22, 0x33, 0x44, CELL_PAD_HOST_AXIS_ALL, 0) ==
          0x44332211u);
    done();
    return 1;
}

/* A configured deadzone shapes controller sticks only: a small mouse-look
 * delta (right stick) or WASD (left stick) passes unchanged. */
static int test_deadzone_only_controller_axes(void)
{
    const uint8_t dz = 15;
    uint32_t axes;

    CHECK(init_closed());
    /* Controller on both sticks: small deflections are swallowed. */
    CHECK(cellpad_overlay_axes(0, 130, 126, 131, 125, CELL_PAD_HOST_AXIS_ALL, dz) ==
          splat(128));
    /* Mouse-look owns the right stick (RX/RY bits clear), controller the left. */
    axes = cellpad_overlay_axes(0, 130, 126, 131, 125,
                                CELL_PAD_HOST_AXIS_LX | CELL_PAD_HOST_AXIS_LY, dz);
    CHECK((axes & 0xFFu) == 130 && ((axes >> 8) & 0xFFu) == 126);
    CHECK(((axes >> 16) & 0xFFu) == 128 && ((axes >> 24) & 0xFFu) == 128);
    /* Full keyboard deflection stays full; a controller one is rescaled to full. */
    CHECK(cellpad_overlay_axes(0, 255, 0, 255, 0, 0, dz) == 0x00FF00FFu);
    CHECK(cellpad_overlay_axes(0, 255, 0, 255, 0, CELL_PAD_HOST_AXIS_ALL, dz) == 0x00FF00FFu);
    done();
    return 1;
}

/* Open menu: port 0 reports centered sticks (controller, WASD, mouse-look,
 * Windows arrow-keys-as-right-stick alike) and withheld buttons carry no
 * pressure; other ports and the closed menu are untouched. */
static int test_open_menu_holds_analog_and_pressure(void)
{
    uint16_t out;

    CHECK(init_closed());
    CHECK(open_with_f1());
    CHECK(cellpad_overlay_axes(0, 255, 0, 17, 240, CELL_PAD_HOST_AXIS_ALL, 0) == 0x80808080u);
    CHECK(cellpad_overlay_axes(0, 255, 0, 17, 240, 0, 0) == 0x80808080u);
    CHECK(cellpad_overlay_axes(1, 255, 0, 17, 240, CELL_PAD_HOST_AXIS_ALL, 0) == 0xF01100FFu);
    /* Controller CROSS held while the menu is open: withheld, pressure 0. */
    out = cellpad_overlay_filter(CELL_PAD_CTRL_CROSS, CELL_PAD_CTRL_CROSS);
    CHECK((out & CELL_PAD_CTRL_CROSS) == 0);
    CHECK(cellPad_pressure_after_capture(CELL_PAD_CTRL_CROSS, out, CELL_PAD_CTRL_CROSS,
                                         200, 1) == 0);
    /* Keyboard-only L2 (Ctrl) likewise. */
    out = cellpad_overlay_filter(CELL_PAD_CTRL_L2, 0);
    CHECK(cellPad_pressure_after_capture(CELL_PAD_CTRL_L2, out, CELL_PAD_CTRL_L2,
                                         255, 0) == 0);
    CHECK(cellpad_overlay_filter(0, 0) == 0);
    /* Close: sticks and pressure flow again. */
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_F1, 1, 0));
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_F1, 0, 0));
    CHECK(!rsx_overlay_visible());
    CHECK(cellpad_overlay_axes(0, 255, 0, 17, 240, CELL_PAD_HOST_AXIS_ALL, 0) == 0xF01100FFu);
    /* One poll with nothing held releases the close's guard (a button held
     * at the first poll after a close stays the menu's until released). */
    CHECK(cellpad_overlay_filter(0, 0) == 0);
    out = cellpad_overlay_filter(CELL_PAD_CTRL_CROSS, CELL_PAD_CTRL_CROSS);
    CHECK(out == CELL_PAD_CTRL_CROSS);
    CHECK(cellPad_pressure_after_capture(CELL_PAD_CTRL_CROSS, out, CELL_PAD_CTRL_CROSS,
                                         200, 1) == 200);
    done();
    return 1;
}

/* PS/Home is a host-only bit: it toggles the menu on its rising edge and is
 * never a CELL_PAD_CTRL_* bit, so the game cannot see it. */
static int test_home_button_toggles_and_never_reaches_guest(void)
{
    CHECK(init_closed());
    cellpad_overlay_home(1);
    CHECK(rsx_overlay_visible());
    cellpad_overlay_home(1);                 /* held: no repeat toggle */
    CHECK(rsx_overlay_visible());
    CHECK(cellpad_overlay_filter(CELL_PAD_CTRL_CROSS, CELL_PAD_CTRL_CROSS) == 0);
    CHECK(cellpad_overlay_filter(0, 0) == 0);
    cellpad_overlay_home(0);
    cellpad_overlay_home(1);
    CHECK(!rsx_overlay_visible());
    cellpad_overlay_home(0);
    CHECK(cellpad_overlay_filter(0, 0) == 0);          /* releases the close's guard */
    CHECK(cellpad_overlay_filter(CELL_PAD_CTRL_START, CELL_PAD_CTRL_START) == CELL_PAD_CTRL_START);
    CHECK(cellpad_overlay_filter(0, 0) == 0);
    /* Focus loss forgets a held Home: the next report counts as a press. */
    cellpad_overlay_home(1);
    CHECK(rsx_overlay_visible());
    rsx_overlay_focus_lost();
    cellpad_overlay_home(1);
    CHECK(!rsx_overlay_visible());
    cellpad_overlay_home(0);
    /* A disabled overlay ignores Home. */
    rsx_overlay_disable("test: renderer unavailable");
    cellpad_overlay_home(1);
    CHECK(!rsx_overlay_visible());
    done();
    return 1;
}

/* PS/Home toggles the shell but the guest frame never carries it: with R3
 * held (outside the menu's reserved set) and neutral sticks, every frame gives
 * the game exactly R3 and centered axes -- through press, hold, release,
 * a second toggle and a disconnect with a stale Home bit. */
static int test_home_never_reaches_guest_frames(void)
{
    static const uint8_t home[7] = { 0, 1, 1, 0, 1, 0, 0 };
    static const int visible[7] = { 0, 1, 1, 1, 0, 0, 0 };
    u16 map[CELL_PAD_HOST_BUTTON_COUNT], host, guest;
    u32 axes;
    PadHostState hs;

    for (unsigned i = 0; i < CELL_PAD_HOST_BUTTON_COUNT; ++i)
        map[i] = (u16)(1u << i);
    memset(&hs, 0, sizeof hs);
    hs.connected = 1;
    hs.analog_lx = hs.analog_ly = hs.analog_rx = hs.analog_ry = 128;
    hs.controller_axes = CELL_PAD_HOST_AXIS_ALL;
    hs.buttons = hs.controller_buttons = CELL_PAD_CTRL_R3;
    CHECK(init_closed());
    for (int i = 0; i < 7; ++i) {
        hs.host_home = home[i];
        cellpad_overlay_port_frame(0, &hs, map, 0, &host, &guest, &axes);
        CHECK(guest == CELL_PAD_CTRL_R3);
        CHECK(axes == 0x80808080u);
        CHECK(rsx_overlay_visible() == visible[i]);
    }
    /* Disconnected pad with a stale Home bit: no toggle, nothing for the game. */
    hs.connected = 0;
    hs.host_home = 1;
    hs.buttons = hs.controller_buttons = 0;
    cellpad_overlay_port_frame(0, &hs, map, 0, &host, &guest, &axes);
    CHECK(guest == 0 && axes == 0x80808080u && !rsx_overlay_visible());
    /* Other ports never consume Home. */
    hs.connected = 1;
    cellpad_overlay_port_frame(1, &hs, map, 0, &host, &guest, &axes);
    CHECK(!rsx_overlay_visible());
    done();
    return 1;
}

/* A button still held when the menu closes from inside is not a game press. */
static int test_closing_from_menu_withholds_held_buttons(void)
{
    CHECK(init_closed());
    CHECK(open_with_f1());
    CHECK(cellpad_overlay_filter(CELL_PAD_CTRL_CROSS, CELL_PAD_CTRL_CROSS) == 0);
    tick(0.02);
    rsx_overlay_build_draw_data();                     /* Cross on "Voltar ao jogo" */
    CHECK(!rsx_overlay_visible());
    for (int frame = 0; frame < 3; ++frame)
        CHECK(cellpad_overlay_filter(CELL_PAD_CTRL_CROSS, CELL_PAD_CTRL_CROSS) == 0);
    CHECK(cellpad_overlay_filter(0, 0) == 0);
    CHECK(cellpad_overlay_filter(CELL_PAD_CTRL_CROSS, CELL_PAD_CTRL_CROSS) == CELL_PAD_CTRL_CROSS);
    CHECK(cellpad_overlay_filter(0, 0) == 0);
    settle();
    /* Keyboard Enter (START on the pad path) likewise never pauses the game. */
    CHECK(open_with_f1());
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_ENTER, 1, 0) == 1);
    CHECK(keyboard_frame(CELL_PAD_CTRL_START) == 0);
    tick(0.02);
    rsx_overlay_build_draw_data();
    CHECK(!rsx_overlay_visible());
    CHECK(keyboard_frame(CELL_PAD_CTRL_START) == 0);
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_ENTER, 0, 0) == 0);
    CHECK(keyboard_frame(0) == 0);
    CHECK(keyboard_frame(CELL_PAD_CTRL_START) == CELL_PAD_CTRL_START);
    CHECK(keyboard_frame(0) == 0);
    settle();
    /* PS/Home closing while Square is held: the same rule as every close path. */
    CHECK(open_with_f1());
    CHECK(cellpad_overlay_filter(CELL_PAD_CTRL_SQUARE, CELL_PAD_CTRL_SQUARE) == 0);
    cellpad_overlay_home(1);
    CHECK(!rsx_overlay_visible());
    for (int frame = 0; frame < 3; ++frame)
        CHECK(cellpad_overlay_filter(CELL_PAD_CTRL_SQUARE, CELL_PAD_CTRL_SQUARE) == 0);
    cellpad_overlay_home(0);
    CHECK(cellpad_overlay_filter(0, 0) == 0);
    CHECK(cellpad_overlay_filter(CELL_PAD_CTRL_SQUARE, CELL_PAD_CTRL_SQUARE) == CELL_PAD_CTRL_SQUARE);
    CHECK(cellpad_overlay_filter(0, 0) == 0);
    settle();

    /* Production order: the close happens before any pad poll saw the button
     * (SDL reports Return to the pad path only after the pump that already
     * ran handle_key + build). The first poll after the close must still
     * withhold it. */
    /* (1) Keyboard Enter on "Voltar ao jogo". */
    CHECK(open_with_f1());
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_ENTER, 1, 0) == 1);
    tick(0.02);
    rsx_overlay_build_draw_data();
    CHECK(!rsx_overlay_visible());
    for (int frame = 0; frame < 3; ++frame)
        CHECK(keyboard_frame(CELL_PAD_CTRL_START) == 0);
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_ENTER, 0, 0) == 0);
    CHECK(keyboard_frame(0) == 0);
    CHECK(keyboard_frame(CELL_PAD_CTRL_START) == CELL_PAD_CTRL_START);
    CHECK(keyboard_frame(0) == 0);
    settle();
    /* (2) Pad Cross, with the build landing between the guest's two steps
     *     (handle_pad published the press; release_guard not yet called). */
    CHECK(open_with_f1());
    {
        uint16_t guest = CELL_PAD_CTRL_CROSS;
        (void)rsx_overlay_handle_pad(CELL_PAD_CTRL_CROSS, &guest);
    }
    tick(0.02);
    rsx_overlay_build_draw_data();                     /* closes */
    CHECK(!rsx_overlay_visible());
    CHECK(rsx_overlay_release_guard(CELL_PAD_CTRL_CROSS) == CELL_PAD_CTRL_CROSS);
    CHECK(cellpad_overlay_filter(CELL_PAD_CTRL_CROSS, CELL_PAD_CTRL_CROSS) == 0);
    CHECK(cellpad_overlay_filter(0, 0) == 0);
    CHECK(cellpad_overlay_filter(CELL_PAD_CTRL_CROSS, CELL_PAD_CTRL_CROSS) == CELL_PAD_CTRL_CROSS);
    CHECK(cellpad_overlay_filter(0, 0) == 0);
    settle();
    /* (3) Circle at card level, same interleaving. */
    CHECK(open_with_f1());
    {
        uint16_t guest = CELL_PAD_CTRL_CIRCLE;
        (void)rsx_overlay_handle_pad(CELL_PAD_CTRL_CIRCLE, &guest);
    }
    tick(0.02);
    rsx_overlay_build_draw_data();
    CHECK(!rsx_overlay_visible());
    CHECK(cellpad_overlay_filter(CELL_PAD_CTRL_CIRCLE, CELL_PAD_CTRL_CIRCLE) == 0);
    CHECK(cellpad_overlay_filter(0, 0) == 0);
    CHECK(cellpad_overlay_filter(CELL_PAD_CTRL_CIRCLE, CELL_PAD_CTRL_CIRCLE) == CELL_PAD_CTRL_CIRCLE);
    CHECK(cellpad_overlay_filter(0, 0) == 0);
    settle();
    /* (4) Select+Start chord pressed together with Cross (first frame). */
    CHECK(open_with_f1());
    CHECK(cellpad_overlay_filter(CHORD | CELL_PAD_CTRL_CROSS, CHORD | CELL_PAD_CTRL_CROSS) == 0);
    CHECK(!rsx_overlay_visible());
    CHECK(cellpad_overlay_filter(CELL_PAD_CTRL_CROSS, CELL_PAD_CTRL_CROSS) == 0);
    CHECK(cellpad_overlay_filter(0, 0) == 0);
    CHECK(cellpad_overlay_filter(CELL_PAD_CTRL_CROSS, CELL_PAD_CTRL_CROSS) == CELL_PAD_CTRL_CROSS);
    CHECK(cellpad_overlay_filter(0, 0) == 0);
    settle();
    /* (5) PS/Home with Triangle first pressed on the close frame itself. */
    CHECK(open_with_f1());
    cellpad_overlay_home(1);
    CHECK(!rsx_overlay_visible());
    CHECK(cellpad_overlay_filter(CELL_PAD_CTRL_TRIANGLE, CELL_PAD_CTRL_TRIANGLE) == 0);
    cellpad_overlay_home(0);
    CHECK(cellpad_overlay_filter(0, 0) == 0);
    CHECK(cellpad_overlay_filter(CELL_PAD_CTRL_TRIANGLE, CELL_PAD_CTRL_TRIANGLE) == CELL_PAD_CTRL_TRIANGLE);
    CHECK(cellpad_overlay_filter(0, 0) == 0);
    done();
    return 1;
}

/* The guard is one atomic word: an arm that lands between two guest polls is
 * never lost, and a guest poll never clears an arm it did not see. */
static int test_release_guard_arm_is_never_lost(void)
{
    CHECK(init_closed());
    CHECK(open_with_f1());
    CHECK(rsx_overlay_release_guard(0) == 0);          /* guest poll: nothing held */
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_F1, 1, 0));   /* arm on the event thread */
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_F1, 0, 0));
    CHECK(rsx_overlay_release_guard(CELL_PAD_CTRL_R1) == CELL_PAD_CTRL_R1);
    CHECK(rsx_overlay_release_guard(CELL_PAD_CTRL_R1 | CELL_PAD_CTRL_L1) == CELL_PAD_CTRL_R1);
    CHECK(rsx_overlay_release_guard(0) == 0);
    CHECK(rsx_overlay_release_guard(CELL_PAD_CTRL_R1) == 0);
    done();
    return 1;
}

/* The button a remap capture just bound (L3, outside the navigation mask)
 * stays with the menu until released, though the menu is still open. */
/* A guest poll that lands INSIDE a close (between its two atomic steps) never
 * lets a held navigation button through, and a poll that sees nothing held
 * there cannot disarm the guard for a key the pad path reports later. */
static int s_close_hook_leaked;
static void close_hook_poll_cross(void)
{
    if (cellpad_overlay_filter(CELL_PAD_CTRL_CROSS, CELL_PAD_CTRL_CROSS) & CELL_PAD_CTRL_CROSS)
        s_close_hook_leaked = 1;
}
static void close_hook_poll_nothing(void)
{
    (void)cellpad_overlay_filter(0, 0);
}

static int test_close_race_never_leaks(void)
{
    CHECK(init_closed());
    CHECK(open_with_f1());
    s_close_hook_leaked = 0;
    rsx_overlay_test_set_close_hook(close_hook_poll_cross);
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_F1, 1, 0));
    rsx_overlay_test_set_close_hook(NULL);
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_F1, 0, 0));
    CHECK(!rsx_overlay_visible());
    CHECK(!s_close_hook_leaked);
    CHECK(cellpad_overlay_filter(CELL_PAD_CTRL_CROSS, CELL_PAD_CTRL_CROSS) == 0);
    CHECK(cellpad_overlay_filter(0, 0) == 0);
    CHECK(cellpad_overlay_filter(CELL_PAD_CTRL_CROSS, CELL_PAD_CTRL_CROSS) == CELL_PAD_CTRL_CROSS);
    CHECK(cellpad_overlay_filter(0, 0) == 0);
    settle();

    CHECK(open_with_f1());
    rsx_overlay_test_set_close_hook(close_hook_poll_nothing);
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_F1, 1, 0));
    rsx_overlay_test_set_close_hook(NULL);
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_F1, 0, 0));
    CHECK(!rsx_overlay_visible());
    /* First poll after the close: the held button is still the menu's. */
    CHECK(cellpad_overlay_filter(CELL_PAD_CTRL_CROSS, CELL_PAD_CTRL_CROSS) == 0);
    CHECK(cellpad_overlay_filter(0, 0) == 0);
    CHECK(cellpad_overlay_filter(CELL_PAD_CTRL_CROSS, CELL_PAD_CTRL_CROSS) == CELL_PAD_CTRL_CROSS);
    CHECK(cellpad_overlay_filter(0, 0) == 0);
    done();
    return 1;
}

/* Keyboard Enter closes the menu through the event channel (SDL/Win32 key
 * events) while the pad path learns about Return on its own channel
 * (GCKeyboard on macOS), in any order. START stays withheld until it has
 * been seen held and then released, whenever it shows up -- and is never
 * stuck if it never shows up. */
static int close_with_keyboard_enter(void)
{
    if (!open_with_f1())
        return 0;
    if (rsx_overlay_handle_key(RSX_OVERLAY_KEY_ENTER, 1, 0) != 1)
        return 0;
    tick(0.02);
    rsx_overlay_build_draw_data();                     /* "Voltar ao jogo" */
    if (rsx_overlay_visible())
        return 0;
    return rsx_overlay_handle_key(RSX_OVERLAY_KEY_ENTER, 0, 0) == 0;
}

static int test_keyboard_close_withholds_late_start(void)
{
    CHECK(init_closed());
    /* (a) The first poll after the close already sees START. */
    CHECK(close_with_keyboard_enter());
    CHECK(keyboard_frame(CELL_PAD_CTRL_START) == 0);
    CHECK(keyboard_frame(CELL_PAD_CTRL_START) == 0);
    CHECK(keyboard_frame(0) == 0);
    CHECK(keyboard_frame(CELL_PAD_CTRL_START) == CELL_PAD_CTRL_START);
    CHECK(keyboard_frame(0) == 0);
    settle();
    /* (b) START reaches the pad path only on the 3rd poll. */
    CHECK(close_with_keyboard_enter());
    CHECK(keyboard_frame(0) == 0);
    tick(0.016);
    CHECK(keyboard_frame(0) == 0);
    tick(0.016);
    CHECK(keyboard_frame(CELL_PAD_CTRL_START) == 0);
    tick(0.016);
    CHECK(keyboard_frame(CELL_PAD_CTRL_START) == 0);
    CHECK(keyboard_frame(0) == 0);
    CHECK(keyboard_frame(CELL_PAD_CTRL_START) == CELL_PAD_CTRL_START);
    CHECK(keyboard_frame(0) == 0);
    settle();
    /* (c) START never shows up: released after the timeout, never stuck. */
    CHECK(close_with_keyboard_enter());
    CHECK(keyboard_frame(0) == 0);
    tick(0.3);
    CHECK(keyboard_frame(0) == 0);
    CHECK(keyboard_frame(CELL_PAD_CTRL_START) == CELL_PAD_CTRL_START);
    CHECK(keyboard_frame(0) == 0);
    /* A pad-sourced close records no keyboard bits: a later lone START is the game's. */
    settle();
    CHECK(open_with_f1());
    cellpad_overlay_home(1);
    cellpad_overlay_home(0);
    CHECK(!rsx_overlay_visible());
    CHECK(cellpad_overlay_filter(0, 0) == 0);
    CHECK(keyboard_frame(CELL_PAD_CTRL_START) == CELL_PAD_CTRL_START);
    done();
    return 1;
}

static int test_captured_button_withheld_until_release(void)
{
    CHECK(init_closed());
    CHECK(open_with_f1());
    tap_key(RSX_OVERLAY_KEY_RIGHT);
    tap_key(RSX_OVERLAY_KEY_RIGHT);
    tap_key(RSX_OVERLAY_KEY_ENTER);
    tap_key(RSX_OVERLAY_KEY_DOWN);
    tap_key(RSX_OVERLAY_KEY_DOWN);
    tap_key(RSX_OVERLAY_KEY_ENTER);
    tap_key(RSX_OVERLAY_KEY_ENTER);                    /* capture Select's slot */
    CHECK(cellpad_overlay_filter(CELL_PAD_CTRL_L3, CELL_PAD_CTRL_L3) == 0);
    tick(0.02);
    rsx_overlay_build_draw_data();                     /* bound; capture over */
    CHECK(rsx_overlay_visible());
    for (int frame = 0; frame < 3; ++frame)
        CHECK(cellpad_overlay_filter(CELL_PAD_CTRL_L3, CELL_PAD_CTRL_L3) == 0);
    CHECK(cellpad_overlay_filter(0, 0) == 0);
    CHECK(cellpad_overlay_filter(CELL_PAD_CTRL_L3, CELL_PAD_CTRL_L3) == CELL_PAD_CTRL_L3);
    CHECK(cellpad_overlay_filter(0, 0) == 0);
    done();
    return 1;
}

/* Remap capture vs the Select+Start chord: Select or Start pressed during a
 * capture is bound only on its release, so pressing Select then Start still
 * forms the chord and closes the shell without remapping anything. A lone
 * Select tapped (pressed and released) is captured as usual. */
static int test_remap_capture_keeps_chord(void)
{
    uint16_t before[CELL_PAD_HOST_BUTTON_COUNT];

    CHECK(init_closed());
    CHECK(open_with_f1());
    tap_key(RSX_OVERLAY_KEY_RIGHT);
    tap_key(RSX_OVERLAY_KEY_RIGHT);
    tap_key(RSX_OVERLAY_KEY_ENTER);                    /* Controles */
    tap_key(RSX_OVERLAY_KEY_DOWN);
    tap_key(RSX_OVERLAY_KEY_DOWN);
    tap_key(RSX_OVERLAY_KEY_ENTER);                    /* Remapear botoes */
    for (int i = 0; i < 14; ++i)
        tap_key(RSX_OVERLAY_KEY_DOWN);                 /* Cruz */
    tap_key(RSX_OVERLAY_KEY_ENTER);                    /* capture */
    memcpy(before, s_last_map, sizeof before);
    CHECK(cellpad_overlay_filter(CELL_PAD_CTRL_SELECT, CELL_PAD_CTRL_SELECT) == 0);
    tick(0.02);
    rsx_overlay_build_draw_data();
    CHECK(memcmp(before, s_last_map, sizeof before) == 0); /* not bound yet */
    CHECK(rsx_overlay_visible());
    CHECK(cellpad_overlay_filter(CHORD, CHORD) == 0);      /* chord closes */
    CHECK(!rsx_overlay_visible());
    tick(0.02);
    rsx_overlay_build_draw_data();
    CHECK(cellpad_overlay_filter(0, 0) == 0);
    tick(0.02);
    rsx_overlay_build_draw_data();
    CHECK(memcmp(before, s_last_map, sizeof before) == 0); /* nothing remapped */
    settle();
    /* Reopen, capture again, tap Select alone: bound on release. */
    CHECK(open_with_f1());
    tap_key(RSX_OVERLAY_KEY_RIGHT);
    tap_key(RSX_OVERLAY_KEY_RIGHT);
    tap_key(RSX_OVERLAY_KEY_ENTER);
    tap_key(RSX_OVERLAY_KEY_DOWN);
    tap_key(RSX_OVERLAY_KEY_DOWN);
    tap_key(RSX_OVERLAY_KEY_ENTER);
    for (int i = 0; i < 14; ++i)
        tap_key(RSX_OVERLAY_KEY_DOWN);
    tap_key(RSX_OVERLAY_KEY_ENTER);
    CHECK(cellpad_overlay_filter(CELL_PAD_CTRL_SELECT, CELL_PAD_CTRL_SELECT) == 0);
    tick(0.02);
    rsx_overlay_build_draw_data();
    CHECK(s_last_map[0] == CELL_PAD_CTRL_SELECT);
    CHECK(cellpad_overlay_filter(0, 0) == 0);
    tick(0.02);
    rsx_overlay_build_draw_data();
    CHECK(s_last_map[0] == CELL_PAD_CTRL_CROSS);          /* host Select drives Cruz */
    CHECK(s_last_map[14] == CELL_PAD_CTRL_SELECT);        /* swapped: bijection kept */
    done();
    return 1;
}

/* Remap capture takes any controller button and nothing leaks to the game. */
static int test_remap_capture_takes_any_button(void)
{
    CHECK(init_closed());
    CHECK(open_with_f1());
    tap_key(RSX_OVERLAY_KEY_RIGHT);
    tap_key(RSX_OVERLAY_KEY_RIGHT);
    tap_key(RSX_OVERLAY_KEY_ENTER);                    /* Controles */
    tap_key(RSX_OVERLAY_KEY_DOWN);
    tap_key(RSX_OVERLAY_KEY_DOWN);
    tap_key(RSX_OVERLAY_KEY_ENTER);                    /* Remapear botões */
    for (int i = 0; i < 14; ++i)
        tap_key(RSX_OVERLAY_KEY_DOWN);                 /* Cruz */
    tap_key(RSX_OVERLAY_KEY_ENTER);                    /* capture */
    CHECK(keyboard_frame(CELL_PAD_CTRL_R3) == 0);      /* even non-menu keys are withheld */
    CHECK(cellpad_overlay_filter(CELL_PAD_CTRL_L3, CELL_PAD_CTRL_L3) == 0);
    tick(0.02);
    rsx_overlay_build_draw_data();
    CHECK(s_last_map[1] == CELL_PAD_CTRL_CROSS);       /* host L3 drives Cruz */
    CHECK(s_last_map[14] == CELL_PAD_CTRL_L3);         /* old owner swapped: bijection kept */
    CHECK(cellpad_overlay_filter(0, 0) == 0);
    CHECK(cellpad_overlay_filter(CELL_PAD_CTRL_L3, CELL_PAD_CTRL_L3) == CELL_PAD_CTRL_L3);
    done();
    return 1;
}

static int test_vibration_gate_zeroes_motors(void)
{
    u8 motor[CELL_PAD_ACTUATOR_MAX] = { 1, 200 };
    cellPad_gate_actuators(motor, CELL_PAD_ACTUATOR_MAX, 1);
    CHECK(motor[0] == 1 && motor[1] == 200);
    cellPad_gate_actuators(motor, CELL_PAD_ACTUATOR_MAX, 0);
    CHECK(motor[0] == 0 && motor[1] == 0);
    return 1;
}

/* Fast path (closed, idle shell + identity map) vs the full path: the
 * identity map is recognised, and translating through it is the same as
 * passing no map at all (what cellPadGetData then does, also for pressure). */
static int test_identity_map_fast_translate(void)
{
    u16 map[CELL_PAD_HOST_BUTTON_COUNT];
    for (unsigned i = 0; i < CELL_PAD_HOST_BUTTON_COUNT; ++i)
        map[i] = (u16)(1u << i);
    CHECK(cellPad_host_map_is_identity(map));
    for (uint32_t x = 0; x <= 0xFFFFu; ++x)
        CHECK(cellPad_translate_host_buttons((u16)x, map) == cellPad_translate_host_buttons((u16)x, NULL));
    map[0] = (u16)(1u << 3);
    map[3] = (u16)(1u << 0);
    CHECK(!cellPad_host_map_is_identity(map));
    return 1;
}

static int test_pad_idle_tracks_shell_state(void)
{
    rsx_overlay_shutdown();
    CHECK(rsx_overlay_pad_idle());                     /* uninitialized: nothing to route */
    CHECK(init_closed());
    CHECK(rsx_overlay_pad_idle());
    CHECK(open_with_f1());
    CHECK(!rsx_overlay_pad_idle());                    /* open */
    CHECK(cellpad_overlay_filter(CELL_PAD_CTRL_CROSS, CELL_PAD_CTRL_CROSS) == 0);
    tap_key(RSX_OVERLAY_KEY_F1);                       /* close with Cross held */
    CHECK(!rsx_overlay_visible());
    CHECK(!rsx_overlay_pad_idle());                    /* release guard armed */
    CHECK(cellpad_overlay_filter(CELL_PAD_CTRL_CROSS, CELL_PAD_CTRL_CROSS) == 0);
    CHECK(!rsx_overlay_pad_idle());                    /* still held -> still guarded */
    CHECK(cellpad_overlay_filter(0, 0) == 0);          /* released */
    CHECK(rsx_overlay_pad_idle());
    done();
    return 1;
}

typedef struct { u16 buttons, controller; uint8_t home; } route_step;
typedef struct { u16 host, guest; u32 axes; int visible; } route_out;

/* One scripted session: closed play, keyboard Return, lone Start, chord
 * open, navigation and keyboard bits while open, Home close with Cross held
 * (release guard), release, Home open/close, closed play again. */
static const route_step kRouteSteps[] = {
    { CELL_PAD_CTRL_CROSS, CELL_PAD_CTRL_CROSS, 0 },
    { CELL_PAD_CTRL_START | CELL_PAD_CTRL_UP, CELL_PAD_CTRL_UP, 0 },           /* keyboard Return + pad Up */
    { CELL_PAD_CTRL_START, CELL_PAD_CTRL_START, 0 },                         /* lone Start: the game's */
    { 0, 0, 0 },
    { CELL_PAD_CTRL_SELECT | CELL_PAD_CTRL_START, CELL_PAD_CTRL_SELECT | CELL_PAD_CTRL_START, 0 }, /* open */
    { CELL_PAD_CTRL_SELECT | CELL_PAD_CTRL_START, CELL_PAD_CTRL_SELECT | CELL_PAD_CTRL_START, 0 },
    { CELL_PAD_CTRL_START, CELL_PAD_CTRL_START, 0 },                         /* half released */
    { 0, 0, 0 },
    { CELL_PAD_CTRL_RIGHT, CELL_PAD_CTRL_RIGHT, 0 },
    { CELL_PAD_CTRL_R3 | CELL_PAD_CTRL_LEFT, CELL_PAD_CTRL_R3, 0 },          /* keyboard Left while open */
    { 0, 0, 0 },
    { CELL_PAD_CTRL_CROSS, CELL_PAD_CTRL_CROSS, 1 },                         /* Home: close, Cross held */
    { CELL_PAD_CTRL_CROSS, CELL_PAD_CTRL_CROSS, 0 },
    { CELL_PAD_CTRL_CROSS | CELL_PAD_CTRL_SQUARE, CELL_PAD_CTRL_CROSS | CELL_PAD_CTRL_SQUARE, 0 },
    { CELL_PAD_CTRL_SQUARE, CELL_PAD_CTRL_SQUARE, 0 },
    { 0, 0, 0 },
    { 0, 0, 1 },                                                             /* Home: open */
    { CELL_PAD_CTRL_TRIANGLE, CELL_PAD_CTRL_TRIANGLE, 0 },
    { 0, 0, 1 },                                                             /* Home: close */
    { 0, 0, 0 },
    { CELL_PAD_CTRL_L1 | CELL_PAD_CTRL_R2, CELL_PAD_CTRL_L1 | CELL_PAD_CTRL_R2, 0 },
    { 0xFFFF & (u16)~(CELL_PAD_CTRL_SELECT | CELL_PAD_CTRL_START), 0x00F0, 0 },
    { 0, 0, 0 },
};
#define ROUTE_STEPS ((int)(sizeof kRouteSteps / sizeof kRouteSteps[0]))

static int run_route(int fast, route_out out[ROUTE_STEPS], int* idle_steps)
{
    u16 map[CELL_PAD_HOST_BUTTON_COUNT];
    PadHostState hs;

    for (unsigned i = 0; i < CELL_PAD_HOST_BUTTON_COUNT; ++i)
        map[i] = (u16)(1u << i);
    memset(&hs, 0, sizeof hs);
    hs.connected = 1;
    hs.analog_lx = 40; hs.analog_ly = 200; hs.analog_rx = 128; hs.analog_ry = 90;
    hs.controller_axes = CELL_PAD_HOST_AXIS_ALL;
    s_now = 5000.0;
    rsx_overlay_test_set_clock(s_now);
    CHECK(init_closed());
    *idle_steps = 0;
    for (int i = 0; i < ROUTE_STEPS; ++i) {
        hs.buttons = kRouteSteps[i].buttons;
        hs.controller_buttons = kRouteSteps[i].controller;
        hs.host_home = kRouteSteps[i].home;
        if (fast && rsx_overlay_pad_idle())
            ++*idle_steps;
        cellpad_overlay_port_frame_path(0, &hs, fast ? NULL : map, 10, &out[i].host, &out[i].guest,
                                        &out[i].axes, fast);
        out[i].visible = rsx_overlay_visible();
        tick(0.02);
        rsx_overlay_build_draw_data();
    }
    done();
    return 1;
}

static int test_fast_path_guest_bytes_identical(void)
{
    route_out slow[ROUTE_STEPS], quick[ROUTE_STEPS];
    int idle_slow, idle_fast, opened = 0;
    CHECK(run_route(0, slow, &idle_slow));
    CHECK(run_route(1, quick, &idle_fast));
    for (int i = 0; i < ROUTE_STEPS; ++i) {
        if (slow[i].host != quick[i].host || slow[i].guest != quick[i].guest ||
            slow[i].axes != quick[i].axes || slow[i].visible != quick[i].visible) {
            fprintf(stderr, "step %d: slow %04x/%04x/%08x/%d fast %04x/%04x/%08x/%d\n", i,
                    slow[i].host, slow[i].guest, slow[i].axes, slow[i].visible,
                    quick[i].host, quick[i].guest, quick[i].axes, quick[i].visible);
            CHECK(0);
        }
        opened += slow[i].visible;
    }
    CHECK(opened > 0);                  /* the script really opened the shell */
    CHECK(idle_fast >= 6);              /* and the fast path really ran */
    CHECK(idle_fast < ROUTE_STEPS);     /* but not while open / guarded */
    return 1;
}

int main(void)
{
    int ok = 1;
    rsx_overlay_test_set_clock(s_now);
    ok &= test_uninitialized_is_identity();
    ok &= test_closed_keyboard_return_reaches_guest();
    ok &= test_open_keyboard_enter_acts_once();
    ok &= test_open_keyboard_arrows_and_buttons();
    ok &= test_controller_chord_toggles();
    ok &= test_default_axes_byte_identical();
    ok &= test_deadzone_only_controller_axes();
    ok &= test_open_menu_holds_analog_and_pressure();
    ok &= test_home_button_toggles_and_never_reaches_guest();
    ok &= test_home_never_reaches_guest_frames();
    ok &= test_closing_from_menu_withholds_held_buttons();
    ok &= test_remap_capture_takes_any_button();
    ok &= test_remap_capture_keeps_chord();
    ok &= test_release_guard_arm_is_never_lost();
    ok &= test_close_race_never_leaks();
    ok &= test_keyboard_close_withholds_late_start();
    ok &= test_captured_button_withheld_until_release();
    ok &= test_vibration_gate_zeroes_motors();
    ok &= test_identity_map_fast_translate();
    ok &= test_pad_idle_tracks_shell_state();
    ok &= test_fast_path_guest_bytes_identical();
    if (!ok) {
        fprintf(stderr, "test_pad_overlay_route: FAILED\n");
        return 1;
    }
    printf("test_pad_overlay_route: all passed\n");
    return 0;
}
