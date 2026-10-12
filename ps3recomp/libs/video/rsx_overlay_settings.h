#ifndef PS3RECOMP_RSX_OVERLAY_SETTINGS_H
#define PS3RECOMP_RSX_OVERLAY_SETTINGS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RSX_OVERLAY_SETTINGS_VERSION 2u
#define RSX_OVERLAY_SETTINGS_MIN_VERSION 1u /* older files load; missing keys take defaults */
#define RSX_OVERLAY_HOST_BUTTON_COUNT 16u

/* These are host-independent binding identifiers. Backends translate their
 * native key events before they reach the overlay core. */
typedef enum rsx_overlay_toggle_key {
    RSX_OVERLAY_TOGGLE_KEY_F1 = 1,
    RSX_OVERLAY_TOGGLE_KEY_F2 = 2,
} rsx_overlay_toggle_key;

typedef struct rsx_overlay_settings {
    uint32_t version;

    /* enabled controls whether F1 / Select+Start can open the overlay.
     * visible is kept in the file format for compatibility only: the runtime
     * core always starts with the menu closed, ignores it at load and writes
     * it back as 0. */
    uint8_t overlay_enabled;
    uint8_t overlay_visible;
    uint8_t diagnostics_visible;
    uint8_t toggle_key;

    uint32_t window_width;
    uint32_t window_height;
    uint8_t fullscreen;
    uint8_t vsync;
    uint8_t analog_deadzone;

    /* v2: console-shell switches. */
    uint8_t ui_sounds;   /* synthesized UI sounds */
    uint8_t ui_haptics;  /* controller tick on UI navigation */
    uint8_t game_mute;   /* runtime mute of game audio (cellAudio) */
    uint8_t vibration;   /* game rumble reaches the controller */

    uint16_t host_button_to_guest_button[RSX_OVERLAY_HOST_BUTTON_COUNT];
} rsx_overlay_settings;

void rsx_overlay_settings_defaults(rsx_overlay_settings* out);

/* Returns nonzero only when a supported, versioned settings file was loaded.
 * In all failure cases out contains safe defaults. Invalid individual keys in
 * a supported file leave their corresponding default values intact. A null or
 * empty path uses the platform per-user runtime settings location. */
int rsx_overlay_settings_load(rsx_overlay_settings* out, const char* path);

/* Returns nonzero after atomically replacing path. A null or empty path uses
 * the platform per-user runtime settings location. Invalid settings are never
 * written. */
int rsx_overlay_settings_save(const rsx_overlay_settings* settings, const char* path);

/* Normalizes invalid fields back to their defaults. Returns nonzero when the
 * input was already valid. */
int rsx_overlay_settings_validate(rsx_overlay_settings* settings);

/* Launch-time precedence for fullscreen/VSync: an explicit environment value
 * wins ("0" off, any other non-empty value on), then a loaded settings file,
 * then the backend default. Mirrors metal_env_default's parsing. */
static inline int rsx_overlay_resolve_flag(const char* env_value, int settings_loaded,
                                           int saved_value, int backend_default)
{
    if (env_value != NULL && env_value[0] != '\0')
        return env_value[0] != '0';
    if (settings_loaded)
        return saved_value != 0;
    return backend_default != 0;
}

#ifdef __cplusplus
}
#endif

#endif
