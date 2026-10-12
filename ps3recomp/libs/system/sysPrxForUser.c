/*
 * ps3recomp - sysPrxForUser HLE implementation
 *
 * Real host-backed implementation: lwmutex uses CRITICAL_SECTION/pthread_mutex,
 * lwcond uses CONDITION_VARIABLE/pthread_cond, threads use CreateThread/pthread.
 * Heap uses standard malloc with tracking.
 */

#include "sysPrxForUser.h"

/* Contador de geracao do lwcond: o incremento ja' e' atomico, as leituras nao
 * eram. Relaxed chega -- o valor so' serve para detectar "mudou desde que eu
 * comecei a esperar", e a exclusao real vem do mutex do slot. */
#if defined(_MSC_VER)
#  define LWC_GEN_LD(p)  (*(p))
#else
#  define LWC_GEN_LD(p)  __atomic_load_n((long*)(p), __ATOMIC_RELAXED)
#endif
#include "ps3_checkpoint.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#ifdef __MINGW32__ /* winport: mingw */
#include <pthread.h>
#endif
#else
#include <pthread.h>
#include <unistd.h>
#include <sched.h>

/* PS3_WATCH_STORE: watch de escritas HOST em bloco (ppu_loader.cpp). */
extern void ps3_watch_store_bulk(u32 ea, u32 len, const char* who);
#endif

/* ---------------------------------------------------------------------------
 * Internal: Lightweight mutex table
 * -----------------------------------------------------------------------*/
#define MAX_LWMUTEX 256

typedef struct {
    int in_use;
    int recursive;
    pthread_t owner_thr; int owner_set;   /* E211: quem detem o slot real */
    char name[8];
    volatile long held;   /* lock depth; sys_lwmutex_destroy must report EBUSY
                           * while held (TryEnterCriticalSection is recursive and
                           * silently succeeds for the owning thread, so it cannot
                           * detect a self-held lock — games use destroy(EBUSY) as
                           * a "heap still alive / in use" probe). */
#ifdef _WIN32
    CRITICAL_SECTION cs;
#ifdef __MINGW32__ /* winport: real lwmutex on mingw */
    pthread_mutex_t mtx; pthread_cond_t lk_cv; int locked;
#endif
#else
    pthread_mutex_t mtx;      /* short internal guard (also the lwcond wait mutex) */
    pthread_cond_t  lk_cv;    /* waiters for the lock itself */
    int             locked;   /* lv2 lwmutex taken -- released by ANY thread */
#endif
} LwMutexSlot;

static LwMutexSlot s_lwmutex[MAX_LWMUTEX];
static u32 s_lwmutex_next = 0;

/* Guards slot allocation in the create paths now that guest threads are
 * real host threads. Lock/unlock/signal stay lock-free: they only touch
 * the slot the caller already owns. */
#ifdef _WIN32
static SRWLOCK s_slot_lock = SRWLOCK_INIT;
static void slot_lock(void)   { AcquireSRWLockExclusive(&s_slot_lock); }
static void slot_unlock(void) { ReleaseSRWLockExclusive(&s_slot_lock); }
#else
static pthread_mutex_t s_slot_lock = PTHREAD_MUTEX_INITIALIZER;
static void slot_lock(void)   { pthread_mutex_lock(&s_slot_lock); }
static void slot_unlock(void) { pthread_mutex_unlock(&s_slot_lock); }
#endif

/* Reset all lwmutex/lwcond state — call before CRT redirect to game main */
void sys_lwmutex_reset_all(void)
{
    for (u32 i = 0; i < MAX_LWMUTEX; i++) {
        if (s_lwmutex[i].in_use) {
#ifdef _WIN32
            DeleteCriticalSection(&s_lwmutex[i].cs);
#else
            pthread_mutex_destroy(&s_lwmutex[i].mtx);
#endif
        }
    }
    memset(s_lwmutex, 0, sizeof(s_lwmutex));
    s_lwmutex_next = 0;
}

/* ---------------------------------------------------------------------------
 * Internal: Lightweight cond table
 * -----------------------------------------------------------------------*/
#define MAX_LWCOND 256

typedef struct {
    int in_use;
    /* Guest EA of the associated sys_lwmutex_t_hle -- NOT a pre-resolved
     * s_lwmutex[] slot id. The real sys_lwcond_t only stores a pointer
     * (lwmutex_addr); resolving the slot id here at create time breaks if
     * the game creates the lwcond before (or racing) the paired lwmutex's
     * sys_lwmutex_create finishes writing its sleep_queue -- observed in
     * practice as lwmutex_id capturing pre-init garbage from the guest
     * memory (e.g. 0x1F000000), permanently failing every later wait with
     * CELL_ESRCH once actually exercised. Resolve lazily in wait/signal
     * instead, exactly like the real lwmutex_addr pointer field implies. */
    u32 lwmutex_guest_ea;
    /* Signal generation counter: closes the classic lost-wakeup window.
     * sys_lwcond_wait captures gen on entry; a signal that lands before the
     * host condvar registers the waiter still bumps gen, and the waiter
     * returns immediately instead of sleeping forever (timeout==0 means
     * INFINITE -- the FIOS "schedul" thread was one lost signal away from
     * never issuing another dearchive op). */
    volatile long gen;
    /* Diagnostic-only: cond name captured at create (PS3_LWCOND_TRACE). Lets
     * wait/signal name the exact cond a stuck thread is parked on. */
    char name[9];
#ifdef _WIN32
    CONDITION_VARIABLE cv;
    /* Private CS for the condvar sleep. Using the lwmutex slot's CS wedged
     * the waiter behind whoever held that CS via the sys_lwmutex_lock HLE
     * (observed: the fios scheduler stuck INSIDE sys_lwcond_wait for 39 s
     * at EnterCriticalSection). The guest's real mutual exclusion is its
     * user-space lwmutex fast path; this CS only exists to give
     * SleepConditionVariableCS an owned lock. */
    CRITICAL_SECTION cvcs;
    int cvcs_init;
#else
    pthread_cond_t cv;
#endif
} LwCondSlot;

static LwCondSlot s_lwcond[MAX_LWCOND];
static u32 s_lwcond_next = 0;

/* ---------------------------------------------------------------------------
 * Internal: Thread table
 * -----------------------------------------------------------------------*/
#define MAX_PRX_THREADS 64

typedef struct {
    int in_use;
    u64 thread_id;
    sys_ppu_thread_entry_t entry;
    u64 arg;
    u64 exitcode;
    int joined;
    int detached;
#ifdef _WIN32
    HANDLE handle;
#else
    pthread_t pt;
#endif
} PrxThreadSlot;

static PrxThreadSlot s_threads[MAX_PRX_THREADS];
static u64 s_next_thread_id = 0x10000; /* start high to avoid collision with sys_ppu_thread */

#ifdef _WIN32
static DWORD WINAPI prx_thread_entry(LPVOID param)
{
    PrxThreadSlot* t = (PrxThreadSlot*)param;
    t->entry(t->arg);
    return 0;
}
#else
static void* prx_thread_entry(void* param)
{
    PrxThreadSlot* t = (PrxThreadSlot*)param;
    t->entry(t->arg);
    return NULL;
}
#endif

/* ---------------------------------------------------------------------------
 * Internal: Heap table
 * -----------------------------------------------------------------------*/
#define MAX_HEAPS 16

typedef struct {
    int in_use;
    u32 id;
} HeapSlot;

static HeapSlot s_heaps[MAX_HEAPS];
static u32 s_next_heap_id = 1;

/* ---------------------------------------------------------------------------
 * Thread management
 *
 * Thread functions (sys_ppu_thread_create, _exit, _join, _detach, _get_id,
 * _yield) are implemented in runtime/syscalls/sys_ppu_thread.c to avoid
 * duplication. They are declared in sysPrxForUser.h and linked from the
 * syscall implementation.
 * -----------------------------------------------------------------------*/

/* ---------------------------------------------------------------------------
 * Process management
 * -----------------------------------------------------------------------*/

void sys_process_exit(s32 exitcode)
{
    printf("[sysPrxForUser] sys_process_exit(code=%d)\n", exitcode);
    /* Task 2 (bringup-instrumentacao-fase0): SHUTDOWN -- encerramento
     * ordenado, se ocorrer. Registado ANTES do exit() (que termina o
     * processo sem retornar) para garantir que a linha [PS3T] chega ao
     * stderr; ps3_trace_emit ja faz fwrite+fflush numa so' chamada (G1). */
    ps3_checkpoint_fire(PS3_CP_SHUTDOWN, "sys_process_exit", "code=%d", exitcode);
    exit(exitcode);
}

