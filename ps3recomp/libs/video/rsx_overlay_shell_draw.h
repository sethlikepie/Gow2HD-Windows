#ifndef PS3RECOMP_RSX_OVERLAY_SHELL_DRAW_H
#define PS3RECOMP_RSX_OVERLAY_SHELL_DRAW_H

/* Draws the console shell with ImDrawList primitives only (no ImGui widgets).
 * Solid shapes sample the atlas white texel (the core points ImGui's shared
 * white UV at it); text and SF Symbols are atlas quads. Event thread only. */

#include "imgui.h"
#include "rsx_overlay.h"
#include "rsx_overlay_assets.h"
#include "rsx_overlay_shell.h"

typedef struct rsx_shell_view {
    const rsx_overlay_snapshot* snapshot;
    int metrics_available;
    int caps_registered;
    rsx_overlay_backend_capabilities caps;
    rsx_overlay_graphics_settings graphics;
    uint8_t analog_deadzone, vibration, game_mute, ui_sounds, ui_haptics;
    uint16_t map[RSX_SHELL_REMAP_COUNT];
    const char* title;
    char clock[8];
    int controller_connected;
    int battery_percent;          /* -1: hidden */
    const char* const* log_lines; /* newest first */
    int log_count;
} rsx_shell_view;

void rsx_shell_draw(ImDrawList* dl, const rsx_shell* shell, const rsx_shell_layout* layout,
                    const rsx_overlay_assets* assets, const rsx_shell_view* view, double now);

#endif
