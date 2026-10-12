/* Isolated unit test for spu_channel_read_blocking() (runtime/spu/spu_context.h).
 *
 * This does NOT exercise the lifted SPU program in this directory's pair
 * (test_spu_channel_blocking.elf is a single `stop`, never invoked below) --
 * it tests a plain host-side C primitive directly:
 *
 *   Case 1 (write-then-read): a pthread sleeps ~50ms then writes a known
 *   value to a channel. The main thread calls spu_channel_read_blocking()
 *   with a generous timeout and asserts it (a) returns success, (b) yields
 *   the correct value, and (c) only returned after the write actually
 *   happened (elapsed >= ~40ms, allowing scheduling slack below the 50ms
 *   producer delay).
 *
 *   Case 2 (no writer / timeout): a fresh, never-written channel is read
 *   with a short, bounded timeout. Asserts the call returns failure (0)
 *   and that elapsed time stays within a small, predictable window above
 *   the requested timeout -- proving the poll loop is bounded and never
 *   spins forever.
 *
 *   Case 3 (post-timeout invariant): after a no-writer timeout, the channel
 *   itself must be untouched -- count still 0, value still 0. Guards against a
 *   timeout path that accidentally consumes/mutates channel state.
 *
 *   Case 4 (late publish in the last poll quantum): the producer writes at a
 *   delay only a little under the timeout, so the write lands after several
 *   poll iterations, near the deadline. Asserts the value is still observed
 *   (return 1, correct value) -- the loop checks count BEFORE the deadline on
 *   every iteration, so data always wins a same-iteration race with expiry.
 *
 *   Case 5 (repeated-iteration ping-pong): a consumer thread performs many
 *   back-to-back blocking reads of a producer-written incrementing sequence
 *   (strict ping-pong via the acquire/release count handoff). Every consumed
 *   value must equal the round's expected magic. This is a STRUCTURAL regression
 *   guard on the acquire/release call PATH -- it proves the handoff still hands
 *   back the right values under sustained back-to-back use. It does NOT, by
 *   itself, prove the memory ORDERING: on a single-copy-atomic core (Apple
 *   Silicon) a naturally-aligned 32-bit word never tears, and a 1ms-poll
 *   ping-pong closes the visibility window in wall-clock time, so even the old
 *   broken plain-store/volatile-read code passes this case with zero observed
 *   stale reads (measured: 100k handoffs, 0 failures). The test that actually
 *   DISCRIMINATES the fixed vs. broken synchronization is the separate
 *   ThreadSanitizer probe, tsan_spu_channel_probe.c, which run_tests.sh builds
 *   with -fsanitize=thread: TSan reasons about the happens-before graph directly
 *   (clean with the acquire/release pair, a reported data race on ch->value if
 *   it regresses to store+volatile) instead of hoping a stale read surfaces by
 *   timing. See that file's header for the two-way verification.
 *
 * spu_channel_read_blocking() is currently NOT called from anywhere else in
 * the runtime (see the comment above its definition in spu_context.h) --
 * this test is its only exerciser.
 */
#include "spu_recomp.h"
#include <pthread.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

/* This test never runs the generated spu_func_00000000 ("stop"-only) nor
 * spu_recomp_register(), but both are part of every out_<name>/spu_recomp.c
 * this suite's run_tests.sh links in, so the glue symbols they reference must
 * still be defined to satisfy the linker (same stub pattern every other
 * test_*_main.c in this directory uses). */
u128 spu_rdch(spu_context* ctx, uint32_t channel) { (void)ctx; (void)channel; return spu_make_preferred_u32(0); }
uint32_t spu_rchcnt(spu_context* ctx, uint32_t channel) { (void)ctx; (void)channel; return 1; }
void spu_wrch(spu_context* ctx, uint32_t channel, u128 value) { (void)ctx; (void)channel; (void)value; }
void spu_indirect_branch(spu_context* ctx) {
    (void)ctx; fprintf(stderr, "FAIL: unexpected indirect branch\n");
}
void spu_register_function(uint32_t addr, void (*fn)(spu_context*)) { (void)addr; (void)fn; }

