#include "rsx_overlay_settings.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  include <direct.h>
#  include <io.h>
#  include <process.h>
#  include <windows.h>
#  define RSX_OPEN _open
#  define RSX_CLOSE _close
#  define RSX_FDOPEN _fdopen
#  define RSX_FILENO _fileno
#  define RSX_FSYNC _commit
#  define RSX_GETPID _getpid
#  define RSX_MKDIR(path) _mkdir(path)
#else
#  include <sys/stat.h>
#  include <sys/types.h>
#  include <unistd.h>
#  define RSX_OPEN open
#  define RSX_CLOSE close
#  define RSX_FDOPEN fdopen
#  define RSX_FILENO fileno
#  define RSX_FSYNC fsync
#  define RSX_GETPID getpid
#  define RSX_MKDIR(path) mkdir((path), 0700)
#endif

#define RSX_OVERLAY_SETTINGS_PATH_CAPACITY 1024u
#define RSX_OVERLAY_SETTINGS_LINE_CAPACITY 512u
#define RSX_OVERLAY_SETTINGS_FILE_LIMIT (64u * 1024u)

static int rsx_overlay_settings_map_is_bijective(
    const uint16_t map[RSX_OVERLAY_HOST_BUTTON_COUNT])
{
    uint32_t seen = 0;
    size_t i;

    for (i = 0; i < RSX_OVERLAY_HOST_BUTTON_COUNT; ++i) {
        uint32_t button = map[i];
        if (button == 0 || (button & (button - 1u)) != 0 || (seen & button) != 0)
            return 0;
        seen |= button;
    }
    return seen == 0xffffu;
}

void rsx_overlay_settings_defaults(rsx_overlay_settings* out)
{
    size_t i;

    if (out == NULL)
        return;
    memset(out, 0, sizeof(*out));
    out->version = RSX_OVERLAY_SETTINGS_VERSION;
    out->overlay_enabled = 1;
    out->overlay_visible = 0;
    out->diagnostics_visible = 0;
    out->toggle_key = RSX_OVERLAY_TOGGLE_KEY_F1;
    out->window_width = 1280;
    out->window_height = 720;
    out->fullscreen = 0;
    out->vsync = 1;
    out->analog_deadzone = 0; /* CELL_PAD_HOST_DEADZONE_DEFAULT: identity */
    out->ui_sounds = 1;
    out->ui_haptics = 1;
    out->game_mute = 0;
    out->vibration = 1;
    for (i = 0; i < RSX_OVERLAY_HOST_BUTTON_COUNT; ++i)
        out->host_button_to_guest_button[i] = (uint16_t)(1u << i);
}

int rsx_overlay_settings_validate(rsx_overlay_settings* settings)
{
    rsx_overlay_settings defaults;
    int valid = 1;

    if (settings == NULL)
        return 0;
    rsx_overlay_settings_defaults(&defaults);
    if (settings->version != RSX_OVERLAY_SETTINGS_VERSION) {
        settings->version = defaults.version;
        valid = 0;
    }
    if (settings->overlay_enabled > 1) {
        settings->overlay_enabled = defaults.overlay_enabled;
        valid = 0;
    }
    if (settings->overlay_visible > 1) {
        settings->overlay_visible = defaults.overlay_visible;
        valid = 0;
    }
    if (settings->diagnostics_visible > 1) {
        settings->diagnostics_visible = defaults.diagnostics_visible;
        valid = 0;
    }
    if (settings->toggle_key != RSX_OVERLAY_TOGGLE_KEY_F1 &&
        settings->toggle_key != RSX_OVERLAY_TOGGLE_KEY_F2) {
        settings->toggle_key = defaults.toggle_key;
        valid = 0;
    }
    if (settings->window_width < 640 || settings->window_width > 7680) {
        settings->window_width = defaults.window_width;
        valid = 0;
    }
    if (settings->window_height < 480 || settings->window_height > 4320) {
        settings->window_height = defaults.window_height;
        valid = 0;
    }
    if (settings->fullscreen > 1) {
        settings->fullscreen = defaults.fullscreen;
        valid = 0;
    }
    if (settings->vsync > 1) {
        settings->vsync = defaults.vsync;
        valid = 0;
    }
    if (settings->analog_deadzone > 100) {
        settings->analog_deadzone = defaults.analog_deadzone;
        valid = 0;
    }
    if (settings->ui_sounds > 1) {
        settings->ui_sounds = defaults.ui_sounds;
        valid = 0;
    }
    if (settings->ui_haptics > 1) {
        settings->ui_haptics = defaults.ui_haptics;
        valid = 0;
    }
    if (settings->game_mute > 1) {
        settings->game_mute = defaults.game_mute;
        valid = 0;
    }
    if (settings->vibration > 1) {
        settings->vibration = defaults.vibration;
        valid = 0;
    }
    if (!rsx_overlay_settings_map_is_bijective(settings->host_button_to_guest_button)) {
        memcpy(settings->host_button_to_guest_button,
               defaults.host_button_to_guest_button,
               sizeof(settings->host_button_to_guest_button));
        valid = 0;
    }
    return valid;
}

