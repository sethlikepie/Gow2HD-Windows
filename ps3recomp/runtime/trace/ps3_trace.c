/*
 * ps3recomp - ps3_trace: implementação do núcleo de instrumentação comum.
 * Ver include/ps3_trace.h para o contrato de formato/gating.
 *
 * Este ficheiro tem de viver em runtime/trace (não em runtime/host): o
 * CMakeLists.txt faz GLOB_RECURSE dos fontes C dentro de runtime, e exclui
 * apenas os diretórios tests, runtime/ppu e runtime/host — por isso um
 * ficheiro C em runtime/trace entra na libps3recomp_runtime.a sem editar o
 * CMakeLists.txt, e fica visível às chamadas a partir de libs (que também
 * estão nessa lib).
 *
 * Sanitização de <data> (promovido da Task 2, ver sanitize_data_kv() abaixo):
 * ps3_trace_emit() aplica-a a TODO subsistema, não só a CHECKPOINTS/SNAPSHOT
 * — a camada de checkpoint (ps3_checkpoint.c) tinha a sua própria cópia desta
 * função; foi removida de lá na Task 3 (MEMORY/HEAP) porque manter cópias por
 * camada é exactamente a duplicação que um segundo (e terceiro, e quarto)
 * consumidor do núcleo deveria evitar.
 */
#include "ps3_trace.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------------------
 * ps3_timebase_now() é definido em runtime/syscalls/sys_timer.c:39, mas
 * runtime/syscalls/sys_timer.h NÃO o declara (só expõe os handlers de
 * syscall) — confirmado por inspecção antes de escrever isto, para não
 * duplicar uma declaração já existente. Declaramos aqui em vez de incluir
 * sys_timer.h, que arrastaria lv2_syscall_table.h/ppu_context.h (dependências
 * pesadas e desnecessárias para este ficheiro game-agnostic).
 * -----------------------------------------------------------------------*/
extern uint64_t ps3_timebase_now(void);

/* ---------------------------------------------------------------------------
 * G2 — os dez subsistemas, a env var que os liga e o token <subsys> exacto.
 * Índices alinhados 1:1 com ps3_trace_subsys (Task brief, ordem exacta).
 * -----------------------------------------------------------------------*/
_Static_assert(PS3_TS__COUNT == 10, "ps3_trace: G2 exige exactamente dez subsistemas");

static const char* const k_env_names[PS3_TS__COUNT] = {
    "PS3_TRACE_CHECKPOINTS",
    "PS3_TRACE_MEMORY",
    "PS3_TRACE_HEAP",
    "PS3_TRACE_THREADS",
    "PS3_TRACE_SPURS",
    "PS3_TRACE_DMA",
    "PS3_TRACE_FIOS",
    "PS3_TRACE_MEDIA",
    "PS3_TRACE_RSX",
    "PS3_TRACE_SNAPSHOT",
};

static const char* const k_subsys_names[PS3_TS__COUNT] = {
    "CHECKPOINTS",
    "MEMORY",
    "HEAP",
    "THREADS",
    "SPURS",
    "DMA",
    "FIOS",
    "MEDIA",
    "RSX",
    "SNAPSHOT",
};

/* -1 = por resolver, 0 = desligado, 1 = ligado. Resolvido no primeiro
 * ps3_trace_on() de cada subsistema (getenv uma vez); nunca mais getenv
 * depois disso. Mesmo idioma já usado em runtime/syscalls/sys_event.c:15 e
 * runtime/ppu/ppu_loader.cpp:826 para os gates PS3_TRACE_SPURS/HEAP
 * pré-existentes (G3) — aqui replicado por subsistema, sem tocar naqueles. */
static int s_gate_state[PS3_TS__COUNT] = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };

int ps3_trace_on(ps3_trace_subsys s)
{
    if ((unsigned)s >= (unsigned)PS3_TS__COUNT) {
        return 0;
    }
    /* Cache memoizada do env, lida e escrita por varias threads: o resultado
     * e' idempotente, mas ler/escrever em texto simples continua a ser data
     * race (TSan). Relaxed chega. */
    int cached = __atomic_load_n(&s_gate_state[s], __ATOMIC_RELAXED);
    if (cached < 0) {
        const char* v = getenv(k_env_names[s]);
        cached = (v && v[0] != '\0' && v[0] != '0') ? 1 : 0;
        __atomic_store_n(&s_gate_state[s], cached, __ATOMIC_RELAXED);
    }
    return cached;
}

const char* ps3_trace_subsys_name(ps3_trace_subsys s)
{
    if ((unsigned)s >= (unsigned)PS3_TS__COUNT) {
        return "?";
    }
    return k_subsys_names[s];
}

/* ---------------------------------------------------------------------------
 * Thread guest corrente (TLS). Requisito 5 do brief: só o mecanismo aqui —
 * quem chama ps3_trace_set_thread() são as tarefas 2-5.
 * -----------------------------------------------------------------------*/
static _Thread_local uint32_t s_guest_tid = 0;

