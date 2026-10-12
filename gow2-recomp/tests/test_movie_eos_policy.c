/*
 * Unit offline do amostrador do movie player (Task 1 do plano
 * ../../ps3recomp/docs/superpowers/plans/2026-07-20-macos-movie-eos-fsm.md).
 *
 * O que se prova aqui, sem boot nenhum:
 *
 *  1. movie_eos_peek32/peek8 NAO tocam na memoria do guest quando o range nao
 *     esta commitado. Isto nao e' testado por inspeccao: o "range nao
 *     commitado" e' uma pagina real com PROT_NONE, portanto uma leitura
 *     indevida mata o processo com SIGSEGV e o teste reprova de facto.
 *     Era exactamente o papel do VirtualQuery no host Windows.
 *
 *  2. movie_eos_can_sample rejeita obj=0, obj fora da janela do guest,
 *     vm_base=NULL e ranges nao commitados -- os quatro casos em que o
 *     amostrador do Windows fazia `continue`.
 *
 * Build/run: ../smoke_moviefsm_mac.sh (ou clang -std=c11 este ficheiro +
 * ../movie_eos_arm.c).
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <time.h>

#include "../movie_eos_arm.h"
#include "../../../libs/video/movie_clock.h"

/* --- simbolos que o movie_eos_arm.c espera do runtime/host ---------------- */

unsigned char* vm_base = NULL;
uint32_t       g_movie_eos_ea = 0;
long           movie_hle_overlay_done(void) { return 0; }
void           rsx_overlay_log_write(int level, const char* message) { (void)level; (void)message; }
static char    g_cache_path[1024];
const char*    movie_hle_cache_path(void) { return g_cache_path[0] ? g_cache_path : NULL; }
#if defined(__APPLE__)
void           movie_hle_autostart_cache_if_needed(void) {}
void           movie_vt_clear_overlay_done(void) {}
#endif
void           rsx_host_boot_logo_tick(void) {}
void           ppu_blockmark_dump(void) {}
void           movie_clock_note(int sequence_open, int ingame_index)
{ (void)sequence_open; (void)ingame_index; }
int            movie_clock_ingame_index(void) { return 0; }
int            vdec_startseq_count(void) { return 0; }

/* Mapa de commits falso: so a primeira pagina conta como commitada. A segunda
 * fica PROT_NONE de propósito (ver o cabecalho).
 *
 * O tamanho de pagina e' lido em runtime: no arm64 da Apple sao 16 KB, nao os
 * 4 KB habituais, e o mprotect recusa enderecos que nao estejam alinhados a
 * pagina real. */
static uint32_t PAGE = 0;
static int g_committed_consulted = 0;

/*
 * A banda alta (>= 0x4E000000) e' declarada COMMITADA de proposito, apesar de
 * nao ter memoria nenhuma por tras. Sem isso o teste da guarda de objecto era
 * falso: com o mapa de commits a recusar tudo la em cima, apagar a guarda
 * `obj_ea >= MOVIE_OBJ_MAX_EA` do can_sample continuava a passar (mutacao M3
 * escapou na primeira versao deste teste). Com a banda aberta, so a guarda de
 * objecto pode rejeitar aqueles enderecos -- e a mutacao morre. O limiar fica
 * ABAIXO de MOVIE_OBJ_MAX_EA (0x4F000000) para os dois lados da fronteira
 * caírem na banda aberta e o teste ser mesmo de fronteira.
 *
 * Nada nestes enderecos e' lido: o can_sample so decide, nao desreferencia.
 */
static int g_voice_table_committed = 1;

int ppu_guest_range_committed(uint32_t addr, uint32_t n)
{
    g_committed_consulted++;
    if (addr >= 0x4E000000u) return 1;
    /* Scream voice table (0x008AF900, stride 0x300). The stream update copies
     * voice+0x14 over stream+0x154, so the clock test maps this range for real.
     * g_voice_table_committed=0 is the failed-poke case: publish must write
     * nothing, including stream+0x154. */
    if (addr >= 0x008AF900u &&
        (uint64_t)addr + n <= 0x008AF900u + 64u * 0x300u)
        return g_voice_table_committed;
    /* TOC slot movie_audio_resolve_session reads (0x541178 - 0x394). */
    if (addr >= 0x540DE4u && (uint64_t)addr + n <= 0x540DE8u)
        return 1;
    return ((uint64_t)addr + n) <= (uint64_t)PAGE;
}

