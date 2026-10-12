/*
 * Dispatch HOSTRES/WAD content hold + force-bind to the active backend.
 * Also owns the post-movie HOSTRES logo queue (real DXT1 pixels from inflate).
 */
#include "rsx_host_content.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#if defined(__APPLE__)
extern void rsx_metal_content_hold(const void* rgba, unsigned w, unsigned h);
extern void rsx_metal_content_hold_clear(void);
extern void rsx_metal_force_bind_rgba(unsigned unit, const void* rgba,
                                      unsigned w, unsigned h);
#elif defined(_WIN32)
extern void rsx_d3d12_content_hold(const void* rgba, unsigned w, unsigned h);
extern void rsx_d3d12_force_bind_rgba(unsigned unit, const void* rgba,
                                      unsigned w, unsigned h);
/* D3D12: clear via empty hold not exported — no-op clear if missing. */
static void rsx_d3d12_content_hold_clear(void) { /* keep last; Windows path TBD */ }
#endif

#define BOOT_LOGO_MAX 6

typedef struct {
    char     name[48];
    uint8_t* rgba;
    unsigned w, h;
    int      after_movie;
} BootLogo;

static BootLogo s_logos[BOOT_LOGO_MAX];
static int      s_n;
static int      s_q_active;   /* post-movie queue running */
static int      s_q_started;  /* one-shot: company logos after INTRO only */
static int      s_q_idx;      /* index into s_logos for current after_movie step */
static uint64_t s_q_t0_ms;
static int      s_q_hold_ms = -1;

static uint64_t mono_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)ts.tv_nsec / 1000000ull;
}

static int queue_on(void)
{
    const char* e = getenv("PS3_BOOT_LOGO_QUEUE");
    /* Default ON (real HOSTRES pixels). Explicit 0 disables. */
    if (e && e[0] == '0') return 0;
    return 1;
}

static int hold_ms(void)
{
    if (s_q_hold_ms < 0) {
        const char* e = getenv("PS3_BOOT_LOGO_MS");
        int v = e && e[0] ? atoi(e) : 2500;
        if (v < 500) v = 500;
        if (v > 15000) v = 15000;
        s_q_hold_ms = v;
    }
    return s_q_hold_ms;
}

void rsx_host_content_hold(const void* rgba, unsigned w, unsigned h)
{
#if defined(__APPLE__)
    rsx_metal_content_hold(rgba, w, h);
#elif defined(_WIN32)
    rsx_d3d12_content_hold(rgba, w, h);
#else
    (void)rgba; (void)w; (void)h;
    static int once;
    if (!once) {
        once = 1;
        fprintf(stderr, "[CONTENT] host hold: no backend on this platform\n");
    }
#endif
}

void rsx_host_content_hold_clear(void)
{
#if defined(__APPLE__)
    rsx_metal_content_hold_clear();
#elif defined(_WIN32)
    rsx_d3d12_content_hold_clear();
#endif
}

void rsx_host_force_bind_rgba(unsigned unit, const void* rgba, unsigned w, unsigned h)
{
#if defined(__APPLE__)
    rsx_metal_force_bind_rgba(unit, rgba, w, h);
#elif defined(_WIN32)
    rsx_d3d12_force_bind_rgba(unit, rgba, w, h);
#else
    (void)unit; (void)rgba; (void)w; (void)h;
#endif
}

