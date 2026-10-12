/* spu_workload.c — SPU workload / task dispatch registry (see spu_workload.h).
 *
 * Maps a registered SPU image (by FNV-1a-64 content fingerprint) to its
 * pre-lifted native entry, loads the image into a 256 KB local store, and runs
 * it with the SPURS task ABI. cellSpurs's AddWorkload/CreateTask call
 * spu_workload_dispatch(); the registry is populated by the title's lifted set.
 */
/* Feature macros ANTES de qualquer include: com -std=c11 o clang/gcc define
 * __STRICT_ANSI__ e as libcs escondem sigaction/siginfo_t/ucontext_t atras
 * destas. Sem isto a isolacao POSIX abaixo nem compila. */
#if !defined(_WIN32)
#  if !defined(_DARWIN_C_SOURCE)
#    define _DARWIN_C_SOURCE 1
#  endif
#  if !defined(_GNU_SOURCE)
#    define _GNU_SOURCE 1
#  endif
#endif
#include "spu_workload.h"
int wp_stats_on(void);   /* winport release: WINPORT_STATS=1 logs the per-second statistics (win_compat.c) */
#include "spu_pm_arg.h"
#include "spu_lifted_job.h"   /* spu_run_lifted_job */
#include "spu_interp.h"       /* image 7: SCREAM policy module */
#include "ps3_checkpoint.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#ifdef __MINGW32__
#include <pthread.h> /* winport: SCREAM LS mutex */
#endif
/* Thunk that runs a lifted SPU job on this (large-stack) DEDICATED thread.
 *
 * Fault isolation: gcc/mingw has NO __try/__except (an MSVC-only extension --
 * confirmed: it fails to compile with "'__try' undeclared"), so we cannot wrap
 * the job in a structured handler. Instead the job runs on its own thread and a
 * vectored exception handler (VEH) intercepts a fatal fault: it logs the RIP
 * (map via nm -> lifted spu function) + fault address and then calls
 * ExitThread(), which tears down ONLY this job thread. The dispatcher's
 * WaitForSingleObject then returns and the host process keeps running instead
 * of dying. A per-thread flag (t_spu_job_thread) scopes the VEH strictly to job
 * threads: on any other thread it returns EXCEPTION_CONTINUE_SEARCH immediately,
 * so a fault on the main thread still propagates exactly as before.
 *
 * The VEH is installed once (process-global, idempotent) rather than per-job:
 * ExitThread() never returns, so a per-job RemoveVectoredExceptionHandler()
 * after the job call would leak on every crash. A permanently-installed handler
 * gated on the thread flag avoids both the leak and any effect on other threads. */
/* ---- winport v79-spu-side (engine_fixes_v79_spu_side.py) ---- */
SPU_THREAD_LOCAL int t_wp79_ls_discard = 0;   /* spu_lifted_job.h: skip the LS copy-out (buffer freed right after) */
static int wp79_nocopyout(void)
{
    static int v = -1;
    if (v < 0) { const char* e = getenv("WINPORT_SPU_LS_NOCOPYOUT"); v = (e && e[0] == '1') ? 1 : 0;
                 if (v) { fprintf(stderr, "[SPUW79] non-resident job LS is not copied back out (freed after the job)\n"); fflush(stderr); } }
    return v;
}
/* ---- end winport v79-spu-side ---- */
struct spu_job_args { spu_lifted_entry_fn fn; uint8_t* ls; uint32_t ea;
                      uint64_t r4_hi, r4_lo; int image_id; };
