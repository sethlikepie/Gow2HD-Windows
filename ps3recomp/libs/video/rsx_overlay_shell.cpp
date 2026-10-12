#include "rsx_overlay_shell.h"

#include <cmath>
#include <cstring>

const uint32_t rsx_shell_window_sizes[RSX_SHELL_WINDOW_SIZE_COUNT][2] = {
    { 1280u, 720u }, { 1600u, 900u }, { 1920u, 1080u }, { 2560u, 1440u }
};

const char* const rsx_shell_button_names[RSX_SHELL_REMAP_COUNT] = {
    "Select", "L3", "R3", "Start", "Cima", "Direita", "Baixo", "Esquerda",
    "L2", "R2", "L1", "R1", "Tri\xC3\xA2ngulo", "C\xC3\xADrculo", "Cruz", "Quadrado"
};

static const uint32_t kDirs = RSX_SHELL_IN_UP | RSX_SHELL_IN_DOWN | RSX_SHELL_IN_LEFT | RSX_SHELL_IN_RIGHT;

static float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

void rsx_spring_step(rsx_spring* s, float omega, float dt)
{
    if (s == nullptr || !(dt > 0.0f))
        return;
    /* Critically damped closed form: exact for any dt, so the result depends
     * only on elapsed time, never on the frame rate. */
    const float x0 = s->value - s->target;
    const float v0 = s->velocity;
    const float e = std::exp(-omega * dt);
    const float c = v0 + omega * x0;
    s->value = s->target + (x0 + c * dt) * e;
    s->velocity = (v0 - omega * c * dt) * e;
}

void rsx_shell_init(rsx_shell* s)
{
    if (s == nullptr)
        return;
    std::memset(s, 0, sizeof(*s));
    s->phase = RSX_SHELL_CLOSED;
    s->dialog_choice = 1;
    for (int i = 0; i < RSX_SHELL_CARD_COUNT; ++i) {
        s->card_scale[i].value = 1.0f;
        s->card_scale[i].target = 1.0f;
    }
}

int rsx_shell_panel_rows(int card, rsx_shell_row* rows, int capacity)
{
    static const rsx_shell_row display[] = { RSX_SHELL_ROW_FULLSCREEN, RSX_SHELL_ROW_VSYNC, RSX_SHELL_ROW_WINDOW_SIZE };
    static const rsx_shell_row controls[] = { RSX_SHELL_ROW_DEADZONE, RSX_SHELL_ROW_VIBRATION, RSX_SHELL_ROW_REMAP, RSX_SHELL_ROW_RESTORE };
    static const rsx_shell_row sound[] = { RSX_SHELL_ROW_GAME_SOUND, RSX_SHELL_ROW_UI_SOUNDS, RSX_SHELL_ROW_UI_HAPTICS };
    static const rsx_shell_row developer[] = { RSX_SHELL_ROW_LOGS };
    const rsx_shell_row* src = nullptr;
    int n = 0;
    switch (card) {
    case RSX_SHELL_CARD_DISPLAY: src = display; n = 3; break;
    case RSX_SHELL_CARD_CONTROLS: src = controls; n = 4; break;
    case RSX_SHELL_CARD_SOUND: src = sound; n = 3; break;
    case RSX_SHELL_CARD_DEVELOPER: src = developer; n = 1; break;
    default: return 0;
    }
    if (rows != nullptr)
        for (int i = 0; i < n && i < capacity; ++i)
            rows[i] = src[i];
    return n;
}

int rsx_shell_row_enabled(rsx_shell_row row, const rsx_shell_model* m)
{
    if (m == nullptr)
        return 1;
    switch (row) {
    case RSX_SHELL_ROW_FULLSCREEN: return m->support_fullscreen != RSX_OVERLAY_SETTING_UNAVAILABLE;
    case RSX_SHELL_ROW_VSYNC: return m->support_vsync != RSX_OVERLAY_SETTING_UNAVAILABLE;
    case RSX_SHELL_ROW_WINDOW_SIZE: return m->support_window_size != RSX_OVERLAY_SETTING_UNAVAILABLE;
    default: return 1;
    }
}

static int list_count(rsx_shell_list list, const rsx_shell_model* m)
{
    switch (list) {
    case RSX_SHELL_LIST_WINDOW_SIZE: return RSX_SHELL_WINDOW_SIZE_COUNT;
    case RSX_SHELL_LIST_REMAP: return RSX_SHELL_REMAP_COUNT;
    case RSX_SHELL_LIST_LOGS: return m != nullptr ? (int)m->log_lines : 0;
    default: return 0;
    }
}