/* --- helpers -------------------------------------------------------------- */

static int g_fail = 0;

#define CHECK(cond, msg) do {                                            \
        if (!(cond)) { printf("  FAIL: %s\n", (msg)); g_fail++; }        \
        else         { printf("  ok:   %s\n", (msg)); }                  \
    } while (0)

static int write_pcm_wav(const char* path, unsigned data_bytes, unsigned byterate)
{
    unsigned char wav_hdr[44];
    unsigned riff = 36 + data_bytes;
    FILE* wf;
    unsigned i;
    memset(wav_hdr, 0, sizeof wav_hdr);
    memcpy(wav_hdr, "RIFF", 4);
    wav_hdr[4] = (unsigned char)riff;
    wav_hdr[5] = (unsigned char)(riff >> 8);
    wav_hdr[6] = (unsigned char)(riff >> 16);
    wav_hdr[7] = (unsigned char)(riff >> 24);
    memcpy(wav_hdr + 8, "WAVE", 4);
    memcpy(wav_hdr + 12, "fmt ", 4);
    wav_hdr[16] = 16;
    wav_hdr[20] = 1; wav_hdr[22] = 1;
    wav_hdr[24] = (unsigned char)byterate;
    wav_hdr[25] = (unsigned char)(byterate >> 8);
    wav_hdr[26] = (unsigned char)(byterate >> 16);
    wav_hdr[27] = (unsigned char)(byterate >> 24);
    wav_hdr[28] = (unsigned char)byterate;
    wav_hdr[29] = (unsigned char)(byterate >> 8);
    wav_hdr[30] = (unsigned char)(byterate >> 16);
    wav_hdr[31] = (unsigned char)(byterate >> 24);
    wav_hdr[32] = 1; wav_hdr[34] = 8;
    memcpy(wav_hdr + 36, "data", 4);
    wav_hdr[40] = (unsigned char)data_bytes;
    wav_hdr[41] = (unsigned char)(data_bytes >> 8);
    wav_hdr[42] = (unsigned char)(data_bytes >> 16);
    wav_hdr[43] = (unsigned char)(data_bytes >> 24);
    wf = fopen(path, "wb");
    if (!wf) return 0;
    if (fwrite(wav_hdr, 1, 44, wf) != 44) { fclose(wf); return 0; }
    for (i = 0; i < data_bytes; i++) fputc(0, wf);
    fclose(wf);
    return 1;
}

