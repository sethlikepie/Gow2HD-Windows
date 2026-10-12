#!/usr/bin/env python3
"""Probe do despacho POR TIPO do func_000CB56C (icall1 = construct do produto).

Contexto
--------
notes/2026-07-30-ponto1-despacho-vtable-o-lift-esta-fiel.md fechou o ponto 1: o
lift esta' FIEL ao ELF em toda a cadeia que constroi o estado TYPE15 (vtables,
OPD, chamadas, jump table 74/74). O que sobra aponta para nos.

O bloco que o lifter gera para o icall1 do CB56C usa o MESMO padrao de indice
que a jump table de func_00029AF0 (`rlwinm 2,14,29`), mas sobre um campo de
TIPO de 16 bits:

    r9  = vm_read16(r1 + 0x72)              <- o TIPO pedido
    r27 = vm_read32(r2 - 0x5EA0)            <- base da tabela de classes
    r9  = rlwinm(r9, 2, 14, 29)             <- indice*4
    r11 = vm_read32(r27 + r9)               <- tabela[tipo]  = a "classe"
    r9  = vm_read32(r11 + 0x0)              <- vtable dessa classe
    r10 = vm_read32(r9 + 0x14)              <- slot 5 = o construtor
    ctr = vm_read32(r10 + 0x0)              <- code do OPD
    ps3_indirect_call(ctx)                  <- icall1: constroi o produto

E' um despacho por tipo em CASCATA: o tipo escolhe a classe, a classe escolhe o
metodo. Se o tipo estiver errado, o produto nasce da classe errada e todo o
despacho a jusante -- incluindo o que construiria as classes de 1D7FCC/1A9D24 --
indexa para o sitio errado. Nada disto se veria: nao ha' crash, ha' um objecto
plausivel do tipo errado.

Esta probe mede, sem adivinhar:

  1. que TIPO e' pedido (r1+0x72), e a distribuicao dele;
  2. que ENTRADA a tabela devolve para esse tipo -- e se e' plausivel;
  3. que VTABLE e CONSTRUTOR saem dessa entrada;
  4. que PRODUTO o icall1 devolve (r3), e o seu vptr;
  5. o campo de tipo do proprio produto (produto+0x2), que e' o que o icall2
     de attach usa a seguir -- e' aqui que a cadeia continua ou morre.

O ponto 5 e' o mais importante: se `vm_read16(produto+0x2)` for 0 ou lixo, o
attach indexa para nada e a cascata inteira para -- que e' exactamente o
sintoma (`f4=0` terminal, CC9D0 a girar 7,8M vezes sobre dois objectos).

Gate (default OFF, regra 6 do CLAUDE.md)
  PS3_TRACE_CB56C_TYPE=1  -> ON so' se o primeiro caracter for '1'
  ausente / '' / '0' / outro  -> OFF, no-op total no baseline

Telemetria
  [CB56CTY] pre  tipo=0xNNNN idx=0xNN tab=0x........ ent=0x........
            vt=0x........ ctor_opd=0x........ code=0x........ post=P
  [CB56CTY] post prod=0x........ prod_vt=0x........ prod_tipo=0xNNNN post=P
  [CB56CTY] SUMMARY calls=N rperma_full=R
  [CB56CTY] SUMMARY tipo=0xNNNN n=K ent=0x........ code=0x........
  [CB56CTY] SUMMARY prod_tipo=0xNNNN n=K            <- o que o attach vai usar
  [CB56CTY] SUMMARY prod_tipo INVALIDO (0 ou >0x3FFF): K de N

A ultima linha e' impressa SEMPRE, mesmo a 0/0: e' o veredicto da probe.

NAO altera fluxo de controlo guest -- le' `ctx` e memoria guest, conta, e sai.
Todas as leituras sao guardadas por range check (0x10000..0x4F000000), o mesmo
que os blocos TYPE15 ja' em producao usam.

Convivencia com outros patches (verificada em 2026-07-30):
  patch_type15_cb56c_highbit.py     ancora em `vm_read16(ctx->gpr[1] + 0x72)`
  patch_type15_cb56c_prefer_*.py    ancora no par rldicl/or do produto
  patch_type15_cc9d0_base_blocks.py substitui o bloco do icall2 (CB_CLEAN)
Esta probe NAO toca em nenhuma dessas linhas: insere ANTES do par
`r11 = vm_read32(r27+r9)` / `r3 = r11|r11` e DEPOIS do `TOCFIX` que fecha o
icall1. As agulhas deles ficam byte a byte intactas.

Idempotente: marcador presente -> ALREADY, rc=0.
rc: 0 aplicado ou ja'-aplicado; 2 alvo inexistente; 3 agulha em falta.
"""
from __future__ import annotations

