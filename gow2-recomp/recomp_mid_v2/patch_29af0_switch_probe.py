#!/usr/bin/env python3
"""Probe do despacho por indice de func_00029AF0 (jump table de 74 casos).

Contexto
--------
notes/2026-07-30-ponto1-despacho-vtable-o-lift-esta-fiel.md mediu, estaticamente,
que a cadeia `func_00029AF0 -> CD9DC/CD7B4 -> CD498` esta' INTEIRA no lift:

  - os 3 chamadores de CD7B4/CD9DC vivem em codigo que o functions.json NAO
    declara (gaps de 1792 e 228 instrucoes), mas o boundary recovery apanhou-os
    e as chamadas estao emitidas com o `lr` certo;
  - a jump table foi recuperada EXACTAMENTE: 108 entradas, 74 alvos distintos,
    74 `case`, zero em falta e zero a mais.

Ou seja: o codigo esta' la' e e' alcancavel. O que nao acontece e' o INDICE tomar
os valores que la' chegam. Os blocos que chamam a cadeia sao:

    alvo 0x0002ABA4 -> bl 0xCD9DC     indice 52
    alvo 0x0002AE08 -> bl 0xCD7B4     indices 27, 55, 84

Esta probe responde a uma pergunta binaria que a analise estatica nao pode:

    func_00029AF0 chega a ser chamada? E se sim, que indices chegam?

Tres desfechos, e cada um manda para um sitio diferente:

  entradas=0            a funcao nunca corre -> o problema esta' a MONTANTE
                        (quem deveria despachar para ela; e' slot 3 da vtable
                        0x00510FD0, so' alcancavel por despacho virtual).
  entradas>0, sem 27/52/55/84
                        corre, mas o tipo/comando que levaria a' cadeia nunca
                        e' emitido -> subir ao emissor do indice (gpr[4]).
  27/52/55/84 aparecem  a cadeia E' alcancada e o problema esta' DENTRO dela
                        -- e ai o CD498-PROBE (tot=0) estaria em contradicao,
                        o que so' por si seria um achado.

Gate (default OFF, regra 6 do CLAUDE.md)
  PS3_TRACE_29AF0=1  -> ON so' se o primeiro caracter for '1'
  ausente / '' / '0' / outro  -> OFF, no-op total no baseline

Telemetria
  [29AF0] enter n=N post=P                        (cap 4 linhas)
  [29AF0] disp idx=0xNN ctr=0x000XXXXX post=P     (cap 24 linhas)
  [29AF0] SUMMARY entradas=N despachos=M rperma_full=R
  [29AF0] SUMMARY idx=NN ctr=0x000XXXXX n=K       (um por alvo distinto visto)
  [29AF0] SUMMARY ALVO-CHAVE ctr=0x0002ABA4 n=K   (impresso SEMPRE, mesmo a 0)
  [29AF0] SUMMARY ALVO-CHAVE ctr=0x0002AE08 n=K   (idem)

Os dois ALVO-CHAVE sao impressos mesmo a zero: a ausencia de uma linha e'
ambigua (probe partida? nunca aconteceu?), um zero explicito nao e'. E o
`entradas` distingue "nunca chamada" de "chamada e nunca despachou".

NAO altera fluxo de controlo guest: le' `ctx` e conta, nada mais. O ponto de
insercao fica ENTRE `ctx->ctr = (uint32_t)ctx->gpr[0];` e o `switch`, onde
gpr[4] (indice cru) e ctr (alvo ja' resolvido) estao ambos vivos.

Idempotente: marcador presente -> ALREADY, rc=0.
rc: 0 aplicado ou ja'-aplicado; 2 alvo inexistente; 3 agulha em falta.
"""
from __future__ import annotations

import sys
from pathlib import Path

MARKER = "29AF0-SWITCH-PROBE"
CHUNK = "ppu_recomp_000.cpp"

