/* UI sounds on a dedicated AVAudioEngine (independent of the game's
 * AudioUnit) and UI haptics through the runtime's async haptics queue.
 * All engine work runs on one serial queue; play() only enqueues. */
#import <AVFoundation/AVFoundation.h>

#include <stdatomic.h>
#include <stdio.h>

#include "rsx_overlay_feedback.h"
#include "rsx_overlay_feedback_apple.h"
#include "rsx_overlay_log.h"
#include "rsx_overlay_ui_sound.h"

static void (*_Atomic s_haptic)(float, float);
static atomic_int s_state;           /* 0 idle, 1 running, -1 failed */
static AVAudioEngine* s_engine;      /* ui_queue only */
static AVAudioPlayerNode* s_player;
static AVAudioPCMBuffer* s_buffers[RSX_OVERLAY_FEEDBACK_COUNT];
static id s_config_change_observer; /* ui_queue only */
/* Idle pause: the UI engine is a second output IOProc next to the game's
 * AudioUnit, so it is paused once no sound played for s_idle_seconds.
 * ui_audio_start restarts a stopped/paused engine on the next sound. */
static double s_idle_seconds = 2.0;  /* ui_queue only (test hook aside) */
static uint64_t s_sound_generation;  /* ui_queue only */
static int s_idle_paused;            /* ui_queue only: paused by us, not by the OS */

static const float k_haptic[RSX_OVERLAY_FEEDBACK_COUNT][2] = {
    { 0.5f, 0.3f }, { 0.5f, 0.3f }, { 0.35f, 0.8f }, { 0.6f, 0.6f }, { 0.4f, 0.4f }, { 0.8f, 1.0f }
};

static dispatch_queue_t ui_queue(void)
{
    static dispatch_queue_t q;
    static dispatch_once_t once;
    dispatch_once(&once, ^{ q = dispatch_queue_create("ps3recomp.ui-sound", DISPATCH_QUEUE_SERIAL); });
    return q;
}

/* connect:to:format:/play throw or assert on some OS versions instead of
 * returning an error (see ui_player_play). Both have an error-returning
 * replacement as of macOS 27; the pre-27 path is still the one exercised by
 * every OS this runtime ships on today, so it stays and the deprecation
 * warning is silenced locally instead of chasing an unreleased-everywhere API. */
static BOOL ui_engine_connect(AVAudioEngine* engine, AVAudioPlayerNode* player,
                               AVAudioFormat* fmt, NSError** error)
{
    if (@available(macOS 27.0, *)) {
        return [engine connect:player to:engine.mainMixerNode format:fmt error:error];
    }
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    [engine connect:player to:engine.mainMixerNode format:fmt];
#pragma clang diagnostic pop
    if (error) *error = nil;
    return YES;
}

/* -play asserts "_engine->IsRunning()" (uncaught NSException) if the engine
 * was stopped out from under us -- e.g. AVAudioEngineConfigurationChangeNotification
 * on an output device change (AirPods/Bluetooth, headphones). The caller
 * (ui_audio_start) already checks isRunning/restarts before this is reached,
 * but the @try/@catch is kept as a second line of defense: a thrown
 * exception here would otherwise cross a GCD queue uncaught and abort the
 * whole game process over a UI click sound. */
static BOOL ui_player_play(AVAudioPlayerNode* player, NSError** error)
{
    if (@available(macOS 27.0, *)) {
        return [player playAndReturnError:error];
    }
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    @try {
        [player play];
    } @catch (NSException* exc) {
        if (error)
            *error = [NSError errorWithDomain:@"ps3recomp.ui-sound" code:1 userInfo:@{
                NSLocalizedDescriptionKey: exc.reason ?: @"AVAudioPlayerNode play threw"
            }];
        return NO;
    }
#pragma clang diagnostic pop
    if (error) *error = nil;
    return YES;
}

