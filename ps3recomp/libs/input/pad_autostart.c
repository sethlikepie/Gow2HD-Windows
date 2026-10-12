/*
 * PS3_PAD_AUTOSTART sequence — the button mask for a given cellPadGetData
 * call index. Extracted so the same function the HLE calls is unit-tested.
 *
 * Why four glyphs (was START then CROSS only): E466's Power-Up overlay
 * binds CROSS = "Power Up", not New Game; E439b ran ~25 min of START+CROSS
 * with LDRSH still 0. TRIANGLE = Back, DOWN = menu nav, CROSS = confirm.
 *
 * Question: after the frontend plateau, does this sequence let the title
 * leave the start screens so ICGLdrShader (func_0032109C / [LDRSH]) runs?
 *   P1 — yes: LDRSH>0 on the natural path.
 *   P2 — PADTAP fires, still LDRSH=0: producer/queue, not the glyphs.
 */
#include "cellPad.h"
#include <time.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#define PAD_AUTOSTART_PERIOD 200u
#define PAD_MOVIE_MS 12000u
/* TRIANGLE=Back on Power-Up (×=Power Up). Title is up at ~40s
 * (nav_40s.png); TRIANGLE there is Back and dumps the menu into attract.
 * Stop Back at 35s, idle until 42s, then Select. */
#define PAD_TRIANGLE_UNTIL_MS 35000u
#define PAD_TITLE_READY_MS 42000u
/* CROSS=Select New Game. 250ms down / 1750ms up: 200ms/1s still sat on
 * NEW GAME (nav_140s, frame_30000 diorama with no confirm). */
#define PAD_CROSS_HOLD_MS 250u
#define PAD_CROSS_PERIOD_MS 2000u
/* After New Game is confirmed, further CROSS/START skip the Rhodes/Athena
 * opener. Keep 4 pulses (3 CROSS + 1 START over 8 s) then idle; a cutscene
 * latch idles immediately even inside that window. */
#define PAD_NEWGAME_CONFIRM_PULSES 4u

u16 pad_autostart_buttons_at_ms(unsigned call_index, unsigned elapsed_ms)
{
    if (elapsed_ms < PAD_MOVIE_MS) {
        const unsigned phase = call_index % PAD_AUTOSTART_PERIOD;
        if (phase < 8u)
            return CELL_PAD_CTRL_START;
        if (phase >= 100u && phase < 108u)
            return CELL_PAD_CTRL_CROSS;
        return 0;
    }
    if (elapsed_ms < PAD_TRIANGLE_UNTIL_MS) {
        unsigned t = elapsed_ms - PAD_MOVIE_MS;
        return ((t / 1000u) % 2u == 0u) ? CELL_PAD_CTRL_TRIANGLE : 0;
    }
    if (elapsed_ms < PAD_TITLE_READY_MS)
        return 0;
    /* E476: a fase final era CROSS para sempre, e a sequencia inteira e'
     * roteirizada por RELOGIO DE PAREDE (START aos 12 s, TRIANGLE ate' 35 s,
     * CROSS a partir dos 42 s). Isso assume um tempo de carregamento fixo.
     *
     * MEDIDO hoje: com a maquina a carga 10 (tres instancias do jogo a correr em
     * paralelo) o boot fica mais lento, cada glifo cai no ecra' errado e o
     * gameplay NUNCA comeca -- o mesmo binario que dera 125 linhas de [FPS] e
     * 45,5 fps medianos passou a dar zero. Foram quatro corridas perdidas assim,
     * e cheguei a atribuir a falha a codigo alheio antes de o controlo (re-correr
     * o binario bom) mostrar que o problema era o harness.
     *
     * Agora a fase final alterna CROSS e START em vez de so' CROSS. Os dois
     * AVANCAM (confirmar / saltar) e nenhum volta atras -- TRIANGLE fica de fora
     * de proposito, porque no ecra' Power-Up e' "Back" e despeja o menu no
     * atract. Assim, seja qual for o ecra' em que a deriva de tempo nos deixou,
     * um dos dois destranca, e a sequencia deixa de depender de o boot demorar
     * exactamente o que demorava numa maquina vazia. */
    const unsigned t = elapsed_ms - PAD_TITLE_READY_MS;
    const unsigned pulse = t / PAD_CROSS_PERIOD_MS;
    if (pulse >= PAD_NEWGAME_CONFIRM_PULSES)
        return 0;   /* idle: do not CROSS-skip the New Game opener */
    if ((t % PAD_CROSS_PERIOD_MS) >= PAD_CROSS_HOLD_MS)
        return 0;   /* solto: sem flanco de subida o guest nao ve' um toque novo */
    /* tres CROSS para cada START: CROSS e' o que confirma, START so' resgata */
    return ((t / PAD_CROSS_PERIOD_MS) % 4u == 3u) ? CELL_PAD_CTRL_START
                                                  : CELL_PAD_CTRL_CROSS;
}

