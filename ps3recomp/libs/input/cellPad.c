/*
 * ps3recomp - cellPad HLE implementation
 *
 * Reads real gamepad input from the host and translates to PS3 pad format.
 *
 * Backend selection:
 *   - Windows default: XInput (no extra dependencies)
 *   - Everywhere else / if PS3RECOMP_PAD_USE_SDL2 is defined: SDL2 GameController
 *
 * Define PS3RECOMP_PAD_USE_SDL2 to force SDL2 backend on Windows.
 */

#include "cellPad.h"
#include "rsx_overlay.h"
#include "cellpad_overlay.h"
#include "cellpad_mapping.h"
#include <stdio.h>
#include "host_prompts.h"
#include <stdlib.h>   /* getenv (vinha transitivo via SDL.h; o backend GameController nao inclui SDL) */

/* PS3_PAD_AUTOSTART: set and not "0". It used to be presence only, so the
 * launcher's "PS3_PAD_AUTOSTART=0" (autostart unticked) turned it ON and the
 * automatic pad skipped the opening movie and drove the menu. */
static int pad_autostart_env_on(void)
{
    const char* e = getenv("PS3_PAD_AUTOSTART");
    return (e && e[0] && e[0] != '0') ? 1 : 0;
}
#include <string.h>
#include <math.h>
#include <stdatomic.h>

/* ---------------------------------------------------------------------------
 * Backend selection
 * -----------------------------------------------------------------------*/

#if defined(PS3RECOMP_PAD_USE_SDL2)
  #define PAD_BACKEND_SDL2  1
  #define PAD_BACKEND_XINPUT 0
  #define PAD_BACKEND_GAMECONTROLLER 0
#elif defined(_WIN32)
  #define PAD_BACKEND_SDL2  0
  #define PAD_BACKEND_XINPUT 1
  #define PAD_BACKEND_GAMECONTROLLER 0
#elif defined(__APPLE__)
  /* macOS: GameController nativo (DS4/DS5/Xbox + teclado GCKeyboard + mouse
   * GCMouse + haptics) -- regra Port Apple. Forcar SDL2 com
   * -DPS3RECOMP_PAD_USE_SDL2. */
  #define PAD_BACKEND_SDL2  0
  #define PAD_BACKEND_XINPUT 0
  #define PAD_BACKEND_GAMECONTROLLER 1
#else
  #define PAD_BACKEND_SDL2  1
  #define PAD_BACKEND_XINPUT 0
  #define PAD_BACKEND_GAMECONTROLLER 0
#endif

#if PAD_BACKEND_XINPUT
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>
  #include <xinput.h>
  #pragma comment(lib, "xinput.lib")
#endif

#if PAD_BACKEND_SDL2
  #include <SDL2/SDL.h>
#endif

#if PAD_BACKEND_GAMECONTROLLER
  #include "cellpad_gamecontroller.h"
#endif

/* ---------------------------------------------------------------------------
 * Internal state  (PadHostState + PAD_MAX_HOST_PORTS em cellpad_internal.h,
 * partilhado com o backend nativo GameController)
 * -----------------------------------------------------------------------*/
#include "cellpad_internal.h"

static int           s_pad_initialized = 0;
static u32           s_max_connect = 0;
static u32           s_port_setting[CELL_PAD_MAX_PORT_NUM];
static PadHostState  s_host_state[PAD_MAX_HOST_PORTS];
static atomic_int s_host_vibration = 1;
/* Map, deadzone and identity flag under one sequence lock (cellpad_mapping.h):
 * the fast path's "identity" decision and the deadzone it uses always come
 * from the same publication. Starts as the default (identity) map. */
static cellpad_mapping s_mapping = {
    .sequence = 0,
    .writer = ATOMIC_FLAG_INIT,
    .map = {
        CELL_PAD_CTRL_SELECT, CELL_PAD_CTRL_L3, CELL_PAD_CTRL_R3, CELL_PAD_CTRL_START,
        CELL_PAD_CTRL_UP, CELL_PAD_CTRL_RIGHT, CELL_PAD_CTRL_DOWN, CELL_PAD_CTRL_LEFT,
        CELL_PAD_CTRL_L2, CELL_PAD_CTRL_R2, CELL_PAD_CTRL_L1, CELL_PAD_CTRL_R1,
        CELL_PAD_CTRL_TRIANGLE, CELL_PAD_CTRL_CIRCLE, CELL_PAD_CTRL_CROSS, CELL_PAD_CTRL_SQUARE
    },
    .deadzone = CELL_PAD_HOST_DEADZONE_DEFAULT,
    .identity = 1,
};

#if PAD_BACKEND_SDL2
static SDL_GameController* s_sdl_controllers[PAD_MAX_HOST_PORTS];
static int s_sdl_inited = 0;
#endif

static int pad_host_mapping_is_bijective(const u16 map[CELL_PAD_HOST_BUTTON_COUNT])
{
    u32 seen = 0;
    unsigned bit;

    if (map == NULL)
        return 0;
    for (bit = 0; bit < CELL_PAD_HOST_BUTTON_COUNT; ++bit) {
        u32 value = map[bit];
        if (value == 0 || (value & (value - 1u)) != 0 || (seen & value) != 0)
            return 0;
        seen |= value;
    }
    return seen == 0xffffu;
}

void cellPad_set_host_mapping(
    const uint16_t guest_button_for_host_bit[CELL_PAD_HOST_BUTTON_COUNT],
    uint8_t deadzone)
{
    const uint8_t dz = deadzone > 100 ? 100 : deadzone;
    /* A non-bijective map is rejected; its deadzone still applies. */
    cellpad_mapping_publish(&s_mapping,
                            pad_host_mapping_is_bijective(guest_button_for_host_bit)
                                ? guest_button_for_host_bit : NULL,
                            dz);
}

void cellPad_set_host_vibration(int enabled)
{
    atomic_store_explicit(&s_host_vibration, enabled ? 1 : 0, memory_order_relaxed);
}

static void pad_set_mapped_pressure(CellPadData* data,
                                    const u16 map[CELL_PAD_HOST_BUTTON_COUNT],
                                    u16 host_button, u8 value)
{
    const u16 guest_button = cellPad_translate_host_buttons(
        host_button, map);
    int offset = -1;

    switch (guest_button) {
    case CELL_PAD_CTRL_RIGHT: offset = CELL_PAD_BTN_OFFSET_PRESS_RIGHT; break;
    case CELL_PAD_CTRL_LEFT: offset = CELL_PAD_BTN_OFFSET_PRESS_LEFT; break;
    case CELL_PAD_CTRL_UP: offset = CELL_PAD_BTN_OFFSET_PRESS_UP; break;
    case CELL_PAD_CTRL_DOWN: offset = CELL_PAD_BTN_OFFSET_PRESS_DOWN; break;
    case CELL_PAD_CTRL_TRIANGLE: offset = CELL_PAD_BTN_OFFSET_PRESS_TRIANGLE; break;
    case CELL_PAD_CTRL_CIRCLE: offset = CELL_PAD_BTN_OFFSET_PRESS_CIRCLE; break;
    case CELL_PAD_CTRL_CROSS: offset = CELL_PAD_BTN_OFFSET_PRESS_CROSS; break;
    case CELL_PAD_CTRL_SQUARE: offset = CELL_PAD_BTN_OFFSET_PRESS_SQUARE; break;
    case CELL_PAD_CTRL_L1: offset = CELL_PAD_BTN_OFFSET_PRESS_L1; break;
    case CELL_PAD_CTRL_R1: offset = CELL_PAD_BTN_OFFSET_PRESS_R1; break;
    case CELL_PAD_CTRL_L2: offset = CELL_PAD_BTN_OFFSET_PRESS_L2; break;
    case CELL_PAD_CTRL_R2: offset = CELL_PAD_BTN_OFFSET_PRESS_R2; break;
    default: break;
    }
    if (offset >= 0 && value > data->button[offset])
        data->button[offset] = value;
}

static void pad_set_host_pressure(CellPadData* data, u16 raw_host_buttons,
                                  u16 host_buttons,
                                  const u16 map[CELL_PAD_HOST_BUTTON_COUNT],
                                  u16 host_button, u8 value, int digital_fallback)
{
    value = cellPad_pressure_after_capture(raw_host_buttons, host_buttons,
                                           host_button, value, digital_fallback);
    pad_set_mapped_pressure(data, map, host_button, value);
}

/* ---------------------------------------------------------------------------
 * XInput backend
 * -----------------------------------------------------------------------*/

#if PAD_BACKEND_XINPUT

/* Deadzone for analog sticks (same as XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE) */
#define PAD_STICK_DEADZONE  7849
#define PAD_TRIGGER_THRESHOLD 30

static host_input_device pad_windows_raw_family_scan(void);   /* winport: mingw */
static host_input_device pad_windows_raw_family(void)
{
    static ULONGLONG last = 0; static host_input_device cached = HOST_INPUT_XBOX;
    ULONGLONG now = GetTickCount64();
    if (!last || now - last >= 2000) { cached = pad_windows_raw_family_scan(); last = now; }
    return cached;
}
static host_input_device pad_windows_raw_family_scan(void)
{
    RAWINPUTDEVICELIST devices[64];
    UINT count = (UINT)(sizeof(devices) / sizeof(devices[0]));
    if (GetRawInputDeviceList(devices, &count, sizeof(devices[0])) == (UINT)-1)
        return HOST_INPUT_XBOX;
    for (UINT i = 0; i < count; i++) {
        char name[512];
        UINT size = (UINT)sizeof(name);
        if (GetRawInputDeviceInfoA(devices[i].hDevice, RIDI_DEVICENAME,
                                   name, &size) == (UINT)-1)
            continue;
        if (strstr(name, "VID_057E") || strstr(name, "vid_057e"))
            return HOST_INPUT_NINTENDO;
        if (strstr(name, "VID_054C") || strstr(name, "vid_054c"))
            return HOST_INPUT_PLAYSTATION;
    }
    return HOST_INPUT_XBOX;
}

static u8 pad_xinput_stick_to_u8(short raw, short deadzone)
{
    float normalized;
    if (raw > deadzone)
        normalized = (float)(raw - deadzone) / (float)(32767 - deadzone);
    else if (raw < -deadzone)
        normalized = (float)(raw + deadzone) / (float)(32767 - deadzone);
    else
        normalized = 0.0f;

    /* Map -1.0..1.0 to 0..255 with center at 128 */
    int val = (int)(normalized * 127.0f) + 128;
    if (val < 0) val = 0;
    if (val > 255) val = 255;
    return (u8)val;
}

static void pad_poll_xinput(void)
{
    /* XInput best practice (MSDN, "XInputGetState"): probing an EMPTY
     * controller slot walks the USB/HID stack and is very expensive --
     * calling it every frame for absent slots stalls the caller badly (on
     * some Windows setups it blocks for many ms per empty slot). GoW2's
     * movie/menu loop polls the pad every frame via cellPadGetInfo2, so once
     * the pad subsystem was initialized this froze the main thread here.
     * Re-probe a slot known-disconnected only once every ~120 polls; poll
     * connected slots every call. PS3_PAD_NO_XINPUT disables XInput entirely
     * (headless/CI hosts where XInput can hang). */
    static int s_xi_skip[PAD_MAX_HOST_PORTS] = {0};
    static int s_no_xinput = -1;
    const host_input_device raw_family = pad_windows_raw_family();
    memset(s_host_state, 0, sizeof(s_host_state));
    if (s_no_xinput < 0) s_no_xinput = getenv("PS3_PAD_NO_XINPUT") ? 1 : 0;
    if (s_no_xinput) return;

    for (int i = 0; i < PAD_MAX_HOST_PORTS; i++) {
        XINPUT_STATE state;
        memset(&state, 0, sizeof(state));

        if (!s_host_state[i].connected && s_xi_skip[i] > 0) { s_xi_skip[i]--; continue; }

        DWORD result = XInputGetState((DWORD)i, &state);
        if (result != ERROR_SUCCESS) {
            s_host_state[i].connected = 0;
            s_xi_skip[i] = 120;   /* don't re-probe this empty slot for ~120 polls */
            continue;
        }

        s_host_state[i].connected = 1;
        s_host_state[i].device = raw_family;
        s_xi_skip[i] = 0;
        XINPUT_GAMEPAD* gp = &state.Gamepad;

        /* Map XInput buttons to PS3 CELL_PAD_CTRL_* */
        u16 btns = 0;
        if (gp->wButtons & XINPUT_GAMEPAD_BACK)           btns |= CELL_PAD_CTRL_SELECT;
        if (gp->wButtons & XINPUT_GAMEPAD_LEFT_THUMB)     btns |= CELL_PAD_CTRL_L3;
        if (gp->wButtons & XINPUT_GAMEPAD_RIGHT_THUMB)    btns |= CELL_PAD_CTRL_R3;
        if (gp->wButtons & XINPUT_GAMEPAD_START)          btns |= CELL_PAD_CTRL_START;
        if (gp->wButtons & XINPUT_GAMEPAD_DPAD_UP)        btns |= CELL_PAD_CTRL_UP;
        if (gp->wButtons & XINPUT_GAMEPAD_DPAD_RIGHT)     btns |= CELL_PAD_CTRL_RIGHT;
        if (gp->wButtons & XINPUT_GAMEPAD_DPAD_DOWN)      btns |= CELL_PAD_CTRL_DOWN;
        if (gp->wButtons & XINPUT_GAMEPAD_DPAD_LEFT)      btns |= CELL_PAD_CTRL_LEFT;
        if (gp->bLeftTrigger > PAD_TRIGGER_THRESHOLD)     btns |= CELL_PAD_CTRL_L2;
        if (gp->bRightTrigger > PAD_TRIGGER_THRESHOLD)    btns |= CELL_PAD_CTRL_R2;
        if (gp->wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER)  btns |= CELL_PAD_CTRL_L1;
        if (gp->wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) btns |= CELL_PAD_CTRL_R1;
        if (gp->wButtons & XINPUT_GAMEPAD_Y)              btns |= CELL_PAD_CTRL_TRIANGLE;
        if (gp->wButtons & XINPUT_GAMEPAD_B)              btns |= CELL_PAD_CTRL_CIRCLE;
        if (gp->wButtons & XINPUT_GAMEPAD_A)              btns |= CELL_PAD_CTRL_CROSS;
        if (gp->wButtons & XINPUT_GAMEPAD_X)              btns |= CELL_PAD_CTRL_SQUARE;

        s_host_state[i].buttons = btns;
        s_host_state[i].controller_buttons = btns;

        /* Analog sticks */
        s_host_state[i].analog_lx = pad_xinput_stick_to_u8(gp->sThumbLX, PAD_STICK_DEADZONE);
        s_host_state[i].analog_ly = (u8)(255 - pad_xinput_stick_to_u8(gp->sThumbLY, PAD_STICK_DEADZONE)); /* Y inverted */
        s_host_state[i].analog_rx = pad_xinput_stick_to_u8(gp->sThumbRX, PAD_STICK_DEADZONE);
        s_host_state[i].analog_ry = (u8)(255 - pad_xinput_stick_to_u8(gp->sThumbRY, PAD_STICK_DEADZONE));
        s_host_state[i].controller_axes = CELL_PAD_HOST_AXIS_ALL;

        /* Triggers */
        s_host_state[i].trigger_l2 = gp->bLeftTrigger;
        s_host_state[i].trigger_r2 = gp->bRightTrigger;

        /* Pressure-sensitive buttons: XInput has digital only, so 0 or 255 */
        s_host_state[i].press_up       = (btns & CELL_PAD_CTRL_UP)       ? 255 : 0;
        s_host_state[i].press_down     = (btns & CELL_PAD_CTRL_DOWN)     ? 255 : 0;
        s_host_state[i].press_left     = (btns & CELL_PAD_CTRL_LEFT)     ? 255 : 0;
        s_host_state[i].press_right    = (btns & CELL_PAD_CTRL_RIGHT)    ? 255 : 0;
        s_host_state[i].press_triangle = (btns & CELL_PAD_CTRL_TRIANGLE) ? 255 : 0;
        s_host_state[i].press_circle   = (btns & CELL_PAD_CTRL_CIRCLE)   ? 255 : 0;
        s_host_state[i].press_cross    = (btns & CELL_PAD_CTRL_CROSS)    ? 255 : 0;
        s_host_state[i].press_square   = (btns & CELL_PAD_CTRL_SQUARE)   ? 255 : 0;
        s_host_state[i].press_l1       = (btns & CELL_PAD_CTRL_L1)       ? 255 : 0;
        s_host_state[i].press_r1       = (btns & CELL_PAD_CTRL_R1)       ? 255 : 0;
        s_host_state[i].activity = host_input_state_active(
            btns, s_host_state[i].analog_lx, s_host_state[i].analog_ly,
            s_host_state[i].analog_rx, s_host_state[i].analog_ry,
            s_host_state[i].trigger_l2, s_host_state[i].trigger_r2);
        if (s_host_state[i].activity) {
            if (raw_family == HOST_INPUT_NINTENDO)
                host_prompts_note_pad("Nintendo Switch");
            else if (raw_family == HOST_INPUT_PLAYSTATION)
                host_prompts_note_pad("PlayStation");
            else
                host_prompts_note_pad("Xbox");
        }
    }
}

static int pad_poll_windows_keyboard(void)
{
    PadHostState* st = &s_host_state[0];
    int active = 0;
    u16 b = st->buttons;
    float lx = 0.0f, ly = 0.0f;
    float rx = 0.0f, ry = 0.0f;
    if (!st->connected) {
        st->analog_lx = st->analog_ly = 128;
        st->analog_rx = st->analog_ry = 128;
    }
    #define KEY(vk) ((GetAsyncKeyState(vk) & 0x8000) != 0)
    if (KEY('D')) { lx += 1.0f; active = 1; }
    if (KEY('A')) { lx -= 1.0f; active = 1; }
    if (KEY('W')) { ly -= 1.0f; active = 1; }
    if (KEY('S')) { ly += 1.0f; active = 1; }
    if (KEY(VK_RIGHT)) { rx += 1.0f; active = 1; }
    if (KEY(VK_LEFT)) { rx -= 1.0f; active = 1; }
    if (KEY(VK_DOWN)) { ry += 1.0f; active = 1; }
    if (KEY(VK_UP)) { ry -= 1.0f; active = 1; }
    if (lx || ly) {
        st->analog_lx = (u8)(128.0f + lx * 127.0f);
        st->analog_ly = (u8)(128.0f + ly * 127.0f);
        st->controller_axes &= (u8)~(CELL_PAD_HOST_AXIS_LX | CELL_PAD_HOST_AXIS_LY);
    }
    if (rx || ry) {
        st->analog_rx = (u8)(128.0f + rx * 127.0f);
        st->analog_ry = (u8)(128.0f + ry * 127.0f);
        st->controller_axes &= (u8)~(CELL_PAD_HOST_AXIS_RX | CELL_PAD_HOST_AXIS_RY);
    }
    if (KEY(VK_SPACE)) { b |= CELL_PAD_CTRL_CROSS; active = 1; }
    if (KEY('J')) { b |= CELL_PAD_CTRL_SQUARE; active = 1; }
    if (KEY('K')) { b |= CELL_PAD_CTRL_TRIANGLE; active = 1; }
    if (KEY('E')) { b |= CELL_PAD_CTRL_CIRCLE; active = 1; }
    if (KEY(VK_LSHIFT)) { b |= CELL_PAD_CTRL_L1; active = 1; }
    if (KEY('Q')) { b |= CELL_PAD_CTRL_R1; active = 1; }
    if (KEY(VK_LCONTROL)) { b |= CELL_PAD_CTRL_L2; st->trigger_l2 = 255; active = 1; }
    if (KEY('R')) { b |= CELL_PAD_CTRL_R2; st->trigger_r2 = 255; active = 1; }
    if (KEY(VK_RETURN)) { b |= CELL_PAD_CTRL_START; active = 1; }
    if (KEY(VK_TAB)) { b |= CELL_PAD_CTRL_SELECT; active = 1; }
    if (KEY('F')) { b |= CELL_PAD_CTRL_L3; active = 1; }
    if (KEY('G')) { b |= CELL_PAD_CTRL_R3; active = 1; }
    #undef KEY
    if (!active) {
        static int kb = -1;   /* winport: mingw */
        if (kb < 0) { const char* e = getenv("PS3_PAD_KEYBOARD"); kb = !(e && *e == '0'); }
        if (kb && !st->connected) {
            st->connected = 1; st->device = HOST_INPUT_KEYBOARD; st->buttons = 0;
            st->analog_lx = st->analog_ly = st->analog_rx = st->analog_ry = 128;
        }
        return 0;
    }

    st->connected = 1;
    st->device = HOST_INPUT_KEYBOARD;
    st->buttons = b;
    st->press_cross = (b & CELL_PAD_CTRL_CROSS) ? 255 : 0;
    st->press_circle = (b & CELL_PAD_CTRL_CIRCLE) ? 255 : 0;
    st->press_square = (b & CELL_PAD_CTRL_SQUARE) ? 255 : 0;
    st->press_triangle = (b & CELL_PAD_CTRL_TRIANGLE) ? 255 : 0;
    st->press_l1 = (b & CELL_PAD_CTRL_L1) ? 255 : 0;
    st->press_r1 = (b & CELL_PAD_CTRL_R1) ? 255 : 0;
    st->activity = 1;
    host_prompts_note_keyboard();
    return 1;
}

static void pad_init_backend(void)
{
    /* XInput needs no explicit init */
}

static void pad_shutdown_backend(void)
{
    /* XInput needs no explicit shutdown */
}

#endif /* PAD_BACKEND_XINPUT */

/* ---------------------------------------------------------------------------
 * SDL2 backend
 * -----------------------------------------------------------------------*/

#if PAD_BACKEND_SDL2

static u8 pad_sdl_axis_to_u8(int raw)
{
    /* SDL axis: -32768..32767 -> 0..255 with center at 128 */
    int val = ((raw + 32768) * 255) / 65535;
    if (val < 0) val = 0;
    if (val > 255) val = 255;
    return (u8)val;
}

static u8 pad_sdl_trigger_to_u8(int raw)
{
    /* SDL trigger: 0..32767 -> 0..255 */
    int val = (raw * 255) / 32767;
    if (val < 0) val = 0;
    if (val > 255) val = 255;
    return (u8)val;
}

