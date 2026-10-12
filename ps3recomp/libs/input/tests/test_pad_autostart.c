/*
 * Drives pad_autostart_buttons — the same function cellPadGetData uses for
 * PS3_PAD_AUTOSTART. Asserts the four-phase sequence and the digital1/digital2
 * split the HLE writes (CROSS must land in digital2 as 0x40, not 0x4000).
 */
#include "cellPad.h"
#include "../../video/movie_clock.h"
#include <stdio.h>

int main(void)
{
    int ok = 1;
#define CHECK(cond) do { if (!(cond)) { printf("  [FAIL] %s\n", #cond); ok = 0; } } while (0)

    /* Movie skip (t<12s): START then CROSS. */
    CHECK(pad_autostart_buttons_at(0, 0) == CELL_PAD_CTRL_START);
    CHECK(pad_autostart_buttons_at(7, 10) == CELL_PAD_CTRL_START);
    CHECK(pad_autostart_buttons_at(8, 10) == 0);
    CHECK(pad_autostart_buttons_at(100, 11) == CELL_PAD_CTRL_CROSS);

    /* 12-35s: TRIANGLE only (Back on Power-Up; 28s was still Power-Up). */
    CHECK(pad_autostart_buttons_at(100, 12) == CELL_PAD_CTRL_TRIANGLE);
    CHECK(pad_autostart_buttons_at(0, 13) == 0);
    CHECK(pad_autostart_buttons_at(0, 20) == CELL_PAD_CTRL_TRIANGLE);
    CHECK(pad_autostart_buttons_at(100, 34) == CELL_PAD_CTRL_TRIANGLE);
    CHECK(pad_autostart_buttons_at_ms(0, 28000) == CELL_PAD_CTRL_TRIANGLE);

    /* 35-42s: idle so TRIANGLE does not Back the title (nav_40s). */
    CHECK(pad_autostart_buttons_at_ms(0, 35000) == 0);
    CHECK(pad_autostart_buttons_at_ms(0, 40000) == 0);
    CHECK(pad_autostart_buttons_at_ms(0, 41999) == 0);

    /* Visual-probe mode reaches the title, then leaves the virtual pad neutral. */
    CHECK(pad_autostart_buttons_hold_title_at_ms(0, 12000) == CELL_PAD_CTRL_TRIANGLE);
    CHECK(pad_autostart_buttons_hold_title_at_ms(0, 41999) == 0);
    CHECK(pad_autostart_buttons_hold_title_at_ms(0, 42000) == 0);
    CHECK(pad_autostart_buttons_hold_title_at_ms(8, 44000) == 0);

    /* Hand-off: idle host input keeps the virtual pad; the first real button
     * releases port 0 for good, even after the key is let go. */
    {
        int latch = 0;
        CHECK(pad_autostart_user_took_over(&latch, 0) == 0);
        CHECK(pad_autostart_user_took_over(&latch, CELL_PAD_CTRL_CROSS) == 1);
        CHECK(pad_autostart_user_took_over(&latch, 0) == 1);
        CHECK(pad_autostart_user_took_over(NULL, CELL_PAD_CTRL_CROSS) == 0);
    }

    /* Cutscene skip: only on the press of START or CROSS, never while held,
     * and never when the feature is off. */
    CHECK(pad_movie_skip_pressed(0, CELL_PAD_CTRL_START, 1) == 1);
    CHECK(pad_movie_skip_pressed(0, CELL_PAD_CTRL_CROSS, 1) == 1);
    CHECK(pad_movie_skip_pressed(CELL_PAD_CTRL_START, CELL_PAD_CTRL_START, 1) == 0);
    CHECK(pad_movie_skip_pressed(0, CELL_PAD_CTRL_TRIANGLE, 1) == 0);
    CHECK(pad_movie_skip_pressed(0, CELL_PAD_CTRL_START, 0) == 0);
    CHECK(pad_movie_skip_should_fire(0, CELL_PAD_CTRL_CROSS, 1, 0) == 1);
    CHECK(pad_movie_skip_should_fire(0, CELL_PAD_CTRL_CROSS, 1, 1) == 0);
    /* O que decide e' a ORIGEM do toque, nao a cutscene: movie_hle marca
     * cutscene em todo filme que nao e' logo, e travar por isso deixava o
     * jogador sem skip -- o defeito que o utilizador apanhou a jogar. */
    pad_autostart_note_cutscene(1);
    CHECK(pad_autostart_in_cutscene() == 1);
    pad_autoplay_note_gameplay();
    CHECK(pad_autostart_in_cutscene() == 1);
    /* pad automatico a carregar dentro do filme: nao salta */
    CHECK(pad_movie_skip_should_fire(0, CELL_PAD_CTRL_CROSS, 1,
                                    1 && pad_autostart_in_cutscene()) == 0);
    /* jogador a carregar no MESMO filme: salta */
    CHECK(pad_movie_skip_should_fire(0, CELL_PAD_CTRL_CROSS, 1,
                                    0 && pad_autostart_in_cutscene()) == 1);
    CHECK(pad_movie_skip_should_fire(0, CELL_PAD_CTRL_START, 1,
                                    0 && pad_autostart_in_cutscene()) == 1);
    pad_autostart_note_cutscene(0);
    CHECK(pad_autostart_in_cutscene() == 0);
    /* fora do filme o pad automatico continua a poder saltar os logos */
    CHECK(pad_movie_skip_should_fire(0, CELL_PAD_CTRL_CROSS, 1,
                                    1 && pad_autostart_in_cutscene()) == 1);
    /* O pulo real termina a sequencia aberta, como o fim natural. O pad
     * automatico, durante o filme, nao. Sem sequencia aberta o toque nao
     * arma fim-de-filme. Um filme anterior sai sem EOS e sem logos. */
    {
        int natural = movie_open_sequence_end(1, 1, 1, 0, 0);
        int skip_cross = movie_open_sequence_end(
            1, 1, 0, pad_movie_skip_should_fire(0, CELL_PAD_CTRL_CROSS, 1, 0), 0);
        int skip_start = movie_open_sequence_end(
            1, 1, 0, pad_movie_skip_should_fire(0, CELL_PAD_CTRL_START, 1, 0), 0);
        int autopad = movie_open_sequence_end(
            1, 1, 0, pad_movie_skip_should_fire(0, CELL_PAD_CTRL_CROSS, 1, 1), 1);
        int before_open = movie_open_sequence_end(
            0, 1, 1, pad_movie_skip_should_fire(0, CELL_PAD_CTRL_START, 1, 0), 0);
        int earlier = movie_open_sequence_end(
            1, 0, 0, pad_movie_skip_should_fire(0, CELL_PAD_CTRL_CROSS, 1, 0), 0);
        CHECK(natural == MOVIE_SEQ_END_NATURAL);
        CHECK(skip_cross == MOVIE_SEQ_END_SKIP);
        CHECK(skip_start == MOVIE_SEQ_END_SKIP);
        CHECK(natural != MOVIE_SEQ_END_NONE && natural != MOVIE_SEQ_END_DROP_STALE);
        CHECK(skip_cross != MOVIE_SEQ_END_NONE && skip_cross != MOVIE_SEQ_END_DROP_STALE);
        CHECK(skip_start != MOVIE_SEQ_END_NONE && skip_start != MOVIE_SEQ_END_DROP_STALE);
        CHECK(autopad == MOVIE_SEQ_END_NONE);
        CHECK(before_open == MOVIE_SEQ_END_NONE);
        CHECK(earlier == MOVIE_SEQ_END_DROP_STALE);
        CHECK(movie_end_arms_boot_logos(1, 0) == 0);
        CHECK(movie_end_arms_boot_logos(0, 1) == 0);
        CHECK(movie_host_picture_is_current_path("/c/SmLogo_v2.m2v", 1, 2) == 0);
        CHECK(movie_host_picture_is_current_path("/c/introhud.m2v", 1, 2) == 0);
        CHECK(movie_ingame_picture("/c/introhud.m2v", 1, "/c/introhud.m2v", 1) == NULL);
        printf("pad end natural=%d cross=%d start=%d autopad=%d before_open=%d earlier=%d logos_later=%d\n",
               natural, skip_cross, skip_start, autopad, before_open, earlier,
               movie_end_arms_boot_logos(1, 0));
    }

    /* 42s+: 250ms CROSS / 1750ms idle, then idle after 4 pulses so the
     * New Game opener is not CROSS-skipped. */
    CHECK(pad_autostart_buttons_at_ms(0, 42000) == CELL_PAD_CTRL_CROSS);
    CHECK(pad_autostart_buttons_at_ms(99, 42249) == CELL_PAD_CTRL_CROSS);
    CHECK(pad_autostart_buttons_at_ms(0, 42250) == 0);
    CHECK(pad_autostart_buttons_at_ms(0, 43999) == 0);
    CHECK(pad_autostart_buttons_at_ms(8, 44000) == CELL_PAD_CTRL_CROSS);
    CHECK(pad_autostart_buttons_at(0, 90) != CELL_PAD_CTRL_START);

    /* Combined-mask split used by cellPadGetData. */
    CHECK((pad_autostart_buttons_at(0, 0) & 0xFFu) == CELL_PAD_CTRL_START);
    CHECK((pad_autostart_buttons_at_ms(0, 42000) >> 8) == 0x40u);
    CHECK((pad_autostart_buttons_at_ms(0, 42000) & 0xFFu) == 0);

    /* GetData len: first sample is a change; repeats are 0; a new glyph is 8. */
    {
        u16 prev = 0;
        int seen = 0;
        CHECK(pad_report_len(CELL_PAD_CTRL_CROSS, &prev, &seen, 0)
              == CELL_PAD_LEN_CHANGE_DEFAULT);
        CHECK(pad_report_len(CELL_PAD_CTRL_CROSS, &prev, &seen, 0)
              == CELL_PAD_LEN_NO_CHANGE);
        CHECK(pad_report_len(0, &prev, &seen, 0)
              == CELL_PAD_LEN_CHANGE_DEFAULT);
        CHECK(pad_report_len(0, &prev, &seen, 0)
              == CELL_PAD_LEN_NO_CHANGE);
        CHECK(pad_report_len(CELL_PAD_CTRL_CROSS, &prev, &seen,
                             CELL_PAD_SETTING_PRESS_ON)
              == CELL_PAD_LEN_CHANGE_PRESS_ON);
    }

    /* Sticks count as a change: releasing W (left stick back to 0x80 with no
     * button change) must be reported, or the game keeps the last sample and
     * Kratos walks forever. */
    {
        u16 prev = 0;
        u32 prev_axes = 0;
        int seen = 0;
        const u32 neutral = 0x80808080u, walking = 0x80800080u; /* LY = 0 */
        CHECK(pad_report_len_axes(0, neutral, &prev, &prev_axes, &seen, 0)
              == CELL_PAD_LEN_CHANGE_DEFAULT);
        CHECK(pad_report_len_axes(0, walking, &prev, &prev_axes, &seen, 0)
              == CELL_PAD_LEN_CHANGE_DEFAULT);
        CHECK(pad_report_len_axes(0, walking, &prev, &prev_axes, &seen, 0)
              == CELL_PAD_LEN_NO_CHANGE);
        CHECK(pad_report_len_axes(0, neutral, &prev, &prev_axes, &seen, 0)
              == CELL_PAD_LEN_CHANGE_DEFAULT);   /* the release */
        CHECK(pad_report_len_axes(0, neutral, &prev, &prev_axes, &seen, 0)
              == CELL_PAD_LEN_NO_CHANGE);
    }

    /* E476 -- a fase de titulo alterna CROSS e START, para o harness se
     * recuperar quando a deriva de carga desalinha o roteiro por relogio. */
    {
        const unsigned T = 42000u;   /* PAD_TITLE_READY_MS */
        CHECK(pad_autostart_buttons_at_ms(0, T + 0)    == CELL_PAD_CTRL_CROSS);
        CHECK(pad_autostart_buttons_at_ms(0, T + 2000) == CELL_PAD_CTRL_CROSS);
        CHECK(pad_autostart_buttons_at_ms(0, T + 4000) == CELL_PAD_CTRL_CROSS);
        CHECK(pad_autostart_buttons_at_ms(0, T + 6000) == CELL_PAD_CTRL_START);
        /* 5th pulse would have been CROSS; now idle so the opener is not skipped */
        CHECK(pad_autostart_buttons_at_ms(0, T + 8000) == 0);
        CHECK(pad_autostart_buttons_at_ms(0, T + 20000) == 0);
        CHECK(pad_autostart_buttons_at_ms(0, T + 90000) == 0);
        /* solto entre toques, senao nao ha' flanco de subida */
        CHECK(pad_autostart_buttons_at_ms(0, T + 250)  == 0);
        CHECK(pad_autostart_buttons_at_ms(0, T + 1999) == 0);
        /* TRIANGLE nunca aparece nesta fase: no Power-Up e' "Back" */
        for (unsigned k = 0; k < 12u; k++)
            CHECK(pad_autostart_buttons_at_ms(0, T + k * 2000u) != CELL_PAD_CTRL_TRIANGLE);
    }

    /* Opening cinematic hold: after New Game, skip buttons stay idle. */
    {
        CHECK(pad_autostart_buttons_during_cutscene_at_ms(0, 10000)
              == CELL_PAD_CTRL_START); /* intro skip still works */
        CHECK(pad_autostart_buttons_during_cutscene_at_ms(0, 42000) == 0);
        CHECK(pad_autostart_buttons_during_cutscene_at_ms(8, 44000) == 0);
        CHECK(pad_autostart_buttons_during_cutscene_at_ms(0, 90000) == 0);
        pad_autostart_note_cutscene(1);
        pad_autostart_note_cutscene(0);
    }

    /* E465 -- rotacao de combate: cinco slots, SQUARE duas vezes, e solto
     * entre toques (sem flanco de subida o guest nao ve' um toque novo). */
    {
        CHECK(pad_autoplay_buttons_at_ms(0)    == CELL_PAD_CTRL_SQUARE);
        CHECK(pad_autoplay_buttons_at_ms(700)  == CELL_PAD_CTRL_SQUARE);
        CHECK(pad_autoplay_buttons_at_ms(1400) == CELL_PAD_CTRL_TRIANGLE);
        CHECK(pad_autoplay_buttons_at_ms(2100) == CELL_PAD_CTRL_CROSS);
        CHECK(pad_autoplay_buttons_at_ms(2800) == CELL_PAD_CTRL_CIRCLE);
        CHECK(pad_autoplay_buttons_at_ms(3500) == CELL_PAD_CTRL_SQUARE); /* volta ao inicio */
        /* solto depois dos 150 ms de hold, em qualquer slot */
        CHECK(pad_autoplay_buttons_at_ms(150)  == 0);
        CHECK(pad_autoplay_buttons_at_ms(699)  == 0);
        CHECK(pad_autoplay_buttons_at_ms(1550) == 0);
        /* e' mesmo alternado: dois slots seguidos nunca dao o mesmo par
         * (SQUARE,SQUARE) seguido de outro (SQUARE,SQUARE) sem variar */
        {
            int vistos = 0;
            u16 b;
            for (unsigned k = 0; k < 5u; k++) {
                b = pad_autoplay_buttons_at_ms(k * 700u);
                if (b == CELL_PAD_CTRL_SQUARE || b == CELL_PAD_CTRL_TRIANGLE
                    || b == CELL_PAD_CTRL_CROSS || b == CELL_PAD_CTRL_CIRCLE)
                    vistos |= (int)b;
            }
            /* os quatro glifos aparecem no ciclo */
            CHECK((vistos & CELL_PAD_CTRL_SQUARE) != 0);
            CHECK((vistos & CELL_PAD_CTRL_TRIANGLE) != 0);
            CHECK((vistos & CELL_PAD_CTRL_CROSS) != 0);
            CHECK((vistos & CELL_PAD_CTRL_CIRCLE) != 0);
        }
    }

#undef CHECK
    printf("%s\n", ok ? "test_pad_autostart: PASS" : "test_pad_autostart: FAIL");
    return ok ? 0 : 1;
}
