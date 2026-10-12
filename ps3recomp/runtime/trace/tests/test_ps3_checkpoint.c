/* test_ps3_checkpoint.c -- teste unitario de runtime/trace/ps3_checkpoint.c
 * (Task 2 do plano "bringup-instrumentacao-fase0"): once-guard por
 * checkpoint, pareamento CHECKPOINTS+SNAPSHOT, gate off -> zero bytes.
 *
 * Standalone; NAO liga o runtime inteiro. Compilar e correr com:
 *
 *   clang -std=c11 -Wall -Wextra -O0 \
 *       -I <repo>/include \
 *       <repo>/runtime/trace/ps3_trace.c \
 *       <repo>/runtime/trace/ps3_checkpoint.c \
 *       <repo>/runtime/trace/tests/test_ps3_checkpoint.c \
 *       -o /tmp/test_ps3_checkpoint
 *   PS3_TRACE_CHECKPOINTS=0                          /tmp/test_ps3_checkpoint off
 *   PS3_TRACE_CHECKPOINTS=1                          /tmp/test_ps3_checkpoint once
 *   PS3_TRACE_CHECKPOINTS=1 PS3_TRACE_SNAPSHOT=1      /tmp/test_ps3_checkpoint snapshot
 *
 * Ou, mais simples, o runner desta pasta:
 *
 *   ./run_tests_checkpoint.sh
 *
 * Porque tres processos e nao um: ps3_trace_on() (Task 1) resolve getenv()
 * UMA vez por subsistema e cacheia para sempre, e o once-guard de
 * ps3_checkpoint_fire() e' static por-processo -- uma vez disparado, um
 * checkpoint NUNCA mais dispara nesse processo. Isso e' exactamente o
 * comportamento real (um boot so' tem UM primeiro ELF_LOADED), mas obriga o
 * teste a isolar "CHECKPOINTS desligado", "CHECKPOINTS ligado sem SNAPSHOT" e
 * "CHECKPOINTS+SNAPSHOT ligados" em processos separados -- mesmo principio
 * de test_ps3_trace.c (T1.1/T1.2/T1.3) e de spu_fma_precision
 * (runtime/spu/tests/run_tests.sh).
 *
 * Este ficheiro fornece stubs de ps3_timebase_now() e dos tres accessors de
 * SNAPSHOT (ps3_ppu_thread_alive_count/ps3_sys_memory_used/
 * ps3_gcm_flip_count) -- ps3_checkpoint.h so' os DECLARA (review pos-Task 2:
 * ja nao e' `extern` solto em ps3_checkpoint.c, ver esse ficheiro); ligar as
 * implementacoes reais (sys_ppu_thread.c/sys_memory.c/cellGcmSys.c)
 * arrastaria o runtime inteiro, que este teste isolado nao precisa. Mesmo
 * principio do stub de ps3_timebase_now() em test_ps3_trace.c.
 *
 * Cenario adicional "concurrent" (review pos-Task 2, achado: o once-guard so'
 * era exercitado sequencialmente): N threads chamam ps3_checkpoint_fire() com
 * o MESMO checkpoint ao mesmo tempo; espera-se exactamente uma linha
 * CHECKPOINTS (ver test_concurrent() abaixo). Requer -pthread no build (ver
 * run_tests_checkpoint.sh).
 *
 * O achado do DEADLOCK de boot (accessor de SNAPSHOT reentrando um lock que o
 * caller ja detinha) tem o seu proprio ficheiro,
 * test_ps3_checkpoint_lockorder.c, porque a classe de bug so' se manifesta
 * quando UM stub toma mesmo um lock -- nao pode coexistir no mesmo binario
 * com os stubs triviais usados por off/once/snapshot/concurrent aqui (um so'
 * conjunto de definicoes de ps3_ppu_thread_alive_count/etc. pode existir por
 * executavel).
 */
#define _POSIX_C_SOURCE 200809L

#include "ps3_checkpoint.h"
#include "ps3_trace.h"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ---------------------------------------------------------------------------
 * Stubs -- ver nota no cabecalho do ficheiro.
 * -----------------------------------------------------------------------*/
uint64_t ps3_timebase_now(void)
{
    static uint64_t counter = 1;
    return counter++;
}
uint32_t ps3_ppu_thread_alive_count(void) { return 3; }
uint32_t ps3_sys_memory_used(void)        { return 12345; }
uint32_t ps3_gcm_flip_count(void)         { return 7; }