u16 pad_autostart_buttons_during_cutscene_at_ms(unsigned call_index, unsigned elapsed_ms)
{
    if (elapsed_ms >= PAD_TITLE_READY_MS)
        return 0;
    return pad_autostart_buttons_at_ms(call_index, elapsed_ms);
}

u16 pad_autostart_buttons_hold_title_at_ms(unsigned call_index, unsigned elapsed_ms)
{
    if (elapsed_ms >= PAD_TITLE_READY_MS)
        return 0;
    return pad_autostart_buttons_at_ms(call_index, elapsed_ms);
}

int pad_movie_skip_pressed(u16 prev_buttons, u16 now_buttons, int enabled)
{
    const u16 keys = (u16)(CELL_PAD_CTRL_START | CELL_PAD_CTRL_CROSS);
    if (!enabled) return 0;
    return (now_buttons & ~prev_buttons & keys) != 0;
}

/* `suppress` = este toque nao e' do jogador (vem do pad automatico) durante um
 * filme. Era `cutscene_hold`, ou seja "estamos numa cutscene" -- e como
 * movie_hle liga esse sinalizador em TODA cutscene que nao e' logo, o skip
 * ficava morto exactamente quando o jogador o quer. O que tem de ser travado e'
 * o CROSS do pad automatico, nao o dedo de quem joga. */
int pad_movie_skip_should_fire(u16 prev_buttons, u16 now_buttons, int enabled,
                               int suppress)
{
    if (suppress) return 0;
    return pad_movie_skip_pressed(prev_buttons, now_buttons, enabled);
}

int pad_autostart_user_took_over(int* latch, u16 host_buttons)
{
    if (!latch) return 0;
    if (!*latch && host_buttons != 0)
        *latch = 1;
    return *latch;
}

/* --- Rotacao de combate (E465) -------------------------------------------
 *
 * Depois de PAD_TITLE_READY_MS a sequencia ficava em CROSS para sempre. No GoW2
 * CROSS e' SALTAR: o resultado e' o Kratos a pular no sitio indefinidamente, que
 * nao exercita combate nenhum -- nem os sistemas que mais custam no frame
 * (particulas de sangue: fp=59864B76 e' 47% dos draws quando ha' combate).
 *
 * A rotacao usa os glifos do combate do jogo:
 *   SQUARE   ataque rapido   (o pedido do usuario; e' o que encadeia combos)
 *   TRIANGLE ataque pesado
 *   CROSS    saltar          (mantido: tambem e' "confirmar" se aparecer prompt)
 *   CIRCLE   agarrar / accao de contexto
 *
 * SQUARE aparece duas vezes no ciclo de cinco porque e' o ataque base e o que
 * mantem o combate a correr.
 *
 * Cadencia mais rapida que a dos menus (150 ms carregado / 700 ms de periodo):
 * a de 250/2000 ms foi afinada para o Scaleform ver um flanco de subida em
 * NEW GAME, nao para encadear golpes.
 *
 * Isto MUDA A CENA medida: com combate os draws e o fps deixam de ser
 * comparaveis com corridas anteriores (ver o aviso do fps_aligned.py sobre a
 * mediana de draws). PS3_PAD_AUTOPLAY=0 repoe o CROSS-so' para medicoes A/B
 * que precisem da cena antiga. */
