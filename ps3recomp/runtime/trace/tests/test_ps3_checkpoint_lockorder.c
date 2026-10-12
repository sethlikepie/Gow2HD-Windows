/* test_ps3_checkpoint_lockorder.c -- regressao do achado do review pos-Task 2
 * (bringup-instrumentacao-fase0): self-deadlock de boot quando um accessor de
 * SNAPSHOT reentra um lock que o CALLER de ps3_checkpoint_fire() ja detinha.
 *
 * Bug real (corrigido): runtime/syscalls/sys_ppu_thread.c:sys_ppu_thread_create()
 * chama ps3_checkpoint_fire(PS3_CP_FIRST_THREAD, ...) de DENTRO da seccao
 * critica de table_lock(). Com PS3_TRACE_SNAPSHOT=1, ps3_checkpoint_fire()
 * chamava ps3_ppu_thread_alive_count(), cujo PRIMEIRO gesto era table_lock()
 * outra vez -- pthread_mutex_t (PTHREAD_MUTEX_INITIALIZER) e' NAO-recursivo
 * em macOS/Linux, logo a mesma thread autodeadlockava. Reproduzido em boot
 * real (3 runs independentes, ver .superpowers/sdd/task-2-report.md). O fix
 * torna o accessor lock-free (ver o comentario em sys_ppu_thread.c).
 *
 * Este ficheiro nao liga sys_ppu_thread.c (arrastaria o runtime inteiro --
 * mesmo principio de test_ps3_checkpoint.c); em vez disso reproduz a MESMA
 * classe de hazard com um stub PROPRIO cujo comportamento e' escolhido em
 * tempo de compilacao por LOCKORDER_REPRO_BUG:
 *
 *   -DLOCKORDER_REPRO_BUG=1  accessor toma o MESMO lock que o "caller"
 *                            (main(), abaixo) ja detem antes de chamar
 *                            ps3_checkpoint_fire() -- exactamente o padrao
 *                            PRE-FIX de table_lock()/table_unlock(). TEM de
 *                            deadlockar (watchdog mata o processo e reporta
 *                            exit 1) -- prova que o padrao e' um hazard real,
 *                            nao hipotetico.
 *   -DLOCKORDER_REPRO_BUG=0  (default) accessor lock-free -- o padrao do FIX
 *                            real em ps3_ppu_thread_alive_count(). TEM de
 *                            completar normalmente (exit 0).
 *
 * run_tests_checkpoint.sh compila e corre AS DUAS variantes desta mesma
 * fonte e falha se BUG=1 nao reproduzir o deadlock OU se BUG=0 nao completar
 * -- e' o discriminador que teria apanhado o bug original antes do boot.
 */
#ifndef LOCKORDER_REPRO_BUG
#define LOCKORDER_REPRO_BUG 0
#endif

#define _POSIX_C_SOURCE 200809L

#include "ps3_checkpoint.h"
#include "ps3_trace.h"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/* Simula table_lock()/table_unlock() de runtime/syscalls/sys_ppu_thread.c:
 * PTHREAD_MUTEX_INITIALIZER e' nao-recursivo por default em macOS/Linux --
 * mesma classe de mutex do bug real. */
static pthread_mutex_t s_guard = PTHREAD_MUTEX_INITIALIZER;

uint64_t ps3_timebase_now(void)
{
    static uint64_t counter = 1;
    return counter++;
}

uint32_t ps3_ppu_thread_alive_count(void)
{
#if LOCKORDER_REPRO_BUG
    /* Padrao PRE-FIX: primeiro gesto do accessor e' tomar o MESMO lock que o
     * call site (main(), abaixo) ja detem antes de chamar
     * ps3_checkpoint_fire() -- exactamente o que sys_ppu_thread_create()
     * fazia com table_lock() em volta de FIRST_THREAD. */
    pthread_mutex_lock(&s_guard);
    pthread_mutex_unlock(&s_guard);
#endif
    /* Padrao FIX: leitura relaxada, nenhum lock -- nunca contende com
     * nenhum lock que um caller possa ja deter. */
    return 3;
}
uint32_t ps3_sys_memory_used(void) { return 12345; }
uint32_t ps3_gcm_flip_count(void)  { return 7; }

/* Watchdog: se o processo nao terminar dentro de WATCHDOG_SECS, o padrao
 * reproduziu o autodeadlock -- mata o processo com um diagnostico em vez de
 * o deixar pendurado para sempre num corredor de CI/terminal. */
#define WATCHDOG_SECS 3

static void* watchdog(void* arg)
{
    (void)arg;
    sleep(WATCHDOG_SECS);
    fprintf(stderr,
            "[lockorder] DEADLOCK: ps3_checkpoint_fire nao retornou em %ds "
            "(accessor reentrou um lock que o caller ja detinha, BUG=%d)\n",
            WATCHDOG_SECS, LOCKORDER_REPRO_BUG);
    fflush(stderr);
    _exit(1);
}

int main(void)
{
    pthread_t wd;

    setenv("PS3_TRACE_CHECKPOINTS", "1", 1);
    setenv("PS3_TRACE_SNAPSHOT", "1", 1);

    pthread_create(&wd, NULL, watchdog, NULL);
    pthread_detach(wd);

    /* Simula sys_ppu_thread_create(): toma o lock ANTES do checkpoint, tal
     * como table_lock() .. ps3_checkpoint_fire(FIRST_THREAD, ...) ..
     * table_unlock() no sitio real. */
    pthread_mutex_lock(&s_guard);
    ps3_checkpoint_fire(PS3_CP_FIRST_THREAD, "test_lockorder", "tid=%d", 1);
    pthread_mutex_unlock(&s_guard);

    printf("[lockorder] OK: ps3_checkpoint_fire retornou sem autodeadlock (BUG=%d)\n",
           LOCKORDER_REPRO_BUG);
    return 0;
}
