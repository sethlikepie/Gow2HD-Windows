#!/usr/bin/env python3
"""Enter probes para func_000CD7B4 e func_000CD498 (instalador do estado TYPE15).

Contexto
--------
notes/2026-07-25-f4-e-um-enum-de-estado.md: `f4` e' o campo em +0x4 do objecto
TYPE15 e e' um ENUM de estado {0,1,2,5,6,7,8,9}. `func_000CC9D0` e' o tick da
maquina: 6->1, 5->8, 7->2, 8->0. Com f4=0 nenhum case casa -- estado TERMINAL.

Varrendo os 13.096 corpos decompilados pelo Ghidra, NINGUEM escreve 5/6/7 no
+0x4. O unico candidato a instalar um estado de arranque e' func_000CD498
(`+4 = 9`), chamado por func_000CD7B4. Dai a pergunta desta probe:

    CD7B4 -> CD498 chega a correr no boot? E antes ou depois do R_PermA?

Se tot=0 nos dois, o "install de trabalho" nunca acontece e o wall do Gate A
tem um culpado a montante. Se correm mas f4 continua 0, o culpado esta' dentro
deles (early-return) -- e ai a probe de r3/rc discrimina.

Gate (default OFF, regra 6 do CLAUDE.md)
  PS3_TRACE_CD498=1  -> ON so' se o primeiro caracter for '1'
  ausente / '' / '0' / outro  -> OFF, no-op no baseline

Telemetria
  [CD498] enter fn=CD7B4 tot=N post=M r3=0x.. r4=0x..
  [CD498] SUMMARY fn=... tot=N post=M pre=K      <- impresso SEMPRE, mesmo a 0
O SUMMARY imprime todos os sitios mesmo com tot=0: a ausencia de linha e'
ambigua, um zero explicito nao e'.

Idempotente: MARKER presente -> ALREADY. Nao altera fluxo de controlo guest.
"""
from __future__ import annotations

import sys
from pathlib import Path

MARKER = "CD498-PROBE"

# (nome curto, simbolo, cap de linhas de log)
SITES = [
    ("CD7B4", "func_000CD7B4", 8),
    ("CD498", "func_000CD498", 8),
    # topo da cadeia: CD9DC nao tem chamador estatico nem referencia ao seu OPD
    # em lado nenhum da imagem -- so' entra por despacho indirecto (ou nunca).
    ("CD9DC", "func_000CD9DC", 8),
    # candidatos encontrados por varrimento de instrucoes em todo o .text:
    # `li rX,{5,6,7,9}` seguido de `stw rX, 0x4(rY)` -- os estados de arranque
    # que faltavam. NAO esta' provado que operem no objecto TYPE15; a probe
    # existe precisamente para nao adivinhar.
    ("1D7FCC", "func_001D7FCC", 8),
    ("1A9D24", "func_001A9D24", 8),
    # CONTROLO POSITIVO: CC9D0 e' o tick que as notas mostram em spin
    # constante. Se ele der tot=0 tambem, a probe esta' partida e nenhum
    # dos zeros acima significa nada.
    ("CC9D0", "func_000CC9D0", 3),
    # Construtores das duas classes cujas vtables contem 1D7FCC/1A9D24 no
    # slot 2 (+8). Carregam a vtable do TOC (0xCAE0 / 0xCBBC) logo a
    # entrada. Se nenhum correr, a classe nunca e' construida e nenhum
    # despacho pode alcancar o instalador de estado.
    ("C96014", "func_00196014", 2),
    ("C96FF0", "func_00196FF0", 2),
    ("C9DCF4", "func_0019DCF4", 2),
    ("C9FBE0", "func_0019FBE0", 2),
    ("CA3AB0", "func_001A3AB0", 2),
    ("CA3B68", "func_001A3B68", 2),
    ("CAD870", "func_001AD870", 2),
    ("CADA28", "func_001ADA28", 2),
    ("CADBD4", "func_001ADBD4", 2),
    ("CCAE1C", "func_001CAE1C", 2),
    ("CCAE90", "func_001CAE90", 2),
    ("CDAFB4", "func_001DAFB4", 2),
]

