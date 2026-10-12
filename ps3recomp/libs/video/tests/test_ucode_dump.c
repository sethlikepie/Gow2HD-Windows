#include "rsx_ucode_dump.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int fails = 0;
static void check(const char* n, int c) { if (!c) { printf("FAIL %s\n", n); fails++; } else printf("[PASS] %s\n", n); }
static long fsize(const char* p) { struct stat st; return stat(p, &st) == 0 ? (long)st.st_size : -1; }

int main(void)
{
    const u8 fp[32] = { 1, 2, 3, 4 };
    const u32 vp[8] = { 0x11223344u, 0, 0, 1u };
    char dir[] = "/tmp/ucode_dump_test_XXXXXX";
    if (!mkdtemp(dir)) { printf("mkdtemp failed\n"); return 1; }
    char pf[256], pv[256];
    snprintf(pf, sizeof pf, "%s/0000ABCD.fp", dir);
    snprintf(pv, sizeof pv, "%s/00001234.vp", dir);

    /* dump_off_is_noop */
    unsetenv("PS3_RSX_DUMP_UCODE");
    rsx_ucode_dump_reset_for_test();
    rsx_ucode_dump(fp, sizeof fp, 0xABCDu, vp, 4, 0x1234u);
    check("dump_off_is_noop", fsize(pf) < 0 && fsize(pv) < 0);

    /* dump_writes_raw */
    setenv("PS3_RSX_DUMP_UCODE", dir, 1);
    rsx_ucode_dump_reset_for_test();
    rsx_ucode_dump(fp, sizeof fp, 0xABCDu, vp, 4, 0x1234u);
    check("dump_writes_fp", fsize(pf) == 32);
    check("dump_writes_vp", fsize(pv) == 16);
    {
        FILE* f = fopen(pv, "rb"); unsigned char b[4] = {0};
        if (f) { fread(b, 1, 4, f); fclose(f); }
        check("dump_vp_little_endian", b[0] == 0x44 && b[3] == 0x11);
    }

    /* dump_bad_dir: missing directory -> nothing, no crash */
    setenv("PS3_RSX_DUMP_UCODE", "/nonexistent/dir/for/ucode", 1);
    rsx_ucode_dump_reset_for_test();
    rsx_ucode_dump(fp, sizeof fp, 0x1u, vp, 4, 0x2u);
    check("dump_bad_dir", 1);

    /* dump_second_call_skips_fs / dump_reset_clears_seen: a repeat call with an
     * already-seen hash must not touch the filesystem at all (no fopen() to
     * re-probe an existing/missing file); rsx_ucode_dump_reset_for_test() must
     * clear that in-memory state so the same hash is written again. */
    {
        char px[256], pw[256];
        snprintf(px, sizeof px, "%s/00005678.fp", dir);
        snprintf(pw, sizeof pw, "%s/00009ABC.vp", dir);

        setenv("PS3_RSX_DUMP_UCODE", dir, 1);
        rsx_ucode_dump_reset_for_test();
        rsx_ucode_dump(fp, sizeof fp, 0x5678u, vp, 4, 0x9ABCu);
        check("dump_second_call_skips_fs_setup", fsize(px) == 32 && fsize(pw) == 16);

        /* Simulate the file having vanished (or never existed on this disk) --
         * the second call for the SAME hash must not recreate it. */
        remove(px);
        remove(pw);
        rsx_ucode_dump(fp, sizeof fp, 0x5678u, vp, 4, 0x9ABCu);
        check("dump_second_call_skips_fs", fsize(px) < 0 && fsize(pw) < 0);

        /* Reset clears the seen sets -- the same hash is written again. */
        rsx_ucode_dump_reset_for_test();
        rsx_ucode_dump(fp, sizeof fp, 0x5678u, vp, 4, 0x9ABCu);
        check("dump_reset_clears_seen", fsize(px) == 32 && fsize(pw) == 16);

        remove(px);
        remove(pw);
    }

    remove(pf); remove(pv); rmdir(dir);
    printf(fails ? "%d FAILED\n" : "ALL PASS\n", fails);
    return fails ? 1 : 0;
}
