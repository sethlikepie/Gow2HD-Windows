/* stderr_capture.h -- shared stderr-mark-and-scan helpers for the Task 5
 * (bringup-instrumentacao-fase0) PS3_TRACE unit tests.
 *
 * Review fix (follow-up pos-Task 5, item 6): ftell_stderr()/
 * stderr_range_contains() were pasted byte-for-byte into six test files
 * (runtime/ppu/tests/test_movie_eos_trace.c, test_fs_movie_io.cpp,
 * libs/video/tests/test_fios_aread_hle.c, test_rsx_trace.c, test_movie_io.c,
 * libs/codec/tests/test_cellvdec_force_gate.c) -- exactly the duplication
 * that runtime/trace/tests/trace_link_stubs.h was already extracted to stop
 * (see that header's comment for the earlier eight-harness instance of the
 * same pattern). Extracted here so adding a seventh consumer is a one-line
 * `#include`.
 *
 * Both functions stay `static` (internal linkage) on purpose, UNLIKE
 * trace_link_stubs.h's externally-linked stubs: this header is safe to
 * `#include` from more than one translation unit that ends up in the same
 * binary (each TU gets its own private copy, no duplicate-symbol error) --
 * none of the six consumers above needs more than one, but none is harmed
 * by it either.
 *
 * Usage: freopen() stderr to a private temp file first (read+write), take a
 * mark with ftell_stderr() before the action under test, then
 * stderr_range_contains(mark, needle) checks only the bytes written after
 * that mark -- same idiom in every one of the six original call sites.
 *
 * Works from both C and C++ (no linkage-name concerns: static functions
 * aren't name-mangled across the C/C++ boundary the way trace_link_stubs.h's
 * externs are, so no `extern "C"` needed here).
 */
#ifndef PS3RECOMP_STDERR_CAPTURE_H
#define PS3RECOMP_STDERR_CAPTURE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static long ftell_stderr(void)
{
    fflush(stderr);
    return ftell(stderr);
}

static int stderr_range_contains(long from, const char* needle)
{
    fflush(stderr);
    long to = ftell(stderr);
    if (to <= from) return 0;
    size_t len = (size_t)(to - from);
    char* buf = (char*)malloc(len + 1);
    if (!buf) return 0;
    fseek(stderr, from, SEEK_SET);
    size_t got = fread(buf, 1, len, stderr);
    buf[got] = '\0';
    fseek(stderr, to, SEEK_SET);   /* restore: keep appending at the end */
    int found = (strstr(buf, needle) != NULL);
    free(buf);
    return found;
}

#endif /* PS3RECOMP_STDERR_CAPTURE_H */
