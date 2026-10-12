#include <stdio.h>

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  include <direct.h>
#  include <windows.h>
#  define TEST_MKDIR(path) _mkdir(path)
#  define TEST_RMDIR(path) _rmdir(path)
#else
#  include <sys/stat.h>
#  include <sys/types.h>
#  include <unistd.h>
#  define TEST_MKDIR(path) mkdir((path), 0700)
#  define TEST_RMDIR(path) rmdir(path)
#endif

#include "rsx_overlay_settings.h"

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "[FAIL] %s:%d: %s\n", __func__, __LINE__, #expr); \
        return 0; \
    } \
} while (0)

static int make_temp_dir(char* out, size_t capacity)
{
#if defined(_WIN32)
    char temp[MAX_PATH];
    char name[MAX_PATH];
    DWORD n = GetTempPathA((DWORD)sizeof(temp), temp);
    if (n == 0 || n >= sizeof(temp) || GetTempFileNameA(temp, "rso", 0, name) == 0)
        return 0;
    DeleteFileA(name);
    if (!CreateDirectoryA(name, NULL))
        return 0;
    if (strlen(name) + 1 > capacity)
        return 0;
    strcpy(out, name);
    return 1;
#else
    const char pattern[] = "/tmp/rsx-overlay-settings-XXXXXX";
    if (sizeof(pattern) > capacity)
        return 0;
    memcpy(out, pattern, sizeof(pattern));
    return mkdtemp(out) != NULL;
#endif
}

static int path_join(char* out, size_t capacity, const char* dir, const char* name)
{
    int n = snprintf(out, capacity, "%s/%s", dir, name);
    return n > 0 && (size_t)n < capacity;
}

static int write_text(const char* path, const char* text)
{
    FILE* file = fopen(path, "wb");
    size_t len = strlen(text);
    int closed = 0;

    if (file == NULL)
        return 0;
    if (fwrite(text, 1, len, file) == len) {
        closed = 1;
        if (fclose(file) == 0)
            return 1;
    }
    if (!closed)
        fclose(file);
    return 0;
}

static int write_bytes(const char* path, const void* data, size_t length)
{
    FILE* file = fopen(path, "wb");
    int closed = 0;

    if (file == NULL)
        return 0;
    if (fwrite(data, 1, length, file) == length) {
        closed = 1;
        if (fclose(file) == 0)
            return 1;
    }
    if (!closed)
        fclose(file);
    return 0;
}

static int maps_match(const rsx_overlay_settings* a, const rsx_overlay_settings* b)
{
    return memcmp(a->host_button_to_guest_button, b->host_button_to_guest_button,
                  sizeof(a->host_button_to_guest_button)) == 0;
}

static int settings_match(const rsx_overlay_settings* a, const rsx_overlay_settings* b)
{
    return a->version == b->version &&
           a->overlay_enabled == b->overlay_enabled &&
           a->overlay_visible == b->overlay_visible &&
           a->diagnostics_visible == b->diagnostics_visible &&
           a->toggle_key == b->toggle_key &&
           a->window_width == b->window_width &&
           a->window_height == b->window_height &&
           a->fullscreen == b->fullscreen &&
           a->vsync == b->vsync &&
           a->analog_deadzone == b->analog_deadzone &&
           a->ui_sounds == b->ui_sounds && a->ui_haptics == b->ui_haptics &&
           a->game_mute == b->game_mute && a->vibration == b->vibration &&
           maps_match(a, b);
}

static int test_defaults(void)
{
    rsx_overlay_settings settings;
    size_t i;

    rsx_overlay_settings_defaults(&settings);
    CHECK(settings.version == RSX_OVERLAY_SETTINGS_VERSION);
    CHECK(settings.overlay_enabled == 1);
    CHECK(settings.overlay_visible == 0);
    CHECK(settings.diagnostics_visible == 0);
    CHECK(settings.toggle_key == RSX_OVERLAY_TOGGLE_KEY_F1);
    CHECK(settings.window_width == 1280);
    CHECK(settings.window_height == 720);
    CHECK(settings.fullscreen == 0);
    CHECK(settings.vsync == 1);
    CHECK(settings.analog_deadzone == 0);
    CHECK(settings.ui_sounds == 1);
    CHECK(settings.ui_haptics == 1);
    CHECK(settings.game_mute == 0);
    CHECK(settings.vibration == 1);
    for (i = 0; i < RSX_OVERLAY_HOST_BUTTON_COUNT; ++i)
        CHECK(settings.host_button_to_guest_button[i] == (uint16_t)(1u << i));
    return 1;
}

