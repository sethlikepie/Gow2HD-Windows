/*
 * ps3recomp - Reader-writer lock syscalls (implementation)
 */

#include "sys_rwlock.h"
#include "../memory/vm.h"
#include "ps3_trace.h"   /* Task 4 (bringup-instrumentacao-fase0): subsistema THREADS */
#include <string.h>

/* ---------------------------------------------------------------------------
 * Globals
 * -----------------------------------------------------------------------*/
sys_rwlock_info g_sys_rwlocks[SYS_RWLOCK_MAX];

#ifndef _WIN32
/* Dono do write lock no POSIX (porte de upstream 429d8a33 + 959af58e). So' o dono
 * escreve, e so' enquanto tem o lock; acquire/release publicam o id com a
 * aquisicao. Sem isto: no Darwin um wrlock re-entrante do proprio dono devolve 0
 * (o guest julga ter um 2.o nivel que nenhum unlock casa) e um wunlock alheio
 * soltava o lock de outro -- pthread_rwlock_unlock nao distingue leitura de escrita.
 *
 * writer_tid guarda o thread id do dono + 1, para 0 continuar a significar "sem dono":
 * o contexto da thread principal tem thread_id 0, e comparado com o 0 de um lock LIVRE
 * fazia todo o write-lock da principal num rwlock livre falhar com EDEADLK (e deixava o
 * seu wunlock de um lock livre chegar ao pthread_rwlock_unlock). Medido no GoW2
 * (2026-09-17): 'scheduler.m_opCallbackLock' devolvia EDEADLK 110-725 vezes por boot,
 * conforme o lock calhava livre ou nao (dependente de timing); com esta codificacao, 0.
 * O LV2 so' devolve EDEADLK a quem ja' tem o write lock. */
static uint64_t rwlock_owner_key(uint64_t tid)
{
    return tid == UINT64_MAX ? UINT64_MAX : tid + 1u;   /* never 0 ("sem dono") */
}

static int rwlock_writer_is(const sys_rwlock_info* r, uint64_t tid)
{
    return __atomic_load_n(&r->writer_tid, __ATOMIC_ACQUIRE) == rwlock_owner_key(tid);
}

static void rwlock_set_writer(sys_rwlock_info* r, uint64_t tid)
{
    __atomic_store_n(&r->writer_tid, rwlock_owner_key(tid), __ATOMIC_RELEASE);
}

static void rwlock_clear_writer(sys_rwlock_info* r)
{
    __atomic_store_n(&r->writer_tid, (uint64_t)0, __ATOMIC_RELEASE);
}
#endif

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
 * sys_rwlock_create
 *
 * r3 = pointer to receive rwlock ID (u32*)
 * r4 = pointer to attribute struct
 * -----------------------------------------------------------------------*/
int64_t sys_rwlock_create(ppu_context* ctx)
{
    uint32_t id_out_addr = LV2_ARG_PTR(ctx, 0);
    uint32_t attr_addr   = LV2_ARG_PTR(ctx, 1);

    int slot = -1;
    for (int i = 0; i < SYS_RWLOCK_MAX; i++) {
        if (!g_sys_rwlocks[i].active) { slot = i; break; }
    }
    if (slot < 0)
        return (int64_t)(int32_t)CELL_EAGAIN;

    sys_rwlock_info* r = &g_sys_rwlocks[slot];
    memset(r, 0, sizeof(*r));
    r->active = 1;

    if (attr_addr != 0) {
        uint8_t* attr_raw = (uint8_t*)vm_to_host(attr_addr);
        uint32_t proto_be;
        memcpy(&proto_be, attr_raw, 4);
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__ || defined(_WIN32)
        proto_be = ((proto_be >> 24) & 0xFF) | ((proto_be >> 8) & 0xFF00) |
                   ((proto_be << 8) & 0xFF0000) | ((proto_be << 24) & 0xFF000000u);
#endif
        r->protocol = proto_be;
        memcpy(r->name, attr_raw + 8, 8);
    }

#ifdef _WIN32
    InitializeSRWLock(&r->srw);
    r->readers = 0;
    r->writer  = 0;
#else
    pthread_rwlock_init(&r->rwl, NULL);
#endif

    uint32_t rwlock_id = (uint32_t)(slot + 1);
    if (id_out_addr != 0) {
        write_be32(id_out_addr, rwlock_id);
    }

    return CELL_OK;
}

int64_t sys_rwlock_destroy(ppu_context* ctx)
{
    uint32_t rwlock_id = LV2_ARG_U32(ctx, 0);

    if (rwlock_id == 0 || rwlock_id > SYS_RWLOCK_MAX)
        return (int64_t)(int32_t)CELL_ESRCH;

    sys_rwlock_info* r = &g_sys_rwlocks[rwlock_id - 1];
    if (!r->active)
        return (int64_t)(int32_t)CELL_ESRCH;

#ifndef _WIN32
    pthread_rwlock_destroy(&r->rwl);
#endif
    /* Windows SRWLock doesn't need destruction */

    r->active = 0;
    return CELL_OK;
}

