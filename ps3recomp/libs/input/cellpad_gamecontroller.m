/*
 * ps3recomp - backend nativo de input macOS (GameController) -- ver .h.
 *
 * GCController (DualShock 4/DualSense/Xbox/MFi nativos), GCKeyboard e GCMouse,
 * todos mapeados para o pad do PS3. Teclado/mouse estilo GoW (PC):
 *   WASD          -> stick esquerdo (mover)        Space  -> Cross  (saltar)
 *   Mouse move    -> stick direito (camera)        LMB    -> Square (ataque rapido)
 *   Setas         -> stick direito (fallback)      RMB    -> Triangle (ataque pesado)
 *   L-Shift       -> L1 (bloquear)                 E      -> Circle (agarrar/contexto)
 *   L-Ctrl        -> L2                            Q      -> R1
 *   R             -> R2                            Enter  -> Start   Tab -> Select
 * Rumble via Core Haptics do proprio controller (best-effort).
 */
#import <GameController/GameController.h>
#import <CoreHaptics/CoreHaptics.h>
#import <AppKit/AppKit.h>
#include "cellpad_gamecontroller.h"
#include "host_prompts.h"
#include "rsx_overlay.h"
#include <math.h>
#include <string.h>

/* ---- estado de mouse acumulado entre polls (mouse-look no stick direito) ---- */
static float s_mouse_dx, s_mouse_dy;         /* deltas acumulados desde o ultimo poll */
static const float s_mouse_sens = 0.06f;     /* delta-px -> deflexao de stick */

/* rumble: um engine Core Haptics por porto (lazy) */
static CHHapticEngine*          s_haptic_engine[PAD_MAX_HOST_PORTS];
static id<CHHapticPatternPlayer> s_haptic_player[PAD_MAX_HOST_PORTS];

static u8 axis_to_u8(float v /* -1..1 */) {
    int c = (int)lroundf(128.0f + v * 127.0f);
    if (c < 0) c = 0; if (c > 255) c = 255;
    return (u8)c;
}
static u8 unit_to_u8(float v /* 0..1 */) {
    int c = (int)lroundf(v * 255.0f);
    if (c < 0) c = 0; if (c > 255) c = 255;
    return (u8)c;
}

/* Ask macOS not to act on the PS/Home press while we run (Launchpad / Game
 * Center overlay). The setter talks to the game controller daemon and can
 * block, so it runs once per controller -- on connect, and for controllers
 * already connected at init -- on its own serial queue, never on the guest
 * thread that polls the pad. */
static void disable_home_gesture(GCController* c) {
    static dispatch_queue_t q; static dispatch_once_t once;
    dispatch_once(&once, ^{ q = dispatch_queue_create("ps3recomp.pad.gesture", DISPATCH_QUEUE_SERIAL); });
    if (!c) return;
    dispatch_async(q, ^{
        if (@available(macOS 11.0, *)) {
            GCControllerButtonInput* home = c.extendedGamepad.buttonHome;
            if (home && home.preferredSystemGestureState != GCSystemGestureStateDisabled)
                home.preferredSystemGestureState = GCSystemGestureStateDisabled;
        }
    });
}

void cpgc_init(void) {
    /* Home gesture: controllers connected now and every later connect. */
    for (GCController* c in GCController.controllers)
        disable_home_gesture(c);
    static dispatch_once_t observe_once;   /* cellPadInit may run again after cellPadEnd */
    dispatch_once(&observe_once, ^{
        [[NSNotificationCenter defaultCenter] addObserverForName:GCControllerDidConnectNotification
            object:nil queue:nil usingBlock:^(NSNotification* _Nonnull note) {
                disable_home_gesture((GCController*)note.object);
            }];
    });
    /* GameController descobre controllers/teclado/mouse ligados por si; so
     * precisamos do handler de mouse para acumular deltas entre polls. */
    if (@available(macOS 11.0, *)) {
        GCMouse* m = GCMouse.current;
        if (m) {
            m.mouseInput.mouseMovedHandler = ^(GCMouseInput* _Nonnull mouse, float dx, float dy) {
                (void)mouse; s_mouse_dx += dx; s_mouse_dy += dy;
            };
        }
        /* re-arma o handler quando um novo mouse liga */
        [[NSNotificationCenter defaultCenter] addObserverForName:GCMouseDidConnectNotification
            object:nil queue:nil usingBlock:^(NSNotification* _Nonnull note) {
                GCMouse* nm = note.object;
                nm.mouseInput.mouseMovedHandler = ^(GCMouseInput* _Nonnull mouse, float dx, float dy) {
                    (void)mouse; s_mouse_dx += dx; s_mouse_dy += dy;
                };
            }];
    }
}

