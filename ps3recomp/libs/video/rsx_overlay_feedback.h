#ifndef PS3RECOMP_RSX_OVERLAY_FEEDBACK_H
#define PS3RECOMP_RSX_OVERLAY_FEEDBACK_H

/* UI feedback events shared by the shell (which emits them), the overlay core
 * (which forwards them to a sink) and the platform players (sound, haptics). */
typedef enum rsx_overlay_feedback_event {
    RSX_OVERLAY_FEEDBACK_OPEN = 0,
    RSX_OVERLAY_FEEDBACK_CLOSE,
    RSX_OVERLAY_FEEDBACK_MOVE,
    RSX_OVERLAY_FEEDBACK_CONFIRM,
    RSX_OVERLAY_FEEDBACK_BACK,
    RSX_OVERLAY_FEEDBACK_ERROR,
    RSX_OVERLAY_FEEDBACK_COUNT
} rsx_overlay_feedback_event;

#define RSX_OVERLAY_FEEDBACK_BIT(e) (1u << (unsigned)(e))

/* Called on the overlay's event thread; must not block. `sounds`/`haptics`
 * are the user's switches at the time of the event. */
typedef void (*rsx_overlay_feedback_sink)(int event, int sounds, int haptics, void* userdata);

#endif
