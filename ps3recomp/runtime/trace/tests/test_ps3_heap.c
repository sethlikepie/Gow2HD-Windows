/* test_ps3_heap.c -- teste unitario de runtime/syscalls/sys_memory.c
 * (Task 3 do plano "bringup-instrumentacao-fase0"): subsistema HEAP --
 * ALLOC/FREE/FREELIST canonicos no allocator LV2 sys_memory (o allocator
 * host REAL instrumentado; ver o comentario grande junto de
 * sys_memory_allocate() em runtime/syscalls/sys_memory.c para o porque este
 * e' o allocator host-observavel e nao o allocator CRT/game-heap de
 * free-list/boundary-tag que o CLAUDE.md descreve -- esse e' codigo PPC
 * liftado, nao host).
 *
 * Standalone; NAO liga ppu_loader.cpp nem o runtime inteiro. Compilar e
 * correr com:
 *
 *   clang -std=c11 -Wall -Wextra -O0 \
 *       -I <repo>/include \
 *       <repo>/runtime/trace/ps3_trace.c \
 *       <repo>/runtime/syscalls/sys_memory.c \
 *       <repo>/runtime/trace/tests/test_ps3_heap.c \
 *       -o /tmp/test_ps3_heap
 *   /tmp/test_ps3_heap off
 *   PS3_TRACE_HEAP=1 /tmp/test_ps3_heap alloc_free
 *   PS3_TRACE_HEAP=1 /tmp/test_ps3_heap freelist
 *   PS3_TRACE_HEAP=1 /tmp/test_ps3_heap free_missing
 *
 * Ou, mais simples, o runner desta pasta:
 *
 *   ./run_tests_heap.sh
 *
 * Cada cenario corre no SEU PROPRIO processo: (a) ps3_trace_on() resolve
 * getenv() uma vez por subsistema e cacheia para sempre (mesmo principio dos
 * outros testes desta pasta); (b) os globais de sys_memory.c (bump pointer,
 * tabela de allocations, s_total_allocated) sao estado estatico por-processo
 * -- nao ha sys_memory_init() aqui, por isso cada processo comeca com esse
 * estado a zero (como um `.bss` fresco) e o ENDERECO da PRIMEIRA alocacao de
 * cada processo e' sempre SYS_MEM_ALLOC_BASE = 0x40000000 (deterministico,
 * usado pelos asserts abaixo).
 *
 * vm_base: sys_memory_allocate/free chamam vm_commit() (runtime/memory/vm.h)
 * antes de escrever no bloco alocado; este ficheiro reserva os mesmos 4 GB
 * PROT_NONE que vm_init() reserva em producao (mesmo mmap, mesmas flags),
 * para que o mprotect() de vm_commit() tenha uma regiao real para trabalhar
 * -- sem chamar vm_init() em si (que dispararia o checkpoint VM_READY da
 * Task 2, exigindo os seus proprios stubs; nao e' preciso aqui).
 */
#include "../../syscalls/sys_memory.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

/* ---------------------------------------------------------------------------
 * Stub de ps3_timebase_now() -- ps3_trace.c declara-o `extern` (definido de
 * verdade em runtime/syscalls/sys_timer.c:39, nao linkado aqui); mesmo
 * principio dos outros testes desta pasta.
 * -----------------------------------------------------------------------*/
uint64_t ps3_timebase_now(void)
{
    static uint64_t counter = 1;
    return counter++;
}

/* ---------------------------------------------------------------------------
 * vm_base -- ver nota no cabecalho do ficheiro.
 * -----------------------------------------------------------------------*/
uint8_t* vm_base = NULL;

static int setup_vm(void)
{
    vm_base = (uint8_t*)mmap(NULL, (size_t)VM_TOTAL_SIZE, PROT_NONE,
                             MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (vm_base == MAP_FAILED) {
        vm_base = NULL;
        return -1;
    }
    return 0;
}

/* ---------------------------------------------------------------------------
 * Captura de stderr -- identica aos outros testes desta pasta.
 * -----------------------------------------------------------------------*/
static int  s_saved_stderr_fd = -1;
static int  s_capture_fd = -1;
static char s_capture_path[] = "/tmp/ps3_heap_test_XXXXXX";

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
 * heap_test_ctx_init -- prepara o ppu_context para sys_memory_allocate(ctx)
 * com o padrao (r3=size, r4=flags 0 -> alinhamento 64KB default, r5=addr_out
 * 0 -> nao escreve de volta). Extraido de test_alloc_free() (Fase 1 Task 5)
 * para reuso por test_alloc_regista_range() abaixo, sem alterar o
 * comportamento dos testes ja existentes.
 * -----------------------------------------------------------------------*/
static void heap_test_ctx_init(ppu_context* ctx, uint32_t size)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->gpr[3] = size; /* size */
    ctx->gpr[4] = 0;    /* flags: default 64KB alignment */
    ctx->gpr[5] = 0;    /* addr_out: 0 -> nao escreve de volta */
}