static void ui_engine_config_changed(NSNotification* note) /* any thread (NSNotificationCenter) */
{
    (void)note;
    /* Do not touch the engine here -- restart happens on ui_queue, on the
     * next play(), or right away below (best-effort, does not block). */
    dispatch_async(ui_queue(), ^{
        /* An engine we paused for idleness stays paused: the next sound
         * restarts it on whatever the output device is by then. */
        if (atomic_load(&s_state) > 0 && s_engine && !s_engine.isRunning && !s_idle_paused) {
            NSError* err = nil;
            if (![s_engine startAndReturnError:&err]) {
                rsx_overlay_log_write(RSX_OVERLAY_LOG_WARN,
                    "overlay: UI sound engine did not restart after a device change");
            }
        }
    });
}

static int ui_audio_start(void) /* ui_queue */
{
    const int state = atomic_load(&s_state);
    if (state < 0) return 0;
    s_idle_paused = 0;
    if (state > 0) {
        /* Already set up. The OS can stop the engine on its own -- an output
         * device change (switching to AirPods/Bluetooth, plugging in
         * headphones) posts AVAudioEngineConfigurationChangeNotification and
         * stops it -- without touching s_state, so isRunning (not s_state
         * alone) is what must be checked before ever calling play again. */
        if (s_engine.isRunning) return 1;
        NSError* err = nil;
        if ([s_engine startAndReturnError:&err]) return 1;
        char line[160];
        snprintf(line, sizeof line, "overlay: UI sound engine restart failed (%s)",
                 err ? err.localizedDescription.UTF8String : "unknown");
        rsx_overlay_log_write(RSX_OVERLAY_LOG_WARN, line);
        atomic_store(&s_state, -1);
        return 0;
    }
    AVAudioFormat* fmt = [[AVAudioFormat alloc] initStandardFormatWithSampleRate:RSX_OVERLAY_UI_SOUND_RATE
                                                                        channels:1];
    s_engine = [[AVAudioEngine alloc] init];
    s_player = [[AVAudioPlayerNode alloc] init];
    [s_engine attachNode:s_player];
    NSError* connectErr = nil;
    if (!ui_engine_connect(s_engine, s_player, fmt, &connectErr)) {
        char line[160];
        snprintf(line, sizeof line, "overlay: UI sounds unavailable (%s)",
                 connectErr ? connectErr.localizedDescription.UTF8String : "connect failed");
        rsx_overlay_log_write(RSX_OVERLAY_LOG_WARN, line);
        s_engine = nil;
        s_player = nil;
        atomic_store(&s_state, -1);
        return 0;
    }
    for (int e = 0; e < RSX_OVERLAY_FEEDBACK_COUNT; ++e) {
        const AVAudioFrameCount frames = (AVAudioFrameCount)rsx_overlay_ui_sound_length(e);
        AVAudioPCMBuffer* b = [[AVAudioPCMBuffer alloc] initWithPCMFormat:fmt frameCapacity:frames];
        b.frameLength = (AVAudioFrameCount)rsx_overlay_ui_sound_render(e, b.floatChannelData[0], frames);
        s_buffers[e] = b;
    }
    NSError* err = nil;
    if (![s_engine startAndReturnError:&err]) {
        char line[160];
        snprintf(line, sizeof line, "overlay: UI sounds unavailable (%s)",
                 err ? err.localizedDescription.UTF8String : "engine start failed");
        rsx_overlay_log_write(RSX_OVERLAY_LOG_WARN, line);
        s_engine = nil;
        s_player = nil;
        atomic_store(&s_state, -1);
        return 0;
    }
    s_config_change_observer = [[NSNotificationCenter defaultCenter]
        addObserverForName:AVAudioEngineConfigurationChangeNotification
                    object:s_engine
                     queue:nil
                usingBlock:^(NSNotification* note) { ui_engine_config_changed(note); }];
    NSError* playErr = nil;
    if (!ui_player_play(s_player, &playErr)) {
        char line[160];
        snprintf(line, sizeof line, "overlay: UI sound play failed (%s)",
                 playErr ? playErr.localizedDescription.UTF8String : "unknown");
        rsx_overlay_log_write(RSX_OVERLAY_LOG_WARN, line);
        atomic_store(&s_state, -1);
        return 0;
    }
    atomic_store(&s_state, 1);
    return 1;
}