void ps3_trace_set_thread(uint32_t guest_tid)
{
    s_guest_tid = guest_tid;
}

uint32_t ps3_trace_thread(void)
{
    return s_guest_tid;
}

/* ---------------------------------------------------------------------------
 * Sanitização (requisito 7): substitui '|' por '/' e '\n'/'\r' por espaço,
 * copiando para `dst` (capacidade `cap`, sempre NUL-terminado). Usada nos
 * campos livres <fn>/<event>/<data> ANTES de os inserir na linha final —
 * nunca depois, senão apagaria os separadores " | " estruturais.
 * -----------------------------------------------------------------------*/
static void sanitize_field(char* dst, size_t cap, const char* src)
{
    size_t i = 0;
    if (cap == 0) {
        return;
    }
    if (src != NULL) {
        for (; src[i] != '\0' && i < cap - 1; i++) {
            char c = src[i];
            if (c == '|') {
                c = '/';
            } else if (c == '\n' || c == '\r') {
                c = ' ';
            }
            dst[i] = c;
        }
    }
    dst[i] = '\0';
}

/* ---------------------------------------------------------------------------
 * Sanitizacao k=v de <data> (promovido da Task 2 -- review pos-Task-2 achou
 * que um valor "%s" de conteudo guest arbitrario pode conter espacos, o que
 * quebra a regra "<data> = pares k=v separados por espaco simples" (G1) sem
 * quebrar `sanitize_field` acima (que so' lida com '|' e '\n'/'\r', nao com
 * espacos "soltos" dentro de um valor). Exemplo real, observado num boot:
 *
 *   [PS3T] ... | CHECKPOINTS | FIRST_THREAD | tid=1 name=fios mediathread entry=0x00534370
 *
 * onde um parser k=v ingenuo que faz split em espaco leria "name=fios" e um
 * token solto "mediathread". A Task 2 (ps3_checkpoint.c) tinha a sua propria
 * copia desta funcao; promovida aqui (Task 3) para o NUCLEO, porque o mesmo
 * problema se repete em qualquer subsistema que formate <data> a partir de
 * "%s" guest (MEMORY/HEAP agora, THREADS/SPURS/DMA/FIOS/MEDIA/RSX depois) --
 * com a sanitizacao dentro de ps3_trace_emit(), nenhum call site nem camada
 * precisa de replicar isto.
 *
 * Regra: um espaco so' e' tratado como SEPARADOR entre pares k=v se o token a
 * seguir comecar por [A-Za-z_][A-Za-z0-9_]* seguido de '=' (uma nova chave
 * valida); caso contrario e' um espaco DENTRO do valor anterior e vira '_'.
 * Ex.: "name=fios mediathread entry=..." -> "name=fios_mediathread entry=...".
 * '|' literal vira '/' aqui tambem (defesa em profundidade -- a sanitize_field
 * acima repete a mesma troca sobre o resultado desta funcao, ja' redundante
 * para '|'). '\n'/'\r' TAMBEM viram '_' aqui (review pos-Task 3, "Also fix"
 * #1 -- antes desta funcao nao os tratava, e sanitize_field acima so' os via
 * DEPOIS, mapeando para ESPACO, recriando o mesmo bug do token solto para uma
 * newline literal dentro de um valor). Uma sequencia de 1+ espacos
 * consecutivos (incluindo espacos iniciais) colapsa num UNICO separador
 * (review pos-Task 3, "Also fix" #2 -- evita tanto o '_' a mais de um espaco
 * duplicado como um <data> a comecar por espaco). Aplicada SO' a <data> (o
 * unico campo com estrutura "k=v k=v ..."; <fn>/<event> sao tokens simples e
 * nao precisam disto, mas continuam a passar por sanitize_field, que ainda e'
 * a unica defesa de '\n'/'\r' PARA ESSES DOIS CAMPOS). Opera in-place: nunca
 * expande o buffer (cada substituicao e' 1:1 ou colapsa um separador), por
 * isso e' seguro com um so' ponteiro de escrita <= ponteiro de leitura.
 * -----------------------------------------------------------------------*/
