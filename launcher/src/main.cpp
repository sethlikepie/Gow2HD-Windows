// Gow2HD-Windows launcher - window, pages, command line.
// The look is drawn with GDI only: dark warm background, a classical serif title and one Greek-key rule.
// No game artwork, fonts or sounds are used.
#include "common.h"
#include <windowsx.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <dwmapi.h>
#include <thread>
#include <memory>

// ================================================================ look
#define C_BG_TOP    RGB(0x17, 0x13, 0x10)
#define C_BG_BOT    RGB(0x0B, 0x0A, 0x09)
#define C_PANEL     RGB(0x12, 0x0F, 0x0D)
#define C_FIELD     RGB(0x1C, 0x18, 0x15)
#define C_LINE      RGB(0x36, 0x2C, 0x24)
#define C_LINE_HI   RGB(0x6B, 0x55, 0x3A)
#define C_BRONZE    RGB(0xB0, 0x8D, 0x57)
#define C_BRONZE_DK RGB(0x5C, 0x48, 0x31)
#define C_TEXT      RGB(0xE8, 0xE0, 0xD2)
#define C_DIM       RGB(0x95, 0x8A, 0x7B)
#define C_FAINT     RGB(0x5E, 0x55, 0x4B)
#define C_RED       RGB(0x8A, 0x1B, 0x16)
#define C_RED_HI    RGB(0xA6, 0x25, 0x1E)
#define C_OK        RGB(0x86, 0xA8, 0x6B)
#define C_WARN      RGB(0xD0, 0xA0, 0x48)
#define C_ERR       RGB(0xD2, 0x5A, 0x4C)

static const int W = 860, H = 560;    // client size in 96-dpi units

enum Page { PG_SETUP, PG_BUILD, PG_HOME, PG_SETTINGS, PG_ABOUT };
enum { BT_PRIMARY, BT_SECONDARY, BT_MENU, BT_ARROW, BT_LINK };
enum {
    ID_NONE = 0, ID_PLAY = 100, ID_SETTINGS, ID_REBUILD, ID_FOLDER, ID_UPDATE, ID_REMOVE, ID_ABOUT,
    ID_BR_GAME = 200, ID_BR_RAP, ID_BR_ELF, ID_BR_INSTALL, ID_BUILD, ID_RESCAN, ID_SETUP_BACK, ID_BR_PKG,
    ID_SHOWLOG = 300, ID_CANCEL, ID_RETRY, ID_BUILD_BACK, ID_BUILD_PLAY,
    ID_SET_PREV = 400, ID_SET_NEXT = 420, ID_SET_SAVE = 440, ID_SET_BACK,
    ID_ABOUT_BACK = 500, ID_ABOUT_PAGE,
};
static const int HOME_ITEMS = ID_ABOUT - ID_PLAY + 1;
enum { WM_APP_DETECTED = WM_APP + 1, WM_APP_PROGRESS, WM_APP_LOG, WM_APP_DONE, WM_APP_UPDATE, WM_APP_SELFUPDATE };
enum { TIMER_VALIDATE = 1, TIMER_TICK = 2, TIMER_TESTCANCEL = 3 };

struct Btn { int id; RECT rc; wstr text; int type; bool enabled; };

// ================================================================ state
static HINSTANCE g_inst;
static HWND g_hwnd;
static float g_scale = 1.0f;
static Page g_page = PG_SETUP;
static std::vector<Btn> g_btns;
static int g_hover = ID_NONE, g_pressed = ID_NONE, g_focusMenu = 0;
static HFONT f_title, f_sub, f_head, f_body, f_small, f_label, f_menu, f_menuBig, f_mono, f_btn, f_numeral, f_pct, f_edit;
static HBRUSH g_fieldBrush;
static InstallPaths g_ip;
static wstr g_status;                 // footer message on the home page

// setup page
static HWND g_edGame, g_edRap, g_edElf, g_edInstall;
struct FieldState { int level = 0; wstr msg; };   // level: 0 neutral, 1 ok, 2 warn, 3 error, 4 action needed
static FieldState g_fsGame, g_fsRap, g_fsElf, g_fsInstall;
static bool g_detecting = false, g_canBuild = false;
static wstr g_gameResolved;

// build page
static std::atomic<bool> g_cancel{ false };
static bool g_building = false;
static int g_buildPct = 0, g_buildResult = 0;      // 0 running, 1 ok, 2 failed
static wstr g_buildStep, g_buildErr;
static std::vector<wstr> g_logTail;
static DWORD g_buildStart = 0, g_buildEnd = 0;
static std::thread g_worker;
static BuildRequest g_lastReq;

// settings page
static const int kCaps[] = { 30, 60, 120, 144, 162, 165, 240, 300, 1000 };   // 162 = G-Sync cap for 165 Hz
static int g_setCap = 60, g_setBorderless = 0, g_setRes = 1, g_setOverlay = 0, g_setAutoplay = 0;
static bool g_setDirty = false;

static int S(float v) { return (int)(v * g_scale + 0.5f); }
static RECT SR(int x, int y, int w, int h) { RECT r = { S((float)x), S((float)y), S((float)(x + w)), S((float)(y + h)) }; return r; }

// ================================================================ helpers
static HFONT MakeFont(const wchar_t* face, int px, int weight, bool italic = false) {
    return CreateFontW(-S((float)px), 0, 0, 0, weight, italic, 0, 0, DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH, face);
}
static void MakeFonts() {
    HFONT* all[] = { &f_title, &f_sub, &f_head, &f_body, &f_small, &f_label, &f_menu, &f_menuBig, &f_mono, &f_btn, &f_numeral, &f_pct, &f_edit };
    for (auto f : all) if (*f) { DeleteObject(*f); *f = nullptr; }
    const wchar_t* serif = L"Palatino Linotype";
    f_title   = MakeFont(serif, 38, FW_NORMAL);
    f_sub     = MakeFont(L"Segoe UI", 11, FW_SEMIBOLD);
    f_head    = MakeFont(serif, 15, FW_NORMAL);
    f_body    = MakeFont(L"Segoe UI", 14, FW_NORMAL);
    f_small   = MakeFont(L"Segoe UI", 12, FW_NORMAL);
    f_label   = MakeFont(L"Segoe UI", 11, FW_SEMIBOLD);
    f_menu    = MakeFont(serif, 18, FW_NORMAL);
    f_menuBig = MakeFont(serif, 27, FW_NORMAL);
    f_mono    = MakeFont(L"Consolas", 12, FW_NORMAL);
    f_btn     = MakeFont(L"Segoe UI", 12, FW_SEMIBOLD);
    f_numeral = MakeFont(serif, 380, FW_NORMAL);
    f_pct     = MakeFont(serif, 30, FW_NORMAL);
    f_edit    = MakeFont(L"Segoe UI", 14, FW_NORMAL);
    for (HWND e : { g_edGame, g_edRap, g_edElf, g_edInstall }) if (e) SendMessageW(e, WM_SETFONT, (WPARAM)f_edit, TRUE);
}
static wstr GetText(HWND e) {
    int n = GetWindowTextLengthW(e);
    wstr s(n, L'\0');
    if (n) GetWindowTextW(e, &s[0], n + 1);
    return TrimW(s);
}
static void SetText(HWND e, const wstr& s) { SetWindowTextW(e, s.c_str()); }
static void Redraw() { InvalidateRect(g_hwnd, nullptr, FALSE); }

static void Text(HDC dc, HFONT f, COLORREF c, const wstr& s, RECT r, UINT fmt, int track = 0) {
    SelectObject(dc, f);
    SetTextColor(dc, c);
    SetTextCharacterExtra(dc, S((float)track));
    DrawTextW(dc, s.c_str(), (int)s.size(), &r, fmt | DT_NOPREFIX);
    SetTextCharacterExtra(dc, 0);
}
static void Fill(HDC dc, RECT r, COLORREF c) {
    SetDCBrushColor(dc, c);
    FillRect(dc, &r, (HBRUSH)GetStockObject(DC_BRUSH));
}
static void Frame(HDC dc, RECT r, COLORREF c) {
    SetDCBrushColor(dc, c);
    FrameRect(dc, &r, (HBRUSH)GetStockObject(DC_BRUSH));
}
static void HLine(HDC dc, int x0, int x1, int y, COLORREF c) { RECT r = SR(x0, y, x1 - x0, 1); r.bottom = r.top + std::max(1, S(1)); Fill(dc, r, c); }

