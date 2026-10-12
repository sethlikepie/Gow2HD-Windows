/*
 * ps3recomp - SPU DMA engine (MFC)
 *
 * The Memory Flow Controller (MFC) handles DMA transfers between an SPU's
 * 256 KB local store and the main memory (EA space).  Each SPU has a 16-entry
 * MFC command queue.
 *
 * Supported operations:
 *   - DMA get/put  (local store <-> main memory)
 *   - DMA list commands (scatter/gather)
 *   - Tag group management and synchronization
 *   - Barrier and fence
 */

#ifndef SPU_DMA_H
#define SPU_DMA_H

#include "spu_context.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdio.h>

/* ---------------------------------------------------------------------------
 * Task 4 (bringup-instrumentacao-fase0): subsistema DMA -- GET/PUT/LIST/
 * TAG_WAIT/DONE do motor MFC.
 *
 * This header is `static inline` and is #include'd directly by ~8 isolated
 * SPU unit tests (runtime/spu/tests/test_*_main.c) that build tiny binaries
 * WITHOUT linking runtime/trace/ps3_trace.c or runtime/syscalls/sys_timer.c
 * (ps3_trace_emit()'s ps3_timebase_now() dependency). Calling PS3_TRACE()
 * unconditionally here would force every one of those binaries to resolve
 * ps3_trace_on/ps3_trace_emit at link time even though they never asked for
 * DMA tracing -- new build failures in tests this task must not touch.
 *
 * Gate: only the ONE real production consumer of this header
 * (runtime/spu/spu_channels.c) #defines SPU_DMA_TRACE_HOOKS before including
 * spu_dma.h, and the dedicated offline test
 * (runtime/spu/tests/test_dma_trace_main.c) does the same + links ps3_trace.c
 * + a ps3_timebase_now() stub. Every other include site leaves the macro
 * undefined, so the code below compiles to nothing there -- zero new symbol
 * references, zero risk to the existing SPU suite (runtime/spu/tests/
 * run_tests.sh). PS3_TRACE_DMA (the runtime env var) still gates emission on
 * top of this compile-time gate.
 *
 * Post-review fix (Task 4 Minor #5): this compile-time gate used to be named
 * PS3_TRACE_DMA_HOOKS, squatting the PS3_TRACE_* ENV VAR namespace (a user
 * exporting PS3_TRACE_DMA_HOOKS=1 expecting a trace gate would get nothing --
 * this is a -D compiler macro, not a getenv()). Renamed to SPU_DMA_TRACE_HOOKS
 * to stop the collision; no behavioural change, same compile-time-only gate.
 * -----------------------------------------------------------------------*/
#ifdef SPU_DMA_TRACE_HOOKS
#include "ps3_trace.h"

/* PS3_WATCH_STORE: watch de escritas HOST em bloco (ppu_loader.cpp).
 * Um PUT de SPU escreve directo em vm_base e nao passa por vm_write*. */
#ifdef __cplusplus
extern "C"
#endif
void ps3_watch_store_bulk(uint32_t ea, uint32_t len, const char* who);
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* MFC queue depth */
#define MFC_QUEUE_DEPTH     16

/* Maximum DMA transfer size per element */
#define MFC_MAX_DMA_SIZE    (16 * 1024)

/* ---------------------------------------------------------------------------
 * MFC command entry
 * -----------------------------------------------------------------------*/
typedef struct mfc_cmd {
    uint32_t lsa;       /* Local store address */
    uint64_t ea;        /* Effective (main memory) address: EAH << 32 | EAL */
    uint32_t size;      /* Transfer size in bytes */
    uint32_t tag;       /* Tag group ID (0-31) */
    uint32_t cmd;       /* Command opcode (MFC_PUT_CMD, MFC_GET_CMD, etc.) */

    /* Status */
    int      active;    /* 1 = pending, 0 = completed/free */
} mfc_cmd;

/* DMA list element (used by PUTL/GETL) */
typedef struct mfc_list_element {
    /* In PS3 memory these are big-endian:
     *   upper halfword: 0x8000 = stall-and-notify (bit 31 of the word)
     *   lower halfword: transfer size (15 bits)
     *   bits 16-31 : transfer size (low 16 bits)
     *   bits 32-63 : effective address low 32 bits
     */
    uint32_t size_and_flags;
    uint32_t eal;
} mfc_list_element;

/* ---------------------------------------------------------------------------
 * MFC DMA engine state
 * -----------------------------------------------------------------------*/
typedef struct mfc_engine {
    mfc_cmd     queue[MFC_QUEUE_DEPTH];
    uint32_t    queue_count;

    /* Per-tag completion tracking (bitmask: bit N = tag N completed) */
    uint32_t    tag_completed;

    /* DMA-list stall-and-notify: a list element with bit 15 set stops the
     * list after its transfer; its tag reads back in MFC_RdListStallStat and
     * raises the Sn event (0x2) until MFC_WrListStallAck resumes the list. */
    uint32_t    stall_stat;
    struct { int active; uint32_t list_lsa, next, count, base_cmd, lsa_cursor; uint64_t ea_base; }
                stall[32];
} mfc_engine;