int rsx_shell_panel_display_rows(const rsx_shell* s, const rsx_shell_model* m)
{
    if (s->level == RSX_SHELL_LEVEL_LIST || s->level == RSX_SHELL_LEVEL_CAPTURE) {
        const int n = list_count(s->list, m);
        return n < 1 ? 1 : n;
    }
    switch (s->card) {
    case RSX_SHELL_CARD_RESUME:
    case RSX_SHELL_CARD_QUIT: return 1;
    case RSX_SHELL_CARD_DEVELOPER: return 6; /* five metrics + Logs */
    default: return rsx_shell_panel_rows(s->card, nullptr, 0);
    }
}

int rsx_shell_panel_wide(const rsx_shell* s) { return s->card == RSX_SHELL_CARD_DEVELOPER; }

float rsx_shell_panel_fade(const rsx_shell* s, double now)
{
    return clamp01((float)((now - s->panel_switch_time) / RSX_SHELL_FADE_S));
}

int rsx_shell_direction_held(const rsx_shell* s) { return s->repeat_dir != 0; }

void rsx_shell_remap_assign(uint16_t map[RSX_SHELL_REMAP_COUNT], unsigned guest_index, unsigned host_index)
{
    if (map == nullptr || guest_index >= RSX_SHELL_REMAP_COUNT || host_index >= RSX_SHELL_REMAP_COUNT)
        return;
    const uint16_t guest = (uint16_t)(1u << guest_index);
    for (unsigned owner = 0; owner < RSX_SHELL_REMAP_COUNT; ++owner) {
        if (map[owner] != guest)
            continue;
        if (owner != host_index) {
            map[owner] = map[host_index];
            map[host_index] = guest;
        }
        return;
    }
}

void rsx_shell_open(rsx_shell* s, double now)
{
    if (s->phase == RSX_SHELL_OPEN || s->phase == RSX_SHELL_OPENING)
        return;
    if (s->phase == RSX_SHELL_CLOSED)
        s->last_time = now;
    s->phase = RSX_SHELL_OPENING;
    s->level = RSX_SHELL_LEVEL_CARDS;
    s->card = RSX_SHELL_CARD_RESUME;
    s->row = 0;
    s->list = RSX_SHELL_LIST_NONE;
    s->list_index = 0;
    s->list_scroll = 0;
    s->dialog_choice = 1;
    s->repeat_dir = 0;
    s->panel_switch_time = now;
    s->open.target = 1.0f;
    s->pending_feedback |= RSX_OVERLAY_FEEDBACK_BIT(RSX_OVERLAY_FEEDBACK_OPEN);
}

void rsx_shell_close(rsx_shell* s, double now)
{
    (void)now;
    if (s->phase != RSX_SHELL_OPEN && s->phase != RSX_SHELL_OPENING)
        return;
    s->phase = RSX_SHELL_CLOSING;
    s->open.target = 0.0f;
    s->repeat_dir = 0;
    s->pending_feedback |= RSX_OVERLAY_FEEDBACK_BIT(RSX_OVERLAY_FEEDBACK_CLOSE);
}

static void emit(rsx_shell_output* out, int kind, int a, int b)
{
    if (out->count < RSX_SHELL_MAX_ACTIONS) {
        out->actions[out->count].kind = kind;
        out->actions[out->count].a = a;
        out->actions[out->count].b = b;
        ++out->count;
    }
}

static void fb(rsx_shell_output* out, int event) { out->feedback |= RSX_OVERLAY_FEEDBACK_BIT(event); }

static void begin_close(rsx_shell* s, rsx_shell_output* out, int kind)
{
    emit(out, kind, 0, 0);
    s->phase = RSX_SHELL_CLOSING;
    s->open.target = 0.0f;
    s->repeat_dir = 0;
    fb(out, RSX_OVERLAY_FEEDBACK_CLOSE);
}

static uint32_t dir_events(rsx_shell* s, const rsx_shell_input* in, double now)
{
    uint32_t ev = in->pressed & kDirs;
    const uint32_t held = in->held & kDirs;
    if (ev != 0) {
        s->repeat_dir = ev & (~ev + 1u); /* lowest pressed direction repeats */
        s->repeat_next = now + RSX_SHELL_REPEAT_DELAY_S;
    } else if (s->repeat_dir != 0 && (held & s->repeat_dir) != 0) {
        if (now >= s->repeat_next) {
            ev = s->repeat_dir;
            s->repeat_next = now + RSX_SHELL_REPEAT_INTERVAL_S;
        }
    } else {
        s->repeat_dir = 0;
    }
    return ev;
}

