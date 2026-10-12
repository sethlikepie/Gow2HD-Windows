/*
 * ps3recomp - SPU (Synergistic Processing Unit) execution context
 *
 * Models the full architectural state of an SPU:
 *   - 128 x 128-bit general-purpose registers
 *   - 256 KB local store
 *   - Channel state (MFC command queue, mailboxes, signal notification)
 */

#ifndef SPU_CONTEXT_H
#define SPU_CONTEXT_H

#include "../../include/ps3emu/ps3types.h"
#include <stdint.h>
#include <string.h>

/* winport v34-spu-simd: x86 quadword LS access = one pshufb (see engine_fixes_v34_spu_simd.py).
 * -DWINPORT_SPU_SIMD=0 restores the byte loops. */
#ifndef WINPORT_SPU_SIMD
#define WINPORT_SPU_SIMD 1
#endif
#if WINPORT_SPU_SIMD && defined(__x86_64__) && defined(__SSSE3__) && (defined(__clang__) || defined(__GNUC__))
#define SPU_LS128_X86 1
#include <tmmintrin.h>
#endif

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
#else
  #include <time.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Portable 16-byte alignment (MSVC __declspec vs GCC/clang __attribute__). */
#if defined(_MSC_VER)
#  define SPU_ALIGN16 __declspec(align(16))
#else
#  define SPU_ALIGN16 __attribute__((aligned(16)))
#endif

/* Local store size: 256 KB */
#define SPU_LS_SIZE         (256 * 1024)
#define SPU_LS_MASK         (SPU_LS_SIZE - 1)

/* Maximum number of MFC tag groups */
#define SPU_MFC_MAX_TAGS    32

/* Mailbox / signal capacities */
#define SPU_MBOX_DEPTH      1    /* SPU write outbound mailbox depth */
#define SPU_INTR_MBOX_DEPTH 1    /* SPU write inbound interrupt mailbox depth */

/* ---------------------------------------------------------------------------
 * SPU channel IDs
 * -----------------------------------------------------------------------*/
#define SPU_RdEventStat     0
#define SPU_WrEventMask     1
#define SPU_WrEventAck      2
#define SPU_RdSigNotify1    3
#define SPU_RdSigNotify2    4
#define SPU_WrDec           7
#define SPU_RdDec           8
#define SPU_RdEventMask     11
#define SPU_RdMachStat      13
#define SPU_WrSRR0          14
#define SPU_RdSRR0          15
#define SPU_WrOutMbox       28
#define SPU_RdInMbox        29
#define SPU_WrOutIntrMbox   30

/* MFC channels */
#define MFC_WrMSSyncReq     9
#define MFC_RdTagMask       12
#define MFC_LSA             16
#define MFC_EAH             17
#define MFC_EAL             18
#define MFC_Size            19
#define MFC_TagID           20
#define MFC_Cmd             21
#define MFC_WrTagMask       22
#define MFC_WrTagUpdate     23
#define MFC_RdTagStat       24
#define MFC_RdListStallStat 25
#define MFC_WrListStallAck  26
#define MFC_RdAtomicStat    27

/* ---------------------------------------------------------------------------
 * MFC DMA command opcodes
 * -----------------------------------------------------------------------*/
#define MFC_PUT_CMD         0x20
#define MFC_PUTB_CMD        0x21
#define MFC_PUTF_CMD        0x22
#define MFC_GET_CMD         0x40
#define MFC_GETB_CMD        0x41
#define MFC_GETF_CMD        0x42
#define MFC_PUTL_CMD        0x24
#define MFC_PUTLB_CMD       0x25
#define MFC_PUTLF_CMD       0x26
#define MFC_GETL_CMD        0x44
#define MFC_GETLB_CMD       0x45
#define MFC_GETLF_CMD       0x46
#define MFC_SNDSIG_CMD      0xA0
#define MFC_BARRIER_CMD     0xC0
#define MFC_EIEIO_CMD       0xC8
#define MFC_SYNC_CMD        0xCC

/* ---------------------------------------------------------------------------
 * Channel state
 * -----------------------------------------------------------------------*/
