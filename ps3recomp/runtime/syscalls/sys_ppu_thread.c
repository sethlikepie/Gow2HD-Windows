/*
 * ps3recomp - PPU thread management syscalls (implementation)
 */

#include "sys_ppu_thread.h"
#include "ps3_checkpoint.h"
#include "ps3_trace.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>   /* getenv (gate do QoS) */
#ifdef __APPLE__
#include <pthread.h>
#include <pthread/qos.h>
#endif

/* ---------------------------------------------------------------------------
 * Globals
 * -----------------------------------------------------------------------*/
ppu_thread_info       g_ppu_threads[PPU_THREAD_MAX];
vm_stack_alloc        g_vm_stack_alloc;
ppu_thread_entry_fn   g_ppu_thread_entry_trampoline = NULL;

/* Simple mutex for thread table access */
#ifdef _WIN32
static CRITICAL_SECTION s_table_lock;
static int              s_table_lock_init = 0;
#else
static pthread_mutex_t  s_table_lock = PTHREAD_MUTEX_INITIALIZER;
#endif

static void table_lock(void)
{
#ifdef _WIN32
    if (!s_table_lock_init) {
        InitializeCriticalSection(&s_table_lock);
        s_table_lock_init = 1;
    }
    EnterCriticalSection(&s_table_lock);
#else
    pthread_mutex_lock(&s_table_lock);
#endif
}

static void table_unlock(void)
{
#ifdef _WIN32
    LeaveCriticalSection(&s_table_lock);
#else
    pthread_mutex_unlock(&s_table_lock);
#endif
}

/* Find a free slot. Returns index or -1. Must be called under lock. */
static int find_free_slot(void)
{
    for (int i = 0; i < PPU_THREAD_MAX; i++) {
        if (g_ppu_threads[i].state == PPU_THREAD_STATE_FREE)
            return i;
    }
    return -1;
}

/* Find thread by ID. The ID is the index + 1 (0 is invalid). */
static ppu_thread_info* find_thread(uint64_t thread_id)
{
    if (thread_id == 0 || thread_id > PPU_THREAD_MAX) return NULL;
    ppu_thread_info* t = &g_ppu_threads[thread_id - 1];
    if (t->state == PPU_THREAD_STATE_FREE) return NULL;
    return t;
}

/* E154 (2026-09-02): prioridade guest de uma thread (para o giant lock: threads com prioridade
 * numericamente MENOR que a principal -- ex. syn_tick_timer_thread prio 100, fios 99 -- devem
 * re-adquirir como prio-waiters, o que na consola e' preempcao). -1 se desconhecida. */
int32_t sys_ppu_thread_priority_of(uint64_t thread_id)
{
    ppu_thread_info* t = find_thread(thread_id);
    return t ? t->priority : -1;
}

int sys_ppu_thread_name_is(uint64_t thread_id, const char* prefix)
{
    ppu_thread_info* t = find_thread(thread_id);
    if (!t || !prefix || !prefix[0]) return 0;
    size_t n = strlen(prefix);
    return strncmp(t->name, prefix, n) == 0;
}

/* Cross-thread lwarx/stwcx. reservation registry (defined in ppu_loader.cpp).
 * A spawned PPU thread must participate so its stores cancel other threads'
 * reservations and vice-versa. */
extern void ppu_rsv_register(ppu_context* ctx);
extern void ppu_rsv_unregister(ppu_context* ctx);

/* ---------------------------------------------------------------------------
 * Host thread entry point
 * -----------------------------------------------------------------------*/
