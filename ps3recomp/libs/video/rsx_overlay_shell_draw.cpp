#include "rsx_overlay_shell_draw.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

struct Ctx {
    ImDrawList* dl;
    const rsx_overlay_assets* a;
    float u;  /* drawable pixels per layout point */
    float k;  /* drawable pixels per atlas pixel */
};

const char* const kCardLabels[RSX_SHELL_CARD_COUNT] = {
    "Voltar ao jogo", "Tela", "Controles", "Som", "Desenvolvedor", "Sair"
};
const int kCardIcons[RSX_SHELL_CARD_COUNT] = {
    RSX_OVERLAY_ICON_PLAY, RSX_OVERLAY_ICON_DISPLAY, RSX_OVERLAY_ICON_GAMEPAD,
    RSX_OVERLAY_ICON_SPEAKER, RSX_OVERLAY_ICON_HAMMER, RSX_OVERLAY_ICON_POWER
};
const char* const kRowLabels[RSX_SHELL_ROW_COUNT] = {
    "Tela cheia", "VSync", "Tamanho da janela",
    "Zona morta do anal\xC3\xB3gico", "Vibra\xC3\xA7\xC3\xA3o", "Remapear bot\303\265es", "Restaurar padr\xC3\xA3o",
    "Som do jogo", "Sons da interface", "Vibra\xC3\xA7\xC3\xA3o da interface", "Logs"
};
const char kUnavailable[] = "indispon\xC3\xADvel";
const float kPi = 3.14159265358979f; /* IM_PI lives in imgui_internal.h */

float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
ImU32 rgba(int r, int g, int b, float a) { return IM_COL32(r, g, b, (int)(clamp01(a) * 255.0f + 0.5f)); }

/* GoW palette -- the single source of the shell's colours. Same values as the
 * launcher's Theme.Palette (games/gow2/launcher/macos/Theme.swift). */
struct Rgb { int r, g, b; };
constexpr Rgb kStone950 = { 0x0E, 0x0D, 0x0C };
constexpr Rgb kStone900 = { 0x17, 0x15, 0x13 };
constexpr Rgb kStone800 = { 0x22, 0x1F, 0x1C };
constexpr Rgb kStone700 = { 0x2E, 0x2A, 0x26 };
constexpr Rgb kParchment = { 0xED, 0xE6, 0xDC };
constexpr Rgb kAsh = { 0xA8, 0x9F, 0x93 };
constexpr Rgb kBlood = { 0x9B, 0x1B, 0x12 };
constexpr Rgb kBloodHi = { 0xB8, 0x26, 0x1A };
constexpr Rgb kGold = { 0xC9, 0xA1, 0x5A };
constexpr Rgb kGoldHi = { 0xE3, 0xC2, 0x7E };
constexpr Rgb kAmber = { 0xD0, 0xA2, 0x3C };
/* bloodHi lifted for legibility as small text on stone glass (error logs). */
constexpr Rgb kBloodHiLight = { 0xE8, 0x5A, 0x4C };

ImU32 pal(Rgb c, float a) { return rgba(c.r, c.g, c.b, a); }
ImU32 parchment(float a) { return pal(kParchment, a); }  /* titles, primary text, icons */
ImU32 ash(float a) { return pal(kAsh, a); }              /* secondary text */
ImU32 gold(float a) { return pal(kGold, a); }            /* focus */
ImU32 on_accent(float a) { return pal(kBloodHi, a); }    /* switch / slider ON */
ImU32 off_track(float a) { return pal(kAsh, 0.40f * a); } /* warm stone OFF track */

int glyph_for(const rsx_overlay_assets* a, int style, uint32_t cp)
{
    int gi = rsx_overlay_glyph_index(cp);
    if (gi < 0 || !a->glyphs[style][gi].present)
        gi = rsx_overlay_glyph_index('?');
    return gi;
}

float text_w(const Ctx& c, int style, const char* s) { return rsx_overlay_text_width(c.a, style, s, c.k); }

float text(const Ctx& c, int style, float x, float baseline, const char* s, ImU32 col)
{
    const float iw = 1.0f / (float)c.a->width, ih = 1.0f / (float)c.a->height;
    float pen = x;
    if ((col & IM_COL32_A_MASK) == 0)
        return text_w(c, style, s);
    while (*s) {
        const rsx_overlay_glyph& g = c.a->glyphs[style][glyph_for(c.a, style, rsx_overlay_utf8_next(&s))];
        if (g.w != 0 && g.h != 0) {
            const ImVec2 p0(pen + (float)g.left * c.k, baseline - (float)g.top * c.k);
            const ImVec2 p1(p0.x + (float)g.w * c.k, p0.y + (float)g.h * c.k);
            c.dl->PrimReserve(6, 4);
            c.dl->PrimRectUV(p0, p1, ImVec2((float)g.x * iw, (float)g.y * ih),
                             ImVec2((float)(g.x + g.w) * iw, (float)(g.y + g.h) * ih), col);
        }
        pen += g.advance * c.k;
    }
    return pen - x;
}

