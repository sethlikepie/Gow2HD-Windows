/*
 * ps3recomp — host movie overlay on macOS (M0 Task 2 + M4 zero-copy)
 *
 * AVURLAsset + AVAssetReader (kCVPixelFormatType_32BGRA) →
 *   preferred: rsx_metal_movie_frame_cvpixel (IOSurface / CVMetalTextureCache)
 *   fallback:  rsx_metal_movie_frame (CPU BGRA) if PS3_MOVIE_FORCE_RGBA=1
 * Spike 297b22d proved SmLogo_v2.m2v (MPEG-2 ES) decodes natively without remux.
 * Sibling .wav via AVAudioPlayer. Gated by PS3_MOVIE_HLE.
 */

#import <AVFoundation/AVFoundation.h>
#import <CoreVideo/CoreVideo.h>
#import <Foundation/Foundation.h>

#include "movie_vt_metal.h"
#include "rsx_metal_backend.h"
#include "movie_clock.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include <dirent.h>
#include <strings.h>

extern int  ppu_giant_lock_held_by_caller(void) __attribute__((weak));
extern void ppu_giant_lock_release(void) __attribute__((weak));
extern void ppu_giant_lock_acquire(void) __attribute__((weak));

static volatile int  s_running;
static volatile long s_overlay_done;
/* Skip request: the player asked to cut this cutscene short (see
 * movie_vt_request_skip). The decode loop stops and reports a normal EOS, so
 * the guest's movie state machine moves on exactly as at the real end. */
static volatile int s_skip_req;
static pthread_t     s_thr;
static char          s_path[1024];
static int           s_loop;
static int           s_thr_live; /* 1 while thread object is joinable */

static AVAudioPlayer* s_wav_player; /* __strong under ARC */
/* Volume last pushed to s_wav_player (-1: none yet for this player). */
static float s_wav_volume = -1.0f;

static int hle_on(void)
{
    const char* e = getenv("PS3_MOVIE_HLE");
    return (e && e[0] && e[0] != '0') ? 1 : 0;
}

static int force_rgba(void)
{
    const char* e = getenv("PS3_MOVIE_FORCE_RGBA");
    return (e && e[0] && e[0] != '0') ? 1 : 0;
}

static int perf_movie(void)
{
    const char* e = getenv("PS3_PERF_MOVIE");
    return (e && e[0] && e[0] != '0') ? 1 : 0;
}

static uint64_t mono_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)ts.tv_nsec / 1000000ull;
}

long movie_vt_overlay_done(void)
{
    return s_overlay_done;
}

static void wav_stop(void)
{
    if (s_wav_player) {
        [s_wav_player stop];
        s_wav_player = nil;
    }
    s_wav_volume = -1.0f;
}

/* Runtime mute (overlay "Som do jogo") for the movie's WAV: called once at
 * open (below) AND every decoded frame (decode_once's loop) so toggling it
 * mid-cutscene takes effect within one frame instead of only at the next
 * open. PS3_MUTE keeps the separate early-return in wav_play (it never
 * changes at runtime, so gating player creation on it is equivalent and
 * saves creating an AVAudioPlayer we would only ever silence). The
 * player's volume is only written when it changes (the per-frame call is
 * otherwise one atomic load). */
static void wav_apply_runtime_mute(void)
{
    if (!s_wav_player) return;
    extern int ps3_audio_output_silenced(void); /* libs/audio/cellAudio.h */
    const float volume = ps3_audio_output_silenced() ? 0.0f : 1.0f;
    if (volume == s_wav_volume) return;
    s_wav_volume = volume;
    s_wav_player.volume = volume;
}

