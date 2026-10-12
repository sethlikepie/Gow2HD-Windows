/*
 * ps3recomp - Memory management syscalls (implementation)
 */

#include "sys_memory.h"
/* Inclui a mesma declaracao de ps3_sys_memory_used() que ps3_checkpoint.h
 * expoe ao resto do runtime (review pos-Task 2, achado do extern duplicado):
 * se esta definicao alguma vez divergir da assinatura la declarada, este TU
 * (que ve as duas) apanha um erro de compilacao em vez de UB silenciosa entre
 * TUs. ps3_checkpoint.h nao arrasta nada pesado (so' stdint.h). */
#include "ps3_checkpoint.h"
/* Task 3 (bringup-instrumentacao-fase0): subsistema HEAP -- ver o comentario
 * grande junto de sys_memory_allocate() abaixo para o porque este e' o
 * allocator real instrumentado (a alternativa, o allocator guest de
 * free-list/boundary-tag descrito no CLAUDE.md, e' codigo PPC liftado, nao
 * host -- ver esse comentario). */
#include "ps3_trace.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Fase 1 (fase1-desmascarar-memoria, Task 5): definido em
 * runtime/ppu/ppu_loader.cpp:538 (compilado por jogo, nao entra na lib).
 * Declarado aqui porque sys_memory.c ENTRA na libps3recomp_runtime.a: o
 * simbolo so' resolve no link do boot_gow2 (ou de qualquer consumidor que
 * tambem ligue ppu_loader.cpp). */
extern void ppu_register_committed_range(uint32_t lo, uint32_t hi);

/* ---------------------------------------------------------------------------
 * Globals
 * -----------------------------------------------------------------------*/
sys_mem_alloc_info       g_sys_mem_allocs[SYS_MEMORY_ALLOC_MAX];
sys_mem_container_info   g_sys_mem_containers[SYS_MEMORY_CONTAINER_MAX];
sys_mmapper_shared_info  g_sys_mmapper_shared[SYS_MMAPPER_SHARED_MAX];

/* Bump allocator for main memory pool.
 * Starts after a reasonable offset to avoid the low addresses used by ELF. */
uint32_t g_sys_mem_bump_ptr = 0;

/* Total user memory size (default 213 MB, after kernel reservation) */
#define SYS_MEM_USER_TOTAL  (213 * 1024 * 1024)

/* Guest window handed out by sys_memory_allocate / sys_mmapper.
 * 0x40000000+ matches where the real lv2 places these allocations (verified
 * against an RPCS3 boot log of Yakuza: Dead Souls); the window is outside
 * the pre-committed main region, so pages are committed on demand. */
#define SYS_MEM_ALLOC_BASE  0x40000000u
/* 512 MB window (was 256 MB): GoW2's boot issues 27+ allocations including
 * several 9 MB streaming buffers on top of the CRT heap's 124 MB arena, and
 * the bump allocator never recycles -- 256 MB ran out mid-gfxInit (malloc
 * returned 0 -> the engine surfaced it as a bogus "vector<T> too long"). */
#define SYS_MEM_ALLOC_END   0x60000000u

/* Fase 1 (fase1-desmascarar-memoria, Task 4): vm.h e' a fonte unica destes
 * limites (VM_LV2_HEAP_*); esta duplicacao so' sobrevive como cache local --
 * se um dia divergir, drift vira erro de compilacao aqui, nao bug silencioso
 * em vm_region_name(). vm.h ja e' incluido via sys_memory.h. */
_Static_assert(SYS_MEM_ALLOC_BASE == VM_LV2_HEAP_BASE,
               "mapa divergiu: vm.h e' a fonte unica (Fase 1)");
_Static_assert(SYS_MEM_ALLOC_END  == VM_LV2_HEAP_END,
               "mapa divergiu: vm.h e' a fonte unica (Fase 1)");

static uint32_t s_total_allocated = 0;