float baseline_at(const Ctx& c, int style, float center_y)
{
    return center_y + (c.a->ascent[style] - c.a->descent[style]) * 0.5f * c.k;
}

void text_centered(const Ctx& c, int style, float cx, float baseline, const char* s, ImU32 col)
{
    text(c, style, cx - text_w(c, style, s) * 0.5f, baseline, s, col);
}

void text_right(const Ctx& c, int style, float right, float baseline, const char* s, ImU32 col)
{
    text(c, style, right - text_w(c, style, s), baseline, s, col);
}

/* Truncates with an ellipsis to fit max_w. */
const char* fit(const Ctx& c, int style, const char* s, float max_w, char* buf, size_t cap)
{
    if (text_w(c, style, s) <= max_w || cap < 8)
        return s;
    const float ell = text_w(c, style, "\xE2\x80\xA6");
    float w = 0.0f;
    const char* p = s;
    const char* last = s;
    while (*p) {
        const char* next = p;
        const rsx_overlay_glyph& g = c.a->glyphs[style][glyph_for(c.a, style, rsx_overlay_utf8_next(&next))];
        if (w + g.advance * c.k + ell > max_w)
            break;
        w += g.advance * c.k;
        p = next;
        last = p;
    }
    size_t n = (size_t)(last - s);
    if (n + 4 > cap)
        n = cap - 4;
    std::memcpy(buf, s, n);
    std::memcpy(buf + n, "\xE2\x80\xA6", 4); /* includes the NUL */
    return buf;
}

void shadow(const Ctx& c, ImVec2 p0, ImVec2 p1, float rounding, float alpha)
{
    static const float grow[4] = { 2.0f, 4.0f, 7.0f, 11.0f };
    static const float a4[4] = { 0.10f, 0.07f, 0.045f, 0.025f };
    for (int i = 0; i < 4; ++i) {
        const float g = grow[i] * c.u, dy = 4.0f * c.u;
        c.dl->AddRectFilled(ImVec2(p0.x - g, p0.y - g + dy), ImVec2(p1.x + g, p1.y + g + dy),
                            rgba(0, 0, 0, a4[i] * alpha), rounding + g);
    }
}

void glow(const Ctx& c, ImVec2 p0, ImVec2 p1, float rounding, float alpha)
{
    static const float grow[3] = { 3.0f, 6.0f, 9.0f };
    static const float a3[3] = { 0.12f, 0.07f, 0.04f };
    for (int i = 0; i < 3; ++i) {
        const float g = grow[i] * c.u;
        c.dl->AddRectFilled(ImVec2(p0.x - g, p0.y - g), ImVec2(p1.x + g, p1.y + g),
                            gold(a3[i] * alpha), rounding + g);
    }
}