int64_t sys_rwlock_rlock(ppu_context* ctx)
{
    uint32_t rwlock_id  = LV2_ARG_U32(ctx, 0);
    /* uint64_t timeout = LV2_ARG_U64(ctx, 1); -- timeout not commonly used */

    if (rwlock_id == 0 || rwlock_id > SYS_RWLOCK_MAX)
        return (int64_t)(int32_t)CELL_ESRCH;

    sys_rwlock_info* r = &g_sys_rwlocks[rwlock_id - 1];
    if (!r->active)
        return (int64_t)(int32_t)CELL_ESRCH;

    /* Task 4 (bringup-instrumentacao-fase0): THREADS/BLOCK+WAKE. No table
     * lock exists for sys_rwlock (this module never took one to begin
     * with -- see sys_rwlock_create above), so this cannot deadlock. Same
     * "bracket every wait unconditionally" design as sys_mutex_lock. */
    PS3_TRACE(PS3_TS_THREADS, "sys_rwlock_rlock", "BLOCK",
              "tid=%llu on=rwlock obj=0x%08X",
              (unsigned long long)ctx->thread_id, rwlock_id);
    { extern void ppu_giant_lock_release(void); extern void ppu_giant_lock_acquire(void); ppu_giant_lock_release(); /* E139: ver sys_rwlock_wlock */
#ifdef _WIN32
    AcquireSRWLockShared(&r->srw);
    InterlockedIncrement(&r->readers);
#else
    pthread_rwlock_rdlock(&r->rwl);
#endif
    ppu_giant_lock_acquire(); }
    PS3_TRACE(PS3_TS_THREADS, "sys_rwlock_rlock", "WAKE",
              "tid=%llu on=rwlock obj=0x%08X",
              (unsigned long long)ctx->thread_id, rwlock_id);

    return CELL_OK;
}

int64_t sys_rwlock_tryrlock(ppu_context* ctx)
{
    uint32_t rwlock_id = LV2_ARG_U32(ctx, 0);

    if (rwlock_id == 0 || rwlock_id > SYS_RWLOCK_MAX)
        return (int64_t)(int32_t)CELL_ESRCH;

    sys_rwlock_info* r = &g_sys_rwlocks[rwlock_id - 1];
    if (!r->active)
        return (int64_t)(int32_t)CELL_ESRCH;

#ifdef _WIN32
    if (!TryAcquireSRWLockShared(&r->srw))
        return (int64_t)(int32_t)CELL_EBUSY;
    InterlockedIncrement(&r->readers);
#else
    int rc = pthread_rwlock_tryrdlock(&r->rwl);
    if (rc != 0)
        return (int64_t)(int32_t)CELL_EBUSY;
#endif

    return CELL_OK;
}

int64_t sys_rwlock_runlock(ppu_context* ctx)
{
    uint32_t rwlock_id = LV2_ARG_U32(ctx, 0);

    if (rwlock_id == 0 || rwlock_id > SYS_RWLOCK_MAX)
        return (int64_t)(int32_t)CELL_ESRCH;

    sys_rwlock_info* r = &g_sys_rwlocks[rwlock_id - 1];
    if (!r->active)
        return (int64_t)(int32_t)CELL_ESRCH;

#ifdef _WIN32
    InterlockedDecrement(&r->readers);
    ReleaseSRWLockShared(&r->srw);
#else
    pthread_rwlock_unlock(&r->rwl);
#endif

    return CELL_OK;
}

