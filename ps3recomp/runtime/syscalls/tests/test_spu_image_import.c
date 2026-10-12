/* test_spu_image_import.c -- teste offline do `sys_spu_image_import`
 * (bloco LV2S do inventario docs/UPSTREAM_DELTA.tsv, sha upstream dd936c4).
 *
 * NIVEL DE EVIDENCIA: offline-unit. `grep -r image_import games/gow2/` da'
 * ZERO, logo NAO existe aceite in-boot para este caminho e nenhum e'
 * reclamado aqui. Este ficheiro prova uma coisa e so' uma: que o handler
 * REAL escreve o EA de origem do segmento COPY como u32 em `+0x10`.
 *
 * PORQUE UM #include DE UMA UNIDADE DE TRADUCAO INTEIRA:
 * `sys_spu_image_import_handler` e' `static` dentro de
 * runtime/syscalls/lv2_register.c. A unica forma de correr o CAMINHO REAL
 * (e nao uma copia da logica, o que nao provaria nada sobre a producao) e'
 * incluir a unidade de traducao. O caminho e' indirecto -- pela macro
 * LV2_REGISTER_C -- para o script de mutacao
 * (runtime/syscalls/tests/run_spu_image_import_mutation.sh) poder
 * redireccionar o MESMO ficheiro de teste para uma COPIA mutada fora da
 * arvore, sem tocar na producao.
 *
 * PORQUE NAO SO' assert():
 * um configure `-DCMAKE_BUILD_TYPE=Release` injecta -DNDEBUG, que desliga
 * assert() da libc -- ja' foi provado por injeccao neste repositorio
 * (ver o comentario do alvo test_autoload_map em CMakeLists.txt). Aqui cada
 * verificacao tem caminho de falha explicito: imprime esperado/lido em hex,
 * conta a falha, e o main() devolve 1 se o contador nao for zero.
 */

#ifndef LV2_REGISTER_C
#define LV2_REGISTER_C "../lv2_register.c"
#endif

#include LV2_REGISTER_C

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------------------
 * Superficie de link.
 *
 * `vm_base` e' a memoria do guest (o lv2_register.c so' a declara `extern`).
 * Os stubs abaixo sao EXACTAMENTE os simbolos que o lv2_register.c referencia
 * e que vivem noutras unidades de traducao do runtime; linkar a biblioteca
 * inteira nao e' opcao (daria simbolos duplicados, ja' que este ficheiro
 * INCLUI o lv2_register.c). Nenhum deles e' exercitado pelo caminho do
 * image_import -- so' precisam de existir. Os prototipos reais ja' estao em
 * contexto pelos headers que o lv2_register.c inclui, portanto o compilador
 * verifica as assinaturas destas definicoes contra os originais.
 * -----------------------------------------------------------------------*/
uint8_t* vm_base = NULL;

void sys_ppu_thread_init(lv2_syscall_table* tbl) { (void)tbl; }
void sys_mutex_init     (lv2_syscall_table* tbl) { (void)tbl; }
void sys_cond_init      (lv2_syscall_table* tbl) { (void)tbl; }
void sys_semaphore_init (lv2_syscall_table* tbl) { (void)tbl; }
void sys_rwlock_init    (lv2_syscall_table* tbl) { (void)tbl; }
void sys_timer_init     (lv2_syscall_table* tbl) { (void)tbl; }
void sys_event_init     (lv2_syscall_table* tbl) { (void)tbl; }
void sys_memory_init    (lv2_syscall_table* tbl) { (void)tbl; }
void sys_vm_init        (lv2_syscall_table* tbl) { (void)tbl; }
void sys_fs_init        (lv2_syscall_table* tbl) { (void)tbl; }

void sys_fs_translate_path(const char* ps3_path, char* host_path, int host_path_size)
{
    (void)ps3_path;
    if (host_path && host_path_size > 0) host_path[0] = '\0';
}

spu_ppu_fallback_fn spu_lookup_ppu_fallback(uint32_t entry_point, void** out_user)
{
    (void)entry_point;
    if (out_user) *out_user = NULL;
    return NULL;
}

int sys_event_queue_push_by_id(uint32_t queue_id, uint64_t source, uint64_t data1,
                               uint64_t data2, uint64_t data3)
{
    (void)queue_id; (void)source; (void)data1; (void)data2; (void)data3;
    return -1;
}

