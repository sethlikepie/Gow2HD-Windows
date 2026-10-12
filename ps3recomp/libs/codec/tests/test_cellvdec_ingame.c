/*
 * In-game cellVdec sequence: StartSeq → DecodeAu (guest AU) → GetPicItem /
 * GetPicture → EndSeq. Overlay movie_hle / FORCE watchdog must stay off.
 * Also drives shipped cellDmux SetStream/GetAu and cellPamfReaderInitialize
 * on a synthetic buffer (no game data).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>

#include "cellVdec.h"
#include "../../video/movie_clock.h"
#include "cellDmux.h"
#include "cellPamf.h"
#include "ps3emu/guest_call.h"

uint64_t ps3_timebase_now(void) { return 1; }
#if defined(__APPLE__)
void rsx_metal_movie_frame_stage(const void* bgra, unsigned w, unsigned h)
{ (void)bgra; (void)w; (void)h; }
int rsx_metal_movie_overlay_active(void) { return 0; }
#endif

/* Covers the Scream voice table at 0x008AF900, the address
 * movie_cutscene_publish_samples writes. */
#define ARENA_SIZE 0x900000u
static unsigned char g_arena[ARENA_SIZE];
unsigned char* vm_base = g_arena;

static pthread_mutex_t g_giant = PTHREAD_MUTEX_INITIALIZER;
void ppu_giant_lock_acquire(void) { pthread_mutex_lock(&g_giant); }
void ppu_giant_lock_release(void) { pthread_mutex_unlock(&g_giant); }

static const char* g_cache_path = NULL;
const char* movie_hle_cache_path(void) { return g_cache_path; }
uint32_t g_movie_eos_ea = 0;
long movie_hle_overlay_done(void) { return 0; }
void movie_hle_autostart_cache_if_needed(void) {}
void movie_vt_clear_overlay_done(void) {}
void rsx_host_boot_logo_tick(void) {}
void ppu_blockmark_dump(void) {}
void movie_clock_note(int sequence_open, int ingame_index)
{ (void)sequence_open; (void)ingame_index; }
int movie_clock_ingame_index(void) { return 0; }
int ppu_guest_range_committed(uint32_t addr, uint32_t n)
{
    return (uint64_t)addr + n <= ARENA_SIZE;
}

ps3_guest_caller_fn g_ps3_guest_caller = NULL;

typedef struct { uint32_t opd; uint64_t a0, a1, a2, a3; } CbRec;
#define CB_MAX 64
static CbRec g_cb[CB_MAX];
static int g_cbn;

static void record_cb(uint32_t opd, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3)
{
    if (g_cbn < CB_MAX) {
        g_cb[g_cbn].opd = opd; g_cb[g_cbn].a0 = a0; g_cb[g_cbn].a1 = a1;
        g_cb[g_cbn].a2 = a2; g_cb[g_cbn].a3 = a3;
        g_cbn++;
    }
}

static int cb_count_type(uint64_t msg)
{
    int n = 0, i;
    for (i = 0; i < g_cbn; i++) if (g_cb[i].a1 == msg) n++;
    return n;
}

#define HLE8 uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t
extern uint64_t cellVdecOpen(HLE8);
extern uint64_t cellVdecStartSeq(HLE8);
extern uint64_t cellVdecEndSeq(HLE8);
extern uint64_t cellVdecClose(HLE8);
extern uint64_t cellVdecDecodeAu(HLE8);
extern uint64_t cellVdecGetPicItem(HLE8);
extern uint64_t cellVdecGetPicture(HLE8);

static void be32(uint32_t ea, uint32_t v)
{
    g_arena[ea+0] = (uint8_t)(v >> 24); g_arena[ea+1] = (uint8_t)(v >> 16);
    g_arena[ea+2] = (uint8_t)(v >> 8);  g_arena[ea+3] = (uint8_t)v;
}
static uint32_t rd_be32(uint32_t ea)
{
    return ((uint32_t)g_arena[ea+0] << 24) | ((uint32_t)g_arena[ea+1] << 16) |
           ((uint32_t)g_arena[ea+2] << 8)  |  (uint32_t)g_arena[ea+3];
}
static uint16_t rd_be16(uint32_t ea)
{
    return (uint16_t)((g_arena[ea] << 8) | g_arena[ea+1]);
}

