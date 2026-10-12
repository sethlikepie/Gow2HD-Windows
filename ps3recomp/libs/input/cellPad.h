/*
 * ps3recomp - cellPad HLE
 *
 * Gamepad input: initialization, reading button/analog state.
 * Backend: XInput on Windows, SDL2 GameController elsewhere.
 */

#ifndef PS3RECOMP_CELL_PAD_H
#define PS3RECOMP_CELL_PAD_H

#include "ps3emu/ps3types.h"
#include "ps3emu/error_codes.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * Constants
 * -----------------------------------------------------------------------*/

#define CELL_PAD_MAX_PORT_NUM       7
#define CELL_PAD_MAX_CODES          64
#define CELL_MAX_PADS               CELL_PAD_MAX_PORT_NUM

/* Button bitmask constants (digital buttons, in button[2] low word) */
#define CELL_PAD_CTRL_SELECT        (1 << 0)
#define CELL_PAD_CTRL_L3            (1 << 1)
#define CELL_PAD_CTRL_R3            (1 << 2)
#define CELL_PAD_CTRL_START         (1 << 3)
#define CELL_PAD_CTRL_UP            (1 << 4)
#define CELL_PAD_CTRL_RIGHT         (1 << 5)
#define CELL_PAD_CTRL_DOWN          (1 << 6)
#define CELL_PAD_CTRL_LEFT          (1 << 7)
#define CELL_PAD_CTRL_L2            (1 << 8)
#define CELL_PAD_CTRL_R2            (1 << 9)
#define CELL_PAD_CTRL_L1            (1 << 10)
#define CELL_PAD_CTRL_R1            (1 << 11)
#define CELL_PAD_CTRL_TRIANGLE      (1 << 12)
#define CELL_PAD_CTRL_CIRCLE        (1 << 13)
#define CELL_PAD_CTRL_CROSS         (1 << 14)
#define CELL_PAD_CTRL_SQUARE        (1 << 15)

#define CELL_PAD_HOST_BUTTON_COUNT 16u

/* Host analog deadzone (percent) when no overlay setting says otherwise. 0 is
 * the identity: axes reach the guest exactly as the host backend produced
 * them (the pre-overlay behavior). */
#define CELL_PAD_HOST_DEADZONE_DEFAULT 0u

/* PadHostState.controller_axes: which stick axes currently hold a
 * game-controller reading (as opposed to keyboard/mouse-synthesised values). */
#define CELL_PAD_HOST_AXIS_LX 0x1u
#define CELL_PAD_HOST_AXIS_LY 0x2u
#define CELL_PAD_HOST_AXIS_RX 0x4u
#define CELL_PAD_HOST_AXIS_RY 0x8u
#define CELL_PAD_HOST_AXIS_ALL 0xFu

/* Pure helpers used by cellPadGetData and standalone input tests. */
static inline u16 cellPad_translate_host_buttons(
    u16 host_buttons, const u16 guest_button_for_host_bit[CELL_PAD_HOST_BUTTON_COUNT])
{
    u16 guest_buttons = 0;
    unsigned bit;

    if (guest_button_for_host_bit == 0)
        return host_buttons;
    for (bit = 0; bit < CELL_PAD_HOST_BUTTON_COUNT; ++bit) {
        if (host_buttons & ((u16)1u << bit))
            guest_buttons |= guest_button_for_host_bit[bit];
    }
    return guest_buttons;
}

/* 1 when `map` sends every host bit to the same guest bit: translating
 * through it is then the same as passing no map. */
static inline int cellPad_host_map_is_identity(
    const u16 guest_button_for_host_bit[CELL_PAD_HOST_BUTTON_COUNT])
{
    unsigned bit;

    if (guest_button_for_host_bit == 0)
        return 0;
    for (bit = 0; bit < CELL_PAD_HOST_BUTTON_COUNT; ++bit)
        if (guest_button_for_host_bit[bit] != (u16)(1u << bit))
            return 0;
    return 1;
}

/* `deadzone` is a percentage of the 127-unit distance from centered 128.
 * Values on the boundary remain centered. */