/* Guest threads are real host threads; serialize the bump allocator. */
#ifdef _WIN32
static SRWLOCK s_bump_lock = SRWLOCK_INIT;
static void bump_lock(void)   { AcquireSRWLockExclusive(&s_bump_lock); }
static void bump_unlock(void) { ReleaseSRWLockExclusive(&s_bump_lock); }
#else
#include <pthread.h>
static pthread_mutex_t s_bump_mtx = PTHREAD_MUTEX_INITIALIZER;
static void bump_lock(void)   { pthread_mutex_lock(&s_bump_mtx); }
static void bump_unlock(void) { pthread_mutex_unlock(&s_bump_mtx); }
#endif

static void write_be32(uint32_t addr, uint32_t val)
{
    uint32_t* p = (uint32_t*)vm_to_host(addr);
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__ || defined(_WIN32)
    val = ((val >> 24) & 0xFF) | ((val >> 8) & 0xFF00) |
          ((val <<  8) & 0xFF0000) | ((val << 24) & 0xFF000000u);
#endif
    *p = val;
}

/* ---------------------------------------------------------------------------
 * ps3_emergency_alloc -- host-callable fallback allocator.
 *
 * GoW2's C-heap arena-grow (func_0037BF30) probes a sentinel lwmutex via
 * sys_lwmutex_destroy and only allocates when it reports the heap "busy"
 * (EBUSY); under our HLE the probe returns CELL_OK, so the grow fails and
 * large allocations (e.g. the gzip/asset decompression buffer) return NULL.
 * When that happens the recompiled code would inflate into guest address 0
 * and later read a garbage count -> std::vector "vector<T> too long".
 *
 * As a surgical workaround, hand out a committed chunk from the
 * sys_memory bump window so the decompression has a real buffer.
 * -----------------------------------------------------------------------*/
uint32_t ps3_emergency_alloc(uint32_t size)
{
    if (size == 0) return 0;
    size = VM_ALIGN_UP(size, 0x100);

    bump_lock();
    if (g_sys_mem_bump_ptr == 0)
        g_sys_mem_bump_ptr = SYS_MEM_ALLOC_BASE;
    g_sys_mem_bump_ptr = VM_ALIGN_UP(g_sys_mem_bump_ptr, 0x100);
    if (g_sys_mem_bump_ptr + size > SYS_MEM_ALLOC_END) {
        bump_unlock();
        return 0;
    }
    uint32_t addr = g_sys_mem_bump_ptr;
    g_sys_mem_bump_ptr += size;
    bump_unlock();

    if (vm_commit(addr, size) != CELL_OK)
        return 0;
    /* Fase 1: sem este registo o vm_uncommitted_guard trata a memoria como
     * nao-comitada e ENGOLE escritas do jogo a memoria que ele proprio
     * alocou. Medido: 5 vm_commit, 0 registos. */
    ppu_register_committed_range(addr, addr + size);
    return addr;
}

/* ---------------------------------------------------------------------------
 * sys_memory_allocate
 *
 * r3 = size
 * r4 = flags (page size)
 * r5 = pointer to receive allocated address (u32*)
 * -----------------------------------------------------------------------*/
