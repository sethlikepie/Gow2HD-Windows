/* audio_mute.c -- PS3_MUTE, o unico sitio que decide se o audio sai.
 *
 * Num ficheiro so' seu (como o gcm_wait_ns.c ao lado) para poder ser testado
 * sem arrastar o cellAudio inteiro, que precisa do loader do PPU. */
#include "cellAudio.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

int ps3_audio_muted(void)
{
    static int muted = -1;
    if (muted < 0) {
        const char* e = getenv("PS3_MUTE");
        /* Convencao do repo: um "0" explicito (ou vazio) NAO muta. Antes era
         * `getenv(...) ? 1 : 0`, por isso PS3_MUTE=0 -- escrito por quem quer
         * som -- deixava o jogo mudo na mesma. */
        muted = (e && e[0] && e[0] != '0') ? 1 : 0;
    }
    return muted;
}

/* Runtime mute (overlay "Som do jogo"): independent of PS3_MUTE, read per
 * block. Both only silence the samples, never the device, so every host
 * thread keeps its timing (see the CoreAudio note in cellAudio.c). */
static atomic_int s_runtime_mute;

void ps3_audio_set_runtime_mute(int muted)
{
    atomic_store_explicit(&s_runtime_mute, muted ? 1 : 0, memory_order_relaxed);
}

int ps3_audio_runtime_muted(void)
{
    return atomic_load_explicit(&s_runtime_mute, memory_order_relaxed);
}

int ps3_audio_output_silenced(void)
{
    return ps3_audio_muted() || ps3_audio_runtime_muted();
}

const float* ps3_audio_gate_block(const float* mixed, float* scratch, size_t floats)
{
    if (!ps3_audio_output_silenced())
        return mixed;
    memset(scratch, 0, floats * sizeof(float));
    return scratch;
}