#ifdef _WIN32
static DWORD WINAPI ppu_host_thread_proc(LPVOID param)
#else
static void* ppu_host_thread_proc(void* param)
#endif
{
    ppu_thread_info* info = (ppu_thread_info*)param;

    /* Task 2 (bringup-instrumentacao-fase0), requisito 6: liga a thread guest
     * corrente em TLS assim que ela comeca a executar, para que <thr> deixe
     * de ser "-" em qualquer PS3_TRACE/ps3_checkpoint_fire chamado desta
     * thread daqui em diante (incluindo pelas tarefas seguintes do plano). */
    ps3_trace_set_thread((uint32_t)info->ctx.thread_id);

    /* Task 4: THREADS/START -- the host thread backing this guest PPU thread
     * has begun running, about to invoke the guest entry point. Emitted
     * before any lock (table_lock is not held here) and before the giant
     * lock acquire below. */
    PS3_TRACE(PS3_TS_THREADS, "ppu_host_thread_proc", "START",
              "tid=%llu entry=0x%08llX", (unsigned long long)info->ctx.thread_id,
              (unsigned long long)info->entry_addr);

    fprintf(stderr, "[THREAD %llu] host thread started, entry=0x%08llX\n",
            (unsigned long long)info->ctx.thread_id,
            (unsigned long long)info->entry_addr);
    { extern void ppu_blockmark_name(const char* name);
      ppu_blockmark_name(info->name[0] ? info->name : "?"); }
#ifndef _WIN32
    /* Host thread named after the guest thread: sample/lldb and the lwmutex
     * owner report then say which PPU thread is which. */
    { char nm[64]; snprintf(nm, sizeof nm, "ppu:%s", info->name[0] ? info->name : "?");
      pthread_setname_np(nm); }
#endif

    /* Invoke the recompiled entry point. Serialized behind the PPU giant
     * lock (see ppu_loader.cpp) -- lifted code and shared HLE state are not
     * safe for concurrent execution from multiple real host threads yet. */
    extern void ppu_giant_lock_acquire(void);
    extern void ppu_giant_lock_release(void);
    extern int ppu_giant_hle_only_on(void);
    const int hle_only = ppu_giant_hle_only_on();
#ifdef __APPLE__
    /* Apple Silicon e' heterogeneo (P-cores e E-cores) e, sem uma dica de QoS,
     * o macOS manda threads de background para os E-cores. Uma thread guest e'
     * o caminho critico do frame -- ainda mais com o giant lock, que deixa uma
     * so' a correr de cada vez -- por isso pede explicitamente o nivel de
     * interaccao. PS3_QOS=0 desliga, para A/B. */
    { const char* q = getenv("PS3_QOS");
      if (!(q && q[0] == '0'))
          pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0); }
#endif
    ppu_rsv_register(&info->ctx);
    /* PS3_GIANT_HLE_ONLY: a thread guest corre sem o lock; quem o toma sao os
     * funis HLE (ps3_hle_call / lv2_syscall). */
    if (!hle_only) ppu_giant_lock_acquire();
    if (g_ppu_thread_entry_trampoline) {
        g_ppu_thread_entry_trampoline(&info->ctx);
    } else {
        fprintf(stderr, "[THREAD %llu] g_ppu_thread_entry_trampoline is NULL — thread is a no-op!\n",
                (unsigned long long)info->ctx.thread_id);
    }
    if (!hle_only) ppu_giant_lock_release();

    /* Mark as finished */
    ppu_rsv_unregister(&info->ctx);
    table_lock();
    info->exit_status = (int64_t)info->ctx.gpr[3];

    /* Task 4: capture locals BEFORE table_unlock() -- a DETACHED thread's
     * slot is marked FREE right below and can be reused by a concurrent
     * sys_ppu_thread_create() the instant table_unlock() returns (find_free_slot()
     * scans for state==FREE). Reading info->ctx.thread_id/exit_status AFTER
     * unlock would race that reuse. Emitting the trace itself after unlock is
     * still safe/preferred (ps3_trace_emit takes no lock -- see the lock-safety
     * table in the report), it is the SOURCE DATA that is not safe to read late. */
    uint64_t exited_tid    = info->ctx.thread_id;
    int64_t  exited_status = info->exit_status;
    /* O nome tem de ser COPIADO aqui, pela mesma razao que o tid e o status:
     * o slot de uma thread DETACHED e' marcado FREE logo abaixo e pode ser
     * reutilizado por um sys_ppu_thread_create() concorrente no instante em
     * que table_unlock() retorna. */
    char exited_name[sizeof(info->name)];
    strncpy(exited_name, info->name, sizeof(exited_name) - 1);
    exited_name[sizeof(exited_name) - 1] = '\0';

    if (info->state == PPU_THREAD_STATE_DETACHED) {
        /* Detached threads self-clean */
        info->state = PPU_THREAD_STATE_FREE;
    } else {
        info->state = PPU_THREAD_STATE_FINISHED;
    }
    table_unlock();

    /* Marcador simetrico ao de sys_ppu_thread_create, e pela mesma razao:
     * sem ele nao ha' maneira de observar que uma thread guest TERMINOU.
     * Medido a 2026-08-01: o smoke da cadeia procurava 'thr_auto_load() end',
     * string que NUNCA existiu em nenhum dos 26 binarios guardados -- o elo
     * AUTO_LOAD dava sempre 0 e o gate saia sempre REGRESSAO, medisse o que
     * medisse. Um instrumento que nao pode passar nao mede nada. */
    fprintf(stderr, "[SYS] sys_ppu_thread end name=\"%s\" tid=%llu status=%lld\n",
            exited_name, (unsigned long long)exited_tid,
            (long long)exited_status);

    PS3_TRACE(PS3_TS_THREADS, "ppu_host_thread_proc", "EXIT",
              "tid=%llu status=%lld", (unsigned long long)exited_tid,
              (long long)exited_status);

    /* Signal anyone waiting for join */