int64_t sys_memory_allocate(ppu_context* ctx)
{
    uint32_t size      = LV2_ARG_U32(ctx, 0);
    uint32_t flags     = LV2_ARG_U32(ctx, 1);
    uint32_t addr_out  = LV2_ARG_PTR(ctx, 2);

    fprintf(stderr, "[sys_memory] allocate(size=0x%X, flags=0x%X)\n",
            size, flags);

    /* Determine alignment based on page size flags */
    uint32_t alignment;
    if (flags & SYS_MEMORY_PAGE_SIZE_1M) {
        alignment = 0x100000; /* 1 MB */
    } else {
        alignment = 0x10000;  /* 64 KB */
    }

    size = VM_ALIGN_UP(size, alignment);

    if (size == 0)
        return (int64_t)(int32_t)CELL_EINVAL;

    bump_lock();

    /* Initialize bump pointer on first call */
    if (g_sys_mem_bump_ptr == 0)
        g_sys_mem_bump_ptr = SYS_MEM_ALLOC_BASE;

    /* Align bump pointer */
    g_sys_mem_bump_ptr = VM_ALIGN_UP(g_sys_mem_bump_ptr, alignment);

    /* Check if we have room */
    if (g_sys_mem_bump_ptr + size > SYS_MEM_ALLOC_END) {
        bump_unlock();
        return (int64_t)(int32_t)CELL_ENOMEM;
    }

    /* Find a free allocation slot */
    int slot = -1;
    for (int i = 0; i < SYS_MEMORY_ALLOC_MAX; i++) {
        if (!g_sys_mem_allocs[i].active) { slot = i; break; }
    }
    if (slot < 0) {
        bump_unlock();
        return (int64_t)(int32_t)CELL_ENOMEM;
    }

    uint32_t alloc_addr = g_sys_mem_bump_ptr;
    g_sys_mem_bump_ptr += size;
    s_total_allocated += size;

    sys_mem_alloc_info* a = &g_sys_mem_allocs[slot];
    a->active       = 1;       /* claim the slot before unlocking */
    a->addr         = alloc_addr;
    a->size         = size;
    a->container_id = 0;

    bump_unlock();

    /* Commit the pages (window is outside the pre-committed main region);
     * fresh commits are already zeroed by the OS */
    if (vm_commit(alloc_addr, size) != CELL_OK) {
        a->active = 0;
        return (int64_t)(int32_t)CELL_ENOMEM;
    }
    /* Fase 1: sem este registo o vm_uncommitted_guard trata a memoria como
     * nao-comitada e ENGOLE escritas do jogo a memoria que ele proprio
     * alocou. Medido: 5 vm_commit, 0 registos. */
    ppu_register_committed_range(alloc_addr, alloc_addr + size);

    fprintf(stderr, "[sys_memory] allocate -> 0x%08X\n", alloc_addr);

    /* Task 3 (bringup-instrumentacao-fase0), subsistema HEAP: ALLOC canonico.
     * Este e' o allocator LV2 sys_memory (kernel-level; bump pointer +
     * reuso por container -- ver sys_memory_allocate_from_container abaixo),
     * DIFERENTE do allocator CRT/game-heap de free-list/boundary-tag que o
     * CLAUDE.md descreve (func_00263040/160/178, cursor/magic/arena) -- esse
     * e' codigo PPC LIFTADO (ppu_recomp_*.cpp, gerado e gitignored),
     * inatingivel a partir de host C sem tocar em ficheiros gerados (ver a
     * nota completa no relatorio da Task 3). sys_memory_allocate() e' o
     * allocator real, host, exercitado todo boot -- heap_used= do SNAPSHOT
     * (Task 2) ja depende de s_total_allocated, mutado so' aqui e em
     * sys_memory_free()/sys_memory_allocate_from_container(). Emitido FORA
     * do bump_lock/bump_unlock acima (a secao critica ja acabou; o emissor
     * faz fwrite+fflush -- I/O dentro do lock teria custo de latencia real
     * mesmo sem risco de deadlock). arena=main: esta chamada serve sempre do
     * pool principal (container_id=0); allocate_from_container() e' quem
     * preenche arena=<cid>. */
    PS3_TRACE(PS3_TS_HEAP, "sys_memory_allocate", "ALLOC",
              "ea=0x%08X size=%u align=%u arena=main", alloc_addr, size, alignment);

    if (addr_out != 0) {
        write_be32(addr_out, alloc_addr);
    }

    return CELL_OK;
}

/* ---------------------------------------------------------------------------
 * sys_memory_free
 *
 * r3 = address
 * -----------------------------------------------------------------------*/