/* External: pointer to host mapping of PS3 main memory.
 * Must be set by the VM manager before any DMA. */
extern uint8_t* vm_base;

/* ---------------------------------------------------------------------------
 * Initialization
 * -----------------------------------------------------------------------*/
static inline void mfc_engine_init(mfc_engine* mfc)
{
    memset(mfc, 0, sizeof(*mfc));
    mfc->tag_completed = 0xFFFFFFFF; /* all tags idle = completed */
}

/* ---------------------------------------------------------------------------
 * Core DMA transfer (synchronous for recompiled code)
 *
 * In real hardware this would be asynchronous.  For a static recompiler the
 * DMA is executed immediately; the tag status is updated after completion.
 * -----------------------------------------------------------------------*/

static inline int mfc_is_get(uint32_t cmd)
{
    return (cmd & 0x40) != 0; /* GET family has bit 6 set */
}

static inline int mfc_is_put(uint32_t cmd)
{
    return (cmd & 0x20) != 0 && !mfc_is_get(cmd);
}

static inline int mfc_is_list(uint32_t cmd)
{
    return (cmd & 0x04) != 0; /* list variants have bit 2 set */
}

static inline int mfc_is_barrier(uint32_t cmd)
{
    return (cmd & 0x01) != 0; /* barrier variants have bit 0 set */
}

static inline int mfc_is_fence(uint32_t cmd)
{
    return (cmd & 0x02) != 0; /* fence variants have bit 1 set */
}

/*
 * Execute a single DMA transfer between local store and main memory.
 * Returns 0 on success, -1 on error.
 */
