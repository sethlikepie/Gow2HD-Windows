/*
 * Overlay: last enabled mod wins, USRDIR layout, basename for movie cache.
 * Each case resets env so the cache cannot leak between them.
 */
#include "ps3emu/ps3_mods.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef _WIN32
#  include <direct.h>
#  define MKDIR(p) _mkdir(p)
#else
#  define MKDIR(p) mkdir((p), 0755)
#endif

static char g_root[512];

static void write_file(const char* path, const char* body)
{
    FILE* f = fopen(path, "wb");
    assert(f);
    fputs(body, f);
    fclose(f);
}

static void setup_tree(void)
{
    char cmd[640];
    snprintf(g_root, sizeof g_root, "/tmp/ps3_mods_test_%d", (int)getpid());
    snprintf(cmd, sizeof cmd, "rm -rf %s", g_root);
    assert(system(cmd) == 0);
    MKDIR(g_root);

    char p[640];
    snprintf(p, sizeof p, "%s/vfs", g_root); MKDIR(p);
    snprintf(p, sizeof p, "%s/vfs/foo.wad", g_root); write_file(p, "orig");
    snprintf(p, sizeof p, "%s/vfs/only_orig.txt", g_root); write_file(p, "orig-only");

    snprintf(p, sizeof p, "%s/mods", g_root); MKDIR(p);
    snprintf(p, sizeof p, "%s/mods/pack_a", g_root); MKDIR(p);
    snprintf(p, sizeof p, "%s/mods/pack_a/foo.wad", g_root); write_file(p, "A");
    snprintf(p, sizeof p, "%s/mods/pack_b", g_root); MKDIR(p);
    snprintf(p, sizeof p, "%s/mods/pack_b/USRDIR", g_root); MKDIR(p);
    snprintf(p, sizeof p, "%s/mods/pack_b/USRDIR/foo.wad", g_root); write_file(p, "B");
    snprintf(p, sizeof p, "%s/mods/pack_b/movie_cache", g_root); MKDIR(p);
    snprintf(p, sizeof p, "%s/mods/pack_b/movie_cache/SmLogo_v2.m2v", g_root);
    write_file(p, "MOV");
}

static int same_suffix(const char* path, const char* want)
{
    size_t lp = strlen(path), lw = strlen(want);
    if (lp < lw)
        return 0;
    return strcmp(path + lp - lw, want) == 0;
}

int main(void)
{
    setup_tree();
    char mods[640], vfs[640], host[1400];
    snprintf(mods, sizeof mods, "%s/mods", g_root);
    snprintf(vfs, sizeof vfs, "%s/vfs", g_root);

    /* Off when PS3_MODS_DIR is unset. */
    ps3_mods_reset();
    unsetenv("PS3_MODS_DIR");
    unsetenv("PS3_MODS_ENABLED");
    snprintf(host, sizeof host, "%s/foo.wad", vfs);
    assert(ps3_mods_overlay_host(vfs, host, sizeof host) == 0);
    assert(same_suffix(host, "/vfs/foo.wad"));

    /* pack_a replaces foo.wad at the mod root. */
    ps3_mods_reset();
    setenv("PS3_MODS_DIR", mods, 1);
    setenv("PS3_MODS_ENABLED", "pack_a", 1);
    snprintf(host, sizeof host, "%s/foo.wad", vfs);
    assert(ps3_mods_overlay_host(vfs, host, sizeof host) == 1);
    assert(same_suffix(host, "/mods/pack_a/foo.wad"));

    /* Last name wins: pack_b USRDIR layout beats pack_a. */
    ps3_mods_reset();
    setenv("PS3_MODS_DIR", mods, 1);
    setenv("PS3_MODS_ENABLED", "pack_a,pack_b", 1);
    snprintf(host, sizeof host, "%s/foo.wad", vfs);
    assert(ps3_mods_overlay_host(vfs, host, sizeof host) == 1);
    assert(same_suffix(host, "/mods/pack_b/USRDIR/foo.wad"));

    /* Unmodded file stays on the VFS path. */
    ps3_mods_reset();
    setenv("PS3_MODS_DIR", mods, 1);
    setenv("PS3_MODS_ENABLED", "pack_a", 1);
    snprintf(host, sizeof host, "%s/only_orig.txt", vfs);
    assert(ps3_mods_overlay_host(vfs, host, sizeof host) == 0);
    assert(same_suffix(host, "/vfs/only_orig.txt"));

    /* Empty enabled list: no overlay. */
    ps3_mods_reset();
    setenv("PS3_MODS_DIR", mods, 1);
    setenv("PS3_MODS_ENABLED", "", 1);
    snprintf(host, sizeof host, "%s/foo.wad", vfs);
    assert(ps3_mods_overlay_host(vfs, host, sizeof host) == 0);

    /* Basename hit for movie cache. */
    ps3_mods_reset();
    setenv("PS3_MODS_DIR", mods, 1);
    setenv("PS3_MODS_ENABLED", "pack_b", 1);
    assert(ps3_mods_overlay_basename("SmLogo_v2", host, sizeof host) == 1);
    assert(same_suffix(host, "/mods/pack_b/movie_cache/SmLogo_v2.m2v"));

    /* Unknown basename misses. */
    ps3_mods_reset();
    setenv("PS3_MODS_DIR", mods, 1);
    setenv("PS3_MODS_ENABLED", "pack_b", 1);
    assert(ps3_mods_overlay_basename("nope.m2v", host, sizeof host) == 0);

    printf("test_ps3_mods: PASS\n");
    return 0;
}