static int test_valid_round_trip(const char* dir)
{
    char path[512];
    rsx_overlay_settings written;
    rsx_overlay_settings loaded;
    size_t i;

    CHECK(path_join(path, sizeof(path), dir, "round-trip.settings"));
    rsx_overlay_settings_defaults(&written);
    written.overlay_visible = 1;
    written.diagnostics_visible = 1;
    written.toggle_key = RSX_OVERLAY_TOGGLE_KEY_F2;
    written.window_width = 1920;
    written.window_height = 1080;
    written.fullscreen = 1;
    written.vsync = 0;
    written.analog_deadzone = 35;
    written.ui_sounds = 0;
    written.ui_haptics = 0;
    written.game_mute = 1;
    written.vibration = 0;
    written.host_button_to_guest_button[0] = 2;
    written.host_button_to_guest_button[1] = 1;
    CHECK(rsx_overlay_settings_save(&written, path) == 1);
    CHECK(rsx_overlay_settings_load(&loaded, path) == 1);
    CHECK(settings_match(&written, &loaded));
#if !defined(_WIN32)
    {
        struct stat st;
        CHECK(stat(path, &st) == 0);
        CHECK((st.st_mode & 0077) == 0);
    }
#endif
    for (i = 0; i < RSX_OVERLAY_HOST_BUTTON_COUNT; ++i)
        CHECK(loaded.host_button_to_guest_button[i] == written.host_button_to_guest_button[i]);
    remove(path);
    return 1;
}

static int test_invalid_fields_keep_defaults(const char* dir)
{
    char path[512];
    rsx_overlay_settings settings;
    rsx_overlay_settings defaults;
    const char* text =
        "version=1\n"
        "overlay_enabled=2\n"
        "overlay_visible=1\n"
        "toggle_key=99\n"
        "window_width=18446744073709553600\n"
        "window_height=99999\n"
        "fullscreen=2\n"
        "vsync=3\n"
        "analog_deadzone=101\n"
        "host_button_to_guest_button=1,1,4,8,16,32,64,128,256,512,1024,2048,4096,8192,16384,32768\n"
        "future_key=42\n";

    CHECK(path_join(path, sizeof(path), dir, "invalid.settings"));
    CHECK(write_text(path, text));
    rsx_overlay_settings_defaults(&defaults);
    CHECK(rsx_overlay_settings_load(&settings, path) == 1);
    CHECK(settings.overlay_enabled == defaults.overlay_enabled);
    CHECK(settings.overlay_visible == 1);
    CHECK(settings.toggle_key == defaults.toggle_key);
    CHECK(settings.window_width == defaults.window_width);
    CHECK(settings.window_height == defaults.window_height);
    CHECK(settings.fullscreen == defaults.fullscreen);
    CHECK(settings.vsync == defaults.vsync);
    CHECK(settings.analog_deadzone == defaults.analog_deadzone);
    CHECK(maps_match(&settings, &defaults));
    remove(path);
    return 1;
}

static int test_truncated_file_keeps_valid_prior_settings(const char* dir)
{
    char path[512];
    rsx_overlay_settings settings;
    rsx_overlay_settings defaults;

    CHECK(path_join(path, sizeof(path), dir, "truncated.settings"));
    CHECK(write_text(path, "version=1\noverlay_enabled=0\nwindow_width="));
    rsx_overlay_settings_defaults(&defaults);
    CHECK(rsx_overlay_settings_load(&settings, path) == 1);
    CHECK(settings.overlay_enabled == 0);
    CHECK(settings.window_width == defaults.window_width);
    remove(path);
    return 1;
}

static int test_overlong_line_is_ignored(const char* dir)
{
    char path[512];
    rsx_overlay_settings settings;
    rsx_overlay_settings defaults;
    FILE* file;
    size_t i;

    CHECK(path_join(path, sizeof(path), dir, "overlong-line.settings"));
    file = fopen(path, "wb");
    CHECK(file != NULL);
    CHECK(fputs("version=", file) >= 0);
    for (i = 0; i < 512; ++i)
        CHECK(fputc('1', file) != EOF);
    CHECK(fputs("\noverlay_visible=1\n", file) >= 0);
    CHECK(fclose(file) == 0);
    rsx_overlay_settings_defaults(&defaults);
    CHECK(rsx_overlay_settings_load(&settings, path) == 0);
    CHECK(settings_match(&settings, &defaults));
    remove(path);
    return 1;
}

