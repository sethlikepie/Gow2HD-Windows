/*
 * Framebuffer feedback: a draw samples the colour surface it is writing.
 * RSX allows it (blood/distortion). Metal does not — same MTLTexture as
 * attachment and as fragment texture is undefined.
 */

#ifndef PS3RECOMP_RSX_RT_FEEDBACK_H
#define PS3RECOMP_RSX_RT_FEEDBACK_H

#include "ps3emu/ps3types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 1 when dest colour offset equals a bound texture offset (and dest != 0). */
int rsx_rt_is_feedback(u32 dest_off, u32 tex_off);

#ifdef __cplusplus
}
#endif
#endif