typedef struct spu_channel {
    uint32_t value;
    uint32_t count;   /* number of valid entries (0 or 1 for most channels) */
} spu_channel;

/* ---------------------------------------------------------------------------
 * SPU execution context
 * -----------------------------------------------------------------------*/
typedef struct spu_context {
    /* 128 general-purpose 128-bit registers */
    SPU_ALIGN16 u128 gpr[128];

    /* 256 KB local store, 16-byte aligned */
    SPU_ALIGN16 uint8_t ls[SPU_LS_SIZE];

    /* Program counter (local store address, 0-0x3FFFF) */
    uint32_t pc;

    /* SPU status (running, stopped, etc.) */
    uint32_t status;
    #define SPU_STATUS_STOPPED      0x0
    #define SPU_STATUS_RUNNING      0x1
    #define SPU_STATUS_STOPPED_BY_STOP  0x2
    #define SPU_STATUS_STOPPED_BY_HALT  0x4
    #define SPU_STATUS_WAITING_CHANNEL  0x8
    #define SPU_STATUS_SINGLE_STEP  0x10

    /* 14-bit code from the SPU `stop` instruction (STOP-and-signal). The host
     * reads it to learn WHY the SPU stopped — SPURS uses specific stop codes to
     * signal job/kernel completion and dispatch decisions. Written by lifted
     * code (spu_lifter.py stop path); the field is required for that generated
     * code to compile. */
    uint32_t stop_code;

    /* SPU thread identification */
    uint32_t spu_id;
    uint32_t spu_group_id;

    /* Active recompiled image for per-context indirect-branch dispatch. SPURS
     * loads kernel/policy/job into overlapping LS addresses at different times,
     * so spu_indirect_branch resolves pc within the image currently selected
     * here. 0 = match any image (back-compat for single-image contexts). */
    int image_id;

#ifdef SPU_PC_TRACE_HOOKS
    /* PS3_TRACE_SPU_PC=1 (diagnostico, OFF por default): anel com os ultimos
     * enderecos de local store por onde o job passou. Vive no contexto (e nao
     * num global) porque quem escreve e' o codigo liftado e quem le' e' o DMA,
     * que estao em unidades de compilacao diferentes. Serve para saber que
     * codigo produziu os dados que um PUT acabou de enviar. */
    int      trace_pc;
    uint32_t pc_ring[32];
    uint32_t pc_ring_n;
    /* Primeiras escritas em local store de um float na gama dos raios
     * inflados: onde o valor APARECE, antes de ser copiado para o buffer que
     * o DMA envia. Impresso pelo PUT, junto com o caminho. */
    uint32_t huge_pc[8], huge_lsa[8], huge_n;
    float    huge_v[8];
    /* Instantaneo do banco de registadores (palavra preferida como float) na
     * PRIMEIRA escrita inflada: mostra o valor certo ao lado do errado. */
    float    huge_regs[128];
    int      huge_regs_taken;
#endif

    /* Decrementer (a free-running down counter) */
    uint32_t decrementer;

    /* SRR0 - Save/Restore Register (exception return address) */
    uint32_t srr0;

    /* Event status / mask */
    uint32_t event_status;
    uint32_t event_mask;

    /* SPU interrupt emulation (spu_irq_window, spu_channels.c): set while the
     * handler at LS 0 runs, and a count of consecutive open windows with no
     * pending event (a poll loop with nothing to wait for ends the job). */
    int      in_irq;
    uint32_t irq_spins;

    /* Shadow of the return addresses of the lifted brsl calls in progress
     * (spu_call_enter/leave): a `bi $rX` to the innermost one is a return. */
    uint32_t call_ret[64];
    int      call_depth;

    /* Channels */
    spu_channel ch_out_mbox;        /* SPU -> PPU outbound mailbox */
    spu_channel ch_in_mbox;         /* PPU -> SPU inbound mailbox */
    spu_channel ch_out_intr_mbox;   /* SPU -> PPU interrupt mailbox */
    spu_channel ch_sig_notify[2];   /* Signal notification 1 & 2 */

    /* MFC command staging registers (written via channels before issuing cmd) */
    uint32_t mfc_lsa;
    uint32_t mfc_eah;
    uint32_t mfc_eal;
    uint32_t mfc_size;
    uint32_t mfc_tag;

    /* MFC tag completion mask and status */
    uint32_t mfc_tag_mask;
    uint32_t mfc_tag_status;

    /* Job-exit escape hatch: when non-NULL, points at a jmp_buf owned by the
     * job runner (spu_run_lifted_job); spu_indirect_branch longjmps here on a
     * branch-to-0 (task main returned with the context's default LR) or an
     * unresolvable target, instead of letting lifted code run past the end. */
    void* exit_jmp;

    /* Idle accounting for the SPURS taskset relinquish contract (implemented
     * in spu_taskset_syscall_hle, spu_channels.c). Monotonic ns timestamp of
     * the last time this job MOVED DATA through the MFC; 0 = not armed yet.
     * A SPURS task that finds its queue empty spins "try-pop / cellSpursPoll"
     * forever by design -- on hardware the SPURS kernel preempts it and takes
     * the SPU back. Our "SPU" is a pooled host worker thread, so the poll
     * syscall uses this timestamp to answer "has the kernel got other work for
     * this SPU?" truthfully. Zeroed by spu_context_init, i.e. re-armed for
     * every job, which is why it lives in the context and not in a TLS slot. */
    uint64_t task_idle_ns;
    /* E469: o caminho de POLL le' o relogio; amortizado por estes dois campos.
     * No contexto e nao em TLS para nao vazar o valor entre contextos que
     * partilhem a thread. Zerados pelo memset de spu_context_init. */
    unsigned idle_poll_n;
    unsigned idle_last_ms;

    /* SPU interpreter state (runtime/spu/spu_interp.c). Lifted code never
     * touches these; kept at the END of the struct so every field above keeps
     * its offset. Zeroed by spu_context_init. */
    int      interp_ie;          /* interrupts enabled (bie/bid/iret E/D) */
    int      interp_spurs_pm;    /* 1: SPURS kernel ctx at LS 0x100 (exit/select hooks) */
    uint32_t interp_tag_update;  /* last MFC_WrTagUpdate mode (RdTagStat blocking) */
    uint32_t interp_link;        /* return address of the last brsl/brasl/bisl */
    uint32_t interp_dec_v0;      /* decrementer value at interp_dec_t0_ns */
    int      interp_dec_armed;   /* TM event fires on the next 0 -> -1 transition */
    uint64_t interp_dec_t0_ns;   /* host time of the last SPU_WrDec (0 = not started) */

} spu_context;