void rsx_host_boot_logo_push(const char* name, const void* rgba,
                             unsigned w, unsigned h, int after_movie)
{
    if (!rgba || !w || !h || !queue_on()) return;
    if (s_n >= BOOT_LOGO_MAX) {
        fprintf(stderr, "[CONTENT] boot logo queue full, drop '%s'\n",
                name ? name : "?");
        fflush(stderr);
        return;
    }
    size_t nbytes = (size_t)w * (size_t)h * 4u;
    uint8_t* copy = (uint8_t*)malloc(nbytes);
    if (!copy) return;
    memcpy(copy, rgba, nbytes);
    BootLogo* L = &s_logos[s_n++];
    memset(L, 0, sizeof(*L));
    if (name && name[0]) {
        strncpy(L->name, name, sizeof(L->name) - 1);
    } else {
        snprintf(L->name, sizeof(L->name), "logo_%d", s_n);
    }
    L->rgba = copy;
    L->w = w;
    L->h = h;
    L->after_movie = after_movie ? 1 : 0;
    fprintf(stderr,
            "[CONTENT] boot logo push #%d '%s' %ux%u after_movie=%d\n",
            s_n, L->name, w, h, L->after_movie);
    fflush(stderr);

    if (!after_movie) {
        /* Legal (and any pre-movie fullscreen): sticky hold now. */
        rsx_host_content_hold(copy, w, h);
    }
}

static int next_after_movie_from(int start)
{
    for (int i = start; i < s_n; i++) {
        if (s_logos[i].after_movie)
            return i;
    }
    return -1;
}

/* Strong definition lives in movie_hle.c. Without it, a movie end does not
 * start the company logos (the intro clock is what allows the one arm). */
__attribute__((weak)) int movie_clock_arms_logos(void)
{
    return 0;
}

static int s_hle_session;

void rsx_host_movie_hle_set(int on)
{
    s_hle_session = on ? 1 : 0;
}

int movie_playback_end_arm_logos(void)
{
    if (!movie_clock_arms_logos()) return 0;
    return rsx_host_boot_logo_on_movie_end();
}

int rsx_host_boot_logo_queue_started(void)
{
    return s_q_started;
}

void rsx_metal_movie_playback_end(void)
{
    int was = s_hle_session;
    s_hle_session = 0;
    if (!was) {
        fprintf(stderr, "[CONTENT] movie end — hold unchanged (no HLE session)\n");
        fflush(stderr);
        return;
    }
    if (movie_playback_end_arm_logos()) {
        fprintf(stderr, "[CONTENT] movie end — boot logo queue armed\n");
        fflush(stderr);
        return;
    }
    fprintf(stderr, "[CONTENT] movie end — hold cleared (no legal re-show)\n");
    fflush(stderr);
}

int rsx_host_boot_logo_on_movie_end(void)
{
    if (!queue_on()) return 0;
    /* scep_e / bluepoint are for the Santa Monica intro, not every HLE
     * overlay. A later Play (introhud) ending used to START the queue again. */
    if (s_q_started || s_q_active) return 0;
    int i = next_after_movie_from(0);
    if (i < 0) return 0;
    s_q_started = 1;
    s_q_active = 1;
    s_q_idx = i;
    s_q_t0_ms = mono_ms();
    BootLogo* L = &s_logos[i];
    rsx_host_content_hold(L->rgba, L->w, L->h);
    fprintf(stderr,
            "[CONTENT] boot logo queue START '%s' %ux%u (ms=%d)\n",
            L->name, L->w, L->h, hold_ms());
    fflush(stderr);
    return 1;
}

void rsx_host_boot_logo_tick(void)
{
    if (!s_q_active || !queue_on()) return;
    uint64_t now = mono_ms();
    if (now - s_q_t0_ms < (uint64_t)hold_ms())
        return;
    int next = next_after_movie_from(s_q_idx + 1);
    if (next < 0) {
        s_q_active = 0;
        rsx_host_content_hold_clear();
        fprintf(stderr, "[CONTENT] boot logo queue DONE — hold cleared (guest/menu)\n");
        fflush(stderr);
        return;
    }
    s_q_idx = next;
    s_q_t0_ms = now;
    BootLogo* L = &s_logos[next];
    rsx_host_content_hold(L->rgba, L->w, L->h);
    fprintf(stderr, "[CONTENT] boot logo queue NEXT '%s' %ux%u\n",
            L->name, L->w, L->h);
    fflush(stderr);
}
