/*
 * ps3recomp - Semaphore syscalls (implementation)
 */

#include "sys_semaphore.h"
#include "../memory/vm.h"
#include "ps3_trace.h"   /* Task 4 (bringup-instrumentacao-fase0): subsistema THREADS */
#include <string.h>
#include <stdlib.h>

/* ---------------------------------------------------------------------------
 * Globals
 * -----------------------------------------------------------------------*/
sys_semaphore_info g_sys_semaphores[SYS_SEMAPHORE_MAX];

#ifdef _WIN32
static CRITICAL_SECTION s_sem_table_lock;
static int              s_sem_table_lock_init = 0;
#else
static pthread_mutex_t  s_sem_table_lock = PTHREAD_MUTEX_INITIALIZER;
#endif

static void sem_table_lock(void)
{
#ifdef _WIN32
    if (!s_sem_table_lock_init) {
        InitializeCriticalSection(&s_sem_table_lock);
        s_sem_table_lock_init = 1;
    }
    EnterCriticalSection(&s_sem_table_lock);
#else
    pthread_mutex_lock(&s_sem_table_lock);
#endif
}

static void sem_table_unlock(void)
{
#ifdef _WIN32
    LeaveCriticalSection(&s_sem_table_lock);
#else
    pthread_mutex_unlock(&s_sem_table_lock);
#endif
}

static void write_be32(uint32_t addr, uint32_t val)
{
    uint32_t* p = (uint32_t*)vm_to_host(addr);
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__ || defined(_WIN32)
    val = ((val >> 24) & 0xFF) | ((val >> 8) & 0xFF00) |
          ((val <<  8) & 0xFF0000) | ((val << 24) & 0xFF000000u);
#endif
    *p = val;
}

/* ---------------------------------------------------------------------------
 * sys_semaphore_create
 *
 * r3 = pointer to receive semaphore ID (u32*)
 * r4 = pointer to attribute struct
 * r5 = initial value
 * r6 = max value
 * -----------------------------------------------------------------------*/
int64_t sys_semaphore_create(ppu_context* ctx)
{
    uint32_t id_out_addr  = LV2_ARG_PTR(ctx, 0);
    uint32_t attr_addr    = LV2_ARG_PTR(ctx, 1);
    int32_t  initial      = LV2_ARG_S32(ctx, 2);
    int32_t  max_val      = LV2_ARG_S32(ctx, 3);

    if (max_val <= 0 || initial < 0 || initial > max_val)
        return (int64_t)(int32_t)CELL_EINVAL;

    sem_table_lock();

    int slot = -1;
    for (int i = 0; i < SYS_SEMAPHORE_MAX; i++) {
        if (!g_sys_semaphores[i].active) { slot = i; break; }
    }
    if (slot < 0) {
        sem_table_unlock();
        return (int64_t)(int32_t)CELL_EAGAIN;
    }

    sys_semaphore_info* s = &g_sys_semaphores[slot];
    memset(s, 0, sizeof(*s));
    s->active    = 1;
    s->value     = initial;
    s->max_value = max_val;

    if (attr_addr != 0) {
        uint8_t* attr_raw = (uint8_t*)vm_to_host(attr_addr);
        uint32_t proto_be;
        memcpy(&proto_be, attr_raw, 4);
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__ || defined(_WIN32)
        proto_be = ((proto_be >> 24) & 0xFF) | ((proto_be >> 8) & 0xFF00) |
                   ((proto_be << 8) & 0xFF0000) | ((proto_be << 24) & 0xFF000000u);
#endif
        s->protocol = proto_be;
        /* name at offset 8 */
        memcpy(s->name, attr_raw + 8, 8);
    }

#ifdef _WIN32
    s->sem_handle = CreateSemaphoreA(NULL, initial, max_val, NULL);
    InitializeCriticalSection(&s->value_lock);
    if (s->sem_handle == NULL) {
        s->active = 0;
        sem_table_unlock();
        return (int64_t)(int32_t)CELL_EAGAIN;
    }
#else
    pthread_mutex_init(&s->mtx, NULL);
    pthread_cond_init(&s->cv, NULL);
#endif

    uint32_t sem_id = (uint32_t)(slot + 1);
    if (id_out_addr != 0) {
        write_be32(id_out_addr, sem_id);
    }

    sem_table_unlock();
    return CELL_OK;
}

/* ---------------------------------------------------------------------------
 * sys_semaphore_destroy
 *
 * r3 = sem_id
 * -----------------------------------------------------------------------*/
