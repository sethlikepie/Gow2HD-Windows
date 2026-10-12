/*
 * wp_release.c -- release start-up of the Windows port: compiled-in defaults, paths, log file.
 *
 * Runs as the first constructor of the process (priority 101, before every other constructor,
 * the C++ static initialisers, the renderer's env snapshot and the Port Settings ini reader), so
 * the game runs correctly with NO environment variables set:
 *
 *   1. Release defaults. Every switch of the tested play configuration (v85) is set here when the
 *      environment does not already set it. An environment variable set by the user always wins,
 *      and port_settings.ini (Port Settings panel, next to the exe) still overrides the frame-rate
 *      cap / display mode / resolution scale / fps counter after this. Diagnostic and test-only
 *      switches (frame captures, pad scripts, synthetic keys, profilers, stream/sound diagnostics,
 *      [PERF] statistics, no-present benches) are NOT in the table: they stay off unless set.
 *   2. Paths. The exe's folder is the install folder: gow2_port.ini there names the game folder
 *      (written by build.py), movie_cache\ and EBOOT.ELF live next to the exe, the exe's folder
 *      is put first on PATH (ffmpeg.exe / ffprobe.exe for the movies) and made the working dir.
 *   3. Log. stdout+stderr go to gow2.log next to the exe (the previous one is kept as
 *      gow2.prev.log) unless the parent already redirected them to a file or pipe.
 *      WINPORT_LOG_FILE=<path> picks another file, WINPORT_LOG_FILE=0 leaves the streams alone.
 *   4. A console window that was created only for this process (double-click start) is hidden;
 *      started from a command prompt the console is left as it is.
 */
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { const char* key; const char* value; } WpDefault;

/* The v85 play configuration (was win\play_v85_test.cmd = v82 + the v83..v85 performance switches).
 * One line per switch; see the git history
 * of each feature for what it does. */
