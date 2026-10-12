#include <pthread.h>
#include <stdio.h>
#include <string.h>

#include "rsx_overlay_log.h"

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "[FAIL] %s:%d: %s\n", __func__, __LINE__, #expr); \
        return 0; \
    } \
} while (0)

static void* concurrent_writer(void* userdata)
{
    int id = *(int*)userdata;
    char message[64];
    int i;

    for (i = 0; i < 1000; ++i) {
        snprintf(message, sizeof(message), "writer-%d entry-%d", id, i);
        rsx_overlay_log_write(RSX_OVERLAY_LOG_INFO, message);
    }
    return 0;
}

static int test_bounded_newest_snapshot(void)
{
    char snapshot[4096];
    size_t n;
    int i;
    char message[32];

    for (i = 0; i < 96; ++i) {
        snprintf(message, sizeof(message), "entry-%02d", i);
        rsx_overlay_log_write(RSX_OVERLAY_LOG_DEBUG, message);
    }
    n = rsx_overlay_log_snapshot(snapshot, sizeof(snapshot));
    CHECK(n > 0 && n < sizeof(snapshot));
    CHECK(strstr(snapshot, "entry-95") != 0);
    CHECK(strstr(snapshot, "entry-00") == 0);
    return 1;
}

static int test_oversized_entry_is_truncated(void)
{
    char message[1024];
    char snapshot[4096];
    size_t n;

    memset(message, 'x', sizeof(message) - 1);
    message[sizeof(message) - 1] = '\0';
    rsx_overlay_log_write(RSX_OVERLAY_LOG_ERROR, message);
    n = rsx_overlay_log_snapshot(snapshot, sizeof(snapshot));
    CHECK(n > 0 && n < sizeof(snapshot));
    CHECK(strstr(snapshot, "[ERROR]") != 0);
    CHECK(strstr(snapshot, "...") != 0);
    return 1;
}

/* Full ring of long entries: a buffer smaller than the whole ring keeps the
 * NEWEST entries (the Logs tab used a 4 KB buffer filled oldest-first, so the
 * most recent warnings were the ones cut off). */
static void write_long_entries(int count)
{
    char message[256];
    int i;

    for (i = 0; i < count; ++i) {
        memset(message, 'y', sizeof(message));
        snprintf(message, 24, "long-%03d-", i);
        message[strlen(message)] = 'y';
        message[190] = '\0';
        rsx_overlay_log_write(RSX_OVERLAY_LOG_WARN, message);
    }
}

static int test_small_buffer_keeps_newest(void)
{
    char snapshot[4096];
    size_t n;

    write_long_entries(200);
    n = rsx_overlay_log_snapshot(snapshot, sizeof(snapshot));
    CHECK(n > 0 && n < sizeof(snapshot));
    CHECK(strstr(snapshot, "long-199-") != 0);
    CHECK(strstr(snapshot, "long-198-") != 0);
    CHECK(strstr(snapshot, "long-136-") == 0);
    /* Whole entries only, oldest first among those kept. */
    CHECK(strncmp(snapshot, "[WARN] long-", 12) == 0);
    CHECK(snapshot[n - 1] == '\n');
    return 1;
}

static int test_full_capacity_holds_whole_ring(void)
{
    static char snapshot[RSX_OVERLAY_LOG_SNAPSHOT_CAPACITY];
    char oversized[1024];
    size_t n;
    int i;

    /* Worst case: 64 entries at the maximum message length and the longest
     * level name. */
    memset(oversized, 'z', sizeof(oversized) - 1);
    oversized[sizeof(oversized) - 1] = '\0';
    for (i = 0; i < 64; ++i)
        rsx_overlay_log_write(RSX_OVERLAY_LOG_ERROR, oversized);
    write_long_entries(1);
    n = rsx_overlay_log_snapshot(snapshot, sizeof(snapshot));
    CHECK(n > 0 && n < sizeof(snapshot));
    CHECK(strstr(snapshot, "long-000-") != 0);
    for (i = 0; i < 64; ++i)
        rsx_overlay_log_write(RSX_OVERLAY_LOG_ERROR, oversized);
    n = rsx_overlay_log_snapshot(snapshot, sizeof(snapshot));
    {
        size_t lines = 0;
        for (size_t k = 0; k < n; ++k)
            lines += snapshot[k] == '\n';
        CHECK(lines == 64);
    }
    return 1;
}

static int test_concurrent_write_and_snapshot(void)
{
    pthread_t threads[4];
    int ids[4] = { 0, 1, 2, 3 };
    char snapshot[4096];
    int i;

    for (i = 0; i < 4; ++i)
        CHECK(pthread_create(&threads[i], 0, concurrent_writer, &ids[i]) == 0);
    for (i = 0; i < 1000; ++i) {
        size_t n = rsx_overlay_log_snapshot(snapshot, sizeof(snapshot));
        CHECK(n < sizeof(snapshot));
        CHECK(snapshot[n] == '\0');
    }
    for (i = 0; i < 4; ++i)
        CHECK(pthread_join(threads[i], 0) == 0);
    CHECK(rsx_overlay_log_snapshot(snapshot, sizeof(snapshot)) > 0);
    return 1;
}

int main(void)
{
    int ok = 1;

    ok &= test_bounded_newest_snapshot();
    ok &= test_oversized_entry_is_truncated();
    ok &= test_small_buffer_keeps_newest();
    ok &= test_full_capacity_holds_whole_ring();
    ok &= test_concurrent_write_and_snapshot();
    printf("test_rsx_overlay_log: %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