#ifdef _WIN32
    SetEvent(info->finish_event);
    return 0;
#else
    pthread_mutex_lock(&info->finish_mutex);
    info->finished = 1;
    pthread_cond_signal(&info->finish_cond);
    pthread_mutex_unlock(&info->finish_mutex);
    return NULL;
#endif
}

/* ---------------------------------------------------------------------------
 * sys_ppu_thread_create
 *
 * r3 = pointer to receive thread ID (u64*)
 * r4 = entry point address
 * r5 = argument (passed in new thread's r3)
 * r6 = priority (s32)
 * r7 = stack size
 * r8 = flags
 * r9 = thread name pointer
 * -----------------------------------------------------------------------*/
int64_t sys_ppu_thread_create(ppu_context* ctx)
{
    uint32_t tid_out_addr = LV2_ARG_PTR(ctx, 0);
    uint64_t entry        = LV2_ARG_U64(ctx, 1);
    uint64_t arg          = LV2_ARG_U64(ctx, 2);
    int32_t  priority     = LV2_ARG_S32(ctx, 3);
    uint32_t stack_size   = LV2_ARG_U32(ctx, 4);
    /* uint64_t flags     = LV2_ARG_U64(ctx, 5); */
    uint32_t name_addr    = LV2_ARG_PTR(ctx, 6);

    if (stack_size == 0) stack_size = VM_PPU_STACK_SIZE;
    if (stack_size < 0x4000) stack_size = 0x4000; /* 16 KB minimum */

    table_lock();

    int slot = find_free_slot();
    if (slot < 0) {
        table_unlock();
        return (int64_t)(int32_t)CELL_EAGAIN;
    }

    ppu_thread_info* t = &g_ppu_threads[slot];
    memset(t, 0, sizeof(*t));

    /* Allocate guest stack */
    uint32_t stack_addr = vm_stack_allocate(&g_vm_stack_alloc, stack_size);
    if (stack_addr == 0) {
        table_unlock();
        return (int64_t)(int32_t)CELL_ENOMEM;
    }

    /* Set up the PPU context for the new thread */
    ppu_context_init(&t->ctx);
    t->ctx.cia = entry;
    t->ctx.gpr[3] = arg;
    ppu_set_stack(&t->ctx, (uint64_t)stack_addr, (uint64_t)stack_size);
    /* PS3_TRACE_THRSTACK: a thread whose guest stack is bogus shows up much
     * later as OOB accesses on small addresses (r1-relative locals wrapping
     * below 0), far from the cause. Log the allocation so that is one grep. */
    { extern char* getenv(const char*);
      static int ts = -1; if (ts < 0) ts = getenv("PS3_TRACE_THRSTACK") ? 1 : 0;
      if (ts) fprintf(stderr, "[THRSTACK] tid=%u entry=0x%08X stack=0x%08X..0x%08X r1=0x%08llX toc=0x%08llX\n",
                      (unsigned)(slot + 1), (unsigned)entry, stack_addr,
                      stack_addr + stack_size,
                      (unsigned long long)t->ctx.gpr[1],
                      (unsigned long long)ctx->gpr[2]); }
    /* Copy TOC from creating thread */
    t->ctx.gpr[2] = ctx->gpr[2];
    /* r13 = thread pointer (PPC64 ELF). ppu_run so o define para a thread
     * principal (ppu_loader.cpp, PPU_TLS_TP); ppu_context_init zera tudo, por
     * isso ate aqui TODA thread criada corria com r13=0 e cada acesso
     * TLS-relativo (`addi rX, r13, -N`) caia num endereco pequeno negativo --
     * observado como "[vm] OOB access 0xFFFF90D4" (= 0 - 0x6F2C) nas threads do
     * FIOS, vindo de func_00380480.
     *
     * LIMITACAO ASSUMIDA, nao e o fim da historia: herdar o TP faz todas as
     * threads PARTILHAREM o bloco TLS da principal. Isso restaura o acesso a
     * area de dados pequena/partilhada (que e o que o GoW2 le aqui), mas NAO e
     * TLS por-thread fiel: uma variavel genuinamente thread-local passa a ser
     * vista por todas. O fix completo aloca um bloco por thread a partir do
     * template PT_TLS (g_tls_* em ppu_loader.cpp) e poe r13 = bloco + 0x7000. */
    t->ctx.gpr[13] = ctx->gpr[13];

    uint64_t thread_id = (uint64_t)(slot + 1);
    t->ctx.thread_id = thread_id;

    t->state      = PPU_THREAD_STATE_RUNNING;
    t->priority   = priority;
    t->joinable   = 1;
    t->stack_addr = stack_addr;
    t->stack_size = stack_size;
    t->entry_addr = entry;

    /* Copy thread name if provided */
    if (name_addr != 0) {
        const char* name = (const char*)vm_to_host(name_addr);
        strncpy(t->name, name, sizeof(t->name) - 1);
        t->name[sizeof(t->name) - 1] = '\0';
    }

    /* Create synchronization for join */
#ifdef _WIN32
    t->finish_event = CreateEventA(NULL, TRUE, FALSE, NULL);
#else
    pthread_mutex_init(&t->finish_mutex, NULL);
    pthread_cond_init(&t->finish_cond, NULL);
    t->finished = 0;
#endif

    /* Write thread ID to output pointer */
    if (tid_out_addr != 0) {
        uint64_t* out = (uint64_t*)vm_to_host(tid_out_addr);
        /* Store as big-endian u64 */
        uint64_t be_id = thread_id;
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__ || defined(_WIN32)
        be_id = ((be_id >> 56) & 0xFF) |
                ((be_id >> 40) & 0xFF00) |
                ((be_id >> 24) & 0xFF0000) |
                ((be_id >>  8) & 0xFF000000ULL) |
                ((be_id <<  8) & 0xFF00000000ULL) |
                ((be_id << 24) & 0xFF0000000000ULL) |
                ((be_id << 40) & 0xFF000000000000ULL) |
                ((be_id << 56) & 0xFF00000000000000ULL);
#endif
        *out = be_id;
    }

    /* lr = endereco de retorno guest, ou seja QUEM criou a thread. Sem isto
     * nao ha' como responder "porque e' que a thread X nunca e' criada?" --
     * a unica pista seria comparar com quem cria as que existem. */
    fprintf(stderr, "[SYS] sys_ppu_thread_create name=\"%s\" entry=0x%08llX arg=0x%llX stack=0x%X prio=%d lr=0x%08llX\n",
            t->name, (unsigned long long)entry, (unsigned long long)arg,
            stack_size, priority, (unsigned long long)ctx->lr);

    /* AUTO_LOAD (post-B71): guest entry 0x521768 crashes host (SIGSEGV in
     * thr_auto_load) before the run can prove post-exit FATAL absence. Stub:
     * succeed create, mark finished, no host thread — main keeps living.
     * PROBE gate (OFF by default): PS3_AUTO_LOAD_RUN=1 lets it run so the
     * real SIGSEGV site can be caught (this stub is exactly what blocks the
     * post-B71 menu/stage load — main spins forever without it). */
    /* E93 (2026-09-02): the stub was itself wall [G] -- the title FSM
     * (FUN_000bb0b0) polls the thread's status word and the console runs it
     * (cellSaveDataAutoLoad2 -> CBRESULT -> thr_auto_load end -> shaders).
     * Default: RUN. PS3_AUTO_LOAD_STUB=1 restores the stub for A/B only. */
    { static int autorun = -1;
      if (autorun < 0) { const char* st = getenv("PS3_AUTO_LOAD_STUB");
                         autorun = (st && *st && *st != '0') ? 0 : 1; }
    if (!autorun && t->name[0] && (strcmp(t->name, "AUTO_LOAD") == 0
                       || entry == 0x00521768ull)) {
        fprintf(stderr, "[SYS] AUTO_LOAD stubbed (no host thread; post-B71 stability)\n");
        t->state = PPU_THREAD_STATE_FINISHED;
#ifdef _WIN32
        if (t->finish_event) SetEvent(t->finish_event);
#else
        t->finished = 1;
        pthread_cond_broadcast(&t->finish_cond);
#endif
        table_unlock();
        return 0;
    } }

    /* Create the host thread */
#ifdef _WIN32
    t->host_thread = CreateThread(NULL, 0, ppu_host_thread_proc, t, 0, &t->host_tid);
    if (t->host_thread == NULL) {
        t->state = PPU_THREAD_STATE_FREE;
        CloseHandle(t->finish_event);
        table_unlock();
        return (int64_t)(int32_t)CELL_EAGAIN;
    }
#else
    int rc = pthread_create(&t->host_thread, NULL, ppu_host_thread_proc, t);
    if (rc != 0) {
        t->state = PPU_THREAD_STATE_FREE;
        pthread_mutex_destroy(&t->finish_mutex);
        pthread_cond_destroy(&t->finish_cond);
        table_unlock();
        return (int64_t)(int32_t)CELL_EAGAIN;
    }
#endif

    /* Checkpoint FIRST_THREAD (Task 2, anchor verificado no brief): primeiro
     * sys_ppu_thread_create bem sucedido, ou seja, um host thread real foi
     * criado (nao o stub AUTO_LOAD acima, que nunca chega a correr codigo
     * guest). Once-guard interno a ps3_checkpoint_fire cobre runs com varias
     * threads. */
    ps3_checkpoint_fire(PS3_CP_FIRST_THREAD, "sys_ppu_thread_create",
                       "tid=%llu name=%s entry=0x%08llX",
                       (unsigned long long)thread_id, t->name,
                       (unsigned long long)entry);

    /* Task 4: THREADS/CREATE -- fires every successful create (not once-
     * guarded like the checkpoint above). Emitted while still holding
     * table_lock(): safe, ps3_trace_emit() takes no application lock (see
     * the lock-safety table in the report) -- same reasoning that already
     * applies to the ps3_checkpoint_fire() call right above it. */
    PS3_TRACE(PS3_TS_THREADS, "sys_ppu_thread_create", "CREATE",
              "tid=%llu name=%s entry=0x%08llX prio=%d",
              (unsigned long long)thread_id, t->name,
              (unsigned long long)entry, priority);

    table_unlock();
    return CELL_OK;
}

