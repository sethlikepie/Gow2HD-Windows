#import <Foundation/Foundation.h>
#include <mach/mach_time.h>
#include <stdio.h>
#include <unistd.h>

#include "rsx_overlay_feedback.h"
#include "rsx_overlay_feedback_apple.h"

static volatile int s_ticks;
static void fake_tick(float intensity, float sharpness)
{
    (void)intensity; (void)sharpness;
    __sync_fetch_and_add(&s_ticks, 1);
}

int main(void)
{
    @autoreleasepool {
        mach_timebase_info_data_t tb;
        mach_timebase_info(&tb);
        rsx_overlay_feedback_apple_set_haptic(fake_tick);
        const uint64_t t0 = mach_absolute_time();
        for (int i = 0; i < 200; ++i)
            rsx_overlay_feedback_apple_play(RSX_OVERLAY_FEEDBACK_MOVE, 1, 1, NULL);
        const double ms = (double)(mach_absolute_time() - t0) * tb.numer / tb.denom / 1e6;
        if (ms > 50.0) { fprintf(stderr, "[FAIL] play blocked for %.1f ms\n", ms); return 1; }
        if (s_ticks != 200) { fprintf(stderr, "[FAIL] ticks %d\n", s_ticks); return 1; }
        rsx_overlay_feedback_apple_play(RSX_OVERLAY_FEEDBACK_OPEN, 0, 0, NULL);
        rsx_overlay_feedback_apple_play(99, 1, 1, NULL);  /* out of range: ignored */
        if (s_ticks != 200) { fprintf(stderr, "[FAIL] switches off still ticked\n"); return 1; }
        /* The engine starts or fails cleanly (no audio device on CI): never hangs. */
        for (int i = 0; i < 200 && rsx_overlay_feedback_apple_state() == 0; ++i) usleep(10000);
        const int state = rsx_overlay_feedback_apple_state();
        if (state != 1 && state != -1) { fprintf(stderr, "[FAIL] state %d\n", state); return 1; }

        /* Simulate what AVAudioEngineConfigurationChangeNotification does to
         * the engine on an output device change (AirPods/Bluetooth,
         * headphones plugged in): it stops itself without our s_state
         * noticing. play() right after that must never crash (an uncaught
         * NSException from -play on a stopped engine would abort the whole
         * process here) and must leave the engine either restarted or the
         * sound cleanly skipped -- never a call into a stopped engine. */
        rsx_overlay_feedback_apple_test_stop_engine();
        rsx_overlay_feedback_apple_play(RSX_OVERLAY_FEEDBACK_CONFIRM, 1, 0, NULL);
        for (int i = 0; i < 200; ++i) {
            /* Give the async ui_queue block time to run to completion; if it
             * threw, the process would already be dead by now. */
            usleep(5000);
        }
        const int state_after_stop = rsx_overlay_feedback_apple_state();
        if (state_after_stop != 1 && state_after_stop != -1) {
            fprintf(stderr, "[FAIL] state after simulated device change %d\n", state_after_stop);
            return 1;
        }
        /* One more play() must also survive regardless of which way the
         * restart went (running again, or cleanly given up). */
        rsx_overlay_feedback_apple_play(RSX_OVERLAY_FEEDBACK_BACK, 1, 1, NULL);
        if (s_ticks != 201) { fprintf(stderr, "[FAIL] haptics stopped ticking (ticks=%d)\n", s_ticks); return 1; }

        /* Idle: the UI engine (a second output IOProc next to the game's
         * AudioUnit) is paused once no sound played for the idle window, a
         * device change does not wake it, and the next sound restarts it. */
        int idle_checked = 0;
        if (rsx_overlay_feedback_apple_state() == 1) {
            rsx_overlay_feedback_apple_test_set_idle_seconds(0.1);
            rsx_overlay_feedback_apple_play(RSX_OVERLAY_FEEDBACK_MOVE, 1, 0, NULL);
            usleep(30000);
            if (!rsx_overlay_feedback_apple_test_engine_running()) {
                fprintf(stderr, "[FAIL] engine not running right after a sound\n"); return 1;
            }
            /* Sounds keep coming inside the window: never paused mid-use. */
            for (int i = 0; i < 6; ++i) {
                usleep(50000);
                rsx_overlay_feedback_apple_play(RSX_OVERLAY_FEEDBACK_MOVE, 1, 0, NULL);
            }
            usleep(30000);
            if (!rsx_overlay_feedback_apple_test_engine_running()) {
                fprintf(stderr, "[FAIL] engine paused while sounds kept playing\n"); return 1;
            }
            usleep(400000);
            if (rsx_overlay_feedback_apple_test_engine_running()) {
                fprintf(stderr, "[FAIL] engine still running after the idle window\n"); return 1;
            }
            rsx_overlay_feedback_apple_test_config_change();
            usleep(50000);
            if (rsx_overlay_feedback_apple_test_engine_running()) {
                fprintf(stderr, "[FAIL] device change woke the idle engine\n"); return 1;
            }
            rsx_overlay_feedback_apple_play(RSX_OVERLAY_FEEDBACK_CONFIRM, 1, 0, NULL);
            usleep(30000);
            if (rsx_overlay_feedback_apple_state() == 1 &&
                !rsx_overlay_feedback_apple_test_engine_running()) {
                fprintf(stderr, "[FAIL] next sound did not restart the engine\n"); return 1;
            }
            idle_checked = 1;
        }

        rsx_overlay_feedback_apple_shutdown();
        if (!idle_checked)
            printf("test_rsx_overlay_feedback_apple: idle pause not exercised (no audio engine)\n");
        printf("test_rsx_overlay_feedback_apple: PASS (engine %s, post-device-change %s)\n",
               state == 1 ? "running" : "unavailable",
               state_after_stop == 1 ? "restarted" : "skipped");
    }
    return 0;
}