static void ui_schedule_idle_pause(void) /* ui_queue */
{
    const uint64_t generation = ++s_sound_generation;
    const int64_t delay = (int64_t)(s_idle_seconds * (double)NSEC_PER_SEC);
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, delay), ui_queue(), ^{
        if (generation != s_sound_generation || atomic_load(&s_state) <= 0 ||
            s_engine == nil || !s_engine.isRunning)
            return;
        [s_engine pause];
        s_idle_paused = 1;
    });
}

void rsx_overlay_feedback_apple_set_haptic(void (*tick)(float, float))
{
    atomic_store(&s_haptic, tick);
}

void rsx_overlay_feedback_apple_play(int event, int sounds, int haptics, void* userdata)
{
    (void)userdata;
    if (event < 0 || event >= RSX_OVERLAY_FEEDBACK_COUNT)
        return;
    if (haptics) {
        void (*tick)(float, float) = atomic_load(&s_haptic);
        if (tick) tick(k_haptic[event][0], k_haptic[event][1]);
    }
    if (!sounds || atomic_load(&s_state) < 0)
        return;
    dispatch_async(ui_queue(), ^{
        if (!ui_audio_start()) return; /* not running and could not restart: skip this sound */
        [s_player scheduleBuffer:s_buffers[event] atTime:nil
                         options:AVAudioPlayerNodeBufferInterrupts completionHandler:nil];
        if (!s_player.playing) {
            NSError* playErr = nil;
            if (!ui_player_play(s_player, &playErr)) {
                char line[160];
                snprintf(line, sizeof line, "overlay: UI sound play failed (%s)",
                         playErr ? playErr.localizedDescription.UTF8String : "unknown");
                rsx_overlay_log_write(RSX_OVERLAY_LOG_WARN, line);
                atomic_store(&s_state, -1);
            }
        }
        ui_schedule_idle_pause();
    });
}

int rsx_overlay_feedback_apple_state(void)
{
    return atomic_load(&s_state);
}

void rsx_overlay_feedback_apple_shutdown(void)
{
    dispatch_sync(ui_queue(), ^{
        if (s_config_change_observer) {
            [[NSNotificationCenter defaultCenter] removeObserver:s_config_change_observer];
            s_config_change_observer = nil;
        }
        [s_player stop];
        [s_engine stop];
        s_player = nil;
        s_engine = nil;
        for (int e = 0; e < RSX_OVERLAY_FEEDBACK_COUNT; ++e) s_buffers[e] = nil;
        ++s_sound_generation; /* a pending idle pause becomes a no-op */
        s_idle_paused = 0;
        atomic_store(&s_state, 0);
    });
}

#ifdef RSX_OVERLAY_TESTING
/* Test-only: simulates what AVAudioEngineConfigurationChangeNotification does
 * to the engine (stops it) without touching s_state or tearing anything else
 * down, so a covering test can exercise the isRunning-but-state-stayed-1 path
 * that ui_audio_start's restart-or-skip logic exists for. */
void rsx_overlay_feedback_apple_test_stop_engine(void)
{
    dispatch_sync(ui_queue(), ^{
        if (s_engine) [s_engine stop];
    });
}

void rsx_overlay_feedback_apple_test_set_idle_seconds(double seconds)
{
    dispatch_sync(ui_queue(), ^{ s_idle_seconds = seconds; });
}

int rsx_overlay_feedback_apple_test_engine_running(void)
{
    __block int running = 0;
    dispatch_sync(ui_queue(), ^{ running = s_engine != nil && s_engine.isRunning; });
    return running;
}

void rsx_overlay_feedback_apple_test_config_change(void)
{
    ui_engine_config_changed(nil);
}
#endif