/* Numero de threads PPU guest vivas (Task 2: contador threads= do SNAPSHOT).
 *
 * NAO toma table_lock() (review pos-Task 2, achado do deadlock de boot):
 * ps3_checkpoint_fire() chama este accessor sempre que PS3_TRACE_SNAPSHOT=1,
 * de QUALQUER call site de checkpoint -- incluindo FIRST_THREAD, disparado
 * em sys_ppu_thread_create() a partir de DENTRO da secao critica de
 * table_lock() (linha ~161-311 desta funcao). table_lock() usa
 * PTHREAD_MUTEX_INITIALIZER em POSIX, que e' NAO-recursivo em macOS/Linux;
 * chamar table_lock() outra vez da MESMA thread enquanto ja o detem
 * autodeadlocka -- reproduzido em boot real (todo checkpoint tinha uma linha
 * SNAPSHOT pareada excepto FIRST_THREAD, cujo SNAPSHOT nunca chegava; o log
 * repetia "[MOVIEFSM] st620 0 -> 0" para sempre a seguir). Confirmado
 * identico em runs de 40s/100s/45s. So' acontece em macOS/Linux: no Windows
 * s_table_lock e' CRITICAL_SECTION, que E' recursiva.
 *
 * Em vez de reordenar locks (fragil -- qualquer sitio futuro de checkpoint
 * que dispare dentro de table_lock() reabriria o mesmo buraco), o accessor
 * fica lock-free: leitura relaxada de .state por slot. Adequado para um
 * contador de DIAGNOSTICO (SNAPSHOT nunca decide comportamento do runtime);
 * o pior caso e' contar uma thread em transicao de estado como viva/morta um
 * instante antes/depois do valor exacto sob lock, o que e' aceitavel aqui e
 * elimina inteiramente a classe de hazard (nenhuma lock-order possivel com
 * QUALQUER lock que um call site de checkpoint futuro venha a deter). */