void geometric_icon(const Ctx& c, int id, ImVec2 m, float s, ImU32 col)
{
    ImDrawList* dl = c.dl;
    const float h = s * 0.5f, t = std::fmax(1.5f, s * 0.07f);
    switch (id) {
    case RSX_OVERLAY_ICON_PLAY:
        dl->AddTriangleFilled(ImVec2(m.x - h * 0.55f, m.y - h * 0.7f), ImVec2(m.x + h * 0.75f, m.y),
                              ImVec2(m.x - h * 0.55f, m.y + h * 0.7f), col);
        break;
    case RSX_OVERLAY_ICON_DISPLAY:
        dl->AddRect(ImVec2(m.x - h, m.y - h * 0.7f), ImVec2(m.x + h, m.y + h * 0.45f), col, s * 0.08f, 0, t);
        dl->AddLine(ImVec2(m.x - h * 0.4f, m.y + h * 0.75f), ImVec2(m.x + h * 0.4f, m.y + h * 0.75f), col, t);
        break;
    case RSX_OVERLAY_ICON_GAMEPAD:
        dl->AddRect(ImVec2(m.x - h, m.y - h * 0.45f), ImVec2(m.x + h, m.y + h * 0.45f), col, h * 0.45f, 0, t);
        dl->AddLine(ImVec2(m.x - h * 0.62f, m.y), ImVec2(m.x - h * 0.22f, m.y), col, t);
        dl->AddLine(ImVec2(m.x - h * 0.42f, m.y - h * 0.2f), ImVec2(m.x - h * 0.42f, m.y + h * 0.2f), col, t);
        dl->AddCircleFilled(ImVec2(m.x + h * 0.35f, m.y - h * 0.1f), t, col);
        dl->AddCircleFilled(ImVec2(m.x + h * 0.58f, m.y + h * 0.12f), t, col);
        break;
    case RSX_OVERLAY_ICON_SPEAKER:
        dl->AddRectFilled(ImVec2(m.x - h * 0.9f, m.y - h * 0.25f), ImVec2(m.x - h * 0.55f, m.y + h * 0.25f), col);
        dl->AddQuadFilled(ImVec2(m.x - h * 0.55f, m.y - h * 0.25f), ImVec2(m.x - h * 0.05f, m.y - h * 0.7f),
                          ImVec2(m.x - h * 0.05f, m.y + h * 0.7f), ImVec2(m.x - h * 0.55f, m.y + h * 0.25f), col);
        dl->PathArcTo(ImVec2(m.x - h * 0.05f, m.y), h * 0.45f, -0.9f, 0.9f);
        dl->PathStroke(col, 0, t);
        dl->PathArcTo(ImVec2(m.x - h * 0.05f, m.y), h * 0.8f, -0.9f, 0.9f);
        dl->PathStroke(col, 0, t);
        break;
    case RSX_OVERLAY_ICON_HAMMER:
        dl->AddLine(ImVec2(m.x - h * 0.6f, m.y + h * 0.8f), ImVec2(m.x + h * 0.25f, m.y - h * 0.05f), col, t * 1.4f);
        dl->AddQuadFilled(ImVec2(m.x + h * 0.05f, m.y - h * 0.55f), ImVec2(m.x + h * 0.55f, m.y - h * 0.05f),
                          ImVec2(m.x + h * 0.35f, m.y + h * 0.15f), ImVec2(m.x - h * 0.15f, m.y - h * 0.35f), col);
        break;
    case RSX_OVERLAY_ICON_POWER:
        dl->PathArcTo(m, h * 0.75f, -kPi * 0.5f + 0.6f, kPi * 1.5f - 0.6f);
        dl->PathStroke(col, 0, t);
        dl->AddLine(ImVec2(m.x, m.y - h * 0.95f), ImVec2(m.x, m.y - h * 0.1f), col, t);
        break;
    default:
        break;
    }
}

void icon(const Ctx& c, int id, ImVec2 center, float size_px, ImU32 col)
{
    const rsx_overlay_assets* a = c.a;
    if (a->system_icons && a->icons[id].present) {
        const rsx_overlay_glyph& g = a->icons[id];
        const float s = size_px / (RSX_OVERLAY_ICON_POINT_SIZE * a->scale);
        const float w = (float)g.w * s, h = (float)g.h * s;
        const float iw = 1.0f / (float)a->width, ih = 1.0f / (float)a->height;
        const ImVec2 p0(center.x - w * 0.5f, center.y - h * 0.5f);
        c.dl->PrimReserve(6, 4);
        c.dl->PrimRectUV(p0, ImVec2(p0.x + w, p0.y + h), ImVec2((float)g.x * iw, (float)g.y * ih),
                         ImVec2((float)(g.x + g.w) * iw, (float)(g.y + g.h) * ih), col);
        return;
    }
    geometric_icon(c, id, center, size_px, col);
}

void chevron(const Ctx& c, float right, float cy, ImU32 col)
{
    const float s = 5.0f * c.u;
    c.dl->AddLine(ImVec2(right - s, cy - s), ImVec2(right, cy), col, 1.6f * c.u);
    c.dl->AddLine(ImVec2(right, cy), ImVec2(right - s, cy + s), col, 1.6f * c.u);
}

void checkmark(const Ctx& c, float right, float cy, ImU32 col)
{
    const float s = 6.0f * c.u;
    c.dl->AddLine(ImVec2(right - 2.0f * s, cy), ImVec2(right - 1.2f * s, cy + 0.8f * s), col, 2.0f * c.u);
    c.dl->AddLine(ImVec2(right - 1.2f * s, cy + 0.8f * s), ImVec2(right, cy - s), col, 2.0f * c.u);
}