static void pad_poll_sdl2(void)
{
    memset(s_host_state, 0, sizeof(s_host_state));
    SDL_GameControllerUpdate();

    for (int i = 0; i < PAD_MAX_HOST_PORTS; i++) {
        if (!s_sdl_controllers[i]) {
            /* Try to open newly connected controllers */
            if (SDL_IsGameController(i)) {
                s_sdl_controllers[i] = SDL_GameControllerOpen(i);
            }
        }

        SDL_GameController* gc = s_sdl_controllers[i];
        if (!gc || !SDL_GameControllerGetAttached(gc)) {
            s_host_state[i].connected = 0;
            if (gc) {
                SDL_GameControllerClose(gc);
                s_sdl_controllers[i] = NULL;
            }
            continue;
        }

        s_host_state[i].connected = 1;

        const char* controller_name = SDL_GameControllerName(gc);
        s_host_state[i].device = host_input_classify(controller_name);
        if (s_host_state[i].device == HOST_INPUT_UNKNOWN)
            s_host_state[i].device = HOST_INPUT_XBOX;

        u16 btns = 0;
        if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_BACK))          btns |= CELL_PAD_CTRL_SELECT;
        if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_LEFTSTICK))     btns |= CELL_PAD_CTRL_L3;
        if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_RIGHTSTICK))    btns |= CELL_PAD_CTRL_R3;
        if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_START))         btns |= CELL_PAD_CTRL_START;
        if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_UP))       btns |= CELL_PAD_CTRL_UP;
        if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_RIGHT))    btns |= CELL_PAD_CTRL_RIGHT;
        if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_DOWN))     btns |= CELL_PAD_CTRL_DOWN;
        if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_LEFT))     btns |= CELL_PAD_CTRL_LEFT;
        if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_LEFTSHOULDER))  btns |= CELL_PAD_CTRL_L1;
        if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) btns |= CELL_PAD_CTRL_R1;
        if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_Y))             btns |= CELL_PAD_CTRL_TRIANGLE;
        if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_B))             btns |= CELL_PAD_CTRL_CIRCLE;
        if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_A))             btns |= CELL_PAD_CTRL_CROSS;
        if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_X))             btns |= CELL_PAD_CTRL_SQUARE;

        /* Triggers via axis */
        int lt = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERLEFT);
        int rt = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
        if (lt > 3000) btns |= CELL_PAD_CTRL_L2;
        if (rt > 3000) btns |= CELL_PAD_CTRL_R2;

        s_host_state[i].buttons = btns;
        s_host_state[i].controller_buttons = btns;

        /* Analog sticks */
        s_host_state[i].analog_lx = pad_sdl_axis_to_u8(SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTX));
        s_host_state[i].analog_ly = pad_sdl_axis_to_u8(SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTY));
        s_host_state[i].analog_rx = pad_sdl_axis_to_u8(SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_RIGHTX));
        s_host_state[i].analog_ry = pad_sdl_axis_to_u8(SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_RIGHTY));
        s_host_state[i].controller_axes = CELL_PAD_HOST_AXIS_ALL;

        /* Triggers */
        s_host_state[i].trigger_l2 = pad_sdl_trigger_to_u8(lt);
        s_host_state[i].trigger_r2 = pad_sdl_trigger_to_u8(rt);

        /* Pressure: SDL has digital buttons, so 0 or 255 */
        s_host_state[i].press_up       = (btns & CELL_PAD_CTRL_UP)       ? 255 : 0;
        s_host_state[i].press_down     = (btns & CELL_PAD_CTRL_DOWN)     ? 255 : 0;
        s_host_state[i].press_left     = (btns & CELL_PAD_CTRL_LEFT)     ? 255 : 0;
        s_host_state[i].press_right    = (btns & CELL_PAD_CTRL_RIGHT)    ? 255 : 0;
        s_host_state[i].press_triangle = (btns & CELL_PAD_CTRL_TRIANGLE) ? 255 : 0;
        s_host_state[i].press_circle   = (btns & CELL_PAD_CTRL_CIRCLE)   ? 255 : 0;
        s_host_state[i].press_cross    = (btns & CELL_PAD_CTRL_CROSS)    ? 255 : 0;
        s_host_state[i].press_square   = (btns & CELL_PAD_CTRL_SQUARE)   ? 255 : 0;
        s_host_state[i].press_l1       = (btns & CELL_PAD_CTRL_L1)       ? 255 : 0;
        s_host_state[i].press_r1       = (btns & CELL_PAD_CTRL_R1)       ? 255 : 0;
        s_host_state[i].activity = host_input_state_active(
            btns, s_host_state[i].analog_lx, s_host_state[i].analog_ly,
            s_host_state[i].analog_rx, s_host_state[i].analog_ry,
            s_host_state[i].trigger_l2, s_host_state[i].trigger_r2);
        if (s_host_state[i].activity) host_prompts_note_pad(controller_name);
    }
}

static int pad_poll_sdl_keyboard(void)
{
    const Uint8* key = SDL_GetKeyboardState(NULL);
    if (!key || PAD_MAX_HOST_PORTS <= 0) return 0;

    PadHostState* st = &s_host_state[0];
    int active = 0;
    u16 b = st->buttons;
    float lx = 0.0f, ly = 0.0f;
    float rx = 0.0f, ry = 0.0f;
    if (!st->connected) {
        st->analog_lx = st->analog_ly = 128;
        st->analog_rx = st->analog_ry = 128;
    }
    if (key[SDL_SCANCODE_D]) { lx += 1.0f; active = 1; }
    if (key[SDL_SCANCODE_A]) { lx -= 1.0f; active = 1; }
    if (key[SDL_SCANCODE_W]) { ly -= 1.0f; active = 1; }
    if (key[SDL_SCANCODE_S]) { ly += 1.0f; active = 1; }
    if (key[SDL_SCANCODE_RIGHT]) { rx += 1.0f; active = 1; }
    if (key[SDL_SCANCODE_LEFT]) { rx -= 1.0f; active = 1; }
    if (key[SDL_SCANCODE_DOWN]) { ry += 1.0f; active = 1; }
    if (key[SDL_SCANCODE_UP]) { ry -= 1.0f; active = 1; }
    if (lx || ly) {
        st->analog_lx = (u8)(128.0f + lx * 127.0f);
        st->analog_ly = (u8)(128.0f + ly * 127.0f);
        st->controller_axes &= (u8)~(CELL_PAD_HOST_AXIS_LX | CELL_PAD_HOST_AXIS_LY);
    }
    if (rx || ry) {
        st->analog_rx = (u8)(128.0f + rx * 127.0f);
        st->analog_ry = (u8)(128.0f + ry * 127.0f);
        st->controller_axes &= (u8)~(CELL_PAD_HOST_AXIS_RX | CELL_PAD_HOST_AXIS_RY);
    }

    if (key[SDL_SCANCODE_SPACE]) { b |= CELL_PAD_CTRL_CROSS; active = 1; }
    if (key[SDL_SCANCODE_J])     { b |= CELL_PAD_CTRL_SQUARE; active = 1; }
    if (key[SDL_SCANCODE_K])     { b |= CELL_PAD_CTRL_TRIANGLE; active = 1; }
    if (key[SDL_SCANCODE_E])     { b |= CELL_PAD_CTRL_CIRCLE; active = 1; }
    if (key[SDL_SCANCODE_LSHIFT]) { b |= CELL_PAD_CTRL_L1; active = 1; }
    if (key[SDL_SCANCODE_Q])     { b |= CELL_PAD_CTRL_R1; active = 1; }
    if (key[SDL_SCANCODE_LCTRL]) { b |= CELL_PAD_CTRL_L2; st->trigger_l2 = 255; active = 1; }
    if (key[SDL_SCANCODE_R])     { b |= CELL_PAD_CTRL_R2; st->trigger_r2 = 255; active = 1; }
    if (key[SDL_SCANCODE_RETURN]) { b |= CELL_PAD_CTRL_START; active = 1; }
    if (key[SDL_SCANCODE_TAB])   { b |= CELL_PAD_CTRL_SELECT; active = 1; }
    if (key[SDL_SCANCODE_F])     { b |= CELL_PAD_CTRL_L3; active = 1; }
    if (key[SDL_SCANCODE_G])     { b |= CELL_PAD_CTRL_R3; active = 1; }

    if (!active) return 0;
    st->connected = 1;
    st->device = HOST_INPUT_KEYBOARD;
    st->buttons = b;
    st->press_cross = (b & CELL_PAD_CTRL_CROSS) ? 255 : 0;
    st->press_circle = (b & CELL_PAD_CTRL_CIRCLE) ? 255 : 0;
    st->press_square = (b & CELL_PAD_CTRL_SQUARE) ? 255 : 0;
    st->press_triangle = (b & CELL_PAD_CTRL_TRIANGLE) ? 255 : 0;
    st->press_l1 = (b & CELL_PAD_CTRL_L1) ? 255 : 0;
    st->press_r1 = (b & CELL_PAD_CTRL_R1) ? 255 : 0;
    st->activity = 1;
    host_prompts_note_keyboard();
    return 1;
}

static void pad_init_backend(void)
{
    if (!s_sdl_inited) {
        if (SDL_WasInit(SDL_INIT_GAMECONTROLLER) == 0) {
            SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER);
        }
        s_sdl_inited = 1;
    }
    memset(s_sdl_controllers, 0, sizeof(s_sdl_controllers));

    /* Open any controllers already connected */
    int num = SDL_NumJoysticks();
    for (int i = 0; i < num && i < PAD_MAX_HOST_PORTS; i++) {
        if (SDL_IsGameController(i)) {
            s_sdl_controllers[i] = SDL_GameControllerOpen(i);
        }
    }
}

static void pad_shutdown_backend(void)
{
    for (int i = 0; i < PAD_MAX_HOST_PORTS; i++) {
        if (s_sdl_controllers[i]) {
            SDL_GameControllerClose(s_sdl_controllers[i]);
            s_sdl_controllers[i] = NULL;
        }
    }
}

#endif /* PAD_BACKEND_SDL2 */

/* ---------------------------------------------------------------------------
 * GameController backend (macOS nativo) -- toda a logica ObjC vive em
 * cellpad_gamecontroller.m; aqui so os hooks.
 * -----------------------------------------------------------------------*/
#if PAD_BACKEND_GAMECONTROLLER

static void pad_poll_gamecontroller(void)
{
    cpgc_poll(s_host_state, PAD_MAX_HOST_PORTS);
}

static void pad_init_backend(void)
{
    cpgc_init();
    printf("[cellPad] GameController nativo (controllers + teclado GCKeyboard + mouse GCMouse)\n");
}

static void pad_shutdown_backend(void)
{
    cpgc_shutdown();
}

#endif /* PAD_BACKEND_GAMECONTROLLER */

/* ---------------------------------------------------------------------------
 * Poll dispatcher
 * -----------------------------------------------------------------------*/

/* winport v53-focus: is a window of this process in the foreground? (see engine_fixes_v53_focus_input.py) */
static int wp_focus_ok(void)
{
    static int on = -1;
    if (on < 0) { const char* e = getenv("WINPORT_FOCUS_INPUT"); on = (e && e[0] == '1') ? 1 : 0; }
    if (!on) return 1;
    HWND fg = GetForegroundWindow(); DWORD pid = 0;
    if (fg) GetWindowThreadProcessId(fg, &pid);
    return pid == GetCurrentProcessId();
}
static void wp_focus_neutralise(void)
{
    static int was = 1;
    const int ok = wp_focus_ok();
    if (ok != was) { fprintf(stderr, "[FOCUS53] game window %s: input %s\n", ok ? "focused" : "not focused",
                             ok ? "live" : "ignored"); fflush(stderr); was = ok; }
    if (ok) return;
    for (int i = 0; i < PAD_MAX_HOST_PORTS; i++) {
        PadHostState* h = &s_host_state[i];
        h->buttons = 0; h->controller_buttons = 0; h->host_home = 0; h->controller_axes = 0;
        h->analog_lx = h->analog_ly = h->analog_rx = h->analog_ry = 128;
        h->trigger_l2 = h->trigger_r2 = 0;
        h->press_right = h->press_left = h->press_up = h->press_down = 0;
        h->press_triangle = h->press_circle = h->press_cross = h->press_square = 0;
        h->press_l1 = h->press_r1 = 0;
    }
}
/* ---- winport v81-pset-anywhere (engine_fixes_v81_port_settings_anywhere.py): prototypes ---- */
volatile LONG wp81_pad_tick = 0;   /* GetTickCount of the last real pad poll (renderer fallback for boot screens) */
static int wp81_anywhere(void);
static int wp81_movie(void);
static void wp81_sel_tap(void);
static unsigned long long wp81_test_at(void);
#if PAD_BACKEND_XINPUT
/* ---- winport v80-port-settings (engine_fixes_v80_port_settings.py): pad side of the Port Settings panel ---- */
static unsigned wp52_be32(unsigned ea);   /* defined below (v52); guest big-endian word */
volatile LONG wp80_pset_open = 0, wp80_pset_hint = 0, wp80_pset_nav = 0;   /* read by the D3D12 renderer */
extern volatile LONG wp92_sub, wp92_close_req;   /* winport v92a-ckpt-ingame (defined below) */
#define WP80_NAV_UP 1
#define WP80_NAV_DOWN 2
#define WP80_NAV_LEFT 4
#define WP80_NAV_RIGHT 8
#define WP80_NAV_OK 16
static int wp80_on(void)
{
    static int on = -1;
    if (on < 0) {
        const char* e = getenv("WINPORT_PORT_SETTINGS"); on = (e && e[0] == '1') ? 1 : 0;
        if (on) { fprintf(stderr, "[PSET80] pad side on: in the pause menu (game state 12) L3 / F1 opens the Port Settings panel\n"); fflush(stderr); }
    }
    return on;
}
static int wp80_fg(void)   /* a window of THIS process is in the foreground */
{
    HWND f = GetForegroundWindow(); DWORD pid = 0;
    if (f) GetWindowThreadProcessId(f, &pid);
    return f && pid == GetCurrentProcessId();
}
static void wp80_neutralise(void)
{
    for (int i = 0; i < PAD_MAX_HOST_PORTS; i++) {
        PadHostState* h = &s_host_state[i];
        h->buttons = 0; h->controller_buttons = 0; h->host_home = 0; h->controller_axes = 0;
        h->analog_lx = h->analog_ly = h->analog_rx = h->analog_ry = 128;
        h->trigger_l2 = h->trigger_r2 = 0;
        h->press_right = h->press_left = h->press_up = h->press_down = 0;
        h->press_triangle = h->press_circle = h->press_cross = h->press_square = 0;
        h->press_l1 = h->press_r1 = 0;
    }
}
static void wp80_pad_poll(void)   /* once per real poll (~4 ms), BEFORE wp_focus_neutralise; raw host state */
{
    if (!wp80_on()) return;
    InterlockedExchange(&wp81_pad_tick, (LONG)GetTickCount());   /* winport v81-pset-anywhere */
    static u32 prev_pad; static unsigned prev_keys;
    static int hold; static DWORD hold_t0;
    enum { K_F1 = 1, K_UP = 2, K_DOWN = 4, K_LEFT = 8, K_RIGHT = 16, K_ENTER = 32, K_ESC = 64, K_DEL = 128 };   /* winport v92a-ckpt-ingame: K_DEL */
    const unsigned st = wp52_be32(0x0057EB18u);   /* WP52_G_STATE: G (0x0057E960) + 0x1B8; 12 = the game's pause menu */
    const int inmenu = (st == 12u);
    const int open = (int)wp80_pset_open;
    u32 pad = 0;
    for (int i = 0; i < PAD_MAX_HOST_PORTS; i++) if (s_host_state[i].connected) pad |= s_host_state[i].buttons;
    unsigned keys = 0;
    if (wp80_fg()) {
        #define K80(vk, bit) if (GetAsyncKeyState(vk) & 0x8000) keys |= (bit)
        K80(VK_F1, K_F1); K80(VK_UP, K_UP); K80(VK_DOWN, K_DOWN); K80(VK_LEFT, K_LEFT); K80(VK_RIGHT, K_RIGHT);
        K80(VK_RETURN, K_ENTER); K80(VK_ESCAPE, K_ESC); K80(VK_DELETE, K_DEL);   /* winport v92a-ckpt-ingame */
        #undef K80
    }
    {   /* winport v80b-pset-test: WINPORT_PSET80_TEST synthetic keys */
        static const char* t80 = (const char*)1; static DWORD t12, tl; static int pos;
        if (t80 == (const char*)1) t80 = getenv("WINPORT_PSET80_TEST");
        if (t80 && t80[pos]) {
            const DWORD now = GetTickCount();
            static DWORD t0_81; if (!t0_81) t0_81 = now;   /* winport v81-pset-anywhere: WINPORT_PSET81_AT */
            if (wp81_test_at()) { if (!t12 && (unsigned long long)(now - t0_81) >= wp81_test_at()) t12 = now - 3001u; }
            else if (!inmenu) t12 = 0; else if (!t12) t12 = now;
            if (t12 && now - t12 > 3000u && now - tl > 1500u) {
                const char* a = t80 + pos; int n = 0; while (a[n] && a[n] != ',') n++;
                unsigned b = 0;
                if (n == 2 && !strncmp(a, "F1", 2)) b = K_F1; else if (n == 1 && a[0] == 'U') b = K_UP;
                else if (n == 1 && a[0] == 'D') b = K_DOWN; else if (n == 1 && a[0] == 'L') b = K_LEFT;
                else if (n == 1 && a[0] == 'R') b = K_RIGHT; else if (n == 2 && !strncmp(a, "EN", 2)) b = K_ENTER;
                else if (n == 3 && !strncmp(a, "ESC", 3)) b = K_ESC;
                else if (n == 2 && !strncmp(a, "SQ", 2)) b = K_DEL; else if (n == 2 && !strncmp(a, "BK", 2)) b = K_ESC;   /* winport v92a-ckpt-ingame */
                fprintf(stderr, "[PSET80T] synthetic key '%.*s'\n", n, a); fflush(stderr);
                keys |= b; tl = now; pos += n; if (t80[pos] == ',') pos++;
            }
        }
    }
    const int padok = wp_focus_ok();
    const u32 pe = padok ? (pad & ~prev_pad) : 0;
    const unsigned ke = keys & ~prev_keys;
    prev_pad = pad; prev_keys = keys;
    if (!open) {
        InterlockedExchange(&wp80_pset_hint, inmenu ? 1 : 0);
        const int any81 = wp81_anywhere(), ingame81 = (st == 11u);   /* winport v81-pset-anywhere */
        if (any81 ? (((pe & CELL_PAD_CTRL_L3) && !ingame81) || (ke & K_F1))
                  : (inmenu && ((pe & CELL_PAD_CTRL_L3) || (ke & K_F1)))) {
            InterlockedExchange(&wp80_pset_nav, 0);
            InterlockedExchange(&wp80_pset_open, 1);
            InterlockedExchange(&wp80_pset_hint, 0);
            const int tap81 = any81 && ingame81 && !wp81_movie();
            if (tap81) wp81_sel_tap();
            fprintf(stderr, "[PSET80] panel OPEN (%s, game state %u%s)\n", (ke & K_F1) ? "F1" : "L3", st,
                    tap81 ? ", gameplay: SELECT tap -> the game pauses behind the panel" : (any81 && ingame81 ? ", movie: no pause tap" : "")); fflush(stderr);
            wp80_neutralise();
            return;
        }
        if (hold) {   /* panel just closed: keep the game blind until the closing buttons are released */
            const u32 pm = CELL_PAD_CTRL_UP | CELL_PAD_CTRL_DOWN | CELL_PAD_CTRL_LEFT | CELL_PAD_CTRL_RIGHT | CELL_PAD_CTRL_CROSS |
                           CELL_PAD_CTRL_CIRCLE | CELL_PAD_CTRL_L3 | CELL_PAD_CTRL_START | CELL_PAD_CTRL_SELECT |
                           CELL_PAD_CTRL_TRIANGLE | CELL_PAD_CTRL_SQUARE;
            if (!(pad & pm) && !keys) hold = 0;
            else if (GetTickCount() - hold_t0 > 1000u) hold = 0;
            else wp80_neutralise();
        }
        return;
    }
    /* panel open */
    InterlockedExchange(&wp80_pset_hint, 0);
    int close = 0; const char* why = "";
    if (!inmenu && !wp81_anywhere()) { close = 1; why = "game left the pause menu"; }   /* winport v81-pset-anywhere: v80 rule only when off */
    else if ((pe & CELL_PAD_CTRL_L3) || (ke & K_F1)) { close = 1; why = "L3/F1"; }
    else if (wp92_close_req) { InterlockedExchange(&wp92_close_req, 0); close = 1; why = "checkpoint load"; }   /* winport v92a-ckpt-ingame */
    else if (wp92_sub && ((pe & (CELL_PAD_CTRL_CIRCLE | CELL_PAD_CTRL_TRIANGLE)) || (ke & K_ESC))) InterlockedOr(&wp80_pset_nav, 64);   /* winport v92a-ckpt-ingame: back */
    else if ((pe & (CELL_PAD_CTRL_CIRCLE | CELL_PAD_CTRL_TRIANGLE)) || (ke & K_ESC)) { close = 1; why = "O/Tri/Esc"; }   /* winport v82a-triangle-back */
    else {
        LONG nav = 0;
        if ((pe & CELL_PAD_CTRL_UP) || (ke & K_UP)) nav |= WP80_NAV_UP;
        if ((pe & CELL_PAD_CTRL_DOWN) || (ke & K_DOWN)) nav |= WP80_NAV_DOWN;
        if ((pe & CELL_PAD_CTRL_LEFT) || (ke & K_LEFT)) nav |= WP80_NAV_LEFT;
        if ((pe & CELL_PAD_CTRL_RIGHT) || (ke & K_RIGHT)) nav |= WP80_NAV_RIGHT;
        if ((pe & CELL_PAD_CTRL_CROSS) || (ke & K_ENTER)) nav |= WP80_NAV_OK;
        if ((pe & CELL_PAD_CTRL_SQUARE) || (ke & K_DEL)) nav |= 32;   /* winport v92a-ckpt-ingame: delete */
        if (nav) InterlockedOr(&wp80_pset_nav, nav);
    }
    if (close) {
        InterlockedExchange(&wp80_pset_open, 0);
        hold = 1; hold_t0 = GetTickCount();
        fprintf(stderr, "[PSET80] panel CLOSE (%s)\n", why); fflush(stderr);
    }
    wp80_neutralise();
}
/* ---- end winport v80-port-settings ---- */
#else
static void wp80_pad_poll(void) {}
#endif
static void pad_poll_backend(void)
{
#if PAD_BACKEND_XINPUT
    { static LARGE_INTEGER f, last; LARGE_INTEGER now;   /* winport: mingw */
      if (!f.QuadPart) QueryPerformanceFrequency(&f);
      QueryPerformanceCounter(&now);
      if (last.QuadPart && (now.QuadPart - last.QuadPart) * 1000 < f.QuadPart * 4) goto winport_pad_cached;
      last = now; }
    pad_poll_xinput();
    pad_poll_windows_keyboard();
    wp80_pad_poll();   /* winport v80-port-settings */
    wp_focus_neutralise();   /* winport v53-focus */
winport_pad_cached: ;
#elif PAD_BACKEND_SDL2
    pad_poll_sdl2();
    pad_poll_sdl_keyboard();
#elif PAD_BACKEND_GAMECONTROLLER
    pad_poll_gamecontroller();
#endif
    host_prompts_sim_tick(pad_autostart_elapsed_ms());
}

/* ---------------------------------------------------------------------------
 * API implementations
 * -----------------------------------------------------------------------*/

s32 cellPadInit(u32 max_connect)
{
    printf("[cellPad] Init(max_connect=%u)\n", max_connect);

    /* GoW2 calls cellPadInit(0).  On the retail library this is accepted as
     * the default maximum rather than leaving the pad service unopened.  A
     * strict rejection here makes every later GetData fail, which also makes
     * PS3_PAD_AUTOSTART unreachable in headless bring-up. */
    if (max_connect == 0)
        max_connect = CELL_PAD_MAX_PORT_NUM;
    if (max_connect > CELL_PAD_MAX_PORT_NUM)
        return CELL_PAD_ERROR_INVALID_PARAMETER;

    /* The title's movie loop re-enters its pad-init wrapper while the service
     * is already alive.  Treat that as idempotent: returning ALREADY_OPENED
     * is propagated as a fatal input error by this wrapper and prevents its
     * virtual/autostart controller from ever being consumed. */
    if (s_pad_initialized)
        return CELL_OK;

    s_pad_initialized = 1;
    s_max_connect = max_connect;
    memset(s_port_setting, 0, sizeof(s_port_setting));
    memset(s_host_state, 0, sizeof(s_host_state));

    pad_init_backend();

    /* Do an initial poll to detect connected controllers */
    pad_poll_backend();

    return CELL_OK;
}