s32 sys_process_getpid(void)
{
    return 1001;
}

s32 sys_process_get_number_of_object(u32 object_type, u32* count)
{
    (void)object_type;
    if (count) *count = 0;
    return CELL_OK;
}

s32 sys_process_is_spu_lock_line_reservation_address(u32 addr, u64 flags)
{
    (void)addr; (void)flags;
    return CELL_OK;
}

/* ---------------------------------------------------------------------------
 * String/memory functions
 * -----------------------------------------------------------------------*/

s32 _sys_printf(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    printf("[PS3] ");
    int ret = vprintf(fmt, ap);
    va_end(ap);
    return ret;
}

s32 _sys_sprintf(char* buf, const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int ret = vsprintf(buf, fmt, ap);
    va_end(ap);
    return ret;
}

s32 _sys_snprintf(char* buf, u32 size, const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int ret = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return ret;
}

s32 _sys_strlen(const char* str)
{
    if (!str) return 0;
    return (s32)strlen(str);
}

s32 _sys_strncpy(char* dst, const char* src, u32 size)
{
    if (!dst || !src) return CELL_EFAULT;
    strncpy(dst, src, size);
    return CELL_OK;
}

s32 _sys_strcat(char* dst, const char* src)
{
    if (!dst || !src) return CELL_EFAULT;
    strcat(dst, src);
    return CELL_OK;
}

s32 _sys_strcmp(const char* s1, const char* s2)
{
    if (!s1 || !s2) return -1;
    return strcmp(s1, s2);
}

/* PS3_WATCH_STORE: estes dois escrevem DIRECTAMENTE em memoria guest (o `dst`
 * ja vem traduzido para ponteiro host) e nunca passam por vm_write*, logo sao
 * invisiveis aos dois watches de store. Converter de volta a EA e reportar
 * quando o destino cai dentro do mapa guest. Gated, OFF por default. */
void ps3_watch_store_bulk(uint32_t ea, uint32_t len, const char* who); /* winport: mingw */
static void yz_watch_store_guest(void* dst, u32 size, const char* who)
{
    extern u8* vm_base;
    extern u32 ppu_vm_size;
    if (!size || !vm_base || !ppu_vm_size) return;
    if ((u8*)dst < vm_base || (u8*)dst >= vm_base + ppu_vm_size) return;
    ps3_watch_store_bulk((u32)((u8*)dst - vm_base), size, who);
}

void* _sys_memset(void* dst, s32 val, u32 size)
{
    void* r = memset(dst, val, size);
    yz_watch_store_guest(dst, size, "_sys_memset");
    return r;
}

void* _sys_memcpy(void* dst, const void* src, u32 size)
{
    void* r = memcpy(dst, src, size);
    yz_watch_store_guest(dst, size, "_sys_memcpy");
    return r;
}

s32 _sys_memcmp(const void* s1, const void* s2, u32 size)
{
    return memcmp(s1, s2, size);
}

s32 _sys_toupper(s32 c) { return toupper(c); }
s32 _sys_tolower(s32 c) { return tolower(c); }

/* ---------------------------------------------------------------------------
 * Lightweight mutex
 * -----------------------------------------------------------------------*/

extern u8* vm_base;  /* for guest-address diagnostics in the boot log */
#define YZ_GUEST_ADDR(p) ((u32)((u8*)(p) - vm_base))

s32 sys_lwmutex_create(sys_lwmutex_t_hle* lwmutex, const sys_lwmutex_attribute_t* attr)
{
    printf("[sysPrxForUser] sys_lwmutex_create(name='%.8s', guest=0x%08X)\n",
           attr ? attr->name : "???", YZ_GUEST_ADDR(lwmutex));
    fprintf(stderr, "[LWM] create guest=0x%08X lwmutex_ptr=%p\n", (unsigned)YZ_GUEST_ADDR(lwmutex), (void*)lwmutex); fflush(stderr);

    if (!lwmutex)
        return CELL_EFAULT;

    slot_lock();
    u32 idx = s_lwmutex_next;
    for (u32 i = 0; i < MAX_LWMUTEX; i++) {
        u32 slot = (idx + i) % MAX_LWMUTEX;
        if (!s_lwmutex[slot].in_use) {
            LwMutexSlot* m = &s_lwmutex[slot];
            m->in_use = 1;
            m->recursive = (attr && (attr->recursive & SYS_SYNC_RECURSIVE)) ? 1 : 0;
            if (attr)
                memcpy(m->name, attr->name, 8);

#ifdef _WIN32
            InitializeCriticalSection(&m->cs);
#ifdef __MINGW32__
            pthread_mutex_init(&m->mtx, NULL);
            pthread_cond_init(&m->lk_cv, NULL);
            m->locked = 0; m->owner_set = 0; m->held = 0;
#endif
#else
            pthread_mutexattr_t mattr;
            pthread_mutexattr_init(&mattr);
            /* The guard is never held across a critical section any more
             * (ownership is m->locked), so it is never recursive. */
#ifdef __APPLE__
            /* lv2 lwmutexes queue waiters and hand the lock over on unlock.
             * Darwin's default FIRSTFIT lets the unlocker re-grab it at once:
             * measured 2026-09-22, the main thread starved a SPURS worker on
             * slot 41 for 40 s+. FAIRSHARE gives the lwmutex handoff. */
            pthread_mutexattr_setpolicy_np(&mattr, PTHREAD_MUTEX_POLICY_FAIRSHARE_NP);
#endif
            pthread_mutex_init(&m->mtx, &mattr);
            pthread_mutexattr_destroy(&mattr);
            pthread_cond_init(&m->lk_cv, NULL);
            m->locked = 0; m->owner_set = 0; m->held = 0;
#endif

            memset(lwmutex, 0, sizeof(*lwmutex));
            lwmutex->sleep_queue = slot + 1; /* 1-based ID */
            s_lwmutex_next = (slot + 1) % MAX_LWMUTEX;
            slot_unlock();
            return CELL_OK;
        }
    }
    slot_unlock();
    return CELL_EAGAIN;
}

/* ---------------------------------------------------------------------------
 * sys_lwmutex_alloc_real_slot
 *
 * The ctx-handler that actually services sys_lwmutex_create's NID
 * (runtime/ppu/ppu_sysprx.cpp, registered via ps3_hle_register_ctx which
 * takes priority over this file's sys_lwmutex_create) only maintains guest-
 * side bookkeeping and never populates this table's real CRITICAL_SECTION/
 * pthread_mutex_t slots. That leaves sys_lwcond_wait's lwmutex_id always
 * pointing at an empty slot (CELL_ESRCH), so the game's real sys_lwcond_wait
 * calls fail immediately and spin retrying without blocking. This lets the
 * ctx-handler allocate a REAL slot (host synchronization object, no guest
 * pointer needed) so sys_lwcond_wait has something valid to wait on. Returns
 * 1-based slot id, or 0 on exhaustion. */
s32 sys_lwmutex_alloc_real_slot(int recursive)
{
    slot_lock();
    u32 idx = s_lwmutex_next;
    for (u32 i = 0; i < MAX_LWMUTEX; i++) {
        u32 slot = (idx + i) % MAX_LWMUTEX;
        if (!s_lwmutex[slot].in_use) {
            LwMutexSlot* m = &s_lwmutex[slot];
            m->in_use = 1;
            m->recursive = recursive ? 1 : 0;
            m->held = 0;
#ifdef _WIN32
            InitializeCriticalSection(&m->cs);
#ifdef __MINGW32__
            pthread_mutex_init(&m->mtx, NULL);
            pthread_cond_init(&m->lk_cv, NULL);
            m->locked = 0; m->owner_set = 0; m->held = 0;
#endif
#else
            pthread_mutexattr_t mattr;
            pthread_mutexattr_init(&mattr);
            /* The guard is never held across a critical section any more
             * (ownership is m->locked), so it is never recursive. */
#ifdef __APPLE__
            /* lv2 lwmutexes queue waiters and hand the lock over on unlock.
             * Darwin's default FIRSTFIT lets the unlocker re-grab it at once:
             * measured 2026-09-22, the main thread starved a SPURS worker on
             * slot 41 for 40 s+. FAIRSHARE gives the lwmutex handoff. */
            pthread_mutexattr_setpolicy_np(&mattr, PTHREAD_MUTEX_POLICY_FAIRSHARE_NP);
#endif
            pthread_mutex_init(&m->mtx, &mattr);
            pthread_mutexattr_destroy(&mattr);
            pthread_cond_init(&m->lk_cv, NULL);
            m->locked = 0; m->owner_set = 0; m->held = 0;
#endif
            s_lwmutex_next = (slot + 1) % MAX_LWMUTEX;
            slot_unlock();
            return (s32)(slot + 1);
        }
    }
    slot_unlock();
    return 0;
}

