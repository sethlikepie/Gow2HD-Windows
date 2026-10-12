#ifndef PS3RECOMP_RSX_OVERLAY_LOG_H
#define PS3RECOMP_RSX_OVERLAY_LOG_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RSX_OVERLAY_LOG_ENTRY_COUNT 64u
#define RSX_OVERLAY_LOG_MESSAGE_CAPACITY 192u
/* A snapshot buffer of this size always holds the whole ring: per entry
 * "[ERROR] " (8) + message (<= MESSAGE_CAPACITY - 1) + '\n', plus the NUL. */
#define RSX_OVERLAY_LOG_SNAPSHOT_CAPACITY \
    (RSX_OVERLAY_LOG_ENTRY_COUNT * (RSX_OVERLAY_LOG_MESSAGE_CAPACITY + 8u) + 1u)

enum {
    RSX_OVERLAY_LOG_DEBUG = 0,
    RSX_OVERLAY_LOG_INFO = 1,
    RSX_OVERLAY_LOG_WARN = 2,
    RSX_OVERLAY_LOG_ERROR = 3
};

void rsx_overlay_log_write(int level, const char* message);
/* Copies whole entries, oldest first. When capacity cannot hold every entry
 * the OLDEST are dropped, so the newest entry is always present (given room
 * for at least that one). */
size_t rsx_overlay_log_snapshot(char* dst, size_t capacity);

#ifdef __cplusplus
}
#endif

#endif