/* m2v path → sibling .wav; play async if present. */
static void wav_play(const char* m2v_path)
{
    wav_stop();
    if (!m2v_path) return;
    size_t L = strlen(m2v_path);
    if (L < 4 || L + 1 >= 1024) return;
    char wav[1024];
    memcpy(wav, m2v_path, L + 1);
    wav[L - 3] = 'w';
    wav[L - 2] = 'a';
    wav[L - 1] = 'v';
    struct stat st;
    if (stat(wav, &st) != 0 || !S_ISREG(st.st_mode)) {
        fprintf(stderr, "[movie-vt] no sibling wav '%s' (video only)\n", wav);
        fflush(stderr);
        return;
    }
    @autoreleasepool {
        /* PS3_MUTE: o mesmo interruptor do cellAudio (libs/audio/cellAudio.c)
         * passa a valer tambem para o WAV do overlay do filme. Sem isto, mutar
         * o jogo deixava a intro a tocar na mesma -- sao caminhos de audio
         * distintos e so' um obedecia. PS3_MUTE nunca muda em runtime (e' lido
         * uma vez do ambiente), por isso continua a evitar criar o player de
         * todo -- ao contrario do mudo do overlay (game_mute), que pode ligar
         * e desligar a qualquer momento e por isso e' aplicado ao volume
         * (wav_apply_runtime_mute), nao a criar/nao-criar o player. */
        { extern int ps3_audio_muted(void);   /* libs/audio/cellAudio.h */
          if (ps3_audio_muted()) {
              static int said = 0;
              if (!said) { said = 1;
                  fprintf(stderr, "[movie-vt] audio silenciado por PS3_MUTE\n");
                  fflush(stderr); }
              return;
          } }
        NSString* path = [NSString stringWithUTF8String:wav];
        NSURL* url = [NSURL fileURLWithPath:path];
        NSError* err = nil;
        s_wav_player = [[AVAudioPlayer alloc] initWithContentsOfURL:url error:&err];
        if (!s_wav_player) {
            fprintf(stderr, "[movie-vt] AVAudioPlayer failed for '%s': %s\n", wav,
                    err ? [[err localizedDescription] UTF8String] : "?");
            fflush(stderr);
            return;
        }
        s_wav_player.numberOfLoops = 0;
        s_wav_volume = -1.0f; /* new player: push the initial volume */
        /* Runtime mute (overlay "Som do jogo") may already be on when the
         * movie starts -- set the initial volume before play(), not after,
         * so no audible frame slips through. decode_once's per-frame loop
         * keeps this live for the rest of playback. */
        wav_apply_runtime_mute();
        if (![s_wav_player play]) {
            fprintf(stderr, "[movie-vt] AVAudioPlayer play failed for '%s'\n", wav);
            fflush(stderr);
        } else {
            fprintf(stderr, "[movie-vt] audio '%s'\n", wav);
            fflush(stderr);
        }
    }
}

/* Pace to ~30 fps when sample timestamps are missing/unstable. */
static void pace_frame(CMTime pts, CMTime* first_pts, uint64_t* t0_ms, int* have_t0)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t now = (uint64_t)ts.tv_sec * 1000ull + (uint64_t)ts.tv_nsec / 1000000ull;

    if (CMTIME_IS_VALID(pts) && CMTIME_IS_NUMERIC(pts)) {
        if (!*have_t0) {
            *first_pts = pts;
            *t0_ms = now;
            *have_t0 = 1;
            return;
        }
        Float64 sec = CMTimeGetSeconds(CMTimeSubtract(pts, *first_pts));
        if (sec < 0) sec = 0;
        uint64_t target = *t0_ms + (uint64_t)(sec * 1000.0);
        if (target > now + 2) {
            usleep((useconds_t)((target - now) * 1000ull));
        }
        return;
    }
    /* fallback ~33 ms */
    usleep(33000);
}

