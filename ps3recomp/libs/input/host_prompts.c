/*
 * ps3recomp - host-side button prompts (see host_prompts.h).
 */
#include "host_prompts.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    MODE_OFF = 0,
    MODE_AUTO,
    MODE_FORCED
} prompt_mode;

static int         s_mode_read;      /* the env has been parsed */
static prompt_mode s_mode;
static host_prompt_set s_forced;
static host_prompt_set s_last;       /* last device that produced input (auto) */
static int         s_have_last;

static const host_prompt_view s_views[] = {
    [HOST_PROMPT_SET_PS] = {
        .set = HOST_PROMPT_SET_PS, .cross = "×", .circle = "○", .square = "□", .triangle = "△",
        .cross_icon = "ps_cross", .circle_icon = "ps_circle", .square_icon = "ps_square", .triangle_icon = "ps_triangle",
        .start = "Start", .select = "Select", .start_icon = "ps_start", .select_icon = "ps_select",
        .up = "Up", .down = "Down", .left = "Left", .right = "Right",
        .up_icon = "ps_up", .down_icon = "ps_down", .left_icon = "ps_left", .right_icon = "ps_right",
        .l1 = "L1", .r1 = "R1", .l2 = "L2", .r2 = "R2", .l3 = "L3", .r3 = "R3",
        .l1_icon = "ps_l1", .r1_icon = "ps_r1", .l2_icon = "ps_l2", .r2_icon = "ps_r2",
        .l3_icon = "ps_l3", .r3_icon = "ps_r3", .left_stick_icon = "ps_ls", .right_stick_icon = "ps_rs"
    },
    [HOST_PROMPT_SET_XBOX] = {
        .set = HOST_PROMPT_SET_XBOX, .cross = "A", .circle = "B", .square = "X", .triangle = "Y",
        .cross_icon = "xbox_a", .circle_icon = "xbox_b", .square_icon = "xbox_x", .triangle_icon = "xbox_y",
        .start = "Menu", .select = "View", .start_icon = "xbox_menu", .select_icon = "xbox_view",
        .up = "Up", .down = "Down", .left = "Left", .right = "Right",
        .up_icon = "xbox_up", .down_icon = "xbox_down", .left_icon = "xbox_left", .right_icon = "xbox_right",
        .l1 = "LB", .r1 = "RB", .l2 = "LT", .r2 = "RT", .l3 = "LS", .r3 = "RS",
        .l1_icon = "xbox_lb", .r1_icon = "xbox_rb", .l2_icon = "xbox_lt", .r2_icon = "xbox_rt",
        .l3_icon = "xbox_ls", .r3_icon = "xbox_rs", .left_stick_icon = "xbox_ls", .right_stick_icon = "xbox_rs"
    },
    [HOST_PROMPT_SET_SWITCH] = {
        .set = HOST_PROMPT_SET_SWITCH, .cross = "B", .circle = "A", .square = "Y", .triangle = "X",
        .cross_icon = "switch_b", .circle_icon = "switch_a", .square_icon = "switch_y", .triangle_icon = "switch_x",
        .start = "+", .select = "-", .start_icon = "switch_plus", .select_icon = "switch_minus",
        .up = "Up", .down = "Down", .left = "Left", .right = "Right",
        .up_icon = "switch_up", .down_icon = "switch_down", .left_icon = "switch_left", .right_icon = "switch_right",
        .l1 = "L", .r1 = "R", .l2 = "ZL", .r2 = "ZR", .l3 = "LS", .r3 = "RS",
        .l1_icon = "switch_l", .r1_icon = "switch_r", .l2_icon = "switch_zl", .r2_icon = "switch_zr",
        .l3_icon = "switch_ls", .r3_icon = "switch_rs", .left_stick_icon = "switch_ls", .right_stick_icon = "switch_rs"
    },
    [HOST_PROMPT_SET_KB] = {
        .set = HOST_PROMPT_SET_KB, .cross = "Space", .circle = "E", .square = "J", .triangle = "K",
        .cross_icon = "key_space", .circle_icon = "key_e", .square_icon = "key_j", .triangle_icon = "key_k",
        .start = "Enter", .select = "Tab", .start_icon = "key_enter", .select_icon = "key_tab",
        .up = "Up", .down = "Down", .left = "Left", .right = "Right",
        .up_icon = "key_up", .down_icon = "key_down", .left_icon = "key_left", .right_icon = "key_right",
        .l1 = "L-Shift", .r1 = "Q", .l2 = "L-Ctrl", .r2 = "R", .l3 = "F", .r3 = "G",
        .l1_icon = "key_lshift", .r1_icon = "key_q", .l2_icon = "key_lctrl", .r2_icon = "key_r",
        .l3_icon = "key_f", .r3_icon = "key_g", .left_stick_icon = "key_wasd", .right_stick_icon = "mouse"
    }
};