static const WpDefault k_release_defaults[] = {
    /* engine / HLE setup */
    { "PS3_RSX_BACKEND", "d3d12" },
    { "PS3_RSX_FIFO", "1" },
    { "PS3_MOVIE_HLE", "0" },
    { "PS3_BOOT_LOGO_QUEUE", "0" },
    { "PS3_SPU_WORKERS", "6" },
    { "PS3_GCM_VSYNC", "0" },
    { "PS3_NOMOVIES", "0" },
    { "PS3_MOVIE_IO", "1" },
    { "PS3_MOVIE_EOS", "1" },
    { "PS3_VDEC_ASYNC", "1" },
    { "PS3_SPU1", "1" },
    { "PS3_SPU6", "1" },
    { "PS3_CELLSYS_REORDER", "1" },
    { "PS3_GCM_CB", "1" },
    { "PS3_LWMUTEX_REAL", "1" },
    { "PS3_FIOS_STICKY_OWNER", "1" },
    { "PS3_MOVIE_DONE_MS", "auto" },
    { "RT_DISPLAY_BY_SIZE", "1" },
    /* (PS3_SKIP_INGAME_MOVIE_MS / _ONLY removed: a bench-only "fast-forward the New Game opener" aid that had been
     * inert since the v71 movie pick; it must never fire for players. Movies keep playing in full.) */
    /* audio */
    { "PS3_MFC_SLOT_FIX", "1" },        /* SCREAM mixer gets its own SPU DMA engine (no silence after the menu) */
    { "PS3_SCREAM_NATIVE", "1" },       /* SCREAM mixer jobs as native code */
    { "PS3_TICK_GIANT", "15" },         /* audio pacer thread gets the PPU lock first */
    { "PS3_AUDIO_TAG_WAIT", "1" },      /* a missed mixer block is skipped after 1 period */
    /* graphics fixes */
    { "WINPORT_VTX_CMP", "1" },
    { "WINPORT_STENCIL", "1" },
    { "WINPORT_STENCIL_BASE", "128" },
    { "WINPORT_ORB_FIX", "1" },
    { "WINPORT_SKY_FIX", "1" },
    { "WINPORT_LINE_FIX", "1" },
    { "WINPORT_VS_PRECISE", "1" },
    { "WINPORT_BONE_REPAIR", "1" },
    /* performance */
    { "WINPORT_VM_GATE", "1" },
    { "WINPORT_VTX_THREADS", "4" },
    { "WINPORT_INFL_UNLOCK", "1" },
    { "WINPORT_VTX_WAIT", "1" },
    { "WINPORT_ASYNC_RSX", "1" },
    { "WINPORT_ASYNC_SEG_KB", "512" },
    { "WINPORT_VM_INLINE", "1" },
    { "WINPORT_HLE_CACHE", "1" },
    { "WINPORT_VM_INLINE_W", "1" },
    { "WINPORT_DEFER_VTX", "3" },
    { "WINPORT_FRAMES_IN_FLIGHT", "2" },
    { "WINPORT_SPURS_KICK_RUN", "1" },
    { "WINPORT_SPU_RUNNER", "1" },
    { "WINPORT_SPURS_WAKE1", "1" },
    { "WINPORT_SPURS_KSPIN_US", "50" },
    { "WINPORT_MFC_CACHE79", "1" },
    { "WINPORT_RSV_CAS_ONLY", "1" },
    { "WINPORT_VM_FAST8", "1" },
    { "WINPORT_SPU_LOOKUP83", "1" },    /* per-thread SPU indirect-branch lookup cache (v83) */
    { "WINPORT_RSX_BULK85", "1" },      /* walker hands vertex-program/constant runs to one bulk handler (v85) */
    { "WINPORT_WALKFAST85", "1" },      /* renderer: shadow constant blocks, VP hash memo, index max, tex slot memo (v85) */
    { "WINPORT_IDXDRAW85", "0" },       /* v85 indexed draws: OFF -- breaks the opening Colossus QTE sync (Seth 2026-10-10, cause open) */
    /* timing / display */
    { "WINPORT_DT_MIN_US", "2000" },    /* game clock follows real time above 60 fps */
    { "WINPORT_UI_RT", "1" },           /* Flash UI animates in real time */
    { "WINPORT_FPS_CAP", "300" },       /* own frame limiter + tearing present */
    { "WINPORT_RES_SCALE", "2" },
    { "WINPORT_BORDERLESS", "1" },
    { "WINPORT_FPS_OVERLAY", "0" },
    { "WINPORT_PRESENT_WD82", "1" },    /* start-up hang recovery (present watchdog) */
    /* UI / Port Settings */
    { "WINPORT_PORT_SETTINGS", "1" },
    { "WINPORT_PSET81_ANYWHERE", "1" },
    { "WINPORT_UI81", "1" },
    { "WINPORT_CUTSCENE_UI", "1" },
    { "WINPORT_FOCUS_INPUT", "1" },
    /* movies */
    { "WINPORT_MOVIE_SKIP", "0" },
    { "WINPORT_ALL_MOVIES", "1" },
    { "WINPORT_MOVIE_PICK_FIRST", "1" },
    { "WINPORT_MOVIE_PICK_ALWAYS", "1" },
    { "PS3_VDEC_SOFTQ", "1" },
    { "WINPORT_MOVIE_PACE", "1" },
    { "WINPORT_MOVIE_SOLO", "1" },
    { "WINPORT_MOVIE_SOLO_MS", "2" },
    { "WINPORT_CUTSCENE_NATIVE_PAUSE", "0" },   /* SELECT in a movie = our pause (picture + sound frozen): the game never opens its own menu during a movie */
    { "WINPORT_MOVIE_SYNC", "1" },
    { "WINPORT_BOOT_SCREENS", "1" },
    /* checkpoints (v90/v92a): F6 / Port Settings "Save Checkpoint" writes the game's latest checkpoint + a thumbnail to
     * checkpoints\ next to the exe; Port Settings "Load Checkpoint" lists them and loads one in game */
    { "WINPORT_CKPT90", "1" },
    /* visual fixes (v92b) */
    { "WINPORT_BESEM92", "1" },         /* back-end label release writes the un-swizzled value (Full Frame hang) */
    { "WINPORT_LBL16_92", "1" },        /* cellGcmGetLabelAddress uses 16-byte label slots (Full Frame hang) */
    { "WINPORT_DISPSIZE92", "1" },      /* display-sized surfaces are render targets once display buffers exist (Full Frame picture) */
    { "WINPORT_EDGE92", "1" },          /* no dark left/top edge line at resolution scale > 1 */
    { "WINPORT_PREVFRAME92", "1" },     /* display-buffer textures sample the last finished frame (Colossus scream ripple) */
    /* v94 */
    { "WINPORT_FULLFRAME94", "1" },     /* movie overlay follows the game's Full Frame option */
    { "WINPORT_PREVFRAME94", "1" },     /* per-display-buffer previous-frame copy taken before host overlays (scream ripple) */
    { "WINPORT_CKPT94", "1" },          /* checkpoint load from the pause menu resumes the game first (music keeps playing) */
    /* v95 */
    { "WINPORT_EDGE95", "1" },          /* exact half-pixel for full-screen quads at scale > 1 (no offset haze; replaces EDGE92) */
    /* v96 */
    { "WINPORT_PPU_PACE96", "95" },     /* even frame pacing: the game thread waits at each flip until 95% of the render thread's average busy time has passed (Kratos animation stutter below the refresh rate) */
};

static char s_exe_dir[MAX_PATH];
static char s_eboot[MAX_PATH + 16];

const char* wp_release_exe_dir(void) { return s_exe_dir; }

/* argv[1] default for boot_main.cpp: EBOOT.ELF next to the exe (NULL if it is not there). */
const char* wp_release_default_eboot(void)
{
    if (!s_exe_dir[0]) return NULL;
    snprintf(s_eboot, sizeof s_eboot, "%s\\EBOOT.ELF", s_exe_dir);
    return GetFileAttributesA(s_eboot) != INVALID_FILE_ATTRIBUTES ? s_eboot : NULL;
}

static int env_set(const char* k)
{
    const char* v = getenv(k);
    return v && v[0];
}

