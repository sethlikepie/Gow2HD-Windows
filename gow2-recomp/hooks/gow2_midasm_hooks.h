/* gow2_midasm_hooks.h -- declaracoes dos mid-asm hooks HOST do GoW2.
 *
 * PORQUE ESTE FICHEIRO EXISTE
 * ---------------------------
 * Fase 17 (marco v1.3). Um `[[midasm_hook]]` declarado no
 * games/gow2/config/gow2_recomp.toml faz o ppu_lifter.py emitir, ao lado da
 * instrucao ancorada, uma chamada `gow2_midasm_<Name>(ctx);` (plano 17-01,
 * tools/ppu_lifter.py: MIDASM_SYMBOL_PREFIX / _midasm_lines / _preamble_lines).
 * O corpo dessa funcao vive AQUI: codigo host versionado e compilado, em vez
 * das linhas injectadas por um patch_*.py de texto que um re-lift apaga.
 *
 * O CONTRATO DO SIMBOLO (fixado e verificado no 17-01, nao inventado aqui)
 * -----------------------------------------------------------------------
 *   - assinatura: `void gow2_midasm_<Name>(ppu_context* ctx)` -- um so'
 *     parametro, o contexto guest;
 *   - `extern "C"` (o lifter emite a declaracao dentro de um `extern "C"`
 *     condicionado a __cplusplus; ver ppu_lifter.py::_preamble_lines);
 *   - SEM prefixo de simbolo (--symbol-prefix): os hooks sao simbolos host,
 *     nao funcoes liftadas.
 * A TU gerada ja' traz a sua propria declaracao no preambulo, por isso o lift
 * NAO precisa de incluir este header -- ele existe para o .cpp dos hooks e
 * para qualquer codigo host que lhes queira chamar. As duas declaracoes tem
 * de casar; se divergirem, o build parte (e e' esse o objectivo).
 *
 * NOTA SOBRE `ppu_context`: a TU liftada usa o typedef que o lifter escreve em
 * ppu_recomp.h; este header usa o canonico do motor (runtime/ppu/ppu_context.h,
 * alcancavel pelo -I do build_macos.sh). Sao dois typedefs distintos ao nivel
 * da linguagem mas com layout identico campo-a-campo -- verificado a
 * 2026-08-02 -- e, por serem `extern "C"`, o simbolo nao leva os tipos, logo
 * o link e' o mesmo. E' o mesmo arranjo que o ppu_loader do motor ja' usa.
 *
 * NAO METER AQUI: probes gated por env var com escritor proprio (esses
 * continuam patch_*.py, por decisao do 17-CONTEXT), nem overrides de funcao
 * inteira (isso e' weak override, Fase 19).
 */
#ifndef GOW2_MIDASM_HOOKS_H
#define GOW2_MIDASM_HOOKS_H

#include "ppu_context.h"

#ifdef __cplusplus
extern "C" {
#endif

/* CE03C wait-idle da intro (EA guest 0x000CE03C, piloto da Fase 17).
 * P0 no ledger games/gow2/lift_baseline/PATCH_MIGRATION.tsv; ate' ao plano
 * 17-03 era TEXTO INJECTADO no lift por
 * recomp_mid_v2/patch_ce03c_introseq_block.py, que um re-lift limpo apagava.
 * Desde o 17-03 o corpo -- pump ate' ao MovieStop natural, arm do +0x714,
 * limpeza do sticky EOS e movie_done_timebased_reset -- vive no .cpp ao lado,
 * e a entrada [[midasm_hook]] address = 0x000CE03C esta' em
 * games/gow2/config/gow2_recomp.toml.
 *
 * NAO cobre o pad de setjmp/longjmp (g_ce03c_play_abort) que o patch punha a'
 * volta do corpo natural: um mid-asm corre AO LADO de uma instrucao, nao
 * ENVOLVE a funcao. Ver o comentario do corpo no .cpp e o 17-03-SUMMARY.md. */
void gow2_midasm_Ce03cWaitIdle(ppu_context* ctx);

/* ---------------------------------------------------------------------------
 * SONDA E1 -- o walk de tipos da parede 4. Gated por PS3_TYPEWALK_TRACE=1,
 * OFF por default (G2: sem a variavel, zero bytes e zero mudanca de
 * comportamento -- o corpo le um int cacheado e devolve).
 *
 * NAO E' UM FIX. E' observacao. Nenhum dos dois hooks escreve na memoria
 * guest, nem toca em ctx->gpr/cr/ctr/lr -- so' le'. Ver
 * docs/re_sessions/2026-08-03-E1-typewalk-trace.md para as duas previsoes
 * (escritas antes de correr) e o desenho.
 * ------------------------------------------------------------------------ */

/* E3 -- a sonda de CONTROLO. EA guest 0x0041F700 (primeira instrucao de
 * func_0041F700, o `vt+0x40` push da vtable do gestor 0x00515AA0). Conta
 * entradas. Sem ela, um silencio nao distingue "nao passou aqui" de "a sonda
 * nao foi compilada" -- foi assim que as Fases 7-10 mediram uma string
 * inexistente. */
void gow2_midasm_TypewalkPushEnter(ppu_context* ctx);

/* A MEDICAO. EA guest 0x0041F78C -- `rlwinm r9,r0,2,14,29` (low16*4), dentro
 * do anel do walk, ANTES da guarda de poda. Nesse ponto r0=w0(filho),
 * r10=h(filho), r4=q(filho), r3=this, r28=tab, r29=subtag(pai),
 * r30=h(pai)+0x80, r31=no' de lista. Discriminador: tab[low16] == tab[subtag]? */
void gow2_midasm_TypewalkPushChild(ppu_context* ctx);

/* O POP. EA guest 0x0041F924 (`vt+0x44` da mesma vtable). r3 = o gestor em que
 * o pop foi chamado, cursor +0xC8 ainda NAO decrementado. Existe porque a nota
 * de 2026-08-02 mediu 27 539 083 passagens pelo walk contra 2 088 pops: contar
 * os dois lados na MESMA corrida e' a unica forma de saber se a desproporcao
 * e' desta corrida. */
void gow2_midasm_TypewalkPop(ppu_context* ctx);

/* ---------------------------------------------------------------------------
 * SONDA E3 -- a contagem que decide D-11.2 (fix A vs fix B da parede 4).
 * Gated por PS3_OUTER_SUBTAG_CENSUS=1, OFF por default (G2). NAO E' UM FIX:
 * so' le' -- nao escreve memoria guest, nem toca em ctx->gpr/cr/ctr/lr. Ver
 * docs/re_sessions/2026-08-04-E3-outer-walk-subtag-census.md para a pergunta,
 * as ancoras e as duas previsoes (escritas ANTES deste hook existir).
 * ------------------------------------------------------------------------ */

/* EA guest 0x0042003C -- `lwz r27, 0x88(r28)` dentro de func_0041FF70 (o walk
 * exterior), imediatamente APOS os dois filtros de flags (flags&0x10,
 * flags&3) e o gate "tem filho" terem passado. Nesse ponto ctx->gpr[28] e' o
 * `piVar8` do C do Ghidra -- o wrapper de registo que a instrucao seguinte
 * vai desreferenciar em +0x88. O hook le' o MESMO valor, sem alterar o que a
 * instrucao traduzida faz a seguir. Discriminador: quantos desses wrappers,
 * com subtag != 1, chegam aqui numa corrida? */
void gow2_midasm_OuterWalkSubtagCensus(ppu_context* ctx);

#ifdef __cplusplus
}
#endif

#endif /* GOW2_MIDASM_HOOKS_H */