static int step_row(const rsx_shell* s, const rsx_shell_model* m, int from, int delta)
{
    rsx_shell_row rows[8];
    const int n = rsx_shell_panel_rows(s->card, rows, 8);
    for (int i = from + delta; i >= 0 && i < n; i += delta)
        if (rsx_shell_row_enabled(rows[i], m))
            return i;
    return -1;
}

static void fix_scroll(rsx_shell* s, const rsx_shell_model* m)
{
    const int visible = m->list_visible_rows < 1 ? 1 : (int)m->list_visible_rows;
    if (s->list_index < s->list_scroll) s->list_scroll = s->list_index;
    if (s->list_index >= s->list_scroll + visible) s->list_scroll = s->list_index - visible + 1;
    if (s->list_scroll < 0) s->list_scroll = 0;
}

void rsx_shell_refit_list(rsx_shell* s, const rsx_shell_model* m)
{
    if (s == nullptr || m == nullptr ||
        (s->level != RSX_SHELL_LEVEL_LIST && s->level != RSX_SHELL_LEVEL_CAPTURE))
        return;
    const int count = list_count(s->list, m);
    if (s->list_index >= count) s->list_index = count > 0 ? count - 1 : 0;
    if (s->list_index < 0) s->list_index = 0;
    fix_scroll(s, m);
    const int visible = m->list_visible_rows < 1 ? 1 : (int)m->list_visible_rows;
    const int max_scroll = count > visible ? count - visible : 0;
    if (s->list_scroll > max_scroll) s->list_scroll = max_scroll; /* no empty tail */
}

static void open_list(rsx_shell* s, rsx_shell_list list, int index, double now)
{
    s->level = RSX_SHELL_LEVEL_LIST;
    s->list = list;
    s->list_index = index;
    s->list_scroll = 0;
    s->panel_switch_time = now;
}

static void confirm_row(rsx_shell* s, const rsx_shell_model* m, double now, rsx_shell_output* out)
{
    rsx_shell_row rows[8];
    const int n = rsx_shell_panel_rows(s->card, rows, 8);
    if (s->row < 0 || s->row >= n)
        return;
    fb(out, RSX_OVERLAY_FEEDBACK_CONFIRM);
    switch (rows[s->row]) {
    case RSX_SHELL_ROW_FULLSCREEN: emit(out, RSX_SHELL_ACT_TOGGLE_FULLSCREEN, 0, 0); break;
    case RSX_SHELL_ROW_VSYNC: emit(out, RSX_SHELL_ACT_TOGGLE_VSYNC, 0, 0); break;
    case RSX_SHELL_ROW_WINDOW_SIZE:
        open_list(s, RSX_SHELL_LIST_WINDOW_SIZE,
                  m->window_size_index < RSX_SHELL_WINDOW_SIZE_COUNT ? m->window_size_index : 0, now);
        fix_scroll(s, m);
        break;
    case RSX_SHELL_ROW_VIBRATION: emit(out, RSX_SHELL_ACT_TOGGLE_VIBRATION, 0, 0); break;
    case RSX_SHELL_ROW_REMAP: open_list(s, RSX_SHELL_LIST_REMAP, 0, now); break;
    case RSX_SHELL_ROW_RESTORE: emit(out, RSX_SHELL_ACT_RESTORE_CONTROLS, 0, 0); break;
    case RSX_SHELL_ROW_GAME_SOUND: emit(out, RSX_SHELL_ACT_TOGGLE_GAME_SOUND, 0, 0); break;
    case RSX_SHELL_ROW_UI_SOUNDS: emit(out, RSX_SHELL_ACT_TOGGLE_UI_SOUNDS, 0, 0); break;
    case RSX_SHELL_ROW_UI_HAPTICS: emit(out, RSX_SHELL_ACT_TOGGLE_UI_HAPTICS, 0, 0); break;
    case RSX_SHELL_ROW_LOGS: open_list(s, RSX_SHELL_LIST_LOGS, 0, now); break;
    default: break; /* DEADZONE: left/right only */
    }
}