// A Greek-key rule: a bottom rail with a small inward spiral every period.
static void Meander(HDC dc, int x0, int x1, int y, COLORREF c) {
    float u = 2.0f * g_scale;
    HPEN pen = CreatePen(PS_SOLID, std::max(1, S(1)), c);
    HGDIOBJ old = SelectObject(dc, pen);
    float px = S((float)x0), right = S((float)x1), top = S((float)y);
    float period = 7 * u;
    int n = (int)((right - px) / period);
    float start = px + ((right - px) - n * period) / 2;
    float bottom = top + 5 * u;
    MoveToEx(dc, (int)px, (int)(bottom), nullptr);
    LineTo(dc, (int)right, (int)(bottom));
    MoveToEx(dc, (int)px, (int)(top - 2 * u), nullptr);
    LineTo(dc, (int)right, (int)(top - 2 * u));
    for (int i = 0; i < n; ++i) {
        float x = start + i * period;
        POINT p[] = { { (int)x, (int)bottom }, { (int)x, (int)top }, { (int)(x + 5 * u), (int)top },
                      { (int)(x + 5 * u), (int)(top + 3 * u) }, { (int)(x + 2 * u), (int)(top + 3 * u) },
                      { (int)(x + 2 * u), (int)(top + 1.5f * u) } };
        Polyline(dc, p, 6);
    }
    SelectObject(dc, old);
    DeleteObject(pen);
}
static void Diamond(HDC dc, int cx, int cy, int r, COLORREF c) {
    POINT p[] = { { S((float)cx), S((float)(cy - r)) }, { S((float)(cx + r)), S((float)cy) },
                  { S((float)cx), S((float)(cy + r)) }, { S((float)(cx - r)), S((float)cy) } };
    SetDCBrushColor(dc, c); SetDCPenColor(dc, c);
    HGDIOBJ ob = SelectObject(dc, GetStockObject(DC_BRUSH)), op = SelectObject(dc, GetStockObject(DC_PEN));
    Polygon(dc, p, 4);
    SelectObject(dc, ob); SelectObject(dc, op);
}
static wstr Elide(HDC dc, HFONT f, const wstr& s, int maxW) {
    SelectObject(dc, f);
    std::vector<wchar_t> buf(s.begin(), s.end()); buf.push_back(0); buf.resize(buf.size() + 8);
    RECT r = { 0, 0, S((float)maxW), 100 };
    DrawTextW(dc, buf.data(), -1, &r, DT_PATH_ELLIPSIS | DT_MODIFYSTRING | DT_SINGLELINE | DT_CALCRECT | DT_NOPREFIX);
    return buf.data();
}

// ================================================================ settings ini
static wstr SettingsIniPath() {
    KvFile st; st.Load(g_ip.state);
    wstr exe = FindBuiltExe(g_ip, st);
    return JoinPath(exe.empty() ? g_ip.root : ParentDir(exe), L"port_settings.ini");
}
// The game exe's own defaults (winport/host/wp_release.c) when port_settings.ini lacks a key. The launcher shows these
// and writes only the keys the player changed, so it never pins a value over the exe's defaults.
static const int DEF_FPS_CAP = 300, DEF_BORDERLESS = 1, DEF_RES_SCALE = 2, DEF_FPS_OVERLAY = 0;
static int g_setLoaded[4];
static void LoadSettings() {
    KvFile k; k.Load(SettingsIniPath());
    g_setCap = k.GetInt("fps_cap", DEF_FPS_CAP);
    g_setBorderless = k.GetInt("borderless", DEF_BORDERLESS) ? 1 : 0;
    g_setRes = std::min(4, std::max(1, k.GetInt("res_scale", DEF_RES_SCALE)));
    g_setOverlay = k.GetInt("fps_overlay", DEF_FPS_OVERLAY) ? 1 : 0;
    g_setLoaded[0] = g_setCap; g_setLoaded[1] = g_setBorderless; g_setLoaded[2] = g_setRes; g_setLoaded[3] = g_setOverlay;
    KvFile st; st.Load(g_ip.state);
    g_setAutoplay = st.GetInt("autoplay", 0) ? 1 : 0;
    g_setDirty = false;
}
static bool SaveSettings() {
    wstr p = SettingsIniPath();
    KvFile k;
    bool had = k.Load(p);
    int cur[4] = { g_setCap, g_setBorderless, g_setRes, g_setOverlay };
    const char* keys[4] = { "fps_cap", "borderless", "res_scale", "fps_overlay" };
    bool any = false;
    for (int i = 0; i < 4; ++i) any |= cur[i] != g_setLoaded[i];
    if (any && !had) k.lines.push_back("# GoW2 HD port settings (same keys as the in-game F1 Port Settings panel)");
    for (int i = 0; i < 4; ++i)
        if (cur[i] != g_setLoaded[i]) k.Set(keys[i], std::to_string(cur[i]));
    KvFile st; st.Load(g_ip.state);
    st.Set("autoplay", std::to_string(g_setAutoplay));
    st.Save(g_ip.state);
    g_setDirty = false;
    if (!any) return true;
    for (int i = 0; i < 4; ++i) g_setLoaded[i] = cur[i];
    return k.Save(p);
}
static wstr CapLabel(int c) { return c >= 1000 ? L"Unlimited" : Fmt(L"%d fps", c); }

// ================================================================ layout (logical coordinates)
static void AddBtn(int id, int x, int y, int w, int h, const wstr& t, int type, bool en = true) {
    Btn b; b.id = id; b.rc = { x, y, x + w, y + h }; b.text = t; b.type = type; b.enabled = en;
    g_btns.push_back(b);
}
static const int ROW_Y0 = 178, ROW_H = 72;
static void PlaceEdits() {
    HWND eds[] = { g_edGame, g_edRap, g_edElf, g_edInstall };
    for (int i = 0; i < 4; ++i) {
        int y = ROW_Y0 + i * ROW_H + 18;
        RECT r = SR(48 + 10, y + 7, 624 - 20, 18);
        int fh = S(19);
        SetWindowPos(eds[i], nullptr, r.left, S((float)(y + 15)) - fh / 2, r.right - r.left, fh, SWP_NOZORDER | SWP_NOACTIVATE);
        ShowWindow(eds[i], g_page == PG_SETUP ? SW_SHOWNA : SW_HIDE);
    }
}
static void Layout() {
    g_btns.clear();
    switch (g_page) {
    case PG_HOME: {
        const wchar_t* items[] = { L"PLAY", L"SETTINGS", L"REBUILD", L"INSTALL FOLDER", L"CHECK FOR UPDATES", L"REMOVE BUILD", L"ABOUT" };
        int y = 152;
        for (int i = 0; i < HOME_ITEMS; ++i) {
            int h = i == 0 ? 52 : 36;
            AddBtn(ID_PLAY + i, 40, y, 380, h, items[i], BT_MENU);
            y += h + (i == 0 ? 10 : 0);
        }
        break;
    }
    case PG_SETUP: {
        int ids[] = { ID_BR_GAME, ID_BR_RAP, ID_BR_ELF, ID_BR_INSTALL };
        // the game row takes a folder or a PSN package: one button each (a picker cannot offer both at once)
        AddBtn(ID_BR_GAME, 684, ROW_Y0 + 18, 62, 30, L"FOLDER", BT_SECONDARY);
        AddBtn(ID_BR_PKG, 750, ROW_Y0 + 18, 62, 30, L".PKG", BT_SECONDARY);
        for (int i = 1; i < 4; ++i) AddBtn(ids[i], 684, ROW_Y0 + i * ROW_H + 18, 128, 30, i == 3 ? L"CHANGE" : L"BROWSE", BT_SECONDARY);
        bool built = IsBuilt(g_ip);
        AddBtn(ID_BUILD, 652, 488, 160, 40, built ? L"REBUILD" : L"BUILD", BT_PRIMARY, g_canBuild && !g_detecting);
        AddBtn(ID_RESCAN, 48, 494, 120, 28, L"SEARCH AGAIN", BT_LINK, !g_detecting);
        if (built) AddBtn(ID_SETUP_BACK, 532, 488, 108, 40, L"BACK", BT_SECONDARY);
        break;
    }
    case PG_BUILD:
        if (g_buildResult == 0) {
            AddBtn(ID_SHOWLOG, 48, 488, 120, 40, L"SHOW LOG", BT_SECONDARY);
            AddBtn(ID_CANCEL, 692, 488, 120, 40, L"CANCEL", BT_SECONDARY, !g_cancel);
        } else if (g_buildResult == 1) {
            AddBtn(ID_SHOWLOG, 48, 488, 120, 40, L"SHOW LOG", BT_SECONDARY);
            AddBtn(ID_BUILD_BACK, 520, 488, 120, 40, L"CONTINUE", BT_SECONDARY);
            AddBtn(ID_BUILD_PLAY, 652, 488, 160, 40, L"PLAY", BT_PRIMARY);
        } else {
            AddBtn(ID_SHOWLOG, 48, 488, 120, 40, L"SHOW LOG", BT_SECONDARY);
            AddBtn(ID_BUILD_BACK, 520, 488, 120, 40, L"BACK", BT_SECONDARY);
            AddBtn(ID_RETRY, 652, 488, 160, 40, L"TRY AGAIN", BT_PRIMARY);
        }
        break;
    case PG_SETTINGS:
        for (int i = 0; i < 5; ++i) {
            int y = 150 + i * 56;
            AddBtn(ID_SET_PREV + i, 556, y + 8, 32, 32, L"\u2039", BT_ARROW);
            AddBtn(ID_SET_NEXT + i, 780, y + 8, 32, 32, L"\u203A", BT_ARROW);
        }
        AddBtn(ID_SET_BACK, 520, 488, 120, 40, L"BACK", BT_SECONDARY);
        AddBtn(ID_SET_SAVE, 652, 488, 160, 40, L"SAVE", BT_PRIMARY, g_setDirty);
        break;
    case PG_ABOUT:
        AddBtn(ID_ABOUT_PAGE, 48, 494, 160, 28, L"PROJECT PAGE", BT_LINK);
        AddBtn(ID_ABOUT_BACK, 692, 488, 120, 40, L"BACK", BT_SECONDARY);
        break;
    }
    PlaceEdits();
}
static void Go(Page p) {
    g_page = p;
    g_hover = g_pressed = ID_NONE;
    if (p == PG_SETTINGS) LoadSettings();
    Layout();
    if (p != PG_SETUP) SetFocus(g_hwnd);
    Redraw();
}

