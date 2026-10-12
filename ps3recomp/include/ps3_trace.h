/*
 * ps3recomp - ps3_trace: núcleo de instrumentação comum (Task 1 do plano
 * "bringup-instrumentacao-fase0").
 *
 * Formato de linha canónico (contrato com o parser da Task 6):
 *
 *   [PS3T] <tb> | <thr> | <fn> | <subsys> | <event> | <data>
 *
 * onde:
 *   <tb>     ps3_timebase_now() em decimal (nunca ausente)
 *   <thr>    thread guest em 0x%08X, ou "-" se ps3_trace_thread()==0
 *   <fn>     nome/EA da função guest, ou "-" se fn==NULL
 *   <subsys> um dos dez tokens abaixo, sempre em maiúsculas (nunca ausente)
 *   <event>  token curto sem espaços nem '|' (nunca ausente)
 *   <data>   pares "k=v" separados por espaço, ou "-" se fmt==NULL/vazio
 *
 * O prefixo "[PS3T] " é a sentinela que distingue estas linhas das sondas
 * ad-hoc pré-existentes no mesmo stderr (interpretação nossa do requisito de
 * formato único do plano de origem — ver G1 do plano de bring-up).
 *
 * Gating: cada subsistema é ligado por uma env var própria (ver a tabela em
 * ps3_trace.c), lida via getenv() UMA vez e cacheada — sem getenv no caminho
 * quente. Default OFF em todas: com todas as variáveis por definir, esta
 * camada não emite um único byte e não muda nenhum comportamento observável.
 *
 * Esta camada é aditiva: PS3_TRACE_HEAP e PS3_TRACE_SPURS já tinham
 * consumidores próprios (runtime/ppu/ppu_loader.cpp, runtime/syscalls/sys_event.c,
 * runtime/ppu/ppu_hle.cpp) — esta camada lê as MESMAS variáveis em paralelo,
 * sem alterar esses consumidores.
 */
#ifndef PS3_TRACE_H
#define PS3_TRACE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PS3_TS_CHECKPOINTS = 0,
    PS3_TS_MEMORY,
    PS3_TS_HEAP,
    PS3_TS_THREADS,
    PS3_TS_SPURS,
    PS3_TS_DMA,
    PS3_TS_FIOS,
    PS3_TS_MEDIA,
    PS3_TS_RSX,
    PS3_TS_SNAPSHOT,
    PS3_TS__COUNT
} ps3_trace_subsys;

/* 1 se o subsistema `s` está ligado (env var correspondente definida com
 * primeiro carácter != '\0' e != '0'), 0 caso contrário. Resolvido uma vez
 * por subsistema (getenv só na primeira chamada); barato o suficiente para
 * ser chamado no caminho quente. */
int ps3_trace_on(ps3_trace_subsys s);

/* Emite uma linha "[PS3T] ..." em stderr com uma única escrita (fwrite +
 * fflush). fn==NULL vira "-"; fmt==NULL ou "" faz <data> virar "-". Qualquer
 * '|' produzido em <fn>/<event>/<data> é substituído por '/'; '\n'/'\r' por
 * espaço. Chamar diretamente é incomum — use a macro PS3_TRACE() abaixo, que
 * já faz o gating. */
#ifdef __GNUC__
void ps3_trace_emit(ps3_trace_subsys s, const char* fn, const char* event,
                     const char* fmt, ...) __attribute__((format(printf, 4, 5)));
#else
void ps3_trace_emit(ps3_trace_subsys s, const char* fn, const char* event,
                     const char* fmt, ...);
#endif

/* Thread guest corrente, armazenada em TLS (_Thread_local). Mecanismo apenas
 * — quem chama ps3_trace_set_thread() são as tarefas seguintes do plano. */
void     ps3_trace_set_thread(uint32_t guest_tid);
uint32_t ps3_trace_thread(void); /* 0 = desconhecida */

/* Token <subsys> exacto (G2) para `s`. Exposto para o teste unitário
 * verificar T1.7 sem duplicar a tabela. */
const char* ps3_trace_subsys_name(ps3_trace_subsys s);

/* Gate + emissão num só passo: getenv não corre se o subsistema estiver
 * desligado (ps3_trace_on curto-circuita antes de avaliar os args). */
#define PS3_TRACE(s, fn, ev, ...) \
    do { if (ps3_trace_on(s)) ps3_trace_emit((s), (fn), (ev), __VA_ARGS__); } while (0)

#ifdef __cplusplus
}
#endif

#endif /* PS3_TRACE_H */