#define PAD_PLAY_HOLD_MS   150u
#define PAD_PLAY_PERIOD_MS 700u

u16 pad_autoplay_buttons_at_ms(unsigned since_gameplay_ms)
{
    static const u16 cycle[5] = {
        CELL_PAD_CTRL_SQUARE,
        CELL_PAD_CTRL_SQUARE,
        CELL_PAD_CTRL_TRIANGLE,
        CELL_PAD_CTRL_CROSS,
        CELL_PAD_CTRL_CIRCLE,
    };
    const unsigned slot = since_gameplay_ms / PAD_PLAY_PERIOD_MS;
    const unsigned phase = since_gameplay_ms % PAD_PLAY_PERIOD_MS;
    if (phase >= PAD_PLAY_HOLD_MS)
        return 0;   /* solto: sem flanco de subida o guest nao ve' um toque novo */
    return cycle[slot % 5u];
}

u16 pad_autostart_buttons_at(unsigned call_index, unsigned elapsed_sec)
{
    return pad_autostart_buttons_at_ms(call_index, elapsed_sec * 1000u);
}

/* libpad / RPCS3: len=0 means "unchanged since last GetData". We used to
 * always return 8/24, so Scaleform never saw a CROSS rising edge — 400s of
 * TAS CROSS stayed on the frontend 3D diorama, no R_Colsus, LDRSH=0. */
s32 pad_report_len(u16 now_buttons, u16 *prev_buttons, int *seen, u32 setting)
{
    u32 axes = 0;
    return pad_report_len_axes(now_buttons, 0, prev_buttons, &axes, seen, setting);
}

s32 pad_report_len_axes(u16 now_buttons, u32 now_axes, u16 *prev_buttons,
                        u32 *prev_axes, int *seen, u32 setting)
{
    int changed = !*seen || now_buttons != *prev_buttons || now_axes != *prev_axes;
    *prev_buttons = now_buttons;
    *prev_axes = now_axes;
    *seen = 1;
    if (!changed)
        return CELL_PAD_LEN_NO_CHANGE;
    if (setting & CELL_PAD_SETTING_SENSOR_ON)
        return CELL_PAD_LEN_CHANGE_SENSOR_ON;
    if (setting & CELL_PAD_SETTING_PRESS_ON)
        return CELL_PAD_LEN_CHANGE_PRESS_ON;
    return CELL_PAD_LEN_CHANGE_DEFAULT;
}

