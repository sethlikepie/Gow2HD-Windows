/*
 * ps3recomp - Virtual memory manager
 *
 * Manages the host-side mapping of the PS3's 32-bit address space.
 * On Windows uses VirtualAlloc; on POSIX uses mmap.
 *
 * PS3 memory map (simplified):
 *   0x00010000 - 0x0FFFFFFF : User-space (main memory, 256 MB)
 *   0x10000000 - 0x1FFFFFFF : RSX mapped memory
 *   0x30000000 - 0x3FFFFFFF : SPU local store windows (raw SPU mapping)
 *   0xD0000000 - 0xDFFFFFFF : Stack area
 */

#ifndef VM_H
#define VM_H

#include "../../include/ps3emu/ps3types.h"
#include "../../include/ps3emu/error_codes.h"
#include "../../include/ps3_checkpoint.h" /* Task 2 (bringup-instrumentacao-fase0): VM_READY */

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
#else
  #include <sys/mman.h>
  #include <signal.h>
  #include <unistd.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * Address space constants
 * -----------------------------------------------------------------------*/
#define VM_TOTAL_SIZE       0x100000000ull   /* 4 GB virtual address space */
#define VM_MAIN_MEM_BASE    0x00010000u
#define VM_MAIN_MEM_SIZE    0x10000000u      /* 256 MB main memory */
#define VM_RSX_BASE         0x10000000u
#define VM_RSX_SIZE         0x10000000u      /* 256 MB RSX */
#define VM_SPU_BASE         0x30000000u
#define VM_SPU_WINDOW_SIZE  0x00040000u      /* 256 KB per SPU LS */
#define VM_STACK_BASE       0xD0000000u
#define VM_STACK_REGION     0x10000000u      /* 256 MB stack region */

#define VM_PPU_STACK_SIZE   0x00100000u      /* 1 MB default PPU stack */
#define VM_PAGE_SIZE        0x00001000u      /* 4 KB: the GUEST page size */

/* The HOST page size, which is what mprotect actually works in (port of upstream
 * d77930aa). Apple Silicon uses 16 KB: an mprotect on a 4 KB-aligned address that
 * is not 16 KB-aligned fails with EINVAL, and vm_commit then reported ENOMEM for a
 * perfectly valid guest range. Guest-visible layout stays 4 KB granular; only the
 * host protection calls are widened to the host pages that cover the range. */
static inline uint32_t vm_host_page_size(void)
{
#ifdef _WIN32
    return VM_PAGE_SIZE;
#else
    static uint32_t s_page = 0;
    if (!s_page) {
        long v = sysconf(_SC_PAGESIZE);
        s_page = (v > 0) ? (uint32_t)v : VM_PAGE_SIZE;
    }
    return s_page;
#endif
}

/* Fase 1: regioes que existiam no runtime mas nao no mapa, por isso
 * vm_region_name() devolvia "unmapped" para a heap real do jogo -- e o campo
 * region= do MEMORY/OOB da Fase 0 foi medido atraves dessa lente.
 * vm.h e' a fonte unica: quem duplicava estes valores ganha um _Static_assert.
 * So' entram intervalos confirmados por codigo (ver o plano da Fase 1). */
#define VM_LV2_HEAP_BASE    0x40000000u   /* == SYS_MEM_ALLOC_BASE  */
#define VM_LV2_HEAP_END     0x60000000u   /* == SYS_MEM_ALLOC_END   */
#define VM_SYS_VM_BASE      0x60000000u   /* == SYS_VM_REGION_BASE  */
#define VM_SYS_VM_END       0x70000000u   /* == SYS_VM_REGION_END   */
#define VM_RSX_LOCAL_BASE   0xC0000000u   /* RSX local real         */
/* Fase 1, correcao pos-review (Round 1): o END original (0xC2000000, 32 MB)
 * vinha de uma citacao errada -- ppu_loader.cpp:1892 e' so' um corte de scan
 * heuristico truncado (gated PS3_TRACE_MC), nao a definicao da regiao. A
 * fonte autoritativa e' cellGcmSys.c:295-296 (s_config.localAddress =
 * 0xC0000000; s_config.localSize = 256*1024*1024) e boot_main.cpp:453, que
 * comita exactamente esses 256 MB. 0xC0000000+256MB = 0xD0000000, que encosta
 * exactamente em VM_STACK_BASE sem buraco. */
