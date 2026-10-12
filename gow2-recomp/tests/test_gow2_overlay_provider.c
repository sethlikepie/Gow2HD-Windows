/*
 * Offline test of the GoW2 runtime-overlay provider (Task 6):
 *  - the lock-free movie_io WAD snapshot (movie_io_status.h) driven through
 *    open/read/close like movie_hle.c drives it, including a reader thread
 *    racing the writer;
 *  - gow2_overlay_compose: every missing source stays unavailable, never 0;
 *  - the provider registered with the real overlay core, and the core with no
 *    provider at all.
 * No guest memory, no boot.
 */
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cellPad.h"
#include "../gow2_overlay_provider.h"

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "[FAIL] %s:%d: %s\n", __func__, __LINE__, #expr); \
        return 0; \
    } \
} while (0)

/* --- host symbols the provider reads (normally movie_hle.c / movie_eos_arm.c) */
static movie_io_status_store s_store;
int movie_io_wad_status(movie_io_wad_view* out) { return movie_io_status_read_view(&s_store, out); }

static int s_movie_sampled;
static movie_eos_overlay_view s_movie;
int movie_eos_overlay_state(movie_eos_overlay_view* out)
{
    if (!s_movie_sampled) return 0;
    *out = s_movie;
    return 1;
}

/* rsx_overlay.cpp applies the pad mapping at init. */
void cellPad_set_host_mapping(const uint16_t map[CELL_PAD_HOST_BUTTON_COUNT], uint8_t deadzone)
{
    (void)map;
    (void)deadzone;
}
void cellPad_set_host_vibration(int enabled) { (void)enabled; }

static int test_status_store_lifecycle(void)
{
    movie_io_status_store store;
    movie_io_wad_view view;

    memset(&store, 0, sizeof store);
    memset(&view, 0x5A, sizeof view);
    CHECK(movie_io_status_read_view(&store, &view) == 0);
    CHECK(view.seen == 0 && view.name[0] == '\0');

    movie_io_status_open(&store, 2, "/cache/wad/R_PermA.wad_ps3", 20169344ull);
    CHECK(movie_io_status_read_view(&store, &view) == 1);
    CHECK(strcmp(view.name, "R_PermA.wad_ps3") == 0);
    CHECK(view.open == 1 && view.open_wads == 1 && view.total_opens == 1);
    CHECK(view.size == 20169344ull && view.bytes_read == 0);

    movie_io_status_read(&store, 2, 131072ull);
    movie_io_status_read(&store, 1, 999ull);            /* another slot: ignored */
    CHECK(movie_io_status_read_view(&store, &view) == 1);
    CHECK(view.bytes_read == 131072ull);

    movie_io_status_open(&store, 1, "C:\\cache\\R_LglScA.wad_ps3", 3072ull);
    CHECK(movie_io_status_read_view(&store, &view) == 1);
    CHECK(strcmp(view.name, "R_LglScA.wad_ps3") == 0);
    CHECK(view.open_wads == 2 && view.total_opens == 2);

    movie_io_status_close(&store, 2, 20169344ull);      /* older WAD closes */
    CHECK(movie_io_status_read_view(&store, &view) == 1);
    CHECK(view.open_wads == 1 && view.open == 1 && view.bytes_read == 0);
    movie_io_status_close(&store, 1, 3072ull);
    CHECK(movie_io_status_read_view(&store, &view) == 1);
    CHECK(view.open == 0 && view.open_wads == 0 && view.bytes_read == 3072ull);

    /* A very long name is truncated, still terminated. */
    movie_io_status_open(&store, 0,
        "/x/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.wad_ps3", 1ull);
    CHECK(movie_io_status_read_view(&store, &view) == 1);
    CHECK(strlen(view.name) == MOVIE_IO_STATUS_NAME_CAPACITY - 1u);
    return 1;
}

/* Reader never sees a name from one open with the size of another. */
static movie_io_status_store s_race;
static atomic_int s_race_stop;
static void* race_writer(void* unused)
{
    unsigned i;
    (void)unused;
    for (i = 0; !atomic_load(&s_race_stop); ++i) {
        if (i & 1u)
            movie_io_status_open(&s_race, 0, "/c/AAAAAAAAAAAAAAAAAAAAAAAA.wad_ps3", 1111ull);
        else
            movie_io_status_open(&s_race, 0, "/c/B.wad_ps3", 2222ull);
        movie_io_status_close(&s_race, 0, 7ull);
    }
    return NULL;
}

static int test_status_store_reader_is_coherent(void)
{
    pthread_t writer;
    movie_io_wad_view view;
    int i;

    memset(&s_race, 0, sizeof s_race);
    movie_io_status_open(&s_race, 0, "/c/B.wad_ps3", 2222ull);
    atomic_store(&s_race_stop, 0);
    CHECK(pthread_create(&writer, NULL, race_writer, NULL) == 0);
    for (i = 0; i < 200000; ++i) {
        movie_io_status_read_view(&s_race, &view);
        if (view.size == 1111ull) {
            CHECK(strcmp(view.name, "AAAAAAAAAAAAAAAAAAAAAAAA.wad_ps3") == 0);
        } else {
            CHECK(view.size == 2222ull);
            CHECK(strcmp(view.name, "B.wad_ps3") == 0);
        }
    }
    atomic_store(&s_race_stop, 1);
    pthread_join(writer, NULL);
    return 1;
}

