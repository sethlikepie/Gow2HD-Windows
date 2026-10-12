/*
 * User-mod overlay. Gated by PS3_MODS_DIR (off when unset).
 *
 * A mod is a subdirectory of that folder. Lookups try, in last-wins order:
 *   <mod>/<rel>
 *   <mod>/USRDIR/<rel>
 *   <mod>/extracted/USRDIR/<rel>
 *   <mod>/movie_cache/<base>
 * Basename lookup also tries those layouts at the mod root.
 */
#include "ps3emu/ps3_mods.h"
#include "ps3emu/host_compat.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef _WIN32
#  include <windows.h>
#else
#  include <dirent.h>
#  include <sys/stat.h>
#endif

#define PS3_MODS_MAX  32
#define PS3_MODS_NAME 64

static int  s_ready;
static int  s_on;
static int  s_hits;
static char s_dir[1024];
static char s_names[PS3_MODS_MAX][PS3_MODS_NAME];
static int  s_n;

void ps3_mods_reset(void)
{
    s_ready = 0;
    s_on = 0;
    s_hits = 0;
    s_n = 0;
    s_dir[0] = 0;
    memset(s_names, 0, sizeof s_names);
}

static void strip_slash(char* s)
{
    size_t n = strlen(s);
    while (n && (s[n - 1] == '/' || s[n - 1] == '\\')) {
        s[--n] = 0;
    }
}

static int name_ok(const char* n)
{
    if (!n || !*n || n[0] == '.')
        return 0;
    if (strcmp(n, "enabled.json") == 0)
        return 0;
    if (strchr(n, '/') || strchr(n, '\\'))
        return 0;
    if (strstr(n, ".."))
        return 0;
    return 1;
}

static int add_name(const char* n)
{
    if (!name_ok(n) || s_n >= PS3_MODS_MAX)
        return 0;
    for (int i = 0; i < s_n; i++) {
        if (strcmp(s_names[i], n) == 0)
            return 0;
    }
    strncpy(s_names[s_n], n, PS3_MODS_NAME - 1);
    s_names[s_n][PS3_MODS_NAME - 1] = 0;
    s_n++;
    return 1;
}

static void scan_dir(void)
{
#ifdef _WIN32
    char pat[1100];
    snprintf(pat, sizeof pat, "%s\\*", s_dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            add_name(fd.cFileName);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR* d = opendir(s_dir);
    if (!d)
        return;
    struct dirent* e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.')
            continue;
        char p[1100];
        snprintf(p, sizeof p, "%s/%s", s_dir, e->d_name);
        PS3_STAT64 st;
        if (ps3_stat64(p, &st) != 0)
            continue;
        if (S_ISDIR(st.st_mode))
            add_name(e->d_name);
    }
    closedir(d);
#endif
}

