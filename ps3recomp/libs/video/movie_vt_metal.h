/*
 * macOS host movie overlay decode (M0 Task 2 + M4 zero-copy).
 * AVAssetReader → CVPixelBuffer → rsx_metal_movie_frame_cvpixel (preferred)
 * or CPU BGRA via rsx_metal_movie_frame if PS3_MOVIE_FORCE_RGBA=1. No ffmpeg.
 */
#ifndef PS3RECOMP_MOVIE_VT_METAL_H
#define PS3RECOMP_MOVIE_VT_METAL_H

#ifdef __cplusplus
extern "C" {
#endif

/* Start decode+audio thread if PS3_MOVIE_HLE is on and path is a readable .m2v.
 * loop=0: single pass (intro); loop!=0: replay until movie_vt_stop.
 * Sticky overlay_done: does not clear a previous natural EOS (Win parity). */
void movie_vt_start(const char* host_m2v_path, int loop);

/* Cooperative stop + join of the decode thread. */
void movie_vt_stop(void);

/*
 * Ends the cutscene now, as a normal EOS: the guest sees the same end-of-movie
 * it would see at the real end, so its state machine moves on. Returns 1 when
 * a movie was playing (request taken), 0 otherwise. The PS3 game itself has no
 * skip for pre-rendered cutscenes; this is a host feature, like the RPCS3
 * community patch, and PS3_MOVIE_SKIP=0 turns it off.
 */
int movie_vt_request_skip(void);

/* Clear sticky overlay_done so a later single-pass Play can restart VT. */
void movie_vt_clear_overlay_done(void);

/* 1 after a completed single-pass (or end of a loop iteration if we set it). */
long movie_vt_overlay_done(void);

/* One-shot: if PS3_MOVIE_HLE is on and no decode has started yet, pick the
 * first .m2v in PS3_MOVIE_CACHE and start single-pass. Used when the guest
 * never routes the intro through movie_io_open (Mac: only psarc seen). Safe
 * to call every tick. */
void movie_vt_autostart_from_cache(void);

#ifdef __cplusplus
}
#endif

#endif /* PS3RECOMP_MOVIE_VT_METAL_H */