static inline int mfc_do_transfer(spu_context* spu, uint32_t lsa, uint64_t ea,
                                   uint32_t size, uint32_t cmd)
{
    /* Validate size */
    if (size == 0 || size > MFC_MAX_DMA_SIZE)
        return -1;

    /* Mask LSA to local store range */
    lsa &= SPU_LS_MASK;

    /* Bounds-check the effective address against the committed guest VM before
     * the memcpy. A decoder job descriptor with an uninitialized/garbage EA
     * (or one just past a committed region) otherwise faults inside memcpy and
     * takes the whole process down (observed: spu1 MPEG decoder SIGSEGV). Turn
     * it into a recoverable no-op + one-shot log instead. */
    { extern uint32_t ppu_vm_size;
      extern int ppu_guest_range_committed(uint32_t addr, uint32_t n);
      uint32_t e = (uint32_t)ea;
      if (ppu_vm_size && ((uint64_t)e + size > ppu_vm_size || e < 0x10000u)) {
          /* O cap estava no PRINT e o bloco faz `return -1`: a partir da 20a,
           * DMAs fora de alcance eram descartadas SEM UMA LINHA no log. Mesmo
           * defeito dos disjuntores ICALL (ledger E63) -- primeiras 16 mais
           * cada potencia de 2, para a magnitude aparecer mesmo sob kill. */
          static unsigned long n = 0; ++n;
          if (n <= 16 || (n & (n - 1)) == 0)
              fprintf(stderr, "[spu-dma] SKIP #%lu out-of-range %s ea=0x%08X size=%u lsa=0x%05X pc=0x%05X lr=0x%05X\n",
                      n, mfc_is_get(cmd) ? "GET" : "PUT", e, size, lsa,
                      spu->pc & SPU_LS_MASK, spu->gpr[0]._u32[0] & SPU_LS_MASK);
          return -1;
      }
      /* In-range but uncommitted (e.g. a SPURS error code like 0x80410913
       * used as an EA by unchecked task-lib code): memcpy would AV. */
      if (!ppu_guest_range_committed(e, size)) {
          static int n2 = 0; if (n2++ < 20)
              fprintf(stderr, "[spu-dma] SKIP uncommitted %s ea=0x%08X size=%u lsa=0x%05X\n",
                      mfc_is_get(cmd) ? "GET" : "PUT", e, size, lsa);
          return -1;
      } }

    uint8_t* ls_ptr = &spu->ls[lsa];
    uint8_t* ea_ptr = vm_base + (uint32_t)ea; /* PS3 uses 32-bit effective addresses for SPU DMA */

    /* PS3_TRACE_SPUDMA: log the first transfers (what does a task read before
     * deciding it has no work?). */
    /* PS3_TRACE_SPUDMA=1: uma linha por transferencia (cap PS3_TRACE_SPUDMA_CAP, default 6000).
     * PS3_TRACE_SPUDMA_EA=lo-hi (hex): so' transferencias cujo EA cai na janela -- E163 (2026-09-02):
     * sem janela, 6000 linhas acabam antes de o mixer do Scream ser sequer despachado. */
    /* PS3_TRACE_SPUDMA_IMG=N: only transfers of SPU image N (7 = SCREAM mixer). */
    { static int on = -1; static unsigned long f_lo = 0, f_hi = 0; static int cap = 6000; static int f_img = -1;
      if (on < 0) { extern char* getenv(const char*); on = getenv("PS3_TRACE_SPUDMA") ? 1 : 0;
          const char* w = getenv("PS3_TRACE_SPUDMA_EA");
          if (w && *w) { extern unsigned long strtoul(const char*, char**, int); char* e2 = 0;
              f_lo = strtoul(w, &e2, 16); if (e2 && *e2 == '-') f_hi = strtoul(e2 + 1, 0, 16); }
          const char* im = getenv("PS3_TRACE_SPUDMA_IMG"); if (im && *im) { extern int atoi(const char*); f_img = atoi(im); }
          const char* c = getenv("PS3_TRACE_SPUDMA_CAP"); if (c && *c) { extern int atoi(const char*); cap = atoi(c); } }
      if (on && (f_img < 0 || spu->image_id == f_img)
             && (f_hi == 0 || ((uint32_t)ea + size > f_lo && (uint32_t)ea < f_hi))) { static int k = 0; if (k < cap) {
          fprintf(stderr, "[SPUDMA] %s ea=0x%08X lsa=0x%05X size=%u img=%d pc=0x%05X",
                  mfc_is_get(cmd) ? "GET" : "PUT", (uint32_t)ea, lsa, size,
                  spu->image_id, spu->pc & SPU_LS_MASK);
          /* Small PUTs into the game's static data are result/handshake
           * writes (e.g. the GoW2 completion-ring head at 0x5733A0) -- show
           * the payload so the protocol can be read off the trace. */
          if (!mfc_is_get(cmd) && size <= 16 && (uint32_t)ea < 0x01000000u) {
              const uint8_t* p = &spu->ls[lsa];
              fprintf(stderr, " data=");
              for (uint32_t i = 0; i < size; i++) fprintf(stderr, "%02X", p[i]);
          }
          fprintf(stderr, "\n");
          k++; } } }

#ifdef SPU_DMA_LOG
    { extern int g_spu_dma_log; if (g_spu_dma_log-- > 0)
        fprintf(stderr, "[spu-dma] %s lsa=0x%05X ea=0x%08X size=%u\n",
                mfc_is_get(cmd) ? "GET" : "PUT", lsa, (uint32_t)ea, size); }
#endif
#ifdef SPU_DMA_TRACE_HOOKS
    /* Task 4: GET/PUT before the transfer -- ea/lsa/size/tag are the brief's
     * mandatory <data> fields for DMA. spu->mfc_tag is already masked to
     * 0-31 at the MFC_TagID channel write (mfc_channel_write() below). */
    PS3_TRACE(PS3_TS_DMA, "mfc_do_transfer", mfc_is_get(cmd) ? "GET" : "PUT",
              "ea=0x%08X lsa=0x%05X size=%u tag=%u",
              (uint32_t)ea, lsa, size, spu->mfc_tag);
#endif
    if (mfc_is_put(cmd) && size >= 2048u &&
        (uint32_t)ea >= 0x40000000u && (uint32_t)ea < 0x60000000u) {
#ifdef _WIN32
        { extern void wp_aud_dma_note(int, unsigned); wp_aud_dma_note(spu->image_id, size); }   /* winport v20 */
        { extern void wp27_aud_put(int, unsigned, unsigned); wp27_aud_put(spu->image_id, (unsigned)ea, size); }   /* winport v27-audio */
#endif
        static unsigned heap_puts = 0;
        if (heap_puts < 8) {
            heap_puts++;
            fprintf(stderr, "[AUDMA] img=%d PUT ea=0x%08X size=%u\n",
                    spu->image_id, (unsigned)ea, size);
        }
    }
    if (mfc_is_get(cmd)) {
        /* GET: main memory -> local store */
        if (spu->image_id == 7 && lsa >= 0x4000u && size >= 1024u) {   /* winport v28-scream: [SCREAMLD] */
            extern void wp28_job_get(unsigned, unsigned, unsigned, const void*);
            wp28_job_get((unsigned)ea, lsa, size, ea_ptr);
        }
        if (spu->interp_spurs_pm && lsa < 0x10000u && lsa + size > 0x4080u) {   /* winport v29-scream-native */
            extern volatile int g_wp29_dirty;
            g_wp29_dirty = 1;
        }
        memcpy(ls_ptr, ea_ptr, size);
#ifdef SPU_DMA_TRACE_HOOKS
        { extern void spu_get_record(unsigned, unsigned, int, unsigned, unsigned);
          spu_get_record((uint32_t)ea, size, spu->image_id,
                         spu->pc & SPU_LS_MASK, spu->gpr[0]._u32[0] & SPU_LS_MASK); }
#endif
    } else if (mfc_is_put(cmd)) {
        /* PUT: local store -> main memory */
        memcpy(ea_ptr, ls_ptr, size);
        /* PS3_WATCH_STORE: um PUT de SPU escreve directamente em vm_base e
         * nunca passa por vm_write* -- e' invisivel aos dois watches de
         * store. Gated, OFF por default. */
#ifdef SPU_DMA_TRACE_HOOKS
        /* PS3_TRACE_SPU_PUT=1 (diagnostico, OFF por default): quem escreveu
         * este endereco guest. Um PUT so' faz memcpy para vm_base, por isso
         * nada mais no runtime sabe que job produziu um buffer. */
        { extern void spu_put_record(unsigned, unsigned, int, unsigned, unsigned);
          spu_put_record((uint32_t)ea, size, spu->image_id,
                         spu->pc & SPU_LS_MASK, spu->gpr[0]._u32[0] & SPU_LS_MASK); }
#ifdef SPU_PC_TRACE_HOOKS
        /* PS3_TRACE_SPU_PC=1: se o que acabou de sair contem um float enorme
         * (as particulas do GoW2 saem com o raio do sprite inflado), imprime o
         * caminho de codigo que o produziu. */
        if (spu->trace_pc) {
            extern void spu_pc_ring_dump(const uint32_t* ring, uint32_t n,
                                         uint32_t ea, uint32_t size, float v);
            /* So' o que o RSX le' como vertices (0x48xxxxxx no GoW2) e so'
             * valores na gama dos raios inflados medidos (8 mil a 1 milhao):
             * fora disto o anel enche-se de outros jobs. */
            const uint32_t ea32 = (uint32_t)ea;
            for (uint32_t o = 0; (ea32 >> 24) == 0x48u && o + 4 <= size; o += 4) {
                uint32_t w; memcpy(&w, ls_ptr + o, 4);
                w = __builtin_bswap32(w);
                const uint32_t e = (w >> 23) & 0xFFu;
                if (e < 0x8Cu || e > 0x93u) continue;      /* |v| fora de [8k, 1M) */
                float f; memcpy(&f, &w, 4);
                spu_pc_ring_dump(spu->pc_ring, spu->pc_ring_n, (uint32_t)ea, size, f);
                { extern void spu_huge_dump(const uint32_t* pc, const uint32_t* lsa,
                                            const float* v, uint32_t n);
                  spu_huge_dump(spu->huge_pc, spu->huge_lsa, spu->huge_v, spu->huge_n);
                  { extern void spu_huge_regs_dump(const float* regs, int taken);
                    spu_huge_regs_dump(spu->huge_regs, spu->huge_regs_taken); }
                  spu->huge_n = 0; }   /* janela nova ate' ao proximo PUT */
                break;
            }
        }
#endif
        /* Gated pelo MESMO macro de compilacao do PS3_TRACE aqui em cima:
         * os ~8 testes unitarios que incluem este header nao linkam o
         * ppu_loader.o e ficariam com simbolo por resolver. */
        ps3_watch_store_bulk((uint32_t)ea, size, "spu-dma PUT");
#endif
    }

#ifdef SPU_DMA_TRACE_HOOKS
    /* DONE: this recompiler executes DMA synchronously (see file header --
     * "In a real implementation this would be asynchronous"), so completion
     * is genuinely immediate here, not a fabricated pairing. */
    PS3_TRACE(PS3_TS_DMA, "mfc_do_transfer", "DONE",
              "ea=0x%08X lsa=0x%05X size=%u tag=%u",
              (uint32_t)ea, lsa, size, spu->mfc_tag);
#endif
    return 0;
}