int64_t sys_semaphore_destroy(ppu_context* ctx)
{
    uint32_t sem_id = LV2_ARG_U32(ctx, 0);

    if (sem_id == 0 || sem_id > SYS_SEMAPHORE_MAX)
        return (int64_t)(int32_t)CELL_ESRCH;

    sem_table_lock();

    sys_semaphore_info* s = &g_sys_semaphores[sem_id - 1];
    if (!s->active) {
        sem_table_unlock();
        return (int64_t)(int32_t)CELL_ESRCH;
    }

#ifdef _WIN32
    CloseHandle(s->sem_handle);
    DeleteCriticalSection(&s->value_lock);
#else
    pthread_cond_destroy(&s->cv);
    pthread_mutex_destroy(&s->mtx);
#endif

    s->active = 0;
    sem_table_unlock();
    return CELL_OK;
}

/* ---------------------------------------------------------------------------
 * sys_semaphore_wait
 *
 * r3 = sem_id
 * r4 = timeout_usec (0 = infinite)
 * -----------------------------------------------------------------------*/
int64_t sys_semaphore_wait(ppu_context* ctx)
{
    uint32_t sem_id     = LV2_ARG_U32(ctx, 0);
    uint64_t timeout_us = LV2_ARG_U64(ctx, 1);

    /* ----- bring-up: break producer-less spin waits -----
     * Some titles wait on a sync object whose producer (RSX / SPU / SPURS)
     * never initialized, so the handle is a stale sentinel and the wait loops
     * forever (GoW II: while (sys_semaphore_wait(0x80000004, 1ms) != CELL_OK){}).
     * With PS3_SEM_SPIN_BREAK=N, after N consecutive FAILED waits on the same
     * sem_id from the same thread we return CELL_OK -- as if the missing
     * producer finally posted -- so the caller proceeds and the next wall shows.
     * Off by default; one successful wait resets the counter, so legitimate
     * producer/consumer waits (which succeed periodically) are never broken. */
    static int s_spin_break = -1;
    if (s_spin_break < 0) {
        const char* e = getenv("PS3_SEM_SPIN_BREAK");
        s_spin_break = (e && atoi(e) > 0) ? atoi(e) : 0;
    }
    static __thread uint32_t s_spin_sem = 0;
    static __thread uint64_t s_spin_cnt = 0;
    #define SPIN_NOTE() do { if (s_spin_break > 0) { \
        if (sem_id == s_spin_sem) s_spin_cnt++; \
        else { s_spin_sem = sem_id; s_spin_cnt = 1; } } } while (0)
    if (s_spin_break > 0 && sem_id == s_spin_sem && s_spin_cnt >= (uint64_t)s_spin_break) {
        s_spin_sem = 0; s_spin_cnt = 0;
        return CELL_OK;
    }

    if (sem_id == 0 || sem_id > SYS_SEMAPHORE_MAX) {
        SPIN_NOTE();
        return (int64_t)(int32_t)CELL_ESRCH;
    }

    sys_semaphore_info* s = &g_sys_semaphores[sem_id - 1];
    if (!s->active) {
        SPIN_NOTE();
        return (int64_t)(int32_t)CELL_ESRCH;
    }

    /* Release the PPU giant lock (ppu_loader.cpp) across the genuinely
     * blocking wait below, or every other PPU thread stalls for the full
     * timeout on every retry -- fatal for exactly the GoW II flip-semaphore
     * spin documented above, which retries every 1ms forever when the
     * producer never posts. */
    extern void ppu_giant_lock_release(void);
    extern void ppu_giant_lock_acquire(void);

    /* Task 4 (bringup-instrumentacao-fase0): THREADS/BLOCK, emitted AFTER
     * releasing the giant lock (never before) so the trace reflects "this
     * thread is now actually able to block without stalling every other PPU
     * thread" -- matching the giant-lock-release comment above. Same
     * "bracket every wait unconditionally" design as sys_mutex_lock. */
    uint64_t caller_tid = ctx->thread_id;
    ppu_giant_lock_release();
    PS3_TRACE(PS3_TS_THREADS, "sys_semaphore_wait", "BLOCK",
              "tid=%llu on=sema obj=0x%08X",
              (unsigned long long)caller_tid, sem_id);

