/*
 * ps3recomp - cellVdec HLE (real MPEG-2 decode via ffmpeg)
 *
 * The PS3 cellVdec API decodes an elementary video stream (AVC / MPEG-2)
 * fed one Access Unit at a time, delivering frames through a guest callback:
 *
 *   cellVdecOpen(type, res, cb, handle*)   cb = {u32 cbFuncOPD, u32 cbArg}
 *   cellVdecStartSeq(handle)
 *   cellVdecDecodeAu(handle, mode, auInfo) auInfo = {startAddr,size,pts,dts,ud}
 *      -> callback(handle, AUDONE, 0, cbArg)
 *      -> callback(handle, PICOUT, 0, cbArg)   (one per output picture)
 *   cellVdecGetPicItem(handle, &picItem)      picItem = guest ptr to metadata
 *   cellVdecGetPicture(handle, format, outBuff) writes the decoded pixels
 *   cellVdecEndSeq(handle) -> callback(handle, SEQDONE, 0, cbArg)
 *
 * This HLE performs a REAL decode: GoW2's movies live inside the psarc and the
 * game feeds their AUs here, but reconstructing the elementary stream from the
 * game's AU fragments is fragile, so we decode the SAME movie file the overlay
 * already extracted to the host cache (movie_hle_cache_path) with ffmpeg to
 * YUV420P, and hand those frames back through the API in order. The callback is
 * a guest OPD, invoked on the calling (game) PPU thread via g_ps3_guest_caller.
 *
 * ABI: the HLE dispatcher (ppu_hle.cpp) calls handlers as
 *   uint64_t fn(gpr3, gpr4, ..., gpr10)
 * with the PPC return value taken from the result. Pointer args are GUEST
 * effective addresses; structs are big-endian.
 */

#include "cellVdec.h"
#include "ps3emu/guest_call.h"
#include "ps3_trace.h"   /* Task 5 (bringup-instrumentacao-fase0): subsistema MEDIA */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

/* ---- host compat -------------------------------------------------------
 * The decode driver and the FORCE watchdog need four host services: a thread,
 * a sleep, a single-shot claim, and a pipe to ffmpeg. Windows had them spelled
 * inline; POSIX gets the same four through this shim so the body below stays
 * SINGLE-SOURCED. It was not: everything past this point used to be
 * #ifdef _WIN32 with no-op stubs off Windows, so PS3_VDEC_FORCE_SEQDONE_MS
 * (and every other cellVdec entry point) silently did nothing on macOS/Linux
 * -- the guest got CELL_OK from Open without even being handed a handle. */
#ifdef _WIN32

#include <windows.h>   /* Sleep, CreateThread (proactive decode driver) */
#ifdef __MINGW32__ /* winport: mingw */
#include <unistd.h>
#include <time.h>
#include <pthread_time.h>
#endif

#define VDEC_THREAD_RET   unsigned long __stdcall
#define VDEC_THREAD_DONE  return 0
typedef unsigned long (__stdcall *vdec_thread_fn)(void*);
#define VDEC_POPEN_MODE   "rb"
#define vdec_popen        _popen
#define vdec_pclose       _pclose
static inline void vdec_sleep_ms(int ms) { Sleep((DWORD)ms); }
/* Claim the single-shot SEQDONE token: 1 for the winner, 0 for every later
 * caller. LONG is long on Windows, so the shared `volatile long` field casts
 * cleanly into the Interlocked API. */
static inline int vdec_claim(volatile long* tok)
{ return InterlockedCompareExchange((volatile LONG*)tok, 1, 0) == 0; }

#else  /* POSIX (macOS / Linux) */

#include <pthread.h>
#include <unistd.h>

#define VDEC_THREAD_RET   void*
#define VDEC_THREAD_DONE  return NULL
typedef void* (*vdec_thread_fn)(void*);
/* POSIX popen() rejects any mode longer than one character -- "rb" returns
 * NULL on macOS/BSD -- and there is no text mode to escape from anyway. */
#define VDEC_POPEN_MODE   "r"
#define vdec_popen        popen
#define vdec_pclose       pclose
static inline void vdec_sleep_ms(int ms) { usleep((useconds_t)ms * 1000u); }
static inline int vdec_claim(volatile long* tok)
{ return __sync_bool_compare_and_swap(tok, 0L, 1L); }

#endif

/* These workers re-enter GUEST code through g_ps3_guest_caller, and the lifted
 * -O0 frames are deep: the 512 KB pthread default is nowhere near enough, while
 * Windows threads inherit the PE header's 32 MB reserve. Match that reserve
 * explicitly (lazily mapped -- address space, not resident memory), the same
 * reasoning as the SPURS workers in libs/spurs/spurs_kernel.c. */
#define VDEC_THREAD_STACK_BYTES (32u << 20)

/* Start a detached worker running fn(arg). */
static void vdec_spawn(vdec_thread_fn fn, void* arg)
{
#ifdef _WIN32
    HANDLE th = CreateThread(NULL, 0, fn, arg, 0, NULL);
    if (th) CloseHandle(th);
    else fprintf(stderr, "[cellVdec] CreateThread failed\n");
#else
    pthread_attr_t attr;
    int have_attr = (pthread_attr_init(&attr) == 0);
    if (have_attr) {
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        pthread_attr_setstacksize(&attr, VDEC_THREAD_STACK_BYTES);
    }
    pthread_t th;
    if (pthread_create(&th, have_attr ? &attr : NULL, fn, arg) == 0) {
        if (!have_attr) pthread_detach(th);
    } else {
        fprintf(stderr, "[cellVdec] pthread_create failed\n");
    }
    if (have_attr) pthread_attr_destroy(&attr);
#endif
}

extern unsigned char* vm_base;
extern const char* movie_hle_cache_path(void);

/* ---- big-endian guest memory helpers ---------------------------------- */
static inline uint32_t g_rd32(uint32_t ea)
{ const uint8_t* p = vm_base + ea; return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3]; }
static inline uint64_t g_rd64(uint32_t ea) { return ((uint64_t)g_rd32(ea)<<32) | g_rd32(ea+4); }
static inline void g_wr32(uint32_t ea, uint32_t v)
{ uint8_t* p = vm_base + ea; p[0]=(uint8_t)(v>>24); p[1]=(uint8_t)(v>>16); p[2]=(uint8_t)(v>>8); p[3]=(uint8_t)v; }
static inline void g_wr16(uint32_t ea, uint16_t v)
{ uint8_t* p = vm_base + ea; p[0]=(uint8_t)(v>>8); p[1]=(uint8_t)v; }
static inline void g_wr64(uint32_t ea, uint64_t v) { g_wr32(ea,(uint32_t)(v>>32)); g_wr32(ea+4,(uint32_t)v); }

/* ---- internal state --------------------------------------------------- */
#define MAX_VDEC 4

typedef struct {
    int      in_use;
    uint32_t codecType;
    uint32_t cbOpd;         /* guest OPD of the callback */
    uint32_t cbArg;         /* opaque guest arg passed back */
    int      seqStarted;
    uint64_t play_ms;       /* elapsed fed to the guest stream clock */
    uint32_t auCount;       /* AUs / pictures produced so far */
    uint16_t width, height;

    /* ffmpeg decode of the cached movie -> raw YUV420P frames */
    FILE*    dec;           /* popen pipe, NULL until first decode */
    uint8_t* yuv;           /* one frame buffer (host), w*h*3/2 */
    size_t   yuvBytes;
    int      eof;

    /* metadata of the picture currently pending GetPicItem/GetPicture */
    int      hasPic;
    uint64_t pts, dts, userData;

    /* Set once the game feeds cellVdecDecodeAu itself. When set, the proactive
     * decode driver thread must NOT run: both read the same v->dec pipe / v->yuv
     * buffer, and the driver re-popen'ing the stream from frame 0 corrupts the
     * game-driven decode (only the first picture survives, then the player's
     * frame accounting desyncs and it stops feeding AUs -> movie hangs). */
    volatile int gameDriven;

    /* Async output model (matches the real cellVdec contract): DecodeAu only
     * enqueues the AU and returns; a decoder thread turns each solicited AU
     * into one PICOUT, paced by consumption (hasPic). Synchronous PICOUT inside
     * DecodeAu made GoW2's player retrieve only 1 of ~300 offered frames. */
    volatile int  auPending;     /* AUs fed but not yet emitted as a picture */
    volatile int  stopThread;    /* signal the decoder thread to exit */
    volatile int  threadRunning;
    volatile long seqDoneSent;   /* exactly one SEQDONE per started sequence */
    int           forceMs;       /* PS3_VDEC_FORCE_SEQDONE_MS, latched by StartSeq */
    uint64_t      ptsRing[256], dtsRing[256], udRing[256];
    volatile int  ringHead, ringTail;   /* SPSC ts queue: DecodeAu->thread */
    int           synth;         /* PS3_VDEC_METAL_DEMO: synthetic YUV when no ffmpeg */
    uint32_t      synth_n;       /* synthetic frame counter */
    /* M9 (2026-09-02): descodificar os AUs que o GUEST entrega (VideoToolbox, macOS),
     * em vez do filme em cache. Fila host de AUs copiados em DecodeAu (a memoria guest
     * do AU e' reutilizada pelo jogo). PS3_VDEC_VT=0 desliga (volta ao cache/ffmpeg). */
    int           vt;            /* 1 = sessao VT activa para este slot */
    uint8_t*      auQ[64]; size_t auQn[64]; uint64_t auQpts[64];
    volatile int  auQh, auQt;
    int           vtSubmitted;   /* AUs submetidos ao VT ainda sem frame emitido */
    /* PTS gerado quando o AU vem com CELL_CODEC_PTS_INVALID (o GoW2 alimenta o ES cru sem
     * timestamps): o libvdec real interpola a partir do frame_rate_code do sequence header;
     * o player compara o pts do picItem com o seu relogio e sem pts valido nunca pede a
     * 2.a imagem (medido 2026-09-02: GetPicture #1 e depois nada, auPending=35). */
    uint64_t      ptsNext;
    uint32_t      ptsStep;       /* ticks de 90 kHz por imagem (0 = desconhecido -> 3000) */
    uint32_t      memAddr, memSize;  /* CellVdecResource do guest (Open): onde vivem os picItem/picInfo */
    uint8_t       frc, aspect;       /* frame_rate_code / aspect_ratio_information do sequence header */
    uint32_t      itemSeq;
    /* Fila de imagens anunciadas (VT): o libvdec real emite PICOUT por CADA imagem descodificada
     * e guarda-as; GetPicItem/GetPicture devolvem a mais antiga. O player do GoW2 so' pede a
     * imagem N enquanto N < contagem de PICOUT (0x2C0BD0: cmplw r9, obj+0x60C) -- com um PICOUT
     * de cada vez o relogio ultrapassava a contagem e o player parava na 1.a imagem (E143). */
    uint64_t      picPts[64], picDts[64], picUd[64];
    volatile int  picH, picT;    /* fila de pts das imagens anunciadas mas nao levantadas */
    volatile int  picReady;      /* = picT - picH */
    int           soft;          /* winport v20: software picture queue (PS3_VDEC_SOFTQ=1) */
    unsigned long long skipT0, skipFiredMs;   /* winport v21: cutscene skip timer */
    int           skipFired;
    volatile int  endReq82;      /* winport v82c-vdec-drain: 1 = EndSeq seen, thread drains the queued AUs */
    unsigned long long end82Ms; unsigned end82Cnt, end82Q;
} VdecSlot;
#define VDEC_PTS_INVALID 0xFFFFFFFFFFFFFFFFull
static uint32_t vdec_fps_step(unsigned frame_rate_code) {
    switch (frame_rate_code) { case 1: return 3754; case 2: return 3750; case 3: return 3600; case 4: return 3003;
        case 5: return 3000; case 6: return 1800; case 7: return 1502; case 8: return 1500; default: return 3000; }
}
/* ---- winport v20: one line per second about the in-game movie decoder ------------------------ */
extern int g_vdec_getpic_empty;
static unsigned g_wp_calls, g_wp_busy, g_wp_gpi, g_wp_gp;
static void vdec_dbg_tick(VdecSlot* v)
{
    static int on = -1;
    static unsigned long long last_ms;
    static unsigned p_calls, p_busy, p_gpi, p_gp;
    static int p_empty;
    if (on < 0) { const char* e = getenv("PS3_VDEC_DBG"); on = !(e && e[0] == '0'); }
    if (!on) return;
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    unsigned long long ms = (unsigned long long)ts.tv_sec * 1000ull + (unsigned long long)ts.tv_nsec / 1000000ull;
    if (!last_ms) { last_ms = ms; return; }
    if (ms - last_ms < 1000) return;
    fprintf(stderr,
        "[VDEC1S] T=%llu dt=%llums DecodeAu=%u (busy=%u ok=%u) GetPicItem=%u GetPicture=%u empty=%d | "
        "auPending=%d auQ=%d picReady=%d hasPic=%d soft=%d vt=%d pics=%u play_ms=%u seq=%d\n",
        ms, ms - last_ms, g_wp_calls - p_calls, g_wp_busy - p_busy, (g_wp_calls - p_calls) - (g_wp_busy - p_busy),
        g_wp_gpi - p_gpi, g_wp_gp - p_gp, g_vdec_getpic_empty - p_empty,
        v->auPending, (v->auQt - v->auQh) & 63, v->picReady, v->hasPic, v->soft, v->vt, v->auCount, (unsigned)v->play_ms,
        (int)v->seqStarted);
    fflush(stderr);
    p_calls = g_wp_calls; p_busy = g_wp_busy; p_gpi = g_wp_gpi; p_gp = g_wp_gp; p_empty = g_vdec_getpic_empty;
    last_ms = ms;
}