/*
 * Execute a DMA list command (scatter/gather).
 * The list resides in the SPU's local store at `lsa`.
 * Each list element describes a (size, EA) pair for a sub-transfer.
 */
/* SPU event bit raised by a DMA-list stall-and-notify element (SPU_RdEventStat). */
#define SPU_EVENT_SN 0x2u

/* Walk list elements [first, count) of a list held in local store at list_lsa.
 * Data lands at / is taken from the running LS cursor lsa_cursor (MFC_LSA at
 * issue time). Each element: word 0 = stall-and-notify (bit 15) | size (low 15
 * bits), word 1 = EA low; EA high comes from the command's EAH. Returns 1 when
 * an element with the stall bit stopped the list (state kept in mfc->stall[tag]),
 * 0 when the list completed, <0 on a transfer error. */
static inline int mfc_list_walk(mfc_engine* mfc, spu_context* spu, uint32_t tag,
                                uint32_t list_lsa, uint64_t ea_base, uint32_t first,
                                uint32_t count, uint32_t base_cmd, uint32_t lsa_cursor)
{
    for (uint32_t i = first; i < count; i++) {
        uint32_t elem_lsa = (list_lsa + i * 8) & SPU_LS_MASK;
        uint32_t size_and_flags = spu_ls_read32(spu, elem_lsa);
        uint32_t eal = spu_ls_read32(spu, elem_lsa + 4);
        /* Element = u16 sb | u16 ts | u32 eal: stall-and-notify is 0x8000 of
         * the UPPER halfword (bit 31 of the word), the size the lower one.
         * (Until 2026-09-23 bit 15 -- inside the size -- was tested.) */
        uint32_t xfer_size = size_and_flags & 0x7FFF;
        int stall_notify = (size_and_flags >> 31) & 1;
        uint64_t ea = (ea_base & 0xFFFFFFFF00000000ull) | eal;
        /* Each element occupies whole LS quadwords; the EA gives the offset
         * inside the first one (RPCS3 do_list_transfer). */
        uint32_t transfer_lsa = (lsa_cursor & ~15u) | (eal & 15u);
        { extern int spu_irq_trace_on(void);
          if (spu_irq_trace_on()) fprintf(stderr, "[SPUIRQ] list tag=%u elem %u/%u ea=0x%08X size=%u stall=%d lsa=0x%05X img=%d\n",
                                         tag, i, count, (uint32_t)ea, xfer_size, stall_notify, transfer_lsa, spu->image_id); }
        /* A zero-size element moves nothing; it is how a list asks only for
         * the stall-and-notify (RPCS3 do_list_transfer skips the copy). */
        int rc = xfer_size ? mfc_do_transfer(spu, transfer_lsa, ea, xfer_size, base_cmd) : 0;
        if (rc != 0) { extern int spu_irq_trace_on(void); if (spu_irq_trace_on()) fprintf(stderr, "[SPUIRQ] list elem rc=%d\n", rc); return rc; }
        lsa_cursor = (lsa_cursor & ~15u) + ((xfer_size + 15u) & ~15u);
        if (stall_notify && i + 1 <= count) {
            mfc->stall[tag].active = 1;
            mfc->stall[tag].list_lsa = list_lsa;
            mfc->stall[tag].next = i + 1;
            mfc->stall[tag].count = count;
            mfc->stall[tag].base_cmd = base_cmd;
            mfc->stall[tag].lsa_cursor = lsa_cursor;
            mfc->stall[tag].ea_base = ea_base;
            mfc->stall_stat |= 1u << tag;
            spu->event_status |= SPU_EVENT_SN;
            return 1;
        }
    }
    return 0;
}