void toggle(const Ctx& c, float right, float cy, bool on, float fade)
{
    const float w = 44.0f * c.u, h = 26.0f * c.u;
    const ImVec2 p0(right - w, cy - h * 0.5f), p1(right, cy + h * 0.5f);
    c.dl->AddRectFilled(p0, p1, on ? on_accent(fade) : off_track(fade), h * 0.5f);
    const float kx = on ? p1.x - h * 0.5f : p0.x + h * 0.5f;
    c.dl->AddCircleFilled(ImVec2(kx, cy), h * 0.5f - 2.5f * c.u, parchment(fade), 24);
}

void slider(const Ctx& c, float right, float cy, int value, int max, float fade)
{
    char t[16];
    std::snprintf(t, sizeof t, "%d%%", value);
    const float tw = text_w(c, RSX_OVERLAY_TEXT_ROW, t);
    text(c, RSX_OVERLAY_TEXT_ROW, right - tw, baseline_at(c, RSX_OVERLAY_TEXT_ROW, cy), t, parchment(0.9f * fade));
    const float track = 120.0f * c.u, x1 = right - tw - 12.0f * c.u, x0 = x1 - track;
    const float f = value >= max ? 1.0f : (float)value / (float)max;
    c.dl->AddRectFilled(ImVec2(x0, cy - 2.0f * c.u), ImVec2(x1, cy + 2.0f * c.u), off_track(fade), 2.0f * c.u);
    c.dl->AddRectFilled(ImVec2(x0, cy - 2.0f * c.u), ImVec2(x0 + track * f, cy + 2.0f * c.u), pal(kBlood, fade), 2.0f * c.u);
    c.dl->AddCircleFilled(ImVec2(x0 + track * f, cy), 7.0f * c.u, parchment(fade), 20);
}

uint8_t row_support(int row, const rsx_shell_view* v)
{
    if (row != RSX_SHELL_ROW_FULLSCREEN && row != RSX_SHELL_ROW_VSYNC && row != RSX_SHELL_ROW_WINDOW_SIZE)
        return RSX_OVERLAY_SETTING_LIVE;
    if (!v->caps_registered)
        return RSX_OVERLAY_SETTING_UNAVAILABLE;
    return row == RSX_SHELL_ROW_FULLSCREEN ? v->caps.fullscreen
         : row == RSX_SHELL_ROW_VSYNC ? v->caps.vsync : v->caps.window_size;
}

void row_highlight(const Ctx& c, float x, float top, float w, float h, float fade)
{
    c.dl->AddRectFilled(ImVec2(x, top + 2.0f * c.u), ImVec2(x + w, top + h - 2.0f * c.u), gold(0.16f * fade), 10.0f * c.u);
}

void draw_rows(const Ctx& c, const rsx_shell* s, const rsx_shell_view* v, float x, float y, float w, float fade)
{
    rsx_shell_row rows[8];
    const int n = rsx_shell_panel_rows(s->card, rows, 8);
    const float row_h = RSX_SHELL_ROW_H_PT * c.u;
    for (int i = 0; i < n; ++i) {
        const float top = y + (float)i * row_h, cy = top + row_h * 0.5f, right = x + w - 12.0f * c.u;
        const uint8_t support = row_support(rows[i], v);
        const bool enabled = support != RSX_OVERLAY_SETTING_UNAVAILABLE;
        if (s->level == RSX_SHELL_LEVEL_PANEL && s->row == i)
            row_highlight(c, x, top, w, row_h, fade);
        const ImU32 label = enabled ? parchment(0.95f * fade) : ash(0.75f * fade);   /* disabled, still legible on stone */
        if (support == RSX_OVERLAY_SETTING_RESTART_REQUIRED) {
            const float b = cy - 1.0f * c.u;
            text(c, RSX_OVERLAY_TEXT_ROW, x + 12.0f * c.u, b, kRowLabels[rows[i]], label);
            text(c, RSX_OVERLAY_TEXT_CAPTION, x + 12.0f * c.u,
                 b + c.a->ascent[RSX_OVERLAY_TEXT_CAPTION] * c.k + 3.0f * c.u, "requer reiniciar", ash(0.85f * fade));
        } else {
            text(c, RSX_OVERLAY_TEXT_ROW, x + 12.0f * c.u, baseline_at(c, RSX_OVERLAY_TEXT_ROW, cy), kRowLabels[rows[i]], label);
        }
        if (!enabled) {
            text_right(c, RSX_OVERLAY_TEXT_CAPTION, right, baseline_at(c, RSX_OVERLAY_TEXT_CAPTION, cy), kUnavailable, ash(0.75f * fade));
            continue;
        }
        switch (rows[i]) {
        case RSX_SHELL_ROW_FULLSCREEN: toggle(c, right, cy, v->graphics.fullscreen != 0, fade); break;
        case RSX_SHELL_ROW_VSYNC: toggle(c, right, cy, v->graphics.vsync != 0, fade); break;
        case RSX_SHELL_ROW_WINDOW_SIZE: {
            char t[32];
            std::snprintf(t, sizeof t, "%u\xC3\x97%u", v->graphics.window_width, v->graphics.window_height);
            chevron(c, right, cy, ash(fade));
            text_right(c, RSX_OVERLAY_TEXT_ROW, right - 14.0f * c.u, baseline_at(c, RSX_OVERLAY_TEXT_ROW, cy), t, ash(fade));
            break;
        }
        case RSX_SHELL_ROW_DEADZONE: slider(c, right, cy, v->analog_deadzone, RSX_SHELL_DEADZONE_MAX, fade); break;
        case RSX_SHELL_ROW_VIBRATION: toggle(c, right, cy, v->vibration != 0, fade); break;
        case RSX_SHELL_ROW_REMAP: chevron(c, right, cy, ash(fade)); break;
        case RSX_SHELL_ROW_GAME_SOUND: toggle(c, right, cy, v->game_mute == 0, fade); break;
        case RSX_SHELL_ROW_UI_SOUNDS: toggle(c, right, cy, v->ui_sounds != 0, fade); break;
        case RSX_SHELL_ROW_UI_HAPTICS: toggle(c, right, cy, v->ui_haptics != 0, fade); break;
        case RSX_SHELL_ROW_LOGS: {
            char t[16];
            std::snprintf(t, sizeof t, "%d", v->log_count);
            chevron(c, right, cy, ash(fade));
            text_right(c, RSX_OVERLAY_TEXT_ROW, right - 14.0f * c.u, baseline_at(c, RSX_OVERLAY_TEXT_ROW, cy), t, ash(0.9f * fade));
            break;
        }
        default: break;
        }
    }
}