unsigned pad_autostart_elapsed_ms(void)
{
    static uint64_t t0;
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0;
    uint64_t now = (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
    if (!t0) {   /* winport v32-bench: WINPORT_PAD_DELAY_MS shifts the autoplay script (slow boots) */
        const char* e = getenv("WINPORT_PAD_DELAY_MS");
        long d = (e && *e) ? strtol(e, NULL, 0) : 0;
        if (d < 0 || d > 600000) d = 0;
        t0 = now + (uint64_t)d * 1000000ull;
    }
    if (now < t0) return 0;
    return (unsigned)((now - t0) / 1000000ull);
}

/* PS3_PAD_AUTOPLAY=0 desliga a rotacao de combate e volta ao CROSS-so'. */
static int pad_autoplay_on(void)
{
    static int on = -1;
    if (on < 0) { const char* e = getenv("PS3_PAD_AUTOPLAY"); on = (e && e[0] == '0') ? 0 : 1; }
    return on;
}

/* Quem sabe que o gameplay comecou e' o backend de video (e' ele que conta os
 * draws do frame). Em vez de este modulo procurar um simbolo do runtime do PPU
 * -- que no macOS nao pode ser fraco porque vem de um arquivo estatico, e o
 * linker falha em vez de o dar como nulo -- a dependencia e' invertida: o
 * backend chama pad_autoplay_note_gameplay() e os dois vivem na mesma lib. */
static int g_pad_in_gameplay;
void pad_autoplay_note_gameplay(void) { g_pad_in_gameplay = 1; }

static int g_pad_cutscene;
static int g_wp_seen_cut;   /* winport v33-autoplay: first in-game movie (New Game opener) seen */
static int wp_autoskip(void) { static int a = -1; if (a < 0) { const char* e = getenv("WINPORT_AUTOSKIP"); a = (e && e[0] == '1'); } return a; }
void pad_autostart_note_cutscene(int on) { g_pad_cutscene = on ? 1 : 0; if (on) g_wp_seen_cut = 1; }

int pad_autostart_in_cutscene(void)
{
    /* Frontend diorama already trips pad_autoplay_note_gameplay() (~400 draws)
     * before New Game. CROSS from that combat rotation must not skip Rhodes. */
    return g_pad_cutscene;
}

u16 pad_autostart_buttons(unsigned call_index)
{
    unsigned elapsed_ms = pad_autostart_elapsed_ms();
    if (wp_autoskip()) { static unsigned last_s = 0xFFFFFFFFu; const unsigned s_ = elapsed_ms / 10000u;   /* winport v33-autoplay */
        if (s_ != last_s) { last_s = s_; fprintf(stderr, "[PADSTATE] t=%us cutscene=%d seen_opener=%d gameplay=%d\n",
                                elapsed_ms / 1000u, g_pad_cutscene, g_wp_seen_cut, g_pad_in_gameplay); fflush(stderr); } }
    unsigned elapsed_sec = elapsed_ms / 1000u;
    static int hold_title = -1;
    if (hold_title < 0) {
        const char* e = getenv("PS3_PAD_AUTOSTART_HOLD_TITLE");
        hold_title = e && e[0] == '1';
    }
    if (g_pad_cutscene) {
        /* winport v30-bench: WINPORT_AUTOSKIP=1 taps START once a second during a movie (cellPad lets it skip) */
        static int as = -1; if (as < 0) { const char* a = getenv("WINPORT_AUTOSKIP"); as = (a && a[0] == '1'); }
        if (as) return (elapsed_ms % 1000u) < 150u ? CELL_PAD_CTRL_START : 0;
        return pad_autostart_buttons_during_cutscene_at_ms(call_index, elapsed_ms);
    }
    if (pad_autoplay_on() && g_pad_in_gameplay && (!wp_autoskip() || g_wp_seen_cut)) {   /* winport v33-autoplay */
        /* Marca o instante em que o gameplay armou, para a rotacao comecar do
         * inicio do ciclo em vez de apanhar o relogio a meio. */
        static unsigned t_gameplay;
        if (!t_gameplay) {
            t_gameplay = elapsed_ms ? elapsed_ms : 1u;
            fprintf(stderr, "[PADPLAY] gameplay armado em %ums -- rotacao "
                            "SQUARE/SQUARE/TRIANGLE/CROSS/CIRCLE\n", elapsed_ms);
            fflush(stderr);
        }
        return pad_autoplay_buttons_at_ms(elapsed_ms - t_gameplay);
    }
    static unsigned last_log = 0xFFFFFFFFu;
    if (elapsed_sec != last_log && (elapsed_sec % 10u) == 0u) {
        fprintf(stderr, "[PADMENU] elapsed=%us ms=%u\n", elapsed_sec, elapsed_ms);
        fflush(stderr);
        last_log = elapsed_sec;
    }
    if (hold_title)
        return pad_autostart_buttons_hold_title_at_ms(call_index, elapsed_ms);
    if (wp_autoskip() && !g_wp_seen_cut) {   /* winport v33-autoplay: pulse until New Game */
        const unsigned t = elapsed_ms;   /* from launch: Sony logo -> main menu -> X New Game -> X difficulty (no TRIANGLE) */
        if ((t % PAD_CROSS_PERIOD_MS) >= PAD_CROSS_HOLD_MS) return 0;
        return ((t / PAD_CROSS_PERIOD_MS) % 4u == 3u) ? CELL_PAD_CTRL_START : CELL_PAD_CTRL_CROSS;
    }
    return pad_autostart_buttons_at_ms(call_index, elapsed_ms);
}