/* List command: MFC_LSA = LS address of the data, EAL = LS address of the list
 * (8-byte aligned), EAH = high EA word, size = list size in bytes. (Before
 * 2026-09-23 the elements were read at MFC_LSA -- the data address.) */
static inline int mfc_do_list_transfer(mfc_engine* mfc, spu_context* spu, uint32_t tag,
                                       uint32_t data_lsa, uint64_t ea, uint32_t list_size,
                                       uint32_t cmd)
{
    uint32_t list_lsa = (uint32_t)ea & SPU_LS_MASK & ~7u;
#ifdef SPU_DMA_TRACE_HOOKS
    PS3_TRACE(PS3_TS_DMA, "mfc_do_list_transfer", "LIST",
              "ea=0x%08X lsa=0x%05X size=%u tag=%u",
              (uint32_t)ea, data_lsa, list_size, tag);
#endif
    return mfc_list_walk(mfc, spu, tag, list_lsa, ea, 0, list_size / 8,
                         cmd & ~0x04u, data_lsa);
}

/* MFC_WrListStallAck: resume the stalled list of `tag`. */
static inline void mfc_list_stall_ack(mfc_engine* mfc, spu_context* spu, uint32_t tag)
{
    tag &= 0x1F;
    if (!mfc->stall[tag].active) return;
    mfc->stall[tag].active = 0;
    int rc = mfc_list_walk(mfc, spu, tag, mfc->stall[tag].list_lsa, mfc->stall[tag].ea_base,
                           mfc->stall[tag].next, mfc->stall[tag].count,
                           mfc->stall[tag].base_cmd, mfc->stall[tag].lsa_cursor);
    if (rc != 1) mfc->tag_completed |= (1u << tag);   /* list done (or failed) */
}

/* ---------------------------------------------------------------------------
 * Enqueue an MFC command (called when SPU writes to MFC_Cmd channel)
 * -----------------------------------------------------------------------*/
static inline int mfc_enqueue(mfc_engine* mfc, spu_context* spu)
{
    if (mfc->queue_count >= MFC_QUEUE_DEPTH)
        return -1; /* queue full */

    uint32_t lsa  = spu->mfc_lsa;
    uint64_t ea   = ((uint64_t)spu->mfc_eah << 32) | spu->mfc_eal;
    uint32_t size = spu->mfc_size;
    uint32_t tag  = spu->mfc_tag;
    uint32_t cmd  = spu->ls[0]; /* overridden below */

    /* The cmd was just written to the MFC_Cmd channel -- we receive it
     * as a parameter to the channel write handler.  For this inline API,
     * the caller should pass it.  We use a wrapper below. */
    (void)cmd;
    return 0;
}

/*
 * Submit and immediately execute an MFC command.
 * This is the main entry point called when the SPU writes to MFC_Cmd.
 */