/* ---------------------------------------------------------------------------
 * Fase 1 (fase1-desmascarar-memoria) Task 5: cada vm_commit bem-sucedido tem
 * de registar a range comitada via ppu_register_committed_range(), senao o
 * vm_uncommitted_guard (runtime/ppu/ppu_loader.cpp) trata a memoria como
 * nao-comitada e ENGOLE escritas do jogo a memoria que ele proprio alocou.
 * Medido antes desta task: 5 chamadas a vm_commit em sys_memory.c, 0 a
 * ppu_register_committed_range.
 *
 * Duplo do simbolo real definido em runtime/ppu/ppu_loader.cpp:538 (esse
 * ficheiro NAO e' ligado por este teste standalone -- ver o cabecalho do
 * ficheiro); conta chamadas e guarda o ultimo par (lo, hi).
 * -----------------------------------------------------------------------*/
static unsigned g_reg_calls = 0;
static uint32_t g_reg_lo = 0, g_reg_hi = 0;

/* Fase 1 Task 5 Round 1: espelha PPU_MAX_COMMITTED_RANGES e o array
 * g_committed_lo/hi de ppu_loader.cpp:497-500, para o duplo poder provar a
 * mesma logica de contencao + aviso-uma-vez que foi inserida na funcao real
 * (ppu_loader.cpp:538, ficheiro protegido -- este teste continua sem o
 * ligar). g_reg_overflow_attempts conta quantas vezes uma range NAO contida
 * foi rejeitada por falta de slot; g_reg_warn_emits conta quantas vezes o
 * aviso (fprintf) teria saido -- tem de ficar em <=1 mesmo com varias
 * rejeicoes. */
#define TEST_MAX_COMMITTED_RANGES 8
static uint32_t g_reg_ranges_lo[TEST_MAX_COMMITTED_RANGES];
static uint32_t g_reg_ranges_hi[TEST_MAX_COMMITTED_RANGES];
static int      g_reg_ranges_count = 0;
static unsigned g_reg_overflow_attempts = 0;
static unsigned g_reg_warn_emits = 0;

void ppu_register_committed_range(uint32_t lo, uint32_t hi)
{
    int i;
    g_reg_calls++; g_reg_lo = lo; g_reg_hi = hi;

    /* Espelha ppu_loader.cpp:538-569 (Fase 1 Task 5 Round 1; revisao final
     * de branch acrescentou o lock/atomic em volta desta logica, ver I1):
     * range ja inteiramente coberta por uma range registada nao consome
     * slot novo. */
    for (i = 0; i < g_reg_ranges_count; i++) {
        if (lo >= g_reg_ranges_lo[i] && hi <= g_reg_ranges_hi[i])
            return;
    }
    if (g_reg_ranges_count < TEST_MAX_COMMITTED_RANGES) {
        g_reg_ranges_lo[g_reg_ranges_count] = lo;
        g_reg_ranges_hi[g_reg_ranges_count] = hi;
        g_reg_ranges_count++;
        return;
    }
    /* Array cheio e a range NAO esta coberta: aviso uma unica vez, espelha
     * o "static int warned" da funcao real. */
    g_reg_overflow_attempts++;
    {
        static int warned = 0;
        if (!warned) { warned = 1; g_reg_warn_emits++; }
    }
}

static int s_regista_fails = 0;
static void check(int cond, const char* what)
{
    /* Prefixo generico (nao o nome de um teste especifico): partilhado por
     * test_alloc_regista_range/test_regista_contencao/
     * test_mmapper_alloc_commit_falha -- o [FAIL] <mode> do run_tests_heap.sh
     * ja identifica o cenario. */
    if (!cond) { printf("  FAIL: %s\n", what); s_regista_fails++; }
}