/* Counterpart to sys_lwmutex_alloc_real_slot: releases a real slot allocated
 * for a guest lwmutex that's genuinely being destroyed (not the heap-alive
 * EBUSY probe, which must NOT free the slot). No-op for slot_id==0 (pool was
 * exhausted at create time, or the caller never allocated a real slot). */
void sys_lwmutex_free_real_slot(s32 slot_id)
{
    if (slot_id <= 0 || (u32)(slot_id - 1) >= MAX_LWMUTEX) return;
    u32 slot = (u32)(slot_id - 1);
    slot_lock();
    if (s_lwmutex[slot].in_use) {
#ifdef _WIN32
        DeleteCriticalSection(&s_lwmutex[slot].cs);
#else
        pthread_mutex_destroy(&s_lwmutex[slot].mtx);
#endif
        s_lwmutex[slot].in_use = 0;
    }
    slot_unlock();
}

/* E211 (2026-09-02): exclusao mutua REAL por slot (id 1-based em lwmutex+0x10), usada pelos handlers
 * ctx de runtime/ppu/ppu_sysprx.cpp (ate' aqui um stub sem exclusao). Bloquear com o giant lock na
 * mao poria em deadlock quem detem o lwmutex e espera pelo giant: tenta; se ocupado, larga o giant,
 * bloqueia (com aviso a cada 2 s), readquire com prioridade. */
static int lwm_trace_on(void) { static int on = -1; if (on < 0) on = getenv("PS3_LWM_TRACE") ? 1 : 0; return on; }
#define LWMT(...) do { if (lwm_trace_on()) { fprintf(stderr, __VA_ARGS__); fflush(stderr); } } while (0)
#if !defined(_WIN32) || defined(__MINGW32__)
/* lv2 lwmutex semantics on POSIX (2026-09-23). Ownership is m->locked +
 * owner_thr, guarded by the short m->mtx; waiters sleep on m->lk_cv. Any
 * thread may release it: GoW2 locks and unlocks the same lwmutex from
 * different PPU threads (0x00884D90 main<->thread 9; slot 41
 * snd_stream_service_thread / syn_tick_timer_thread). A pthread mutex
 * cannot be unlocked by a non-owner, so the slot used to stay locked for
 * good after such an unlock -> SCREAM stalled (no game audio) or the main
 * thread froze on the slot. */
static int lwm_take(u32 slot, int try_only, int depth)
{
    LwMutexSlot* m = &s_lwmutex[slot];
    pthread_mutex_lock(&m->mtx);
    if (m->locked && m->owner_set && pthread_equal(m->owner_thr, pthread_self())) {
        m->held += depth > 0 ? depth : 1;
        pthread_mutex_unlock(&m->mtx);
        return CELL_OK;
    }
    if (m->locked) {
        if (try_only) { pthread_mutex_unlock(&m->mtx); return CELL_EBUSY; }
        extern void ppu_giant_lock_release(void); extern void ppu_giant_lock_acquire_prio(void);
        /* Never sleep with the giant lock: the holder needs it to reach its unlock. */
        pthread_mutex_unlock(&m->mtx);
        ppu_giant_lock_release();
        pthread_mutex_lock(&m->mtx);
        while (m->locked) pthread_cond_wait(&m->lk_cv, &m->mtx);
        m->locked = 1; m->owner_thr = pthread_self(); m->owner_set = 1; m->held = depth > 0 ? depth : 1;
        pthread_mutex_unlock(&m->mtx);
        ppu_giant_lock_acquire_prio();
        return CELL_OK;
    }
    m->locked = 1; m->owner_thr = pthread_self(); m->owner_set = 1; m->held = depth > 0 ? depth : 1;
    pthread_mutex_unlock(&m->mtx);
    return CELL_OK;
}
/* Full release by whoever calls it (owner or not); wakes one waiter. */
static void lwm_give_locked(LwMutexSlot* m)
{
    m->locked = 0; m->owner_set = 0; m->held = 0;
    pthread_cond_signal(&m->lk_cv);
}
#endif
s32 sys_lwmutex_slot_lock(s32 id, int try_only)
{
    u32 slot = (u32)id - 1u;
    if (slot >= MAX_LWMUTEX || !s_lwmutex[slot].in_use) return CELL_ESRCH;
#if !defined(_WIN32) || defined(__MINGW32__)
    s32 rc = lwm_take(slot, try_only, 0);
    if (rc == CELL_OK)
        LWMT("[LWMT] lock   slot=%u %.8s held=%ld thr=%p\n", slot, s_lwmutex[slot].name, (long)s_lwmutex[slot].held, (void*)pthread_self());
    return rc;
#else
    return CELL_OK;
#endif
}
s32 sys_lwmutex_slot_unlock(s32 id)
{
    u32 slot = (u32)id - 1u;
    if (slot >= MAX_LWMUTEX || !s_lwmutex[slot].in_use) return CELL_ESRCH;
#if !defined(_WIN32) || defined(__MINGW32__)
    LwMutexSlot* m = &s_lwmutex[slot];
    pthread_mutex_lock(&m->mtx);
    if (!m->locked) { pthread_mutex_unlock(&m->mtx); return CELL_OK; }
    const int mine = m->owner_set && pthread_equal(m->owner_thr, pthread_self());
    if (mine && m->held > 1) { m->held--; pthread_mutex_unlock(&m->mtx); return CELL_OK; }
    if (!mine) {
        static int n = 0; if (n++ < 12) {
            char me[64] = "", own[64] = "";
            pthread_getname_np(pthread_self(), me, sizeof me);
            if (m->owner_set) pthread_getname_np(m->owner_thr, own, sizeof own);
            fprintf(stderr, "[LWM] unlock por outra thread (posse passada, como no lv2): slot %u held=%ld dono='%s' quem='%s'\n",
                    slot, (long)m->held, own, me); }
    }
    lwm_give_locked(m);
    pthread_mutex_unlock(&m->mtx);
    LWMT("[LWMT] unlock slot=%u %.8s thr=%p\n", slot, s_lwmutex[slot].name, (void*)pthread_self());
#endif
    return CELL_OK;
}
static int lwm_slot_release_if_mine(u32 mslot)
{
#if !defined(_WIN32) || defined(__MINGW32__)
    if (mslot < MAX_LWMUTEX && s_lwmutex[mslot].in_use) {
        LwMutexSlot* m = &s_lwmutex[mslot];
        pthread_mutex_lock(&m->mtx);
        if (m->locked && m->owner_set && pthread_equal(m->owner_thr, pthread_self())) {
            int depth = (int)m->held;
            lwm_give_locked(m);
            pthread_mutex_unlock(&m->mtx);
            LWMT("[LWMT] cw-rel slot=%u %.8s depth=%d thr=%p\n", mslot, m->name, depth, (void*)pthread_self());
            return depth > 0 ? depth : 1;
        }
        pthread_mutex_unlock(&m->mtx);
    }
#endif
    (void)mslot; return 0;
}
static void lwm_slot_reacquire(u32 mslot, int depth)
{
#if !defined(_WIN32) || defined(__MINGW32__)
    if (!depth) return;
    lwm_take(mslot, 0, depth);
    LWMT("[LWMT] cw-reacq slot=%u %.8s depth=%d thr=%p\n", mslot, s_lwmutex[mslot].name, depth, (void*)pthread_self());
#else
    (void)mslot; (void)depth;
#endif
}
s32 sys_lwmutex_lock(sys_lwmutex_t_hle* lwmutex, u64 timeout)
{
    { extern void ppu_blockmark_set(const char*, unsigned long long);
      ppu_blockmark_set("lwmutex_lock", (unsigned long long)(uintptr_t)lwmutex); }

    (void)timeout;
    if (!lwmutex) return CELL_EFAULT;

    u32 slot = lwmutex->sleep_queue - 1;
    if (slot >= MAX_LWMUTEX || !s_lwmutex[slot].in_use) {
#ifdef _WIN32
        printf("[sysPrxForUser] sys_lwmutex_lock FAIL guest=0x%08X "
               "sleep_queue=0x%08X (%s) [host tid %lu] -> ESRCH\n",
               YZ_GUEST_ADDR(lwmutex), lwmutex->sleep_queue,
               slot >= MAX_LWMUTEX ? "bad slot" : "slot not in use",
               GetCurrentThreadId());
#else
        printf("[sysPrxForUser] sys_lwmutex_lock FAIL guest=0x%08X "
               "sleep_queue=0x%08X (%s) -> ESRCH\n",
               YZ_GUEST_ADDR(lwmutex), lwmutex->sleep_queue,
               slot >= MAX_LWMUTEX ? "bad slot" : "slot not in use");
#endif
        return CELL_ESRCH;
    }

#ifdef _WIN32
    EnterCriticalSection(&s_lwmutex[slot].cs);
    InterlockedIncrement(&s_lwmutex[slot].held);
#else
    lwm_take(slot, 0, 0);
#endif

    lwmutex->lock_var = 1;
    lwmutex->recursive_count++;
    return CELL_OK;
}