static inline int mfc_submit(mfc_engine* mfc, spu_context* spu, uint32_t cmd)
{
    uint32_t lsa  = spu->mfc_lsa;
    uint64_t ea   = ((uint64_t)spu->mfc_eah << 32) | spu->mfc_eal;
    uint32_t size = spu->mfc_size;
    uint32_t tag  = spu->mfc_tag & 0x1F;
    int rc = 0;

    /* Mark tag as in-progress */
    mfc->tag_completed &= ~(1u << tag);
    { extern int spu_irq_trace_on(void);
      if (spu->image_id == 7 && spu_irq_trace_on()) { static unsigned k = 0; if (k++ < 20000)
          fprintf(stderr, "[SPUIRQ] cmd=0x%02X lsa=0x%05X ea=0x%08X size=%u tag=%u pc=0x%05X\n",
                  cmd, lsa, (uint32_t)ea, size, tag, spu->pc & SPU_LS_MASK); } }

    /* Handle sync/barrier commands */
    if (cmd == MFC_BARRIER_CMD || cmd == MFC_EIEIO_CMD || cmd == MFC_SYNC_CMD) {
        /* For synchronous emulation these are no-ops; all prior DMAs
         * have already completed. */
        mfc->tag_completed |= (1u << tag);
        return 0;
    }

    /* Atomic commands: GETLLAR 0xD0, PUTLLC 0xB4, PUTLLUC 0xB0, PUTQLLUC 0xB8.
     * They MUST be intercepted before the generic paths: hardware fixes the
     * transfer at one 128-byte cache line and ignores MFC_Size (games write 0,
     * which the generic path rejects -> silent no-op, observed as GoW2's edge
     * task seeing its LFQueue eternally empty), and PUTLLC's bit 2 would
     * misroute it into the list-transfer path (observed as a garbage sweep of
     * 2-byte PUTs). Reservation semantics collapse to always-succeed, matching
     * RdAtomicStat below. */
    if (cmd == 0xD0 || cmd == 0xB4 || cmd == 0xB0 || cmd == 0xB8) {
        extern unsigned char* vm_base;
        extern uint32_t ppu_vm_size;
        extern int ppu_guest_range_committed(uint32_t addr, uint32_t n);
        uint32_t e   = (uint32_t)ea & ~127u;
        uint32_t l   = lsa & SPU_LS_MASK & ~127u;
        /* Atomic LFQueue traffic revisits the same cache-line pages millions
         * of times per second. Keep only positive results: committed guest
         * ranges are monotonic during a workload, while a negative result is
         * deliberately rechecked so a later vm_commit cannot be hidden. */
        static _Thread_local uint32_t committed_page[4];
        static _Thread_local unsigned char committed_valid[4];
        const uint32_t page = e >> 12;
        unsigned slot = page & 3u;
        int committed = !ppu_vm_size ||
                        ((uint64_t)e + 128 <= ppu_vm_size &&
                         ((committed_valid[slot] && committed_page[slot] == page) ||
                          ppu_guest_range_committed(e, 128)));
        if (committed && ppu_vm_size) {
            committed_page[slot] = page;
            committed_valid[slot] = 1;
        }
        { static int trace_atomic = -1; static unsigned atomic_n = 0;
          if (trace_atomic < 0) { extern char* getenv(const char*);
              trace_atomic = getenv("PS3_TRACE_SPUDMA") ? 1 : 0; }
          if (trace_atomic && atomic_n < 128)
              fprintf(stderr, "[SPUATOMIC] #%u cmd=0x%02X ea=0x%08X lsa=0x%05X committed=%d\n",
                      ++atomic_n, cmd, e, l, committed); }
        if (committed) {
            /* E164 (2026-09-02): o cap fixo de 100 linhas enganou uma leitura (33 GETLLAR "e nenhum
             * PUTLLC" eram so' os 100 primeiros eventos). Agora: janela PS3_TRACE_SPUDMA_EA=lo-hi (hex),
             * cap PS3_TRACE_SPUDMA_CAP, e com PS3_TRACE_LFQHDR=1 os 0x80 bytes da linha (LS apos GETLLAR,
             * LS antes de PUTLL*) para comparar com o que o PPU escreveu ([LFQHDR] ppu-push). */
            { static int on = -1; static unsigned long f_lo = 0, f_hi = 0; static int cap = 100, hdr = 0;
              if (on < 0) { extern char* getenv(const char*); on = getenv("PS3_TRACE_SPUDMA") ? 1 : 0;
                  const char* w = getenv("PS3_TRACE_SPUDMA_EA");
                  if (w && *w) { extern unsigned long strtoul(const char*, char**, int); char* e2 = 0;
                      f_lo = strtoul(w, &e2, 16); if (e2 && *e2 == '-') f_hi = strtoul(e2 + 1, 0, 16); }
                  const char* c = getenv("PS3_TRACE_SPUDMA_CAP"); if (c && *c) { extern int atoi(const char*); cap = atoi(c); }
                  hdr = getenv("PS3_TRACE_LFQHDR") ? 1 : 0; }
              if (on && (f_hi == 0 || (e + 128 > f_lo && e < f_hi))) { static int k = 0; if (k < cap || (k % 5000) == 0) {
                  fprintf(stderr, "[SPUDMA] %s ea=0x%08X lsa=0x%05X (atomic) #%d img=%d pc=0x%05X",
                          cmd == 0xD0 ? "GETLLAR" : (cmd == 0xB4 ? "PUTLLC" : "PUTLLU*"), e, l, k,
                          spu->image_id, spu->pc & SPU_LS_MASK);
                  if (hdr) { const uint8_t* h = (cmd == 0xD0) ? (const uint8_t*)(vm_base + e) : (const uint8_t*)&spu->ls[l];
                      fprintf(stderr, " hdr:"); for (int i = 0; i < 0x80; i++) fprintf(stderr, "%s%02X", (i % 16) ? "" : " ", h[i]); }
                  fprintf(stderr, "\n"); }
                  k++; } }
            /* E190 (2026-09-02): RESERVA A SERIO. "Sempre sucesso" no PUTLLC perdia as
             * escritas do PPU feitas entre o GETLLAR e o PUTLLC do SPU (o SPU escrevia a
             * linha inteira a partir da sua copia antiga). Medido na LFQueue do SCREAM
             * (0x47CA9B80): h1 (cursor de pop) 1288 > h8 (push) 1281 -> o push do PPU ve
             * "cheia" (h8-h1 negativo -> +4096) e fica em spin com o giant lock -> tudo
             * a' fome, o loader nunca corre. Como no hardware: GETLLAR guarda um
             * instantaneo da linha; PUTLLC so' escreve se a linha nao mudou entretanto
             * (comparacao e escrita sob o mesmo mutex que o HLE do PPU usa para o
             * cabecalho) e devolve o resultado em MFC_RdAtomicStat (1 = falhou, o
             * libspurs no SPU repete). PUTLLUC/PUTQLLUC continuam incondicionais. */
            { extern void ps3_atomic_line_lock(void); extern void ps3_atomic_line_unlock(void);
              extern __thread uint32_t t_spu_rsv_ea; extern __thread int t_spu_rsv_valid;
              extern __thread int t_spu_atomic_stat; extern __thread uint8_t t_spu_rsv_line[128];
              ps3_atomic_line_lock();
              if (cmd == 0xD0) {
                  memcpy(&spu->ls[l], vm_base + e, 128);
                  { extern volatile unsigned int g_wp32_lbl_ea; extern void wp32_lbl_get_note(unsigned int, int);   /* winport v32-gcmdiag-l */
                    if (__builtin_expect(e <= g_wp32_lbl_ea && g_wp32_lbl_ea < e + 128u, 0)) wp32_lbl_get_note(e, 1); }
                  /* Snapshot a partir do LS, NAO uma 2.a leitura da memoria (porte de
                   * upstream d7f28198). O GETLLAR do hardware e' UMA leitura atomica de
                   * 128 B. Os stores do PPU liftado nao passam por ps3_atomic_line_lock,
                   * logo um store entre as duas leituras dava ao SPU uma linha VELHA e
                   * uma reserva NOVA: o PUTLLC seguinte passava e apagava a escrita do
                   * PPU (classe do E190). Do LS, um store posterior deixa as DUAS velhas
                   * e o PUTLLC falha como deve. */
                  memcpy(t_spu_rsv_line, &spu->ls[l], 128);
                  t_spu_rsv_ea = e; t_spu_rsv_valid = 1;
              } else if (cmd == 0xB4) {
                  if (t_spu_rsv_valid && t_spu_rsv_ea == e && memcmp(t_spu_rsv_line, vm_base + e, 128) == 0) {
                      memcpy(vm_base + e, &spu->ls[l], 128);
                      t_spu_atomic_stat = 0;
                  } else {
                      t_spu_atomic_stat = 1;                  /* MFC_PUTLLC_FAILURE */
                      { static unsigned long nf = 0; nf++;
                        static int on = -1; if (on < 0) { extern char* getenv(const char*); on = getenv("PS3_TRACE_SPUDMA") ? 1 : 0; }
                        if (on && (nf <= 20 || (nf % 10000) == 0))
                            fprintf(stderr, "[SPUDMA] PUTLLC FALHOU #%lu ea=0x%08X (linha mudou desde o GETLLAR)\n", nf, e); }
                  }
                  t_spu_rsv_valid = 0;
              } else {
                  memcpy(vm_base + e, &spu->ls[l], 128);      /* PUTLLUC / PUTQLLUC */
                  t_spu_rsv_valid = 0;
              }
              ps3_atomic_line_unlock(); }
#ifdef SPU_DMA_TRACE_HOOKS
            if (cmd != 0xD0) ps3_watch_store_bulk(e, 128, "spu-dma PUTLL*");
#endif
        } else {
            static unsigned long skipped = 0;
            ++skipped;
            if (skipped <= 16 || (skipped & (skipped - 1)) == 0)
                fprintf(stderr,
                        "[spu-dma] SKIP atomic out-of-range #%lu cmd=0x%02X ea=0x%08X lsa=0x%05X\n",
                        skipped, cmd, e, l);
        }
        mfc->tag_completed |= (1u << tag);
        return 0;
    }

    /* Execute the transfer */
    if (mfc_is_list(cmd)) {
        rc = mfc_do_list_transfer(mfc, spu, tag, lsa, ea, size, cmd);
        if (rc == 1) return 0;   /* stalled: the tag completes when the list does */
    } else {
        rc = mfc_do_transfer(spu, lsa, ea, size, cmd);
    }

    /* Mark tag as completed */
    mfc->tag_completed |= (1u << tag);

    return rc;
}

