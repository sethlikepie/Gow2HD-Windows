#include "rsx_overlay_ui_sound.h"

#include <math.h>

#include "rsx_overlay_feedback.h"

typedef struct { float f0, f1, start, dur; } ui_tone;
typedef struct { float total; int count; ui_tone tone[2]; } ui_sound;

static const ui_sound k_sounds[RSX_OVERLAY_FEEDBACK_COUNT] = {
    /* OPEN    */ { 0.180f, 2, { { 660.0f, 660.0f, 0.000f, 0.090f }, { 990.0f, 990.0f, 0.060f, 0.120f } } },
    /* CLOSE   */ { 0.160f, 2, { { 990.0f, 990.0f, 0.000f, 0.080f }, { 660.0f, 660.0f, 0.050f, 0.110f } } },
    /* MOVE    */ { 0.028f, 1, { { 1760.0f, 1760.0f, 0.000f, 0.028f }, { 0, 0, 0, 0 } } },
    /* CONFIRM */ { 0.110f, 2, { { 880.0f, 880.0f, 0.000f, 0.060f }, { 1320.0f, 1320.0f, 0.040f, 0.070f } } },
    /* BACK    */ { 0.080f, 1, { { 740.0f, 520.0f, 0.000f, 0.080f }, { 0, 0, 0, 0 } } },
    /* ERROR   */ { 0.160f, 2, { { 220.0f, 220.0f, 0.000f, 0.060f }, { 220.0f, 220.0f, 0.090f, 0.070f } } },
};

#define UI_PEAK 0.18f
#define UI_LIMIT 0.30f

static float envelope(float t, float dur)
{
    const float attack = 0.003f, release = 0.004f;
    float a, r;
    if (t < 0.0f || t >= dur) return 0.0f;
    a = t < attack ? t / attack : 1.0f;
    r = (dur - t) < release ? (dur - t) / release : 1.0f;
    return a * r * expf(-t / (dur * 0.25f));
}

size_t rsx_overlay_ui_sound_length(int event)
{
    if (event < 0 || event >= RSX_OVERLAY_FEEDBACK_COUNT) return 0;
    return (size_t)lroundf(k_sounds[event].total * (float)RSX_OVERLAY_UI_SOUND_RATE);
}

size_t rsx_overlay_ui_sound_render(int event, float* out, size_t capacity)
{
    const float two_pi = 6.28318530718f;
    size_t n = rsx_overlay_ui_sound_length(event);
    if (out == NULL || n == 0) return n;
    if (n > capacity) n = capacity;
    for (size_t i = 0; i < n; ++i) {
        const float t = (float)i / (float)RSX_OVERLAY_UI_SOUND_RATE;
        float s = 0.0f;
        for (int k = 0; k < k_sounds[event].count; ++k) {
            const ui_tone* tone = &k_sounds[event].tone[k];
            const float lt = t - tone->start;
            if (lt < 0.0f || lt >= tone->dur) continue;
            /* Linear glide f0 -> f1: phase is the integral of the frequency. */
            const float phase = two_pi * (tone->f0 * lt + (tone->f1 - tone->f0) * lt * lt / (2.0f * tone->dur));
            s += envelope(lt, tone->dur) * sinf(phase);
        }
        s *= UI_PEAK;
        out[i] = s > UI_LIMIT ? UI_LIMIT : (s < -UI_LIMIT ? -UI_LIMIT : s);
    }
    return n;
}