void draw_developer(const Ctx& c, const rsx_shell* s, const rsx_shell_view* v, float x, float y, float w, float fade)
{
    static const char* const labels[5] = {
        "FPS", "Draws do jogo", "Estado do WAD", "Shaders (cache)", "Compila\xC3\xA7\303\265es"
    };
    const rsx_overlay_snapshot* m = v->snapshot;
    const float row_h = RSX_SHELL_ROW_H_PT * c.u, vx = x + w * 0.42f;
    char values[5][128];
    char buf[160];
    if (v->metrics_available) {
        std::snprintf(values[0], sizeof values[0], "%.1f  (%.2f ms)", m->fps, m->frame_ms);
        std::snprintf(values[1], sizeof values[1], "%llu por quadro", (unsigned long long)m->guest_draws_frame);
    } else {
        std::snprintf(values[0], sizeof values[0], "%s", kUnavailable);
        std::snprintf(values[1], sizeof values[1], "%s", kUnavailable);
    }
    std::snprintf(values[2], sizeof values[2], "%s", m->wad_state_available ? m->wad_state : kUnavailable);
    if (m->shader_stats_available) {
        std::snprintf(values[3], sizeof values[3], "%u acertos \xC2\xB7 %u falhas", m->shader_hits, m->shader_misses);
        std::snprintf(values[4], sizeof values[4], "%u compilados \xC2\xB7 %u falhas", m->shader_compiles, m->shader_failures);
    } else {
        std::snprintf(values[3], sizeof values[3], "%s", kUnavailable);
        std::snprintf(values[4], sizeof values[4], "%s", kUnavailable);
    }
    for (int i = 0; i < 5; ++i) {
        const float cy = y + ((float)i + 0.5f) * row_h;
        text(c, RSX_OVERLAY_TEXT_ROW, x + 12.0f * c.u, baseline_at(c, RSX_OVERLAY_TEXT_ROW, cy), labels[i], ash(0.9f * fade));
        text(c, RSX_OVERLAY_TEXT_ROW, vx, baseline_at(c, RSX_OVERLAY_TEXT_ROW, cy),
             fit(c, RSX_OVERLAY_TEXT_ROW, values[i], x + w - 12.0f * c.u - vx, buf, sizeof buf), parchment(0.95f * fade));
    }
    /* Logs: the only interactive row. */
    draw_rows(c, s, v, x, y + 5.0f * row_h, w, fade);
}