#define VM_RSX_LOCAL_END    0xD0000000u
/* s_config.localSize e' atribuido em runtime (nao e' um #define), por isso
 * nao ha macro do outro lado para um _Static_assert cruzado directo como os
 * das outras tres regioes. O assert possivel e' de auto-consistencia: o
 * TAMANHO aqui declarado tem de ser 256 MB, o mesmo numero citado em
 * cellGcmSys.c:295-296 / boot_main.cpp:453 -- nao prova ausencia de drift
 * nessas duas fontes, mas fixa a intencao e obriga a olhar para la se algum
 * dia BASE/END mudarem sem preservar os 256 MB. */
#if defined(__cplusplus)
static_assert(VM_RSX_LOCAL_END - VM_RSX_LOCAL_BASE == 256u * 1024u * 1024u,
              "RSX local: tamanho tem de ser 256 MB (cellGcmSys.c:295-296 / boot_main.cpp:453, Fase 1)");
#else
_Static_assert(VM_RSX_LOCAL_END - VM_RSX_LOCAL_BASE == 256u * 1024u * 1024u,
               "RSX local: tamanho tem de ser 256 MB (cellGcmSys.c:295-296 / boot_main.cpp:453, Fase 1)");
#endif
#define VM_TLS_IMG_BASE     0x10F00000u   /* == PPU_TLS_IMG         */
#define VM_TLS_IMG_WINDOW   0x00010000u   /* == PPU_TLS_WINDOW      */

/* Align a value up to `align` (must be power of 2) */
#define VM_ALIGN_UP(val, align) (((val) + (align) - 1) & ~((align) - 1))

/* ---------------------------------------------------------------------------
 * Global base pointer
 *
 * All PS3 guest address translation goes through this:
 *   host_ptr = vm_base + guest_addr
 * -----------------------------------------------------------------------*/
extern uint8_t* vm_base;

#ifndef _WIN32
/* mprotect over the host pages covering [addr, addr+size) (upstream d77930aa). */
static inline int vm__mprotect_pages(uint32_t addr, uint32_t size, int prot)
{
    uint64_t pg = vm_host_page_size();
    uint64_t a0 = (uint64_t)addr & ~(pg - 1u);
    uint64_t a1 = ((uint64_t)addr + size + pg - 1u) & ~(pg - 1u);
    return mprotect(vm_base + a0, (size_t)(a1 - a0), prot);
}
#endif

/* ---------------------------------------------------------------------------
 * Initialization / Shutdown
 * -----------------------------------------------------------------------*/

/*
 * Reserve the host address range and commit the main memory region.
 * Returns CELL_OK on success, CELL_ENOMEM on failure.
 */
static inline int32_t vm_init(void)
{
    if (vm_base != NULL) return CELL_OK; /* already initialized */

#ifdef _WIN32
    /*
     * Reserve a contiguous 4 GB region.  We only commit pages as needed.
     * MEM_RESERVE just reserves address space without backing pages.
     */
    vm_base = (uint8_t*)VirtualAlloc(NULL, (SIZE_T)VM_TOTAL_SIZE,
                                      MEM_RESERVE, PAGE_NOACCESS);
    if (!vm_base) return CELL_ENOMEM;

    /* Commit main memory region (256 MB, read/write) */
    if (!VirtualAlloc(vm_base + VM_MAIN_MEM_BASE, VM_MAIN_MEM_SIZE,
                      MEM_COMMIT, PAGE_READWRITE)) {
        VirtualFree(vm_base, 0, MEM_RELEASE);
        vm_base = NULL;
        return CELL_ENOMEM;
    }

    /* Commit stack region */
    if (!VirtualAlloc(vm_base + VM_STACK_BASE, VM_STACK_REGION,
                      MEM_COMMIT, PAGE_READWRITE)) {
        VirtualFree(vm_base, 0, MEM_RELEASE);
        vm_base = NULL;
        return CELL_ENOMEM;
    }

#else /* POSIX */
    vm_base = (uint8_t*)mmap(NULL, (size_t)VM_TOTAL_SIZE,
                              PROT_NONE,
                              MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE,
                              -1, 0);
    if (vm_base == MAP_FAILED) {
        vm_base = NULL;
        return CELL_ENOMEM;
    }

    /* Make main memory readable/writable */
    if (mprotect(vm_base + VM_MAIN_MEM_BASE, VM_MAIN_MEM_SIZE,
                 PROT_READ | PROT_WRITE) != 0) {
        munmap(vm_base, (size_t)VM_TOTAL_SIZE);
        vm_base = NULL;
        return CELL_ENOMEM;
    }

    /* Make stack region readable/writable */
    if (mprotect(vm_base + VM_STACK_BASE, VM_STACK_REGION,
                 PROT_READ | PROT_WRITE) != 0) {
        munmap(vm_base, (size_t)VM_TOTAL_SIZE);
        vm_base = NULL;
        return CELL_ENOMEM;
    }
#endif

    /* Zero main memory */
    memset(vm_base + VM_MAIN_MEM_BASE, 0, VM_MAIN_MEM_SIZE);

    /* Task 2 (bringup-instrumentacao-fase0): VM_READY -- memoria guest
     * mapeada/comprometida. vm_init() e' o unico sitio, game-agnostic, onde
     * isto acontece (chamado por boot_macos.cpp ANTES de ppu_load_elf), por
     * isso o checkpoint fica aqui em vez de no harness de boot per-jogo
     * (fora deste repo). */
    ps3_checkpoint_fire(PS3_CP_VM_READY, "vm_init",
                       "main=0x%08X..0x%08X stack=0x%08X..0x%08X",
                       VM_MAIN_MEM_BASE, VM_MAIN_MEM_BASE + VM_MAIN_MEM_SIZE,
                       VM_STACK_BASE, VM_STACK_BASE + VM_STACK_REGION);

    return CELL_OK;
}

