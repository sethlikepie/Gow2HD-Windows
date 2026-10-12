/*
 * ps3recomp - PS3_RSX_DUMP_UCODE=<dir> (diagnostic, OFF by default)
 *
 * Writes each distinct fragment/vertex program the runtime binds as raw
 * microcode in RPCS3's shaders_cache/raw format: <fp_hash>.fp = the guest
 * bytes as the RSX reads them, <vp_hash>.vp = little-endian instruction
 * words. Metal backend only (hooked in metal_set_shader, rsx_metal_backend.m)
 * -- the D3D12/Vulkan backends do not call this. The files are game code:
 * keep them out of git (../gow2-recomp/shader_corpus/ is gitignored).
 */
#ifndef PS3RECOMP_RSX_UCODE_DUMP_H
#define PS3RECOMP_RSX_UCODE_DUMP_H

#include "ps3emu/ps3types.h"

#ifdef __cplusplus
extern "C" {
#endif

void rsx_ucode_dump(const u8* fp, u32 fp_bytes, u32 fp_hash,
                    const u32* vp_words, u32 vp_nwords, u32 vp_hash);
void rsx_ucode_dump_reset_for_test(void);

#ifdef __cplusplus
}
#endif
#endif /* PS3RECOMP_RSX_UCODE_DUMP_H */