/* ---------------------------------------------------------------------------
 * Inter-function control-flow trampoline (mirrors the PPU lifter).
 *
 * The SPU lifter turns an inter-function branch (br/brsl/conditional to another
 * function, or fall-through into the next function) into a tail dispatch. If it
 * emitted a direct `spu_func_X(ctx); return;` there, a control-flow LOOP that
 * spans two functions (funcA -> funcB -> funcA ...) would become unbounded C
 * recursion and overflow the host stack (observed: the spu1 MPEG decoder
 * SIGSEGV'd before any DMA). Instead the lifter sets g_spu_trampoline_fn and
 * returns; the nearest enclosing call site (or the job runner) drains the
 * trampoline in a loop, so a cross-function loop runs in O(1) stack. A real
 * call (brsl/bisl) still uses a direct C call, then drains any trampoline the
 * callee left before continuing -- exactly the PPU DRAIN_TRAMPOLINE pattern. */
/* MUST be a working TLS qualifier: with the multi-worker SPURS kernel,
 * several SPU jobs run concurrently and a shared trampoline slot produces
 * NULL-call crashes. GCC (MinGW included) IGNORES __declspec(thread) with a
 * warning -- use __thread for GCC everywhere, __declspec only for MSVC. */
#if defined(_MSC_VER)
#define SPU_THREAD_LOCAL __declspec(thread)
#else
#define SPU_THREAD_LOCAL __thread
#endif
extern SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*);
#ifdef SPU_PC_TRACE_HOOKS
#define SPU_TRACE_STEP(ctx) do { \
    if ((ctx)->trace_pc) (ctx)->pc_ring[(ctx)->pc_ring_n++ & 31u] = (ctx)->pc; \
    } while (0)