#ifdef _WIN32
    DWORD ms = (timeout_us == 0) ? INFINITE : (DWORD)(timeout_us / 1000);
    if (ms == 0 && timeout_us > 0) ms = 1;

    DWORD result = WaitForSingleObject(s->sem_handle, ms);
    if (result == WAIT_TIMEOUT) {
        /* Correcao pos-review (Important 2): fecha o par BLOCK/WAKE no
         * timeout (o parser da Task 6 conta um BLOCK sem WAKE/EXIT como
         * thread permanentemente bloqueada). Emitido ANTES de readquirir o
         * giant lock, mesmo motivo do WAKE de sucesso abaixo. */
        PS3_TRACE(PS3_TS_THREADS, "sys_semaphore_wait", "WAKE",
                  "tid=%llu on=sema obj=0x%08X result=timeout",
                  (unsigned long long)caller_tid, sem_id);
        ppu_giant_lock_acquire();
        SPIN_NOTE();
        return (int64_t)(int32_t)CELL_ETIMEDOUT;
    }
    if (result != WAIT_OBJECT_0) {
        /* WaitForSingleObject falhou por outra razao que nao timeout
         * (WAIT_FAILED/WAIT_ABANDONED) -- caminho raro, mas o BLOCK acima
         * ainda precisa de um terminador. */
        PS3_TRACE(PS3_TS_THREADS, "sys_semaphore_wait", "WAKE",
                  "tid=%llu on=sema obj=0x%08X result=error",
                  (unsigned long long)caller_tid, sem_id);
        ppu_giant_lock_acquire();
        return (int64_t)(int32_t)CELL_EINVAL;
    }
    /* Correcao pos-review (Important 1): WAKE (sucesso) emitido ANTES de
     * readquirir o giant lock -- o fwrite+fflush de ps3_trace_emit deixa de
     * correr sob o lock mais contestado do projecto (2202 pares sema
     * observados in-boot numa janela de 40s). tid/sem_id ja sao conhecidos
     * neste ponto, nao precisam de captura extra em locals. */
    PS3_TRACE(PS3_TS_THREADS, "sys_semaphore_wait", "WAKE",
              "tid=%llu on=sema obj=0x%08X",
              (unsigned long long)caller_tid, sem_id);
    ppu_giant_lock_acquire();

    EnterCriticalSection(&s->value_lock);
    s->value--;
    LeaveCriticalSection(&s->value_lock);
#else
    pthread_mutex_lock(&s->mtx);

    if (timeout_us == 0) {
        while (s->value <= 0) {
            pthread_cond_wait(&s->cv, &s->mtx);
        }
    } else {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec  += (time_t)(timeout_us / 1000000);
        ts.tv_nsec += (long)((timeout_us % 1000000) * 1000);
        if (ts.tv_nsec >= 1000000000L) {
            ts.tv_sec++;
            ts.tv_nsec -= 1000000000L;
        }
        while (s->value <= 0) {
            int rc = pthread_cond_timedwait(&s->cv, &s->mtx, &ts);
            if (rc == ETIMEDOUT) {
                pthread_mutex_unlock(&s->mtx);
                /* Correcao pos-review (Important 2): idem ao ramo Windows
                 * acima -- fecha o par BLOCK/WAKE no timeout, emitido antes
                 * de readquirir o giant lock. */
                PS3_TRACE(PS3_TS_THREADS, "sys_semaphore_wait", "WAKE",
                          "tid=%llu on=sema obj=0x%08X result=timeout",
                          (unsigned long long)caller_tid, sem_id);
                ppu_giant_lock_acquire();
                SPIN_NOTE();
                return (int64_t)(int32_t)CELL_ETIMEDOUT;
            }
        }
    }

    s->value--;
    pthread_mutex_unlock(&s->mtx);
    /* Correcao pos-review (Important 1): WAKE (sucesso) emitido ANTES de
     * readquirir o giant lock -- mesmo motivo do ramo Windows acima. */
    PS3_TRACE(PS3_TS_THREADS, "sys_semaphore_wait", "WAKE",
              "tid=%llu on=sema obj=0x%08X",
              (unsigned long long)caller_tid, sem_id);
    ppu_giant_lock_acquire();
#endif

    s_spin_sem = 0; s_spin_cnt = 0;   /* a real success clears the spin tracker */
    #undef SPIN_NOTE
    return CELL_OK;
}

/* ---------------------------------------------------------------------------
 * sys_semaphore_trywait
 *
 * r3 = sem_id
 * -----------------------------------------------------------------------*/
int64_t sys_semaphore_trywait(ppu_context* ctx)
{
    uint32_t sem_id = LV2_ARG_U32(ctx, 0);

    if (sem_id == 0 || sem_id > SYS_SEMAPHORE_MAX)
        return (int64_t)(int32_t)CELL_ESRCH;

    sys_semaphore_info* s = &g_sys_semaphores[sem_id - 1];
    if (!s->active)
        return (int64_t)(int32_t)CELL_ESRCH;

#ifdef _WIN32
    DWORD result = WaitForSingleObject(s->sem_handle, 0);
    if (result == WAIT_TIMEOUT) {
        return (int64_t)(int32_t)CELL_EBUSY;
    }
    if (result != WAIT_OBJECT_0) {
        return (int64_t)(int32_t)CELL_EINVAL;
    }
    EnterCriticalSection(&s->value_lock);
    s->value--;
    LeaveCriticalSection(&s->value_lock);
#else
    pthread_mutex_lock(&s->mtx);
    if (s->value <= 0) {
        pthread_mutex_unlock(&s->mtx);
        return (int64_t)(int32_t)CELL_EBUSY;
    }
    s->value--;
    pthread_mutex_unlock(&s->mtx);
#endif

    return CELL_OK;
}

