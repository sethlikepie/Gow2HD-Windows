#ifndef PS3RECOMP_GCM_FIFO_FAST_H
#define PS3RECOMP_GCM_FIFO_FAST_H

/*
 * FIFO walker hot path: the IO window is committed, words are 4-aligned.
 * Used by rsx_process_fifo_inner so payload decode does not pay vm_read32
 * or the per-word TRACE/STATS/SYNC switch on bulk noinc methods.
 */
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline uint32_t gcm_fifo_load32_be(const unsigned char* base, uint32_t off)
{
    uint32_t raw;
    memcpy(&raw, base + off, 4);
    return __builtin_bswap32(raw);
}

static inline int gcm_fifo_method_is_sync(uint32_t method)
{
    return method == 0x0050u || method == 0x0010u || method == 0x0014u
        || method == 0x1d6cu || method == 0x1d70u || method == 0x1d74u;
}

/* Tight payload: same method for every word, not a NV406E sync, at least
 * two data words (the 450k-index-word frames are noinc DRAW/const bursts). */
static inline int gcm_fifo_use_tight_payload(uint32_t noinc, uint32_t count,
                                             uint32_t method)
{
    return noinc != 0u && count >= 2u && !gcm_fifo_method_is_sync(method);
}

/* FIFO get/JUMP/CALL are offsets in the mapped IO window. A desynced header
 * decoded as JUMP (E419b, SIGBUS 0x7e463664) must not be fed to vm_read32. */
static inline int gcm_fifo_off_in_window(uint32_t off, uint32_t io_size)
{
    return io_size != 0u && off < io_size;
}

#ifdef __cplusplus
}
#endif

#endif