static int decode_once(const char* path)
{
    @autoreleasepool {
        NSString* ns = [NSString stringWithUTF8String:path];
        NSURL* url = [NSURL fileURLWithPath:ns];
        /* Precise duration walks the whole MPEG-2 ES (introhud is 166 MB). */
        NSDictionary* opts = @{
            AVURLAssetPreferPreciseDurationAndTimingKey: @NO
        };
        uint64_t t_open = mono_ms();
        AVURLAsset* asset = [AVURLAsset URLAssetWithURL:url options:opts];
        if (!asset) {
            fprintf(stderr, "[movie-vt] AVURLAsset nil for '%s'\n", path);
            return -1;
        }
        NSArray* tracks = [asset tracksWithMediaType:AVMediaTypeVideo];
        fprintf(stderr, "[movie-vt] asset ready '%s' tracks=%lu in %llums\n",
                path, (unsigned long)tracks.count,
                (unsigned long long)(mono_ms() - t_open));
        fflush(stderr);
        if (tracks.count < 1) {
            fprintf(stderr, "[movie-vt] no video track in '%s'\n", path);
            return -1;
        }
        AVAssetTrack* track = tracks[0];
        NSError* err = nil;
        AVAssetReader* reader = [[AVAssetReader alloc] initWithAsset:asset error:&err];
        if (!reader) {
            fprintf(stderr, "[movie-vt] AVAssetReader: %s\n",
                    err ? [[err localizedDescription] UTF8String] : "?");
            return -1;
        }
        NSDictionary* settings = @{
            (id)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA)
        };
        AVAssetReaderTrackOutput* out =
            [[AVAssetReaderTrackOutput alloc] initWithTrack:track outputSettings:settings];
        /* H-M1: YES — with NO, AVFoundation recycles the IOSurface after
         * CFRelease(sampleBuffer) even if we CFRetain the CVPixelBuffer.
         * Guest Metal then samples a recycled (often black) surface while
         * CPU nonblack probes on the retained object still saw real pixels. */
        out.alwaysCopiesSampleData = YES;
        if (![reader canAddOutput:out]) {
            fprintf(stderr, "[movie-vt] cannot add track output\n");
            return -1;
        }
        [reader addOutput:out];
        if (![reader startReading]) {
            fprintf(stderr, "[movie-vt] startReading failed status=%ld\n",
                    (long)reader.status);
            return -1;
        }

        rsx_metal_movie_playback_begin();
        int use_rgba = force_rgba();
        fprintf(stderr,
                "[movie-vt] decode start '%s' path=%s (BGRA AVAssetReader)\n",
                path, use_rgba ? "rgba-cpu" : "iosurface");
        fflush(stderr);

        CMTime first_pts = kCMTimeInvalid;
        uint64_t t0_ms = 0;
        int have_t0 = 0;
        int frames = 0;
        uint64_t sum_submit_ms = 0;

        while (s_running && !s_skip_req) {
            /* Live, not just at open: the overlay's runtime mute can toggle
             * mid-cutscene. Checked once per decoded frame (~33 ms), the
             * same cadence the loop already runs at. */
            wav_apply_runtime_mute();
            CMSampleBufferRef sb = [out copyNextSampleBuffer];
            if (!sb) break;

            CVImageBufferRef img = CMSampleBufferGetImageBuffer(sb);
            if (img) {
                size_t w = CVPixelBufferGetWidth(img);
                size_t h = CVPixelBufferGetHeight(img);
                uint64_t t_sub0 = mono_ms();
                if (use_rgba) {
                    /* M0 path: lock + optional pack + memcpy into Metal staging. */
                    CVPixelBufferLockBaseAddress(img, kCVPixelBufferLock_ReadOnly);
                    const void* base = CVPixelBufferGetBaseAddress(img);
                    size_t stride = CVPixelBufferGetBytesPerRow(img);
                    if (base && w > 0 && h > 0) {
                        if (stride == w * 4) {
                            rsx_metal_movie_frame(base, (unsigned)w, (unsigned)h);
                        } else {
                            size_t tight = w * 4;
                            uint8_t* tmp = (uint8_t*)malloc(tight * h);
                            if (tmp) {
                                for (size_t y = 0; y < h; y++) {
                                    memcpy(tmp + y * tight,
                                           (const uint8_t*)base + y * stride, tight);
                                }
                                rsx_metal_movie_frame(tmp, (unsigned)w, (unsigned)h);
                                free(tmp);
                            }
                        }
                    }
                    CVPixelBufferUnlockBaseAddress(img, kCVPixelBufferLock_ReadOnly);
                } else {
                    /* M4: hand CVPixelBuffer to Metal (IOSurface → MTLTexture).
                     * Backend retains the buffer; we may CFRelease the sample. */
                    rsx_metal_movie_frame_cvpixel((void*)img);
                }
                sum_submit_ms += mono_ms() - t_sub0;
                frames++;
                /* PS3_MOVIE_AUTOSKIP_FRAMES=N (diagnostic, OFF): press "skip"
                 * by itself after N frames of a non-logo movie, so the skip
                 * path can be reproduced without a controller. */
                { static int as = -1;
                  if (as < 0) { const char* e = getenv("PS3_MOVIE_AUTOSKIP_FRAMES"); as = (e && *e) ? atoi(e) : 0; }
                  if (as > 0 && frames == as && !strstr(path, "SmLogo") && movie_vt_request_skip()) {
                      fprintf(stderr, "[movie-vt] AUTOSKIP (diagnostic) after %d frames '%s'\n", frames, path);
                      fflush(stderr);
                  } }
                if (frames == 1 || (frames % 60) == 0) {
                    fprintf(stderr, "[movie-vt] frame %d %zux%zu path=%s\n",
                            frames, w, h, use_rgba ? "rgba-cpu" : "iosurface");
                    fflush(stderr);
                }
            }
            CMTime pts = CMSampleBufferGetPresentationTimeStamp(sb);
            CFRelease(sb);
            pace_frame(pts, &first_pts, &t0_ms, &have_t0);
        }

        int completed = (reader.status == AVAssetReaderStatusCompleted);
        double avg_ms = (frames > 0) ? (double)sum_submit_ms / (double)frames : 0.0;
        fprintf(stderr,
                "[movie-vt] decode end path=%s frames=%d avg_submit_ms=%.2f "
                "reader.status=%ld completed=%d running=%d\n",
                use_rgba ? "rgba-cpu" : "iosurface", frames, avg_ms,
                (long)reader.status, completed, s_running);
        fprintf(stderr, "[movie-vt] path=%s frames=%d avg_ms=%.2f\n",
                use_rgba ? "rgba-cpu" : "iosurface", frames, avg_ms);
        if (perf_movie()) {
            fprintf(stderr, "[movie-vt] PERF_MOVIE path=%s frames=%d avg_ms=%.2f\n",
                    use_rgba ? "rgba-cpu" : "iosurface", frames, avg_ms);
        }
        fflush(stderr);

        if (s_skip_req) {
            s_skip_req = 0;
            /* Same end the natural pass below reports: overlay_done, so the
             * guest sequence that is open observes EOS and moves on. */
            s_overlay_done = 1;
            fprintf(stderr, "[movie-vt] skip pedido pelo jogador: EOS ao fim de %d quadros\n",
                    frames);
            fflush(stderr);
            return 1;
        }
        if (completed && frames > 0 && s_running) {
            /* Natural EOS of this pass */
            s_overlay_done = 1;
            fprintf(stderr,
                    "[movie-vt] EOS flag set: movie_vt_overlay_done=1 (frames=%d)\n",
                    frames);
            fflush(stderr);
            return 1;
        }
        return (frames > 0) ? 0 : -1;
    }
}