/* ---------------------------------------------------------------------------
 * sys_semaphore_post
 *
 * r3 = sem_id
 * r4 = count (number to post)
 * -----------------------------------------------------------------------*/
int64_t sys_semaphore_post(ppu_context* ctx)
{
    uint32_t sem_id = LV2_ARG_U32(ctx, 0);
    int32_t  count  = LV2_ARG_S32(ctx, 1);

    if (sem_id == 0 || sem_id > SYS_SEMAPHORE_MAX)
        return (int64_t)(int32_t)CELL_ESRCH;

    if (count <= 0)
        return (int64_t)(int32_t)CELL_EINVAL;

    sys_semaphore_info* s = &g_sys_semaphores[sem_id - 1];
    if (!s->active)
        return (int64_t)(int32_t)CELL_ESRCH;

#ifdef _WIN32
    EnterCriticalSection(&s->value_lock);
    if (s->value + count > s->max_value) {
        LeaveCriticalSection(&s->value_lock);
        return (int64_t)(int32_t)CELL_EINVAL;
    }
    s->value += count;
    LeaveCriticalSection(&s->value_lock);

    ReleaseSemaphore(s->sem_handle, count, NULL);
#else
    pthread_mutex_lock(&s->mtx);
    if (s->value + count > s->max_value) {
        pthread_mutex_unlock(&s->mtx);
        return (int64_t)(int32_t)CELL_EINVAL;
    }
    s->value += count;
    /* Wake waiters */
    for (int i = 0; i < count; i++) {
        pthread_cond_signal(&s->cv);
    }
    pthread_mutex_unlock(&s->mtx);
#endif

    return CELL_OK;
}

/* ---------------------------------------------------------------------------
 * sys_semaphore_get_value
 *
 * r3 = sem_id
 * r4 = pointer to receive value (s32*)
 * -----------------------------------------------------------------------*/
int64_t sys_semaphore_get_value(ppu_context* ctx)
{
    uint32_t sem_id   = LV2_ARG_U32(ctx, 0);
    uint32_t out_addr = LV2_ARG_PTR(ctx, 1);

    if (sem_id == 0 || sem_id > SYS_SEMAPHORE_MAX)
        return (int64_t)(int32_t)CELL_ESRCH;

    sys_semaphore_info* s = &g_sys_semaphores[sem_id - 1];
    if (!s->active)
        return (int64_t)(int32_t)CELL_ESRCH;

    int32_t val;
#ifdef _WIN32
    EnterCriticalSection(&s->value_lock);
    val = s->value;
    LeaveCriticalSection(&s->value_lock);
#else
    pthread_mutex_lock(&s->mtx);
    val = s->value;
    pthread_mutex_unlock(&s->mtx);
#endif

    if (out_addr != 0) {
        write_be32(out_addr, (uint32_t)val);
    }

    return CELL_OK;
}

/* ---------------------------------------------------------------------------
 * Registration
 * -----------------------------------------------------------------------*/
void sys_semaphore_init(lv2_syscall_table* tbl)
{
    memset(g_sys_semaphores, 0, sizeof(g_sys_semaphores));

#ifdef _WIN32
    if (!s_sem_table_lock_init) {
        InitializeCriticalSection(&s_sem_table_lock);
        s_sem_table_lock_init = 1;
    }
#endif

    lv2_syscall_register(tbl, SYS_SEMAPHORE_CREATE,    sys_semaphore_create);
    lv2_syscall_register(tbl, SYS_SEMAPHORE_DESTROY,   sys_semaphore_destroy);
    lv2_syscall_register(tbl, SYS_SEMAPHORE_WAIT,      sys_semaphore_wait);
    lv2_syscall_register(tbl, SYS_SEMAPHORE_TRYWAIT,   sys_semaphore_trywait);
    lv2_syscall_register(tbl, SYS_SEMAPHORE_POST,      sys_semaphore_post);
    lv2_syscall_register(tbl, SYS_SEMAPHORE_GET_VALUE, sys_semaphore_get_value);
}