#else
#define SPU_TRACE_STEP(ctx) do { } while (0)
#endif
/* Interrupt window of a lifted `bie`/`bisle` (spu_channels.c). Declared here,
 * not only in each lift's generated header, so lifts whose header predates it
 * still build when they import newer functions. */
void spu_irq_window(spu_context* ctx);

static inline void spu_call_enter(spu_context* ctx, uint32_t ret)
{
    if (ctx->call_depth < 64) ctx->call_ret[ctx->call_depth] = ret;
    ctx->call_depth++;
}
static inline void spu_call_leave(spu_context* ctx)
{
    if (ctx->call_depth > 0) ctx->call_depth--;
}

#define SPU_DRAIN(ctx) do { \
    while (g_spu_trampoline_fn) { \
        void (*_tf)(spu_context*) = g_spu_trampoline_fn; \
        g_spu_trampoline_fn = 0; \
        SPU_TRACE_STEP(ctx); \
        _tf(ctx); \
    } } while (0)

/* ---------------------------------------------------------------------------
 * Initialization
 * -----------------------------------------------------------------------*/
static inline void spu_context_init(spu_context* ctx, uint32_t spu_id)
{
    memset(ctx, 0, sizeof(*ctx));
#ifdef SPU_PC_TRACE_HOOKS
    {   /* ver trace_pc no struct */
        static int on = -1;
        if (on < 0) {
            extern char* getenv(const char*);
            const char* e = getenv("PS3_TRACE_SPU_PC");
            on = (e && e[0] && e[0] != '0') ? 1 : 0;
        }
        ctx->trace_pc = on;
    }
#endif
    ctx->spu_id = spu_id;
    ctx->status = SPU_STATUS_STOPPED;
}

/* ---------------------------------------------------------------------------
 * Local store access helpers
 * -----------------------------------------------------------------------*/
static inline uint8_t* spu_ls_ptr(spu_context* ctx, uint32_t lsa)
{
    return &ctx->ls[lsa & SPU_LS_MASK];
}

static inline uint32_t spu_ls_read32(const spu_context* ctx, uint32_t lsa)
{
    lsa &= SPU_LS_MASK;
    const uint8_t* p = &ctx->ls[lsa];
    /* SPU local store is big-endian */
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  | (uint32_t)p[3];
}

static inline void spu_ls_write32(spu_context* ctx, uint32_t lsa, uint32_t val)
{
    lsa &= SPU_LS_MASK;
    uint8_t* p = &ctx->ls[lsa];
    p[0] = (uint8_t)(val >> 24);
    p[1] = (uint8_t)(val >> 16);
    p[2] = (uint8_t)(val >> 8);
    p[3] = (uint8_t)(val);
}

/* Local-store quadword access.
 *
 * LS bytes are big-endian (the SPU's native order); registers in our
 * model are stored in native (little-endian on x86) order so that
 * `_u32[i]` directly gives lane i's value and `spu_preferred_u32(r)`
 * == `r._u32[0]`. The byte-swap below converts between the two on
 * every quadword load/store, mirroring what `spu_ls_read32` already
 * does explicitly. Doing the swap here (rather than in every channel
 * extractor) keeps the per-word `_u32[i]` semantics that the lifter
 * helpers in `spu_helpers.h` were written against. */
/* The two hottest helpers by call site in the GoW2 SPU image (2 120 loads /
 * 1 146 stores): on AArch64 little-endian the byte swap below is exactly
 * "reverse the bytes inside each 32-bit lane", i.e. one instruction. */
#if defined(__aarch64__) && defined(__ARM_NEON) && \
    defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define SPU_LS128_NEON 1
#include <arm_neon.h>
#endif

static inline u128 spu_ls_read128(const spu_context* ctx, uint32_t lsa)
{
    u128 v;
    lsa &= SPU_LS_MASK & ~0xFu;
    const uint8_t* p = &ctx->ls[lsa];
#ifdef SPU_LS128_NEON
    vst1q_u8((uint8_t*)&v, vrev32q_u8(vld1q_u8(p)));
#elif defined(SPU_LS128_X86)   /* winport v34-spu-simd */
    { const __m128i x = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)p), _mm_setr_epi8(3,2,1,0, 7,6,5,4, 11,10,9,8, 15,14,13,12));
      memcpy(&v, &x, 16); }
