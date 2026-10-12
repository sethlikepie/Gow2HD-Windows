/*
 * ps3recomp - SPU interpreter (see spu_interp.h)
 *
 * Fetch / decode / execute straight from local store. The ALU semantics and the
 * decode table come from spu_interp_gen.h, which tools/gen_spu_interp.py
 * derives from the lifter and the disassembler; this file only holds what an
 * interpreter does differently from a lift: control flow, local-store access,
 * channels (incl. blocking reads), stop, and SPU interrupts.
 *
 * Interrupts are the hardware ones (CBEA / SPU ISA; RPCS3's interpreter used
 * as a cross-check oracle, no code copied): with interrupts enabled and
 * (event_status & event_mask) != 0 the SPU sets SRR0 = the address of the next
 * instruction, disables interrupts and continues at LS 0. The E / D flags of
 * the indirect branches (and of iret) enable / disable them. Because the code
 * runs from local store, the handler vector the program writes at LS 0 and any
 * code it generates at run time just execute.
 */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE) && !defined(_DARWIN_C_SOURCE)
#  define _DARWIN_C_SOURCE 1
#endif
#include "spu_interp.h"
#include "spu_helpers.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#  include <windows.h>
#else
#  include <time.h>
#endif

/* Runtime glue implemented in spu_channels.c (or by a test harness). */
u128     spu_rdch(spu_context* ctx, uint32_t channel);
uint32_t spu_rchcnt(spu_context* ctx, uint32_t channel);
void     spu_wrch(spu_context* ctx, uint32_t channel, u128 value);
void     spu_halt_if(spu_context* ctx, int taken, uint32_t pc);
uint32_t spu_mfc_tag_completed(spu_context* ctx);

#include "spu_interp_gen.h"

/* winport v28-scream: executed-instruction / branch-target bitmaps of image 7 for the lift dump
 * (PS3_SCREAM_DUMP, spu_workload.c). One predictable branch per instruction while off. */
volatile int g_wp28_cov = 0;
unsigned char g_wp28_cov_bits[8192], g_wp28_tgt_bits[8192];

/* winport v27-audio: sampled pc of image 7 (PS3_AUDIO_DIAG27=1), counted in cellAudio.c. Tells a
 * SCREAM that is busy mixing from one spinning in a poll loop. */
extern volatile int g_wp27_pcs;
void wp27_pc_sample(int image_id, unsigned pc);

/* SPU event bits (SPU_RdEventStat). */
#define SPUI_EV_TM  0x20u        /* decrementer 0 -> -1 */

/* Decrementer / timebase frequency of the CELL (79.8 MHz). */
#define SPUI_TB_NUM 798u
#define SPUI_TB_DEN 10000u       /* ticks = ns * 798 / 10000 */

static SPU_THREAD_LOCAL uint64_t t_last_count;

uint64_t spu_interp_last_count(void) { return t_last_count; }

const char* spu_interp_result_name(int r)
{
    switch (r) {
    case SPU_INTERP_EXIT:   return "exit";
    case SPU_INTERP_STOP:   return "stop";
    case SPU_INTERP_BUDGET: return "budget";
    case SPU_INTERP_WAIT:   return "wait";
    case SPU_INTERP_FAULT:  return "fault";
    default:                return "?";
    }
}

static uint64_t spui_now_ns(void)
{
#ifdef _WIN32
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return f.QuadPart ? (uint64_t)((c.QuadPart * 1000000000ull) / (uint64_t)f.QuadPart) : 1;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
#endif
}

static inline uint32_t spui_ls32(const spu_context* ctx, uint32_t lsa)
{
    return spu_ls_read32(ctx, lsa);
}

/* Decrementer: counts down at the timebase from the value last written. */
static uint32_t spui_dec(spu_context* ctx)
{
    const uint64_t now = spui_now_ns();
    if (!ctx->interp_dec_t0_ns) {                 /* never written: start now */
        ctx->interp_dec_t0_ns = now;
        ctx->interp_dec_v0 = ctx->decrementer;
    }
    const uint64_t ticks = (now - ctx->interp_dec_t0_ns) * SPUI_TB_NUM / SPUI_TB_DEN;
    return ctx->interp_dec_v0 - (uint32_t)ticks;
}

