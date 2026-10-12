#ifndef PS3RECOMP_RSX_OVERLAY_SHELL_H
#define PS3RECOMP_RSX_OVERLAY_SHELL_H

/* Console shell: pure logic, no ImGui, no platform. Time is passed in
 * (seconds, monotonic), so every value is deterministic in tests. */

#include <stdint.h>

#include "rsx_overlay.h"
#include "rsx_overlay_feedback.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum rsx_shell_phase {
    RSX_SHELL_CLOSED = 0, RSX_SHELL_OPENING, RSX_SHELL_OPEN, RSX_SHELL_CLOSING
} rsx_shell_phase;

typedef enum rsx_shell_card {
    RSX_SHELL_CARD_RESUME = 0, RSX_SHELL_CARD_DISPLAY, RSX_SHELL_CARD_CONTROLS,
    RSX_SHELL_CARD_SOUND, RSX_SHELL_CARD_DEVELOPER, RSX_SHELL_CARD_QUIT,
    RSX_SHELL_CARD_COUNT
} rsx_shell_card;

typedef enum rsx_shell_level {
    RSX_SHELL_LEVEL_CARDS = 0, RSX_SHELL_LEVEL_PANEL, RSX_SHELL_LEVEL_LIST,
    RSX_SHELL_LEVEL_CAPTURE, RSX_SHELL_LEVEL_DIALOG
} rsx_shell_level;

typedef enum rsx_shell_list {
    RSX_SHELL_LIST_NONE = 0, RSX_SHELL_LIST_WINDOW_SIZE, RSX_SHELL_LIST_REMAP, RSX_SHELL_LIST_LOGS
} rsx_shell_list;

typedef enum rsx_shell_row {
    RSX_SHELL_ROW_FULLSCREEN = 0, RSX_SHELL_ROW_VSYNC, RSX_SHELL_ROW_WINDOW_SIZE,
    RSX_SHELL_ROW_DEADZONE, RSX_SHELL_ROW_VIBRATION, RSX_SHELL_ROW_REMAP, RSX_SHELL_ROW_RESTORE,
    RSX_SHELL_ROW_GAME_SOUND, RSX_SHELL_ROW_UI_SOUNDS, RSX_SHELL_ROW_UI_HAPTICS,
    RSX_SHELL_ROW_LOGS, RSX_SHELL_ROW_COUNT
} rsx_shell_row;

enum {
    RSX_SHELL_IN_UP = 1u << 0, RSX_SHELL_IN_DOWN = 1u << 1, RSX_SHELL_IN_LEFT = 1u << 2,
    RSX_SHELL_IN_RIGHT = 1u << 3, RSX_SHELL_IN_CONFIRM = 1u << 4, RSX_SHELL_IN_BACK = 1u << 5
};

typedef struct rsx_shell_input {
    uint32_t held;        /* RSX_SHELL_IN_* down now (keyboard + pad) */
    uint32_t pressed;     /* RSX_SHELL_IN_* pressed since the last update */
    uint32_t key_pressed; /* subset of `pressed` that came from the keyboard */
    uint16_t pad_pressed; /* raw host pad bits (CELL_PAD_CTRL_*) newly pressed */
} rsx_shell_input;

typedef enum rsx_shell_action_kind {
    RSX_SHELL_ACT_NONE = 0, RSX_SHELL_ACT_CLOSE, RSX_SHELL_ACT_QUIT,
    RSX_SHELL_ACT_TOGGLE_FULLSCREEN, RSX_SHELL_ACT_TOGGLE_VSYNC, RSX_SHELL_ACT_SET_WINDOW_SIZE,
    RSX_SHELL_ACT_DEADZONE_STEP, RSX_SHELL_ACT_TOGGLE_VIBRATION, RSX_SHELL_ACT_REMAP,
    RSX_SHELL_ACT_RESTORE_CONTROLS, RSX_SHELL_ACT_TOGGLE_GAME_SOUND,
    RSX_SHELL_ACT_TOGGLE_UI_SOUNDS, RSX_SHELL_ACT_TOGGLE_UI_HAPTICS
} rsx_shell_action_kind;

/* SET_WINDOW_SIZE: a = index; DEADZONE_STEP: a = -1/+1; REMAP: a = guest bit index, b = host bit index. */
typedef struct rsx_shell_action { int kind; int a; int b; } rsx_shell_action;

#define RSX_SHELL_MAX_ACTIONS 8
typedef struct rsx_shell_output {
    rsx_shell_action actions[RSX_SHELL_MAX_ACTIONS];
    int count;
    uint32_t feedback; /* RSX_OVERLAY_FEEDBACK_BIT(...) mask */
} rsx_shell_output;

typedef struct rsx_shell_model {
    uint8_t support_fullscreen;  /* rsx_overlay_setting_support */
    uint8_t support_vsync;
    uint8_t support_window_size;
    uint8_t window_size_index;   /* 0..3, 255 when the size is not in the list */
    uint32_t log_lines;          /* entries in the logs list */
    uint32_t list_visible_rows;  /* rows the panel shows (from the layout), >= 1 */
} rsx_shell_model;

typedef struct rsx_spring { float value; float velocity; float target; } rsx_spring;

