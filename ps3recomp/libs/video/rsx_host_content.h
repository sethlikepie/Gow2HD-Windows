/*
 * Portable host content overlay API — dispatches to D3D12 or Metal backend.
 * Call sites (HOSTRES inflate, WAD tex) should use these, not backend-specific names.
 */
#ifndef PS3RECOMP_RSX_HOST_CONTENT_H
#define PS3RECOMP_RSX_HOST_CONTENT_H

#ifdef __cplusplus
extern "C" {
#endif

/* Sticky fullscreen hold (RGBA8888 host). */
void rsx_host_content_hold(const void* rgba, unsigned w, unsigned h);

/* Drop sticky hold (guest present path). */
void rsx_host_content_hold_clear(void);

/* Force native texture unit bind from host RGBA (dimensions may be ≠ 1280×720). */
void rsx_host_force_bind_rgba(unsigned unit, const void* rgba, unsigned w, unsigned h);

/*
 * Boot logo queue (HOSTRES fullscreen DXT1: legal → post-movie company logos).
 * Real game pixels from inflate; sequenced after SmLogo HLE when guest bind
 * stays on the same EA and draws black (VP fail / no UI).
 *
 * after_movie=0 → hold immediately (legal).
 * after_movie=1 → show after movie EOS (scep_e, bluepoint, …).
 * Disable: PS3_BOOT_LOGO_QUEUE=0. Duration: PS3_BOOT_LOGO_MS (default 2500).
 */
void rsx_host_boot_logo_push(const char* name, const void* rgba,
                             unsigned w, unsigned h, int after_movie);
/* Call from movie playback_end when HLE session completed. Returns 1 if queue
 * armed a hold (caller should not clear hold). */
int rsx_host_boot_logo_on_movie_end(void);
/* playback_end's logo step. Calls on_movie_end only when movie_clock_arms_logos
 * is true (intro, index 0). introhud (index 1) returns 0 and does not arm. */
int movie_playback_end_arm_logos(void);
/* 1 after the intro end has started the company-logo queue. */
int rsx_host_boot_logo_queue_started(void);
/* Movie HLE session flag read by rsx_metal_movie_playback_end. */
void rsx_host_movie_hle_set(int on);
/* Shipped end of a host movie. Arms logos only for the intro session. */
void rsx_metal_movie_playback_end(void);
/* 1 only for the Santa Monica intro (index 0). Strong def is movie_hle.c. */
int movie_clock_arms_logos(void);
/* Call once per present to advance timed logos. */
void rsx_host_boot_logo_tick(void);

#ifdef __cplusplus
}
#endif
#endif
