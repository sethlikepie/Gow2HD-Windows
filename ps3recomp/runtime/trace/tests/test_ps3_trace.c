/* test_ps3_trace.c -- teste unitario de runtime/trace/ps3_trace.c (Task 1 do
 * plano "bringup-instrumentacao-fase0"): formato de linha (G1), gating (G2),
 * sentinela [PS3T].
 *
 * Standalone; NAO liga o runtime inteiro. Compilar e correr com:
 *
 *   clang -std=c11 -Wall -Wextra -O0 \
 *       -I <repo>/include \
 *       <repo>/runtime/trace/ps3_trace.c \
 *       <repo>/runtime/trace/tests/test_ps3_trace.c \
 *       -o /tmp/test_ps3_trace
 *   /tmp/test_ps3_trace format          # T1.4, T1.5, T1.6, T1.7, T1.8
 *   /tmp/test_ps3_trace gate_all_off    # T1.1 (todas as PS3_TRACE_* por definir)
 *   PS3_TRACE_RSX=1  /tmp/test_ps3_trace gate_rsx   # T1.2
 *   PS3_TRACE_HEAP=1 /tmp/test_ps3_trace gate_heap  # T1.3
 *
 * Ou, mais simples, usar o runner desta pasta (compila uma vez, corre os
 * quatro cenarios com o env certo em cada, agrega PASS/FAIL):
 *
 *   ./run_tests.sh
 *
 * Porque quatro processos e nao um: ps3_trace_on() resolve getenv() UMA vez
 * por subsistema e cacheia para sempre (requisito 3 do brief -- "sem getenv
 * no caminho quente"). Isso significa que T1.1 (todas off), T1.2 (so RSX) e
 * T1.3 (so HEAP) nao podem partilhar o mesmo processo: assim que uma env var
 * e' lida (ou nao-lida) para um subsistema, mudar a variavel depois nao
 * teria efeito nenhum -- exatamente como as sondas PS3_TRACE_HEAP/SPURS
 * pre-existentes ja se comportam (runtime/ppu/ppu_loader.cpp:826,
 * runtime/syscalls/sys_event.c:15). Por isso cada cenario de gating corre
 * como o SEU PROPRIO processo, escolhido por argv[1] -- o mesmo principio do
 * teste spu_fma_precision em runtime/spu/tests/run_tests.sh (compila uma vez,
 * corre duas, uma por combinacao de env).
 *
 * ps3_timebase_now() e' declarado `extern` por ps3_trace.c mas definido de
 * verdade em runtime/syscalls/sys_timer.c:39 -- ligar esse ficheiro aqui
 * arrastaria runtime/ppu/ppu_context.h e a tabela de syscalls lv2 inteira,
 * que este teste nao precisa. Por isso este ficheiro fornece o seu proprio
 * stub (abaixo) -- um contador crescente, nunca zero, suficiente para o
 * campo <tb> (so' precisa de bater com "[0-9]+" no regex de T1.2).
 */
#define _POSIX_C_SOURCE 200809L

#include "ps3_trace.h"

#include <pthread.h>
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
 * Captura de stderr: dup2 do fd 2 para um ficheiro tmp (nao freopen, para
 * preservar o FILE* `stderr` que ps3_trace_emit usa com fwrite/fflush).
 * -----------------------------------------------------------------------*/
static int  s_saved_stderr_fd = -1;
static int  s_capture_fd = -1;
static char s_capture_path[] = "/tmp/ps3_trace_test_XXXXXX";

static void capture_stderr_begin(void)
{
    fflush(stderr);
    s_capture_fd = mkstemp(s_capture_path);
    if (s_capture_fd < 0) {
        perror("mkstemp");
        exit(97);
    }
    s_saved_stderr_fd = dup(STDERR_FILENO);
    dup2(s_capture_fd, STDERR_FILENO);
}

/* Restaura stderr, le os bytes capturados para buf (capacidade cap, sempre
 * NUL-terminado) e devolve o numero de bytes lidos. */
