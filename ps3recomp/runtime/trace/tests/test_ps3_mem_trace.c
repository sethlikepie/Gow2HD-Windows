/* test_ps3_mem_trace.c -- teste unitario de runtime/trace/ps3_mem_trace.c
 * (Task 3 do plano "bringup-instrumentacao-fase0"): subsistema MEMORY,
 * evento canonico OOB -- formato G1, campos ea=/len=/region=/op=, cap
 * explicito + OOB_TRUNCATED, gate off -> zero bytes.
 *
 * Standalone; NAO liga ppu_loader.cpp (que inclui o ppu_recomp.h GERADO por
 * jogo e por isso nao compila fora de um jogo concreto -- exactamente o
 * motivo pelo qual ps3_mem_oob_report() foi extraido para
 * runtime/trace/ps3_mem_trace.c, ver o comentario de cabecalho la).
 * Compilar e correr com:
 *
 *   clang -std=c11 -Wall -Wextra -O0 \
 *       -I <repo>/include \
 *       <repo>/runtime/trace/ps3_trace.c \
 *       <repo>/runtime/trace/ps3_mem_trace.c \
 *       <repo>/runtime/trace/tests/test_ps3_mem_trace.c \
 *       -o /tmp/test_ps3_mem_trace
 *   /tmp/test_ps3_mem_trace off                        # gate off
 *   PS3_TRACE_MEMORY=1 /tmp/test_ps3_mem_trace format   # formato G1 + campos
 *   PS3_TRACE_MEMORY=1 /tmp/test_ps3_mem_trace cap      # cap + 1a OOB_TRUNCATED
 *   PS3_TRACE_MEMORY=1 /tmp/test_ps3_mem_trace flood    # Important 2: agenda de
 *                                                        # crescimento pos-cap
 *   /tmp/test_ps3_mem_trace region                      # vm_region_name(), sem gate
 *
 * Ou, mais simples, o runner desta pasta:
 *
 *   ./run_tests_mem_trace.sh
 *
 * Porque processos separados para off/format/cap: ps3_trace_on() (Task 1)
 * resolve getenv() UMA vez por subsistema e cacheia para sempre -- mesmo
 * principio de test_ps3_trace.c/test_ps3_checkpoint.c.
 *
 * ps3_timebase_now() e' declarado `extern` por ps3_trace.c mas definido de
 * verdade em runtime/syscalls/sys_timer.c:39 -- este ficheiro fornece o seu
 * proprio stub, mesmo principio dos outros testes desta pasta.
 *
 * NAO define _POSIX_C_SOURCE (ao contrario de test_ps3_trace.c/
 * test_ps3_checkpoint.c): runtime/memory/vm.h (incluido abaixo, para
 * vm_region_name()/VM_*_BASE) usa MAP_ANONYMOUS/MAP_NORESERVE, extensoes
 * Darwin que uma janela POSIX estrita esconde de <sys/mman.h> -- mkstemp/
 * dup/dup2 (usados pela captura de stderr) continuam visiveis no namespace
 * default do macOS sem essa macro.
 */

#include "ps3_mem_trace.h"
#include "ps3_trace.h"
#include "../../memory/vm.h" /* vm_region_name() -- teste "region" abaixo */

#include <regex.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ---------------------------------------------------------------------------
 * Stub de ps3_timebase_now() -- ver nota no cabecalho do ficheiro.
 * -----------------------------------------------------------------------*/
uint64_t ps3_timebase_now(void)
{
    static uint64_t counter = 1;
    return counter++;
}

/* ---------------------------------------------------------------------------
 * Captura de stderr -- identica a test_ps3_trace.c/test_ps3_checkpoint.c.
 * -----------------------------------------------------------------------*/
static int  s_saved_stderr_fd = -1;
static int  s_capture_fd = -1;
static char s_capture_path[] = "/tmp/ps3_mem_trace_test_XXXXXX";

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

static int count_substr(const char* buf, const char* needle)
{
    int n = 0;
    const char* p = buf;
    size_t len = strlen(needle);
    while ((p = strstr(p, needle)) != NULL) { n++; p += len; }
    return n;
}

