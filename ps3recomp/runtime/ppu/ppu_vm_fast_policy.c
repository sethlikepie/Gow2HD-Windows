#include "ppu_vm_fast_policy.h"
#include <stdio.h>

volatile int g_ps3_vm_gameplay;   /* nao-static: lido inline pelo caminho quente */

int ps3_vm_fast_effective_mask(int configured, int gameplay)
{
    if (configured != 0)
        return configured;
    return gameplay ? PS3_VM_FAST_ALL : 0;
}

void ps3_vm_note_gameplay_draws(unsigned draws)
{
    if (g_ps3_vm_gameplay)
        return;
    if (draws < PS3_VM_FAST_GAMEPLAY_DRAWS)
        return;
    g_ps3_vm_gameplay = 1;
    { extern void wp84_tables_rebuild(void); wp84_tables_rebuild(); }   /* winport v84a */
    fprintf(stderr, "[vm] gameplay fast path mask=0x%02X after draws=%u\n",
            PS3_VM_FAST_ALL, draws);
    fflush(stderr);
}

int ps3_vm_gameplay_ready(void)
{
    return g_ps3_vm_gameplay;
}

/* Mesmo valor, com nome proprio para o caminho quente do ppu_loader. */
int ppu_vm_gameplay_ready_hot_c(void)
{
    return __atomic_load_n(&g_ps3_vm_gameplay, __ATOMIC_RELAXED);
}