/* ---------------------------------------------------------------------------
 * Tag group synchronization
 * -----------------------------------------------------------------------*/

/* Tag update types (values written to MFC_WrTagUpdate) */
#define MFC_TAG_UPDATE_IMMEDIATE  0  /* return status immediately */
#define MFC_TAG_UPDATE_ANY        1  /* wait for any tag in mask */
#define MFC_TAG_UPDATE_ALL        2  /* wait for all tags in mask */

/*
 * Query tag status.  Returns the bitmask of completed tags that match
 * the tag mask.  In synchronous mode, all submitted DMAs are already
 * complete, so this just returns the intersection.
 */
static inline uint32_t mfc_read_tag_status(const mfc_engine* mfc, uint32_t tag_mask)
{
    return mfc->tag_completed & tag_mask;
}

/*
 * Poll for tag completion.
 * update_type: 0 = immediate, 1 = any, 2 = all.
 * Returns the completed tag mask.  In synchronous mode, always succeeds.
 */
static inline uint32_t mfc_tag_wait(const mfc_engine* mfc, uint32_t tag_mask,
                                     uint32_t update_type)
{
    uint32_t completed = mfc->tag_completed & tag_mask;

#ifdef SPU_DMA_TRACE_HOOKS
    /* Task 4: TAG_WAIT. Deliberate deviation from the brief's usual
     * ea=/lsa=/size= triple -- a tag-mask wait has no single EA/LS/size (it
     * is a wait over a BITMASK of up to 32 tags, not one transfer), so
     * fabricating ea=0x00000000/lsa=0x00000/size=0 would misleadingly imply
     * a real address/size that does not exist for this operation.
     *
     * Field name for the mask value is `mask=`, NOT `tag=` (corrected
     * 2026-07-24, see the report's DMA event table for the full history):
     * GET/PUT/LIST/DONE emit `tag=%u`, a small integer tag id. A tag-mask
     * wait has no single tag id -- it waits on a bitmask of up to 32 tags,
     * formatted `0x%08X`. Reusing the key `tag=` for both would give one key
     * two incompatible value domains, and Task 6's parser -- which pairs DMA
     * transfers by tag -- would mis-pair them. `mode=` is the update_type
     * (IMMEDIATE/ANY/ALL) and `result=` is the mask already completed. */
    PS3_TRACE(PS3_TS_DMA, "mfc_tag_wait", "TAG_WAIT",
              "mask=0x%08X mode=%u result=0x%08X", tag_mask, update_type, completed);
#endif

    switch (update_type) {
    case MFC_TAG_UPDATE_IMMEDIATE:
        return completed;
    case MFC_TAG_UPDATE_ANY:
        /* In async mode we would block until any bit is set.
         * Synchronous: always returns immediately. */
        return completed;
    case MFC_TAG_UPDATE_ALL:
        /* Block until all bits in mask are set. */
        return completed;
    default:
        return completed;
    }
}

