/*
 * host_prompts: the gate, the device -> set table and the "auto" switching.
 * Offline: no GameController, no window.
 */
#include "host_prompts.h"
#include "host_prompt_overlay.h"
#include "cellPad.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;

static void check(int cond, const char* what)
{
    if (!cond) {
        fails++;
        printf("FAIL %s\n", what);
    }
}

static void set_env(const char* v)
{
    if (v) setenv("PS3_HOST_PROMPTS", v, 1);
    else unsetenv("PS3_HOST_PROMPTS");
    host_prompts_reset_for_test();
}

int main(void)
{
    /* The title's visible confirmation glyph follows the active device by
     * default. An explicit 0 remains the opt-out for visual diagnostics. */
    set_env(NULL);
    check(host_prompts_enabled(), "unset = adaptive prompt on");
    set_env("0");
    check(!host_prompts_enabled(), "\"0\" = off");
    set_env("banana");
    check(!host_prompts_enabled(), "unknown value = off");
    set_env("auto");
    check(host_prompts_enabled(), "auto = on");

    /* Classification: Apple's productCategory strings. */
    check(host_prompts_classify("DualSense") == HOST_PROMPT_SET_PS, "DualSense -> PS");
    check(host_prompts_classify("DualShock 4") == HOST_PROMPT_SET_PS, "DualShock 4 -> PS");
    check(host_prompts_classify("Xbox One") == HOST_PROMPT_SET_XBOX, "Xbox One -> Xbox");
    check(host_prompts_classify("Switch Pro Controller") == HOST_PROMPT_SET_SWITCH,
          "Switch Pro -> Switch");
    check(host_prompts_classify("Joy-Con (L)") == HOST_PROMPT_SET_SWITCH, "Joy-Con -> Switch");
    check(host_prompts_classify("MFi") == HOST_PROMPT_SET_XBOX, "unknown pad -> Xbox labels");
    check(host_prompts_classify(NULL) == HOST_PROMPT_SET_PS, "no string -> PS");
    check(host_prompts_classify("") == HOST_PROMPT_SET_PS, "empty string -> PS");

    /* auto follows the last device that produced input, both ways. */
    set_env("auto");
    check(host_prompts_current() == HOST_PROMPT_SET_PS, "auto before any input = PS");
    host_prompts_note_keyboard();
    check(host_prompts_current() == HOST_PROMPT_SET_KB, "auto after keyboard = KB");
    host_prompts_note_pad("Xbox One");
    check(host_prompts_current() == HOST_PROMPT_SET_XBOX, "auto after Xbox pad = Xbox");
    host_prompts_note_pad("DualSense");
    check(host_prompts_current() == HOST_PROMPT_SET_PS, "auto after DualSense = PS");
    host_prompts_note_keyboard();
    check(host_prompts_current() == HOST_PROMPT_SET_KB, "auto back to keyboard = KB");

    /* Nothing plugged in: keyboard and mouse ARE the pad, so the prompts show
     * keys from the first frame -- but a real input still wins over that, and a
     * device that already spoke is never downgraded to the keyboard default. */
    host_prompts_reset_for_test();
    set_env("auto");
    host_prompts_note_no_pad();
    check(host_prompts_current() == HOST_PROMPT_SET_KB, "no pad attached = KB");
    host_prompts_note_pad("Xbox One");
    host_prompts_note_no_pad();
    check(host_prompts_current() == HOST_PROMPT_SET_XBOX, "a pad that spoke beats the KB default");

    /* A forced set ignores the device in hand. */
    set_env("switch");
    host_prompts_note_pad("DualSense");
    check(host_prompts_current() == HOST_PROMPT_SET_SWITCH, "forced switch wins over pad");
    set_env("kb");
    host_prompts_note_pad("Xbox One");
    check(host_prompts_current() == HOST_PROMPT_SET_KB, "forced kb wins over pad");
    set_env("ps");
    host_prompts_note_pad("Xbox One");
    check(host_prompts_current() == HOST_PROMPT_SET_PS, "forced ps wins over pad");
    set_env("XBOX");
    check(host_prompts_current() == HOST_PROMPT_SET_XBOX, "value is case-insensitive");

    /* New host descriptor: labels follow logical PS3 actions, not the
     * physical glyph order printed on the controller. */
    {
        const host_prompt_view* view;
        check(host_input_classify("Nintendo Switch Pro Controller") == HOST_INPUT_NINTENDO,
              "Nintendo device classification");
        check(host_input_classify("Xbox Wireless Controller") == HOST_INPUT_XBOX,
              "Xbox device classification");
        check(host_input_classify("DualSense Wireless Controller") == HOST_INPUT_PLAYSTATION,
              "PlayStation device classification");
        check(host_input_classify("keyboard") == HOST_INPUT_KEYBOARD,
              "keyboard classification");

        view = host_prompts_view_for(HOST_PROMPT_SET_SWITCH);
        check(view && strcmp(view->cross, "B") == 0, "Switch Cross prompt is B");
        check(view && strcmp(view->circle, "A") == 0, "Switch Circle prompt is A");
        check(view && strcmp(view->square, "Y") == 0, "Switch Square prompt is Y");
        check(view && strcmp(view->triangle, "X") == 0, "Switch Triangle prompt is X");
        check(view && strcmp(view->cross_icon, "switch_b") == 0, "Switch Cross icon is B");
        check(view && strcmp(view->l1_icon, "switch_l") == 0, "Switch L1 icon is L");
        check(view && strcmp(view->l2_icon, "switch_zl") == 0, "Switch L2 icon is ZL");
        check(view && strcmp(view->r1_icon, "switch_r") == 0, "Switch R1 icon is R");
        check(view && strcmp(view->r2_icon, "switch_zr") == 0, "Switch R2 icon is ZR");
        check(view && strcmp(view->start_icon, "switch_plus") == 0, "Switch Start icon is plus");
        check(view && strcmp(view->select_icon, "switch_minus") == 0, "Switch Select icon is minus");
        check(view && strcmp(view->up_icon, "switch_up") == 0, "Switch up icon is present");
        check(view && strcmp(view->left_stick_icon, "switch_ls") == 0, "Switch left stick icon is present");

        view = host_prompts_view_for(HOST_PROMPT_SET_KB);
        check(view && strcmp(view->cross, "Space") == 0, "keyboard Cross prompt is Space");
        check(view && strcmp(view->circle, "E") == 0, "keyboard Circle prompt is E");
        check(view && strcmp(view->square, "J") == 0, "keyboard Square prompt is J");
        check(view && strcmp(view->triangle, "K") == 0, "keyboard Triangle prompt is K");
        check(view && strcmp(view->cross_icon, "key_space") == 0, "keyboard Cross icon is Space");
        check(view && strcmp(view->l1_icon, "key_lshift") == 0, "keyboard L1 icon is present");
        check(view && strcmp(view->r2_icon, "key_r") == 0, "keyboard R2 icon is present");
        check(view && strcmp(view->start_icon, "key_enter") == 0, "keyboard Start icon is present");
        check(view && strcmp(view->left_stick_icon, "key_wasd") == 0, "keyboard movement icon is present");
        check(view && strcmp(view->up_icon, "key_up") == 0, "keyboard D-pad is the arrow keys");
        check(view && strcmp(view->right_stick_icon, "mouse") == 0, "keyboard camera is the mouse");
    }

    check(!host_input_state_active(0, 128, 128, 128, 128, 0, 0),
          "neutral pad is inactive");
    check(host_input_state_active(CELL_PAD_CTRL_CROSS, 128, 128, 128, 128, 0, 0),
          "button makes pad active");
    check(host_input_state_active(0, 160, 128, 128, 128, 0, 0),
          "stick outside deadzone makes pad active");
    check(host_input_state_active(0, 128, 128, 128, 128, 31, 0),
          "trigger outside threshold makes pad active");

    {
        set_env("kb");
        unsigned w = 0, h = 0;
        const unsigned char* frame = host_prompt_overlay_frame(&w, &h);
        check(frame != NULL && w == 1280 && h == 720, "prompt overlay has host frame size");
        check(frame != NULL && frame[(648u * w + 64u) * 4u + 3u] != 0,
              "keyboard glyph paints the menu X location");
        check(frame != NULL && frame[(548u * w + 742u) * 4u + 3u] == 0,
              "keyboard glyph does not paint the old prompt panel");
        check(!host_prompt_overlay_is_menu_draw_count(2), "movie draw count is not menu");
        check(host_prompt_overlay_is_menu_draw_count(219), "menu draw count is accepted");
        check(!host_prompt_overlay_is_menu_draw_count(594), "gameplay draw count is not menu");
    }

    /* Simulated device change (diagnostic): fires once, at its time, through
     * the same path a real controller uses, and "auto" follows it. */
    setenv("PS3_HOST_PROMPTS_SIM", "Xbox Wireless Controller@2", 1);
    set_env("auto");
    check(host_prompts_current() == HOST_PROMPT_SET_PS, "auto before any device = PS");
    check(host_prompts_sim_tick(1999) == 0, "sim does not fire early");
    check(host_prompts_current() == HOST_PROMPT_SET_PS, "no change before the sim time");
    check(host_prompts_sim_tick(2000) == 1, "sim fires at its time");
    check(host_prompts_current() == HOST_PROMPT_SET_XBOX, "auto follows the simulated Xbox pad");
    check(host_prompts_sim_tick(5000) == 0, "sim fires only once");
    unsetenv("PS3_HOST_PROMPTS_SIM");

    if (fails) {
        printf("test_host_prompts: %d FAIL\n", fails);
        return 1;
    }
    printf("test_host_prompts: OK\n");
    return 0;
}