HELPER_BLOCK = r'''
/* CD498-PROBE: instalador do estado TYPE15 chega a correr? (PS3_TRACE_CD498=1) */
#include <signal.h>
extern "C" volatile int g_ps3_rperma_full;
enum { PS3_CD498_FN_N = 18 };
static const char* const g_ps3_cd498_fn_name[PS3_CD498_FN_N] =
    { "CD7B4", "CD498", "CD9DC", "1D7FCC", "1A9D24", "CC9D0(ctrl)",
      "C96014", "C96FF0", "C9DCF4", "C9FBE0", "CA3AB0", "CA3B68", "CAD870", "CADA28", "CADBD4", "CCAE1C", "CCAE90", "CDAFB4" };
static const int g_ps3_cd498_log_cap[PS3_CD498_FN_N] = { 8, 8, 8, 8, 8, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2 };
unsigned long long g_ps3_cd498_tot[PS3_CD498_FN_N];
unsigned long long g_ps3_cd498_post[PS3_CD498_FN_N];
static unsigned long long g_ps3_cd498_logged[PS3_CD498_FN_N];
static int g_ps3_cd498_atexit_reg = 0;
static int g_ps3_cd498_gate = -1;

static int ps3_cd498_gate_on(void) {
    if (g_ps3_cd498_gate < 0) {
        extern char* getenv(const char*);
        const char* e = getenv("PS3_TRACE_CD498");
        g_ps3_cd498_gate = (e && *e == '1') ? 1 : 0;
    }
    return g_ps3_cd498_gate;
}

static void ps3_cd498_dump_summary(void) {
    if (!ps3_cd498_gate_on()) return;
    fprintf(stderr, "[CD498] SUMMARY rperma_full=%d\n", (int)g_ps3_rperma_full);
    for (int i = 0; i < PS3_CD498_FN_N; i++) {
        unsigned long long t = g_ps3_cd498_tot[i];
        unsigned long long p = g_ps3_cd498_post[i];
        /* imprimir SEMPRE: tot=0 e' o resultado que interessa distinguir */
        fprintf(stderr, "[CD498] SUMMARY fn=%s tot=%llu post=%llu pre=%llu\n",
                g_ps3_cd498_fn_name[i], t, p, (t >= p ? t - p : 0));
    }
    fflush(stderr);
}

/* ENCADEAMENTO (defeito medido em 2026-07-30): `signal()` SUBSTITUI o
 * handler, nao o acumula. Com varios probes armados no mesmo binario, o
 * ultimo constructor a correr ficava com o SIGTERM e os outros nunca
 * imprimiam o SUMMARY. Guardar o handler anterior e chama-lo no fim mantem
 * a cadeia inteira viva, seja qual for a ordem de arranque -- mesmo padrao
 * ja provado em 29AF0/CB56C (commit 09c3850). */
typedef void (*ps3_cd498_sigh_t)(int);
static ps3_cd498_sigh_t g_ps3_cd498_prev_sigterm = 0;

static void ps3_cd498_on_sigterm(int sig) {
    ps3_cd498_dump_summary();
    if (g_ps3_cd498_prev_sigterm
        && g_ps3_cd498_prev_sigterm != SIG_DFL
        && g_ps3_cd498_prev_sigterm != SIG_IGN) {
        g_ps3_cd498_prev_sigterm(sig);
    }
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((constructor))
#endif
static void ps3_cd498_ctor(void) {
    if (!ps3_cd498_gate_on()) return;
    fprintf(stderr, "[CD498] probe armed (CD7B4/CD498; post=g_ps3_rperma_full)\n");
    fflush(stderr);
    if (!g_ps3_cd498_atexit_reg) {
        g_ps3_cd498_atexit_reg = 1;
        atexit(ps3_cd498_dump_summary);
#ifndef _WIN32
        g_ps3_cd498_prev_sigterm = signal(SIGTERM, ps3_cd498_on_sigterm);
#endif
    }
}

void ps3_cd498_on_enter(int id, ppu_context* ctx) {
    if (!ps3_cd498_gate_on()) return;
    if (id < 0 || id >= PS3_CD498_FN_N || !ctx) return;
    if (!g_ps3_cd498_atexit_reg) {
        g_ps3_cd498_atexit_reg = 1;
        atexit(ps3_cd498_dump_summary);
    }
    unsigned long long t = ++g_ps3_cd498_tot[id];
    int post = (g_ps3_rperma_full != 0);
    if (post) g_ps3_cd498_post[id]++;
    /* Discriminador: para o CC9D0 (id 5, o objecto VIVO) ler o vptr do
     * objecto e compara-lo com os vptr das duas classes que detem o metodo
     * instalador de estado (1D7FCC -> 0x00513F40, 1A9D24 -> 0x00513520).
     * Binario: igual -> mesma classe, construida por caminho nao instrumentado;
     * diferente -> o f4 daquele objecto nao e' o mesmo campo semantico. */
    if (id == 5) {
        static int vt_logged = 0;
        if (vt_logged < 6) {
            vt_logged++;
            uint32_t obj = (uint32_t)ctx->gpr[3];
            uint32_t vt  = obj ? vm_read32(obj) : 0u;
            const char* verdict =
                (vt == 0x00513F40u) ? "IGUAL a classe-1D7FCC"
              : (vt == 0x00513520u) ? "IGUAL a classe-1A9D24"
              : "DIFERENTE das duas";
            fprintf(stderr, "[CD498] VPTR obj=0x%08X vptr=0x%08X  %s\n",
                    obj, vt, verdict);
            fflush(stderr);
        }
    }
    if (g_ps3_cd498_logged[id] < (unsigned long long)g_ps3_cd498_log_cap[id]) {
        g_ps3_cd498_logged[id]++;
        fprintf(stderr,
                "[CD498] enter fn=%s tot=%llu post=%d r3=0x%08X r4=0x%08X\n",
                g_ps3_cd498_fn_name[id], t, post,
                (unsigned)ctx->gpr[3], (unsigned)ctx->gpr[4]);
        fflush(stderr);
    }
}
'''