s32 cellPadEnd(void)
{
    printf("[cellPad] End()\n");

    if (!s_pad_initialized)
        return CELL_PAD_ERROR_NOT_OPENED;

    pad_shutdown_backend();

    s_pad_initialized = 0;
    s_max_connect = 0;
    return CELL_OK;
}

void cellPad_poll(void)
{
    if (s_pad_initialized) {
        pad_poll_backend();
    }
}

/* The generic HLE adapter hands us raw GUEST addresses, and the guest reads
 * the struct big-endian -- so fill a host-local copy and store it BE via
 * vm_write*. (Dereferencing the raw pointer both faults and, on paths where
 * it happens to land in the vm, writes the wrong endianness.) */
extern void vm_write16(unsigned long long a, unsigned short v);
extern void vm_write32(unsigned long long a, unsigned int v);
extern unsigned char vm_read8(unsigned long long a);

/*
 * PS3_TRACE_PAD_IN=1 (diagnostic, OFF by default): what the GAME actually
 * reads. One line per changed report (buttons and len) plus a count of reads
 * per second, to tell "the guest never polls here" from "it polls and ignores
 * the button" -- e.g. while a cutscene plays.
 */
#if defined(__APPLE__)
extern int movie_vt_request_skip(void) __attribute__((weak));
#endif

/* ---- winport v52-movie-pausemenu: forward declarations (bodies after the v51 block) ---- */
#if defined(_WIN32)
static struct { volatile int on, seen; volatile unsigned long long t0, t_rel; int mode; volatile int fake; } s52n = { 0, 0, 0, 0, -1, 0 };   /* fake: test-only scene */
static int wp52_native_core(u16 btns, u16 rise, unsigned long long now);   /* 1 = the game's own menu path owns this poll */
#endif
/* ---- winport v51-rt-cutscene: forward declarations (bodies after the v49-native-pause block) ---- */
#if defined(_WIN32)
static int wp51_rt_scene_ui(void);
static u16 wp51_rt_core(u16 btns, u16 rise, unsigned long long now);
static u16 wp51_native_key(void);
#endif
/* ---- winport v49-native-pause: forward declarations (bodies after the v49 block) ---- */
#if defined(_WIN32)
static struct { volatile int on_pause; volatile unsigned long long t_inj; } s49n;
static int  wp49_native(void);
static void wp49_native_toggle(unsigned long long now);
static void wp49_native_end(unsigned long long now, int inject);
#endif
/* ---- winport v47-cutscene-ui (engine_fixes_v47_cutscene_ui.py, research/F1_cutscene_ui.md) ----------------------
 * WINPORT_CUTSCENE_UI=1: while a movie plays, SELECT pauses / resumes it and START shows "Hold START to skip"
 * (bottom right, drawn by the D3D12 renderer from wp47_cutscene_ui_snapshot); holding START for
 * WINPORT_CUTSCENE_HOLD_MS skips. The game does not see START / SELECT (nor X unless WINPORT_CUTSCENE_MASK_X=0)
 * while the movie plays; keys still held when it ends stay hidden from the game until released (no pause menu
 * popping up right after a skip). Keyboard: Enter = START, Tab = SELECT (Alt+Tab is ignored).
 * If the game stops polling the pad (e.g. frozen by the audio-clock pause) a host thread reads the buttons itself. */
#if defined(_WIN32)
#include <windows.h>
extern int  movie_wp47_overlay_running(void);
extern void movie_wp47_set_paused(int on);
extern int  movie_wp47_request_skip(void);
extern int  wp47_vdec_ingame_active(void);
extern volatile long g_wp47_audio_hold;
typedef struct { unsigned int st620; int eos_armed; int done; } wp47_ov_view;   /* = movie_eos_overlay_view */
extern int movie_eos_overlay_state(wp47_ov_view* out);                         /* gow2-recomp/movie_eos_arm.c */

static struct {
    int on, hold_ms, prompt_ms, mask_x, audio;
    volatile int active, paused, ov, watch_note;
    volatile unsigned long long t_prompt, t_show, t_hold, t_rel, t_skip, t_note, t_call;
    volatile float rel_p;
    u16 prev, sticky;
} s47 = { -1 };
static SRWLOCK s47_lock = SRWLOCK_INIT;

static unsigned long long wp47_ms(void)
{
    static LARGE_INTEGER f;
    LARGE_INTEGER c;
    if (!f.QuadPart) QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (unsigned long long)(c.QuadPart / (f.QuadPart / 1000 ? f.QuadPart / 1000 : 1));
}

static int wp47_on(void)
{
    if (s47.on < 0) {
        const char* e = getenv("WINPORT_CUTSCENE_UI");
        int v;
        s47.on = (e && e[0] == '1') ? 1 : 0;
        e = getenv("WINPORT_CUTSCENE_HOLD_MS");   v = e ? atoi(e) : 0;  s47.hold_ms   = (v >= 200 && v <= 10000) ? v : 1200;
        e = getenv("WINPORT_CUTSCENE_PROMPT_MS"); v = e ? atoi(e) : 0;  s47.prompt_ms = (v >= 500 && v <= 60000) ? v : 3000;
        e = getenv("WINPORT_CUTSCENE_MASK_X");    s47.mask_x = !(e && e[0] == '0');
        e = getenv("WINPORT_CUTSCENE_PAUSE_AUDIO"); v = e ? atoi(e) : 1; s47.audio = (v >= 0 && v <= 2) ? v : 1;
        fprintf(stderr, "[CSUI] cutscene UI %s (hold=%d ms prompt=%d ms mask_x=%d pause_audio=%d)\n",
                s47.on ? "ON" : "off (WINPORT_CUTSCENE_UI not 1)", s47.hold_ms, s47.prompt_ms, s47.mask_x, s47.audio);
        fflush(stderr);
    }
    return s47.on;
}

/* A movie the game decodes itself counts only in Play (st620 11): states 3..10 open / prebuffer it while the
 * in-engine scene before it (sometimes with player control) still runs. */
static int wp47_guest_movie_playing(void)
{
    wp47_ov_view v;
    if (!wp47_vdec_ingame_active()) return 0;
    if (!movie_eos_overlay_state(&v)) return 0;
    return v.st620 == 11u;
}

static void wp47_set_pause(int p, const char* why)
{
    if (p == s47.paused) return;
    s47.paused = p;
    movie_wp47_set_paused(p && s47.ov);
    g_wp47_audio_hold = p ? s47.audio : 0;
    fprintf(stderr, "[CSUI] %s (%s; overlay movie=%d, audio hold=%d)\n", p ? "PAUSED" : "resumed", why, s47.ov,
            p ? s47.audio : 0);
    fflush(stderr);
}

/* prompt opacity 0..1 at time `now` */
static float wp47_prompt_vis(unsigned long long now)
{
    float a = 0.0f;
    if (s47.t_hold) a = 1.0f;
    else if (s47.t_skip) { const float d = (float)(now - s47.t_skip); a = d < 500.0f ? 1.0f : 1.0f - (d - 500.0f) / 300.0f; }
    else if (s47.t_prompt) {
        const float d = (float)(now - s47.t_prompt), end = (float)s47.prompt_ms;
        a = d < end - 400.0f ? 1.0f : (end - d) / 400.0f;
    }
    if (a <= 0.0f) return 0.0f;
    if (s47.t_show) { const float fi = (float)(now - s47.t_show) / 150.0f; if (fi < a) a = fi; }   /* fade in */
    return a > 1.0f ? 1.0f : (a < 0.0f ? 0.0f : a);
}

/* The state machine (s47_lock held). Returns the buttons the game must not see. */
static u16 wp47_core(u16 btns, int synthetic)
{
    const u16 own = (u16)(CELL_PAD_CTRL_START | CELL_PAD_CTRL_SELECT | (s47.mask_x ? CELL_PAD_CTRL_CROSS : 0));
    const unsigned long long now = wp47_ms();
    const int ov = movie_wp47_overlay_running();
    const int gm47 = wp47_guest_movie_playing();
    const int rt51 = (!ov && !gm47) ? wp51_rt_scene_ui() : 0;   /* winport v51-rt-cutscene: real-time scene (detector) */
    const int active = ov || gm47 || rt51 || s52n.fake;   /* winport v52-movie-pausemenu: fake = WINPORT_F3_TEST fakeon/fakeoff (agent test of the gameplay-state path) */
    u16 rise = (u16)(btns & ~s47.prev);
    s47.prev = btns;
    if (!active) {
        if (s47.active) {
            s47.sticky = (u16)(btns & own);
            wp47_set_pause(0, "scene ended");
            s47.active = 0; s47.ov = 0; s49n.on_pause = 0; s52n.on = 0;   /* winport v49-native-pause + winport v52-movie-pausemenu */
            s47.t_prompt = s47.t_show = s47.t_hold = s47.t_rel = s47.t_skip = s47.t_note = 0;
            fprintf(stderr, "[CSUI] scene ended (keys kept from the game until released: 0x%04X)\n", (unsigned)s47.sticky);
            fflush(stderr);
        }
        s47.sticky &= btns;
        return s47.sticky;
    }
    if (!s47.active) {
        s47.active = 1; s47.sticky = 0; s47.ov = ov;
        rise = 0;   /* a key already down when the scene starts counts only after it is pressed again */
        fprintf(stderr, "[CSUI] scene active (%s)\n", ov ? "host overlay movie" : gm47 ? "in-game movie, player in Play"
                                                      : "real-time cutscene (v51 detector)");   /* winport v51-rt-cutscene */
        fflush(stderr);
    }
    s47.ov = ov;
    if (synthetic) return 0;   /* autoplay presses: no UI, the game gets them as before */
    if (rt51) return wp51_rt_core(btns, rise, now);   /* winport v51-rt-cutscene: START = skip (fast-forward), SELECT to the game */
    if ((rise & CELL_PAD_CTRL_SELECT) && (GetAsyncKeyState(VK_MENU) & 0x8000))
        rise = (u16)(rise & ~CELL_PAD_CTRL_SELECT);   /* Alt+Tab is not a pause */
    if (wp52_native_core(btns, rise, now)) return 0;   /* winport v52-movie-pausemenu: the game's own pause menu -> every key to the game */
    if (s49n.on_pause && !wp49_native() && !s52n.on) s49n.on_pause = 0;   /* winport v52-movie-pausemenu */
    if ((rise & CELL_PAD_CTRL_SELECT) && wp49_native()) {   /* winport v49-native-pause */
        wp49_native_toggle(now);
        rise = (u16)(rise & ~CELL_PAD_CTRL_SELECT);
    }
    if (s49n.on_pause) {   /* winport v49-native-pause: the game's menu is up -- everything but SELECT goes to the game */
        if (rise & CELL_PAD_CTRL_START) wp49_native_end(now, 0);
        return CELL_PAD_CTRL_SELECT;
    }
    if (rise & CELL_PAD_CTRL_SELECT) {
        if (s47.paused) wp47_set_pause(0, "SELECT");
        else if (ov || s47.audio) wp47_set_pause(1, "SELECT");
        else { s47.t_note = now; fprintf(stderr, "[CSUI] SELECT: pause not available for this movie\n"); fflush(stderr); }
    }
    if (s47.t_skip && now - s47.t_skip > 3000) {   /* the skip did not end the scene: allow another try */
        fprintf(stderr, "[CSUI] skip requested 3 s ago but the movie is still playing -- skip did NOT work here\n");
        fflush(stderr);
        s47.t_skip = 0;
    }
    if ((rise & CELL_PAD_CTRL_START) && !s47.t_skip) {
        if (wp47_prompt_vis(now) <= 0.0f) s47.t_show = now;
        s47.t_prompt = now; s47.t_hold = now; s47.t_rel = 0;
        fprintf(stderr, "[CSUI] START down: prompt shown, hold %d ms to skip\n", s47.hold_ms);
        fflush(stderr);
    }
    if (s47.t_hold && (btns & CELL_PAD_CTRL_START)) {
        if (now - s47.t_hold >= (unsigned long long)s47.hold_ms) {
            int r;
            wp47_set_pause(0, "skip");
            r = movie_wp47_request_skip();
            s47.t_skip = now; s47.t_hold = 0; s47.t_rel = 0; s47.t_prompt = now;
            fprintf(stderr, "[CSUI] START held %d ms -> SKIP (%s)\n", s47.hold_ms,
                    r == 1 ? "overlay movie ends as a normal EOS" : "game-decoded movie: EOS signal raised");
            fflush(stderr);
        }
    } else if (s47.t_hold) {
        float p = (float)(now - s47.t_hold) / (float)s47.hold_ms;
        s47.rel_p = p > 1.0f ? 1.0f : p;
        s47.t_rel = now; s47.t_hold = 0; s47.t_prompt = now;
        fprintf(stderr, "[CSUI] START released early (%.0f%% of the hold) -> no skip\n", 100.0f * s47.rel_p);
        fflush(stderr);
    }
    return own;
}

/* Host watch: when the game has not polled the pad for 250 ms during a scene, read the controller / keyboard here. */
static DWORD WINAPI wp47_watch(LPVOID unused)
{
    (void)unused;
    for (;;) {
        Sleep(15);
        if (!s47.active) continue;
        if (wp47_ms() - s47.t_call < 250) { s47.watch_note = 0; continue; }
        u16 b = 0;
#if PAD_BACKEND_XINPUT
        { static int noxi = -1; static unsigned long long next_probe[4];
          if (noxi < 0) noxi = getenv("PS3_PAD_NO_XINPUT") ? 1 : 0;
          if (!noxi) for (DWORD i = 0; i < 4; i++) {
              XINPUT_STATE st;
              const unsigned long long now = wp47_ms();
              if (next_probe[i] && now < next_probe[i]) continue;
              memset(&st, 0, sizeof st);
              if (XInputGetState(i, &st) != ERROR_SUCCESS) { next_probe[i] = now + 2000; continue; }
              next_probe[i] = 0;
              if (st.Gamepad.wButtons & XINPUT_GAMEPAD_START) b |= CELL_PAD_CTRL_START;
              if (st.Gamepad.wButtons & XINPUT_GAMEPAD_BACK)  b |= CELL_PAD_CTRL_SELECT;
              if (st.Gamepad.wButtons & XINPUT_GAMEPAD_A)     b |= CELL_PAD_CTRL_CROSS;
          } }
#endif
        { static int kb = -1;
          if (kb < 0) { const char* e = getenv("PS3_PAD_KEYBOARD"); kb = !(e && *e == '0'); }
          if (kb) {
              if (GetAsyncKeyState(VK_RETURN) & 0x8000) b |= CELL_PAD_CTRL_START;
              if (GetAsyncKeyState(VK_TAB) & 0x8000)    b |= CELL_PAD_CTRL_SELECT;
          } }
        AcquireSRWLockExclusive(&s47_lock);
        if (!s47.watch_note) {
            s47.watch_note = 1;
            fprintf(stderr, "[CSUI] the game is not polling the pad during the scene (paused=%d) -> host watch reads the buttons\n",
                    s47.paused);
            fflush(stderr);
        }
        (void)wp47_core(b, 0);
        ReleaseSRWLockExclusive(&s47_lock);
    }
    return 0;
}

/* cellPadGetData (port 0): run the UI, return the buttons to hide from the game. */
static u16 wp47_cutscene_ui(u16 btns, int synthetic)
{
    static int started = 0;
    u16 m;
    if (!wp47_on()) return 0;
    if (!started) {
        HANDLE h = CreateThread(NULL, 0, wp47_watch, NULL, 0, NULL);
        started = 1;
        if (h) CloseHandle(h);
    }
    AcquireSRWLockExclusive(&s47_lock);
    s47.t_call = wp47_ms();
    m = wp47_core(btns, synthetic);
    ReleaseSRWLockExclusive(&s47_lock);
    return m;
}

/* Renderer (fix_v47_cutscene_ui_renderer.py): what to draw now. Returns 1 if anything is visible. */
int wp47_cutscene_ui_snapshot(float* prompt_a, float* progress, int* paused, float* note_a)
{
    float a, p = 0.0f, na = 0.0f;
    unsigned long long now;
    *prompt_a = 0.0f; *progress = 0.0f; *paused = 0; *note_a = 0.0f;
    if (s47.on != 1 || !s47.active) return 0;
    AcquireSRWLockShared(&s47_lock);
    now = wp47_ms();
    a = wp47_prompt_vis(now);
    if (s47.t_hold) { p = (float)(now - s47.t_hold) / (float)s47.hold_ms; if (p > 1.0f) p = 1.0f; }
    else if (s47.t_skip) p = 1.0f;
    else if (s47.t_rel) { p = s47.rel_p - (float)(now - s47.t_rel) / 300.0f; if (p < 0.0f) p = 0.0f; }   /* drains */
    if (s47.t_note && now - s47.t_note < 2000) { const float d = (float)(now - s47.t_note); na = d < 1600.0f ? 1.0f : (2000.0f - d) / 400.0f; }
    *prompt_a = a; *progress = p; *paused = s47.paused; *note_a = na;
    ReleaseSRWLockShared(&s47_lock);
    return (a > 0.0f || *paused || na > 0.0f) ? 1 : 0;
}
#else
static int wp47_on(void) { return 0; }
static u16 wp47_cutscene_ui(u16 btns, int synthetic) { (void)btns; (void)synthetic; return 0; }
#endif
/* ---- end winport v47-cutscene-ui ---- */
/* ---- winport v49-cutscene (engine_fixes_v49_cutscene.py): test injection + real-time cutscene probe ---------- */
#if defined(_WIN32)
static int s49_scene_ms = -2;            /* -2 = env not read, -1 = off */
static unsigned s49_at[16]; static int s49_nat;
static int wp49_test_on(void)
{
    if (s49_scene_ms == -2) {
        const char* e = getenv("WINPORT_CSUI_TEST_SCENE_START_MS");
        s49_scene_ms = (e && *e) ? atoi(e) : -1;
        e = getenv("WINPORT_CSUI_TEST_START_AT_MS");
        while (e && *e && s49_nat < 16) {
            s49_at[s49_nat++] = (unsigned)strtoul(e, (char**)&e, 10);
            while (*e == ',' || *e == ' ') e++;
        }
        if (s49_scene_ms >= 0 || s49_nat)
            fprintf(stderr, "[CSUI49] TEST injection ON: START %d ms into each scene, %d timed taps\n", s49_scene_ms, s49_nat);
    }
    return s49_scene_ms >= 0 || s49_nat > 0;
}
/* buttons to inject into the game now (port 0) */
static u16 wp49_test_buttons(void)
{
    static unsigned long long t0, scene_t0;
    static int last_on;
    unsigned long long now = wp47_ms(), rel;
    u16 b = 0;
    if (!t0) t0 = now;
    rel = now - t0;
    if (s47.active && !scene_t0) scene_t0 = now;
    if (!s47.active) scene_t0 = 0;
    if (s49_scene_ms >= 0 && scene_t0 && now - scene_t0 >= (unsigned long long)s49_scene_ms
        && now - scene_t0 < (unsigned long long)s49_scene_ms + 200ull) b |= CELL_PAD_CTRL_START;
    for (int i = 0; i < s49_nat; i++)
        if (rel >= s49_at[i] && rel < (unsigned long long)s49_at[i] + 200ull) b |= CELL_PAD_CTRL_START;
    if ((b != 0) != last_on) {
        fprintf(stderr, "[CSUI49] TEST: START %s for the game (t=%llu ms, scene=%d)\n", b ? "DOWN" : "up", rel, s47.active);
        fflush(stderr);
        last_on = (b != 0);
    }
    return b;
}

