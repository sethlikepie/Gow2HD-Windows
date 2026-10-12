/* scream_native_rt.c -- hybrid runner: SCREAM's mixer jobs as native lifted code inside the SPU
 * interpreter (winport v29, PS3_SCREAM_NATIVE=1; engine_fixes_v29_scream_native.py patches the
 * interpreter side). Hand-written, not game-derived. The lifted jobs are winport/scream_lift/scr?.c.
 *
 * Model. The policy module, its interrupt handler and every other piece of image-7 code stay in
 * the interpreter. When the interpreter branches to an address that is a lifted entry of the job
 * currently in LS (validated by a hash of the job's .text, recomputed after any DMA GET into the
 * job's code range), wp29 calls scr_enter(). The lifted code runs on the SAME spu_context (GPRs,
 * LS, MFC, interp_ie) and returns to the interpreter -- by longjmp from any depth -- at an exact
 * guest PC whenever the interpreter's semantics are needed: a call into the PM, a channel read
 * that would stall, an event that becomes pending with interrupts enabled, stop/iret, a return
 * whose $r0 does not match the C frame, an op budget. The guest state is fully in ctx, so the
 * interpreter simply continues there (and takes a pending interrupt first).
 */
#include "scr_native.h"
#include "spu_helpers.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* spu_interp.c (winport v29): the interpreter's own channel semantics + hook pointer. */
int      spu_interp_rdch_x(spu_context* ctx, uint32_t ch, u128* out);
uint32_t spu_interp_rchcnt_x(spu_context* ctx, uint32_t ch);
void     spu_interp_wrch_x(spu_context* ctx, uint32_t ch, u128 v);
void     spu_interp_dec_event_x(spu_context* ctx);
extern int (*g_wp29_enter)(spu_context*, uint32_t, uint64_t, uint32_t*, uint64_t*);
extern volatile int g_wp29_dirty;
unsigned long long wp27_now_us(void);

#define SCR_LO   0x4000u
#define SCR_SLOTS ((0x10000u - SCR_LO) >> 2)
#define SCR_MAXJ 4

typedef struct { scr_fn fn; uint32_t label; } scr_slot;
/* Minimal setjmp/longjmp for the bail path. clang's builtin setjmp does NOT restore the Win64
 * non-volatile RSI/RDI/XMM6-15 (crashed in v29 build 1), and the CRT longjmp unwinds via SEH (slow).
 * This one saves every Win64 callee-saved register; no frames in between need unwinding (lifted
 * code has no handlers). Buffer: rbx rbp rdi rsi r12-r15 rsp rip, then xmm6-15. */
typedef struct { uint64_t g[10]; uint64_t x[20]; } scr_jbuf;
extern int  scr_sj_set(scr_jbuf* b) __attribute__((returns_twice));
extern void scr_sj_jump(scr_jbuf* b) __attribute__((noreturn));
__asm__(
    ".text\n.p2align 4\n.globl scr_sj_set\nscr_sj_set:\n"
    "  movq %rbx, 0(%rcx)\n  movq %rbp, 8(%rcx)\n  movq %rdi, 16(%rcx)\n  movq %rsi, 24(%rcx)\n"
    "  movq %r12, 32(%rcx)\n  movq %r13, 40(%rcx)\n  movq %r14, 48(%rcx)\n  movq %r15, 56(%rcx)\n"
    "  leaq 8(%rsp), %rdx\n  movq %rdx, 64(%rcx)\n  movq (%rsp), %rdx\n  movq %rdx, 72(%rcx)\n"
    "  movdqu %xmm6, 80(%rcx)\n  movdqu %xmm7, 96(%rcx)\n  movdqu %xmm8, 112(%rcx)\n  movdqu %xmm9, 128(%rcx)\n"
    "  movdqu %xmm10, 144(%rcx)\n  movdqu %xmm11, 160(%rcx)\n  movdqu %xmm12, 176(%rcx)\n  movdqu %xmm13, 192(%rcx)\n"
    "  movdqu %xmm14, 208(%rcx)\n  movdqu %xmm15, 224(%rcx)\n"
    "  xorl %eax, %eax\n  ret\n"
    ".p2align 4\n.globl scr_sj_jump\nscr_sj_jump:\n"
    "  movq 0(%rcx), %rbx\n  movq 8(%rcx), %rbp\n  movq 16(%rcx), %rdi\n  movq 24(%rcx), %rsi\n"
    "  movq 32(%rcx), %r12\n  movq 40(%rcx), %r13\n  movq 48(%rcx), %r14\n  movq 56(%rcx), %r15\n"
    "  movdqu 80(%rcx), %xmm6\n  movdqu 96(%rcx), %xmm7\n  movdqu 112(%rcx), %xmm8\n  movdqu 128(%rcx), %xmm9\n"
    "  movdqu 144(%rcx), %xmm10\n  movdqu 160(%rcx), %xmm11\n  movdqu 176(%rcx), %xmm12\n  movdqu 192(%rcx), %xmm13\n"
    "  movdqu 208(%rcx), %xmm14\n  movdqu 224(%rcx), %xmm15\n"
    "  movq 64(%rcx), %rsp\n  movl $1, %eax\n  jmpq *72(%rcx)\n");

