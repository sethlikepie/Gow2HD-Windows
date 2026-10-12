/* test_dma_trace_main.c -- teste unitario do subsistema DMA (Task 4 do plano
 * "bringup-instrumentacao-fase0"): GET/PUT/LIST/DONE do motor MFC
 * (runtime/spu/spu_dma.h), campos ea=/lsa=/size=/tag=; TAG_WAIT usa um
 * conjunto DIFERENTE -- mask=/mode=/result=, sem ea=/lsa=/size= e sem tag=
 * (corrigido 2026-07-24 -- ver o comentario grande em mfc_tag_wait() em
 * spu_dma.h e o relatorio da Task 4), gate off -> zero bytes.
 *
 * Standalone; usa o mfc_engine harness (mesma familia de
 * runtime/spu/tests/test_dma_main.c, que cobre a MECANICA da transferencia --
 * este ficheiro cobre o TRACE). Define SPU_DMA_TRACE_HOOKS antes de incluir
 * spu_dma.h para activar os PS3_TRACE(...) que ali vivem por tras desse gate
 * de compilacao (ver o comentario grande no topo de spu_dma.h para o motivo:
 * sem o gate, TODOS os outros testes SPU que incluem este header teriam de
 * ligar ps3_trace.c so' por causa deste ficheiro).
 *
 * Fornece os proprios stubs de vm_base/ppu_vm_size/ppu_guest_range_committed
 * (mesmo padrao de test_dma_main.c) e de ps3_timebase_now() (mesmo padrao de
 * runtime/trace/tests/test_ps3_mem_trace.c -- ps3_trace_emit() declara-o
 * `extern`, definido de verdade em runtime/syscalls/sys_timer.c, que este
 * teste isolado nao quer arrastar).
 *
 * ps3_trace_on() resolve getenv() uma vez por subsistema e nunca mais re-le
 * -- por isso "off" e "on" correm cada uma no SEU PROPRIO processo, mesmo
 * principio de test_ps3_mem_trace.c / run_tests_mem_trace.sh.
 *
 * Usage: ./test_dma_trace.exe off|on
 * Ou, mais simples, o runner desta pasta: ./run_tests_dma_trace.sh
 */
#define SPU_DMA_TRACE_HOOKS 1
#include "spu_dma.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

/* ---------------------------------------------------------------------------
 * Stubs exigidos por spu_dma.h (mesmo padrao de test_dma_main.c).
 * -----------------------------------------------------------------------*/
static uint8_t g_mem[4096];
uint8_t* vm_base = g_mem;

uint32_t ppu_vm_size = 0;   /* 0 desliga o guard de tamanho de endereco */
int ppu_guest_range_committed(uint32_t addr, uint32_t n)
{
    (void)addr; (void)n;
    return 1;   /* este teste nao exercita o guard de commit -- ja coberto
                 * em test_dma_main.c */
}

/* ps3_timebase_now() -- ver nota no cabecalho do ficheiro. */
uint64_t ps3_timebase_now(void) { return 1; }

/* ---------------------------------------------------------------------------
 * Captura de stderr -- identica a runtime/trace/tests/test_ps3_mem_trace.c.
 * -----------------------------------------------------------------------*/
static int  s_saved_stderr_fd = -1;
static int  s_capture_fd = -1;
static char s_capture_path[] = "/tmp/ps3_dma_trace_test_XXXXXX";

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
 * Cenario comum: GET (ea=0x40, lsa=0x100, size=64, tag=5), depois PUT
 * (lsa=0x100, ea=0x140, size=64, tag=6), depois um LIST de 1 elemento
 * (GETL), depois um TAG_WAIT (mask=ALL). Via mfc_channel_write() -- o
 * mesmo caminho que uma escrita real de canal SPU (`spu_wrch`) usaria --
 * nao chamadas diretas as funcoes mfc_do_* (essas ja sao cobertas por
 * test_dma_main.c).
 * -----------------------------------------------------------------------*/