/* ---- real-time cutscene probe: guest RAM snapshots ---- */
extern unsigned char* vm_base;
static void wp49_probe_dump(int scene, const char* why)
{
    static int n = 0;
    char path[256];
    FILE* f;
    unsigned long long ea = 0, written = 0;
    if (!vm_base) return;
    CreateDirectoryA("logs", NULL);
    snprintf(path, sizeof path, "logs/csprobe_%02d_%s.bin", n, scene ? "scene" : "game");
    f = fopen(path, "wb");
    if (!f) return;
    /* format: repeated { u32 ea, u32 len, bytes } for each committed host region of EA 0..0x10000000 */
    while (ea < 0x10000000ull) {
        MEMORY_BASIC_INFORMATION mbi;
        unsigned long long len;
        if (!VirtualQuery(vm_base + ea, &mbi, sizeof mbi)) break;
        len = (unsigned long long)((unsigned char*)mbi.BaseAddress + mbi.RegionSize - (vm_base + ea));
        if (ea + len > 0x10000000ull) len = 0x10000000ull - ea;
        if (mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) {
            unsigned hdr[2] = { (unsigned)ea, (unsigned)len };
            fwrite(hdr, sizeof hdr, 1, f);
            fwrite(vm_base + ea, 1, (size_t)len, f);
            written += len;
        }
        ea += len;
    }
    fclose(f);
    fprintf(stderr, "[CSPROBE] #%d %s snapshot (%s): %llu MB -> %s\n", n, scene ? "CUTSCENE" : "GAMEPLAY", why,
            written >> 20, path);
    fflush(stderr);
    n++;
}
static void wp49_probe_poll(void)
{
    static int on = -1, nat = 0, done[32];
    static unsigned at[32]; static char cls[32];
    static unsigned long long t0;
    static SHORT p10, p11;
    unsigned long long now;
    if (on < 0) {
        const char* e = getenv("WINPORT_CS_PROBE");
        on = (e && e[0] == '1');
        e = getenv("WINPORT_CS_PROBE_AT_MS");
        while (e && *e && nat < 32) {
            at[nat] = (unsigned)strtoul(e, (char**)&e, 10);
            cls[nat] = (*e == ':') ? e[1] : 'g';
            while (*e && *e != ',') e++;
            while (*e == ',') e++;
            nat++;
        }
        if (nat) on = 1;
        if (on) { fprintf(stderr, "[CSPROBE] ON: F10 = cutscene snapshot, F11 = gameplay snapshot, %d timed\n", nat); fflush(stderr); }
    }
    if (!on) return;
    now = wp47_ms();
    if (!t0) t0 = now;
    {   SHORT k10 = GetAsyncKeyState(VK_F10), k11 = GetAsyncKeyState(VK_F11);
        if ((k10 & 0x8000) && !(p10 & 0x8000)) { wp49_probe_dump(1, "F10"); MessageBeep(MB_OK); }
        if ((k11 & 0x8000) && !(p11 & 0x8000)) { wp49_probe_dump(0, "F11"); MessageBeep(MB_ICONASTERISK); }
        p10 = k10; p11 = k11; }
    for (int i = 0; i < nat; i++)
        if (!done[i] && now - t0 >= at[i]) { done[i] = 1; wp49_probe_dump(cls[i] == 's', "timed"); }
}
#else
static int wp49_test_on(void) { return 0; }
static u16 wp49_test_buttons(void) { return 0; }
static void wp49_probe_poll(void) {}
#endif
/* ---- end winport v49-cutscene ---- */
/* ---- winport v49-native-pause (engine_fixes_v49_native_pause.py): SELECT = the game's own pause menu ---- */
#if defined(_WIN32)
static int wp49_native(void)
{
    static int on = -1;
    if (on < 0) {
        const char* e = getenv("WINPORT_CUTSCENE_NATIVE_PAUSE");
        on = (e && e[0] == '1') ? 1 : 0;
        fprintf(stderr, "[CSUI49] SELECT in a movie = %s\n", on ? "the GAME's pause menu (START tap injected)" : "host pause (v47)");
        fflush(stderr);
    }
    return on;
}
static void wp49_native_end(unsigned long long now, int inject)
{
    s49n.on_pause = 0;
    s47.paused = 0;
    movie_wp47_set_paused(0);
    g_wp47_audio_hold = 0;
    if (inject) s49n.t_inj = now;
    fprintf(stderr, "[CSUI49] native pause END (%s) -> movie resumes\n", inject ? "SELECT: START tap injected to close the game's menu" : "player's START passed to the game");
    fflush(stderr);
}
static void wp49_native_toggle(unsigned long long now)
{
    if (s49n.on_pause) { wp49_native_end(now, 1); return; }
    s49n.on_pause = 1;
    s47.paused = 1;
    movie_wp47_set_paused(s47.ov);
    g_wp47_audio_hold = 0;          /* the game keeps running: its pause menu has to draw */
    s49n.t_inj = now;
    s47.t_hold = 0; s47.t_rel = 0;
    fprintf(stderr, "[CSUI49] native pause: overlay movie=%d frozen, START tap injected -> the game's pause menu should open\n", s47.ov);
    fflush(stderr);
}
/* 1 while the host waits in the game's pause menu (renderer: dim + composite the game's frame) */
int wp49_cutscene_native_paused(void) { return (s47.on == 1 && s47.active && s49n.on_pause) ? 1 : 0; }
/* START bits to inject into the game now (150 ms tap after a toggle) */
static u16 wp49_native_inject(void)
{
    if (!s49n.t_inj) return 0;
    if (wp47_ms() - s49n.t_inj < 150ull) return wp51_native_key();   /* winport v51-rt-cutscene: SELECT (default) or START */
    s49n.t_inj = 0;
    return 0;
}
#else
int wp49_cutscene_native_paused(void) { return 0; }
static u16 wp49_native_inject(void) { return 0; }
#endif
/* ---- end winport v49-native-pause ---- */
/* ---- winport v81-pset-anywhere: SELECT tap into the game + helpers ---- */
#if defined(_WIN32)
static volatile unsigned long long s81_sel_t;
static int wp81_anywhere(void)
{
    static int on = -1;
    if (on < 0) {
        const char* e = getenv("WINPORT_PSET81_ANYWHERE"); on = (e && e[0] == '0') ? 0 : 1;
        fprintf(stderr, "[PSET81] Port Settings %s\n", on ? "anywhere: F1 in every game state, L3 outside gameplay (state 11); "
                "opening in gameplay pauses the game (SELECT tap)" : "only in the pause menu (v80 rule, WINPORT_PSET81_ANYWHERE=0)");
        fflush(stderr);
    }
    return on;
}
static int wp81_movie(void) { return s47.active ? 1 : 0; }
static void wp81_sel_tap(void) { s81_sel_t = GetTickCount64(); }
static unsigned long long wp81_test_at(void)
{
    static long long v = -1;
    if (v < 0) { const char* e = getenv("WINPORT_PSET81_AT"); v = (e && atoi(e) > 0) ? atoi(e) : 0; }
    return (unsigned long long)v;
}
/* SELECT bits to inject into the game now (150 ms tap after the panel opened in gameplay) */
static u16 wp94_inject(void);   /* winport v94b-ckpt-music (defined below) */
static u16 wp81_inject(void)
{
    const unsigned long long t = s81_sel_t;
    { const u16 b94 = wp94_inject(); if (b94) return b94; }   /* winport v94b-ckpt-music: SELECT tap closes the pause menu */
    if (!t) return 0;
    if (GetTickCount64() - t < 150ull) return CELL_PAD_CTRL_SELECT;
    s81_sel_t = 0;
    return 0;
}
#else
static int wp81_anywhere(void) { return 0; }
static int wp81_movie(void) { return 0; }
static void wp81_sel_tap(void) {}
static unsigned long long wp81_test_at(void) { return 0; }
static u16 wp81_inject(void) { return 0; }
#endif
/* ---- end winport v81-pset-anywhere ---- */
/* ---- winport v51-rt-cutscene (engine_fixes_v51_realtime_cutscene.py, research/F2_realtime_cutscenes.md) ---------
 * Real-time (in-engine) cutscenes: a RAM detector (WINPORT_RTCS_DETECT), a diagnostic mode that only logs it
 * (WINPORT_RT_CUTSCENE=2), the full mode (=1: START hold -> host fast-forward until the detector clears, SELECT to the
 * game), and a learn mode that finds the detector (WINPORT_RTCS_LEARN=1, F7/F8/F6). */
#if defined(_WIN32)
extern unsigned char* vm_base;
extern volatile long g_wp51_ff_k;             /* sys_timer.c */
extern volatile long long g_wp51_tb_extra;    /* sys_timer.c */
extern void wp51_timewarp(long k);            /* sys_timer.c */

typedef struct { int deref, size, ne, have; unsigned addr, off, mask, value, last; } wp51_term;
static struct {
    int mode;                      /* -1 = env not read, 0 off, 1 full, 2 diag (log only) */
    int nterm, on_ms, off_ms;
    wp51_term term[4];
    int raw, active, ff_on;
    long ff_k, ff_time_k;
    unsigned long long t_edge, t_on, t_ff, ff_max_ms, n_scene, t_raw_sec;
    unsigned raw_lines, raw_sec_lines;
    u16 prev;
} s51 = { -1 };

static int wp51_env_int(const char* name, int def, int lo, int hi)
{
    const char* e = getenv(name);
    int v = (e && *e) ? atoi(e) : def;
    return (v < lo || v > hi) ? def : v;
}

/* guest read (big-endian). The whole 256 MB guest window is one committed region once the game runs (csprobe
 * snapshots of 2026-10-07: one region 0x0..0x10000000); VirtualQuery still guards every read. */
static int wp51_rd(unsigned ea, int size, unsigned* out)
{
    MEMORY_BASIC_INFORMATION mbi;
    const unsigned char* b;
    if (!vm_base || ea > 0x10000000u - 4u) return 0;
    b = vm_base + ea;
    if (!VirtualQuery(b, &mbi, sizeof mbi) || mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
        return 0;
    if ((const unsigned char*)mbi.BaseAddress + mbi.RegionSize < b + 4) return 0;
    *out = size == 1 ? b[0] : size == 2 ? ((unsigned)b[0] << 8 | b[1])
         : ((unsigned)b[0] << 24 | (unsigned)b[1] << 16 | (unsigned)b[2] << 8 | (unsigned)b[3]);
    return 1;
}

static int wp51_parse_term(const char* p, const char** endp, wp51_term* t)
{
    char* e;
    memset(t, 0, sizeof *t);
    t->size = 4; t->mask = 0xFFFFFFFFu;
    while (*p == ' ') p++;
    if (*p == '*') { t->deref = 1; p++; }
    t->addr = (unsigned)strtoul(p, &e, 0); if (e == p) return 0; p = e;
    if (*p == '+') { t->off = (unsigned)strtoul(p + 1, &e, 0); p = e; }
    if (*p == '/') { t->size = (int)strtoul(p + 1, &e, 0); p = e; }
    if (t->size != 1 && t->size != 2) t->size = 4;
    if (t->size < 4 && t->mask == 0xFFFFFFFFu) t->mask = t->size == 1 ? 0xFFu : 0xFFFFu;
    if (*p == '&') { t->mask = (unsigned)strtoul(p + 1, &e, 0); p = e; }
    if (p[0] == '!' && p[1] == '=') { t->ne = 1; p += 2; }
    else if (p[0] == '=') p += (p[1] == '=') ? 2 : 1;
    else return 0;
    t->value = (unsigned)strtoul(p, &e, 0); if (e == p) return 0;
    *endp = e;
    return 1;
}

static int wp51_cfg(void)
{
    const char* e;
    if (s51.mode >= 0) return s51.mode;
    e = getenv("WINPORT_RT_CUTSCENE");
    s51.mode = !(e && *e) ? 0 : e[0] == '1' ? 1 : (e[0] == '2' || e[0] == 'd' || e[0] == 'D') ? 2 : 0;
    s51.on_ms = wp51_env_int("WINPORT_RTCS_ON_MS", 200, 0, 10000);
    s51.off_ms = wp51_env_int("WINPORT_RTCS_OFF_MS", 500, 0, 10000);
    s51.ff_k = wp51_env_int("WINPORT_RTCS_FF_SPEED", 4, 2, 16);
    s51.ff_time_k = wp51_env_int("WINPORT_RTCS_FF_TIME", (int)s51.ff_k, 1, 16);
    s51.ff_max_ms = 1000ull * (unsigned long long)wp51_env_int("WINPORT_RTCS_FF_MAX_S", 120, 5, 3600);
    e = getenv("WINPORT_RTCS_DETECT");
    while (e && *e && s51.nterm < 4) {
        const char* end = e;
        if (!wp51_parse_term(e, &end, &s51.term[s51.nterm])) {
            fprintf(stderr, "[RTCS] WINPORT_RTCS_DETECT: cannot parse '%s' (want [*]ADDR[+OFF][/SIZE][&MASK]=VALUE or !=)\n", e);
            s51.nterm = 0;
            break;
        }
        s51.nterm++;
        e = end;
        while (*e == ';' || *e == ' ' || *e == ',') e++;
    }
    if (s51.mode) {
        fprintf(stderr, "[RTCS] real-time cutscene mode %s, detector %d term(s), on/off debounce %d/%d ms, skip = fast-forward x%ld "
                "(guest time x%ld, max %llu s)\n", s51.mode == 1 ? "FULL" : "DIAG (log only)", s51.nterm, s51.on_ms, s51.off_ms,
                s51.ff_k, s51.ff_time_k, s51.ff_max_ms / 1000ull);
        for (int i = 0; i < s51.nterm; i++) {
            const wp51_term* t = &s51.term[i];
            fprintf(stderr, "[RTCS]   term %d: %s0x%08X+0x%X /%d &0x%08X %s 0x%08X\n", i, t->deref ? "*" : "", t->addr, t->off,
                    t->size, t->mask, t->ne ? "!=" : "==", t->value);
        }
        if (!s51.nterm)
            fprintf(stderr, "[RTCS] no detector (WINPORT_RTCS_DETECT unset or invalid): real-time cutscene features stay idle. "
                    "Find one with WINPORT_RTCS_LEARN=1 (research/F2_realtime_cutscenes.md)\n");
        if (s51.mode == 1 && !wp47_on())
            fprintf(stderr, "[RTCS] WINPORT_RT_CUTSCENE=1 needs WINPORT_CUTSCENE_UI=1 for the prompt / skip (only logging now)\n");
        fflush(stderr);
    }
    return s51.mode;
}

/* all terms true? diag: log every raw change (<= 20 lines/s, <= 4000 lines) */
static int wp51_eval(int log_raw, unsigned long long now)
{
    int all = 1;
    if (now - s51.t_raw_sec >= 1000ull) { s51.t_raw_sec = now; s51.raw_sec_lines = 0; }
    for (int i = 0; i < s51.nterm; i++) {
        wp51_term* t = &s51.term[i];
        unsigned ea = t->addr + t->off, v = 0;
        if (t->deref) {
            unsigned ptr = 0;
            if (!wp51_rd(t->addr, 4, &ptr) || ptr == 0 || ptr >= 0x10000000u) { all = 0; continue; }
            ea = ptr + t->off;
        }
        if (!wp51_rd(ea, t->size, &v)) { all = 0; continue; }
        if (log_raw && (!t->have || v != t->last) && s51.raw_lines < 4000u && s51.raw_sec_lines < 20u) {
            fprintf(stderr, "[RTCS] term %d raw 0x%08X -> 0x%08X (ea 0x%08X)%s\n", i, t->last, v, ea, t->have ? "" : " first read");
            fflush(stderr);
            s51.raw_lines++; s51.raw_sec_lines++;
        }
        t->last = v; t->have = 1;
        { const int m = ((v & t->mask) == (t->value & t->mask));
          if (t->ne ? m : !m) all = 0; }
    }
    return s51.nterm > 0 && all;
}

static void wp51_ff_end(unsigned long long now, const char* why)
{
    if (!s51.ff_on) return;
    s51.ff_on = 0;
    g_wp51_ff_k = 0;
    wp51_timewarp(1);
    { LARGE_INTEGER f; QueryPerformanceFrequency(&f);
      fprintf(stderr, "[RTCS] fast-forward END (%s) after %.1f s real; guest clock lead now %.1f s\n", why,
              (double)(now - s51.t_ff) / 1000.0, (double)g_wp51_tb_extra / (double)(f.QuadPart ? f.QuadPart : 1)); }
    fflush(stderr);
}

static void wp51_ff_begin(unsigned long long now)
{
    if (s51.ff_on) return;
    s51.ff_on = 1;
    s51.t_ff = now;
    wp51_timewarp(s51.ff_time_k);
    g_wp51_ff_k = s51.ff_k;
    fprintf(stderr, "[RTCS] SKIP: fast-forward x%ld (guest time x%ld, audio muted, no flip pacing) until the detector clears\n",
            s51.ff_k, s51.ff_time_k);
    fflush(stderr);
}

/* renderer (fix_v51_ff_present_renderer.py): 1 while skipping -> Present without vsync */
int wp51_ff_active(void) { return g_wp51_ff_k > 1 ? 1 : 0; }

/* v47 core: is a real-time scene with the host UI active (full mode only)? */
static int wp51_rt_scene_ui(void) { return (s51.mode == 1 && s51.active) ? 1 : 0; }

/* v47 core, real-time scene branch (s47_lock held). Returns the buttons the game must not see. */
static u16 wp51_rt_core(u16 btns, u16 rise, unsigned long long now)
{
    if (s51.ff_on) {   /* skipping: START / SELECT stay with the host; SELECT aborts the skip */
        if (rise & CELL_PAD_CTRL_SELECT) wp51_ff_end(now, "SELECT pressed");
        return (u16)(CELL_PAD_CTRL_START | CELL_PAD_CTRL_SELECT);
    }
    if (rise & CELL_PAD_CTRL_SELECT) {
        fprintf(stderr, "[RTCS] SELECT in a real-time scene -> passed to the game (its own pause menu)\n");
        fflush(stderr);
    }
    if (s47.t_skip && now - s47.t_skip > 3000ull) s47.t_skip = 0;   /* an aborted skip can be retried */
    if ((rise & CELL_PAD_CTRL_START) && !s47.t_skip) {
        if (wp47_prompt_vis(now) <= 0.0f) s47.t_show = now;
        s47.t_prompt = now; s47.t_hold = now; s47.t_rel = 0;
        fprintf(stderr, "[RTCS] START down in a real-time scene: prompt shown, hold %d ms to skip\n", s47.hold_ms);
        fflush(stderr);
    }
    if (s47.t_hold && (btns & CELL_PAD_CTRL_START)) {
        if (now - s47.t_hold >= (unsigned long long)s47.hold_ms) {
            s47.t_skip = now; s47.t_hold = 0; s47.t_rel = 0; s47.t_prompt = now;
            fprintf(stderr, "[RTCS] START held %d ms -> SKIP (real-time scene)\n", s47.hold_ms);
            wp51_ff_begin(now);
        }
    } else if (s47.t_hold) {
        float p = (float)(now - s47.t_hold) / (float)s47.hold_ms;
        s47.rel_p = p > 1.0f ? 1.0f : p;
        s47.t_rel = now; s47.t_hold = 0; s47.t_prompt = now;
        fprintf(stderr, "[RTCS] START released early (%.0f%% of the hold) -> no skip\n", 100.0f * s47.rel_p);
        fflush(stderr);
    }
    return CELL_PAD_CTRL_START;   /* START is the skip key here; SELECT, X and the rest reach the game */
}

/* v49 native movie pause: which button is tapped into the game (Seth: SELECT = the game's normal pause menu) */
static u16 wp51_native_key(void)
{
    static int k = -1;
    if (k < 0) {
        const char* e = getenv("WINPORT_CUTSCENE_NATIVE_KEY");
        k = (e && (e[0] == 's' || e[0] == 'S') && (e[1] == 't' || e[1] == 'T')) ? CELL_PAD_CTRL_START : CELL_PAD_CTRL_SELECT;
        fprintf(stderr, "[RTCS] native movie pause taps %s into the game (WINPORT_CUTSCENE_NATIVE_KEY)\n",
                k == CELL_PAD_CTRL_START ? "START" : "SELECT");
        fflush(stderr);
    }
    return (u16)k;
}

/* ---- learn mode: per-word statistics of the static data for two labels (0 = gameplay, 1 = real-time cutscene) ---- */
static struct {
    int on, label, skip, nr;
    unsigned lo[6], hi[6], nw;
    unsigned* a[2]; unsigned* o[2]; unsigned* f[2]; unsigned char* eq[2];
    unsigned long long ns[2], eps[2], interval, guard, t_last, t_toggle, t_rep, ns_rep;
    SHORT k6, k7, k8;
} s51l = { -1 };

static void wp51_learn_report(const char* why)
{
    FILE* fp;
    unsigned w = 0, nval = 0, nbit = 0, lines = 0;
    if (s51l.on != 1 || !s51l.ns[0] || !s51l.ns[1]) {
        fprintf(stderr, "[RTCS-LEARN] report (%s): need samples of both labels (gameplay %llu, cutscene %llu)\n", why,
                s51l.ns[0], s51l.ns[1]);
        fflush(stderr);
        return;
    }
    CreateDirectoryA("logs", NULL);
    fp = fopen("logs/rtcs_learn.txt", "w");
    if (!fp) return;
    fprintf(fp, "# rtcs_learn (%s): cutscene samples %llu in %llu episode(s), gameplay samples %llu in %llu episode(s)\n"
                "# VAL = one value in every cutscene sample, another single value in every gameplay sample\n"
                "# BIT = bits set in every cutscene sample and clear in every gameplay sample (or the reverse)\n"
                "# last column: a ready WINPORT_RTCS_DETECT term. Trust a line only after 3+ cutscene episodes.\n",
            why, s51l.ns[1], s51l.eps[1], s51l.ns[0], s51l.eps[0]);
    for (int pass = 0; pass < 2; pass++) {   /* pass 0: flag-like (small values, 1-2 bits), pass 1: the rest */
        w = 0;
        for (int r = 0; r < s51l.nr; r++)
            for (unsigned ea = s51l.lo[r]; ea < s51l.hi[r]; ea += 4u, w++) {
                const unsigned b1 = s51l.a[1][w] & ~s51l.o[0][w], b0 = ~s51l.o[1][w] & s51l.a[0][w];
                const int val = s51l.eq[1][w] && s51l.eq[0][w] && s51l.f[1][w] != s51l.f[0][w];
                const unsigned bits = b1 | b0;
                int small;
                if (!val && !bits) continue;
                small = val ? (s51l.f[1][w] <= 0xFFu && s51l.f[0][w] <= 0xFFu) : (bits && !(bits & (bits - 1u) & ((bits & (bits - 1u)) - 1u)));
                if ((pass == 0) != (small != 0)) continue;
                if (pass == 0) { if (val) nval++; else nbit++; }
                if (lines >= 3000u) continue;
                lines++;
                if (val)
                    fprintf(fp, "VAL 0x%08X scene=0x%08X game=0x%08X   0x%08X=0x%08X\n", ea, s51l.f[1][w], s51l.f[0][w], ea, s51l.f[1][w]);
                else
                    fprintf(fp, "BIT 0x%08X mask=0x%08X scene-bits=0x%08X   0x%08X&0x%08X=0x%08X\n", ea, bits, b1, ea, bits, b1);
            }
    }
    fclose(fp);
    s51l.ns_rep = s51l.ns[0] + s51l.ns[1];
    fprintf(stderr, "[RTCS-LEARN] report (%s): %u flag-like candidates (%u values, %u bits), %u lines -> logs/rtcs_learn.txt "
            "(cutscene %llu samples / %llu episodes, gameplay %llu / %llu)\n", why, nval + nbit, nval, nbit, lines,
            s51l.ns[1], s51l.eps[1], s51l.ns[0], s51l.eps[0]);
    fflush(stderr);
}

static void wp51_learn_init(void)
{
    const char* e = getenv("WINPORT_RTCS_LEARN");
    unsigned total = 0;
    s51l.on = (e && e[0] == '1') ? 1 : 0;
    if (!s51l.on) return;
    s51l.interval = (unsigned long long)wp51_env_int("WINPORT_RTCS_LEARN_MS", 250, 50, 10000);
    s51l.guard = (unsigned long long)wp51_env_int("WINPORT_RTCS_LEARN_GUARD_MS", 1000, 0, 10000);
    s51l.lo[0] = 0x00510000u; s51l.hi[0] = 0x009BA000u;   /* EBOOT .data + .bss (ELF PT_LOAD vaddr 0x510000, memsz 0x4A9808) */
    s51l.nr = 1;
    e = getenv("WINPORT_RTCS_LEARN_RANGES");
    while (e && *e && s51l.nr < 6) {
        char* x;
        unsigned lo = (unsigned)strtoul(e, &x, 0), hi;
        if (x == e || *x != '-') break;
        e = x + 1; hi = (unsigned)strtoul(e, &x, 0);
        if (x == e) break;
        e = x;
        while (*e == ',' || *e == ' ') e++;
        lo &= ~3u; hi &= ~3u;
        if (hi > lo && hi <= 0x10000000u) { s51l.lo[s51l.nr] = lo; s51l.hi[s51l.nr] = hi; s51l.nr++; }
    }
    for (int r = 0; r < s51l.nr; r++) {
        if (total + (s51l.hi[r] - s51l.lo[r]) > 16u * 1024u * 1024u + 0x4AA000u) { s51l.nr = r; break; }
        total += s51l.hi[r] - s51l.lo[r];
    }
    s51l.nw = total / 4u;
    for (int L = 0; L < 2; L++) {
        s51l.a[L] = (unsigned*)malloc((size_t)s51l.nw * 4u);
        s51l.o[L] = (unsigned*)malloc((size_t)s51l.nw * 4u);
        s51l.f[L] = (unsigned*)malloc((size_t)s51l.nw * 4u);
        s51l.eq[L] = (unsigned char*)malloc((size_t)s51l.nw);
        if (!s51l.a[L] || !s51l.o[L] || !s51l.f[L] || !s51l.eq[L]) {
            fprintf(stderr, "[RTCS-LEARN] out of memory (%u words) -> learn mode off\n", s51l.nw);
            fflush(stderr);
            s51l.on = 0;
            return;
        }
    }
    fprintf(stderr, "[RTCS-LEARN] ON: %d range(s), %u KB sampled every %llu ms. F7 = real-time cutscene on screen (toggle, high "
            "beep = ON), F8 = pause sampling (menus / loading), F6 = write logs/rtcs_learn.txt\n", s51l.nr, total / 1024u, s51l.interval);
    fflush(stderr);
}

static void wp51_learn_sample(void)
{
    const int L = s51l.label, first = (s51l.ns[L] == 0);
    unsigned* A = s51l.a[L]; unsigned* O = s51l.o[L]; unsigned* F = s51l.f[L];
    unsigned char* E = s51l.eq[L];
    unsigned w = 0;
    for (int r = 0; r < s51l.nr; r++) {
        const unsigned char* b = vm_base + s51l.lo[r];
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery(b, &mbi, sizeof mbi) || mbi.State != MEM_COMMIT
            || (const unsigned char*)mbi.BaseAddress + mbi.RegionSize < vm_base + s51l.hi[r]) return;   /* not mapped yet */
    }
    for (int r = 0; r < s51l.nr; r++) {
        const unsigned char* b = vm_base + s51l.lo[r];
        const unsigned n = (s51l.hi[r] - s51l.lo[r]) / 4u;
        for (unsigned i = 0; i < n; i++, w++, b += 4) {
            const unsigned v = (unsigned)b[0] << 24 | (unsigned)b[1] << 16 | (unsigned)b[2] << 8 | (unsigned)b[3];
            if (first) { A[w] = O[w] = F[w] = v; E[w] = 1; }
            else { A[w] &= v; O[w] |= v; if (v != F[w]) E[w] = 0; }
        }
    }
    s51l.ns[L]++;
}