static scr_slot* s_tbl[SCR_MAXJ];
static int       s_cur = -1;            /* job in LS, -1 = none / unknown */
uint32_t         g_scr_resume;
static scr_jbuf  s_jb;
static uint32_t  s_bail_pc, s_ret_pc;
static int       s_ret_valid, s_bail_kind;
static uint64_t  s_ops, s_ops_limit;
static unsigned long long s_t0;

enum { BK_IB, BK_RDCH, BK_EVENT, BK_INSN, BK_RET, BK_BUDGET, BK_N };
static const char* const s_bk_name[BK_N] = { "ib", "rdch", "event", "insn", "ret", "budget" };
static unsigned long long st_native_us, st_enter, st_bail[BK_N], st_topret, st_ops, st_hash, st_hash_miss, st_noent;
static unsigned st_pm_pc[8], st_pm_n[8], st_ne_pc[12], st_ne_n[12];

static uint64_t scr_hash(const uint8_t* p, uint32_t n)
{
    uint64_t h = 0xCBF29CE484222325ull, w;
    for (uint32_t i = 0; i + 8 <= n; i += 8) { memcpy(&w, p + i, 8); h = (h ^ w) * 0x100000001B3ull; }
    return h;
}

static void scr_build(void)
{
    for (unsigned j = 0; j < g_scr_njobs && j < SCR_MAXJ; j++) {
        s_tbl[j] = (scr_slot*)calloc(SCR_SLOTS, sizeof(scr_slot));
        for (unsigned k = 0; k < g_scr_jobs[j].n; k++) {
            const scr_ent_t* e = &g_scr_jobs[j].ents[k];
            if (e->addr < SCR_LO || e->addr >= 0x10000u) continue;
            scr_slot* s = &s_tbl[j][(e->addr - SCR_LO) >> 2];
            if (s->fn && !e->label) continue;          /* a function start wins over a label */
            s->fn = e->fn; s->label = e->label;
        }
    }
}

static int scr_validate(spu_context* ctx)
{
    if (!g_wp29_dirty) return s_cur;
    g_wp29_dirty = 0;
    const int was = s_cur;
    s_cur = -1;
    st_hash++;
    for (unsigned j = 0; j < g_scr_njobs && j < SCR_MAXJ; j++)
        if (scr_hash(ctx->ls + g_scr_jobs[j].lo, g_scr_jobs[j].hi - g_scr_jobs[j].lo) == g_scr_jobs[j].hash) {
            s_cur = (int)j; break;
        }
    if (s_cur < 0) st_hash_miss++;
    static unsigned told;
    if (s_cur != was && told < 24) {
        told++;
        fprintf(stderr, "[SCR29] job in LS: %s -> %s\n", was < 0 ? "none" : g_scr_jobs[was].name,
                s_cur < 0 ? "none (interpreter)" : g_scr_jobs[s_cur].name);
        fflush(stderr);
    }
    return s_cur;
}

__attribute__((noreturn)) static void scr_bail_k(spu_context* ctx, uint32_t pc, int kind)
{
    (void)ctx;
    s_bail_pc = pc & 0x3FFFCu;
    s_bail_kind = kind;
    scr_sj_jump(&s_jb);
}
__attribute__((noreturn)) void scr_bail(spu_context* ctx, uint32_t pc) { scr_bail_k(ctx, pc, BK_INSN); }

u128 scr_rdch(spu_context* ctx, uint32_t ch, uint32_t pc)
{
    u128 v;
    s_ops++;
    if (!spu_interp_rdch_x(ctx, ch, &v)) scr_bail_k(ctx, pc, BK_RDCH);
    return v;
}

uint32_t scr_rchcnt(spu_context* ctx, uint32_t ch) { return spu_interp_rchcnt_x(ctx, ch); }

void scr_wrch(spu_context* ctx, uint32_t ch, u128 v, uint32_t next)
{
    s_ops++;
    spu_interp_wrch_x(ctx, ch, v);
    if (ctx->interp_ie && (ctx->event_status & ctx->event_mask)) scr_bail_k(ctx, next, BK_EVENT);
}

static inline const scr_slot* scr_slot_at(uint32_t t)
{
    if (s_cur < 0 || t < g_scr_jobs[s_cur].lo || t >= g_scr_jobs[s_cur].hi) return NULL;
    const scr_slot* s = &s_tbl[s_cur][(t - SCR_LO) >> 2];
    return s->fn ? s : NULL;
}

