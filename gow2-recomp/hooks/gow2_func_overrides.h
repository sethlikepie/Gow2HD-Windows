/* gow2_func_overrides.h -- declaracoes dos WEAK OVERRIDES host do GoW2.
 *
 * PORQUE ESTE FICHEIRO EXISTE
 * ---------------------------
 * Fase 19 (marco v1.3, XEN-04). Uma entrada `[[functions_override]]` no
 * games/gow2/config/gow2_recomp.toml, com a emissao ligada
 * (`[main].emit_weak_wrappers = true` ou `--emit-weak-wrappers`), faz o
 * ppu_lifter.py partir a funcao liftada em DOIS simbolos (plano 19-01,
 * tools/ppu_lifter.py::_function_def_lines):
 *
 *     PPC_FUNC_IMPL(func_XXXXXXXX) { ...corpo liftado... }   -> __imp_func_XXXXXXXX (FORTE)
 *     PPC_FUNC(func_XXXXXXXX) { __imp_func_XXXXXXXX(ctx); }  -> func_XXXXXXXX      (FRACO)
 *
 * O corpo do override host vive no .cpp ao lado, define `func_XXXXXXXX` como
 * simbolo FORTE, e GANHA o link sobre o wrapper fraco -- medido, nao assumido,
 * no `ld` da Apple em arm64 (tools/test_weak_override_symbols.py, cenarios
 * L1..L7 do 19-01-SUMMARY.md: chamada directa, chamada via `function_table`,
 * chamada de outra funcao liftada da mesma TU, e a -O0/-O1/-O2).
 *
 * NAO CONFUNDIR COM O MID-ASM DA FASE 17 (gow2_midasm_hooks.{h,cpp}):
 *   - `[[midasm_hook]]` corre AO LADO de uma instrucao e a funcao guest
 *     continua a correr a seguir;
 *   - `[[functions_override]]` SUBSTITUI a funcao inteira (e pode chamar o
 *     corpo original por `__imp_*`, o padrao "envolver, nao substituir").
 * Sao mecanismos diferentes para problemas diferentes -- ver
 * ../../../docs/RECOMP_CONFIG.md.
 *
 * O CONTRATO DO SIMBOLO (fixado no 19-01, nao inventado aqui)
 * ----------------------------------------------------------
 *   - assinatura: `void func_XXXXXXXX(ppu_context* ctx)`;
 *   - linkagem C++, **SEM `extern "C"`** -- e' a linkagem com que o lifter
 *     emite as funcoes liftadas (`ppu_recomp.h` declara-as sem extern "C"),
 *     e o simbolo leva o tipo no nome: __Z13func_00010230P11ppu_context.
 *     Com `extern "C"` o override teria OUTRO nome, o wrapper fraco continuava
 *     a ganhar e o override era um no-op SILENCIOSO -- o pior resultado
 *     possivel. E' a diferenca mais facil de errar deste mecanismo inteiro.
 *
 * NOTA SOBRE `ppu_context`: mesmo arranjo do gow2_midasm_hooks.h -- a TU
 * liftada usa o typedef que o lifter escreve em ppu_recomp.h e este header usa
 * o canonico do motor (runtime/ppu/ppu_context.h, alcancavel pelo -I do
 * build_macos.sh). Sao dois typedefs distintos ao nivel da linguagem, MAS
 * partilham o mesmo TAG `struct ppu_context` -- e e' o tag, nao o typedef, que
 * entra na decoracao Itanium (`P11ppu_context`). Logo o simbolo do override
 * casa byte-a-byte com o do wrapper fraco. Se um dia os tags divergirem, isto
 * falha no LINK (undefined symbol), que e' a falha alta desejavel.
 *
 * A MACRO `GOW2_FUNC_OVERRIDE` NAO E' COSMETICA
 * --------------------------------------------
 * Alem de carregar o `__attribute__((used))`, ela e' o que o guard
 * anti-duplicate-symbol do build_macos.sh usa para ENUMERAR, a partir deste
 * .cpp, quais os `func_*` que estao a ser substituidos -- em vez de a lista
 * viver duplicada no script de build e apodrecer no primeiro override novo.
 * (O guard tambem apanha uma definicao escrita a mao sem a macro; ver la' o
 * comentario, e a licao do host_gow2_f2b: nunca ancorar um guard numa so'
 * forma sintactica.)
 */
#ifndef GOW2_FUNC_OVERRIDES_H
#define GOW2_FUNC_OVERRIDES_H

#include "ppu_context.h"   /* typedef struct ppu_context (canonico do motor) */
#include "ppc_hooks.h"     /* PPC_FUNC / PPC_FUNC_IMPL / PPC_FUNC_IMPL_DECL  */

/* Cinto e suspensorios contra dead-strip: a linha de link do build_macos.sh
 * NAO passa -dead_strip (verificado por leitura), por isso o que prova o
 * aceite e' o `nm -m` registado no SUMMARY, nao este atributo. Mesmo padrao do
 * GOW2_MIDASM_USED. Fora de GCC/clang expande para nada (G4: nao partir o
 * Windows -- que, alias, nem compila este ficheiro). */
#if defined(__GNUC__) || defined(__clang__)
#  define GOW2_FUNC_OVERRIDE_USED __attribute__((used))
#else
#  define GOW2_FUNC_OVERRIDE_USED
#endif

/* Define um override host FORTE de uma funcao liftada.
 *
 *     PPC_FUNC_IMPL_DECL(func_00010230);
 *     GOW2_FUNC_OVERRIDE(func_00010230)
 *     {
 *         ...
 *         __imp_func_00010230(ctx);   // corpo liftado original, se se quiser
 *     }
 *
 * Deliberadamente SEM `extern "C"` -- ver o contrato do simbolo acima. */
#define GOW2_FUNC_OVERRIDE(name) GOW2_FUNC_OVERRIDE_USED void name(ppu_context* ctx)

#endif /* GOW2_FUNC_OVERRIDES_H */