/* ---------------------------------------------------------------------------
 * Infraestrutura do teste
 * -----------------------------------------------------------------------*/

#define VM_SIZE   (256u * 1024u * 1024u)   /* 0x10000000: cobre o bump de segmentos em 0x0D000000 */
#define SRC_EA    0x00100000u
#define IMG_EA    0x00200000u
#define ENTRY_VAL 0x00000340u

static int g_falhas = 0;

/* Leitores independentes: NAO reutilizam os helpers do ficheiro sob teste,
 * para a verificacao nao ser validada pelo proprio codigo que mede. */
static uint32_t rd32(uint32_t ea)
{
    const uint8_t* p = vm_base + ea;
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] <<  8) | (uint32_t)p[3];
}
static void wr32(uint32_t ea, uint32_t v)
{
    uint8_t* p = vm_base + ea;
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >>  8); p[3] = (uint8_t)v;
}
static void wr16(uint32_t ea, uint16_t v)
{
    uint8_t* p = vm_base + ea;
    p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v;
}

static void check_eq32(int linha, const char* caso, const char* campo,
                       uint32_t esperado, uint32_t lido)
{
    if (esperado != lido) {
        fprintf(stdout,
                "FAIL %s:%d [%s] %s: esperado=0x%08X lido=0x%08X\n",
                __FILE__, linha, caso, campo, esperado, lido);
        g_falhas++;
    } else {
        fprintf(stdout, "  ok [%s] %s = 0x%08X\n", caso, campo, lido);
    }
}
static void check_eq_i64(int linha, const char* caso, const char* campo,
                         int64_t esperado, int64_t lido)
{
    if (esperado != lido) {
        fprintf(stdout,
                "FAIL %s:%d [%s] %s: esperado=%lld lido=%lld\n",
                __FILE__, linha, caso, campo, (long long)esperado, (long long)lido);
        g_falhas++;
    } else {
        fprintf(stdout, "  ok [%s] %s = %lld\n", caso, campo, (long long)lido);
    }
}
#define CHECK32(caso, campo, esp, lido)  check_eq32(__LINE__, (caso), (campo), (esp), (lido))
#define CHECKRC(caso, esp, lido)         check_eq_i64(__LINE__, (caso), "rc", (esp), (lido))

/* Zera a memoria do guest entre casos: o bump allocator de segmentos do
 * handler (`s_spu_seg_bump`) e' `static` e AVANCA entre chamadas, por isso o
 * teste nunca assume 0x0D000000 -- le sempre o ponteiro de segmentos do
 * proprio cabecalho de imagem (img + 0x08). */
static void reset_vm(void) { memset(vm_base, 0, VM_SIZE); }

/* Escreve um ELF32 big-endian de SPU minimo em SRC_EA com `nph` program
 * headers. Devolve o EA do primeiro program header. */
static uint32_t build_elf(uint32_t nph)
{
    const uint32_t phoff   = 0x40;
    const uint16_t phentsz = 0x20;
    uint8_t* e = vm_base + SRC_EA;
    e[0] = 0x7F; e[1] = 'E'; e[2] = 'L'; e[3] = 'F';
    e[4] = 1;    /* ELFCLASS32 */
    e[5] = 2;    /* ELFDATA2MSB */
    e[6] = 1;    /* EV_CURRENT  */
    wr32(SRC_EA + 0x18, ENTRY_VAL);   /* e_entry     */
    wr32(SRC_EA + 0x1C, phoff);       /* e_phoff     */
    wr16(SRC_EA + 0x2A, phentsz);     /* e_phentsize */
    wr16(SRC_EA + 0x2C, (uint16_t)nph); /* e_phnum   */
    return SRC_EA + phoff;
}

static void build_phdr(uint32_t ph_ea, uint32_t p_type, uint32_t p_off,
                       uint32_t p_vaddr, uint32_t p_filesz, uint32_t p_memsz)
{
    wr32(ph_ea + 0x00, p_type);
    wr32(ph_ea + 0x04, p_off);
    wr32(ph_ea + 0x08, p_vaddr);
    wr32(ph_ea + 0x0C, 0);          /* p_paddr */
    wr32(ph_ea + 0x10, p_filesz);
    wr32(ph_ea + 0x14, p_memsz);
}