static inline u8 cellPad_apply_deadzone_axis(u8 value, u8 deadzone)
{
    int delta = (int)value - 128;
    int magnitude = delta < 0 ? -delta : delta;
    int limit = delta < 0 ? 128 : 127;
    int threshold;
    int scaled;

    if (deadzone > 100)
        deadzone = 100;
    threshold = ((int)deadzone * limit) / 100;
    if (magnitude <= threshold || threshold >= limit)
        return 128;
    scaled = ((magnitude - threshold) * limit) / (limit - threshold);
    if (scaled > limit)
        scaled = limit;
    return (u8)(delta < 0 ? 128 - scaled : 128 + scaled);
}

/* The configured deadzone shapes controller sticks only. Keyboard- and
 * mouse-synthesised axes are already exact (full deflection or a small
 * mouse-look delta that a deadzone would swallow) and pass unchanged. */
static inline u8 cellPad_host_axis(u8 value, u8 deadzone, int from_controller)
{
    return from_controller ? cellPad_apply_deadzone_axis(value, deadzone) : value;
}

/* Suppress pressure only when an overlay-consumed host button was physically
 * down. The guest digital mask is already post-capture at this point. */
static inline u8 cellPad_pressure_after_capture(u16 raw_host_buttons,
                                                u16 delivered_host_buttons,
                                                u16 host_button, u8 pressure,
                                                int digital_fallback)
{
    if ((raw_host_buttons & host_button) && !(delivered_host_buttons & host_button))
        return 0;
    if ((delivered_host_buttons & host_button) && pressure == 0 && digital_fallback)
        return 255;
    return pressure;
}

/* PS button (in button[3] high byte) */
#define CELL_PAD_CTRL_PS            (1 << 0)

/* Port setting flags */
#define CELL_PAD_SETTING_PRESS_ON       (1 << 1)
#define CELL_PAD_SETTING_SENSOR_ON      (1 << 2)
#define CELL_PAD_SETTING_PRESS_OFF      0
#define CELL_PAD_SETTING_SENSOR_OFF     0

/* Port status flags */
#define CELL_PAD_STATUS_DISCONNECTED    0
#define CELL_PAD_STATUS_CONNECTED       1
#define CELL_PAD_STATUS_ASSIGN_CHANGES  2

/* Device type */
#define CELL_PAD_DEV_TYPE_STANDARD      0
#define CELL_PAD_DEV_TYPE_BD_REMOCON    4
#define CELL_PAD_DEV_TYPE_LDD           5

/* Device capability flags */
#define CELL_PAD_CAPABILITY_PS3_CONFORMITY  (1 << 0)
#define CELL_PAD_CAPABILITY_PRESS_MODE      (1 << 1)
#define CELL_PAD_CAPABILITY_SENSOR_MODE     (1 << 2)
#define CELL_PAD_CAPABILITY_HP_ANALOG_STICK (1 << 3)
#define CELL_PAD_CAPABILITY_ACTUATOR        (1 << 4)

/* Standard data lengths */
#define CELL_PAD_LEN_NO_CHANGE          0
#define CELL_PAD_LEN_CHANGE_DEFAULT     8
#define CELL_PAD_LEN_CHANGE_PRESS_ON    24
#define CELL_PAD_LEN_CHANGE_SENSOR_ON   28

/* Pressure-sensitive button offsets in button[] array */
#define CELL_PAD_BTN_OFFSET_DIGITAL1    2
#define CELL_PAD_BTN_OFFSET_DIGITAL2    3
#define CELL_PAD_BTN_OFFSET_ANALOG_RIGHT_X  4
#define CELL_PAD_BTN_OFFSET_ANALOG_RIGHT_Y  5
#define CELL_PAD_BTN_OFFSET_ANALOG_LEFT_X   6
#define CELL_PAD_BTN_OFFSET_ANALOG_LEFT_Y   7
#define CELL_PAD_BTN_OFFSET_PRESS_RIGHT     8
#define CELL_PAD_BTN_OFFSET_PRESS_LEFT      9
#define CELL_PAD_BTN_OFFSET_PRESS_UP        10
#define CELL_PAD_BTN_OFFSET_PRESS_DOWN      11
#define CELL_PAD_BTN_OFFSET_PRESS_TRIANGLE  12
#define CELL_PAD_BTN_OFFSET_PRESS_CIRCLE    13
#define CELL_PAD_BTN_OFFSET_PRESS_CROSS     14
#define CELL_PAD_BTN_OFFSET_PRESS_SQUARE    15
#define CELL_PAD_BTN_OFFSET_PRESS_L1        16
#define CELL_PAD_BTN_OFFSET_PRESS_R1        17
#define CELL_PAD_BTN_OFFSET_PRESS_L2        18
#define CELL_PAD_BTN_OFFSET_PRESS_R2        19
#define CELL_PAD_BTN_OFFSET_SENSOR_X        20
#define CELL_PAD_BTN_OFFSET_SENSOR_Y        21
#define CELL_PAD_BTN_OFFSET_SENSOR_Z        22
#define CELL_PAD_BTN_OFFSET_SENSOR_G        23

