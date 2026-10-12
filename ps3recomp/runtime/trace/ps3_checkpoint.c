/*
 * ps3recomp - ps3_checkpoint: implementacao do CHECKPOINTS/SNAPSHOT.
 * Ver include/ps3_checkpoint.h para o contrato.
 *
 * Vive em runtime/trace/ pelo mesmo motivo de ps3_trace.c (comentario de
 * cabecalho la): o CMakeLists.txt faz GLOB_RECURSE de runtime dot-c e so'
 * exclui tests/, runtime/ppu/ e runtime/host/ -- um .c aqui entra na
 * libps3recomp_runtime.a sem tocar no CMakeLists.txt.
 */
#include "ps3_checkpoint.h"
#include "ps3_trace.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>

_Static_assert(PS3_CP__COUNT == 13, "ps3_checkpoint: a Task 2 exige exactamente 13 checkpoints");

static const char* const k_checkpoint_names[PS3_CP__COUNT] = {
    "ELF_LOADED",
    "VM_READY",
    "TLS_INIT",
    "GUEST_ENTRY",
    "FIRST_THREAD",
    "FIRST_FS_OPEN",
    "FIRST_WAD_OPEN",
    "FIRST_SPU_JOB",
    "FIRST_DRAW",
    "FIRST_FLIP",
    "FIRST_PRESENT",
    "MOVIE_EOS",
    "SHUTDOWN",
};

const char* ps3_checkpoint_name(ps3_checkpoint_id id)
{
    if ((unsigned)id >= (unsigned)PS3_CP__COUNT) {
        return "?";
    }
    return k_checkpoint_names[id];
}

/* Once-guard per-checkpoint (requisito "Ordering" do brief: o guard e' POR
 * CHECKPOINT, nunca global). 0 = ainda nao disparou, 1 = ja disparou.
 * __sync_bool_compare_and_swap (GCC/Clang builtin, disponivel em MinGW e em
 * Apple clang sem <intrin.h> -- ver CLAUDE.md) garante que, se dois call
 * sites do MESMO checkpoint correrem em threads diferentes (ex.:
 * FIRST_FS_OPEN tem sitio em runtime/ppu/ppu_fs.cpp E em
 * libs/filesystem/cellFs.c — ver relatorio da Task 2), exactamente UM
 * dispara a linha. */
static int s_fired[PS3_CP__COUNT];

/* ps3_timebase_now(): mesma declaracao extern de ps3_trace.c (definido de
 * verdade em runtime/syscalls/sys_timer.c:39; nao ha header leve que o
 * exponha -- ver o comentario equivalente em ps3_trace.c). Usado aqui so'
 * para o par tb= dentro do <data> do SNAPSHOT (requisito 4); o proprio G1
 * <tb> de cada linha ja vem de dentro de ps3_trace_emit(). */
extern uint64_t ps3_timebase_now(void);

/* Os tres accessors de SNAPSHOT (ps3_ppu_thread_alive_count/ps3_sys_memory_used/
 * ps3_gcm_flip_count) estao declarados em ps3_checkpoint.h (review pos-Task 2 --
 * ver o comentario la para o porque de ja nao serem `extern` soltos aqui). */

/* ---------------------------------------------------------------------------
 * Sanitizacao de <data> (achado G1 da review pos-Task 2, exemplo real:
 * "name=fios mediathread" quebra um parser k=v que faz split em espaco) --
 * a implementacao vivia aqui ate' a Task 3, que a PROMOVEU para o nucleo
 * (sanitize_data_kv(), static em runtime/trace/ps3_trace.c), aplicada
 * automaticamente por TODO ps3_trace_emit(). Este modulo ja' nao tem copia
 * propria: o `data` composto pelo vsnprintf abaixo vai para ps3_trace_emit()
 * tal como sai do vsnprintf, e sai sanitizado do outro lado -- ver o
 * comentario grande em include/ps3_checkpoint.h (contrato) e o cabecalho de
 * ps3_trace.c (motivo da promocao). */

#define PS3_CHECKPOINT_DATA_MAX 512

void ps3_checkpoint_fire(ps3_checkpoint_id id, const char* fn, const char* fmt, ...)
{
    if ((unsigned)id >= (unsigned)PS3_CP__COUNT) {
        return;
    }
    if (!ps3_trace_on(PS3_TS_CHECKPOINTS)) {
        return; /* G2: CHECKPOINTS desligado -> nem checkpoint nem snapshot */
    }
    /* Pre-check relaxado (review pos-Task 2, achado do custo per-call): uma
     * vez disparado, TODA chamada subsequente ao mesmo `id` (hot sites: por
     * draw/flip, e o poll do guest ao byte EOS em vm_read8) cai aqui com uma
     * leitura simples em vez de pagar o CAS -- que e' uma barreira completa --
     * para sempre. O CAS abaixo continua a ser a fonte de verdade para a
     * PRIMEIRA corrida entre threads (esta leitura e' so' uma otimizacao,
     * nunca decide sozinha "ja disparou"). */
    if (__atomic_load_n(&s_fired[id], __ATOMIC_RELAXED)) {
        return;
    }
    if (__sync_bool_compare_and_swap(&s_fired[id], 0, 1) == 0) {
        return; /* ja disparou (por este ou outro call site) */
    }

    const char* name = k_checkpoint_names[id];
    char data[PS3_CHECKPOINT_DATA_MAX];

    if (fmt == NULL || fmt[0] == '\0') {
        ps3_trace_emit(PS3_TS_CHECKPOINTS, fn, name, NULL);
    } else {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(data, sizeof(data), fmt, ap);
        va_end(ap);
        /* Sanitizacao de <data> (espacos "soltos" dentro de um valor, '|')
         * ja' acontece dentro de ps3_trace_emit() -- ver a nota da Task 3 no
         * cabecalho de ps3_trace.c; este modulo nao precisa de a repetir. */
        ps3_trace_emit(PS3_TS_CHECKPOINTS, fn, name, "%s", data);
    }

    if (ps3_trace_on(PS3_TS_SNAPSHOT)) {
        uint64_t tb      = ps3_timebase_now();
        uint32_t threads = ps3_ppu_thread_alive_count();
        uint32_t heap    = ps3_sys_memory_used();
        uint32_t flips   = ps3_gcm_flip_count();
        ps3_trace_emit(PS3_TS_SNAPSHOT, fn, name,
                       "tb=%llu threads=%u heap_used=%u last_flip=%u",
                       (unsigned long long)tb, threads, heap, flips);
    }
}
