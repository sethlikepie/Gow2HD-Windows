/*
 * ps3recomp - host-side button prompts: which icon/label set the host overlay should
 * paint for the device the player is actually using.
 *
 * The title draws DualShock prompts from its own WAD textures; it has no idea an Xbox
 * pad, a Switch pad or a keyboard exists, so any other set has to come from the host.
 * This unit owns only the decision (device -> set) and its gate; drawing lives in the
 * backend overlay. Plain C, no Apple frameworks: the macOS pad backend feeds it and
 * Windows can feed it the same way.
 *
 * PS3_HOST_PROMPTS: unset or "auto" = follow the last device that produced input,
 * "0" = off, "ps" | "xbox" | "switch" | "kb" = forced set.
 */
#ifndef PS3RECOMP_HOST_PROMPTS_H
#define PS3RECOMP_HOST_PROMPTS_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HOST_PROMPT_SET_PS = 0,   /* cross / circle / square / triangle (what the game draws) */
    HOST_PROMPT_SET_XBOX,     /* A / B / X / Y */
    HOST_PROMPT_SET_SWITCH,   /* B / A / Y / X -- same positions, swapped labels */
    HOST_PROMPT_SET_KB        /* keyboard/mouse keys */
} host_prompt_set;

typedef enum {
    HOST_INPUT_UNKNOWN = 0,
    HOST_INPUT_KEYBOARD,
    HOST_INPUT_PLAYSTATION,
    HOST_INPUT_XBOX,
    HOST_INPUT_NINTENDO
} host_input_device;

/* Stable, renderer-facing labels for logical PS3 actions. The strings are
 * immutable and owned by host_prompts.c. */
typedef struct {
    host_prompt_set set;
    const char* cross;
    const char* circle;
    const char* square;
    const char* triangle;
    const char* cross_icon;
    const char* circle_icon;
    const char* square_icon;
    const char* triangle_icon;
    const char* start;
    const char* select;
    const char* start_icon;
    const char* select_icon;
    const char* up;
    const char* down;
    const char* left;
    const char* right;
    const char* up_icon;
    const char* down_icon;
    const char* left_icon;
    const char* right_icon;
    const char* l1;
    const char* r1;
    const char* l2;
    const char* r2;
    const char* l3;
    const char* r3;
    const char* l1_icon;
    const char* r1_icon;
    const char* l2_icon;
    const char* r2_icon;
    const char* l3_icon;
    const char* r3_icon;
    const char* left_stick_icon;
    const char* right_stick_icon;
} host_prompt_view;

/*
 * "No controller is attached": with nothing but a keyboard and mouse on the
 * machine, they ARE the pad, so `auto` shows keyboard prompts from the first
 * frame instead of the title's PlayStation glyphs. Any real input (keyboard
 * or a controller that shows up later) still wins over this.
 */
void host_prompts_note_no_pad(void);

/*
 * Diagnostic (OFF unless PS3_HOST_PROMPTS_SIM is set): "<product category>@<seconds>",
 * e.g. "Xbox Wireless Controller@90", reports that device as the last input once
 * `elapsed_ms` passes the given time -- the same call a real controller makes --
 * so a boot can prove the prompts follow a device change without a restart.
 * Returns 1 on the tick that fires (once per process), otherwise 0.
 */
int host_prompts_sim_tick(unsigned elapsed_ms);

/* 1 when the host menu-glyph replacement should paint (adaptive by default; "0" disables). */
int host_prompts_enabled(void);

/* The set to paint now. With "auto" this follows the last device that sent input; with a
 * forced value it is that value. Meaningless when host_prompts_enabled() is 0. */
host_prompt_set host_prompts_current(void);

/* Device name/category -> host device kind. This is deliberately independent
 * of SDL/GameController so every backend can use the same classification. */
host_input_device host_input_classify(const char* product_category);

/* Immutable prompt labels for a set and for the currently selected set. */
const host_prompt_view* host_prompts_view_for(host_prompt_set set);
const host_prompt_view* host_prompts_current_view(void);

/* True when a host sample represents real user activity. Axes use the PS3
 * 0..255 representation with 128 as center. */
int host_input_state_active(unsigned buttons, unsigned analog_lx, unsigned analog_ly,
                            unsigned analog_rx, unsigned analog_ry,
                            unsigned trigger_l2, unsigned trigger_r2);

/* Apple's GCController.productCategory (or any vendor string) -> set. NULL/empty keeps
 * the PlayStation set; an unknown pad gets the Xbox labels, which is what MFi pads show. */
host_prompt_set host_prompts_classify(const char* product_category);

/* The pad backend calls these when a poll saw real input, so "auto" can follow the device
 * in the player's hands even when both are connected. */
void host_prompts_note_pad(const char* product_category);
void host_prompts_note_keyboard(void);

/* Tests only: re-read the environment and forget the last device. */
void host_prompts_reset_for_test(void);

#ifdef __cplusplus
}
#endif
#endif /* PS3RECOMP_HOST_PROMPTS_H */
