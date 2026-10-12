#!/usr/bin/env python3
"""R5 — enter probes for func_00017ACC + static callers (pre vs post R_Perm).

Context
-------
Only flip issuer pre-WAD is the func_00017ACC band (guest EA 0x17ADE/0x17ADF).
Zero SetFlip after R_LglScA open. This probe answers: who calls 17ACC, and does
any enter happen AFTER R_PermA full (g_ps3_rperma_full)?

Targets (static direct callers) — ver SITES abaixo; o chunk de cada uma e'
DESCOBERTO, ja' nao esta' escrito no script.

Gate (default OFF) — M1 disc contract:
  PS3_TRACE_17ACC=1  → ON only if first char is '1'
  unset / empty / '0' / 'false' / anything else → OFF (no-op baseline)

Telemetry
---------
Shared counters (defined in the helper chunk, used by the others via extern):
  g_ps3_17acc_tot[id]  — all enters
  g_ps3_17acc_post[id] — enters while g_ps3_rperma_full != 0
post_rperm uses the existing movie_hle flag (set on R_PermA full) — no new
runtime mark required.

Log format (capped):
  [17ACC] enter fn=17ACC tot=N post=M r3=0x.. r4=0x..
  first 8 of 17ACC, first 4 of each caller; summary every 10000 grand or atexit.

Idempotent: MARKER present → ALREADY. Region-scoped: inject right after each
`void func_... {` signature line. Does not alter guest control flow when OFF.

CORRECCAO 2026-07-25 (chunk-fixo)
---------------------------------
O script tinha o chunk de cada funcao HARDCODED na tabela SITES ("000"/"001")
e so' abria ppu_recomp_000.cpp e ppu_recomp_001.cpp por nome. O lifter passou
de 31 para 7 chunks e as funcoes migraram: func_0001E34C, por exemplo, deixou
de estar no chunk 000 e passou para o 001 — o que fazia o script abortar com
"missing signature for func_0001E34C" (SystemExit) depois de ja' ter escrito
metade das sondas.
Agora:
  - usa resolve_lift_paths() (aceita DIRECTORIO -> todos os chunks);
  - descobre em que chunk vive cada funcao (passagem 1) e so' depois injecta
    (passagem 2);
  - o bloco de helpers vai para o chunk que define func_00017ACC e todos os
    outros chunks tocados recebem o extern;
  - agulha tolerante: se o lifter tiver fundido o fragmento e a funcao so'
    existir como label "loc_XXXXXXXX:", injecta a seguir ao label;
  - um site em falta deixa de abortar o resto (fica visivel no relatorio e no
    codigo de saida), para nao deixar o lift meio-patchado.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

from lift_paths import resolve_lift_paths

MARKER = "17ACC-PROBE"
LOG_TAG = "[17ACC] enter"

# (short name, function symbol, log cap) — a ORDEM define o id usado nas
# tabelas do HELPER_BLOCK; nao reordenar sem actualizar o bloco.
SITES = [
    ("17ACC", "func_00017ACC", 8),
    ("1E1A8", "func_0001E1A8", 4),
    ("1E34C", "func_0001E34C", 4),
    ("1EAD8", "func_0001EAD8", 4),
    ("25064", "func_00025064", 4),
    ("25614", "func_00025614", 4),
    ("1E3DC", "func_0001E3DC", 4),
    ("1E3E8", "func_0001E3E8", 4),
    ("1E400", "func_0001E400", 4),
    ("1E4AC", "func_0001E4AC", 4),
]

HELPER_BLOCK = r'''
/* 17ACC-PROBE: R5 enter counters pre vs post R_Perm (PS3_TRACE_17ACC=1) */
#include <signal.h>
extern "C" volatile int g_ps3_rperma_full;
enum { PS3_17ACC_FN_N = 10 };
static const char* const g_ps3_17acc_fn_name[PS3_17ACC_FN_N] = {
    "17ACC","1E1A8","1E34C","1EAD8","25064","25614","1E3DC","1E3E8","1E400","1E4AC"
};
static const int g_ps3_17acc_log_cap[PS3_17ACC_FN_N] = {
    8,4,4,4,4,4,4,4,4,4
};
unsigned long long g_ps3_17acc_tot[PS3_17ACC_FN_N];
unsigned long long g_ps3_17acc_post[PS3_17ACC_FN_N];
unsigned long long g_ps3_17acc_grand;
static int g_ps3_17acc_atexit_reg = 0;
static int g_ps3_17acc_gate = -1;

static int ps3_17acc_gate_on(void) {
    if (g_ps3_17acc_gate < 0) {
        extern char* getenv(const char*);
        const char* e = getenv("PS3_TRACE_17ACC");
        /* M1 disc contract: ON only if first char is '1'. */
        g_ps3_17acc_gate = (e && *e == '1') ? 1 : 0;
    }
    return g_ps3_17acc_gate;
}

static void ps3_17acc_dump_summary(void) {
    if (!ps3_17acc_gate_on()) return;
    fprintf(stderr, "[17ACC] SUMMARY grand=%llu rperma_full=%d\n",
            (unsigned long long)g_ps3_17acc_grand, (int)g_ps3_rperma_full);
    for (int i = 0; i < PS3_17ACC_FN_N; i++) {
        unsigned long long t = g_ps3_17acc_tot[i];
        unsigned long long p = g_ps3_17acc_post[i];
        /* Always print every site so tot=0 is visible. */
        fprintf(stderr,
                "[17ACC] SUMMARY fn=%s tot=%llu post=%llu pre=%llu\n",
                g_ps3_17acc_fn_name[i],
                (unsigned long long)t,
                (unsigned long long)p,
                (unsigned long long)(t >= p ? t - p : 0));
    }
    fflush(stderr);
}

/* ENCADEAMENTO (defeito medido em 2026-07-30): signal() SUBSTITUI o handler,
 * nao o acumula -- ver a nota longa em patch_29af0_switch_probe.py (commit
 * 09c3850). Guardar o handler anterior e chama-lo no fim mantem a cadeia
 * inteira viva, seja qual for a ordem de arranque. */
typedef void (*ps3_17acc_sigh_t)(int);
static ps3_17acc_sigh_t g_ps3_17acc_prev_sigterm = 0;

static void ps3_17acc_on_sigterm(int sig) {
    ps3_17acc_dump_summary();
    if (g_ps3_17acc_prev_sigterm
        && g_ps3_17acc_prev_sigterm != SIG_DFL
        && g_ps3_17acc_prev_sigterm != SIG_IGN) {
        g_ps3_17acc_prev_sigterm(sig);
    }
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((constructor))
#endif
static void ps3_17acc_ctor(void) {
    if (!ps3_17acc_gate_on()) return;
    fprintf(stderr, "[17ACC] probe armed (post=g_ps3_rperma_full)\n");
    fflush(stderr);
    if (!g_ps3_17acc_atexit_reg) {
        g_ps3_17acc_atexit_reg = 1;
        atexit(ps3_17acc_dump_summary);
#ifndef _WIN32
        g_ps3_17acc_prev_sigterm = signal(SIGTERM, ps3_17acc_on_sigterm);
#endif
    }
}

void ps3_17acc_on_enter(int id, ppu_context* ctx) {
    if (!ps3_17acc_gate_on()) return;
    if (id < 0 || id >= PS3_17ACC_FN_N || !ctx) return;
    if (!g_ps3_17acc_atexit_reg) {
        g_ps3_17acc_atexit_reg = 1;
        atexit(ps3_17acc_dump_summary);
    }
    unsigned long long t = ++g_ps3_17acc_tot[id];
    unsigned long long p = g_ps3_17acc_post[id];
    if (g_ps3_rperma_full) p = ++g_ps3_17acc_post[id];
    unsigned long long g = ++g_ps3_17acc_grand;
    int cap = g_ps3_17acc_log_cap[id];
    int do_log = (t <= (unsigned long long)cap) || ((g % 10000ull) == 0);
    if (do_log) {
        fprintf(stderr,
                "[17ACC] enter fn=%s tot=%llu post=%llu r3=0x%08X r4=0x%08X\n",
                g_ps3_17acc_fn_name[id],
                (unsigned long long)t,
                (unsigned long long)p,
                (unsigned)(uint32_t)ctx->gpr[3],
                (unsigned)(uint32_t)ctx->gpr[4]);
        fflush(stderr);
    }
    if ((g % 10000ull) == 0) ps3_17acc_dump_summary();
}
/* 17ACC-PROBE end helpers */
'''

EXTERN_DECL = (
    "/* 17ACC-PROBE extern (defs no chunk que define func_00017ACC) */\n"
    "void ps3_17acc_on_enter(int id, ppu_context* ctx);\n"
)

HELPER_HEAD = "/* 17ACC-PROBE: R5 enter counters"
EXTERN_HEAD = "/* 17ACC-PROBE extern"


def site_sig_re(fn: str) -> re.Pattern:
    """Assinatura da funcao (ancora preferida: entrada real da funcao)."""
    return re.compile(
        r"(?:^|\n)void[ \t]+" + re.escape(fn)
        + r"\(ppu_context\*[ \t]*ctx\)[ \t]*\{[ \t]*\n"
    )


def site_label_re(fn: str) -> re.Pattern:
    """Fallback: o lifter fundiu o fragmento e o EA so' existe como label.

    IMPORTANTE: e' so' fallback. O mesmo label pode aparecer noutro chunk como
    alvo interno de outra funcao, por isso a assinatura tem SEMPRE prioridade
    global (senao a sonda de entrada aterrava no meio de outra funcao).
    """
    ea = fn.replace("func_", "")
    return re.compile(r"(?:^|\n)loc_" + re.escape(ea) + r":[ \t]*\n")


def probe_call(site_id: int) -> str:
    return (
        f"        /* {MARKER} id={site_id} */\n"
        f"        {{ ps3_17acc_on_enter({site_id}, ctx); }}\n"
    )


# ---- upgrade-in-place: encadear o SIGTERM num bloco JA baked (codigo antigo) --
OLD_SIGTERM_HANDLER = (
    "static void ps3_17acc_on_sigterm(int sig) {\n"
    "    (void)sig;\n"
    "    ps3_17acc_dump_summary();\n"
    "}"
)
NEW_SIGTERM_HANDLER = (
    "typedef void (*ps3_17acc_sigh_t)(int);\n"
    "static ps3_17acc_sigh_t g_ps3_17acc_prev_sigterm = 0;\n"
    "\n"
    "static void ps3_17acc_on_sigterm(int sig) {\n"
    "    ps3_17acc_dump_summary();\n"
    "    if (g_ps3_17acc_prev_sigterm\n"
    "        && g_ps3_17acc_prev_sigterm != SIG_DFL\n"
    "        && g_ps3_17acc_prev_sigterm != SIG_IGN) {\n"
    "        g_ps3_17acc_prev_sigterm(sig);\n"
    "    }\n"
    "}"
)
OLD_SIGNAL_CALL = "        signal(SIGTERM, ps3_17acc_on_sigterm);\n"
NEW_SIGNAL_CALL = "        g_ps3_17acc_prev_sigterm = signal(SIGTERM, ps3_17acc_on_sigterm);\n"


def upgrade_sigterm_chain(t: str, path: Path) -> str:
    if "g_ps3_17acc_prev_sigterm" in t:
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


def add_helpers(t: str, path: Path) -> str:
    if HELPER_HEAD in t:
        return upgrade_sigterm_chain(t, path)
    anchor = "static int g_ps3_type15_disc_n = 0;\n"
    if anchor in t:
        t = t.replace(anchor, anchor + HELPER_BLOCK, 1)
    else:
        alt = "#include <stdlib.h>\n"
        if alt not in t:
            h = '#include "ppu_recomp.h"\n'
            if h not in t:
                raise SystemExit(f"{path}: no place for helper block")
            t = t.replace(h, h + HELPER_BLOCK, 1)
        else:
            t = t.replace(alt, alt + HELPER_BLOCK, 1)
    print(f"  helpers: added to {path.name}")
    return t


def add_extern(t: str, path: Path) -> str:
    if EXTERN_HEAD in t or HELPER_HEAD in t:
        print(f"  extern: already in {path.name}")
        return t
    alt = "#include <stdlib.h>\n"
    if alt in t:
        t = t.replace(alt, alt + EXTERN_DECL, 1)
    else:
        h = '#include "ppu_recomp.h"\n'
        if h not in t:
            raise SystemExit(f"{path}: no place for extern decl")
        t = t.replace(h, h + EXTERN_DECL, 1)
    print(f"  extern: added to {path.name}")
    return t


def main() -> int:
    paths = [p for p in resolve_lift_paths(
        sys.argv[1:],
        str(Path(__file__).resolve().parent.parent / "recomp_macos_v2"),
    ) if p.is_file()]
    if not paths:
        print("lift dir/ficheiros em falta", file=sys.stderr)
        return 2

    # --- passagem 1: descobrir em que chunk vive cada site ---
    # assinatura tem prioridade GLOBAL sobre o label (ver site_label_re).
    by_sig: dict[int, Path] = {}
    by_label: dict[int, Path] = {}
    for p in paths:
        t = p.read_text(encoding="utf-8", errors="replace")
        for site_id, (_short, fn, _cap) in enumerate(SITES):
            if site_id not in by_sig and site_sig_re(fn).search(t):
                by_sig[site_id] = p
            if site_id not in by_label and site_label_re(fn).search(t):
                by_label[site_id] = p
        del t

    where: dict[int, Path] = {}
    kind: dict[int, str] = {}
    for site_id in range(len(SITES)):
        if site_id in by_sig:
            where[site_id] = by_sig[site_id]
            kind[site_id] = "sig"
        elif site_id in by_label:
            where[site_id] = by_label[site_id]
            kind[site_id] = "label"

    missing = [SITES[i][1] for i in range(len(SITES)) if i not in where]
    helper_chunk = where.get(0) or (min(where.values()) if where else paths[0])

    # --- passagem 2: injectar, um chunk de cada vez ---
    rc = 0
    touched = sorted(set(where.values()) | {helper_chunk})
    for p in touched:
        print(f"== {p.name} ==")
        t = orig = p.read_text(encoding="utf-8", errors="replace")
        try:
            if p == helper_chunk:
                t = add_helpers(t, p)
            else:
                t = add_extern(t, p)
            for site_id, (short, fn, _cap) in enumerate(SITES):
                if where.get(site_id) != p:
                    continue
                rx = site_sig_re(fn) if kind[site_id] == "sig" else site_label_re(fn)
                m = rx.search(t)
                if not m:
                    print(f"  {fn} ({short}): ANCHOR PERDIDA")
                    rc = 1
                    continue
                ahead = t[m.end(): m.end() + 160]
                if f"ps3_17acc_on_enter({site_id}," in ahead:
                    print(f"  {fn} ({short}): ALREADY")
                    continue
                t = t[: m.end()] + probe_call(site_id) + t[m.end():]
                print(f"  {fn} ({short}): APPLIED id={site_id} [{kind[site_id]}]")
        except SystemExit as e:
            print(f"FAILED {p}: {e}")
            rc = 1
            continue
        if t != orig:
            try:
                p.write_text(t, encoding="utf-8", newline="\n")
            except TypeError:
                p.write_text(t, encoding="utf-8")
            print(f"APPLIED {p}")
        else:
            print(f"ALREADY-APPLIED {p}")

    if missing:
        print(f"SITES EM FALTA (sem assinatura nem label): {', '.join(missing)}")
        rc = 1
    print(f"sites: {len(where)}/{len(SITES)}")
    return rc


if __name__ == "__main__":
    raise SystemExit(main())
