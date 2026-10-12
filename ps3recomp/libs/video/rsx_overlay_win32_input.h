#ifndef PS3RECOMP_RSX_OVERLAY_WIN32_INPUT_H
#define PS3RECOMP_RSX_OVERLAY_WIN32_INPUT_H

/* Win32 virtual-key / modifier translation for the overlay. Pure and free of
 * <windows.h> so it is unit-tested on every host; the numeric values are the
 * documented winuser.h VK_* codes. */

#include "rsx_overlay.h"

/* Returns the overlay key for a Win32 virtual-key code, or 0 when the overlay
 * does not own that key (it is then never forwarded to the overlay). */
static inline int rsx_overlay_win32_key(unsigned virtual_key)
{
    switch (virtual_key) {
    case 0x70u: return RSX_OVERLAY_KEY_F1;      /* VK_F1 */
    case 0x71u: return RSX_OVERLAY_KEY_F2;      /* VK_F2 */
    case 0x26u: return RSX_OVERLAY_KEY_UP;      /* VK_UP */
    case 0x28u: return RSX_OVERLAY_KEY_DOWN;    /* VK_DOWN */
    case 0x25u: return RSX_OVERLAY_KEY_LEFT;    /* VK_LEFT */
    case 0x27u: return RSX_OVERLAY_KEY_RIGHT;   /* VK_RIGHT */
    case 0x0Du: return RSX_OVERLAY_KEY_ENTER;   /* VK_RETURN */
    case 0x1Bu: return RSX_OVERLAY_KEY_ESCAPE;  /* VK_ESCAPE */
    default: return 0;
    }
}

/* Callers pass GetKeyState(VK_SHIFT/VK_CONTROL/VK_MENU) < 0. */
static inline unsigned rsx_overlay_win32_modifiers(int shift_down, int ctrl_down, int alt_down)
{
    return (shift_down ? RSX_OVERLAY_MOD_SHIFT : 0u) |
           (ctrl_down ? RSX_OVERLAY_MOD_CTRL : 0u) |
           (alt_down ? RSX_OVERLAY_MOD_ALT : 0u);
}

#endif