static void vdec_fill_pts(VdecSlot* v) {
    if (v->pts == VDEC_PTS_INVALID) { v->pts = v->ptsNext; v->ptsNext += v->ptsStep ? v->ptsStep : 3000u; }
    if (v->dts == VDEC_PTS_INVALID) v->dts = v->pts;
}

/* MPEG-2 sequence header 00 00 01 B3: 12-bit width/height, 4-bit frame_rate. */
static int vdec_parse_mpeg2_seq(const uint8_t* b, uint32_t sz,
                                unsigned* w, unsigned* h, unsigned* frc)
{
    if (!b || sz < 8u || b[0] || b[1] || b[2] != 1u || b[3] != 0xB3u)
        return 0;
    *w = ((unsigned)b[4] << 4) | (b[5] >> 4);
    *h = ((unsigned)(b[5] & 0xFu) << 8) | b[6];
    *frc = b[7] & 0xFu;
    return (*w > 0u && *h > 0u);
}

/* In-game path: the AU payload is the picture source (not movie_hle cache).
 * Geometry from a sequence header when present; otherwise keep the last size
 * or a tiny default so GetPicture still writes bytes. */
static int vdec_ingest_au_bytes(VdecSlot* v, const uint8_t* b, uint32_t sz)
{
    if (!v || !b || !sz || sz >= 0x400000u)
        return 0;
    unsigned w = 0, h = 0, frc = 0;
    if (vdec_parse_mpeg2_seq(b, sz, &w, &h, &frc)) {
        v->width = (uint16_t)w;
        v->height = (uint16_t)h;
        v->frc = (uint8_t)frc;
        v->aspect = b[7] >> 4;
        v->ptsStep = vdec_fps_step(frc);
    }
    if (!v->width || !v->height) {
        v->width = 16;
        v->height = 16;
    }
    {
        size_t bytes = (size_t)v->width * (size_t)v->height * 3u / 2u;
        if (!v->yuv || v->yuvBytes != bytes) {
            free(v->yuv);
            v->yuv = (uint8_t*)malloc(bytes);
            v->yuvBytes = bytes;
        }
        if (!v->yuv)
            return 0;
        memset(v->yuv, 0x10, (size_t)v->width * (size_t)v->height);
        memset(v->yuv + (size_t)v->width * (size_t)v->height, 0x80,
               bytes - (size_t)v->width * (size_t)v->height);
        v->yuv[0] = b[0];
        if (bytes > 8u)
            v->yuv[8] = b[(sz > 8u) ? 8u : (sz - 1u)];
    }
    v->eof = 0;
    return 1;
}

static int vdec_ingest_guest_au(VdecSlot* v, uint32_t sa, uint32_t sz)
{
    if (!sa || !sz || sz >= 0x400000u)
        return 0;
    return vdec_ingest_au_bytes(v, vm_base + sa, sz);
}
#ifdef __APPLE__
int  vdec_vt_open(unsigned w, unsigned h);
int  vdec_vt_decode(const uint8_t* au, size_t n, uint64_t pts90k);
int  vdec_vt_pop(uint8_t* dst, unsigned w, unsigned h);
void vdec_vt_flush(void);
int  vdec_vt_pending(void);
void vdec_vt_close(void);
static int vdec_vt_enabled(void) { static int on = -1; if (on < 0) { const char* e = getenv("PS3_VDEC_VT"); on = (e && e[0] == '0') ? 0 : 1; } return on; }
#else
static int vdec_vt_enabled(void) { return 0; }
static int vdec_vt_open(unsigned w, unsigned h) { (void)w; (void)h; return 0; }
static int vdec_vt_decode(const uint8_t* au, size_t n, uint64_t p) { (void)au; (void)n; (void)p; return -1; }
static int vdec_vt_pop(uint8_t* d, unsigned w, unsigned h) { (void)d; (void)w; (void)h; return 0; }
static void vdec_vt_flush(void) {}
static int vdec_vt_pending(void) { return 0; }
static void vdec_vt_close(void) {}
#endif
static void vdec_auq_clear(VdecSlot* v) { while (v->auQh != v->auQt) { free(v->auQ[v->auQh]); v->auQ[v->auQh] = NULL; v->auQh = (v->auQh + 1) & 63; } v->vtSubmitted = 0; }
#define VDEC_RING 256

static VdecSlot s_vdec[MAX_VDEC];

/* Strong definition is movie_eos_arm.c. It writes stream+0x154 (and the voice
 * position copied over it) for the open movie. Tests that link cellVdec alone
 * get this no-op. */
__attribute__((weak)) uint32_t movie_cutscene_on_picture(uint64_t elapsed_ms)
{
    (void)elapsed_ms;
    return 0;
}

/* Count of StartSeq this boot (host hang-breaker after real #2 may longjmp). */
volatile int g_vdec_startseq_count = 0;

int vdec_startseq_count(void)
{
    return g_vdec_startseq_count;
}

/* How many cellVdecStartSeq belong to the intro. The game asks for the
 * Santa Monica logo twice; on RPCS3 the 2nd request stops the 1st before it
 * opens the decoder, so the intro is ONE sequence. Our CE03C wait-idle hook
 * (gow2 hooks, OFF by default since 2026-09-23) used to let both play, which
 * is where "count >= 3 is in-game" came from. With the hook off the 2nd
 * StartSeq is already the in-game cutscene; treating it as intro let the
 * intro timer kill it ~1 s after Play. */
int vdec_intro_seqs(void)
{
    static int n = -1;
    if (n < 0) { const char* e = getenv("PS3_CE03C_WAIT_IDLE"); n = (e && e[0] == '1') ? 2 : 1; }
    return n;
}

/* Weak: pad TAS idles skip buttons once a post-intro sequence starts.
 * Strong definition lives in pad_autostart.c (boot). Tests that do not
 * link the pad module get this no-op. */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak)) void pad_autostart_note_cutscene(int on) { (void)on; }
__attribute__((weak)) void movie_hle_begin_ingame_sequence(void) {}
#else
static void pad_autostart_note_cutscene(int on) { (void)on; }
static void movie_hle_begin_ingame_sequence(void) {}
#endif

int vdec_replay_nopic_skip(int startseq_count)
{
    static int s_skip = -1;
    if (s_skip < 0) {
        const char* nopic = getenv("PS3_VDEC_REPLAY_NOPIC");
        const char* pic = getenv("PS3_VDEC_REPLAY_PIC");
        /* Default: deliver PICOUT on every StartSeq (in-game movies).
         * PS3_VDEC_REPLAY_NOPIC=1 restores the intro-replay skip (count>=2).
         * PS3_VDEC_REPLAY_PIC=1 still forces delivery (old opt-out). */
        if (pic && pic[0] && pic[0] != '0')
            s_skip = 0;
        else if (nopic && nopic[0] && nopic[0] != '0')
            s_skip = 1;
        else
            s_skip = 0;
    }
    return s_skip && startseq_count >= 2;
}

/* Proactive driver: GoW2 opens the decoder and StartSeq's it, but never feeds
 * DecodeAu (the elementary stream comes through a FIOS/dmux path that isn't
 * wired). So we drive the decode ourselves: a host thread pulls frames from the
 * cached movie and raises AUDONE/PICOUT/SEQDONE, taking the PPU giant lock
 * around each callback so the movie player's callback runs exclusive of the
 * game thread. This makes cellVdec self-sufficient (the visible overlay and the
 * decode share the same file). */
extern void ppu_giant_lock_acquire(void);
extern void ppu_giant_lock_release(void);

/* Sticky for movie_eos_arm (Task 4). Defined here so vdec_callback can see it;
 * also listed with the other g_vdec_* counters below for discoverability. */
volatile int g_vdec_seqdone_fired = 0;

/* ---- guest callback --------------------------------------------------- */
/* Task 5 (bringup-instrumentacao-fase0): seq_src distingue, so' para
 * CELL_VDEC_MSG_TYPE_SEQDONE, se este disparo veio do watchdog/branch
 * PS3_VDEC_FORCE_SEQDONE_MS ("forced") ou de um fim de sequencia genuino --
 * EOF real do decode, fim do stream sintetico do DEMO, ou cellVdecEndSeq
 * chamado pelo proprio guest ("natural"). NULL para PICOUT/AUDONE (nao se
 * aplica). G8: nao muda a semantica de PS3_VDEC_FORCE_SEQDONE_MS -- so'
 * relata qual dos produtores do token single-shot seqDoneSent chamou aqui. */
static void vdec_callback(VdecSlot* v, uint32_t handle, uint32_t msgType, int32_t msgData,
                          const char* seq_src)
{
    extern ps3_guest_caller_fn g_ps3_guest_caller;
    extern int g_vdec_picout;
    /* Todos os call sites de vdec_callback correm com o giant lock da PPU ja
     * detido -- ou ambiente (handlers HLE sincronos chamados na propria
     * thread do jogo) ou explicito (ppu_giant_lock_acquire/release em volta
     * das chamadas feitas pelas threads host vdec_driver/vdec_force_seqdone).
     * Seguro: ps3_trace_emit() nunca toca nesse lock (so' getenv cacheado,
     * vsnprintf em buffers locais, fwrite+fflush no FILE* privado do stderr). */
    if (msgType == CELL_VDEC_MSG_TYPE_PICOUT) {
        g_vdec_picout++;
        PS3_TRACE(PS3_TS_MEDIA, "vdec_callback", "PICOUT", "handle=%u au=%u", handle, v->auCount);
    }
    if (msgType == CELL_VDEC_MSG_TYPE_AUDONE) {
        PS3_TRACE(PS3_TS_MEDIA, "vdec_callback", "AUDONE", "handle=%u", handle);
    }
    if (msgType == CELL_VDEC_MSG_TYPE_SEQDONE) {
        g_vdec_seqdone_fired = 1;
        { extern volatile unsigned g_wp93_seqdone_n; g_wp93_seqdone_n++; }   /* winport v93-movie */
        PS3_TRACE(PS3_TS_MEDIA, "vdec_callback", "SEQDONE", "handle=%u src=%s",
                  handle, seq_src ? seq_src : "unknown");
        fprintf(stderr,
                "[cellVdec] SEQDONE -> guest cbOpd=0x%08X handle=%u data=%d caller=%s\n",
                v->cbOpd, handle, (int)msgData,
                (g_ps3_guest_caller && v->cbOpd) ? "yes" : "no");
        fflush(stderr);
    }
    if (g_ps3_guest_caller && v->cbOpd)
        g_ps3_guest_caller(v->cbOpd, (uint64_t)handle, (uint64_t)msgType,
                           (uint64_t)(uint32_t)msgData, (uint64_t)v->cbArg);
}

