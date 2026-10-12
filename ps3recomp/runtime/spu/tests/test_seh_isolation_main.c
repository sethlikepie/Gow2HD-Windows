/* test_seh_isolation_main.c -- isolacao de crash de job SPU (parede W12).
 *
 * Um job SPU liftado que falta (deref invalido, salto para lixo, overflow de
 * stack) NAO pode derrubar o processo do host: o dispatcher tem de reportar o
 * crash e continuar. No Windows isso e' feito por um vectored exception handler
 * + ExitThread; em POSIX por sigaction(SIGSEGV/SIGBUS/...) + siglongjmp para um
 * landing pad POR THREAD. Este teste exercita o CAMINHO REAL
 * (spu_workload_dispatch -> spu_run_lifted_job_ex -> entry liftada), nao uma
 * replica da mecanica, e passa sse:
 *
 *   1) um job que falta nao mata o host        -- o main volta do dispatch;
 *   2) o job foi mesmo CORTADO                 -- a linha a seguir a falta nunca
 *                                                 correu;
 *   3) o crash foi REPORTADO                   -- "[SPUCRASH]" no stderr, mesmo
 *                                                 formato que o Windows emite;
 *   4) um job limpo ainda corre a seguir       -- a maquinaria nao ficou presa;
 *   5) N threads a faltar em simultaneo        -- landing pad e' por thread
 *                                                 (os workers do kernel SPURS sao
 *                                                 2+; um pad partilhado saltaria
 *                                                 para a stack da thread errada);
 *   6) um job limpo ainda corre depois disso;
 *   7) uma falta FORA de um job NAO e' engolida -- vai parar ao handler do
 *      proprio host. Sem isto a "isolacao" teria sequestrado os sinais do
 *      processo inteiro e escondido crashes a serio.
 *
 * As "imagens" registadas sao blobs curtos que nao sao ELF: o dispatch aceita-os
 * pelo caminho "raw LS image", que so' e' alcancado DEPOIS do fingerprint casar
 * com uma entry registada -- exactamente o que queremos aqui.
 */
#include "spu_workload.h"
#include "spu_context.h"
#include "ps3_checkpoint.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Correcao de regressao (bringup-instrumentacao-fase0, Tasks 1-2 vs.
 * run_tests.sh): spu_workload.c chama ps3_checkpoint_fire() (FIRST_SPU_JOB),
 * por isso este harness liga o $CHECKPOINT_SRC partilhado (ps3_trace.c +
 * ps3_checkpoint.c -- ver run_tests.sh). Correcao pos-review (Task 4
 * Minor #3): os quatro stubs que ps3_checkpoint.c precisa ficam num header
 * partilhado -- ver runtime/trace/tests/trace_link_stubs.h. */
#include "trace_link_stubs.h"

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <io.h>
#  define DUP   _dup
#  define DUP2  _dup2
#  define CLOSE _close
#else
#  include <unistd.h>
#  include <pthread.h>
#  define DUP   dup
#  define DUP2  dup2
#  define CLOSE close
#endif

/* ---- simbolos que o build completo traz de ppu_loader/spu_channels -------- */
unsigned char* vm_base = 0;
uint32_t       ppu_vm_size = 0;
/* O lifter emite `g_spu_trampoline_fn` para o salto entre fragmentos e
 * spu_run_lifted_job drena-o; em build completo vive em spu_channels.c, que
 * estas harnesses nao linkam. */
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*) = 0;

/* ---- os jobs -------------------------------------------------------------- */

/* Ponteiro por uma variavel VOLATIL: um `*(int*)0 = x` literal e' UB e a -O2 o
 * clang tem licenca para o apagar ou reduzir a `brk`/`ud2`. Lendo o endereco de
 * uma variavel volatil o compilador nao pode assumir nada sobre ele e emite a
 * store a serio -- que e' o que precisamos de faltar. */
static int* volatile g_bad_ptr = NULL;

static volatile int g_crash_job_finished = 0;   /* tem de ficar 0 */
static volatile int g_clean_job_ran      = 0;