// ================================================================ painting
static void DrawButton(HDC dc, const Btn& b) {
    RECT r = SR(b.rc.left, b.rc.top, b.rc.right - b.rc.left, b.rc.bottom - b.rc.top);
    bool hot = g_hover == b.id && b.enabled, down = hot && g_pressed == b.id;
    switch (b.type) {
    case BT_PRIMARY: {
        COLORREF bg = !b.enabled ? RGB(0x2A, 0x22, 0x1E) : down ? RGB(0x72, 0x16, 0x12) : hot ? C_RED_HI : C_RED;
        Fill(dc, r, bg);
        if (b.enabled) { RECT t = r; t.bottom = t.top + std::max(1, S(1)); Fill(dc, t, hot ? RGB(0xC4, 0x45, 0x3A) : RGB(0xA2, 0x30, 0x28)); }
        Text(dc, f_btn, b.enabled ? RGB(0xF6, 0xEE, 0xE2) : C_FAINT, b.text, r, DT_CENTER | DT_VCENTER | DT_SINGLELINE, 2);
        break;
    }
    case BT_SECONDARY:
        Fill(dc, r, down ? RGB(0x24, 0x1E, 0x19) : hot ? RGB(0x1F, 0x1A, 0x16) : C_PANEL);
        Frame(dc, r, hot ? C_BRONZE : C_LINE_HI);
        Text(dc, f_btn, !b.enabled ? C_FAINT : hot ? C_TEXT : RGB(0xCF, 0xC4, 0xB3), b.text, r, DT_CENTER | DT_VCENTER | DT_SINGLELINE, 2);
        break;
    case BT_LINK:
        Text(dc, f_btn, !b.enabled ? C_FAINT : hot ? C_TEXT : C_BRONZE, b.text, r, DT_LEFT | DT_VCENTER | DT_SINGLELINE, 2);
        break;
    case BT_ARROW:
        Text(dc, f_menuBig, hot ? C_TEXT : C_BRONZE, b.text, r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        break;
    case BT_MENU: {
        int idx = b.id - ID_PLAY;
        bool focus = hot || (g_hover == ID_NONE && g_focusMenu == idx && GetFocus() == g_hwnd);
        bool big = idx == 0;
        RECT t = r; t.left += S(22);
        if (focus) {
            RECT bar = SR(b.rc.left, b.rc.top + 6, 2, b.rc.bottom - b.rc.top - 12);
            Fill(dc, bar, C_RED_HI);
            Diamond(dc, b.rc.left + 12, (b.rc.top + b.rc.bottom) / 2, 3, C_BRONZE);
        }
        COLORREF c = focus ? C_TEXT : big ? RGB(0xD9, 0xCF, 0xBF) : C_DIM;
        Text(dc, big ? f_menuBig : f_menu, c, b.text, t, DT_LEFT | DT_VCENTER | DT_SINGLELINE, big ? 6 : 3);
        break;
    }
    }
}

static void DrawHeader(HDC dc, const wchar_t* section) {
    Text(dc, f_title, C_TEXT, L"GOD OF WAR II", SR(46, 30, 600, 50), DT_LEFT | DT_TOP | DT_SINGLELINE, 5);
    Text(dc, f_sub, C_BRONZE, L"HD  \u00B7  NATIVE WINDOWS PORT", SR(49, 82, 500, 18), DT_LEFT | DT_TOP | DT_SINGLELINE, 3);
    if (section) Text(dc, f_sub, C_DIM, section, SR(312, 82, 500, 18), DT_RIGHT | DT_TOP | DT_SINGLELINE, 3);
    Meander(dc, 48, 812, 114, C_BRONZE_DK);
}
static void DrawFooterLine(HDC dc) { HLine(dc, 48, 812, 470, C_LINE); }

static void DrawStatusLine(HDC dc, int x, int y, int w, const FieldState& fs) {
    COLORREF c = fs.level == 1 ? C_OK : fs.level == 2 ? C_WARN : fs.level == 3 ? C_ERR : fs.level == 4 ? C_BRONZE : C_FAINT;
    if (fs.level) Diamond(dc, x + 3, y + 8, 3, c);
    Text(dc, f_small, fs.level == 0 ? C_DIM : fs.level == 1 ? RGB(0xB4, 0xBE, 0xA6) : fs.level == 4 ? RGB(0xCF, 0xC4, 0xB3) : c, Elide(dc, f_small, fs.msg, w - 14),
         SR(x + 14, y, w - 14, 18), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
}

static void PaintSetup(HDC dc) {
    DrawHeader(dc, IsBuilt(g_ip) ? L"REBUILD" : L"FIRST-TIME SETUP");
    Text(dc, f_body, C_DIM,
         L"The port is built on this PC from your own copy of the game. Nothing from the game is downloaded.",
         SR(48, 136, 764, 22), DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
    const wchar_t* labels[] = { L"GAME FOLDER OR PSN PACKAGE  (NPUA80491 · .PKG)",L"LICENSE FILE  (.RAP)", L"DECRYPTED EBOOT.ELF  (OPTIONAL)", L"INSTALL FOLDER" };
    FieldState* st[] = { &g_fsGame, &g_fsRap, &g_fsElf, &g_fsInstall };
    for (int i = 0; i < 4; ++i) {
        int y = ROW_Y0 + i * ROW_H;
        Text(dc, f_label, C_DIM, labels[i], SR(48, y, 600, 16), DT_LEFT | DT_TOP | DT_SINGLELINE, 2);
        RECT box = SR(48, y + 18, 624, 30);
        Fill(dc, box, C_FIELD);
        Frame(dc, box, C_LINE);
        DrawStatusLine(dc, 48, y + 49, 764, *st[i]);
    }
}

static wstr Clock(DWORD ms) { DWORD s = ms / 1000; return Fmt(L"%02lu:%02lu", s / 60, s % 60); }

static void PaintBuild(HDC dc) {
    DrawHeader(dc, L"BUILDING");
    const wchar_t* head = g_buildResult == 0 ? (g_cancel ? L"Cancelling\u2026" : L"Building the Windows port")
                        : g_buildResult == 1 ? L"The port is built" : L"The build stopped";
    Text(dc, f_menu, C_TEXT, head, SR(48, 150, 560, 30), DT_LEFT | DT_VCENTER | DT_SINGLELINE, 1);
    Text(dc, f_pct, g_buildResult == 2 ? C_ERR : C_TEXT, Fmt(L"%d%%", g_buildPct), SR(612, 140, 200, 44), DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    wstr step = g_buildResult == 2 ? g_buildErr : g_buildStep;
    Text(dc, f_body, g_buildResult == 2 ? C_ERR : C_DIM, step, SR(48, 184, 764, 40), DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS);
    RECT track = SR(48, 230, 764, 4);
    Fill(dc, track, RGB(0x26, 0x20, 0x1B));
    RECT fill = track; fill.right = fill.left + (track.right - track.left) * g_buildPct / 100;
    Fill(dc, fill, g_buildResult == 2 ? RGB(0x6A, 0x2A, 0x24) : C_RED_HI);
    DWORD el = (g_buildResult ? g_buildEnd : GetTickCount()) - g_buildStart;
    Text(dc, f_small, C_FAINT, L"Elapsed " + Clock(el), SR(48, 242, 300, 18), DT_LEFT | DT_TOP | DT_SINGLELINE);
    Text(dc, f_small, C_FAINT, L"Log: " + g_ip.log, SR(352, 242, 460, 18), DT_RIGHT | DT_TOP | DT_SINGLELINE | DT_PATH_ELLIPSIS);
    RECT panel = SR(48, 272, 764, 180);
    Fill(dc, panel, C_PANEL);
    Frame(dc, panel, C_LINE);
    int y = 282;
    size_t from = g_logTail.size() > 9 ? g_logTail.size() - 9 : 0;
    for (size_t i = from; i < g_logTail.size(); ++i, y += 18)
        Text(dc, f_mono, i + 1 == g_logTail.size() ? RGB(0xB8, 0xAE, 0x9F) : C_FAINT, g_logTail[i], SR(60, y, 740, 18),
             DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
}

static void PaintHome(HDC dc) {
    // a large, barely visible numeral as the only ornament
    Text(dc, f_numeral, RGB(0x18, 0x14, 0x11), L"II", SR(470, 40, 400, 520), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    DrawHeader(dc, nullptr);
    KvFile st; st.Load(g_ip.state);
    KvFile ini; ini.Load(SettingsIniPath());
    int x = 520, y = 170;
    auto info = [&](const wchar_t* label, const wstr& value) {
        Text(dc, f_label, C_FAINT, label, SR(x, y, 292, 16), DT_LEFT | DT_TOP | DT_SINGLELINE, 2);
        Text(dc, f_small, RGB(0xC9, 0xBF, 0xB0), Elide(dc, f_small, value, 292), SR(x, y + 17, 292, 18), DT_LEFT | DT_TOP | DT_SINGLELINE);
        y += 50;
    };
    bool stale = st.Get("launcher_version") != LAUNCHER_VERSION;
    info(L"STATUS", stale ? L"Built by launcher " + st.GetW("launcher_version") + L"  \u00B7  a rebuild is recommended"
                          : L"Ready  \u00B7  built " + st.GetW("built_at"));
    info(L"GAME", st.GetW("game"));
    info(L"INSTALLED IN", g_ip.root);
    int cap = ini.GetInt("fps_cap", DEF_FPS_CAP);
    info(L"DISPLAY", CapLabel(cap) + (ini.GetInt("borderless", DEF_BORDERLESS) ? L"  \u00B7  borderless" : L"  \u00B7  windowed") +
                     Fmt(L"  \u00B7  %d\u00D7 resolution", ini.GetInt("res_scale", DEF_RES_SCALE)));
    DrawFooterLine(dc);
    Text(dc, f_small, C_FAINT, L"Launcher " LAUNCHER_VERSION_W, SR(48, 480, 200, 20), DT_LEFT | DT_TOP | DT_SINGLELINE);
    Text(dc, f_small, C_DIM, g_status, SR(250, 480, 562, 20), DT_RIGHT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
}

static void PaintSettings(HDC dc) {
    DrawHeader(dc, L"SETTINGS");
    struct Row { const wchar_t* name; const wchar_t* hint; wstr value; } rows[] = {
        { L"Frame rate cap", L"Applies live in game too", CapLabel(g_setCap) },
        { L"Display mode", L"F11 switches in game", g_setBorderless ? L"Borderless" : L"Windowed" },
        { L"Resolution scale", L"Takes effect at the next start", Fmt(L"%d\u00D7", g_setRes) },
        { L"FPS counter", L"F10 toggles in game", g_setOverlay ? L"On" : L"Off" },
        { L"Start the game directly", L"Skip this window (hold Shift to see it)", g_setAutoplay ? L"On" : L"Off" },
    };
    for (int i = 0; i < 5; ++i) {
        int y = 150 + i * 56;
        Text(dc, f_body, C_TEXT, rows[i].name, SR(48, y + 6, 400, 22), DT_LEFT | DT_TOP | DT_SINGLELINE);
        Text(dc, f_small, C_FAINT, rows[i].hint, SR(48, y + 27, 480, 18), DT_LEFT | DT_TOP | DT_SINGLELINE);
        Text(dc, f_menu, C_TEXT, rows[i].value, SR(588, y + 8, 192, 32), DT_CENTER | DT_VCENTER | DT_SINGLELINE, 1);
        HLine(dc, 48, 812, y + 54, C_LINE);
    }
    Text(dc, f_small, C_FAINT, L"Same settings as the in-game F1 panel. Saved to " + SettingsIniPath(),
         SR(48, 452, 764, 18), DT_LEFT | DT_TOP | DT_SINGLELINE | DT_PATH_ELLIPSIS);
}

static void PaintAbout(HDC dc) {
    DrawHeader(dc, L"ABOUT");
    int y = 146;
    auto para = [&](HFONT f, COLORREF c, const wstr& s, int h) {
        Text(dc, f, c, s, SR(48, y, 764, h), DT_LEFT | DT_TOP | DT_WORDBREAK);
        y += h;
    };
    para(f_head, C_TEXT, L"Gow2HD-Windows launcher " LAUNCHER_VERSION_W, 28);
    para(f_body, C_DIM, L"Builds a native Windows version of God of War II HD (PlayStation 3, NPUA80491) by static "
                        L"recompilation, on your PC, from the copy you own.", 48);
    y += 6;
    para(f_label, C_FAINT, L"BUILT ON", 20);
    para(f_body, C_TEXT, L"ps3recomp by sp00nznet  \u00B7  MIT license", 22);
    para(f_body, C_TEXT, L"gow2-recomp by andrebrumdev  \u00B7  MIT license", 22);
    para(f_body, C_TEXT, L"Windows port, Direct3D 12 renderer and launcher by sethlikepie", 30);
    y += 6;
    para(f_label, C_FAINT, L"LEGAL", 20);
    para(f_body, C_DIM, L"Not affiliated with or endorsed by Sony Interactive Entertainment or Santa Monica Studio. "
                        L"God of War is a trademark of Sony Interactive Entertainment. You must own the game: no game "
                        L"files are included or downloaded, and the port is built only from your own copy.", 80);
}

static void Paint(HDC dc, RECT cr) {
    TRIVERTEX v[2] = { { cr.left, cr.top, (COLOR16)(GetRValue(C_BG_TOP) << 8), (COLOR16)(GetGValue(C_BG_TOP) << 8), (COLOR16)(GetBValue(C_BG_TOP) << 8), 0 },
                       { cr.right, cr.bottom, (COLOR16)(GetRValue(C_BG_BOT) << 8), (COLOR16)(GetGValue(C_BG_BOT) << 8), (COLOR16)(GetBValue(C_BG_BOT) << 8), 0 } };
    GRADIENT_RECT gr = { 0, 1 };
    GradientFill(dc, v, 2, &gr, 1, GRADIENT_FILL_RECT_V);
    SetBkMode(dc, TRANSPARENT);
    switch (g_page) {
    case PG_SETUP: PaintSetup(dc); break;
    case PG_BUILD: PaintBuild(dc); break;
    case PG_HOME: PaintHome(dc); break;
    case PG_SETTINGS: PaintSettings(dc); break;
    case PG_ABOUT: PaintAbout(dc); break;
    }
    for (auto& b : g_btns) DrawButton(dc, b);
}

// ================================================================ setup logic
static void Validate() {
    wstr game = GetText(g_edGame), rap = GetText(g_edRap), elf = GetText(g_edElf), inst = GetText(g_edInstall);
    bool gameOk = false, rapOk = false, spaceOk = false, isPkg = false;
    if (g_detecting && game.empty()) g_fsGame = { 0, L"Searching RPCS3 and the usual folders\u2026" };
    else if (game.empty()) g_fsGame = { 4, L"Not found automatically. Pick the NPUA80491 folder (FOLDER) or the game's PSN package (.PKG), or drop it here." };
    else {
        GameCheck g = CheckGameInput(game);
        gameOk = g.status == GAME_OK;
        isPkg = g.isPkg;
        g_gameResolved = gameOk ? g.dir : wstr();
        g_fsGame = { gameOk ? 1 : 3, gameOk ? g.message + (IEquals(g.dir, FullPath(game)) ? L"" : L"  (" + g.dir + L")") : g.message };
    }
    if (g_detecting && rap.empty()) g_fsRap = { 0, L"Searching\u2026" };
    else if (rap.empty()) g_fsRap = { 4, L"Not found automatically. Browse to " RAP_FILE_NAME L" (RPCS3: dev_hdd0\\home\\<user>\\exdata)." };
    else {
        wstr m; rapOk = CheckRapFile(rap, m);
        g_fsRap = { rapOk ? (m.find(L"may belong") != wstr::npos ? 2 : 1) : 3, m };
    }
    if (elf.empty()) g_fsElf = { 0, L"Not needed. The build decrypts EBOOT.BIN with your license file." };
    else if (FileExists(elf)) g_fsElf = { 1, L"Will be used instead of decrypting EBOOT.BIN." };
    else g_fsElf = { 3, L"File not found." };
    if (inst.empty()) g_fsInstall = { 3, L"Pick a folder." };
    else {
        uint64_t freeB = FreeSpace(inst);
        bool already = FileExists(JoinPath(inst, L"launcher_state.ini"));
        // from a package the extracted game files (7 GB) live in the install folder too, and must stay: the game
        // reads gow2.psarc from there while it runs (no "delete after build" option for that reason)
        uint64_t need = REQUIRED_BYTES + (isPkg ? PKG_EXTRA_BYTES : 0);
        spaceOk = freeB >= need || already;
        g_fsInstall = { spaceOk ? 1 : 3, Fmt(L"%s free  \u00B7  about %s needed%s%s", FormatBytes(freeB).c_str(), FormatBytes(need).c_str(),
                                            isPkg ? (L" (" + FormatBytes(PKG_EXTRA_BYTES) + L" of it: the game files from the package, "
                                                     L"kept because the game reads them while it runs)").c_str() : L"",
                                            spaceOk ? L"" : L"  \u00B7  not enough space") };
        wstr li = ToLowerW(FullPath(inst)) + L"\\", le = ToLowerW(ExeDir()) + L"\\";
        if (spaceOk && ManagedByQuiver() && li.compare(0, le.size(), le) == 0)
            g_fsInstall = { 2, L"Inside Quiver's app folder, Quiver would ask which exe to start. Pick another folder." };
    }
    bool elfOk = elf.empty() || FileExists(elf);
    g_canBuild = gameOk && rapOk && spaceOk && elfOk;
    Layout();
    Redraw();
}

static void StartDetect() {
    g_detecting = true;
    Validate();
    std::thread([] {
        Detected* d = new Detected(DetectAll());
        PostMessageW(g_hwnd, WM_APP_DETECTED, 0, (LPARAM)d);
    }).detach();
}

static bool PickPath(bool folder, const wchar_t* title, const wchar_t* filterName, const wchar_t* filterSpec, wstr& out) {
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return false;
    DWORD opt; dlg->GetOptions(&opt);
    dlg->SetOptions(opt | FOS_FORCEFILESYSTEM | (folder ? FOS_PICKFOLDERS : FOS_FILEMUSTEXIST));
    dlg->SetTitle(title);
    if (!folder) {
        COMDLG_FILTERSPEC spec[] = { { filterName, filterSpec }, { L"All files", L"*.*" } };
        dlg->SetFileTypes(2, spec);
    }
    bool ok = false;
    if (SUCCEEDED(dlg->Show(g_hwnd))) {
        IShellItem* it = nullptr;
        if (SUCCEEDED(dlg->GetResult(&it))) {
            PWSTR p = nullptr;
            if (SUCCEEDED(it->GetDisplayName(SIGDN_FILESYSPATH, &p))) { out = p; ok = true; CoTaskMemFree(p); }
            it->Release();
        }
    }
    dlg->Release();
    return ok;
}

// ================================================================ build
static void PushLog(const wstr& l) {
    g_logTail.push_back(l);
    if (g_logTail.size() > 40) g_logTail.erase(g_logTail.begin());
}
static void StartBuild(const BuildRequest& req) {
    if (g_building) return;
    if (g_worker.joinable()) g_worker.join();
    g_lastReq = req;
    g_ip.Init(req.install);
    SaveInstallDir(g_ip.root);
    g_cancel = false; g_building = true;
    g_buildPct = 0; g_buildResult = 0; g_buildErr.clear(); g_buildStep = L"Starting\u2026"; g_logTail.clear();
    g_buildStart = GetTickCount();
    SetTimer(g_hwnd, TIMER_TICK, 1000, nullptr);
    Go(PG_BUILD);
    int testCancel = _wtoi(GetEnv(L"GOW2_TEST_CANCEL_MS").c_str());     // test hook: cancel without the prompt
    if (testCancel > 0) SetTimer(g_hwnd, TIMER_TESTCANCEL, testCancel, nullptr);
    g_worker = std::thread([req] {
        BuildEvents ev;
        ev.progress = [](int p, const wstr& s) { PostMessageW(g_hwnd, WM_APP_PROGRESS, p, (LPARAM)new wstr(s)); };
        ev.logLine = [](const wstr& s) { PostMessageW(g_hwnd, WM_APP_LOG, 0, (LPARAM)new wstr(s)); };
        wstr err;
        int code = RunBuildPipeline(req, ev, &g_cancel, err);
        PostMessageW(g_hwnd, WM_APP_DONE, code, (LPARAM)new wstr(err));
    });
}
static void OpenPath(const wstr& p) { ShellExecuteW(g_hwnd, L"open", p.c_str(), nullptr, nullptr, SW_SHOWNORMAL); }

// ================================================================ updates
static void SelfUpdate(UpdateInfo u) {
    std::thread([u] {
        wstr exe = ExePath(), fresh = exe + L".new", err;
        bool ok = DownloadVerified(Widen(u.assetUrl), fresh, u.sha256, 0, nullptr, nullptr, err);
        if (ok) {
            wstr old = exe + L".old";
            DeleteFileW(old.c_str());
            ok = MoveFileW(exe.c_str(), old.c_str()) && MoveFileW(fresh.c_str(), exe.c_str());
            if (!ok) { MoveFileW(old.c_str(), exe.c_str()); err = L"Could not replace the launcher file."; }
        }
        PostMessageW(g_hwnd, WM_APP_SELFUPDATE, ok, (LPARAM)new wstr(err));
    }).detach();
}

static void CycleSetting(int row, int d) {
    const int n = (int)(sizeof(kCaps) / sizeof(kCaps[0]));
    switch (row) {
    case 0: {
        int i;
        if (d > 0) { i = 0; while (i < n && kCaps[i] <= g_setCap) ++i; i %= n; }        // next bigger value
        else { i = n - 1; while (i >= 0 && kCaps[i] >= g_setCap) --i; if (i < 0) i = n - 1; }   // next smaller value
        g_setCap = kCaps[i];
        break;
    }
    case 1: g_setBorderless ^= 1; break;
    case 2: g_setRes = (g_setRes - 1 + d + 4) % 4 + 1; break;
    case 3: g_setOverlay ^= 1; break;
    case 4: g_setAutoplay ^= 1; break;
    }
    g_setDirty = true;
    Layout(); Redraw();
}

// ================================================================ actions
static void OnCommand(int id) {
    switch (id) {
    case ID_PLAY: case ID_BUILD_PLAY: {
        wstr err;
        if (LaunchGame(g_ip, err)) { DestroyWindow(g_hwnd); return; }
        MessageBoxW(g_hwnd, err.c_str(), L"Gow2HD-Windows", MB_ICONWARNING);
        break;
    }
    case ID_SETTINGS: Go(PG_SETTINGS); break;
    case ID_ABOUT: Go(PG_ABOUT); break;
    case ID_REBUILD: {
        KvFile st; st.Load(g_ip.state);
        SetText(g_edGame, st.GetW("game")); SetText(g_edRap, st.GetW("rap"));
        SetText(g_edElf, st.GetW("elf")); SetText(g_edInstall, g_ip.root);
        Go(PG_SETUP); Validate();
        break;
    }
    case ID_FOLDER: OpenPath(g_ip.root); break;
    case ID_REMOVE: {
        RemovePlan plan = PlanRemoveBuild(g_ip);
        if (plan.dirs.empty() && plan.files.empty()) { g_status = L"Nothing to remove."; Redraw(); break; }
        wstr msg = L"Remove the built port and its tools from\n" + g_ip.root + L"\n\nThis frees about " + FormatBytes(plan.bytes) +
                   L". You can build again at any time.\n\nKept:\n";
        for (auto& k : plan.kept) msg += L"  • " + k + L"\n";
        for (auto& s : plan.saveRoots) msg += L"  • your saves: " + s + L"\n";
        msg += L"\nRemove the build now?";
        if (MessageBoxW(g_hwnd, msg.c_str(), L"Gow2HD-Windows", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) break;
        HCURSOR old = SetCursor(LoadCursor(nullptr, IDC_WAIT));
        wstr err;
        int bad = RemoveBuild(g_ip, plan, err);
        SetCursor(old);
        if (bad) MessageBoxW(g_hwnd, (L"Some files could not be removed (is the game still running?)\n\n" + err).c_str(), L"Gow2HD-Windows", MB_ICONWARNING);
        KvFile st; st.Load(g_ip.state);
        SetText(g_edGame, st.GetW("game")); SetText(g_edRap, st.GetW("rap"));
        SetText(g_edElf, st.GetW("elf")); SetText(g_edInstall, g_ip.root);
        Go(PG_SETUP); Validate();
        break;
    }
    case ID_UPDATE:
        g_status = L"Checking GitHub for a newer launcher\u2026"; Redraw();
        std::thread([] { PostMessageW(g_hwnd, WM_APP_UPDATE, 0, (LPARAM)new UpdateInfo(CheckForUpdate())); }).detach();
        break;
    case ID_BR_GAME: { wstr p; if (PickPath(true, L"Pick the NPUA80491 game folder", nullptr, nullptr, p)) { SetText(g_edGame, p); Validate(); } break; }
    case ID_BR_PKG: {
        wstr p;
        if (PickPath(false, L"Pick the game's PSN package (UP9000-NPUA80491_00-GODOFWARIIHDUS00.pkg, any name)",
                     L"PS3 game package (*.pkg)", L"*.pkg", p)) { SetText(g_edGame, p); Validate(); }
        break;
    }
    case ID_BR_RAP: {
        wstr p;
        if (PickPath(false, L"Pick the license file " RAP_FILE_NAME, L"PS3 license (*.rap)", L"*.rap", p)) {
            SetText(g_edRap, p); Validate();
        }
        break;
    }
    case ID_BR_ELF: { wstr p; if (PickPath(false, L"Pick a decrypted EBOOT.ELF (optional)", L"ELF (*.elf)", L"*.elf", p)) { SetText(g_edElf, p); Validate(); } break; }
    case ID_BR_INSTALL: {
        wstr p;
        if (PickPath(true, L"Pick the install folder", nullptr, nullptr, p)) {
            if (!IEquals(FileNameOf(p), L"GoW2-Windows") && !FileExists(JoinPath(p, L"launcher_state.ini")) && !ListDir(p, false).empty())
                p = JoinPath(p, L"GoW2-Windows");    // never build into a folder full of the player's files
            SetText(g_edInstall, p); Validate();
        }
        break;
    }
    case ID_RESCAN: SetText(g_edGame, L""); SetText(g_edRap, L""); SetText(g_edElf, L""); StartDetect(); break;
    case ID_SETUP_BACK: g_ip.Init(LoadInstallDir()); Go(PG_HOME); break;
    case ID_BUILD: {
        Validate();
        if (!g_canBuild) break;
        BuildRequest r;
        r.game = g_gameResolved; r.rap = FullPath(GetText(g_edRap)); r.elf = GetText(g_edElf);
        if (!r.elf.empty()) r.elf = FullPath(r.elf);
        r.install = FullPath(GetText(g_edInstall));
        StartBuild(r);
        break;
    }
    case ID_SHOWLOG: OpenPath(g_ip.log); break;
    case ID_CANCEL:
        if (MessageBoxW(g_hwnd, L"Stop the build? You can start it again later; finished downloads are kept.",
                        L"Gow2HD-Windows", MB_YESNO | MB_ICONQUESTION) == IDYES) {
            g_cancel = true; CancelRunningProcess(); Layout(); Redraw();
        }
        break;
    case ID_RETRY: StartBuild(g_lastReq); break;
    case ID_BUILD_BACK:
        if (g_buildResult == 1) Go(PG_HOME);
        else { Go(PG_SETUP); Validate(); }
        break;
    case ID_SET_SAVE:
        if (!SaveSettings()) MessageBoxW(g_hwnd, (L"Could not write " + SettingsIniPath()).c_str(), L"Gow2HD-Windows", MB_ICONWARNING);
        Go(PG_HOME);
        break;
    case ID_SET_BACK: Go(PG_HOME); break;
    case ID_ABOUT_BACK: Go(PG_HOME); break;
    case ID_ABOUT_PAGE: OpenPath(L"https://github.com/" GITHUB_REPO_W); break;
    default:
        if (id >= ID_SET_PREV && id < ID_SET_PREV + 5) CycleSetting(id - ID_SET_PREV, -1);
        else if (id >= ID_SET_NEXT && id < ID_SET_NEXT + 5) CycleSetting(id - ID_SET_NEXT, +1);
        break;
    }
}

// ================================================================ window
static int HitTest(int x, int y) {
    float lx = x / g_scale, ly = y / g_scale;
    for (auto& b : g_btns)
        if (lx >= b.rc.left && lx < b.rc.right && ly >= b.rc.top && ly < b.rc.bottom) return b.enabled ? b.id : ID_NONE;
    return ID_NONE;
}
static HWND MakeEdit(HWND parent, int id) {
    HWND e = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 10, 10, parent, (HMENU)(INT_PTR)id, g_inst, nullptr);
    SendMessageW(e, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);
    return e;
}
static void ApplyDarkTitle(HWND h) {
    BOOL dark = TRUE;
    DwmSetWindowAttribute(h, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof(dark));
    COLORREF cap = C_BG_TOP, txt = C_DIM, border = RGB(0x2A, 0x23, 0x1E);
    DwmSetWindowAttribute(h, 35 /*DWMWA_CAPTION_COLOR*/, &cap, sizeof(cap));
    DwmSetWindowAttribute(h, 36 /*DWMWA_TEXT_COLOR*/, &txt, sizeof(txt));
    DwmSetWindowAttribute(h, 34 /*DWMWA_BORDER_COLOR*/, &border, sizeof(border));
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_CREATE: {
        g_hwnd = h;
        g_scale = GetDpiForWindow(h) / 96.0f;
        g_edGame = MakeEdit(h, 1); g_edRap = MakeEdit(h, 2); g_edElf = MakeEdit(h, 3); g_edInstall = MakeEdit(h, 4);
        MakeFonts();
        ApplyDarkTitle(h);
        DragAcceptFiles(h, TRUE);
        return 0;
    }
    case WM_DROPFILES: {   // setup page: a dropped .pkg or folder goes to the game field, a .rap / .elf to theirs
        HDROP drop = (HDROP)wp;
        UINT n = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        for (UINT i = 0; i < n && g_page == PG_SETUP && !g_building; ++i) {
            wchar_t buf[MAX_PATH * 4];
            if (!DragQueryFileW(drop, i, buf, MAX_PATH * 4)) continue;
            wstr p = buf;
            if (EndsWithI(p, L".rap")) SetText(g_edRap, p);
            else if (EndsWithI(p, L".elf")) SetText(g_edElf, p);
            else if (IsPkgPath(p) || DirExists(p)) SetText(g_edGame, p);
            else if (IEquals(FileNameOf(p), L"PARAM.SFO") || IEquals(FileNameOf(p), L"EBOOT.BIN")) SetText(g_edGame, ParentDir(p));
        }
        DragFinish(drop);
        if (g_page == PG_SETUP) { KillTimer(h, TIMER_VALIDATE); Validate(); }
        return 0;
    }
    case WM_DPICHANGED: {
        g_scale = HIWORD(wp) / 96.0f;
        MakeFonts();
        RECT* r = (RECT*)lp;
        SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        Layout(); Redraw();
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT cr; GetClientRect(h, &cr);
        HDC mem = CreateCompatibleDC(dc);
        HBITMAP bmp = CreateCompatibleBitmap(dc, cr.right, cr.bottom);
        HGDIOBJ old = SelectObject(mem, bmp);
        Paint(mem, cr);
        BitBlt(dc, 0, 0, cr.right, cr.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old); DeleteObject(bmp); DeleteDC(mem);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_CTLCOLOREDIT: {
        HDC dc = (HDC)wp;
        SetTextColor(dc, C_TEXT);
        SetBkColor(dc, C_FIELD);
        return (LRESULT)g_fieldBrush;
    }
    case WM_COMMAND:
        if (HIWORD(wp) == EN_CHANGE) SetTimer(h, TIMER_VALIDATE, 350, nullptr);
        return 0;
    case WM_TIMER:
        if (wp == TIMER_VALIDATE) { KillTimer(h, TIMER_VALIDATE); if (g_page == PG_SETUP) Validate(); }
        if (wp == TIMER_TICK && g_page == PG_BUILD) Redraw();
        if (wp == TIMER_TESTCANCEL) { KillTimer(h, TIMER_TESTCANCEL); g_cancel = true; CancelRunningProcess(); Layout(); Redraw(); }
        return 0;
    case WM_MOUSEMOVE: {
        int id = HitTest(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        if (id != g_hover) {
            g_hover = id;
            if (id >= ID_PLAY && id <= ID_ABOUT) g_focusMenu = id - ID_PLAY;
            SetCursor(LoadCursor(nullptr, id ? IDC_HAND : IDC_ARROW));
            TRACKMOUSEEVENT t = { sizeof(t), TME_LEAVE, h, 0 };
            TrackMouseEvent(&t);
            Redraw();
        }
        return 0;
    }
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT && g_hover) { SetCursor(LoadCursor(nullptr, IDC_HAND)); return TRUE; }
        break;
    case WM_MOUSELEAVE: g_hover = ID_NONE; g_pressed = ID_NONE; Redraw(); return 0;
    case WM_LBUTTONDOWN:
        SetFocus(h);
        g_pressed = HitTest(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        if (g_pressed) SetCapture(h);
        Redraw();
        return 0;
    case WM_LBUTTONUP: {
        int id = HitTest(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        int was = g_pressed;
        g_pressed = ID_NONE;
        ReleaseCapture();
        Redraw();
        if (id && id == was) OnCommand(id);
        return 0;
    }
    case WM_KEYDOWN:
        if (g_page == PG_HOME) {
            if (wp == VK_UP) { g_focusMenu = (g_focusMenu + HOME_ITEMS - 1) % HOME_ITEMS; g_hover = ID_NONE; Redraw(); }
            else if (wp == VK_DOWN) { g_focusMenu = (g_focusMenu + 1) % HOME_ITEMS; g_hover = ID_NONE; Redraw(); }
            else if (wp == VK_RETURN || wp == VK_SPACE) OnCommand(ID_PLAY + g_focusMenu);
        } else if (wp == VK_ESCAPE) {
            if (g_page == PG_SETTINGS || g_page == PG_ABOUT) Go(PG_HOME);
        }
        return 0;
    case WM_APP_DETECTED: {
        std::unique_ptr<Detected> d((Detected*)lp);
        g_detecting = false;
        if (GetText(g_edGame).empty()) SetText(g_edGame, d->game);
        if (GetText(g_edRap).empty()) SetText(g_edRap, d->rap);
        if (GetText(g_edElf).empty()) SetText(g_edElf, d->elf);
        KillTimer(h, TIMER_VALIDATE);
        Validate();
        if (!d->game.empty() && g_fsGame.level == 1) g_fsGame.msg += L"  \u00B7  found " + d->gameFrom;
        if (!d->rap.empty() && g_fsRap.level == 1) g_fsRap.msg += L"  \u00B7  found " + d->rapFrom;
        Redraw();
        return 0;
    }
    case WM_APP_PROGRESS: {
        std::unique_ptr<wstr> s((wstr*)lp);
        g_buildPct = (int)wp;
        if (!s->empty()) g_buildStep = *s;
        Redraw();
        return 0;
    }
    case WM_APP_LOG: { std::unique_ptr<wstr> s((wstr*)lp); PushLog(*s); Redraw(); return 0; }
    case WM_APP_DONE: {
        std::unique_ptr<wstr> s((wstr*)lp);
        KillTimer(h, TIMER_TICK);
        g_building = false;
        g_buildEnd = GetTickCount();
        if (g_worker.joinable()) g_worker.join();
        g_buildResult = wp == 0 ? 1 : 2;
        g_buildErr = *s;
        if (wp == 0) { g_buildPct = 100; g_buildStep = L"Finished in " + Clock(g_buildEnd - g_buildStart) + L". Have fun."; }
        Layout(); Redraw();
        FlashWindow(h, TRUE);
        return 0;
    }
    case WM_APP_UPDATE: {
        std::unique_ptr<UpdateInfo> u((UpdateInfo*)lp);
        if (!u->ok) { g_status = u->err; Redraw(); return 0; }
        if (u->noRelease) { g_status = L"No release published yet. This launcher (" LAUNCHER_VERSION_W L") is current."; Redraw(); return 0; }
        if (!u->newer) { g_status = L"You have the latest launcher (" + Widen(u->tag) + L")."; Redraw(); return 0; }
        g_status = L"Launcher " + Widen(u->tag) + L" is available.";
        Redraw();
        if (ManagedByQuiver() || u->assetUrl.empty()) {
            wstr msg = L"Launcher " + Widen(u->tag) + L" is available." +
                       (ManagedByQuiver() ? wstr(L"\n\nThis copy is managed by Quiver: update it there.") : wstr()) +
                       L"\n\nOpen the release page?";
            if (MessageBoxW(h, msg.c_str(), L"Gow2HD-Windows", MB_YESNO | MB_ICONINFORMATION) == IDYES) OpenPath(Widen(u->pageUrl));
        } else {
            wstr msg = L"Launcher " + Widen(u->tag) + L" is available (you have " LAUNCHER_VERSION_W L").\n\n"
                       L"Download it and restart now? Your build and settings are kept. "
                       L"A changed launcher may want to rebuild the port.";
            if (MessageBoxW(h, msg.c_str(), L"Gow2HD-Windows", MB_YESNO | MB_ICONINFORMATION) == IDYES) {
                g_status = L"Downloading launcher " + Widen(u->tag) + L"\u2026"; Redraw();
                SelfUpdate(*u);
            }
        }
        return 0;
    }
    case WM_APP_SELFUPDATE: {
        std::unique_ptr<wstr> s((wstr*)lp);
        if (!wp) { g_status = L"Update failed: " + *s; Redraw(); return 0; }
        wstr exe = ExePath();
        ShellExecuteW(nullptr, L"open", exe.c_str(), nullptr, ExeDir().c_str(), SW_SHOWNORMAL);
        DestroyWindow(h);
        return 0;
    }
    case WM_CLOSE:
        if (g_building) {
            if (MessageBoxW(h, L"A build is running. Stop it and quit?", L"Gow2HD-Windows", MB_YESNO | MB_ICONQUESTION) != IDYES) return 0;
            g_cancel = true; CancelRunningProcess();
            if (g_worker.joinable()) g_worker.join();
            g_building = false;
        } else if (g_page == PG_SETTINGS && g_setDirty) {
            int r = MessageBoxW(h, L"Save the changed settings?", L"Gow2HD-Windows", MB_YESNOCANCEL | MB_ICONQUESTION);
            if (r == IDCANCEL) return 0;
            if (r == IDYES) SaveSettings();
        }
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        if (g_worker.joinable()) { g_cancel = true; CancelRunningProcess(); g_worker.join(); }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

// ================================================================ console output for --silent-build / --detect
static HANDLE g_out = nullptr;
static bool g_outIsConsole = false;
static void ConOpen() {
    g_out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode;
    if (!g_out || g_out == INVALID_HANDLE_VALUE || GetFileType(g_out) == FILE_TYPE_UNKNOWN) {
        if (AttachConsole(ATTACH_PARENT_PROCESS)) {
            g_out = CreateFileW(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        } else g_out = nullptr;
    }
    g_outIsConsole = g_out && GetConsoleMode(g_out, &mode);
}
static void ConPrint(const wstr& s) {
    if (!g_out || g_out == INVALID_HANDLE_VALUE) return;
    wstr line = s + L"\r\n";
    DWORD n;
    if (g_outIsConsole) WriteConsoleW(g_out, line.c_str(), (DWORD)line.size(), &n, nullptr);
    else { std::string u = Narrow(line); WriteFile(g_out, u.data(), (DWORD)u.size(), &n, nullptr); }
}

static std::atomic<bool> g_conCancel{ false };
static BOOL WINAPI CtrlHandler(DWORD) { g_conCancel = true; CancelRunningProcess(); return TRUE; }

static int SilentBuild(const BuildRequest& req) {
    ConOpen();
    SetConsoleCtrlHandler(CtrlHandler, TRUE);
    if (req.game.empty() || req.rap.empty() || req.install.empty()) {
        ConPrint(L"usage: Gow2HD-Windows.exe --silent-build (--game <dir> | --pkg <file.pkg>) --rap <file> [--elf <file>] --out <dir> [--jobs N]");
        return 2;
    }
    GameCheck g = CheckGameInput(req.game);
    if (g.status != GAME_OK) { ConPrint(L"ERROR " + g.message); return 3; }
    BuildRequest r = req;
    r.game = g.dir; r.rap = FullPath(r.rap); r.install = FullPath(r.install);
    if (!r.elf.empty()) r.elf = FullPath(r.elf);
    SaveInstallDir(r.install);
    int lastPct = -1; wstr lastStep;
    BuildEvents ev;
    ev.progress = [&](int p, const wstr& s) {
        if (p == lastPct && (s == lastStep || s.find(L"Downloading") == 0)) return;
        lastPct = p; lastStep = s;
        ConPrint(Fmt(L"PROGRESS %d ", p) + s);
    };
    ev.logLine = [](const wstr& s) { ConPrint(L"  " + s); };
    wstr err;
    int code = RunBuildPipeline(r, ev, &g_conCancel, err);
    if (code) ConPrint(L"ERROR " + err);
    else ConPrint(L"OK " + FindBuiltExe([&] { InstallPaths ip; ip.Init(r.install); return ip; }(), [&] {
        KvFile k; InstallPaths ip; ip.Init(r.install); k.Load(ip.state); return k; }()));
    return code;
}

static int DetectCmd(const wstr& hint) {
    ConOpen();
    Detected d = DetectAll(hint);
    for (auto& l : d.log) ConPrint(l);
    ConPrint(L"game=" + d.game);
    ConPrint(L"rap=" + d.rap);
    ConPrint(L"elf=" + d.elf);
    if (!d.game.empty()) { GameCheck g = CheckGameInput(d.game); ConPrint(L"check=" + g.message); }
    InstallPaths ip; ip.Init(LoadInstallDir());
    ConPrint(L"install=" + ip.root + Fmt(L" built=%d", IsBuilt(ip) ? 1 : 0));
    return d.game.empty() || d.rap.empty() ? 1 : 0;
}

// ================================================================ entry
int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
    g_inst = inst;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    { wstr old = ExePath() + L".old"; DeleteFileW(old.c_str()); }   // left over from a self-update

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool play = false, setup = false, silent = false, detect = false, checkUpdate = false, removeBuild = false, yes = false;
    BuildRequest req;
    wstr page;
    for (int i = 1; i < argc; ++i) {
        wstr a = argv[i];
        auto next = [&]() -> wstr { return i + 1 < argc ? wstr(argv[++i]) : wstr(); };
        if (a == L"--play") play = true;
        else if (a == L"--setup") setup = true;
        else if (a == L"--silent-build") silent = true;
        else if (a == L"--detect") detect = true;
        else if (a == L"--check-update") checkUpdate = true;
        else if (a == L"--remove-build") removeBuild = true;
        else if (a == L"--yes") yes = true;
        else if (a == L"--game" || a == L"--pkg") req.game = next();   // a .pkg path is recognised by CheckGameInput
        else if (a == L"--rap") req.rap = next();
        else if (a == L"--elf") req.elf = next();
        else if (a == L"--out" || a == L"--install") req.install = next();
        else if (a == L"--jobs") req.jobs = _wtoi(next().c_str());
        else if (a == L"--page") page = next();                 // development: open a given page
    }
    LocalFree(argv);
    if (silent) return SilentBuild(req);
    if (detect) return DetectCmd(req.game);
    if (removeBuild) {   // console: prints the plan; deletes only with --yes
        ConOpen();
        InstallPaths ip; ip.Init(req.install.empty() ? LoadInstallDir() : FullPath(req.install));
        RemovePlan plan = PlanRemoveBuild(ip);
        for (auto& d : plan.dirs) ConPrint(L"delete dir  " + d);
        for (auto& f : plan.files) ConPrint(L"delete file " + f);
        for (auto& k : plan.kept) ConPrint(L"keep        " + k);
        for (auto& s : plan.saveRoots) ConPrint(L"saves       " + s);
        ConPrint(L"bytes=" + std::to_wstring(plan.bytes));
        if (!yes) { ConPrint(L"dry run (add --yes to delete)"); return 0; }
        wstr err;
        int bad = RemoveBuild(ip, plan, err);
        ConPrint(bad ? L"ERROR " + err : wstr(L"OK removed"));
        return bad ? 1 : 0;
    }
    if (checkUpdate) {
        ConOpen();
        UpdateInfo u = CheckForUpdate();
        if (u.ok && u.noRelease) { ConPrint(L"latest=none (no release published yet) newer=0"); return 0; }
        ConPrint(u.ok ? Fmt(L"latest=%s newer=%d asset=%s digest=%s quiver=%d", Widen(u.tag).c_str(), u.newer ? 1 : 0,
                            Widen(u.assetUrl).c_str(), Widen(u.sha256).c_str(), ManagedByQuiver() ? 1 : 0)
                      : L"update check: " + u.err);
        return u.ok ? 0 : 1;
    }

    g_ip.Init(req.install.empty() ? LoadInstallDir() : req.install);
    bool built = IsBuilt(g_ip);
    KvFile st; st.Load(g_ip.state);
    bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    if (built && !setup && !shift && (play || st.GetInt("autoplay", 0))) {
        wstr err;
        if (LaunchGame(g_ip, err)) return 0;
        MessageBoxW(nullptr, err.c_str(), L"Gow2HD-Windows", MB_ICONWARNING);
    }

    g_fieldBrush = CreateSolidBrush(C_FIELD);
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.hIconSm = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
    wc.lpszClassName = L"GoW2WindowsLauncher";
    RegisterClassExW(&wc);
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
    HWND h = CreateWindowExW(0, wc.lpszClassName, L"God of War II HD \u2014 Windows port", style,
                             CW_USEDEFAULT, CW_USEDEFAULT, 100, 100, nullptr, nullptr, inst, nullptr);
    if (!h) return 1;
    UINT dpi = GetDpiForWindow(h);
    RECT r = { 0, 0, MulDiv(W, dpi, 96), MulDiv(H, dpi, 96) };
    AdjustWindowRectExForDpi(&r, style, FALSE, 0, dpi);
    HMONITOR mon = MonitorFromWindow(h, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi = { sizeof(mi) }; GetMonitorInfoW(mon, &mi);
    int ww = r.right - r.left, wh = r.bottom - r.top;
    SetWindowPos(h, nullptr, (mi.rcWork.left + mi.rcWork.right - ww) / 2, (mi.rcWork.top + mi.rcWork.bottom - wh) / 2, ww, wh, SWP_NOZORDER);

    SetText(g_edInstall, g_ip.root);
    if (!req.game.empty()) SetText(g_edGame, req.game);
    if (!req.rap.empty()) SetText(g_edRap, req.rap);
    if (!req.elf.empty()) SetText(g_edElf, req.elf);
    Page start = built && !setup ? PG_HOME : PG_SETUP;
    if (page == L"settings" && built) start = PG_SETTINGS;
    if (page == L"about") start = PG_ABOUT;
    if (start == PG_SETUP && built) {
        SetText(g_edGame, req.game.empty() ? st.GetW("game") : req.game);
        SetText(g_edRap, req.rap.empty() ? st.GetW("rap") : req.rap);
        SetText(g_edElf, st.GetW("elf"));
    }
    Go(start);
    ShowWindow(h, show);
    UpdateWindow(h);
    if (start == PG_SETUP) {
        if (GetText(g_edGame).empty() || GetText(g_edRap).empty()) StartDetect(); else Validate();
    }
    if (page == L"build" && !req.game.empty()) { Validate(); OnCommand(ID_BUILD); }   // development: start a build at once

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_TAB && g_page == PG_SETUP) {
            HWND eds[] = { g_edGame, g_edRap, g_edElf, g_edInstall };
            HWND f = GetFocus(); int k = -1;
            for (int i = 0; i < 4; ++i) if (eds[i] == f) k = i;
            bool back = GetKeyState(VK_SHIFT) < 0;
            int n = (k < 0 ? (back ? 3 : 0) : (k + (back ? 3 : 1)) % 4);
            SetFocus(eds[n]); SendMessageW(eds[n], EM_SETSEL, 0, -1);
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