/* ---- ffmpeg decode of the cached movie -------------------------------- */
static void vdec_open_decoder(VdecSlot* v)
{
    const char* path = movie_hle_cache_path();
    /* Probe geometry (fallback 1280x720; GoW2 smlogo is 1280x720). */
    v->width  = v->width  ? v->width  : 1280;
    v->height = v->height ? v->height : 720;
    v->synth = 0;
    v->synth_n = 0;

    if (!path) {
        const char* demo = getenv("PS3_VDEC_METAL_DEMO");
        if (demo && demo[0] && demo[0] != '0') {
            v->yuvBytes = (size_t)v->width * v->height * 3 / 2;
            v->yuv = (uint8_t*)malloc(v->yuvBytes);
            v->synth = 1;
            v->eof = 0;
            fprintf(stderr, "[cellVdec] no movie cache path — PS3_VDEC_METAL_DEMO "
                    "synthetic YUV %ux%u\n", v->width, v->height);
            return;
        }
        v->eof = 1;
        return;
    }
    { char cmd[1200]; FILE* p;
      snprintf(cmd, sizeof cmd,
          "ffprobe -v error -select_streams v:0 -show_entries stream=width,height "
          "-of csv=p=0 \"%s\"", path);
      p = vdec_popen(cmd, "r");
      if (p) { unsigned w=0,h=0; char line[128];
          if (fgets(line,sizeof line,p) && sscanf(line,"%u,%u",&w,&h)==2 && w && h) {
              v->width=(uint16_t)w; v->height=(uint16_t)h; }
          vdec_pclose(p); } }

    v->yuvBytes = (size_t)v->width * v->height * 3 / 2;   /* YUV420P */
    free(v->yuv);
    v->yuv = (uint8_t*)malloc(v->yuvBytes);

    /* Prefer PATH; fall back to common Homebrew prefixes. */
    {
        static const char* bins[] = {
            "ffmpeg", "/opt/homebrew/bin/ffmpeg", "/usr/local/bin/ffmpeg", NULL
        };
        char cmd[1400];
        v->dec = NULL;
        for (int bi = 0; bins[bi] && !v->dec; bi++) {
            snprintf(cmd, sizeof cmd,
                "%s -v error -i \"%s\" -f rawvideo -pix_fmt yuv420p - 2>/dev/null",
                bins[bi], path);
            v->dec = vdec_popen(cmd, VDEC_POPEN_MODE);
            if (v->dec) break;
        }
    }
    v->eof = (v->dec == NULL);
    if (!v->dec) {
        const char* demo = getenv("PS3_VDEC_METAL_DEMO");
        if (demo && demo[0] && demo[0] != '0') {
            v->synth = 1;
            v->eof = 0;
            fprintf(stderr,
                    "[cellVdec] decoder FAILED — PS3_VDEC_METAL_DEMO synthetic "
                    "YUV %ux%u (no ffmpeg)\n",
                    v->width, v->height);
        } else {
            fprintf(stderr, "[cellVdec] decoder opened '%s' %ux%u yuv=%zu (FAILED)\n",
                    path, v->width, v->height, v->yuvBytes);
        }
    } else {
        fprintf(stderr, "[cellVdec] decoder opened '%s' %ux%u yuv=%zu\n",
                path, v->width, v->height, v->yuvBytes);
    }
}

/* Fill v->yuv with a moving colour-bar pattern (YUV420P). */
static void vdec_synth_yuv(VdecSlot* v)
{
    if (!v->yuv || !v->width || !v->height) return;
    uint16_t w = v->width, h = v->height;
    uint8_t* Y = v->yuv;
    uint8_t* U = v->yuv + (size_t)w * h;
    uint8_t* V = U + (size_t)(w / 2) * (h / 2);
    uint32_t phase = v->synth_n * 3u;
    for (uint16_t y = 0; y < h; y++) {
        for (uint16_t x = 0; x < w; x++) {
            int band = ((x + phase) / 64) % 6;
            /* Approximate luma for R/G/B primaries */
            static const uint8_t yv[] = { 76, 150, 29, 226, 105, 181 };
            Y[(size_t)y * w + x] = yv[band];
        }
    }
    for (uint16_t y = 0; y < h / 2; y++) {
        for (uint16_t x = 0; x < w / 2; x++) {
            int band = ((x * 2 + phase) / 64) % 6;
            static const uint8_t uv[][2] = {
                {84, 255}, {43, 21}, {255, 107},
                {0, 128}, {171, 0}, {128, 128}
            };
            U[(size_t)y * (w / 2) + x] = uv[band][0];
            V[(size_t)y * (w / 2) + x] = uv[band][1];
        }
    }
    v->synth_n++;
}

/* Diagnostic counters (PS3_VDEC_STATS): understand why the movie stalls. */
int g_vdec_decode=0, g_vdec_next_ok=0, g_vdec_next_eof=0, g_vdec_picout=0,
    g_vdec_getitem=0, g_vdec_getpic=0, g_vdec_getpic_empty=0;
/* g_vdec_seqdone_fired: defined above vdec_callback (Task 4 FORCE/EOS order). */

/* ---- M9: present decoded frames on Metal (opt-in / Apple default) -------- */
#if defined(__APPLE__)
extern void rsx_metal_movie_frame_stage(const void* bgra, unsigned w, unsigned h);
extern void rsx_metal_movie_present(void);
extern int  rsx_metal_movie_overlay_active(void);
#endif

static int vdec_metal_present_enabled(void)
{
    static int v = -1;
    if (v >= 0) return v;
    const char* e = getenv("PS3_VDEC_METAL_PRESENT");
#if defined(__APPLE__)
    /* Default ON for Apple unless explicitly disabled (=0). */
    if (!e) v = 1;
    else v = (e[0] && e[0] != '0') ? 1 : 0;
#else
    v = (e && e[0] && e[0] != '0') ? 1 : 0;
#endif
    return v;
}

/* YUV420P planar → packed BGRA8888 (BT.601 limited-range, CPU MVP). */
static void vdec_yuv420p_to_bgra(const uint8_t* yuv, uint16_t w, uint16_t h, uint8_t* bgra)
{
    const uint8_t* Y = yuv;
    const uint8_t* U = yuv + (size_t)w * h;
    const uint8_t* V = U + (size_t)(w / 2) * (h / 2);
    for (uint16_t y = 0; y < h; y++) {
        for (uint16_t x = 0; x < w; x++) {
            int Yp = (int)Y[(size_t)y * w + x];
            int Up = (int)U[(size_t)(y / 2) * (w / 2) + (x / 2)] - 128;
            int Vp = (int)V[(size_t)(y / 2) * (w / 2) + (x / 2)] - 128;
            int C = Yp - 16;
            if (C < 0) C = 0;
            int r = (298 * C + 409 * Vp + 128) >> 8;
            int g = (298 * C - 100 * Up - 208 * Vp + 128) >> 8;
            int b = (298 * C + 516 * Up + 128) >> 8;
            if (r < 0) r = 0; else if (r > 255) r = 255;
            if (g < 0) g = 0; else if (g > 255) g = 255;
            if (b < 0) b = 0; else if (b > 255) b = 255;
            size_t o = ((size_t)y * w + x) * 4;
            bgra[o + 0] = (uint8_t)b;
            bgra[o + 1] = (uint8_t)g;
            bgra[o + 2] = (uint8_t)r;
            bgra[o + 3] = 255;
        }
    }
}

static void vdec_try_metal_present(VdecSlot* v)
{
    if (!v || !v->yuv || !v->width || !v->height) return;
    if (!vdec_metal_present_enabled()) return;
#if defined(__APPLE__)
    /* M0/M4 HLE overlay owns the screen until EOS — do not stomp it. */
    if (rsx_metal_movie_overlay_active()) return;
    {
        const char* hle = getenv("PS3_MOVIE_HLE");
        if (hle && hle[0] && hle[0] != '0') {
            /* HLE enabled but overlay not yet active: still defer to HLE path. */
            static int def = 0;
            if (!def) {
                def = 1;
                fprintf(stderr, "[vdec-metal] defer present (PS3_MOVIE_HLE on)\n");
                fflush(stderr);
            }
            return;
        }
    }
    size_t nbytes = (size_t)v->width * (size_t)v->height * 4u;
    uint8_t* bgra = (uint8_t*)malloc(nbytes);
    if (!bgra) return;
    vdec_yuv420p_to_bgra(v->yuv, v->width, v->height, bgra);
    /* Stage only — GCM flip / pump presents (worker must not nextDrawable). */
    rsx_metal_movie_frame_stage(bgra, v->width, v->height);
    free(bgra);
    {
        static int n = 0;
        n++;
        if (n <= 30 || (n % 60) == 0) {
            fprintf(stderr, "[vdec-metal] frame %d %ux%u staged\n",
                    n, (unsigned)v->width, (unsigned)v->height);
            fflush(stderr);
        }
    }
#else
    (void)v;
#endif
}

/* Pull the next YUV frame into v->yuv. Returns 1 on success, 0 at EOF. */
static int vdec_next_frame(VdecSlot* v)
{
    if (v->eof) return 0;
    /* VideoToolbox (macOS default) consumes guest AUs. Opening ffmpeg of
     * introhud.m2v here is the same 166 MB stall as vdec_driver. */
    if (vdec_vt_enabled())
        return 0;
    if (!v->dec && !v->synth) {
        vdec_open_decoder(v);
        if (v->eof && !v->synth) return 0;
    }
    if (v->synth) {
        if (v->synth_n >= 90) { /* ~3s @ 30fps demo */
            v->eof = 1;
            g_vdec_next_eof++;
            return 0;
        }
        vdec_synth_yuv(v);
        g_vdec_next_ok++;
        vdec_try_metal_present(v);
        return 1;
    }
    size_t got = fread(v->yuv, 1, v->yuvBytes, v->dec);
    if (got < v->yuvBytes) {
        v->eof = 1; vdec_pclose(v->dec); v->dec = NULL; g_vdec_next_eof++;
        { static int o=-1; if(o<0)o=getenv("PS3_VDEC_STATS")?1:0;
          if(o) fprintf(stderr,"[cellVdec] next_frame EOF: got %zu/%zu bytes after %d ok frames\n",
                        got, v->yuvBytes, g_vdec_next_ok); }
        return 0;
    }
    g_vdec_next_ok++;
    vdec_try_metal_present(v);
    return 1;
}

static void vdec_close_decoder(VdecSlot* v)
{
    if (v->vt) { vdec_vt_close(); v->vt = 0; }
    v->soft = 0;   /* winport v20 */
    vdec_auq_clear(v); v->picH = v->picT = 0; v->picReady = 0;
    if (v->dec) { vdec_pclose(v->dec); v->dec = NULL; }
    if (v->yuv) { free(v->yuv); v->yuv = NULL; }
    v->eof = 0; v->yuvBytes = 0;
}

/* The decoder worker can block in ffmpeg's pipe read.  A movie-skip timeout
 * must not depend on that worker reaching its polling loop: GoW2 cannot leave
 * its logo FSM until it receives SEQDONE.  Keep this entirely opt-in and make
 * the callback single-shot so a natural EOF and the watchdog cannot race. */
static VDEC_THREAD_RET vdec_force_seqdone(void* arg)
{
#ifdef __APPLE__
    pthread_setname_np("host:vdec-force");
#endif
    VdecSlot* v = (VdecSlot*)arg;
    int force_ms = v->forceMs;      /* single gate, latched by StartSeq */
    if (force_ms <= 0) VDEC_THREAD_DONE;
    vdec_sleep_ms(force_ms);
    if (!v->in_use || !v->seqStarted || !vdec_claim(&v->seqDoneSent))
        VDEC_THREAD_DONE;
    uint32_t handle = (uint32_t)(v - s_vdec);
    fprintf(stderr, "[cellVdec] FORCE SEQDONE watchdog after %d ms (skip intro)\n", force_ms);
    v->seqStarted = 0;
    v->eof = 1;
    ppu_giant_lock_acquire();
    vdec_callback(v, handle, CELL_VDEC_MSG_TYPE_SEQDONE, 0, "forced");
    ppu_giant_lock_release();
    VDEC_THREAD_DONE;
}

