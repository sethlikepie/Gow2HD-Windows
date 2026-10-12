/*
 * ps3recomp - ps3_mem_trace: implementacao do evento canonico MEMORY/OOB.
 * Ver include/ps3_mem_trace.h para o contrato.
 *
 * Vive em runtime/trace/ pelo mesmo motivo de ps3_trace.c/ps3_checkpoint.c
 * (comentario de cabecalho la): o CMakeLists.txt faz GLOB_RECURSE de runtime
 * dot-c e so' exclui tests/, runtime/ppu/ e runtime/host/ -- um .c aqui entra
 * na libps3recomp_runtime.a sem tocar no CMakeLists.txt. runtime/memory/vm.h
 * (para vm_region_name()) e' game-agnostic tambem (so' constantes do mapa de
 * enderecos + dladdr), por isso este ficheiro nao arrasta nada especifico de
 * jogo -- ao contrario de runtime/ppu/ppu_loader.cpp, que inclui o
 * ppu_recomp.h GERADO por jogo e por isso nao compila standalone (motivo
 * pelo qual esta logica foi extraida para aqui: ver o teste unitario em
 * runtime/trace/tests/test_ps3_mem_trace.c).
 */
#include "ps3_mem_trace.h"
#include "ps3_trace.h"
#include "../memory/vm.h"

/* Important 1 (review pos-Task 3): dladdr()/<dlfcn.h> sao POSIX-only -- nao
 * existem em MSVC nem em mingw-w64 stock. Este ficheiro NAO e' excluido do
 * GLOB_RECURSE de runtime dot-c em CMakeLists.txt (so' tests/, runtime/ppu/ e
 * runtime/host/ ficam de fora), por isso entra em libps3recomp_runtime.a em
 * TODAS as plataformas, incluindo Windows -- um #include<dlfcn.h> desguardado
 * aqui quebrava esse build. Guardado com o mesmo padrao ja usado em
 * runtime/syscalls/sys_memory.c:50-59 (#ifdef _WIN32 ... #else ... #endif).
 * Em Windows fn fica sempre NULL (<fn> renderiza "-") -- ver
 * ps3_mem_oob_report() abaixo. __sync_add_and_fetch (usado mais abaixo) NAO
 * precisa deste guard: e' um builtin GCC/Clang disponivel em MinGW, ja'
 * documentado como tal em ps3_checkpoint.c:45. */
#ifndef _WIN32
#include <dlfcn.h>
#endif
#include <stdio.h>

void ps3_mem_oob_report(uint32_t ea, uint32_t len, const char* op, void* caller_ra)
{
    if (!ps3_trace_on(PS3_TS_MEMORY)) {
        return;
    }

    static uint64_t s_count = 0;
    uint64_t n = __sync_add_and_fetch(&s_count, (uint64_t)1);

    int emit_oob       = (n <= (uint64_t)PS3_MEM_OOB_CANONICAL_CAP);
    /* Important 2 (review pos-Task 3): agenda de crescimento em vez de uma
     * unica linha OOB_TRUNCATED presa para sempre em count=CAP+1. A versao
     * original so' emitia em n==CAP+1 e depois retornava cedo para todo n
     * maior -- s_count continuava a somar, mas nada lia esse total, e acima
     * do cap o "numero de OOB" (uma das dez metricas do baseline Fase 0)
     * ficava irrecuperavel do trace, exactamente o cenario de regressao que
     * este subsistema existe para apanhar. Fix: re-emite a cada CAP eventos
     * ALEM do primeiro corte (n = CAP+1, 2*CAP+1, 3*CAP+1, ...) -- "growth
     * schedule" tipo B do achado da review (a alternativa aceite, total
     * final no exit, exigiria atexit()/estado extra de processo; esta e' so'
     * aritmetica inteira, mesma granularidade do cap ja documentado, sem
     * custo extra). Staleness maximo entre a ultima linha e o total real:
     * CAP-1 eventos -- aceitavel para uma metrica de regressao, nao para
     * contagem exacta byte-a-byte. */
    int emit_truncated = !emit_oob &&
        ((n - (uint64_t)PS3_MEM_OOB_CANONICAL_CAP - 1) % (uint64_t)PS3_MEM_OOB_CANONICAL_CAP == 0);

    if (!emit_oob && !emit_truncated) {
        return; /* corte silencioso entre marcos: s_count continua a somar */
    }

    const char* fn = NULL;
#ifdef _WIN32
    (void)caller_ra; /* Important 1: sem dladdr() em Windows, fn fica "-" */
#else
    char fn_buf[128];
    Dl_info di;
    if (caller_ra != NULL && dladdr(caller_ra, &di) && di.dli_sname) {
        snprintf(fn_buf, sizeof(fn_buf), "%s+0x%lX", di.dli_sname,
                 (unsigned long)((const char*)caller_ra - (const char*)di.dli_saddr));
        fn = fn_buf;
    }
#endif

    if (emit_oob) {
        ps3_trace_emit(PS3_TS_MEMORY, fn, "OOB", "ea=0x%08X len=%u region=%s op=%s",
                       ea, len, vm_region_name(ea), op);
    } else {
        ps3_trace_emit(PS3_TS_MEMORY, fn, "OOB_TRUNCATED", "count=%llu",
                       (unsigned long long)n);
    }
}