static void wp51_learn_poll(void)
{
    unsigned long long now;
    SHORT k6, k7, k8;
    if (s51l.on < 0) wp51_learn_init();
    if (s51l.on != 1 || !vm_base) return;
    now = wp47_ms();
    k6 = GetAsyncKeyState(VK_F6); k7 = GetAsyncKeyState(VK_F7); k8 = GetAsyncKeyState(VK_F8);
    if ((k7 & 0x8000) && !(s51l.k7 & 0x8000)) {
        s51l.label ^= 1;
        s51l.eps[s51l.label]++;
        s51l.t_toggle = now;
        MessageBeep(s51l.label ? MB_ICONASTERISK : MB_OK);
        fprintf(stderr, "[RTCS-LEARN] F7: label = %s (episode %llu)\n", s51l.label ? "REAL-TIME CUTSCENE" : "gameplay", s51l.eps[s51l.label]);
        fflush(stderr);
        if (!s51l.label) wp51_learn_report("cutscene episode ended");
    }
    if ((k8 & 0x8000) && !(s51l.k8 & 0x8000)) {
        s51l.skip ^= 1;
        fprintf(stderr, "[RTCS-LEARN] F8: sampling %s\n", s51l.skip ? "PAUSED" : "resumed");
        fflush(stderr);
    }
    if ((k6 & 0x8000) && !(s51l.k6 & 0x8000)) wp51_learn_report("F6");
    s51l.k6 = k6; s51l.k7 = k7; s51l.k8 = k8;
    if (!s51l.eps[0]) s51l.eps[0] = 1;   /* the run starts in gameplay */
    if (s51l.skip || now - s51l.t_last < s51l.interval || now - s51l.t_toggle < s51l.guard) return;
    s51l.t_last = now;
    wp51_learn_sample();
    if (now - s51l.t_rep >= 60000ull) { s51l.t_rep = now; if (s51l.ns[0] + s51l.ns[1] != s51l.ns_rep) wp51_learn_report("periodic"); }
}

/* cellPadGetData port 0, before the v47 UI: learn sampling, detector + debounce, diag logging, skip safety */
static void wp51_poll(u16 btns)
{
    const int mode = wp51_cfg();
    unsigned long long now;
    wp51_learn_poll();
    if (!mode || !s51.nterm) return;
    now = wp47_ms();
    {
        const int raw = wp51_eval(mode == 2, now);
        unsigned long long need;
        if (raw != s51.raw) { s51.raw = raw; s51.t_edge = now; }
        need = raw ? (unsigned long long)s51.on_ms : (s51.ff_on ? 100ull : (unsigned long long)s51.off_ms);
        if (raw != s51.active && now - s51.t_edge >= need) {
            s51.active = raw;
            if (raw) {
                s51.t_on = now; s51.n_scene++;
                fprintf(stderr, "[RTCS] real-time cutscene START #%llu (%s)\n", s51.n_scene, mode == 1 ? "full mode" : "diag: input untouched");
            } else {
                fprintf(stderr, "[RTCS] real-time cutscene END #%llu after %.1f s\n", s51.n_scene, (double)(now - s51.t_on) / 1000.0);
                wp51_ff_end(now, "scene ended");
            }
            fflush(stderr);
        }
    }
    if (s51.active && mode == 2) {
        const u16 rise = (u16)(btns & ~s51.prev);
        if (rise & (CELL_PAD_CTRL_SELECT | CELL_PAD_CTRL_START)) {
            fprintf(stderr, "[RTCS] diag: %s pressed in the detected scene (passed to the game unchanged)\n",
                    (rise & CELL_PAD_CTRL_SELECT) ? "SELECT" : "START");
            fflush(stderr);
        }
    }
    s51.prev = btns;
    if (s51.ff_on && now - s51.t_ff > s51.ff_max_ms) wp51_ff_end(now, "safety limit WINPORT_RTCS_FF_MAX_S");
}
#else
static void wp51_poll(u16 btns) { (void)btns; }
int wp51_ff_active(void) { return 0; }
#endif
/* ---- end winport v51-rt-cutscene ---- */
/* ---- winport v52-movie-pausemenu (engine_fixes_v52_movie_pausemenu.py, research/F3_movie_pausemenu.md) ------ */
#if defined(_WIN32)
/* last digital bytes the game accepted (a report with len > 0); see Part 2 in the script header */
static u16 s52_last_d1 = 0xFFFF, s52_last_d2 = 0xFFFF;
static void wp52_fix_len(CellPadData* d, u32 setting)
{
    const u16 d1 = d->button[CELL_PAD_BTN_OFFSET_DIGITAL1], d2 = d->button[CELL_PAD_BTN_OFFSET_DIGITAL2];
    if (d->len == 0 && (d1 != s52_last_d1 || d2 != s52_last_d2)) {
        d->len = (setting & CELL_PAD_SETTING_SENSOR_ON) ? CELL_PAD_LEN_CHANGE_SENSOR_ON
               : (setting & CELL_PAD_SETTING_PRESS_ON) ? CELL_PAD_LEN_CHANGE_PRESS_ON : CELL_PAD_LEN_CHANGE_DEFAULT;
        d->button[0] = (u16)d->len;
    }
    if (d->len > 0) { s52_last_d1 = d1; s52_last_d2 = d2; }
}

/* ---- [F3DIAG]: the game's UI script-variable object + movie state ---- */
#define WP52_UIOBJ_PTR 0x0057CF74u
#define WP52_UIOBJ_SIZE 0x2024u
static unsigned wp52_be32(unsigned ea)
{
    const unsigned char* b;
    if (!vm_base || ea < 0x10000u || ea > 0xFFFFFFF0u) return 0;
    b = vm_base + ea;
    return ((unsigned)b[0] << 24) | ((unsigned)b[1] << 16) | ((unsigned)b[2] << 8) | b[3];
}
/* ---- winport v92a-ckpt-ingame (engine_fixes_v92a_ckpt_ingame.py): shared state (renderer + ppu_hle.cpp) ---- */
#define WP92_TW 320
#define WP92_TH 180
volatile LONG wp92_pending = 0;     /* 1 = an in-game checkpoint load waits for the main game thread (ps3_hle_call hook) */
volatile LONG wp92_sub = 0;         /* renderer: Load Checkpoint list / confirm page open (O / Tri / Esc = back, not close) */
volatile LONG wp92_close_req = 0;   /* renderer: close the panel now (after X = load) */
volatile LONG wp92_snap_req = 0;    /* hash of the blob that still wants a thumbnail (0 = none) */
volatile LONG wp92_snap_now = 0;    /* 1 = capture the next presented frame whatever the game state */
volatile LONG wp92_thumb_hash = 0;  /* blob hash the thumbnail below belongs to (written last by the renderer) */
unsigned char wp92_thumb[WP92_TW * WP92_TH * 3];   /* RGB, top row first */
unsigned wp92_gstate(void);
static void wp92_write_thumb(const char* binpath, unsigned hash);
static void wp92_request(const char* path);
static void wp92_backfill(void);   /* winport v92a-ckpt-backfill */
/* ---- end winport v92a-ckpt-ingame decl ---- */
/* ---- winport v90-ckpt (engine_fixes_v90_ckpt.py, research/SS2_checkpoint_saves.md): checkpoint blob -> save file ---- */
#define WP90_BLOB 0xA000u
#define WP90_OPD 0x0051DED0u   /* OPD of func_000CAB38 = { 0x000CAB38, TOC 0x00541178 } */
volatile LONG wp90_req = 0;    /* renderer Port Settings row "Save Checkpoint": 1 = save now */
volatile LONG wp90_gen = 0;    /* +1 per finished save attempt (renderer toast) */
volatile LONG wp90_have = 0;   /* a valid blob is cached */
char wp90_msg[200];            /* result of the last save attempt */
char wp90_load[MAX_PATH];      /* renderer Port Settings row "Load Checkpoint": file to restart into (wp90_req = 2) */
static unsigned char s90_cur[WP90_BLOB], s90_a[WP90_BLOB], s90_b[WP90_BLOB];
static unsigned s90_hash, s90_blobs, s90_buf_logged;
static unsigned long long s90_t_first;
static int s90_scan_on, s90_save_at = -1, s90_save_at_done, s90_load_at = -1, s90_load_at_done;
static char s90_dir[MAX_PATH], s90_dump[MAX_PATH];
const char* wp90_dirp(void) { return s90_dir; }   /* checkpoints folder (renderer lists ckpt_*.bin) */
static void wp90_exe_dir(char* out, size_t cap);
static unsigned wp90_fnv(const unsigned char* p, unsigned n)
{
    unsigned h = 2166136261u;
    for (unsigned i = 0; i < n; i++) { h ^= p[i]; h *= 16777619u; }
    return h;
}
static int wp90_readable(unsigned ea, unsigned n)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (!vm_base || ea < 0x10000u || ea > 0xFFFFFFFFu - n) return 0;
    if (!VirtualQuery(vm_base + ea, &mbi, sizeof mbi) || mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return 0;
    return (const unsigned char*)mbi.BaseAddress + mbi.RegionSize >= vm_base + ea + n;
}
static unsigned wp90_bufaddr(unsigned* stream)
{
    if (!wp90_readable(WP90_OPD, 8) || wp52_be32(WP90_OPD) != 0x000CAB38u) return 0;
    const unsigned toc = wp52_be32(WP90_OPD + 4u);
    if (!wp90_readable(toc - 0x6000u, 0x6000u)) return 0;
    const unsigned s = wp52_be32(toc - 0x5EDCu);
    if (!wp90_readable(s, 0x9110u + WP90_BLOB)) return 0;
    if (stream) *stream = s;
    if (!s90_buf_logged) { s90_buf_logged = 1; fprintf(stderr, "[CKPT90] TOC 0x%08X stream 0x%08X blob buffer 0x%08X\n", toc, s, s + 0x9110u); fflush(stderr); }
    return s + 0x9110u;
}
/* DATAxx.BIN trailer: big-endian u32 at 0x9FFC = sum of the big-endian u32 words 0..0x9FF8 (checked: DATA00-02 of
 * seed_colossus). The buffer keeps the trailer of the LAST LOADED save, so a later checkpoint has a stale sum -> the LOAD
 * menu says "Error loading file" (run SR2). Recomputed for every file we write. */
static void wp90_fix_sum(unsigned char* b)
{
    unsigned sum = 0;
    for (unsigned i = 0; i < WP90_BLOB - 4u; i += 4) sum += (unsigned)b[i] << 24 | (unsigned)b[i + 1] << 16 | (unsigned)b[i + 2] << 8 | b[i + 3];
    b[WP90_BLOB - 4] = (unsigned char)(sum >> 24); b[WP90_BLOB - 3] = (unsigned char)(sum >> 16);
    b[WP90_BLOB - 2] = (unsigned char)(sum >> 8); b[WP90_BLOB - 1] = (unsigned char)sum;
}
static unsigned wp90_used(const unsigned char* b)
{
    unsigned n = WP90_BLOB;
    while (n > 8 && !b[n - 1]) n--;
    return n;
}
/* level names: +8 and +0x1D (seen "rhod24" / "rhod25"); printable, max 15 chars */
static void wp90_name(const unsigned char* b, unsigned off, char* out, int cap)
{
    int n = 0;
    while (n < cap - 1 && n < 15 && b[off + n] > 0x20 && b[off + n] < 0x7F) { out[n] = (char)b[off + n]; n++; }
    out[n] = 0;
    if (!n) snprintf(out, (size_t)cap, "none");
}
static void wp90_strings(const unsigned char* b, unsigned lim, char* out, int cap)
{
    int o = 0, cnt = 0; unsigned i = 8;
    out[0] = 0;
    while (i < lim && cnt < 14 && o < cap - 24) {
        unsigned j = i;
        while (j < lim && b[j] >= 0x20 && b[j] < 0x7F) j++;
        if (j - i >= 4) { o += snprintf(out + o, (size_t)(cap - o), "%s%.*s", cnt ? " " : "", (int)(j - i > 24 ? 24 : j - i), (const char*)b + i); cnt++; }
        i = j + 1;
    }
}
/* stable copy of the blob buffer: the stream must be idle ([stream+8] == 0) and two copies 20 ms apart must match */
static int wp90_grab(unsigned char* dst)
{
    unsigned st = 0;
    const unsigned a = wp90_bufaddr(&st);
    if (!a || wp52_be32(st + 8u) != 0u) return 0;
    memcpy(s90_a, vm_base + a, WP90_BLOB);
    Sleep(20);
    if (wp52_be32(st + 8u) != 0u) return 0;
    memcpy(dst, vm_base + a, WP90_BLOB);
    if (memcmp(s90_a, dst, WP90_BLOB)) return 0;
    return dst[0] == 0xCA && dst[1] == 0xFE && dst[2] == 0xBA && dst[3] == 0xD4 && dst[4] == 0 && dst[5] == 0 && dst[6] == 0 && dst[7] == 1;
}
static void wp90_poll_blob(void)
{
    if (!wp90_grab(s90_b)) return;
    const unsigned h = wp90_fnv(s90_b, WP90_BLOB);
    if (wp90_have && h == s90_hash) return;
    memcpy(s90_cur, s90_b, WP90_BLOB); s90_hash = h; s90_blobs++;
    if (!s90_t_first) s90_t_first = GetTickCount64();
    InterlockedExchange(&wp90_have, 1);
    InterlockedExchange(&wp92_snap_req, (LONG)h);   /* winport v92a-ckpt-ingame: thumbnail of this checkpoint */
    char l1[20], l2[20], ss[600];
    wp90_name(s90_cur, 8u, l1, sizeof l1); wp90_name(s90_cur, 0x1Du, l2, sizeof l2);
    const unsigned used = wp90_used(s90_cur);
    wp90_strings(s90_cur, used < 4096u ? used : 4096u, ss, sizeof ss);
    unsigned toc = wp52_be32(WP90_OPD + 4u);
    fprintf(stderr, "[CKPT90] blob #%u t=%llums state=%u hash=%08X used=%u lv=%s/%s mode=0x%X strings: %s\n", s90_blobs,
            (unsigned long long)GetTickCount64(), wp52_be32(0x0057EB18u), h, used, l1, l2, wp52_be32(wp52_be32(toc - 0x5ECCu)), ss);
    fflush(stderr);
    if (s90_dump[0]) {
        char p[MAX_PATH + 64];
        snprintf(p, sizeof p, "%s\\blob_%03u_%s_%s.bin", s90_dump, s90_blobs, l1, l2);
        FILE* f = fopen(p, "wb");
        if (f) { fwrite(s90_cur, 1, WP90_BLOB, f); fclose(f); }
    }
}
/* WINPORT_CKPT90_SCAN=1: every CAFEBAD4 00000001 header in committed guest memory (checkpoint history or not?) */
static void wp90_scan(void)
{
    static unsigned last_sig; static int first = 1;
    char out[1400]; int o = 0, cnt = 0; unsigned sig = 0;
    unsigned long long ea = 0x10000ull;
    const unsigned long long t0 = GetTickCount64();
    while (ea < 0x100000000ull) {
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery(vm_base + ea, &mbi, sizeof mbi)) break;
        const unsigned long long rs = (unsigned long long)((const unsigned char*)mbi.BaseAddress - vm_base);
        const unsigned long long re = rs + mbi.RegionSize;
        if (mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) && rs < 0xC0000000ull) {
            const unsigned char* p = vm_base + rs;
            const size_t n = (size_t)(re - rs);
            for (size_t i = 0; i + 0x40 <= n; i += 4) {
                if (p[i] != 0xCA || p[i + 1] != 0xFE || p[i + 2] != 0xBA || p[i + 3] != 0xD4) continue;
                if (p[i + 4] || p[i + 5] || p[i + 6] || p[i + 7] != 1) continue;
                const unsigned a = (unsigned)(rs + i);
                const unsigned h = wp90_fnv(p + i, 0x400u < n - i ? 0x400u : (unsigned)(n - i));
                sig = sig * 31u + a * 7u + h;
                if (cnt < 24 && o < (int)sizeof out - 64) {
                    char l1[20], l2[20]; wp90_name(p + i, 8u, l1, sizeof l1); wp90_name(p + i, 0x1Du, l2, sizeof l2);
                    o += snprintf(out + o, sizeof out - (size_t)o, " 0x%08X(%s/%s h%08X)", a, l1, l2, h);
                }
                cnt++;
            }
        }
        ea = re > ea ? re : ea + 0x1000ull;
    }
    if (first || sig != last_sig) {
        fprintf(stderr, "[CKPT90] scan %d header(s) in %llums:%s\n", cnt, (unsigned long long)(GetTickCount64() - t0), out);
        fflush(stderr);
        last_sig = sig; first = 0;
    }
}
static void wp90_mkdir_p(const char* d)
{
    char t[MAX_PATH]; snprintf(t, sizeof t, "%s", d);
    for (char* q = t + 3; *q; q++) if (*q == '\\' || *q == '/') { char c = *q; *q = 0; CreateDirectoryA(t, NULL); *q = c; }
    CreateDirectoryA(t, NULL);
}
static void wp90_save(const char* why)
{
    char l1[20], l2[20], p[MAX_PATH + 96];
    if (!wp90_have) {
        snprintf(wp90_msg, sizeof wp90_msg, "No checkpoint yet");
        fprintf(stderr, "[CKPT90] save (%s): no checkpoint blob in memory yet\n", why); fflush(stderr);
        InterlockedIncrement(&wp90_gen);
        return;
    }
    wp90_name(s90_cur, 8u, l1, sizeof l1); wp90_name(s90_cur, 0x1Du, l2, sizeof l2);
    SYSTEMTIME tm; GetLocalTime(&tm);
    wp90_mkdir_p(s90_dir);
    snprintf(p, sizeof p, "%s\\ckpt_%04u%02u%02u_%02u%02u%02u_%s_%s.bin", s90_dir, tm.wYear, tm.wMonth, tm.wDay, tm.wHour,
             tm.wMinute, tm.wSecond, l1, l2);
    static unsigned char out[WP90_BLOB];
    memcpy(out, s90_cur, WP90_BLOB); wp90_fix_sum(out);
    FILE* f = fopen(p, "wb");
    size_t w = f ? fwrite(out, 1, WP90_BLOB, f) : 0;
    if (f) fclose(f);
    if (w != WP90_BLOB) {
        snprintf(wp90_msg, sizeof wp90_msg, "Checkpoint NOT saved (cannot write)");
        fprintf(stderr, "[CKPT90] save (%s): cannot write %s\n", why, p); fflush(stderr);
    } else {
        char ss[600], ip[MAX_PATH + 32];
        wp90_strings(s90_cur, 4096u, ss, sizeof ss);
        snprintf(ip, sizeof ip, "%s\\index.txt", s90_dir);
        FILE* x = fopen(ip, "ab");
        if (x) { fprintf(x, "%s\t%04u-%02u-%02u %02u:%02u:%02u\t%s/%s\thash %08X\t%s\r\n", strrchr(p, '\\') + 1, tm.wYear, tm.wMonth,
                         tm.wDay, tm.wHour, tm.wMinute, tm.wSecond, l1, l2, s90_hash, ss); fclose(x); }
        snprintf(wp90_msg, sizeof wp90_msg, "Checkpoint saved: %s / %s  %02u:%02u", l1, l2, tm.wHour, tm.wMinute);
        fprintf(stderr, "[CKPT90] save (%s): wrote %s (blob #%u hash %08X)\n", why, p, s90_blobs, s90_hash); fflush(stderr);
        wp92_write_thumb(p, s90_hash);   /* winport v92a-ckpt-ingame */
    }
    InterlockedIncrement(&wp90_gen);
}
static int wp90_fg(void)
{
    HWND fw = GetForegroundWindow(); DWORD pid = 0;
    if (fw) GetWindowThreadProcessId(fw, &pid);
    return fw && pid == GetCurrentProcessId();
}
/* Port Settings "Load Checkpoint": restart the game through the warp launcher with wp90_load (the game can only load a
 * save from its own LOAD menu, and it reads MASTER.BIN once at boot). The new launcher gets the ORIGINAL save root back. */