static void handle_input(rsx_shell* s, const rsx_shell_input* in, const rsx_shell_model* m,
                         double now, rsx_shell_output* out)
{
    const uint32_t dir = dir_events(s, in, now);
    const uint32_t btn = in->pressed & (RSX_SHELL_IN_CONFIRM | RSX_SHELL_IN_BACK);

    switch (s->level) {
    case RSX_SHELL_LEVEL_CARDS: {
        int next = s->card + ((dir & RSX_SHELL_IN_RIGHT) ? 1 : 0) - ((dir & RSX_SHELL_IN_LEFT) ? 1 : 0);
        if (next < 0) next = 0;
        if (next >= RSX_SHELL_CARD_COUNT) next = RSX_SHELL_CARD_COUNT - 1;
        if (next != s->card) {
            s->card = next;
            s->panel_switch_time = now;
            fb(out, RSX_OVERLAY_FEEDBACK_MOVE);
        }
        if (btn & RSX_SHELL_IN_CONFIRM) {
            if (s->card == RSX_SHELL_CARD_RESUME) {
                begin_close(s, out, RSX_SHELL_ACT_CLOSE);
                return;
            }
            if (s->card == RSX_SHELL_CARD_QUIT) {
                s->level = RSX_SHELL_LEVEL_DIALOG;
                s->dialog_choice = 1;
                fb(out, RSX_OVERLAY_FEEDBACK_CONFIRM);
                return;
            }
        }
        if ((btn & RSX_SHELL_IN_CONFIRM) || (dir & RSX_SHELL_IN_DOWN)) {
            if (s->card == RSX_SHELL_CARD_RESUME || s->card == RSX_SHELL_CARD_QUIT)
                return;
            const int first = step_row(s, m, -1, 1);
            if (first < 0) {
                /* Only a fresh press answers "unavailable": a held Down
                 * auto-repeats here and must not buzz ten times a second. */
                if (btn != 0 || (in->pressed & RSX_SHELL_IN_DOWN) != 0)
                    fb(out, RSX_OVERLAY_FEEDBACK_ERROR);
                return;
            }
            s->level = RSX_SHELL_LEVEL_PANEL;
            s->row = first;
            fb(out, (btn & RSX_SHELL_IN_CONFIRM) ? RSX_OVERLAY_FEEDBACK_CONFIRM : RSX_OVERLAY_FEEDBACK_MOVE);
            return;
        }
        if (btn & RSX_SHELL_IN_BACK)
            begin_close(s, out, RSX_SHELL_ACT_CLOSE);
        return;
    }
    case RSX_SHELL_LEVEL_PANEL: {
        rsx_shell_row rows[8];
        const int n = rsx_shell_panel_rows(s->card, rows, 8);
        if (dir & RSX_SHELL_IN_UP) {
            const int prev = step_row(s, m, s->row, -1);
            if (prev < 0) s->level = RSX_SHELL_LEVEL_CARDS; else s->row = prev;
            fb(out, RSX_OVERLAY_FEEDBACK_MOVE);
            return;
        }
        if (dir & RSX_SHELL_IN_DOWN) {
            const int nxt = step_row(s, m, s->row, 1);
            if (nxt >= 0) { s->row = nxt; fb(out, RSX_OVERLAY_FEEDBACK_MOVE); }
            return;
        }
        if ((dir & (RSX_SHELL_IN_LEFT | RSX_SHELL_IN_RIGHT)) && s->row < n &&
            rows[s->row] == RSX_SHELL_ROW_DEADZONE) {
            emit(out, RSX_SHELL_ACT_DEADZONE_STEP, (dir & RSX_SHELL_IN_RIGHT) ? 1 : -1, 0);
            fb(out, RSX_OVERLAY_FEEDBACK_MOVE);
            return;
        }
        if (btn & RSX_SHELL_IN_CONFIRM) { confirm_row(s, m, now, out); return; }
        if (btn & RSX_SHELL_IN_BACK) { s->level = RSX_SHELL_LEVEL_CARDS; fb(out, RSX_OVERLAY_FEEDBACK_BACK); }
        return;
    }
    case RSX_SHELL_LEVEL_LIST: {
        const int count = list_count(s->list, m);
        const int before = s->list_index;
        if ((dir & RSX_SHELL_IN_UP) && s->list_index > 0) --s->list_index;
        if ((dir & RSX_SHELL_IN_DOWN) && s->list_index + 1 < count) ++s->list_index;
        if (s->list_index != before) { fix_scroll(s, m); fb(out, RSX_OVERLAY_FEEDBACK_MOVE); }
        if (btn & RSX_SHELL_IN_CONFIRM) {
            if (s->list == RSX_SHELL_LIST_WINDOW_SIZE) {
                emit(out, RSX_SHELL_ACT_SET_WINDOW_SIZE, s->list_index, 0);
                s->level = RSX_SHELL_LEVEL_PANEL;
                s->list = RSX_SHELL_LIST_NONE;
                fb(out, RSX_OVERLAY_FEEDBACK_CONFIRM);
            } else if (s->list == RSX_SHELL_LIST_REMAP) {
                s->level = RSX_SHELL_LEVEL_CAPTURE;
                s->capture_guest = s->list_index;
                s->capture_started = now;
                fb(out, RSX_OVERLAY_FEEDBACK_CONFIRM);
            }
            return;
        }
        if (btn & RSX_SHELL_IN_BACK) {
            s->level = RSX_SHELL_LEVEL_PANEL;
            s->list = RSX_SHELL_LIST_NONE;
            s->panel_switch_time = now;
            fb(out, RSX_OVERLAY_FEEDBACK_BACK);
        }
        return;
    }
    case RSX_SHELL_LEVEL_CAPTURE:
        if (in->key_pressed & RSX_SHELL_IN_BACK) {
            s->level = RSX_SHELL_LEVEL_LIST;
            fb(out, RSX_OVERLAY_FEEDBACK_BACK);
        } else if (in->pad_pressed != 0) {
            unsigned host = 0;
            while (((in->pad_pressed >> host) & 1u) == 0)
                ++host;
            emit(out, RSX_SHELL_ACT_REMAP, s->capture_guest, (int)host);
            s->level = RSX_SHELL_LEVEL_LIST;
            fb(out, RSX_OVERLAY_FEEDBACK_CONFIRM);
        } else if (now - s->capture_started >= RSX_SHELL_CAPTURE_TIMEOUT_S) {
            s->level = RSX_SHELL_LEVEL_LIST;
            fb(out, RSX_OVERLAY_FEEDBACK_ERROR);
        }
        return;
    case RSX_SHELL_LEVEL_DIALOG:
        if (dir & (RSX_SHELL_IN_LEFT | RSX_SHELL_IN_RIGHT)) {
            s->dialog_choice ^= 1;
            fb(out, RSX_OVERLAY_FEEDBACK_MOVE);
        }
        if (btn & RSX_SHELL_IN_CONFIRM) {
            if (s->dialog_choice == 0) {
                fb(out, RSX_OVERLAY_FEEDBACK_CONFIRM);
                begin_close(s, out, RSX_SHELL_ACT_QUIT);
            } else {
                s->level = RSX_SHELL_LEVEL_CARDS;
                fb(out, RSX_OVERLAY_FEEDBACK_BACK);
            }
        } else if (btn & RSX_SHELL_IN_BACK) {
            s->level = RSX_SHELL_LEVEL_CARDS;
            fb(out, RSX_OVERLAY_FEEDBACK_BACK);
        }
        return;
    }
}