static void* movie_vt_thread(void* arg)
{
    (void)arg;
    do {
        wav_play(s_path);
        int rc = decode_once(s_path);
        wav_stop();
        if (!s_running) break;
        if (rc < 0) {
            fprintf(stderr, "[movie-vt] decode failed — stop\n");
            fflush(stderr);
            break;
        }
        if (!s_loop) break; /* single pass intro */
        /* loop: do not clear sticky done if already 1; Win keeps done sticky */
    } while (s_running && s_loop);

    s_running = 0;
    /* H-M1/H-M2: release movie overlay so content hold / guest can present.
     * Without this, present_mode stays movie=1 paused=1 with a stale (often
     * black) CV/staging texture forever after EOS. */
    rsx_metal_movie_stop();
    return NULL;
}

int movie_vt_request_skip(void)
{
    int open;
    int idx;
    int current;
    int action;
    if (!s_running) return 0;
    open = movie_clock_sequence_open();
    idx = movie_clock_ingame_index();
    current = movie_host_picture_is_current_path(s_path, open, idx);
    action = movie_open_sequence_end(open, current, 0, 1, 0);
    if (action == MOVIE_SEQ_END_SKIP) {
        movie_clock_note_stale(0);
        s_skip_req = 1;
        return 1;
    }
    if (action == MOVIE_SEQ_END_DROP_STALE) {
        /* Earlier movie (logo / introhud) is what the overlay is showing.
         * Take it down without EOS and without putting the logo queue back,
         * so the open sequence's own pictures stay the current ones. */
        movie_clock_note_stale(1);
        s_skip_req = 0;
        s_running = 0;
        fprintf(stderr,
                "[movie-vt] skip de filme anterior '%s': overlay fora, sem EOS\n",
                s_path);
        fflush(stderr);
        return 1;
    }
    return 0;
}