/* ---------------------------------------------------------------------------
 * Captura de stderr -- identica a test_ps3_trace.c.
 * -----------------------------------------------------------------------*/
static int  s_saved_stderr_fd = -1;
static int  s_capture_fd = -1;
static char s_capture_path[] = "/tmp/ps3_checkpoint_test_XXXXXX";

static void capture_stderr_begin(void)
{
    fflush(stderr);
    s_capture_fd = mkstemp(s_capture_path);
    if (s_capture_fd < 0) { perror("mkstemp"); exit(97); }
    s_saved_stderr_fd = dup(STDERR_FILENO);
    dup2(s_capture_fd, STDERR_FILENO);
}

static size_t capture_stderr_end(char* buf, size_t cap)
{
    ssize_t n;
    fflush(stderr);
    dup2(s_saved_stderr_fd, STDERR_FILENO);
    close(s_saved_stderr_fd);
    lseek(s_capture_fd, 0, SEEK_SET);
    n = read(s_capture_fd, buf, cap - 1);
    if (n < 0) n = 0;
    buf[n] = '\0';
    close(s_capture_fd);
    unlink(s_capture_path);
    return (size_t)n;
}

static int count_lines(const char* buf)
{
    int n = 0;
    for (const char* p = buf; *p; p++) if (*p == '\n') n++;
    return n;
}

/* ---------------------------------------------------------------------------
 * T2.off -- PS3_TRACE_CHECKPOINTS por definir: duas chamadas ao mesmo
 * checkpoint produzem ZERO bytes (G2: CHECKPOINTS desligado -> nem
 * checkpoint nem snapshot, mesmo com PS3_TRACE_SNAPSHOT setada no ambiente
 * do processo -- o runner nao a define neste cenario, mas o teste confirma
 * que ps3_trace_on(CHECKPOINTS) e' quem manda).
 * -----------------------------------------------------------------------*/
static int test_off(void)
{
    char buf[4096];
    int fails = 0;
    size_t n;

    capture_stderr_begin();
    ps3_checkpoint_fire(PS3_CP_ELF_LOADED, "fn", "segments=%d", 1);
    ps3_checkpoint_fire(PS3_CP_ELF_LOADED, "fn", "segments=%d", 2);
    n = capture_stderr_end(buf, sizeof(buf));

    if (n != 0) {
        printf("  FAIL T2.off: esperava 0 bytes com CHECKPOINTS desligado, obteve %zu: %s\n", n, buf);
        fails++;
    }
    return fails;
}

/* ---------------------------------------------------------------------------
 * T2.once -- PS3_TRACE_CHECKPOINTS=1, SNAPSHOT por definir:
 *   (a) duas chamadas ao MESMO checkpoint -> uma so' linha (a primeira data=
 *       vence; a segunda e' descartada pelo once-guard).
 *   (b) um checkpoint DIFERENTE -> uma segunda linha distinta.
 *   (c) nenhuma linha SNAPSHOT aparece (subsistema desligado).
 * -----------------------------------------------------------------------*/
static int test_once(void)
{
    char buf[4096];
    int fails = 0;
    size_t n;

    capture_stderr_begin();
    ps3_checkpoint_fire(PS3_CP_ELF_LOADED, "fnA", "segments=%d", 1);
    ps3_checkpoint_fire(PS3_CP_ELF_LOADED, "fnA", "segments=%d", 2); /* deve ser ignorada */
    ps3_checkpoint_fire(PS3_CP_VM_READY,   "fnB", "ok=%d", 1);
    n = capture_stderr_end(buf, sizeof(buf));

    if (n == 0) {
        printf("  FAIL T2.once: nao emitiu nada com CHECKPOINTS=1\n");
        return 1;
    }
    if (count_lines(buf) != 2) {
        printf("  FAIL T2.once: esperava 2 linhas (once-guard + checkpoint distinto), obteve %d: %s\n",
               count_lines(buf), buf);
        fails++;
    }
    if (strstr(buf, "segments=1") == NULL) {
        printf("  FAIL T2.once: primeira chamada (segments=1) nao apareceu: %s\n", buf);
        fails++;
    }
    if (strstr(buf, "segments=2") != NULL) {
        printf("  FAIL T2.once: segunda chamada ao MESMO checkpoint nao devia ter disparado: %s\n", buf);
        fails++;
    }
    if (strstr(buf, "| CHECKPOINTS | ELF_LOADED |") == NULL) {
        printf("  FAIL T2.once: linha ELF_LOADED nao esta no formato esperado: %s\n", buf);
        fails++;
    }
    if (strstr(buf, "| CHECKPOINTS | VM_READY |") == NULL) {
        printf("  FAIL T2.once: linha VM_READY (checkpoint distinto) em falta: %s\n", buf);
        fails++;
    }
    if (strstr(buf, "SNAPSHOT") != NULL) {
        printf("  FAIL T2.once: linha SNAPSHOT apareceu com PS3_TRACE_SNAPSHOT desligado: %s\n", buf);
        fails++;
    }
    return fails;
}

