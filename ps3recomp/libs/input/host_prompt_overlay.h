#ifndef PS3RECOMP_HOST_PROMPT_OVERLAY_H
#define PS3RECOMP_HOST_PROMPT_OVERLAY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Returns a cached transparent BGRA frame with only the lower-left confirmation
 * glyph replaced. The buffer is owned by this module and remains valid until
 * the next call. Guest WAD textures are never changed. */
const uint8_t* host_prompt_overlay_frame(unsigned* width, unsigned* height);

/* The GoW2 main-menu capture is a compact 180..300-draw RSX frame. This keeps
 * the small glyph out of the much denser gameplay presentation. */
int host_prompt_overlay_is_menu_draw_count(unsigned draws);

#ifdef __cplusplus
}
#endif

#endif