static __thread int t_spu_job_thread = 0;
static LONG WINAPI spu_job_veh(EXCEPTION_POINTERS* ep)
{
    if (!t_spu_job_thread) return EXCEPTION_CONTINUE_SEARCH; /* untouched elsewhere */
    unsigned code = ep->ExceptionRecord->ExceptionCode;
    int fatal = (code == EXCEPTION_ACCESS_VIOLATION   ||
                 code == EXCEPTION_STACK_OVERFLOW     ||
                 code == EXCEPTION_ILLEGAL_INSTRUCTION||
                 code == EXCEPTION_PRIV_INSTRUCTION   ||
                 code == EXCEPTION_IN_PAGE_ERROR      ||
                 code == EXCEPTION_INT_DIVIDE_BY_ZERO ||
                 code == EXCEPTION_ARRAY_BOUNDS_EXCEEDED);
    if (fatal) {
        fprintf(stderr, "[SPUCRASH] code=0x%08X rip=0x%016llX fault=0x%016llX\n",
                code, (unsigned long long)ep->ContextRecord->Rip,
                code == EXCEPTION_ACCESS_VIOLATION
                    ? (unsigned long long)ep->ExceptionRecord->ExceptionInformation[1] : 0ull);
        fprintf(stderr,
                "[SPUJOB] aborted by SEH code=0x%08X (process continues)\n", code);
        fflush(stderr);
        ExitThread(0xE59);   /* isolate: kill only this job thread; never returns */
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
static void spu_job_install_veh(void)
{
    static volatile LONG installed = 0;
    if (InterlockedCompareExchange(&installed, 1, 0) == 0)
        AddVectoredExceptionHandler(1, spu_job_veh);
}
unsigned long __stdcall spu_job_thunk(void* p)
{
    struct spu_job_args* a = (struct spu_job_args*)p;
    t_spu_job_thread = 1;              /* scope the VEH to this thread */
    spu_job_install_veh();             /* idempotent process-global install */
    t_wp79_ls_discard = (wp79_nocopyout() && a->image_id != 7) ? 1 : 0;   /* winport v79-spu-side: image 7 = resident SCREAM LS */
    spu_run_lifted_job_ex_image(a->fn, a->ls, a->ea, a->r4_hi, a->r4_lo,
                                a->image_id);
    t_wp79_ls_discard = 0;
    { static int _t = -1; if (_t < 0) _t = getenv("PS3_TRACE_SPUJOB") ? 1 : 0;   /* winport: mingw */
      if (_t) { fprintf(stderr, "[SPUJOB] spu job returned cleanly\n"); fflush(stderr); } }
    return 0;
}
/* ---- winport v76b-spu-runner (engine_fixes_v76b_spu_runner.py): WINPORT_SPU_RUNNER=1 ---- */
struct wp76_runner { HANDLE th, go, done; struct spu_job_args* volatile ja; };
static __thread struct wp76_runner* t_wp76_runner = NULL;
static volatile long long g_wp76_rj[4];   /* jobs, runner spawns, runner deaths (job fault), fallbacks to the old path */
static int wp76_runner_on(void)
{
    static int v = -1;
    if (v < 0) {
        const char* e = getenv("WINPORT_SPU_RUNNER");
        v = (e && e[0] == '1') ? 1 : 0;
        if (v) { fprintf(stderr, "[SPUJOB76] persistent SPU job runner threads ON (no CreateThread per dispatch)\n"); fflush(stderr); }
    }
    return v;
}
static unsigned long __stdcall wp76_runner_main(void* p)
{
    struct wp76_runner* r = (struct wp76_runner*)p;
    for (;;) {
        if (WaitForSingleObject(r->go, INFINITE) != WAIT_OBJECT_0) return 1;
        { extern __thread int t_spu_rsv_valid; t_spu_rsv_valid = 0; }   /* a fresh job thread started with no reservation */
        spu_job_thunk((void*)r->ja);   /* a fault ExitThread()s us from spu_job_veh -- the caller notices */
        SetEvent(r->done);
    }
}
static long long wp76_ft(const FILETIME* f) { return (long long)(((unsigned long long)f->dwHighDateTime << 32) | f->dwLowDateTime); }
/* 1 = the job ran on this caller's runner (or died there); 0 = use the old per-job thread. *cpu_ns = -1 if unknown. */
static int wp76_runner_run(struct spu_job_args* ja, long long* cpu_ns)
{
    struct wp76_runner* r = t_wp76_runner;
    *cpu_ns = -1;
    if (!r) {
        r = (struct wp76_runner*)calloc(1, sizeof *r);
        if (!r) return 0;
        r->go = CreateEventW(NULL, FALSE, FALSE, NULL);
        r->done = CreateEventW(NULL, FALSE, FALSE, NULL);
        if (!r->go || !r->done) {
            if (r->go) CloseHandle(r->go);
            if (r->done) CloseHandle(r->done);
            free(r);
            __atomic_add_fetch(&g_wp76_rj[3], 1ll, __ATOMIC_RELAXED);
            return 0;
        }
        t_wp76_runner = r;
    }
    if (!r->th) {
        r->th = CreateThread(NULL, (SIZE_T)256 * 1024 * 1024, wp76_runner_main, r, STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
        if (!r->th) { __atomic_add_fetch(&g_wp76_rj[3], 1ll, __ATOMIC_RELAXED); return 0; }
        {   /* winport v79-spu-side: WINPORT_SPU_RUNNER_PRIO=1 */
            static int pr = -1;
            if (pr < 0) { const char* e = getenv("WINPORT_SPU_RUNNER_PRIO"); pr = (e && e[0] == '1') ? 1 : 0;
                          if (pr) { fprintf(stderr, "[SPUW79] SPU runner threads at ABOVE_NORMAL priority\n"); fflush(stderr); } }
            if (pr) SetThreadPriority(r->th, THREAD_PRIORITY_ABOVE_NORMAL);
        }
        __atomic_add_fetch(&g_wp76_rj[1], 1ll, __ATOMIC_RELAXED);
    }
    FILETIME c_, e_, k0, u0, k1, u1;
    const int t0ok = GetThreadTimes(r->th, &c_, &e_, &k0, &u0);
    r->ja = ja;
    ResetEvent(r->done);
    SetEvent(r->go);
    HANDLE hs[2] = { r->done, r->th };
    const DWORD w = WaitForMultipleObjects(2, hs, FALSE, INFINITE);
    __atomic_add_fetch(&g_wp76_rj[0], 1ll, __ATOMIC_RELAXED);
    if (w == WAIT_OBJECT_0 + 1) {
        /* runner died: spu_job_veh ExitThread()ed it on a job fault -> the job is aborted exactly as with the old
         * per-job thread; the next job spawns a new runner. */
        CloseHandle(r->th);
        r->th = NULL;
        ResetEvent(r->go);
        __atomic_add_fetch(&g_wp76_rj[2], 1ll, __ATOMIC_RELAXED);
    } else {
        if (w != WAIT_OBJECT_0) WaitForSingleObject(r->done, INFINITE);   /* never leave while the job may still run */
        if (t0ok && GetThreadTimes(r->th, &c_, &e_, &k1, &u1))
            *cpu_ns = (wp76_ft(&k1) - wp76_ft(&k0) + wp76_ft(&u1) - wp76_ft(&u0)) * 100ll;
    }
    {   /* [SPUJOB76] once a second */
        static volatile long long last = 0;
        const long long now = (long long)GetTickCount64();
        long long l = __atomic_load_n(&last, __ATOMIC_RELAXED);
        if (!l) __atomic_compare_exchange_n(&last, &l, now, 0, __ATOMIC_RELAXED, __ATOMIC_RELAXED);
        else if (now - l >= 1000 && __atomic_compare_exchange_n(&last, &l, now, 0, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
            const long long n = __atomic_exchange_n(&g_wp76_rj[0], 0ll, __ATOMIC_RELAXED);
            if (!wp_stats_on()) ; else fprintf(stderr, "[SPUJOB76] runner jobs=%.0f/s spawned=%lld died=%lld fallback=%lld (totals)\n",
                    (double)n * 1000.0 / (double)(now - l), (long long)g_wp76_rj[1], (long long)g_wp76_rj[2], (long long)g_wp76_rj[3]);
            fflush(stderr);
        }
    }
    return 1;
}
/* ---- end winport v76b-spu-runner ---- */
#else   /* ------------------------------------------------------------------ */
/* Isolacao de falta de job em POSIX (macOS/Linux) -- o equivalente do VEH.
 *
 * O bloco de cima e' Win32 puro: `AddVectoredExceptionHandler`/`ExitThread` nao
 * existem aqui, portanto ate' agora um job liftado que faltasse (deref fora do
 * local store, salto para lixo, stack overflow) matava o PROCESSO INTEIRO --
 * nao so' o job. Confirmado com o test_seh_isolation: sem isto, exit 139
 * (SIGSEGV) na primeira falta.
 *
 * Mecanica: `sigaction` para os sinais fatais + `siglongjmp` de volta a um
 * landing pad armado com `sigsetjmp` mesmo antes de o job correr.
 *
 *  - sigsetjmp/siglongjmp (e NAO setjmp/longjmp): o handler corre com o sinal
 *    bloqueado, e so' o par sig* repoe a mascara ao saltar. Com setjmp simples o
 *    SIGSEGV ficava mascarado para sempre nessa thread e o SEGUNDO crash matava
 *    o processo -- o oposto do que queremos.
 *
 *  - Landing pad POR THREAD (SPU_THREAD_LOCAL, spu_context.h): os jobs correm
 *    nos workers do kernel SPURS, que sao 2+ (libs/spurs/spurs_kernel.c). Um pad
 *    global faria o handler de uma thread saltar para a stack de outra.
 *
 *  - O handler NAO sequestra os sinais do resto do processo. Instala-se uma so'
 *    vez (removê-lo por job seria uma corrida entre workers concorrentes: o
 *    worker B reporia o default a meio do job do worker A) e e' guardado pelo
 *    pad por thread: se a falta NAO vier de dentro de um job, o handler
 *    reencaminha para a disposicao anterior -- ou, se essa era SIG_DFL, repoe-a
 *    e volta, deixando a instrucao re-executar para o processo morrer com o
 *    crash report normal do SO. Mesmo raciocinio do lado Windows.
 *
 *  - sigaltstack: uma entry liftada com call graph profundo pode estourar a
 *    stack (e' por isso que o lado Windows corre o job numa thread de 256 MB).
 *    Um SIGSEGV de stack overflow nao consegue correr o handler na stack que
 *    acabou de estourar; sem stack alternativa o processo morria na mesma.
 *
 *  - O log sai do LANDING PAD, ja em contexto normal -- nao de dentro do
 *    handler. Faz do `fprintf` uma chamada legitima (dentro do handler seria
 *    async-signal-unsafe). Limite conhecido: se a falta acontecer dentro do
 *    proprio malloc (job a corromper a heap), o lock da heap fica preso e o
 *    fprintf do relatorio pode bloquear. E' o mesmo limite do caminho Windows.
 */
#include <signal.h>
#include <setjmp.h>
#include <pthread.h>
/* <sys/ucontext.h> e nao <ucontext.h>: no macOS este ultimo e' um #error a menos
 * que _XOPEN_SOURCE esteja definido (as rotinas makecontext/swapcontext estao
 * deprecadas). So' queremos o TIPO ucontext_t, que o <sys/...> expoe em ambas as
 * libcs -- e onde a glibc poe tambem o REG_RIP. */
#include <sys/ucontext.h>
#include <stdint.h>

static const int k_spu_fatal_sig[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE };
#define SPU_NFATAL ((int)(sizeof k_spu_fatal_sig / sizeof k_spu_fatal_sig[0]))
static struct sigaction s_spu_prev_sa[SPU_NFATAL];

static int spu_fatal_slot(int sig)
{
    int i;
    for (i = 0; i < SPU_NFATAL; i++) if (k_spu_fatal_sig[i] == sig) return i;
    return -1;
}

/* Estado por thread: para onde saltar e o que reportar quando la' chegarmos. */
static SPU_THREAD_LOCAL sigjmp_buf* t_spu_landing = NULL;
static SPU_THREAD_LOCAL volatile sig_atomic_t t_spu_crash_sig = 0;
static SPU_THREAD_LOCAL volatile uintptr_t    t_spu_crash_pc  = 0;
static SPU_THREAD_LOCAL volatile uintptr_t    t_spu_crash_at  = 0;

#ifndef SPU_ALTSTACK_SIZE
#define SPU_ALTSTACK_SIZE (64 * 1024)   /* SIGSTKSZ nao e' constante na glibc nova */
#endif
static SPU_THREAD_LOCAL char t_spu_altstack[SPU_ALTSTACK_SIZE];
static SPU_THREAD_LOCAL int  t_spu_altstack_armed = 0;

/* PC da instrucao que faltou (mapeavel com nm -> funcao spu liftada). */
static uintptr_t spu_fault_pc(void* uc_)
{
    if (!uc_) return 0;
    {
        ucontext_t* uc = (ucontext_t*)uc_;
#if defined(__APPLE__)
# if defined(__aarch64__) || defined(__arm64__)
        return (uintptr_t)uc->uc_mcontext->__ss.__pc;
# elif defined(__x86_64__)
        return (uintptr_t)uc->uc_mcontext->__ss.__rip;
# endif
#elif defined(__linux__)
# if defined(__x86_64__)
        return (uintptr_t)uc->uc_mcontext.gregs[REG_RIP];
# elif defined(__aarch64__)
        return (uintptr_t)uc->uc_mcontext.pc;
# endif
#endif
        (void)uc;
        return 0;
    }
}

static void spu_job_fault_handler(int sig, siginfo_t* si, void* uc)
{
    if (t_spu_landing) {                       /* falta DENTRO de um job */
        sigjmp_buf* pad = t_spu_landing;
        t_spu_landing    = NULL;               /* nao reentrar neste pad */
        t_spu_crash_sig  = sig;
        t_spu_crash_pc   = spu_fault_pc(uc);
        t_spu_crash_at   = si ? (uintptr_t)si->si_addr : 0;
        siglongjmp(*pad, 1);                   /* nunca volta */
    }
    /* Fora de um job: devolver o sinal a quem o tinha antes de nos. */
    {
        int slot = spu_fatal_slot(sig);
        struct sigaction* prev;
        if (slot < 0) return;
        prev = &s_spu_prev_sa[slot];
        if ((prev->sa_flags & SA_SIGINFO) && prev->sa_sigaction) {
            prev->sa_sigaction(sig, si, uc);
            return;
        }
        if (prev->sa_handler == SIG_IGN) return;
        if (prev->sa_handler != SIG_DFL && prev->sa_handler != NULL) {
            prev->sa_handler(sig);
            return;
        }
        /* Era SIG_DFL: repor e voltar. A instrucao re-executa, falta outra vez
         * e agora com a accao default -- crash report / core dump normais. */
        sigaction(sig, prev, NULL);
    }
}

static void spu_job_install_sigs_once(void)
{
    struct sigaction sa;
    int i;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = spu_job_fault_handler;
    sa.sa_flags     = SA_SIGINFO | SA_ONSTACK | SA_RESTART;
    sigemptyset(&sa.sa_mask);
    for (i = 0; i < SPU_NFATAL; i++)
        sigaction(k_spu_fatal_sig[i], &sa, &s_spu_prev_sa[i]);
}

static void spu_job_install_sigs(void)
{
    static pthread_once_t once = PTHREAD_ONCE_INIT;
    pthread_once(&once, spu_job_install_sigs_once);
}

static void spu_job_arm_altstack(void)
{
    stack_t ss;
    if (t_spu_altstack_armed) return;
    t_spu_altstack_armed = 1;                  /* tentar uma vez por thread */
    ss.ss_sp    = t_spu_altstack;
    ss.ss_size  = sizeof t_spu_altstack;
    ss.ss_flags = 0;
    /* Se falhar (MINSIGSTKSZ maior que o nosso buffer nalguma plataforma), as
     * faltas normais continuam isoladas -- perde-se apenas o caso do estouro de
     * stack. Dizê-lo em vez de degradar em silencio. */
    if (sigaltstack(&ss, NULL) != 0)
        fprintf(stderr, "[spu_workload] sigaltstack falhou: estouro de stack "
                        "num job SPU nao sera isolado nesta thread\n");
}

/* Mesmo formato do lado Windows: o `[SPUCRASH]` e' identico byte a byte (os
 * greps de smoke contam-no), com `code` = numero do sinal. A segunda linha diz
 * "signal" em vez de "SEH" porque aqui nao ha SEH nenhum -- mas mantem o prefixo
 * `[SPUJOB] aborted by`. */
static void spu_job_report_crash(void)
{
    int sig = (int)t_spu_crash_sig;
    const char* nm = sig == SIGSEGV ? "SIGSEGV" : sig == SIGBUS ? "SIGBUS"
                   : sig == SIGILL  ? "SIGILL"  : sig == SIGFPE ? "SIGFPE" : "?";
    fprintf(stderr, "[SPUCRASH] code=0x%08X rip=0x%016llX fault=0x%016llX\n",
            (unsigned)sig, (unsigned long long)t_spu_crash_pc,
            (unsigned long long)t_spu_crash_at);
    fprintf(stderr, "[SPUJOB] aborted by signal %s code=0x%08X (process continues)\n",
            nm, (unsigned)sig);
    fflush(stderr);
}

/* Corre o job com o landing pad armado. Se ele faltar, voltamos AQUI: o job
 * morre, o host continua. Aninhavel (guarda e repoe o pad anterior). */
static void spu_job_run_isolated(spu_lifted_entry_fn fn, uint8_t* ls, uint32_t ea,
                                 uint64_t r4_hi, uint64_t r4_lo, int image_id)
{
    sigjmp_buf  pad;
    /* volatile: um local nao-volatil de uma funcao que chama sigsetjmp tem valor
     * indeterminado depois do salto se tiver sido alterado no meio. `prev` nao e'
     * -- mas marca-lo custa zero e tira a duvida a quem le. */
    sigjmp_buf* volatile prev = t_spu_landing;

    spu_job_install_sigs();
    spu_job_arm_altstack();

    if (sigsetjmp(pad, 1) == 0) {          /* savemask=1: repoe a mascara */
        t_spu_landing = &pad;
        spu_run_lifted_job_ex_image(fn, ls, ea, r4_hi, r4_lo, image_id);
        t_spu_landing = prev;
        /* Mesma linha, byte a byte, que o thunk do Windows imprime. Sem ela um
         * job que TERMINA e um job que ficou preso sao indistinguiveis num log
         * POSIX -- e "o job termina" e' precisamente o que se quer observar.
         * So' as 20 primeiras (ou todas com PS3_TRACE_SPUJOB=1): a cada job,
         * com fflush, fazia 1,1 M linhas por 50 MB de log e 2 GB por sessao
         * de jogo (medido 2026-09-23). */
        static int every = -1;
        static unsigned shown;
        if (every < 0) { const char* e = getenv("PS3_TRACE_SPUJOB"); every = (e && *e && *e != '0'); }
        if (every || __atomic_fetch_add(&shown, 1, __ATOMIC_RELAXED) < 20) {
            fprintf(stderr, "[SPUJOB] spu job returned cleanly\n"); fflush(stderr);
        }
        return;
    }
    /* ---- landing pad: chegamos aqui vindos de uma falta ---- */
    t_spu_landing = prev;
    /* O job foi cortado a meio de spu_run_lifted_job_ex, que nao chegou a
     * limpar o trampolim. Deixa-lo pendurado faria o PROXIMO job desta thread
     * saltar para um fragmento do job morto. */
    g_spu_trampoline_fn = 0;
    spu_job_report_crash();
}
#endif

/* ---- fingerprint ------------------------------------------------------- */

uint64_t spu_workload_fingerprint(const void* data, size_t n)
{
    const uint8_t* p = (const uint8_t*)data;
    uint64_t h = 1469598103934665603ULL;          /* FNV offset basis */
    for (size_t i = 0; i < n; i++) {
        h ^= p[i];
        h *= 1099511628211ULL;                     /* FNV prime */
    }
    return h;
}

/* ---- registry ---------------------------------------------------------- */

#ifndef SPU_WORKLOAD_MAX
#define SPU_WORKLOAD_MAX 256
#endif

typedef struct {
    uint64_t            fp;
    spu_lifted_entry_fn fn;
    const char*         name;
    uint32_t            raw_ls_base;
    int                 image_id;
} spu_workload_entry;

static spu_workload_entry s_registry[SPU_WORKLOAD_MAX];
static unsigned           s_registry_count = 0;

void spu_workload_register(uint64_t fingerprint, spu_lifted_entry_fn fn,
                           const char* name)
{
    spu_workload_register_image(fingerprint, fn, name, 0);
}

void spu_workload_register_image(uint64_t fingerprint, spu_lifted_entry_fn fn,
                                 const char* name, int image_id)
{
    if (!fn) return;
    for (unsigned i = 0; i < s_registry_count; i++) {
        if (s_registry[i].fp == fingerprint) {     /* idempotent on fingerprint */
            s_registry[i].fn   = fn;
            s_registry[i].name = name;
            s_registry[i].image_id = image_id;
            return;
        }
    }
    if (s_registry_count >= SPU_WORKLOAD_MAX) {
        fprintf(stderr, "[spu_workload] registry full (%u); dropping '%s'\n",
                SPU_WORKLOAD_MAX, name ? name : "?");
        return;
    }
    s_registry[s_registry_count].fp   = fingerprint;
    s_registry[s_registry_count].fn   = fn;
    s_registry[s_registry_count].name = name;
    s_registry[s_registry_count].raw_ls_base = 0;
    s_registry[s_registry_count].image_id = image_id;
    s_registry_count++;
    /* Incondicional: sem isto, "a imagem SPU foi registada" nao e observavel
     * num log -- so o caso de registry cheia falava. Uma imagem que nao entra
     * (simbolo em falta, constructor que nao correu, fingerprint trocado) fica
     * indistinguivel de uma que entra e nunca e despachada. */
    fprintf(stderr, "[spu_workload] registered '%s' fp=0x%016llX (%u no total)\n",
            name ? name : "?", (unsigned long long)fingerprint, s_registry_count);
}

void spu_workload_register_raw(uint64_t fingerprint, spu_lifted_entry_fn fn,
                               const char* name, uint32_t raw_ls_base)
{
    spu_workload_register_raw_image(fingerprint, fn, name, raw_ls_base, 0);
}

void spu_workload_register_raw_image(uint64_t fingerprint, spu_lifted_entry_fn fn,
                                     const char* name, uint32_t raw_ls_base,
                                     int image_id)
{
    spu_workload_register_image(fingerprint, fn, name, image_id);
    for (unsigned i = 0; i < s_registry_count; i++)
        if (s_registry[i].fp == fingerprint) {
            s_registry[i].raw_ls_base = raw_ls_base;
            return;
        }
}

spu_lifted_entry_fn spu_workload_find(uint64_t fingerprint)
{
    for (unsigned i = 0; i < s_registry_count; i++)
        if (s_registry[i].fp == fingerprint)
            return s_registry[i].fn;
    return NULL;
}

unsigned spu_workload_count(void) { return s_registry_count; }

/* ---- SPU ELF loader (32-bit big-endian) -------------------------------- */

static uint16_t rd_be16(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }
static uint32_t rd_be32(const uint8_t* p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
}

int spu_elf_load_to_ls(const uint8_t* image, size_t image_size, uint8_t* ls,
                       uint32_t* entry_out)
{
    if (!image || !ls || image_size < 0x34) return 0;

    /* ELF ident: 0x7F 'E' 'L' 'F', ELFCLASS32 (1), ELFDATA2MSB (2). */
    if (!(image[0] == 0x7F && image[1] == 'E' && image[2] == 'L' && image[3] == 'F'))
        return 0;
    if (image[4] != 1 /*ELFCLASS32*/ || image[5] != 2 /*ELFDATA2MSB*/)
        return 0;

    uint32_t e_entry     = rd_be32(image + 0x18);
    uint32_t e_phoff     = rd_be32(image + 0x1C);
    uint16_t e_phentsize = rd_be16(image + 0x2A);
    uint16_t e_phnum     = rd_be16(image + 0x2C);
    if (e_phentsize < 0x20) e_phentsize = 0x20;

    for (uint16_t i = 0; i < e_phnum; i++) {
        size_t po = (size_t)e_phoff + (size_t)i * e_phentsize;
        if (po + 0x20 > image_size) break;
        const uint8_t* ph = image + po;

        uint32_t p_type   = rd_be32(ph + 0x00);
        if (p_type != 1 /*PT_LOAD*/) continue;
        uint32_t p_offset = rd_be32(ph + 0x04);
        uint32_t p_vaddr  = rd_be32(ph + 0x08);
        uint32_t p_filesz = rd_be32(ph + 0x10);
        uint32_t p_memsz  = rd_be32(ph + 0x14);

        /* bounds: segment must fit in local store and in the image */
        if ((uint64_t)p_vaddr + p_memsz > SPU_LS_SIZE)            return 0;
        if ((uint64_t)p_offset + p_filesz > image_size)          return 0;

        if (p_filesz) memcpy(ls + p_vaddr, image + p_offset, p_filesz);
        if (p_memsz > p_filesz)
            memset(ls + p_vaddr + p_filesz, 0, p_memsz - p_filesz);
    }
    if (entry_out) *entry_out = e_entry;
    return 1;
}

size_t spu_elf_image_size(const uint8_t* image, size_t max_avail)
{
    if (!image || max_avail < 0x34) return 0;
    if (!(image[0] == 0x7F && image[1] == 'E' && image[2] == 'L' && image[3] == 'F'))
        return 0;
    if (image[4] != 1 || image[5] != 2) return 0;     /* ELFCLASS32, ELFDATA2MSB */

    uint32_t e_phoff     = rd_be32(image + 0x1C);
    uint32_t e_shoff     = rd_be32(image + 0x20);
    uint16_t e_phentsize = rd_be16(image + 0x2A);
    uint16_t e_phnum     = rd_be16(image + 0x2C);
    uint16_t e_shentsize = rd_be16(image + 0x2E);
    uint16_t e_shnum     = rd_be16(image + 0x30);

    uint64_t end = (uint64_t)e_shoff + (uint64_t)e_shnum * e_shentsize;

    for (uint16_t k = 0; k < e_phnum; k++) {           /* program headers */
        size_t po = (size_t)e_phoff + (size_t)k * e_phentsize;
        if (po + 0x14 > max_avail) break;
        uint32_t p_offset = rd_be32(image + po + 0x04);
        uint32_t p_filesz = rd_be32(image + po + 0x10);
        uint64_t e = (uint64_t)p_offset + p_filesz;
        if (e > end) end = e;
    }
    for (uint16_t k = 0; k < e_shnum; k++) {           /* section headers */
        size_t so = (size_t)e_shoff + (size_t)k * e_shentsize;
        if (so + 0x18 > max_avail) break;
        uint32_t sh_type   = rd_be32(image + so + 0x04);
        uint32_t sh_offset = rd_be32(image + so + 0x10);
        uint32_t sh_size   = rd_be32(image + so + 0x14);
        if (sh_type != 8 /*SHT_NOBITS*/) {
            uint64_t e = (uint64_t)sh_offset + sh_size;
            if (e > end) end = e;
        }
    }
    if (end > max_avail) end = max_avail;
    return (size_t)end;
}

/* ---- dispatch ---------------------------------------------------------- */

static void wr_be32(uint8_t* p, uint32_t v)
{ p[0]=(uint8_t)(v>>24); p[1]=(uint8_t)(v>>16); p[2]=(uint8_t)(v>>8); p[3]=(uint8_t)v; }
static void wr_be64(uint8_t* p, uint64_t v)
{ wr_be32(p, (uint32_t)(v>>32)); wr_be32(p+4, (uint32_t)v); }
static uint64_t rd_be64(const uint8_t* p)
{ return ((uint64_t)rd_be32(p) << 32) | rd_be32(p+4); }
static uint32_t ls_rd32_ext(const uint8_t* p) { return rd_be32(p); }

/* Populate the SpursTasksetContext window (LS 0x2700..0x2FFF) that the SDK
 * task library inside the task ELF reads: taskset temp copy, this task's
 * TaskInfo, the taskset EA / kernel addr / task id fields. Without this the
 * task starts over a zeroed context, reads taskset EA 0 and "finds nothing
 * to do" (observed: thousands of clean early exits, queues never popped).
 * syscallAddr is left 0: a task yield/wait branches to LS 0 which the lifted
 * runtime treats as clean task exit -- pushes re-kick the task.
 * Layout: RPCS3 SpursTasksetContext / spursTasksetDispatch. */
static void spu_task_ls_setup(uint8_t* ls, const uint8_t* vmbase,
                              uint32_t taskset_ea, uint32_t task_id,
                              uint64_t* r4_hi, uint64_t* r4_lo)
{
    const uint8_t* ts = vmbase + taskset_ea;
    memcpy(ls + 0x2700, ts, 0x80);                          /* tempAreaTaskset  */
    memcpy(ls + 0x2780, ts + 0x80 + 48u * task_id, 48);     /* tempAreaTaskInfo */
    /* The task-lib's dispatch check (spu func 0x8E88) reads 16 bytes at
     * [ctx+152] = TaskInfo.context_save (0x2798) and errors (0x8041090F) if
     * 12+ of them are zero. A real context_save is a save-area EA | alloc
     * ls-blocks; our HLE never allocates one, so seed a non-degenerate value
     * (context_save = a scratch EA, alloc_ls_blocks = full LS) so the task
     * proceeds to its work loop instead of the error path. */
    { uint32_t cs = ls_rd32_ext(ls + 0x2798 + 4);          /* current low word */
      if (!cs) {
          wr_be32(ls + 0x2798, 0x00000000);
          wr_be32(ls + 0x279C, taskset_ea | 0x1);          /* save EA (nonzero) */
          wr_be32(ls + 0x27A0, 0x00000000);
          wr_be32(ls + 0x27A4, 0x0003FFFF);                /* alloc ls blocks   */
      } }
    wr_be64(ls + 0x27B8, taskset_ea);                       /* taskset EA       */
    wr_be32(ls + 0x27C0, 0x100);                            /* kernelMgmtAddr   */
    wr_be32(ls + 0x27C4, 0x1FF00);                          /* syscallAddr -> HLE hook */
    wr_be32(ls + 0x27CC, 0);                                /* spuNum           */
    wr_be32(ls + 0x27D0, 8);                                /* dmaTagId         */
    wr_be32(ls + 0x27D4, task_id);                          /* taskId           */
    wr_be32(ls + 0x2FB8, 0x2700);                           /* tasksetMgmtAddr  */
    /* Task-lib context global (BSS at 0xBEC0 in GoW2's edgezlib task-lib):
     * word2 = management-object pointer; the lib calls [word2+196] (= our
     * syscallAddr at 0x27C4) and checks [word2+152] != 0 (= the taskInfo
     * context field at 0x2798). Also seed the kernel low-LS fallback slots
     * for the base==0 path. */
    wr_be32(ls + 0xBEC0 + 8, 0x2700);
    wr_be32(ls + 196, 0x1FF00);                             /* +196: syscall     */
    wr_be32(ls + 152, 0x1FF10);                             /* +152: service #2  */
    *r4_hi = rd_be64(ts + 0x60);                            /* spurs EA         */
    *r4_lo = rd_be64(ts + 0x68);                            /* taskset args     */
}

/* ---- interpreted policy module (SCREAM mixer, image 7) -------------------
 *
 * GoW2's SCREAM policy module cannot be expressed by the static lift: its
 * interrupt handler saves registers with code it GENERATES at LS 0x3FEC0, it
 * takes real SPU interrupts (DMA-list stall-and-notify, bie/bid windows) and it
 * switches context between itself and the mixer job it loads at LS 0x4000.
 * It runs in the SPU interpreter instead (spu_interp.c), against ONE persistent
 * context: local store, registers, pc, interrupt state and the MFC engine
 * (keyed by the context) all survive between dispatches. A dispatch runs at
 * most PS3_SPU_INTERP_BUDGET instructions; a run cut by the budget, or parked
 * on a blocking channel read, resumes where it stopped on the next dispatch.
 * PS3_SPU_INTERP=0 goes back to the lifted spu6 (A/B). */
static int spu_interp_enabled(void)
{
    static int on = -1;
    if (on < 0) {
        const char* e = getenv("PS3_SPU_INTERP");
        on = (e && e[0] == '0') ? 0 : 1;
    }
    return on;
}

static uint64_t spu_interp_budget(void)
{
    static long long b = -1;
    if (b < 0) {
        const char* e = getenv("PS3_SPU_INTERP_BUDGET");
        b = (e && *e) ? atoll(e) : 4000000LL;
        if (b <= 0) b = 4000000LL;
    }
    return (uint64_t)b;
}

uint32_t spu_mfc_tag_completed(spu_context* ctx);   /* spu_channels.c */
static spu_context* s_pm_ictx;        /* image 7 */
static int          s_pm_iresume;     /* last run ended by budget / wait */
static unsigned long long s_pm_iruns, s_pm_iinsns, s_pm_ires[5];
static int          s_pm_ibusy;       /* a worker is running the module */

/* ---- winport v28-scream: LS + coverage dump for lifting the mixer (PS3_SCREAM_DUMP=<dir>) ----
 * spu_interp.c sets a bit per executed instruction / branch target of image 7 while g_wp28_cov.
 * After PS3_SCREAM_DUMP_SEC (default 30) s the local store and both bitmaps are written once. */
extern volatile int g_wp28_cov;
extern unsigned char g_wp28_cov_bits[8192], g_wp28_tgt_bits[8192];
static void wp28_dump_tick(spu_context* ctx)
{
    static int st = -1;
    static unsigned long long t_arm;
    static const char* dir;
    if (st == 0 || st == 2) return;
    extern unsigned long long wp27_now_us(void);
    const unsigned long long now = wp27_now_us();
    if (st < 0) {
        dir = getenv("PS3_SCREAM_DUMP");
        if (!dir || !*dir) { st = 0; return; }
        st = 1; t_arm = now; g_wp28_cov = 1;
        fprintf(stderr, "[SCREAM28] coverage armed, dump to %s\n", dir); fflush(stderr);
        return;
    }
    const char* s = getenv("PS3_SCREAM_DUMP_SEC");
    const unsigned sec = (s && *s) ? (unsigned)atoi(s) : 30u;
    if (now - t_arm < (unsigned long long)sec * 1000000ull) return;
    st = 2; g_wp28_cov = 0;
    char path[1024]; FILE* f;
    snprintf(path, sizeof path, "%s/scream_ls.bin", dir);
    if ((f = fopen(path, "wb"))) { fwrite(ctx->ls, 1, SPU_LS_SIZE, f); fclose(f); }
    snprintf(path, sizeof path, "%s/scream_cov.bin", dir);
    if ((f = fopen(path, "wb"))) { fwrite(g_wp28_cov_bits, 1, sizeof g_wp28_cov_bits, f); fclose(f); }
    snprintf(path, sizeof path, "%s/scream_tgt.bin", dir);
    if ((f = fopen(path, "wb"))) { fwrite(g_wp28_tgt_bits, 1, sizeof g_wp28_tgt_bits, f); fclose(f); }
    unsigned cov = 0, tgt = 0;
    snprintf(path, sizeof path, "%s/scream_cov.txt", dir);
    f = fopen(path, "w");
    for (unsigned w = 0, run0 = 0, in = 0; w <= 0x10000u; w++) {
        const int b = w < 0x10000u && ((g_wp28_cov_bits[w >> 3] >> (w & 7)) & 1);
        if (w < 0x10000u) { cov += b; tgt += (g_wp28_tgt_bits[w >> 3] >> (w & 7)) & 1; }
        if (b && !in) { in = 1; run0 = w; }
        if (!b && in) { in = 0; if (f) fprintf(f, "0x%05X-0x%05X %u insns\n", run0 * 4, w * 4, w - run0); }
    }
    if (f) { fprintf(f, "# pc=0x%05X ie=%d covered=%u targets=%u\n", ctx->pc, ctx->interp_ie, cov, tgt); fclose(f); }
    fprintf(stderr, "[SCREAM28] dumped LS + coverage to %s: %u instructions executed, %u branch targets\n", dir, cov, tgt);
    fflush(stderr);
}

static int spu_workload_run_interp(const uint8_t* image, uint32_t image_size,
                                   uint32_t raw_ls_base, uint32_t args_ea,
                                   uint64_t fp, int image_id)
{
    if (raw_ls_base > SPU_LS_SIZE || image_size > SPU_LS_SIZE - raw_ls_base) return 0;
    /* One SPU runs this module. A dispatch that finds it already running on
     * another worker has nothing to add: the running slice sees the same
     * queue. Do not park a second worker waiting for it. */
    if (__atomic_exchange_n(&s_pm_ibusy, 1, __ATOMIC_ACQUIRE) != 0) {
        static unsigned busy_skips;   /* one-shot: a run that never cleared the flag */
        if (++busy_skips == 5000) {
            fprintf(stderr, "[SPUINTERP] image %d: 5000 dispatches skipped as already running\n", image_id);
            fflush(stderr);
        }
        return 1;
    }
    spu_context* ctx = s_pm_ictx;
    if (!ctx) {
#ifdef _WIN32
        ctx = (spu_context*)_aligned_malloc(sizeof *ctx, 64);
#else
        if (posix_memalign((void**)&ctx, 64, sizeof *ctx) != 0) ctx = NULL;
#endif
        if (!ctx) {
            __atomic_store_n(&s_pm_ibusy, 0, __ATOMIC_RELEASE);
            return 0;
        }
        spu_context_init(ctx, 0);
        ctx->image_id = image_id;
        ctx->interp_spurs_pm = 1;
        memcpy(ctx->ls + raw_ls_base, image, image_size);
        /* Kernel context at LS 0x100 (spursKernelDispatchWorkload, kernel 2). */
        wr_be32(ctx->ls + 0x1CC, 31);         /* dmaTagId */
        wr_be32(ctx->ls + 0x1E0, 0x838);      /* exitToKernelAddr */
        wr_be32(ctx->ls + 0x1E4, 0x290);      /* selectWorkloadAddr */
        s_pm_ictx = ctx;
        s_pm_iresume = 0;
        fprintf(stderr, "[spu_workload] image %d fp=0x%016llX: SPU interpreter, raw LS image "
                "(%u bytes @0x%05X), budget %llu insns/dispatch\n", image_id,
                (unsigned long long)fp, image_size, raw_ls_base,
                (unsigned long long)spu_interp_budget());
    }

    uint32_t pc;
    if (s_pm_iresume) {
        pc = ctx->pc;
    } else {
        /* Fresh entry, registers as spursKernelDispatchWorkload sets them:
         * r0 = exitToKernelAddr, r1 = SP, r3 = kernel context, r4 = { workload
         * data (upper u64), 0 }, r5 = pollStatus. Interrupts off. */
        uint32_t w[4];
        spu_pm_data_r4(args_ea, w);
        ctx->gpr[0] = spu_make_preferred_u32(0x838);
        ctx->gpr[1] = spu_make_preferred_u32(0x3FFB0);
        ctx->gpr[3] = spu_make_preferred_u32(0x100);
        for (int i = 0; i < 4; i++) ctx->gpr[4]._u32[i] = w[i];
        ctx->gpr[5] = spu_make_preferred_u32(0);
        ctx->interp_ie = 0;
        pc = raw_ls_base;
    }

    static int tr = -1;
    if (tr < 0) { const char* e = getenv("PS3_TRACE_SPU_INTERP"); tr = (e && *e && *e != '0') ? 1 : 0; }
    uint64_t t0 = 0;
#if !defined(_WIN32)
    struct timespec ts;
    if (tr) { clock_gettime(CLOCK_MONOTONIC, &ts); t0 = (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec; }
#endif
    if (tr) {                          /* dispatch gaps: SCREAM starved of an SPU */
        static uint64_t last_t0;
        if (last_t0 && t0 - last_t0 > 20000000ull)
            fprintf(stderr, "[SPUINTERP] image %d gap %llu ms since last dispatch\n", image_id,
                    (unsigned long long)((t0 - last_t0) / 1000000ull));
        last_t0 = t0;
    }
    /* winport v27-audio: wall time, result and parked channel of every SCREAM run (the
     * PS3_TRACE_SPU_INTERP timing above is compiled out on _WIN32), optional priority boost
     * (PS3_SCREAM_PRIO=1) and an optional resident slice (PS3_SCREAM_RESIDENT_US=N): a run that ends
     * on BUDGET or WAIT is resumed right here instead of waiting for the next SPURS tick (2 ms
     * usleep in cellSpurs.c) and a free worker to dispatch it again. Defaults: neither. */
    extern unsigned long long wp27_now_us(void);
    extern void wp27_interp_note(unsigned long long, unsigned long long, unsigned long long, int, unsigned, int);
    extern int  wp27_scream_begin(void);
    extern void wp27_scream_end(int);
    extern unsigned wp27_resident_us(void);
    extern void wp27_yield(void);
    extern void wp27_stuck_check(spu_context*, int);
    const int wp27_tok = wp27_scream_begin();
    const unsigned long long wp27_t0 = wp27_now_us();
    int rc = spu_interp_run(ctx, pc, spu_interp_budget());
    {
        unsigned long long ta = wp27_t0, t1 = wp27_now_us();
        const unsigned res_us = wp27_resident_us();
        int cont = 0;
        for (;;) {
            unsigned wch = 255u;
            if (rc == SPU_INTERP_WAIT) {
                const uint32_t wp = ctx->pc & 0x3FFFCu;
                wch = ((((uint32_t)ctx->ls[wp + 2] << 8) | ctx->ls[wp + 3]) >> 7) & 0x7Fu;
            }
            wp27_interp_note(ta, t1, spu_interp_last_count(), rc, wch, cont);
            if (!res_us || (rc != SPU_INTERP_BUDGET && rc != SPU_INTERP_WAIT) || t1 - wp27_t0 >= res_us) break;
            if (rc == SPU_INTERP_WAIT) wp27_yield();
            cont = 1;
            ta = wp27_now_us();
            rc = spu_interp_run(ctx, ctx->pc, spu_interp_budget());
            t1 = wp27_now_us();
        }
    }
    wp27_scream_end(wp27_tok);
    wp28_dump_tick(ctx);   /* winport v28-scream: PS3_SCREAM_DUMP */
    uint64_t dt_us = 0;
#if !defined(_WIN32)
    if (tr) { clock_gettime(CLOCK_MONOTONIC, &ts);
              dt_us = ((uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec - t0) / 1000ull; }
#endif
    s_pm_iresume = (rc == SPU_INTERP_BUDGET || rc == SPU_INTERP_WAIT);
    /* One-shot report: the mixer parked on the same channel read for 2000
     * dispatches in a row is the "audio dies after a few minutes" state
     * (2026-09-23: every block skipped, image 7 never seen running). Say which
     * channel it waits on and the event/DMA state, once per stall. */
    { static unsigned waits; static uint32_t wpc; static int told;
      if (rc == SPU_INTERP_WAIT && ctx->pc == wpc) {
          if (++waits == 2000 && !told) {
              const uint32_t pc = ctx->pc & 0x3FFFCu;
              const uint32_t insn = ((uint32_t)ctx->ls[pc] << 24) | ((uint32_t)ctx->ls[pc + 1] << 16)
                                  | ((uint32_t)ctx->ls[pc + 2] << 8) | ctx->ls[pc + 3];
              told = 1;
              fprintf(stderr, "[SPUINTERP] image %d STUCK: 2000 dispatches waiting at pc=0x%05X "
                      "insn=0x%08X ch=%u ie=%d ev=0x%X mask=0x%X tagmask=0x%X tagdone=0x%X lr=0x%05X\n",
                      image_id, pc, insn, (insn >> 7) & 0x7Fu, ctx->interp_ie, ctx->event_status,
                      ctx->event_mask, ctx->mfc_tag_mask, spu_mfc_tag_completed(ctx),
                      ctx->gpr[0]._u32[0]);
              fflush(stderr);
          }
      } else {
          if (told && waits >= 2000) {
              fprintf(stderr, "[SPUINTERP] image %d resumed after %u waiting dispatches\n", image_id, waits);
              fflush(stderr); told = 0;
          }
          waits = 0; wpc = ctx->pc;
      } }
    wp27_stuck_check(ctx, rc);   /* winport v27-audio: stuck detail + PS3_SCREAM_UNSTICK */
    s_pm_iruns++;
    s_pm_iinsns += spu_interp_last_count();
    if (rc >= 0 && rc < 5) s_pm_ires[rc]++;
    if (tr && dt_us > 20000)
        fprintf(stderr, "[SPUINTERP] image %d slow dispatch %llu us (%llu insns, %s)\n", image_id,
                (unsigned long long)dt_us, (unsigned long long)spu_interp_last_count(),
                spu_interp_result_name(rc));
    if (tr && (s_pm_iruns <= 20 || (s_pm_iruns % 500) == 0))
        fprintf(stderr, "[SPUINTERP] image %d dispatch #%llu -> %s pc=0x%05X %llu insns in %llu us "
                "(total %llu insns; exit %llu stop %llu budget %llu wait %llu fault %llu)\n", image_id,
                s_pm_iruns, spu_interp_result_name(rc), ctx->pc,
                (unsigned long long)spu_interp_last_count(), (unsigned long long)dt_us, s_pm_iinsns,
                s_pm_ires[0], s_pm_ires[1], s_pm_ires[2], s_pm_ires[3], s_pm_ires[4]);
    __atomic_store_n(&s_pm_ibusy, 0, __ATOMIC_RELEASE);
    return 1;
}

int spu_workload_dispatch(const uint8_t* image, uint32_t image_size,
                          uint32_t args_ea)
{
    return spu_workload_dispatch_task(image, image_size, args_ea, 0, 0);
}

int spu_workload_dispatch_task(const uint8_t* image, uint32_t image_size,
                               uint32_t args_ea, uint32_t taskset_ea,
                               uint32_t task_id)
{
    if (!image || image_size == 0) return 0;
    extern unsigned long long wp32_now_ns(void);
    static int wp79_st = -1;   /* winport v79-spu-side */
    if (wp79_st < 0) { const char* e = getenv("WINPORT_SPU_STATS79"); wp79_st = (e && e[0] == '1') ? 1 : 0; }
    const unsigned long long wp79_t0 = wp79_st ? wp32_now_ns() : 0ull;

    uint64_t fp = spu_workload_fingerprint(image, image_size);
    spu_lifted_entry_fn fn = NULL;
    uint32_t raw_ls_base = 0;
    int image_id = 0;
    for (unsigned i = 0; i < s_registry_count; i++)
        if (s_registry[i].fp == fp) {
            fn = s_registry[i].fn;
            raw_ls_base = s_registry[i].raw_ls_base;
            image_id = s_registry[i].image_id;
            break;
        }
    { static int trace_image = -1;
      if (trace_image < 0) trace_image = getenv("PS3_SPU_IMAGE_TRACE") ? 1 : 0;
      if (trace_image)
          fprintf(stderr, "[SPUIMAGE] fp=0x%016llX entry=%p image=%d\n",
                  (unsigned long long)fp, (void*)fn, image_id); }
    if (!fn) {
        fprintf(stderr,
            "[spu_workload] dispatch MISS fp=0x%016llX size=%u "
            "(no lifted SPU binary registered for this image)\n",
            (unsigned long long)fp, image_size);
        /* Env-gated dump of the in-memory image so it can be diffed against
         * the statically extracted SPU ELFs (they hash differently: the game
         * patches the image before dispatch). */
        { static int dmp = -1; if (dmp < 0) dmp = getenv("PS3_SPU_DUMP") ? 1 : 0;
          if (dmp) { static int n = 0;
              if (n < 8) { char path[64];
                  snprintf(path, sizeof path, "spu_miss_%016llx.bin", (unsigned long long)fp);
                  FILE* f = fopen(path, "wb");
                  if (f) { fwrite(image, 1, image_size, f); fclose(f);
                           fprintf(stderr, "[spu_workload] dumped -> %s\n", path); }
                  n++; } } }
        return 0;
    }

    if (raw_ls_base && image_id == 7 && !taskset_ea && spu_interp_enabled())
        return spu_workload_run_interp(image, image_size, raw_ls_base, args_ea, fp, image_id);

    /* Load the SPU ELF into a fresh local store, then run the lifted entry with
     * the task arg in r3. 256 KB is heap-allocated (too large for the stack,
     * and spu_run_lifted_job already builds a full spu_context on its stack). */
    /* E213: PS3_SPU_DUMP_HIT=1 despeja tambem as imagens reconhecidas (uma vez por fingerprint),
     * para re-liftar com --extra-funcs quando o lift nao cobre um alvo de bi (ex.: LS 0x3308). */
    { static int dh = -1; if (dh < 0) dh = getenv("PS3_SPU_DUMP_HIT") ? 1 : 0;
      if (dh) { static uint64_t seen[16]; static int ns = 0; int k, dup = 0;
          for (k = 0; k < ns; k++) if (seen[k] == fp) dup = 1;
          if (!dup && ns < 16) { char path[64]; seen[ns++] = fp;
              snprintf(path, sizeof path, "spu_hit_%016llx.bin", (unsigned long long)fp);
              FILE* f = fopen(path, "wb"); if (f) { fwrite(image, 1, image_size, f); fclose(f);
                  fprintf(stderr, "[spu_workload] dumped HIT -> %s (%u bytes)\n", path, image_size); } } } }
    /* Resident LS for the SCREAM policy module (image 7). RPCS3's kernel
     * keeps local store across the branch to 0x2CF0/0x2D38 and re-enters
     * the module. A zeroed LS every submit leaves 0x12A0 idle, so the
     * mixer only polls and never writes the cellAudio ring. */
    static uint8_t* s_scream_ls;
    static int s_scream_ls_ready;
#if !defined(_WIN32) || defined(__MINGW32__) /* winport */
    static pthread_mutex_t s_scream_ls_mu = PTHREAD_MUTEX_INITIALIZER;
#endif
    const int scream_resident = (raw_ls_base != 0 && image_id == 7);
#if !defined(_WIN32) || defined(__MINGW32__) /* winport */
    if (scream_resident) pthread_mutex_lock(&s_scream_ls_mu);
#endif
    uint8_t* ls = scream_resident ? s_scream_ls : NULL;
    if (scream_resident && !ls) {
        ls = (uint8_t*)calloc(1, SPU_LS_SIZE);
        s_scream_ls = ls;
    } else if (!scream_resident) {
        ls = (uint8_t*)calloc(1, SPU_LS_SIZE);
    }
    if (!ls) {
#if !defined(_WIN32) || defined(__MINGW32__) /* winport */
        if (scream_resident) pthread_mutex_unlock(&s_scream_ls_mu);
#endif
        return 0;
    }

    uint32_t entry = 0;
    if (scream_resident && s_scream_ls_ready) {
        entry = raw_ls_base;
    } else if (!spu_elf_load_to_ls(image, image_size, ls, &entry)) {
        /* Some SDK workloads (notably GoW2's Scream audio job) are submitted
         * as a raw local-store image rather than an ELF.  A raw image is safe
         * to accept only after its exact byte fingerprint has matched an
         * explicitly registered lifted entry above. */
        if (raw_ls_base > SPU_LS_SIZE || image_size > SPU_LS_SIZE - raw_ls_base) {
            fprintf(stderr, "[spu_workload] dispatch fp=0x%016llX: raw image exceeds local store\n",
                    (unsigned long long)fp);
#if !defined(_WIN32) || defined(__MINGW32__) /* winport */
            if (scream_resident) pthread_mutex_unlock(&s_scream_ls_mu);
            else
#endif
            free(ls);
            return 0;
        }
        memcpy(ls + raw_ls_base, image, image_size);
        entry = raw_ls_base;
        /* Policy modules (SCREAM) load the kernel stack pointer from LS
         * 0x1540 before they touch the workload argument. A zero there makes
         * the prologue store through a null LS address and return without
         * ever mixing. Point it at an unused spot above the image. */
        /* RPCS3 spursKernelDispatchWorkload: SP = 0x3FFB0, kernel context
         * lives at LS 0x100. The policy module reads the stack pointer from
         * LS 0x1540 (not from r1) and the exit address from context+0xE0. */
        /* The image carries its own stack pointer at LS 0x1540 once it sits at
         * its real base (0xA00); nothing to seed there. Kernel context at LS
         * 0x100 as RPCS3's spursKernelDispatchWorkload leaves it: dmaTagId and
         * the kernel's exit / select-workload entry points (kernel 2). Those
         * addresses are not lifted, so reaching them ends the job -- the
         * module handing control back to the kernel. */
        wr_be32(ls + 0x1CC, 31);          /* dmaTagId */
        wr_be32(ls + 0x1E0, 0x838);       /* exitToKernelAddr (CELL_SPURS_KERNEL2_EXIT_ADDR) */
        wr_be32(ls + 0x1E4, 0x290);       /* selectWorkloadAddr */
        if (scream_resident) s_scream_ls_ready = 1;
        fprintf(stderr, "[spu_workload] dispatch fp=0x%016llX: raw LS image (%u bytes @0x%05X)\n",
                (unsigned long long)fp, image_size, raw_ls_base);
    }

    /* SPURS taskset task: give it the LS context + r4 the SDK task-lib needs.
     * A raw policy module is not a task: its workload `data` pointer arrives
     * as r3's source address today, but the PM reads it from r4 (upper u64,
     * promoted by shlqbyi 4 into the preferred slot). */
    uint64_t r4_hi = 0, r4_lo = 0;
    uint32_t run_args = args_ea;
    if (taskset_ea) {
        extern unsigned char* vm_base;
        spu_task_ls_setup(ls, vm_base, taskset_ea, task_id, &r4_hi, &r4_lo);
    } else if (raw_ls_base && args_ea) {
        uint32_t w[4];
        spu_pm_data_r4(args_ea, w);
        r4_hi = ((uint64_t)w[0] << 32) | w[1];
        r4_lo = ((uint64_t)w[2] << 32) | w[3];
        run_args = 0x100;   /* r3 = kernel context (spursKernelDispatchWorkload) */
    }

    /* Optional per-job timing for A/B of a lifted image.  It is deliberately
     * outside the normal path: no clock read or logging unless requested. */
    static int trace_timing = -1;
    if (trace_timing < 0) {
        const char* e = getenv("PS3_TRACE_SPU_TIMING");
        trace_timing = (e && e[0] == '1') ? 1 : 0;
    }
    uint64_t timing_start_ns = 0;
#if !defined(_WIN32)
    if (trace_timing) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        timing_start_ns = (uint64_t)ts.tv_sec * 1000000000ull +
                          (uint64_t)ts.tv_nsec;
    }
#endif

    { static int hits = 0;   /* contador de diagnostico partilhado entre workers */
      if (__atomic_fetch_add(&hits, 1, __ATOMIC_RELAXED) < 40)
          fprintf(stderr,
              "[spu_workload] dispatch HIT fp=0x%016llX entry=0x%05X args=0x%08X r4=0x%08X ts=0x%08X image=%d task=%u -> running\n",
              (unsigned long long)fp, entry, run_args, (unsigned)r4_hi, taskset_ea, image_id, task_id); }

    /* Task 2 (bringup-instrumentacao-fase0): FIRST_SPU_JOB -- primeiro job
     * SPU genuinamente despachado (imagem reconhecida + LS carregada, prestes
     * a correr), nao uma tentativa MISS (fp desconhecido, ver acima). */
    ps3_checkpoint_fire(PS3_CP_FIRST_SPU_JOB, "spu_workload_dispatch_task",
                       "fp=0x%016llX entry=0x%X args_ea=0x%X",
                       (unsigned long long)fp, entry, args_ea);

    /* The SPU lifter turns `brsl` into direct nested C calls, so a decoder
     * with a deep call graph (spu1 = MPEG) can exceed the default 1 MB host
     * stack and SIGSEGV before any DMA. Run the job on a dedicated thread
     * with a 256 MB stack so deep-but-finite recursion completes. */
#ifdef _WIN32
    {
        struct spu_job_args ja = { fn, ls, run_args, r4_hi, r4_lo, image_id };
        /* STACK_SIZE_PARAM_IS_A_RESERVATION: reserve (not commit) the 256 MB —
         * committing it per job starved the process page file ("arquivo de
         * paginação muito pequeno" load failures in ~2/5 spu1 runs). */
        extern unsigned long long wp32_now_ns(void); extern volatile long long g_wp32_job_n[16], g_wp32_job_ns[16];   /* winport v32-gcmdiag-l */
        const unsigned long long wp32_j0 = wp32_now_ns();
        if (wp79_t0) { extern void wp79_note_setup(long long); wp79_note_setup((long long)(wp32_j0 - wp79_t0)); }   /* winport v79-spu-side */
        long long wp76_cpu = -1;
        if (wp76_runner_on() && wp76_runner_run(&ja, &wp76_cpu)) {   /* winport v76b-spu-runner */
            extern volatile long long g_wp32_job_cpu[16];
            if (wp76_cpu > 0) __atomic_add_fetch(&g_wp32_job_cpu[image_id & 15], wp76_cpu, __ATOMIC_RELAXED);
            __atomic_add_fetch(&g_wp32_job_n[image_id & 15], 1ll, __ATOMIC_RELAXED);
            __atomic_add_fetch(&g_wp32_job_ns[image_id & 15], (long long)(wp32_now_ns() - wp32_j0), __ATOMIC_RELAXED);
        } else {
        HANDLE h = CreateThread(NULL, (SIZE_T)256 * 1024 * 1024,
                                spu_job_thunk, &ja, STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
        if (h) { WaitForSingleObject(h, INFINITE);
                 { FILETIME c_, e_, k_, u_; extern volatile long long g_wp32_job_cpu[16];
                   if (GetThreadTimes(h, &c_, &e_, &k_, &u_)) {
                       const long long cpu = (long long)(((unsigned long long)k_.dwHighDateTime << 32 | k_.dwLowDateTime) +
                                                         ((unsigned long long)u_.dwHighDateTime << 32 | u_.dwLowDateTime)) * 100ll;
                       __atomic_add_fetch(&g_wp32_job_cpu[image_id & 15], cpu, __ATOMIC_RELAXED); } }
                 CloseHandle(h);
                 __atomic_add_fetch(&g_wp32_job_n[image_id & 15], 1ll, __ATOMIC_RELAXED);
                 __atomic_add_fetch(&g_wp32_job_ns[image_id & 15], (long long)(wp32_now_ns() - wp32_j0), __ATOMIC_RELAXED); }
        else    spu_run_lifted_job_ex_image(fn, ls, run_args, r4_hi, r4_lo, image_id);
        }   /* winport v76b-spu-runner */
    }
#else
    /* POSIX: o job corre NESTA thread (a do worker SPURS) com o landing pad
     * armado -- uma falta aborta o JOB, nao o processo.
     *
     * O que fica em aberto: o lado Windows nao corre o job aqui, corre-o numa
     * thread dedicada de 256 MB porque o lifter transforma `brsl` em chamadas C
     * aninhadas e um decoder fundo (spu1 = MPEG) estoura a stack normal. Aqui o
     * job herda a stack do worker, que o spurs_kernel.c cria com
     * pthread_create(attr=NULL): MEDIDO em 512 KB no macOS (vs 8 MB da thread
     * principal). Um job fundo passa portanto a ser ABORTADO de forma limpa em
     * vez de completar -- o host sobrevive (que e' o objectivo desta parede),
     * mas para o job fazer trabalho util falta ainda dar-lhe uma stack grande,
     * como no Windows. Ver a nota de promocao em gow2_spu_register.c. */
    spu_job_run_isolated(fn, ls, run_args, r4_hi, r4_lo, image_id);
#endif

    if (trace_timing) {
#if !defined(_WIN32)
        struct timespec timing_end;
        clock_gettime(CLOCK_MONOTONIC, &timing_end);
        const uint64_t end_ns = (uint64_t)timing_end.tv_sec * 1000000000ull +
                                (uint64_t)timing_end.tv_nsec;
        static unsigned timing_logs;
        if (timing_logs++ < 256u)
            fprintf(stderr, "[SPUTIME] fp=0x%016llX image=%d entry=0x%05X "
                    "task=%u elapsed_us=%llu\n",
                    (unsigned long long)fp, image_id, entry, task_id,
                    (unsigned long long)((end_ns - timing_start_ns) / 1000ull));
#endif
    }

#if !defined(_WIN32) || defined(__MINGW32__) /* winport */
    if (scream_resident) pthread_mutex_unlock(&s_scream_ls_mu);
    else
#endif
    free(ls);
    return 1;
}
