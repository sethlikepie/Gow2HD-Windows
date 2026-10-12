/*
 * ps3recomp - cellSpurs LFQueue HLE (PPU side of the CellSyncLFQueue protocol)
 *
 * GoW2's movie player feeds its SPU decoder task through SPURS lock-free
 * queues (_cellSpursLFQueueInitialize / _cellSpursLFQueuePushBody imports).
 * The queue control block (128 bytes, big-endian, in guest memory) is SHARED
 * with the SPU-side task library, which operates on it with raw DMA atomics
 * (getllar/putllc through the lifted SPU runtime). The PPU side must
 * therefore write the structure with protocol fidelity, not a private HLE
 * shape. This is a C port of the v1 (non-ANY2ANY) protocol as reverse-
 * engineered in RPCS3's cellSync.cpp (cellSyncLFQueueInitialize,
 * _cellSyncLFQueueGetPushPointer/CompletePushPointer/PushBody + pop side).
 *
 * CellSyncLFQueue layout (BE, 128B, 128-aligned):
 *   0x00 pop1  { u16 m_h1, m_h2, m_h3, m_h4 }
 *   0x08 push1 { u16 m_h5, m_h6, m_h7, m_h8 }
 *   0x10 u32 m_size      0x14 u32 m_depth
 *   0x18 u64 m_buffer    0x20 u8 m_bs[4]
 *   0x24 u32 m_direction 0x28 u32 m_v1
 *   0x2C s32 init        0x30 u16 push2.pack
 *   0x32 u16 m_hs1[15]   0x50 u16 pop2.pack
 *   0x52 u16 m_hs2[15]   0x70 u64 m_eaSignal
 *   0x78 u32 m_v2        0x7C u32 m_eq_id
 *
 * The SPURS wrapper stores the taskset EA as m_eaSignal; a push completing
 * against a registered waiter (m_hs1[slot]) sends that task a SPURS signal.
 * PPU-PPU atomicity comes from one global lock (all our PPU-side accesses
 * take it). PPU-SPU races mirror the existing runtime-wide limitation
 * (putllc in the lifted runtime always succeeds).
 */

#include "ps3_trace.h"   /* Task 4 (bringup-instrumentacao-fase0): subsistema SPURS */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static __thread unsigned t_wp76_lfq_yn = 0;   /* winport v76c-usleep-fast: waits within one get_push/get_pop pointer call */
#ifdef _WIN32
#include <windows.h>
/* winport: mingw */ /* E190: same lock as the SPU atomic path, as on POSIX */
extern void ps3_atomic_line_lock(void); extern void ps3_atomic_line_unlock(void);
static void lfq_lock(void)   { ps3_atomic_line_lock(); }
static void lfq_unlock(void) { ps3_atomic_line_unlock(); }
static void lfq_yield(void)   /* winport: mingw */ /* wait WITHOUT the giant lock */
{
    extern int ppu_giant_lock_held_by_caller(void);
    extern void ppu_giant_lock_release(void);
    extern void ppu_giant_lock_acquire(void);
    /* winport v76c-usleep-fast: WINPORT_LFQ_SPINYIELD=N -> the first N waits of one call only SwitchToThread */
    extern volatile long long g_wp76_sl[4];
    static int spin = -1;
    if (spin < 0) { const char* e = getenv("WINPORT_LFQ_SPINYIELD"); int v = (e && *e) ? atoi(e) : 0;
                    spin = v < 0 ? 0 : v > 4096 ? 4096 : v;
                    if (spin) { fprintf(stderr, "[LFQ76] full/empty LFQueue waits: %d x SwitchToThread before Sleep(1)\n", spin); fflush(stderr); } }
    const int quick = (unsigned)spin > t_wp76_lfq_yn++;
    if (ppu_giant_lock_held_by_caller()) {
        ppu_giant_lock_release();
        if (quick) SwitchToThread(); else { Sleep(1); __atomic_add_fetch(&g_wp76_sl[2], 1ll, __ATOMIC_RELAXED); }
        ppu_giant_lock_acquire();
    } else {
        if (quick) SwitchToThread(); else { Sleep(0); __atomic_add_fetch(&g_wp76_sl[3], 1ll, __ATOMIC_RELAXED); }
    }
}
#else
#include <pthread.h>
#include <stdlib.h>   /* getenv: os gates PS3_* deste modulo */
#include <sched.h>
/* E190: o mesmo mutex que o caminho atomico do SPU (GETLLAR/PUTLLC) usa -- o PPU escreve o
 * cabecalho da fila sob ele, logo um PUTLLC do SPU nunca ve uma linha "a meio". */
extern void ps3_atomic_line_lock(void); extern void ps3_atomic_line_unlock(void);
static void lfq_lock(void)   { ps3_atomic_line_lock(); }
static void lfq_unlock(void) { ps3_atomic_line_unlock(); }
static void lfq_yield(void)  { sched_yield(); }
#endif

extern unsigned char* vm_base;

/* Guest-EA -> host pointer (EAs are 32-bit; HLE args arrive as raw u64). */
static inline uint8_t* lfq_p2h(uint64_t ea) { return vm_base + (uint32_t)ea; }