void movie_vt_stop(void)
{
    s_running = 0;
    s_skip_req = 0;
    if (s_thr_live) {
        pthread_t old = s_thr;
        s_thr_live = 0;
        /* StartSeq holds the PPU giant lock. Joining the overlay while it
         * waits on Metal/AVFoundation with that lock frozen the boot. */
        int held = ppu_giant_lock_held_by_caller && ppu_giant_lock_held_by_caller();
        if (held && ppu_giant_lock_release)
            ppu_giant_lock_release();
        pthread_join(old, NULL);
        if (held && ppu_giant_lock_acquire)
            ppu_giant_lock_acquire();
    }
    wav_stop();
    rsx_metal_movie_stop();
}

/* Allow a 2nd single-pass movie after intro (sticky s_overlay_done blocked
 * re-start — CE03C Play#2 needs a clean slate). */
void movie_vt_clear_overlay_done(void)
{
    s_overlay_done = 0;
}

/* PS3_MOVIE_VT_AUTOSTART: gate do autostart do overlay. Default ON.
 *
 * O QUE ISTO E': o overlay do host toca o .m2v da intro por conta propria, sem
 * o guest o pedir. Existe porque o Play#1 da sequencia de intro NUNCA chega a
 * movie_io_open neste port -- e o que o comentario do chamador (movie_hle.c,
 * movie_hle_autostart_cache_if_needed) diz: "on this port the intro .m2v often
 * never hits movie_io_open (only gow2.psarc does)".
 *
 * O DEFEITO VISIVEL que isto causa (reportado por observacao directa do ecra,
 * 2026-08-01, e confirmado em 12/12 corridas do smoke_chain_gate): o filme toca
 * DUAS vezes.
 *
 *     vt_start=2     [movie-vt] thread started ... duas vezes
 *     autostart=1    [movie-vt] autostart from cache '.../SmLogo_v2.m2v'
 *     guest_open=1   [movieio] open '/_movies/smlogo_v2.m2v'
 *
 * As duas NAO sao o mesmo pedido repetido: a 1a e' este autostart a substituir
 * o Play#1 que nunca chega ao IO; a 2a e' o Play#2 natural do guest (CE03C,
 * ver patch_ce03c_introseq_block.py), que chega mesmo ao movie_io_open.
 *
 * PORQUE O DEFAULT CONTINUA ON: desligar isto NAO remove so' a repeticao --
 * parte a cadeia inteira. Medido com o gate desligado, 3 corridas:
 *
 *     st620 max   11 -> 3
 *     StartSeq     2 -> 0
 *     R_PermA      1 -> 0
 *     movie         1 -> 0   (nao toca de todo)
 *
 * A FSM da intro so' avanca com o EOS do overlay; sem ele o guest nunca chega
 * ao Play#2 e nada mais acontece. Ou seja o autostart nao e' redundante hoje:
 * e' o que segura a cadeia.
 *
 * O FIX A SERIO (nao feito aqui) e' fazer o Play#1 do guest chegar mesmo a
 * movie_io_open; ai o autostart deixa de ser preciso e a repeticao desaparece
 * por consequencia, sem mascarar nada. Suprimir cosmeticamente a 2a passagem
 * seria esconder o Play#2 REAL para preservar o substituto -- ao contrario. */