/* Async decoder thread: turn each SOLICITED AU (fed via cellVdecDecodeAu, which
 * bumps auPending) into exactly one PICOUT, one at a time, gated on the previous
 * picture being consumed (hasPic==0). This matches the real cellVdec async
 * contract -- pictures arrive on the decoder's timeline, not synchronously
 * inside DecodeAu -- which is what GoW2's movie player expects. The earlier
 * synchronous path emitted 299 PICOUT inside 300 DecodeAu but the player only
 * retrieved 1, then spun forever waiting for frames it never pulled. */
/* ---- winport v82c-vdec-drain (engine_fixes_v82c_vdec_drain.py) ---- */
extern long movie_hle_overlay_done(void);
static int wp82_drain_on(void)
{
    static int v = -1;
    if (v < 0) { const char* e = getenv("WINPORT_VDEC_DRAIN82"); v = (e && e[0] == '1') ? 1 : 0;
                 fprintf(stderr, "[VDEC82] EndSeq drains the queued AUs before SEQDONE: %s\n", v ? "ON" : "off"); fflush(stderr); }
    return v;
}
static unsigned long long wp82_now_ms(void)
{
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long long)ts.tv_sec * 1000ull + (unsigned long long)ts.tv_nsec / 1000000ull;
}
/* decoder thread, once per loop: 1 = SEQDONE sent (or not ours to send) -> leave the loop */
static int wp82_drain_step(VdecSlot* v, uint32_t handle)
{
    if (v->endReq82 != 1) return 0;
    const unsigned long long now = wp82_now_ms();
    if (v->auCount != v->end82Cnt) { v->end82Cnt = v->auCount; v->end82Ms = now; }
    const int empty = (v->auQh == v->auQt) || v->auPending <= 0;
    const char* why = NULL;
    if (empty && !v->hasPic) why = "queue drained, last picture taken";
    else if (movie_hle_overlay_done()) why = "host overlay ended (skip / end)";
    else if (now - v->end82Ms > 3000ull) why = "no progress for 3 s (safety)";
    if (!why) return 0;
    v->endReq82 = 2;
    if (vdec_claim(&v->seqDoneSent)) {
        ppu_giant_lock_acquire();
        fprintf(stderr, "[VDEC82] drained: %s -> SEQDONE after %u pics (%u were queued at EndSeq)\n", why, v->auCount, v->end82Q);
        fflush(stderr);
        vdec_callback(v, handle, CELL_VDEC_MSG_TYPE_SEQDONE, 0, "natural");
        ppu_giant_lock_release();
    }
    return 1;
}
/* ---- end winport v82c-vdec-drain ---- */
static VDEC_THREAD_RET vdec_driver(void* arg)
{
#ifdef __APPLE__
    pthread_setname_np("host:vdec");
#endif
    VdecSlot* v = (VdecSlot*)arg;
    uint32_t handle = (uint32_t)(v - s_vdec);
    v->threadRunning = 1;
    /* Open ffmpeg of the cache file only when VideoToolbox is off. With VT
     * (macOS default) guest AUs are the pictures; popen/ffprobe of introhud
     * (166 MB) at StartSeq stalled the machine while AVAssetReader also opened
     * the same file for the overlay. */
    if (!v->synth && !v->dec && !v->vt && !vdec_vt_enabled())
        vdec_open_decoder(v);                /* popen (slow) outside the lock */
    /* PS3_VDEC_FORCE_SEQDONE_MS: force end-of-sequence after N ms so a movie the
     * guest player can't fully drain (retrieve stall) is treated as finished and
     * the game advances past the intro. Bring-up "skip intro" aid. 0 = off.
     * Latched by StartSeq -- see the comment there for why it is not re-read. */
    int force_ms = v->forceMs;
    int elapsed_ms = 0;
    for (;;) {
        if (v->stopThread || !v->in_use || !v->seqStarted) break;
        if (wp82_drain_step(v, handle)) break;   /* winport v82c-vdec-drain */
        if (force_ms > 0 && elapsed_ms >= force_ms) {
            if (vdec_claim(&v->seqDoneSent)) {
                fprintf(stderr, "[cellVdec] FORCE SEQDONE worker after %d ms (skip intro)\n", elapsed_ms);
                v->eof = 1;
                ppu_giant_lock_acquire();
                vdec_callback(v, handle, CELL_VDEC_MSG_TYPE_SEQDONE, 0, "forced");
                ppu_giant_lock_release();
            }
            break;
        }
        /* Synth demo self-feeds AUs so we do not require guest DecodeAu. */
        if (v->synth && v->auPending <= 0 && !v->eof && v->synth_n < 90)
            v->auPending = 1;

        /* Guest-AU ingest when VT is off: one PICOUT per queued payload. */
        if (!v->vt && !v->soft && v->auQh != v->auQt && v->auPending > 0 && !v->hasPic) {
            int i = v->auQh;
            int got = vdec_ingest_au_bytes(v, v->auQ[i], (uint32_t)v->auQn[i]);
            free(v->auQ[i]); v->auQ[i] = NULL; v->auQh = (i + 1) & 63;
            if (got) {
                ppu_giant_lock_acquire();
                if (v->ringHead != v->ringTail) {
                    v->pts = v->ptsRing[v->ringHead]; v->dts = v->dtsRing[v->ringHead];
                    v->userData = v->udRing[v->ringHead];
                    v->ringHead = (v->ringHead + 1) & (VDEC_RING - 1);
                }
                vdec_fill_pts(v);
                v->auPending--; v->auCount++; v->hasPic = 1;
                g_vdec_next_ok++;
                vdec_callback(v, handle, CELL_VDEC_MSG_TYPE_PICOUT, 0, NULL);
                ppu_giant_lock_release();
            }
            vdec_sleep_ms(1); elapsed_ms += 1; continue;
        }
        /* winport v20 (opt-in, PS3_VDEC_SOFTQ=1): software picture queue for the in-game movie. The
         * guest's pictures are placeholders (the host overlay shows the real video), so turn every
         * queued AU into a PICOUT at once, up to 48 ahead, the way the VideoToolbox path does --
         * instead of one picture at a time gated on the guest's GetPicture. */
        if (v->soft) {
            int announced = 0;
            { static int fz = -2; if (fz == -2) { const char* e = getenv("WINPORT_VDEC_FREEZE93"); fz = e ? atoi(e) : 0; }   /* winport v93b-movie-test */
              if (fz > 0 && v->auCount >= (unsigned)fz) { vdec_sleep_ms(2); elapsed_ms += 2; continue; } }
            while (v->auQh != v->auQt && v->auPending > 0 && v->picReady < 48 && !v->stopThread) {
                int i = v->auQh;
                ppu_giant_lock_acquire();
                vdec_ingest_au_bytes(v, v->auQ[i], (uint32_t)v->auQn[i]);   /* geometry + placeholder picture */
                free(v->auQ[i]); v->auQ[i] = NULL; v->auQh = (i + 1) & 63;
                if (v->ringHead != v->ringTail) {
                    v->pts = v->ptsRing[v->ringHead]; v->dts = v->dtsRing[v->ringHead]; v->userData = v->udRing[v->ringHead];
                    v->ringHead = (v->ringHead + 1) & (VDEC_RING - 1);
                } else { v->pts = VDEC_PTS_INVALID; v->dts = VDEC_PTS_INVALID; }
                vdec_fill_pts(v);
                v->picPts[v->picT & 63] = v->pts; v->picDts[v->picT & 63] = v->dts; v->picUd[v->picT & 63] = v->userData; v->picT++;
                v->picReady = v->picT - v->picH;
                v->auPending--; v->auCount++; v->hasPic = 1;
                g_vdec_next_ok++; g_vdec_picout++;
                vdec_callback(v, handle, CELL_VDEC_MSG_TYPE_PICOUT, 0, NULL);
                ppu_giant_lock_release();
                announced++;
            }
            vdec_sleep_ms(announced ? 1 : 2); elapsed_ms += 2; continue;
        }
        /* need a solicited AU AND a free output slot; else wait */
        if (v->vt) {
            /* M9: submeter os AUs em fila ao VideoToolbox e emitir um PICOUT por frame. */
            while (v->auQh != v->auQt && v->vtSubmitted < 8) {
                int i = v->auQh; vdec_vt_decode(v->auQ[i], v->auQn[i], v->auQpts[i]);
                free(v->auQ[i]); v->auQ[i] = NULL; v->auQh = (i + 1) & 63; v->vtSubmitted++;
            }
            /* anunciar (PICOUT) cada frame que o VT ja' descodificou e ainda nao foi anunciado */
            int announced = 0;
            while (vdec_vt_pending() > v->picReady && v->picReady < 60 && v->auPending > 0 && !v->stopThread) {
                ppu_giant_lock_acquire();
                if (v->ringHead != v->ringTail) {
                    v->pts = v->ptsRing[v->ringHead]; v->dts = v->dtsRing[v->ringHead]; v->userData = v->udRing[v->ringHead];
                    v->ringHead = (v->ringHead + 1) & (VDEC_RING - 1);
                } else { v->pts = VDEC_PTS_INVALID; v->dts = VDEC_PTS_INVALID; }
                vdec_fill_pts(v);
                v->picPts[v->picT & 63] = v->pts; v->picDts[v->picT & 63] = v->dts; v->picUd[v->picT & 63] = v->userData; v->picT++;
                v->picReady = v->picT - v->picH;
                v->auPending--; v->auCount++; v->hasPic = 1;
                if (v->vtSubmitted > 0) v->vtSubmitted--;
                g_vdec_next_ok++; g_vdec_picout++;
                vdec_callback(v, handle, CELL_VDEC_MSG_TYPE_PICOUT, 0, NULL);
                ppu_giant_lock_release();
                announced++;
            }
            vdec_sleep_ms(announced ? 1 : 2); elapsed_ms += 2; continue;
        } else {
        if (v->auPending <= 0 || v->hasPic || (!v->dec && !v->synth) || v->eof) {
            vdec_sleep_ms(2); elapsed_ms += 2; continue;
        }

        /* Decode one frame into v->yuv OUTSIDE the giant lock. Safe against
         * GetPicture (which only touches v->yuv when hasPic==1; here hasPic==0
         * and only this thread flips it to 1, under the lock, below). */
        if (v->synth) {
            if (v->synth_n >= 90) {
                v->eof = 1;
                if (vdec_claim(&v->seqDoneSent)) {
                    ppu_giant_lock_acquire();
                    /* Task 5: o gerador synth (PS3_VDEC_METAL_DEMO) esgotou o
                     * seu proprio contador de frames -- fim de dados, nao um
                     * corte do watchdog PS3_VDEC_FORCE_SEQDONE_MS. */
                    vdec_callback(v, handle, CELL_VDEC_MSG_TYPE_SEQDONE, 0, "natural");
                    ppu_giant_lock_release();
                }
                break;
            }
            vdec_synth_yuv(v);
            g_vdec_next_ok++;
            vdec_try_metal_present(v);
            /* Synth path: free output slot without guest GetPicture. */
            ppu_giant_lock_acquire();
            if (v->auPending > 0) v->auPending--;
            v->auCount++;
            v->hasPic = 0;
            ppu_giant_lock_release();
            if ((v->synth_n % 10u) == 0u) {
                fprintf(stderr, "[vdec-metal] synth loop n=%u au=%d\n",
                        v->synth_n, v->auPending);
                fflush(stderr);
            }
            vdec_sleep_ms(33);
            continue;
        } else {
        size_t got = fread(v->yuv, 1, v->yuvBytes, v->dec);
        if (got < v->yuvBytes) {
            v->eof = 1; vdec_pclose(v->dec); v->dec = NULL; g_vdec_next_eof++;
            /* Natural end of stream. Claim the same single-shot token the
             * watchdog uses: without this the header comment above ("a natural
             * EOF and the watchdog cannot race") was simply not true -- the EOF
             * left the token at 0 and seqStarted at 1, so a watchdog whose
             * deadline fell later still delivered a SECOND SEQDONE for a
             * sequence that had already ended. */
            if (vdec_claim(&v->seqDoneSent)) {
                ppu_giant_lock_acquire();
                /* Task 5: fread() real chegou a EOF do ffmpeg -- fim de
                 * stream genuino, nao o watchdog. */
                vdec_callback(v, handle, CELL_VDEC_MSG_TYPE_SEQDONE, 0, "natural");
                ppu_giant_lock_release();
            }
            break;
        }
        g_vdec_next_ok++;
        /* Present as soon as pixels exist (M9); does not require GetPicture. */
        vdec_try_metal_present(v);
        }
        }   /* !v->vt */

        ppu_giant_lock_acquire();
        if (!v->hasPic && v->auPending > 0 && !v->stopThread) {
            /* pop the oldest AU's timestamps for this picture (keeps the
             * player's PTS/A-V cadence intact) */
            if (v->ringHead != v->ringTail) {
                v->pts      = v->ptsRing[v->ringHead];
                v->dts      = v->dtsRing[v->ringHead];
                v->userData = v->udRing[v->ringHead];
                v->ringHead = (v->ringHead + 1) & (VDEC_RING - 1);
            }
            vdec_fill_pts(v);
            v->auPending--;
            v->auCount++;
            /* Re-Play wall (measured 2026-07-23): after StartSeq#2 + GetPicture#1
             * guest loads an OPD from the movie path string (ICALL-BAD r2="vies"
             * from "/_movies", ctr=0x005F6D6F). That skip used to be the DEFAULT
             * for StartSeq_count>=2, which also starved in-game movies of PICOUT
             * and crashed Play. Default is now deliver; PS3_VDEC_REPLAY_NOPIC=1
             * restores the intro-replay skip. */
            { int skip_guest_pic = vdec_replay_nopic_skip(g_vdec_startseq_count);
              if (skip_guest_pic) {
                v->hasPic = 0;
                { static int n = 0;
                  if (n++ < 4) {
                    fprintf(stderr,
                            "[cellVdec] REPLAY-NOPIC skip guest PICOUT (StartSeq_count=%d "
                            "au=%u; set PS3_VDEC_REPLAY_PIC=1 to deliver)\n",
                            g_vdec_startseq_count, v->auCount);
                    fflush(stderr);
                  }
                }
              } else {
                v->hasPic = 1;
                vdec_callback(v, handle, CELL_VDEC_MSG_TYPE_PICOUT, 0, NULL);
                /* Synth DEMO has no guest GetPicture consumer — free the slot so
                 * the present loop keeps producing frames. */
                if (v->synth)
                    v->hasPic = 0;
              }
            }
        }
        ppu_giant_lock_release();
        if (v->synth)
            vdec_sleep_ms(33); /* ~30 fps present cadence */
    }
    /* thread owns the decoder while running: clean it up on exit */
    if (v->dec) { vdec_pclose(v->dec); v->dec = NULL; }
    if (v->yuv) { free(v->yuv); v->yuv = NULL; }
    v->eof = 0; v->yuvBytes = 0;
    v->threadRunning = 0;
    VDEC_THREAD_DONE;
}