static inline uint16_t rd16(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }
static inline uint32_t rd32(const uint8_t* p)
{ return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static inline uint64_t rd64(const uint8_t* p)
{ return ((uint64_t)rd32(p) << 32) | rd32(p + 4); }
static inline void wr16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static inline void wr32(uint8_t* p, uint32_t v)
{ p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
static inline void wr64(uint8_t* p, uint64_t v) { wr32(p, (uint32_t)(v >> 32)); wr32(p + 4, (uint32_t)v); }

/* Commit-aware guest-range check for the RE probes below. The guest address
 * space is RESERVED as one big range but only sub-regions are COMMITTED, so a
 * "looks like an EA" word (e.g. 0x2E667800, ASCII ".fx") can be logically
 * in-range yet fault on touch. Every diagnostic that dereferences a
 * guest-derived word must pass this, not just a numeric range check.
 * Non-static: lfq_host_inflate.c reuses it. */
static int lfq_trace_on(void)   /* winport: mingw */ /* PS3_TRACE_LFQ: periodic push probes */
{ static int c = -1; if (c < 0) { const char* e = getenv("PS3_TRACE_LFQ"); c = (e && *e && *e != '0') ? 1 : 0; } return c; }
int lfq_ea_readable(uint32_t ea, uint32_t len)
{
    if (!ea || !len || ea + len < ea) return 0;
#ifdef _WIN32
    { extern int ppu_guest_range_committed(uint32_t addr, uint32_t n);   /* winport: mingw */
      if (ppu_guest_range_committed(ea, len)) return 1; }   /* registered commit: no page walk */
    {
        const uint8_t* p = vm_base + ea;
        const uint8_t* end = p + len;
        while (p < end) {
            MEMORY_BASIC_INFORMATION mbi;
            if (!VirtualQuery(p, &mbi, sizeof mbi)) return 0;
            if (mbi.State != MEM_COMMIT) return 0;
            if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return 0;
            p = (const uint8_t*)mbi.BaseAddress + mbi.RegionSize;
        }
        return 1;
    }
#else
    return 1;   /* posix builds commit the whole range up front */
#endif
}

/* CELL_SYNC error codes mapped through SyncErrorToSpursError (0x80410900 base
 * like the task-side codes the game compares against). */
#define LFQ_OK          0
#define LFQ_ERR_AGAIN   ((int32_t)0x80410901)
#define LFQ_ERR_INVAL   ((int32_t)0x80410902)
#define LFQ_ERR_NULLPTR ((int32_t)0x80410905)
#define LFQ_ERR_PERM    ((int32_t)0x80410909)
#define LFQ_ERR_BUSY    ((int32_t)0x8041090A)
#define LFQ_ERR_STAT    ((int32_t)0x8041090D)
#define LFQ_ERR_ALIGN   ((int32_t)0x80410910)

/* Porque e' que o alinhamento falhou. Os tres sitios devolviam LFQ_ERR_ALIGN em
 * silencio, e a jusante esse codigo (0x80410910) acaba usado como EA por
 * task-lib nao verificado -- e' o intermitente a ~29% que o ledger E69 apanhou:
 *
 *     [ATTERR]   a=0x80410910
 *     [EDGEATT1] a=0x0000BA10 b=0x80410910
 *     [spu-dma] SKIP out-of-range GET ea=0x00003300 ...
 *
 * Sem esta linha nao ha' como saber se falha a fila (128) ou o buffer (16), nem
 * com que endereco. Primeiras 16 mais cada potencia de 2, como os outros
 * contadores desta arvore (E63/E64). */
static void lfq_align_fail(const char* who, uint64_t queue, uint64_t buffer)
{
    static unsigned long n = 0; ++n;
    if (n <= 16 || (n & (n - 1)) == 0)
        fprintf(stderr,
                "[LFQALIGN] #%lu %s queue=0x%08X (&127=%u) buffer=0x%08X (&15=%u)\n",
                n, who, (uint32_t)queue, (unsigned)(queue & 127),
                (uint32_t)buffer, (unsigned)(buffer & 15));
}

#define DIR_SPU2SPU 0
#define DIR_SPU2PPU 1
#define DIR_PPU2SPU 2
#define DIR_ANY2ANY 3

/* Wake the taskset's decoder task: SPURS signal from cellSpurs.c. */
extern int32_t cellSpursSendSignal(void* taskset, uint32_t taskId);
/* Data-available kick: re-dispatch lifted tasks so a consumer that exited on
 * queue-empty (instead of kernel-waiting, which the standalone lifted runtime
 * cannot express) gets another run. Defined in cellSpurs.c. */
extern void spurs_task_kick_all(void);

/* ------------------------------------------------------------------ */
/* _cellSpursLFQueueInitialize(pTasksetOrSpurs, pQueue, buffer, size,
 *                             depth, direction)
 * = cellSyncLFQueueInitialize(pQueue, buffer, size, depth, direction,
 *                             eaSignal=pTasksetOrSpurs)                */
/* ------------------------------------------------------------------ */
uint64_t _cellSpursLFQueueInitialize(uint64_t tasksetOrSpurs, uint64_t queue,
                                     uint64_t buffer, uint64_t size64,
                                     uint64_t depth64, uint64_t direction64)
{
    uint32_t size = (uint32_t)size64, depth = (uint32_t)depth64,
             direction = (uint32_t)direction64;

    if (!queue) return (uint64_t)(int64_t)LFQ_ERR_NULLPTR;
    if (size) {
        if (!buffer) return (uint64_t)(int64_t)LFQ_ERR_NULLPTR;
        if (size > 0x4000 || (size % 16)) return (uint64_t)(int64_t)LFQ_ERR_INVAL;
    }
    if (!depth || depth > 0x7fff || direction > 3) return (uint64_t)(int64_t)LFQ_ERR_INVAL;
    if ((queue & 127) || (buffer & 15)) {
        lfq_align_fail(__func__, queue, buffer);
        return (uint64_t)(int64_t)LFQ_ERR_ALIGN;
    }

    uint8_t* q = lfq_p2h(queue);
    lfq_lock();

    fprintf(stderr, "[LFQ] init q=0x%X buf=0x%X size=0x%X depth=%u dir=%u taskset=0x%X\n",
            (uint32_t)queue, (uint32_t)buffer, size, depth, direction,
            (uint32_t)tasksetOrSpurs);

    if (rd32(q + 0x2C) == 2) {                      /* already initialized */
        if (rd32(q + 0x10) != size || rd32(q + 0x14) != depth ||
            (uint32_t)rd64(q + 0x18) != (uint32_t)buffer) {
            lfq_unlock();
            return (uint64_t)(int64_t)LFQ_ERR_INVAL;
        }
        lfq_unlock();
        return LFQ_OK;
    }

    wr32(q + 0x10, size);
    wr32(q + 0x14, depth);
    wr64(q + 0x18, buffer);
    wr32(q + 0x24, direction);
    memset(q + 0x32, 0, 15 * 2);                    /* m_hs1 */
    memset(q + 0x52, 0, 15 * 2);                    /* m_hs2 */
    wr64(q + 0x70, tasksetOrSpurs);                 /* m_eaSignal */

    if (direction == DIR_ANY2ANY) {
        wr64(q + 0x00, 0);                          /* pop1 */
        wr64(q + 0x08, 0);                          /* push1 */
        wr64(q + 0x18, buffer | 1);
        q[0x20] = 0xFF; q[0x21] = 0xFF;
        wr32(q + 0x28, 0xFFFFFFFFu);                /* m_v1 */
        wr16(q + 0x30, 0xFFFF);                     /* push2 */
        wr16(q + 0x50, 0xFFFF);                     /* pop2 */
    } else {
        uint16_t h3 = rd16(q + 0x04), h7 = rd16(q + 0x0C);
        wr16(q + 0x00, 0); wr16(q + 0x02, 0); wr16(q + 0x04, h3); wr16(q + 0x06, 0);
        wr16(q + 0x08, 0); wr16(q + 0x0A, 0); wr16(q + 0x0C, h7); wr16(q + 0x0E, 0);
        q[0x20] = 0xFF; q[0x21] = 0xFF; q[0x22] = 0xFF; q[0x23] = 0xFF;
        wr32(q + 0x28, 0);                          /* m_v1 */
        wr16(q + 0x30, 0);                          /* push2 */
        wr16(q + 0x50, 0);                          /* pop2 */
    }
    wr32(q + 0x78, 0);                              /* m_v2 */
    wr32(q + 0x7C, 0);                              /* m_eq_id */
    wr32(q + 0x2C, 0);                              /* init: released */

    lfq_unlock();
    return LFQ_OK;
}

/* ------------------------------------------------------------------ */
/* v1 GetPushPointer (PPU2SPU): reserve the next push slot.
 * Called with the lock HELD; may drop it to wait when full+blocking.  */
/* ------------------------------------------------------------------ */
static int32_t lfq_get_push_pointer(uint8_t* q, int32_t* pointer, uint32_t isBlocking)
{
    t_wp76_lfq_yn = 0;   /* winport v76c-usleep-fast */
    const int32_t depth = (int32_t)rd32(q + 0x14);

    for (;;) {
        int32_t h8 = (int16_t)rd16(q + 0x0E);       /* next push seq */
        int32_t h1 = rd16(q + 0x00);                /* pop cursor    */
        int32_t var2 = h8 - h1;
        if (var2 < 0) var2 += depth * 2;

        if (var2 < depth) {                         /* room available */
            *pointer = h8;
            wr16(q + 0x0E, (uint16_t)((h8 + 1 >= depth * 2) ? 0 : h8 + 1));
            return LFQ_OK;
        }
        if (!isBlocking) return LFQ_ERR_AGAIN;
        /* E188: push bloqueante numa fila cheia -- dizer QUAL (medido e186: a thread
         * principal fica aqui, com o giant lock, e tudo o resto morre a' fome). */
        { static int on = -1; if (on < 0) { extern char* getenv(const char*); on = getenv("PS3_TRACE_LFQHDR") ? 1 : 0; }
          static unsigned long spins = 0; if (on && ((++spins) % 20000) == 1)
              fprintf(stderr, "[LFQ] push BLOQUEADO q=0x%08X h8=%d h1=%d depth=%d (spins=%lu)\n",
                      (uint32_t)(uintptr_t)(q - vm_base), h8, h1, depth, spins); }
        lfq_unlock(); lfq_yield(); lfq_lock();      /* full: wait for the consumer */
    }
}

/* ------------------------------------------------------------------ */
/* v1 CompletePushPointer (PPU2SPU): publish the filled slot; if the
 * consumer registered as a waiter, send it a SPURS signal.
 * Lock HELD by caller (the CAS pair collapses to plain writes).       */
/* ------------------------------------------------------------------ */
static int32_t lfq_complete_push_pointer(uint8_t* q, int32_t pointer, uint32_t* out_signal)
{
    const int32_t depth = (int32_t)rd32(q + 0x14);

    /* push3 = first 2 u16 of push1; pop3 = first 2 u16 of pop1 */
    int32_t h5 = rd16(q + 0x08), h6 = rd16(q + 0x0A);

    int32_t var1 = pointer - h5;
    if (var1 < 0) var1 += depth * 2;

    int32_t var2 = (int16_t)rd16(q + 0x06) - (int32_t)rd16(q + 0x00);   /* pop h4 - h1 */
    if (var2 < 0) var2 += depth * 2;

    int32_t var9_ = 15 - var1;
    var9_ = (var9_ & 0x30) ? 0 : (1 << var9_);
    uint16_t nz = (uint16_t)~(var9_ | h6);
    int32_t var9 = nz ? (__builtin_clz((uint32_t)nz) - 16) : 16;

    int32_t var5 = h6 | var9_;
    if (var9 & 0x30) var5 = 0; else var5 <<= var9;

    int32_t var3 = h5 + var9;
    if (var3 >= depth * 2) var3 -= depth * 2;

    uint16_t pack = rd16(q + 0x30);                 /* push2: 3 packed 5-bit fields */
    int32_t var4 = ((pack >> 10) & 0x1f) - ((pack >> 5) & 0x1f);
    if (var4 < 0) var4 += 0x1e;

    uint32_t var6;
    if (var2 + var4 <= 15 && ((pack >> 10) & 0x1f) != (pack & 0x1f)) {
        int32_t var11 = (pack >> 10) & 0x1f;
        if (var11 >= 15) var11 -= 15;
        uint16_t var12 = (pack >> 10) & 0x1f;
        var12 = (var12 == 0x1d) ? 0 : (uint16_t)((var12 + 1) << 10);
        wr16(q + 0x30, (uint16_t)((pack & 0x83ff) | var12));
        var6 = rd16(q + 0x32 + 2 * var11);          /* waiting task id (m_hs1) */
    } else {
        var6 = 0xFFFFFFFFu;
    }

    wr16(q + 0x08, (uint16_t)var3);                 /* push3.m_h5 */
    wr16(q + 0x0A, (uint16_t)var5);                 /* push3.m_h6 */

    *out_signal = var6;
    return LFQ_OK;
}

/* ------------------------------------------------------------------ */
/* _cellSpursLFQueuePushBody(pQueue, buffer, isBlocking)               */
/* ------------------------------------------------------------------ */
static int32_t lfq_get_pop_pointer(uint8_t* q, int32_t* pointer, uint32_t isBlocking);
static int32_t lfq_complete_pop_pointer(uint8_t* q, int32_t pointer, uint32_t* out_signal);
uint64_t _cellSpursLFQueuePushBody(uint64_t queue, uint64_t buffer, uint64_t isBlocking)
{
    if (!queue || !buffer) return (uint64_t)(int64_t)LFQ_ERR_NULLPTR;
    if ((queue & 127) || (buffer & 15)) {
        lfq_align_fail(__func__, queue, buffer);
        return (uint64_t)(int64_t)LFQ_ERR_ALIGN;
    }

    uint8_t* q = lfq_p2h(queue);
    lfq_lock();

    const uint32_t direction = rd32(q + 0x24);
    if (direction == DIR_ANY2ANY) {
        /* v2 (ANY2ANY) push. RPCS3 leaves the v2 PPU push as TODO, but the
         * SPU-side v2 POP is fully lifted from GoW2's task-lib (spu1 0x9A10/
         * 0x9B28) and reads the same 3-phase pop1/push1 cursors at +0x00..0x0F
         * as v1 -- so drive the push through the v1 pointer protocol, mask
         * the v2 buffer-EA tag bit (|1 set at Initialize), and wake the
         * parked consumer via the taskset signalled bit. This is the channel
         * FIOS's dearchiver uses to hand inflate commands (32B) to the
         * edgezlib task; dropping these pushes left the whole asset pipeline
         * starved (manifest never inflated). */
        int32_t pos2 = 0;
        int32_t res2 = lfq_get_push_pointer(q, &pos2, (uint32_t)isBlocking);
        if (res2 != LFQ_OK) { lfq_unlock(); return (uint64_t)(int64_t)res2; }
        const int32_t depth2 = (int32_t)rd32(q + 0x14);
        const int32_t size2  = (int32_t)rd32(q + 0x10);
        const uint32_t slot2 = (uint32_t)((rd64(q + 0x18) & ~1ull) +
                               (uint32_t)size2 * (uint32_t)(pos2 >= depth2 ? pos2 - depth2 : pos2));
        memcpy(vm_base + slot2, vm_base + (uint32_t)buffer, (size_t)size2);
        uint32_t sig2 = 0xFFFFFFFFu;
        lfq_complete_push_pointer(q, pos2, &sig2);
        uint64_t eaSig2 = rd64(q + 0x70);
        lfq_unlock();
        /* QUEUE_PUSH (Task 4): emitted after lfq_unlock(), locals only
         * (pos2/size2 captured under lfq_lock() above) -- never re-enters
         * lfq's lock. */
        PS3_TRACE(PS3_TS_SPURS, "_cellSpursLFQueuePushBody", "QUEUE_PUSH",
                  "q=0x%08X pos=%d size=%d dir=any2any", (uint32_t)queue, pos2, size2);
        { static int n = 0;
          if (n < 16) { n++;
              const uint8_t* e = vm_base + (uint32_t)buffer;
              fprintf(stderr, "[LFQ] push2 q=0x%X pos=%d size=%d sig=%d data:",
                      (uint32_t)queue, pos2, size2, (int32_t)sig2);
              for (int i = 0; i < size2 && i < 32; i++) fprintf(stderr, " %02X", e[i]);
              fprintf(stderr, "\n"); } }
        /* Wake the consumer. m_eaSignal for this queue is the SPURS instance
         * (|1 tag), not the taskset, so a targeted SendSignal may miss; the
         * kick re-dispatches every registered task -- the parked edge task
         * re-runs, re-attaches and pops the now-present element. */
        /* RE probe: snapshot the element's pointees now and again 5 s later
         * (detached host thread) -- the diff shows what (if anything) the
         * consumer task wrote and which completion field the PPU poller is
         * still waiting on. Regenerable diagnostic. */
        if (lfq_trace_on()) { const uint8_t* e2 = vm_base + (uint32_t)buffer;   /* winport: mingw */
          static uint32_t s_eas[4];
          s_eas[0] = ((uint32_t)e2[0]<<24)|((uint32_t)e2[1]<<16)|((uint32_t)e2[2]<<8)|e2[3];
          s_eas[1] = ((uint32_t)e2[16]<<24)|((uint32_t)e2[17]<<16)|((uint32_t)e2[18]<<8)|e2[19];
          s_eas[2] = ((uint32_t)e2[20]<<24)|((uint32_t)e2[21]<<16)|((uint32_t)e2[22]<<8)|e2[23];
          s_eas[3] = 0;
          for (int t = 0; t < 3; t++) {
              uint32_t a = s_eas[t] & ~15u;
              if (!a || a > 0x4F000000u || !lfq_ea_readable(a, 48)) continue;
              const uint8_t* p = vm_base + a;
              fprintf(stderr, "[LFQ2PRE] 0x%08X:", a);
              for (int i = 0; i < 48; i++) fprintf(stderr, "%s%02X", (i%4)?"":" ", p[i]);
              fprintf(stderr, "%c", 10);
          }
#ifdef _WIN32
          { extern unsigned long __stdcall lfq2_probe_thread(void*);
            CreateThread(NULL, 0, lfq2_probe_thread, (void*)s_eas, 0, NULL); }
#endif
        }
        /* Host fulfiller: raw-inflate the command right here (the lifted v2
         * pop cannot see the v1-style push state yet) so the synchronous
         * dearchive waiter on the render thread completes. */
        { extern int lfq_host_inflate_try(const uint8_t* elem);
          /* E249 (2026-09-03): dois consumidores da mesma fila edgezlib (a task SPU liftada e este
           * host fulfiller) sao uma CORRIDA (ledger §9): quando o host ganha, o intro nao completa
           * (st620 preso em 3, EDGE=0); quando a task SPU ganha, completa (e130: EDGE=48, st620->0).
           * PS3_HOST_INFLATE=0 desliga o host e deixa a task SPU ser o consumidor unico. Default: ON
           * (comportamento actual) ate' o smoke confirmar a task SPU sozinha. */
          static int _hi=-1; if(_hi<0){ const char* _e=getenv("PS3_HOST_INFLATE"); _hi=(_e&&_e[0]=='0')?0:1; }
          int done2 = _hi ? lfq_host_inflate_try(vm_base + (uint32_t)buffer) : 0;
          /* E138 (2026-09-02): o host CONSUMIU o elemento -- avancar tambem o
           * lado pop (como o SPU faria). Sem isto a fila (depth 12) enchia e o
           * push bloqueante do mediathread do FIOS ficava preso para sempre em
           * lfq_get_push_pointer (medido com `sample`: dearchiver parado a
           * seguir aos 4 inflates do header do psarc). */
          if (done2) {
              lfq_lock();
              int32_t pp2 = 0;
              if (lfq_get_pop_pointer(q, &pp2, 0) == LFQ_OK) {
                  uint32_t sg2 = 0xFFFFFFFFu;
                  lfq_complete_pop_pointer(q, pp2, &sg2);
              }
              lfq_unlock();
          } }
        if (sig2 != 0xFFFFFFFFu && eaSig2 && eaSig2 < 0x100000000ull)
            cellSpursSendSignal((void*)(uintptr_t)(uint32_t)(eaSig2 & ~1ull), sig2);
        spurs_task_kick_all();
        return LFQ_OK;
    }
    if (direction != DIR_PPU2SPU) { lfq_unlock(); return (uint64_t)(int64_t)LFQ_ERR_PERM; }

    int32_t pos = 0;
    int32_t res = lfq_get_push_pointer(q, &pos, (uint32_t)isBlocking);
    if (res != LFQ_OK) { lfq_unlock(); return (uint64_t)(int64_t)res; }

    const int32_t depth = (int32_t)rd32(q + 0x14);
    const int32_t size  = (int32_t)rd32(q + 0x10);
    const uint32_t slot_ea = (uint32_t)((rd64(q + 0x18) & ~1ull) +
                             (uint32_t)size * (uint32_t)(pos >= depth ? pos - depth : pos));
    memcpy(vm_base + slot_ea, vm_base + (uint32_t)buffer, (size_t)size);

    uint32_t sig = 0xFFFFFFFFu;
    lfq_complete_push_pointer(q, pos, &sig);
    { extern volatile unsigned g_wp79_qline; g_wp79_qline = (uint32_t)queue; }   /* winport v79-spu-side */
    uint64_t eaSignal = rd64(q + 0x70);
    /* E164 (2026-09-02): PS3_TRACE_LFQHDR=1 -> os 0x80 bytes do cabecalho DEPOIS do push (as 3 primeiras
     * vezes e depois a cada 200), para comparar com o que o SPU le no GETLLAR (mesmo formato). */
    { static int on = -1; if (on < 0) { extern char* getenv(const char*); on = getenv("PS3_TRACE_LFQHDR") ? 1 : 0; }
      if (on) { static int k = 0; static int k_other = 0; k++;
        int other = ((uint32_t)queue != 0x47CA9B80u) && (k_other++ < 60);   /* filas raras: sempre (cap 60) */
        if (k <= 3 || (k % 200) == 0 || other) {
          fprintf(stderr, "[LFQHDR] ppu-push#%d q=0x%08X pos=%d sig=0x%X hdr:", k, (uint32_t)queue, pos, sig);
          for (int i = 0; i < 0x80; i++) fprintf(stderr, "%s%02X", (i % 16) ? "" : " ", q[i]);
          fprintf(stderr, " elem:"); for (int i = 0; i < (size < 0x60 ? size : 0x60); i++) fprintf(stderr, "%s%02X", (i % 16) ? "" : " ", vm_base[slot_ea + i]);
          fprintf(stderr, "\n"); } } }
    lfq_unlock();

    /* QUEUE_PUSH (Task 4): emitted after lfq_unlock(), locals only (pos/size
     * captured under lfq_lock() above) -- never re-enters lfq's lock. */
    PS3_TRACE(PS3_TS_SPURS, "_cellSpursLFQueuePushBody", "QUEUE_PUSH",
              "q=0x%08X pos=%d size=%d dir=ppu2spu", (uint32_t)queue, pos, size);

    { static int n = 0;
      n++;
      if (n <= 3) {
          /* Reliable caller ID: the guest-LR capture lands in rodata here
           * (the game reaches the push through a trampolined path), so walk
           * the HOST stack instead and print return addresses relative to the
           * module base -- map each with `nm boot_v2_new.exe` to the lifted
           * func_XXXX that actually pushes the 224B job. */
#ifdef _WIN32
          void* bt[24]; unsigned long h;
          unsigned long got = RtlCaptureStackBackTrace(0, 24, bt, &h);
          uintptr_t base = (uintptr_t)GetModuleHandleW(0);
          fprintf(stderr, "[LFQPUSHBT] push#%d q=0x%X size=%d base=0x%llX frames:",
                  n, (uint32_t)queue, size, (unsigned long long)base);
          for (unsigned long i = 0; i < got; i++)
              fprintf(stderr, " +0x%llX", (unsigned long long)((uintptr_t)bt[i] - base));
          fprintf(stderr, "\n");
#endif
      }
      if (n <= 40 || (lfq_trace_on() && (n % 200) == 0)) {   /* winport: mingw */
          extern __thread uint32_t g_last_hle_caller_guest[4];
          fprintf(stderr, "[LFQ] push#%d q=0x%X pos=%d size=%d sig=%d pop_h1=%u pop_h4=%u callers=%X/%X/%X/%X\n",
                  n, (uint32_t)queue, pos, size, (int32_t)sig,
                  rd16(q + 0x00), rd16(q + 0x06),
                  g_last_hle_caller_guest[0], g_last_hle_caller_guest[1],
                  g_last_hle_caller_guest[2], g_last_hle_caller_guest[3]);
          /* RE probe: completion-ring cursors (GoW2 job system, func_0017EA60:
           * TOC-0x3948..-0x393C = 0x53D830..0x53D83C = {consumerCntPtr,
           * ringPtr, producerCntPtr, nodeCursorPtr}) + head + node0. */
          { uint32_t a0 = rd32(vm_base + 0x53D830), a1 = rd32(vm_base + 0x53D834);
            uint32_t a2 = rd32(vm_base + 0x53D838), a3 = rd32(vm_base + 0x53D83C);
            fprintf(stderr, "[LFQRING] anch={%08X %08X %08X %08X} *={%08X %08X %08X %08X} head=%08X n0={%08X %08X %08X}\n",
                    a0, a1, a2, a3,
                    a0 && a0 < 0x50000000u && lfq_ea_readable(a0, 4) ? rd32(vm_base + a0) : 0xDEAD,
                    a1 && a1 < 0x50000000u && lfq_ea_readable(a1, 4) ? rd32(vm_base + a1) : 0xDEAD,
                    a2 && a2 < 0x50000000u && lfq_ea_readable(a2, 4) ? rd32(vm_base + a2) : 0xDEAD,
                    a3 && a3 < 0x50000000u && lfq_ea_readable(a3, 4) ? rd32(vm_base + a3) : 0xDEAD,
                    rd32(vm_base + 0x5733A0),
                    rd32(vm_base + 0x67D1F0), rd32(vm_base + 0x67D1F4),
                    rd32(vm_base + 0x67D1F0 + 0x118)); } } }
    /* RE aid: dump the first few job descriptors in full plus the structures
     * they point at (job-system HLE groundwork). */
    { static int nd = 0;
      if (nd < 3) {
          const uint8_t* e = vm_base + (uint32_t)buffer;
          fprintf(stderr, "[LFQJOB] q=0x%X entry %d (%d bytes):", (uint32_t)queue, nd, size);
          for (int32_t i = 0; i < size; i += 4) {
              if ((i & 31) == 0) fprintf(stderr, "\n[LFQJOB] +%02X:", i);
              fprintf(stderr, " %02X%02X%02X%02X", e[i], e[i+1], e[i+2], e[i+3]);
          }
          fprintf(stderr, "\n");
          /* pointees */
          struct { const char* name; uint32_t ea; int len; } tgt[8];
          int nt = 0;
          tgt[nt].name="obj0C";  tgt[nt].ea=rd32(e+0x0C); tgt[nt].len=192; nt++;
          tgt[nt].name="objA4";  tgt[nt].ea=rd32(e+0xA4); tgt[nt].len=96;  nt++;
          tgt[nt].name="stk10";  tgt[nt].ea=rd32(e+0x14); tgt[nt].len=48;  nt++;
          tgt[nt].name="stk30";  tgt[nt].ea=rd32(e+0x34); tgt[nt].len=48;  nt++;
          tgt[nt].name="stk5C";  tgt[nt].ea=rd32(e+0x5C); tgt[nt].len=96;  nt++;
          tgt[nt].name="bssAC";  tgt[nt].ea=rd32(e+0xAC); tgt[nt].len=48;  nt++;
          tgt[nt].name="bss1C";  tgt[nt].ea=rd32(e+0x1C); tgt[nt].len=48;  nt++;
          tgt[nt].name="stkB4";  tgt[nt].ea=rd32(e+0xB4); tgt[nt].len=48;  nt++;
          for (int t = 0; t < nt; t++) {
              if (!tgt[t].ea || tgt[t].ea > 0x50000000u ||
                  !lfq_ea_readable(tgt[t].ea, (uint32_t)tgt[t].len)) continue;
              const uint8_t* p = vm_base + tgt[t].ea;
              fprintf(stderr, "[LFQPTR] %s @0x%08X:", tgt[t].name, tgt[t].ea);
              for (int i = 0; i < tgt[t].len; i += 4) {
                  if ((i & 31) == 0) fprintf(stderr, "\n[LFQPTR]   +%02X:", i);
                  fprintf(stderr, " %02X%02X%02X%02X", p[i], p[i+1], p[i+2], p[i+3]);
              }
              fprintf(stderr, "\n[LFQPTR]   |");
              for (int i = 0; i < tgt[t].len; i++)
                  fputc((p[i] >= 32 && p[i] < 127) ? p[i] : '.', stderr);
              fprintf(stderr, "|\n");
          }
          /* Buffer probe: for each plausible EA anywhere in the element, sniff
           * for a zlib/deflate/edge header at its target -- locates the
           * compressed source and the dest for the host inflate. */
          for (int i = 0; i + 4 <= size; i += 4) {
              uint32_t a = rd32(e + i);
              if (a < 0x10000u || a >= 0x50000000u || !lfq_ea_readable(a, 8)) continue;
              const uint8_t* p = vm_base + a;
              uint8_t b0 = p[0], b1 = p[1];
              int zlib = (b0 == 0x78 && (b1==0x01||b1==0x9C||b1==0xDA||b1==0x5E));
              int edge = (b0=='E'||b0=='e'||(b0==0 && p[1]==0 && p[2]==0));
              if (zlib)
                  fprintf(stderr, "[LFQZ] elem+0x%02X -> 0x%08X ZLIB %02X%02X%02X%02X%02X%02X%02X%02X\n",
                          i, a, p[0],p[1],p[2],p[3],p[4],p[5],p[6],p[7]);
          }
          nd++;
      } }

    if (sig != 0xFFFFFFFFu)
        cellSpursSendSignal((void*)(uintptr_t)(uint32_t)eaSignal, sig);
    else
        spurs_task_kick_all();      /* data available: give consumers a run */

    /* PS3_EDGE_ACK experiment: GoW2's 224B queue elements are edgezlib-style
     * jobs whose pusher spin-waits on completion counters (u64 EAs at +0x10 /
     * +0x30 -- PPU stack addresses). Probe the contract: log the counters'
     * current values and optionally complete them immediately. */
    { static int ack = -1;
      if (ack < 0) ack = getenv("PS3_EDGE_ACK") ? 1 : 0;
      if (ack) {
          const uint8_t* e = vm_base + (uint32_t)buffer;
          uint32_t c1 = rd32(e + 0x14);       /* low word of u64 at +0x10 */
          uint32_t c2 = rd32(e + 0x34);       /* low word of u64 at +0x30 */
          static int n = 0;
          if (n < 24 && (c1 || c2)) {
              uint32_t v1 = c1 && lfq_ea_readable(c1, 4) ? rd32(vm_base + c1) : 0;
              uint32_t v2 = c2 && lfq_ea_readable(c2, 4) ? rd32(vm_base + c2) : 0;
              fprintf(stderr, "[EDGE] elem ctr1=0x%X(*=0x%X) ctr2=0x%X(*=0x%X)\n",
                      c1, v1, c2, v2);
              n++;
          }
          if (ack > 0) {
              if (c1 && c1 < 0x40000000u && lfq_ea_readable(c1, 4)) { uint8_t* p = vm_base + c1; p[0]=p[1]=p[2]=p[3]=0; }
              if (c2 && c2 < 0x40000000u && lfq_ea_readable(c2, 4)) { uint8_t* p = vm_base + c2; p[0]=p[1]=p[2]=p[3]=0; }
          }
      } }

    return LFQ_OK;
}

/* ------------------------------------------------------------------ */
/* Pop side (SPU2PPU), mirror of the push side.                       */
/* ------------------------------------------------------------------ */
static int32_t lfq_get_pop_pointer(uint8_t* q, int32_t* pointer, uint32_t isBlocking)
{
    t_wp76_lfq_yn = 0;   /* winport v76c-usleep-fast */
    const int32_t depth = (int32_t)rd32(q + 0x14);

    for (;;) {
        int32_t h4 = (int16_t)rd16(q + 0x06);       /* next pop seq  */
        int32_t h5 = rd16(q + 0x08);                /* push cursor   */
        int32_t var2 = h5 - h4;
        if (var2 < 0) var2 += depth * 2;

        if (var2 > 0) {                             /* data available */
            *pointer = h4;
            wr16(q + 0x06, (uint16_t)((h4 + 1 >= depth * 2) ? 0 : h4 + 1));
            return LFQ_OK;
        }
        if (!isBlocking) return LFQ_ERR_AGAIN;
        lfq_unlock(); lfq_yield(); lfq_lock();      /* empty: wait for producer */
    }
}

static int32_t lfq_complete_pop_pointer(uint8_t* q, int32_t pointer, uint32_t* out_signal)
{
    const int32_t depth = (int32_t)rd32(q + 0x14);

    int32_t h1 = rd16(q + 0x00), h2 = rd16(q + 0x02);   /* pop3 */

    int32_t var1 = pointer - h1;
    if (var1 < 0) var1 += depth * 2;

    int32_t var2 = (int16_t)rd16(q + 0x0E) - (int32_t)rd16(q + 0x08);   /* push h8 - h5 */
    if (var2 < 0) var2 += depth * 2;

    int32_t var9_ = 15 - var1;
    var9_ = (var9_ & 0x30) ? 0 : (1 << var9_);
    uint16_t nz = (uint16_t)~(var9_ | h2);
    int32_t var9 = nz ? (__builtin_clz((uint32_t)nz) - 16) : 16;

    int32_t var5 = h2 | var9_;
    if (var9 & 0x30) var5 = 0; else var5 <<= var9;

    int32_t var3 = h1 + var9;
    if (var3 >= depth * 2) var3 -= depth * 2;

    uint16_t pack = rd16(q + 0x50);                 /* pop2 */
    int32_t var4 = ((pack >> 10) & 0x1f) - ((pack >> 5) & 0x1f);
    if (var4 < 0) var4 += 0x1e;

    uint32_t var6;
    if (var2 + var4 <= 15 && ((pack >> 10) & 0x1f) != (pack & 0x1f)) {
        int32_t var11 = (pack >> 10) & 0x1f;
        if (var11 >= 15) var11 -= 15;
        uint16_t var12 = (pack >> 10) & 0x1f;
        var12 = (var12 == 0x1d) ? 0 : (uint16_t)((var12 + 1) << 10);
        wr16(q + 0x50, (uint16_t)((pack & 0x83ff) | var12));
        var6 = rd16(q + 0x52 + 2 * var11);          /* waiting task id (m_hs2) */
    } else {
        var6 = 0xFFFFFFFFu;
    }

    wr16(q + 0x00, (uint16_t)var3);                 /* pop3.m_h1 */
    wr16(q + 0x02, (uint16_t)var5);                 /* pop3.m_h2 */

    *out_signal = var6;
    return LFQ_OK;
}

uint64_t _cellSpursLFQueuePopBody(uint64_t queue, uint64_t buffer, uint64_t isBlocking)
{
    if (!queue || !buffer) return (uint64_t)(int64_t)LFQ_ERR_NULLPTR;
    if ((queue & 127) || (buffer & 15)) {
        lfq_align_fail(__func__, queue, buffer);
        return (uint64_t)(int64_t)LFQ_ERR_ALIGN;
    }

    uint8_t* q = lfq_p2h(queue);
    lfq_lock();

    const uint32_t direction = rd32(q + 0x24);
    if (direction == DIR_ANY2ANY) {
        lfq_unlock();
        fprintf(stderr, "[LFQ] PopBody on ANY2ANY queue 0x%X: v2 protocol not implemented\n",
                (uint32_t)queue);
        return (uint64_t)(int64_t)LFQ_ERR_AGAIN;
    }
    if (direction != DIR_SPU2PPU) { lfq_unlock(); return (uint64_t)(int64_t)LFQ_ERR_PERM; }

    int32_t pos = 0;
    int32_t res = lfq_get_pop_pointer(q, &pos, (uint32_t)isBlocking);
    if (res != LFQ_OK) { lfq_unlock(); return (uint64_t)(int64_t)res; }

    const int32_t depth = (int32_t)rd32(q + 0x14);
    const int32_t size  = (int32_t)rd32(q + 0x10);
    const uint32_t slot_ea = (uint32_t)((rd64(q + 0x18) & ~1ull) +
                             (uint32_t)size * (uint32_t)(pos >= depth ? pos - depth : pos));
    memcpy(vm_base + (uint32_t)buffer, vm_base + slot_ea, (size_t)size);

    uint32_t sig = 0xFFFFFFFFu;
    lfq_complete_pop_pointer(q, pos, &sig);
    uint64_t eaSignal = rd64(q + 0x70);
    lfq_unlock();

    /* QUEUE_POP (Task 4): emitted after lfq_unlock(), locals only (pos/size
     * captured under lfq_lock() above) -- never re-enters lfq's lock. */
    PS3_TRACE(PS3_TS_SPURS, "_cellSpursLFQueuePopBody", "QUEUE_POP",
              "q=0x%08X pos=%d size=%d", (uint32_t)queue, pos, size);

    if (sig != 0xFFFFFFFFu)
        cellSpursSendSignal((void*)(uintptr_t)(uint32_t)eaSignal, sig);

    return LFQ_OK;
}

/* ------------------------------------------------------------------ */
/* Attach/detach: we don't back the queue with an lv2 event queue (the
 * blocking paths above spin-yield instead), so these are accepted as-is. */
/* ------------------------------------------------------------------ */
uint64_t cellSpursLFQueueAttachLv2EventQueue(uint64_t queue)
{
    fprintf(stderr, "[LFQ] AttachLv2EventQueue q=0x%X\n", (uint32_t)queue);
    return LFQ_OK;
}

uint64_t cellSpursLFQueueDetachLv2EventQueue(uint64_t queue)
{
    (void)queue;
    return LFQ_OK;
}

uint64_t cellSpursLFQueueGetTasksetAddress(uint64_t queue, uint64_t taskset_out)
{
    if (!queue || !taskset_out) return (uint64_t)(int64_t)LFQ_ERR_NULLPTR;
    uint8_t* q = lfq_p2h(queue);
    wr32(lfq_p2h(taskset_out), (uint32_t)rd64(q + 0x70));
    return LFQ_OK;
}


#ifdef _WIN32
unsigned long __stdcall lfq2_probe_thread(void* arg)
{
    const uint32_t* eas = (const uint32_t*)arg;
    Sleep(5000);
    for (int t = 0; t < 3; t++) {
        uint32_t a = eas[t] & ~15u;
        if (!a || a > 0x4F000000u || !lfq_ea_readable(a, 48)) continue;
        const uint8_t* p = vm_base + a;
        fprintf(stderr, "[LFQ2POST] 0x%08X:", a);
        for (int i = 0; i < 48; i++) fprintf(stderr, "%s%02X", (i%4)?"":" ", p[i]);
        fprintf(stderr, "%c", 10);
    }
    fflush(stderr);
    return 0;
}
#endif
