/*
 * Amostrador portatil do objecto do movie player (Task 1 do plano
 * ../ps3recomp/docs/superpowers/plans/2026-07-20-macos-movie-eos-fsm.md).
 *
 * Equivalente POSIX da thread inline que o host de referencia do Windows
 * (ps3recomp/runtime/ppu/tests/boot_main.cpp) arranca no main(). NESTA TASK e
 * SO OBSERVACAO: nao arma o g_movie_eos_ea, nao escreve nada no guest.
 *
 * O arm do canal NATURAL depende de um produtor real de "done"
 * (movie_hle_overlay_done), que no POSIX ainda devolve 0 sempre -- armar sem
 * esse produtor seria forjar progresso do guest. Fica para a Task 3.
 */
#ifndef MOVIE_EOS_ARM_H
#define MOVIE_EOS_ARM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Arranca a thread de amostragem se PS3_TRACE_MOVIEOBJ estiver definido.
 * Sem essa variavel e um no-op total (nem thread se cria), por isso o baseline
 * fica byte a byte igual.
 *
 * Task 3 acrescenta PS3_MOVIE_EOS como segundo gate, quando existir de facto
 * o que armar.
 */
void movie_eos_sampler_start(void);

/*
 * Runtime-overlay view of the movie player, as the sampler thread last saw
 * it. Copied values only (no guest pointer), read without any guest lock.
 * Returns 0 (out untouched) until the sampler has sampled st620 at least once
 * -- e.g. when the sampler is not running.
 */
typedef struct movie_eos_overlay_view {
    uint32_t st620;     /* intro-movie FSM state (obj+0x620) */
    int      eos_armed; /* EOS read-hook armed on obj+0x744 */
    int      done;      /* "movie finished" producer fired (overlay or timer) */
} movie_eos_overlay_view;
int movie_eos_overlay_state(movie_eos_overlay_view* out);

/*
 * Seam de teste: 1 se (obj_ea .. obj_ea+MOVIE_OBJ_SPAN) e' legivel segundo o
 * mapa de commits registado (ppu_guest_range_committed). Substitui o
 * VirtualQuery do host Windows, que nao tem equivalente POSIX barato.
 */
int movie_eos_can_sample(uint32_t obj_ea);

/*
 * Leitura big-endian de 32 bits do espaco do guest, com verificacao de commit
 * ANTES de tocar na memoria. Devolve 1 e preenche *out em caso de sucesso; 0 e
 * deixa *out intacto se o range nao estiver commitado (nesse caso NAO le).
 */
int movie_eos_peek32(uint32_t ea, uint32_t* out);

/* Leitura big-endian de 8 bits, mesmas regras do movie_eos_peek32. */
int movie_eos_peek8(uint32_t ea, uint8_t* out);

/*
 * Politica PURA de arm do read-hook de EOS. Alem das tres condicoes
 * originais do produtor de "done", exige tambem que a FSM do intro-movie ja
 * tenha passado o estado de abertura do vdec (Task 2, 2026-07-21, plano
 * intro-vdec-open-force-wad.md):
 *
 *     return eos_env && eos_ea == 0 && overlay_done != 0
 *            && st620 != 0xFFFFFFFFu && st620 >= 5;   // 5 = MOVIE_STATE_POST_OPEN
 *
 *   eos_env      -- PS3_MOVIE_EOS ligado.
 *   eos_ea       -- valor actual de g_movie_eos_ea (0 = ainda nao armado). A
 *                   guarda ==0 garante um unico arm (one-shot).
 *   overlay_done -- sinal REAL de "filme acabou" (produtor). No Windows vem do
 *                   overlay ffmpeg (movie_hle_overlay_done); no POSIX vem do
 *                   produtor time-based gated por PS3_MOVIE_DONE_MS (ver o .c).
 *   st620        -- estado actual da FSM do intro-movie (obj+0x620), lido pelo
 *                   amostrador. 0xFFFFFFFF e' o sentinela "ainda nao lido" e e'
 *                   sempre rejeitado. O valor 5 e' o que func_002C069C escreve
 *                   DEPOIS de o estado 4 (Open+StartSeq) correr: armar antes
 *                   disso (st620 3 ou 4) fazia o handler do estado 3 saltar
 *                   3->4->5 sem NUNCA despachar o corpo do estado 4, e o
 *                   vdec/WADs nunca abriam -- o bug que este gate fecha.
 *
 * NAO escreve nada: so decide. O sampler e' que arma, e SO quando isto da 1.
 * O caso movie_eos_should_arm(1,0,0,5)==0 (EOS ligado, SEM produtor) e' a
 * armadilha de forja M3 -- inegociavel, com qualquer st620. Exposta para o
 * teste offline bater na funcao REAL, nao numa copia: uma mutacao aqui parte
 * o test_movie_eos_policy.
 *
 * Ja NAO e' a condicao de 3 argumentos byte-a-byte identica ao host Windows
 * (boot_main.cpp:367) -- o gate st620>=5 e' politica adicional desta build
 * macOS/arm64 (ver o comentario junto a PARIDADE COM O WINDOWS no .c).
 */