s32 sys_lwmutex_trylock(sys_lwmutex_t_hle* lwmutex)
{
    if (!lwmutex) return CELL_EFAULT;

    u32 slot = lwmutex->sleep_queue - 1;
    if (slot >= MAX_LWMUTEX || !s_lwmutex[slot].in_use)
        return CELL_ESRCH;

#ifdef _WIN32
    if (!TryEnterCriticalSection(&s_lwmutex[slot].cs))
        return CELL_EBUSY;
    InterlockedIncrement(&s_lwmutex[slot].held);
#else
    if (lwm_take(slot, 1, 0) != CELL_OK)
        return CELL_EBUSY;
#endif

    lwmutex->lock_var = 1;
    lwmutex->recursive_count++;
    return CELL_OK;
}

s32 sys_lwmutex_unlock(sys_lwmutex_t_hle* lwmutex)
{
    if (!lwmutex) return CELL_EFAULT;

    u32 slot = lwmutex->sleep_queue - 1;
    if (slot >= MAX_LWMUTEX || !s_lwmutex[slot].in_use)
        return CELL_ESRCH;

    lwmutex->recursive_count--;
    if (lwmutex->recursive_count == 0)
        lwmutex->lock_var = 0;

#ifdef _WIN32
    if (s_lwmutex[slot].held > 0) InterlockedDecrement(&s_lwmutex[slot].held);
    LeaveCriticalSection(&s_lwmutex[slot].cs);
#else
    sys_lwmutex_slot_unlock((s32)slot + 1);
#endif
    return CELL_OK;
}

s32 sys_lwmutex_destroy(sys_lwmutex_t_hle* lwmutex)
{
#ifdef _WIN32
    printf("[sysPrxForUser] sys_lwmutex_destroy(guest=0x%08X) [host tid %lu]\n",
           lwmutex ? YZ_GUEST_ADDR(lwmutex) : 0, GetCurrentThreadId());
#else
    printf("[sysPrxForUser] sys_lwmutex_destroy(guest=0x%08X)\n",
           lwmutex ? YZ_GUEST_ADDR(lwmutex) : 0);
#endif

    if (!lwmutex) return CELL_EFAULT;

    u32 slot = lwmutex->sleep_queue - 1;
    if (slot >= MAX_LWMUTEX || !s_lwmutex[slot].in_use) {
        printf("[sysPrxForUser] sys_lwmutex_destroy -> ESRCH\n");
        return CELL_ESRCH;   /* already destroyed / never created */
    }

    /* lv2 refuses to destroy a held lwmutex (CELL_EBUSY) - games rely on
     * this when tearing down a heap another thread is still allocating
     * from: the EBUSY keeps the lock (and the heap) alive. The explicit
     * `held` counter catches a lock held by the *calling* thread, which
     * TryEnterCriticalSection cannot (it re-enters recursively and reports
     * success). GoW2's heap malloc uses destroy(EBUSY) as a liveness probe. */
    if (s_lwmutex[slot].held > 0) {
        printf("[sysPrxForUser] sys_lwmutex_destroy -> EBUSY (held=%ld)\n", s_lwmutex[slot].held);
        return CELL_EBUSY;
    }
    /* TEST: GoW2 heap-alive probe — destroy(heap_mutex) is expected to return
     * EBUSY while the heap is live. Heap lwmutexes live in 0x880000-0x882000. */
    { extern char* getenv(const char*); static int t=-1; if(t<0)t=getenv("PS3_HEAP_EBUSY")?1:0;
      if(t){ unsigned int ga = lwmutex ? (unsigned)YZ_GUEST_ADDR(lwmutex) : 0;
        if(ga>=0x880000u && ga<0x882000u){ printf("[sysPrxForUser] sys_lwmutex_destroy -> EBUSY (heap-probe ga=0x%X)\n", ga); return CELL_EBUSY; } } }
#ifdef _WIN32
    if (!TryEnterCriticalSection(&s_lwmutex[slot].cs)) {
        printf("[sysPrxForUser] sys_lwmutex_destroy -> EBUSY\n");
        return CELL_EBUSY;
    }
    LeaveCriticalSection(&s_lwmutex[slot].cs);
    DeleteCriticalSection(&s_lwmutex[slot].cs);
#else
    pthread_mutex_lock(&s_lwmutex[slot].mtx);
    if (s_lwmutex[slot].locked) { pthread_mutex_unlock(&s_lwmutex[slot].mtx); return CELL_EBUSY; }
    pthread_mutex_unlock(&s_lwmutex[slot].mtx);
    pthread_mutex_destroy(&s_lwmutex[slot].mtx);
    pthread_cond_destroy(&s_lwmutex[slot].lk_cv);
#endif
    s_lwmutex[slot].in_use = 0;

    memset(lwmutex, 0, sizeof(*lwmutex));
    printf("[sysPrxForUser] sys_lwmutex_destroy -> OK\n");
    return CELL_OK;
}

/* ---------------------------------------------------------------------------
 * Lightweight condition variable
 * -----------------------------------------------------------------------*/

s32 sys_lwcond_create(sys_lwcond_t_hle* lwcond, sys_lwmutex_t_hle* lwmutex,
                      const sys_lwcond_attribute_t* attr)
{
    /* lwcond/lwmutex/attr are guest EAs (raw 32-bit values from ctx->gpr,
     * implicitly cast to pointer type by the generic HLE call path) -- a raw
     * host deref faults. Translate via vm_base before touching guest memory. */
    sys_lwcond_t_hle*         g_lwcond  = lwcond  ? (sys_lwcond_t_hle*)(vm_base + (uintptr_t)lwcond)   : NULL;
    sys_lwmutex_t_hle*        g_lwmutex = lwmutex ? (sys_lwmutex_t_hle*)(vm_base + (uintptr_t)lwmutex) : NULL;
    const sys_lwcond_attribute_t* g_attr = attr   ? (const sys_lwcond_attribute_t*)(vm_base + (uintptr_t)attr) : NULL;

    printf("[sysPrxForUser] sys_lwcond_create(name='%.8s')\n",
           g_attr ? g_attr->name : "???");

    if (!g_lwcond || !g_lwmutex)
        return CELL_EFAULT;

    slot_lock();
    u32 idx = s_lwcond_next;
    for (u32 i = 0; i < MAX_LWCOND; i++) {
        u32 slot = (idx + i) % MAX_LWCOND;
        if (!s_lwcond[slot].in_use) {
            LwCondSlot* c = &s_lwcond[slot];
            c->in_use = 1;
            c->lwmutex_guest_ea = (u32)(uintptr_t)lwmutex;  /* resolved lazily, see LwCondSlot comment */
            { int j; for (j = 0; j < 8; j++) c->name[j] = (g_attr && g_attr->name[j]) ? g_attr->name[j] : 0; c->name[8] = 0; }

#ifdef _WIN32
            InitializeConditionVariable(&c->cv);
#else
            pthread_cond_init(&c->cv, NULL);
#endif

            /* The guest reads this struct big-endian and validates it (GoW2's
             * FIOS prints "signal of invalid cond" and stalls when the fields
             * look empty): store the lwmutex EA + queue id byte-swapped. */
            { extern void vm_write32(unsigned long long a, unsigned int v);
              unsigned int cond_ea = (unsigned int)(uintptr_t)lwcond;
              vm_write32(cond_ea + 0, (unsigned int)(uintptr_t)lwmutex);
              vm_write32(cond_ea + 4, slot + 1); }
            s_lwcond_next = (slot + 1) % MAX_LWCOND;
            slot_unlock();
            return CELL_OK;
        }
    }
    slot_unlock();
    return CELL_EAGAIN;
}