static void spui_dec_event(spu_context* ctx)
{
    if (ctx->interp_dec_armed && (ctx->event_mask & SPUI_EV_TM) &&
        (spui_dec(ctx) & 0x80000000u)) {
        ctx->event_status |= SPUI_EV_TM;
        ctx->interp_dec_armed = 0;
    }
}

/* MFC_RdTagStat readiness for the last MFC_WrTagUpdate mode. */
static int spui_tag_ready(spu_context* ctx, uint32_t* done_out)
{
    const uint32_t mask = ctx->mfc_tag_mask;
    const uint32_t done = spu_mfc_tag_completed(ctx) & mask;
    *done_out = done;
    switch (ctx->interp_tag_update) {
    case 1:  return mask == 0 || done != 0;       /* ANY */
    case 2:  return done == mask;                 /* ALL */
    default: return 1;                            /* IMMEDIATE */
    }
}

/* Read channel `ch` into *out. Returns 0 when the read would stall on
 * hardware (nothing to read): the caller parks the SPU at this instruction. */
static int spui_rdch(spu_context* ctx, uint32_t ch, u128* out)
{
    switch (ch) {
    case SPU_RdEventStat: {
        spui_dec_event(ctx);
        const uint32_t v = ctx->event_status & ctx->event_mask;
        if (!v) return 0;
        *out = spu_pref_u32(v);
        return 1; }
    case SPU_RdDec:
        *out = spu_pref_u32(spui_dec(ctx));
        return 1;
    case SPU_RdMachStat:                          /* bit 0: interrupts enabled */
        *out = spu_pref_u32(ctx->interp_ie ? 1u : 0u);
        return 1;
    case MFC_RdTagStat: {
        uint32_t done;
        if (!spui_tag_ready(ctx, &done)) return 0;
        ctx->mfc_tag_status = done;
        *out = spu_pref_u32(done);
        return 1; }
    case MFC_RdListStallStat:
    case SPU_RdInMbox:
    case SPU_RdSigNotify1:
    case SPU_RdSigNotify2:
        if (!spu_rchcnt(ctx, ch)) return 0;
        *out = spu_rdch(ctx, ch);
        return 1;
    default:
        *out = spu_rdch(ctx, ch);
        return 1;
    }
}

static uint32_t spui_rchcnt(spu_context* ctx, uint32_t ch)
{
    switch (ch) {
    case SPU_RdEventStat:
        spui_dec_event(ctx);
        return (ctx->event_status & ctx->event_mask) ? 1u : 0u;
    case MFC_RdTagStat: {
        uint32_t done;
        return (uint32_t)spui_tag_ready(ctx, &done); }
    default:
        return spu_rchcnt(ctx, ch);
    }
}

static void spui_wrch(spu_context* ctx, uint32_t ch, u128 v)
{
    switch (ch) {
    case SPU_WrDec:
        ctx->interp_dec_v0 = v._u32[0];
        ctx->interp_dec_t0_ns = spui_now_ns();
        ctx->interp_dec_armed = (v._u32[0] & 0x80000000u) == 0;
        break;
    case MFC_WrTagUpdate:
        ctx->interp_tag_update = v._u32[0];
        break;
    default:
        break;
    }
    spu_wrch(ctx, ch, v);
}

/* ---- winport v29-scream-native: hooks for the native SCREAM jobs (winport/scream_native_rt.c) ----
 * The lifted job code calls the interpreter's OWN channel semantics (time-based decrementer,
 * blocking RdTagStat / RdInMbox, tag-update mode), so native and interpreted code see the same SPU.
 * g_wp29_enter is set by the hybrid runner's constructor only with PS3_SCREAM_NATIVE=1. */
