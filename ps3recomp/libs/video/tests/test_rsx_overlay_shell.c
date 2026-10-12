#include <math.h>
#include <stdio.h>
#include <string.h>

#include "cellPad.h"
#include "rsx_overlay_shell.h"

#define CHECK(expr) do { \
    if (!(expr)) { fprintf(stderr, "[FAIL] %s:%d: %s\n", __func__, __LINE__, #expr); return 0; } \
} while (0)
#define NEAR(a, b) (fabs((double)(a) - (double)(b)) < 0.01)

static rsx_shell_model live_model(void)
{
    rsx_shell_model m;
    memset(&m, 0, sizeof m);
    m.support_fullscreen = m.support_vsync = m.support_window_size = RSX_OVERLAY_SETTING_LIVE;
    m.window_size_index = 0;
    m.log_lines = 10;
    m.list_visible_rows = 4;
    return m;
}

static rsx_shell_output step(rsx_shell* s, const rsx_shell_model* m, double t,
                             uint32_t pressed, uint32_t held, uint16_t pad)
{
    rsx_shell_input in;
    rsx_shell_output out;
    memset(&in, 0, sizeof in);
    in.pressed = pressed;
    in.key_pressed = pad ? 0 : pressed;
    in.held = held | pressed;
    in.pad_pressed = pad;
    rsx_shell_update(s, &in, m, t, &out);
    return out;
}

/* Press and release one key; returns the press frame's output. */
static rsx_shell_output tap(rsx_shell* s, const rsx_shell_model* m, double* t, uint32_t bit)
{
    rsx_shell_output o = step(s, m, *t, bit, 0, 0);
    *t += 0.05;
    step(s, m, *t, 0, 0, 0);
    *t += 0.05;
    return o;
}

static int has_action(const rsx_shell_output* o, int kind, int a, int b)
{
    for (int i = 0; i < o->count; ++i)
        if (o->actions[i].kind == kind && o->actions[i].a == a && o->actions[i].b == b)
            return 1;
    return 0;
}

static void settle_open(rsx_shell* s, const rsx_shell_model* m, double* t)
{
    rsx_shell_open(s, *t);
    for (int i = 0; i < 40; ++i) { step(s, m, *t, 0, 0, 0); *t += 1.0 / 60.0; }
}

static int test_spring_is_analytic_and_deterministic(void)
{
    rsx_spring a = { 0.0f, 0.0f, 1.0f }, b = { 0.0f, 0.0f, 1.0f };
    const float w = RSX_SHELL_OPEN_OMEGA;
    rsx_spring_step(&a, w, 0.22f);
    for (int i = 0; i < 22; ++i) rsx_spring_step(&b, w, 0.01f);
    CHECK(fabsf(a.value - b.value) < 1e-4f);
    CHECK(fabsf(a.value - (1.0f - (1.0f + w * 0.22f) * expf(-w * 0.22f))) < 1e-5f);
    CHECK(a.value > 0.94f && a.value < 0.97f); /* ~95 % at 220 ms */
    /* Critically damped from rest: never overshoots. */
    rsx_spring c = { 0.0f, 0.0f, 1.0f };
    for (int i = 0; i < 200; ++i) { rsx_spring_step(&c, w, 0.005f); CHECK(c.value <= 1.0f + 1e-6f); }
    rsx_spring_step(&c, w, 0.0f); /* dt 0: no change */
    return 1;
}

static int test_open_settles_and_close_returns_to_closed(void)
{
    rsx_shell s; rsx_shell_model m = live_model(); double t = 10.0;
    rsx_shell_init(&s);
    CHECK(s.phase == RSX_SHELL_CLOSED);
    rsx_shell_open(&s, t);
    rsx_shell_output o = step(&s, &m, t, 0, 0, 0);
    CHECK(o.feedback & RSX_OVERLAY_FEEDBACK_BIT(RSX_OVERLAY_FEEDBACK_OPEN));
    CHECK(s.phase == RSX_SHELL_OPENING && s.card == RSX_SHELL_CARD_RESUME);
    for (int i = 0; i < 30; ++i) { t += 1.0 / 60.0; step(&s, &m, t, 0, 0, 0); }
    CHECK(s.phase == RSX_SHELL_OPEN && s.open.value == 1.0f);
    CHECK(NEAR(s.card_scale[RSX_SHELL_CARD_RESUME].value, RSX_SHELL_FOCUS_SCALE));
    rsx_shell_close(&s, t);
    o = step(&s, &m, t, 0, 0, 0);
    CHECK(o.feedback & RSX_OVERLAY_FEEDBACK_BIT(RSX_OVERLAY_FEEDBACK_CLOSE));
    CHECK(s.phase == RSX_SHELL_CLOSING);
    for (int i = 0; i < 40; ++i) { t += 1.0 / 60.0; step(&s, &m, t, 0, 0, 0); }
    CHECK(s.phase == RSX_SHELL_CLOSED && s.open.value == 0.0f && s.level == RSX_SHELL_LEVEL_CARDS);
    return 1;
}

static int test_reopen_during_close_resumes(void)
{
    rsx_shell s; rsx_shell_model m = live_model(); double t = 0.0;
    rsx_shell_init(&s);
    settle_open(&s, &m, &t);
    tap(&s, &m, &t, RSX_SHELL_IN_RIGHT);
    rsx_shell_close(&s, t);
    step(&s, &m, t, 0, 0, 0);
    t += 0.05;
    step(&s, &m, t, 0, 0, 0);
    const float mid = s.open.value;
    CHECK(mid > 0.0f && mid < 1.0f);
    /* Input is ignored while closing. */
    rsx_shell_output o = step(&s, &m, t, RSX_SHELL_IN_RIGHT, 0, 0);
    CHECK(o.count == 0 && s.card == RSX_SHELL_CARD_DISPLAY);
    rsx_shell_open(&s, t);
    CHECK(s.phase == RSX_SHELL_OPENING);
    CHECK(fabsf(s.open.value - mid) < 0.05f);
    CHECK(s.card == RSX_SHELL_CARD_RESUME && s.level == RSX_SHELL_LEVEL_CARDS);
    return 1;
}

static int test_card_navigation_clamps_and_repeats(void)
{
    rsx_shell s; rsx_shell_model m = live_model(); double t = 0.0;
    rsx_shell_init(&s);
    settle_open(&s, &m, &t);
    rsx_shell_output o = step(&s, &m, t, RSX_SHELL_IN_RIGHT, 0, 0);
    CHECK(s.card == 1 && (o.feedback & RSX_OVERLAY_FEEDBACK_BIT(RSX_OVERLAY_FEEDBACK_MOVE)));
    const double t0 = t;
    step(&s, &m, t0 + 0.39, 0, RSX_SHELL_IN_RIGHT, 0);
    CHECK(s.card == 1);
    step(&s, &m, t0 + 0.41, 0, RSX_SHELL_IN_RIGHT, 0);
    CHECK(s.card == 2);
    step(&s, &m, t0 + 0.52, 0, RSX_SHELL_IN_RIGHT, 0);
    CHECK(s.card == 3);
    step(&s, &m, t0 + 0.60, 0, 0, 0);
    step(&s, &m, t0 + 2.00, 0, 0, 0);
    CHECK(s.card == 3 && !rsx_shell_direction_held(&s));
    t = t0 + 2.1;
    for (int i = 0; i < 5; ++i) tap(&s, &m, &t, RSX_SHELL_IN_LEFT);
    CHECK(s.card == 0);
    o = tap(&s, &m, &t, RSX_SHELL_IN_LEFT);
    CHECK(s.card == 0 && !(o.feedback & RSX_OVERLAY_FEEDBACK_BIT(RSX_OVERLAY_FEEDBACK_MOVE)));
    for (int i = 0; i < 8; ++i) tap(&s, &m, &t, RSX_SHELL_IN_RIGHT);
    CHECK(s.card == RSX_SHELL_CARD_QUIT);
    return 1;
}

static int test_resume_confirm_and_back_close(void)
{
    rsx_shell s; rsx_shell_model m = live_model(); double t = 0.0;
    rsx_shell_init(&s);
    settle_open(&s, &m, &t);
    rsx_shell_output o = tap(&s, &m, &t, RSX_SHELL_IN_CONFIRM);
    CHECK(has_action(&o, RSX_SHELL_ACT_CLOSE, 0, 0) && s.phase == RSX_SHELL_CLOSING);
    for (int i = 0; i < 40; ++i) { t += 1.0 / 60.0; step(&s, &m, t, 0, 0, 0); }
    settle_open(&s, &m, &t);
    tap(&s, &m, &t, RSX_SHELL_IN_RIGHT);
    o = tap(&s, &m, &t, RSX_SHELL_IN_BACK);
    CHECK(has_action(&o, RSX_SHELL_ACT_CLOSE, 0, 0));
    return 1;
}

static int test_panel_skips_unavailable_rows(void)
{
    rsx_shell s; rsx_shell_model m = live_model(); double t = 0.0;
    m.support_fullscreen = RSX_OVERLAY_SETTING_UNAVAILABLE;
    rsx_shell_init(&s);
    settle_open(&s, &m, &t);
    tap(&s, &m, &t, RSX_SHELL_IN_RIGHT);
    tap(&s, &m, &t, RSX_SHELL_IN_CONFIRM);
    CHECK(s.level == RSX_SHELL_LEVEL_PANEL && s.row == 1); /* VSync */
    tap(&s, &m, &t, RSX_SHELL_IN_DOWN);
    CHECK(s.row == 2);
    tap(&s, &m, &t, RSX_SHELL_IN_DOWN);
    CHECK(s.row == 2);
    tap(&s, &m, &t, RSX_SHELL_IN_UP);
    CHECK(s.row == 1);
    tap(&s, &m, &t, RSX_SHELL_IN_UP);
    CHECK(s.level == RSX_SHELL_LEVEL_CARDS);
    tap(&s, &m, &t, RSX_SHELL_IN_DOWN);
    CHECK(s.level == RSX_SHELL_LEVEL_PANEL && s.row == 1);
    rsx_shell_output o = tap(&s, &m, &t, RSX_SHELL_IN_CONFIRM);
    CHECK(has_action(&o, RSX_SHELL_ACT_TOGGLE_VSYNC, 0, 0));
    tap(&s, &m, &t, RSX_SHELL_IN_BACK);
    CHECK(s.level == RSX_SHELL_LEVEL_CARDS && s.phase == RSX_SHELL_OPEN);
    /* A card whose rows are all unavailable cannot be entered. */
    m.support_vsync = m.support_window_size = RSX_OVERLAY_SETTING_UNAVAILABLE;
    o = tap(&s, &m, &t, RSX_SHELL_IN_CONFIRM);
    CHECK(s.level == RSX_SHELL_LEVEL_CARDS &&
          (o.feedback & RSX_OVERLAY_FEEDBACK_BIT(RSX_OVERLAY_FEEDBACK_ERROR)));
    return 1;
}

static int test_window_size_list(void)
{
    rsx_shell s; rsx_shell_model m = live_model(); double t = 0.0;
    m.window_size_index = 2;
    rsx_shell_init(&s);
    settle_open(&s, &m, &t);
    tap(&s, &m, &t, RSX_SHELL_IN_RIGHT);
    tap(&s, &m, &t, RSX_SHELL_IN_CONFIRM);
    tap(&s, &m, &t, RSX_SHELL_IN_DOWN);
    tap(&s, &m, &t, RSX_SHELL_IN_DOWN);
    tap(&s, &m, &t, RSX_SHELL_IN_CONFIRM);
    CHECK(s.level == RSX_SHELL_LEVEL_LIST && s.list == RSX_SHELL_LIST_WINDOW_SIZE && s.list_index == 2);
    tap(&s, &m, &t, RSX_SHELL_IN_DOWN);
    tap(&s, &m, &t, RSX_SHELL_IN_DOWN);
    CHECK(s.list_index == 3);
    rsx_shell_output o = tap(&s, &m, &t, RSX_SHELL_IN_CONFIRM);
    CHECK(has_action(&o, RSX_SHELL_ACT_SET_WINDOW_SIZE, 3, 0) && s.level == RSX_SHELL_LEVEL_PANEL);
    CHECK(rsx_shell_window_sizes[3][0] == 2560u && rsx_shell_window_sizes[3][1] == 1440u);
    return 1;
}

static int test_deadzone_slider_steps(void)
{
    rsx_shell s; rsx_shell_model m = live_model(); double t = 0.0;
    rsx_shell_init(&s);
    settle_open(&s, &m, &t);
    tap(&s, &m, &t, RSX_SHELL_IN_RIGHT);
    tap(&s, &m, &t, RSX_SHELL_IN_RIGHT);
    tap(&s, &m, &t, RSX_SHELL_IN_CONFIRM);
    CHECK(s.level == RSX_SHELL_LEVEL_PANEL && s.row == 0);
    rsx_shell_output o = tap(&s, &m, &t, RSX_SHELL_IN_RIGHT);
    CHECK(has_action(&o, RSX_SHELL_ACT_DEADZONE_STEP, 1, 0));
    o = tap(&s, &m, &t, RSX_SHELL_IN_LEFT);
    CHECK(has_action(&o, RSX_SHELL_ACT_DEADZONE_STEP, -1, 0));
    return 1;
}

static int test_remap_capture(void)
{
    rsx_shell s; rsx_shell_model m = live_model(); double t = 0.0;
    rsx_shell_init(&s);
    settle_open(&s, &m, &t);
    tap(&s, &m, &t, RSX_SHELL_IN_RIGHT);
    tap(&s, &m, &t, RSX_SHELL_IN_RIGHT);
    tap(&s, &m, &t, RSX_SHELL_IN_CONFIRM);
    tap(&s, &m, &t, RSX_SHELL_IN_DOWN);
    tap(&s, &m, &t, RSX_SHELL_IN_DOWN);
    tap(&s, &m, &t, RSX_SHELL_IN_CONFIRM);
    CHECK(s.level == RSX_SHELL_LEVEL_LIST && s.list == RSX_SHELL_LIST_REMAP);
    for (int i = 0; i < 14; ++i) tap(&s, &m, &t, RSX_SHELL_IN_DOWN);
    CHECK(s.list_index == 14 && s.list_scroll == 11); /* 4 visible rows: 11..14 */
    tap(&s, &m, &t, RSX_SHELL_IN_CONFIRM);
    CHECK(s.level == RSX_SHELL_LEVEL_CAPTURE && s.capture_guest == 14);
    /* Pad Circle is captured, not treated as Back. */
    rsx_shell_output o = step(&s, &m, t, RSX_SHELL_IN_BACK, 0, CELL_PAD_CTRL_CIRCLE);
    CHECK(has_action(&o, RSX_SHELL_ACT_REMAP, 14, 13) && s.level == RSX_SHELL_LEVEL_LIST);
    t += 0.1;
    tap(&s, &m, &t, RSX_SHELL_IN_CONFIRM);
    CHECK(s.level == RSX_SHELL_LEVEL_CAPTURE);
    o = tap(&s, &m, &t, RSX_SHELL_IN_BACK); /* keyboard Esc cancels */
    CHECK(o.count == 0 && s.level == RSX_SHELL_LEVEL_LIST);
    tap(&s, &m, &t, RSX_SHELL_IN_CONFIRM);
    o = step(&s, &m, t + RSX_SHELL_CAPTURE_TIMEOUT_S + 0.1, 0, 0, 0);
    CHECK(o.count == 0 && s.level == RSX_SHELL_LEVEL_LIST &&
          (o.feedback & RSX_OVERLAY_FEEDBACK_BIT(RSX_OVERLAY_FEEDBACK_ERROR)));
    return 1;
}

static int test_remap_assign_keeps_bijection(void)
{
    uint16_t map[16];
    uint32_t seen = 0;
    for (unsigned i = 0; i < 16; ++i) map[i] = (uint16_t)(1u << i);
    rsx_shell_remap_assign(map, 14, 1); /* Cruz <- host L3 */
    CHECK(map[1] == CELL_PAD_CTRL_CROSS && map[14] == CELL_PAD_CTRL_L3);
    rsx_shell_remap_assign(map, 14, 1);
    CHECK(map[1] == CELL_PAD_CTRL_CROSS && map[14] == CELL_PAD_CTRL_L3);
    rsx_shell_remap_assign(map, 99, 1);  /* out of range: ignored */
    rsx_shell_remap_assign(map, 1, 99);
    for (unsigned i = 0; i < 16; ++i) { CHECK((map[i] & (map[i] - 1u)) == 0 && !(seen & map[i])); seen |= map[i]; }
    CHECK(seen == 0xFFFFu);
    return 1;
}

static int test_quit_dialog(void)
{
    rsx_shell s; rsx_shell_model m = live_model(); double t = 0.0;
    rsx_shell_init(&s);
    settle_open(&s, &m, &t);
    for (int i = 0; i < 5; ++i) tap(&s, &m, &t, RSX_SHELL_IN_RIGHT);
    rsx_shell_output o = tap(&s, &m, &t, RSX_SHELL_IN_CONFIRM);
    CHECK(s.level == RSX_SHELL_LEVEL_DIALOG && s.dialog_choice == 1 && o.count == 0);
    o = tap(&s, &m, &t, RSX_SHELL_IN_CONFIRM); /* Cancelar */
    CHECK(s.level == RSX_SHELL_LEVEL_CARDS && o.count == 0);
    tap(&s, &m, &t, RSX_SHELL_IN_CONFIRM);
    tap(&s, &m, &t, RSX_SHELL_IN_LEFT);
    CHECK(s.dialog_choice == 0);
    o = tap(&s, &m, &t, RSX_SHELL_IN_CONFIRM);
    CHECK(has_action(&o, RSX_SHELL_ACT_QUIT, 0, 0) && s.phase == RSX_SHELL_CLOSING);
    return 1;
}

static int test_logs_list_scrolls(void)
{
    rsx_shell s; rsx_shell_model m = live_model(); double t = 0.0;
    m.log_lines = 64;
    m.list_visible_rows = 6;
    rsx_shell_init(&s);
    settle_open(&s, &m, &t);
    for (int i = 0; i < 4; ++i) tap(&s, &m, &t, RSX_SHELL_IN_RIGHT);
    tap(&s, &m, &t, RSX_SHELL_IN_CONFIRM);
    CHECK(s.level == RSX_SHELL_LEVEL_PANEL);
    tap(&s, &m, &t, RSX_SHELL_IN_CONFIRM);
    CHECK(s.level == RSX_SHELL_LEVEL_LIST && s.list == RSX_SHELL_LIST_LOGS);
    for (int i = 0; i < 10; ++i) tap(&s, &m, &t, RSX_SHELL_IN_DOWN);
    CHECK(s.list_index == 10 && s.list_scroll == 5);
    for (int i = 0; i < 10; ++i) tap(&s, &m, &t, RSX_SHELL_IN_UP);
    CHECK(s.list_index == 0 && s.list_scroll == 0);
    CHECK(rsx_shell_panel_display_rows(&s, &m) == 64 && rsx_shell_panel_wide(&s));
    return 1;
}

static int test_panel_fade(void)
{
    rsx_shell s; rsx_shell_model m = live_model(); double t = 0.0;
    rsx_shell_init(&s);
    settle_open(&s, &m, &t);
    step(&s, &m, t, RSX_SHELL_IN_RIGHT, 0, 0);
    CHECK(rsx_shell_panel_fade(&s, t) < 0.01f);
    CHECK(NEAR(rsx_shell_panel_fade(&s, t + 0.06), 0.5f));
    CHECK(rsx_shell_panel_fade(&s, t + 0.2) == 1.0f);
    return 1;
}

static int test_layout_reference_1280x720(void)
{
    rsx_shell_layout L;
    rsx_shell_compute_layout(1280.0f, 720.0f, 1.0f, RSX_SHELL_CARD_DISPLAY, 3, 0, 1.0f, &L);
    CHECK(NEAR(L.px_per_pt, 1.0f));
    CHECK(NEAR(L.cards[0].x, 48) && NEAR(L.cards[1].x, 164) && NEAR(L.cards[4].x, 512));
    CHECK(NEAR(L.cards[RSX_SHELL_CARD_QUIT].x, 1136) && NEAR(L.cards[0].y, 552) && NEAR(L.cards[0].w, 96));
    CHECK(NEAR(L.top_strip.h, 44) && NEAR(L.band.y, 460));
    CHECK(NEAR(L.panel.x, 48) && NEAR(L.panel.w, 380) && NEAR(L.panel.h, 208));
    CHECK(NEAR(L.panel.y, 315.36) && L.panel_visible_rows == 3);
    rsx_shell_compute_layout(1280.0f, 720.0f, 1.0f, 0, 3, 0, 0.0f, &L);
    CHECK(NEAR(L.cards[0].y, 592)); /* closed: sunk by the rise */
    return 1;
}

static int test_layout_retina_doubles(void)
{
    rsx_shell_layout a, b;
    rsx_shell_compute_layout(1280.0f, 720.0f, 1.0f, 2, 4, 0, 1.0f, &a);
    rsx_shell_compute_layout(2560.0f, 1440.0f, 2.0f, 2, 4, 0, 1.0f, &b);
    CHECK(NEAR(b.px_per_pt, 2.0f));
    for (int i = 0; i < RSX_SHELL_CARD_COUNT; ++i)
        CHECK(NEAR(b.cards[i].x, 2 * a.cards[i].x) && NEAR(b.cards[i].y, 2 * a.cards[i].y));
    CHECK(NEAR(b.panel.x, 2 * a.panel.x) && NEAR(b.panel.y, 2 * a.panel.y) && NEAR(b.panel.h, 2 * a.panel.h));
    return 1;
}

static int test_layout_extremes_fit(void)
{
    static const float sizes[][3] = { { 640, 480, 1 }, { 3440 * 2, 1440 * 2, 2 }, { 2880, 1800, 2 }, { 1280, 720, 2 } };
    for (unsigned k = 0; k < sizeof sizes / sizeof sizes[0]; ++k) {
        for (int card = 0; card < RSX_SHELL_CARD_COUNT; ++card) {
            rsx_shell_layout L;
            const float W = sizes[k][0], H = sizes[k][1];
            rsx_shell_compute_layout(W, H, sizes[k][2], card, 16, card == RSX_SHELL_CARD_DEVELOPER, 1.0f, &L);
            for (int i = 0; i < RSX_SHELL_CARD_COUNT; ++i) {
                CHECK(L.cards[i].x >= 0 && L.cards[i].x + L.cards[i].w <= W);
                CHECK(L.cards[i].y >= 0 && L.cards[i].y + L.cards[i].h <= H);
                if (i > 0)
                    CHECK(L.cards[i].x > L.cards[i - 1].x + L.cards[i - 1].w);
            }
            CHECK(L.panel.x >= 0 && L.panel.x + L.panel.w <= W + 0.01f);
            CHECK(L.panel.y >= L.top_strip.h - 0.01f);
            CHECK(L.panel.y + L.panel.h <= L.cards[card].y + 0.01f);
            CHECK(L.panel_visible_rows >= 1 && L.panel_visible_rows <= 16);
        }
    }
    rsx_shell_layout small;
    rsx_shell_compute_layout(640.0f, 480.0f, 1.0f, RSX_SHELL_CARD_CONTROLS, 16, 0, 1.0f, &small);
    CHECK(small.panel_visible_rows < 16);
    return 1;
}

/* A card with no available rows answers a Down press with one ERROR, not one
 * per auto-repeat while Down stays held. */
static int test_error_only_on_press_edge(void)
{
    rsx_shell s; rsx_shell_model m = live_model(); double t = 0.0;
    rsx_shell_output o;
    int errors = 0;
    m.support_fullscreen = m.support_vsync = m.support_window_size = RSX_OVERLAY_SETTING_UNAVAILABLE;
    rsx_shell_init(&s);
    settle_open(&s, &m, &t);
    tap(&s, &m, &t, RSX_SHELL_IN_RIGHT);               /* Tela: nothing available */
    o = step(&s, &m, t, RSX_SHELL_IN_DOWN, 0, 0);
    CHECK(o.feedback & RSX_OVERLAY_FEEDBACK_BIT(RSX_OVERLAY_FEEDBACK_ERROR));
    for (int i = 0; i < 60; ++i) {                     /* 1.5 s held: many repeats */
        t += 0.025;
        o = step(&s, &m, t, 0, RSX_SHELL_IN_DOWN, 0);
        errors += (o.feedback & RSX_OVERLAY_FEEDBACK_BIT(RSX_OVERLAY_FEEDBACK_ERROR)) != 0;
    }
    CHECK(errors == 0 && s.level == RSX_SHELL_LEVEL_CARDS);
    return 1;
}

/* A resize (fewer visible rows) or a shorter log keeps the selection on screen. */
static int test_list_refit_after_resize(void)
{
    rsx_shell s; rsx_shell_model m = live_model(); double t = 0.0;
    m.log_lines = 64;
    m.list_visible_rows = 6;
    rsx_shell_init(&s);
    settle_open(&s, &m, &t);
    for (int i = 0; i < 4; ++i) tap(&s, &m, &t, RSX_SHELL_IN_RIGHT);
    tap(&s, &m, &t, RSX_SHELL_IN_CONFIRM);
    tap(&s, &m, &t, RSX_SHELL_IN_CONFIRM);
    for (int i = 0; i < 10; ++i) tap(&s, &m, &t, RSX_SHELL_IN_DOWN);
    CHECK(s.list_index == 10 && s.list_scroll == 5);
    m.list_visible_rows = 3;
    rsx_shell_refit_list(&s, &m);
    CHECK(s.list_index == 10 && s.list_scroll == 8);
    m.list_visible_rows = 20;
    rsx_shell_refit_list(&s, &m);
    CHECK(s.list_index == 10 && s.list_scroll <= 10 && s.list_scroll + 20 > 10);
    m.log_lines = 4;
    rsx_shell_refit_list(&s, &m);
    CHECK(s.list_index == 3 && s.list_scroll == 0);
    m.log_lines = 0;
    rsx_shell_refit_list(&s, &m);
    CHECK(s.list_index == 0 && s.list_scroll == 0);
    return 1;
}

int main(void)
{
    int ok = 1;
    ok &= test_spring_is_analytic_and_deterministic();
    ok &= test_open_settles_and_close_returns_to_closed();
    ok &= test_reopen_during_close_resumes();
    ok &= test_card_navigation_clamps_and_repeats();
    ok &= test_resume_confirm_and_back_close();
    ok &= test_panel_skips_unavailable_rows();
    ok &= test_window_size_list();
    ok &= test_deadzone_slider_steps();
    ok &= test_remap_capture();
    ok &= test_remap_assign_keeps_bijection();
    ok &= test_quit_dialog();
    ok &= test_logs_list_scrolls();
    ok &= test_panel_fade();
    ok &= test_layout_reference_1280x720();
    ok &= test_layout_retina_doubles();
    ok &= test_layout_extremes_fit();
    ok &= test_error_only_on_press_edge();
    ok &= test_list_refit_after_resize();
    printf("test_rsx_overlay_shell: %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
