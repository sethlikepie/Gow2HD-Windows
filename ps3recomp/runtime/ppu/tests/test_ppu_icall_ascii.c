/* Drives the shipped ASCII-CTR predicate used on the icall miss path. */
#include "../ppu_icall_ascii.h"
#include <stdio.h>

#define CHECK(cond) do { \
    if (!(cond)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); return 1; } \
} while (0)

int main(void)
{
    CHECK(ps3_icall_looks_like_ascii(0x005F6D6Fu) == 1); /* "_mo" */
    CHECK(ps3_icall_looks_like_ascii(0x41424344u) == 1); /* "ABCD" */
    CHECK(ps3_icall_looks_like_ascii(0x00000000u) == 0);
    CHECK(ps3_icall_looks_like_ascii(0x00000041u) == 0); /* one printable */
    CHECK(ps3_icall_looks_like_ascii(0x00293010u) == 0); /* guest code EA */
    CHECK(ps3_icall_looks_like_ascii(0x0A0C0DE0u) == 0); /* GCM overflow sentinel */
    return 0;
}