#else
    for (int i = 0; i < 4; i++) {
        v._u32[i] = ((uint32_t)p[i*4]     << 24) |
                    ((uint32_t)p[i*4 + 1] << 16) |
                    ((uint32_t)p[i*4 + 2] <<  8) |
                    (uint32_t)p[i*4 + 3];
    }
#endif
    return v;
}

static inline void spu_ls_write128(spu_context* ctx, uint32_t lsa, u128 val)
{
    lsa &= SPU_LS_MASK & ~0xFu;
#ifdef SPU_PC_TRACE_HOOKS
    if (ctx->trace_pc && ctx->huge_n < 8u) {
        for (int _i = 0; _i < 4; _i++) {
            const uint32_t w = val._u32[_i];
            const uint32_t e = (w >> 23) & 0xFFu;
            if (e < 0x8Cu || e > 0x8Eu) continue;   /* |v| fora de [8k, 64k) */
            float f; memcpy(&f, &w, sizeof f);
            ctx->huge_pc[ctx->huge_n] = ctx->pc;
            ctx->huge_lsa[ctx->huge_n] = lsa;
            ctx->huge_v[ctx->huge_n] = f;
            ctx->huge_n++;
            if (!ctx->huge_regs_taken) {
                ctx->huge_regs_taken = 1;
                for (int _r = 0; _r < 128; _r++) {
                    const uint32_t rw = ctx->gpr[_r]._u32[0];
                    memcpy(&ctx->huge_regs[_r], &rw, sizeof(float));
                }
            }
            break;
        }
    }
#endif
    uint8_t* p = &ctx->ls[lsa];
#ifdef SPU_LS128_NEON
    vst1q_u8(p, vrev32q_u8(vld1q_u8((const uint8_t*)&val)));
#elif defined(SPU_LS128_X86)   /* winport v34-spu-simd */
    { __m128i x; memcpy(&x, &val, 16);
      _mm_storeu_si128((__m128i*)p, _mm_shuffle_epi8(x, _mm_setr_epi8(3,2,1,0, 7,6,5,4, 11,10,9,8, 15,14,13,12))); }
#else
    for (int i = 0; i < 4; i++) {
        uint32_t w = val._u32[i];
        p[i*4]     = (uint8_t)(w >> 24);
        p[i*4 + 1] = (uint8_t)(w >> 16);
        p[i*4 + 2] = (uint8_t)(w >>  8);
        p[i*4 + 3] = (uint8_t)w;
    }
#endif
}

/* ---------------------------------------------------------------------------
 * Preferred slot extraction
 *
 * SPU instructions operate on the "preferred slot" of a 128-bit register,
 * which is the leftmost (highest-address in big-endian) element.
 * For word operations, preferred slot = element 0 of the _u32 array
 * (since our u128 stores big-endian element order).
 * -----------------------------------------------------------------------*/
static inline uint32_t spu_preferred_u32(const u128* reg)
{
    return reg->_u32[0];
}

static inline int32_t spu_preferred_s32(const u128* reg)
{
    return reg->_s32[0];
}

static inline float spu_preferred_f32(const u128* reg)
{
    return reg->_f32[0];
}

static inline uint64_t spu_preferred_u64(const u128* reg)
{
    return reg->_u64[0];
}

/* Create a register with a value splatted to the preferred word slot */
static inline u128 spu_make_preferred_u32(uint32_t val)
{
    u128 r;
    memset(&r, 0, sizeof(r));
    r._u32[0] = val;
    return r;
}

/* ---------------------------------------------------------------------------
 * Channel read/write helpers
 * -----------------------------------------------------------------------*/