#define RSX_SHELL_WINDOW_SIZE_COUNT 4
#define RSX_SHELL_REMAP_COUNT 16
#define RSX_SHELL_DEADZONE_MAX 30
#define RSX_SHELL_CAPTURE_TIMEOUT_S 5.0
#define RSX_SHELL_OPEN_OMEGA 22.0f  /* ~95 % at 220 ms, critically damped */
#define RSX_SHELL_CARD_OMEGA 28.0f
#define RSX_SHELL_FOCUS_SCALE 1.18f
#define RSX_SHELL_FADE_S 0.12
#define RSX_SHELL_REPEAT_DELAY_S 0.40
#define RSX_SHELL_REPEAT_INTERVAL_S 0.10
#define RSX_SHELL_BACKDROP_DIM 0.35f

/* Layout, in points (multiplied by the layout's px_per_pt). */
#define RSX_SHELL_CARD_PT 96.0f
#define RSX_SHELL_CARD_GAP_PT 20.0f
#define RSX_SHELL_MARGIN_PT 48.0f
#define RSX_SHELL_CARD_BOTTOM_PT 72.0f
#define RSX_SHELL_RISE_PT 40.0f
#define RSX_SHELL_STRIP_PT 44.0f
#define RSX_SHELL_BAND_PT 260.0f
#define RSX_SHELL_PANEL_W_PT 380.0f
#define RSX_SHELL_PANEL_WIDE_W_PT 600.0f
#define RSX_SHELL_PANEL_HEADER_PT 52.0f
#define RSX_SHELL_PANEL_PAD_PT 12.0f
#define RSX_SHELL_PANEL_GAP_PT 20.0f
#define RSX_SHELL_ROW_H_PT 44.0f
#define RSX_SHELL_CARD_RADIUS_PT 14.0f
#define RSX_SHELL_NEED_W_PT 792.0f /* 2 margins + 6 cards + 4 gaps + 2 gaps before Sair */
#define RSX_SHELL_NEED_H_PT 480.0f

typedef struct rsx_shell {
    rsx_shell_phase phase;
    rsx_shell_level level;
    int card;
    int row;               /* index into rsx_shell_panel_rows(card) */
    rsx_shell_list list;
    int list_index;
    int list_scroll;
    int capture_guest;
    double capture_started;
    int dialog_choice;     /* 0 = Sair, 1 = Cancelar */
    double last_time;
    double panel_switch_time;
    double repeat_next;
    uint32_t repeat_dir;
    uint32_t pending_feedback;
    rsx_spring open;
    rsx_spring card_scale[RSX_SHELL_CARD_COUNT];
} rsx_shell;

typedef struct rsx_shell_rect { float x, y, w, h; } rsx_shell_rect;

typedef struct rsx_shell_layout {
    float px_per_pt;
    float width, height;
    rsx_shell_rect top_strip;
    rsx_shell_rect band;
    rsx_shell_rect cards[RSX_SHELL_CARD_COUNT]; /* unscaled rects, risen by open progress */
    rsx_shell_rect panel;
    int panel_visible_rows;
} rsx_shell_layout;

extern const uint32_t rsx_shell_window_sizes[RSX_SHELL_WINDOW_SIZE_COUNT][2];
/* pt-BR names of the 16 pad buttons in CELL_PAD_CTRL_* bit order. */
extern const char* const rsx_shell_button_names[RSX_SHELL_REMAP_COUNT];

void rsx_spring_step(rsx_spring* spring, float omega, float dt);
void rsx_shell_init(rsx_shell* shell);
void rsx_shell_open(rsx_shell* shell, double now);
void rsx_shell_close(rsx_shell* shell, double now);
void rsx_shell_update(rsx_shell* shell, const rsx_shell_input* input,
                      const rsx_shell_model* model, double now, rsx_shell_output* out);
/* Interactive rows of a card's panel; rows == NULL returns the count. */
int rsx_shell_panel_rows(int card, rsx_shell_row* rows, int capacity);
int rsx_shell_row_enabled(rsx_shell_row row, const rsx_shell_model* model);
/* Rows the panel displays (for the layout) and whether it is the wide one. */
int rsx_shell_panel_display_rows(const rsx_shell* shell, const rsx_shell_model* model);
int rsx_shell_panel_wide(const rsx_shell* shell);
float rsx_shell_panel_fade(const rsx_shell* shell, double now);
/* 1 while a direction is held (a slider may still be moving). */
int rsx_shell_direction_held(const rsx_shell* shell);
/* Keeps the list selection valid and on screen after the model changed under
 * it (fewer visible rows after a resize or backing-scale change, a shorter
 * log). No-op outside a list. */
void rsx_shell_refit_list(rsx_shell* shell, const rsx_shell_model* model);
/* Binds host bit `host_index` to guest bit `guest_index`, swapping with the
 * previous owner so the map stays a bijection. */
void rsx_shell_remap_assign(uint16_t map[RSX_SHELL_REMAP_COUNT], unsigned guest_index,
                            unsigned host_index);
void rsx_shell_compute_layout(float width_px, float height_px, float backing_scale,
                              int focused_card, int panel_rows, int wide_panel,
                              float open_progress, rsx_shell_layout* out);

#ifdef __cplusplus
}
#endif

#endif
