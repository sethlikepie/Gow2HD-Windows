/* ThreadSanitizer discriminator for the spu_channel cross-thread handoff that
 * spu_channel_read_blocking() relies on (runtime/spu/spu_context.h).
 *
 * WHY THIS EXISTS (and why case5 of test_spu_channel_blocking_main.c is NOT
 * enough on its own):
 *
 *   The HIGH finding this PR fixes is a weak-memory data race -- a consumer on
 *   arm64 observing count>0 and then reading a STALE `value` because the
 *   producer's plain store of `value` had not yet become visible. The fix pairs
 *   a release-store of `count` in spu_channel_write() with an acquire-load of
 *   `count` in spu_channel_read_blocking(), giving a real happens-before edge.
 *
 *   A plain functional ping-pong test (case5) CANNOT reliably catch a
 *   regression of that fix: on a single-copy-atomic core (Apple Silicon) a
 *   naturally-aligned 32-bit word never tears, and a strict drain-then-write
 *   ping-pong with a millisecond poll closes the visibility window in wall-clock
 *   time -- so the broken plain-store/volatile-read code passes case5 with zero
 *   observed stale reads (empirically verified: 100k handoffs, 0 failures). case5
 *   is therefore only a STRUCTURAL guard on the acquire/release call path, not a
 *   test of the memory ordering itself.
 *
 *   ThreadSanitizer IS the discriminating test. It builds the happens-before
 *   graph from the atomic operations, independent of whether a stale read
 *   happens to manifest in a given run. With the acquire/release pair present it
 *   is clean; revert spu__count_load_acquire/spu__count_store_release to a plain
 *   store + volatile read and it deterministically reports a data race on
 *   ch->value (consumer read vs producer write) and aborts.  This was confirmed
 *   both ways on arm64 while writing the fix:
 *       fixed  header -> exit 0  ("no race")
 *       broken header -> ThreadSanitizer: data race ... in consumer, exit 134
 *
 * SELF-CONTAINED ON PURPOSE: this probe includes only spu_context.h. It does NOT
 * go through the gen_test + spu_lifter.py pipeline, so run_tests.sh can build and
 * run it even in trees where the SPU lift path is unavailable (e.g. an
 * interpreter too old for the tool type-hint syntax). run_tests.sh compiles it
 * with -fsanitize=thread; a clean exit is PASS, a TSan report (nonzero exit
 * under halt_on_error=1) is FAIL.
 *
 * It is a race detector, not a functional test -- it uses the SAME strict
 * ping-pong shape as case5 (producer drains via the acquire poll before writing,
 * so a single-slot mailbox is never overwritten while being read). Under correct
 * synchronization that shape is race-free; the whole point is that TSan proves
 * the ordering rather than hoping a stale read surfaces by timing.
 */
#include "spu_context.h"
#include <pthread.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

/* Enough concurrent handoffs to give the two threads ample real overlap for
 * TSan to observe; each is a sub-millisecond ping-pong so the whole probe stays
 * well under a second even under the ~10x TSan slowdown. */
#define ROUNDS      4000u
#define TIMEOUT_MS  2000u

/* Per-round value that differs every round, so a functional mismatch (stale
 * previous-round value) is also caught on any platform that happens to expose
 * one -- redundant with the TSan check but cheap. */
static uint32_t magic(uint32_t r) { return (r * 2654435761u) ^ 0xA5A5A5A5u; }

typedef struct {
    spu_channel* ch;
    uint32_t     rounds;
    uint32_t     timeout_ms;
    int          timed_out;
    int          mismatch;
    uint32_t     bad_round;
} cargs;

/* Consumer: read `rounds` values back-to-back via the primitive under test. */
static void* consumer(void* a) {
    cargs* c = (cargs*)a;
    for (uint32_t i = 0; i < c->rounds; i++) {
        uint32_t v = 0;
        if (!spu_channel_read_blocking(c->ch, c->timeout_ms, &v)) {
            c->timed_out = 1; c->bad_round = i; return NULL;
        }
        if (v != magic(i)) { c->mismatch = 1; c->bad_round = i; return NULL; }
    }
    return NULL;
}

int main(void) {
    spu_channel ch; memset(&ch, 0, sizeof ch);
    cargs c; memset(&c, 0, sizeof c);
    c.ch = &ch; c.rounds = ROUNDS; c.timeout_ms = TIMEOUT_MS;

    pthread_t tid;
    if (pthread_create(&tid, NULL, consumer, &c) != 0) {
        printf("FAIL: probe: pthread_create\n");
        return 1;
    }

    /* Producer (main thread): strict alternation. Wait (acquire-ordered) for the
     * consumer to drain the previous value before publishing the next, so the
     * single-slot mailbox is never overwritten mid-read. A bounded safety spin
     * keeps a wedged consumer from hanging the probe forever. */
    int stalled = 0;
    for (uint32_t i = 0; i < ROUNDS; i++) {
        long guard = 0;
        while (spu_channel_has_data_acquire(&ch)) {
            struct timespec r = { 0, 50000L };   /* 0.05 ms */
            nanosleep(&r, NULL);
            if (++guard > 400000L) { stalled = 1; break; }  /* ~20s -> give up */
        }
        if (stalled) break;
        spu_channel_write(&ch, magic(i));
    }

    pthread_join(tid, NULL);

    if (stalled) {
        printf("FAIL: probe: producer stalled waiting for the consumer to drain\n");
        return 1;
    }
    if (c.timed_out) {
        printf("FAIL: probe: consumer timed out on round %u of %u\n", c.bad_round, ROUNDS);
        return 1;
    }
    if (c.mismatch) {
        printf("FAIL: probe: round %u handed back a stale value\n", c.bad_round);
        return 1;
    }
    printf("OK: %u concurrent handoffs, TSan clean -- acquire/release ordering holds\n", ROUNDS);
    return 0;
}