void rsx_shell_update(rsx_shell* s, const rsx_shell_input* in, const rsx_shell_model* m,
                      double now, rsx_shell_output* out)
{
    std::memset(out, 0, sizeof(*out));
    out->feedback = s->pending_feedback;
    s->pending_feedback = 0;
    /* The springs are analytic, so any finite dt is exact: a long frame (or
     * a test that advances the clock by a second) lands where real time says.
     * The cap only keeps a non-finite or absurd gap out of the exponentials. */
    float dt = (float)(now - s->last_time);
    if (!(dt > 0.0f)) dt = 0.0f;
    if (dt > 1.0f) dt = 1.0f;
    s->last_time = now;
    if (s->phase == RSX_SHELL_CLOSED)
        return;
    if ((s->phase == RSX_SHELL_OPENING || s->phase == RSX_SHELL_OPEN) && in != nullptr && m != nullptr)
        handle_input(s, in, m, now, out);
    rsx_spring_step(&s->open, RSX_SHELL_OPEN_OMEGA, dt);
    for (int i = 0; i < RSX_SHELL_CARD_COUNT; ++i) {
        s->card_scale[i].target = (i == s->card && s->phase != RSX_SHELL_CLOSING) ? RSX_SHELL_FOCUS_SCALE : 1.0f;
        rsx_spring_step(&s->card_scale[i], RSX_SHELL_CARD_OMEGA, dt);
    }
    if (s->phase == RSX_SHELL_OPENING && s->open.value >= 0.995f) {
        s->phase = RSX_SHELL_OPEN;
        s->open.value = 1.0f;
        s->open.velocity = 0.0f;
    } else if (s->phase == RSX_SHELL_CLOSING && s->open.value <= 0.005f) {
        s->phase = RSX_SHELL_CLOSED;
        s->open.value = 0.0f;
        s->open.velocity = 0.0f;
        s->level = RSX_SHELL_LEVEL_CARDS;
        s->list = RSX_SHELL_LIST_NONE;
    }
}