/* ---------------------------------------------------------------------------
 * "off" -- PS3_TRACE_MEMORY por definir: ps3_mem_oob_report() nao emite nada,
 * mesmo chamado repetidamente. Corre so' no processo "off".
 * -----------------------------------------------------------------------*/
static int test_off(void)
{
    char buf[4096];
    int fails = 0;
    size_t n;

    capture_stderr_begin();
    ps3_mem_oob_report(0x20000000u, 4, "read", (void*)1);
    ps3_mem_oob_report(0xFFFF0000u, 8, "write", (void*)1);
    n = capture_stderr_end(buf, sizeof(buf));

    if (n != 0) {
        printf("  FAIL off: esperava 0 bytes com PS3_TRACE_MEMORY desligado, obteve %zu: %s\n", n, buf);
        fails++;
    }
    return fails;
}

/* ---------------------------------------------------------------------------
 * "format" -- PS3_TRACE_MEMORY=1: um EA fora do mapa (0x20000000 -- logo a
 * seguir ao fim nominal de VM_RSX_BASE+VM_RSX_SIZE, fora de main/stack/spu/
 * rsx) produz uma linha G1 com subsistema MEMORY, evento OOB, e os campos
 * ea=/len=/region=unmapped/op=read pedidos pelo brief. caller_ra=(void*)1
 * (garbage, nao resolve por dladdr) -> <fn> cai em "-".
 * -----------------------------------------------------------------------*/
static int test_format(void)
{
    char buf[4096];
    int fails = 0;
    size_t n;
    regex_t re;
    int rc;
    const char* pattern =
        "^\\[PS3T\\] [0-9]+ \\| - \\| - \\| MEMORY \\| OOB \\| "
        "ea=0x20000000 len=4 region=unmapped op=read$";

    capture_stderr_begin();
    ps3_mem_oob_report(0x20000000u, 4, "read", (void*)1);
    n = capture_stderr_end(buf, sizeof(buf));

    if (n == 0) {
        printf("  FAIL format: PS3_TRACE_MEMORY=1 nao emitiu nada\n");
        return 1;
    }
    if (buf[n - 1] == '\n') {
        buf[n - 1] = '\0';
    }

    rc = regcomp(&re, pattern, REG_EXTENDED);
    if (rc != 0) {
        printf("  FAIL format: regcomp falhou (rc=%d)\n", rc);
        return 1;
    }
    rc = regexec(&re, buf, 0, NULL, 0);
    if (rc != 0) {
        printf("  FAIL format: linha nao bate com o formato esperado: %s\n", buf);
        fails++;
    }
    regfree(&re);
    return fails;
}

/* ---------------------------------------------------------------------------
 * "cap" -- PS3_TRACE_MEMORY=1: dispara PS3_MEM_OOB_CANONICAL_CAP+2 eventos
 * reais. Espera: exactamente CAP linhas "MEMORY | OOB", exactamente 1 linha
 * "MEMORY | OOB_TRUNCATED" com count=CAP+1 (o PRIMEIRO marco da agenda de
 * crescimento, Important 2 da review pos-Task 3 -- ver test_flood() abaixo
 * para o comportamento ALEM deste primeiro marco, que este cenario nao
 * exercita: CAP+2 fica bem antes do segundo marco em 2*CAP+1).
 * -----------------------------------------------------------------------*/
static int test_cap(void)
{
    char buf[16384];
    int fails = 0;
    size_t n;
    int i;
    char expect_truncated[64];

    capture_stderr_begin();
    for (i = 0; i < PS3_MEM_OOB_CANONICAL_CAP + 2; i++) {
        ps3_mem_oob_report(0x20000000u + (uint32_t)i, 4, "read", (void*)1);
    }
    n = capture_stderr_end(buf, sizeof(buf));

    if (n == 0) {
        printf("  FAIL cap: nao emitiu nada\n");
        return 1;
    }

    int oob_lines       = count_substr(buf, "| MEMORY | OOB |");
    int truncated_lines = count_substr(buf, "| MEMORY | OOB_TRUNCATED |");

    if (oob_lines != PS3_MEM_OOB_CANONICAL_CAP) {
        printf("  FAIL cap: esperava %d linhas OOB, obteve %d\n",
               PS3_MEM_OOB_CANONICAL_CAP, oob_lines);
        fails++;
    }
    if (truncated_lines != 1) {
        printf("  FAIL cap: esperava exactamente 1 linha OOB_TRUNCATED, obteve %d\n",
               truncated_lines);
        fails++;
    }
    snprintf(expect_truncated, sizeof(expect_truncated), "count=%d",
             PS3_MEM_OOB_CANONICAL_CAP + 1);
    if (strstr(buf, expect_truncated) == NULL) {
        printf("  FAIL cap: OOB_TRUNCATED nao tem %s: %s\n", expect_truncated, buf);
        fails++;
    }
    return fails;
}

