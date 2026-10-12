#ifndef PS3RECOMP_CELLPAD_MAPPING_H
#define PS3RECOMP_CELLPAD_MAPPING_H

/* Host button map + analog deadzone published by the overlay (any thread)
 * and read by cellPadGetData (guest thread) under one sequence lock, so a
 * reader always gets the map, the deadzone and the identity flag of ONE
 * publication. Header-only so the protocol can be tested on its own. */

#include <stdatomic.h>
#include <stdint.h>
#include <string.h>

#define CELLPAD_MAPPING_BUTTONS 16

/* Test hook: stage 1 = reader between the identity flag and the deadzone,
 * stage 2 = reader after the fields, stage 10 = writer mid-publication. */
#ifndef CELLPAD_MAPPING_TEST_HOOK
#define CELLPAD_MAPPING_TEST_HOOK(stage) ((void)0)
#endif

typedef struct cellpad_mapping {
    atomic_uint sequence;             /* odd while a publication is in progress */
    atomic_flag writer;
    _Atomic(uint16_t) map[CELLPAD_MAPPING_BUTTONS];
    atomic_uchar deadzone;
    atomic_int identity;              /* map[bit] == 1 << bit for every bit */
} cellpad_mapping;

static inline int cellpad_mapping_is_identity(const uint16_t* map)
{
    unsigned bit;
    if (map == 0)
        return 0;
    for (bit = 0; bit < CELLPAD_MAPPING_BUTTONS; ++bit)
        if (map[bit] != (uint16_t)(1u << bit))
            return 0;
    return 1;
}

/* Publishes `map` (NULL keeps the current one) and `deadzone` atomically
 * with respect to cellpad_mapping_snapshot. */
static inline void cellpad_mapping_publish(cellpad_mapping* m, const uint16_t* map, uint8_t deadzone)
{
    unsigned bit;
    while (atomic_flag_test_and_set_explicit(&m->writer, memory_order_acquire))
        ;
    atomic_fetch_add_explicit(&m->sequence, 1u, memory_order_relaxed);
    atomic_thread_fence(memory_order_release);   /* odd sequence before any field */
    if (map != 0) {
        for (bit = 0; bit < CELLPAD_MAPPING_BUTTONS; ++bit) {
            atomic_store_explicit(&m->map[bit], map[bit], memory_order_relaxed);
            if (bit == CELLPAD_MAPPING_BUTTONS / 2)
                CELLPAD_MAPPING_TEST_HOOK(10);
        }
        atomic_store_explicit(&m->identity, cellpad_mapping_is_identity(map), memory_order_relaxed);
    }
    atomic_store_explicit(&m->deadzone, deadzone, memory_order_relaxed);
    atomic_fetch_add_explicit(&m->sequence, 1u, memory_order_release);
    atomic_flag_clear_explicit(&m->writer, memory_order_release);
}

/* One consistent publication: returns its identity flag and deadzone and,
 * when it is not the identity, copies its map into `map_out`. After three
 * torn attempts falls back to the identity map and `fallback_deadzone`
 * (returns 1). */
static inline int cellpad_mapping_snapshot(cellpad_mapping* m, uint16_t map_out[CELLPAD_MAPPING_BUTTONS],
                                           uint8_t* deadzone, uint8_t fallback_deadzone)
{
    unsigned attempt, bit;
    for (attempt = 0; attempt < 3; ++attempt) {
        const unsigned before = atomic_load_explicit(&m->sequence, memory_order_acquire);
        int identity;
        uint8_t dz;
        if (before & 1u)
            continue;
        identity = atomic_load_explicit(&m->identity, memory_order_relaxed);
        CELLPAD_MAPPING_TEST_HOOK(1);
        dz = atomic_load_explicit(&m->deadzone, memory_order_relaxed);
        if (!identity)
            for (bit = 0; bit < CELLPAD_MAPPING_BUTTONS; ++bit)
                map_out[bit] = atomic_load_explicit(&m->map[bit], memory_order_relaxed);
        CELLPAD_MAPPING_TEST_HOOK(2);
        atomic_thread_fence(memory_order_acquire);   /* fields before the re-check */
        if (atomic_load_explicit(&m->sequence, memory_order_relaxed) == before) {
            *deadzone = dz;
            return identity;
        }
    }
    *deadzone = fallback_deadzone;
    return 1;
}

#endif