# ---- upgrade-in-place: encadear o SIGTERM num bloco JA baked (codigo antigo) --
# O guard de idempotencia do apply_all_patches.sh olha so' para o MARKER do
# bloco -- um lift que ja tem o HELPER_BLOCK antigo (signal() sem guardar o
# handler anterior) ficava para sempre "ALREADY", nunca actualizado. Este
# ramo faz uma substituicao textual cirurgica APENAS das duas linhas antigas
# (o corpo do on_sigterm e o unico `signal(SIGTERM, ...)` do ctor), sem tocar
# em mais nada do bloco ja aplicado.
OLD_SIGTERM_HANDLER = (
    "static void ps3_cd498_on_sigterm(int sig) { (void)sig; ps3_cd498_dump_summary(); }"
)
NEW_SIGTERM_HANDLER = (
    "typedef void (*ps3_cd498_sigh_t)(int);\n"
    "static ps3_cd498_sigh_t g_ps3_cd498_prev_sigterm = 0;\n"
    "\n"
    "static void ps3_cd498_on_sigterm(int sig) {\n"
    "    ps3_cd498_dump_summary();\n"
    "    if (g_ps3_cd498_prev_sigterm\n"
    "        && g_ps3_cd498_prev_sigterm != SIG_DFL\n"
    "        && g_ps3_cd498_prev_sigterm != SIG_IGN) {\n"
    "        g_ps3_cd498_prev_sigterm(sig);\n"
    "    }\n"
    "}"
)
OLD_SIGNAL_CALL = "        signal(SIGTERM, ps3_cd498_on_sigterm);\n"
NEW_SIGNAL_CALL = "        g_ps3_cd498_prev_sigterm = signal(SIGTERM, ps3_cd498_on_sigterm);\n"


