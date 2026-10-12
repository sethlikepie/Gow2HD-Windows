#include "rsx_overlay_log.h"

#include <stdatomic.h>
#include <stdint.h>
#include <string.h>


typedef struct rsx_overlay_log_entry {
    int level;
    char message[RSX_OVERLAY_LOG_MESSAGE_CAPACITY];
} rsx_overlay_log_entry;

static rsx_overlay_log_entry s_entries[RSX_OVERLAY_LOG_ENTRY_COUNT];
static uint64_t s_next_entry;
static atomic_flag s_ring_lock = ATOMIC_FLAG_INIT;

static const char* rsx_overlay_log_level_name(int level)
{
    switch (level) {
    case RSX_OVERLAY_LOG_DEBUG: return "DEBUG";
    case RSX_OVERLAY_LOG_INFO: return "INFO";
    case RSX_OVERLAY_LOG_WARN: return "WARN";
    case RSX_OVERLAY_LOG_ERROR: return "ERROR";
    default: return "LOG";
    }
}

static void rsx_overlay_log_copy_message(char* dst, size_t capacity, const char* source)
{
    size_t length = 0;

    while (source[length] != '\0' && length + 1 < capacity)
        ++length;
    if (source[length] == '\0') {
        memcpy(dst, source, length + 1);
        return;
    }
    if (capacity > 4) {
        memcpy(dst, source, capacity - 4);
        memcpy(dst + capacity - 4, "...", 4);
    } else if (capacity > 0) {
        dst[0] = '\0';
    }
}

void rsx_overlay_log_write(int level, const char* message)
{
    rsx_overlay_log_entry* entry;

    if (message == NULL)
        return;
    /* Hot paths never wait for a logger/UI reader. A contended message is
     * deliberately dropped instead of extending a guest or render frame. */
    if (atomic_flag_test_and_set_explicit(&s_ring_lock, memory_order_acquire))
        return;
    entry = &s_entries[s_next_entry % RSX_OVERLAY_LOG_ENTRY_COUNT];
    entry->level = level;
    rsx_overlay_log_copy_message(entry->message, sizeof(entry->message), message);
    ++s_next_entry;
    atomic_flag_clear_explicit(&s_ring_lock, memory_order_release);
}

size_t rsx_overlay_log_snapshot(char* dst, size_t capacity)
{
    uint64_t first;
    uint64_t current;
    size_t used = 0;

    if (dst == NULL || capacity == 0)
        return 0;
    dst[0] = '\0';
    if (atomic_flag_test_and_set_explicit(&s_ring_lock, memory_order_acquire))
        return 0;

    first = s_next_entry > RSX_OVERLAY_LOG_ENTRY_COUNT
        ? s_next_entry - RSX_OVERLAY_LOG_ENTRY_COUNT : 0;
    /* Walk back from the newest entry to find the oldest one that still fits
     * with everything after it: a short buffer drops the oldest, never the
     * newest. */
    {
        size_t total = 0;
        uint64_t start = s_next_entry;
        while (start > first) {
            const rsx_overlay_log_entry* entry =
                &s_entries[(start - 1) % RSX_OVERLAY_LOG_ENTRY_COUNT];
            size_t needed = strlen(rsx_overlay_log_level_name(entry->level)) +
                            strlen(entry->message) + 4; /* [L] m\n */
            if (total + needed + 1 > capacity)
                break;
            total += needed;
            --start;
        }
        first = start;
    }
    for (current = first; current < s_next_entry; ++current) {
        const rsx_overlay_log_entry* entry =
            &s_entries[current % RSX_OVERLAY_LOG_ENTRY_COUNT];
        const char* level = rsx_overlay_log_level_name(entry->level);
        size_t level_length = strlen(level);
        size_t message_length = strlen(entry->message);
        size_t needed = level_length + message_length + 5; /* [L] m\n + NUL */

        if (needed > capacity - used)
            break;
        dst[used++] = '[';
        memcpy(dst + used, level, level_length);
        used += level_length;
        dst[used++] = ']';
        dst[used++] = ' ';
        memcpy(dst + used, entry->message, message_length);
        used += message_length;
        dst[used++] = '\n';
        dst[used] = '\0';
    }
    atomic_flag_clear_explicit(&s_ring_lock, memory_order_release);
    return used;
}