/* ---------------------------------------------------------------------------
 * "flood" -- Important 2 da review pos-Task 3: "cap" acima so' prova a
 * PRIMEIRA linha OOB_TRUNCATED (count=CAP+1); nada exercitava o caminho de
 * flood BEM alem do cap, onde o comportamento original (uma unica linha,
 * congelada para sempre) tornava o total real irrecuperavel do trace --
 * exactamente o cenario de regressao ("nº de OOB", uma das dez metricas do
 * baseline Fase 0) que este subsistema existe para apanhar.
 *
 * Dispara 3*CAP+3 eventos reais (bem alem do terceiro marco da agenda de
 * crescimento) e espera exactamente TRES linhas OOB_TRUNCATED, com
 * count=CAP+1, 2*CAP+1 e 3*CAP+1 (a agenda "a cada CAP eventos", ver
 * comentario em ps3_mem_trace.c/ps3_mem_oob_report) -- nenhum valor
 * intermedio, e nenhuma linha extra na cauda ate' ao proximo marco
 * (3*CAP+3 < 4*CAP+1). oob_lines continua fixo em CAP (o cap em si nao
 * muda -- so' a frequencia de OOB_TRUNCATED depois dele).
 * -----------------------------------------------------------------------*/
static int test_flood(void)
{
    char buf[16384];
    int fails = 0;
    size_t n;
    int i;
    int total = 3 * PS3_MEM_OOB_CANONICAL_CAP + 3;
    char expect1[64], expect2[64], expect3[64];

    capture_stderr_begin();
    for (i = 0; i < total; i++) {
        ps3_mem_oob_report(0x20000000u + (uint32_t)i, 4, "read", (void*)1);
    }
    n = capture_stderr_end(buf, sizeof(buf));

    if (n == 0) {
        printf("  FAIL flood: nao emitiu nada\n");
        return 1;
    }

    int oob_lines       = count_substr(buf, "| MEMORY | OOB |");
    int truncated_lines = count_substr(buf, "| MEMORY | OOB_TRUNCATED |");

    if (oob_lines != PS3_MEM_OOB_CANONICAL_CAP) {
        printf("  FAIL flood: esperava %d linhas OOB, obteve %d\n",
               PS3_MEM_OOB_CANONICAL_CAP, oob_lines);
        fails++;
    }
    if (truncated_lines != 3) {
        printf("  FAIL flood: esperava exactamente 3 linhas OOB_TRUNCATED (agenda de "
               "crescimento a cada CAP eventos), obteve %d\n", truncated_lines);
        fails++;
    }

    snprintf(expect1, sizeof(expect1), "count=%d", PS3_MEM_OOB_CANONICAL_CAP + 1);
    snprintf(expect2, sizeof(expect2), "count=%d", 2 * PS3_MEM_OOB_CANONICAL_CAP + 1);
    snprintf(expect3, sizeof(expect3), "count=%d", 3 * PS3_MEM_OOB_CANONICAL_CAP + 1);
    if (strstr(buf, expect1) == NULL) {
        printf("  FAIL flood: falta o 1o marco (%s): %s\n", expect1, buf);
        fails++;
    }
    if (strstr(buf, expect2) == NULL) {
        printf("  FAIL flood: falta o 2o marco (%s) -- o total real ficaria "
               "irrecuperavel do trace sem este marco: %s\n", expect2, buf);
        fails++;
    }
    if (strstr(buf, expect3) == NULL) {
        printf("  FAIL flood: falta o 3o marco (%s): %s\n", expect3, buf);
        fails++;
    }
    return fails;
}