#define PRODUCER_DELAY_MS   50u
#define CASE1_TIMEOUT_MS     500u
#define CASE2_TIMEOUT_MS     100u
/* Generous upper bound on top of CASE2_TIMEOUT_MS to absorb scheduling
 * jitter from the 1ms poll granularity -- proves "bounded", not "instant". */
#define CASE2_SLACK_MS       500u

/* Case 4: producer writes at CASE4_DELAY_MS, reader waits up to
 * CASE4_TIMEOUT_MS. The margin (timeout - delay) is many poll quanta wide so
 * the write reliably lands late-but-before-deadline without being flaky. */
#define CASE4_DELAY_MS        50u
#define CASE4_TIMEOUT_MS      65u

/* Case 5: repeated ping-pong handoff. Enough rounds to exercise the
 * acquire/release path hard while keeping the whole test well under a second
 * (each round is a sub-millisecond handoff in steady state). */
#define STRESS_ROUNDS       500u
#define STRESS_TIMEOUT_MS  2000u

#define KNOWN_VALUE          0xC0FFEEu

typedef struct {
    spu_channel* ch;
    uint32_t     delay_ms;
    uint32_t     value;
} producer_args;

static void* producer_thread(void* arg)
{
    producer_args* pa = (producer_args*)arg;
    struct timespec req;
    req.tv_sec = pa->delay_ms / 1000;
    req.tv_nsec = (long)(pa->delay_ms % 1000) * 1000000L;
    nanosleep(&req, NULL);
    spu_channel_write(pa->ch, pa->value);
    return NULL;
}

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1.0e6;
}

/* ---- Case 5 (stress ping-pong) support -------------------------------- */

/* A per-round value that differs every round, so a stale (previous-round)
 * value would fail the equality check IF one were ever observed. Note this only
 * bites when a stale read actually surfaces at runtime -- on a single-copy-atomic
 * core it may never, which is exactly why the discriminating check lives in the
 * ThreadSanitizer probe (tsan_spu_channel_probe.c), not here. Cheap
 * invertible-ish hash, never zero-heavy in a way that hides a fresh-vs-memset(0)
 * mixup. */
static uint32_t stress_magic(uint32_t round)
{
    return (round * 2654435761u) ^ 0xA5A5A5A5u;
}

typedef struct {
    spu_channel* ch;
    uint32_t     rounds;
    uint32_t     timeout_ms;
    int          timed_out;     /* a blocking read gave up (should never) */
    int          mismatch;      /* a consumed value != expected magic     */
    uint32_t     bad_round;
    uint32_t     bad_expected;
    uint32_t     bad_got;
} stress_args;

/* Consumer side: read `rounds` values back-to-back, each must equal the
 * round's magic. Stops early and records the first anomaly. */
static void* stress_consumer(void* arg)
{
    stress_args* sa = (stress_args*)arg;
    for (uint32_t i = 0; i < sa->rounds; i++) {
        uint32_t v = 0;
        if (!spu_channel_read_blocking(sa->ch, sa->timeout_ms, &v)) {
            sa->timed_out = 1;
            sa->bad_round = i;
            return NULL;
        }
        if (v != stress_magic(i)) {
            sa->mismatch = 1;
            sa->bad_round = i;
            sa->bad_expected = stress_magic(i);
            sa->bad_got = v;
            return NULL;
        }
    }
    return NULL;
}