static inline void vm_shutdown(void)
{
    if (!vm_base) return;

#ifdef _WIN32
    VirtualFree(vm_base, 0, MEM_RELEASE);
#else
    munmap(vm_base, (size_t)VM_TOTAL_SIZE);
#endif

    vm_base = NULL;
}

/* ---------------------------------------------------------------------------
 * Memory region commit / protect
 * -----------------------------------------------------------------------*/

static inline int32_t vm_commit(uint32_t addr, uint32_t size)
{
    if (!vm_base) return CELL_EFAULT;
    size = VM_ALIGN_UP(size, VM_PAGE_SIZE);

#ifdef _WIN32
    if (!VirtualAlloc(vm_base + addr, size, MEM_COMMIT, PAGE_READWRITE))
        return CELL_ENOMEM;
#else
    if (vm__mprotect_pages(addr, size, PROT_READ | PROT_WRITE) != 0)
        return CELL_ENOMEM;
#endif

    return CELL_OK;
}

static inline int32_t vm_protect(uint32_t addr, uint32_t size, int read, int write, int exec)
{
    if (!vm_base) return CELL_EFAULT;
    size = VM_ALIGN_UP(size, VM_PAGE_SIZE);

#ifdef _WIN32
    DWORD prot = PAGE_NOACCESS;
    if (read && write && exec) prot = PAGE_EXECUTE_READWRITE;
    else if (read && write)    prot = PAGE_READWRITE;
    else if (read && exec)     prot = PAGE_EXECUTE_READ;
    else if (read)             prot = PAGE_READONLY;
    else if (exec)             prot = PAGE_EXECUTE;

    DWORD old;
    if (!VirtualProtect(vm_base + addr, size, prot, &old))
        return CELL_EFAULT;
#else
    int prot = PROT_NONE;
    if (read)  prot |= PROT_READ;
    if (write) prot |= PROT_WRITE;
    if (exec)  prot |= PROT_EXEC;

    if (vm__mprotect_pages(addr, size, prot) != 0)
        return CELL_EFAULT;
#endif

    return CELL_OK;
}

/* ---------------------------------------------------------------------------
 * Stack allocation for PPU threads
 *
 * Simple bump allocator within the stack region.  Each thread gets
 * a fixed-size stack with a guard page at the bottom.
 * -----------------------------------------------------------------------*/
typedef struct vm_stack_alloc {
    uint32_t next_addr;     /* next stack base to allocate */
    uint32_t region_end;    /* end of the stack region */
} vm_stack_alloc;

static inline void vm_stack_alloc_init(vm_stack_alloc* sa)
{
    sa->next_addr  = VM_STACK_BASE;
    sa->region_end = VM_STACK_BASE + VM_STACK_REGION;
}