/* ---------------------------------------------------------------------------
 * Convenience: process an SPU channel write for MFC-related channels.
 * -----------------------------------------------------------------------*/
static inline void mfc_channel_write(mfc_engine* mfc, spu_context* spu,
                                      uint32_t channel, uint32_t value)
{
    switch (channel) {
    case MFC_LSA:
        spu->mfc_lsa = value;
        break;
    case MFC_EAH:
        spu->mfc_eah = value;
        break;
    case MFC_EAL:
        spu->mfc_eal = value;
        break;
    case MFC_Size:
        spu->mfc_size = value;
        break;
    case MFC_TagID:
        spu->mfc_tag = value & 0x1F;
        break;
    case MFC_Cmd:
        mfc_submit(mfc, spu, value);
        break;
    case MFC_WrTagMask:
        spu->mfc_tag_mask = value;
        break;
    case MFC_WrTagUpdate:
        spu->mfc_tag_status = mfc_tag_wait(mfc, spu->mfc_tag_mask, value);
        break;
    case MFC_WrListStallAck:
        mfc_list_stall_ack(mfc, spu, value);
        break;
    default:
        break;
    }
}

static inline uint32_t mfc_channel_read(mfc_engine* mfc, spu_context* spu,
                                         uint32_t channel)
{
    switch (channel) {
    case MFC_RdTagStat:
        return spu->mfc_tag_status;
    case MFC_RdTagMask:
        return spu->mfc_tag_mask;
    case MFC_RdListStallStat: {
        uint32_t st = mfc->stall_stat;
        mfc->stall_stat = 0;
        return st; }
    case MFC_RdAtomicStat: {
        /* E190: 0 = MFC_PUTLLC_SUCCESS, 1 = MFC_PUTLLC_FAILURE (ver o caminho atomico). */
        extern __thread int t_spu_atomic_stat;
        uint32_t st = (uint32_t)t_spu_atomic_stat; t_spu_atomic_stat = 0;
        return st; }
    default:
        return 0;
    }
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SPU_DMA_H */