static void crashing_entry(spu_context* ctx)
{
    (void)ctx;
    *g_bad_ptr = 0xDEAD;            /* <-- SIGSEGV/SIGBUS aqui */
    g_crash_job_finished = 1;       /* inalcancavel se a isolacao funcionar */
}

static void clean_entry(spu_context* ctx)
{
    (void)ctx;
    g_clean_job_ran = 1;
}

/* Imagens sinteticas (nao-ELF) so' para dar dois fingerprints distintos. */
static uint8_t kCrashImage[64];
static uint8_t kCleanImage[64];

/* ---- captura de stderr (para provar o formato do log) --------------------- */

static char g_cap[8192];
static int  g_cap_saved_fd = -1;
static char g_cap_path[512];

static void cap_path_init(void)
{
#ifdef _WIN32
    const char* dir = getenv("TEMP"); if (!dir) dir = ".";
#else
    const char* dir = getenv("TMPDIR"); if (!dir) dir = "/tmp";
#endif
    snprintf(g_cap_path, sizeof g_cap_path, "%s/spu_seh_isolation_stderr.tmp", dir);
}

static int cap_begin(void)
{
    FILE* f;
    fflush(stderr);
    g_cap_saved_fd = DUP(2);
    if (g_cap_saved_fd < 0) return 0;
    f = fopen(g_cap_path, "w+");
    if (!f) { CLOSE(g_cap_saved_fd); g_cap_saved_fd = -1; return 0; }
    DUP2(fileno(f), 2);
    fclose(f);                       /* o fd 2 ja aponta para o ficheiro */
    return 1;
}

static void cap_end(void)
{
    FILE* f;
    size_t got = 0;
    fflush(stderr);
    if (g_cap_saved_fd >= 0) { DUP2(g_cap_saved_fd, 2); CLOSE(g_cap_saved_fd); }
    g_cap_saved_fd = -1;
    f = fopen(g_cap_path, "rb");
    if (f) { got = fread(g_cap, 1, sizeof g_cap - 1, f); fclose(f); }
    g_cap[got] = '\0';
    remove(g_cap_path);
}

/* ---- threads (o cenario real: workers do kernel SPURS) -------------------- */

#define NWORKERS 4
static volatile int g_worker_returned[NWORKERS];

static void worker_body(long idx)
{
    spu_workload_dispatch(kCrashImage, (uint32_t)sizeof kCrashImage, 0);
    g_worker_returned[idx] = 1;      /* so' chega aqui se o job foi isolado */
}

#ifdef _WIN32
typedef HANDLE thread_t;
static unsigned long __stdcall worker_tramp(void* a)
{ worker_body((long)(intptr_t)a); return 0; }
static int thread_start(thread_t* t, long idx)
{ *t = CreateThread(NULL, 0, worker_tramp, (void*)(intptr_t)idx, 0, NULL);
  return *t != NULL; }
static void thread_join(thread_t t) { WaitForSingleObject(t, INFINITE); CloseHandle(t); }
#else
typedef pthread_t thread_t;
static void* worker_tramp(void* a) { worker_body((long)(intptr_t)a); return NULL; }
static int thread_start(thread_t* t, long idx)
{ return pthread_create(t, NULL, worker_tramp, (void*)(intptr_t)idx) == 0; }
static void thread_join(thread_t t) { pthread_join(t, NULL); }
#endif

/* ---- sentinela do host (prova que os sinais nao foram sequestrados) ------- */
#ifndef _WIN32
#include <signal.h>
#include <setjmp.h>
static volatile sig_atomic_t g_sentinel_hits = 0;
static sigjmp_buf g_sentinel_pad;
static void sentinel_handler(int sig)
{
    (void)sig;
    g_sentinel_hits++;
    siglongjmp(g_sentinel_pad, 1);   /* so' para o teste poder continuar */
}
#endif

/* -------------------------------------------------------------------------- */

