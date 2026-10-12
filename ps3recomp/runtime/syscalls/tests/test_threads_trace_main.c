/* test_threads_trace_main.c -- teste unitario do subsistema THREADS (Task 4
 * do plano "bringup-instrumentacao-fase0"): CREATE (sys_ppu_thread_create),
 * BLOCK/WAKE com on=/obj= obrigatorios (sys_ppu_thread_join num "join" real,
 * e sys_mutex_lock num mutex real do runtime), e JOIN.
 *
 * Standalone; liga os ficheiros REAIS runtime/syscalls/sys_ppu_thread.c e
 * runtime/syscalls/sys_mutex.c (nao uma reimplementacao) -- o brief pede
 * explicitamente "num mutex ou semaforo do runtime". sys_ppu_thread.c ja'
 * chama ps3_checkpoint_fire(PS3_CP_FIRST_THREAD, ...) (Task 2) e
 * vm_stack_allocate() (runtime/memory/vm.h) precisa de vm_init() chamado
 * primeiro, por isso este teste tambem liga runtime/trace/ps3_checkpoint.c e
 * chama vm_init() no arranque -- os mesmos dois ficheiros que qualquer boot
 * real ja liga, apenas sem o resto do motor (ppu_loader.cpp, gerado por
 * jogo, NAO entra: e por isso que ppu_rsv_register/unregister,
 * ppu_giant_lock_acquire/release e ppu_blockmark_name (definidos la) sao
 * stubados abaixo -- mesmo padrao dos stubs de test_dma_main.c para o motor
 * MFC).
 *
 * ps3_trace_on() resolve getenv() uma vez por subsistema e nunca mais re-le
 * -- por isso "off"/"on" correm cada uma no SEU PROPRIO processo, mesmo
 * principio de runtime/trace/tests/test_ps3_mem_trace.c.
 *
 * Usage: ./test_threads_trace.exe off|on
 * Ou, mais simples, o runner desta pasta: ./run_tests_threads_trace.sh
 */
#include "sys_ppu_thread.h"
#include "sys_mutex.h"
#include "ps3_trace.h"
#include "ps3_checkpoint.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ---------------------------------------------------------------------------
 * Stubs de dependencias externas que vivem em runtime/ppu/ppu_loader.cpp
 * (gerado por jogo, nao linkavel standalone). Nenhum destes e' exercitado
 * de forma interessante aqui -- so' precisam de existir para o link e de
 * nao fazer nada que atrapalhe o cenario de teste.
 * -----------------------------------------------------------------------*/
uint64_t ps3_timebase_now(void) { return 1; }
uint32_t ps3_sys_memory_used(void) { return 0; }
uint32_t ps3_gcm_flip_count(void) { return 0; }

/* vm.h declares this `extern` (defined per-game in runtime/ppu/ppu_loader.cpp
 * normally); vm_init() sets it via mmap/VirtualAlloc. */
uint8_t* vm_base = NULL;

void ppu_rsv_register(ppu_context* ctx)   { (void)ctx; }
void ppu_rsv_unregister(ppu_context* ctx) { (void)ctx; }
void ppu_giant_lock_acquire(void) {}
void ppu_giant_lock_release(void) {}
void ppu_blockmark_name(const char* name) { (void)name; }

/* ---------------------------------------------------------------------------
 * Captura de stderr -- identica a runtime/trace/tests/test_ps3_mem_trace.c.
 * -----------------------------------------------------------------------*/
static int  s_saved_stderr_fd = -1;
static int  s_capture_fd = -1;
static char s_capture_path[] = "/tmp/ps3_threads_trace_test_XXXXXX";

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