uint32_t ps3_ppu_thread_alive_count(void)
{
    uint32_t n = 0;
    for (int i = 0; i < PPU_THREAD_MAX; i++) {
        int st = __atomic_load_n(&g_ppu_threads[i].state, __ATOMIC_RELAXED);
        if (st != PPU_THREAD_STATE_FREE) {
            n++;
        }
    }
    return n;
}

/* ---------------------------------------------------------------------------
 * sys_ppu_thread_exit
 *
 * r3 = exit status
 * -----------------------------------------------------------------------*/
int64_t sys_ppu_thread_exit(ppu_context* ctx)
{
    uint64_t status = LV2_ARG_U64(ctx, 0);
    uint64_t tid = ctx->thread_id;

    table_lock();
    ppu_thread_info* t = find_thread(tid);
    if (t) {
        t->exit_status = (int64_t)status;
        if (t->state == PPU_THREAD_STATE_DETACHED) {
            t->state = PPU_THREAD_STATE_FREE;
        } else {
            t->state = PPU_THREAD_STATE_FINISHED;
        }

#ifdef _WIN32
        SetEvent(t->finish_event);
#else
        pthread_mutex_lock(&t->finish_mutex);
        t->finished = 1;
        pthread_cond_signal(&t->finish_cond);
        pthread_mutex_unlock(&t->finish_mutex);
#endif
    }
    table_unlock();

    /* In a real implementation this would terminate the calling thread.
     * The thread proc wrapper handles this after return. */
    return CELL_OK;
}

/* ---------------------------------------------------------------------------
 * sys_ppu_thread_join
 *
 * r3 = thread_id
 * r4 = pointer to receive exit status (s64*)
 * -----------------------------------------------------------------------*/