static int64_t run_import(uint32_t img_ea, uint32_t src_ea)
{
    ppu_context ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.gpr[3] = img_ea;
    ctx.gpr[4] = src_ea;
    ctx.gpr[5] = 0;                 /* SYS_SPU_IMAGE_PROTECT */
    return sys_spu_image_import_handler(&ctx);
}

/* ---------------------------------------------------------------------------
 * Casos
 * -----------------------------------------------------------------------*/

/* Caso 1 -- p_offset = 0: read32(seg + 0x10) == src_ea.
 * E' a formulacao literal do aceite do requisito LV2S-01. */
static void caso1_offset_zero(void)
{
    reset_vm();
    uint32_t ph = build_elf(1);
    build_phdr(ph, 1 /*PT_LOAD*/, 0x0, 0x0000, 0x200, 0x200);

    int64_t rc = run_import(IMG_EA, SRC_EA);
    CHECKRC("caso1/offset=0", 0, rc);

    uint32_t segs = rd32(IMG_EA + 0x08);
    CHECK32("caso1/offset=0", "nsegs", 1, rd32(IMG_EA + 0x0C));
    CHECK32("caso1/offset=0", "seg+0x00 (type COPY)", 1, rd32(segs + 0x00));
    CHECK32("caso1/offset=0", "seg+0x04 (ls_start)",  0x0000, rd32(segs + 0x04));
    CHECK32("caso1/offset=0", "seg+0x08 (size)",      0x200,  rd32(segs + 0x08));
    CHECK32("caso1/offset=0", "seg+0x10 (src u32)",   SRC_EA, rd32(segs + 0x10));
}

/* Caso 2 -- p_offset = 0x1000: o campo e' o EA de ORIGEM DO SEGMENTO, nao o
 * do ficheiro, portanto read32(seg + 0x10) == src_ea + 0x1000. */
static void caso2_offset_nao_zero(void)
{
    reset_vm();
    uint32_t ph = build_elf(1);
    build_phdr(ph, 1 /*PT_LOAD*/, 0x1000, 0x3000, 0x400, 0x400);

    int64_t rc = run_import(IMG_EA, SRC_EA);
    CHECKRC("caso2/offset=0x1000", 0, rc);

    uint32_t segs = rd32(IMG_EA + 0x08);
    CHECK32("caso2/offset=0x1000", "seg+0x04 (ls_start)", 0x3000, rd32(segs + 0x04));
    CHECK32("caso2/offset=0x1000", "seg+0x08 (size)",     0x400,  rd32(segs + 0x08));
    CHECK32("caso2/offset=0x1000", "seg+0x10 (src u32)",  SRC_EA + 0x1000, rd32(segs + 0x10));
    /* +0x14 mantem-se como espelho (u64-lo) por compatibilidade: e' o que o
     * upstream faz em dd936c4 e e' inofensivo para quem leia o par como u64. */
    CHECK32("caso2/offset=0x1000", "seg+0x14 (espelho)",  SRC_EA + 0x1000, rd32(segs + 0x14));
}

/* Caso 3 -- p_memsz > p_filesz cria DOIS segmentos: o segundo e' FILL, e em
 * FILL o `+0x10` e' o campo `value`, cujo valor CORRECTO continua a ser 0.
 * Este caso existe para apanhar uma substituicao descuidada que altere os
 * dois ramos de uma vez. */
static void caso3_fill_tail(void)
{
    reset_vm();
    uint32_t ph = build_elf(1);
    build_phdr(ph, 1 /*PT_LOAD*/, 0x800, 0x1000, 0x100, 0x180);

    int64_t rc = run_import(IMG_EA, SRC_EA);
    CHECKRC("caso3/fill", 0, rc);

    uint32_t segs = rd32(IMG_EA + 0x08);
    CHECK32("caso3/fill", "nsegs", 2, rd32(IMG_EA + 0x0C));

    /* segmento 0: COPY */
    CHECK32("caso3/fill", "seg0+0x00 (type COPY)", 1, rd32(segs + 0x00));
    CHECK32("caso3/fill", "seg0+0x10 (src u32)", SRC_EA + 0x800, rd32(segs + 0x10));

    /* segmento 1: FILL -- +0x10 e' `value`, e 0 e' o valor correcto */
    uint32_t seg1 = segs + 0x18;
    CHECK32("caso3/fill", "seg1+0x00 (type FILL)", 2, rd32(seg1 + 0x00));
    CHECK32("caso3/fill", "seg1+0x04 (ls_start)",  0x1000 + 0x100, rd32(seg1 + 0x04));
    CHECK32("caso3/fill", "seg1+0x08 (size)",      0x180 - 0x100,  rd32(seg1 + 0x08));
    CHECK32("caso3/fill", "seg1+0x10 (value)",     0, rd32(seg1 + 0x10));
}