/* Acquire-load / release-store of the channel `count` word.
 *
 * These exist so the cross-thread bounded blocking read further down
 * (spu_channel_read_blocking) can hand a value off from a producer thread to a
 * consumer thread with a real happens-before edge. A plain `volatile` read --
 * what an earlier version of this file relied on -- forces the compiler to
 * re-issue the load but establishes NO inter-thread ordering: on a weak-memory
 * target such as arm64 a consumer could observe `count > 0` yet still read a
 * stale `value` written before the producer's store became visible. Pairing a
 * release-store of `count` in the producer with an acquire-load of `count` in
 * the consumer closes that hole: once the acquire-load observes the value the
 * release-store published, every store the producer sequenced BEFORE it (here,
 * the `value` write) is guaranteed visible to the consumer. `value` itself
 * stays a plain word -- a naturally-aligned 32-bit load/store never tears, and
 * the count acquire/release orders it -- so the struct layout is unchanged and
 * every single-threaded call site (spu_channels.c) keeps compiling untouched.
 *
 * Kept as free helpers over a plain `uint32_t` (rather than making the struct
 * member `_Atomic`) on purpose: spu_context.h is included from C++ too
 * (runtime/ppu/ppu_loader.cpp), and a `_Atomic`-qualified member does not parse
 * under C++ pre-C++23. The GCC/Clang `__atomic_*` builtins below are valid in
 * both C and C++ and operate on ordinary objects; the MSVC branch mirrors the
 * existing `_MSC_VER` intrinsic pattern used elsewhere in this tree
 * (spu_helpers.h). */
#if defined(_MSC_VER)
#  include <intrin.h>
static inline uint32_t spu__count_load_acquire(const uint32_t* p) {
    /* _InterlockedOr(...,0): full-barrier read returning the current value. */
    return (uint32_t)_InterlockedOr((volatile long*)p, 0);
}
static inline void spu__count_store_release(uint32_t* p, uint32_t v) {
    (void)_InterlockedExchange((volatile long*)p, (long)v);
}
#else
static inline uint32_t spu__count_load_acquire(const uint32_t* p) {
    return __atomic_load_n((const uint32_t*)p, __ATOMIC_ACQUIRE);
}
static inline void spu__count_store_release(uint32_t* p, uint32_t v) {
    __atomic_store_n(p, v, __ATOMIC_RELEASE);
}
#endif

static inline void spu_channel_write(spu_channel* ch, uint32_t val)
{
    ch->value = val;
    /* Release-store the count last: an acquire-load in
     * spu_channel_read_blocking() that observes count>0 is then guaranteed to
     * also see the value store above. For the single-threaded callers this is
     * an ordinary store plus a release barrier -- identical observable result,
     * negligible cost (stlr on arm64, a plain store on x86). */
    spu__count_store_release(&ch->count, 1u);
}

static inline uint32_t spu_channel_read(spu_channel* ch)
{
    uint32_t val = ch->value;
    ch->count = 0;
    return val;
}

static inline int spu_channel_has_data(const spu_channel* ch)
{
    return ch->count > 0;
}

/* Acquire-ordered variant of spu_channel_has_data(): the poll a cross-thread
 * consumer (or a producer waiting for its slot to drain) must use so a
 * subsequent access is correctly ordered against the peer's release-store. The
 * plain spu_channel_has_data() above stays for the single-threaded call sites,
 * which never race and don't want the barrier. */
static inline int spu_channel_has_data_acquire(const spu_channel* ch)
{
    return spu__count_load_acquire(&ch->count) > 0;
}