import sys
from pathlib import Path

MARKER = "CB56C-TYPE-PROBE"
CHUNK = "ppu_recomp_000.cpp"
FN_SIG = "void func_000CB56C(ppu_context* ctx) {\n"

# Agulha PRE: o par que resolve a tabela. Procurado DENTRO do corpo do CB56C.
PRE_NEEDLE = ("        ctx->gpr[11] = vm_read32((ctx->gpr[27] + ctx->gpr[9]));\n"
              "        ctx->gpr[3] = ctx->gpr[11] | ctx->gpr[11];\n")
# Agulha POST: o fecho do icall1 (a 1a ocorrencia DEPOIS da agulha PRE).
POST_NEEDLE = ("        ps3_indirect_call(ctx); DRAIN_TRAMPOLINE(ctx);\n"
               "        ctx->gpr[2] = 0x00541178ULL; /*TOCFIX ld r2,N(r1)*/\n")

HELPER = r'''
/* CB56C-TYPE-PROBE: o tipo pedido ao construct e' plausivel? (PS3_TRACE_CB56C_TYPE=1) */
#include <signal.h>

/* g_ps3_rperma_full e' definido por outro patch (WADLD) que pode nao ter corrido
 * neste lift. Uma DECLARACAO `weak` nao serve: no Mach-O/arm64 da undefined
 * symbol no link -- medido em 2026-07-30. Uma DEFINICAO fraca linka sempre e
 * cede o lugar a definicao forte quando ela existe. O guard evita a redefinicao
 * quando os dois probes desta leva caem no mesmo chunk. */
#ifndef PS3_RPERMA_WEAK_DEFINED
#define PS3_RPERMA_WEAK_DEFINED 1
extern "C" __attribute__((weak)) volatile int g_ps3_rperma_full = 0;
#endif
static int ps3_cbty_rperma(void) { return (int)g_ps3_rperma_full; }

enum { PS3_CBTY_SLOTS = 64 };
static unsigned int       g_ps3_cbty_tipo[PS3_CBTY_SLOTS];
static unsigned int       g_ps3_cbty_ent[PS3_CBTY_SLOTS];
static unsigned int       g_ps3_cbty_code[PS3_CBTY_SLOTS];
static unsigned long long g_ps3_cbty_n[PS3_CBTY_SLOTS];
static int                g_ps3_cbty_used = 0;

static unsigned int       g_ps3_cbty_ptipo[PS3_CBTY_SLOTS];
static unsigned long long g_ps3_cbty_pn[PS3_CBTY_SLOTS];
static int                g_ps3_cbty_pused = 0;

static unsigned long long g_ps3_cbty_calls = 0;
static unsigned long long g_ps3_cbty_prods = 0;
static unsigned long long g_ps3_cbty_bad = 0;
static unsigned long long g_ps3_cbty_ovf = 0;
static unsigned long long g_ps3_cbty_log_pre = 0;
static unsigned long long g_ps3_cbty_log_post = 0;
static int g_ps3_cbty_reg = 0;
static int g_ps3_cbty_gate = -1;

static int ps3_cbty_gate_on(void) {
    if (g_ps3_cbty_gate < 0) {
        extern char* getenv(const char*);
        const char* e = getenv("PS3_TRACE_CB56C_TYPE");
        g_ps3_cbty_gate = (e && *e == '1') ? 1 : 0;
    }
    return g_ps3_cbty_gate;
}

/* Mesma banda que os blocos TYPE15 ja' em producao usam para decidir se um EA
 * guest e' derefenciavel. Sem isto a probe podia ser ELA a rebentar o boot. */
static int ps3_cbty_ok(unsigned int ea) {
    return ea >= 0x10000u && ea < 0x4F000000u;
}
static unsigned int ps3_cbty_rd32(unsigned int ea) {
    return ps3_cbty_ok(ea) ? (unsigned int)vm_read32(ea) : 0u;
}
static unsigned int ps3_cbty_rd16(unsigned int ea) {
    return ps3_cbty_ok(ea) ? (unsigned int)vm_read16(ea) : 0xFFFFu;
}

static void ps3_cbty_dump_summary(void) {
    if (!ps3_cbty_gate_on()) return;
    /* SIGTERM e atexit podem ambos disparar (o boot e' morto por TERM depois de
     * um timeout). Sem esta guarda o SUMMARY sai duas vezes. */
    static int dumped = 0;
    if (dumped) return;
    dumped = 1;
    fprintf(stderr, "[CB56CTY] SUMMARY calls=%llu rperma_full=%d\n",
            g_ps3_cbty_calls, ps3_cbty_rperma());
    for (int i = 0; i < g_ps3_cbty_used; i++) {
        fprintf(stderr, "[CB56CTY] SUMMARY tipo=0x%04X n=%llu ent=0x%08X code=0x%08X\n",
                g_ps3_cbty_tipo[i], g_ps3_cbty_n[i],
                g_ps3_cbty_ent[i], g_ps3_cbty_code[i]);
    }
    for (int i = 0; i < g_ps3_cbty_pused; i++) {
        fprintf(stderr, "[CB56CTY] SUMMARY prod_tipo=0x%04X n=%llu\n",
                g_ps3_cbty_ptipo[i], g_ps3_cbty_pn[i]);
    }
    if (g_ps3_cbty_ovf) {
        fprintf(stderr, "[CB56CTY] SUMMARY AVISO: %llu valores novos alem dos %d "
                        "slots -- histograma TRUNCADO\n",
                g_ps3_cbty_ovf, (int)PS3_CBTY_SLOTS);
    }
    /* O veredicto. Impresso SEMPRE, mesmo 0 de 0: um zero explicito distingue
     * "nunca aconteceu" de "a probe nao correu", a ausencia de linha nao. */
    fprintf(stderr, "[CB56CTY] SUMMARY prod_tipo INVALIDO (0 ou >0x3FFF): %llu de %llu\n",
            g_ps3_cbty_bad, g_ps3_cbty_prods);
    fflush(stderr);
}

/* ENCADEAMENTO -- ver a nota longa no probe 29AF0. `signal()` substitui em vez
 * de acumular; sem isto o ultimo probe a armar rouba o SIGTERM aos outros e o
 * SUMMARY deles nunca sai. Medido a serio em 2026-07-30. */
typedef void (*ps3_cbty_sigh_t)(int);
static ps3_cbty_sigh_t g_ps3_cbty_prev_sigterm = 0;

static void ps3_cbty_on_sigterm(int sig) {
    ps3_cbty_dump_summary();
    if (g_ps3_cbty_prev_sigterm
        && g_ps3_cbty_prev_sigterm != SIG_DFL
        && g_ps3_cbty_prev_sigterm != SIG_IGN) {
        g_ps3_cbty_prev_sigterm(sig);
    }
}

static void ps3_cbty_arm(void) {
    if (g_ps3_cbty_reg) return;
    g_ps3_cbty_reg = 1;
    atexit(ps3_cbty_dump_summary);
#ifndef _WIN32
    g_ps3_cbty_prev_sigterm = signal(SIGTERM, ps3_cbty_on_sigterm);
#endif
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((constructor))
#endif
static void ps3_cbty_ctor(void) {
    if (!ps3_cbty_gate_on()) return;
    fprintf(stderr, "[CB56CTY] probe armed (icall1 construct: tipo r1+0x72 -> "
                    "tabela r2-0x5EA0 -> vtable -> slot5)\n");
    fflush(stderr);
    ps3_cbty_arm();
}

/* Chamada com r27 (base da tabela), r9 (indice*4) e r11 (entrada) ja' vivos. */
void ps3_cbty_pre(ppu_context* ctx) {
    if (!ps3_cbty_gate_on() || !ctx) return;
    ps3_cbty_arm();
    g_ps3_cbty_calls++;

    unsigned int idx4 = (unsigned int)ctx->gpr[9];
    unsigned int tab  = (unsigned int)ctx->gpr[27];
    unsigned int ent  = (unsigned int)ctx->gpr[11];
    /* o tipo cru, relido da origem -- rlwinm(x,2,14,29) = (x<<2) & 0x3FFFC */
    unsigned int tipo = ps3_cbty_rd16((unsigned int)ctx->gpr[1] + 0x72u);
    unsigned int vt   = ps3_cbty_rd32(ent);
    unsigned int opd  = ps3_cbty_rd32(vt + 0x14u);
    unsigned int code = ps3_cbty_rd32(opd);

    int slot = -1;
    for (int i = 0; i < g_ps3_cbty_used; i++)
        if (g_ps3_cbty_tipo[i] == tipo) { slot = i; break; }
    if (slot < 0) {
        if (g_ps3_cbty_used < PS3_CBTY_SLOTS) {
            slot = g_ps3_cbty_used++;
            g_ps3_cbty_tipo[slot] = tipo;
            g_ps3_cbty_ent[slot]  = ent;
            g_ps3_cbty_code[slot] = code;
            g_ps3_cbty_n[slot]    = 0;
        } else { g_ps3_cbty_ovf++; }
    }
    if (slot >= 0) g_ps3_cbty_n[slot]++;

    if (g_ps3_cbty_log_pre < 16) {
        g_ps3_cbty_log_pre++;
        fprintf(stderr, "[CB56CTY] pre  tipo=0x%04X idx=0x%X tab=0x%08X ent=0x%08X "
                        "vt=0x%08X ctor_opd=0x%08X code=0x%08X post=%d\n",
                tipo, idx4, tab, ent, vt, opd, code, ps3_cbty_rperma());
        fflush(stderr);
    }
}

/* Chamada logo depois do icall1: r3 = produto devolvido pelo construct. */
void ps3_cbty_post(ppu_context* ctx) {
    if (!ps3_cbty_gate_on() || !ctx) return;
    unsigned int prod = (unsigned int)ctx->gpr[3];
    unsigned int pvt  = ps3_cbty_rd32(prod);
    /* O campo que o icall2 de attach vai indexar a seguir. 0xFFFF = ilegivel. */
    unsigned int ptipo = ps3_cbty_rd16(prod + 0x2u);
    g_ps3_cbty_prods++;
    /* rlwinm(x,2,14,29) so' preserva 14 bits uteis: acima de 0x3FFF o indice
     * dobra sobre si proprio, e 0 e' o valor de um shell por preencher. */
    if (ptipo == 0u || ptipo > 0x3FFFu) g_ps3_cbty_bad++;

    int slot = -1;
    for (int i = 0; i < g_ps3_cbty_pused; i++)
        if (g_ps3_cbty_ptipo[i] == ptipo) { slot = i; break; }
    if (slot < 0) {
        if (g_ps3_cbty_pused < PS3_CBTY_SLOTS) {
            slot = g_ps3_cbty_pused++;
            g_ps3_cbty_ptipo[slot] = ptipo;
            g_ps3_cbty_pn[slot] = 0;
        } else { g_ps3_cbty_ovf++; }
    }
    if (slot >= 0) g_ps3_cbty_pn[slot]++;

    if (g_ps3_cbty_log_post < 16) {
        g_ps3_cbty_log_post++;
        fprintf(stderr, "[CB56CTY] post prod=0x%08X prod_vt=0x%08X prod_tipo=0x%04X post=%d\n",
                prod, pvt, ptipo, ps3_cbty_rperma());
        fflush(stderr);
    }
}
'''