int64_t sys_memory_free(ppu_context* ctx)
{
    uint32_t addr = LV2_ARG_U32(ctx, 0);

    if (getenv("PS3_TRACE_SHADERSRC"))
        fprintf(stderr, "[sys_memory] free(addr=0x%08X lr=0x%08X ra=%p/%p/%p/%p)\n",
                addr, (uint32_t)ctx->lr,
                __builtin_return_address(0), __builtin_return_address(1),
                __builtin_return_address(2), __builtin_return_address(3));

    for (int i = 0; i < SYS_MEMORY_ALLOC_MAX; i++) {
        if (g_sys_mem_allocs[i].active && g_sys_mem_allocs[i].addr == addr) {
            uint32_t cid        = g_sys_mem_allocs[i].container_id;
            uint32_t freed_size = g_sys_mem_allocs[i].size;
            if (cid >= 1 && cid <= SYS_MEMORY_CONTAINER_MAX && g_sys_mem_containers[cid - 1].active)
                g_sys_mem_containers[cid - 1].used_size -= freed_size;
            s_total_allocated -= freed_size;
            g_sys_mem_allocs[i].active = 0;   /* keep addr/size for reuse */

            /* Task 3 (bringup-instrumentacao-fase0): FREE canonico -- mesmo
             * allocator host-observavel de sys_memory_allocate() acima (ver o
             * comentario la). `size`/`arena` lidos ANTES do active=0 (que so'
             * limpa a flag; addr/size ficam para o reuso em
             * sys_memory_allocate_from_container()), por isso continuam
             * validos aqui a seguir. */
            if (ps3_trace_on(PS3_TS_HEAP)) {
                char arena_buf[16];
                if (cid >= 1 && cid <= SYS_MEMORY_CONTAINER_MAX) {
                    snprintf(arena_buf, sizeof(arena_buf), "%u", cid);
                } else {
                    snprintf(arena_buf, sizeof(arena_buf), "main");
                }
                PS3_TRACE(PS3_TS_HEAP, "sys_memory_free", "FREE",
                          "ea=0x%08X size=%u arena=%s", addr, freed_size, arena_buf);
            }
            return CELL_OK;
        }
    }

    return (int64_t)(int32_t)CELL_EINVAL;
}

/* ---------------------------------------------------------------------------
 * sys_memory_get_user_memory_size
 *
 * r3 = pointer to output struct: { u32 total, u32 available }
 * -----------------------------------------------------------------------*/
int64_t sys_memory_get_user_memory_size(ppu_context* ctx)
{
    uint32_t out_addr = LV2_ARG_PTR(ctx, 0);

    fprintf(stderr, "[sys_memory] get_user_memory_size()\n");
    if (out_addr != 0) {
        uint32_t total = SYS_MEM_USER_TOTAL;
        uint32_t avail = total - s_total_allocated;
        write_be32(out_addr + 0, total);
        write_be32(out_addr + 4, avail);
    }

    return CELL_OK;
}

/* ---------------------------------------------------------------------------
 * sys_memory_container_create
 *
 * r3 = pointer to receive container ID (u32*)
 * r4 = size
 * -----------------------------------------------------------------------*/
int64_t sys_memory_container_create(ppu_context* ctx)
{
    uint32_t id_out_addr = LV2_ARG_PTR(ctx, 0);
    uint32_t size        = LV2_ARG_U32(ctx, 1);

    int slot = -1;
    for (int i = 0; i < SYS_MEMORY_CONTAINER_MAX; i++) {
        if (!g_sys_mem_containers[i].active) { slot = i; break; }
    }
    if (slot < 0)
        return (int64_t)(int32_t)CELL_EAGAIN;

    sys_mem_container_info* c = &g_sys_mem_containers[slot];
    c->active     = 1;
    c->total_size = size;
    c->used_size  = 0;

    uint32_t container_id = (uint32_t)(slot + 1);
    if (id_out_addr != 0) {
        write_be32(id_out_addr, container_id);
    }

    return CELL_OK;
}