static dispatch_queue_t rumble_queue(void);
static id<CHHapticPatternPlayer> s_rumble_player[PAD_MAX_HOST_PORTS];
void cpgc_shutdown(void) {
    /* Same serial queue as the rumble work, so shutdown never races it. */
    dispatch_sync(rumble_queue(), ^{
        for (int i = 0; i < PAD_MAX_HOST_PORTS; i++) {
            s_haptic_player[i] = nil;
            s_rumble_player[i] = nil;
            if (s_haptic_engine[i]) { [s_haptic_engine[i] stopWithCompletionHandler:nil]; s_haptic_engine[i] = nil; }
        }
    });
}

/* --- teclado -> pad (mapeamento estilo GoW) --- */
static int poll_keyboard_mouse(PadHostState* st) {
    if (@available(macOS 11.0, *)) {} else return 0;
    /* GCKeyboard/GCMouse see keys and motion system-wide: typing in another
     * app drove Kratos and switched off the bring-up pad. Keyboard and mouse
     * only count while the game is the active app; controllers always do. */
    if (!NSApp.isActive) {
        s_mouse_dx = s_mouse_dy = 0;
        return 0;
    }
    GCKeyboard* kb = GCKeyboard.coalescedKeyboard;
    GCKeyboardInput* k = kb.keyboardInput;
    int have_kbd = (k != nil);
    GCMouse* mouse = GCMouse.current;
    int have_mouse = (mouse != nil);
    if (!have_kbd && !have_mouse) return 0;

    st->connected = 1;
    u16 b = st->buttons;   /* combina com o que ja veio do controller */
    int active = 0;

    #define KB(code) (have_kbd && [k buttonForKeyCode:(code)].pressed)
    /* stick esquerdo: WASD */
    float lx = 0, ly = 0;
    if (KB(GCKeyCodeKeyD)) lx += 1; if (KB(GCKeyCodeKeyA)) lx -= 1;
    if (KB(GCKeyCodeKeyW)) ly -= 1; if (KB(GCKeyCodeKeyS)) ly += 1;   /* PS3: -Y = cima */
    if (lx || ly) {
        st->analog_lx = axis_to_u8(lx); st->analog_ly = axis_to_u8(ly); active = 1;
        st->controller_axes &= (u8)~(CELL_PAD_HOST_AXIS_LX | CELL_PAD_HOST_AXIS_LY);
    }

    /* stick direito: mouse (camera) */
    float rx = s_mouse_dx * s_mouse_sens, ry = s_mouse_dy * s_mouse_sens;
    if (rx < -1) rx = -1; if (rx > 1) rx = 1; if (ry < -1) ry = -1; if (ry > 1) ry = 1;
    if (rx || ry) {
        st->analog_rx = axis_to_u8(rx); st->analog_ry = axis_to_u8(ry); active = 1;
        st->controller_axes &= (u8)~(CELL_PAD_HOST_AXIS_RX | CELL_PAD_HOST_AXIS_RY);
    }
    s_mouse_dx = s_mouse_dy = 0;   /* consome os deltas */

    /* botoes: teclado + mouse */
    int lmb = have_mouse && mouse.mouseInput.leftButton.pressed;
    int rmb = have_mouse && mouse.mouseInput.rightButton.pressed;
    int mmb = have_mouse && mouse.mouseInput.middleButton &&
              mouse.mouseInput.middleButton.pressed;
    /* D-pad: setas */
    if (KB(GCKeyCodeUpArrow))    { b |= CELL_PAD_CTRL_UP;    st->press_up = 255;    active = 1; }
    if (KB(GCKeyCodeDownArrow))  { b |= CELL_PAD_CTRL_DOWN;  st->press_down = 255;  active = 1; }
    if (KB(GCKeyCodeLeftArrow))  { b |= CELL_PAD_CTRL_LEFT;  st->press_left = 255;  active = 1; }
    if (KB(GCKeyCodeRightArrow)) { b |= CELL_PAD_CTRL_RIGHT; st->press_right = 255; active = 1; }
    if (KB(GCKeyCodeSpacebar))                        { b |= CELL_PAD_CTRL_CROSS; active = 1; }
    if (lmb || KB(GCKeyCodeKeyJ))                     { b |= CELL_PAD_CTRL_SQUARE; active = 1; }
    if (rmb || KB(GCKeyCodeKeyK))                     { b |= CELL_PAD_CTRL_TRIANGLE; active = 1; }
    if (KB(GCKeyCodeKeyE))                            { b |= CELL_PAD_CTRL_CIRCLE; active = 1; }
    if (KB(GCKeyCodeLeftShift))                       { b |= CELL_PAD_CTRL_L1; active = 1; }
    if (KB(GCKeyCodeKeyQ))                            { b |= CELL_PAD_CTRL_R1; active = 1; }
    if (KB(GCKeyCodeLeftControl))                   { b |= CELL_PAD_CTRL_L2; st->trigger_l2 = 255; active = 1; }
    if (KB(GCKeyCodeKeyR))                          { b |= CELL_PAD_CTRL_R2; st->trigger_r2 = 255; active = 1; }
    if (KB(GCKeyCodeReturnOrEnter))                   { b |= CELL_PAD_CTRL_START; active = 1; }
    if (KB(GCKeyCodeTab))                             { b |= CELL_PAD_CTRL_SELECT; active = 1; }
    if (KB(GCKeyCodeKeyF))                            { b |= CELL_PAD_CTRL_L3; active = 1; }
    if (mmb || KB(GCKeyCodeKeyG))                     { b |= CELL_PAD_CTRL_R3; active = 1; }
    #undef KB
    st->buttons = b;
    st->press_cross = (b & CELL_PAD_CTRL_CROSS) ? 255 : 0;
    st->press_circle = (b & CELL_PAD_CTRL_CIRCLE) ? 255 : 0;
    st->press_square = (b & CELL_PAD_CTRL_SQUARE) ? 255 : 0;
    st->press_triangle = (b & CELL_PAD_CTRL_TRIANGLE) ? 255 : 0;
    st->press_l1 = (b & CELL_PAD_CTRL_L1) ? 255 : 0;
    st->press_r1 = (b & CELL_PAD_CTRL_R1) ? 255 : 0;
    return active;
}