/* Stop the async decoder thread and wait for it to finish. Must RELEASE the PPU
 * giant lock while waiting: the thread needs that lock to run its PICOUT
 * callback, so holding it here would deadlock the join. Called from HLE
 * handlers (StartSeq/EndSeq/Close) that run with the giant lock held. */
static void vdec_stop_thread(VdecSlot* v)
{
    if (!v->threadRunning) return;
    v->stopThread = 1;
    ppu_giant_lock_release();
    for (int i = 0; i < 500 && v->threadRunning; i++) vdec_sleep_ms(2);
    ppu_giant_lock_acquire();
}

/* After CE03C longjmp abort: stop async drivers so PICOUT does not keep
 * hitting FO residual path-as-code under the giant lock. */
void cellVdec_stop_all_for_play_abort(void)
{
    for (int i = 0; i < MAX_VDEC; i++) {
        if (!s_vdec[i].in_use) continue;
        s_vdec[i].hasPic = 0;
        s_vdec[i].seqStarted = 0;
        s_vdec[i].stopThread = 1;
        vdec_stop_thread(&s_vdec[i]);
    }
    fprintf(stderr, "[cellVdec] stop_all_for_play_abort (StartSeq_count=%d)\n",
            g_vdec_startseq_count);
    fflush(stderr);
}

/* ======================================================================= */
/* API (ABI: handlers receive gpr3..gpr10 as uint64_t; pointers are guest EAs) */
/* ======================================================================= */

uint64_t cellVdecOpen(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);

/* PS3_VDEC_METAL_DEMO: self-drive a synthetic (or ffmpeg) stream so M9 present
 * can be proven even when guest Open/StartSeq is still blocked. */
static void vdec_metal_demo_autostart(void)
{
    static int tried;
    if (tried) return;
    tried = 1;
    const char* e = getenv("PS3_VDEC_METAL_DEMO");
    if (!e || !e[0] || e[0] == '0') return;
    if (!vdec_metal_present_enabled()) {
        fprintf(stderr, "[vdec-metal] DEMO skipped (PS3_VDEC_METAL_PRESENT off)\n");
        return;
    }
    for (int i = 0; i < MAX_VDEC; i++) {
        if (s_vdec[i].in_use) continue;
        memset(&s_vdec[i], 0, sizeof(VdecSlot));
        s_vdec[i].in_use = 1;
        s_vdec[i].codecType = 0;
        s_vdec[i].width = 1280;
        s_vdec[i].height = 720;
        s_vdec[i].seqStarted = 1;
        s_vdec[i].forceMs = 0;
        vdec_open_decoder(&s_vdec[i]);
        if (s_vdec[i].eof && !s_vdec[i].synth) {
            fprintf(stderr, "[vdec-metal] DEMO: decoder open failed (install ffmpeg "
                    "or keep PS3_VDEC_METAL_DEMO for synthetic)\n");
            s_vdec[i].in_use = 0;
            return;
        }
        /* Force synthetic if we want stable colors without relying on m2v. */
        if (e[0] == '2' || getenv("PS3_VDEC_METAL_DEMO_SYNTH")) {
            if (s_vdec[i].dec) { vdec_pclose(s_vdec[i].dec); s_vdec[i].dec = NULL; }
            s_vdec[i].synth = 1;
            s_vdec[i].eof = 0;
            if (!s_vdec[i].yuv) {
                s_vdec[i].yuvBytes = (size_t)1280 * 720 * 3 / 2;
                s_vdec[i].yuv = (uint8_t*)malloc(s_vdec[i].yuvBytes);
            }
        }
        fprintf(stderr, "[vdec-metal] DEMO autostart handle=%d synth=%d\n",
                i, s_vdec[i].synth);
        fflush(stderr);
        vdec_spawn(vdec_driver, &s_vdec[i]);
        return;
    }
}

/* cellVdecQueryAttrEx(typeEx*, attr*) : GoW2 calls this before OpenEx to learn
 * how much memory to allocate. Fill CellVdecAttr {decoderVerLower, Upper,
 * memSize, cmdDepth}. We don't own the decode memory (ffmpeg host-side), so a
 * modest memSize the game will happily allocate is enough. */
uint64_t cellVdecQueryAttrEx(uint64_t a_type, uint64_t a_attr, uint64_t a2, uint64_t a3,
                             uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7)
{
    (void)a2;(void)a3;(void)a4;(void)a5;(void)a6;(void)a7;
    uint32_t typeEa = (uint32_t)a_type, attrEa = (uint32_t)a_attr;
    uint32_t codec = typeEa ? g_rd32(typeEa + 0) : 0;
    { static int k=0; if(k++<4 && typeEa){
        fprintf(stderr,"[cellVdec] QueryAttrEx type@0x%08X:", typeEa);
        for(int i=0;i<0x18;i+=4) fprintf(stderr," %08X", g_rd32(typeEa+i));
        fprintf(stderr," attr@0x%08X\n", attrEa); fflush(stderr); } }
    if (attrEa) {
        /* CellVdecAttr {be u32 decoderVerLower, be u32 decoderVerUpper,
         * be u32 memSize, u8 cmdDepth}. cmdDepth is a BYTE. */
        g_wr32(attrEa + 0x00, 0);                 /* decoderVerLower */
        g_wr32(attrEa + 0x04, 0);                 /* decoderVerUpper */
        g_wr32(attrEa + 0x08, 4u * 1024 * 1024);  /* memSize (4 MB, host decode) */
        vm_base[attrEa + 0x0C] = 4;               /* cmdDepth (u8) */
    }
    fprintf(stderr, "[cellVdec] QueryAttrEx codec=%u -> memSize=4MB\n", codec);
    vdec_metal_demo_autostart();
    return CELL_OK;
}

/* cellVdecOpenEx(typeEx*, resEx*, cb*, handle*) : the Open variant GoW2 uses.
 * codecType is at typeEx+0; cb = {u32 cbFuncOPD, u32 cbArg}; handle is OUT. */
uint64_t cellVdecOpenEx(uint64_t a_type, uint64_t a_res, uint64_t a_cb, uint64_t a_handle,
                        uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7)
{
    return cellVdecOpen(a_type, a_res, a_cb, a_handle, a4, a5, a6, a7);
}

/* cellVdecOpen(type*, res*, cb*, handle*) : cb = {u32 cbFuncOPD, u32 cbArg} */
uint64_t cellVdecOpen(uint64_t a_type, uint64_t a_res, uint64_t a_cb, uint64_t a_handle,
                      uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7)
{
    (void)a_res; (void)a4; (void)a5; (void)a6; (void)a7;
    uint32_t typeEa = (uint32_t)a_type, cbEa = (uint32_t)a_cb, hEa = (uint32_t)a_handle;
    uint32_t codec = typeEa ? g_rd32(typeEa + 0) : 0;

    for (int i = 0; i < MAX_VDEC; i++) {
        if (!s_vdec[i].in_use) {
            memset(&s_vdec[i], 0, sizeof(VdecSlot));
            s_vdec[i].in_use    = 1;
            s_vdec[i].memAddr = (a_res && a_res < 0xE0000000ull) ? g_rd32((uint32_t)a_res) : 0;
            s_vdec[i].memSize = (a_res && a_res < 0xE0000000ull) ? g_rd32((uint32_t)a_res + 4) : 0;
            s_vdec[i].itemSeq = 0;
            s_vdec[i].codecType = codec;
            if (cbEa) { s_vdec[i].cbOpd = g_rd32(cbEa + 0); s_vdec[i].cbArg = g_rd32(cbEa + 4); }
            if (hEa) g_wr32(hEa, (uint32_t)i);
            fprintf(stderr, "[cellVdec] Open codec=%u cbOpd=0x%08X cbArg=0x%08X -> handle=%d\n",
                    codec, s_vdec[i].cbOpd, s_vdec[i].cbArg, i);
            return CELL_OK;
        }
    }
    return (uint32_t)CELL_VDEC_ERROR_BUSY;
}

uint64_t cellVdecClose(uint64_t a_handle, uint64_t a1, uint64_t a2, uint64_t a3,
                       uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7)
{
    (void)a1;(void)a2;(void)a3;(void)a4;(void)a5;(void)a6;(void)a7;
    uint32_t h = (uint32_t)a_handle;
    if (h >= MAX_VDEC || !s_vdec[h].in_use) return (uint32_t)CELL_VDEC_ERROR_ARG;
    fprintf(stderr, "[cellVdec] Close(handle=%u)\n", h);
    s_vdec[h].seqStarted = 0;
    vdec_stop_thread(&s_vdec[h]);
    vdec_close_decoder(&s_vdec[h]);
    s_vdec[h].in_use = 0;
    return CELL_OK;
}