static int ascii_lower(int c)
{
    return (c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c;
}

/* Case-insensitive substring match, ASCII only (product strings are ASCII). */
static int has_word(const char* hay, const char* needle)
{
    size_t n;
    if (!hay || !needle) return 0;
    n = strlen(needle);
    if (n == 0) return 0;
    for (; *hay; hay++) {
        size_t i;
        for (i = 0; i < n; i++) {
            if (!hay[i] || ascii_lower((unsigned char)hay[i]) != ascii_lower((unsigned char)needle[i]))
                break;
        }
        if (i == n) return 1;
    }
    return 0;
}

static void read_mode(void)
{
    const char* e;
    if (s_mode_read) return;
    s_mode_read = 1;
    s_mode = MODE_AUTO;
    s_forced = HOST_PROMPT_SET_PS;
    e = getenv("PS3_HOST_PROMPTS");
    if (!e || !e[0]) return;
    if (e[0] == '0' && e[1] == '\0') {
        s_mode = MODE_OFF;
        return;
    }
    if (has_word(e, "auto")) {
        s_mode = MODE_AUTO;
    } else if (has_word(e, "xbox")) {
        s_mode = MODE_FORCED; s_forced = HOST_PROMPT_SET_XBOX;
    } else if (has_word(e, "switch")) {
        s_mode = MODE_FORCED; s_forced = HOST_PROMPT_SET_SWITCH;
    } else if (has_word(e, "kb") || has_word(e, "key")) {
        s_mode = MODE_FORCED; s_forced = HOST_PROMPT_SET_KB;
    } else if (has_word(e, "ps") || has_word(e, "dual")) {
        s_mode = MODE_FORCED; s_forced = HOST_PROMPT_SET_PS;
    }
    /* An unreadable explicit value is off: do not paint an arbitrary icon. */
    else {
        s_mode = MODE_OFF;
    }
}

int host_prompts_enabled(void)
{
    read_mode();
    return s_mode != MODE_OFF;
}

host_prompt_set host_prompts_classify(const char* product_category)
{
    if (!product_category || !product_category[0]) return HOST_PROMPT_SET_PS;
    if (has_word(product_category, "dualsense") || has_word(product_category, "dualshock") ||
        has_word(product_category, "playstation"))
        return HOST_PROMPT_SET_PS;
    if (has_word(product_category, "switch") || has_word(product_category, "joy-con") ||
        has_word(product_category, "joycon") || has_word(product_category, "nintendo"))
        return HOST_PROMPT_SET_SWITCH;
    if (has_word(product_category, "xbox")) return HOST_PROMPT_SET_XBOX;
    /* MFi and other unknown pads are labelled A/B/X/Y like an Xbox pad. */
    return HOST_PROMPT_SET_XBOX;
}

host_input_device host_input_classify(const char* product_category)
{
    if (!product_category || !product_category[0]) return HOST_INPUT_UNKNOWN;
    if (has_word(product_category, "keyboard") || has_word(product_category, "key"))
        return HOST_INPUT_KEYBOARD;
    if (has_word(product_category, "dualsense") || has_word(product_category, "dualshock") ||
        has_word(product_category, "playstation"))
        return HOST_INPUT_PLAYSTATION;
    if (has_word(product_category, "switch") || has_word(product_category, "joy-con") ||
        has_word(product_category, "joycon") || has_word(product_category, "nintendo"))
        return HOST_INPUT_NINTENDO;
    if (has_word(product_category, "xbox")) return HOST_INPUT_XBOX;
    return HOST_INPUT_UNKNOWN;
}

const host_prompt_view* host_prompts_view_for(host_prompt_set set)
{
    if (set < HOST_PROMPT_SET_PS || set > HOST_PROMPT_SET_KB)
        return &s_views[HOST_PROMPT_SET_PS];
    return &s_views[set];
}

void host_prompts_note_pad(const char* product_category)
{
    s_last = host_prompts_classify(product_category);
    s_have_last = 1;
}

void host_prompts_note_no_pad(void)
{
    if (!s_have_last) s_last = HOST_PROMPT_SET_KB;   /* not a real input yet */
}

void host_prompts_note_keyboard(void)
{
    s_last = HOST_PROMPT_SET_KB;
    s_have_last = 1;
}

host_prompt_set host_prompts_current(void)
{
    read_mode();
    if (s_mode == MODE_FORCED) return s_forced;
    if (s_mode == MODE_AUTO) return s_last;
    return HOST_PROMPT_SET_PS;
}

const host_prompt_view* host_prompts_current_view(void)
{
    return host_prompts_view_for(host_prompts_current());
}

int host_input_state_active(unsigned buttons, unsigned analog_lx, unsigned analog_ly,
                            unsigned analog_rx, unsigned analog_ry,
                            unsigned trigger_l2, unsigned trigger_r2)
{
    if (buttons) return 1;
    if (analog_lx > 152 || analog_lx < 104) return 1;
    if (analog_ly > 152 || analog_ly < 104) return 1;
    if (analog_rx > 152 || analog_rx < 104) return 1;
    if (analog_ry > 152 || analog_ry < 104) return 1;
    return trigger_l2 > 30 || trigger_r2 > 30;
}

int host_prompts_sim_tick(unsigned elapsed_ms)
{
    static int parsed = 0, fired = 0;
    static char category[64];
    static unsigned at_ms = 0;
    if (!parsed) {
        const char* e = getenv("PS3_HOST_PROMPTS_SIM");
        const char* at = e ? strrchr(e, '@') : NULL;
        parsed = 1;
        if (at && at > e && (size_t)(at - e) < sizeof category) {
            memcpy(category, e, (size_t)(at - e));
            category[at - e] = '\0';
            at_ms = (unsigned)strtoul(at + 1, NULL, 10) * 1000u;
        } else {
            fired = 1; /* unset or malformed: never fires */
        }
    }
    if (fired || elapsed_ms < at_ms) return 0;
    fired = 1;
    host_prompts_note_pad(category);
    fprintf(stderr, "[PROMPT-SIM] t=%ums device=\"%s\" -> set=%d\n",
            elapsed_ms, category, (int)host_prompts_current());
    fflush(stderr);
    return 1;
}

void host_prompts_reset_for_test(void)
{
    s_mode_read = 0;
    s_have_last = 0;
    s_last = HOST_PROMPT_SET_PS;
}