static size_t capture_stderr_end(char* buf, size_t cap)
{
    ssize_t n;

    fflush(stderr);
    dup2(s_saved_stderr_fd, STDERR_FILENO);
    close(s_saved_stderr_fd);

    lseek(s_capture_fd, 0, SEEK_SET);
    n = read(s_capture_fd, buf, cap - 1);
    if (n < 0) {
        n = 0;
    }
    buf[n] = '\0';

    close(s_capture_fd);
    unlink(s_capture_path);
    return (size_t)n;
}

/* ---------------------------------------------------------------------------
 * T1.4 -- fn=NULL, thread nao definida, fmt=NULL -> "-" nos tres campos.
 * Tem de correr ANTES de qualquer ps3_trace_set_thread() nesta thread (T1.6
 * define a thread mas restaura para 0 no fim, entao a ordem no main() nao e'
 * estritamente obrigatoria -- mesmo assim corre primeiro, por clareza).
 * -----------------------------------------------------------------------*/
static int test_t1_4_absent_fields(void)
{
    char buf[2048];
    int fails = 0;

    capture_stderr_begin();
    ps3_trace_emit(PS3_TS_MEMORY, NULL, "ev", NULL);
    capture_stderr_end(buf, sizeof(buf));

    if (strstr(buf, "| - | - | MEMORY | ev | -") == NULL) {
        printf("  FAIL T1.4: campos ausentes nao viraram '-': %s", buf);
        fails++;
    }
    return fails;
}

/* ---------------------------------------------------------------------------
 * T1.5 -- '|' em <data> vira '/'; '\n' vira '_' (review pos-Task 3, "Also
 * fix" #1 -- ANTES desta correcao, '\n' sobrevivia a sanitize_data_kv() e so'
 * virava ESPACO mais tarde em sanitize_field(), recriando o bug do token
 * solto que a sanitizacao k=v existe para evitar; a asserçao abaixo era
 * "a/b c/d" (espaco) e foi actualizada para "a/b_c/d" (underscore) para
 * bater com o comportamento corrigido); a linha continua com exactamente 5
 * ocorrencias de " | " (os cinco separadores estruturais).
 * -----------------------------------------------------------------------*/
static int test_t1_5_sanitization(void)
{
    char buf[2048];
    int fails = 0;
    int count;
    const char* p;

    capture_stderr_begin();
    ps3_trace_emit(PS3_TS_DMA, "fnX", "evX", "%s", "a|b\nc|d");
    capture_stderr_end(buf, sizeof(buf));

    if (strstr(buf, "a/b_c/d") == NULL) {
        printf("  FAIL T1.5: <data> nao sanitizado como esperado: %s", buf);
        fails++;
    }

    count = 0;
    p = buf;
    while ((p = strstr(p, " | ")) != NULL) {
        count++;
        p += 3;
    }
    if (count != 5) {
        printf("  FAIL T1.5: esperava 5 ocorrencias de ' | ', obteve %d: %s", count, buf);
        fails++;
    }
    return fails;
}

/* ---------------------------------------------------------------------------
 * T1.8 -- review pos-Task 3, "Also fix" #2: um espaco duplicado entre dois
 * pares k=v validos colapsa num UNICO separador (nao deve sobrar nenhum '_'
 * de um "token vazio"); um espaco inicial antes do primeiro par nao
 * sobrevive (senao <data> comecaria por espaco, e a linha conteria
 * "| <space>a=1" logo a seguir ao separador estrutural " | ").
 * -----------------------------------------------------------------------*/
static int test_t1_8_data_spacing(void)
{
    char buf[2048];
    int fails = 0;

    capture_stderr_begin();
    ps3_trace_emit(PS3_TS_DMA, "fnY", "evY", "%s", " a=1  b=2");
    capture_stderr_end(buf, sizeof(buf));

    if (strstr(buf, "| a=1 b=2") == NULL) {
        printf("  FAIL T1.8: espaco duplicado/inicial nao colapsou como esperado: %s", buf);
        fails++;
    }
    if (strstr(buf, "_") != NULL) {
        printf("  FAIL T1.8: '_' inesperado (espaco duplicado/inicial nao deve gerar "
               "token vazio): %s", buf);
        fails++;
    }
    return fails;
}