/* ---------------------------------------------------------------------------
 * T2.snapshot -- PS3_TRACE_CHECKPOINTS=1 e PS3_TRACE_SNAPSHOT=1: um unico
 * disparo produz DUAS linhas (CHECKPOINTS seguida imediatamente de
 * SNAPSHOT), mesmo <event>, e o <data> do SNAPSHOT tem os quatro pares
 * pedidos pelo requisito 4.
 * -----------------------------------------------------------------------*/
static int test_snapshot(void)
{
    char buf[4096];
    int fails = 0;
    size_t n;
    const char* checkpoint_line;
    const char* snapshot_line;

    capture_stderr_begin();
    ps3_checkpoint_fire(PS3_CP_TLS_INIT, "fnC", "tls_img=0x%08X", 0x0Eu);
    n = capture_stderr_end(buf, sizeof(buf));

    if (n == 0) {
        printf("  FAIL T2.snapshot: nao emitiu nada\n");
        return 1;
    }
    if (count_lines(buf) != 2) {
        printf("  FAIL T2.snapshot: esperava 2 linhas (checkpoint + snapshot), obteve %d: %s\n",
               count_lines(buf), buf);
        fails++;
    }
    checkpoint_line = strstr(buf, "| CHECKPOINTS | TLS_INIT |");
    snapshot_line   = strstr(buf, "| SNAPSHOT | TLS_INIT |");
    if (!checkpoint_line) {
        printf("  FAIL T2.snapshot: linha CHECKPOINTS/TLS_INIT em falta: %s\n", buf);
        fails++;
    }
    if (!snapshot_line) {
        printf("  FAIL T2.snapshot: linha SNAPSHOT/TLS_INIT em falta: %s\n", buf);
        fails++;
    }
    if (checkpoint_line && snapshot_line && snapshot_line < checkpoint_line) {
        printf("  FAIL T2.snapshot: SNAPSHOT apareceu ANTES do CHECKPOINTS: %s\n", buf);
        fails++;
    }
    if (snapshot_line) {
        if (strstr(snapshot_line, "threads=3") == NULL) {
            printf("  FAIL T2.snapshot: threads= nao reflete o stub (3): %s\n", snapshot_line);
            fails++;
        }
        if (strstr(snapshot_line, "heap_used=12345") == NULL) {
            printf("  FAIL T2.snapshot: heap_used= nao reflete o stub (12345): %s\n", snapshot_line);
            fails++;
        }
        if (strstr(snapshot_line, "last_flip=7") == NULL) {
            printf("  FAIL T2.snapshot: last_flip= nao reflete o stub (7): %s\n", snapshot_line);
            fails++;
        }
        if (strstr(snapshot_line, "tb=") == NULL) {
            printf("  FAIL T2.snapshot: tb= em falta no <data> do SNAPSHOT: %s\n", snapshot_line);
            fails++;
        }
    }
    return fails;
}

/* ---------------------------------------------------------------------------
 * T2.concurrent -- (review pos-Task 2, achado: o once-guard so' era
 * exercitado sequencialmente nos testes) N threads chamam ps3_checkpoint_fire
 * para o MESMO checkpoint ao mesmo tempo; o once-guard (CAS atomico +
 * pre-check relaxado) tem de deixar passar exactamente UMA. Usa
 * PS3_CP_FIRST_SPU_JOB (nao usado por T2.off/once/snapshot acima, para nao
 * colidir com o once-guard estatico por-processo).
 * -----------------------------------------------------------------------*/
#define T2_CONCURRENT_THREADS 16

static volatile int s_concurrent_go = 0;

