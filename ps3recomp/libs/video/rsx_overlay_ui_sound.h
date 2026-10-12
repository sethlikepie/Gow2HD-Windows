#ifndef PS3RECOMP_RSX_OVERLAY_UI_SOUND_H
#define PS3RECOMP_RSX_OVERLAY_UI_SOUND_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Deterministic mono UI sounds (our own synthesis, no external assets). */
#define RSX_OVERLAY_UI_SOUND_RATE 48000
size_t rsx_overlay_ui_sound_length(int event);
/* Writes min(length, capacity) samples; out == NULL returns the length. */
size_t rsx_overlay_ui_sound_render(int event, float* out, size_t capacity);
#ifdef __cplusplus
}
#endif
#endif