# A agulha e' a linha do switch. E' unica no ficheiro inteiro (74 `case` com
# estes alvos exactos) -- por isso nao precisa de janela em volta da assinatura.
SWITCH_HEAD = "        switch ((uint32_t)ctx->ctr) { case 0x00029D0Cu: goto loc_00029D0C;"

FN_SIG = "void func_00029AF0(ppu_context* ctx) {\n"

HELPER = r'''
/* 29AF0-SWITCH-PROBE: que indices chegam a jump table? (PS3_TRACE_29AF0=1) */
#include <signal.h>

/* g_ps3_rperma_full e' definido por outro patch (WADLD) que pode nao ter corrido
 * neste lift. Uma DECLARACAO `weak` nao serve: no Mach-O/arm64 da undefined
 * symbol no link -- medido em 2026-07-30, e foi o que este teste apanhou. Uma
 * DEFINICAO fraca linka sempre e cede o lugar a definicao forte quando ela
 * existe (provado nos dois sentidos). O guard evita a redefinicao quando os dois
 * probes desta leva caem no mesmo chunk. */
#ifndef PS3_RPERMA_WEAK_DEFINED
#define PS3_RPERMA_WEAK_DEFINED 1
extern "C" __attribute__((weak)) volatile int g_ps3_rperma_full = 0;
#endif
static int g_ps3_29af0_rperma(void) { return (int)g_ps3_rperma_full; }

enum { PS3_29AF0_SLOTS = 96 };
static unsigned int       g_ps3_29af0_ctr[PS3_29AF0_SLOTS];
static unsigned int       g_ps3_29af0_idx[PS3_29AF0_SLOTS];
static unsigned long long g_ps3_29af0_cnt[PS3_29AF0_SLOTS];
static int                g_ps3_29af0_used = 0;
static unsigned long long g_ps3_29af0_enter = 0;
static unsigned long long g_ps3_29af0_disp = 0;
static unsigned long long g_ps3_29af0_overflow = 0;
static unsigned long long g_ps3_29af0_log_enter = 0;
static unsigned long long g_ps3_29af0_log_disp = 0;
static int g_ps3_29af0_reg = 0;
static int g_ps3_29af0_gate = -1;

/* Os dois alvos que chamam a cadeia CD9DC/CD7B4 -> CD498 (instalador do estado
 * TYPE15). Medidos estaticamente na tabela em 2026-07-30: 0x0002ABA4 e' o
 * indice 52; 0x0002AE08 sao os indices 27, 55 e 84. */
enum { PS3_29AF0_KEY_N = 2 };
static const unsigned int g_ps3_29af0_key[PS3_29AF0_KEY_N] = { 0x0002ABA4u, 0x0002AE08u };

static int ps3_29af0_gate_on(void) {
    if (g_ps3_29af0_gate < 0) {
        extern char* getenv(const char*);
        const char* e = getenv("PS3_TRACE_29AF0");
        g_ps3_29af0_gate = (e && *e == '1') ? 1 : 0;
    }
    return g_ps3_29af0_gate;
}

static void ps3_29af0_dump_summary(void) {
    if (!ps3_29af0_gate_on()) return;
    /* SIGTERM e atexit podem ambos disparar (o boot e' morto por TERM depois de
     * um timeout). Sem esta guarda o SUMMARY sai duas vezes e quem le' o log tem
     * de adivinhar se sao duas corridas ou uma. */
    static int dumped = 0;
    if (dumped) return;
    dumped = 1;
    fprintf(stderr, "[29AF0] SUMMARY entradas=%llu despachos=%llu rperma_full=%d\n",
            g_ps3_29af0_enter, g_ps3_29af0_disp, (int)g_ps3_29af0_rperma());
    for (int i = 0; i < g_ps3_29af0_used; i++) {
        fprintf(stderr, "[29AF0] SUMMARY idx=%u ctr=0x%08X n=%llu\n",
                g_ps3_29af0_idx[i], g_ps3_29af0_ctr[i], g_ps3_29af0_cnt[i]);
    }
    if (g_ps3_29af0_overflow) {
        fprintf(stderr, "[29AF0] SUMMARY AVISO: %llu despachos com alvo novo alem "
                        "dos %d slots -- histograma TRUNCADO\n",
                g_ps3_29af0_overflow, (int)PS3_29AF0_SLOTS);
    }
    /* impressos SEMPRE, mesmo a zero: e' o zero que interessa distinguir */
    for (int k = 0; k < PS3_29AF0_KEY_N; k++) {
        unsigned long long n = 0;
        for (int i = 0; i < g_ps3_29af0_used; i++)
            if (g_ps3_29af0_ctr[i] == g_ps3_29af0_key[k]) n += g_ps3_29af0_cnt[i];
        fprintf(stderr, "[29AF0] SUMMARY ALVO-CHAVE ctr=0x%08X n=%llu\n",
                g_ps3_29af0_key[k], n);
    }
    fflush(stderr);
}

/* ENCADEAMENTO (defeito medido em 2026-07-30, na primeira corrida real):
 * `signal()` SUBSTITUI o handler, nao o acumula. Com tres probes armados no
 * mesmo binario, o ultimo constructor a correr ficava com o SIGTERM e os outros
 * dois nunca imprimiam o SUMMARY -- e o SUMMARY e' precisamente o resultado. Foi
 * o que aconteceu: o [CD498] imprimiu, estes dois nao.
 * Guardar o handler anterior e chama-lo no fim mantem a cadeia inteira viva,
 * seja qual for a ordem de arranque. */
typedef void (*ps3_29af0_sigh_t)(int);
static ps3_29af0_sigh_t g_ps3_29af0_prev_sigterm = 0;

static void ps3_29af0_on_sigterm(int sig) {
    ps3_29af0_dump_summary();
    if (g_ps3_29af0_prev_sigterm
        && g_ps3_29af0_prev_sigterm != SIG_DFL
        && g_ps3_29af0_prev_sigterm != SIG_IGN) {
        g_ps3_29af0_prev_sigterm(sig);
    }
}

static void ps3_29af0_arm(void) {
    if (g_ps3_29af0_reg) return;
    g_ps3_29af0_reg = 1;
    atexit(ps3_29af0_dump_summary);
#ifndef _WIN32
    g_ps3_29af0_prev_sigterm = signal(SIGTERM, ps3_29af0_on_sigterm);
#endif
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((constructor))
#endif
static void ps3_29af0_ctor(void) {
    if (!ps3_29af0_gate_on()) return;
    fprintf(stderr, "[29AF0] probe armed (jump table 74 casos; alvos-chave "
                    "0x0002ABA4 idx52, 0x0002AE08 idx27/55/84)\n");
    fflush(stderr);
    ps3_29af0_arm();
}

void ps3_29af0_on_enter(ppu_context* ctx) {
    (void)ctx;
    if (!ps3_29af0_gate_on()) return;
    ps3_29af0_arm();
    unsigned long long n = ++g_ps3_29af0_enter;
    if (g_ps3_29af0_log_enter < 4) {
        g_ps3_29af0_log_enter++;
        fprintf(stderr, "[29AF0] enter n=%llu post=%d\n", n, (int)g_ps3_29af0_rperma());
        fflush(stderr);
    }
}

void ps3_29af0_on_dispatch(ppu_context* ctx) {
    if (!ps3_29af0_gate_on() || !ctx) return;
    ps3_29af0_arm();
    unsigned int tgt = (unsigned int)ctx->ctr;
    unsigned int idx = (unsigned int)ctx->gpr[4];
    g_ps3_29af0_disp++;
    int slot = -1;
    for (int i = 0; i < g_ps3_29af0_used; i++) {
        if (g_ps3_29af0_ctr[i] == tgt) { slot = i; break; }
    }
    if (slot < 0) {
        if (g_ps3_29af0_used < PS3_29AF0_SLOTS) {
            slot = g_ps3_29af0_used++;
            g_ps3_29af0_ctr[slot] = tgt;
            g_ps3_29af0_idx[slot] = idx;
            g_ps3_29af0_cnt[slot] = 0;
        } else {
            g_ps3_29af0_overflow++;          /* nunca perder em silencio */
            return;
        }
    }
    g_ps3_29af0_cnt[slot]++;
    if (g_ps3_29af0_log_disp < 24) {
        g_ps3_29af0_log_disp++;
        fprintf(stderr, "[29AF0] disp idx=0x%X ctr=0x%08X post=%d\n",
                idx, tgt, (int)g_ps3_29af0_rperma());
        fflush(stderr);
    }
}
'''