static int test_oversized_file_uses_defaults(const char* dir)
{
    char path[512];
    rsx_overlay_settings settings;
    rsx_overlay_settings defaults;
    FILE* file;
    size_t i;

    CHECK(path_join(path, sizeof(path), dir, "oversized.settings"));
    file = fopen(path, "wb");
    CHECK(file != NULL);
    CHECK(fputs("version=1\noverlay_visible=1\n", file) >= 0);
    for (i = 0; i < 64u * 1024u; ++i)
        CHECK(fputc('\n', file) != EOF);
    CHECK(fclose(file) == 0);
    rsx_overlay_settings_defaults(&defaults);
    CHECK(rsx_overlay_settings_load(&settings, path) == 0);
    CHECK(settings_match(&settings, &defaults));
    remove(path);
    return 1;
}

static int test_embedded_nul_discards_only_its_line(const char* dir)
{
    static const unsigned char text[] =
        "version=1\n"
        "overlay_visible=1\0garbage\n"
        "diagnostics_visible=1\n";
    char path[512];
    rsx_overlay_settings settings;
    rsx_overlay_settings defaults;

    CHECK(path_join(path, sizeof(path), dir, "embedded-nul.settings"));
    CHECK(write_bytes(path, text, sizeof(text) - 1));
    rsx_overlay_settings_defaults(&defaults);
    CHECK(rsx_overlay_settings_load(&settings, path) == 1);
    CHECK(settings.overlay_visible == defaults.overlay_visible);
    CHECK(settings.diagnostics_visible == 1);
    remove(path);
    return 1;
}

static int test_null_and_empty_paths_use_temp_home(const char* dir)
{
#if defined(__APPLE__)
    char path[512];
    char old_home[512];
    const char* previous_home = getenv("HOME");
    rsx_overlay_settings settings;
    rsx_overlay_settings loaded;
    int had_home = previous_home != NULL;

    if (had_home) {
        CHECK(strlen(previous_home) < sizeof(old_home));
        strcpy(old_home, previous_home);
    }
    CHECK(setenv("HOME", dir, 1) == 0);
    rsx_overlay_settings_defaults(&settings);
    settings.overlay_visible = 1;
    CHECK(rsx_overlay_settings_save(&settings, NULL) == 1);
    CHECK(rsx_overlay_settings_load(&loaded, "") == 1);
    CHECK(settings_match(&settings, &loaded));
    CHECK(snprintf(path, sizeof(path),
                   "%s/Library/Application Support/ps3recomp/gow2/runtime-overlay.settings",
                   dir) > 0);
    CHECK(remove(path) == 0);
    CHECK(snprintf(path, sizeof(path), "%s/Library/Application Support/ps3recomp/gow2", dir) > 0);
    CHECK(TEST_RMDIR(path) == 0);
    CHECK(snprintf(path, sizeof(path), "%s/Library/Application Support/ps3recomp", dir) > 0);
    CHECK(TEST_RMDIR(path) == 0);
    CHECK(snprintf(path, sizeof(path), "%s/Library/Application Support", dir) > 0);
    CHECK(TEST_RMDIR(path) == 0);
    CHECK(snprintf(path, sizeof(path), "%s/Library", dir) > 0);
    CHECK(TEST_RMDIR(path) == 0);
    if (had_home)
        CHECK(setenv("HOME", old_home, 1) == 0);
    else
        CHECK(unsetenv("HOME") == 0);
#else
    (void)dir;
#endif
    return 1;
}

static int test_unknown_version_uses_defaults(const char* dir)
{
    char path[512];
    rsx_overlay_settings settings;
    rsx_overlay_settings defaults;

    CHECK(path_join(path, sizeof(path), dir, "unknown-version.settings"));
    CHECK(write_text(path, "version=3\noverlay_enabled=0\n"));
    rsx_overlay_settings_defaults(&defaults);
    CHECK(rsx_overlay_settings_load(&settings, path) == 0);
    CHECK(settings_match(&settings, &defaults));
    remove(path);
    return 1;
}

static int test_invalid_button_maps_use_default_map(const char* dir)
{
    char path[512];
    rsx_overlay_settings settings;
    rsx_overlay_settings defaults;

    CHECK(path_join(path, sizeof(path), dir, "bad-map.settings"));
    rsx_overlay_settings_defaults(&defaults);
    CHECK(write_text(path,
        "version=1\nhost_button_to_guest_button=1,2,4,8,16,32,64,128,256,512,1024,2048,4096,8192,16384,16384\n"));
    CHECK(rsx_overlay_settings_load(&settings, path) == 1);
    CHECK(maps_match(&settings, &defaults));
    CHECK(write_text(path,
        "version=1\nhost_button_to_guest_button=3,2,4,8,16,32,64,128,256,512,1024,2048,4096,8192,16384,32768\n"));
    CHECK(rsx_overlay_settings_load(&settings, path) == 1);
    CHECK(maps_match(&settings, &defaults));
    remove(path);
    return 1;
}