int main(void)
{
    int failed = 0;

    /* ---- Case 1: producer writes after PRODUCER_DELAY_MS, reader blocks ---- */
    {
        spu_channel ch;
        memset(&ch, 0, sizeof(ch));

        producer_args pa;
        pa.ch = &ch;
        pa.delay_ms = PRODUCER_DELAY_MS;
        pa.value = KNOWN_VALUE;

        pthread_t tid;
        if (pthread_create(&tid, NULL, producer_thread, &pa) != 0) {
            printf("FAIL: case1: could not spawn producer thread\n");
            return 1;
        }

        double t0 = now_ms();
        uint32_t out_value = 0;
        int got = spu_channel_read_blocking(&ch, CASE1_TIMEOUT_MS, &out_value);
        double elapsed = now_ms() - t0;

        pthread_join(tid, NULL);

        /* Allow scheduling slack below the producer's own delay: the read
         * must not have observed data before the write could plausibly have
         * happened. */
        const double kMinElapsed = (double)PRODUCER_DELAY_MS - 10.0;

        if (!got) {
            printf("FAIL: case1: spu_channel_read_blocking timed out (expected the "
                   "producer's write to be observed)\n");
            failed = 1;
        } else if (out_value != KNOWN_VALUE) {
            printf("FAIL: case1: got value 0x%X, expected 0x%X\n", out_value, KNOWN_VALUE);
            failed = 1;
        } else if (elapsed < kMinElapsed) {
            printf("FAIL: case1: returned after only %.2fms (producer delay is %ums) -- "
                   "read did not actually wait for the write\n", elapsed, PRODUCER_DELAY_MS);
            failed = 1;
        } else {
            printf("OK: case1: blocking read observed write of 0x%X after %.2fms "
                   "(producer delay %ums)\n", out_value, elapsed, PRODUCER_DELAY_MS);
        }

        if (spu_channel_has_data(&ch)) {
            printf("FAIL: case1: channel still reports data after a successful read "
                   "(count not cleared)\n");
            failed = 1;
        }
    }

    /* ---- Case 2: no writer at all -- must time out, never spin forever ---- */
    {
        spu_channel ch;
        memset(&ch, 0, sizeof(ch));

        double t0 = now_ms();
        uint32_t out_value = 0xDEADBEEFu;
        int got = spu_channel_read_blocking(&ch, CASE2_TIMEOUT_MS, &out_value);
        double elapsed = now_ms() - t0;

        if (got) {
            printf("FAIL: case2: spu_channel_read_blocking returned success with no "
                   "writer ever present\n");
            failed = 1;
        } else if (out_value != 0xDEADBEEFu) {
            printf("FAIL: case2: out_value was written to on a timeout path (0x%X)\n",
                   out_value);
            failed = 1;
        } else if (elapsed < (double)CASE2_TIMEOUT_MS - 10.0) {
            printf("FAIL: case2: timed out too early (%.2fms, requested %ums)\n",
                   elapsed, CASE2_TIMEOUT_MS);
            failed = 1;
        } else if (elapsed > (double)CASE2_TIMEOUT_MS + (double)CASE2_SLACK_MS) {
            printf("FAIL: case2: timeout took %.2fms -- requested %ums + %ums slack "
                   "exceeded (looks unbounded / spinning)\n",
                   elapsed, CASE2_TIMEOUT_MS, CASE2_SLACK_MS);
            failed = 1;
        } else {
            printf("OK: case2: no-writer read timed out after %.2fms as expected "
                   "(requested %ums, bounded -- never infinite)\n", elapsed, CASE2_TIMEOUT_MS);
        }
    }

    /* ---- Case 3: after a no-writer timeout the channel is untouched ------- */
    {
        spu_channel ch;
        memset(&ch, 0, sizeof(ch));

        uint32_t out_value = 0x1234u;
        int got = spu_channel_read_blocking(&ch, CASE2_TIMEOUT_MS, &out_value);

        if (got) {
            printf("FAIL: case3: timeout path unexpectedly returned success\n");
            failed = 1;
        } else if (ch.count != 0 || ch.value != 0) {
            printf("FAIL: case3: channel mutated by a timeout (count=%u, value=0x%X) -- "
                   "must stay untouched\n", ch.count, ch.value);
            failed = 1;
        } else {
            printf("OK: case3: channel state (count=0, value=0) intact after timeout\n");
        }
    }

    /* ---- Case 4: write lands late (near the deadline) but is still seen --- */
    {
        spu_channel ch;
        memset(&ch, 0, sizeof(ch));

        producer_args pa;
        pa.ch = &ch;
        pa.delay_ms = CASE4_DELAY_MS;
        pa.value = KNOWN_VALUE;

        pthread_t tid;
        if (pthread_create(&tid, NULL, producer_thread, &pa) != 0) {
            printf("FAIL: case4: could not spawn producer thread\n");
            return 1;
        }

        double t0 = now_ms();
        uint32_t out_value = 0;
        int got = spu_channel_read_blocking(&ch, CASE4_TIMEOUT_MS, &out_value);
        double elapsed = now_ms() - t0;

        pthread_join(tid, NULL);

        if (!got) {
            printf("FAIL: case4: late write (delay %ums, timeout %ums) was NOT observed -- "
                   "the poll gave up on a same-iteration race it should win\n",
                   CASE4_DELAY_MS, CASE4_TIMEOUT_MS);
            failed = 1;
        } else if (out_value != KNOWN_VALUE) {
            printf("FAIL: case4: got value 0x%X, expected 0x%X\n", out_value, KNOWN_VALUE);
            failed = 1;
        } else if (elapsed < (double)CASE4_DELAY_MS - 10.0) {
            printf("FAIL: case4: returned after only %.2fms (write delay is %ums) -- "
                   "did not actually wait for the late write\n", elapsed, CASE4_DELAY_MS);
            failed = 1;
        } else {
            printf("OK: case4: late write of 0x%X observed after %.2fms "
                   "(delay %ums, timeout %ums)\n",
                   out_value, elapsed, CASE4_DELAY_MS, CASE4_TIMEOUT_MS);
        }
    }

    /* ---- Case 5: repeated ping-pong -- every consumed value must match ---- */
    {
        spu_channel ch;
        memset(&ch, 0, sizeof(ch));

        stress_args sa;
        memset(&sa, 0, sizeof(sa));
        sa.ch = &ch;
        sa.rounds = STRESS_ROUNDS;
        sa.timeout_ms = STRESS_TIMEOUT_MS;

        pthread_t tid;
        if (pthread_create(&tid, NULL, stress_consumer, &sa) != 0) {
            printf("FAIL: case5: could not spawn consumer thread\n");
            return 1;
        }

        /* Producer: strict alternation. Wait (acquire-ordered) for the
         * consumer to drain the previous value before publishing the next, so
         * no write is lost or overwritten. A bounded safety spin keeps a dead
         * consumer from hanging the test forever. */
        int producer_stalled = 0;
        for (uint32_t i = 0; i < STRESS_ROUNDS; i++) {
            long guard = 0;
            while (spu_channel_has_data_acquire(&ch)) {
                struct timespec r;
                r.tv_sec = 0;
                r.tv_nsec = 100000L;          /* 0.1 ms */
                nanosleep(&r, NULL);
                if (++guard > 200000L) {       /* ~20s worst case -> give up */
                    producer_stalled = 1;
                    break;
                }
            }
            if (producer_stalled)
                break;
            spu_channel_write(&ch, stress_magic(i));
        }

        pthread_join(tid, NULL);

        if (producer_stalled) {
            printf("FAIL: case5: producer stalled waiting for the consumer to drain "
                   "(consumer likely wedged/timed out)\n");
            failed = 1;
        } else if (sa.timed_out) {
            printf("FAIL: case5: consumer timed out on round %u of %u\n",
                   sa.bad_round, STRESS_ROUNDS);
            failed = 1;
        } else if (sa.mismatch) {
            printf("FAIL: case5: round %u handed back 0x%X, expected 0x%X "
                   "(value mismatch -- handoff broken)\n",
                   sa.bad_round, sa.bad_got, sa.bad_expected);
            failed = 1;
        } else {
            printf("OK: case5: %u ping-pong rounds all handed back the exact "
                   "expected value (structural guard on the acquire/release path; "
                   "the TSan probe proves the ordering)\n", STRESS_ROUNDS);
        }
    }

    if (!failed)
        printf("PASS: spu_channel_read_blocking is additive/unwired and works in isolation.\n");
    return failed;
}