static int test_alloc_regista_range(void)
{
    ppu_context ctx;
    int64_t rc;
    char buf[4096];

    if (setup_vm() != 0) { printf("  FAIL alloc_regista_range: mmap falhou\n"); return 1; }

    s_regista_fails = 0;
    g_reg_calls = 0; g_reg_lo = 0; g_reg_hi = 0;

    /* Mesmo padrao de test_ps3_heap.c (test_alloc_free) -- sys_memory_allocate
     * le os argumentos do ppu_context (r3 = size, r4 = flags, r5 = ponteiro
     * de saida do EA) e devolve int64_t. */
    heap_test_ctx_init(&ctx, /*size=*/0x10000u);

    capture_stderr_begin();
    rc = sys_memory_allocate(&ctx);
    capture_stderr_end(buf, sizeof(buf));

    check(rc == CELL_OK,    "alloc devolveu CELL_OK");
    check(g_reg_calls == 1, "ppu_register_committed_range chamado 1x");
    check(g_reg_hi - g_reg_lo == 0x10000u,
          "range registada cobre exactamente o tamanho pedido");

    return s_regista_fails;
}

/* ---------------------------------------------------------------------------
 * Fase 1 Task 5 Round 1: review confirmou que registar em TODA chamada
 * (mesmo redundante) esgota o array fixo de PPU_MAX_COMMITTED_RANGES=8 sem
 * beneficio -- medido in-boot: 5 chamadas de sys_memory.c, todas dentro da
 * regiao baixa ja pre-registada por boot_macos.cpp, encheram o array a 8/8.
 * Prova, via o duplo acima (que agora espelha a contencao + aviso-uma-vez
 * inseridos em ppu_register_committed_range real, ppu_loader.cpp:538):
 *   1. range CONTIDA numa ja registada nao consome slot;
 *   2. range NAO contida consome;
 *   3. com o array cheio, uma range nova e' rejeitada e o aviso sai 1x so',
 *      mesmo com rejeicoes repetidas.
 * -----------------------------------------------------------------------*/
static int test_regista_contencao(void)
{
    s_regista_fails = 0;
    g_reg_calls = 0; g_reg_lo = 0; g_reg_hi = 0;
    g_reg_ranges_count = 0;
    g_reg_overflow_attempts = 0;
    g_reg_warn_emits = 0;

    /* 1) range base -> consome o 1o slot */
    ppu_register_committed_range(0x1000u, 0x2000u);
    check(g_reg_ranges_count == 1, "range base consome 1 slot");

    /* 2) range CONTIDA na base -- nao deve consumir slot novo */
    ppu_register_committed_range(0x1200u, 0x1800u);
    check(g_reg_ranges_count == 1, "range contida NAO consome slot");

    /* 3) range NAO contida (disjunta) -- deve consumir slot */
    ppu_register_committed_range(0x3000u, 0x4000u);
    check(g_reg_ranges_count == 2, "range nao contida consome slot");

    /* 4) preenche ate ao limite: 6 ranges disjuntas adicionais -> total 8 */
    {
        uint32_t base = 0x5000u;
        int i;
        for (i = 0; i < 6; i++) {
            ppu_register_committed_range(base, base + 0x1000u);
            base += 0x2000u;
        }
    }
    check(g_reg_ranges_count == 8, "array cheio com 8 ranges");

    /* 5) array cheio: range NOVA (nao contida) tem de ser rejeitada e o
     * aviso tem de sair exactamente 1 vez */
    ppu_register_committed_range(0x11000u, 0x12000u);
    check(g_reg_ranges_count == 8,        "array cheio nao cresce alem do limite");
    check(g_reg_overflow_attempts == 1,   "1a rejeicao contada");
    check(g_reg_warn_emits == 1,          "aviso emitido exactamente 1x");

    /* 6) segunda rejeicao -- overflow conta de novo, mas o aviso NAO repete */
    ppu_register_committed_range(0x13000u, 0x14000u);
    check(g_reg_ranges_count == 8,        "array continua em 8 apos 2a rejeicao");
    check(g_reg_overflow_attempts == 2,   "2a rejeicao tambem contada");
    check(g_reg_warn_emits == 1,          "aviso NAO sai de novo na 2a rejeicao");

    return s_regista_fails;
}