static int movie_vt_autostart_on(void)
{
    static int on = -1;
    if (on < 0) {
        const char* e = getenv("PS3_MOVIE_VT_AUTOSTART");
        on = (e && *e) ? (*e != '0') : 1;   /* default ON */
    }
    return on;
}

static char s_last_pick[1024];   /* E178: ultimo .m2v do cache arrancado (para o replay gated) */
void movie_vt_autostart_from_cache(void)
{
    static int s_tried;
    if (s_tried || s_thr_live || s_running) return;
    if (!hle_on()) return;
    if (!movie_vt_autostart_on()) {
        static int said;
        if (!said) {
            said = 1;
            fprintf(stderr, "[movie-vt] autostart desligado por "
                            "PS3_MOVIE_VT_AUTOSTART=0 -- a cadeia da intro NAO avanca "
                            "sem o EOS do overlay (medido: st620 11->3)\n");
            fflush(stderr);
        }
        return;
    }
    s_tried = 1;

    const char* cache = getenv("PS3_MOVIE_CACHE");
    if (!cache || !*cache) cache = "movie_cache";

    DIR* d = opendir(cache);
    if (!d) {
        fprintf(stderr, "[movie-vt] autostart: cannot opendir '%s'\n", cache);
        fflush(stderr);
        return;
    }
    char pick[1024];
    char logo[1024];
    pick[0] = 0;
    logo[0] = 0;
    struct dirent* e;
    while ((e = readdir(d)) != NULL) {
        size_t n = strlen(e->d_name);
        if (n < 5) continue;
        if (strcasecmp(e->d_name + n - 4, ".m2v") != 0) continue;
        if (strncasecmp(e->d_name, "SmLogo", 6) == 0
            || strncasecmp(e->d_name, "smlogo", 6) == 0) {
            snprintf(logo, sizeof logo, "%s/%s", cache, e->d_name);
            continue;
        }
        if (!pick[0])
            snprintf(pick, sizeof pick, "%s/%s", cache, e->d_name);
    }
    closedir(d);
    if (logo[0])
        snprintf(pick, sizeof pick, "%s", logo);
    if (!pick[0]) {
        fprintf(stderr, "[movie-vt] autostart: no .m2v in '%s'\n", cache);
        fflush(stderr);
        return;
    }
    fprintf(stderr, "[movie-vt] autostart from cache '%s'\n", pick);
    fflush(stderr);
    strncpy(s_last_pick, pick, sizeof s_last_pick - 1); s_last_pick[sizeof s_last_pick - 1] = 0;
    movie_vt_start(pick, /*loop=*/0);
}

