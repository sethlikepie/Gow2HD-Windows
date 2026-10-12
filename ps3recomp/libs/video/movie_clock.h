/*
 * Decisions the cutscene player already depends on, kept pure so a test can
 * call them from a cold start. Decode, the pad device, and Metal present stay
 * outside.
 *
 * The playback count is the 48 kHz sample field the guest compares with
 * picture timestamps (sound stream +0x154, read by FUN_0045b780). A host
 * clock that never lands in that field does not release the next picture.
 */
#ifndef PS3RECOMP_MOVIE_CLOCK_H
#define PS3RECOMP_MOVIE_CLOCK_H

#include <stdint.h>
#include <string.h>

#define MOVIE_SEQ_END_NONE       0
#define MOVIE_SEQ_END_NATURAL    1
#define MOVIE_SEQ_END_SKIP       2
/* Stop a host picture that is an earlier movie. No end-of-movie, no logo. */
#define MOVIE_SEQ_END_DROP_STALE 3

/* 48 kHz samples. 0 when no cutscene is the active sequence, so the value
 * the picture comparison reads does not move on its own. */
static inline uint32_t movie_cutscene_playback_samples(int active,
                                                      uint64_t elapsed_ms)
{
    uint64_t samples;
    if (!active || elapsed_ms == 0) return 0;
    samples = elapsed_ms * 48ull;
    if (samples > 0x7fffffffull) samples = 0x7fffffffull;
    return (uint32_t)samples;
}

/* Picture PTS is 90 kHz (CellVdecPicItem auPts). Eligible once playback
 * samples have reached that timestamp. Picture 0 (pts 0) is eligible at 0;
 * later pictures are not while the sample count stays 0. */
static inline int movie_picture_eligible(uint32_t samples_48k, uint64_t pts90k)
{
    return (uint64_t)samples_48k * 90ull >= pts90k * 48ull;
}

/* How this input ends the open pre-rendered sequence.
 * NATURAL and SKIP are the same guest-visible end of the open video.
 * NONE leaves the sequence alone: the sequence is not open (a sticky end
 * must not arm), the press is the automatic pad, or nothing is playing.
 * DROP_STALE removes an earlier host movie without that end, so the
 * in-engine scene under it is not treated as the cutscene that just finished
 * and the boot-logo queue is not brought back. */
static inline int movie_open_sequence_end(int sequence_open, int host_is_current,
                                         int natural_done, int skip_edge,
                                         int suppress)
{
    if (suppress && skip_edge && !natural_done) return MOVIE_SEQ_END_NONE;
    if (!sequence_open) return MOVIE_SEQ_END_NONE;
    if (!host_is_current) {
        if (skip_edge && !suppress) return MOVIE_SEQ_END_DROP_STALE;
        return MOVIE_SEQ_END_NONE;
    }
    if (natural_done) return MOVIE_SEQ_END_NATURAL;
    if (skip_edge) return MOVIE_SEQ_END_SKIP;
    return MOVIE_SEQ_END_NONE;
}

/* Company logos arm on the intro's end only. A later sequence, or dropping
 * a stale picture, must not start that queue again. */
static inline int movie_end_arms_boot_logos(int later_sequence, int stale_drop)
{
    if (later_sequence || stale_drop) return 0;
    return 1;
}

/* In-game cutscenes must not be finished by the intro timer. Index 0 is the
 * intro; 1 is the first in-game sequence; 2+ are later cutscenes. */
static inline int movie_timebase_done_allowed(int ingame_index)
{
    return ingame_index < 1;
}

static inline const char* movie_clock_basename(const char* path)
{
    const char* b;
    if (!path || !path[0]) return path;
    b = strrchr(path, '/');
#ifdef _WIN32
    {
        const char* b2 = strrchr(b ? b : path, '\\');
        if (b2) b = b2;
    }
#endif
    return b ? b + 1 : path;
}

static inline int movie_clock_has_prefix(const char* path, const char* pfx)
{
    const char* b = movie_clock_basename(path);
    size_t i;
    if (!b || !pfx) return 0;
    for (i = 0; pfx[i]; i++) {
        char a = b[i];
        char c = pfx[i];
        if (!a) return 0;
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (a != c) return 0;
    }
    return 1;
}

static inline int movie_clock_is_intro_logo(const char* path)
{
    return movie_clock_has_prefix(path, "smlogo");
}

static inline int movie_clock_is_introhud(const char* path)
{
    return movie_clock_has_prefix(path, "introhud");
}

/* ingame_index: 0 intro (the logo is the current picture), 1 first in-game
 * opener (introhud may be current, the logo may not), 2+ a later cutscene
 * (neither the logo nor introhud is this sequence's picture). An empty host
 * path means the guest's own pictures are current. */
static inline int movie_host_picture_is_current_path(const char* host_path,
                                                    int sequence_open,
                                                    int ingame_index)
{
    if (!sequence_open) return 0;
    if (!host_path || !host_path[0]) return 1;
    if (ingame_index >= 1 && movie_clock_is_intro_logo(host_path)) return 0;
    if (ingame_index >= 2 && movie_clock_is_introhud(host_path)) return 0;
    return 1;
}

/* Which host file an in-game StartSeq should show.
 * `current_is_earlier` — the latched file is a previous movie (logo, or
 * introhud once a later cutscene has opened).
 * `later` — this is not the first in-game sequence.
 * Returns the caller's pointer, or NULL when the host must not force a
 * picture (guest pictures stay; an earlier movie is not put back). */
static inline const char* movie_ingame_picture(const char* current,
                                              int current_is_earlier,
                                              const char* opener,
                                              int later)
{
    if (current && current[0] && !current_is_earlier) return current;
    if (later) return NULL;
    if (opener && opener[0]) return opener;
    return NULL;
}

/* Sampler / StartSeq publish the open sequence. Index 0 = intro. */
void movie_clock_note(int sequence_open, int ingame_index);
void movie_clock_note_stale(int on);
int  movie_clock_sequence_open(void);
int  movie_clock_ingame_index(void);
/* 1 when this movie end may start the company-logo queue. */
int  movie_clock_arms_logos(void);

#endif /* PS3RECOMP_MOVIE_CLOCK_H */
