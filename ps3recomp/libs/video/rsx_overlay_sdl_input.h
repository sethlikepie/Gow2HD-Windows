#ifndef PS3RECOMP_RSX_OVERLAY_SDL_INPUT_H
#define PS3RECOMP_RSX_OVERLAY_SDL_INPUT_H

/* SDL keysym / modifier translation for the overlay (Metal backend event
 * pump). Pure: it only needs SDL's keycode constants, so it is unit-tested
 * headless (libs/video/tests/test_rsx_overlay_metal.m). */

#include <SDL2/SDL_keycode.h>

#include "rsx_overlay.h"

/* Returns the overlay key for an SDL keycode, or 0 when the overlay does not
 * own that key (it then never reaches the overlay). Return with Cmd or Alt
 * held is the fullscreen shortcut and always stays with the backend. */
static inline int rsx_overlay_sdl_key(SDL_Keycode sym, Uint16 mod)
{
    switch (sym) {
    case SDLK_F1: return RSX_OVERLAY_KEY_F1;
    case SDLK_F2: return RSX_OVERLAY_KEY_F2;
    case SDLK_UP: return RSX_OVERLAY_KEY_UP;
    case SDLK_DOWN: return RSX_OVERLAY_KEY_DOWN;
    case SDLK_LEFT: return RSX_OVERLAY_KEY_LEFT;
    case SDLK_RIGHT: return RSX_OVERLAY_KEY_RIGHT;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        return (mod & (KMOD_GUI | KMOD_ALT)) ? 0 : RSX_OVERLAY_KEY_ENTER;
    case SDLK_ESCAPE: return RSX_OVERLAY_KEY_ESCAPE;
    default: return 0;
    }
}

static inline unsigned rsx_overlay_sdl_modifiers(Uint16 mod)
{
    return ((mod & KMOD_SHIFT) ? RSX_OVERLAY_MOD_SHIFT : 0u) |
           ((mod & KMOD_CTRL) ? RSX_OVERLAY_MOD_CTRL : 0u) |
           ((mod & KMOD_ALT) ? RSX_OVERLAY_MOD_ALT : 0u);
}

#endif
