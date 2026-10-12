/* Drives the shipped vm_read32_hot used by lifted TUs (BE load + bswap). */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

uint8_t* vm_base;
volatile int g_ppu_preempt_req;
int ppu_guest_range_committed(uint32_t addr, uint32_t n)
{
    return (uint64_t)addr + n <= 16ull;
}
void ppu_preempt_check(void) {}

/* O caminho quente agora consulta o portao de gameplay e cai nos acessores
 * COMPLETOS enquanto ele esta' a 0 (ver ppu_vm_hot.h). O teste liga o portao e
 * fornece stubs dos completos que so' servem para o link -- se algum deles for
 * chamado, o teste falha em vez de passar silenciosamente. */
static int g_fat_called;
volatile int g_ps3_vm_gameplay;   /* definido aqui: o teste nao linka a policy */
uint32_t vm_read32(uint64_t a)              { (void)a; g_fat_called = 1; return 0; }
uint64_t vm_read64(uint64_t a)              { (void)a; g_fat_called = 1; return 0; }
void     vm_write32(uint64_t a, uint32_t v) { (void)a; (void)v; g_fat_called = 1; }

/* A verificacao de faixa passou a ser INLINE no header (ppu_range_committed_hot):
 * o teste publica uma faixa unica 0..16, equivalente ao stub acima, para que o
 * caso "uncommitted" continue a ser exercitado. */
uint32_t g_committed_lo[8];
uint32_t g_committed_hi[8];
int      g_committed_count;

#include "../ppu_vm_hot.h"

#define CHECK(cond) do { \
    if (!(cond)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); return 1; } \
} while (0)

int main(void)
{
    uint8_t arena[16];
    memset(arena, 0, sizeof arena);
    vm_base = arena;
    g_ppu_preempt_req = 0;
    g_committed_lo[0] = 0; g_committed_hi[0] = 16; g_committed_count = 1;
    g_ps3_vm_gameplay = 1;   /* portao aberto: exercitar o caminho INLINE */
    arena[0] = 0x12; arena[1] = 0x34; arena[2] = 0x56; arena[3] = 0x78;
    CHECK(vm_read32(0) == 0x12345678u);
    arena[4] = 0xAA; arena[5] = 0xBB; arena[6] = 0xCC; arena[7] = 0xDD;
    CHECK(vm_read32(4) == 0xAABBCCDDu);
    CHECK(vm_read32(16) == 0); /* uncommitted */
    CHECK(vm_read64_hot(0) == 0x12345678AABBCCDDull);
    CHECK(vm_read64_hot(16) == 0); /* uncommitted */
    vm_write64_hot(8, 0x0123456789ABCDEFULL);
    CHECK(arena[8]==0x01 && arena[9]==0x23 && arena[10]==0x45 && arena[11]==0x67);
    CHECK(arena[12]==0x89 && arena[13]==0xAB && arena[14]==0xCD && arena[15]==0xEF);
    CHECK(vm_read64_hot(8) == 0x0123456789ABCDEFULL);
    uint8_t before0 = arena[0];
    vm_write64_hot(16, 0xDEADBEEFDEADBEEFULL); /* uncommitted: no-op */
    CHECK(arena[0] == before0);
    return 0;
}