def patch(path: Path) -> int:
    t = path.read_text(encoding="utf-8", errors="replace")

    if f"/* {MARKER} */" in t:
        print(f"  {path.name}: ALREADY (marcador presente)")
        return 0

    # --- 1. preambulo -------------------------------------------------------
    # Ancora no FIM da zona de preambulos (mesmo antes da 1a funcao lifted), nao
    # a seguir ao #include. Razao medida em 2026-07-30: no clang/Mach-O os
    # constructors correm por ORDEM DE DEFINICAO no ficheiro (prioridade nao
    # reordena de forma util -- testado), e quem arma por ULTIMO fica com o
    # SIGTERM. Ancorado no include, este probe armava ANTES do CD498-PROBE e
    # perdia o sinal para ele; o SUMMARY nunca saia. Aqui fica sempre depois de
    # qualquer preambulo existente, arma por ultimo, e encadeia para tras.
    anchor_at = t.find("\nvoid func_")
    if anchor_at < 0:
        print(f"ERRO: {path.name} nao tem nenhuma funcao lifted onde ancorar",
              file=sys.stderr)
        return 3
    t = t[:anchor_at + 1] + HELPER + t[anchor_at + 1:]

    # --- 2. contador de entrada na funcao -----------------------------------
    if FN_SIG not in t:
        print(f"ERRO: {path.name} nao tem {FN_SIG.strip()}", file=sys.stderr)
        return 3
    if t.count(FN_SIG) != 1:
        print(f"ERRO: {FN_SIG.strip()} aparece {t.count(FN_SIG)}x (esperado 1)",
              file=sys.stderr)
        return 3
    i = t.find(FN_SIG) + len(FN_SIG)
    t = t[:i] + f"        /* {MARKER} */\n        {{ ps3_29af0_on_enter(ctx); }}\n" + t[i:]

    # --- 3. o despacho ------------------------------------------------------
    n = t.count(SWITCH_HEAD)
    if n != 1:
        print(f"ERRO: agulha do switch aparece {n}x (esperado 1) -- o lift mudou de "
              f"forma; NAO aplicado", file=sys.stderr)
        return 3
    t = t.replace(SWITCH_HEAD,
                  "        { ps3_29af0_on_dispatch(ctx); }\n" + SWITCH_HEAD, 1)

    try:
        path.write_text(t, encoding="utf-8", newline="\n")
    except TypeError:                                # macOS system python3 (3.9.6)
        path.write_text(t, encoding="utf-8")
    print(f"  {path.name}: APPLIED (entrada + despacho)")
    return 0


def main() -> int:
    root = Path(sys.argv[1]) if len(sys.argv) > 1 else \
        Path(__file__).resolve().parent.parent / "recomp_macos_v2"
    target = root / CHUNK if root.is_dir() else root
    if not target.is_file():
        print(f"ERRO: {target} nao existe", file=sys.stderr)
        return 2
    print(f"[{MARKER}] {target}")
    rc = patch(target)
    print(f"[{MARKER}] rc={rc}")
    return rc


if __name__ == "__main__":
    sys.exit(main())
