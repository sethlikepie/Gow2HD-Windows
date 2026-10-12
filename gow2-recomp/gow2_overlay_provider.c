#include "gow2_overlay_provider.h"

#include <stdio.h>
#include <string.h>

void gow2_overlay_compose(const gow2_overlay_inputs* in, rsx_overlay_snapshot* out)
{
    char wad[64];
    char movie[40];

    if (!in || !out)
        return;
    out->wad_state_available = 0;
    out->wad_state[0] = '\0';
    out->shader_stats_available = 0;
    out->shader_hits = out->shader_misses = 0;
    out->shader_compiles = out->shader_failures = 0;

    wad[0] = '\0';
    movie[0] = '\0';
    if (in->wad_available && in->wad.seen) {
        char name[32];
        snprintf(name, sizeof name, "%s", in->wad.name);
        snprintf(wad, sizeof wad, "%s %.1f MB, read %.1f MB%s", name,
                 in->wad.size / 1048576.0, in->wad.bytes_read / 1048576.0,
                 in->wad.open ? "" : " (closed)");
    }
    if (in->movie_available)
        snprintf(movie, sizeof movie, "movie st620=%u%s%s", (unsigned)in->movie.st620,
                 in->movie.eos_armed ? " EOS" : "", in->movie.done ? " done" : "");
    if (wad[0] || movie[0]) {
        out->wad_state_available = 1;
        snprintf(out->wad_state, sizeof out->wad_state, "%s%s%s",
                 wad[0] ? wad : "WAD n/a", movie[0] ? " | " : "", movie);
    }
    if (in->shaders_available) {
        out->shader_stats_available = 1;
        out->shader_hits = in->shaders.hits;
        out->shader_misses = in->shaders.misses;
        out->shader_compiles = in->shaders.compiles;
        out->shader_failures = in->shaders.failures;
    }
}

void gow2_overlay_provide(rsx_overlay_snapshot* out, void* userdata)
{
    gow2_overlay_inputs in;

    (void)userdata;
    memset(&in, 0, sizeof in);
    in.wad_available = movie_io_wad_status(&in.wad);
    in.movie_available = movie_eos_overlay_state(&in.movie);
    in.shaders_available = rsx_overlay_read_shader_stats(&in.shaders);
    gow2_overlay_compose(&in, out);
}

void gow2_overlay_provider_register(void)
{
    rsx_overlay_set_game_provider(gow2_overlay_provide, NULL);
}
