/*
 * ps3recomp - ps3_mem_trace: subsistema MEMORY, evento canonico OOB (Task 3
 * do plano "bringup-instrumentacao-fase0").
 *
 * Extraido de runtime/ppu/ppu_loader.cpp (onde vive o unico caller real,
 * vm_read8/16/32/64 e vm_write8/16/32/64) para um modulo game-agnostic
 * proprio, pelo MESMO motivo que levou a Task 2 a centralizar
 * ps3_checkpoint_fire() em vez de PS3_TRACE() directo em cada call site:
 * ppu_loader.cpp inclui ppu_recomp.h (gerado por jogo) e nao compila
 * standalone, por isso a logica de cap/truncamento/resolucao de <fn> tem de
 * viver num ficheiro que NAO dependa disso, para poder ser testada
 * offline-unit (ver runtime/trace/tests/test_ps3_mem_trace.c) exactamente
 * como e' exercitada no boot real.
 *
 * Contrato da linha G1 emitida (subsistema MEMORY, evento OOB):
 *
 *   [PS3T] <tb> | <thr> | <fn> | MEMORY | OOB | ea=0x%08X len=%u region=%s op=<read|write>
 *
 * region=: nome da regiao guest que DEVERIA conter `ea` (vm_region_name(),
 * runtime/memory/vm.h) ou "unmapped".
 * fn=: resolvido via dladdr() do return address do CHAMADOR direto de
 * vm_read* / vm_write* (a funcao guest liftada que fez o acesso) -- "-" se
 * dladdr nao encontrar simbolo (ex.: endereco fora de qualquer modulo
 * carregado, ou um ponteiro sintetico de teste).
 */
#ifndef PS3_MEM_TRACE_H
#define PS3_MEM_TRACE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Cap EXPLICITO (requisito da ambiguidade resolvida no brief da Task 3: a
 * linha canonica MEMORY/OOB nao pode herdar em silencio o `logged<40` de
 * "[vm] OOB access" em ppu_loader.cpp:395 -- esse e' local aquela linha
 * antiga, G3, inalterado). Os primeiros PS3_MEM_OOB_CANONICAL_CAP eventos
 * reais (com ea=/len=/region=/op=) saem um a um; o (CAP+1)-esimo dispara a
 * PRIMEIRA linha OOB_TRUNCATED (count=CAP+1). Dai' em diante, o total real
 * continua RECUPERAVEL do trace: a cada CAP eventos adicionais (Important 2,
 * review pos-Task 3 -- n = 2*CAP+1, 3*CAP+1, ...) sai mais uma linha
 * OOB_TRUNCATED com o total acumulado ate' aquele ponto; entre marcos o
 * contador continua a somar em silencio (staleness maximo CAP-1 eventos).
 * Isto substitui o comportamento original (uma unica linha, congelada em
 * count=CAP+1 para sempre), que tornava o "numero de OOB" -- uma das dez
 * metricas do baseline Fase 0 -- irrecuperavel do trace acima do cap.
 * Exposto para o teste unitario poder disparar exactamente CAP+1 (ou mais)
 * chamadas sem duplicar o numero. */
#define PS3_MEM_OOB_CANONICAL_CAP 64

/*
 * ps3_mem_oob_report -- chamar a partir de um acesso real de memoria guest
 * QUANDO ja se sabe que o acesso e' out-of-bounds (ex.: vm_oob() devolveu 1
 * em ppu_loader.cpp). Gate interno em ps3_trace_on(PS3_TS_MEMORY) -- chamar
 * sempre que houver um OOB de verdade, sem checar o gate no call site (mesmo
 * principio de PS3_TRACE()); o CALLER e' quem decide que houve OOB.
 *
 * ea/len: o endereco/tamanho do acesso que falhou.
 * op: literal "read" ou "write" (G1 do plano de origem, campo op=<read|write>).
 * caller_ra: __builtin_return_address(0) do chamador direto de
 * vm_read* / vm_write* -- usado so' para resolver <fn> via dladdr(); pode ser
 * NULL/invalido (fn cai em "-").
 */
void ps3_mem_oob_report(uint32_t ea, uint32_t len, const char* op, void* caller_ra);

#ifdef __cplusplus
}
#endif

#endif /* PS3_MEM_TRACE_H */