void scr_ib(spu_context* ctx)
{
    const uint32_t t = ctx->pc & 0x3FFFCu;
    if (ctx->interp_ie) {
        if ((ctx->event_mask & 0x20u) && (s_ops & 255u) == 0) spu_interp_dec_event_x(ctx);
        if (ctx->event_status & ctx->event_mask) scr_bail_k(ctx, t, BK_EVENT);
    }
    if (++s_ops > s_ops_limit) scr_bail_k(ctx, t, BK_BUDGET);
    const int d = ctx->call_depth;
    if (d > 0 && d <= 64 && t == ctx->call_ret[d - 1]) return;          /* return to the C caller */
    const scr_slot* s = scr_slot_at(t);
    if (!s) {
        if (t < SCR_LO) {                          /* which PM services the job calls (stats) */
            for (int i = 0; i < 8; i++) {
                if (st_pm_pc[i] == t || !st_pm_n[i]) { st_pm_pc[i] = t; st_pm_n[i]++; break; }
            }
        }
        scr_bail_k(ctx, t, BK_IB);
    }
    g_scr_resume = s->label ? t : 0u;
    ctx->pc = t;
    g_spu_trampoline_fn = s->fn;
}

void scr_ret0(spu_context* ctx)
{
    const uint32_t t = ctx->gpr[0]._u32[0] & 0x3FFFCu;
    const int d = ctx->call_depth;
    if (d <= 0) { s_ret_pc = t; s_ret_valid = 1; return; }               /* out of the entry frame */
    if (d <= 64 && t != ctx->call_ret[d - 1]) scr_bail_k(ctx, t, BK_RET);
}

static void scr_report(void)
{
    static unsigned long long t_last;
    const unsigned long long now = wp27_now_us();
    if (!t_last) { t_last = now; return; }
    if (now - t_last < 5000000ull) return;
    t_last = now;
    fprintf(stderr, "[SCR29] native=%llums job=%s enters=%llu topret=%llu ops=%llu hash=%llu miss=%llu noent=%llu bails:",
            st_native_us / 1000ull, s_cur < 0 ? "none" : g_scr_jobs[s_cur].name, st_enter, st_topret, st_ops, st_hash, st_hash_miss, st_noent);
    for (int k = 0; k < BK_N; k++) fprintf(stderr, " %s=%llu", s_bk_name[k], st_bail[k]);
    fprintf(stderr, " pm:");
    for (int i = 0; i < 8 && st_pm_n[i]; i++) fprintf(stderr, " %05X=%u", st_pm_pc[i], st_pm_n[i]);
    fprintf(stderr, " noent:");
    for (int i = 0; i < 12 && st_ne_n[i]; i++) fprintf(stderr, " %05X=%u", st_ne_pc[i], st_ne_n[i]);
    fprintf(stderr, "\n");
    fflush(stderr);
}

/* Called by the interpreter (spu_interp.c) at a branch target pc. Returns 1 when native code ran:
 * *out_pc = where the interpreter continues, *out_ops = native ops (budget accounting). */
__attribute__((noinline)) static int scr_enter(spu_context* ctx, uint32_t pc, uint64_t budget,
                                               uint32_t* out_pc, uint64_t* out_ops)
{
    if (scr_validate(ctx) < 0) return 0;
    const scr_slot* s = scr_slot_at(pc);
    if (!s) {
        st_noent++;
        for (int i = 0; i < 12; i++)
            if (st_ne_pc[i] == pc || !st_ne_n[i]) { st_ne_pc[i] = pc; st_ne_n[i]++; break; }
        return 0;
    }
    st_enter++;
    scr_report();
    s_t0 = wp27_now_us();
    s_ops = 0;
    s_ops_limit = budget / 16u + 1u;
    ctx->call_depth = 0;
    g_spu_trampoline_fn = 0;
    if (scr_sj_set(&s_jb)) {
        g_spu_trampoline_fn = 0;
        g_scr_resume = 0;
        st_bail[s_bail_kind]++;
        st_ops += s_ops;
        st_native_us += wp27_now_us() - s_t0;
        *out_pc = s_bail_pc;
        *out_ops = s_ops;
        return 1;
    }
    for (;;) {
        s_ret_valid = 0;
        g_scr_resume = s->label ? pc : 0u;
        ctx->pc = pc;
        s->fn(ctx);
        SPU_DRAIN(ctx);
        if (!s_ret_valid) {
            static int told;
            if (!told) { told = 1; fprintf(stderr, "[SCR29] lifted code returned without a guest return (pc=0x%05X)\n", ctx->pc); fflush(stderr); }
            pc = ctx->pc & 0x3FFFCu;
            break;
        }
        pc = s_ret_pc;
        st_topret++;
        if (++s_ops > s_ops_limit || !(s = scr_slot_at(pc))) break;
    }
    st_ops += s_ops;
    st_native_us += wp27_now_us() - s_t0;
    *out_pc = pc;
    *out_ops = s_ops;
    return 1;
}

__attribute__((constructor)) static void scr_init(void)
{
    const char* e = getenv("PS3_SCREAM_NATIVE");
    if (!e || e[0] != '1') return;
    scr_build();
    g_wp29_enter = scr_enter;
    fprintf(stderr, "[SCR29] SCREAM native jobs ON: %u lifted job image(s)", g_scr_njobs);
    for (unsigned j = 0; j < g_scr_njobs; j++)
        fprintf(stderr, " %s[0x%05X-0x%05X %u entries]", g_scr_jobs[j].name, g_scr_jobs[j].lo, g_scr_jobs[j].hi, g_scr_jobs[j].n);
    fprintf(stderr, "\n");
    fflush(stderr);
}