void draw_list(const Ctx& c, const rsx_shell* s, const rsx_shell_layout* L, const rsx_shell_view* v,
               float x, float y, float w, float fade)
{
    const float row_h = RSX_SHELL_ROW_H_PT * c.u, right = x + w - 12.0f * c.u;
    const int count = s->list == RSX_SHELL_LIST_WINDOW_SIZE ? RSX_SHELL_WINDOW_SIZE_COUNT
                    : s->list == RSX_SHELL_LIST_REMAP ? RSX_SHELL_REMAP_COUNT : v->log_count;
    char buf[256];
    if (s->list == RSX_SHELL_LIST_LOGS && count == 0) {
        text(c, RSX_OVERLAY_TEXT_ROW, x + 12.0f * c.u, baseline_at(c, RSX_OVERLAY_TEXT_ROW, y + row_h * 0.5f),
             "Nenhum log recente", ash(0.8f * fade));
        return;
    }
    int last = s->list_scroll + L->panel_visible_rows;
    if (last > count) last = count;
    for (int i = s->list_scroll; i < last; ++i) {
        const float top = y + (float)(i - s->list_scroll) * row_h, cy = top + row_h * 0.5f;
        const float b = baseline_at(c, RSX_OVERLAY_TEXT_ROW, cy);
        if (s->list_index == i)
            row_highlight(c, x, top, w, row_h, fade);
        if (s->list == RSX_SHELL_LIST_WINDOW_SIZE) {
            std::snprintf(buf, sizeof buf, "%u\xC3\x97%u", rsx_shell_window_sizes[i][0], rsx_shell_window_sizes[i][1]);
            text(c, RSX_OVERLAY_TEXT_ROW, x + 12.0f * c.u, b, buf, parchment(0.95f * fade));
            if (v->graphics.window_width == rsx_shell_window_sizes[i][0] &&
                v->graphics.window_height == rsx_shell_window_sizes[i][1])
                checkmark(c, right, cy, pal(kGoldHi, fade));
        } else if (s->list == RSX_SHELL_LIST_REMAP) {
            const char* host = "\xE2\x80\x94";
            for (int h = 0; h < RSX_SHELL_REMAP_COUNT; ++h)
                if (v->map[h] == (uint16_t)(1u << i))
                    host = rsx_shell_button_names[h];
            text(c, RSX_OVERLAY_TEXT_ROW, x + 12.0f * c.u, b, rsx_shell_button_names[i], parchment(0.95f * fade));
            if (s->level == RSX_SHELL_LEVEL_CAPTURE && s->capture_guest == i)
                text_right(c, RSX_OVERLAY_TEXT_ROW, right, b, "Pressione um bot\xC3\xA3o\xE2\x80\xA6", pal(kGoldHi, fade));
            else
                text_right(c, RSX_OVERLAY_TEXT_ROW, right, b, host, ash(fade));
        } else {
            const char* line = v->log_lines[i];
            const ImU32 lc = std::strncmp(line, "[ERROR]", 7) == 0 ? pal(kBloodHiLight, fade)
                           : std::strncmp(line, "[WARN]", 6) == 0 ? pal(kAmber, fade) : parchment(0.85f * fade);
            text(c, RSX_OVERLAY_TEXT_CAPTION, x + 12.0f * c.u, baseline_at(c, RSX_OVERLAY_TEXT_CAPTION, cy),
                 fit(c, RSX_OVERLAY_TEXT_CAPTION, line, w - 24.0f * c.u, buf, sizeof buf), lc);
        }
    }
}

const char* panel_title(const rsx_shell* s)
{
    if (s->level == RSX_SHELL_LEVEL_LIST || s->level == RSX_SHELL_LEVEL_CAPTURE) {
        switch (s->list) {
        case RSX_SHELL_LIST_WINDOW_SIZE: return "Tamanho da janela";
        case RSX_SHELL_LIST_REMAP: return "Remapear bot\303\265es";
        case RSX_SHELL_LIST_LOGS: return "Logs";
        default: break;
        }
    }
    return kCardLabels[s->card];
}