static int rsx_overlay_settings_default_path(char* out, size_t capacity)
{
    const char* base;
    int n;

#if defined(_WIN32)
    base = getenv("APPDATA");
    if (base == NULL || base[0] == '\0')
        return 0;
    n = snprintf(out, capacity, "%s\\ps3recomp\\gow2\\runtime-overlay.settings", base);
#elif defined(__APPLE__)
    base = getenv("HOME");
    if (base == NULL || base[0] == '\0')
        return 0;
    n = snprintf(out, capacity,
                 "%s/Library/Application Support/ps3recomp/gow2/runtime-overlay.settings",
                 base);
#else
    base = getenv("XDG_CONFIG_HOME");
    if (base != NULL && base[0] != '\0')
        n = snprintf(out, capacity, "%s/ps3recomp/gow2/runtime-overlay.settings", base);
    else {
        base = getenv("HOME");
        if (base == NULL || base[0] == '\0')
            return 0;
        n = snprintf(out, capacity, "%s/.config/ps3recomp/gow2/runtime-overlay.settings", base);
    }
#endif
    return n > 0 && (size_t)n < capacity;
}

static int rsx_overlay_settings_resolve_path(const char* path, char* out, size_t capacity)
{
    size_t length;

    if (path == NULL || path[0] == '\0')
        return rsx_overlay_settings_default_path(out, capacity);
    length = strlen(path);
    if (length == 0 || length >= capacity)
        return 0;
    memcpy(out, path, length + 1);
    return 1;
}

static int rsx_overlay_settings_make_parent_directories(const char* path)
{
    char parent[RSX_OVERLAY_SETTINGS_PATH_CAPACITY];
    size_t length;
    size_t i;

    length = strlen(path);
    if (length >= sizeof(parent))
        return 0;
    memcpy(parent, path, length + 1);
    for (i = 0; i < length; ++i) {
        if (parent[i] != '/' && parent[i] != '\\')
            continue;
        if (i == 0 || (i == 2 && parent[1] == ':'))
            continue;
        parent[i] = '\0';
        if (RSX_MKDIR(parent) != 0 && errno != EEXIST) {
            parent[i] = path[i];
            return 0;
        }
        parent[i] = path[i];
    }
    return 1;
}

static int rsx_overlay_settings_parse_u32(const char* text, uint32_t* out)
{
    uint64_t value = 0;
    const unsigned char* p = (const unsigned char*)text;

    if (*p == '\0')
        return 0;
    while (*p != '\0') {
        uint64_t digit;
        if (*p < '0' || *p > '9')
            return 0;
        digit = (uint64_t)(*p - '0');
        if (value > (UINT32_MAX - digit) / 10u)
            return 0;
        value = value * 10u + digit;
        ++p;
    }
    *out = (uint32_t)value;
    return 1;
}

static int rsx_overlay_settings_parse_bool(const char* text, uint8_t* out)
{
    uint32_t value;
    if (!rsx_overlay_settings_parse_u32(text, &value) || value > 1)
        return 0;
    *out = (uint8_t)value;
    return 1;
}

static int rsx_overlay_settings_parse_map(
    char* text, uint16_t out[RSX_OVERLAY_HOST_BUTTON_COUNT])
{
    size_t i;
    char* value = text;

    for (i = 0; i < RSX_OVERLAY_HOST_BUTTON_COUNT; ++i) {
        char* separator = strchr(value, ',');
        uint32_t button;
        if (i + 1 == RSX_OVERLAY_HOST_BUTTON_COUNT) {
            if (separator != NULL)
                return 0;
        } else {
            if (separator == NULL)
                return 0;
            *separator = '\0';
        }
        if (!rsx_overlay_settings_parse_u32(value, &button) || button > UINT16_MAX)
            return 0;
        out[i] = (uint16_t)button;
        value = separator == NULL ? value : separator + 1;
    }
    return rsx_overlay_settings_map_is_bijective(out);
}