/* --- controller (GCExtendedGamepad) -> pad --- */
static int poll_controller(GCExtendedGamepad* gp, PadHostState* st) {
    if (!gp) return 0;
    u16 b = 0;
    if (gp.buttonA.pressed) b |= CELL_PAD_CTRL_CROSS;
    if (gp.buttonB.pressed) b |= CELL_PAD_CTRL_CIRCLE;
    if (gp.buttonX.pressed) b |= CELL_PAD_CTRL_SQUARE;
    if (gp.buttonY.pressed) b |= CELL_PAD_CTRL_TRIANGLE;
    if (gp.dpad.up.pressed) b |= CELL_PAD_CTRL_UP;
    if (gp.dpad.down.pressed) b |= CELL_PAD_CTRL_DOWN;
    if (gp.dpad.left.pressed) b |= CELL_PAD_CTRL_LEFT;
    if (gp.dpad.right.pressed) b |= CELL_PAD_CTRL_RIGHT;
    if (gp.leftShoulder.pressed) b |= CELL_PAD_CTRL_L1;
    if (gp.rightShoulder.pressed) b |= CELL_PAD_CTRL_R1;
    if (gp.buttonMenu.pressed) b |= CELL_PAD_CTRL_START;
    if (gp.buttonOptions && gp.buttonOptions.pressed) b |= CELL_PAD_CTRL_SELECT;
    if (gp.leftThumbstickButton && gp.leftThumbstickButton.pressed) b |= CELL_PAD_CTRL_L3;
    if (gp.rightThumbstickButton && gp.rightThumbstickButton.pressed) b |= CELL_PAD_CTRL_R3;
    float lt = gp.leftTrigger.value, rt = gp.rightTrigger.value;
    if (lt > 0.12f) b |= CELL_PAD_CTRL_L2;
    if (rt > 0.12f) b |= CELL_PAD_CTRL_R2;
    if (@available(macOS 11.0, *)) {
        GCControllerButtonInput* home = gp.buttonHome;
        if (home) {
            /* No gesture-state work here: this runs on the guest thread
             * (cellPadGetData). See disable_home_gesture. */
            st->host_home = home.pressed ? 1 : 0;
        }
    }
    st->buttons = b;
    st->analog_lx = axis_to_u8(gp.leftThumbstick.xAxis.value);
    st->analog_ly = axis_to_u8(-gp.leftThumbstick.yAxis.value);   /* GC +Y=cima; PS3 -Y=cima */
    st->analog_rx = axis_to_u8(gp.rightThumbstick.xAxis.value);
    st->analog_ry = axis_to_u8(-gp.rightThumbstick.yAxis.value);
    st->controller_axes = CELL_PAD_HOST_AXIS_ALL;
    st->trigger_l2 = unit_to_u8(lt);
    st->trigger_r2 = unit_to_u8(rt);
    st->press_cross    = unit_to_u8(gp.buttonA.value);
    st->press_circle   = unit_to_u8(gp.buttonB.value);
    st->press_square   = unit_to_u8(gp.buttonX.value);
    st->press_triangle = unit_to_u8(gp.buttonY.value);
    st->press_up = (b&CELL_PAD_CTRL_UP)?255:0; st->press_down=(b&CELL_PAD_CTRL_DOWN)?255:0;
    st->press_left=(b&CELL_PAD_CTRL_LEFT)?255:0; st->press_right=(b&CELL_PAD_CTRL_RIGHT)?255:0;
    st->press_l1=(b&CELL_PAD_CTRL_L1)?255:0; st->press_r1=(b&CELL_PAD_CTRL_R1)?255:0;
    st->connected = 1;
    return 1;
}

