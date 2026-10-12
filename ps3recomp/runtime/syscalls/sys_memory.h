/*
 * ps3recomp - Memory management syscalls
 */

#ifndef SYS_MEMORY_H
#define SYS_MEMORY_H

#include "lv2_syscall_table.h"
#include "../ppu/ppu_context.h"
#include "../../include/ps3emu/ps3types.h"
#include "../../include/ps3emu/error_codes.h"
#include "../memory/vm.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Page sizes */
#define SYS_MEMORY_PAGE_SIZE_1M   0x400
#define SYS_MEMORY_PAGE_SIZE_64K  0x200

/* Maximum tracked allocations */
#define SYS_MEMORY_ALLOC_MAX      1024
#define SYS_MEMORY_CONTAINER_MAX  16
#define SYS_MMAPPER_SHARED_MAX    64

/* Allocation record */
typedef struct sys_mem_alloc_info {
    int      active;
    uint32_t addr;
    uint32_t size;
    int32_t  container_id;  /* 0 = main pool */
} sys_mem_alloc_info;

/* Container */
typedef struct sys_mem_container_info {
    int      active;
    uint32_t total_size;
    uint32_t used_size;
} sys_mem_container_info;

/* Shared memory object */
typedef struct sys_mmapper_shared_info {
    int      active;
    uint32_t size;
    uint32_t addr;   /* mapped address, 0 if not mapped */
    uint64_t key;
} sys_mmapper_shared_info;

extern sys_mem_alloc_info       g_sys_mem_allocs[SYS_MEMORY_ALLOC_MAX];
extern sys_mem_container_info   g_sys_mem_containers[SYS_MEMORY_CONTAINER_MAX];
extern sys_mmapper_shared_info  g_sys_mmapper_shared[SYS_MMAPPER_SHARED_MAX];
extern uint32_t                 g_sys_mem_bump_ptr;  /* bump allocator pointer */

/* Syscall handlers */
int64_t sys_memory_allocate(ppu_context* ctx);
/* Faltava aqui (achado da Task 3, bringup-instrumentacao-fase0, ao escrever
 * o teste offline-unit de HEAP): a funcao existe e e' registada em
 * sys_memory_init() (sys_memory.c), mas nunca tinha protótipo neste header
 * -- so' funcionava dentro do proprio sys_memory.c, onde a definicao
 * antecede o unico uso (sys_memory_init). Sem isto, qualquer OUTRO TU que
 * precise de chamar a funcao directamente (como o teste unitario de HEAP)
 * apanha "implicit function declaration" (erro em C99+). */
int64_t sys_memory_allocate_from_container(ppu_context* ctx);
int64_t sys_memory_free(ppu_context* ctx);
int64_t sys_memory_get_user_memory_size(ppu_context* ctx);
int64_t sys_memory_container_create(ppu_context* ctx);
int64_t sys_memory_container_destroy(ppu_context* ctx);
int64_t sys_memory_container_get_size(ppu_context* ctx);
int64_t sys_mmapper_allocate_address(ppu_context* ctx);
int64_t sys_mmapper_free_address(ppu_context* ctx);
int64_t sys_mmapper_allocate_shared_memory(ppu_context* ctx);
int64_t sys_mmapper_map_shared_memory(ppu_context* ctx);

/* Registration */
void sys_memory_init(lv2_syscall_table* tbl);

/* Total actualmente alocado pelo allocador de sys_memory
 * (sys_memory_allocate/sys_memory_allocate_from_container menos os frees de
 * sys_memory_free -- ver sys_memory.c para os pontos exactos de mutacao de
 * s_total_allocated; sys_mmapper_allocate_shared_memory NAO lhe toca, e'
 * outro caminho de alocacao). Adicionado na Task 2 do plano
 * bringup-instrumentacao-fase0 para o campo heap_used= do subsistema
 * SNAPSHOT (ver ps3_checkpoint.c) -- e' o valor real mais proximo de um
 * "heap usado" que o runtime rastreia; nao ha um malloc-heap generico do
 * guest separado. */
uint32_t ps3_sys_memory_used(void);

#ifdef __cplusplus
}
#endif

#endif /* SYS_MEMORY_H */