/* winport release v92: the v90 relaunch path (dev launcher) is not part of the release; Load Checkpoint runs in game */
static DWORD WINAPI wp90_thread(LPVOID u)
{
    (void)u;
    unsigned long long t_blob = 0, t_scan = 0;
    SHORT pk = 0;
    for (;;) {
        Sleep(50);
        if (!vm_base) continue;
        const unsigned long long now = GetTickCount64();
        int want = 0;
        const SHORT k = wp90_fg() ? GetAsyncKeyState(VK_F6) : 0;
        if ((k & 0x8000) && !(pk & 0x8000)) want = 1;
        pk = k;
        { const LONG rq = InterlockedExchange(&wp90_req, 0); if (rq == 2) { wp92_request(wp90_load); continue; } if (rq) want = 2; }   /* winport v92a-ckpt-ingame: in game, no relaunch */
        if (s90_save_at >= 0 && !s90_save_at_done && s90_t_first && now - s90_t_first >= (unsigned long long)s90_save_at) { s90_save_at_done = 1; want = 3; }
        if (s90_load_at >= 0 && !s90_load_at_done && s90_t_first && now - s90_t_first >= (unsigned long long)s90_load_at) {
            s90_load_at_done = 1;   /* test hook: the newest ckpt_*.bin of the folder, as the panel row would pick it */
            WIN32_FIND_DATAA fd; char pat[MAX_PATH + 16]; FILETIME best = {0, 0}; wp90_load[0] = 0;
            snprintf(pat, sizeof pat, "%s\\ckpt_*.bin", s90_dir);
            HANDLE fh = FindFirstFileA(pat, &fd);
            if (fh != INVALID_HANDLE_VALUE) { do { if (CompareFileTime(&fd.ftLastWriteTime, &best) > 0) { best = fd.ftLastWriteTime;
                snprintf(wp90_load, sizeof wp90_load, "%s\\%s", s90_dir, fd.cFileName); } } while (FindNextFileA(fh, &fd)); FindClose(fh); }
            fprintf(stderr, "[CKPT90] test LOAD_AT: %s\n", wp90_load[0] ? wp90_load : "(no ckpt file)"); fflush(stderr);
            if (wp90_load[0]) { wp92_request(wp90_load); continue; }   /* winport v92a-ckpt-ingame */
        }
        if (want || now - t_blob >= 1000ull) { t_blob = now; wp90_poll_blob(); }
        wp92_backfill();   /* winport v92a-ckpt-backfill */
        if (want) wp90_save(want == 1 ? "F6" : want == 2 ? "panel" : "test SAVE_AT");
        if (s90_scan_on && now - t_scan >= 10000ull) { t_scan = now; wp90_scan(); }
    }
    return 0;
}
static void wp90_exe_dir(char* out, size_t cap)
{
    DWORD n = GetModuleFileNameA(NULL, out, (DWORD)cap);
    if (!n || n >= cap) { snprintf(out, cap, "."); return; }
    char* a = strrchr(out, '\\'); if (a) *a = 0;
}
/* winport release v92: the dev warp (scratch save root for agents) is not part of the release */
__attribute__((constructor)) static void wp90_ctor(void)
{
    char sw[8]; DWORD l = GetEnvironmentVariableA("WINPORT_CKPT90", sw, sizeof sw);
    if (!l || l >= sizeof sw || sw[0] != '1') return;
    char t[MAX_PATH], exe[MAX_PATH];
    wp90_exe_dir(exe, sizeof exe);
    l = GetEnvironmentVariableA("WINPORT_CKPT90_DIR", t, sizeof t);
    if (l && l < sizeof t) snprintf(s90_dir, sizeof s90_dir, "%s", t); else snprintf(s90_dir, sizeof s90_dir, "%s\\checkpoints", exe);
    l = GetEnvironmentVariableA("WINPORT_CKPT90_DUMP", t, sizeof t);
    if (l && l < sizeof t) { snprintf(s90_dump, sizeof s90_dump, "%s", t); wp90_mkdir_p(s90_dump); }
    l = GetEnvironmentVariableA("WINPORT_CKPT90_SCAN", t, sizeof t); s90_scan_on = (l && l < sizeof t && t[0] == '1');
    l = GetEnvironmentVariableA("WINPORT_CKPT90_SAVE_AT", t, sizeof t); if (l && l < sizeof t) s90_save_at = atoi(t);
    l = GetEnvironmentVariableA("WINPORT_CKPT90_LOAD_AT", t, sizeof t); if (l && l < sizeof t) s90_load_at = atoi(t);
    HANDLE th = CreateThread(NULL, 256 * 1024, wp90_thread, NULL, 0, NULL);
    if (th) CloseHandle(th);
    fprintf(stderr, "[CKPT90] on: F6 / Port Settings 'Save Checkpoint' -> %s (scan %d, dump %s, save_at %d)\n", s90_dir, s90_scan_on,
            s90_dump[0] ? s90_dump : "off", s90_save_at);
    fflush(stderr);
}
/* ---- end winport v90-ckpt ---- */
/* ---- winport v92a-ckpt-ingame: in-game checkpoint load + thumbnails ---- */
static unsigned char s92_blob[WP90_BLOB];
static char s92_file[MAX_PATH];
static unsigned long long s92_t_req;
static int s92_logwait;
unsigned wp92_gstate(void) { return wp90_readable(0x0057EB18u, 4) ? wp52_be32(0x0057EB18u) : 0u; }
static void wp92_request(const char* path)
{
    FILE* f = fopen(path, "rb");
    size_t n = f ? fread(s92_blob, 1, WP90_BLOB, f) : 0;
    if (f) fclose(f);
    if (n < 0x40 || s92_blob[0] != 0xCA || s92_blob[1] != 0xFE || s92_blob[2] != 0xBA || s92_blob[3] != 0xD4) {
        snprintf(wp90_msg, sizeof wp90_msg, "Cannot load: not a checkpoint file");
        fprintf(stderr, "[CKPT92] load: %s is not a checkpoint blob (%u bytes)\n", path, (unsigned)n); fflush(stderr);
        InterlockedIncrement(&wp90_gen);
        return;
    }
    if (n < WP90_BLOB) memset(s92_blob + n, 0, WP90_BLOB - n);
    wp90_fix_sum(s92_blob);
    snprintf(s92_file, sizeof s92_file, "%s", path);
    s92_t_req = GetTickCount64(); s92_logwait = 0;
    char l1[20], l2[20]; wp90_name(s92_blob, 8u, l1, sizeof l1); wp90_name(s92_blob, 0x1Du, l2, sizeof l2);
    fprintf(stderr, "[CKPT92] load requested: %s (%s/%s hash %08X) -> in game at the next main-thread pad read (state %u)\n",
            path, l1, l2, wp90_fnv(s92_blob, WP90_BLOB), wp92_gstate());
    fflush(stderr);
    { void wp92_backfill_arm(void); wp92_backfill_arm(); }   /* winport v92a-ckpt-backfill */
    InterlockedExchange(&wp92_pending, 1);
}
static int wp94_resume_first(unsigned st);   /* winport v94b-ckpt-music (defined below) */
/* main game thread, inside ps3_hle_call(cellPadGetData) before the call: 1 = the caller runs func_000B8254 now */
int wp92_main_prepare(void)
{
    if (!wp92_pending) return 0;
    const unsigned long long now = GetTickCount64();
    const unsigned st = wp92_gstate();
    unsigned s = 0; const unsigned a = wp90_bufaddr(&s);
    extern volatile LONG wp80_pset_open;
    const char* wait = (st != 11u && st != 12u) ? "game state" : wp80_pset_open ? "panel open" : !a ? "no buffer" :
                       wp52_be32(s + 8u) ? "stream busy" : NULL;
    if (wait) {
        if (!s92_logwait) { s92_logwait = 1; fprintf(stderr, "[CKPT92] load waits: %s (state %u)\n", wait, st); fflush(stderr); }
        if (now - s92_t_req > 20000ull) {
            InterlockedExchange(&wp92_pending, 0);
            snprintf(wp90_msg, sizeof wp90_msg, "Load only works during gameplay");
            fprintf(stderr, "[CKPT92] load GIVEN UP after 20 s (%s, state %u)\n", wait, st); fflush(stderr);
            InterlockedIncrement(&wp90_gen);
        }
        return 0;
    }
    if (wp94_resume_first(st)) return 0;   /* winport v94b-ckpt-music: pause menu open -> the game resumes first */
    memcpy(vm_base + a, s92_blob, WP90_BLOB);
    InterlockedExchange(&wp92_pending, 0);
    fprintf(stderr, "[CKPT92] in-game load: blob written to 0x%08X; game Restart-from-checkpoint (func_000B8254) on thread %lu, state %u, %llu ms after the request\n",
            a, GetCurrentThreadId(), st, now - s92_t_req);
    fflush(stderr);
    return 1;
}
void wp92_main_done(void)
{
    char l1[20], l2[20]; wp90_name(s92_blob, 8u, l1, sizeof l1); wp90_name(s92_blob, 0x1Du, l2, sizeof l2);
    const char* b = strrchr(s92_file, '\\');
    snprintf(wp90_msg, sizeof wp90_msg, "Loading checkpoint: %s / %s", l1, l2);
    fprintf(stderr, "[CKPT92] restart call returned (state now %u): loading %s\n", wp92_gstate(), b ? b + 1 : s92_file);
    fflush(stderr);
    InterlockedIncrement(&wp90_gen);
}
/* 24-bit bottom-up BMP of wp92_thumb next to the .bin */
static int wp92_bmp(const char* path)
{
    FILE* f = fopen(path, "wb");
    if (!f) return 0;
    const unsigned rs = (WP92_TW * 3u + 3u) & ~3u, img = rs * WP92_TH, fs = 54u + img;
    unsigned char h[54]; memset(h, 0, sizeof h);
    h[0] = 'B'; h[1] = 'M';
    #define W92(o, v) do { h[o] = (unsigned char)(v); h[o + 1] = (unsigned char)((v) >> 8); h[o + 2] = (unsigned char)((v) >> 16); h[o + 3] = (unsigned char)((v) >> 24); } while (0)
    W92(2, fs); W92(10, 54u); W92(14, 40u); W92(18, (unsigned)WP92_TW); W92(22, (unsigned)WP92_TH); h[26] = 1; h[28] = 24; W92(34, img);
    #undef W92
    fwrite(h, 1, 54, f);
    unsigned char row[WP92_TW * 3 + 4]; memset(row, 0, sizeof row);
    for (int y = WP92_TH - 1; y >= 0; y--) {
        const unsigned char* s = wp92_thumb + (size_t)y * WP92_TW * 3;
        for (int x = 0; x < WP92_TW; x++) { row[x * 3] = s[x * 3 + 2]; row[x * 3 + 1] = s[x * 3 + 1]; row[x * 3 + 2] = s[x * 3]; }
        fwrite(row, 1, rs, f);
    }
    fclose(f);
    return 1;
}
static void wp92_write_thumb(const char* binpath, unsigned hash)
{
    char p[MAX_PATH + 8]; snprintf(p, sizeof p, "%s", binpath);
    char* d = strrchr(p, '.'); if (!d) return;
    snprintf(d, 8, ".bmp");
    const char* how = "checkpoint frame";
    if ((unsigned)wp92_thumb_hash != hash) {   /* no frame of this checkpoint yet: take the current one */
        how = "current frame";
        InterlockedExchange(&wp92_snap_req, (LONG)hash); InterlockedExchange(&wp92_snap_now, 1);
        for (int i = 0; i < 30 && (unsigned)wp92_thumb_hash != hash; i++) Sleep(50);
        InterlockedExchange(&wp92_snap_now, 0);
    }
    if ((unsigned)wp92_thumb_hash != hash) { fprintf(stderr, "[CKPT92] thumbnail: no frame captured (renderer idle?) -> none for %s\n", p); fflush(stderr); return; }
    fprintf(stderr, "[CKPT92] thumbnail %s %s (%dx%d, %s)\n", wp92_bmp(p) ? "wrote" : "CANNOT write", p, WP92_TW, WP92_TH, how);
    fflush(stderr);
}
/* ---- end winport v92a-ckpt-ingame ---- */
/* ---- winport v94b-ckpt-music (engine_fixes_v94b_ckpt_music.py): close the pause menu before a checkpoint load ---- */
static int s94_on = -1, s94_taps;
static volatile unsigned long long s94_tap_t;   /* SELECT tap into the game while now - t < 150 ms */
static unsigned long long s94_t_first, s94_t11;
static int s94_settle = 1500;   /* ms of gameplay after the resume before the load (WINPORT_CKPT94_SETTLE_MS) */
/* SELECT bits for the game now (rides wp81_inject, applied in both pad paths) */
static u16 wp94_inject(void)
{
    const unsigned long long t = s94_tap_t;
    if (!t) return 0;
    if (GetTickCount64() - t < 150ull) return CELL_PAD_CTRL_SELECT;
    s94_tap_t = 0;
    return 0;
}
/* wp92_main_prepare, main game thread, load ready: 1 = not yet (the pause menu is being closed first) */
static int wp94_resume_first(unsigned st)
{
    const unsigned long long now = GetTickCount64();
    if (s94_on < 0) { const char* e = getenv("WINPORT_CKPT94"); s94_on = (e && e[0] == '0') ? 0 : 1;
                      e = getenv("WINPORT_CKPT94_SETTLE_MS"); if (e && *e) s94_settle = atoi(e); }
    if (st != 12u || !s94_on) {
        if (s94_taps && s94_on) {   /* resumed: let the game finish its resume (sound un-pause) before the load */
            if (!s94_t11) { s94_t11 = now; fprintf(stderr, "[CKPT94] game resumed (state %u) %llu ms after the first SELECT tap; load in %d ms\n", st, now - s94_t_first, s94_settle); fflush(stderr); }
            if (now - s94_t11 < (unsigned long long)s94_settle) return 1;
            fprintf(stderr, "[CKPT94] -> load from gameplay (state %u)\n", st); fflush(stderr);
        }
        s94_taps = 0; s94_t11 = 0;
        return 0;
    }
    s94_t11 = 0;
    if (s94_taps && now - s94_t_first < (unsigned long long)s94_taps * 1500ull) return 1;   /* menu closing: wait */
    if (s94_taps >= 3) {
        fprintf(stderr, "[CKPT94] pause menu still open after %d SELECT taps -> loading from the menu (v92a path; music may stay paused)\n", s94_taps);
        fflush(stderr);
        s94_taps = 0;
        return 0;
    }
    if (!s94_taps) s94_t_first = now;
    s94_taps++;
    s94_tap_t = now;
    fprintf(stderr, "[CKPT94] load waits: game pause menu open -> SELECT tap #%d (closes it; the game's resume un-pauses its music/ambience)\n", s94_taps);
    fflush(stderr);
    return 1;
}
/* ---- end winport v94b-ckpt-music ---- */
/* ---- winport v92a-ckpt-backfill: thumbnail for a loaded checkpoint that has none ---- */
static unsigned s92_bf_hash; static int s92_bf_on; static char s92_bf_bmp[MAX_PATH + 8];
void wp92_backfill_arm(void)
{
    snprintf(s92_bf_bmp, sizeof s92_bf_bmp, "%s", s92_file);
    char* d = strrchr(s92_bf_bmp, '.');
    s92_bf_on = 0;
    if (!d || strlen(d) < 4) return;
    snprintf(d, 8, ".bmp");
    if (GetFileAttributesA(s92_bf_bmp) != INVALID_FILE_ATTRIBUTES) return;
    s92_bf_hash = wp90_fnv(s92_blob, WP90_BLOB); s92_bf_on = 1;
}
static void wp92_backfill(void)
{
    if (!s92_bf_on || (unsigned)wp92_thumb_hash != s92_bf_hash) return;
    s92_bf_on = 0;
    if (GetFileAttributesA(s92_bf_bmp) != INVALID_FILE_ATTRIBUTES) return;
    fprintf(stderr, "[CKPT92] backfill: %s %s (first gameplay frame after loading it)\n", wp92_bmp(s92_bf_bmp) ? "wrote" : "CANNOT write", s92_bf_bmp);
    fflush(stderr);
}
/* ---- end winport v92a-ckpt-backfill ---- */
/* the object lives in the game heap (seen at 0x407E2D08): check the host pages are committed before reading */
unsigned wp52_uiobj(void)
{
    static unsigned ok_obj;
    MEMORY_BASIC_INFORMATION mbi;
    const unsigned o = wp52_be32(WP52_UIOBJ_PTR);
    if (o < 0x10000u || o > 0xFFFFFFFFu - WP52_UIOBJ_SIZE) return 0;
    if (o == ok_obj) return o;
    if (!VirtualQuery(vm_base + o, &mbi, sizeof mbi) || mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))
        || (const unsigned char*)mbi.BaseAddress + mbi.RegionSize < vm_base + o + WP52_UIOBJ_SIZE) return 0;
    ok_obj = o;
    return o;
}
/* ---- WINPORT_F3_WATCH=<offset>: x64 hardware write-watchpoint (DR0) on the UI object field at <offset> (default
 * 0x364 = PauseMenu_Event) in every thread; each hit logs the host RIP and up to 10 unwound frames as offsets from the
 * exe base -> map with g2play_v52.syms.txt to the lifted func_XXXXXXXX that raised the event. Diagnostic only. */
#include <tlhelp32.h>
static volatile LONG s52w_hits;
static LONG CALLBACK wp52_watch_veh(PEXCEPTION_POINTERS ep)
{
    if (ep->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP || !(ep->ContextRecord->Dr6 & 1)) return EXCEPTION_CONTINUE_SEARCH;
    ep->ContextRecord->Dr6 = 0;
    if (InterlockedIncrement(&s52w_hits) <= 60) {
        CONTEXT c = *ep->ContextRecord;
        const ULONG64 base = (ULONG64)GetModuleHandleA(NULL);
        char line[512]; int n = 0;
        n += snprintf(line + n, sizeof line - n, "[F3WATCH] hit #%ld tid=%lu rip=+0x%llX frames:", (long)s52w_hits,
                      GetCurrentThreadId(), (unsigned long long)(c.Rip - base));
        for (int f = 0; f < 10 && n < (int)sizeof line - 24; f++) {
            ULONG64 ib = 0; PVOID hd = NULL; ULONG64 est[2] = {0, 0};
            PRUNTIME_FUNCTION rf = RtlLookupFunctionEntry(c.Rip, &ib, NULL);
            if (!rf) { c.Rip = *(ULONG64*)c.Rsp; c.Rsp += 8; }
            else RtlVirtualUnwind(0, ib, c.Rip, rf, &c, &hd, est, NULL);
            if (!c.Rip) break;
            n += snprintf(line + n, sizeof line - n, " +0x%llX", (unsigned long long)(c.Rip - base));
        }
        fprintf(stderr, "%s\n", line);
        fflush(stderr);
    }
    return EXCEPTION_CONTINUE_EXECUTION;
}
static void wp52_watch_arm(void* host_addr)
{
    const DWORD self = GetCurrentThreadId(), pid = GetCurrentProcessId();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 te;
    int armed = 0;
    if (snap == INVALID_HANDLE_VALUE) return;
    te.dwSize = sizeof te;
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
        HANDLE th;
        CONTEXT c;
        if (te.th32OwnerProcessID != pid || te.th32ThreadID == self) continue;
        th = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
        if (!th) continue;
        if (SuspendThread(th) != (DWORD)-1) {
            memset(&c, 0, sizeof c);
            c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
            if (GetThreadContext(th, &c)) {
                c.Dr0 = (DWORD64)host_addr;
                c.Dr7 = (c.Dr7 & ~0xF0003ull) | 1ull | (1ull << 16) | (3ull << 18);   /* L0, RW0=01 (write), LEN0=11 (4 bytes) */
                if (SetThreadContext(th, &c)) armed++;
            }
            ResumeThread(th);
        }
        CloseHandle(th);
    }
    CloseHandle(snap);
    { static int nlog; if (nlog++ < 3) { fprintf(stderr, "[F3WATCH] DR0 write watch on host %p armed in %d threads\n", host_addr, armed); fflush(stderr); } }
}
static DWORD WINAPI wp52_watch_thread(LPVOID p)
{
    for (;;) { wp52_watch_arm(p); Sleep(2000); }   /* re-arm: new threads */
    return 0;
}
static void wp52_watch_poll(unsigned obj)
{
    static int on = -1; static unsigned off;
    if (on < 0) {
        const char* e = getenv("WINPORT_F3_WATCH");
        on = (e && *e) ? 1 : 0;
        off = on ? (unsigned)strtoul(e, NULL, 0) : 0;
        if (on && (off < 4 || off >= WP52_UIOBJ_SIZE)) off = 0x364;
    }
    if (on != 1 || !obj) return;
    on = 2;
    AddVectoredExceptionHandler(1, wp52_watch_veh);
    { HANDLE h = CreateThread(NULL, 0, wp52_watch_thread, (LPVOID)(vm_base + obj + off), 0, NULL); if (h) CloseHandle(h); }
    fprintf(stderr, "[F3WATCH] watching UI object 0x%08X +0x%X (guest 0x%08X)\n", obj, off, obj + off);
    fflush(stderr);
}
static void wp52_diag_poll(void)
{
    static int on = -1, nlog = 0;
    static unsigned obj, prev[WP52_UIOBJ_SIZE / 4]; static unsigned char nchg[WP52_UIOBJ_SIZE / 4];
    static unsigned last_st = 0xFFFFFFFEu;
    static unsigned long long t0;
    wp47_ov_view v;
    unsigned o, st;
    unsigned long long now;
    if (on < 0) { const char* e = getenv("WINPORT_F3_DIAG"); on = (e && e[0] == '1'); if (on) { fprintf(stderr, "[F3DIAG] ON\n"); fflush(stderr); } }
    if (!on) return;
    now = wp47_ms();
    if (!t0) t0 = now;
    st = movie_eos_overlay_state(&v) ? v.st620 : 0xFFFFFFFFu;
    if (st != last_st) { fprintf(stderr, "[F3DIAG] t=%llu st620 %u -> %u (scene=%d)\n", now - t0, last_st, st, s47.active); last_st = st; }
    {   static unsigned long long t_scan;   /* the pad is polled up to ~5000/s during movies: scan every 10 ms */
        if (now - t_scan < 10ull) return;
        t_scan = now; }
    {   /* the game-flow object G (0x0057E960, TOC-0x62B0): +0x1B8 = game state (0xC pause menu, 0xD Power Up menu),
         * +0x218/+0x234/+0x238 pause requests; 0x00869510 = global flags (bit 0x40 set by the pause entry func_000B6050) */
        static unsigned gprev[0xC0 + 1]; static unsigned char gn[0xC0 + 1]; static int ginit;
        for (unsigned i = 0; i <= 0xC0; i++) {
            const unsigned ea = i < 0xC0 ? 0x0057E960u + 0x180u + 4u * i : 0x00869510u;
            const unsigned w = wp52_be32(ea);
            if (!ginit) { gprev[i] = w; continue; }
            if (w == gprev[i]) continue;
            if (gn[i] < 255) gn[i]++;
            if (gn[i] <= 20 && nlog < 3000) {
                fprintf(stderr, "[F3DIAG] t=%llu G%s0x%03X 0x%08X -> 0x%08X (#%u scene=%d)\n", now - t0, i < 0xC0 ? "+" : " flags@0x869510 ",
                        i < 0xC0 ? 0x180u + 4u * i : 0u, gprev[i], w, gn[i], s47.active);
                nlog++;
            }
            gprev[i] = w;
        }
        ginit = 1;
    }
    o = wp52_uiobj();
    {   static unsigned long long t_raw;
        if (!o && now - t_raw > 5000ull) { t_raw = now; fprintf(stderr, "[F3DIAG] t=%llu UI object pointer *0x%08X = 0x%08X (not valid yet)\n", now - t0, WP52_UIOBJ_PTR, wp52_be32(WP52_UIOBJ_PTR)); fflush(stderr); } }
    if (!o) return;
    if (o != obj) {
        obj = o;
        wp52_watch_poll(o);
        for (unsigned i = 0; i < WP52_UIOBJ_SIZE / 4; i++) { prev[i] = wp52_be32(o + 4u * i); nchg[i] = 0; }
        fprintf(stderr, "[F3DIAG] t=%llu UI object at 0x%08X (+0x364=%u +0x390=%u +0x398=%u)\n", now - t0, o,
                prev[0x364 / 4], prev[0x390 / 4], prev[0x398 / 4]);
        fflush(stderr);
        return;
    }
    for (unsigned i = 1; i < WP52_UIOBJ_SIZE / 4; i++) {
        const unsigned w = wp52_be32(o + 4u * i);
        if (w == prev[i]) continue;
        if (nchg[i] < 255) nchg[i]++;
        if (nchg[i] <= 12 && nlog < 3000) {
            fprintf(stderr, "[F3DIAG] t=%llu +0x%03X 0x%08X -> 0x%08X (#%u, st620=%u scene=%d paused=%d)\n", now - t0, 4u * i,
                    prev[i], w, nchg[i], st, s47.active, s47.paused);
            nlog++;
        }
        prev[i] = w;
    }
    fflush(stderr);
}