/* ---------------------------------------------------------------------------
 * sys_memory_allocate_from_container (350)
 *
 * SDK order is (size, cid, flags, out) but GoW2's thunk passes the container
 * id in r3 and the size in r4 -- accept both by treating the small arg
 * (a plausible container id) as the cid. Serves from the same bump window as
 * sys_memory_allocate; per-container accounting is tracked when the id maps
 * to a live container.
 * -----------------------------------------------------------------------*/
int64_t sys_memory_allocate_from_container(ppu_context* ctx)
{
    uint32_t a0       = LV2_ARG_U32(ctx, 0);
    uint32_t a1       = LV2_ARG_U32(ctx, 1);
    uint32_t flags    = LV2_ARG_U32(ctx, 2);
    uint32_t addr_out = LV2_ARG_PTR(ctx, 3);

    uint32_t cid, size;
    if (a0 >= 1 && a0 <= SYS_MEMORY_CONTAINER_MAX) { cid = a0; size = a1; }
    else                                           { size = a0; cid = a1; }

    fprintf(stderr, "[sys_memory] allocate_from_container(cid=0x%X, size=0x%X, flags=0x%X)\n",
            cid, size, flags);
    if (getenv("PS3_TRACE_SHADERSRC"))
        fprintf(stderr, "[sys_memory] allocate_from_container caller lr=0x%08X out=0x%08X ra=%p/%p/%p/%p\n",
                (uint32_t)ctx->lr, addr_out,
                __builtin_return_address(0), __builtin_return_address(1),
                __builtin_return_address(2), __builtin_return_address(3));

    uint32_t alignment = (flags & SYS_MEMORY_PAGE_SIZE_1M) ? 0x100000 : 0x10000;
    size = VM_ALIGN_UP(size, alignment);
    if (size == 0)
        return (int64_t)(int32_t)CELL_EINVAL;

    bump_lock();
    if (g_sys_mem_bump_ptr == 0)
        g_sys_mem_bump_ptr = SYS_MEM_ALLOC_BASE;
    g_sys_mem_bump_ptr = VM_ALIGN_UP(g_sys_mem_bump_ptr, alignment);
    /* Reuse a freed block first: the streaming loader alloc/frees this
     * container for every archive it decompresses; a pure bump pointer
     * leaks the whole window in a few hundred cycles. */
    int slot = -1;
    uint32_t alloc_addr = 0;
    for (int i = 0; i < SYS_MEMORY_ALLOC_MAX; i++) {
        sys_mem_alloc_info* fa = &g_sys_mem_allocs[i];
        if (!fa->active && fa->addr && fa->size >= size &&
            (fa->addr & (alignment - 1)) == 0) {
            slot = i; alloc_addr = fa->addr; size = fa->size; break;
        }
    }
    /* Task 3 (bringup-instrumentacao-fase0): distingue, para o HEAP/FREELIST
     * abaixo, se este pedido foi satisfeito reaproveitando um bloco
     * previamente libertado (o loop acima) em vez de um bump fresco. */
    int reused_from_freelist = (slot >= 0);
    if (slot < 0) {
        if (g_sys_mem_bump_ptr + size > SYS_MEM_ALLOC_END) {
            bump_unlock();
            return (int64_t)(int32_t)CELL_ENOMEM;
        }
        for (int i = 0; i < SYS_MEMORY_ALLOC_MAX; i++) {
            if (!g_sys_mem_allocs[i].active && g_sys_mem_allocs[i].addr == 0) { slot = i; break; }
        }
        if (slot < 0) {
            bump_unlock();
            return (int64_t)(int32_t)CELL_ENOMEM;
        }
        alloc_addr = g_sys_mem_bump_ptr;
        g_sys_mem_bump_ptr += size;
    }
    s_total_allocated += size;
    sys_mem_alloc_info* a = &g_sys_mem_allocs[slot];
    a->active       = 1;
    a->addr         = alloc_addr;
    a->size         = size;
    a->container_id = cid;
    if (cid >= 1 && cid <= SYS_MEMORY_CONTAINER_MAX && g_sys_mem_containers[cid - 1].active)
        g_sys_mem_containers[cid - 1].used_size += size;
    bump_unlock();

    if (vm_commit(alloc_addr, size) != CELL_OK) {
        a->active = 0;
        return (int64_t)(int32_t)CELL_ENOMEM;
    }
    /* Fase 1: sem este registo o vm_uncommitted_guard trata a memoria como
     * nao-comitada e ENGOLE escritas do jogo a memoria que ele proprio
     * alocou. Medido: 5 vm_commit, 0 registos. */
    ppu_register_committed_range(alloc_addr, alloc_addr + size);

    fprintf(stderr, "[sys_memory] allocate_from_container -> 0x%08X\n", alloc_addr);

    /* Task 3 (bringup-instrumentacao-fase0), subsistema HEAP -- mesmo
     * allocator host-observavel de sys_memory_allocate() (ver o comentario
     * grande la). FREELIST: esta chamada satisfez o pedido reaproveitando um
     * bloco previamente libertado (achado real do loop "Reuse a freed block
     * first" acima, nao fabricado); emitido ANTES do ALLOC, que sai sempre a
     * seguir (reuso ou bump fresco). arena=<cid> em decimal, nunca "main" --
     * esta funcao serve sempre um container explicito. */
    if (reused_from_freelist) {
        PS3_TRACE(PS3_TS_HEAP, "sys_memory_allocate_from_container", "FREELIST",
                  "ea=0x%08X size=%u arena=%u", alloc_addr, size, cid);
    }
    PS3_TRACE(PS3_TS_HEAP, "sys_memory_allocate_from_container", "ALLOC",
              "ea=0x%08X size=%u align=%u arena=%u", alloc_addr, size, alignment, cid);

    if (addr_out != 0)
        write_be32(addr_out, alloc_addr);
    return CELL_OK;
}