static int test_save_failure_preserves_existing_target(const char* dir)
{
    char path[512];
    char directory_target[512];
    rsx_overlay_settings settings;
    rsx_overlay_settings loaded;

    CHECK(path_join(path, sizeof(path), dir, "preserved.settings"));
    rsx_overlay_settings_defaults(&settings);
    settings.overlay_visible = 1;
    CHECK(rsx_overlay_settings_save(&settings, path) == 1);
    settings.analog_deadzone = 101;
    CHECK(rsx_overlay_settings_save(&settings, path) == 0);
    CHECK(rsx_overlay_settings_load(&loaded, path) == 1);
    CHECK(loaded.overlay_visible == 1);
    CHECK(loaded.analog_deadzone == 0);

    CHECK(path_join(directory_target, sizeof(directory_target), dir, "rename-target"));
    CHECK(TEST_MKDIR(directory_target) == 0);
    rsx_overlay_settings_defaults(&settings);
    CHECK(rsx_overlay_settings_save(&settings, directory_target) == 0);
    CHECK(TEST_RMDIR(directory_target) == 0);
    remove(path);
    return 1;
}

static int test_version1_file_upgrades(const char* dir)
{
    char path[1024];
    rsx_overlay_settings loaded;

    CHECK(path_join(path, sizeof(path), dir, "v1.settings"));
    CHECK(write_text(path, "version=1\nvsync=0\nanalog_deadzone=12\n"));
    CHECK(rsx_overlay_settings_load(&loaded, path) == 1);
    CHECK(loaded.version == RSX_OVERLAY_SETTINGS_VERSION);
    CHECK(loaded.vsync == 0 && loaded.analog_deadzone == 12);
    CHECK(loaded.ui_sounds == 1 && loaded.ui_haptics == 1);
    CHECK(loaded.game_mute == 0 && loaded.vibration == 1);
    /* Saving writes the current version. */
    CHECK(rsx_overlay_settings_save(&loaded, path) == 1);
    CHECK(rsx_overlay_settings_load(&loaded, path) == 1);
    CHECK(loaded.version == RSX_OVERLAY_SETTINGS_VERSION);
    remove(path);
    return 1;
}

static int test_invalid_v2_fields_fall_back_per_field(const char* dir)
{
    char path[1024];
    rsx_overlay_settings loaded;

    CHECK(path_join(path, sizeof(path), dir, "v2-bad.settings"));
    CHECK(write_text(path, "version=2\nui_sounds=7\nui_haptics=0\ngame_mute=x\nvibration=0\n"));
    CHECK(rsx_overlay_settings_load(&loaded, path) == 1);
    CHECK(loaded.ui_sounds == 1);   /* invalid: default */
    CHECK(loaded.ui_haptics == 0);  /* valid: kept */
    CHECK(loaded.game_mute == 0);   /* invalid: default */
    CHECK(loaded.vibration == 0);   /* valid: kept */
    loaded.vibration = 9;
    CHECK(rsx_overlay_settings_validate(&loaded) == 0);
    CHECK(loaded.vibration == 1);
    remove(path);
    return 1;
}

int main(void)
{
    char dir[512];
    int ok;

    if (!make_temp_dir(dir, sizeof(dir))) {
        fprintf(stderr, "could not create test directory: %s\n", strerror(errno));
        return 1;
    }

    ok = test_defaults() &&
         test_valid_round_trip(dir) &&
         test_invalid_fields_keep_defaults(dir) &&
         test_truncated_file_keeps_valid_prior_settings(dir) &&
         test_overlong_line_is_ignored(dir) &&
         test_oversized_file_uses_defaults(dir) &&
         test_embedded_nul_discards_only_its_line(dir) &&
         test_null_and_empty_paths_use_temp_home(dir) &&
         test_unknown_version_uses_defaults(dir) &&
         test_invalid_button_maps_use_default_map(dir) &&
         test_save_failure_preserves_existing_target(dir) &&
         test_version1_file_upgrades(dir) &&
         test_invalid_v2_fields_fall_back_per_field(dir);
    if (TEST_RMDIR(dir) != 0)
        ok = 0;
    printf("test_rsx_overlay_settings: %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
