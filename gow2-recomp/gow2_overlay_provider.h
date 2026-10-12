/*
 * GoW2 diagnostics for the runtime overlay (Task 6 of
 * docs/superpowers/plans/2026-09-23-gow2-runtime-overlay.md).
 *
 * The provider copies host-side snapshots only: the movie_io WAD stream
 * (movie_io_status.h), the movie player state the sampler thread already read
 * (movie_eos_arm.h) and the backend's published shader counters. It never
 * reads guest memory, keeps no guest pointer and takes no guest lock; a value
 * without a source stays "unavailable" instead of reading as 0.
 */
#ifndef GOW2_OVERLAY_PROVIDER_H
#define GOW2_OVERLAY_PROVIDER_H

#include "rsx_overlay.h"      /* libs/video on the include path */
#include "movie_io_status.h"
#include "movie_eos_arm.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct gow2_overlay_inputs {
    int wad_available;              /* movie_io saw a WAD this session */
    movie_io_wad_view wad;
    int movie_available;            /* the movie sampler has sampled */
    movie_eos_overlay_view movie;
    int shaders_available;          /* the backend published counters */
    rsx_overlay_shader_stats shaders;
} gow2_overlay_inputs;

/* Pure: fills only the provider-owned snapshot fields from the inputs. */
void gow2_overlay_compose(const gow2_overlay_inputs* in, rsx_overlay_snapshot* out);

/* rsx_overlay provider callback: gathers the inputs and composes. */
void gow2_overlay_provide(rsx_overlay_snapshot* out, void* userdata);

/* Registers gow2_overlay_provide with the overlay core. */
void gow2_overlay_provider_register(void);

#ifdef __cplusplus
}
#endif

#endif