/* Guest EAs arriving here are untrusted: a signal against an lwcond living in
 * a page the guest never committed (observed: the movie-audio worker signaling
 * through a not-yet-initialized field after its first processed block) would
 * fault on the deref. Accept only committed, readable pages. */
static int lwc_ea_readable(uintptr_t ea)
{
    if (!ea || ea > 0xFFFFFFF0ull) return 0;
#ifdef _WIN32
    { extern int ppu_guest_range_committed(uint32_t addr, uint32_t n);   /* winport: mingw */
      if (ppu_guest_range_committed((uint32_t)ea, 4u)) return 1; }   /* registered commit: no page walk */
    {
        MEMORY_BASIC_INFORMATION mi;
        if (!VirtualQuery(vm_base + ea, &mi, sizeof mi)) return 0;
        if (mi.State != MEM_COMMIT) return 0;
        if (mi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return 0;
    }
#endif
    return 1;
}

s32 sys_lwcond_signal(sys_lwcond_t_hle* lwcond)
{
    if (!lwcond) return CELL_EFAULT;
    if (!lwc_ea_readable((uintptr_t)lwcond)) {
        static int k = 0;
        if (k++ < 8) {
            fprintf(stderr, "[LWCS] signal EA INVALIDO 0x%llX (nao-committado)\n",
                    (unsigned long long)(uintptr_t)lwcond);
            fflush(stderr);
        }
        return CELL_EFAULT;
    }
    sys_lwcond_t_hle* g_lwcond = (sys_lwcond_t_hle*)(vm_base + (uintptr_t)lwcond);

    /* slot_lock guards s_lwcond[] against a concurrent create/destroy on
     * another thread (create/destroy are the only mutators of .in_use / .cv
     * lifetime); Wake* is non-blocking so holding the lock here is safe. */
    slot_lock();
    u32 slot = __builtin_bswap32(g_lwcond->lwcond_queue) - 1;  /* stored BE by create */
    if (slot >= MAX_LWCOND || !s_lwcond[slot].in_use) {
        slot_unlock();
        return CELL_ESRCH;
    }
    { extern char* getenv(const char*); static int o=-1; if(o<0)o=getenv("PS3_TRACE_ALLOC")?1:0;
      if(o){ static int k=0; if(k++<400) fprintf(stderr,"[LWCS] signal lwcond=0x%X slot=%u%c",(unsigned)(uintptr_t)((u8*)lwcond),slot,10); } }
    { extern char* getenv(const char*); static int o=-1; if(o<0)o=getenv("PS3_LWCOND_TRACE")?1:0;
      if(o){ extern __thread uint32_t g_last_hle_caller_guest[4];
        { uint32_t mea = s_lwcond[slot].lwmutex_guest_ea; const uint8_t* mp = vm_base + mea;
          uint32_t own = ((uint32_t)mp[0]<<24)|((uint32_t)mp[1]<<16)|((uint32_t)mp[2]<<8)|mp[3];
          uint32_t rc  = ((uint32_t)mp[12]<<24)|((uint32_t)mp[13]<<16)|((uint32_t)mp[14]<<8)|mp[15];
          const uint8_t* sp = vm_base + 0x43009270u + 0x208u; uint32_t head = ((uint32_t)sp[0]<<24)|((uint32_t)sp[1]<<16)|((uint32_t)sp[2]<<8)|sp[3];
          fprintf(stderr,"[LWTRACE] SIGNAL     cond='%s' slot=%u gen=%ld caller=0x%X mtx=0x%X owner=0x%X rc=%u sched+208=0x%X\n",
            s_lwcond[slot].name, slot, (long)s_lwcond[slot].gen, g_last_hle_caller_guest[0], mea, own, rc, head); fflush(stderr); } } }
#ifdef _WIN32
    InterlockedIncrement(&s_lwcond[slot].gen);
    WakeConditionVariable(&s_lwcond[slot].cv);
#else
    __sync_fetch_and_add((long*)&s_lwcond[slot].gen, 1);
    pthread_cond_signal(&s_lwcond[slot].cv);
#endif
    slot_unlock();
    return CELL_OK;
}

s32 sys_lwcond_signal_all(sys_lwcond_t_hle* lwcond)
{
    if (!lwcond) return CELL_EFAULT;
    if (!lwc_ea_readable((uintptr_t)lwcond)) return CELL_EFAULT;
    sys_lwcond_t_hle* g_lwcond = (sys_lwcond_t_hle*)(vm_base + (uintptr_t)lwcond);

    slot_lock();
    u32 slot = __builtin_bswap32(g_lwcond->lwcond_queue) - 1;  /* stored BE by create */
    if (slot >= MAX_LWCOND || !s_lwcond[slot].in_use) {
        slot_unlock();
        return CELL_ESRCH;
    }
    { extern char* getenv(const char*); static int o=-1; if(o<0)o=getenv("PS3_TRACE_ALLOC")?1:0;
      if(o){ static int k=0; if(k++<400) fprintf(stderr,"[LWCSA] signal_all lwcond=0x%X slot=%u%c",(unsigned)(uintptr_t)((u8*)lwcond),slot,10); } }
    { extern char* getenv(const char*); static int o=-1; if(o<0)o=getenv("PS3_LWCOND_TRACE")?1:0;
      if(o){ extern __thread uint32_t g_last_hle_caller_guest[4];
        fprintf(stderr,"[LWTRACE] SIGNAL-ALL cond='%s' slot=%u gen=%ld caller=0x%X\n",
          s_lwcond[slot].name, slot, (long)s_lwcond[slot].gen, g_last_hle_caller_guest[0]); fflush(stderr); } }
#ifdef _WIN32
    InterlockedIncrement(&s_lwcond[slot].gen);
    WakeAllConditionVariable(&s_lwcond[slot].cv);
#else
    __sync_fetch_and_add((long*)&s_lwcond[slot].gen, 1);
    pthread_cond_broadcast(&s_lwcond[slot].cv);
#endif
    slot_unlock();
    return CELL_OK;
}

s32 sys_lwcond_wait(sys_lwcond_t_hle* lwcond, u64 timeout)
{
    { extern void ppu_blockmark_set(const char*, unsigned long long);
      ppu_blockmark_set("lwcond_wait", (unsigned long long)(uintptr_t)lwcond); }
    /* PS3_TRACE_MOVIEPOLL: the GoW2 movie-player is a pool of threads that
     * re-wait on cond-vars with a ~250ms timeout, waking to poll the decoder
     * state. Log those waits + a host backtrace (map via nm) to find the
     * movie-player poll functions and, from them, the "intro done" state. */
#ifdef _WIN32
    { static int on=-1; if(on<0){extern char* getenv(const char*); on=getenv("PS3_TRACE_MOVIEPOLL")?1:0;}
      if(on && timeout && timeout <= 300000ull) {   /* short polled waits only */
          static int k=0; if(k<24){ k++;
              void* bt[20]; unsigned long h; unsigned long got=RtlCaptureStackBackTrace(0,20,bt,&h);
              uintptr_t mb=(uintptr_t)GetModuleHandleW(0);
              fprintf(stderr,"[MOVIEPOLL] cond=0x%08X to=%lluus bt:",
                      (unsigned)(uintptr_t)((u8*)lwcond),(unsigned long long)timeout);
              for(unsigned long i=0;i<got;i++) fprintf(stderr," +0x%llX",(unsigned long long)((uintptr_t)bt[i]-mb));
              fprintf(stderr,"\n"); fflush(stderr);
          } }
    }
#endif
    if (!lwcond) return CELL_EFAULT;
    sys_lwcond_t_hle* g_lwcond = (sys_lwcond_t_hle*)(vm_base + (uintptr_t)lwcond);

    /* Validate under the lock, but release it before the blocking wait below
     * (SleepConditionVariableCS/pthread_cond_wait can block indefinitely --
     * holding a global lock across that would stall every other lwmutex/
     * lwcond operation on every other thread, and would deadlock against
     * sys_lwcond_signal's own slot_lock if the signaler runs first). */
    slot_lock();
    u32 cslot = __builtin_bswap32(g_lwcond->lwcond_queue) - 1;  /* stored BE by create */
    if (cslot >= MAX_LWCOND || !s_lwcond[cslot].in_use) {
        { extern char* getenv(const char*); static int o=-1; if(o<0)o=getenv("PS3_TRACE_ALLOC")?1:0;
          if(o){ static uint32_t seen[32]; static int nseen=0; uint32_t key=(unsigned)(uintptr_t)((u8*)lwcond);
            int dup=0; for(int i=0;i<nseen;i++) if(seen[i]==key){dup=1;break;}
            if(!dup && nseen<30){ seen[nseen++]=key; fprintf(stderr,"[LWCW-FAIL] lwcond=0x%X lwcond_queue=%u cslot=%u (>=MAX_LWCOND=%d or !in_use) MAX_LWCOND_val=%d in_use=%d\n",
              (unsigned)(uintptr_t)((u8*)lwcond), g_lwcond->lwcond_queue, cslot, cslot>=MAX_LWCOND, MAX_LWCOND, (cslot<MAX_LWCOND)?s_lwcond[cslot].in_use:-1); } } }
        slot_unlock();
        return CELL_ESRCH;
    }
    /* Resolve the paired lwmutex's real slot NOW (lazily), not from a value
     * captured back at lwcond_create time -- the mutex's sys_lwmutex_create
     * may run after (or concurrently with) the lwcond's, so a slot id cached
     * at create time can be pre-init garbage. See LwCondSlot comment. */
    u32 lwmutex_ea = s_lwcond[cslot].lwmutex_guest_ea;
    sys_lwmutex_t_hle* g_mtx = (sys_lwmutex_t_hle*)(vm_base + (uintptr_t)lwmutex_ea);
    /* sleep_queue is written via vm_write32 (proper BE) by ppu_sysprx.cpp's
     * sys_lwmutex_create ctx-handler, which takes priority over this file's
     * own sys_lwmutex_create -- so a plain struct-field read here (native/LE
     * byte order) reinterprets the real, valid, small slot id (e.g. 41) as
     * that byte shifted into the top of the word (0x29000000), which is
     * always >= MAX_LWMUTEX and permanently fails CELL_ESRCH. Byte-swap on
     * read to match how the value was actually stored. */
    u32 mslot = __builtin_bswap32(g_mtx->sleep_queue) - 1;
    if (mslot >= MAX_LWMUTEX || !s_lwmutex[mslot].in_use) {
        { extern char* getenv(const char*); static int o=-1; if(o<0)o=getenv("PS3_TRACE_ALLOC")?1:0;
          if(o){ static uint32_t seen[32]; static int nseen=0; uint32_t key=(unsigned)(uintptr_t)((u8*)lwcond);
            int dup=0; for(int i=0;i<nseen;i++) if(seen[i]==key){dup=1;break;}
            extern __thread uint32_t g_last_hle_caller_guest[4];
            if(!dup && nseen<30){ seen[nseen++]=key;
              uint32_t* words = (uint32_t*)(vm_base + (uintptr_t)lwmutex_ea);
              fprintf(stderr,"[LWCW-FAIL2] lwcond=0x%X cslot=%u lwmutex_ea=0x%X sleep_queue=%u mslot=%u (>=MAX_LWMUTEX=%d) in_use=%d guest_stack_candidates=0x%X,0x%X,0x%X,0x%X\n",
              (unsigned)(uintptr_t)((u8*)lwcond), cslot, lwmutex_ea, g_mtx->sleep_queue, mslot, mslot>=MAX_LWMUTEX, (mslot<MAX_LWMUTEX)?s_lwmutex[mslot].in_use:-1,
              g_last_hle_caller_guest[0], g_last_hle_caller_guest[1], g_last_hle_caller_guest[2], g_last_hle_caller_guest[3]);
              fprintf(stderr,"[LWCW-FAIL2-DUMP] lwmutex struct @0x%X (BE words, -8..+32): ", lwmutex_ea);
              for (int wi = -2; wi < 8; wi++) fprintf(stderr,"%+d:0x%08X ", wi*4, __builtin_bswap32(words[wi]));
              fprintf(stderr,"\n"); } } }
        slot_unlock();
        return CELL_ESRCH;
    }
    { extern char* getenv(const char*); static int o=-1; if(o<0)o=getenv("PS3_TRACE_ALLOC")?1:0;
      if(o){ static int k=0; if(k++<600) fprintf(stderr,"[LWCW-OK] lwcond=0x%X cslot=%u mslot=%u\n",(unsigned)(uintptr_t)((u8*)lwcond), cslot, mslot); } }
    /* Leituras do contador de geracao passam a ser atomicas: o incremento ja'
     * era (__sync_fetch_and_add), mas ler em texto simples do outro lado
     * continua a ser data race -- apanhado pelo TSan. */
    long gen0 = LWC_GEN_LD(&s_lwcond[cslot].gen);
    /* Trace only INFINITE waits (timeout==0): the SPURS/FIOS scheduler conds we
     * care about. Skips the 250ms movie-poll spam (slots 6-12) that dominates
     * overhead and perturbs the pre-B71 timing race. */
    { extern char* getenv(const char*); static int o=-1; if(o<0)o=getenv("PS3_LWCOND_TRACE")?1:0;
      if(o && timeout==0){ extern __thread uint32_t g_last_hle_caller_guest[4];
        { const uint8_t* mp = vm_base + lwmutex_ea;
          uint32_t own = ((uint32_t)mp[0]<<24)|((uint32_t)mp[1]<<16)|((uint32_t)mp[2]<<8)|mp[3];
          uint32_t rc  = ((uint32_t)mp[12]<<24)|((uint32_t)mp[13]<<16)|((uint32_t)mp[14]<<8)|mp[15];
          const uint8_t* sp = vm_base + 0x43009270u + 0x208u; uint32_t head = ((uint32_t)sp[0]<<24)|((uint32_t)sp[1]<<16)|((uint32_t)sp[2]<<8)|sp[3];
          fprintf(stderr,"[LWTRACE] WAIT-ENTER-MTX mtx=0x%X owner=0x%X rc=%u sched+208=0x%X\n", lwmutex_ea, own, rc, head); }
        fprintf(stderr,"[LWTRACE] WAIT-ENTER cond='%s' slot=%u gen=%ld caller=0x%X,0x%X\n",
          s_lwcond[cslot].name, cslot, gen0,
          g_last_hle_caller_guest[0], g_last_hle_caller_guest[1]); fflush(stderr); } }
    /* Real lwcond protocol: wait ATOMICALLY RELEASES the caller's lwmutex and
     * RE-ACQUIRES it before returning. Our wait previously left the guest
     * lwmutex struct untouched: the producer could never take the mutex (so
     * it never signalled -- the original lost-signal), and on return the
     * caller's unlock found inconsistent state ("attempt to unlock invalid
     * mutex '(null)'"), after which the woken FIOS mediathread guest-spun
     * FOREVER holding the giant lock (watchdog: same holder at +45s/+90s)
     * freezing every other thread. Fast-path layout (SDK/RPCS3):
     * lwmutex+0x00 = owner (BE u32, 0xFFFFFFFF = free), +0x04 = waiter. */
    uint32_t lw_owner_entry = 0;
    { uint8_t* lp = vm_base + (uintptr_t)lwmutex_ea;
      lw_owner_entry = ((uint32_t)lp[0]<<24)|((uint32_t)lp[1]<<16)|((uint32_t)lp[2]<<8)|lp[3];
      /* Diag (one-shot, 8 lines): a wait by a thread that does not own the
       * lwmutex. lv2/RPCS3 refuse it with EPERM; here it frees the real
       * owner's mutex and, after the wake, hands the caller an ownership it
       * never had -- the slot-41 "unlock por NAO-dono" freeze (2026-09-23). */
      { extern uint32_t ps3_trace_thread(void);
        uint32_t me = ps3_trace_thread();
        if (!me || me == 0x4D41494Eu) me = 1u;   /* main: trace tag 'MAIN', lwmutex owner id 1 */
        if (lw_owner_entry != me) {
            static int n = 0;
            if (n++ < 8) {
#ifndef _WIN32
                char nm[64] = ""; pthread_getname_np(pthread_self(), nm, sizeof nm);
#else
                const char* nm = "?";
#endif
                fprintf(stderr, "[LWC] wait sem ser dono: lwmutex=0x%08X owner=0x%08X me=0x%08X thr='%s' mslot=%u\n",
                        (unsigned)lwmutex_ea, lw_owner_entry, me, nm, mslot);
                fflush(stderr);
            }
        } }
      lp[0]=0xFF; lp[1]=0xFF; lp[2]=0xFF; lp[3]=0xFF; }               /* owner -> FREE */
    /* E233 (2026-09-03): a espera larga o lwmutex por completo -- na consola o recursive_count
     * e' guardado e posto a 0 (e reposto ao acordar). Sem isto, com exclusao real, o contador
     * guest acumulava os locks de cada thread que esperou (1->2->3), o proximo dono via rc>1 no
     * unlock e nunca largava o slot real -> as threads acordadas ficavam presas no re-lock. */
    uint32_t lw_rc_entry = 0;
    { uint8_t* rp = vm_base + (uintptr_t)lwmutex_ea + 0x0C;
      lw_rc_entry = ((uint32_t)rp[0]<<24)|((uint32_t)rp[1]<<16)|((uint32_t)rp[2]<<8)|rp[3];
      rp[0]=rp[1]=rp[2]=rp[3]=0; }
#ifdef _WIN32
    /* Init the private condvar CS here, under slot_lock: the lazy init in
     * the wait body raced when several threads (the 3 fios mediathreads
     * share one cond) hit it together -- InitializeCriticalSection on a CS
     * another thread was SLEEPING on corrupts it and the next Enter never
     * returns (observed: every lwcond waiter wedging ~28 s in). */
    if (!s_lwcond[cslot].cvcs_init) {
        InitializeCriticalSection(&s_lwcond[cslot].cvcs);
        s_lwcond[cslot].cvcs_init = 1;
    }
#endif
    slot_unlock();

    /* Release the PPU giant lock (ppu_loader.cpp) across the genuinely
     * blocking wait, or every other PPU thread (including whichever one
     * would signal this condvar) stalls until this wait times out. */
    int lwm_depth = lwm_slot_release_if_mine(mslot);   /* E211: a espera larga o lwmutex REAL */
    extern void ppu_giant_lock_release(void);
    extern void ppu_giant_lock_acquire(void);
    ppu_giant_lock_release();

#ifdef _WIN32
    /* Slice the wait and re-check the signal generation each pass: a signal
     * that fires before the host condvar registers this waiter bumps gen and
     * gets caught on the next slice instead of being lost forever. PROVEN
     * live: the "fios scheduler" thread sat in an INFINITE wait on a cond
     * that had received 5 signals ([BLOCK] + [LWCS] traces) -- every
     * dearchive op starved until process teardown. Also enter the slot CS
     * around the sleep: SleepConditionVariableCS requires an OWNED critical
     * section (the guest's lwmutex fast-path never touches the host CS).
     * (An earlier fps regression blamed on this was actually the emutls
     * cost of the g_trampoline_fn TLS fix -- reverting this didn't restore
     * fps, so the slices are not the bottleneck.) */
    {
        DWORD total_ms = (timeout == 0) ? 0xFFFFFFFF : (DWORD)(timeout / 1000);
        DWORD waited = 0;
        int rc_ok = 0;
        { extern void ppu_blockmark_set(const char*, unsigned long long);
          ppu_blockmark_set("lwc-cvcs", cslot); }
        EnterCriticalSection(&s_lwcond[cslot].cvcs);
        { extern void ppu_blockmark_set(const char*, unsigned long long);
          ppu_blockmark_set("lwc-loop", cslot); }
        for (;;) {
            if (LWC_GEN_LD(&s_lwcond[cslot].gen) != gen0) { rc_ok = 1; break; }
            DWORD step = 100;
            if (total_ms != 0xFFFFFFFF && total_ms - waited < step)
                step = total_ms - waited;
            if (step == 0) break;
            SleepConditionVariableCS(&s_lwcond[cslot].cv,
                                     &s_lwcond[cslot].cvcs, step);
            if (LWC_GEN_LD(&s_lwcond[cslot].gen) != gen0) { rc_ok = 1; break; }
            /* NOTE: an earlier build returned a spurious wakeup here for
             * infinite waits. With the guest-lwmutex release/re-acquire
             * protocol now implemented (the producer can finally TAKE the
             * mutex and signal natively), the spurious lie is retired: a
             * woken FIOS mediathread treated "wait returned OK" as "work
             * arrived", found none, and guest-spun forever holding the
             * giant lock. Infinite waits now genuinely wait, in 100 ms
             * slices that re-check the signal generation. */
            if (total_ms != 0xFFFFFFFF) {
                waited += step;
                if (waited >= total_ms) break;
            }
        }
        LeaveCriticalSection(&s_lwcond[cslot].cvcs);
        { extern void ppu_blockmark_set(const char*, unsigned long long);
          ppu_blockmark_set("lwc-giant", cslot); }
        { extern void ppu_giant_lock_acquire_prio(void);
          ppu_giant_lock_acquire_prio(); }
        lwm_slot_reacquire(mslot, lwm_depth);   /* E211 */
        /* Re-acquire the guest lwmutex for the caller: CAS FREE -> the owner
         * id it held at entry (the value the SDK fast-path compares against).
         * Yield the giant while contending so the current holder can run. */
        { volatile uint32_t* lp = (volatile uint32_t*)(vm_base + (uintptr_t)lwmutex_ea);
          uint32_t be_owner = __builtin_bswap32(lw_owner_entry);
          int tries = 0;
          for (;;) {
              uint32_t cur = *lp;
              if (cur == 0xFFFFFFFFu &&
                  __sync_bool_compare_and_swap((uint32_t*)lp, 0xFFFFFFFFu, be_owner))
                  break;
              if (cur == be_owner) break;            /* already ours */
              if (++tries > 20000) break;            /* refuse to wedge */
              ppu_giant_lock_release();
              Sleep(0);
              { extern void ppu_giant_lock_acquire_prio(void);
                ppu_giant_lock_acquire_prio(); }
          } }
        { uint8_t* rp = vm_base + (uintptr_t)lwmutex_ea + 0x0C;   /* E233: repor recursive_count */
          rp[0]=(uint8_t)(lw_rc_entry>>24); rp[1]=(uint8_t)(lw_rc_entry>>16); rp[2]=(uint8_t)(lw_rc_entry>>8); rp[3]=(uint8_t)lw_rc_entry; }
        { extern void ppu_blockmark_set(const char*, unsigned long long);
          ppu_blockmark_set("after-lwcond", cslot); }
        if (!rc_ok && timeout != 0)
            return CELL_ETIMEDOUT;
    }
#else
    /* Re-acquire with PRIORITY (mirror Win32 acquire_prio): the FIOS
     * scheduler wakes here and must beat the intro poll re-acquirer so it
     * can write [op+0x90]. Without prio, macOS handed the mutex back to the
     * hot poller and done90 never met the state-1 poll.
     *
     * Also yield once after the cond wait returns, before taking the giant
     * lock: pthread_cond_wait re-acquires the slot mutex with the giant
     * still free, but the woken FIOS thread often still loses the very next
     * giant acquire to a spinning poller unless we open a real window. */
    /* Gen-sliced wait (mirror the Win32 branch above). A plain pthread_cond_wait
     * loses a signal whose pthread_cond_signal fires before this thread parks on
     * the cv: the wakeup is dropped AND the bumped gen is never re-checked, so
     * the waiter wedges forever. Observed as the SPURS scheduler worker threads
     * (guest func_00463598, 9 of them) stuck on an already-signalled cond →
     * frontend freeze at the Bluepoint logo, initial menu never loads. Hold the
     * paired host mutex, then slice pthread_cond_timedwait and re-check gen each
     * pass so a pre/mid-wait signal is always caught within one slice. */
    {
        pthread_mutex_lock(&s_lwmutex[mslot].mtx);
        int rc_ok = 0;
        uint64_t left_us = timeout;                 /* 0 => infinite */
        while (LWC_GEN_LD(&s_lwcond[cslot].gen) == gen0) {
            uint64_t slice_us = 50000;              /* 50 ms */
            if (timeout != 0) {
                if (left_us == 0) break;
                if (left_us < slice_us) slice_us = left_us;
            }
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_nsec += (long)(slice_us * 1000);
            ts.tv_sec  += ts.tv_nsec / 1000000000L;
            ts.tv_nsec %= 1000000000L;
            pthread_cond_timedwait(&s_lwcond[cslot].cv, &s_lwmutex[mslot].mtx, &ts);
            if (timeout != 0) left_us = (left_us > slice_us) ? (left_us - slice_us) : 0;
        }
        if (s_lwcond[cslot].gen != gen0) rc_ok = 1;
        pthread_mutex_unlock(&s_lwmutex[mslot].mtx);
        { extern void ppu_giant_lock_acquire_prio(void);
          ppu_giant_lock_acquire_prio(); }
        lwm_slot_reacquire(mslot, lwm_depth);   /* E211 */
        /* Re-acquire the guest lwmutex for the caller (CAS FREE -> the owner id
         * held at entry), mirroring the Win32 branch. The old POSIX path skipped
         * this and left the mutex FREE (owner 0xFFFFFFFF, set above), so the
         * guest's next unlock found inconsistent state ("attempt to unlock
         * invalid mutex '(null)'") and downstream logic (intro FIOS scheduler,
         * SPURS workers) wedged. */
        { extern void ppu_giant_lock_release(void), ppu_giant_lock_acquire_prio(void);
          extern int sched_yield(void);
          volatile uint32_t* lp = (volatile uint32_t*)(vm_base + (uintptr_t)lwmutex_ea);
          uint32_t be_owner = __builtin_bswap32(lw_owner_entry); int tries = 0;
          for (;;) {
              uint32_t cur = *lp;
              if (cur == 0xFFFFFFFFu &&
                  __sync_bool_compare_and_swap((uint32_t*)lp, 0xFFFFFFFFu, be_owner)) break;
              if (cur == be_owner) break;          /* already ours */
              if (++tries > 20000) break;          /* refuse to wedge */
              ppu_giant_lock_release(); sched_yield(); ppu_giant_lock_acquire_prio();
          } }
        if (!rc_ok && timeout != 0)
            return CELL_ETIMEDOUT;
    }
#endif

    { extern char* getenv(const char*); static int o=-1; if(o<0)o=getenv("PS3_LWCOND_TRACE")?1:0;
      if(o && timeout==0){ fprintf(stderr,"[LWTRACE] WAIT-EXIT  cond='%s' slot=%u gen=%ld\n",
        s_lwcond[cslot].name, cslot, (long)s_lwcond[cslot].gen); fflush(stderr); } }
    return CELL_OK;
}

s32 sys_lwcond_destroy(sys_lwcond_t_hle* lwcond)
{
    printf("[sysPrxForUser] sys_lwcond_destroy()\n");

    if (!lwcond) return CELL_EFAULT;
    sys_lwcond_t_hle* g_lwcond = (sys_lwcond_t_hle*)(vm_base + (uintptr_t)lwcond);

    slot_lock();
    u32 slot = __builtin_bswap32(g_lwcond->lwcond_queue) - 1;  /* stored BE by create */
    if (slot < MAX_LWCOND && s_lwcond[slot].in_use) {
#ifndef _WIN32
        pthread_cond_destroy(&s_lwcond[slot].cv);
#endif
        s_lwcond[slot].in_use = 0;
    }
    slot_unlock();

    memset(g_lwcond, 0, sizeof(*g_lwcond));
    return CELL_OK;
}

/* ---------------------------------------------------------------------------
 * Heap management (wraps malloc)
 * -----------------------------------------------------------------------*/

s32 sys_heap_create_heap(sys_heap_t* heap, u32 start_addr, u32 size,
                          u32 flags, void* alloc_func, void* free_func)
{
    (void)start_addr; (void)size; (void)flags;
    (void)alloc_func; (void)free_func;

    printf("[sysPrxForUser] sys_heap_create_heap(size=%u)\n", size);

    if (!heap) return CELL_EFAULT;

    for (int i = 0; i < MAX_HEAPS; i++) {
        if (!s_heaps[i].in_use) {
            s_heaps[i].in_use = 1;
            s_heaps[i].id = s_next_heap_id++;
            *heap = s_heaps[i].id;
            return CELL_OK;
        }
    }
    return CELL_ENOMEM;
}

s32 sys_heap_destroy_heap(sys_heap_t heap)
{
    printf("[sysPrxForUser] sys_heap_destroy_heap(id=%u)\n", heap);

    for (int i = 0; i < MAX_HEAPS; i++) {
        if (s_heaps[i].in_use && s_heaps[i].id == heap) {
            s_heaps[i].in_use = 0;
            return CELL_OK;
        }
    }
    return CELL_ESRCH;
}

void* sys_heap_malloc(sys_heap_t heap, u32 size)
{
    (void)heap;
    return malloc(size);
}

s32 sys_heap_free(sys_heap_t heap, void* ptr)
{
    (void)heap;
    free(ptr);
    return CELL_OK;
}

void* sys_heap_memalign(sys_heap_t heap, u32 align, u32 size)
{
    (void)heap;
#ifdef _WIN32
    return _aligned_malloc(size, align);
#else
    void* ptr = NULL;
    if (posix_memalign(&ptr, align, size) != 0)
        return NULL;
    return ptr;
#endif
}

/* ---------------------------------------------------------------------------
 * PRX utilities
 * -----------------------------------------------------------------------*/

s32 sys_prx_exitspawn_with_level(void)
{
    printf("[sysPrxForUser] sys_prx_exitspawn_with_level() - no-op\n");
    return CELL_OK;
}

s32 sys_prx_get_module_id_by_name(const char* name, u64 flags, u32* id)
{
    (void)flags;
    printf("[sysPrxForUser] sys_prx_get_module_id_by_name('%s')\n",
           name ? name : "(null)");

    if (!id) return CELL_EFAULT;
    *id = 0; /* fake module ID */
    return CELL_OK;
}

/* ---------------------------------------------------------------------------
 * Random number generation
 *
 * Used by many games for seeding RNG, crypto operations, UUID generation.
 * We use the host OS PRNG for quality random data.
 * -----------------------------------------------------------------------*/

s32 sys_get_random_number(void* buf, u64 size)
{
    if (!buf || size == 0) return CELL_EFAULT;

#ifdef _WIN32
    /* Use BCryptGenRandom on Windows */
    #include <bcrypt.h>
    NTSTATUS status = BCryptGenRandom(NULL, (PUCHAR)buf, (ULONG)size,
                                       BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (status != 0) {
        /* Fallback: fill with simple pseudo-random */
        u8* p = (u8*)buf;
        for (u64 i = 0; i < size; i++)
            p[i] = (u8)(rand() & 0xFF);
    }
#else
    /* Use /dev/urandom on Unix */
    FILE* f = fopen("/dev/urandom", "rb");
    if (f) {
        fread(buf, 1, (size_t)size, f);
        fclose(f);
    } else {
        u8* p = (u8*)buf;
        for (u64 i = 0; i < size; i++)
            p[i] = (u8)(rand() & 0xFF);
    }
#endif

    return CELL_OK;
}

/* ---------------------------------------------------------------------------
 * Console I/O (debug)
 * -----------------------------------------------------------------------*/

s32 console_putc(s32 ch)
{
    fputc(ch, stderr);
    return CELL_OK;
}

s32 console_getc(void)
{
    return -1; /* no input */
}

s32 console_write(const void* buf, u32 len)
{
    if (buf && len > 0)
        fwrite(buf, 1, len, stderr);
    return CELL_OK;
}

/* ---------------------------------------------------------------------------
 * Process info
 * -----------------------------------------------------------------------*/

s32 sys_process_get_paramsfo(void* buf)
{
    /* PARAM.SFO data — return a minimal valid SFO with title ID */
    if (!buf) return CELL_EFAULT;
    memset(buf, 0, 256);
    return CELL_OK;
}