void draw_panel(const Ctx& c, const rsx_shell* s, const rsx_shell_layout* L, const rsx_shell_view* v,
                float open, double now)
{
    const rsx_shell_rect& P = L->panel;
    if (P.w <= 0.0f || P.h <= 0.0f)
        return;
    const float fade = open * rsx_shell_panel_fade(s, now);
    const ImVec2 p0(P.x, P.y), p1(P.x + P.w, P.y + P.h);
    const float rounding = 16.0f * c.u, pad = RSX_SHELL_PANEL_PAD_PT * c.u;
    shadow(c, p0, p1, rounding, open);
    c.dl->AddRectFilled(p0, p1, pal(kStone900, 0.86f * open), rounding);
    text(c, RSX_OVERLAY_TEXT_TITLE, P.x + pad + 8.0f * c.u,
         P.y + pad + c.a->ascent[RSX_OVERLAY_TEXT_TITLE] * c.k + 8.0f * c.u, panel_title(s), parchment(fade));
    const float x = P.x + pad, y = P.y + pad + RSX_SHELL_PANEL_HEADER_PT * c.u, w = P.w - 2.0f * pad;
    const float cy = y + RSX_SHELL_ROW_H_PT * c.u * 0.5f;
    if (s->level == RSX_SHELL_LEVEL_LIST || s->level == RSX_SHELL_LEVEL_CAPTURE) {
        draw_list(c, s, L, v, x, y, w, fade);
    } else if (s->card == RSX_SHELL_CARD_RESUME) {
        text(c, RSX_OVERLAY_TEXT_ROW, x + 12.0f * c.u, baseline_at(c, RSX_OVERLAY_TEXT_ROW, cy),
             "Pressione X ou Enter para continuar", parchment(0.8f * fade));
    } else if (s->card == RSX_SHELL_CARD_QUIT) {
        char t[128];
        std::snprintf(t, sizeof t, "Fecha o %s", v->title);
        text(c, RSX_OVERLAY_TEXT_ROW, x + 12.0f * c.u, baseline_at(c, RSX_OVERLAY_TEXT_ROW, cy), t, parchment(0.8f * fade));
    } else if (s->card == RSX_SHELL_CARD_DEVELOPER) {
        draw_developer(c, s, v, x, y, w, fade);
    } else {
        draw_rows(c, s, v, x, y, w, fade);
    }
}

void draw_top_strip(const Ctx& c, const rsx_shell_layout* L, const rsx_shell_view* v, float open)
{
    const float cy = L->top_strip.y + L->top_strip.h * 0.5f;
    const ImU32 fg = parchment(0.92f * open);
    float x = L->top_strip.x + L->top_strip.w - 24.0f * c.u;
    text(c, RSX_OVERLAY_TEXT_TITLE, L->top_strip.x + 24.0f * c.u,
         baseline_at(c, RSX_OVERLAY_TEXT_TITLE, cy), v->title, fg);
    if (v->controller_connected && v->battery_percent >= 0) {
        char pct[8];
        std::snprintf(pct, sizeof pct, "%d%%", v->battery_percent);
        x -= text_w(c, RSX_OVERLAY_TEXT_CAPTION, pct);
        text(c, RSX_OVERLAY_TEXT_CAPTION, x, baseline_at(c, RSX_OVERLAY_TEXT_CAPTION, cy), pct, fg);
        x -= 6.0f * c.u;
        const float bw = 22.0f * c.u, bh = 11.0f * c.u;
        const ImVec2 b0(x - bw, cy - bh * 0.5f), b1(x, cy + bh * 0.5f);
        c.dl->AddRect(b0, b1, fg, 2.0f * c.u, 0, 1.2f * c.u);
        c.dl->AddRectFilled(ImVec2(b1.x + 1.0f * c.u, cy - bh * 0.2f), ImVec2(b1.x + 2.5f * c.u, cy + bh * 0.2f), fg);
        c.dl->AddRectFilled(ImVec2(b0.x + 2.0f * c.u, b0.y + 2.0f * c.u),
                            ImVec2(b0.x + 2.0f * c.u + (bw - 4.0f * c.u) * (float)v->battery_percent / 100.0f,
                                   b1.y - 2.0f * c.u), fg);
        x = b0.x - 12.0f * c.u;
    }
    if (v->controller_connected) {
        icon(c, RSX_OVERLAY_ICON_GAMEPAD, ImVec2(x - 12.0f * c.u, cy), 20.0f * c.u, fg);
        x -= 32.0f * c.u;
    }
    text_right(c, RSX_OVERLAY_TEXT_ROW, x, baseline_at(c, RSX_OVERLAY_TEXT_ROW, cy), v->clock, fg);
}