/* ---------------------------------------------------------------------------
 * Fase 1 Task 5 Round 2: sys_mmapper_allocate_address, antes desta correcao,
 * devolvia sempre CELL_OK e escrevia addr_out mesmo quando vm_commit()
 * falhava -- o guest recebia um "endereco valido" para memoria nunca
 * comitada. Alinhado agora com sys_memory_allocate (:194-196) e
 * sys_memory_allocate_from_container (:407): falha de vm_commit -> devolve
 * CELL_ENOMEM, NAO escreve addr_out, NAO regista a range.
 *
 * Nao ha duplo de vm_commit (e' `static inline` em runtime/memory/vm.h,
 * incluido directamente, sem indireccao possivel neste ficheiro) -- por
 * isso esta e' uma falha REAL, nao fabricada: reservamos uma janela mmap
 * deliberadamente MAIS PEQUENA do que SYS_MEM_ALLOC_BASE (0x40000000), a
 * primeira alocacao deterministica desta familia de syscalls num processo
 * fresco. Quando sys_mmapper_allocate_address tenta mprotect() em
 * vm_base+0x40000000, esse endereco cai fora da regiao mapeada por este
 * mmap (so' os primeiros 256 MB) e mprotect() falha com ENOMEM genuino ->
 * vm_commit() devolve CELL_ENOMEM. Os primeiros 0x10000 bytes da janela
 * pequena ficam RW para podermos colocar um sentinela em addr_out=0x1000 e
 * confirmar, por leitura directa (nao por PS3_TRACE), que write_be32() NAO
 * correu.
 * -----------------------------------------------------------------------*/