/* ---------------------------------------------------------------------------
 * Cenario comum: cria uma thread guest de verdade (sys_ppu_thread_create),
 * junta-se a ela (sys_ppu_thread_join -- BLOCK/WAKE on=join), depois cria e
 * tranca um mutex de verdade (sys_mutex_create/sys_mutex_lock -- BLOCK/WAKE
 * on=mutex). g_ppu_thread_entry_trampoline fica NULL (nenhum jogo ligado),
 * por isso a thread criada e' um no-op que termina em microssegundos --
 * ppu_host_thread_proc ja trata esse caso ("thread is a no-op!") e ainda
 * assim passa por CREATE/START/EXIT.
 * -----------------------------------------------------------------------*/
static int run_scenario(void)
{
    int32_t rc32 = vm_init();
    if (rc32 != CELL_OK) {
        fprintf(stdout, "vm_init falhou: %d\n", rc32);
        exit(90);
    }

    lv2_syscall_table tbl;
    memset(&tbl, 0, sizeof(tbl));
    sys_ppu_thread_init(&tbl);
    sys_mutex_init(&tbl);

    /* --- CREATE + JOIN (BLOCK/WAKE on=join) --- */
    ppu_context caller;
    memset(&caller, 0, sizeof(caller));
    caller.thread_id = 99;   /* "thread" de teste que chama create/join/lock */

    /* r3=tid_out_addr(0, sem escrita) r4=entry r5=arg r6=priority r7=stack_size
     * r8=flags r9=name_addr(0, sem nome) */
    caller.gpr[4] = 0x1000;   /* entry (nunca corre -- trampoline fica NULL) */
    caller.gpr[7] = 0x4000;   /* stack_size minimo */

    int64_t rc = sys_ppu_thread_create(&caller);
    if (rc != CELL_OK) {
        fprintf(stdout, "sys_ppu_thread_create falhou: %lld\n", (long long)rc);
        exit(91);
    }
    /* thread_id retornado e' sempre 1 (primeiro slot livre) -- create nao
     * escreve para guest memory aqui (tid_out_addr==0), logo esta e' a
     * unica forma de o obter neste teste standalone. */
    uint64_t created_tid = 1;

    caller.gpr[3] = created_tid;  /* sys_ppu_thread_join: r3=thread_id */
    caller.gpr[4] = 0;            /* r4=status_addr (0, sem escrita) */
    rc = sys_ppu_thread_join(&caller);
    if (rc != CELL_OK) {
        fprintf(stdout, "sys_ppu_thread_join falhou: %lld\n", (long long)rc);
        exit(92);
    }

    /* --- mutex real: BLOCK/WAKE on=mutex --- */
    memset(&caller, 0, sizeof(caller));
    caller.thread_id = 99;
    caller.gpr[3] = 0;   /* id_out_addr (0, sem escrita) */
    caller.gpr[4] = 0;   /* attr_addr (0, sem atributos) */
    rc = sys_mutex_create(&caller);
    if (rc != CELL_OK) {
        fprintf(stdout, "sys_mutex_create falhou: %lld\n", (long long)rc);
        exit(93);
    }
    uint32_t mutex_id = 1;   /* primeiro slot livre, id_out_addr==0 nao escreveu */

    caller.gpr[3] = mutex_id;
    caller.gpr[4] = 0;   /* timeout_us=0 (infinito) */
    rc = sys_mutex_lock(&caller);
    if (rc != CELL_OK) {
        fprintf(stdout, "sys_mutex_lock falhou: %lld\n", (long long)rc);
        exit(94);
    }

    return 0;
}

/* ---------------------------------------------------------------------------
 * "off" -- PS3_TRACE_THREADS por definir: nenhuma linha [PS3T] sai, mesmo
 * com CREATE/BLOCK/WAKE/JOIN reais a correr.
 * -----------------------------------------------------------------------*/
static int test_off(void)
{
    char buf[16384];
    int fails = 0;
    size_t n;

    capture_stderr_begin();
    run_scenario();
    n = capture_stderr_end(buf, sizeof(buf));
    (void)n;

    /* Filtra as linhas [THREAD ...]/[SYS ...] pre-existentes (nao-[PS3T]) do
     * proprio sys_ppu_thread.c -- so' interessa que NENHUMA linha [PS3T]
     * saia com o gate desligado. */
    const char* p = buf;
    while ((p = strstr(p, "[PS3T]")) != NULL) {
        fails++;
        p += 6;
    }
    if (fails != 0) {
        printf("  FAIL off: esperava 0 linhas [PS3T] com PS3_TRACE_THREADS desligado, obteve %d:\n%s\n",
               fails, buf);
    }
    return fails;
}