static void run_dma_scenario(mfc_engine* mfc, spu_context* ctx)
{
    /* GET: main_mem[0x40..0x40+64) -> LS[0x100..0x100+64) */
    for (uint32_t i = 0; i < 64; i++) g_mem[0x40 + i] = (uint8_t)(0xA0 + i);
    mfc_channel_write(mfc, ctx, MFC_LSA, 0x100);
    mfc_channel_write(mfc, ctx, MFC_EAH, 0);
    mfc_channel_write(mfc, ctx, MFC_EAL, 0x40);
    mfc_channel_write(mfc, ctx, MFC_Size, 64);
    mfc_channel_write(mfc, ctx, MFC_TagID, 5);
    mfc_channel_write(mfc, ctx, MFC_Cmd, MFC_GET_CMD);

    /* PUT: LS[0x100..0x100+64) -> main_mem[0x140..0x140+64) */
    mfc_channel_write(mfc, ctx, MFC_LSA, 0x100);
    mfc_channel_write(mfc, ctx, MFC_EAH, 0);
    mfc_channel_write(mfc, ctx, MFC_EAL, 0x140);
    mfc_channel_write(mfc, ctx, MFC_Size, 64);
    mfc_channel_write(mfc, ctx, MFC_TagID, 6);
    mfc_channel_write(mfc, ctx, MFC_Cmd, MFC_PUT_CMD);

    /* LIST (GETL): um elemento de 8 bytes na LS 0x300 -- {size=32, eal=0x200}.
     * mfc_do_list_transfer() usa spu->mfc_lsa (o MESMO registo/canal MFC_LSA,
     * re-lido de dentro da funcao -- nao list_lsa, o parametro local) como
     * acumulador do destino de CADA elemento; com um so' MFC_LSA=0x300 antes
     * do Cmd, list_lsa (onde o descritor e' lido) e o destino do 1o elemento
     * COINCIDEM em 0x300 -- comportamento existente de spu_dma.h, nao algo
     * que este teste tenta "corrigir". Por isso a linha GET aninhada abaixo
     * espera lsa=0x00300, nao um endereco separado. */
    for (uint32_t i = 0; i < 32; i++) g_mem[0x200 + i] = (uint8_t)(0xC0 + i);
    spu_ls_write32(ctx, 0x300, 32u);          /* size_and_flags: size=32, sem flags */
    spu_ls_write32(ctx, 0x304, 0x00000200u);  /* eal */
    mfc_channel_write(mfc, ctx, MFC_LSA, 0x300);  /* endereco da lista E' o acumulador inicial */
    mfc_channel_write(mfc, ctx, MFC_EAH, 0);
    mfc_channel_write(mfc, ctx, MFC_EAL, 0);
    mfc_channel_write(mfc, ctx, MFC_Size, 8);      /* tamanho da lista: 1 elemento */
    mfc_channel_write(mfc, ctx, MFC_TagID, 7);
    mfc_channel_write(mfc, ctx, MFC_Cmd, MFC_GETL_CMD);

    /* TAG_WAIT: espera por todos os tags no mask. */
    mfc_channel_write(mfc, ctx, MFC_WrTagMask, 0xFFFFFFFFu);
    mfc_channel_write(mfc, ctx, MFC_WrTagUpdate, MFC_TAG_UPDATE_ALL);
}

/* ---------------------------------------------------------------------------
 * "off" -- PS3_TRACE_DMA por definir: nenhuma linha [PS3T] sai, mesmo com
 * GET+PUT+LIST+TAG_WAIT reais a correr.
 * -----------------------------------------------------------------------*/
static int test_off(void)
{
    mfc_engine mfc;
    spu_context ctx;
    char buf[8192];
    int fails = 0;
    size_t n;

    mfc_engine_init(&mfc);
    spu_context_init(&ctx, 0);

    capture_stderr_begin();
    run_dma_scenario(&mfc, &ctx);
    n = capture_stderr_end(buf, sizeof(buf));

    if (n != 0) {
        printf("  FAIL off: esperava 0 bytes com PS3_TRACE_DMA desligado, obteve %zu:\n%s\n", n, buf);
        fails++;
    }
    return fails;
}

/* ---------------------------------------------------------------------------
 * "on" -- PS3_TRACE_DMA=1: verifica GET/PUT/LIST/TAG_WAIT/DONE com
 * ea=/lsa=/size=/tag= corretos, mais a transferencia real (conteudo).
 * -----------------------------------------------------------------------*/
