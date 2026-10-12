/*
 * cellAudio port mix — the part that has to match libaudio on the PS3.
 *
 * The game writes big-endian IEEE floats into the guest port buffer
 * (SCREAM does it by SPU DMA). RPCS3 mixes those bytes in place
 * (cell_audio_thread::mix, Emu/Cell/Modules/cellAudio.cpp) and publishes
 * the next block index as a big-endian u64 modulo nBlock
 * (*port.index = port.cur_pos, cur_pos = position() % num_blocks).
 *
 * Stereo downmix coefficients are the RPCS3 ones: -3 dB on L/R, center
 * split in half, side and rear at 1/2, LFE dropped (Dolby).
 */
#ifndef PS3RECOMP_AUDIO_PORT_MIX_H
#define PS3RECOMP_AUDIO_PORT_MIX_H

#include "ps3emu/endian.h"

#include <stdint.h>
#include <string.h>

#define AUDIO_MIX_BLOCK_SAMPLES 256
#define AUDIO_MIX_TAG_COUNT 6

/* Same six positions RPCS3 plants with -0.0f (PORT_BUFFER_TAG_*). */
static inline uint32_t audio_mix_tag_pos(uint32_t nch, uint32_t nr)
{
    uint32_t last = nch * AUDIO_MIX_BLOCK_SAMPLES - 1u;
    uint32_t delta = last / (AUDIO_MIX_TAG_COUNT - 1u);
    uint32_t first = last % (AUDIO_MIX_TAG_COUNT - 1u);
    return first + nr * delta;
}

/* -0.0f, not +0.0f. libaudio uses the sign bit as "block not written yet"
 * because (-0.0 == 0.0) is true, so a game that checks for silence is fine. */
static inline int audio_mix_is_neg_zero(float f)
{
    uint32_t u;
    memcpy(&u, &f, 4);
    return u == 0x80000000u;
}

static inline void audio_mix_store_f32_be(void* dst, float f)
{
    uint32_t u;
    memcpy(&u, &f, 4);
    u = ps3_bswap32(u);
    memcpy(dst, &u, 4);
}

/* Host float from the 4 bytes sitting in guest memory (big-endian IEEE). */
static inline float audio_mix_load_be_f32(const void* src)
{
    uint32_t raw;
    memcpy(&raw, src, 4);
    raw = ps3_bswap32(raw);
    float f;
    memcpy(&f, &raw, 4);
    return f;
}

static inline void audio_mix_store_u64_be(void* dst, uint64_t host_value)
{
    uint64_t be = ps3_bswap64(host_value);
    memcpy(dst, &be, 8);
}

static inline uint32_t audio_mix_block_floats(uint32_t nch)
{
    return nch * AUDIO_MIX_BLOCK_SAMPLES;
}

/* Block the hardware will read next. Stays inside 0 .. nBlock-1. */
static inline uint32_t audio_mix_next_block(uint32_t index, uint32_t nblock)
{
    if (nblock == 0) return 0;
    return (index + 1u) % nblock;
}

/* Decode one guest block into host floats. `src` is nch*256 big-endian floats. */
static inline void audio_mix_decode_be_block(float* dst, const void* src, uint32_t nfloats)
{
    const unsigned char* p = (const unsigned char*)src;
    for (uint32_t i = 0; i < nfloats; i++)
        dst[i] = audio_mix_load_be_f32(p + (size_t)i * 4);
}

/*
 * Add one decoded block into an interleaved stereo buffer of
 * AUDIO_MIX_BLOCK_SAMPLES frames. Caller zeros `stereo` first.
 * level <= 0 is treated as unity — a port opened without INITLEVEL
 * passes 0 and libaudio still plays it at full volume.
 */
static inline void audio_mix_add_port(float* stereo, const float* src,
                                      uint32_t nch, float level)
{
    const float minus_3db = 0.707f;
    if (level <= 0.0f) level = 1.0f;
    if (level > 1.0f) level = 1.0f;

    if (nch == 2) {
        for (uint32_t s = 0; s < AUDIO_MIX_BLOCK_SAMPLES; s++) {
            stereo[s * 2 + 0] += src[s * 2 + 0] * level;
            stereo[s * 2 + 1] += src[s * 2 + 1] * level;
        }
        return;
    }
    if (nch != 8) return;

    for (uint32_t s = 0; s < AUDIO_MIX_BLOCK_SAMPLES; s++) {
        const float* in = src + s * 8;
        const float left   = in[0] * level;
        const float right  = in[1] * level;
        const float center = in[2] * level;
        /* in[3] is LFE — not mixed into stereo (RPCS3 / Dolby). */
        const float side_l = in[4] * level;
        const float side_r = in[5] * level;
        const float rear_l = in[6] * level;
        const float rear_r = in[7] * level;
        const float mid = center * 0.5f;
        stereo[s * 2 + 0] += left * minus_3db + mid + side_l * 0.5f + rear_l * 0.5f;
        stereo[s * 2 + 1] += right * minus_3db + mid + side_r * 0.5f + rear_r * 0.5f;
    }
}

#endif /* PS3RECOMP_AUDIO_PORT_MIX_H */