/* Actuator (rumble) parameters */
#define CELL_PAD_ACTUATOR_MAX           2
#define CELL_PAD_ACTUATOR_PARAM_SMALL   0
#define CELL_PAD_ACTUATOR_PARAM_LARGE   1

/* ---------------------------------------------------------------------------
 * Structures
 * -----------------------------------------------------------------------*/

/*
 * CellPadData -- returned by cellPadGetData.
 *
 * Layout of the button[] array (each element is u16):
 *   [0] = data length (number of valid entries following)
 *   [1] = reserved
 *   [2] = digital buttons bitmask (CELL_PAD_CTRL_*)
 *   [3] = digital buttons bitmask continued (PS button, etc.)
 *   [4] = right analog stick X (0-255, center=128)
 *   [5] = right analog stick Y (0-255, center=128)
 *   [6] = left  analog stick X (0-255, center=128)
 *   [7] = left  analog stick Y (0-255, center=128)
 *   [8..19] = pressure-sensitive button values (0-255 each)
 *   [20..23] = sensor data (accel X, Y, Z, gyro)
 */
typedef struct CellPadData {
    s32 len;                          /* number of valid button entries */
    u16 button[CELL_PAD_MAX_CODES];   /* button/analog/sensor data */
} CellPadData;

/* Per-port info returned by cellPadGetInfo2 */
typedef struct CellPadInfo2 {
    u32 max_connect;
    u32 now_connect;
    u32 system_info;
    u32 port_status[CELL_PAD_MAX_PORT_NUM];
    u32 port_setting[CELL_PAD_MAX_PORT_NUM];
    u32 device_capability[CELL_PAD_MAX_PORT_NUM];
    u32 device_type[CELL_PAD_MAX_PORT_NUM];
} CellPadInfo2;

/* Capability info for a specific port */
typedef struct CellPadCapabilityInfo {
    u32 info[CELL_PAD_MAX_CODES];
} CellPadCapabilityInfo;

/* Actuator (vibration) parameter */
typedef struct CellPadActParam {
    u8 motor[CELL_PAD_ACTUATOR_MAX];
} CellPadActParam;

/* ---------------------------------------------------------------------------
 * Functions
 * -----------------------------------------------------------------------*/

/* NID: 0x1CF98800 */
s32 cellPadInit(u32 max_connect);

/* NID: 0x4D9B75D5 */
s32 cellPadEnd(void);

/* NID: 0x8B72CBA1 */
s32 cellPadGetData(u32 port_no, CellPadData* data);

/* NID: 0x8B8013DA */
s32 cellPadGetInfo2(CellPadInfo2* info);

/* NID: 0xA703A51D */
s32 cellPadSetPortSetting(u32 port_no, u32 port_setting);

/* NID: 0x578E3C98 */
s32 cellPadGetCapabilityInfo(u32 port_no, CellPadCapabilityInfo* info);

/* NID: 0xBE5C3E81 */
s32 cellPadSetActDirect(u32 port_no, CellPadActParam* param);

/* NID: 0x3733EA3C */
s32 cellPadClearBuf(u32 port_no);

/* Internal: call once per frame to update pad state from host input */
void cellPad_poll(void);