static void rsx_overlay_settings_parse_line(rsx_overlay_settings* settings,
                                            char* line, int* saw_version,
                                            int* version_supported)
{
    char* equals = strchr(line, '=');
    uint32_t value;
    uint8_t boolean;

    if (equals == NULL || strchr(equals + 1, '=') != NULL)
        return;
    *equals = '\0';
    ++equals;
    if (strcmp(line, "version") == 0) {
        *saw_version = 1;
        if (!rsx_overlay_settings_parse_u32(equals, &value) ||
            value < RSX_OVERLAY_SETTINGS_MIN_VERSION || value > RSX_OVERLAY_SETTINGS_VERSION)
            *version_supported = 0;
    } else if (strcmp(line, "overlay_enabled") == 0) {
        if (rsx_overlay_settings_parse_bool(equals, &boolean))
            settings->overlay_enabled = boolean;
    } else if (strcmp(line, "overlay_visible") == 0) {
        if (rsx_overlay_settings_parse_bool(equals, &boolean))
            settings->overlay_visible = boolean;
    } else if (strcmp(line, "diagnostics_visible") == 0) {
        if (rsx_overlay_settings_parse_bool(equals, &boolean))
            settings->diagnostics_visible = boolean;
    } else if (strcmp(line, "toggle_key") == 0) {
        if (rsx_overlay_settings_parse_u32(equals, &value) &&
            (value == RSX_OVERLAY_TOGGLE_KEY_F1 || value == RSX_OVERLAY_TOGGLE_KEY_F2))
            settings->toggle_key = (uint8_t)value;
    } else if (strcmp(line, "window_width") == 0) {
        if (rsx_overlay_settings_parse_u32(equals, &value) && value >= 640 && value <= 7680)
            settings->window_width = value;
    } else if (strcmp(line, "window_height") == 0) {
        if (rsx_overlay_settings_parse_u32(equals, &value) && value >= 480 && value <= 4320)
            settings->window_height = value;
    } else if (strcmp(line, "fullscreen") == 0) {
        if (rsx_overlay_settings_parse_bool(equals, &boolean))
            settings->fullscreen = boolean;
    } else if (strcmp(line, "vsync") == 0) {
        if (rsx_overlay_settings_parse_bool(equals, &boolean))
            settings->vsync = boolean;
    } else if (strcmp(line, "analog_deadzone") == 0) {
        if (rsx_overlay_settings_parse_u32(equals, &value) && value <= 100)
            settings->analog_deadzone = (uint8_t)value;
    } else if (strcmp(line, "ui_sounds") == 0) {
        if (rsx_overlay_settings_parse_bool(equals, &boolean))
            settings->ui_sounds = boolean;
    } else if (strcmp(line, "ui_haptics") == 0) {
        if (rsx_overlay_settings_parse_bool(equals, &boolean))
            settings->ui_haptics = boolean;
    } else if (strcmp(line, "game_mute") == 0) {
        if (rsx_overlay_settings_parse_bool(equals, &boolean))
            settings->game_mute = boolean;
    } else if (strcmp(line, "vibration") == 0) {
        if (rsx_overlay_settings_parse_bool(equals, &boolean))
            settings->vibration = boolean;
    } else if (strcmp(line, "host_button_to_guest_button") == 0) {
        uint16_t map[RSX_OVERLAY_HOST_BUTTON_COUNT];
        if (rsx_overlay_settings_parse_map(equals, map))
            memcpy(settings->host_button_to_guest_button, map, sizeof(map));
    }
}

int rsx_overlay_settings_load(rsx_overlay_settings* out, const char* path)
{
    char resolved_path[RSX_OVERLAY_SETTINGS_PATH_CAPACITY];
    char line[RSX_OVERLAY_SETTINGS_LINE_CAPACITY];
    rsx_overlay_settings candidate;
    FILE* file;
    size_t length = 0;
    size_t total = 0;
    int saw_version = 0;
    int version_supported = 1;
    int line_rejected = 0;
    int read_failed;
    int ch;

    if (out == NULL)
        return 0;
    rsx_overlay_settings_defaults(out);
    if (!rsx_overlay_settings_resolve_path(path, resolved_path, sizeof(resolved_path)))
        return 0;
    file = fopen(resolved_path, "rb");
    if (file == NULL)
        return 0;
    rsx_overlay_settings_defaults(&candidate);
    while ((ch = fgetc(file)) != EOF) {
        if (++total > RSX_OVERLAY_SETTINGS_FILE_LIMIT) {
            fclose(file);
            return 0;
        }
        if (ch == '\n') {
            if (!line_rejected) {
                if (length > 0 && line[length - 1] == '\r')
                    --length;
                line[length] = '\0';
                rsx_overlay_settings_parse_line(&candidate, line, &saw_version,
                                                &version_supported);
            }
            length = 0;
            line_rejected = 0;
        } else if (ch == '\0') {
            /* A NUL makes this logical line non-text. Keep parsing later
             * lines, whose valid values retain the normal recovery policy. */
            line_rejected = 1;
        } else if (length + 1 < sizeof(line)) {
            line[length++] = (char)ch;
        } else {
            line_rejected = 1;
        }
    }
    read_failed = ferror(file);
    if (fclose(file) != 0)
        read_failed = 1;
    if (read_failed)
        return 0;
    if (length > 0 && !line_rejected) {
        if (line[length - 1] == '\r')
            --length;
        line[length] = '\0';
        rsx_overlay_settings_parse_line(&candidate, line, &saw_version, &version_supported);
    }
    if (!saw_version || !version_supported)
        return 0;
    candidate.version = RSX_OVERLAY_SETTINGS_VERSION; /* v1 upgrades in memory */
    rsx_overlay_settings_validate(&candidate);
    *out = candidate;
    return 1;
}