uint64_t cellVdecStartSeq(uint64_t a_handle, uint64_t a1, uint64_t a2, uint64_t a3,
                          uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7)
{
    (void)a1;(void)a2;(void)a3;(void)a4;(void)a5;(void)a6;(void)a7;
    uint32_t h = (uint32_t)a_handle;
    if (h >= MAX_VDEC || !s_vdec[h].in_use) return (uint32_t)CELL_VDEC_ERROR_ARG;
    VdecSlot* v = &s_vdec[h];
    /* stop any previous decoder thread cleanly before re-opening */
    vdec_stop_thread(v);
    vdec_close_decoder(v);
    v->seqStarted = 1;
    v->play_ms = 0;
    v->auCount = 0;
    v->hasPic = 0;
    v->eof = 0;
    v->auPending = 0;
    v->stopThread = 0;
    v->ringHead = v->ringTail = 0;
    v->seqDoneSent = 0;
    v->skipT0 = 0; v->skipFired = 0; v->skipFiredMs = 0;   /* winport v21 */
    v->endReq82 = 0; v->end82Ms = 0; v->end82Cnt = 0; v->end82Q = 0;   /* winport v82c-vdec-drain */
    g_vdec_seqdone_fired = 0;   /* new sequence: re-arm EOS gate for FORCE path */

    /* Latch the FORCE deadline ONCE per sequence, before either worker starts.
     * The watchdog and the decode driver both act on it; three independent
     * getenv() reads could disagree (the env changing mid-sequence, a partial
     * write seen by one reader) and leave one producer armed while the other
     * believes the gate is shut. One read, one gate: if this is 0 nothing in
     * this sequence can ever synthesise a SEQDONE. */
    v->forceMs = 0;
    { const char* e = getenv("PS3_VDEC_FORCE_SEQDONE_MS");
      if (e) { int ms = atoi(e); if (ms > 0) v->forceMs = ms; } }

    g_vdec_startseq_count++;
    /* Intro uses StartSeq 1–2. A later Play (Rhodes / Athena opener, level
     * FMV) is count>=3: stop TAS CROSS/START and drop leftover SmLogo cache
     * so this sequence's AUs are the picture, not the intro latch. */
    if (g_vdec_startseq_count > vdec_intro_seqs()) {
        pad_autostart_note_cutscene(1);
        movie_hle_begin_ingame_sequence();
    }

    static int s_sync = -1;
    if (s_sync < 0) s_sync = getenv("PS3_VDEC_ASYNC") ? 0 : 1;  /* default SYNC (async crasha o backend D3D12; opt-in via PS3_VDEC_ASYNC) */
    if (!s_sync) {
        /* Async decode thread (default): emits one PICOUT per solicited AU,
         * paced by consumption. The decoder is opened inside the thread. */
        fprintf(stderr, "[cellVdec] StartSeq(handle=%u) -> async decode thread\n", h);
        vdec_spawn(vdec_driver, v);
    } else {
        fprintf(stderr, "[cellVdec] StartSeq(handle=%u) -> SYNC mode (PS3_VDEC_SYNC)\n", h);
    }
    /* Independent watchdog: see vdec_force_seqdone. */
    if (v->forceMs > 0) vdec_spawn(vdec_force_seqdone, v);
    return CELL_OK;
}

/* ---- winport v21: skip the in-game prerendered cutscene after N ms ------------------------------
 * UNDO = unset PS3_SKIP_INGAME_MOVIE_MS (launcher line). See engine_fixes_v21.py. */
extern const char* movie_hle_cache_path(void);
static unsigned long long vdec_skip_now_ms(void)
{
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long long)ts.tv_sec * 1000ull + (unsigned long long)ts.tv_nsec / 1000000ull;
}
static void vdec_skip_poll(VdecSlot* v, int mark)
{
    static int ms = -1, mode = 1;
    static const char* only = NULL;
    if (ms < 0) {
        const char* e = getenv("PS3_SKIP_INGAME_MOVIE_MS");   ms = e ? atoi(e) : 0;
        e = getenv("PS3_SKIP_INGAME_MOVIE_MODE");             mode = e ? atoi(e) : 1;
        e = getenv("PS3_SKIP_INGAME_MOVIE_ONLY");             only = (e && e[0]) ? e : NULL;
        if (ms > 0)
            fprintf(stderr, "[SKIP] in-game movie skip ON: %d ms after playback starts, mode %d, only='%s'  (undo: unset PS3_SKIP_INGAME_MOVIE_MS)\n",
                    ms, mode, only ? only : "(any)");
        else
            fprintf(stderr, "[SKIP] in-game movie skip OFF (PS3_SKIP_INGAME_MOVIE_MS not set)\n");
        fflush(stderr);
    }
    if (ms <= 0 || v->skipFired || g_vdec_startseq_count <= vdec_intro_seqs()) return;
    if (!v->skipT0) {
        if (!mark) return;
        const char* p = movie_hle_cache_path();
        { static int fx = -1; if (fx < 0) { const char* e = getenv("WINPORT_SKIP_FIX93"); fx = (e && e[0] == '1'); }
          if (fx && only && !p) return; }   /* winport v93-movie: file not known yet (v71 picks at Play): ask again at the next picture */
        if (only && (!p || !strstr(p, only))) { v->skipT0 = ~0ull; return; }   /* some other movie: leave it alone */
        v->skipT0 = vdec_skip_now_ms();
        fprintf(stderr, "[SKIP] in-game movie '%s': first picture handed to the game, %d ms countdown started\n", p ? p : "?", ms);
        fflush(stderr);
        return;
    }
    if (v->skipT0 == ~0ull) return;
    if (vdec_skip_now_ms() - v->skipT0 < (unsigned long long)ms) return;
    v->skipFired = 1;
    v->skipFiredMs = vdec_skip_now_ms();
    fprintf(stderr, "[SKIP] %d ms reached after %u pictures -> fast-forward: back-pressure off, the game will read to the end of the stream (mode %d)\n",
            ms, v->auCount, mode);
    fflush(stderr);
    if (mode >= 2 && vdec_claim(&v->seqDoneSent)) {
        fprintf(stderr, "[SKIP] mode 2: delivering SEQDONE to the game now\n"); fflush(stderr);
        vdec_callback(v, (uint32_t)(v - s_vdec), CELL_VDEC_MSG_TYPE_SEQDONE, 0, "forced");
    }
}

uint64_t cellVdecEndSeq(uint64_t a_handle, uint64_t a1, uint64_t a2, uint64_t a3,
                        uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7)
{
    (void)a1;(void)a2;(void)a3;(void)a4;(void)a5;(void)a6;(void)a7;
    uint32_t h = (uint32_t)a_handle;
    if (h >= MAX_VDEC || !s_vdec[h].in_use) return (uint32_t)CELL_VDEC_ERROR_ARG;
    VdecSlot* v = &s_vdec[h];
    fprintf(stderr, "[cellVdec] EndSeq(handle=%u) after %u pics\n", h, v->auCount);
    { extern volatile unsigned g_wp93_endseq_pics, g_wp93_endseq_n; g_wp93_endseq_pics = v->auCount; g_wp93_endseq_n++; }   /* winport v93-movie */
    if (v->skipFired) { fprintf(stderr, "[SKIP] the game ended the movie %llu ms after the skip request\n",
                                (unsigned long long)(vdec_skip_now_ms() - v->skipFiredMs)); fflush(stderr); }   /* winport v21 */
    if (wp82_drain_on() && v->endReq82 == 0 && v->threadRunning && !v->stopThread && !v->skipFired &&
        g_vdec_startseq_count > vdec_intro_seqs() && !movie_hle_overlay_done() &&
        (v->auQh != v->auQt || v->hasPic)) {   /* winport v82c-vdec-drain: real libvdec decodes what was fed first */
        v->end82Q = (unsigned)((v->auQt - v->auQh) & 63); v->end82Cnt = v->auCount; v->end82Ms = wp82_now_ms();
        v->endReq82 = 1;
        fprintf(stderr, "[VDEC82] EndSeq after %u pics with %u AUs queued (hasPic=%d) -> draining, SEQDONE after the last picture\n",
                v->auCount, v->end82Q, v->hasPic);
        fflush(stderr);
        return CELL_OK;
    }
    v->seqStarted = 0;
    v->play_ms = 0;
    vdec_stop_thread(v);
    if (v->vt) vdec_vt_flush();
    vdec_close_decoder(v);
    /* RPCS3: end_sequence → SEQDONE once; later au_decode is ignored until
     * start_sequence. Claim the same token the FORCE watchdog uses. */
    if (vdec_claim(&v->seqDoneSent))
        vdec_callback(v, h, CELL_VDEC_MSG_TYPE_SEQDONE, 0, "natural");
    return CELL_OK;
}

