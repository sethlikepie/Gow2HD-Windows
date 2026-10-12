/*
 * ps3recomp - ps3_mask_trace: observacao das mascaras de memoria sempre-ligadas.
 *
 * PROBLEMA: o motor tem guards que ENGOLEM acessos guest silenciosamente (ou
 * com log capado e nao-canonico). Nao sabemos quantos engolem, nem de que
 * funcoes guest vem. Este modulo mede isso SEM alterar a decisao (G3): quem
 * decide engolir continua a decidir.
 *
 * Emite no subsistema MEMORY, que ja existe -- o plano de origem fixa dez
 * subsistemas, nao se cria um decimo-primeiro.
 */
#ifndef PS3_MASK_TRACE_H
#define PS3_MASK_TRACE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Ordem e' o indice das tabelas internas de nome e de gate. Acrescentar SO'
 * antes de PS3_MASK__COUNT, e actualizar as duas tabelas em ps3_mask_trace.c
 * (ha um _Static_assert de contagem la que falha se esquecer). */
typedef enum {
    PS3_MASK_UNCOMMITTED = 0,   /* vm_uncommitted_guard  (ppu_loader.cpp:619) */
    PS3_MASK_TBLSIZE,           /* tblsize_guard         (ppu_loader.cpp:1080) */
    PS3_MASK__COUNT
} ps3_mask_id;

/* Cap de linhas SWALLOWED completas por mascara -- mesmo VALOR do
 * MEMORY/OOB da Fase 0 (PS3_MEM_OOB_CANONICAL_CAP), para o corte inicial
 * ser previsivel quando os dez subsistemas correm juntos no baseline.
 * A AGENDA de crescimento acima do cap ja NAO e' a mesma: a Task 2 mediu
 * 4,1M de engolimentos numa unica mascara num boot de ~1min -- a agenda
 * linear do OOB (marco a cada CAP eventos) dava dezenas de milhares de
 * linhas so' desta mascara, afogando o sinal das outras nove. Ver
 * ps3_mask_trace.c para a agenda GEOMETRICA usada aqui. */
#define PS3_MASK_SWALLOWED_CAP 64

/*
 * Emite uma linha canonica G1:
 *   [PS3T] <tb> | <thr> | <fn> | MEMORY | SWALLOWED |
 *          ea=0x%08X len=%u mask=<nome> op=<op> region=<nome>
 *
 * No-op estrito se PS3_TRACE_MEMORY estiver desligada: nao formata, nao conta,
 * nao aloca. `caller_ra` e' resolvido por dladdr para o simbolo da funcao
 * guest liftada (as funcoes do lift sao simbolos globais T no boot_gow2);
 * NULL ou nao-resolvivel -> campo <fn> = "-".
 * `op` e' um token curto sem espacos: "read" ou "write".
 */
void ps3_mask_swallowed_report(ps3_mask_id mask, uint32_t ea, uint32_t len,
                               const char* op, void* caller_ra);

/*
 * 1 se a mascara deve ser DESLIGADA nesta execucao, i.e. se
 * PS3_MASK_OFF_<NOME> esta definida e o primeiro caracter nao e' '\0' nem '0'.
 * Omissao (variavel por definir) = 0 = mascara LIGADA = comportamento actual.
 * Resolvido uma vez e cacheado; sem getenv no caminho quente.
 */
int ps3_mask_disabled(ps3_mask_id mask);

/* Exposto para o teste unitario conferir a tabela contra o enum. */
const char* ps3_mask_name(ps3_mask_id mask);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* PS3_MASK_TRACE_H */