int (*g_wp29_enter)(spu_context*, uint32_t, uint64_t, uint32_t*, uint64_t*) = 0;
volatile int g_wp29_dirty = 1;   /* job code in LS changed since the last hash (spu_dma.h) */
int      spu_interp_rdch_x(spu_context* ctx, uint32_t ch, u128* out) { return spui_rdch(ctx, ch, out); }
uint32_t spu_interp_rchcnt_x(spu_context* ctx, uint32_t ch) { return spui_rchcnt(ctx, ch); }
void     spu_interp_wrch_x(spu_context* ctx, uint32_t ch, u128 v) { spui_wrch(ctx, ch, v); }
void     spu_interp_dec_event_x(spu_context* ctx) { spui_dec_event(ctx); }

/* Diagnostics (PS3_TRACE_SPU_INTERP, OFF by default): 1 = one line per run end
 * (capped), 2 = also the last control transfers. */
static int spui_trace_level(void)
{
    static int lvl = -1;
    if (lvl < 0) {
        const char* e = getenv("PS3_TRACE_SPU_INTERP");
        lvl = (e && *e) ? atoi(e) : 0;
    }
    return lvl;
}

static void spui_unknown(spu_context* ctx, uint32_t pc, uint32_t insn, unsigned op)
{
    static int shown = 0;
    if (__atomic_fetch_add(&shown, 1, __ATOMIC_RELAXED) < 16)
        fprintf(stderr, "[SPUINTERP] unsupported instruction %s (0x%08X) at LS 0x%05X "
                "(image %d) -- job stopped\n",
                op < SPUI_NUM_OPS ? spui_op_name[op] : "?", insn, pc, ctx->image_id);
}

#define SPUI_RING 32

int spu_interp_run(spu_context* ctx, uint32_t pc, uint64_t max_insns)
{
    const int trace = spui_trace_level();
    uint32_t ring[SPUI_RING];
    unsigned ring_n = 0;
    uint64_t n = 0;
    int result;
    const uint32_t start_pc = pc & 0x3FFFCu;

    /* SPURS kernel entry points the program branches to (kernel context at
     * LS 0x100, as spursKernelDispatchWorkload leaves it): 0x1E0
     * exitToKernelAddr ends the run, 0x1E4 selectWorkloadAddr is emulated. The
     * kernel's own code is not in local store. */
    uint32_t k_exit = 0xFFFFFFFFu, k_select = 0xFFFFFFFFu;
    if (ctx->interp_spurs_pm) {
        k_exit = spui_ls32(ctx, 0x1E0) & 0x3FFFCu;
        k_select = spui_ls32(ctx, 0x1E4) & 0x3FFFCu;
    }

    pc &= 0x3FFFCu;
    ctx->status = SPU_STATUS_RUNNING;
    /* winport v29-scream-native: native SCREAM job code (PS3_SCREAM_NATIVE=1) */
    int (*const wp29)(spu_context*, uint32_t, uint64_t, uint32_t*, uint64_t*) =
        (ctx->interp_spurs_pm && ctx->image_id == 7) ? g_wp29_enter : 0;
#define WP29_TRY() do { uint32_t _np; uint64_t _no; \
        if (wp29(ctx, pc, (max_insns && max_insns > n) ? max_insns - n : 4000000u, &_np, &_no)) { \
            pc = _np & 0x3FFFCu; n += _no * 16u; } } while (0)
    if (wp29 && pc >= 0x4000u && pc < 0x10000u && !(ctx->interp_ie && (ctx->event_status & ctx->event_mask)))
        WP29_TRY();

    /* A plain block, not do/while(0): the `continue` must reach the for loop. */