/* cellVdecDecodeAu(handle, mode, auInfo*) */
uint64_t cellVdecDecodeAu(uint64_t a_handle, uint64_t a_mode, uint64_t a_au, uint64_t a3,
                          uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7)
{
    (void)a_mode;(void)a3;(void)a4;(void)a5;(void)a6;(void)a7;
    uint32_t h = (uint32_t)a_handle, auEa = (uint32_t)a_au;
    if (h >= MAX_VDEC || !s_vdec[h].in_use) return (uint32_t)CELL_VDEC_ERROR_ARG;
    VdecSlot* v = &s_vdec[h];
    /* RPCS3 au_decode: do not decode after end_sequence until start_sequence. */
    if (!v->seqStarted || v->endReq82) return (uint32_t)CELL_VDEC_ERROR_SEQ;   /* winport v82c-vdec-drain */
    v->gameDriven = 1;   /* game feeds AUs itself -> proactive driver stands down */
    g_wp_calls++; vdec_dbg_tick(v); vdec_skip_poll(v, 0);   /* winport v20 / winport v21 */
    g_vdec_decode++;
    /* Task 5: MEDIA AU_IN. Roda como handler HLE sincrono na thread do
     * proprio jogo -- giant lock ambiente ja detido, sem lock proprio deste
     * ficheiro. */
    { uint32_t au_size = auEa ? g_rd32(auEa + 0x04) : 0;
      PS3_TRACE(PS3_TS_MEDIA, "cellVdecDecodeAu", "AU_IN", "handle=%u size=%u", h, au_size); }
    { static int o=-1; if(o<0)o=getenv("PS3_VDEC_STATS")?1:0;
      if(o && (g_vdec_decode % 60)==0)
          fprintf(stderr,"[VDECSTATS] decode=%d next_ok=%d next_eof=%d picout=%d getitem=%d getpic=%d empty=%d\n",
                  g_vdec_decode,g_vdec_next_ok,g_vdec_next_eof,g_vdec_picout,g_vdec_getitem,g_vdec_getpic,g_vdec_getpic_empty); }

    /* PS3_TRACE_VDEC_SESSION (OFF): DecodeAu counted per session (cbArg), uncapped
     * -- the global print below stops at #10, spent by the intro, which hid whether
     * a later session (GoW2 level movie: cbArg 2) receives any AU at all. */
    { static int o=-1; static unsigned long per_arg[8];
      if(o<0) o=getenv("PS3_TRACE_VDEC_SESSION")?1:0;
      if(o){ unsigned a = v->cbArg & 7u; unsigned long c = ++per_arg[a];
        if(c <= 5 || (c % 100u) == 0){
            fprintf(stderr, "[VDECSESS] cbArg=%u DecodeAu #%lu handle=%u size=%u\n",
                    v->cbArg, c, h, auEa ? g_rd32(auEa + 0x04) : 0u);
            fflush(stderr); } } }

    { static int k=0, kin=0, all=-1;   /* PS3_TRACE_VDEC_AU=1: 400 AUs of the in-game sequences */
      if (all < 0) { const char* e = getenv("PS3_TRACE_VDEC_AU"); all = (e && e[0] == '1'); }
      int show = (k++ < 10) || (all && g_vdec_startseq_count > vdec_intro_seqs() && kin++ < 400);
      if(show){
        uint32_t sa = auEa ? g_rd32(auEa+0x00) : 0, sz = auEa ? g_rd32(auEa+0x04) : 0;
        fprintf(stderr, "[cellVdec] DecodeAu #%d handle=%u mode=%u au@0x%08X start=0x%08X size=%u pts=%llu dts=%llu\n",
                k, h, (unsigned)a_mode, auEa, sa, sz, (unsigned long long)(auEa ? g_rd64(auEa + 0x08) : 0), (unsigned long long)(auEa ? g_rd64(auEa + 0x10) : 0)); fflush(stderr); } }

    /* Read the AU descriptor (pts/dts/userData). The actual pixel bytes come
     * from our host ffmpeg decoder, not the guest AU fragment. */
    uint64_t pts = 0, dts = 0, ud = 0;
    if (auEa) { pts = g_rd64(auEa + 0x08); dts = g_rd64(auEa + 0x10); ud = g_rd64(auEa + 0x18); }

    /* AU accepted by the decoder. */
    /* PS3_VDEC_DUMP_AU=<dir>: grava os primeiros 64 AUs (bytes guest crus) para
     * caracterizar o ES que o jogo entrega (M9: AUs -> VideoToolbox). Diagnostico, OFF por default. */
    { static const char* dd = NULL; static int dn = 0, dchk = 0;
      if (!dchk) { dchk = 1; dd = getenv("PS3_VDEC_DUMP_AU"); if (dd && !dd[0]) dd = NULL; }
      if (dd && dn < 64 && auEa) {
          uint32_t sa = g_rd32(auEa + 0x00), sz = g_rd32(auEa + 0x04);
          extern unsigned char* vm_base;
          if (sa && sz && sz < 0x400000) {
              char fn[1024]; snprintf(fn, sizeof fn, "%s/au_%03d.bin", dd, dn++);
              FILE* f = fopen(fn, "wb"); if (f) { fwrite(vm_base + sa, 1, sz, f); fclose(f); }
          } } }
    if (auEa && !v->synth) {
        uint32_t sa = g_rd32(auEa + 0x00), sz = g_rd32(auEa + 0x04);
        if (sa && sz && sz < 0x400000) {
            if (vdec_vt_enabled() && !v->vt) {
                unsigned w = v->width ? v->width : 1280, hh = v->height ? v->height : 720;
                const uint8_t* b = vm_base + sa;
                unsigned pw, ph, pfrc;
                if (vdec_parse_mpeg2_seq(b, sz, &pw, &ph, &pfrc)) {
                    w = pw; hh = ph;
                    v->ptsStep = vdec_fps_step(pfrc); v->ptsNext = 0; v->frc = (uint8_t)pfrc; v->aspect = b[7] >> 4;
                }
                if (vdec_vt_open(w, hh)) {
                    v->vt = 1; v->width = (uint16_t)w; v->height = (uint16_t)hh;
                    if (v->yuv) free(v->yuv);
                    v->yuvBytes = (size_t)w * hh * 3 / 2; v->yuv = (uint8_t*)malloc(v->yuvBytes); v->eof = 0;
                }
            }
            { static int sq = -1; if (sq < 0) { const char* e = getenv("PS3_VDEC_SOFTQ"); sq = (e && e[0] == '1'); }
              if (sq && !v->vt && !v->soft && !vdec_vt_enabled() && getenv("PS3_VDEC_ASYNC") &&
                  g_vdec_startseq_count > vdec_intro_seqs()) {
                  v->soft = 1; fprintf(stderr, "[cellVdec] soft picture queue ON (PS3_VDEC_SOFTQ=1)\n"); } }   /* winport v20 */
            {
                int nt = (v->auQt + 1) & 63;
                /* Queue full: refuse the AU the way the real cellVdec does
                 * (RPCS3 cellVdecDecodeAu: CELL_VDEC_ERROR_BUSY while the
                 * decoder holds its limit) -- no copy, no AUDONE, the player
                 * resubmits. Dropping it and still sending AUDONE let the
                 * player demux the whole in-game .m2v in 8 s (164 MB) while
                 * VideoToolbox got 131 AUs; with no more pictures it ended the
                 * cutscene ~4 s after Play. PS3_VDEC_AU_DROP=1 = old drop. */
                if (nt == v->auQh && !v->skipFired && (v->vt || g_vdec_startseq_count > vdec_intro_seqs())) {   /* winport v21 */
                     /* winport: mingw */ /* v19 */
                    static int drop_old = -1;
                    if (drop_old < 0) { const char* e = getenv("PS3_VDEC_AU_DROP"); drop_old = (e && e[0] == '1'); }
                    if (!drop_old) {
                        static unsigned busy_n;
                        if (busy_n++ < 4) {
                            fprintf(stderr, "[cellVdec] DecodeAu handle=%u: AU queue full -> BUSY\n", h);
                            fflush(stderr);
                        }
                        /* The player retries at once; give the decoder thread
                         * the CPU for a moment instead of spinning with the
                         * giant lock held (1320 DecodeAu for 120 pictures). */
                        { extern void ppu_giant_lock_release(void), ppu_giant_lock_acquire(void);
                          extern void ppu_blockmark_set(const char*, unsigned long long); extern void ppu_blockmark_clear(void);
                          g_wp_busy++; ppu_blockmark_set("vdec-busy-sleep", 0);   /* winport v20 */
                          ppu_giant_lock_release(); usleep(1000); ppu_giant_lock_acquire(); ppu_blockmark_clear(); }
                        return (uint32_t)CELL_VDEC_ERROR_BUSY;
                    }
                }
                if (nt != v->auQh) {
                    uint8_t* c = (uint8_t*)malloc(sz);
                    if (c) {
                        memcpy(c, vm_base + sa, sz);
                        v->auQ[v->auQt] = c;
                        v->auQn[v->auQt] = sz;
                        v->auQpts[v->auQt] = pts;
                        v->auQt = nt;
                    }
                }
            }
        }
    }
    vdec_callback(v, h, CELL_VDEC_MSG_TYPE_AUDONE, 0, NULL);

    static int s_sync = -1;
    if (s_sync < 0) s_sync = getenv("PS3_VDEC_ASYNC") ? 0 : 1;  /* default SYNC (async crasha o backend D3D12; opt-in via PS3_VDEC_ASYNC) */
    if (s_sync) {
        if (v->vt)
            return CELL_OK; /* VT driver emits PICOUT */
        /* In-game: guest AU bytes are the picture. Overlay ffmpeg only if the
         * title did not hand a payload (intro cache path). */
        v->pts = pts; v->dts = dts; v->userData = ud;
        vdec_fill_pts(v);
        {
            uint32_t sa = auEa ? g_rd32(auEa + 0x00) : 0, sz = auEa ? g_rd32(auEa + 0x04) : 0;
            int got = 0;
            if (sa && sz)
                got = vdec_ingest_guest_au(v, sa, sz);
            if (!got)
                got = vdec_next_frame(v);
            if (got) {
                v->auCount++; v->hasPic = 1;
                vdec_callback(v, h, CELL_VDEC_MSG_TYPE_PICOUT, 0, NULL);
            } else v->hasPic = 0;
        }
        return CELL_OK;
    }

    /* Async (default): enqueue the AU's timestamps and bump auPending; the
     * decoder thread emits exactly one PICOUT per AU, paced by consumption.
     * The picture arrives on the decoder's timeline, matching the real async
     * cellVdec contract the movie player relies on. */
    int nt = (v->ringTail + 1) & (VDEC_RING - 1);
    if (nt != v->ringHead) {   /* space in the ts ring */
        v->ptsRing[v->ringTail] = pts;
        v->dtsRing[v->ringTail] = dts;
        v->udRing[v->ringTail]  = ud;
        v->ringTail = nt;
    }
    v->auPending++;
    return CELL_OK;
}

/* Fill a CellVdecPicItem (guest) with the pending picture's metadata. */
static void vdec_write_picitem(VdecSlot* v, uint32_t h, uint32_t itemEa)
{
    /* Layout REAL do CellVdecPicItem (lido no RPCS3 a 2026-09-02, item devolvido por GetPicItem):
     *  +0x00 codecType  +0x04 startAddr  +0x08 size  +0x0C auNum(u8)+3
     *  +0x10 auPts[0]{hi,lo}  +0x18 auPts[1]=invalid  +0x20 auDts[0]  +0x28 auDts[1]=invalid
     *  +0x30 auUserData[2]  +0x40 status  +0x44 attr  +0x48 picInfo (aponta para +0x50)
     *  +0x50 CellVdecMpeg2Info: u16 hsize, u16 vsize, +4 ?, +5 frame_rate_code (o player le'
     *        picInfo+5 para a tabela de fps -- 0x2C0C08), +0x12 picture_coding_type.
     * O layout antigo (pts u64 em +0x10, status em +0x28, w/h em +0x30) deixava o fps a lixo. */
    g_wr32(itemEa + 0x00, v->codecType);
    g_wr32(itemEa + 0x04, 0);
    g_wr32(itemEa + 0x08, (uint32_t)v->yuvBytes);
    g_wr32(itemEa + 0x0C, 0x01000000u);
    g_wr32(itemEa + 0x10, (uint32_t)(v->pts >> 32)); g_wr32(itemEa + 0x14, (uint32_t)v->pts);
    g_wr32(itemEa + 0x18, 0xFFFFFFFFu); g_wr32(itemEa + 0x1C, 0xFFFFFFFFu);
    g_wr32(itemEa + 0x20, (uint32_t)(v->dts >> 32)); g_wr32(itemEa + 0x24, (uint32_t)v->dts);
    g_wr32(itemEa + 0x28, 0xFFFFFFFFu); g_wr32(itemEa + 0x2C, 0xFFFFFFFFu);
    g_wr64(itemEa + 0x30, v->userData); g_wr64(itemEa + 0x38, 0);
    g_wr32(itemEa + 0x40, 0);              /* status = OK */
    g_wr32(itemEa + 0x44, 0);              /* attr */
    g_wr32(itemEa + 0x48, itemEa + 0x50);  /* picInfo */
    g_wr32(itemEa + 0x4C, 0);
    {   /* CellVdecMpeg2Info (bytes do console: 05 00 02 d0 | 01 05 01 01 | 05 00 00 00 | 0 | 00 00 01 00 | 03 03 00 00) */
        uint32_t w = v->width ? v->width : 1280, hh = v->height ? v->height : 720;
        g_wr32(itemEa + 0x50, (w << 16) | (hh & 0xFFFFu));
        g_wr32(itemEa + 0x54, 0x01000101u | ((uint32_t)(v->frc ? v->frc : 5) << 16));
        g_wr32(itemEa + 0x58, 0x05000000u);
        g_wr32(itemEa + 0x5C, 0);
        g_wr32(itemEa + 0x60, (v->auCount <= 1) ? 0x00000100u : 0x00000200u);
        g_wr32(itemEa + 0x64, 0x03030000u);
        for (uint32_t o = 0x68; o < 0xA0; o += 4) g_wr32(itemEa + o, 0);
    }
    (void)h;
}

/* cellVdecGetPicItem(handle, picItem**) : writes a guest ptr to a metadata
 * block. We keep a per-slot scratch block inside guest-visible... but we have
 * none reserved, so the game usually passes GetPicItem a pointer-to-pointer and
 * expects US to hand back a pointer into decoder-owned memory. GoW2's movie
 * player only reads a few fields; point it at a small scratch area right after
 * the AU or reuse a static guest scratch. Simplest correct approach: many SDK
 * builds treat picItem as an OUT struct pointer (single indirection). Support
 * both: if [picItem] looks like a writable guest ptr, fill it; else fill the
 * pointee. */
uint64_t cellVdecGetPicItem(uint64_t a_handle, uint64_t a_item, uint64_t a2, uint64_t a3,
                            uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7)
{
    (void)a2;(void)a3;(void)a4;(void)a5;(void)a6;(void)a7;
    uint32_t h = (uint32_t)a_handle, itemPP = (uint32_t)a_item;
    if (h >= MAX_VDEC || !s_vdec[h].in_use) return (uint32_t)CELL_VDEC_ERROR_ARG;
    VdecSlot* v = &s_vdec[h];
    { static int k=0; if(k++<6){
        fprintf(stderr, "[cellVdec] GetPicItem #%d handle=%u pp=0x%08X hasPic=%d lr=0x%08X pts=%llu\n",
                k, h, itemPP, v->hasPic, (uint32_t)0 /* lr n/d */, (unsigned long long)v->pts); fflush(stderr); } }
    g_vdec_getitem++; g_wp_gpi++; vdec_dbg_tick(v); vdec_skip_poll(v, 1);   /* winport v20 / winport v21 */
    if (v->vt || v->soft) {
        if (v->picReady <= 0) { g_vdec_getpic_empty++; return (uint32_t)CELL_VDEC_ERROR_EMPTY; }
        v->pts = v->picPts[v->picH & 63]; v->dts = v->picDts[v->picH & 63]; v->userData = v->picUd[v->picH & 63];
    }
    if (!v->hasPic) return (uint32_t)CELL_VDEC_ERROR_EMPTY;

    /* Releasing a picture moves the same guest clock the player compares
     * with auPts: stream+0x154, via movie_cutscene_publish_samples. */
    if (v->seqStarted) {
        v->play_ms += 40;
        (void)movie_cutscene_on_picture(v->play_ms);
    }

    /* picItem is a pointer-to-pointer: the game gives &picPtr and expects us to
     * store a pointer to a metadata block. Reserve a fixed guest scratch inside
     * the movie AU region is unsafe; instead store the metadata into a small
     * scratch we carve from the pointer the game supplies if it is already a
     * struct, else write into a static guest scratch page. GoW2 passes a real
     * pointer-to-pointer, so allocate the block just past it is not possible;
     * write the metadata directly at *itemPP if it points into RAM, else at a
     * reserved scratch. We use a scratch just below the supplied pp. */
    if (itemPP) {
        uint32_t blk;
        if (v->memAddr && v->memSize >= 0x1000) {
            /* como o libvdec real: o item vive na memoria do decoder (CellVdecResource.memAddr), 4 slots rotativos de 0x100 */
            blk = v->memAddr + 0x200 + (v->itemSeq++ & 3) * 0x100;
            g_wr32(itemPP, blk);
        } else {
            blk = g_rd32(itemPP);
            if (!blk || blk >= 0x20000000u) { blk = itemPP + 4; g_wr32(itemPP, blk); }
        }
        vdec_write_picitem(v, h, blk);
    }
    return CELL_OK;
}