void rsx_shell_compute_layout(float W, float H, float scale, int card, int rows, int wide,
                              float open, rsx_shell_layout* L)
{
    std::memset(L, 0, sizeof(*L));
    if (!(scale > 0.0f)) scale = 1.0f;
    if (!(W > 0.0f) || !(H > 0.0f)) return;
    const float wpt = W / scale, hpt = H / scale;
    float fit = 1.0f;
    if (wpt < RSX_SHELL_NEED_W_PT) fit = wpt / RSX_SHELL_NEED_W_PT;
    if (hpt < RSX_SHELL_NEED_H_PT && hpt / RSX_SHELL_NEED_H_PT < fit) fit = hpt / RSX_SHELL_NEED_H_PT;
    const float u = scale * fit;
    L->px_per_pt = u;
    L->width = W;
    L->height = H;
    L->top_strip = { 0.0f, 0.0f, W, RSX_SHELL_STRIP_PT * u };
    L->band = { 0.0f, H - RSX_SHELL_BAND_PT * u, W, RSX_SHELL_BAND_PT * u };
    if (L->band.y < 0.0f) { L->band.y = 0.0f; L->band.h = H; }
    const float top = H - (RSX_SHELL_CARD_BOTTOM_PT + RSX_SHELL_CARD_PT) * u +
                      (1.0f - clamp01(open)) * RSX_SHELL_RISE_PT * u;
    for (int i = 0; i < RSX_SHELL_CARD_QUIT; ++i)
        L->cards[i] = { (RSX_SHELL_MARGIN_PT + i * (RSX_SHELL_CARD_PT + RSX_SHELL_CARD_GAP_PT)) * u,
                        top, RSX_SHELL_CARD_PT * u, RSX_SHELL_CARD_PT * u };
    L->cards[RSX_SHELL_CARD_QUIT] = { W - (RSX_SHELL_MARGIN_PT + RSX_SHELL_CARD_PT) * u, top,
                                      RSX_SHELL_CARD_PT * u, RSX_SHELL_CARD_PT * u };
    if (card < 0 || card >= RSX_SHELL_CARD_COUNT) card = 0;
    float pw = (wide ? RSX_SHELL_PANEL_WIDE_W_PT : RSX_SHELL_PANEL_W_PT) * u;
    const float max_w = W - 2.0f * RSX_SHELL_MARGIN_PT * u;
    if (pw > max_w) pw = max_w;
    const rsx_shell_rect& c = L->cards[card];
    const float card_top = c.y - c.h * (RSX_SHELL_FOCUS_SCALE - 1.0f) * 0.5f;
    const float bottom = card_top - RSX_SHELL_PANEL_GAP_PT * u;
    const float min_y = L->top_strip.h + 8.0f * u;
    const float chrome = (RSX_SHELL_PANEL_HEADER_PT + 2.0f * RSX_SHELL_PANEL_PAD_PT) * u;
    int fit_rows = (int)std::floor((bottom - min_y - chrome) / (RSX_SHELL_ROW_H_PT * u));
    if (fit_rows < 1) fit_rows = 1;
    int visible = rows < 1 ? 1 : rows;
    if (visible > fit_rows) visible = fit_rows;
    const float ph = chrome + (float)visible * RSX_SHELL_ROW_H_PT * u;
    float px = c.x + c.w * 0.5f - pw * 0.5f;
    if (px < RSX_SHELL_MARGIN_PT * u) px = RSX_SHELL_MARGIN_PT * u;
    if (px + pw > W - RSX_SHELL_MARGIN_PT * u) px = W - RSX_SHELL_MARGIN_PT * u - pw;
    float py = bottom - ph;
    if (py < min_y) py = min_y;
    L->panel = { px, py, pw, ph };
    L->panel_visible_rows = visible;
}