/* ---------------------------------------------------------------------------
 * T1.6 -- ps3_trace_set_thread(0x12345678) reflete-se em <thr>; uma segunda
 * thread (nunca chamou set_thread) nao ve o valor da primeira.
 * -----------------------------------------------------------------------*/
static void* thread_checker(void* arg)
{
    int* out = (int*)arg;
    *out = (ps3_trace_thread() == 0) ? 1 : 0;
    return NULL;
}

static int test_t1_6_thread_tls(void)
{
    char buf[2048];
    int fails = 0;
    int second_saw_zero = 0;
    pthread_t th;

    ps3_trace_set_thread(0x12345678u);

    capture_stderr_begin();
    ps3_trace_emit(PS3_TS_THREADS, "fnT", "evT", NULL);
    capture_stderr_end(buf, sizeof(buf));

    if (strstr(buf, "| 0x12345678 |") == NULL) {
        printf("  FAIL T1.6: <thr> nao refletiu 0x12345678: %s", buf);
        fails++;
    }

    pthread_create(&th, NULL, thread_checker, &second_saw_zero);
    pthread_join(th, NULL);
    if (!second_saw_zero) {
        printf("  FAIL T1.6: segunda thread viu o thread-id da primeira (TLS nao isolado)\n");
        fails++;
    }

    ps3_trace_set_thread(0); /* restaura o default, para nao afetar outros testes */
    return fails;
}

/* ---------------------------------------------------------------------------
 * T1.7 -- os dez tokens <subsys> sao exactamente os de G2, na ordem exacta.
 * -----------------------------------------------------------------------*/
static int test_t1_7_subsys_tokens(void)
{
    static const char* const expected[PS3_TS__COUNT] = {
        "CHECKPOINTS", "MEMORY", "HEAP", "THREADS", "SPURS",
        "DMA", "FIOS", "MEDIA", "RSX", "SNAPSHOT",
    };
    int fails = 0;
    int i;

    for (i = 0; i < PS3_TS__COUNT; i++) {
        const char* got = ps3_trace_subsys_name((ps3_trace_subsys)i);
        if (strcmp(got, expected[i]) != 0) {
            printf("  FAIL T1.7: subsys[%d] esperado \"%s\", obteve \"%s\"\n",
                   i, expected[i], got);
            fails++;
        }
    }
    return fails;
}

/* ---------------------------------------------------------------------------
 * T1.1 -- todas as PS3_TRACE_* por definir: ps3_trace_on(s)==0 para os 10 e
 * um PS3_TRACE(...) de cada subsistema produz 0 bytes em stderr.
 * Correr apenas no processo "gate_all_off" (env sem nenhuma das dez vars).
 * -----------------------------------------------------------------------*/
static int test_t1_1_all_off(void)
{
    char buf[4096];
    int fails = 0;
    int i;
    size_t n;

    for (i = 0; i < PS3_TS__COUNT; i++) {
        if (ps3_trace_on((ps3_trace_subsys)i) != 0) {
            printf("  FAIL T1.1: ps3_trace_on(%d) devolveu ligado sem env var definida\n", i);
            fails++;
        }
    }

    capture_stderr_begin();
    for (i = 0; i < PS3_TS__COUNT; i++) {
        PS3_TRACE((ps3_trace_subsys)i, "fn", "ev", "k=%d", i);
    }
    n = capture_stderr_end(buf, sizeof(buf));

    if (n != 0) {
        printf("  FAIL T1.1: esperava 0 bytes em stderr, obteve %zu: %s\n", n, buf);
        fails++;
    }
    return fails;
}

