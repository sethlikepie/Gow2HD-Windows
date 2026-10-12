#ifndef PPU_VM_FAST_POLICY_H
#define PPU_VM_FAST_POLICY_H

#ifdef __cplusplus
extern "C" {
#endif

#define PS3_VM_FAST_READS  0x07
#define PS3_VM_FAST_WRITES 0x78
#define PS3_VM_FAST_ALL    0x7F
#define PS3_VM_FAST_GAMEPLAY_DRAWS 400u

/* configured: PS3_VM_FAST_MASK (0 = unset). gameplay: 1 after a frame with
 * >= PS3_VM_FAST_GAMEPLAY_DRAWS recorded draws.
 * Unset + still loading -> 0 (full path; fast reads at boot stall the level).
 * Unset + gameplay -> 0x7F. Explicit mask always wins. */
int ps3_vm_fast_effective_mask(int configured, int gameplay);

void ps3_vm_note_gameplay_draws(unsigned draws);
int  ps3_vm_gameplay_ready(void);

/* A mesma flag, legivel INLINE: o caminho quente do lift consulta-a em cada
 * acesso e nao pode pagar uma chamada para isso. Relaxed -- e' um portao que
 * muda UMA vez, de 0 para 1, e ler o valor velho custa no maximo mais um
 * acesso pelo caminho completo. */
/* Visibilidade escondida: sem isto o acesso passa pela GOT
 * (adrp; ldr x8,[x8]; ldr w8,[x8]) -- duas cargas em vez de uma, em TODO acesso
 * de memoria do guest. Escondido, o linker resolve directo. */
extern volatile int g_ps3_vm_gameplay __attribute__((visibility("hidden")));
static inline int ps3_vm_gameplay_ready_hot(void)
{
    return __atomic_load_n(&g_ps3_vm_gameplay, __ATOMIC_RELAXED);
}

#ifdef __cplusplus
}
#endif

#endif