static void setdef(const char* k, const char* v)
{
    if (!env_set(k)) _putenv_s(k, v);
}

static void trim(char* s)
{
    size_t n = strlen(s);
    while (n && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ' || s[n - 1] == '\t')) s[--n] = 0;
}

/* gow2_port.ini next to the exe (written by build.py):
 *   vfs_root=<the game's USRDIR folder>     -> PS3_VFS_ROOT
 *   movie_cache=<folder>                    -> PS3_MOVIE_CACHE (relative = to the exe folder)
 *   KEY=VALUE under [env]                   -> extra defaults (environment still wins) */
static void read_port_ini(void)
{
    char path[MAX_PATH + 32];
    snprintf(path, sizeof path, "%s\\gow2_port.ini", s_exe_dir);
    FILE* f = fopen(path, "r");
    if (!f) return;
    char line[2048]; int in_env = 0;
    while (fgets(line, sizeof line, f)) {
        trim(line);
        char* s = line; while (*s == ' ' || *s == '\t') s++;
        if (!*s || *s == '#' || *s == ';') continue;
        if (*s == '[') { in_env = !_strnicmp(s, "[env]", 5); continue; }
        char* eq = strchr(s, '='); if (!eq) continue;
        *eq = 0; char* k = s; char* v = eq + 1; trim(k);
        if (in_env) { setdef(k, v); continue; }
        if (!_stricmp(k, "vfs_root")) setdef("PS3_VFS_ROOT", v);
        else if (!_stricmp(k, "movie_cache")) {
            char full[MAX_PATH * 2];
            if (v[0] && (v[1] == ':' || v[0] == '\\' || v[0] == '/')) snprintf(full, sizeof full, "%s", v);
            else snprintf(full, sizeof full, "%s\\%s", s_exe_dir, v);
            setdef("PS3_MOVIE_CACHE", full);
        }
    }
    fclose(f);
}

static void open_log(void)
{
    const char* want = getenv("WINPORT_LOG_FILE");
    if (want && want[0] == '0' && !want[1]) return;
    if (!(want && want[0])) {
        DWORD t = GetFileType(GetStdHandle(STD_ERROR_HANDLE));
        if (t == FILE_TYPE_DISK || t == FILE_TYPE_PIPE) return;   /* the parent redirected us: keep it */
    }
    char path[MAX_PATH + 32], prev[MAX_PATH + 32];
    if (want && want[0]) snprintf(path, sizeof path, "%s", want);
    else {
        snprintf(path, sizeof path, "%s\\gow2.log", s_exe_dir);
        snprintf(prev, sizeof prev, "%s\\gow2.prev.log", s_exe_dir);
        MoveFileExA(path, prev, MOVEFILE_REPLACE_EXISTING);
    }
    int fd = _open(path, _O_WRONLY | _O_CREAT | _O_TRUNC | _O_BINARY, 0644);
    if (fd < 0) return;
    fflush(stdout); fflush(stderr);
    _dup2(fd, 1); _dup2(fd, 2); _close(fd);
    SetStdHandle(STD_OUTPUT_HANDLE, (HANDLE)_get_osfhandle(1));
    SetStdHandle(STD_ERROR_HANDLE, (HANDLE)_get_osfhandle(2));
    setvbuf(stderr, NULL, _IONBF, 0);
}

static void hide_own_console(void)
{
    HWND con = GetConsoleWindow();
    if (!con) return;
    DWORD pids[4];
    if (GetConsoleProcessList(pids, 4) == 1) ShowWindow(con, SW_HIDE);   /* console created just for us */
}

__attribute__((constructor(101))) static void wp_release_startup(void)
{
    DWORD n = GetModuleFileNameA(NULL, s_exe_dir, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) { s_exe_dir[0] = 0; return; }
    char* sl = strrchr(s_exe_dir, '\\'); if (sl) *sl = 0;

    if (!env_set("WINPORT_RELEASE_DEFAULTS") || getenv("WINPORT_RELEASE_DEFAULTS")[0] != '0')
        for (size_t i = 0; i < sizeof k_release_defaults / sizeof k_release_defaults[0]; i++)
            setdef(k_release_defaults[i].key, k_release_defaults[i].value);
    read_port_ini();
    {   /* movie cache default: movie_cache\ next to the exe */
        char mc[MAX_PATH + 32];
        snprintf(mc, sizeof mc, "%s\\movie_cache", s_exe_dir);
        setdef("PS3_MOVIE_CACHE", mc);
    }
    {   /* ffmpeg.exe / ffprobe.exe next to the exe come first */
        const char* p = getenv("PATH");
        size_t len = strlen(s_exe_dir) + (p ? strlen(p) : 0) + 2;
        char* np = (char*)malloc(len);
        if (np) { snprintf(np, len, "%s;%s", s_exe_dir, p ? p : ""); _putenv_s("PATH", np); free(np); }
    }
    if (!(env_set("WINPORT_KEEP_CWD") && getenv("WINPORT_KEEP_CWD")[0] == '1'))
        SetCurrentDirectoryA(s_exe_dir);
    hide_own_console();
    open_log();
}