static int test_mmapper_alloc_commit_falha(void)
{
    ppu_context ctx;
    int64_t rc;
    uint8_t* small_vm;
    volatile uint32_t* sentinel;

    s_regista_fails = 0;
    g_reg_calls = 0; g_reg_lo = 0; g_reg_hi = 0;

    small_vm = (uint8_t*)mmap(NULL, 0x10000000u, PROT_NONE,
                              MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (small_vm == MAP_FAILED) {
        printf("  FAIL mmapper_alloc_commit_falha: mmap (janela pequena) falhou\n");
        return 1;
    }
    if (mprotect(small_vm, 0x10000u, PROT_READ | PROT_WRITE) != 0) {
        printf("  FAIL mmapper_alloc_commit_falha: mprotect inicial (RW) falhou\n");
        munmap(small_vm, 0x10000000u);
        return 1;
    }
    vm_base = small_vm;

    sentinel  = (uint32_t*)vm_to_host(0x1000u);
    *sentinel = 0xDEADBEEFu;

    memset(&ctx, 0, sizeof(ctx));
    ctx.gpr[3] = 0x10000u; /* size */
    ctx.gpr[4] = 0;        /* flags (nao lido) */
    ctx.gpr[5] = 0x10000u; /* alignment */
    ctx.gpr[6] = 0x1000u;  /* addr_out: dentro da janela RW pequena */

    rc = sys_mmapper_allocate_address(&ctx);

    check(rc == (int64_t)(int32_t)CELL_ENOMEM,
          "vm_commit falhou -> devolve CELL_ENOMEM (nao CELL_OK)");
    check(*sentinel == 0xDEADBEEFu,
          "addr_out NAO foi escrito quando vm_commit falha");
    check(g_reg_calls == 0,
          "ppu_register_committed_range NAO chamado quando vm_commit falha");

    munmap(small_vm, 0x10000000u);
    vm_base = NULL;
    return s_regista_fails;
}

/* ---------------------------------------------------------------------------
 * "off" -- PS3_TRACE_HEAP por definir: sys_memory_allocate/free continuam a
 * funcionar (as linhas antigas "[sys_memory] ..." nao gated saem sempre,
 * G3), mas NENHUMA linha "[PS3T]" aparece.
 * -----------------------------------------------------------------------*/
static int test_off(void)
{
    char buf[4096];
    int fails = 0;
    ppu_context ctx;

    if (setup_vm() != 0) { printf("  FAIL off: mmap falhou\n"); return 1; }

    memset(&ctx, 0, sizeof(ctx));
    ctx.gpr[3] = 0x10000; /* size */

    capture_stderr_begin();
    int64_t rc = sys_memory_allocate(&ctx);
    capture_stderr_end(buf, sizeof(buf));

    if (rc != CELL_OK) {
        printf("  FAIL off: sys_memory_allocate devolveu %lld\n", (long long)rc);
        fails++;
    }
    if (strstr(buf, "[PS3T]") != NULL) {
        printf("  FAIL off: linha [PS3T] apareceu com PS3_TRACE_HEAP desligado: %s\n", buf);
        fails++;
    }
    return fails;
}

/* ---------------------------------------------------------------------------
 * "alloc_free" -- PS3_TRACE_HEAP=1: sys_memory_allocate() emite ALLOC com
 * ea=/size=/align=/arena=main; sys_memory_free() do MESMO bloco emite FREE
 * com ea=/size=/arena=main.
 * -----------------------------------------------------------------------*/
static int test_alloc_free(void)
{
    char buf[4096];
    int fails = 0;
    size_t n;
    ppu_context ctx;
    int64_t rc;

    if (setup_vm() != 0) { printf("  FAIL alloc_free: mmap falhou\n"); return 1; }

    heap_test_ctx_init(&ctx, 0x10000u);

    capture_stderr_begin();
    rc = sys_memory_allocate(&ctx);
    n = capture_stderr_end(buf, sizeof(buf));

    if (rc != CELL_OK) {
        printf("  FAIL alloc_free: sys_memory_allocate devolveu %lld (esperava CELL_OK)\n", (long long)rc);
        fails++;
    }
    if (n == 0 || strstr(buf, "| HEAP | ALLOC |") == NULL) {
        printf("  FAIL alloc_free: nao emitiu HEAP/ALLOC: %s\n", buf);
        fails++;
    }
    if (strstr(buf, "ea=0x40000000 size=65536 align=65536 arena=main") == NULL) {
        printf("  FAIL alloc_free: campos do ALLOC inesperados: %s\n", buf);
        fails++;
    }

    memset(&ctx, 0, sizeof(ctx));
    ctx.gpr[3] = 0x40000000u; /* addr: a alocacao acima, deterministica (1a deste processo) */

    capture_stderr_begin();
    rc = sys_memory_free(&ctx);
    n = capture_stderr_end(buf, sizeof(buf));

    if (rc != CELL_OK) {
        printf("  FAIL alloc_free: sys_memory_free devolveu %lld (esperava CELL_OK)\n", (long long)rc);
        fails++;
    }
    if (n == 0 || strstr(buf, "| HEAP | FREE |") == NULL) {
        printf("  FAIL alloc_free: nao emitiu HEAP/FREE: %s\n", buf);
        fails++;
    }
    if (strstr(buf, "ea=0x40000000 size=65536 arena=main") == NULL) {
        printf("  FAIL alloc_free: campos do FREE inesperados: %s\n", buf);
        fails++;
    }
    return fails;
}

/* ---------------------------------------------------------------------------
 * "freelist" -- PS3_TRACE_HEAP=1: sys_memory_allocate_from_container() bump-
 * fresco emite so' ALLOC (arena=<cid>); depois de libertado, um segundo
 * pedido MAIS PEQUENO ao MESMO container reaproveita o bloco (loop "Reuse a
 * freed block first" real, nao fabricado) -- emite FREELIST seguido de
 * ALLOC, ambos com o endereco/size do bloco REUTILIZADO (size=65536, o
 * tamanho original, nao o pedido novo de 32768 -- ver o comentario em
 * sys_memory_allocate_from_container()).
 * -----------------------------------------------------------------------*/
static int test_freelist(void)
{
    char buf[4096];
    int fails = 0;
    size_t n;
    ppu_context ctx;
    int64_t rc;

    if (setup_vm() != 0) { printf("  FAIL freelist: mmap falhou\n"); return 1; }

    /* 1) bump-fresco: cid=5 em r3 (ordem do thunk do GoW2), size=0x10000 em r4 */
    memset(&ctx, 0, sizeof(ctx));
    ctx.gpr[3] = 5;
    ctx.gpr[4] = 0x10000;

    capture_stderr_begin();
    rc = sys_memory_allocate_from_container(&ctx);
    n = capture_stderr_end(buf, sizeof(buf));

    if (rc != CELL_OK) {
        printf("  FAIL freelist: 1a allocate_from_container devolveu %lld\n", (long long)rc);
        fails++;
    }
    if (strstr(buf, "| HEAP | FREELIST |") != NULL) {
        printf("  FAIL freelist: FREELIST apareceu num bump fresco (sem bloco previamente libertado): %s\n", buf);
        fails++;
    }
    if (n == 0 || strstr(buf, "| HEAP | ALLOC |") == NULL ||
        strstr(buf, "ea=0x40000000 size=65536 align=65536 arena=5") == NULL) {
        printf("  FAIL freelist: 1o ALLOC (bump fresco) inesperado: %s\n", buf);
        fails++;
    }

    /* 2) liberta o bloco */
    memset(&ctx, 0, sizeof(ctx));
    ctx.gpr[3] = 0x40000000u;
    capture_stderr_begin();
    rc = sys_memory_free(&ctx);
    capture_stderr_end(buf, sizeof(buf));
    if (rc != CELL_OK) {
        printf("  FAIL freelist: sys_memory_free devolveu %lld\n", (long long)rc);
        fails++;
    }

    /* 3) segundo pedido, MAIS PEQUENO, ao MESMO cid -> deve reaproveitar */
    memset(&ctx, 0, sizeof(ctx));
    ctx.gpr[3] = 5;
    ctx.gpr[4] = 0x8000; /* metade do bloco libertado */

    capture_stderr_begin();
    rc = sys_memory_allocate_from_container(&ctx);
    n = capture_stderr_end(buf, sizeof(buf));

    if (rc != CELL_OK) {
        printf("  FAIL freelist: 2a allocate_from_container devolveu %lld\n", (long long)rc);
        fails++;
    }
    if (n == 0 || strstr(buf, "| HEAP | FREELIST |") == NULL) {
        printf("  FAIL freelist: 2o pedido nao emitiu FREELIST (reuso esperado): %s\n", buf);
        fails++;
    }
    if (strstr(buf, "ea=0x40000000 size=65536 arena=5") == NULL) {
        printf("  FAIL freelist: FREELIST nao tem o endereco/tamanho do bloco reutilizado: %s\n", buf);
        fails++;
    }
    if (count_substr(buf, "| HEAP | ALLOC |") != 1) {
        printf("  FAIL freelist: esperava exactamente 1 linha ALLOC no 2o pedido, obteve %d: %s\n",
               count_substr(buf, "| HEAP | ALLOC |"), buf);
        fails++;
    }
    return fails;
}

/* ---------------------------------------------------------------------------
 * "free_missing" -- PS3_TRACE_HEAP=1: sys_memory_free() de um endereco NUNCA
 * alocado devolve CELL_EINVAL e nao emite HEAP/FREE nenhum (o loop nunca
 * encontra o slot, o PS3_TRACE fica dentro do `if` que so' corre em caso de
 * sucesso).
 * -----------------------------------------------------------------------*/
static int test_free_missing(void)
{
    char buf[4096];
    int fails = 0;
    ppu_context ctx;
    int64_t rc;

    if (setup_vm() != 0) { printf("  FAIL free_missing: mmap falhou\n"); return 1; }

    memset(&ctx, 0, sizeof(ctx));
    ctx.gpr[3] = 0x4DEADBEEu; /* endereco nunca alocado neste processo */

    capture_stderr_begin();
    rc = sys_memory_free(&ctx);
    capture_stderr_end(buf, sizeof(buf));

    if (rc != (int64_t)(int32_t)CELL_EINVAL) {
        printf("  FAIL free_missing: esperava CELL_EINVAL, obteve %lld\n", (long long)rc);
        fails++;
    }
    if (strstr(buf, "| HEAP | FREE |") != NULL) {
        printf("  FAIL free_missing: FREE apareceu para um endereco nunca alocado: %s\n", buf);
        fails++;
    }
    return fails;
}

/* ---------------------------------------------------------------------------
 * main -- dispatch por cenario (ver cabecalho do ficheiro / run_tests_heap.sh).
 * -----------------------------------------------------------------------*/
int main(int argc, char** argv)
{
    const char* mode = (argc > 1) ? argv[1] : "off";
    int fails = 0;

    if (strcmp(mode, "off") == 0) {
        fails += test_off();
    } else if (strcmp(mode, "alloc_free") == 0) {
        fails += test_alloc_free();
    } else if (strcmp(mode, "freelist") == 0) {
        fails += test_freelist();
    } else if (strcmp(mode, "free_missing") == 0) {
        fails += test_free_missing();
    } else if (strcmp(mode, "alloc_regista_range") == 0) {
        fails += test_alloc_regista_range();
    } else if (strcmp(mode, "regista_contencao") == 0) {
        fails += test_regista_contencao();
    } else if (strcmp(mode, "mmapper_alloc_commit_falha") == 0) {
        fails += test_mmapper_alloc_commit_falha();
    } else {
        fprintf(stdout, "modo desconhecido: %s (usar off|alloc_free|freelist|free_missing|alloc_regista_range|regista_contencao|mmapper_alloc_commit_falha)\n", mode);
        return 2;
    }

    if (fails == 0) {
        printf("[%s] 0 failed\n", mode);
        return 0;
    }
    printf("[%s] %d failed\n", mode, fails);
    return 1;
}
