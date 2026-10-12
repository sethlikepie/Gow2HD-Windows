#ifndef PS3RECOMP_CELLPAD_OVERLAY_H
#define PS3RECOMP_CELLPAD_OVERLAY_H

/* Runtime-overlay routing for one pad port's host buttons (cellPadGetData).
 *
 * The keyboard has ONE channel into the overlay: native key events (SDL on
 * macOS, WM_KEYDOWN on Windows -> rsx_overlay_handle_key). The pad backends
 * also fold keyboard keys into the port-0 button mask (Return -> START,
 * arrows -> D-pad on macOS) so the game keeps its keyboard controls. If
 * those bits reached rsx_overlay_handle_pad too, one physical Enter would
 * act twice (activate the item and toggle the menu) and, with the menu
 * closed, Return would open it and strip START from the game.
 *
 * So the overlay only sees controller-sourced bits. Keyboard-only bits pass
 * to the guest untouched while the menu is closed, and are withheld (like the
 * controller's) while it is open, so no menu input leaks into gameplay. */

#include <stdint.h>

#include "cellPad.h"
#include "cellpad_internal.h"
#include "rsx_overlay.h"

static inline uint16_t cellpad_overlay_filter(uint16_t host_buttons,
                                              uint16_t controller_buttons)
{
    uint16_t out = host_buttons;
    const uint16_t keyboard_only = (uint16_t)(host_buttons & (uint16_t)~controller_buttons);

    rsx_overlay_handle_pad((uint16_t)(controller_buttons & host_buttons), &out);
    if (rsx_overlay_visible())
        out &= (uint16_t)~(keyboard_only & rsx_overlay_pad_navigation_mask());
    /* Buttons held when the menu closed from inside stay the menu's until released. */
    out &= (uint16_t)~rsx_overlay_release_guard(host_buttons);
    return out;
}

/* Analog axes of one port as cellPadGetData reports them, packed
 * rx | ry << 8 | lx << 16 | ly << 24. While the menu is open port 0 reports
 * centered sticks, so stick/WASD/mouse-look motion used around the menu never
 * reaches the game. Otherwise the deadzone applies only to the axes flagged
 * in `controller_axes` (CELL_PAD_HOST_AXIS_*); with deadzone 0 every axis is
 * passed through unchanged. */
static inline uint32_t cellpad_overlay_axes(unsigned port, uint8_t rx, uint8_t ry,
                                            uint8_t lx, uint8_t ly,
                                            uint8_t controller_axes, uint8_t deadzone)
{
    if (port == 0 && rsx_overlay_visible())
        return 0x80808080u;
    return (uint32_t)cellPad_host_axis(rx, deadzone, (controller_axes & CELL_PAD_HOST_AXIS_RX) != 0) |
           ((uint32_t)cellPad_host_axis(ry, deadzone, (controller_axes & CELL_PAD_HOST_AXIS_RY) != 0) << 8) |
           ((uint32_t)cellPad_host_axis(lx, deadzone, (controller_axes & CELL_PAD_HOST_AXIS_LX) != 0) << 16) |
           ((uint32_t)cellPad_host_axis(ly, deadzone, (controller_axes & CELL_PAD_HOST_AXIS_LY) != 0) << 24);
}

/* PS/Home: host-only, toggles the overlay; nothing is added to guest buttons. */
static inline void cellpad_overlay_home(int down)
{
    rsx_overlay_handle_home(down);
}

/* One port's host -> guest frame, exactly as cellPadGetData builds it when
 * autostart does not own the port. PS/Home is consumed here (port 0, only
 * from a connected pad) and is never translated into a guest bit.
 * `map` NULL = identity (no translation). With `allow_fast`, a frame without
 * the full controller Select+Start chord skips the filter while the shell is
 * idle (rsx_overlay_pad_idle): the filter would return it unchanged and touch
 * no state, so the guest bytes are the same either way. */
static inline void cellpad_overlay_port_frame_path(unsigned port, const PadHostState* hs,
    const u16* map, u8 deadzone, u16* host_filtered, u16* guest_buttons, u32* axes,
    int allow_fast)
{
    const u16 chord = CELL_PAD_CTRL_SELECT | CELL_PAD_CTRL_START;
    u16 host = hs->buttons;
    if (port == 0) {
        cellpad_overlay_home(hs->connected && hs->host_home);
        if (!allow_fast || (u16)(hs->controller_buttons & host & chord) == chord ||
            !rsx_overlay_pad_idle())
            host = cellpad_overlay_filter(host, hs->controller_buttons);
    }
    *host_filtered = host;
    *guest_buttons = map != 0 ? cellPad_translate_host_buttons(host, map) : host;
    *axes = cellpad_overlay_axes(port, hs->analog_rx, hs->analog_ry, hs->analog_lx,
                                 hs->analog_ly, hs->controller_axes, deadzone);
}

static inline void cellpad_overlay_port_frame(unsigned port, const PadHostState* hs,
    const u16 map[CELL_PAD_HOST_BUTTON_COUNT], u8 deadzone,
    u16* host_filtered, u16* guest_buttons, u32* axes)
{
    cellpad_overlay_port_frame_path(port, hs, map, deadzone, host_filtered, guest_buttons, axes, 0);
}

#endif