int64_t sys_ppu_thread_join(ppu_context* ctx)
{
    uint64_t tid          = LV2_ARG_U64(ctx, 0);
    uint32_t status_addr  = LV2_ARG_PTR(ctx, 1);

    table_lock();
    ppu_thread_info* t = find_thread(tid);
    if (!t) {
        table_unlock();
        return (int64_t)(int32_t)CELL_ESRCH;
    }
    if (!t->joinable || t->state == PPU_THREAD_STATE_DETACHED) {
        table_unlock();
        return (int64_t)(int32_t)CELL_EINVAL;
    }
    table_unlock();

    /* Task 4: THREADS/BLOCK -- the caller genuinely blocks here until the
     * joined thread signals finish_cond (or the OS event, on Windows). No
     * lock is held across this wait (table_lock was released just above),
     * so emitting here cannot deadlock and cannot stall the table for other
     * threads. on=join/obj=<joined tid> satisfies the brief's mandatory
     * BLOCK attribution (checkpoint C3: every wait has a registered reason). */
    PS3_TRACE(PS3_TS_THREADS, "sys_ppu_thread_join", "BLOCK",
              "tid=%llu on=join obj=0x%08X",
              (unsigned long long)ctx->thread_id, (uint32_t)tid);

    /* Wait for completion */
#ifdef _WIN32
    WaitForSingleObject(t->finish_event, INFINITE);
#else
    pthread_mutex_lock(&t->finish_mutex);
    while (!t->finished) {
        pthread_cond_wait(&t->finish_cond, &t->finish_mutex);
    }
    pthread_mutex_unlock(&t->finish_mutex);
#endif

    PS3_TRACE(PS3_TS_THREADS, "sys_ppu_thread_join", "WAKE",
              "tid=%llu on=join obj=0x%08X",
              (unsigned long long)ctx->thread_id, (uint32_t)tid);

    /* finish_mutex/cond already synchronizes this read-after-write (the
     * joined thread sets exit_status before signaling finished=1), so it is
     * safe to read t->exit_status here, once, for both the guest write below
     * and the JOIN trace event further down -- no table_lock needed. */
    int64_t joined_exit_status = t->exit_status;

    /* Write exit status */
    if (status_addr != 0) {
        int64_t es = joined_exit_status;
        int64_t* out = (int64_t*)vm_to_host(status_addr);
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__ || defined(_WIN32)
        uint64_t u = (uint64_t)es;
        u = ((u >> 56) & 0xFF) |
            ((u >> 40) & 0xFF00) |
            ((u >> 24) & 0xFF0000) |
            ((u >>  8) & 0xFF000000ULL) |
            ((u <<  8) & 0xFF00000000ULL) |
            ((u << 24) & 0xFF0000000000ULL) |
            ((u << 40) & 0xFF000000000000ULL) |
            ((u << 56) & 0xFF00000000000000ULL);
        *out = (int64_t)u;
#else
        *out = es;
#endif
    }

    /* Clean up */
#ifdef _WIN32
    table_lock();
    CloseHandle(t->host_thread);
    CloseHandle(t->finish_event);
    t->host_thread = NULL;
    t->finish_event = NULL;
#else
    /* Reap the host thread with the table lock RELEASED.
     *
     * pthread_join blocks until the thread procedure returns, and that
     * procedure's epilogue takes the table lock to stamp its own state. The
     * joiner is woken well before that epilogue runs, because a guest thread
     * normally ends at sys_ppu_thread_exit, which signals `finished` from
     * INSIDE the thread body and then longjmps back out to the epilogue. So a
     * joiner that holds the lock across the join is waiting for a thread that
     * is waiting for the lock -- and the whole title stops with no message.
     *
     * Windows never saw it: its half of this block is CloseHandle, which
     * records nothing about whether the thread has finished and never waits.
     * The two halves have to be read as one thing, and only one of them was.
     * Take the lock again afterwards for the teardown, which is what actually
     * needs it: nothing can claim this slot in between, because it is still
     * FINISHED rather than FREE until the line below. */
    pthread_join(t->host_thread, NULL);
    table_lock();
    pthread_mutex_destroy(&t->finish_mutex);
    pthread_cond_destroy(&t->finish_cond);
#endif
    t->state = PPU_THREAD_STATE_FREE;
    table_unlock();

    /* Task 4: THREADS/JOIN -- semantic completion event (distinct from the
     * BLOCK/WAKE pair above, which only mark the wait itself): "joiner tid
     * successfully joined target tid, with this exit status." joined_exit_status
     * was captured before the slot could be reused (see above), so this is
     * safe even though the slot is now free. */
    PS3_TRACE(PS3_TS_THREADS, "sys_ppu_thread_join", "JOIN",
              "tid=%llu joined=0x%08X status=%lld",
              (unsigned long long)ctx->thread_id, (uint32_t)tid,
              (long long)joined_exit_status);

    return CELL_OK;
}

