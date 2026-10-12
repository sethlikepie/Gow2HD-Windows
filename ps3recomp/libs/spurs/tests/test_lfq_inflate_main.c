/* test_lfq_inflate_main.c — teste unitario do fulfiller host de inflate.
 *
 * Cobre libs/spurs/lfq_host_inflate.c: `lfq_host_inflate_try(const uint8_t*)`,
 * que recebe o comando de 32 bytes que o dearchiver do FIOS empurra pela
 * LFQueue ANY2ANY e faz o raw-deflate na maquina host enquanto o pop protocol
 * v2 do lado SPU nao estiver fiel.
 *
 * Layout do elemento (words BE, documentado no topo de lfq_host_inflate.c):
 *   +0x00 src EA (ja depois do header zlib -> raw deflate)
 *   +0x04 request ptr        (NAO consultado)
 *   +0x08 compressed size    +0x0C opcode/kind (0x0C = inflate)
 *   +0x10 op-struct EA (byte de status, desalinhado de proposito)
 *   +0x14 dest EA            +0x18 max output
 *   +0x1C window bits        (NAO consultado -- o stb usa sempre janela de 32K)
 *
 * O que este teste prende, para alem do caminho feliz:
 *   - cada campo e' lido BIG-ENDIAN no offset certo (um por um, byte-swapped);
 *   - todos os guards recusam SEM tocar no destino;
 *   - inflate falhado nao marca o byte de completion (o waiter tem de continuar
 *     a bloquear em vez de ler lixo);
 *   - nada e' escrito para la de dst+dmax.
 *
 * Sem asset nenhum: a fixture raw-deflate vem de gen_lfq_inflate_fixture.py e
 * o plaintext esperado e' regenerado aqui (lfqi_gen_plain) -- LFQI_PLAIN_FNV1A
 * prova que os dois geradores coincidem antes de qualquer comparacao.
 */
#include "lfq_inflate_fixture.h"
#include "ps3_checkpoint.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Correcao de regressao (bringup-instrumentacao-fase0, Tasks 1-2 vs.
 * run_tests.sh): cellFs.c (linkado aqui so' para resolver
 * cellfs_translate_path) chama ps3_checkpoint_fire() (FIRST_FS_OPEN), por
 * isso este harness liga o $CHECKPOINT_SRC partilhado (ps3_trace.c +
 * ps3_checkpoint.c -- ver run_tests.sh). Correcao pos-review (Task 4
 * Minor #3): os quatro stubs que ps3_checkpoint.c precisa ficam num header
 * partilhado -- ver runtime/trace/tests/trace_link_stubs.h. */
#include "trace_link_stubs.h"

/* ------------------------------------------------------------------ */
/* Ambiente que lfq_host_inflate.c espera do resto do runtime           */
/* ------------------------------------------------------------------ */

/* Memoria guest vista pelo fulfiller (host_ptr = vm_base + ea). 256 KB chega
 * para src + dst de 64 KB; os guards que recusam tamanhos maiores fazem-no
 * ANTES de qualquer deref, logo nao precisam de backing real. */
#define GMEM_SIZE 0x40000u
static unsigned char g_mem[GMEM_SIZE];
unsigned char* vm_base = g_mem;

/* Test double de lfq_ea_readable() (o original vive em cellSpursLFQueue.c).
 * Em POSIX o original e' um `return 1` incondicional -- linkar a TU da fila
 * nao compraria cobertura nenhuma e ainda arrastava cellSpurs.c inteiro. Este
 * duplo modela a semantica do ramo Windows (commit-aware) e, ao recusar EAs
 * fora do g_mem, garante que um teste mal construido falha em vez de ler fora
 * do buffer. `g_ea_readable` desliga-o para exercitar o ramo de recusa. */
static int g_ea_readable = 1;
int lfq_ea_readable(uint32_t ea, uint32_t len)
{
    if (!g_ea_readable) return 0;
    if (!ea || !len || ea + len < ea) return 0;
    return ((uint64_t)ea + (uint64_t)len) <= (uint64_t)GMEM_SIZE;
}

extern int lfq_host_inflate_try(const uint8_t* elem);

/* ------------------------------------------------------------------ */
/* Fixture                                                              */
/* ------------------------------------------------------------------ */

/* Tem de ser byte-a-byte identico a gen_plain() em gen_lfq_inflate_fixture.py. */
static void lfqi_gen_plain(uint8_t* out, uint32_t n)
{
    static const char T[] = "abcdefghijklmnopqrstuvwxyz"
                            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                            "012345678";           /* 61 bytes */
    for (uint32_t i = 0; i < n; i++)
        out[i] = (uint8_t)T[(i * i + i / 7u) % 61u];
}

static uint32_t lfqi_fnv1a(const uint8_t* p, uint32_t n)
{
    uint32_t h = 0x811C9DC5u;
    for (uint32_t i = 0; i < n; i++) { h ^= p[i]; h *= 0x01000193u; }
    return h;
}

static uint8_t g_plain[LFQI_PLAIN_LEN];

/* ------------------------------------------------------------------ */
/* Layout do caso de teste em memoria guest                             */
/* ------------------------------------------------------------------ */

#define ELEM_EA   0x000100u                 /* os 32 bytes do comando       */
#define SRC_EA    0x001000u                 /* stream raw deflate           */
#define OP_EA     0x000203u                 /* byte de status, desalinhado  */
#define DST_EA    0x008000u                 /* saida, 128-aligned           */
#define DMAX      0x10000u                  /* 64 KB = um bloco psarc       */
#define SENTINEL  0xA5

typedef struct {
    uint32_t src, req, csz, kind, opea, dst, dmax, wbits;
} lfqi_elem;

static lfqi_elem elem_default(void)
{
    lfqi_elem e;
    e.src   = SRC_EA;
    e.req   = 0xDEADBEEFu;                  /* +0x04: nunca lido            */
    e.csz   = LFQI_DEFLATE_LEN;
    e.kind  = 0x0Cu;
    e.opea  = OP_EA;
    e.dst   = DST_EA;
    e.dmax  = DMAX;
    e.wbits = 0xFFF4u;                      /* +0x1C: -12, nunca lido       */
    return e;
}

static void be32(uint8_t* p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)v;
}