static int check(const char* what, int ok)
{
    printf("  %-46s %s\n", what, ok ? "OK" : "FAIL");
    return ok;
}

int main(void)
{
    uint64_t fp_crash, fp_clean;
    thread_t th[NWORKERS];
    int i, started = 0, all_returned = 1, ok = 1;

    for (i = 0; i < (int)sizeof kCrashImage; i++) {
        kCrashImage[i] = (uint8_t)(0xC0 + i);
        kCleanImage[i] = (uint8_t)(0x10 + i);
    }
    cap_path_init();

#ifndef _WIN32
    /* Faz de conta que somos um host com o seu proprio reporter de crashes,
     * instalado ANTES de o runtime SPU armar o dele. Tem de continuar a ser
     * chamado para faltas que nao venham de dentro de um job. */
    {
        struct sigaction sent;
        memset(&sent, 0, sizeof sent);
        sent.sa_handler = sentinel_handler;
        sigemptyset(&sent.sa_mask);
        sent.sa_flags = 0;
        sigaction(SIGSEGV, &sent, NULL);
        sigaction(SIGBUS,  &sent, NULL);
    }
#endif

    fp_crash = spu_workload_fingerprint(kCrashImage, sizeof kCrashImage);
    fp_clean = spu_workload_fingerprint(kCleanImage, sizeof kCleanImage);
    spu_workload_register(fp_crash, crashing_entry, "seh_crash_job");
    spu_workload_register(fp_clean, clean_entry,    "seh_clean_job");

    /* 1+2+3) job que falta: host sobrevive, job cortado, crash reportado. */
    if (!cap_begin()) { fprintf(stderr, "FAIL: nao consegui capturar o stderr\n"); return 1; }
    spu_workload_dispatch(kCrashImage, (uint32_t)sizeof kCrashImage, 0);
    cap_end();

    ok &= check("host sobreviveu ao job que falta", 1);   /* chegar aqui ja e' a prova */
    ok &= check("job cortado na falta (nao terminou)", g_crash_job_finished == 0);
    ok &= check("crash reportado com [SPUCRASH]", strstr(g_cap, "[SPUCRASH]") != NULL);
    ok &= check("crash reportado com [SPUJOB] aborted by",
                strstr(g_cap, "[SPUJOB] aborted by") != NULL);

    /* 4) a maquinaria continua utilizavel. */
    spu_workload_dispatch(kCleanImage, (uint32_t)sizeof kCleanImage, 0);
    ok &= check("job limpo corre depois do crash", g_clean_job_ran == 1);

    /* 5) landing pad por thread: N jobs a faltar em paralelo. */
    for (i = 0; i < NWORKERS; i++) {
        if (thread_start(&th[i], i)) started++;
        else fprintf(stderr, "FAIL: thread_start(%d)\n", i);
    }
    for (i = 0; i < started; i++) thread_join(th[i]);
    for (i = 0; i < started; i++) if (!g_worker_returned[i]) all_returned = 0;
    ok &= check("N threads a faltar em paralelo: todas voltam",
                started == NWORKERS && all_returned);

    /* 6) e ainda corre um job limpo depois disso tudo. */
    g_clean_job_ran = 0;
    spu_workload_dispatch(kCleanImage, (uint32_t)sizeof kCleanImage, 0);
    ok &= check("job limpo corre depois dos crashes paralelos", g_clean_job_ran == 1);

#ifndef _WIN32
    /* 7) falta FORA de qualquer job: o handler do runtime SPU tem de a
     *    reencaminhar para a sentinela do host, nao engoli-la. */
    if (sigsetjmp(g_sentinel_pad, 1) == 0)
        *g_bad_ptr = 0xBEEF;
    ok &= check("falta fora de job chega ao handler do host",
                g_sentinel_hits == 1);
#endif

    printf("  %-46s %s\n", "isolacao de crash de job SPU", ok ? "OK" : "FAIL");
    if (ok) printf("  PASS: um job SPU que falta nao derruba o host.\n");
    return ok ? 0 : 1;
}