static void sanitize_data_kv(char* s)
{
    char* out;
    char* p;
    int first;

    if (!s) {
        return;
    }
    /* Passo 1: substituicoes de 1 caractere, in-place (nunca mudam o
     * comprimento). '|' -> '/' (defesa em profundidade -- sanitize_field()
     * repete esta troca sobre o resultado, ja redundante aqui). '\n'/'\r' ->
     * '_' (review pos-Task 3, achado "Also fix" #1): antes desta funcao NAO
     * tratava '\n'/'\r', deixando-os para sanitize_field() mapear para
     * ESPACO mais tarde -- o que recriava, para uma newline literal, o
     * MESMO bug do token solto que esta funcao existe para evitar (um valor
     * guest "name=fios\nmediathread", SEM espaco nenhum no original, saia
     * como "name=fios mediathread" com um espaco novo que um parser k=v
     * ingenuo le como dois tokens). Tratando '\n'/'\r' aqui, como parte da
     * mesma regra "sem espaco solto dentro de um valor" que este ficheiro ja
     * aplica a espacos, o resultado fica "name=fios_mediathread". */
    for (p = s; *p; p++) {
        if (*p == '|') {
            *p = '/';
        } else if (*p == '\n' || *p == '\r') {
            *p = '_';
        }
    }

    /* Passo 2: tokeniza por espaco. Uma sequencia de 1+ espacos consecutivos
     * (incluindo espacos iniciais, antes de qualquer par k=v) e' colapsada
     * num UNICO potencial separador -- review pos-Task 3, achado "Also fix"
     * #2: a versao anterior avancava um espaco de cada vez e tratava cada
     * espaco extra como um "token vazio" por conta propria, o que inseria um
     * '_' a mais por cada espaco duplicado ("a=1  b=2" -> "a=1_ b=2", em vez
     * de "a=1 b=2") e deixava um espaco inicial sobreviver verbatim na saida
     * ("<space>a=1" -> <data> a comecar por espaco, o que faria a linha
     * conter "| <space>a=1" logo a seguir ao separador estrutural " | "). */
    out = s;
    p = s;
    while (*p == ' ') {
        p++;
    }
    first = 1;
    while (*p) {
        char* tok_start = p;
        char* q;
        int tok_is_new_kv = 0;

        while (*p && *p != ' ') {
            p++;
        }
        q = tok_start;
        if (q < p && ((*q >= 'A' && *q <= 'Z') || (*q >= 'a' && *q <= 'z') || *q == '_')) {
            q++;
            while (q < p && ((*q >= 'A' && *q <= 'Z') || (*q >= 'a' && *q <= 'z') ||
                              (*q >= '0' && *q <= '9') || *q == '_')) {
                q++;
            }
            if (q < p && *q == '=') {
                tok_is_new_kv = 1;
            }
        }

        if (!first) {
            *out++ = tok_is_new_kv ? ' ' : '_';
        }
        {
            char* c;
            for (c = tok_start; c < p; c++) {
                *out++ = *c;
            }
        }
        first = 0;
        while (*p == ' ') {
            p++;
        }
    }
    *out = '\0';
}

/* Tamanho máximo da linha emitida (requisito 6), incluindo o '\n' final. Os
 * buffers intermédios de <fn>/<event>/<data> usam a MESMA capacidade: se
 * fossem mais pequenos, truncariam em silêncio antes de chegar à montagem
 * final e o "…" do requisito 6 nunca apareceria para um campo grande — o
 * único ponto de corte tem de ser esta linha montada. */
#define PS3_TRACE_LINE_MAX 1024

void ps3_trace_emit(ps3_trace_subsys s, const char* fn, const char* event,
                     const char* fmt, ...)
{
    char thr_field[16];
    char fn_field[PS3_TRACE_LINE_MAX];
    char ev_field[PS3_TRACE_LINE_MAX];
    char data_raw[PS3_TRACE_LINE_MAX];
    char data_field[PS3_TRACE_LINE_MAX];
    char line[PS3_TRACE_LINE_MAX];
    uint64_t tb;
    uint32_t tid;
    int n;

    tb  = ps3_timebase_now();
    tid = ps3_trace_thread();

    if (tid == 0) {
        thr_field[0] = '-';
        thr_field[1] = '\0';
    } else {
        snprintf(thr_field, sizeof(thr_field), "0x%08X", tid);
    }

    sanitize_field(fn_field, sizeof(fn_field), fn ? fn : "-");
    sanitize_field(ev_field, sizeof(ev_field), event ? event : "-");

    if (fmt == NULL || fmt[0] == '\0') {
        data_raw[0] = '-';
        data_raw[1] = '\0';
    } else {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(data_raw, sizeof(data_raw), fmt, ap);
        va_end(ap);
    }
    sanitize_data_kv(data_raw);
    sanitize_field(data_field, sizeof(data_field), data_raw);

    n = snprintf(line, sizeof(line), "[PS3T] %llu | %s | %s | %s | %s | %s\n",
                 (unsigned long long)tb, thr_field, fn_field,
                 ps3_trace_subsys_name(s), ev_field, data_field);
    if (n < 0) {
        return; /* erro de formatação: nada fiel para emitir */
    }

    size_t len;
    if ((size_t)n >= sizeof(line)) {
        /* Excedeu o limite: trunca com "…" (U+2026, UTF-8 "\xE2\x80\xA6")
         * antes do '\n' final, como pede o requisito 6. snprintf já deixou
         * sizeof(line)-1 bytes válidos + NUL. */
        static const char ellipsis_nl[] = "\xE2\x80\xA6\n";
        len = sizeof(line) - 1;
        memcpy(line + len - (sizeof(ellipsis_nl) - 1), ellipsis_nl, sizeof(ellipsis_nl) - 1);
    } else {
        len = (size_t)n;
    }

    fwrite(line, 1, len, stderr);
    fflush(stderr);
}