void cpgc_poll(PadHostState* states, int max_ports) {
    int pad_used = 0;   /* a controller had input in this poll */
    for (int i = 0; i < max_ports; i++) memset(&states[i], 0, sizeof(states[i]));
    NSArray<GCController*>* ctrls = GCController.controllers;
    for (int i = 0; i < max_ports; i++) {
        if ((NSUInteger)i < ctrls.count) {
            poll_controller(ctrls[(NSUInteger)i].extendedGamepad, &states[i]);
            /* Before the keyboard merge below: only these reach the overlay. */
            states[i].controller_buttons = states[i].buttons;
            const char* cat = NULL;
            if (@available(macOS 11.0, *))
                cat = ctrls[(NSUInteger)i].productCategory.UTF8String;
            states[i].device = host_input_classify(cat);
            states[i].activity = host_input_state_active(
                states[i].buttons, states[i].analog_lx, states[i].analog_ly,
                states[i].analog_rx, states[i].analog_ry,
                states[i].trigger_l2, states[i].trigger_r2);
            if (states[i].activity) {
                host_prompts_note_pad(cat);
                pad_used = 1;
            }
        }
    }
    if (max_ports > 0 && ctrls.count == 0)
        host_prompts_note_no_pad();   /* keyboard/mouse are the only pad */

    /* teclado/mouse combinam no porto 0 (ou passam a ser o porto 0 se nao ha
     * controller) -- estado neutro dos analogicos primeiro se so kbd */
    if (max_ports > 0) {
        if (!states[0].connected) {
            states[0].analog_lx = states[0].analog_ly = 128;
            states[0].analog_rx = states[0].analog_ry = 128;
        }
        const PadHostState before = states[0];
        const int keyboard_active = poll_keyboard_mouse(&states[0]);
        if (keyboard_active || !before.connected)
            states[0].device = HOST_INPUT_KEYBOARD;
        states[0].activity = host_input_state_active(
            states[0].buttons, states[0].analog_lx, states[0].analog_ly,
            states[0].analog_rx, states[0].analog_ry,
            states[0].trigger_l2, states[0].trigger_r2);
        /* Prompts follow keys and mouse BUTTONS only. Mouse motion alone (the
         * right stick) flipped the icons back to the keyboard whenever the
         * captured mouse moved a little, so a player on a controller never
         * kept the controller glyphs. A controller input in the same poll wins. */
        const int kb_pressed = (states[0].buttons & ~before.buttons) != 0
                            || states[0].analog_lx != before.analog_lx
                            || states[0].analog_ly != before.analog_ly;
        if (keyboard_active && kb_pressed && !pad_used && states[0].activity)
            host_prompts_note_keyboard();
    }

    /* Top-strip status for the overlay, at most once a second. The battery is
     * only reported when the controller knows it (never a made-up value). */
    static double s_status_t;
    const double now = CFAbsoluteTimeGetCurrent();
    if (now - s_status_t >= 1.0) {
        s_status_t = now;
        int pct = -1;
        if (ctrls.count > 0) {
            if (@available(macOS 11.0, *)) {
                GCDeviceBattery* bat = ctrls[0].battery;
                if (bat && bat.batteryState != GCDeviceBatteryStateUnknown)
                    pct = (int)lroundf(bat.batteryLevel * 100.0f);
            }
        }
        rsx_overlay_publish_controller_status(ctrls.count > 0, pct);
    }
}