/* ---------------------------------------------------------------------------
 * sys_memory_container_destroy
 *
 * r3 = container_id
 * -----------------------------------------------------------------------*/
int64_t sys_memory_container_destroy(ppu_context* ctx)
{
    uint32_t container_id = LV2_ARG_U32(ctx, 0);

    if (container_id == 0 || container_id > SYS_MEMORY_CONTAINER_MAX)
        return (int64_t)(int32_t)CELL_ESRCH;

    sys_mem_container_info* c = &g_sys_mem_containers[container_id - 1];
    if (!c->active)
        return (int64_t)(int32_t)CELL_ESRCH;

    c->active = 0;
    return CELL_OK;
}

/* ---------------------------------------------------------------------------
 * sys_memory_container_get_size
 *
 * r3 = container_id
 * r4 = pointer to output struct: { u32 total, u32 used }
 * -----------------------------------------------------------------------*/
int64_t sys_memory_container_get_size(ppu_context* ctx)
{
    uint32_t container_id = LV2_ARG_U32(ctx, 0);
    uint32_t out_addr     = LV2_ARG_PTR(ctx, 1);

    if (container_id == 0 || container_id > SYS_MEMORY_CONTAINER_MAX)
        return (int64_t)(int32_t)CELL_ESRCH;

    sys_mem_container_info* c = &g_sys_mem_containers[container_id - 1];
    if (!c->active)
        return (int64_t)(int32_t)CELL_ESRCH;

    if (out_addr != 0) {
        write_be32(out_addr + 0, c->total_size);
        write_be32(out_addr + 4, c->used_size);
    }

    return CELL_OK;
}

/* ---------------------------------------------------------------------------
 * sys_mmapper_allocate_address
 *
 * r3 = size
 * r4 = flags
 * r5 = alignment
 * r6 = pointer to receive address (u32*)
 * -----------------------------------------------------------------------*/
