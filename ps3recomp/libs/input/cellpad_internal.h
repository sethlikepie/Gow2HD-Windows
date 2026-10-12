/*
 * ps3recomp - estado de pad do host, partilhado entre cellPad.c e os backends
 * (XInput/SDL2 inline + o backend nativo GameController em cellpad_gamecontroller.m).
 * Fonte unica da struct para o backend nativo preencher directamente.
 */
#ifndef PS3RECOMP_CELLPAD_INTERNAL_H
#define PS3RECOMP_CELLPAD_INTERNAL_H

#include "cellPad.h"   /* u8/u16/u32 + mascaras CELL_PAD_CTRL_* */
#include "host_prompts.h"

#define PAD_MAX_HOST_PORTS  4

typedef struct {
    int  connected;
    u16  buttons;           /* CELL_PAD_CTRL_* bitmask */
    /* Subset of `buttons` that came from a game controller. Keyboard bits are
     * merged into `buttons` only: the keyboard reaches the runtime overlay
     * through native key events (SDL / Win32), never through the pad path. */
    u16  controller_buttons;
    /* PS/Home on the controller (GameController buttonHome). Host-only: it
     * toggles the runtime overlay and is never translated into a guest bit. */
    u8   host_home;
    u8   analog_lx;         /* 0-255, center=128 */
    u8   analog_ly;
    u8   analog_rx;
    u8   analog_ry;
    /* CELL_PAD_HOST_AXIS_* bits whose analog_* value came from a game
     * controller. Keyboard/mouse overrides clear their axes' bits, so the
     * overlay deadzone never touches synthesised axes. */
    u8   controller_axes;
    u8   trigger_l2;        /* 0-255 */
    u8   trigger_r2;        /* 0-255 */
    /* Pressure-sensitive face buttons (0-255) */
    u8   press_right;
    u8   press_left;
    u8   press_up;
    u8   press_down;
    u8   press_triangle;
    u8   press_circle;
    u8   press_cross;
    u8   press_square;
    u8   press_l1;
    u8   press_r1;
    host_input_device device;
    int activity;
} PadHostState;

#endif /* PS3RECOMP_CELLPAD_INTERNAL_H */
