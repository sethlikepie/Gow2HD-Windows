/*
 * ps3recomp - ps3_mask_trace. Ver include/ps3_mask_trace.h para o contrato.
 *
 * Vive em runtime/trace/ pelo mesmo motivo de ps3_trace.c / ps3_mem_trace.c:
 * o CMakeLists.txt faz GLOB_RECURSE de runtime dot-c e so' exclui tests/,
 * runtime/ppu/ e runtime/host/ -- um .c aqui entra na lib sem tocar no CMake,
 * e compila standalone para o teste unitario.
 */
#include "ps3_mask_trace.h"
#include "ps3_trace.h"
#include "../memory/vm.h"

/* dladdr()/<dlfcn.h> sao POSIX-only. Mesmo tratamento de ps3_mem_trace.c:
 * em Windows o campo <fn> fica "-" em vez de partir o build da lib
 * (CMakeLists.txt:118-129 mete este ficheiro na lib em TODAS as plataformas).
 * __sync_add_and_fetch nao precisa de guard: e' builtin GCC/Clang disponivel
 * em MinGW, ja documentado em ps3_checkpoint.c. */
#ifndef _WIN32
#include <dlfcn.h>
#endif
#include <stdlib.h>
#include <stdio.h>

static const char* const k_mask_names[] = {
    "uncommitted_guard",   /* PS3_MASK_UNCOMMITTED */
    "tblsize_guard",       /* PS3_MASK_TBLSIZE     */
};
static const char* const k_mask_env[] = {
    "PS3_MASK_OFF_UNCOMMITTED",
    "PS3_MASK_OFF_TBLSIZE",
};

/* Falha a compilar se alguem acrescentar ao enum sem actualizar as tabelas. */
_Static_assert(sizeof(k_mask_names) / sizeof(k_mask_names[0]) == PS3_MASK__COUNT,
               "k_mask_names desalinhada com ps3_mask_id");
_Static_assert(sizeof(k_mask_env) / sizeof(k_mask_env[0]) == PS3_MASK__COUNT,
               "k_mask_env desalinhada com ps3_mask_id");

const char* ps3_mask_name(ps3_mask_id mask)
{
    if ((unsigned)mask >= (unsigned)PS3_MASK__COUNT) return "?";
    return k_mask_names[mask];
}

int ps3_mask_disabled(ps3_mask_id mask)
{
    if ((unsigned)mask >= (unsigned)PS3_MASK__COUNT) return 0;

    /* Cache por mascara: -1 = por resolver. Mesmo idioma dos gates de
     * ps3_trace.c; a corrida check-then-set e' benigna (dois escritores
     * calculam o mesmo valor) e espelha o padrao ja existente no repo. */
    static int s_state[PS3_MASK__COUNT] = { -1, -1 };
    int v = s_state[mask];
    if (v < 0) {
        const char* e = getenv(k_mask_env[mask]);
        v = (e && e[0] && e[0] != '0') ? 1 : 0;
        s_state[mask] = v;
    }
    return v;
}

void ps3_mask_swallowed_report(ps3_mask_id mask, uint32_t ea, uint32_t len,
                               const char* op, void* caller_ra)
{
    /* Gate primeiro: no-op estrito (G2) -- nada formatado, nada contado. */
    if (!ps3_trace_on(PS3_TS_MEMORY)) return;
    if ((unsigned)mask >= (unsigned)PS3_MASK__COUNT) return;

    /* Contador por mascara, para o cap de uma nao consumir o da outra. */
    static uint64_t s_count[PS3_MASK__COUNT];
    uint64_t n = __sync_add_and_fetch(&s_count[mask], (uint64_t)1);

    int emit_line = (n <= (uint64_t)PS3_MASK_SWALLOWED_CAP);
    /* Agenda de crescimento GEOMETRICA -- deliberadamente DIFERENTE da linear
     * do MEMORY/OOB (ps3_mem_trace.c, marco a cada CAP eventos). A Task 2
     * mediu 4.163.585 engolimentos do uncommitted_guard num unico boot de
     * ~1min; a agenda linear teria produzido 65.056 linhas TRUNCATED so'
     * desta mascara -- exactamente o volume que a Task 7 (baseline com os
     * dez subsistemas juntos) nao pode admitir, porque afoga o sinal dos
     * outros nove. Emite no primeiro marco (n = CAP+1, ainda preso ao corte
     * linear original) e dai em diante so' em potencias de dois (128, 256,
     * 512, ...). Para os mesmos 4,1M de eventos isto da ~16 linhas em vez de
     * 65 mil. O total lido continua um LIMITE INFERIOR, mas o erro relativo
     * passa a ir ate quase 2x (o valor real pode estar em qualquer ponto
     * entre um marco e o dobro dele) em vez do erro aditivo maximo de CAP-1
     * da agenda linear -- troca aceite: para "milhoes vs milhares" um factor
     * 2x nao muda a classificacao, e o volume de log e' o que importa aqui. */
    int emit_trunc = 0;
    if (!emit_line) {
        uint64_t first_mark = (uint64_t)PS3_MASK_SWALLOWED_CAP + 1u;
        if (n == first_mark || (n > first_mark && (n & (n - 1u)) == 0u))
            emit_trunc = 1;
    }
    if (!emit_line && !emit_trunc) return;

    const char* fn = NULL;
#ifndef _WIN32
    Dl_info di;
    char fn_buf[256];
    if (caller_ra && dladdr(caller_ra, &di) && di.dli_sname) {
        unsigned long off = (unsigned long)((const char*)caller_ra -
                                            (const char*)di.dli_saddr);
        snprintf(fn_buf, sizeof(fn_buf), "%s+0x%lX", di.dli_sname, off);
        fn = fn_buf;
    }
#else
    (void)caller_ra;
#endif

    if (emit_trunc) {
        ps3_trace_emit(PS3_TS_MEMORY, fn, "SWALLOWED_TRUNCATED",
                       "mask=%s count=%llu",
                       k_mask_names[mask], (unsigned long long)n);
        return;
    }

    ps3_trace_emit(PS3_TS_MEMORY, fn, "SWALLOWED",
                   "ea=0x%08X len=%u mask=%s op=%s region=%s",
                   ea, len, k_mask_names[mask],
                   (op && op[0]) ? op : "-", vm_region_name(ea));
}
