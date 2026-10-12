/*
 * ps3recomp - Frame dump helper
 *
 * Shared by the debug RSX backends so a presented frame can be inspected
 * without a window server: set PS3_FRAME_DUMP=1 and each present writes
 * frame_<n>.bmp next to the process.
 *
 * BMP is deliberately boring here -- 32-bit uncompressed, no palette, no
 * encoder dependency -- because its only job is to let tests and CI assert on
 * pixels that a real GPU produced.
 */

#ifndef PS3RECOMP_RSX_FRAME_DUMP_H
#define PS3RECOMP_RSX_FRAME_DUMP_H

#include "rsx_commands.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Write w*h pixels of BGRA8 (byte order B,G,R,A -- what both SDL's
 * ARGB8888 and Metal's BGRA8Unorm produce on a little-endian host) to `path`
 * as a top-down 32-bit BMP. Returns 0 on success. */
int rsx_write_bmp_bgra32(const char* path, const u8* pixels, u32 w, u32 h);

/* True when PS3_FRAME_DUMP is set to something other than "0" or empty. */
int rsx_frame_dump_enabled(void);

#ifdef __cplusplus
}
#endif
#endif