/* Rumble. cellPadSetActDirect calls this from the GUEST thread (giant lock
 * and the pad lwmutex held), every frame. CoreHaptics player creation is a
 * synchronous XPC round trip to the haptics daemon: measured 2026-09-22 as a
 * full-game stall (main thread parked in -[CHHapticEngine
 * createPlayerWithPattern:] -> xpc_connection_send_message_with_reply_sync).
 * So the guest only publishes the wanted intensity; a serial queue does the
 * CoreHaptics work, coalescing to the latest value, creating a player only
 * when the value changes (or to refresh a long rumble) and stopping on 0. */
static float            s_rumble_want[PAD_MAX_HOST_PORTS];
static volatile int     s_rumble_queued[PAD_MAX_HOST_PORTS];
static float            s_rumble_applied[PAD_MAX_HOST_PORTS];
static double           s_rumble_t[PAD_MAX_HOST_PORTS];

static dispatch_queue_t rumble_queue(void) {
    static dispatch_queue_t q; static dispatch_once_t once;
    dispatch_once(&once, ^{ q = dispatch_queue_create("ps3recomp.rumble", DISPATCH_QUEUE_SERIAL); });
    return q;
}

static void rumble_apply(int port) API_AVAILABLE(macos(11.0)) {
    __sync_lock_release(&s_rumble_queued[port]);
    const float want = s_rumble_want[port];
    const double now = CFAbsoluteTimeGetCurrent();
    if (want <= 0.0f) {
        if (s_rumble_player[port]) { [s_rumble_player[port] stopAtTime:0 error:nil]; s_rumble_player[port] = nil; }
        s_rumble_applied[port] = 0.0f;
        return;
    }
    if (s_rumble_player[port] && fabsf(want - s_rumble_applied[port]) < 0.02f && now - s_rumble_t[port] < 0.15)
        return;                                   /* same rumble, still playing */
    NSArray<GCController*>* ctrls = GCController.controllers;
    if ((NSUInteger)port >= ctrls.count) return;
    GCController* c = ctrls[(NSUInteger)port];
    if (!c.haptics) return;
    if (!s_haptic_engine[port]) {
        s_haptic_engine[port] = [c.haptics createEngineWithLocality:GCHapticsLocalityDefault];
        [s_haptic_engine[port] startAndReturnError:nil];
    }
    CHHapticEngine* eng = s_haptic_engine[port];
    if (!eng) return;
    CHHapticEventParameter* ip = [[CHHapticEventParameter alloc]
        initWithParameterID:CHHapticEventParameterIDHapticIntensity value:want];
    CHHapticEvent* ev = [[CHHapticEvent alloc]
        initWithEventType:CHHapticEventTypeHapticContinuous parameters:@[ip]
        relativeTime:0 duration:0.25];
    NSError* err = nil;
    CHHapticPattern* pat = [[CHHapticPattern alloc] initWithEvents:@[ev] parameters:@[] error:&err];
    if (!pat) return;
    id<CHHapticPatternPlayer> pl = [eng createPlayerWithPattern:pat error:&err];
    if (!pl) return;
    if (s_rumble_player[port]) [s_rumble_player[port] stopAtTime:0 error:nil];
    [pl startAtTime:0 error:nil];
    s_rumble_player[port] = pl;
    s_rumble_applied[port] = want;
    s_rumble_t[port] = now;
}

