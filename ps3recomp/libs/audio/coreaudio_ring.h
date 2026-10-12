/*
 * ps3recomp - SPSC (single-producer single-consumer) float ring buffer.
 *
 * Decouples the game's audio push (cellAudio mix thread -> audio_backend_submit)
 * from the CoreAudio render callback's pull, on the macOS AudioUnit backend.
 * One producer writes samples; one consumer reads them; head/tail use C11
 * acquire/release atomics so no lock is needed on the realtime audio thread.
 *
 * Units are interleaved float SAMPLES (a stereo frame = 2 samples). Capacity is
 * whatever the caller provides; keep it a bit larger than one device buffer so
 * brief scheduling jitter doesn't underrun.
 */
#ifndef PS3RECOMP_COREAUDIO_RING_H
#define PS3RECOMP_COREAUDIO_RING_H

#include <stdatomic.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    float*            buf;   /* cap floats, caller-owned */
    uint32_t          cap;   /* number of float slots */
    _Atomic uint32_t  w;     /* write index (producer)  [0, cap) monotonic mod cap */
    _Atomic uint32_t  r;     /* read index  (consumer)  [0, cap) monotonic mod cap */
} ca_ring;

static inline void ca_ring_init(ca_ring* q, float* storage, uint32_t cap_floats) {
    q->buf = storage;
    q->cap = cap_floats;
    atomic_store_explicit(&q->w, 0, memory_order_relaxed);
    atomic_store_explicit(&q->r, 0, memory_order_relaxed);
}

/* Samples currently available to read. */
static inline uint32_t ca_ring_count(const ca_ring* q) {
    uint32_t w = atomic_load_explicit(&q->w, memory_order_acquire);
    uint32_t r = atomic_load_explicit(&q->r, memory_order_acquire);
    return (w - r + q->cap) % q->cap;
}

/* Free space (one slot is kept empty to distinguish full from empty). */
static inline uint32_t ca_ring_space(const ca_ring* q) {
    return q->cap - 1 - ca_ring_count(q);
}

/* Producer: write up to n samples; returns the number actually written
 * (fewer than n if the ring is near full — caller may drop the remainder). */
static inline uint32_t ca_ring_write(ca_ring* q, const float* src, uint32_t n) {
    uint32_t w = atomic_load_explicit(&q->w, memory_order_relaxed);
    uint32_t r = atomic_load_explicit(&q->r, memory_order_acquire);
    uint32_t space = (r - w - 1 + q->cap) % q->cap;
    if (n > space) n = space;
    uint32_t first = q->cap - w;
    if (first > n) first = n;
    memcpy(q->buf + w, src, first * sizeof(float));
    if (n > first) memcpy(q->buf, src + first, (n - first) * sizeof(float));
    atomic_store_explicit(&q->w, (w + n) % q->cap, memory_order_release);
    return n;
}

/* Consumer: read up to n samples into dst; returns the number actually read
 * (fewer than n on underrun — caller fills the rest with silence). */
static inline uint32_t ca_ring_read(ca_ring* q, float* dst, uint32_t n) {
    uint32_t r = atomic_load_explicit(&q->r, memory_order_relaxed);
    uint32_t w = atomic_load_explicit(&q->w, memory_order_acquire);
    uint32_t avail = (w - r + q->cap) % q->cap;
    if (n > avail) n = avail;
    uint32_t first = q->cap - r;
    if (first > n) first = n;
    memcpy(dst, q->buf + r, first * sizeof(float));
    if (n > first) memcpy(dst + first, q->buf, (n - first) * sizeof(float));
    atomic_store_explicit(&q->r, (r + n) % q->cap, memory_order_release);
    return n;
}

#endif /* PS3RECOMP_COREAUDIO_RING_H */