static int test_compose_unavailable_and_available(void)
{
    gow2_overlay_inputs in;
    rsx_overlay_snapshot out;

    memset(&in, 0, sizeof in);
    memset(&out, 0x5A, sizeof out);
    gow2_overlay_compose(&in, &out);
    CHECK(out.wad_state_available == 0 && out.wad_state[0] == '\0');
    CHECK(out.shader_stats_available == 0);
    CHECK(out.shader_hits == 0 && out.shader_failures == 0);

    /* Movie sampled, no WAD seen: movie state shown, WAD marked n/a. */
    in.movie_available = 1;
    in.movie.st620 = 5;
    in.movie.eos_armed = 1;
    gow2_overlay_compose(&in, &out);
    CHECK(out.wad_state_available == 1);
    CHECK(strcmp(out.wad_state, "WAD n/a | movie st620=5 EOS") == 0);

    in.wad_available = 1;
    in.wad.seen = 1;
    in.wad.open = 0;
    in.wad.size = 20169344ull;
    in.wad.bytes_read = 20169344ull;
    strcpy(in.wad.name, "R_PermA.wad_ps3");
    in.movie.done = 1;
    gow2_overlay_compose(&in, &out);
    CHECK(strcmp(out.wad_state,
                 "R_PermA.wad_ps3 19.2 MB, read 19.2 MB (closed) | movie st620=5 EOS done") == 0);

    in.movie_available = 0;
    in.shaders_available = 1;
    in.shaders.hits = 7;
    in.shaders.misses = 3;
    in.shaders.compiles = 2;
    in.shaders.failures = 1;
    gow2_overlay_compose(&in, &out);
    CHECK(strcmp(out.wad_state, "R_PermA.wad_ps3 19.2 MB, read 19.2 MB (closed)") == 0);
    CHECK(out.shader_stats_available == 1);
    CHECK(out.shader_hits == 7 && out.shader_misses == 3);
    CHECK(out.shader_compiles == 2 && out.shader_failures == 1);

    /* Does not touch backend-owned fields. */
    out.fps = 42.0;
    gow2_overlay_compose(&in, &out);
    CHECK(out.fps == 42.0);
    gow2_overlay_compose(NULL, &out);
    gow2_overlay_compose(&in, NULL);
    return 1;
}

static int test_provider_through_core(void)
{
    char path[] = "/tmp/gow2-overlay-provider-XXXXXX";
    int fd = mkstemp(path);
    rsx_overlay_snapshot snapshot;
    rsx_overlay_shader_stats stats = { 11, 5, 4, 2 };

    CHECK(fd >= 0);
    close(fd);
    unlink(path);
    CHECK(rsx_overlay_init(path));

    /* No provider registered: unavailable. */
    rsx_overlay_snapshot_current(&snapshot);
    CHECK(snapshot.wad_state_available == 0 && snapshot.shader_stats_available == 0);

    /* Provider registered, nothing produced yet: still unavailable. */
    memset(&s_store, 0, sizeof s_store);
    s_movie_sampled = 0;
    gow2_overlay_provider_register();
    rsx_overlay_snapshot_current(&snapshot);
    CHECK(snapshot.wad_state_available == 0 && snapshot.shader_stats_available == 0);

    movie_io_status_open(&s_store, 0, "/c/R_PermA.wad_ps3", 1048576ull);
    movie_io_status_read(&s_store, 0, 524288ull);
    s_movie_sampled = 1;
    s_movie.st620 = 11;
    s_movie.eos_armed = 0;
    s_movie.done = 0;
    rsx_overlay_publish_shader_stats(&stats);
    rsx_overlay_snapshot_current(&snapshot);
    CHECK(snapshot.wad_state_available == 1);
    CHECK(strcmp(snapshot.wad_state, "R_PermA.wad_ps3 1.0 MB, read 0.5 MB | movie st620=11") == 0);
    CHECK(snapshot.shader_stats_available == 1);
    CHECK(snapshot.shader_hits == 11 && snapshot.shader_misses == 5);
    CHECK(snapshot.shader_compiles == 4 && snapshot.shader_failures == 2);

    /* The open menu renders these values without touching guest state. */
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_F1, 1, 0));
    CHECK(rsx_overlay_handle_key(RSX_OVERLAY_KEY_F1, 0, 0));
    rsx_overlay_build_draw_data();

    rsx_overlay_set_game_provider(NULL, NULL);
    rsx_overlay_snapshot_current(&snapshot);
    CHECK(snapshot.wad_state_available == 0 && snapshot.shader_stats_available == 0);
    rsx_overlay_shutdown();
    unlink(path);
    return 1;
}

int main(void)
{
    int ok = 1;
    ok &= test_status_store_lifecycle();
    ok &= test_status_store_reader_is_coherent();
    ok &= test_compose_unavailable_and_available();
    ok &= test_provider_through_core();
    printf("test_gow2_overlay_provider: %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