#define SPUI_JUMP(t) { \
        pc = (uint32_t)(t) & 0x3FFFCu; \
        ring[ring_n++ % SPUI_RING] = pc; \
        if (__builtin_expect(g_wp28_cov, 0)) g_wp28_tgt_bits[pc >> 5] |= (unsigned char)(1u << ((pc >> 2) & 7u)); \
        if (wp29 && pc >= 0x4000u && pc < 0x10000u) WP29_TRY();   /* winport v29-scream-native */ \
        continue; }
    /* E and D flags of bi/bisl/biz/.../iret (RR "rb" field bits 0x10 / 0x20). */
#define SPUI_SET_IE() do { \
        const int _e = (insn >> 18) & 1, _d = (insn >> 19) & 1; \
        if (_e && !_d) ctx->interp_ie = 1; \
        else if (_d && !_e) ctx->interp_ie = 0; } while (0)

    for (;;) {
        /* Interrupt: taken between instructions, SRR0 = the next one. */
        if (ctx->interp_ie) {
            if ((ctx->event_mask & SPUI_EV_TM) && (n & 1023u) == 0)
                spui_dec_event(ctx);
            if (ctx->event_status & ctx->event_mask) {
                ctx->srr0 = pc;
                ctx->interp_ie = 0;
                pc = 0;
                ring[ring_n++ % SPUI_RING] = 0x80000000u;   /* marks "interrupt" */
            }
        }
        if (pc == k_exit) { result = SPU_INTERP_EXIT; ctx->status = SPU_STATUS_STOPPED; break; }
        if (pc == k_select) {
            /* One workload per kernel here: keep selecting the current one.
             * r3 = { wklCurrentId (LS 0x1DC) << 32 | pollStatus 0 }. */
            ctx->gpr[3] = spu_pref_u32(spui_ls32(ctx, 0x1DC));
            pc = ctx->interp_link & 0x3FFFCu;
            continue;
        }
        if (max_insns && n >= max_insns) { result = SPU_INTERP_BUDGET; break; }
        n++;
        if (__builtin_expect((n & 4095u) == 0u, 0) && g_wp27_pcs) wp27_pc_sample(ctx->image_id, pc);   /* winport v27-audio */

        if (__builtin_expect(g_wp28_cov, 0)) g_wp28_cov_bits[pc >> 5] |= (unsigned char)(1u << ((pc >> 2) & 7u));   /* winport v28-scream */
        const uint8_t* ip = &ctx->ls[pc];
        const uint32_t insn = ((uint32_t)ip[0] << 24) | ((uint32_t)ip[1] << 16) |
                              ((uint32_t)ip[2] << 8) | (uint32_t)ip[3];
        const unsigned op = spui_decode11[insn >> 21];
        ctx->pc = pc;

        if (spui_exec_gen(ctx, op, insn, pc)) { pc = (pc + 4) & 0x3FFFCu; continue; }

        const uint32_t RT = insn & 0x7Fu;
        const uint32_t RA = (insn >> 7) & 0x7Fu;
        const uint32_t RB = (insn >> 14) & 0x7Fu;
        const int32_t  i10 = (int32_t)(insn << 8) >> 22;
        const int32_t  i16 = (int32_t)(insn << 9) >> 16;
        const uint32_t next = (pc + 4) & 0x3FFFCu;
        (void)RB;

        switch (op) {
        /* ---- no-ops ---- */
        case SPUI_nop: case SPUI_lnop: case SPUI_sync: case SPUI_dsync:
        case SPUI_fscrwr: case SPUI_mtspr:
        case SPUI_hbr: case SPUI_hbra: case SPUI_hbrr:
            pc = next;
            continue;

        /* ---- stop ---- */
        case SPUI_stop:
        case SPUI_stopd:
            ctx->stop_code = (op == SPUI_stop) ? (insn & 0x3FFFu) : 0u;
            ctx->status = SPU_STATUS_STOPPED_BY_STOP;
            pc = next;
            result = SPU_INTERP_STOP;
            goto out;

        /* ---- local store ---- */
        case SPUI_lqd:
            ctx->gpr[RT] = spu_ls_read128(ctx, ctx->gpr[RA]._u32[0] + (uint32_t)(i10 << 4));
            pc = next; continue;
        case SPUI_stqd:
            spu_ls_write128(ctx, ctx->gpr[RA]._u32[0] + (uint32_t)(i10 << 4), ctx->gpr[RT]);
            pc = next; continue;
        case SPUI_lqx:
            ctx->gpr[RT] = spu_ls_read128(ctx, ctx->gpr[RA]._u32[0] + ctx->gpr[RB]._u32[0]);
            pc = next; continue;
        case SPUI_stqx:
            spu_ls_write128(ctx, ctx->gpr[RA]._u32[0] + ctx->gpr[RB]._u32[0], ctx->gpr[RT]);
            pc = next; continue;
        case SPUI_lqa:
            ctx->gpr[RT] = spu_ls_read128(ctx, ((insn >> 7) & 0xFFFFu) << 2);
            pc = next; continue;
        case SPUI_stqa:
            spu_ls_write128(ctx, ((insn >> 7) & 0xFFFFu) << 2, ctx->gpr[RT]);
            pc = next; continue;
        case SPUI_lqr:
            ctx->gpr[RT] = spu_ls_read128(ctx, pc + (uint32_t)(i16 << 2));
            pc = next; continue;
        case SPUI_stqr:
            spu_ls_write128(ctx, pc + (uint32_t)(i16 << 2), ctx->gpr[RT]);
            pc = next; continue;

        /* ---- channels (RA field = channel number) ---- */
        case SPUI_rdch: {
            u128 v;
            if (!spui_rdch(ctx, RA, &v)) {
                ctx->status = SPU_STATUS_WAITING_CHANNEL;
                result = SPU_INTERP_WAIT;         /* re-executes this rdch */
                goto out;
            }
            ctx->gpr[RT] = v;
            pc = next; continue; }
        case SPUI_rchcnt:
            ctx->gpr[RT] = spu_pref_u32(spui_rchcnt(ctx, RA));
            pc = next; continue;
        case SPUI_wrch:
            spui_wrch(ctx, RA, ctx->gpr[RT]);
            pc = next; continue;

        /* ---- relative / absolute branches ---- */
        case SPUI_br:
        case SPUI_bra: {
            const uint32_t t = (op == SPUI_br) ? pc + (uint32_t)(i16 << 2) : (uint32_t)(i16 << 2);
            if ((t & 0x3FFFCu) == pc) {           /* `br .`: the SPU halt idiom */
                ctx->status = SPU_STATUS_STOPPED_BY_HALT;
                result = SPU_INTERP_STOP;
                goto out;
            }
            SPUI_JUMP(t); }
        case SPUI_brsl:
        case SPUI_brasl: {
            const uint32_t t = (op == SPUI_brsl) ? pc + (uint32_t)(i16 << 2) : (uint32_t)(i16 << 2);
            ctx->gpr[RT] = spu_link(next);
            ctx->interp_link = next;
            SPUI_JUMP(t); }
        case SPUI_brz:
            if (ctx->gpr[RT]._u32[0] == 0) SPUI_JUMP(pc + (uint32_t)(i16 << 2));
            pc = next; continue;
        case SPUI_brnz:
            if (ctx->gpr[RT]._u32[0] != 0) SPUI_JUMP(pc + (uint32_t)(i16 << 2));
            pc = next; continue;
        /* Preferred halfword = bytes 2-3 of the preferred word = its low 16 bits. */
        case SPUI_brhz:
            if ((ctx->gpr[RT]._u32[0] & 0xFFFFu) == 0) SPUI_JUMP(pc + (uint32_t)(i16 << 2));
            pc = next; continue;
        case SPUI_brhnz:
            if ((ctx->gpr[RT]._u32[0] & 0xFFFFu) != 0) SPUI_JUMP(pc + (uint32_t)(i16 << 2));
            pc = next; continue;

        /* ---- indirect branches ---- */
        case SPUI_bi:
            SPUI_SET_IE();
            SPUI_JUMP(ctx->gpr[RA]._u32[0]);
        case SPUI_bisl: {
            const uint32_t t = ctx->gpr[RA]._u32[0];   /* read before the link write (RT may be RA) */
            ctx->gpr[RT] = spu_link(next);
            ctx->interp_link = next;
            SPUI_SET_IE();
            SPUI_JUMP(t); }
        case SPUI_bisled: {
            const uint32_t t = ctx->gpr[RA]._u32[0];
            ctx->gpr[RT] = spu_link(next);
            ctx->interp_link = next;
            spui_dec_event(ctx);
            if (ctx->event_status & ctx->event_mask) { SPUI_SET_IE(); SPUI_JUMP(t); }
            pc = next; continue; }
        case SPUI_iret:
            SPUI_SET_IE();
            SPUI_JUMP(ctx->srr0);
        case SPUI_biz:
            if (ctx->gpr[RT]._u32[0] == 0) { SPUI_SET_IE(); SPUI_JUMP(ctx->gpr[RA]._u32[0]); }
            pc = next; continue;
        case SPUI_binz:
            if (ctx->gpr[RT]._u32[0] != 0) { SPUI_SET_IE(); SPUI_JUMP(ctx->gpr[RA]._u32[0]); }
            pc = next; continue;
        case SPUI_bihz:
            if ((ctx->gpr[RT]._u32[0] & 0xFFFFu) == 0) { SPUI_SET_IE(); SPUI_JUMP(ctx->gpr[RA]._u32[0]); }
            pc = next; continue;
        case SPUI_bihnz:
            if ((ctx->gpr[RT]._u32[0] & 0xFFFFu) != 0) { SPUI_SET_IE(); SPUI_JUMP(ctx->gpr[RA]._u32[0]); }
            pc = next; continue;

        default:
            spui_unknown(ctx, pc, insn, op);
            ctx->status = SPU_STATUS_STOPPED_BY_HALT;
            result = SPU_INTERP_FAULT;
            goto out;
        }
    }