static int rsx_overlay_settings_write_file(const char* path, const char* text, size_t length)
{
    int descriptor;
    FILE* file;
    int closed = 0;

    descriptor = RSX_OPEN(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (descriptor < 0)
        return 0;
    file = RSX_FDOPEN(descriptor, "wb");
    if (file == NULL) {
        RSX_CLOSE(descriptor);
        remove(path);
        return 0;
    }
    if (fwrite(text, 1, length, file) == length && fflush(file) == 0 &&
        RSX_FSYNC(RSX_FILENO(file)) == 0) {
        closed = 1;
        if (fclose(file) == 0)
            return 1;
    }
    if (!closed)
        fclose(file);
    remove(path);
    return 0;
}

int rsx_overlay_settings_save(const rsx_overlay_settings* settings, const char* path)
{
    char resolved_path[RSX_OVERLAY_SETTINGS_PATH_CAPACITY];
    char temporary_path[RSX_OVERLAY_SETTINGS_PATH_CAPACITY];
    char contents[1024];
    rsx_overlay_settings candidate;
    int length;
    int temporary_length;
    size_t i;
    static unsigned long temporary_sequence;

    if (settings == NULL ||
        !rsx_overlay_settings_resolve_path(path, resolved_path, sizeof(resolved_path)))
        return 0;
    candidate = *settings;
    if (!rsx_overlay_settings_validate(&candidate))
        return 0;
    if (!rsx_overlay_settings_make_parent_directories(resolved_path))
        return 0;
    length = snprintf(contents, sizeof(contents),
                      "version=%u\n"
                      "overlay_enabled=%u\n"
                      "overlay_visible=%u\n"
                      "diagnostics_visible=%u\n"
                      "toggle_key=%u\n"
                      "window_width=%u\n"
                      "window_height=%u\n"
                      "fullscreen=%u\n"
                      "vsync=%u\n"
                      "analog_deadzone=%u\n"
                      "ui_sounds=%u\n"
                      "ui_haptics=%u\n"
                      "game_mute=%u\n"
                      "vibration=%u\n"
                      "host_button_to_guest_button=",
                      candidate.version, candidate.overlay_enabled, candidate.overlay_visible,
                      candidate.diagnostics_visible, candidate.toggle_key, candidate.window_width,
                      candidate.window_height, candidate.fullscreen, candidate.vsync,
                      candidate.analog_deadzone, candidate.ui_sounds, candidate.ui_haptics,
                      candidate.game_mute, candidate.vibration);
    if (length < 0 || (size_t)length >= sizeof(contents))
        return 0;
    for (i = 0; i < RSX_OVERLAY_HOST_BUTTON_COUNT; ++i) {
        int written = snprintf(contents + length, sizeof(contents) - (size_t)length,
                               "%s%u", i == 0 ? "" : ",",
                               candidate.host_button_to_guest_button[i]);
        if (written < 0 || (size_t)written >= sizeof(contents) - (size_t)length)
            return 0;
        length += written;
    }
    if ((size_t)length + 2 > sizeof(contents))
        return 0;
    contents[length++] = '\n';
    contents[length] = '\0';
    temporary_length = snprintf(temporary_path, sizeof(temporary_path), "%s.tmp.%ld.%lu",
                                resolved_path, (long)RSX_GETPID(), ++temporary_sequence);
    if (temporary_length < 0 || (size_t)temporary_length >= sizeof(temporary_path))
        return 0;
    if (!rsx_overlay_settings_write_file(temporary_path, contents, (size_t)length))
        return 0;
#if defined(_WIN32)
    if (!MoveFileExA(temporary_path, resolved_path,
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        remove(temporary_path);
        return 0;
    }
#else
    if (rename(temporary_path, resolved_path) != 0) {
        remove(temporary_path);
        return 0;
    }
#endif
    return 1;
}