/* ---- WINPORT_F3_TEST: timed player-input taps (bench / agent experiments) ---- */
typedef struct { char base; unsigned at, hold; u16 key; int snap; unsigned long long t_on; int done; } wp52_tap;
static wp52_tap s52_tap[32];
static int s52_ntap = -1;
static unsigned s52_quiet = 12000;
static int wp52_test_on(void)
{
    if (s52_ntap < 0) {
        const char* e = getenv("WINPORT_F3_TEST");
        const char* q = getenv("WINPORT_F3_TEST_QUIET_MS");
        static const struct { const char* n; u16 b; } keys[] = {
            {"select", CELL_PAD_CTRL_SELECT}, {"start", CELL_PAD_CTRL_START}, {"cross", CELL_PAD_CTRL_CROSS},
            {"circle", CELL_PAD_CTRL_CIRCLE}, {"triangle", CELL_PAD_CTRL_TRIANGLE}, {"square", CELL_PAD_CTRL_SQUARE},
            {"up", CELL_PAD_CTRL_UP}, {"down", CELL_PAD_CTRL_DOWN}, {"left", CELL_PAD_CTRL_LEFT}, {"right", CELL_PAD_CTRL_RIGHT} };
        s52_ntap = 0;
        if (q && *q) s52_quiet = (unsigned)atoi(q);
        while (e && *e && s52_ntap < 32) {
            wp52_tap t = { 's', 0, 150, 0, -1, 0, 0 };
            if (*e == 's' || *e == 'e' || *e == 'a') t.base = *e++;
            t.at = (unsigned)strtoul(e, (char**)&e, 10);
            if (*e == ':') {
                e++;
                if (!_strnicmp(e, "fakeon", 6)) { t.snap = 3; e += 6; }         /* test: treat gameplay as a movie scene */
                else if (!_strnicmp(e, "fakeoff", 7)) { t.snap = 4; e += 7; }
                else if (!_strnicmp(e, "pmev", 4)) { t.snap = 2; e += 4; }            /* write PauseMenu_Event = 1 (UI object +0x364) */
                else if (!_strnicmp(e, "snapopen", 8)) { t.snap = 1; e += 8; }        /* RAM snapshot, class "scene" (csprobe) */
                else if (!_strnicmp(e, "snapclosed", 10)) { t.snap = 0; e += 10; } /* RAM snapshot, class "game" */
                else for (unsigned k = 0; k < sizeof keys / sizeof keys[0]; k++) {
                    const size_t L = strlen(keys[k].n);
                    if (!_strnicmp(e, keys[k].n, L)) { t.key = keys[k].b; e += L; break; }
                }
                if (*e == ':') { e++; t.hold = (unsigned)strtoul(e, (char**)&e, 10); }
            }
            while (*e && *e != ',') e++;
            while (*e == ',') e++;
            if (t.key || t.snap >= 0) s52_tap[s52_ntap++] = t;
        }
        if (s52_ntap) { fprintf(stderr, "[F3TEST] %d timed player taps (quiet %u ms after e/a taps)\n", s52_ntap, s52_quiet); fflush(stderr); }
    }
    return s52_ntap > 0;
}
/* the no-controller branch of cellPadGetData, port 0: autoplay + test taps as player input -> data */
static void wp52_test_nohost(CellPadData* data, u32 setting)
{
    static unsigned long long t0, t_scene, t_end, t_quiet;
    static int nscene;   /* 's' taps skip scene #1 (the boot logo movie) */
    static u16 last_b, p52; static int s52;
    const unsigned long long now = wp47_ms();
    u16 autob = (u16)(data->button[CELL_PAD_BTN_OFFSET_DIGITAL1] | (data->button[CELL_PAD_BTN_OFFSET_DIGITAL2] << 8));
    u16 player = 0, mask, game;
    if (!t0) t0 = now;
    for (int i = 0; i < s52_ntap; i++) {   /* one-shot: a tap starts at the first poll after its time, lasts `hold` */
        wp52_tap* t = &s52_tap[i];
        const unsigned long long base = t->base == 's' ? (nscene >= 2 ? t_scene : 0ull) : t->base == 'e' ? t_end : t0;
        if (t->done) continue;
        if (!t->t_on) {
            if (!base || now < base + t->at) continue;
            t->t_on = now;
            if (t->snap == 3 || t->snap == 4) { s52n.fake = (t->snap == 3); fprintf(stderr, "[F3TEST] fake scene %s\n", s52n.fake ? "ON" : "off"); fflush(stderr); t->done = 1; continue; }
            if (t->snap == 2) {
                const unsigned o = wp52_uiobj();
                if (o) *(volatile unsigned*)(vm_base + o + 0x364u) = 0x01000000u;   /* one aligned store of big-endian 1 */
                fprintf(stderr, "[F3TEST] wrote PauseMenu_Event=1 (UI object 0x%08X, scene=%d)\n", o, s47.active); fflush(stderr);
                t->done = 1; continue;
            }
            if (t->snap >= 0) { wp49_probe_dump(t->snap, t->snap ? "F3 menu open" : "F3 menu closed"); t->done = 1; continue; }
        }
        if (now - t->t_on >= t->hold) { t->done = 1; continue; }
        player |= t->key;
        if (t->base != 's') t_quiet = now;
    }
    if (player != last_b) {
        fprintf(stderr, "[F3TEST] player buttons 0x%04X (t=%llu ms, scene=%d +%llu ms, since end %llu ms)\n", player,
                now - t0, s47.active, t_scene ? now - t_scene : 0ull, t_end ? now - t_end : 0ull);
        fflush(stderr);
        last_b = player;
    }
    mask = wp47_cutscene_ui(player, 0);   /* the real cutscene UI, as for a controller */
    if (s47.active && !t_scene) {
        t_scene = now; nscene++;
        for (int i = 0; i < s52_ntap; i++) if (s52_tap[i].base == 's') { s52_tap[i].t_on = 0; s52_tap[i].done = 0; }
    }
    if (!s47.active && t_scene) {   /* 'e' taps count from the end of a LONG scene (the New Game opener, not the logo) */
        if (now - t_scene > 20000ull) { t_end = now; fprintf(stderr, "[F3TEST] long scene ended -> 'e' taps count from now\n"); fflush(stderr); }
        t_scene = 0;
    }
    if (s47.active || pad_autostart_in_cutscene() || (t_quiet && now - t_quiet < s52_quiet)) autob = 0;
    game = (u16)(autob | (player & ~mask) | (wp49_native_inject() & 0x00FFu) | (wp81_inject() & 0x00FFu));   /* winport v81-pset-anywhere */
    data->button[CELL_PAD_BTN_OFFSET_DIGITAL1] = (u16)(game & 0x00FFu);
    data->button[CELL_PAD_BTN_OFFSET_DIGITAL2] = (u16)(game >> 8);
    if ((game & CELL_PAD_CTRL_CROSS) && (setting & CELL_PAD_SETTING_PRESS_ON)) data->button[CELL_PAD_BTN_OFFSET_PRESS_CROSS] = 255;
    data->len = pad_report_len(game, &p52, &s52, setting);
    data->button[0] = (u16)data->len;
}

/* ---- Part 3: WINPORT_CUTSCENE_NATIVE_PAUSE=2 -- SELECT in a movie = the game's OWN pause menu where the game can
 * open it, else the v47 host pause.  Facts (research/F3_movie_pausemenu.md):
 *  - the game-flow object G = 0x0057E960 (TOC-0x62B0) keeps the game state at G+0x1B8: 3 front end, 4 New-Game load
 *    (the introhud movie plays in this state), 11 (0xB) gameplay, 12 (0xC) pause menu, 13 (0xD) Power Up menu
 *    [run_f3_f3i.log];
 *  - only the gameplay update (func_000B6D3C, state 11) turns a SELECT press+release into the pause menu
 *    (G+0x234 latch, func_000B6FD8: PauseMenu_Event=1, state 12) [ppu_recomp_000.cpp:147172-147236];
 *  - in state 4 SELECT does nothing and even a host-raised PauseMenu_Event is never consumed (Flash pause UI not
 *    running) [run_f3_f3g / f3j] -> no game menu is possible there: host pause (picture + sound frozen).
 * In state 11 the SELECT goes to the game unchanged (press + release), the movie picture freezes, the renderer lays
 * the game's frame (its menu over the black placeholder movie) over the dimmed movie (v49 composite), and the movie
 * resumes when the game leaves state 12 (Continue, SELECT, START, Restart, Quit -- whatever closed it).  If the game
 * has not entered state 12 1.5 s after the SELECT release, it refused -> host pause. */
#define WP52_G_STATE 0x0057EB18u   /* G (0x0057E960) + 0x1B8 */
static int wp52_mode(void)
{
    if (s52n.mode < 0) {
        const char* e = getenv("WINPORT_CUTSCENE_NATIVE_PAUSE");
        s52n.mode = (e && e[0] == '2') ? 2 : 0;
        if (s52n.mode == 2) { fprintf(stderr, "[F3] SELECT in a movie = the game's own pause menu when the game is in gameplay state, else host pause\n"); fflush(stderr); }
    }
    return s52n.mode;
}
static int wp52_native_core(u16 btns, u16 rise, unsigned long long now)
{
    unsigned st;
    if (wp52_mode() != 2) return 0;
    if (!s52n.on) {
        if (!(rise & CELL_PAD_CTRL_SELECT) || s47.paused) return 0;
        st = wp52_be32(WP52_G_STATE);
        if (st != 0xBu) {
            fprintf(stderr, "[F3] SELECT in a movie with the game in state %u (not gameplay 11): its pause menu cannot open here -> host pause\n", st);
            fflush(stderr);
            return 0;   /* the v47 code below pauses picture + sound */
        }
        s52n.on = 1; s52n.seen = 0; s52n.t0 = now; s52n.t_rel = 0;
        s49n.on_pause = 1; s47.paused = 1;
        movie_wp47_set_paused(s47.ov);
        g_wp47_audio_hold = 0;   /* the game must run to show its menu */
        fprintf(stderr, "[F3] SELECT passed to the game (state 11): waiting for its pause menu, movie picture frozen (overlay=%d)\n", s47.ov);
        fflush(stderr);
        return 1;
    }
    st = wp52_be32(WP52_G_STATE);
    if (!(btns & CELL_PAD_CTRL_SELECT) && !s52n.t_rel) s52n.t_rel = now;
    if (st == 0xCu) {
        if (!s52n.seen) { s52n.seen = 1; fprintf(stderr, "[F3] the game's pause menu is OPEN (state 12) %llu ms after SELECT\n", now - s52n.t0); fflush(stderr); }
        return 1;
    }
    if (s52n.seen) {
        s52n.on = 0; s49n.on_pause = 0; s47.paused = 0;
        movie_wp47_set_paused(0);
        fprintf(stderr, "[F3] the game's pause menu CLOSED (state %u) after %.1f s -> movie resumes\n", st, (double)(now - s52n.t0) / 1000.0);
        fflush(stderr);
        return 1;
    }
    if (s52n.t_rel && now - s52n.t_rel > 1500ull) {
        s52n.on = 0; s49n.on_pause = 0; s47.paused = 0;
        fprintf(stderr, "[F3] the game did not open its pause menu 1.5 s after SELECT (state %u) -> host pause\n", st);
        fflush(stderr);
        wp47_set_pause(1, "SELECT, game refused its menu");
        return 0;
    }
    return 1;
}
#else
static void wp52_fix_len(CellPadData* d, u32 setting) { (void)d; (void)setting; }
static void wp52_diag_poll(void) {}
static int wp52_test_on(void) { return 0; }
static void wp52_test_nohost(CellPadData* data, u32 setting) { (void)data; (void)setting; }
#endif
/* ---- end winport v52-movie-pausemenu ---- */

/* Player asked to cut the cutscene short (START/CROSS). The movie ends as a
 * normal EOS, so the guest's state machine moves on; PS3_MOVIE_SKIP=0 off. */
static void pad_movie_skip(u16 btns, int synthetic)
{
#if defined(_WIN32)   /* winport v30-bench: Windows skip path, opt-in */
    extern int movie_vt_request_skip(void);   /* movie_hle.c; the weak decl above is macOS-only */
    static int won = -1, autoskip = -1; static u16 wprev;
    if (won < 0) { const char* e = getenv("WINPORT_MOVIE_SKIP"); const char* a = getenv("WINPORT_AUTOSKIP");
                   autoskip = (a && a[0] == '1'); won = autoskip || (e && e[0] == '1');
                   fprintf(stderr, "[movie] Windows button skip %s (autoskip=%d)\n", won ? "ON" : "off", autoskip); fflush(stderr); }
    if ((!autoskip && wp47_on()) || wp49_test_on()) { wprev = btns; return; }   /* winport v49-cutscene: test mode = no old skip */
    if (0)   /* winport v47-cutscene-ui: hold-START UI replaces this skip; X never skips */
    if (won && pad_movie_skip_should_fire(wprev, btns, 1, synthetic && !autoskip && pad_autostart_in_cutscene())
        && movie_vt_request_skip()) {
        fprintf(stderr, "[movie] skip requested by button (btns=0x%04X%s)\n", btns, synthetic ? ", autoplay" : "");
        fflush(stderr);
    }
    wprev = btns;
#elif defined(__APPLE__)
    static int on = -1;
    static u16 prev;
    if (on < 0) { const char* e = getenv("PS3_MOVIE_SKIP"); on = (e && e[0] == '0') ? 0 : 1; }
    /* So' o pad AUTOMATICO e' travado dentro de um filme; o jogador carrega
     * quando quiser. Antes travava-se pela cutscene e nao pela origem do
     * toque, logo carregar no botao durante uma cutscene nao fazia nada. */
    if (pad_movie_skip_should_fire(prev, btns, on, synthetic && pad_autostart_in_cutscene())
        && movie_vt_request_skip && movie_vt_request_skip()) {
        fprintf(stderr, "[movie] skip pedido pelo botao (btns=0x%04X)\n", btns);
        fflush(stderr);
    }
    prev = btns;
#else
    (void)btns; (void)synthetic;
#endif
}

static void pad_trace_in(u32 port_no, u16 btns, s32 len)
{
    static int on = -1;
    if (on < 0) { const char* e = getenv("PS3_TRACE_PAD_IN"); on = (e && e[0] && e[0] != '0') ? 1 : 0; }
    if (!on) return;
    static u16 prev[CELL_PAD_MAX_PORT_NUM];
    static unsigned reads, last_sec;
    const unsigned sec = pad_autostart_elapsed_ms() / 1000u;
    reads++;
    if (port_no < CELL_PAD_MAX_PORT_NUM && btns != prev[port_no]) {
        prev[port_no] = btns;
        fprintf(stderr, "[PADIN] port=%u btns=0x%04X len=%d t=%us\n",
                port_no, btns, (int)len, sec);
        fflush(stderr);
    }
    if (sec != last_sec) {
        fprintf(stderr, "[PADIN] t=%us leituras=%u\n", sec, reads);
        fflush(stderr);
        last_sec = sec; reads = 0;
    }
}

static void pad_data_to_guest(u32 gaddr, const CellPadData* d)
{
    vm_write32(gaddr + 0, (u32)d->len);
    for (int i = 0; i < CELL_PAD_MAX_CODES; i++)
        vm_write16(gaddr + 4 + (u32)i * 2, d->button[i]);
}

/* winport v56-padscript: WINPORT_PADSCRIPT state-driven autoplay (see engine_fixes_v56_padscript.py) */
typedef struct { char kind; u16 btn; unsigned arg; char tok[16]; } Wp56Step;
static Wp56Step s56[64]; static int s56_n = -1, s56_i; static unsigned s56_t0, s56_period = 2000, s56_last_st = 0xFFFFFFFFu;
static u16 wp56_btn(const char* t, int len)
{
    static const struct { const char* n; u16 b; } k[] = {
        {"X", CELL_PAD_CTRL_CROSS}, {"O", CELL_PAD_CTRL_CIRCLE}, {"SQ", CELL_PAD_CTRL_SQUARE}, {"TR", CELL_PAD_CTRL_TRIANGLE},
        {"ST", CELL_PAD_CTRL_START}, {"SE", CELL_PAD_CTRL_SELECT}, {"U", CELL_PAD_CTRL_UP}, {"D", CELL_PAD_CTRL_DOWN},
        {"L", CELL_PAD_CTRL_LEFT}, {"R", CELL_PAD_CTRL_RIGHT}, {"L1", CELL_PAD_CTRL_L1}, {"R1", CELL_PAD_CTRL_R1},
        {"L2", CELL_PAD_CTRL_L2}, {"R2", CELL_PAD_CTRL_R2} };
    for (unsigned i = 0; i < sizeof k / sizeof k[0]; i++)
        if ((int)strlen(k[i].n) == len && !strncmp(k[i].n, t, (size_t)len)) return k[i].b;
    return 0;
}
static int wp56_on(void)
{
    if (s56_n >= 0) return s56_n > 0;
    s56_n = 0;
    const char* e = getenv("WINPORT_PADSCRIPT");
    if (!e || !*e) return 0;
    const char* pe = getenv("WINPORT_PADSCRIPT_PERIOD"); if (pe && atoi(pe) > 0) s56_period = (unsigned)atoi(pe);
    while (*e && s56_n < 64) {
        const char* end = strchr(e, ','); int len = end ? (int)(end - e) : (int)strlen(e);
        Wp56Step* s = &s56[s56_n]; memset(s, 0, sizeof *s);
        snprintf(s->tok, sizeof s->tok, "%.*s", len < 15 ? len : 15, e);
        const char* tl = strchr(s->tok, '~');
        if (s->tok[0] == 'w' && s->tok[1] >= '0' && s->tok[1] <= '9') { s->kind = 'w'; s->arg = (unsigned)atoi(s->tok + 1); }
        else if (s->tok[0] == 's' && s->tok[1] >= '0' && s->tok[1] <= '9') { s->kind = 's'; s->arg = (unsigned)atoi(s->tok + 1); }
        else if (tl && tl[1] == 's') { s->kind = 'p'; s->btn = wp56_btn(s->tok, (int)(tl - s->tok)); s->arg = (unsigned)atoi(tl + 2); }
        else { s->kind = 't'; s->btn = wp56_btn(s->tok, (int)strlen(s->tok)); }
        if ((s->kind == 't' || s->kind == 'p') && !s->btn) fprintf(stderr, "[PADSCRIPT] bad step '%s' (ignored)\n", s->tok);
        else s56_n++;
        e += len; if (*e == ',') e++;
    }
    fprintf(stderr, "[PADSCRIPT] %d steps: %s\n", s56_n, getenv("WINPORT_PADSCRIPT")); fflush(stderr);
    return s56_n > 0;
}
static u16 wp56_buttons(void)
{
    const unsigned now = pad_autostart_elapsed_ms();
    const unsigned st = wp52_be32(WP52_G_STATE);
    if (st != s56_last_st) { fprintf(stderr, "[GSTATE] t=%ums state=%d->%u\n", now, (int)s56_last_st, st); fflush(stderr); s56_last_st = st; }
    for (int guard = 0; guard < 8 && s56_i < s56_n; guard++) {
        Wp56Step* s = &s56[s56_i];
        const unsigned dt = now - s56_t0;
        int done = 0; u16 out = 0;
        switch (s->kind) {
        case 'w': done = dt >= s->arg; break;
        case 's': done = st == s->arg; break;
        case 't': if (dt < 150) out = s->btn; else if (dt >= 500) done = 1; break;
        case 'p': if (st == s->arg && (dt % s56_period) >= 500) done = 1; else if ((dt % s56_period) < 150) out = s->btn; break;
        }
        if (!done) return out;
        s56_i++; s56_t0 = now;
        fprintf(stderr, "[PADSCRIPT] t=%ums step %d '%s' done state=%u%s\n", now, s56_i, s->tok, st,
                s56_i == s56_n ? " -- script finished, pad idle" : ""); fflush(stderr);
    }
    return 0;
}
/* PS3_PAD_AUTOSTART bring-up aid: pad_autostart_buttons() every 200 GetData
 * calls on port 0 (START, TRIANGLE, DOWN, CROSS). `host` = a host device also
 * sits on port 0 (traced only). PS3_TRACE_PAD prints each tap edge (cap 64). */
static u16 pad_autostart_taps(int host)
{
    static int on = -1, trace = -1;
    if (on < 0) on = pad_autostart_env_on();
    if (!on) return 0;
    static unsigned calls = 0;
    const unsigned idx = calls++;
    const u16 b = wp56_on() ? wp56_buttons() : pad_autostart_buttons(idx);   /* winport v56-padscript */
    if (trace < 0) trace = getenv("PS3_TRACE_PAD") ? 1 : 0;
    if (trace) {
        static u16 prev = 0;
        static int n = 0;
        if (b && b != prev && n < 512) {
            const char* name = (b == CELL_PAD_CTRL_START)    ? "START" :
                               (b == CELL_PAD_CTRL_TRIANGLE) ? "TRIANGLE" :
                               (b == CELL_PAD_CTRL_DOWN)     ? "DOWN" :
                               (b == CELL_PAD_CTRL_CROSS)    ? "CROSS" :
                               (b == CELL_PAD_CTRL_SQUARE)   ? "SQUARE" :
                               (b == CELL_PAD_CTRL_CIRCLE)   ? "CIRCLE" : "?";
            fprintf(stderr, "[PADTAP] t=%ums call#%u %s host_on_port0=%d\n",
                    pad_autostart_elapsed_ms(), calls, name, host);
            fflush(stderr);
            n++;
        }
        prev = b;
    }
    return b;
}