out:
#undef SPUI_JUMP
#undef WP29_TRY
#undef SPUI_SET_IE
    ctx->pc = pc;
    t_last_count = n;

    if (trace) {
        static unsigned long lines = 0;
        const unsigned long k = __atomic_fetch_add(&lines, 1, __ATOMIC_RELAXED);
        if (k < 400 || (k % 1000) == 0 || (result != SPU_INTERP_BUDGET && result != SPU_INTERP_WAIT && k < 4000)) {
            fprintf(stderr, "[SPUINTERP] #%lu img=%d %s start=0x%05X pc=0x%05X n=%llu ie=%d "
                    "ev=0x%X mask=0x%X srr0=0x%05X stop=0x%X tagmask=0x%X tagdone=0x%X lr=0x%05X\n",
                    k, ctx->image_id, spu_interp_result_name(result), start_pc, pc,
                    (unsigned long long)n, ctx->interp_ie, ctx->event_status, ctx->event_mask,
                    ctx->srr0, ctx->stop_code, ctx->mfc_tag_mask,
                    spu_mfc_tag_completed(ctx), ctx->gpr[0]._u32[0]);
            if (trace >= 2) {
                const unsigned cnt = ring_n < SPUI_RING ? ring_n : SPUI_RING;
                fprintf(stderr, "[SPUINTERP]   last branches:");
                for (unsigned i = 0; i < cnt; i++) {
                    const uint32_t v = ring[(ring_n - cnt + i) % SPUI_RING];
                    if (v == 0x80000000u) fprintf(stderr, " IRQ");
                    else fprintf(stderr, " %05X", v);
                }
                fprintf(stderr, "\n[SPUINTERP]   LS[0x12A0..0x1300]:");
                for (uint32_t o = 0x12A0; o < 0x1300; o += 4)
                    fprintf(stderr, "%s%08X", ((o - 0x12A0) % 16u) ? " " : " | ", spui_ls32(ctx, o));
                fprintf(stderr, "\n");
            }
        }
    }
    return result;
}