static void le32(uint8_t* p, uint32_t v)
{
    p[0] = (uint8_t)v;         p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

/* Repoe o mundo: destino todo a SENTINEL, byte de status a 0, fixture no src,
 * elemento serializado BE em ELEM_EA. Devolve o ponteiro host do elemento. */
static const uint8_t* arm(const lfqi_elem* e, uint32_t src_len)
{
    uint8_t* p = g_mem + ELEM_EA;
    g_ea_readable = 1;
    memset(g_mem + DST_EA, SENTINEL, DMAX);
    g_mem[OP_EA] = 0;
    memset(g_mem + SRC_EA, 0, 0x1000);
    memcpy(g_mem + SRC_EA, lfqi_deflate, LFQI_DEFLATE_LEN);
    if (src_len > LFQI_DEFLATE_LEN)         /* padding a seguir ao stream    */
        memset(g_mem + SRC_EA + LFQI_DEFLATE_LEN, 0x5A, src_len - LFQI_DEFLATE_LEN);
    be32(p + 0x00, e->src);  be32(p + 0x04, e->req);
    be32(p + 0x08, e->csz);  be32(p + 0x0C, e->kind);
    be32(p + 0x10, e->opea); be32(p + 0x14, e->dst);
    be32(p + 0x18, e->dmax); be32(p + 0x1C, e->wbits);
    return p;
}

/* Destino intacto = nao houve escrita nenhuma (recusa limpa). */
static int dst_untouched(void)
{
    for (uint32_t i = 0; i < DMAX; i++)
        if (g_mem[DST_EA + i] != SENTINEL) return 0;
    return 1;
}

/* Cauda a partir de `from` ainda a SENTINEL = nao passou do limite dado. */
static int tail_clean(uint32_t from)
{
    for (uint32_t i = from; i < DMAX; i++)
        if (g_mem[DST_EA + i] != SENTINEL) return 0;
    return 1;
}

static int inflated_ok(void)
{
    return memcmp(g_mem + DST_EA, g_plain, LFQI_PLAIN_LEN) == 0
        && tail_clean(LFQI_PLAIN_LEN);
}

/* ------------------------------------------------------------------ */

static int g_fail;

static void check(int cond, const char* tag, const char* what)
{
    printf("  [%-10s] %-52s %s\n", tag, what, cond ? "OK" : "FAIL");
    if (!cond) g_fail++;
}

/* Corre o fulfiller com um campo trocado para little-endian e exige recusa:
 * se o decode nao fosse BE naquele offset, o elemento passaria. */
static void check_be_field(uint32_t off, const char* name)
{
    lfqi_elem e = elem_default();
    const uint8_t* p = arm(&e, LFQI_DEFLATE_LEN);
    uint32_t v = ((uint32_t)p[off] << 24) | ((uint32_t)p[off + 1] << 16)
               | ((uint32_t)p[off + 2] << 8) | p[off + 3];
    le32(g_mem + ELEM_EA + off, v);
    int rc = lfq_host_inflate_try(p);
    char msg[96];
    snprintf(msg, sizeof msg, "+0x%02X (%s) em LE -> recusado", off, name);
    check(rc == 0 && g_mem[OP_EA] == 0, "BE", msg);
}

/* Guard que tem de recusar sem tocar no destino. */
static void check_guard(lfqi_elem e, const char* what)
{
    const uint8_t* p = arm(&e, LFQI_DEFLATE_LEN);
    int rc = lfq_host_inflate_try(p);
    check(rc == 0 && dst_untouched() && g_mem[OP_EA] == 0, "GUARD", what);
}

int main(void)
{
    lfqi_gen_plain(g_plain, LFQI_PLAIN_LEN);

    /* 0. O gerador em C coincide com o do Python? Sem isto, "esperado" nao
     *    quer dizer nada. */
    check(lfqi_fnv1a(g_plain, LFQI_PLAIN_LEN) == LFQI_PLAIN_FNV1A,
          "FIXTURE", "plaintext do C == plaintext do Python (FNV-1a)");

    /* 1. Caminho feliz. */
    {
        lfqi_elem e = elem_default();
        const uint8_t* p = arm(&e, LFQI_DEFLATE_LEN);
        int rc = lfq_host_inflate_try(p);
        check(rc == 1, "INFLATE", "comando kind=0x0C aceite (rc==1)");
        check(inflated_ok(), "INFLATE", "dst == 8192 bytes de plaintext exactos");
        check(g_mem[OP_EA] == 1, "COMPLETE", "byte de status do op posto a 1");
    }

    /* 2. csize maior que o stream (blocos psarc vem com padding): o decoder
     *    para no BFINAL e o resto e' ignorado. */
    {
        lfqi_elem e = elem_default();
        e.csz = LFQI_DEFLATE_LEN + 64;
        const uint8_t* p = arm(&e, e.csz);
        int rc = lfq_host_inflate_try(p);
        check(rc == 1 && inflated_ok(), "PADDED", "csize com 64B de padding a seguir ao stream");
    }

    /* 3. Sem op EA: continua a cumprir, so nao ha onde marcar completion. */
    {
        lfqi_elem e = elem_default();
        e.opea = 0;
        const uint8_t* p = arm(&e, LFQI_DEFLATE_LEN);
        int rc = lfq_host_inflate_try(p);
        check(rc == 1 && inflated_ok(), "NOOP-EA", "opea==0 -> inflate na mesma, sem completion");
    }

    /* 4. Campos +0x04 e +0x1C nao sao consultados. */
    {
        lfqi_elem e = elem_default();
        e.req = 0x11111111u; e.wbits = 0x22222222u;
        const uint8_t* p = arm(&e, LFQI_DEFLATE_LEN);
        int rc = lfq_host_inflate_try(p);
        check(rc == 1 && inflated_ok(), "IGNORED", "+0x04/+0x1C com lixo nao alteram nada");
    }

    /* 5. Opcode errado: nem sequer olha para o resto. */
    {
        lfqi_elem e = elem_default();
        e.kind = 0x0Bu;
        const uint8_t* p = arm(&e, LFQI_DEFLATE_LEN);
        int rc = lfq_host_inflate_try(p);
        check(rc == 0 && dst_untouched() && g_mem[OP_EA] == 0,
              "KIND", "kind!=0x0C recusado sem tocar no destino");
    }

    /* 6. Decode big-endian, campo a campo. */
    check_be_field(0x00, "src");
    check_be_field(0x08, "csize");
    check_be_field(0x0C, "kind");
    check_be_field(0x14, "dst");
    check_be_field(0x18, "max out");
    {   /* opea em LE aponta para fora do commit: cumpre na mesma, mas o byte
         * de status REAL nao pode ser marcado. */
        lfqi_elem e = elem_default();
        const uint8_t* p = arm(&e, LFQI_DEFLATE_LEN);
        le32(g_mem + ELEM_EA + 0x10, OP_EA);
        int rc = lfq_host_inflate_try(p);
        check(rc == 1 && inflated_ok() && g_mem[OP_EA] == 0,
              "BE", "+0x10 (op EA) em LE -> completion nao marcada");
    }

    /* 7. csize que corta mesmo o stream: o inflate falha, e uma falha NAO pode
     *    marcar completion -- senao o waiter sincrono do render thread segue em
     *    frente com um buffer meio escrito.
     *    Medido nesta fixture: 402 dos 403 bytes ainda inflacionam na mesma (o
     *    ultimo byte so leva bits de padding a seguir ao end-of-block), 401 ja
     *    da -1. Logo LEN-2 e' o corte mais apertado que discrimina; LEN-1 nao
     *    testaria nada. */
    {
        lfqi_elem e = elem_default();
        e.csz = LFQI_DEFLATE_LEN - 2;
        const uint8_t* p = arm(&e, LFQI_DEFLATE_LEN);
        int rc = lfq_host_inflate_try(p);
        check(rc == 0 && g_mem[OP_EA] == 0, "TRUNC", "csize que corta o stream -> rc=0 e sem completion");
    }

    /* 8. Stream corrompido (BTYPE=3 e' reservado/invalido). */
    {
        lfqi_elem e = elem_default();
        const uint8_t* p = arm(&e, LFQI_DEFLATE_LEN);
        g_mem[SRC_EA] = 0x06;
        int rc = lfq_host_inflate_try(p);
        check(rc == 0 && g_mem[OP_EA] == 0, "CORRUPT", "BTYPE invalido -> rc=0 e sem completion");
    }

    /* 9. dmax pequeno demais: recusa E nao escreve um byte a seguir ao limite. */
    {
        lfqi_elem e = elem_default();
        e.dmax = LFQI_PLAIN_LEN - 1;
        const uint8_t* p = arm(&e, LFQI_DEFLATE_LEN);
        int rc = lfq_host_inflate_try(p);
        check(rc == 0 && g_mem[OP_EA] == 0, "SMALLDST", "dmax < saida -> rc=0 e sem completion");
        check(tail_clean(LFQI_PLAIN_LEN - 1), "SMALLDST", "nada escrito para la de dst+dmax");
    }

    /* 10. Guards de sanidade sobre EAs/tamanhos vindos de um elemento com lixo. */
    {
        lfqi_elem e;
        e = elem_default(); e.src  = 0;          check_guard(e, "src EA zero");
        e = elem_default(); e.dst  = 0;          check_guard(e, "dst EA zero");
        e = elem_default(); e.csz  = 0;          check_guard(e, "csize zero");
        e = elem_default(); e.dmax = 0;          check_guard(e, "max out zero");
        e = elem_default(); e.csz  = 0x20001u;   check_guard(e, "csize > 128K");
        e = elem_default(); e.dmax = 0x100001u;  check_guard(e, "max out > 1M");
        e = elem_default(); e.src  = 0xFFF00000u; check_guard(e, "src EA no topo do espaco");
        e = elem_default(); e.dst  = 0xFFF00000u; check_guard(e, "dst EA no topo do espaco");
    }

    /* 11. Paginas nao commitadas: o guard commit-aware tem de cortar ANTES do
     *     deref (e' o que impede o SIGBUS/AV com elementos de lixo). */
    {
        lfqi_elem e = elem_default();
        const uint8_t* p = arm(&e, LFQI_DEFLATE_LEN);
        g_ea_readable = 0;
        int rc = lfq_host_inflate_try(p);
        g_ea_readable = 1;
        check(rc == 0 && dst_untouched() && g_mem[OP_EA] == 0,
              "UNCOMMIT", "EA nao legivel -> recusado sem deref");
    }

    if (g_fail == 0)
        printf("  PASS: fulfiller host de inflate decodifica BE, inflaciona e completa.\n");
    else
        printf("  FAIL: %d verificacao(oes) do fulfiller host de inflate falharam.\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