#define HLE_CALL(expr) ({ ppu_giant_lock_acquire(); uint64_t r_ = (expr); \
                          ppu_giant_lock_release(); r_; })

static int g_fail;
#define CHECK(cond, ...) do { if (!(cond)) { \
        fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
        fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); g_fail++; } } while (0)

#define EA_TYPE    0x1000u
#define EA_CB      0x1100u
#define EA_HANDLE  0x1200u
#define EA_AU      0x1300u
#define EA_ES      0x2000u
#define EA_ITEMPP  0x2800u
#define EA_ITEMBLK 0x2900u
#define EA_PIC     0x3000u
#define CB_OPD     0x00ABCDEFu
#define CB_ARG     0x11223344u

/* 16x16 MPEG-2 sequence header (00 00 01 B3). */
static void write_mpeg2_seq(uint32_t ea)
{
    static const uint8_t seq[16] = {
        0x00, 0x00, 0x01, 0xB3,
        0x01, 0x00, 0x10, 0x05, /* 16x16, fps code 5 */
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x01, 0x00
    };
    memcpy(g_arena + ea, seq, sizeof seq);
}

static void sleep_ms(long ms)
{
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

static void write_pamf(uint8_t* p, size_t n)
{
    memset(p, 0, n);
    p[0] = 'P'; p[1] = 'A'; p[2] = 'M'; p[3] = 'F';
    p[4] = 0x00; p[5] = 0x04; p[6] = 0x01; p[7] = 0x00; /* version */
    p[8] = 0; p[9] = 0; p[10] = 0; p[11] = 0x40;       /* data offset */
    p[12] = 0; p[13] = 0; p[14] = 0; p[15] = 0x20;      /* data size */
    p[16] = 0; p[17] = 0; p[18] = 0; p[19] = 0x2C;      /* stream table */
    p[20] = 0; p[21] = 1;                               /* 1 stream (u16 BE at 0x14) */
    p[0x2C] = 2; /* PAMF_RAW_TYPE_M2V */
    p[0x2D] = 0; /* channel */
}

extern int vdec_replay_nopic_skip(int startseq_count);
extern int movie_eos_should_arm(int eos_env, uint32_t eos_ea, long overlay_done,
                                uint32_t st620);

int main(void)
{
    unsetenv("PS3_VDEC_FORCE_SEQDONE_MS");
    unsetenv("PS3_MOVIE_DONE_MS");
    /* This test covers the host cutscene clock, OFF by default since 2026-09-23,
     * and the old two-StartSeq intro (CE03C wait-idle). */
    setenv("PS3_MOVIE_CLOCK", "1", 1);
    setenv("PS3_CE03C_WAIT_IDLE", "1", 1);
    unsetenv("PS3_VDEC_ASYNC");
    unsetenv("PS3_VDEC_REPLAY_PIC");
    unsetenv("PS3_VDEC_REPLAY_NOPIC");
    setenv("PS3_VDEC_VT", "0", 1);
    g_ps3_guest_caller = record_cb;
    g_cache_path = NULL;

    be32(EA_TYPE + 0, CELL_VDEC_CODEC_TYPE_MPEG2);
    be32(EA_TYPE + 4, 0);
    be32(EA_CB + 0, CB_OPD);
    be32(EA_CB + 4, CB_ARG);
    be32(EA_HANDLE, 0xDEADBEEFu);

    uint64_t rc = HLE_CALL(cellVdecOpen(EA_TYPE, 0, EA_CB, EA_HANDLE, 0, 0, 0, 0));
    CHECK(rc == CELL_OK, "Open 0x%llx", (unsigned long long)rc);
    uint32_t handle = rd_be32(EA_HANDLE);
    CHECK(handle < 4, "handle %u", handle);

    /* --- no FORCE: 3.1s with no EndSeq → zero SEQDONE --- */
    rc = HLE_CALL(cellVdecStartSeq(handle, 0, 0, 0, 0, 0, 0, 0));
    CHECK(rc == CELL_OK, "StartSeq 0x%llx", (unsigned long long)rc);
    sleep_ms(3100);
    CHECK(cb_count_type(CELL_VDEC_MSG_TYPE_SEQDONE) == 0,
          "SEQDONE from a timer with FORCE unset (%d)", cb_count_type(CELL_VDEC_MSG_TYPE_SEQDONE));

    /* --- Dmux + Pamf synthetic AU source --- */
    {
        uint8_t pamf[0x80];
        write_pamf(pamf, sizeof pamf);
        CellPamfReader reader;
        memset(&reader, 0, sizeof reader);
        CHECK(cellPamfReaderInitialize(&reader, pamf, (u32)sizeof pamf, 0) == CELL_OK,
              "Pamf init");
        CHECK(cellPamfReaderGetNumberOfStreams(&reader) == 1, "Pamf stream count");
        CHECK(cellPamfReaderGetNumberOfSpecificStreams(&reader, CELL_PAMF_STREAM_TYPE_M2V) == 1,
              "Pamf M2V stream");
        printf("dmux_au: pamf streams=%d m2v=%d\n",
               (int)cellPamfReaderGetNumberOfStreams(&reader),
               (int)cellPamfReaderGetNumberOfSpecificStreams(&reader, CELL_PAMF_STREAM_TYPE_M2V));
    }
    {
        CellDmuxType dt; memset(&dt, 0, sizeof dt); dt.streamType = CELL_DMUX_TYPE_PAMF;
        CellDmuxHandle dh = 0xFFFFFFFFu;
        CHECK(cellDmuxOpen(&dt, NULL, NULL, NULL, &dh) == CELL_OK, "Dmux Open");
        CellDmuxEsFilterId fid; memset(&fid, 0, sizeof fid);
        fid.filterIdMajor = 0xE0;
        CellDmuxEsHandle es = 0xFFFFFFFFu;
        CHECK(cellDmuxEnableEs(dh, &fid, NULL, NULL, NULL, &es) == CELL_OK, "EnableEs");
        write_mpeg2_seq(EA_ES);
        CHECK(cellDmuxSetStream(dh, EA_ES, 16, 0, 0) == CELL_OK, "SetStream");
        CellDmuxAuInfo* au = NULL;
        u32 n = 0;
        CHECK(cellDmuxGetAu(es, &au, &n) == CELL_OK, "GetAu");
        CHECK(au && au->auSize > 0, "AU size %u", au ? au->auSize : 0);
        printf("dmux_au: es=%u auAddr=0x%X auSize=%u\n", es, au->auAddr, au->auSize);
        be32(EA_AU + 0, au->auAddr);
        be32(EA_AU + 4, au->auSize);
        be32(EA_AU + 8, 0); be32(EA_AU + 12, 0);
        be32(EA_AU + 16, 0); be32(EA_AU + 20, 0);
        be32(EA_AU + 24, 0); be32(EA_AU + 28, 0);
        cellDmuxClose(dh);
    }

    int pic0 = cb_count_type(CELL_VDEC_MSG_TYPE_PICOUT);
    rc = HLE_CALL(cellVdecDecodeAu(handle, 0, EA_AU, 0, 0, 0, 0, 0));
    CHECK(rc == CELL_OK, "DecodeAu 0x%llx", (unsigned long long)rc);
    CHECK(cb_count_type(CELL_VDEC_MSG_TYPE_AUDONE) >= 1, "AUDONE");
    CHECK(cb_count_type(CELL_VDEC_MSG_TYPE_PICOUT) == pic0 + 1, "PICOUT count %d",
          cb_count_type(CELL_VDEC_MSG_TYPE_PICOUT));

    /* Guest stream the picture comparison reads. GetPicItem must store the
     * playback count here; the test does not call publish itself. */
    {
        const uint32_t obj = 0x6000u;
        const uint32_t table = 0x8000u;
        const uint32_t snd = 0x00A10001u;
        const uint32_t voice = 3u;
        be32(0x540054u, obj);
        be32(obj + 0x720u, snd);
        be32(0x540DE4u, table);
        be32(table, snd);
        be32(table + 0x1C4u, voice);
    }
    be32(EA_ITEMPP, EA_ITEMBLK);
    rc = HLE_CALL(cellVdecGetPicItem(handle, EA_ITEMPP, 0, 0, 0, 0, 0, 0));
    CHECK(rc == CELL_OK, "GetPicItem 0x%llx", (unsigned long long)rc);
    {
        const uint32_t samples_ea = 0x8000u + 0x154u;
        const uint32_t voice_pos = 0x008AF900u + 3u * 0x300u + 0x14u;
        uint32_t first = rd_be32(samples_ea);
        uint32_t voice = rd_be32(voice_pos);
        uint32_t second;
        CHECK(first > 0, "GetPicItem wrote stream+0x154 (%u)", first);
        CHECK(voice == first, "GetPicItem wrote the voice position (%u)", voice);
        CHECK(movie_picture_eligible(first, 3000) == 1,
              "first released picture is eligible");
        rc = HLE_CALL(cellVdecGetPicItem(handle, EA_ITEMPP, 0, 0, 0, 0, 0, 0));
        CHECK(rc == CELL_OK, "GetPicItem again 0x%llx", (unsigned long long)rc);
        second = rd_be32(samples_ea);
        CHECK(second > first, "second GetPicItem advanced stream+0x154 %u -> %u",
              first, second);
        CHECK(rd_be32(voice_pos) == second, "voice position matches the second pull");
        printf("GetPicItem stream+0x154 %u -> %u\n", first, second);
    }
    uint32_t blk = rd_be32(EA_ITEMPP);
    CHECK(blk != 0, "pic item ptr");
    uint16_t w = rd_be16(blk + 0x50);
    uint16_t h = rd_be16(blk + 0x52);
    CHECK(w == 16 && h == 16, "pic %ux%u want 16x16", w, h);

    memset(g_arena + EA_PIC, 0, 384);
    rc = HLE_CALL(cellVdecGetPicture(handle, 0, EA_PIC, 0, 0, 0, 0, 0));
    CHECK(rc == CELL_OK, "GetPicture 0x%llx", (unsigned long long)rc);
    {
        int i, nz = 0;
        for (i = 0; i < 384; i++) if (g_arena[EA_PIC + i]) nz++;
        CHECK(nz > 0, "GetPicture wrote 0 non-zero bytes");
        printf("GetPicture bytes_nonzero=%d w=%u h=%u\n", nz, w, h);
    }

    int seq0 = cb_count_type(CELL_VDEC_MSG_TYPE_SEQDONE);
    rc = HLE_CALL(cellVdecEndSeq(handle, 0, 0, 0, 0, 0, 0, 0));
    CHECK(rc == CELL_OK, "EndSeq");
    CHECK(cb_count_type(CELL_VDEC_MSG_TYPE_SEQDONE) == seq0 + 1,
          "SEQDONE after EndSeq %d", cb_count_type(CELL_VDEC_MSG_TYPE_SEQDONE));
    {
        /* The next sequence has not opened. A picture pull must not move
         * the guest clock, and EOS must not arm during the open. */
        const uint32_t samples_ea = 0x8000u + 0x154u;
        uint32_t held = rd_be32(samples_ea);
        rc = HLE_CALL(cellVdecGetPicItem(handle, EA_ITEMPP, 0, 0, 0, 0, 0, 0));
        CHECK(rd_be32(samples_ea) == held,
              "closed sequence GetPicItem leaves stream+0x154 at %u", held);
        CHECK(movie_cutscene_playback_samples(0, 1000) == 0,
              "clock formula is 0 when no video is open");
        CHECK(movie_eos_should_arm(1, 0, 1, 4) == 0,
              "EOS does not arm before the sequence has opened");
        printf("GetPicItem sequence closed holds %u; eos_opening=%d\n",
               held, movie_eos_should_arm(1, 0, 1, 4));
    }

    int pic1 = cb_count_type(CELL_VDEC_MSG_TYPE_PICOUT);
    rc = HLE_CALL(cellVdecDecodeAu(handle, 0, EA_AU, 0, 0, 0, 0, 0));
    CHECK(rc == (uint64_t)(uint32_t)CELL_VDEC_ERROR_SEQ,
          "DecodeAu after EndSeq want SEQ err, got 0x%llx", (unsigned long long)rc);
    CHECK(cb_count_type(CELL_VDEC_MSG_TYPE_PICOUT) == pic1,
          "DecodeAu after EndSeq added PICOUT");

    /* Second sequence can run (default env: no REPLAY-NOPIC skip). */
    CHECK(vdec_replay_nopic_skip(1) == 0, "seq1 delivers PICOUT");
    CHECK(vdec_replay_nopic_skip(2) == 0, "seq2 delivers PICOUT by default");
    CHECK(vdec_replay_nopic_skip(3) == 0, "seq3 (in-game) delivers PICOUT");
    HLE_CALL(cellVdecStartSeq(handle, 0, 0, 0, 0, 0, 0, 0));
    rc = HLE_CALL(cellVdecDecodeAu(handle, 0, EA_AU, 0, 0, 0, 0, 0));
    CHECK(rc == CELL_OK, "DecodeAu after new StartSeq");
    CHECK(cb_count_type(CELL_VDEC_MSG_TYPE_PICOUT) == pic1 + 1, "second seq PICOUT");
    {
        const uint32_t samples_ea = 0x8000u + 0x154u;
        uint32_t step = movie_cutscene_playback_samples(1, 40);
        rc = HLE_CALL(cellVdecGetPicItem(handle, EA_ITEMPP, 0, 0, 0, 0, 0, 0));
        CHECK(rc == CELL_OK, "GetPicItem on the next sequence 0x%llx",
              (unsigned long long)rc);
        CHECK(step > 0 && rd_be32(samples_ea) == step,
              "next sequence clock starts at the first picture (%u)", step);
        printf("GetPicItem next sequence stream+0x154 %u\n", rd_be32(samples_ea));
    }
    HLE_CALL(cellVdecEndSeq(handle, 0, 0, 0, 0, 0, 0, 0));
    CHECK(cb_count_type(CELL_VDEC_MSG_TYPE_SEQDONE) == seq0 + 2, "second SEQDONE");

    /* Third sequence: post-intro in-game movie (StartSeq count >= 3). */
    int pic2 = cb_count_type(CELL_VDEC_MSG_TYPE_PICOUT);
    HLE_CALL(cellVdecStartSeq(handle, 0, 0, 0, 0, 0, 0, 0));
    rc = HLE_CALL(cellVdecDecodeAu(handle, 0, EA_AU, 0, 0, 0, 0, 0));
    CHECK(rc == CELL_OK, "DecodeAu third StartSeq");
    CHECK(cb_count_type(CELL_VDEC_MSG_TYPE_PICOUT) == pic2 + 1, "third seq PICOUT");
    HLE_CALL(cellVdecEndSeq(handle, 0, 0, 0, 0, 0, 0, 0));
    HLE_CALL(cellVdecClose(handle, 0, 0, 0, 0, 0, 0, 0));

    if (g_fail) {
        printf("test_cellvdec_ingame: FAIL (%d)\n", g_fail);
        return 1;
    }
    printf("test_cellvdec_ingame: PASS picout=%d seqdone=%d\n",
           cb_count_type(CELL_VDEC_MSG_TYPE_PICOUT),
           cb_count_type(CELL_VDEC_MSG_TYPE_SEQDONE));
    return 0;
}
