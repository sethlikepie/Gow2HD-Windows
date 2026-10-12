/* Unit tests for the RPCS3-style patch engine (runtime/patch/ps3_patch.c). */
#include "ps3emu/ps3_patch.h"

#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

int main(void)
{
    ps3_patch_write w;

    CHECK(ps3_patch_parse_line("", &w) == 0, "blank line");
    CHECK(ps3_patch_parse_line("   # comment", &w) == 0, "comment");

    CHECK(ps3_patch_parse_line("be32 0x002C1A88 0x38000001", &w) == 1, "be32");
    CHECK(w.addr == 0x002C1A88 && w.len == 4 &&
          !memcmp(w.data, "\x38\x00\x00\x01", 4), "be32 bytes");

    CHECK(ps3_patch_parse_line("bef32 0x53EECC 1.0", &w) == 1, "bef32");
    CHECK(!memcmp(w.data, "\x3F\x80\x00\x00", 4), "bef32 1.0 big-endian");

    CHECK(ps3_patch_parse_line("le16 0x10 0x1234", &w) == 1 && !memcmp(w.data, "\x34\x12", 2), "le16");
    CHECK(ps3_patch_parse_line("byte 0x10 -1", &w) == 1 && w.data[0] == 0xFF, "byte -1");
    CHECK(ps3_patch_parse_line("byte 0x10 0x100", &w) == -1, "byte overflow rejected");
    CHECK(ps3_patch_parse_line("be64 0x10 0x0102030405060708", &w) == 1 &&
          !memcmp(w.data, "\x01\x02\x03\x04\x05\x06\x07\x08", 8), "be64");
    CHECK(ps3_patch_parse_line("c_utf8 0x20 \"Hi\"", &w) == 1 && w.len == 3 &&
          !memcmp(w.data, "Hi\0", 3), "c_utf8 terminator");
    CHECK(ps3_patch_parse_line("utf8 0x20 \"Hi\"", &w) == 1 && w.len == 2, "utf8 no terminator");
    CHECK(ps3_patch_parse_line("bogus 0x20 1", &w) == -1, "unknown type");
    CHECK(ps3_patch_parse_line("be32 0x20", &w) == -1, "missing value");
    CHECK(ps3_patch_parse_line("be32 0x20 12junk", &w) == -1, "trailing garbage");

    /* apply: one data write, one code write, one out of range */
    static uint8_t mem[0x100];
    const char* path = "/tmp/ps3_patch_test.txt";
    FILE* f = fopen(path, "w");
    fprintf(f, "# patch: test\nbe32 0x80 0xDEADBEEF\nbe32 0x10 0x11223344\nbe32 0xFE 0x1\n");
    fclose(f);
    ps3_patch_range loaded = { 0x00, 0x100 };
    ps3_patch_range exec = { 0x00, 0x40 };
    ps3_patch_stats st = ps3_patch_apply_file(path, mem, &loaded, 1, &exec, 1);
    CHECK(st.applied == 1 && st.code == 1 && st.rejected == 1, "stats %d %d %d",
          st.applied, st.code, st.rejected);
    CHECK(!memcmp(mem + 0x80, "\xDE\xAD\xBE\xEF", 4), "data write landed");
    CHECK(!memcmp(mem + 0x10, "\x11\x22\x33\x44", 4), "code write still lands in memory");
    remove(path);

    printf("test_ps3_patch: %s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