/* ---------------------------------------------------------------------------
 * "on" -- PS3_TRACE_THREADS=1: verifica CREATE + BLOCK(on=/obj=) + WAKE + JOIN.
 * -----------------------------------------------------------------------*/
static int test_on(void)
{
    char buf[16384];
    int fails = 0;
    size_t n;

    capture_stderr_begin();
    run_scenario();
    n = capture_stderr_end(buf, sizeof(buf));

    if (n == 0) {
        printf("  FAIL on: PS3_TRACE_THREADS=1 nao emitiu nada\n");
        return 1;
    }

    /* CREATE: tid=1 (a thread recem-criada), entry=0x1000. */
    if (!strstr(buf, "| THREADS | CREATE | tid=1 ")) {
        printf("  FAIL on: falta a linha CREATE esperada (tid=1)\n%s\n", buf); fails++;
    }
    /* BLOCK no join: tid=99 (quem chama join) on=join obj=0x00000001 (tid
     * 1, a thread sendo joinada) -- requisito explicito do brief: BLOCK tem
     * de ter on= E obj=. */
    if (!strstr(buf, "| THREADS | BLOCK | tid=99 on=join obj=0x00000001")) {
        printf("  FAIL on: falta a linha BLOCK on=join obj= esperada\n%s\n", buf); fails++;
    }
    if (!strstr(buf, "| THREADS | WAKE | tid=99 on=join obj=0x00000001")) {
        printf("  FAIL on: falta a linha WAKE on=join esperada\n%s\n", buf); fails++;
    }
    if (!strstr(buf, "| THREADS | JOIN | tid=99 joined=0x00000001")) {
        printf("  FAIL on: falta a linha JOIN esperada\n%s\n", buf); fails++;
    }
    /* BLOCK/WAKE no mutex: tid=99 on=mutex obj=0x00000001 (mutex_id=1). */
    if (!strstr(buf, "| THREADS | BLOCK | tid=99 on=mutex obj=0x00000001")) {
        printf("  FAIL on: falta a linha BLOCK on=mutex obj= esperada\n%s\n", buf); fails++;
    }
    if (!strstr(buf, "| THREADS | WAKE | tid=99 on=mutex obj=0x00000001")) {
        printf("  FAIL on: falta a linha WAKE on=mutex esperada\n%s\n", buf); fails++;
    }
    /* START/EXIT (bonus -- nao exigidos pelo brief para este teste, mas o
     * mesmo cenario ja os exercita: a thread no-op arranca e termina). */
    if (!strstr(buf, "| THREADS | START | tid=1 ")) {
        printf("  FAIL on: falta a linha START esperada (bonus, tid=1)\n%s\n", buf); fails++;
    }
    if (!strstr(buf, "| THREADS | EXIT | tid=1 ")) {
        printf("  FAIL on: falta a linha EXIT esperada (bonus, tid=1)\n%s\n", buf); fails++;
    }

    return fails;
}

/* ---------------------------------------------------------------------------
 * main -- dispatch por cenario.
 * -----------------------------------------------------------------------*/
int main(int argc, char** argv)
{
    const char* mode = (argc > 1) ? argv[1] : "on";
    int fails = 0;

    if (strcmp(mode, "off") == 0) {
        fails += test_off();
    } else if (strcmp(mode, "on") == 0) {
        fails += test_on();
    } else {
        fprintf(stdout, "modo desconhecido: %s (usar off|on)\n", mode);
        return 2;
    }

    if (fails == 0) {
        printf("[%s] 0 failed\n", mode);
        return 0;
    }
    printf("[%s] %d failed\n", mode, fails);
    return 1;
}