/* Caso 4 -- cabecalho da imagem (sys_spu_image, 16 bytes). */
static void caso4_cabecalho_imagem(void)
{
    reset_vm();
    uint32_t ph = build_elf(2);
    build_phdr(ph,        1 /*PT_LOAD*/, 0x0,   0x0000, 0x100, 0x100);
    build_phdr(ph + 0x20, 1 /*PT_LOAD*/, 0x100, 0x1000, 0x080, 0x080);

    int64_t rc = run_import(IMG_EA, SRC_EA);
    CHECKRC("caso4/cabecalho", 0, rc);

    CHECK32("caso4/cabecalho", "img+0x00 (type USER)", 0, rd32(IMG_EA + 0x00));
    CHECK32("caso4/cabecalho", "img+0x04 (entry)", ENTRY_VAL, rd32(IMG_EA + 0x04));
    CHECK32("caso4/cabecalho", "img+0x0C (nsegs)", 2, rd32(IMG_EA + 0x0C));

    uint32_t segs = rd32(IMG_EA + 0x08);
    if (segs == 0) {
        fprintf(stdout, "FAIL %s:%d [caso4/cabecalho] img+0x08 (segs): "
                        "esperado != 0x00000000, lido=0x00000000\n", __FILE__, __LINE__);
        g_falhas++;
    } else {
        fprintf(stdout, "  ok [caso4/cabecalho] img+0x08 (segs) = 0x%08X (nao-nulo)\n", segs);
    }

    CHECK32("caso4/cabecalho", "seg0+0x10 (src u32)", SRC_EA + 0x000, rd32(segs + 0x10));
    CHECK32("caso4/cabecalho", "seg1+0x10 (src u32)", SRC_EA + 0x100, rd32(segs + 0x18 + 0x10));
}

/* Caso 5 -- controlo negativo: `src` sem magic ELF devolve -8 (ENOEXEC) e os
 * 16 bytes da imagem ficam a zero. Existe para o teste nao poder passar so'
 * porque o handler nunca correu: a imagem e' pre-enchida com 0xAA. */
static void caso5_controlo_negativo(void)
{
    reset_vm();
    memset(vm_base + IMG_EA, 0xAA, 16);
    memset(vm_base + SRC_EA, 0x00, 64);      /* sem magic ELF */

    int64_t rc = run_import(IMG_EA, SRC_EA);
    CHECKRC("caso5/nao-elf", -8, rc);
    CHECK32("caso5/nao-elf", "img+0x00", 0, rd32(IMG_EA + 0x00));
    CHECK32("caso5/nao-elf", "img+0x04", 0, rd32(IMG_EA + 0x04));
    CHECK32("caso5/nao-elf", "img+0x08", 0, rd32(IMG_EA + 0x08));
    CHECK32("caso5/nao-elf", "img+0x0C", 0, rd32(IMG_EA + 0x0C));
}

int main(void)
{
    vm_base = (uint8_t*)calloc(VM_SIZE, 1);
    if (!vm_base) {
        fprintf(stdout, "test_spu_image_import: FAIL (calloc de %u bytes falhou)\n", VM_SIZE);
        return 1;
    }

    caso1_offset_zero();
    caso2_offset_nao_zero();
    caso3_fill_tail();
    caso4_cabecalho_imagem();
    caso5_controlo_negativo();

    free(vm_base);
    vm_base = NULL;

    if (g_falhas != 0) {
        fprintf(stdout, "test_spu_image_import: FAIL (%d verificacoes)\n", g_falhas);
        return 1;
    }
    fprintf(stdout, "test_spu_image_import: PASS\n");
    return 0;
}