/* ---------------------------------------------------------------------------
 * sys_ppu_thread_detach
 *
 * r3 = thread_id
 * -----------------------------------------------------------------------*/
int64_t sys_ppu_thread_detach(ppu_context* ctx)
{
    uint64_t tid = LV2_ARG_U64(ctx, 0);

    table_lock();
    ppu_thread_info* t = find_thread(tid);
    if (!t) {
        table_unlock();
        return (int64_t)(int32_t)CELL_ESRCH;
    }

    if (t->state == PPU_THREAD_STATE_FINISHED) {
        /* Already finished, free it */
#ifdef _WIN32
        CloseHandle(t->host_thread);
        CloseHandle(t->finish_event);
#else
        pthread_detach(t->host_thread);
        pthread_mutex_destroy(&t->finish_mutex);
        pthread_cond_destroy(&t->finish_cond);
#endif
        t->state = PPU_THREAD_STATE_FREE;
    } else {
        t->state = PPU_THREAD_STATE_DETACHED;
        t->joinable = 0;
#ifndef _WIN32
        pthread_detach(t->host_thread);
#endif
    }

    table_unlock();
    return CELL_OK;
}

/* ---------------------------------------------------------------------------
 * sys_ppu_thread_yield
 * -----------------------------------------------------------------------*/
int64_t sys_ppu_thread_yield(ppu_context* ctx)
{
    (void)ctx;
#ifdef _WIN32
    SwitchToThread();
#else
    sched_yield();
#endif
    return CELL_OK;
}

/* ---------------------------------------------------------------------------
 * sys_ppu_thread_get_priority
 *
 * r3 = thread_id
 * r4 = pointer to receive priority (s32*)
 * -----------------------------------------------------------------------*/
int64_t sys_ppu_thread_get_priority(ppu_context* ctx)
{
    uint64_t tid       = LV2_ARG_U64(ctx, 0);
    uint32_t prio_addr = LV2_ARG_PTR(ctx, 1);

    table_lock();
    ppu_thread_info* t = find_thread(tid);
    if (!t) {
        table_unlock();
        return (int64_t)(int32_t)CELL_ESRCH;
    }

    if (prio_addr != 0) {
        int32_t prio = t->priority;
        int32_t* out = (int32_t*)vm_to_host(prio_addr);
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__ || defined(_WIN32)
        uint32_t u = (uint32_t)prio;
        u = ((u >> 24) & 0xFF) | ((u >> 8) & 0xFF00) |
            ((u <<  8) & 0xFF0000) | ((u << 24) & 0xFF000000u);
        *out = (int32_t)u;
#else
        *out = prio;
#endif
    }

    table_unlock();
    return CELL_OK;
}

/* ---------------------------------------------------------------------------
 * sys_ppu_thread_set_priority
 *
 * r3 = thread_id
 * r4 = priority
 * -----------------------------------------------------------------------*/
int64_t sys_ppu_thread_set_priority(ppu_context* ctx)
{
    uint64_t tid      = LV2_ARG_U64(ctx, 0);
    int32_t  priority = LV2_ARG_S32(ctx, 1);

    table_lock();
    ppu_thread_info* t = find_thread(tid);
    if (!t) {
        table_unlock();
        return (int64_t)(int32_t)CELL_ESRCH;
    }

    t->priority = priority;
    table_unlock();
    return CELL_OK;
}

/* ---------------------------------------------------------------------------
 * sys_ppu_thread_rename
 *
 * r3 = thread_id
 * r4 = name pointer
 * -----------------------------------------------------------------------*/
int64_t sys_ppu_thread_rename(ppu_context* ctx)
{
    uint64_t tid       = LV2_ARG_U64(ctx, 0);
    uint32_t name_addr = LV2_ARG_PTR(ctx, 1);

    table_lock();
    ppu_thread_info* t = find_thread(tid);
    if (!t) {
        table_unlock();
        return (int64_t)(int32_t)CELL_ESRCH;
    }

    if (name_addr != 0) {
        const char* name = (const char*)vm_to_host(name_addr);
        strncpy(t->name, name, sizeof(t->name) - 1);
        t->name[sizeof(t->name) - 1] = '\0';
    }

    table_unlock();
    return CELL_OK;
}

/* ---------------------------------------------------------------------------
 * sys_ppu_thread_get_join_state
 *
 * r3 = pointer to receive join state (s32*)
 * -----------------------------------------------------------------------*/