static int test_on(void)
{
    mfc_engine mfc;
    spu_context ctx;
    char buf[8192];
    int fails = 0;
    size_t n;

    mfc_engine_init(&mfc);
    spu_context_init(&ctx, 0);

    capture_stderr_begin();
    run_dma_scenario(&mfc, &ctx);
    n = capture_stderr_end(buf, sizeof(buf));

    if (n == 0) {
        printf("  FAIL on: PS3_TRACE_DMA=1 nao emitiu nada\n");
        return 1;
    }

    /* GET real (ea=0x40, lsa=0x100, size=64, tag=5). */
    if (!strstr(buf, "| DMA | GET | ea=0x00000040 lsa=0x00100 size=64 tag=5")) {
        printf("  FAIL on: falta a linha GET esperada\n%s\n", buf); fails++;
    }
    /* PUT real (lsa=0x100, ea=0x140, size=64, tag=6). */
    if (!strstr(buf, "| DMA | PUT | ea=0x00000140 lsa=0x00100 size=64 tag=6")) {
        printf("  FAIL on: falta a linha PUT esperada\n%s\n", buf); fails++;
    }
    /* DONE tem de aparecer pelo menos duas vezes (GET + PUT; o elemento da
     * LIST soma uma terceira). */
    if (count_substr(buf, "| DMA | DONE |") < 3) {
        printf("  FAIL on: esperava >=3 linhas DONE (GET+PUT+elemento da LIST)\n%s\n", buf);
        fails++;
    }
    /* LIST real (ea=ea_base=0, lsa=list_lsa=0x300, size=list_size=8, tag=7). */
    if (!strstr(buf, "| DMA | LIST | ea=0x00000000 lsa=0x00300 size=8 tag=7")) {
        printf("  FAIL on: falta a linha LIST esperada\n%s\n", buf); fails++;
    }
    /* O elemento da LIST e' um GET real: main_mem[0x200..0x220) -> LS[0x300..0x320)
     * (ver o comentario em run_dma_scenario() sobre list_lsa == acumulador). */
    if (!strstr(buf, "| DMA | GET | ea=0x00000200 lsa=0x00300 size=32 tag=7")) {
        printf("  FAIL on: falta a linha GET do elemento da LIST\n%s\n", buf); fails++;
    }
    /* TAG_WAIT: mask=0xFFFFFFFF, mode=2 (MFC_TAG_UPDATE_ALL), todos os tags
     * completos (result==mask, pois todos os submits acima ja terminaram --
     * motor sincrono). Chave e' `mask=`, NAO `tag=` (corrigido 2026-07-24 --
     * GET/PUT/LIST/DONE usam `tag=%u`, um id pequeno; TAG_WAIT nao tem um
     * tag unico, e' uma mascara de ate 32 tags, dominio de valor
     * incompativel com `tag=%u`). Esta asserção falha se a chave regredir
     * para `tag=`. */
    if (!strstr(buf, "| DMA | TAG_WAIT | mask=0xFFFFFFFF mode=2 result=0xFFFFFFFF")) {
        printf("  FAIL on: falta a linha TAG_WAIT esperada (chave deve ser mask=, nao tag=)\n%s\n", buf); fails++;
    }
    /* Regressão explícita: garante que a chave antiga `tag=` NÃO aparece
     * associada ao TAG_WAIT (o parser da Task 6 pareia DMA por `tag=`; se
     * `mask=` regredir para `tag=` aqui, ficaria ambíguo com o `tag=` de
     * GET/PUT/LIST/DONE, que carrega um id, não uma máscara). */
    if (strstr(buf, "| DMA | TAG_WAIT | tag=")) {
        printf("  FAIL on: TAG_WAIT regrediu para a chave `tag=` (deve ser `mask=`)\n%s\n", buf); fails++;
    }

    /* Conteudo real da transferencia (a linha de trace nao basta -- confirma
     * que o GET/PUT por tras dela realmente moveu bytes). memcpy nao troca
     * bytes; spu_ls_read32 le big-endian (MSB primeiro), logo
     * g_mem[0x40..0x43]={0xA0,0xA1,0xA2,0xA3} -> 0xA0A1A2A3. */
    if (spu_ls_read32(&ctx, 0x100) != 0xA0A1A2A3u) {
        printf("  FAIL on: GET nao moveu o conteudo esperado para a LS (got 0x%08X)\n",
               spu_ls_read32(&ctx, 0x100));
        fails++;
    }
    if (memcmp(&g_mem[0x140], &g_mem[0x40], 64) != 0) {
        printf("  FAIL on: PUT nao moveu o conteudo esperado para main_mem\n");
        fails++;
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