def patch(path: Path) -> int:
    t = path.read_text(encoding="utf-8", errors="replace")

    if f"/* {MARKER} */" in t:
        print(f"  {path.name}: ALREADY (marcador presente)")
        return 0

    if FN_SIG not in t:
        print(f"ERRO: {path.name} nao tem {FN_SIG.strip()}", file=sys.stderr)
        return 3
    if t.count(FN_SIG) != 1:
        print(f"ERRO: {FN_SIG.strip()} aparece {t.count(FN_SIG)}x (esperado 1)",
              file=sys.stderr)
        return 3

    # Restringe as agulhas ao CORPO do CB56C: as duas sequencias sao comuns no
    # chunk inteiro (o TOCFIX aparece milhares de vezes) e uma procura global
    # aplicaria a probe a uma funcao qualquer.
    body_start = t.find(FN_SIG) + len(FN_SIG)
    body_end = t.find("\n}\n", body_start)
    if body_end < 0:
        print(f"ERRO: nao encontrei o fim de func_000CB56C", file=sys.stderr)
        return 3
    body = t[body_start:body_end]

    n_pre = body.count(PRE_NEEDLE)
    if n_pre != 1:
        print(f"ERRO: agulha PRE aparece {n_pre}x no corpo do CB56C (esperado 1) -- "
              f"o lift mudou de forma; NAO aplicado", file=sys.stderr)
        return 3

    pre_at = body.find(PRE_NEEDLE)
    post_at = body.find(POST_NEEDLE, pre_at + len(PRE_NEEDLE))
    if post_at < 0:
        print(f"ERRO: agulha POST (fecho do icall1) nao existe depois da PRE -- "
              f"NAO aplicado", file=sys.stderr)
        return 3

    # Aplica de tras para a frente: inserir na PRE deslocaria o offset da POST.
    body = (body[:post_at + len(POST_NEEDLE)]
            + f"        /* {MARKER} post */\n        {{ ps3_cbty_post(ctx); }}\n"
            + body[post_at + len(POST_NEEDLE):])
    body = (body[:pre_at + len(PRE_NEEDLE)]
            + f"        /* {MARKER} */\n        {{ ps3_cbty_pre(ctx); }}\n"
            + body[pre_at + len(PRE_NEEDLE):])

    t = t[:body_start] + body + t[body_end:]

    # Ancora no FIM da zona de preambulos (mesmo antes da 1a funcao lifted), nao
    # a seguir ao #include. Razao medida em 2026-07-30: no clang/Mach-O os
    # constructors correm por ORDEM DE DEFINICAO no ficheiro, e quem arma por
    # ULTIMO fica com o SIGTERM. Ancorado no include, este probe armava antes do
    # CD498-PROBE e perdia-lhe o sinal; o SUMMARY nunca saia.
    anchor_at = t.find("\nvoid func_")
    if anchor_at < 0:
        print(f"ERRO: {path.name} nao tem nenhuma funcao lifted onde ancorar",
              file=sys.stderr)
        return 3
    t = t[:anchor_at + 1] + HELPER + t[anchor_at + 1:]

    try:
        path.write_text(t, encoding="utf-8", newline="\n")
    except TypeError:                                # macOS system python3 (3.9.6)
        path.write_text(t, encoding="utf-8")
    print(f"  {path.name}: APPLIED (pre-icall1 + post-icall1)")
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
