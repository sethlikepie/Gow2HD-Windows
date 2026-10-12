/*
 * ps3recomp - ps3_checkpoint: subsistemas CHECKPOINTS e SNAPSHOT (Task 2 do
 * plano "bringup-instrumentacao-fase0").
 *
 * Marca a espinha do boot com uma lista FECHADA de 13 checkpoints, cada um
 * emitido no MAXIMO uma vez por run (once-guard por checkpoint, nao global —
 * ver requisito "Ordering" do brief da Task 2). Constroi em cima do nucleo
 * ps3_trace (Task 1): cada checkpoint e' uma linha canonica G1 no subsistema
 * CHECKPOINTS; se PS3_TRACE_SNAPSHOT=1, essa linha e' imediatamente seguida
 * de uma segunda linha no subsistema SNAPSHOT com o mesmo <event> e um
 * <data> com pelo menos tb=/threads=/heap_used=/last_flip= (requisito 4).
 *
 * Porque um modulo central (ps3_checkpoint.c) e nao PS3_TRACE() direto em
 * cada call site:
 *   1. O once-guard tem de ser um unico estado partilhado por checkpoint —
 *      alguns checkpoints (FIRST_FS_OPEN) tem mais que um call site real no
 *      motor (ver runtime/ppu/ppu_fs.cpp vs libs/filesystem/cellFs.c,
 *      documentado no relatorio da Task 2); um guard `static int` local a
 *      cada call site nao serviria — o primeiro que correr tem de bloquear
 *      TODOS os outros para o mesmo checkpoint.
 *   2. O pareamento checkpoint+snapshot (requisito 4) fica atomico num unico
 *      ponto em vez de cada call site ter de lembrar-se de o fazer.
 *
 * Os contadores de SNAPSHOT (threads/heap_used/last_flip) sao fornecidos por
 * accessors `extern` fracamente acoplados (ver ps3_checkpoint.c) para o teste
 * unitario poder stub'ar os tres sem arrastar sys_ppu_thread.c/sys_memory.c/
 * cellGcmSys.c inteiros — mesmo principio do stub de ps3_timebase_now() em
 * test_ps3_trace.c.
 */
#ifndef PS3_CHECKPOINT_H
#define PS3_CHECKPOINT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Lista fechada, na ordem exacta da tabela do brief da Task 2. O indice nao
 * e' contrato externo (ao contrario de ps3_trace_subsys em G2) — apenas
 * identifica a entrada no array de once-guards e de nomes internos a este
 * modulo. */
typedef enum {
    PS3_CP_ELF_LOADED = 0,
    PS3_CP_VM_READY,
    PS3_CP_TLS_INIT,
    PS3_CP_GUEST_ENTRY,
    PS3_CP_FIRST_THREAD,
    PS3_CP_FIRST_FS_OPEN,
    PS3_CP_FIRST_WAD_OPEN,
    PS3_CP_FIRST_SPU_JOB,
    PS3_CP_FIRST_DRAW,
    PS3_CP_FIRST_FLIP,
    PS3_CP_FIRST_PRESENT,
    PS3_CP_MOVIE_EOS,
    PS3_CP_SHUTDOWN,
    PS3_CP__COUNT
} ps3_checkpoint_id;

/* Token <event> exacto (== nome do checkpoint) para `id`. Exposto para o
 * teste unitario e para quem quiser logar sem duplicar a tabela. */
const char* ps3_checkpoint_name(ps3_checkpoint_id id);

/* ---------------------------------------------------------------------------
 * Contadores de SNAPSHOT (requisito 4: tb=, threads=, heap_used=, last_flip=).
 *
 * Declarados AQUI (review pos-Task 2, achado do extern duplicado) em vez de
 * `extern` solto dentro de ps3_checkpoint.c: as tres funcoes sao definidas em
 * sys_ppu_thread.c/sys_memory.c/libs/video/cellGcmSys.c, cada um dos quais ja
 * inclui este header (sys_memory.c precisa de o incluir explicitamente para
 * o mesmo efeito -- ver esse ficheiro). Com a declaracao centralizada aqui,
 * um TU que defina uma das tres com assinatura divergente da deste header
 * apanha um erro de COMPILACAO (tipos conflitantes), nao UB silenciosa entre
 * TUs -- a razao dada antes (stub fracamente ligado) nao se aplicava: um
 * `#include` do header nao arrasta a definicao, so' a declaracao, e por isso
 * nao obriga o teste standalone (test_ps3_checkpoint.c, que fornece os seus
 * proprios stubs) a linkar o runtime inteiro.
 *
 * threads=    numero de threads PPU guest vivas (estado != FREE), de
 *             sys_ppu_thread.c (g_ppu_threads[]). Lock-free (ver o comentario
 *             de ps3_ppu_thread_alive_count() em sys_ppu_thread.c para o
 *             porque -- achado do deadlock de boot do review).
 * heap_used=  total actualmente alocado pelo allocador de sys_memory (LV2
 *             sys_memory_allocate/free), de sys_memory.c (s_total_allocated).
 *             Nao e' um malloc-heap generico do guest (o runtime nao tem um);
 *             e' o valor real mais proximo disponivel.
 * last_flip=  contador de cellGcmSetFlipCommand bem sucedidos ate agora, de
 *             libs/video/cellGcmSys.c. So' incrementa quando SNAPSHOT esta
 *             ligado (o unico consumidor) e usa incremento atomico (mais de
 *             um host thread pode chamar cellGcmSetFlipCommand). */