int main(void)
{
    unsigned char* buf;
    uint32_t v;
    uint8_t  b;

    PAGE = (uint32_t)sysconf(_SC_PAGESIZE);
    printf("page size = %u bytes\n", PAGE);

    /* [0,PAGE) legivel, [PAGE,2*PAGE) PROT_NONE, e o resto ate' a tabela de
     * vozes do Scream (0x008AF900) tambem legivel. O poke do relogio escreve
     * nesse EA de verdade; um mapa curto faria o teste mentir. */
    {
        size_t map_n = 0x900000u;
        buf = (unsigned char*)mmap(NULL, map_n, PROT_READ | PROT_WRITE,
                                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (buf == MAP_FAILED) { printf("FAIL: mmap\n"); return 1; }
        memset(buf, 0, map_n);
        if (mprotect(buf + PAGE, PAGE, PROT_NONE) != 0) { printf("FAIL: mprotect\n"); return 1; }
    }

    /* Palavra big-endian conhecida em EA 0x10 e byte em EA 0x20. */
    buf[0x10] = 0x11; buf[0x11] = 0x22; buf[0x12] = 0x33; buf[0x13] = 0x44;
    buf[0x20] = 0xA5;

    printf("== peek com vm_base=NULL ==\n");
    vm_base = NULL;
    v = 0xDEADBEEFu;
    CHECK(movie_eos_peek32(0x10, &v) == 0, "peek32 recusa vm_base=NULL");
    CHECK(v == 0xDEADBEEFu,                "peek32 nao mexe no *out quando recusa");
    CHECK(movie_eos_can_sample(0x100) == 0, "can_sample recusa vm_base=NULL");

    vm_base = buf;

    printf("== leitura big-endian dentro do commit ==\n");
    v = 0;
    CHECK(movie_eos_peek32(0x10, &v) == 1, "peek32 aceita range commitado");
    CHECK(v == 0x11223344u,                "peek32 desempacota big-endian");
    b = 0;
    CHECK(movie_eos_peek8(0x20, &b) == 1,  "peek8 aceita range commitado");
    CHECK(b == 0xA5,                       "peek8 devolve o byte certo");

    printf("== range NAO commitado: nao pode tocar na memoria ==\n");
    /* EA 0x1000 cai na pagina PROT_NONE. Se o peek ler antes de verificar o
     * commit, o processo morre aqui com SIGSEGV -- e' esse o teste. */
    v = 0xDEADBEEFu;
    g_committed_consulted = 0;
    CHECK(movie_eos_peek32(PAGE, &v) == 0, "peek32 recusa range nao commitado");
    CHECK(v == 0xDEADBEEFu,                "peek32 nao escreveu *out (nao leu)");
    CHECK(g_committed_consulted > 0,       "peek32 consultou o mapa de commits");
    b = 0x5A;
    CHECK(movie_eos_peek8(PAGE, &b) == 0,  "peek8 recusa range nao commitado");
    CHECK(b == 0x5A,                       "peek8 nao escreveu *out (nao leu)");

    /* Leitura que COMECA dentro e ATRAVESSA a fronteira tem de ser recusada
     * inteira: 4 bytes a partir de 0xFFE passam para a pagina PROT_NONE. */
    v = 0xDEADBEEFu;
    CHECK(movie_eos_peek32(PAGE - 2, &v) == 0, "peek32 recusa leitura que atravessa a fronteira");
    CHECK(v == 0xDEADBEEFu,                    "peek32 nao leu a fronteira");

    printf("== can_sample: guardas do objecto ==\n");
    CHECK(movie_eos_can_sample(0) == 0,          "can_sample recusa obj=0");
    /* Estes dois estao marcados como commitados pelo mock (ver o comentario
     * la em cima): so a guarda de objecto os pode rejeitar. */
    CHECK(movie_eos_can_sample(0x4F000000u) == 0, "can_sample recusa obj no limite alto (commitado)");
    CHECK(movie_eos_can_sample(0x80000000u) == 0, "can_sample recusa obj fora da janela (commitado)");
    CHECK(movie_eos_can_sample(0x4EFFFFFFu) == 1, "can_sample aceita o ultimo obj valido");
    /* obj valido mas o span (0x780) sai da pagina commitada -> recusa. */
    CHECK(movie_eos_can_sample(PAGE - 0x100) == 0, "can_sample recusa span que sai do commit");
    /* obj valido com o span inteiro dentro do commit -> aceita. */
    CHECK(movie_eos_can_sample(0x100) == 1,      "can_sample aceita obj com span commitado");

    printf("== arm: politica movie_eos_should_arm (Task 3 arma; Task 2 decide QUANDO) ==\n");
    /* A decisao de armar o read-hook de EOS passa TODA por esta funcao pura.
     * Task 2 (2026-07-21, plano intro-vdec-open-force-wad.md) acrescentou um
     * quarto argumento, st620: alem das tres condicoes originais do produtor
     * de "done", agora tambem exige que a FSM ja tenha passado o estado de
     * abertura do vdec (st620>=5). Ja NAO e' a condicao de 3 argumentos
     * byte-a-byte identica ao boot_main.cpp do Windows (linha 367) -- esse
     * gate extra e' politica desta build macOS/arm64 (ver o .c/.h). Testada
     * aqui na funcao REAL de movie_eos_arm.c (nao numa copia): uma mutacao na
     * condicao do sampler parte estes asserts. */

    /* M0: sem PS3_MOVIE_EOS nunca arma, mesmo com done a 1 e st ja pos-open. */
    CHECK(movie_eos_should_arm(0, 0, 1, 5) == 0, "M0: sem env nao arma (mesmo com st pos-open)");
    CHECK(movie_eos_should_arm(0, 0, 0, 5) == 0, "M0: sem env e sem done nao arma");
    CHECK(movie_eos_should_arm(0, 0x1234, 1, 5) == 0, "M0: sem env, ja armado, com done -> nao arma");

    /* M3 FORGE-TRAP (obrigatorio, inegociavel): EOS ligado mas SEM produtor de
     * done -> NAO pode armar, mesmo com a FSM ja pos-open. Se isto devolvesse
     * 1, o arm seria forjado -- e' exactamente a armadilha que o plano exige
     * que se prove. */
    CHECK(movie_eos_should_arm(1, 0, 0, 5) == 0, "M3: EOS sem done NAO arma (forge-trap)");

    /* GATE DE ESTADO (Task 2, o core desta task): done real, EOS ligado, ainda
     * nao armado -- mas SO arma se a FSM ja passou o estado de abertura do
     * vdec (st620>=5). st=3 e st=4 sao exactamente os estados onde o bug
     * antigo armava cedo demais: o vm_read8(obj+0x744) passava a devolver 1
     * ainda em estado 3, o handler do estado 3 saltava 3->4->5 sem NUNCA
     * despachar o corpo do estado 4 (cellVdecOpenEx+StartSeq), o vdec nunca
     * abria e nenhum WAD carregava. */
    CHECK(movie_eos_should_arm(1, 0, 1, 3) == 0, "GATE: done@st620=3 (WAIT_EOS antigo) NAO arma -- estado 4 nao correu");
    CHECK(movie_eos_should_arm(1, 0, 1, 4) == 0, "GATE: done@st620=4 (Open/StartSeq ainda a correr) NAO arma");
    CHECK(movie_eos_should_arm(1, 0, 1, 5) == 1, "NATURAL: EOS + done + st620=5 (pos Open/StartSeq) -> arma");
    CHECK(movie_eos_should_arm(1, 0, 1, 11) == 1, "GATE: st620=11 (bem depois) tambem arma -- so precisa >=5");
    CHECK(movie_eos_should_arm(1, 0, 1, 0xFFFFFFFFu) == 0, "GATE: st620=0xFFFFFFFF (sentinela 'ainda nao lido') NAO arma");

    /* ONE-SHOT: ja armado (eos_ea != 0) nao re-arma, mesmo com done a 1 e a
     * FSM ja pos-open -- a guarda !g_movie_eos_ea do sampler garante um unico
     * arm. */
    CHECK(movie_eos_should_arm(1, 0x1234, 1, 5) == 0, "ONE-SHOT: ja armado nao re-arma");
    CHECK(movie_eos_should_arm(1, 0x00869F1Cu, 1, 5) == 0, "ONE-SHOT: ja armado (EA real) nao re-arma");

    /* A funcao e' PURA: decidir nao escreve no g_movie_eos_ea (o sampler e' que
     * escreve, e so quando isto devolve 1). */
    CHECK(g_movie_eos_ea == 0, "movie_eos_should_arm nao mexe no g_movie_eos_ea (decisao pura)");

    printf("== Task 4: FORCE vs arm EOS (movie_eos_force_blocks_arm) ==\n");
    /* Com FORCE ligado o arm tem de esperar SEQDONE; senao Close mata o handle. */
    CHECK(movie_eos_force_blocks_arm(0, 0) == 0, "FORCE off: nunca bloqueia (path natural)");
    CHECK(movie_eos_force_blocks_arm(0, 1) == 0, "FORCE off + seqdone: nao bloqueia");
    CHECK(movie_eos_force_blocks_arm(4000, 0) == 1, "FORCE on, sem SEQDONE: BLOQUEIA arm");
    CHECK(movie_eos_force_blocks_arm(4000, 1) == 0, "FORCE on + SEQDONE: deixa armar");
    CHECK(movie_eos_force_blocks_arm(2000, 0) == 1, "FORCE 2000ms sem SEQDONE: bloqueia");
    /* Composicao com should_arm: o sampler exige should_arm==1 E !force_blocks. */
    CHECK(movie_eos_should_arm(1, 0, 1, 11) == 1
          && movie_eos_force_blocks_arm(4000, 0) == 1,
          "comp: done@st11 quer armar mas FORCE sem SEQDONE bloqueia");
    CHECK(movie_eos_should_arm(1, 0, 1, 11) == 1
          && movie_eos_force_blocks_arm(4000, 1) == 0,
          "comp: done@st11 + SEQDONE -> arm permitido");

    printf("== A3b: politica movie_audio_should_mark_done (HLE stream-complete) ==\n");
    /* Desbloqueia o park st620=3 sem forjar st620/+0x744: exige done real,
     * handle de audio real, estado 3, one-shot. M3 (done=0) nunca marca. */
    CHECK(movie_audio_should_mark_done(1, 1, 3, 0x84000002u, 0) == 1,
          "A3b: enabled+done+st3+h720 -> marca");
    CHECK(movie_audio_should_mark_done(0, 1, 3, 0x84000002u, 0) == 0,
          "A3b: disabled (PS3_AUDIO_STREAM_DONE=0) nao marca");
    CHECK(movie_audio_should_mark_done(1, 0, 3, 0x84000002u, 0) == 0,
          "A3b/M3: done=0 nao marca (forge-trap partilhado com EOS)");
    CHECK(movie_audio_should_mark_done(1, 1, 1, 0x84000002u, 0) == 0,
          "A3b: st620=1 (ainda a abrir) nao marca");
    CHECK(movie_audio_should_mark_done(1, 1, 5, 0x84000002u, 0) == 0,
          "A3b: st620=5 (ja pos-open) nao marca -- so o park em 3");
    CHECK(movie_audio_should_mark_done(1, 1, 3, 0, 0) == 0,
          "A3b: h720=0 (sem open real) nao marca");
    CHECK(movie_audio_should_mark_done(1, 1, 3, 0x84000002u, 1) == 0,
          "A3b: already_marked one-shot");
    CHECK(movie_audio_should_mark_done(1, 1, 0xFFFFFFFFu, 0x84000002u, 0) == 0,
          "A3b: st sentinela nao marca");

    printf("== later Play: time-based done reset + auto wav of the movie in play ==\n");
    {
        char dir[512], p_logo[640], p_hud[640], p_logo_m[640], p_hud_m[640];
        const char* tmp = getenv("TMPDIR");
        if (!tmp || !*tmp) tmp = "/tmp";
        snprintf(dir, sizeof dir, "%s/ps3recomp_eoswav_XXXXXX", tmp);
        if (!mkdtemp(dir)) { printf("FAIL: mkdtemp\n"); g_fail++; }
        else {
            snprintf(p_logo, sizeof p_logo, "%s/SmLogo_v2.wav", dir);
            snprintf(p_hud, sizeof p_hud, "%s/introhud.wav", dir);
            snprintf(p_logo_m, sizeof p_logo_m, "%s/SmLogo_v2.m2v", dir);
            snprintf(p_hud_m, sizeof p_hud_m, "%s/introhud.m2v", dir);
            if (!write_pcm_wav(p_logo, 8000, 8000)   /* 1000 ms */
                || !write_pcm_wav(p_hud, 16000, 8000)) { /* 2000 ms */
                printf("FAIL: write wav fixtures\n"); g_fail++;
            }
            setenv("PS3_MOVIE_CACHE", dir, 1);
            setenv("PS3_MOVIE_DONE_MS", "auto", 1);
            snprintf(g_cache_path, sizeof g_cache_path, "%s", p_logo_m);
            movie_done_timebased_reset();
            CHECK(movie_done_interval_ms() == 1000,
                  "auto uses SmLogo_v2.wav of the movie in play (1000 ms)");
            snprintf(g_cache_path, sizeof g_cache_path, "%s", p_hud_m);
            movie_done_timebased_reset();
            CHECK(movie_done_interval_ms() == 2000,
                  "auto uses introhud.wav after reset, not the first directory entry");

            /* Boot cache layout: SmLogo_v2.wav exists, introhud.m2v has no
             * sibling .wav. auto must NOT inherit SmLogo's duration. */
            remove(p_hud);
            {
                FILE* mf = fopen(p_hud_m, "wb");
                if (mf) { fputs("m2v", mf); fclose(mf); }
            }
            snprintf(g_cache_path, sizeof g_cache_path, "%s", p_hud_m);
            movie_done_timebased_reset();
            CHECK(movie_done_interval_ms() == 0,
                  "auto with introhud.m2v and no sibling wav does not reuse SmLogo duration");
            CHECK(movie_done_interval_ms() != 1000,
                  "auto orphan introhud is not SmLogo_v2.wav 1000 ms");
            remove(p_hud_m);

            unsetenv("PS3_MOVIE_DONE_MS");
            setenv("PS3_MOVIE_DONE_MS", "50", 1);
            movie_done_timebased_reset();
            CHECK(movie_done_interval_ms() == 50, "numeric DONE_MS recomputed after reset");
            CHECK(movie_done_timebased_poll(5) == 0, "new Play is not immediately done");
            {
                struct timespec ts;
                ts.tv_sec = 0;
                ts.tv_nsec = 80 * 1000000L;
                nanosleep(&ts, NULL);
            }
            CHECK(movie_done_timebased_poll(5) == 1, "fires after this Play's own interval");
            movie_done_timebased_reset();
            CHECK(movie_done_timebased_poll(5) == 0,
                  "reset + new st620 does not report leftover done");
            unsetenv("PS3_MOVIE_DONE_MS");
            movie_done_timebased_reset();
            CHECK(movie_done_interval_ms() == 0, "unset DONE_MS is off after reset");
            remove(p_logo); remove(p_hud);
            rmdir(dir);
            g_cache_path[0] = 0;
        }
    }

    printf("== cutscene clock: samples the picture comparison already reads ==\n");
    {
        const uint32_t sess = 0x40;
        const uint32_t samples_ea = sess + 0x154u;
        const uint32_t voice_ea = sess + 0x1C4u;
        const uint32_t voice = 3u;
        const uint32_t voice_pos = 0x008AF900u + voice * 0x300u + 0x14u;
        uint32_t got = 0;
        uint32_t voice_got = 0;
        uint32_t a, b, c;
        buf[samples_ea + 0] = 0;
        buf[samples_ea + 1] = 0;
        buf[samples_ea + 2] = 0;
        buf[samples_ea + 3] = 0x63;
        buf[voice_pos + 3] = 0x11;
        /* Voice not assigned yet: publishing must not touch either field.
         * FUN_00461fe8 would copy a zero position back over +0x154. */
        buf[voice_ea + 0] = 0xFF; buf[voice_ea + 1] = 0xFF;
        buf[voice_ea + 2] = 0xFF; buf[voice_ea + 3] = 0xFF;
        CHECK(movie_cutscene_publish_samples(sess, 0, 1000) == 0,
              "inactive cutscene does not advance playback");
        CHECK(movie_cutscene_publish_samples(0, 1, 1000) == 0,
              "no stream object does not report a clock");
        CHECK(movie_cutscene_publish_samples(sess, 1, 1000) == 0,
              "unassigned voice does not publish samples that the copy would wipe");
        CHECK(movie_eos_peek32(samples_ea, &got) == 1 && got == 0x63u,
              "failed publish leaves stream+0x154 alone");
        CHECK(movie_eos_peek32(voice_pos, &voice_got) == 1 && voice_got == 0x11u,
              "failed publish leaves the voice position alone");
        /* Voice is assigned, but the table poke fails. Nothing may change:
         * a sample write here would be the value FUN_00461fe8 then zeros. */
        buf[voice_ea + 0] = 0; buf[voice_ea + 1] = 0;
        buf[voice_ea + 2] = 0; buf[voice_ea + 3] = (unsigned char)voice;
        g_voice_table_committed = 0;
        CHECK(movie_cutscene_publish_samples(sess, 1, 1000) == 0,
              "failed voice poke returns 0");
        CHECK(movie_eos_peek32(samples_ea, &got) == 1 && got == 0x63u,
              "failed voice poke does not write stream+0x154");
        g_voice_table_committed = 1;
        buf[voice_ea + 0] = 0; buf[voice_ea + 1] = 0;
        buf[voice_ea + 2] = 0; buf[voice_ea + 3] = (unsigned char)voice;
        a = movie_cutscene_publish_samples(sess, 1, 0);
        b = movie_cutscene_publish_samples(sess, 1, 1000);
        c = movie_cutscene_publish_samples(sess, 1, 2000);
        CHECK(a == 0, "open sequence at t0 has not released a later picture");
        CHECK(b > a && c > b, "successive samples increase while the cutscene is active");
        CHECK(movie_eos_peek32(samples_ea, &got) == 1 && got == c,
              "published samples are stream+0x154");
        CHECK(movie_eos_peek32(voice_pos, &voice_got) == 1 && voice_got == c,
              "published samples are the voice position the stream update copies");
        CHECK(movie_picture_eligible(a, 3000) == 0,
              "picture after the first stays ineligible at t0");
        CHECK(movie_picture_eligible(b, 3000) == 1,
              "one second of playback releases the next picture");
        CHECK(movie_picture_eligible(0, 0) == 1,
              "the first picture is eligible at sample 0");
        {
            uint32_t idle = movie_cutscene_publish_samples(sess, 0, 3000);
            CHECK(idle == 0, "clock stops when the video is no longer open");
            CHECK(movie_eos_peek32(samples_ea, &got) == 1 && got == c,
                  "inactive publish leaves the last open-sequence count");
            printf("clock open %u -> %u -> %u; closed %u\n", a, b, c, idle);
        }
    }

    printf("== publish goes through movie_audio_resolve_session ==\n");
    {
        const uint32_t toc_slot = 0x541178u - 0x394u;
        const uint32_t table = 0x1000u;
        const uint32_t handle = 0x00A10001u;
        const uint32_t voice = 3u;
        const uint32_t samples_ea = table + 0x154u;
        const uint32_t voice_ea = table + 0x1C4u;
        const uint32_t voice_pos = 0x008AF900u + voice * 0x300u + 0x14u;
        uint32_t got = 0, voice_got = 0, n;
        buf[toc_slot + 0] = 0; buf[toc_slot + 1] = 0;
        buf[toc_slot + 2] = 0x10; buf[toc_slot + 3] = 0x00;
        buf[table + 0] = 0; buf[table + 1] = 0xA1;
        buf[table + 2] = 0; buf[table + 3] = 0x01;
        buf[voice_ea + 0] = 0; buf[voice_ea + 1] = 0;
        buf[voice_ea + 2] = 0; buf[voice_ea + 3] = (unsigned char)voice;
        buf[samples_ea + 0] = 0; buf[samples_ea + 1] = 0;
        buf[samples_ea + 2] = 0; buf[samples_ea + 3] = 0x63;
        CHECK(movie_cutscene_publish_for_handle(0, 1, 1000) == 0,
              "handle 0 does not publish");
        CHECK(movie_cutscene_publish_for_handle(0x00B20002u, 1, 1000) == 0,
              "unresolved handle does not publish");
        CHECK(movie_eos_peek32(samples_ea, &got) == 1 && got == 0x63u,
              "unresolved handle leaves stream+0x154 alone");
        g_voice_table_committed = 0;
        CHECK(movie_cutscene_publish_for_handle(handle, 1, 1000) == 0,
              "resolved session with a failed voice poke returns 0");
        CHECK(movie_eos_peek32(samples_ea, &got) == 1 && got == 0x63u,
              "failed voice poke through resolve writes nothing");
        g_voice_table_committed = 1;
        n = movie_cutscene_publish_for_handle(handle, 1, 1000);
        CHECK(n > 0, "resolved handle publishes a moving clock");
        CHECK(movie_eos_peek32(samples_ea, &got) == 1 && got == n,
              "resolve path wrote stream+0x154");
        CHECK(movie_eos_peek32(voice_pos, &voice_got) == 1 && voice_got == n,
              "resolve path wrote the voice position");
    }

    printf("== skip completes the open video; sticky end does not ==\n");
    CHECK(movie_open_sequence_end(1, 1, 1, 0, 0) == MOVIE_SEQ_END_NATURAL,
          "natural end of the open video");
    CHECK(movie_open_sequence_end(1, 1, 0, 1, 0) == MOVIE_SEQ_END_SKIP,
          "real START/CROSS skip is the same kind of end");
    CHECK(movie_open_sequence_end(1, 1, 1, 0, 0) != MOVIE_SEQ_END_NONE
          && movie_open_sequence_end(1, 1, 0, 1, 0) != MOVIE_SEQ_END_NONE
          && movie_open_sequence_end(1, 1, 0, 1, 0) != MOVIE_SEQ_END_DROP_STALE,
          "skip and natural end both finish the open sequence");
    CHECK(movie_open_sequence_end(1, 1, 0, 1, 1) == MOVIE_SEQ_END_NONE,
          "automatic pad during a cutscene does not skip");
    CHECK(movie_open_sequence_end(0, 1, 1, 0, 0) == MOVIE_SEQ_END_NONE,
          "end-of-movie does not arm before the sequence is open");
    CHECK(movie_open_sequence_end(0, 0, 0, 1, 0) == MOVIE_SEQ_END_NONE,
          "skip with no open video does not finish an earlier scene");
    CHECK(movie_open_sequence_end(1, 0, 0, 1, 0) == MOVIE_SEQ_END_DROP_STALE,
          "skip of an earlier host movie does not EOS the open sequence");
    CHECK(movie_end_arms_boot_logos(0, 0) == 1, "intro end may arm the logo queue");
    CHECK(movie_end_arms_boot_logos(1, 0) == 0, "a later movie end does not re-arm logos");
    CHECK(movie_end_arms_boot_logos(0, 1) == 0, "dropping a stale picture does not re-arm logos");
    CHECK(movie_timebase_done_allowed(0) == 1, "intro may still use its own timer");
    CHECK(movie_timebase_done_allowed(1) == 0, "in-game cutscene is not ended by the intro timer");
    CHECK(movie_host_picture_is_current_path("/cache/SmLogo_v2.m2v", 1, 1) == 0,
          "logo is not the picture of an in-game sequence");
    CHECK(movie_host_picture_is_current_path("/cache/introhud.m2v", 1, 1) == 1,
          "introhud is the first in-game picture");
    CHECK(movie_host_picture_is_current_path("/cache/introhud.m2v", 1, 2) == 0,
          "a later cutscene does not keep introhud as the current picture");
    CHECK(movie_host_picture_is_current_path("/cache/rhodes.m2v", 1, 2) == 1,
          "the open cutscene's own file stays the current picture");
    CHECK(movie_ingame_picture("/c/SmLogo_v2.m2v", 1, "/c/introhud.m2v", 0)
              != NULL
          && strstr(movie_ingame_picture("/c/SmLogo_v2.m2v", 1, "/c/introhud.m2v", 0),
                    "introhud") != NULL,
          "first in-game sequence can take the opener");
    CHECK(movie_ingame_picture("/c/introhud.m2v", 1, "/c/introhud.m2v", 1) == NULL,
          "a later end does not reselect introhud");
    CHECK(movie_ingame_picture("/c/rhodes.m2v", 0, "/c/introhud.m2v", 1)
              != NULL
          && strstr(movie_ingame_picture("/c/rhodes.m2v", 0, "/c/introhud.m2v", 1),
                    "rhodes") != NULL,
          "a later sequence keeps its own file");
    printf("policy end natural=%d skip=%d autopad=%d before_open=%d earlier=%d logos_later=%d eos_opening=%d\n",
           movie_open_sequence_end(1, 1, 1, 0, 0),
           movie_open_sequence_end(1, 1, 0, 1, 0),
           movie_open_sequence_end(1, 1, 0, 1, 1),
           movie_open_sequence_end(0, 1, 1, 1, 0),
           movie_open_sequence_end(1, 0, 0, 1, 0),
           movie_end_arms_boot_logos(1, 0),
           movie_eos_should_arm(1, 0, 1, 4));

    munmap(buf, 0x900000u);
    vm_base = NULL;

    if (g_fail) { printf("\nFAIL: %d assercao(oes)\n", g_fail); return 1; }
    printf("\nPASS: test_movie_eos_policy\n");
    return 0;
}