def upgrade_sigterm_chain(t: str, path: Path) -> str:
    if "g_ps3_cd498_prev_sigterm" in t:
        print(f"  helpers: already in {path.name}")
        return t
    n1 = t.count(OLD_SIGTERM_HANDLER)
    if n1 != 1:
        raise SystemExit(
            f"{path}: OLD_SIGTERM_HANDLER aparece {n1}x (esperado 1) -- upgrade abortado"
        )
    t = t.replace(OLD_SIGTERM_HANDLER, NEW_SIGTERM_HANDLER, 1)
    n2 = t.count(OLD_SIGNAL_CALL)
    if n2 != 1:
        raise SystemExit(
            f"{path}: OLD_SIGNAL_CALL aparece {n2}x (esperado 1) -- upgrade abortado"
        )
    t = t.replace(OLD_SIGNAL_CALL, NEW_SIGNAL_CALL, 1)
    print(f"  helpers: UPGRADED in {path.name}")
    return t


def patch_file(path: Path) -> bool:
    t = path.read_text(encoding="utf-8", errors="replace")
    orig = t

    if "/* CD498-PROBE: instalador" not in t:
        anchor = "static int g_ps3_type15_disc_n = 0;\n"
        if anchor in t:
            t = t.replace(anchor, anchor + HELPER_BLOCK, 1)
        else:
            alt = "#include <stdlib.h>\n"
            if alt not in t:
                raise SystemExit(f"{path}: no place for helper block")
            t = t.replace(alt, alt + HELPER_BLOCK, 1)
        print(f"  helpers: added to {path.name}")
    else:
        t = upgrade_sigterm_chain(t, path)

    applied = 0
    for site_id, (short, fn, _cap) in enumerate(SITES):
        sig = f"void {fn}(ppu_context* ctx) {{\n"
        if sig not in t:
            raise SystemExit(f"{path}: missing signature for {fn}")
        if t.count(sig) != 1:
            raise SystemExit(f"{path}: {fn} signature count={t.count(sig)}")
        idx = t.find(sig)
        region_start = idx + len(sig)
        # Idempotencia: procurar no FICHEIRO INTEIRO, nao numa janela a seguir
        # a' assinatura. Outro patch pode injectar no topo da mesma funcao e
        # empurrar a nossa chamada para fora da janela -- foi o que aconteceu
        # com o id=5 (CC9D0), injectado DUAS vezes depois de um instalador de
        # bloco entrar antes. O id e' unico, logo a procura global e' exacta.
        if f"ps3_cd498_on_enter({site_id}," in t:
            print(f"  {fn} ({short}): ALREADY")
            continue
        call = (f"        /* {MARKER} id={site_id} */\n"
                f"        {{ ps3_cd498_on_enter({site_id}, ctx); }}\n")
        t = t[:region_start] + call + t[region_start:]
        print(f"  {fn} ({short}): APPLIED id={site_id}")
        applied += 1

    if t == orig:
        return False
    try:
        path.write_text(t, encoding="utf-8", newline="\n")
    except TypeError:                                          # Python antigo
        path.write_text(t, encoding="utf-8")
    return True


def main() -> int:
    root = Path(sys.argv[1]) if len(sys.argv) > 1 else \
        Path(__file__).resolve().parent.parent / "recomp_macos_v2"
    target = root / "ppu_recomp_000.cpp" if root.is_dir() else root
    if not target.is_file():
        print(f"ERRO: {target} nao existe", file=sys.stderr)
        return 2
    print(f"[{MARKER}] {target}")
    changed = patch_file(target)
    print(f"[{MARKER}] {'escrito' if changed else 'sem alteracoes (ja aplicado)'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