void cellPad_set_host_mapping(
    const uint16_t guest_button_for_host_bit[CELL_PAD_HOST_BUTTON_COUNT],
    uint8_t deadzone);

/* Overlay "Vibração": 0 stops game rumble reaching the controller. */
void cellPad_set_host_vibration(int enabled);

static inline void cellPad_gate_actuators(u8* motor, unsigned count, int enabled)
{
    unsigned i;
    if (enabled)
        return;
    for (i = 0; i < count; ++i)
        motor[i] = 0; /* zero, not skip: an ongoing rumble is stopped */
}

/* PS3_PAD_AUTOSTART sequence: combined CELL_PAD_CTRL_* mask for GetData call
 * index. cellPadGetData splits it into digital1 (low 8) / digital2 (high 8). */
u16 pad_autostart_buttons(unsigned call_index);
/* Same sequence, with wall-clock time since first GetData injected so
 * the intro vs New Game phases are unit-testable without sleeping. */
u16 pad_autostart_buttons_at(unsigned call_index, unsigned elapsed_sec);
u16 pad_autostart_buttons_at_ms(unsigned call_index, unsigned elapsed_ms);
/* Diagnostic variant: preserves the boot/menu sequence but stays neutral once
 * the title is ready, so a visual probe can inspect its native prompts. */
u16 pad_autostart_buttons_hold_title_at_ms(unsigned call_index, unsigned elapsed_ms);
/* Hand-off latch: returns 1 (and stays 1) once a real keyboard key or
 * controller button reaches port 0. The virtual pad then releases the port
 * for good, so the player controls the game. Stick/mouse motion alone does
 * not count: a stray mouse move must not stop an unattended boot. */
int pad_autostart_user_took_over(int* latch, u16 host_buttons);
/* Rotacao de combate depois do gameplay armar (E465): SQUARE/SQUARE/TRIANGLE/
 * CROSS/CIRCLE. Antes disto a sequencia ficava em CROSS, que no GoW2 e' saltar.
 * PS3_PAD_AUTOPLAY=0 repoe o CROSS-so'. */
u16 pad_autoplay_buttons_at_ms(unsigned since_gameplay_ms);
/* Chamado pelo backend de video quando o portao de gameplay arma. */
void pad_autoplay_note_gameplay(void);
/* Latch: a post-intro cinematic is the active movie (cellVdec StartSeq>=3
 * or a non-SmLogo .m2v open). TAS CROSS/START go idle until gameplay. */
void pad_autostart_note_cutscene(int on);
/* Same timeline as pad_autostart_buttons_at_ms, but idle after the title
 * is ready — used while an opening cinematic is playing so CROSS cannot skip. */
u16 pad_autostart_buttons_during_cutscene_at_ms(unsigned call_index, unsigned elapsed_ms);
unsigned pad_autostart_elapsed_ms(void);
/* len for one GetData poll: 0 if now_buttons equals the previous sample. */
s32 pad_report_len(u16 now_buttons, u16 *prev_buttons, int *seen, u32 setting);
/* Same, but the four stick bytes (packed RX|RY<<8|LX<<16|LY<<24) also count as a
 * change, as in libpad: a stick released with no button change must be reported. */
s32 pad_report_len_axes(u16 now_buttons, u32 now_axes, u16 *prev_buttons,
                        u32 *prev_axes, int *seen, u32 setting);
/*
 * 1 on the press (rising edge) of START or CROSS, the buttons that ask to cut
 * a pre-rendered cutscene short. The PS3 game has no skip of its own, so this
 * is a host feature (like the RPCS3 community patch); `enabled` comes from
 * PS3_MOVIE_SKIP (on unless set to 0).
 */
int pad_movie_skip_pressed(u16 prev_buttons, u16 now_buttons, int enabled);
/* TAS CROSS/START must not skip Rhodes/Athena. Real player takeover still can. */
int pad_autostart_in_cutscene(void);
/* `suppress`: o toque e' do pad automatico durante um filme (ver o .c). */
int pad_movie_skip_should_fire(u16 prev_buttons, u16 now_buttons, int enabled,
                               int cutscene_hold);

#ifdef __cplusplus
}
#endif

#endif /* PS3RECOMP_CELL_PAD_H */