int64_t sys_ppu_thread_get_join_state(ppu_context* ctx)
{
    uint32_t out_addr = LV2_ARG_PTR(ctx, 0);
    uint64_t tid = ctx->thread_id;

    table_lock();
    ppu_thread_info* t = find_thread(tid);
    int32_t joinable = t ? t->joinable : 0;
    table_unlock();

    if (out_addr != 0) {
        int32_t* out = (int32_t*)vm_to_host(out_addr);
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__ || defined(_WIN32)
        uint32_t u = (uint32_t)joinable;
        u = ((u >> 24) & 0xFF) | ((u >> 8) & 0xFF00) |
            ((u <<  8) & 0xFF0000) | ((u << 24) & 0xFF000000u);
        *out = (int32_t)u;
#else
        *out = joinable;
#endif
    }

    return CELL_OK;
}

/* ---------------------------------------------------------------------------
 * sys_ppu_thread_get_stack_information
 *
 * r3 = pointer to receive stack info struct
 *   struct { u32 addr; u32 size; }
 * -----------------------------------------------------------------------*/
int64_t sys_ppu_thread_get_stack_information(ppu_context* ctx)
{
    uint32_t out_addr = LV2_ARG_PTR(ctx, 0);
    uint64_t tid = ctx->thread_id;

    table_lock();
    ppu_thread_info* t = find_thread(tid);
    if (!t) {
        table_unlock();
        /* For the main thread, return sensible defaults */
        if (out_addr != 0) {
            uint32_t* out = (uint32_t*)vm_to_host(out_addr);
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__ || defined(_WIN32)
            /* byteswap both fields */
            uint32_t sa = VM_STACK_BASE;
            uint32_t ss = VM_PPU_STACK_SIZE;
            sa = ((sa >> 24) & 0xFF) | ((sa >> 8) & 0xFF00) |
                 ((sa <<  8) & 0xFF0000) | ((sa << 24) & 0xFF000000u);
            ss = ((ss >> 24) & 0xFF) | ((ss >> 8) & 0xFF00) |
                 ((ss <<  8) & 0xFF0000) | ((ss << 24) & 0xFF000000u);
            out[0] = sa;
            out[1] = ss;
#else
            out[0] = VM_STACK_BASE;
            out[1] = VM_PPU_STACK_SIZE;
#endif
        }
        return CELL_OK;
    }

    if (out_addr != 0) {
        uint32_t* out = (uint32_t*)vm_to_host(out_addr);
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__ || defined(_WIN32)
        uint32_t sa = t->stack_addr;
        uint32_t ss = t->stack_size;
        sa = ((sa >> 24) & 0xFF) | ((sa >> 8) & 0xFF00) |
             ((sa <<  8) & 0xFF0000) | ((sa << 24) & 0xFF000000u);
        ss = ((ss >> 24) & 0xFF) | ((ss >> 8) & 0xFF00) |
             ((ss <<  8) & 0xFF0000) | ((ss << 24) & 0xFF000000u);
        out[0] = sa;
        out[1] = ss;
#else
        out[0] = t->stack_addr;
        out[1] = t->stack_size;
#endif
    }

    table_unlock();
    return CELL_OK;
}

/* ---------------------------------------------------------------------------
 * Registration
 * -----------------------------------------------------------------------*/
void sys_ppu_thread_init(lv2_syscall_table* tbl)
{
    /* Initialize stack allocator */
    vm_stack_alloc_init(&g_vm_stack_alloc);

    /* Clear thread table */
    memset(g_ppu_threads, 0, sizeof(g_ppu_threads));

#ifdef _WIN32
    if (!s_table_lock_init) {
        InitializeCriticalSection(&s_table_lock);
        s_table_lock_init = 1;
    }
#endif

    lv2_syscall_register(tbl, SYS_PPU_THREAD_CREATE,              sys_ppu_thread_create);
    lv2_syscall_register(tbl, SYS_PPU_THREAD_EXIT,                sys_ppu_thread_exit);
    lv2_syscall_register(tbl, SYS_PPU_THREAD_YIELD,               sys_ppu_thread_yield);
    lv2_syscall_register(tbl, SYS_PPU_THREAD_JOIN,                sys_ppu_thread_join);
    lv2_syscall_register(tbl, SYS_PPU_THREAD_DETACH,              sys_ppu_thread_detach);
    lv2_syscall_register(tbl, SYS_PPU_THREAD_GET_JOIN_STATE,      sys_ppu_thread_get_join_state);
    lv2_syscall_register(tbl, SYS_PPU_THREAD_SET_PRIORITY,        sys_ppu_thread_set_priority);
    lv2_syscall_register(tbl, SYS_PPU_THREAD_GET_PRIORITY,        sys_ppu_thread_get_priority);
    lv2_syscall_register(tbl, SYS_PPU_THREAD_GET_STACK_INFORMATION, sys_ppu_thread_get_stack_information);
    lv2_syscall_register(tbl, SYS_PPU_THREAD_RENAME,              sys_ppu_thread_rename);
}