/* Store one decoded YUV420P picture in the layout the guest asked for
 * (CellVdecPicFormat: u32 formatType, u32 colorMatrixType, u8 alpha).
 * formatType 0 = ARGB32 interleaved, 1 = RGBA32, 2 = UYVY422, 3 = YUV420
 * planar. The picture used to be copied as raw YUV420 whatever the format:
 * GoW2 asks for ARGB, so its movies came out grey, four times side by side
 * (1-byte luma read as 4-byte pixels) once the player had the pictures. */
static void vdec_store_picture(uint32_t outEa, uint32_t fmtEa, const uint8_t* yuv,
                               unsigned w, unsigned h, size_t yuvBytes)
{
    uint32_t type = 3, matrix = 0; uint8_t alpha = 0xFF;
    if (!outEa || !yuv || !w || !h) return;
    if (fmtEa) { type = g_rd32(fmtEa + 0); matrix = g_rd32(fmtEa + 4); alpha = vm_base[fmtEa + 8]; }
    { static int k = 0; if (k++ < 3) {
        fprintf(stderr, "[cellVdec] GetPicture format type=%u matrix=%u alpha=0x%02X %ux%u\n",
                type, matrix, alpha, w, h); fflush(stderr); } }
    if (type == 3 || type > 3) {
        if ((uint64_t)outEa + yuvBytes <= 0xE0000000ull) memcpy(vm_base + outEa, yuv, yuvBytes);
        return;
    }
    const size_t need = (type == 2) ? (size_t)w * h * 2 : (size_t)w * h * 4;
    if ((uint64_t)outEa + need > 0xE0000000ull) return;
    const uint8_t* Y = yuv; const uint8_t* U = yuv + (size_t)w * h; const uint8_t* V = U + (size_t)(w / 2) * (h / 2);
    uint8_t* o = vm_base + outEa;
    /* limited-range YCbCr -> RGB, 16.16 fixed point */
    const int crv = matrix == 1 ? 117504 : 104597, cgu = matrix == 1 ? 13954 : 25675,
              cgv = matrix == 1 ? 34903 : 53279,  cbu = matrix == 1 ? 138453 : 132201;
    for (unsigned y = 0; y < h; y++) {
        const uint8_t* yr = Y + (size_t)y * w; const uint8_t* ur = U + (size_t)(y / 2) * (w / 2);
        const uint8_t* vr = V + (size_t)(y / 2) * (w / 2);
        if (type == 2) {   /* UYVY: U Y0 V Y1 */
            uint8_t* d = o + (size_t)y * w * 2;
            for (unsigned x = 0; x + 1 < w; x += 2) {
                d[0] = ur[x / 2]; d[1] = yr[x]; d[2] = vr[x / 2]; d[3] = yr[x + 1]; d += 4; }
            continue;
        }
        uint8_t* d = o + (size_t)y * w * 4;
        for (unsigned x = 0; x < w; x++) {
            int c = ((int)yr[x] - 16) * 76309, du = (int)ur[x / 2] - 128, dv = (int)vr[x / 2] - 128;
            int r = (c + crv * dv + 32768) >> 16, g = (c - cgu * du - cgv * dv + 32768) >> 16,
                b = (c + cbu * du + 32768) >> 16;
            r = r < 0 ? 0 : r > 255 ? 255 : r; g = g < 0 ? 0 : g > 255 ? 255 : g; b = b < 0 ? 0 : b > 255 ? 255 : b;
            if (type == 0) { d[0] = alpha; d[1] = (uint8_t)r; d[2] = (uint8_t)g; d[3] = (uint8_t)b; }
            else           { d[0] = (uint8_t)r; d[1] = (uint8_t)g; d[2] = (uint8_t)b; d[3] = alpha; }
            d += 4;
        }
    }
}

/* cellVdecGetPicture(handle, format*, outBuff) : write decoded pixels. */
uint64_t cellVdecGetPicture(uint64_t a_handle, uint64_t a_fmt, uint64_t a_out, uint64_t a3,
                            uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7)
{
    (void)a3;(void)a4;(void)a5;(void)a6;(void)a7;
    uint32_t h = (uint32_t)a_handle, outEa = (uint32_t)a_out;
    if (h >= MAX_VDEC || !s_vdec[h].in_use) return (uint32_t)CELL_VDEC_ERROR_ARG;
    VdecSlot* v = &s_vdec[h];
    { static int k=0; if(k++<6){
        fprintf(stderr, "[cellVdec] GetPicture #%d handle=%u fmt@0x%08X out=0x%08X hasPic=%d auPending=%d\n",
                k, h, (uint32_t)a_fmt, outEa, v->hasPic, v->auPending); fflush(stderr); } }
    g_vdec_getpic++; g_wp_gp++; vdec_dbg_tick(v); vdec_skip_poll(v, 0);   /* winport v20 / winport v21 */
    if (!v->hasPic) { g_vdec_getpic_empty++; return (uint32_t)CELL_VDEC_ERROR_EMPTY; }
    /* PS3_TRACE_VDEC_PICTIME=1 (diag, OFF): host ms of each picture handed to
     * the guest -- the spacing is what the player sees as smooth or judder. */
    { static int on = -1; static int n = 0;
      if (on < 0) { const char* e = getenv("PS3_TRACE_VDEC_PICTIME"); on = (e && e[0] == '1'); }
      if (on && n++ < 2000) {
          struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
          fprintf(stderr, "[PICTIME] %llu\n", (unsigned long long)ts.tv_sec * 1000ull + (unsigned long long)ts.tv_nsec / 1000000ull);
      } }
    if (v->soft) {   /* winport v20: software picture queue (PS3_VDEC_SOFTQ=1) */
        if (v->picReady <= 0) { g_vdec_getpic_empty++; return (uint32_t)CELL_VDEC_ERROR_EMPTY; }
        if (v->yuv)
            vdec_store_picture(outEa, (uint32_t)a_fmt, v->yuv, v->width, v->height, v->yuvBytes);
        v->picH++; v->picReady = v->picT - v->picH; v->hasPic = (v->picReady > 0);
        g_vdec_getpic++;
        return CELL_OK;
    }
    if (v->vt) {
        if (v->picReady <= 0) { g_vdec_getpic_empty++; return (uint32_t)CELL_VDEC_ERROR_EMPTY; }
        if (vdec_vt_pop(v->yuv, v->width, v->height))
            vdec_store_picture(outEa, (uint32_t)a_fmt, v->yuv, v->width, v->height, v->yuvBytes);
        v->picH++; v->picReady = v->picT - v->picH; v->hasPic = (v->picReady > 0);
        g_vdec_getpic++;
        return CELL_OK;
    }

    /* Copy the decoded YUV420P frame into the guest output buffer. The old
     * guard capped outEa at 0x20000000, but GoW2's movie output buffer lives in
     * the general heap (~0x48xxxxxx), so the frame was never copied -> black
     * video. Use the same ceiling as the movie_io path (0xE0000000). */
    if (v->yuv)
        vdec_store_picture(outEa, (uint32_t)a_fmt, v->yuv, v->width, v->height, v->yuvBytes);

    v->hasPic = 0;   /* picture consumed */
    return CELL_OK;
}

uint64_t cellVdecSetFrameRate(uint64_t a_handle, uint64_t a_code, uint64_t a2, uint64_t a3,
                              uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7)
{
    (void)a_code;(void)a2;(void)a3;(void)a4;(void)a5;(void)a6;(void)a7;
    uint32_t h = (uint32_t)a_handle;
    if (h >= MAX_VDEC || !s_vdec[h].in_use) return (uint32_t)CELL_VDEC_ERROR_ARG;
    return CELL_OK;
}

/* (The POSIX no-op stubs that used to live here are gone: the body above is
 * now built on both hosts through the compat shim at the top of the file.) */

/* ---- winport v47-cutscene-ui: 1 while an in-game movie sequence is open (StartSeq .. EndSeq/Close); cellPad.c ---- */
int wp47_vdec_ingame_active(void)
{
    if (g_vdec_startseq_count <= vdec_intro_seqs()) return 0;
    for (int i = 0; i < MAX_VDEC; i++)
        if (s_vdec[i].in_use && s_vdec[i].seqStarted) return 1;
    return 0;
}

/* ---- winport v50-movie-sync (engine_fixes_v50_movie_sync.py): the game's movie clock for the host overlay ----
 * pictures the game's player has TAKEN in the open in-game sequence (produced minus the one still waiting). The player
 * takes picture k when its audio timebase passes k's pts, so this count is the game's movie position in frames. */
/* ---- winport v93-movie (engine_fixes_v93_movie.py): decoder counters for the movie watch / safety net ---- */
volatile unsigned g_wp93_seqdone_n = 0, g_wp93_endseq_pics = 0, g_wp93_endseq_n = 0;
/* the open in-game sequence: pictures announced (made), taken by the game, announced-not-taken, AUs queued. 0 = none open */
int wp93_vdec_info(unsigned* made, unsigned* taken, int* ready, int* auq)
{
    if (g_vdec_startseq_count <= vdec_intro_seqs()) return 0;
    for (int i = 0; i < MAX_VDEC; i++)
        if (s_vdec[i].in_use && s_vdec[i].seqStarted) {
            VdecSlot* v = &s_vdec[i];
            const int rdy = v->soft ? v->picReady : (v->hasPic ? 1 : 0);
            *made = v->auCount; *taken = v->auCount - (unsigned)(rdy > 0 ? rdy : 0);
            *ready = rdy; *auq = (v->auQt - v->auQh) & 63;
            return 1;
        }
    return 0;
}
/* safety net step 2 (caller holds the PPU giant lock): SEQDONE for the open in-game sequence, once */
void wp93_vdec_force_end(void)
{
    if (g_vdec_startseq_count <= vdec_intro_seqs()) return;
    for (int i = 0; i < MAX_VDEC; i++)
        if (s_vdec[i].in_use && s_vdec[i].seqStarted && vdec_claim(&s_vdec[i].seqDoneSent)) {
            fprintf(stderr, "[MOVIESAFE] decoder: SEQDONE delivered for handle %d after %u pictures\n", i, s_vdec[i].auCount);
            fflush(stderr);
            vdec_callback(&s_vdec[i], (uint32_t)i, CELL_VDEC_MSG_TYPE_SEQDONE, 0, "forced");
        }
}
/* ---- end winport v93-movie ---- */
int wp50_vdec_pictures(unsigned* consumed)
{
    if (g_vdec_startseq_count <= vdec_intro_seqs()) return 0;
    for (int i = 0; i < MAX_VDEC; i++)
        if (s_vdec[i].in_use && s_vdec[i].seqStarted) {
            const unsigned made = s_vdec[i].auCount;
            *consumed = made - (s_vdec[i].soft ? (unsigned)(s_vdec[i].picReady > 0 ? s_vdec[i].picReady : 0)   /* winport v82c-softq-sync */
                                               : ((s_vdec[i].hasPic && made) ? 1u : 0u));
            return 1;
        }
    return 0;
}
