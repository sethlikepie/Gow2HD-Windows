/*
 * Company logos (scep_e / bluepoint) arm once after the intro HLE movie.
 * A later overlay (introhud) must not restart that queue.
 */
#include "rsx_host_content.h"
#include "../movie_clock.h"
#include "trace_link_stubs.h"
#include <stdio.h>
#include <string.h>

void cellGcmMainloopNote(const char* why) { (void)why; }
#if defined(__APPLE__)
void movie_vt_start(const char* host_m2v_path, int loop) { (void)host_m2v_path; (void)loop; }
void movie_vt_stop(void) {}
long movie_vt_overlay_done(void) { return 0; }
void movie_vt_autostart_from_cache(void) {}
void movie_vt_clear_overlay_done(void) {}
unsigned char* vm_base = NULL;
uint32_t ppu_vm_size = 0;
void ps3_watch_store_bulk(uint32_t ea, uint32_t len, const char* who)
{ (void)ea; (void)len; (void)who; }
#endif
volatile int g_ps3_rperma_full = 0;

#if defined(__APPLE__)
void rsx_metal_content_hold(const void* rgba, unsigned w, unsigned h)
{ (void)rgba; (void)w; (void)h; }
void rsx_metal_content_hold_clear(void) {}
void rsx_metal_force_bind_rgba(unsigned unit, const void* rgba,
                               unsigned w, unsigned h)
{ (void)unit; (void)rgba; (void)w; (void)h; }
#endif

#define CHECK(cond) do { \
    if (!(cond)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); return 1; } \
} while (0)

int main(void)
{
    static unsigned char px[16];
    memset(px, 0x7F, sizeof px);
    rsx_host_boot_logo_push("scep_e", px, 2, 2, 1);
    rsx_host_boot_logo_push("bluepoint", px, 2, 2, 1);

    /* introhud (index 1) must not consume the one-shot. The intro (index 0)
     * is what rsx_metal_movie_playback_end arms. */
    movie_clock_note_stale(0);
    movie_clock_note(1, 1);
    printf("movie_clock_arms_logos index 1 -> %d\n", movie_clock_arms_logos());
    CHECK(movie_clock_arms_logos() == 0);
    rsx_host_movie_hle_set(1);
    rsx_metal_movie_playback_end();
    CHECK(rsx_host_boot_logo_queue_started() == 0);
    movie_clock_note(1, 0);
    printf("movie_clock_arms_logos index 0 -> %d\n", movie_clock_arms_logos());
    CHECK(movie_clock_arms_logos() == 1);
    rsx_host_movie_hle_set(1);
    rsx_metal_movie_playback_end();
    CHECK(rsx_host_boot_logo_queue_started() == 1);
    printf("rsx_metal_movie_playback_end index 0 started=%d\n",
           rsx_host_boot_logo_queue_started());
    rsx_host_movie_hle_set(1);
    rsx_metal_movie_playback_end();
    CHECK(rsx_host_boot_logo_on_movie_end() == 0); /* second end does not re-arm */
    CHECK(movie_end_arms_boot_logos(0, 0) == 1);
    CHECK(movie_end_arms_boot_logos(1, 0) == 0); /* later movie */
    CHECK(movie_end_arms_boot_logos(0, 1) == 0); /* stale drop */
    CHECK(movie_ingame_picture("/c/introhud.m2v", 1, "/c/introhud.m2v", 1) == NULL);
    CHECK(movie_host_picture_is_current_path("/c/SmLogo_v2.m2v", 1, 2) == 0);
    return 0;
}