void cpgc_rumble(int port, float motor_large, float motor_small) {
    if (port < 0 || port >= PAD_MAX_HOST_PORTS) return;
    if (@available(macOS 11.0, *)) {} else return;
    s_rumble_want[port] = fmaxf(motor_large, motor_small);
    if (__sync_lock_test_and_set(&s_rumble_queued[port], 1)) return;   /* a block is pending: it reads the latest */
    dispatch_async(rumble_queue(), ^{ if (@available(macOS 11.0, *)) rumble_apply(port); });
}

/* UI haptics (overlay): one transient on port 0, on the same serial queue as
 * rumble -- CoreHaptics player creation is a synchronous XPC round trip and
 * must never run on the UI or guest thread. Ticks coalesce while one is queued. */
static volatile int s_ui_tick_queued;

static void ui_tick_apply(float intensity, float sharpness) API_AVAILABLE(macos(11.0)) {
    NSArray<GCController*>* ctrls = GCController.controllers;
    if (ctrls.count == 0) return;
    GCController* c = ctrls[0];
    if (!c.haptics) return;
    if (!s_haptic_engine[0]) {
        s_haptic_engine[0] = [c.haptics createEngineWithLocality:GCHapticsLocalityDefault];
        [s_haptic_engine[0] startAndReturnError:nil];
    }
    CHHapticEngine* eng = s_haptic_engine[0];
    if (!eng) return;
    CHHapticEventParameter* ip = [[CHHapticEventParameter alloc]
        initWithParameterID:CHHapticEventParameterIDHapticIntensity value:intensity];
    CHHapticEventParameter* sp = [[CHHapticEventParameter alloc]
        initWithParameterID:CHHapticEventParameterIDHapticSharpness value:sharpness];
    CHHapticEvent* ev = [[CHHapticEvent alloc] initWithEventType:CHHapticEventTypeHapticTransient
                                                      parameters:@[ip, sp] relativeTime:0];
    NSError* err = nil;
    CHHapticPattern* pat = [[CHHapticPattern alloc] initWithEvents:@[ev] parameters:@[] error:&err];
    if (!pat) return;
    id<CHHapticPatternPlayer> pl = [eng createPlayerWithPattern:pat error:&err];
    [pl startAtTime:0 error:nil];
}

void cpgc_ui_tick(float intensity, float sharpness) {
    if (@available(macOS 11.0, *)) {} else return;
    if (__sync_lock_test_and_set(&s_ui_tick_queued, 1)) return;
    dispatch_async(rumble_queue(), ^{
        __sync_lock_release(&s_ui_tick_queued);
        if (@available(macOS 11.0, *)) ui_tick_apply(intensity, sharpness);
    });
}