/*
 * Allocate a stack.  Returns the BASE address of the stack region (lowest address).
 * The usable stack top = base + size.
 * Returns 0 on failure.
 */
static inline uint32_t vm_stack_allocate(vm_stack_alloc* sa, uint32_t stack_size)
{
    stack_size = VM_ALIGN_UP(stack_size, VM_PAGE_SIZE);

    /* Reserve a leading page as a nominal guard slot (kept out of the usable
     * stack so the layout/addresses below are unchanged), but do NOT actually
     * mark it PAGE_NOACCESS (2026-07-01). vm_stack_allocate only tracks a bump
     * pointer into a region boot_main.cpp already committed PAGE_READWRITE as
     * one block; VirtualProtect-ing a real guard page here punches an
     * untracked hole into that block that ppu_loader.cpp's committed-range
     * bookkeeping has no visibility into, so a stack access that strays onto
     * it (off-by-a-little pointer math, or a genuine but small guest stack
     * overflow) takes a real, uncaught host access violation and kills the
     * whole process -- confirmed via a page-granularity self-test that every
     * page of the region is genuinely backed right after commit, and via nm
     * mapping the crash RIP into vm_read32 itself (called through
     * ps3_indirect_call -> ps3_import_thunk -> ps3_hle_call), reproducing at
     * GUEST=0xD0009000/0xD0022000/... -- exactly the address class a stray
     * read into one of these guard pages produces. A real PS3 doesn't hard
     * fault the whole console on a guest stack overflow; consistent with
     * every other invalid-access case this session (pool exhaustion, oob,
     * uncommitted pages), degrade instead of crash: leave the slot
     * read/write like the rest of the band. */
    uint32_t total = stack_size + VM_PAGE_SIZE;

    if (sa->next_addr + total > sa->region_end)
        return 0; /* out of stack space */

    uint32_t base = sa->next_addr;
    sa->next_addr += total;

    return base + VM_PAGE_SIZE; /* skip the (nominal, non-trapping) guard page */
}

/* ---------------------------------------------------------------------------
 * SPU local store allocation
 *
 * Each SPU gets a 256 KB window in the VM_SPU_BASE range, used for
 * raw SPU memory-mapped access from the PPU.
 * -----------------------------------------------------------------------*/

static inline int32_t vm_spu_ls_map(uint32_t spu_index)
{
    if (spu_index >= 8) return CELL_EINVAL; /* max 8 SPUs */

    uint32_t addr = VM_SPU_BASE + spu_index * VM_SPU_WINDOW_SIZE;
    return vm_commit(addr, VM_SPU_WINDOW_SIZE);
}

static inline uint32_t vm_spu_ls_addr(uint32_t spu_index)
{
    return VM_SPU_BASE + spu_index * VM_SPU_WINDOW_SIZE;
}

static inline uint8_t* vm_spu_ls_host_ptr(uint32_t spu_index)
{
    return vm_base + VM_SPU_BASE + spu_index * VM_SPU_WINDOW_SIZE;
}

/* ---------------------------------------------------------------------------
 * Address translation helpers
 * -----------------------------------------------------------------------*/

static inline void* vm_to_host(uint32_t addr)
{
    return (void*)(vm_base + addr);
}

static inline uint32_t vm_to_guest(const void* host_ptr)
{
    return (uint32_t)((const uint8_t*)host_ptr - vm_base);
}

static inline int vm_is_valid_addr(uint32_t addr)
{
    /* Check if address falls within a committed region */
    if (addr >= VM_MAIN_MEM_BASE && addr < VM_MAIN_MEM_BASE + VM_MAIN_MEM_SIZE)
        return 1;
    if (addr >= VM_STACK_BASE && addr < VM_STACK_BASE + VM_STACK_REGION)
        return 1;
    if (addr >= VM_SPU_BASE && addr < VM_SPU_BASE + 8 * VM_SPU_WINDOW_SIZE)
        return 1;
    if (addr >= VM_RSX_BASE && addr < VM_RSX_BASE + VM_RSX_SIZE)
        return 1;
    return 0;
}

