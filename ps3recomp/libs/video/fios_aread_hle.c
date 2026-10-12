/*
 * ps3recomp - host-side fulfiller for GoW2's FIOS asynchronous read op.
 *
 * See fios_aread_hle.h for the op layout, its provenance, and how much of it
 * is measured versus inferred. This file holds the mechanics only.
 *
 * Called from the patched entry of func_002B3D1C (r3 = op, r4 = dst, r5 = n).
 * It runs INSIDE the guest's own submit call, on the submitting thread, before
 * that thread can ever reach the poll (func_002B45D0) -- so there is no
 * concurrent observer of the op while its fields are being written and the
 * store order below is the documented Windows order, not a release protocol.
 *
 * Two properties are worth more than the happy path:
 *
 *   ALL OR NOTHING. Bytes are read into a host bounce buffer first and only
 *   copied into the guest once the full (limit-clamped) request came back. A
 *   short or failed read is a decline that leaves guest memory byte-for-byte
 *   untouched -- it never leaves a half-filled ring behind and never marks the
 *   op done. The guest's async reads gate a synchronous waiter; releasing that
 *   waiter over a partly-written buffer hands it garbage it cannot detect.
 *
 *   DECLINING IS THE SAFE DEFAULT. Every guard returns 0 without a store, and
 *   the caller then runs the guest's natural body exactly as if the hook were
 *   not there. That is what makes the hook safe to leave compiled in: an op it
 *   does not understand is an op it does not touch.
 */

#include "fios_aread_hle.h"
#include "ps3_trace.h"   /* Task 5 (bringup-instrumentacao-fase0): subsistema FIOS */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* PS3_WATCH_STORE: watch de escritas HOST em bloco (ppu_loader.cpp). */
extern void ps3_watch_store_bulk(uint32_t ea, uint32_t len, const char* who);

/* Guest memory and its accessors. Signatures match runtime/ppu/ppu_loader.cpp
 * exactly (uint64_t EA); vm_read32/vm_write32 do the big-endian conversion, so
 * the op's 32-bit fields must go through them and never through a host-endian
 * store. The payload is a byte stream and is copied verbatim -- a byte array
 * has no endianness to get wrong. */
extern unsigned char* vm_base;
extern uint32_t       ppu_vm_size;
extern uint32_t       vm_read32(uint64_t ea);
extern void           vm_write32(uint64_t ea, uint32_t v);

/* The host data path (movie_hle.c). */
extern int      movie_io_is(unsigned fd);
extern unsigned movie_io_pread(unsigned fd, void* dst, unsigned n, unsigned pos);

/* PS3_MOVIE_IO is the product gate for the whole host data path; the hook is
 * inert without it. Latched on first use, like every other env gate in the
 * tree -- this runs once per stream read. */
static int aread_enabled(void)
{
    static int on = -1;
    if (on < 0) {
        const char* e = getenv("PS3_MOVIE_IO");
        on = (e && *e && *e != '0') ? 1 : 0;
    }
    return on;
}

static int aread_trace(void)
{
    static int on = -1;
    if (on < 0) {
        const char* e = getenv("PS3_TRACE_AREAD");
        on = (e && *e && *e != '0') ? 1 : 0;
    }
    return on;
}

/* Guest-VM range check, same shape as spu_args_ea_in_range() in
 * runtime/spu/spu_lifted_job.h: ppu_vm_size == 0 means "no address space
 * registered" (the unit harnesses) and disables the check. */
static int aread_ea_in_range(uint32_t ea, uint32_t len)
{
    if (!ea || !len) return 0;
    if (ppu_vm_size == 0) return 1;
    return (uint64_t)ea + (uint64_t)len <= (uint64_t)ppu_vm_size;
}

/* PS3_TRACE_AREAD=1 says WHY an op was declined. Without it "zero [AREAD-HLE]
 * lines" is ambiguous -- it cannot distinguish "never called" from "called and
 * refused everything in silence", which is exactly the ambiguity that cost a
 * measurement round on the sibling lfq_host_inflate fulfiller. Gated, OFF by
 * default (CLAUDE.md rule 6). */
#define AREAD_DECLINE(why) do {                                               \
    if (aread_trace()) {                                                      \
        static int nlog = 0;                                                  \
        if (nlog++ < 32) {                                                    \
            fprintf(stderr, "[AREAD-HLE-NO] %s op=0x%08X dst=0x%08X n=%u "    \
                    "fd=0x%08X\n", (why), op_ea, dst_ea, n, movie_fd);        \
            fflush(stderr);                                                   \
        }                                                                     \
    }                                                                         \
    return 0;                                                                 \
} while (0)

