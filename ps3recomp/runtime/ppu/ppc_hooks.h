/*
 * ps3recomp -- weak function overrides (Fase 19, XEN-04)
 *
 * Substituir uma funcao guest INTEIRA por codigo host, sem editar o `.cpp`
 * gerado e sem um `patch_*.py` que tenha de ser reaplicado a cada re-lift.
 *
 * Nao confundir com o mid-asm da Fase 17 (`[[midasm_hook]]`): esse corre AO
 * LADO de uma instrucao e deixa a funcao a correr; este ENVOLVE a funcao toda
 * e o override host substitui-a por completo. Sao mecanismos diferentes para
 * problemas diferentes -- ver docs/RECOMP_CONFIG.md.
 *
 * ---------------------------------------------------------------------------
 * O esquema de simbolos
 * ---------------------------------------------------------------------------
 *
 *   __imp_func_XXXXXXXX   FORTE   corpo liftado (a implementacao original)
 *   func_XXXXXXXX         FRACO   wrapper fino: chama __imp_func_XXXXXXXX
 *   func_XXXXXXXX (host)  FORTE   o override; GANHA o link sobre o wrapper
 *
 * O lifter emite o par (corpo + wrapper) SO' para as funcoes declaradas em
 * `[[functions_override]]` e SO' com a emissao ligada (`--emit-weak-wrappers`
 * ou `[main].emit_weak_wrappers = true`). Sem isso o output nao muda um byte.
 *
 * O override host escreve-se numa TU propria (p.ex.
 * games/gow2/hooks/gow2_func_overrides.cpp) assim:
 *
 *     #include "ppu_recomp.h"
 *     void func_002C0508(ppu_context* ctx) { ... }
 *
 * Linkage C++, SEM `extern "C"`: e' o linkage com que o lifter emite
 * `void func_XXXXXXXX(ppu_context*)` (ver o docstring de
 * `PPULifter.emit_fallback_stubs` em tools/ppu_lifter.py). Um `extern "C"` no
 * override produzia um simbolo com outro nome, o wrapper fraco continuava a
 * ganhar, e o override era um no-op silencioso -- o pior resultado possivel.
 *
 * ---------------------------------------------------------------------------
 * Dependencias deste header
 * ---------------------------------------------------------------------------
 *
 * NAO inclui `ppu_context.h` de proposito: o `ppu_recomp.h` gerado define o
 * seu PROPRIO `typedef struct ppu_context`, e incluir os dois na mesma TU e'
 * uma redefinicao. Este header e' so' macros -- o `ppu_context` so' e' preciso
 * no ponto de EXPANSAO. Inclua antes dele o que declara o tipo:
 * `ppu_recomp.h` numa TU liftada, `ppu_context.h` numa TU host.
 *
 * ---------------------------------------------------------------------------
 * Portabilidade (o risco que a Fase 19 mediu)
 * ---------------------------------------------------------------------------
 *
 * A plataforma primaria e' macOS/arm64 com Clang (G4), e o `ld` da Apple foi
 * MEDIDO -- nao assumido -- em tools/test_weak_override_symbols.py: `nm` sobre
 * um objecto real confirma o wrapper como fraco e o corpo como forte, e o
 * binario ligado com um override forte corre o override (chamada directa,
 * chamada via `function_table` e chamada de outra funcao liftada da mesma TU).
 *
 * Fora do Clang, `PPC_WEAK` fica VAZIO e o wrapper sai FORTE: o mecanismo
 * degrada para "nao suportado", e um override host produz entao um erro de
 * duplicate symbol no link. Isso e' deliberado -- falhar alto no link e' muito
 * melhor do que um override que nao pega e passa despercebido. Um consumidor
 * que queira recusar cedo pode testar `PPC_HOOKS_WEAK_SUPPORTED`.
 */
#ifndef PPC_HOOKS_H
#define PPC_HOOKS_H

#if defined(__clang__)
/* Caminho primario e o unico MEDIDO (macOS/arm64, Apple clang). */
#  define PPC_HOOKS_WEAK_SUPPORTED 1
#  define PPC_WEAK __attribute__((weak))
#else
/* GCC e MSVC tambem tem formas de simbolo fraco (`__attribute__((weak))`,
 * `/alternatename`), mas nenhuma foi medida por esta fase e nenhuma se assume
 * aqui: sem medicao, degradar para simbolo forte e' a opcao honesta. */
#  define PPC_HOOKS_WEAK_SUPPORTED 0
#  define PPC_WEAK
#endif

/* Corpo liftado. `PPC_FUNC_IMPL(func_00001000) { ... }` ->
 * `void __imp_func_00001000(ppu_context* ctx) { ... }` (forte). */
#define PPC_FUNC_IMPL(name) void __imp_##name(ppu_context* ctx)

/* Wrapper substituivel. `PPC_FUNC(func_00001000) { ... }` ->
 * `__attribute__((weak)) void func_00001000(ppu_context* ctx) { ... }`. */
#define PPC_FUNC(name) PPC_WEAK void name(ppu_context* ctx)

/* Declaracao do corpo, para quem queira chamar a implementacao ORIGINAL a
 * partir do override (o padrao "envolver, nao substituir"):
 *
 *     PPC_FUNC_IMPL_DECL(func_002C0508);
 *     void func_002C0508(ppu_context* ctx) {
 *         gow2_pre(ctx);
 *         __imp_func_002C0508(ctx);   // corre o corpo liftado original
 *     }
 */
#define PPC_FUNC_IMPL_DECL(name) void __imp_##name(ppu_context* ctx)

#endif /* PPC_HOOKS_H */