#if PAD_BACKEND_XINPUT   /* winport: mingw */
static void winport_pad_diag(int is_info2)
{
    static ULONGLONG last = 0; static unsigned n_info2, n_data; static u16 seen_buttons;
    if (is_info2) n_info2++; else n_data++;
    seen_buttons |= s_host_state[0].buttons;
    ULONGLONG now = GetTickCount64();
    if (!last) last = now;
    if (now - last >= 5000) {
        fprintf(stderr, "[PADDIAG] last %llums: GetInfo2=%u GetData=%u | port0 connected=%d device=%d buttons_seen=0x%04X\n",
                (unsigned long long)(now - last), n_info2, n_data, s_host_state[0].connected,
                (int)s_host_state[0].device, (unsigned)seen_buttons);
        fflush(stderr);
        last = now; n_info2 = n_data = 0; seen_buttons = 0;
    }
}
#else
#define winport_pad_diag(x) ((void)0)
#endif
s32 cellPadGetData(u32 port_no, CellPadData* data_guest)
{
    winport_pad_diag(0);
    { static int n = 0; if (n < 5) { n++;
        extern unsigned long long ps3_ms(void);
        fprintf(stderr, "[PADPOLL t=%llums] cellPadGetData port=%u #%d (game IS polling pad)\n",
                ps3_ms(), port_no, n); } }
    if (!s_pad_initialized)
        return CELL_PAD_ERROR_NOT_OPENED;

    if (port_no >= s_max_connect || !data_guest)
        return CELL_PAD_ERROR_INVALID_PARAMETER;

    u32 gaddr = (u32)(uintptr_t)data_guest;
    CellPadData local;
    CellPadData* data = &local;
    memset(data, 0, sizeof(CellPadData));

    /* Poll fresh state */
    pad_poll_backend();

    if (port_no >= PAD_MAX_HOST_PORTS || !s_host_state[port_no].connected) {
        data->len = 0;
        /* Bring-up aid (PS3_PAD_AUTOSTART): with no host controller, report a
         * virtual pad that taps START / TRIANGLE / DOWN / CROSS (~8 frames
         * each, every ~3 s) so logos, Power-Up Back, and New Game advance. */
        { static int autostart = -1;
          if (autostart < 0) autostart = pad_autostart_env_on();
          if (autostart && port_no == 0) {
              const u16 taps = (u16)(pad_autostart_taps(0) | wp94_inject());   /* winport v94b-ckpt-music */
              /* digital2 carries L2..SQUARE in its LOW byte (CROSS = 0x40). */
              const u16 btn1 = (u16)(taps & 0x00FFu), btn2 = (u16)(taps >> 8);
              u32 setting = s_port_setting[port_no];
              static u16 prev_btns[CELL_PAD_MAX_PORT_NUM];
              static int seen_btns[CELL_PAD_MAX_PORT_NUM];
              data->len = pad_report_len(taps, &prev_btns[port_no],
                                         &seen_btns[port_no], setting);
              data->button[0] = (u16)data->len;
              data->button[CELL_PAD_BTN_OFFSET_DIGITAL1] = btn1;
              data->button[CELL_PAD_BTN_OFFSET_DIGITAL2] = btn2;
              data->button[CELL_PAD_BTN_OFFSET_ANALOG_RIGHT_X] = 0x80;
              data->button[CELL_PAD_BTN_OFFSET_ANALOG_RIGHT_Y] = 0x80;
              data->button[CELL_PAD_BTN_OFFSET_ANALOG_LEFT_X]  = 0x80;
              data->button[CELL_PAD_BTN_OFFSET_ANALOG_LEFT_Y]  = 0x80;
              if (setting & CELL_PAD_SETTING_PRESS_ON) {
                  /* E465: a lista tinha so' DOWN/TRIANGLE/CROSS, os glifos que o
                   * autostart usava para navegar os menus. Com a rotacao de
                   * combate isso virou um bug visivel: o God of War le' os
                   * botoes por PRESSAO, portanto o bit digital do SQUARE era
                   * posto mas a pressao ficava a 0 e o ataque era ignorado --
                   * o Kratos so' pulava, porque CROSS estava na lista.
                   * Tabela completa, para nenhum glifo novo voltar a cair fora. */
                  static const struct { u16 bit; int off; } press[] = {
                      { CELL_PAD_CTRL_UP,       CELL_PAD_BTN_OFFSET_PRESS_UP },
                      { CELL_PAD_CTRL_DOWN,     CELL_PAD_BTN_OFFSET_PRESS_DOWN },
                      { CELL_PAD_CTRL_LEFT,     CELL_PAD_BTN_OFFSET_PRESS_LEFT },
                      { CELL_PAD_CTRL_RIGHT,    CELL_PAD_BTN_OFFSET_PRESS_RIGHT },
                      { CELL_PAD_CTRL_TRIANGLE, CELL_PAD_BTN_OFFSET_PRESS_TRIANGLE },
                      { CELL_PAD_CTRL_CIRCLE,   CELL_PAD_BTN_OFFSET_PRESS_CIRCLE },
                      { CELL_PAD_CTRL_CROSS,    CELL_PAD_BTN_OFFSET_PRESS_CROSS },
                      { CELL_PAD_CTRL_SQUARE,   CELL_PAD_BTN_OFFSET_PRESS_SQUARE },
                      { CELL_PAD_CTRL_L1,       CELL_PAD_BTN_OFFSET_PRESS_L1 },
                      { CELL_PAD_CTRL_R1,       CELL_PAD_BTN_OFFSET_PRESS_R1 },
                  };
                  for (unsigned k = 0; k < sizeof press / sizeof press[0]; k++)
                      if (taps & press[k].bit)
                          data->button[press[k].off] = 255;
              }
          } }
        pad_trace_in(port_no,
                     (u16)(data->button[CELL_PAD_BTN_OFFSET_DIGITAL1] |
                           (data->button[CELL_PAD_BTN_OFFSET_DIGITAL2] << 8)),
                     (s32)data->len);
        /* winport v30-bench: this no-host-device branch returned before pad_movie_skip, so the autoplay's
         * presses could never skip a movie when controller + keyboard are off (the bench setup) */
        if (port_no == 0) wp49_probe_poll();   /* winport v49-cutscene */
        if (port_no == 0) wp51_poll((u16)(data->button[CELL_PAD_BTN_OFFSET_DIGITAL1] | (data->button[CELL_PAD_BTN_OFFSET_DIGITAL2] << 8)));   /* winport v51-rt-cutscene */
        if (port_no == 0) wp52_diag_poll();   /* winport v52-movie-pausemenu */
        if (port_no == 0 && wp52_test_on()) wp52_test_nohost(data, s_port_setting[port_no]);   /* winport v52-movie-pausemenu: F3 test taps */
        else
        if (port_no == 0 && wp49_test_on()) {   /* winport v49-cutscene: test injection on the autoplay path */
            static u16 p49; static int s49;
            u16 cur = (u16)(data->button[CELL_PAD_BTN_OFFSET_DIGITAL1] | (data->button[CELL_PAD_BTN_OFFSET_DIGITAL2] << 8));
            (void)wp47_cutscene_ui(cur, 1);   /* keeps the scene state current (synthetic: no UI) */
            if (s47.active) cur = 0;          /* the autoplay's own presses stay out of scenes */
            cur |= wp49_test_buttons();
            data->button[CELL_PAD_BTN_OFFSET_DIGITAL1] = (u16)(cur & 0x00FFu);
            data->button[CELL_PAD_BTN_OFFSET_DIGITAL2] = (u16)(cur >> 8);
            data->len = pad_report_len(cur, &p49, &s49, s_port_setting[port_no]);
            data->button[0] = (u16)data->len;
        }
        if (port_no == 0) pad_movie_skip((u16)(data->button[CELL_PAD_BTN_OFFSET_DIGITAL1] |
                                               (data->button[CELL_PAD_BTN_OFFSET_DIGITAL2] << 8)), 1);
        pad_data_to_guest(gaddr, data);
        return CELL_OK;
    }

    PadHostState* hs = &s_host_state[port_no];
    u32 setting = s_port_setting[port_no];
    u16 guest_button_map[CELL_PAD_HOST_BUTTON_COUNT];
    const u16* map = NULL;   /* NULL = identity: no copy, no translation */
    u8 analog_deadzone;

    /* Fast path: with the identity map (the default) translating is a no-op,
     * so the map is not copied; flag, map and deadzone come from ONE
     * publication. */
    if (!cellpad_mapping_snapshot(&s_mapping, guest_button_map, &analog_deadzone,
                                  CELL_PAD_HOST_DEADZONE_DEFAULT))
        map = guest_button_map;

    data->button[1] = 0; /* reserved */

    /* Digital buttons. The host mask uses the combined 16-bit layout of cellPad.h
     * (SELECT..LEFT = bits 0-7, L2..SQUARE = bits 8-15); CellPadData carries each
     * half in the LOW byte of its own word (digital2 CROSS = 0x40, as in libpad and
     * RPCS3). The old `buttons & 0xFF00` put CROSS at 0x4000, a bit the title never
     * tests, so no face/shoulder button (keyboard, controller or autostart) arrived. */
    u16 raw_host_buttons = hs->buttons;
    u16 host_buttons = raw_host_buttons;
    u16 btns;
    u32 axes;
    int autostart_own = 0;
    /* PS3_PAD_AUTOSTART on a windowed Mac: GCKeyboard/GCMouse mark port 0
     * connected. OR-ing them with the TAS (old path) let mouse-look walk
     * Flash off NEW GAME and RMB/TRIANGLE Back the title. E420's only
     * successful Colossus enqueue was host_on_port0=0. Own the port. */
    { static int autostart = -1;
      if (autostart < 0) autostart = pad_autostart_env_on();
      static int took_over = 0;
      if (autostart && port_no == 0 && !took_over &&
          pad_autostart_user_took_over(&took_over, hs->buttons)) {
          fprintf(stderr, "[PAD] input real na porta 0: pad automatico desligado\n");
          fflush(stderr);
      }
      if (autostart && port_no == 0 && !took_over) {
          btns = pad_autostart_taps(1);
          autostart_own = 1;
      } }
    u32 frame_axes = 0x80808080u;
    if (autostart_own) {
        if (port_no == 0)
            cellpad_overlay_home(hs->connected && hs->host_home); /* PS/Home still opens the menu */
    } else {
        /* The shell's own fast path skips the overlay filter while it is
         * closed and idle (cellpad_overlay_port_frame_path). */
        cellpad_overlay_port_frame_path(port_no, hs, map, analog_deadzone,
                                        &host_buttons, &btns, &frame_axes, 1);
    }
    {
        static u16 prev_btns[CELL_PAD_MAX_PORT_NUM];
        static u32 prev_axes[CELL_PAD_MAX_PORT_NUM];
        static int seen_btns[CELL_PAD_MAX_PORT_NUM];
        /* The sticks count too: with buttons alone, releasing W reported "no
         * change" and the game kept walking on the last stick sample. */
        axes = autostart_own ? 0x80808080u : frame_axes;
        data->len = pad_report_len_axes(btns, axes, &prev_btns[port_no],
                                        &prev_axes[port_no], &seen_btns[port_no],
                                        setting);
        data->button[0] = (u16)data->len;
    }
    data->button[CELL_PAD_BTN_OFFSET_DIGITAL1] = (u16)(btns & 0x00FFu);
    data->button[CELL_PAD_BTN_OFFSET_DIGITAL2] = (u16)(btns >> 8);

    /* Analog sticks: the same values the report length was computed from
     * (neutral while autostart owns the port or the overlay menu is open). */
    data->button[CELL_PAD_BTN_OFFSET_ANALOG_RIGHT_X] = (u16)(axes & 0xFFu);
    data->button[CELL_PAD_BTN_OFFSET_ANALOG_RIGHT_Y] = (u16)((axes >> 8) & 0xFFu);
    data->button[CELL_PAD_BTN_OFFSET_ANALOG_LEFT_X]  = (u16)((axes >> 16) & 0xFFu);
    data->button[CELL_PAD_BTN_OFFSET_ANALOG_LEFT_Y]  = (u16)((axes >> 24) & 0xFFu);

    /* Pressure-sensitive buttons (only meaningful if PRESS_ON) */
    if (setting & CELL_PAD_SETTING_PRESS_ON) {
        if (autostart_own) {
            if (btns & CELL_PAD_CTRL_DOWN)
                data->button[CELL_PAD_BTN_OFFSET_PRESS_DOWN] = 255;
            if (btns & CELL_PAD_CTRL_TRIANGLE)
                data->button[CELL_PAD_BTN_OFFSET_PRESS_TRIANGLE] = 255;
            if (btns & CELL_PAD_CTRL_CROSS)
                data->button[CELL_PAD_BTN_OFFSET_PRESS_CROSS] = 255;
        } else {
            pad_set_host_pressure(data, raw_host_buttons, host_buttons, map, CELL_PAD_CTRL_RIGHT,
                                  hs->press_right, 0);
            pad_set_host_pressure(data, raw_host_buttons, host_buttons, map, CELL_PAD_CTRL_LEFT,
                                  hs->press_left, 0);
            pad_set_host_pressure(data, raw_host_buttons, host_buttons, map, CELL_PAD_CTRL_UP,
                                  hs->press_up, 0);
            pad_set_host_pressure(data, raw_host_buttons, host_buttons, map, CELL_PAD_CTRL_DOWN,
                                  hs->press_down, 1);
            pad_set_host_pressure(data, raw_host_buttons, host_buttons, map, CELL_PAD_CTRL_TRIANGLE,
                                  hs->press_triangle, 1);
            pad_set_host_pressure(data, raw_host_buttons, host_buttons, map, CELL_PAD_CTRL_CIRCLE,
                                  hs->press_circle, 0);
            pad_set_host_pressure(data, raw_host_buttons, host_buttons, map, CELL_PAD_CTRL_CROSS,
                                  hs->press_cross, 1);
            pad_set_host_pressure(data, raw_host_buttons, host_buttons, map, CELL_PAD_CTRL_SQUARE,
                                  hs->press_square, 0);
            pad_set_host_pressure(data, raw_host_buttons, host_buttons, map, CELL_PAD_CTRL_L1,
                                  hs->press_l1, 0);
            pad_set_host_pressure(data, raw_host_buttons, host_buttons, map, CELL_PAD_CTRL_R1,
                                  hs->press_r1, 0);
            pad_set_host_pressure(data, raw_host_buttons, host_buttons, map, CELL_PAD_CTRL_L2,
                                  hs->trigger_l2, 0);
            pad_set_host_pressure(data, raw_host_buttons, host_buttons, map, CELL_PAD_CTRL_R2,
                                  hs->trigger_r2, 0);
        }
    }

    /* Sensor data (only meaningful if SENSOR_ON) */
    if (setting & CELL_PAD_SETTING_SENSOR_ON) {
        /* Default sensor values: accelerometer at rest (512 = 1g center) */
        data->button[CELL_PAD_BTN_OFFSET_SENSOR_X] = 512;
        data->button[CELL_PAD_BTN_OFFSET_SENSOR_Y] = 399; /* gravity */
        data->button[CELL_PAD_BTN_OFFSET_SENSOR_Z] = 512;
        data->button[CELL_PAD_BTN_OFFSET_SENSOR_G] = 512;
    }

    pad_trace_in(port_no, btns, (s32)data->len);
    if (port_no == 0) wp51_poll(btns);   /* winport v51-rt-cutscene: detector / learn mode (before the v47 UI) */
    if (port_no == 0) {   /* winport v47-cutscene-ui: the cutscene UI owns START / SELECT (and X) while a movie plays */
        const u16 m47 = wp47_cutscene_ui(btns, autostart_own);
        if (m47) {
            data->button[CELL_PAD_BTN_OFFSET_DIGITAL1] &= (u16)~(m47 & 0x00FFu);
            data->button[CELL_PAD_BTN_OFFSET_DIGITAL2] &= (u16)~(m47 >> 8);
            if (m47 & CELL_PAD_CTRL_CROSS) data->button[CELL_PAD_BTN_OFFSET_PRESS_CROSS] = 0;
        }
    }
    if (port_no == 0) {   /* winport v49-cutscene: probe keys + test injection (after the v47 mask) */
        wp49_probe_poll();
        if (wp49_test_on()) data->button[CELL_PAD_BTN_OFFSET_DIGITAL1] |= (u16)(wp49_test_buttons() & 0x00FFu);
        data->button[CELL_PAD_BTN_OFFSET_DIGITAL1] |= (u16)(wp49_native_inject() & 0x00FFu);   /* winport v49-native-pause */
        data->button[CELL_PAD_BTN_OFFSET_DIGITAL1] |= (u16)(wp81_inject() & 0x00FFu);   /* winport v81-pset-anywhere */
    }
    if (port_no == 0) { wp52_diag_poll(); wp52_fix_len(data, setting); }   /* winport v52-movie-pausemenu: injected keys get a 'changed' length */
    if (port_no == 0) pad_movie_skip(btns, autostart_own);
    pad_data_to_guest(gaddr, data);
    return CELL_OK;
}

s32 cellPadGetInfo2(CellPadInfo2* info_guest)
{
    winport_pad_diag(1);   /* winport: mingw */
    if (!s_pad_initialized)
        return CELL_PAD_ERROR_NOT_OPENED;

    if (!info_guest)
        return CELL_PAD_ERROR_INVALID_PARAMETER;

    /* Poll to get latest connection state */
    pad_poll_backend();

    u32 gaddr = (u32)(uintptr_t)info_guest;
    CellPadInfo2 local;
    CellPadInfo2* info = &local;
    memset(info, 0, sizeof(CellPadInfo2));
    info->max_connect = s_max_connect;

    /* With PS3_PAD_AUTOSTART a virtual pad occupies port 0 even without a
     * host controller, so the game accepts input from cellPadGetData. */
    static int autostart = -1;
    if (autostart < 0) autostart = pad_autostart_env_on();

    u32 connected = 0;
    for (u32 i = 0; i < s_max_connect && i < PAD_MAX_HOST_PORTS; i++) {
        if (s_host_state[i].connected || (autostart && i == 0)) {
            info->port_status[i]       = CELL_PAD_STATUS_CONNECTED;
            info->port_setting[i]      = s_port_setting[i];
            info->device_capability[i] = CELL_PAD_CAPABILITY_PS3_CONFORMITY
                                       | CELL_PAD_CAPABILITY_PRESS_MODE
                                       | CELL_PAD_CAPABILITY_SENSOR_MODE
                                       | CELL_PAD_CAPABILITY_HP_ANALOG_STICK
                                       | CELL_PAD_CAPABILITY_ACTUATOR;
            info->device_type[i]       = CELL_PAD_DEV_TYPE_STANDARD;
            connected++;
        } else {
            info->port_status[i] = CELL_PAD_STATUS_DISCONNECTED;
        }
    }
    info->now_connect = connected;

    /* CellPadInfo2 is all u32 fields: store each big-endian to the guest. */
    { const u32* src = (const u32*)info;
      for (u32 w = 0; w < (u32)(sizeof(CellPadInfo2) / 4); w++)
          vm_write32(gaddr + w * 4, src[w]); }

    return CELL_OK;
}

s32 cellPadSetPortSetting(u32 port_no, u32 port_setting)
{
    printf("[cellPad] SetPortSetting(port=%u, setting=0x%X)\n",
           port_no, port_setting);

    if (!s_pad_initialized)
        return CELL_PAD_ERROR_NOT_OPENED;

    if (port_no >= CELL_PAD_MAX_PORT_NUM)
        return CELL_PAD_ERROR_INVALID_PARAMETER;

    s_port_setting[port_no] = port_setting;
    return CELL_OK;
}

/* Mesmo defeito que `cellPadSetActDirect` (ver a nota lá): `info` chega como EA
 * do guest, e o `memset` + a escrita directa iam para um ponteiro host inválido.
 * Aqui ainda não houve crash observado porque o jogo só chama esta função
 * depois — mas o defeito é idêntico e cai no mesmo endereço de stack guest.
 * `CellPadCapabilityInfo` é só `u32[]`: escreve-se big-endian, word a word,
 * exactamente como `cellPadGetInfo2` já faz. */
s32 cellPadGetCapabilityInfo(u32 port_no, CellPadCapabilityInfo* info_guest)
{
    if (!s_pad_initialized)
        return CELL_PAD_ERROR_NOT_OPENED;

    if (port_no >= CELL_PAD_MAX_PORT_NUM || !info_guest)
        return CELL_PAD_ERROR_INVALID_PARAMETER;

    u32 gaddr = (u32)(uintptr_t)info_guest;
    CellPadCapabilityInfo local;
    CellPadCapabilityInfo* info = &local;
    memset(info, 0, sizeof(CellPadCapabilityInfo));

    /* Report standard DualShock 3 capabilities */
    info->info[0] = CELL_PAD_CAPABILITY_PS3_CONFORMITY
                   | CELL_PAD_CAPABILITY_PRESS_MODE
                   | CELL_PAD_CAPABILITY_SENSOR_MODE
                   | CELL_PAD_CAPABILITY_HP_ANALOG_STICK
                   | CELL_PAD_CAPABILITY_ACTUATOR;

    { const u32* src = (const u32*)info;
      for (u32 w = 0; w < (u32)(sizeof(CellPadCapabilityInfo) / 4); w++)
          vm_write32(gaddr + w * 4, src[w]); }

    return CELL_OK;
}

/* CORRECÇÃO 2026-08-02 (crash real, reportado pelo utilizador):
 *
 *   EXC_BAD_ACCESS (SIGSEGV) KERN_INVALID_ADDRESS at 0x000000000feff741
 *   0  cellPadSetActDirect + 52
 *   1  ps3_hle_call
 *   ...
 *   16 func_00242C94   <- o LOOP PRINCIPAL do jogo
 *
 * `param` chega como **endereço EA do guest** (0x0FEFF740 — a stack do guest
 * vive em 0x0FEFxxxx), não como ponteiro host. Desreferenciá-lo directamente
 * lê o endereço host 0x0FEFF741, que não está mapeado.
 *
 * As funções vizinhas já tratam isto — `cellPadGetData(.., CellPadData*
 * data_guest)` e `cellPadGetInfo2(CellPadInfo2* info_guest)` nomeiam o
 * parâmetro `_guest` e convertem com `u32 gaddr = (u32)(uintptr_t)p;` antes de
 * usar `vm_read*`/`vm_write*`. Estas duas (SetActDirect e GetCapabilityInfo)
 * ficaram sem a conversão.
 *
 * `CellPadActParam` é `u8 motor[CELL_PAD_ACTUATOR_MAX]` — bytes, sem
 * byte-swap: lê-se com `vm_read8`, um por índice. */
s32 cellPadSetActDirect(u32 port_no, CellPadActParam* param_guest)
{
    if (!s_pad_initialized)
        return CELL_PAD_ERROR_NOT_OPENED;

    if (port_no >= CELL_PAD_MAX_PORT_NUM || !param_guest)
        return CELL_PAD_ERROR_INVALID_PARAMETER;

    u32 gaddr = (u32)(uintptr_t)param_guest;
    CellPadActParam local;
    CellPadActParam* param = &local;
    for (u32 i = 0; i < (u32)CELL_PAD_ACTUATOR_MAX; i++)
        local.motor[i] = vm_read8(gaddr + i);
    cellPad_gate_actuators(local.motor, CELL_PAD_ACTUATOR_MAX,
                           atomic_load_explicit(&s_host_vibration, memory_order_relaxed));

#if PAD_BACKEND_XINPUT
    /* Map to XInput vibration */
    if (port_no < PAD_MAX_HOST_PORTS && s_host_state[port_no].connected) {
        XINPUT_VIBRATION vib;
        vib.wLeftMotorSpeed  = (WORD)(param->motor[CELL_PAD_ACTUATOR_PARAM_LARGE] * 257);
        vib.wRightMotorSpeed = (WORD)(param->motor[CELL_PAD_ACTUATOR_PARAM_SMALL] * 257);
        XInputSetState((DWORD)port_no, &vib);
    }
#endif

#if PAD_BACKEND_SDL2
    if (port_no < PAD_MAX_HOST_PORTS && s_sdl_controllers[port_no]) {
        SDL_GameControllerRumble(
            s_sdl_controllers[port_no],
            (Uint16)(param->motor[CELL_PAD_ACTUATOR_PARAM_LARGE] * 257),
            (Uint16)(param->motor[CELL_PAD_ACTUATOR_PARAM_SMALL] * 257),
            100 /* duration ms */
        );
    }
#endif

#if PAD_BACKEND_GAMECONTROLLER
    if (port_no < PAD_MAX_HOST_PORTS)
        cpgc_rumble((int)port_no,
                    (float)param->motor[CELL_PAD_ACTUATOR_PARAM_LARGE] / 255.0f,
                    (float)param->motor[CELL_PAD_ACTUATOR_PARAM_SMALL] / 255.0f);
#endif

    return CELL_OK;
}

s32 cellPadClearBuf(u32 port_no)
{
    if (!s_pad_initialized)
        return CELL_PAD_ERROR_NOT_OPENED;

    if (port_no >= CELL_PAD_MAX_PORT_NUM)
        return CELL_PAD_ERROR_INVALID_PARAMETER;

    /* Nothing to clear in our implementation -- state is polled fresh */
    return CELL_OK;
}