/* E178 (2026-09-02) -- PONTE DECLARADA, gated, OFF por defeito: PS3_MOVIE_VT_REPLAY=1.
 * Desde o M9 o 2o Play da intro (/_movies/smlogo_v2.m2v) abre pelo FIOS natural e ja' nao
 * passa por movie_io_open -> o overlay VT nao volta a arrancar -> nunca ha' "overlay done"
 * -> o EOS da ponte (PS3_MOVIE_EOS) nao dispara para o 2o filme -> st620 estaciona em 11 e
 * o loader/registry de shaders nunca corre (bisseccao e130/e146, ledger E147-E173 §4c).
 * O produtor NATURAL desse EOS e' o relogio de audio (mixer do SCREAM, §4b). Enquanto esse
 * nao corre, esta funcao repete o overlay do cache no 2o Play -- so' para chegar ao loader
 * e trabalhar [D]/[E]; nao e' aceite de conteudo nem substitui o caminho natural. */
void movie_vt_replay_from_cache(void)
{
    static int on = -1;
    if (on < 0) { const char* e = getenv("PS3_MOVIE_VT_REPLAY"); on = (e && *e && *e != '0') ? 1 : 0; }
    if (!on) return;
    /* O thread do Play#1 fica vivo depois do "decode end" (a apresentar a ultima imagem);
     * para repetir e' preciso para-lo primeiro. */
    if (s_thr_live && !s_running && s_overlay_done) {
        fprintf(stderr, "[movie-vt] REPLAY: a parar o thread do overlay anterior\n"); fflush(stderr);
        movie_vt_stop();
    }
    if (!hle_on() || s_thr_live || s_running || !s_last_pick[0]) {
        static int said = 0;
        if (said++ < 6) { fprintf(stderr, "[movie-vt] REPLAY recusado: hle=%d thr_live=%d running=%d pick='%s'\n",
                                  hle_on(), (int)s_thr_live, (int)s_running, s_last_pick); fflush(stderr); }
        return;
    }
    static int n = 0;
    fprintf(stderr, "[movie-vt] REPLAY (ponte PS3_MOVIE_VT_REPLAY) #%d do overlay '%s' no novo Play\n", ++n, s_last_pick);
    fflush(stderr);
    s_overlay_done = 0;
    movie_vt_start(s_last_pick, /*loop=*/0);
}
void movie_vt_start(const char* host_m2v_path, int loop)
{
    if (!hle_on()) {
        fprintf(stderr, "[movie-vt] PS3_MOVIE_HLE off — skip '%s'\n",
                host_m2v_path ? host_m2v_path : "(null)");
        fflush(stderr);
        return;
    }
    if (!host_m2v_path || !host_m2v_path[0]) return;

    struct stat st;
    if (stat(host_m2v_path, &st) != 0 || !S_ISREG(st.st_mode)) {
        fprintf(stderr, "[movie-vt] not a file '%s'\n", host_m2v_path);
        fflush(stderr);
        return;
    }

    /* Sticky done (Win movie_hle.c): if already completed once, do not restart
     * the SAME overlay that would fight EOS policy. A later Play of a different
     * file (introhud, rhodesreturn) must start a new session. */
    {
        int same = (s_path[0] && strcmp(s_path, host_m2v_path) == 0);
        if (s_overlay_done && !loop && same) {
            fprintf(stderr, "[movie-vt] overlay_done already sticky — skip re-start\n");
            fflush(stderr);
            return;
        }
        if (!same)
            s_overlay_done = 0;
    }

    movie_vt_stop();

    strncpy(s_path, host_m2v_path, sizeof s_path - 1);
    s_path[sizeof s_path - 1] = 0;
    {
        extern void movie_hle_set_cache_path(const char*);
        movie_hle_set_cache_path(s_path);
    }
    s_loop = loop ? 1 : 0;
    s_running = 1;
    /* Do NOT clear s_overlay_done (Win parity comment). */

    if (pthread_create(&s_thr, NULL, movie_vt_thread, NULL) != 0) {
        fprintf(stderr, "[movie-vt] pthread_create failed\n");
        fflush(stderr);
        s_running = 0;
        return;
    }
    s_thr_live = 1;
    fprintf(stderr, "[movie-vt] thread started path='%s' loop=%d\n", s_path, s_loop);
    fflush(stderr);
}