static void* concurrent_worker(void* arg)
{
    long idx = (long)(intptr_t)arg;
    /* Espera activa pelo sinal de partida para amontoar as threads o mais
     * perto possivel umas das outras antes de todas chamarem
     * ps3_checkpoint_fire -- maximiza a janela de corrida no CAS. */
    while (!s_concurrent_go) { /* spin */ }
    ps3_checkpoint_fire(PS3_CP_FIRST_SPU_JOB, "concurrent_worker", "idx=%ld", idx);
    return NULL;
}

static int test_concurrent(void)
{
    char buf[8192];
    int fails = 0;
    size_t n;
    pthread_t th[T2_CONCURRENT_THREADS];
    int i;
    int lines_for_checkpoint = 0;
    const char* p;

    capture_stderr_begin();
    s_concurrent_go = 0;
    for (i = 0; i < T2_CONCURRENT_THREADS; i++) {
        pthread_create(&th[i], NULL, concurrent_worker, (void*)(intptr_t)i);
    }
    s_concurrent_go = 1; /* liberta todas as threads o mais junto possivel */
    for (i = 0; i < T2_CONCURRENT_THREADS; i++) {
        pthread_join(th[i], NULL);
    }
    n = capture_stderr_end(buf, sizeof(buf));

    if (n == 0) {
        printf("  FAIL T2.concurrent: nao emitiu nada com %d threads concorrentes\n",
               T2_CONCURRENT_THREADS);
        return 1;
    }
    p = buf;
    while ((p = strstr(p, "| CHECKPOINTS | FIRST_SPU_JOB |")) != NULL) {
        lines_for_checkpoint++;
        p += 1;
    }
    if (lines_for_checkpoint != 1) {
        printf("  FAIL T2.concurrent: esperava exactamente 1 linha FIRST_SPU_JOB de %d "
               "threads concorrentes (once-guard deveria bloquear as outras %d), obteve %d: %s\n",
               T2_CONCURRENT_THREADS, T2_CONCURRENT_THREADS - 1, lines_for_checkpoint, buf);
        fails++;
    }
    return fails;
}

/* ---------------------------------------------------------------------------
 * T2.name -- ps3_checkpoint_name() devolve os 13 tokens exactos do brief, na
 * ordem exacta da tabela.
 * -----------------------------------------------------------------------*/
static int test_name(void)
{
    static const char* const expected[] = {
        "ELF_LOADED", "VM_READY", "TLS_INIT", "GUEST_ENTRY", "FIRST_THREAD",
        "FIRST_FS_OPEN", "FIRST_WAD_OPEN", "FIRST_SPU_JOB", "FIRST_DRAW",
        "FIRST_FLIP", "FIRST_PRESENT", "MOVIE_EOS", "SHUTDOWN",
    };
    int fails = 0;
    int i;
    int n = (int)(sizeof(expected) / sizeof(expected[0]));

    if (n != 13) { printf("  FAIL T2.name: array de teste nao tem 13 entradas\n"); return 1; }

    for (i = 0; i < n; i++) {
        const char* got = ps3_checkpoint_name((ps3_checkpoint_id)i);
        if (strcmp(got, expected[i]) != 0) {
            printf("  FAIL T2.name: checkpoint[%d] esperado \"%s\", obteve \"%s\"\n",
                   i, expected[i], got);
            fails++;
        }
    }
    return fails;
}

/* ---------------------------------------------------------------------------
 * main -- dispatch por cenario (ver cabecalho do ficheiro / run_tests_checkpoint.sh).
 * -----------------------------------------------------------------------*/
int main(int argc, char** argv)
{
    const char* mode = (argc > 1) ? argv[1] : "off";
    int fails = 0;

    if (strcmp(mode, "off") == 0) {
        fails += test_off();
        fails += test_name(); /* nao depende de gating, corre em qualquer cenario */
    } else if (strcmp(mode, "once") == 0) {
        fails += test_once();
    } else if (strcmp(mode, "snapshot") == 0) {
        fails += test_snapshot();
    } else if (strcmp(mode, "concurrent") == 0) {
        fails += test_concurrent();
    } else {
        fprintf(stdout, "modo desconhecido: %s (usar off|once|snapshot|concurrent)\n", mode);
        return 2;
    }

    if (fails == 0) {
        printf("[%s] 0 failed\n", mode);
        return 0;
    }
    printf("[%s] %d failed\n", mode, fails);
    return 1;
}