int64_t sys_rwlock_wlock(ppu_context* ctx)
{
    uint32_t rwlock_id  = LV2_ARG_U32(ctx, 0);
    /* uint64_t timeout = LV2_ARG_U64(ctx, 1); */

    if (rwlock_id == 0 || rwlock_id > SYS_RWLOCK_MAX)
        return (int64_t)(int32_t)CELL_ESRCH;

    sys_rwlock_info* r = &g_sys_rwlocks[rwlock_id - 1];
    if (!r->active)
        return (int64_t)(int32_t)CELL_ESRCH;

#ifndef _WIN32
    /* Re-lock pelo proprio dono: EDEADLK antes de tocar no lock (e antes de largar
     * o giant lock do E139), como no ramo Win32 e como o LV2 responde. A pergunta e'
     * "sou EU o dono?" -- so' a propria thread muda essa resposta, por isso a
     * verificacao antes do wrlock nao tem janela com outras threads. */
    if (rwlock_writer_is(r, ctx->thread_id))
        return (int64_t)(int32_t)CELL_EDEADLK;
#endif
    /* Task 4: THREADS/BLOCK+WAKE, same reasoning as sys_rwlock_rlock above. */
    PS3_TRACE(PS3_TS_THREADS, "sys_rwlock_wlock", "BLOCK",
              "tid=%llu on=rwlock obj=0x%08X",
              (unsigned long long)ctx->thread_id, rwlock_id);
    /* E139 (2026-09-02): bloquear SEM o giant lock do PPU. Medido no GoW2 (3 amostras
     * `sample`, 20/40/60 s): a thread principal parada em pthread_rwlock_wrlock dentro
     * de FUN_0030644c (conclusao de op FIOS) enquanto a thread "fios scheduler" -- que
     * tinha o rwlock -- estava na mesma funcao, sem poder correr codigo guest porque o
     * giant lock ficava com a principal. Deadlock so' nosso; na consola o rwlock nao
     * impede o scheduler. Mesmo padrao do sys_semaphore_wait. */
    {
        extern void ppu_giant_lock_release(void);
        extern void ppu_giant_lock_acquire(void);
        ppu_giant_lock_release();
#ifdef _WIN32
        AcquireSRWLockExclusive(&r->srw);
        InterlockedExchange(&r->writer, 1);
#else
        pthread_rwlock_wrlock(&r->rwl);
        rwlock_set_writer(r, ctx->thread_id);
#endif
        ppu_giant_lock_acquire();
    }
    PS3_TRACE(PS3_TS_THREADS, "sys_rwlock_wlock", "WAKE",
              "tid=%llu on=rwlock obj=0x%08X",
              (unsigned long long)ctx->thread_id, rwlock_id);

    return CELL_OK;
}

int64_t sys_rwlock_trywlock(ppu_context* ctx)
{
    uint32_t rwlock_id = LV2_ARG_U32(ctx, 0);

    if (rwlock_id == 0 || rwlock_id > SYS_RWLOCK_MAX)
        return (int64_t)(int32_t)CELL_ESRCH;

    sys_rwlock_info* r = &g_sys_rwlocks[rwlock_id - 1];
    if (!r->active)
        return (int64_t)(int32_t)CELL_ESRCH;

#ifdef _WIN32
    if (!TryAcquireSRWLockExclusive(&r->srw))
        return (int64_t)(int32_t)CELL_EBUSY;
    InterlockedExchange(&r->writer, 1);
#else
    int rc = pthread_rwlock_trywrlock(&r->rwl);
    if (rc != 0)   /* busy: EDEADLK when the caller itself is the write owner */
        return (int64_t)(int32_t)(rwlock_writer_is(r, ctx->thread_id) ? CELL_EDEADLK : CELL_EBUSY);
    rwlock_set_writer(r, ctx->thread_id);
#endif

    return CELL_OK;
}

int64_t sys_rwlock_wunlock(ppu_context* ctx)
{
    uint32_t rwlock_id = LV2_ARG_U32(ctx, 0);

    if (rwlock_id == 0 || rwlock_id > SYS_RWLOCK_MAX)
        return (int64_t)(int32_t)CELL_ESRCH;

    sys_rwlock_info* r = &g_sys_rwlocks[rwlock_id - 1];
    if (!r->active)
        return (int64_t)(int32_t)CELL_ESRCH;

#ifdef _WIN32
    InterlockedExchange(&r->writer, 0);
    ReleaseSRWLockExclusive(&r->srw);
#else
    /* So' o dono solta o write lock: pthread_rwlock_unlock nao distingue leitura de
     * escrita, e sem esta guarda um wunlock alheio soltava o lock de outra thread. */
    if (!rwlock_writer_is(r, ctx->thread_id))
        return (int64_t)(int32_t)CELL_EPERM;
    rwlock_clear_writer(r);
    pthread_rwlock_unlock(&r->rwl);
#endif

    return CELL_OK;
}

/* ---------------------------------------------------------------------------
 * Registration
 * -----------------------------------------------------------------------*/
void sys_rwlock_init(lv2_syscall_table* tbl)
{
    memset(g_sys_rwlocks, 0, sizeof(g_sys_rwlocks));

    lv2_syscall_register(tbl, SYS_RWLOCK_CREATE,   sys_rwlock_create);
    lv2_syscall_register(tbl, SYS_RWLOCK_DESTROY,  sys_rwlock_destroy);
    lv2_syscall_register(tbl, SYS_RWLOCK_RLOCK,    sys_rwlock_rlock);
    lv2_syscall_register(tbl, SYS_RWLOCK_TRYRLOCK, sys_rwlock_tryrlock);
    lv2_syscall_register(tbl, SYS_RWLOCK_RUNLOCK,  sys_rwlock_runlock);
    lv2_syscall_register(tbl, SYS_RWLOCK_WLOCK,    sys_rwlock_wlock);
    lv2_syscall_register(tbl, SYS_RWLOCK_TRYWLOCK, sys_rwlock_trywlock);
    lv2_syscall_register(tbl, SYS_RWLOCK_WUNLOCK,  sys_rwlock_wunlock);
}