int movie_eos_should_arm(int eos_env, uint32_t eos_ea, long overlay_done, uint32_t st620);

/*
 * Politica PURA Task 4 (FORCE vs arm EOS). Quando o recipe arma
 * PS3_VDEC_FORCE_SEQDONE_MS (>0), o arm de EOS tem de esperar o SEQDONE
 * (g_vdec_seqdone_fired): se armar +0x744 em st=11 antes do watchdog, o guest
 * faz MovieStop→Close e mata o handle — FORCE nunca loga e WAD nao abre.
 *
 *     return force_ms > 0 && !seqdone_seen;
 *
 * force_ms==0 (FORCE desligado) nunca bloqueia — path natural Windows-like
 * (arm apos overlay/time-based) continua intacto.
 */
int movie_eos_force_blocks_arm(int force_ms, int seqdone_seen);

/*
 * Politica PURA do HLE A3b (stream-complete de audio). Decide se o amostrador
 * deve escrever sessao_audio+0x1B8=1 (o campo que func_0045B2A8 le e que o
 * estado 3 usa para avancar a 4):
 *
 *     enabled && done && st620==3 && h720!=0 && !already_marked
 *
 *   enabled        -- PS3_AUDIO_STREAM_DONE nao e' "0" (default ON).
 *   done           -- mesmo produtor do arm EOS (overlay ou time-based .wav).
 *   st620          -- so no park do estado 3 (antes de Open).
 *   h720           -- handle em obj+0x720 (open real do snd_stream).
 *   already_marked -- one-shot do host.
 *
 * NAO escreve: so decide. M3 (done==0) devolve 0. Exposta para o teste offline.
 */
int movie_audio_should_mark_done(int enabled, long done, uint32_t st620,
                                 uint32_t h720, int already_marked);

/* Reset time-based done + latched interval so a later Play is timed from its
 * own open, not from SmLogo / DONE_MS leftover. */
void movie_done_timebased_reset(void);

/* Latched PS3_MOVIE_DONE_MS interval in ms (0 = off). Recomputed after reset.
 * auto uses the .wav sibling of movie_hle_cache_path(), not the first file. */
long long movie_done_interval_ms(void);

/* 1 when the time-based producer has fired for the current Play. */
long movie_done_timebased_poll(uint32_t st);

/* Write the 48 kHz sample count the guest picture comparison already reads
 * (session+0x154, and the Scream voice position the stream update copies
 * from). Returns that count. Inactive: returns 0 and does not touch memory,
 * so a cutscene that is not playing cannot advance the clock. */
uint32_t movie_cutscene_publish_samples(uint32_t session_ea, int active,
                                       uint64_t elapsed_ms);

/* Resolve the snd_stream handle the way the sampler does, then publish.
 * Returns 0 and writes nothing when the handle does not resolve or the
 * voice poke fails. */
uint32_t movie_cutscene_publish_for_handle(uint32_t handle, int active,
                                          uint64_t elapsed_ms);

/* Picture release. Resolves the open movie's snd_stream and publishes
 * elapsed_ms through movie_cutscene_publish_samples. 0 writes nothing. */
uint32_t movie_cutscene_on_picture(uint64_t elapsed_ms);

#ifdef __cplusplus
}
#endif

#endif /* MOVIE_EOS_ARM_H */