unsigned ps3_fios_aread_hle(uint32_t op_ea, uint32_t dst_ea, uint32_t n,
                            unsigned movie_fd)
{
    if (!aread_enabled())               AREAD_DECLINE("PS3_MOVIE_IO desligado");
    if (!vm_base)                       AREAD_DECLINE("sem vm_base");
    if (!op_ea || !dst_ea || !n)        AREAD_DECLINE("argumento nulo");
    if (n > PS3_AREAD_MAX_BYTES)        AREAD_DECLINE("n absurdo (op lixo)");
    /* Not a movie_io slot: some other stream, or an FD the caller could not
     * identify. The guest's own body owns it. */
    if (!movie_fd || !movie_io_is(movie_fd))
        AREAD_DECLINE("fd nao e token movie_io");
    if (!aread_ea_in_range(op_ea, PS3_AREAD_OP_SPAN))
        AREAD_DECLINE("op fora da VM guest");
    if (!aread_ea_in_range(dst_ea, n))
        AREAD_DECLINE("dst fora da VM guest");

    const uint32_t pos = vm_read32(op_ea + PS3_AREAD_OP_CURSOR);
    const uint32_t lim = vm_read32(op_ea + PS3_AREAD_OP_LIMIT);
    if (lim) {
        /* Cursor at or past the window end: the op has nothing left to
         * deliver, so completing it here would be inventing a transition. */
        if (pos >= lim)  AREAD_DECLINE("op esgotado (cursor >= limite)");
        if (n > lim - pos) n = lim - pos;   /* tail of the window; only shrinks */
    }

    /* Bounce first, commit second -- see the ALL OR NOTHING note up top. */
    unsigned char* bounce = (unsigned char*)malloc(n);
    if (!bounce) AREAD_DECLINE("malloc do bounce falhou");

    const unsigned got = movie_io_pread(movie_fd, bounce, n, pos);
    if (got != n) {
        free(bounce);
        /* Failed (got == 0) or short (got < n). Either way the host cannot
         * satisfy this op, so nothing is written and nothing is completed. */
        AREAD_DECLINE("leitura curta ou falhada");
    }

    memcpy(vm_base + dst_ea, bounce, got);
    /* PS3_WATCH_STORE: escrita host directa em vm_base, invisivel aos watches
     * de store (que vivem em vm_write*). Gated, OFF por default. */
    ps3_watch_store_bulk((uint32_t)dst_ea, (uint32_t)got, "aread-hle memcpy");
    free(bounce);

    /* Complete the op the way the Windows hook did, in that order: advance the
     * cursor, mark done, drop the in-flight bit. All three through vm_write32,
     * i.e. big-endian, because the guest reads them as PowerPC words. */
    vm_write32(op_ea + PS3_AREAD_OP_CURSOR, pos + got);
    vm_write32(op_ea + PS3_AREAD_OP_STATUS, PS3_AREAD_STATUS_DONE);
    vm_write32(op_ea + PS3_AREAD_OP_FLAGS,
               vm_read32(op_ea + PS3_AREAD_OP_FLAGS) & ~PS3_AREAD_FLAG_INFLIGHT);

    /* Task 5 (bringup-instrumentacao-fase0): FIOS CALLBACK -- esta funcao
     * corre inteira, submissao a conclusao, dentro da propria chamada do
     * guest (ver o cabecalho do ficheiro: "there is no concurrent observer of
     * the op"), por isso este e' o unico ponto de "op completo" que existe
     * aqui, equivalente ao callback assincrono que o poll natural do jogo
     * (func_002B45D0) so' observaria mais tarde. Sem lock: sem thread
     * concorrente possivel neste caminho. IN-BOOT (corrige a crença antiga
     * registada aqui numa revisão anterior): a run in-boot da própria Task 5
     * mediu 25 disparos de `FIOS/CALLBACK` numa janela de 40s (ver
     * `.superpowers/sdd/task-5-report.md` §7/§9), refutando a suposição de
     * `docs/superpowers/plans/2026-07-20-fios-async-read-movie-io-hle.md`
     * de que `func_002B3D1C` nunca é chamado nas janelas medidas -- ou seja,
     * o ponto de entrada desta função (patch de lift em ppu_recomp_*.cpp,
     * gitignored, fora deste repo) É alcançado pelo caminho natural do jogo.
     * Causa exacta da mudança de comportamento não investigada (fora do
     * escopo da Task 5; ver o achado no relatório). */
    PS3_TRACE(PS3_TS_FIOS, "ps3_fios_aread_hle", "CALLBACK",
              "op=0x%08X buf=0x%08X off=%u len=%u state=done", op_ea, dst_ea, pos, got);

    if (aread_trace()) {
        static int nlog = 0;
        if (nlog++ < 48) {
            fprintf(stderr, "[AREAD-HLE] op=0x%08X dst=0x%08X pos=%u n=%u "
                    "got=%u fd=0x%08X\n", op_ea, dst_ea, pos, n, got, movie_fd);
            fflush(stderr);
        }
    }
    return got;
}
