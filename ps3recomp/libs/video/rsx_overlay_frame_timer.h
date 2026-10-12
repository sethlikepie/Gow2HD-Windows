#ifndef PS3RECOMP_RSX_OVERLAY_FRAME_TIMER_H
#define PS3RECOMP_RSX_OVERLAY_FRAME_TIMER_H

/* Present-to-present timing for backend metrics. frame_ms is the measured
 * interval of the latest frame; fps is frames divided by the elapsed time of
 * the last completed averaging window. Both are "unavailable" until measured
 * (frame_ms after the second present, fps_valid after the first window).
 * The caller supplies a monotonic clock in nanoseconds. */

#include <stdint.h>

#define RSX_OVERLAY_FRAME_TIMER_WINDOW_NS 500000000ull

typedef struct rsx_overlay_frame_timer {
    uint64_t last_ns;
    uint64_t window_start_ns;
    uint32_t window_frames;
    int started;
    int frame_ms_valid;
    int fps_valid;
    double frame_ms;
    double fps;
} rsx_overlay_frame_timer;

static inline void rsx_overlay_frame_timer_init(rsx_overlay_frame_timer* timer)
{
    timer->last_ns = 0;
    timer->window_start_ns = 0;
    timer->window_frames = 0;
    timer->started = 0;
    timer->frame_ms_valid = 0;
    timer->fps_valid = 0;
    timer->frame_ms = 0.0;
    timer->fps = 0.0;
}

/* Records one present at now_ns. Returns 1 when a new FPS window completed. */
static inline int rsx_overlay_frame_timer_tick(rsx_overlay_frame_timer* timer, uint64_t now_ns)
{
    uint64_t elapsed;

    if (!timer->started) {
        timer->started = 1;
        timer->last_ns = now_ns;
        timer->window_start_ns = now_ns;
        return 0;
    }
    if (now_ns <= timer->last_ns)
        return 0;
    timer->frame_ms = (double)(now_ns - timer->last_ns) / 1000000.0;
    timer->frame_ms_valid = 1;
    timer->last_ns = now_ns;
    ++timer->window_frames;
    elapsed = now_ns - timer->window_start_ns;
    if (elapsed < RSX_OVERLAY_FRAME_TIMER_WINDOW_NS)
        return 0;
    timer->fps = (double)timer->window_frames * 1000000000.0 / (double)elapsed;
    timer->fps_valid = 1;
    timer->window_frames = 0;
    timer->window_start_ns = now_ns;
    return 1;
}

#endif