uint32_t ps3_ppu_thread_alive_count(void);
uint32_t ps3_sys_memory_used(void);
uint32_t ps3_gcm_flip_count(void);

/* Dispara o checkpoint `id` se (a) PS3_TRACE_CHECKPOINTS estiver ligado e
 * (b) este for o PRIMEIRO disparo de `id` neste processo (once-guard
 * per-checkpoint, thread-safe via troca atomica). Chamadas subsequentes ao
 * mesmo `id` — do mesmo call site ou de outro — sao no-ops silenciosos.
 *
 * fn: nome da funcao host/guest que observou o evento, ou NULL -> "-".
 * fmt/...: contexto para <data> (path=/tid=/ea=/buf=, conforme o checkpoint),
 * igual a PS3_TRACE(); fmt==NULL ou "" -> <data> "-".
 *
 * Se PS3_TRACE_SNAPSHOT tambem estiver ligado, uma segunda linha (subsistema
 * SNAPSHOT, mesmo <event>) e emitida logo a seguir, com tb=/threads=/
 * heap_used=/last_flip= — ver o comentario de cabecalho deste ficheiro e
 * ps3_checkpoint.c para a origem de cada contador. */
#ifdef __GNUC__
void ps3_checkpoint_fire(ps3_checkpoint_id id, const char* fn,
                         const char* fmt, ...) __attribute__((format(printf, 3, 4)));
#else
void ps3_checkpoint_fire(ps3_checkpoint_id id, const char* fn,
                         const char* fmt, ...);
#endif

/* Representacao de <data> (achado G1 da review pos-Task 2): um valor
 * formatado a partir de conteudo guest arbitrario (ex.: "%s" de um nome de
 * thread ou de um path FS/WAD) pode conter espacos ou '|', o que quebraria
 * tanto a regra "<data> = pares k=v separados por espaco" como a proibicao
 * geral de '|' em qualquer campo (G1) -- observado de verdade num boot real:
 *
 *   [PS3T] ... | FIRST_THREAD | tid=1 name=fios mediathread entry=0x00534370
 *
 * onde um parser k=v ingenuo que faz split em espaco leria "name=fios" e um
 * token solto "mediathread".
 *
 * ATE' A TASK 2, ps3_checkpoint_fire() sanitizava o <data> JA COMPOSTO com
 * uma copia local antes de o passar a ps3_trace_emit(). A TASK 3 (MEMORY/
 * HEAP) PROMOVEU essa sanitizacao para o nucleo -- sanitize_data_kv(), static
 * em runtime/trace/ps3_trace.c, aplicada automaticamente dentro de
 * ps3_trace_emit() a QUALQUER <data> de QUALQUER subsistema -- porque o
 * mesmo problema ("%s" de conteudo guest com espaco embutido) nao e'
 * exclusivo de CHECKPOINTS: MEMORY/HEAP (Task 3) e as camadas seguintes
 * (Task 4/5) tambem compoem <data> a partir de nomes/paths guest. Este
 * ficheiro (e ps3_checkpoint.c) ja nao tem copia propria da sanitizacao; o
 * comportamento observavel e' o mesmo, so' o sitio mudou:
 *
 *   1. Qualquer '|' literal e' substituido por '/' (defesa em profundidade:
 *      sanitize_field(), dentro do mesmo ps3_trace_emit(), repete a troca
 *      sobre o resultado -- ja redundante para '|' em <data> especificamente,
 *      mas ainda a unica defesa de '|'/'\n'/'\r' para <fn>/<event>, que nao
 *      passam pela sanitizacao k=v). '\n'/'\r' DENTRO de <data> tambem viram
 *      '_' aqui (review pos-Task 3, "Also fix" #1 -- corrigido depois desta
 *      nota ter sido escrita: deixar um '\n' literal sobreviver ate'
 *      sanitize_field(), que o mapeia para ESPACO, recriava o mesmo bug do
 *      token solto para uma newline literal dentro de um valor).
 *   2. Um espaco so' e' tratado como SEPARADOR entre pares k=v se o token
 *      a seguir comecar por [A-Za-z_][A-Za-z0-9_]* seguido de '=' (uma nova
 *      chave valida); caso contrario e' tratado como espaco DENTRO do valor
 *      anterior e substituido por '_'. Ex.: "name=fios mediathread entry=..."
 *      -> "name=fios_mediathread entry=...". Uma sequencia de 1+ espacos
 *      consecutivos (incluindo espacos iniciais) colapsa num UNICO separador
 *      (review pos-Task 3, "Also fix" #2 -- evita tanto um '_' a mais por
 *      espaco duplicado como um <data> a comecar por espaco). Isto sobrevive
 *      a um round-trip por um parser k=v que faz split em espaco (Task 6)
 *      sem exigir que nenhum call site escape os seus proprios argumentos
 *      "%s".
 *
 * Isto aplica-se ao <data> de AMBAS as linhas (CHECKPOINTS e SNAPSHOT); os
 * campos internos do SNAPSHOT (tb=/threads=/heap_used=/last_flip=) sao todos
 * numericos e nunca contem espaco, portanto a sanitizacao e' um no-op neles. */

#ifdef __cplusplus
}
#endif

#endif /* PS3_CHECKPOINT_H */