/* ---------------------------------------------------------------------------
 * "region" -- vm_region_name() (runtime/memory/vm.h), sem gate nenhum (funcao
 * pura). Cobre as quatro regioes + unmapped, incluindo a nota do brief sobre
 * a sobreposicao latente herdada de vm_is_valid_addr(): VM_RSX_BASE cai
 * dentro do range nominal de main, por isso o teste usa um EA so' dentro de
 * RSX (VM_RSX_BASE+VM_RSX_SIZE-1) para a classificacao "rsx".
 * -----------------------------------------------------------------------*/
static int test_region(void)
{
    int fails = 0;

    struct { uint32_t addr; const char* expected; const char* label; } cases[] = {
        { VM_MAIN_MEM_BASE,                       "main",     "main base" },
        { VM_MAIN_MEM_BASE + VM_MAIN_MEM_SIZE - 1, "main",     "main last byte" },
        { VM_STACK_BASE,                          "stack",    "stack base" },
        { VM_STACK_BASE + VM_STACK_REGION - 1,     "stack",    "stack last byte" },
        { VM_SPU_BASE,                             "spu",      "spu base" },
        { VM_RSX_BASE + VM_RSX_SIZE - 1,           "rsx",      "rsx last byte (RSX-only, ver nota)" },
        { 0x20000000u,                             "unmapped", "logo a seguir ao fim de RSX" },
        { 0xFFFF0000u,                             "unmapped", "banda alta de garbage (OOBRA)" },
        /* Fase 1: regioes que existiam no runtime mas nao no mapa (heap real
         * do jogo, sys_vm, RSX local real, imagem TLS) -- ver task-4-brief.md. */
        { VM_LV2_HEAP_BASE,                        "lv2_heap",  "inicio da janela lv2 (sys_memory)" },
        { VM_LV2_HEAP_END - 1,                     "lv2_heap",  "ultimo byte da janela lv2" },
        { VM_SYS_VM_BASE,                          "sys_vm",    "inicio de sys_vm" },
        { VM_SYS_VM_END - 1,                       "sys_vm",    "ultimo byte de sys_vm" },
        { VM_RSX_LOCAL_BASE,                       "rsx_local", "inicio da RSX local real" },
        { VM_RSX_LOCAL_END - 1,                    "rsx_local", "ultimo byte da RSX local" },
        { VM_TLS_IMG_BASE,                         "tls",       "imagem TLS (hoje reporta 'rsx')" },
        { VM_TLS_IMG_BASE + VM_TLS_IMG_WINDOW - 1, "tls",       "ultimo byte da janela TLS" },
    };
    size_t i;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        const char* got = vm_region_name(cases[i].addr);
        if (strcmp(got, cases[i].expected) != 0) {
            printf("  FAIL region: 0x%08X (%s) esperava \"%s\", obteve \"%s\"\n",
                   cases[i].addr, cases[i].label, cases[i].expected, got);
            fails++;
        }
    }
    return fails;
}

/* ---------------------------------------------------------------------------
 * main -- dispatch por cenario (ver cabecalho do ficheiro / run_tests_mem_trace.sh).
 * -----------------------------------------------------------------------*/
int main(int argc, char** argv)
{
    const char* mode = (argc > 1) ? argv[1] : "format";
    int fails = 0;

    if (strcmp(mode, "off") == 0) {
        fails += test_off();
    } else if (strcmp(mode, "format") == 0) {
        fails += test_format();
    } else if (strcmp(mode, "cap") == 0) {
        fails += test_cap();
    } else if (strcmp(mode, "flood") == 0) {
        fails += test_flood();
    } else if (strcmp(mode, "region") == 0) {
        fails += test_region();
    } else {
        fprintf(stdout, "modo desconhecido: %s (usar off|format|cap|flood|region)\n", mode);
        return 2;
    }

    if (fails == 0) {
        printf("[%s] 0 failed\n", mode);
        return 0;
    }
    printf("[%s] %d failed\n", mode, fails);
    return 1;
}