int64_t sys_mmapper_allocate_address(ppu_context* ctx)
{
    uint32_t size      = LV2_ARG_U32(ctx, 0);
    /* uint32_t flags  = LV2_ARG_U32(ctx, 1); */
    uint32_t alignment = LV2_ARG_U32(ctx, 2);
    uint32_t addr_out  = LV2_ARG_PTR(ctx, 3);

    if (alignment == 0) alignment = 0x10000;
    size = VM_ALIGN_UP(size, alignment);

    bump_lock();

    if (g_sys_mem_bump_ptr == 0)
        g_sys_mem_bump_ptr = SYS_MEM_ALLOC_BASE;

    g_sys_mem_bump_ptr = VM_ALIGN_UP(g_sys_mem_bump_ptr, alignment);

    if (g_sys_mem_bump_ptr + size > SYS_MEM_ALLOC_END) {
        bump_unlock();
        return (int64_t)(int32_t)CELL_ENOMEM;
    }

    uint32_t alloc_addr = g_sys_mem_bump_ptr;
    g_sys_mem_bump_ptr += size;

    bump_unlock();

    /* Commit the reserved region.
     * Fase 1: este sitio ignorava o retorno de vm_commit -- passa a
     * verificar antes de registar (mudanca de comportamento adicional,
     * ainda que correcta: antes nao havia verificacao nenhuma aqui). Sem
     * este registo o vm_uncommitted_guard trata a memoria como
     * nao-comitada e ENGOLE escritas do jogo a memoria que ele proprio
     * alocou. Medido: 5 vm_commit, 0 registos.
     * Round 2: alinhado com :194-196 e :407 deste ficheiro -- se o commit
     * falhar, o syscall nao pode devolver CELL_OK nem escrever addr_out (o
     * guest receberia um "endereco valido" para memoria nunca comitada).
     * Nem :194-196 nem :407 fazem rollback do bump pointer em falha (so'
     * desactivam o slot de alocacao, que este sitio nem tem); replica-se o
     * mesmo: a reserva do bump pointer fica perdida, igual aos outros dois. */
    if (vm_commit(alloc_addr, size) != CELL_OK)
        return (int64_t)(int32_t)CELL_ENOMEM;
    ppu_register_committed_range(alloc_addr, alloc_addr + size);

    if (addr_out != 0) {
        write_be32(addr_out, alloc_addr);
    }

    return CELL_OK;
}

/* ---------------------------------------------------------------------------
 * sys_mmapper_free_address
 *
 * r3 = address
 * -----------------------------------------------------------------------*/
int64_t sys_mmapper_free_address(ppu_context* ctx)
{
    /* uint32_t addr = LV2_ARG_U32(ctx, 0); */
    (void)ctx;
    /* In our bump allocator we don't actually free, just acknowledge */
    return CELL_OK;
}

/* ---------------------------------------------------------------------------
 * sys_mmapper_allocate_shared_memory
 *
 * r3 = key
 * r4 = size
 * r5 = flags
 * r6 = pointer to receive shared mem ID (u32*)
 * -----------------------------------------------------------------------*/
int64_t sys_mmapper_allocate_shared_memory(ppu_context* ctx)
{
    uint64_t key      = LV2_ARG_U64(ctx, 0);
    uint32_t size     = LV2_ARG_U32(ctx, 1);
    /* uint32_t flags = LV2_ARG_U32(ctx, 2); */
    uint32_t id_out   = LV2_ARG_PTR(ctx, 3);

    int slot = -1;
    for (int i = 0; i < SYS_MMAPPER_SHARED_MAX; i++) {
        if (!g_sys_mmapper_shared[i].active) { slot = i; break; }
    }
    if (slot < 0)
        return (int64_t)(int32_t)CELL_EAGAIN;

    sys_mmapper_shared_info* s = &g_sys_mmapper_shared[slot];
    s->active = 1;
    s->size   = VM_ALIGN_UP(size, VM_PAGE_SIZE);
    s->key    = key;
    s->addr   = 0;

    uint32_t shm_id = (uint32_t)(slot + 1);
    if (id_out != 0) {
        write_be32(id_out, shm_id);
    }

    return CELL_OK;
}