/* ---------------------------------------------------------------------------
 * T1.2 -- PS3_TRACE_RSX=1: a linha emitida casa exactamente com o regex do
 * brief. Correr apenas no processo "gate_rsx" (env com so PS3_TRACE_RSX=1).
 * -----------------------------------------------------------------------*/
static int test_t1_2_rsx_regex(void)
{
    char buf[4096];
    int fails = 0;
    size_t n;
    regex_t re;
    int rc;
    const char* pattern =
        "^\\[PS3T\\] [0-9]+ \\| (0x[0-9A-F]{8}|-) \\| [^|]* \\| RSX \\| [^| ]+ \\| .*$";

    capture_stderr_begin();
    PS3_TRACE(PS3_TS_RSX, "fnR", "evR", "k=%d", 7);
    n = capture_stderr_end(buf, sizeof(buf));

    if (n == 0) {
        printf("  FAIL T1.2: PS3_TRACE_RSX=1 nao emitiu nada\n");
        return 1;
    }
    /* remove o '\n' final para o regex ancorar '$' no fim da string, sem
     * depender de REG_NEWLINE (a linha nunca tem '\n' embutido -- so o final,
     * dado que fn/event/data ja saem sanitizados). */
    if (buf[n - 1] == '\n') {
        buf[n - 1] = '\0';
    }

    rc = regcomp(&re, pattern, REG_EXTENDED);
    if (rc != 0) {
        printf("  FAIL T1.2: regcomp falhou (rc=%d)\n", rc);
        return 1;
    }
    rc = regexec(&re, buf, 0, NULL, 0);
    if (rc != 0) {
        printf("  FAIL T1.2: linha nao bate com o regex: %s\n", buf);
        fails++;
    }
    regfree(&re);
    return fails;
}

/* ---------------------------------------------------------------------------
 * T1.3 -- PS3_TRACE_HEAP=1 nao liga nenhum dos outros nove. Correr apenas no
 * processo "gate_heap" (env com so PS3_TRACE_HEAP=1).
 * -----------------------------------------------------------------------*/
static int test_t1_3_only_heap(void)
{
    int fails = 0;
    int i;

    if (ps3_trace_on(PS3_TS_HEAP) != 1) {
        printf("  FAIL T1.3: PS3_TRACE_HEAP=1 mas ps3_trace_on(HEAP) != 1\n");
        fails++;
    }
    for (i = 0; i < PS3_TS__COUNT; i++) {
        if (i == PS3_TS_HEAP) {
            continue;
        }
        if (ps3_trace_on((ps3_trace_subsys)i) != 0) {
            printf("  FAIL T1.3: subsys %d ligou junto com HEAP\n", i);
            fails++;
        }
    }
    return fails;
}

/* ---------------------------------------------------------------------------
 * main -- dispatch por cenario (ver cabecalho do ficheiro / run_tests.sh).
 * -----------------------------------------------------------------------*/
int main(int argc, char** argv)
{
    const char* mode = (argc > 1) ? argv[1] : "format";
    int fails = 0;

    if (strcmp(mode, "format") == 0) {
        fails += test_t1_4_absent_fields();
        fails += test_t1_5_sanitization();
        fails += test_t1_6_thread_tls();
        fails += test_t1_7_subsys_tokens();
        fails += test_t1_8_data_spacing();
    } else if (strcmp(mode, "gate_all_off") == 0) {
        fails += test_t1_1_all_off();
    } else if (strcmp(mode, "gate_rsx") == 0) {
        fails += test_t1_2_rsx_regex();
    } else if (strcmp(mode, "gate_heap") == 0) {
        fails += test_t1_3_only_heap();
    } else {
        fprintf(stdout, "modo desconhecido: %s (usar format|gate_all_off|gate_rsx|gate_heap)\n", mode);
        return 2;
    }

    if (fails == 0) {
        printf("[%s] 0 failed\n", mode);
        return 0;
    }
    printf("[%s] %d failed\n", mode, fails);
    return 1;
}