/*
 * vm_region_name -- nome da regiao guest que deveria conter `addr` (Task 3,
 * bringup-instrumentacao-fase0, subsistema MEMORY: campo region= do evento
 * canonico MEMORY/OOB em runtime/ppu/ppu_loader.cpp). Nao existe um mapa de
 * regioes mais rico consultavel no runtime (nao ha um `struct` de regioes com
 * nome/permissoes por entrada).
 *
 * Fase 1 (fase1-desmascarar-memoria, Task 4): ate' aqui esta funcao verificava
 * os MESMOS quatro ranges de vm_is_valid_addr() (main, stack, spu, rsx), na
 * mesma ordem -- por isso a heap real do jogo (lv2_heap/sys_vm/rsx_local) e a
 * imagem TLS liam sempre "unmapped" ou eram mal-atribuidas a "rsx". Corrigido
 * acrescentando tls/lv2_heap/sys_vm/rsx_local (ver DEFINEs acima) -- esta
 * funcao agora verifica OITO regioes (tls, main, stack, spu, lv2_heap,
 * sys_vm, rsx_local, rsx), vm_is_valid_addr() continua a verificar QUATRO.
 * Isto e' divergencia INTENCIONAL, nao paridade perdida por acidente:
 * vm_region_name() so' rotula um EA para o log MEMORY/OOB (nunca decide se o
 * acesso e' permitido); vm_is_valid_addr() e' quem gate-keeps. Rotular um EA
 * como "lv2_heap" em vez de "unmapped" nao muda se esse EA e' tratado como
 * valido -- so' torna o log mais fiel. Um mantenedor que "repuser a paridade"
 * apagando tls/lv2_heap/sys_vm/rsx_local daqui reabre o bug que esta task
 * corrigiu (a heap real volta a reportar "unmapped").
 *
 * Ordem ainda importa dentro desta funcao, por dois motivos preservados de
 * antes da Fase 1: (1) tls (0x10F00000) cai dentro do range NOMINAL de
 * VM_RSX_BASE (0x10000000..0x20000000) -- por isso tls e' verificado primeiro,
 * antes de rsx; sem isso a imagem TLS reportava "rsx". (2) VM_RSX_BASE
 * (0x10000000) tambem se sobrepoe aos ultimos 64KB de
 * VM_MAIN_MEM_BASE+VM_MAIN_MEM_SIZE (0x00010000+0x10000000 = 0x10010000) --
 * um EA nesses 64KB e' reportado "main", nunca "rsx", porque o main-mem check
 * corre primeiro (idem em vm_is_valid_addr, que tem a mesma sobreposicao
 * latente e a mesma ordem). Nenhuma das novas regioes (lv2_heap/sys_vm/
 * rsx_local, 0x40000000+) colide com as quatro antigas, por isso entram no
 * fim sem precisar de reordenar o resto.
 */
static inline const char* vm_region_name(uint32_t addr)
{
    /* Fase 1: TLS antes de "rsx". A janela TLS (0x10F00000) cai dentro do
     * intervalo VM_RSX_BASE nominal (0x10000000..0x20000000), por isso a
     * ordem importa -- sem esta linha a imagem TLS reporta "rsx", como
     * acontecia ate' aqui. */
    if (addr >= VM_TLS_IMG_BASE && addr < VM_TLS_IMG_BASE + VM_TLS_IMG_WINDOW)
        return "tls";
    if (addr >= VM_MAIN_MEM_BASE && addr < VM_MAIN_MEM_BASE + VM_MAIN_MEM_SIZE)
        return "main";
    if (addr >= VM_STACK_BASE && addr < VM_STACK_BASE + VM_STACK_REGION)
        return "stack";
    if (addr >= VM_SPU_BASE && addr < VM_SPU_BASE + 8 * VM_SPU_WINDOW_SIZE)
        return "spu";
    if (addr >= VM_LV2_HEAP_BASE && addr < VM_LV2_HEAP_END)
        return "lv2_heap";
    if (addr >= VM_SYS_VM_BASE && addr < VM_SYS_VM_END)
        return "sys_vm";
    if (addr >= VM_RSX_LOCAL_BASE && addr < VM_RSX_LOCAL_END)
        return "rsx_local";
    /* VM_RSX_BASE (0x10000000) e' o intervalo RSX NOMINAL do mapa antigo; a
     * RSX local real e' rsx_local, acima. Mantido por compatibilidade com
     * vm_is_valid_addr(), que o usa. */
    if (addr >= VM_RSX_BASE && addr < VM_RSX_BASE + VM_RSX_SIZE)
        return "rsx";
    return "unmapped";
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* VM_H */