/* ---------------------------------------------------------------------------
 * sys_mmapper_map_shared_memory
 *
 * r3 = address (where to map)
 * r4 = shared mem ID
 * r5 = flags
 * -----------------------------------------------------------------------*/
int64_t sys_mmapper_map_shared_memory(ppu_context* ctx)
{
    uint32_t addr   = LV2_ARG_U32(ctx, 0);
    uint32_t shm_id = LV2_ARG_U32(ctx, 1);
    /* uint32_t flags = LV2_ARG_U32(ctx, 2); */

    if (shm_id == 0 || shm_id > SYS_MMAPPER_SHARED_MAX)
        return (int64_t)(int32_t)CELL_ESRCH;

    sys_mmapper_shared_info* s = &g_sys_mmapper_shared[shm_id - 1];
    if (!s->active)
        return (int64_t)(int32_t)CELL_ESRCH;

    /* Commit the memory at the specified address */
    int32_t rc = vm_commit(addr, s->size);
    if (rc != CELL_OK)
        return (int64_t)(int32_t)rc;
    /* Fase 1: sem este registo o vm_uncommitted_guard trata a memoria como
     * nao-comitada e ENGOLE escritas do jogo a memoria que ele proprio
     * alocou. Medido: 5 vm_commit, 0 registos. */
    ppu_register_committed_range(addr, addr + s->size);

    s->addr = addr;
    memset(vm_to_host(addr), 0, s->size);

    return CELL_OK;
}

/* ---------------------------------------------------------------------------
 * Registration
 * -----------------------------------------------------------------------*/
void sys_memory_init(lv2_syscall_table* tbl)
{
    memset(g_sys_mem_allocs,     0, sizeof(g_sys_mem_allocs));
    memset(g_sys_mem_containers, 0, sizeof(g_sys_mem_containers));
    memset(g_sys_mmapper_shared, 0, sizeof(g_sys_mmapper_shared));
    g_sys_mem_bump_ptr = 0;
    s_total_allocated  = 0;

    lv2_syscall_register(tbl, SYS_MEMORY_ALLOCATE,             sys_memory_allocate);
    lv2_syscall_register(tbl, SYS_MEMORY_ALLOCATE_FROM_CONTAINER, sys_memory_allocate_from_container);
    lv2_syscall_register(tbl, SYS_MEMORY_FREE,                 sys_memory_free);
    lv2_syscall_register(tbl, SYS_MEMORY_GET_USER_MEMORY_SIZE, sys_memory_get_user_memory_size);
    lv2_syscall_register(tbl, SYS_MEMORY_CONTAINER_CREATE,     sys_memory_container_create);
    lv2_syscall_register(tbl, SYS_MEMORY_CONTAINER_DESTROY,    sys_memory_container_destroy);
    lv2_syscall_register(tbl, SYS_MEMORY_CONTAINER_GET_SIZE,   sys_memory_container_get_size);
    lv2_syscall_register(tbl, SYS_MMAPPER_ALLOCATE_ADDRESS,    sys_mmapper_allocate_address);
    lv2_syscall_register(tbl, SYS_MMAPPER_FREE_ADDRESS,        sys_mmapper_free_address);
    lv2_syscall_register(tbl, SYS_MMAPPER_ALLOCATE_SHARED_MEMORY, sys_mmapper_allocate_shared_memory);
    lv2_syscall_register(tbl, SYS_MMAPPER_MAP_SHARED_MEMORY,  sys_mmapper_map_shared_memory);
}

uint32_t ps3_sys_memory_used(void)
{
    return s_total_allocated;
}