/* ---------------------------------------------------------------------------
 * Bounded-poll channel read -- ADDITIVE, currently UNUSED primitive.
 *
 * !!! DO NOT WIRE THIS INTO spu_rdch()/spu_rchcnt()/spu_channel_read() !!!
 * -----------------------------------------------------------------------
 * This is NOT a faithful model of a hardware RDCH stall and must never be
 * dropped in as one. Two hard blockers, both unresolved in this environment:
 *
 *   (1) TIMEOUT-SEMANTICS MISMATCH (ISA correctness). Real Cell SPU hardware
 *       stalls the pipeline INDEFINITELY on an RDCH from an empty
 *       mailbox/signal-notify channel (SPU_RdInMbox, SPU_RdSigNotify1/2, ...)
 *       until a producer writes -- there is NO timeout and NO failure return
 *       in the ISA; the read simply cannot complete early. This function, by
 *       deliberate contrast, is BOUNDED: it gives up after timeout_ms and
 *       returns 0. Wiring that "gave up" path into spu_rdch would fabricate a
 *       channel completion the hardware never produces, feeding the lifted SPU
 *       program a garbage/zero value and desyncing it. The bounded timeout
 *       exists only so an isolated unit test can prove the loop terminates
 *       (see runtime/spu/tests/test_spu_channel_blocking_main.c); it is a
 *       test affordance, not RDCH semantics.
 *
 *   (2) CROSS-DOMAIN DEADLOCK RISK (documented, unresolved). SPU worker
 *       threads here are decoupled from the PPU-side "giant lock"
 *       (runtime/ppu/ppu_loader.cpp), but cellSpursSendSignal and related
 *       PPU-side HLE ARE serialized by that lock. A real blocking RDCH could
 *       deadlock: an SPU worker blocked waiting for a write whose producer
 *       needs PPU-giant-lock progress, while that PPU thread is itself blocked
 *       on this SPU job. Ruling that out needs a real in-boot trace of the
 *       live task graph, which is not available here.
 *
 * Because of BOTH, this primitive is intentionally not called from any
 * existing call site and is validated standalone only. Solving (1) (true
 * unbounded stall integrated with the SPU scheduler) AND (2) (deadlock proof
 * from a live trace) is a prerequisite for any wiring -- see the PR discussion.
 *
 * What it DOES do, correctly, in isolation: bounded poll loop (acquire-load ->
 * short sleep -> re-check -> compare against a deadline). timeout_ms caps the
 * total wait; timeout_ms == 0 polls exactly once and returns immediately.
 * Returns 1 and stores the channel's value (clearing `count`, mirroring
 * spu_channel_read()) into *out_value if data arrived before the deadline;
 * returns 0 (leaving *out_value untouched) if timeout_ms elapsed first.
 *
 * Cross-thread correctness: `count` is read with spu__count_load_acquire()
 * every iteration and, on success, cleared with spu__count_store_release().
 * Paired with the release-store in spu_channel_write(), this gives a real
 * happens-before edge (producer's `value` store -> consumer's `value` load)
 * on weak-memory targets -- the earlier `volatile`-only read did not, and could
 * hand back a stale value. The acquire-load is also a genuine memory operation,
 * so it doubles as the re-read barrier the old `volatile` provided (an
 * optimizer cannot hoist it out of the loop). The `spu_channel` struct is still
 * a pair of plain words and is used non-atomically by the single-threaded call
 * sites elsewhere; only these paired acquire/release helpers touch it when a
 * second thread is in play.
 */
static inline int spu_channel_read_blocking(spu_channel* ch, uint32_t timeout_ms,
                                             uint32_t* out_value)
{
#ifdef _WIN32
    LARGE_INTEGER freq, start, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start);
#else
    struct timespec start, now;
    clock_gettime(CLOCK_MONOTONIC, &start);
#endif

    for (;;) {
        if (spu__count_load_acquire(&ch->count) > 0) {
            *out_value = ch->value;                 /* ordered after the acquire */
            spu__count_store_release(&ch->count, 0u);
            return 1;
        }

        double elapsed_ms;
#ifdef _WIN32
        QueryPerformanceCounter(&now);
        elapsed_ms = (double)(now.QuadPart - start.QuadPart) * 1000.0 /
                     (double)freq.QuadPart;
#else
        clock_gettime(CLOCK_MONOTONIC, &now);
        elapsed_ms = (double)(now.tv_sec - start.tv_sec) * 1000.0 +
                     (double)(now.tv_nsec - start.tv_nsec) / 1.0e6;
#endif
        if (elapsed_ms >= (double)timeout_ms)
            return 0;   /* deadline reached -- bounded timeout, never infinite */

        /* Short sleep before re-polling (bounded-poll shape). */
#ifdef _WIN32
        Sleep(1);
#else
        {
            struct timespec req;
            req.tv_sec = 0;
            req.tv_nsec = 1000000L;   /* 1 ms */
            nanosleep(&req, NULL);
        }
#endif
    }
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SPU_CONTEXT_H */