void draw_cards(const Ctx& c, const rsx_shell* s, const rsx_shell_layout* L, float open)
{
    for (int i = 0; i < RSX_SHELL_CARD_COUNT; ++i) {
        const rsx_shell_rect& r = L->cards[i];
        const float sc = s->card_scale[i].value;
        const float cx = r.x + r.w * 0.5f, cy = r.y + r.h * 0.5f;
        const ImVec2 p0(cx - r.w * 0.5f * sc, cy - r.h * 0.5f * sc), p1(cx + r.w * 0.5f * sc, cy + r.h * 0.5f * sc);
        const bool focused = i == s->card;
        const float alpha = open * (focused ? 1.0f : 0.75f);
        const float rounding = RSX_SHELL_CARD_RADIUS_PT * c.u * sc;
        shadow(c, p0, p1, rounding, alpha);
        if (focused)
            glow(c, p0, p1, rounding, open);
        c.dl->AddRectFilled(p0, p1, pal(kStone700, 0.82f * alpha), rounding);
        if (focused)
            c.dl->AddRect(p0, p1, gold(open), rounding, 0, 2.0f * c.u);
        icon(c, kCardIcons[i], ImVec2(cx, cy), 34.0f * c.u * sc, parchment(alpha));
        text_centered(c, RSX_OVERLAY_TEXT_LABEL, cx,
                      p1.y + 8.0f * c.u + c.a->ascent[RSX_OVERLAY_TEXT_LABEL] * c.k, kCardLabels[i], parchment(alpha));
    }
}

void draw_dialog(const Ctx& c, const rsx_shell* s, const rsx_shell_layout* L, const rsx_shell_view* v, float open)
{
    char q[128];
    c.dl->AddRectFilled(ImVec2(0.0f, 0.0f), ImVec2(L->width, L->height), rgba(0, 0, 0, 0.40f * open));
    const float w = 440.0f * c.u, h = 190.0f * c.u;
    const float x = (L->width - w) * 0.5f, y = (L->height - h) * 0.5f;
    const ImVec2 p0(x, y), p1(x + w, y + h);
    shadow(c, p0, p1, 18.0f * c.u, open);
    c.dl->AddRectFilled(p0, p1, pal(kStone800, 0.94f * open), 18.0f * c.u);
    std::snprintf(q, sizeof q, "Sair do %s?", v->title);
    text_centered(c, RSX_OVERLAY_TEXT_TITLE, x + w * 0.5f, y + 50.0f * c.u, q, parchment(open));
    text_centered(c, RSX_OVERLAY_TEXT_CAPTION, x + w * 0.5f, y + 78.0f * c.u,
                  "As configura\xC3\xA7\303\265es j\xC3\xA1 est\xC3\xA3o salvas.", ash(open));
    static const char* const buttons[2] = { "Sair", "Cancelar" };
    const float bw = 150.0f * c.u, bh = 44.0f * c.u, gap = 20.0f * c.u, by = y + h - 64.0f * c.u;
    const float bx0 = x + (w - 2.0f * bw - gap) * 0.5f;
    for (int i = 0; i < 2; ++i) {
        const ImVec2 b0(bx0 + (float)i * (bw + gap), by), b1(b0.x + bw, by + bh);
        const bool focused = s->dialog_choice == i;
        c.dl->AddRectFilled(b0, b1, focused ? parchment(open) : parchment(0.12f * open), bh * 0.5f);
        text_centered(c, RSX_OVERLAY_TEXT_ROW, b0.x + bw * 0.5f, baseline_at(c, RSX_OVERLAY_TEXT_ROW, by + bh * 0.5f),
                      buttons[i], focused ? pal(kStone950, open) : parchment(open));
    }
}

} // namespace

void rsx_shell_draw(ImDrawList* dl, const rsx_shell* s, const rsx_shell_layout* L,
                    const rsx_overlay_assets* a, const rsx_shell_view* v, double now)
{
    if (dl == nullptr || s == nullptr || L == nullptr || a == nullptr || a->rgba == nullptr ||
        v == nullptr || s->phase == RSX_SHELL_CLOSED || L->px_per_pt <= 0.0f)
        return;
    const Ctx c = { dl, a, L->px_per_pt, L->px_per_pt / a->scale };
    const float open = clamp01(s->open.value);
    /* 1. Dim: always the first command, so a renderer without blur (D3D12)
     *    still gets the dimmed backdrop. */
    dl->AddRectFilled(ImVec2(0.0f, 0.0f), ImVec2(L->width, L->height), rgba(0, 0, 0, RSX_SHELL_BACKDROP_DIM * open));
    /* 2. Bottom band: transparent -> ~92 % near-black. */
    dl->AddRectFilledMultiColor(ImVec2(L->band.x, L->band.y), ImVec2(L->band.x + L->band.w, L->band.y + L->band.h),
                                pal(kStone950, 0.0f), pal(kStone950, 0.0f),
                                pal(kStone950, 0.92f * open), pal(kStone950, 0.92f * open));
    draw_top_strip(c, L, v, open);
    draw_cards(c, s, L, open);
    draw_panel(c, s, L, v, open, now);
    if (s->level == RSX_SHELL_LEVEL_DIALOG)
        draw_dialog(c, s, L, v, open);
}