static void init(void)
{
    if (s_ready)
        return;
    s_ready = 1;

    const char* dir = getenv("PS3_MODS_DIR");
    if (!dir || !*dir)
        return;
    strncpy(s_dir, dir, sizeof(s_dir) - 1);
    s_dir[sizeof(s_dir) - 1] = 0;
    strip_slash(s_dir);
    if (!s_dir[0])
        return;

    const char* en = getenv("PS3_MODS_ENABLED");
    if (en) {
        char buf[1024];
        strncpy(buf, en, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = 0;
        char* p = buf;
        while (*p) {
            while (*p == ' ' || *p == '\t' || *p == ',')
                p++;
            if (!*p)
                break;
            char* start = p;
            while (*p && *p != ',')
                p++;
            char save = *p;
            *p = 0;
            size_t L = strlen(start);
            while (L && (start[L - 1] == ' ' || start[L - 1] == '\t'))
                start[--L] = 0;
            add_name(start);
            *p = save;
            if (*p)
                p++;
        }
    } else {
        scan_dir();
    }

    if (s_n > 0) {
        s_on = 1;
        fprintf(stderr, "[mods] dir=%s n=%d last-wins", s_dir, s_n);
        for (int i = 0; i < s_n; i++)
            fprintf(stderr, " %s", s_names[i]);
        fprintf(stderr, "\n");
    }
}

static int exists_any(const char* p)
{
    PS3_STAT64 st;
    return p && *p && ps3_stat64(p, &st) == 0;
}

static void slash_norm(char* s)
{
    for (; *s; s++) {
#ifdef _WIN32
        if (*s == '/')
            *s = '\\';
#else
        if (*s == '\\')
            *s = '/';
#endif
    }
}

static int join_try(char* out, size_t cap, const char* a, const char* b, const char* c)
{
    if (c && *c)
        snprintf(out, cap, "%s/%s/%s", a, b, c);
    else
        snprintf(out, cap, "%s/%s", a, b);
    slash_norm(out);
    return exists_any(out);
}

static int try_mod_rel(const char* mod, const char* rel, char* out, size_t cap)
{
    if (join_try(out, cap, s_dir, mod, rel))
        return 1;
    char nested[1100];
    snprintf(nested, sizeof nested, "USRDIR/%s", rel);
    if (join_try(out, cap, s_dir, mod, nested))
        return 1;
    snprintf(nested, sizeof nested, "extracted/USRDIR/%s", rel);
    if (join_try(out, cap, s_dir, mod, nested))
        return 1;
    snprintf(nested, sizeof nested, "movie_cache/%s", rel);
    if (join_try(out, cap, s_dir, mod, nested))
        return 1;
    return 0;
}

static void note_hit(const char* rel, const char* path)
{
    if (s_hits >= 8)
        return;
    s_hits++;
    fprintf(stderr, "[mods] %s -> %s\n", rel, path);
}

int ps3_mods_overlay_host(const char* vfs_root, char* host_path, size_t cap)
{
    init();
    if (!s_on || !vfs_root || !host_path || cap == 0 || !host_path[0])
        return 0;

    char root[1024];
    strncpy(root, vfs_root, sizeof(root) - 1);
    root[sizeof(root) - 1] = 0;
    strip_slash(root);
    size_t n = strlen(root);
    if (n == 0)
        return 0;

    const char* host = host_path;
    if (strncmp(host, root, n) != 0)
        return 0;
    if (host[n] != 0 && host[n] != '/' && host[n] != '\\')
        return 0;
    const char* rel = host + n;
    while (*rel == '/' || *rel == '\\')
        rel++;
    if (!*rel)
        return 0;

    char relbuf[1024];
    strncpy(relbuf, rel, sizeof(relbuf) - 1);
    relbuf[sizeof(relbuf) - 1] = 0;

    char cand[1400];
    for (int i = s_n - 1; i >= 0; i--) {
        if (!try_mod_rel(s_names[i], relbuf, cand, sizeof cand))
            continue;
        snprintf(host_path, cap, "%s", cand);
        note_hit(relbuf, host_path);
        return 1;
    }
    return 0;
}

static const char* file_base(const char* p)
{
    const char* b = strrchr(p, '/');
    b = b ? b + 1 : p;
    const char* b2 = strrchr(b, '\\');
    return b2 ? b2 + 1 : b;
}

int ps3_mods_overlay_basename(const char* base, char* out, size_t cap)
{
    init();
    if (!s_on || !base || !*base || !out || cap == 0)
        return 0;
    base = file_base(base);
    if (!*base)
        return 0;

    char names[3][256];
    int nn = 0;
    snprintf(names[nn++], sizeof names[0], "%s", base);
    if (!strchr(base, '.')) {
        snprintf(names[nn++], sizeof names[0], "%s.m2v", base);
        snprintf(names[nn++], sizeof names[0], "%s.wad_ps3", base);
    }

    char cand[1400];
    for (int i = s_n - 1; i >= 0; i--) {
        for (int k = 0; k < nn; k++) {
            if (!try_mod_rel(s_names[i], names[k], cand, sizeof cand))
                continue;
            snprintf(out, cap, "%s", cand);
            note_hit(names[k], out);
            return 1;
        }
    }
    return 0;
}
