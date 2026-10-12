#ifndef PS3RECOMP_RSX_OVERLAY_FEEDBACK_APPLE_H
#define PS3RECOMP_RSX_OVERLAY_FEEDBACK_APPLE_H
#ifdef __cplusplus
extern "C" {
#endif
/* The haptic hook (cpgc_ui_tick in the runtime); NULL disables haptics. */
void rsx_overlay_feedback_apple_set_haptic(void (*tick)(float intensity, float sharpness));
/* rsx_overlay_feedback_sink: never blocks the caller. */
void rsx_overlay_feedback_apple_play(int event, int sounds, int haptics, void* userdata);
int rsx_overlay_feedback_apple_state(void);
void rsx_overlay_feedback_apple_shutdown(void);
#ifdef RSX_OVERLAY_TESTING
/* Simulates the engine being stopped out from under us (as the OS does on an
 * output device change) without changing rsx_overlay_feedback_apple_state(). */
void rsx_overlay_feedback_apple_test_stop_engine(void);
/* Idle time after the last sound before the engine is paused (default 2 s). */
void rsx_overlay_feedback_apple_test_set_idle_seconds(double seconds);
/* Engine isRunning, read on the UI queue (0 when there is no engine). */
int rsx_overlay_feedback_apple_test_engine_running(void);
/* Delivers what AVAudioEngineConfigurationChangeNotification delivers. */
void rsx_overlay_feedback_apple_test_config_change(void);
#endif
#ifdef __cplusplus
}
#endif
#endif
