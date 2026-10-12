#!/usr/bin/env python3
"""R8 — enter probes for parents of func_00156680 (intro present loop).

Context
-------
R6–R7 fixed the flip chain as:
  156680 → 14FE18 → 2EFD60 → HLE _cellGcmSetFlipCommand
with ~28k enters pre-WAD and **0 post R_Perm**. 2B2E74/B71B8 enter once only.

Static direct parents of 156680 (this probe):
  000: CDBA4, CE0A0 (nest), 194FFC
  001: 2B21C4, 2C07F8, CDC08, CDCF8, CDD00, CDD88 (movie cluster frags)
  002: 2B21F8, 2B2288

Goal: measure which parent is the hot intro loop (~28k) and whether any
fire after g_ps3_rperma_full (post thr / R_Perm).

Gate (default OFF) — M1 disc contract:
  PS3_TRACE_PRESENTLOOP=1  → ON only if first char is '1'
  unset / empty / '0' / anything else → OFF (no-op baseline)

Telemetry
---------
Shared counters (defined in 000, used by 001/002 via extern):
  g_ps3_plp_tot[id]  — all enters
  g_ps3_plp_post[id] — enters while g_ps3_rperma_full != 0

Log:
  [PRESENTLOOP] enter fn=... tot=N post=M r3=0x.. r4=0x..
  SUMMARY at atexit / SIGTERM / every 10000 grand.

Idempotent: MARKER present → ALREADY. Inject right after each
`void func_... {` signature line. Does not alter guest CF when OFF.
Skip missing functions (warn, do not fail whole file).
"""
from __future__ import annotations

import sys
from pathlib import Path

MARKER = "PRESENTLOOP-PROBE"
LOG_TAG = "[PRESENTLOOP] enter"

# (short name, function symbol, cpp chunk key, log cap)
# IDs must match g_ps3_plp_fn_name[] order in HELPER_BLOCK.
SITES = [
    ("CDBA4",  "func_000CDBA4",  "000", 4),
    ("CE0A0",  "func_000CE0A0",  "000", 4),
    ("194FFC", "func_00194FFC",  "000", 4),
    ("2B21C4", "func_002B21C4",  "001", 4),
    ("2C07F8", "func_002C07F8",  "001", 4),
    ("CDC08",  "func_000CDC08",  "001", 4),
    ("CDCF8",  "func_000CDCF8",  "001", 4),
    ("CDD00",  "func_000CDD00",  "001", 4),
    ("CDD88",  "func_000CDD88",  "001", 8),
    ("2B21F8", "func_002B21F8",  "002", 4),
    ("2B2288", "func_002B2288",  "002", 4),
]

HELPER_BLOCK = r'''
/* PRESENTLOOP-PROBE: R8 parents of 156680 pre vs post R_Perm (PS3_TRACE_PRESENTLOOP=1) */
#include <signal.h>
extern "C" volatile int g_ps3_rperma_full;
enum { PS3_PLP_FN_N = 11 };
static const char* const g_ps3_plp_fn_name[PS3_PLP_FN_N] = {
    "CDBA4","CE0A0","194FFC","2B21C4","2C07F8",
    "CDC08","CDCF8","CDD00","CDD88","2B21F8","2B2288"
};
static const int g_ps3_plp_log_cap[PS3_PLP_FN_N] = {
    4,4,4,4,4, 4,4,4,8,4,4
};
unsigned long long g_ps3_plp_tot[PS3_PLP_FN_N];
unsigned long long g_ps3_plp_post[PS3_PLP_FN_N];
unsigned long long g_ps3_plp_grand;
static int g_ps3_plp_atexit_reg = 0;
static int g_ps3_plp_gate = -1;

static int ps3_plp_gate_on(void) {
    if (g_ps3_plp_gate < 0) {
        extern char* getenv(const char*);
        const char* e = getenv("PS3_TRACE_PRESENTLOOP");
        /* M1 disc contract: ON only if first char is '1'. */
        g_ps3_plp_gate = (e && *e == '1') ? 1 : 0;
    }
    return g_ps3_plp_gate;
}

static void ps3_plp_dump_summary(void) {
    if (!ps3_plp_gate_on()) return;
    fprintf(stderr, "[PRESENTLOOP] SUMMARY grand=%llu rperma_full=%d\n",
            (unsigned long long)g_ps3_plp_grand, (int)g_ps3_rperma_full);
    for (int i = 0; i < PS3_PLP_FN_N; i++) {
        unsigned long long t = g_ps3_plp_tot[i];
        unsigned long long p = g_ps3_plp_post[i];
        fprintf(stderr,
                "[PRESENTLOOP] SUMMARY fn=%s tot=%llu post=%llu pre=%llu\n",
                g_ps3_plp_fn_name[i],
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
typedef void (*ps3_plp_sigh_t)(int);
static ps3_plp_sigh_t g_ps3_plp_prev_sigterm = 0;

static void ps3_plp_on_sigterm(int sig) {
    ps3_plp_dump_summary();
    if (g_ps3_plp_prev_sigterm
        && g_ps3_plp_prev_sigterm != SIG_DFL
        && g_ps3_plp_prev_sigterm != SIG_IGN) {
        g_ps3_plp_prev_sigterm(sig);
    }
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((constructor))
#endif
static void ps3_plp_ctor(void) {
    if (!ps3_plp_gate_on()) return;
    fprintf(stderr, "[PRESENTLOOP] probe armed (post=g_ps3_rperma_full)\n");
    fflush(stderr);
    if (!g_ps3_plp_atexit_reg) {
        g_ps3_plp_atexit_reg = 1;
        atexit(ps3_plp_dump_summary);
#ifndef _WIN32
        g_ps3_plp_prev_sigterm = signal(SIGTERM, ps3_plp_on_sigterm);
#endif
    }
}

void ps3_plp_on_enter(int id, ppu_context* ctx) {
    if (!ps3_plp_gate_on()) return;
    if (id < 0 || id >= PS3_PLP_FN_N || !ctx) return;
    if (!g_ps3_plp_atexit_reg) {
        g_ps3_plp_atexit_reg = 1;
        atexit(ps3_plp_dump_summary);
#ifndef _WIN32
        g_ps3_plp_prev_sigterm = signal(SIGTERM, ps3_plp_on_sigterm);
#endif
    }
    unsigned long long t = ++g_ps3_plp_tot[id];
    unsigned long long p = g_ps3_plp_post[id];
    if (g_ps3_rperma_full) p = ++g_ps3_plp_post[id];
    unsigned long long g = ++g_ps3_plp_grand;
    int cap = g_ps3_plp_log_cap[id];
    int do_log = (t <= (unsigned long long)cap) || ((g % 10000ull) == 0);
    if (do_log) {
        fprintf(stderr,
                "[PRESENTLOOP] enter fn=%s tot=%llu post=%llu r3=0x%08X r4=0x%08X\n",
                g_ps3_plp_fn_name[id],
                (unsigned long long)t,
                (unsigned long long)p,
                (unsigned)(uint32_t)ctx->gpr[3],
                (unsigned)(uint32_t)ctx->gpr[4]);
        fflush(stderr);
    }
    if ((g % 10000ull) == 0) ps3_plp_dump_summary();
}
/* PRESENTLOOP-PROBE end helpers */
'''

EXTERN_DECL = (
    "/* PRESENTLOOP-PROBE extern (defs in ppu_recomp_000.cpp) */\n"
    "void ps3_plp_on_enter(int id, ppu_context* ctx);\n"
)


def inject_call(site_id: int) -> str:
    return (
        f"        /* {MARKER} id={site_id} */\n"
        f"        {{ ps3_plp_on_enter({site_id}, ctx); }}\n"
    )


# ---- upgrade-in-place: encadear o SIGTERM num bloco JA baked (codigo antigo) --
OLD_SIGTERM_HANDLER = (
    "static void ps3_plp_on_sigterm(int sig) {\n"
    "    (void)sig;\n"
    "    ps3_plp_dump_summary();\n"
    "}"
)
NEW_SIGTERM_HANDLER = (
    "typedef void (*ps3_plp_sigh_t)(int);\n"
    "static ps3_plp_sigh_t g_ps3_plp_prev_sigterm = 0;\n"
    "\n"
    "static void ps3_plp_on_sigterm(int sig) {\n"
    "    ps3_plp_dump_summary();\n"
    "    if (g_ps3_plp_prev_sigterm\n"
    "        && g_ps3_plp_prev_sigterm != SIG_DFL\n"
    "        && g_ps3_plp_prev_sigterm != SIG_IGN) {\n"
    "        g_ps3_plp_prev_sigterm(sig);\n"
    "    }\n"
    "}"
)
OLD_SIGNAL_CALL = "        signal(SIGTERM, ps3_plp_on_sigterm);\n"
NEW_SIGNAL_CALL = "        g_ps3_plp_prev_sigterm = signal(SIGTERM, ps3_plp_on_sigterm);\n"


def upgrade_sigterm_chain(t: str, path: Path) -> str:
    if "g_ps3_plp_prev_sigterm" in t:
        print(f"  helpers: already in {path.name}")
        return t
    n1 = t.count(OLD_SIGTERM_HANDLER)
    if n1 != 1:
        raise SystemExit(
            f"{path}: OLD_SIGTERM_HANDLER aparece {n1}x (esperado 1) -- upgrade abortado"
        )
    t = t.replace(OLD_SIGTERM_HANDLER, NEW_SIGTERM_HANDLER, 1)
    n2 = t.count(OLD_SIGNAL_CALL)
    if n2 != 2:
        raise SystemExit(
            f"{path}: OLD_SIGNAL_CALL aparece {n2}x (esperado 2 -- ctor + on_enter) -- upgrade abortado"
        )
    t = t.replace(OLD_SIGNAL_CALL, NEW_SIGNAL_CALL)
    print(f"  helpers: UPGRADED in {path.name}")
    return t


def ensure_helpers(t: str, path: Path, chunk: str) -> str:
    if chunk == "000":
        if "/* PRESENTLOOP-PROBE: R8 parents" in t:
            return upgrade_sigterm_chain(t, path)
        if "/* PRESENTLOOP-PROBE: R8 parents" not in t:
            # Prefer after FLIPPATH helpers if present (R7)
            flip = "/* FLIPPATH-PROBE end helpers */\n"
            if flip in t:
                t = t.replace(flip, flip + HELPER_BLOCK, 1)
            else:
                acc = "/* 17ACC-PROBE end helpers */\n"
                if acc in t:
                    t = t.replace(acc, acc + HELPER_BLOCK, 1)
                else:
                    alt = "#include <stdlib.h>\n"
                    if alt not in t:
                        raise SystemExit(f"{path}: no place for helper block")
                    t = t.replace(alt, alt + HELPER_BLOCK, 1)
            print(f"  helpers: added to {path.name}")
        else:
            print(f"  helpers: already in {path.name}")
    else:
        if "/* PRESENTLOOP-PROBE extern" not in t:
            flip_ext = "/* FLIPPATH-PROBE extern (defs in ppu_recomp_000.cpp) */\n"
            if flip_ext in t:
                # place after FLIPPATH extern block
                needle = flip_ext + "void ps3_flipp_on_enter(int id, ppu_context* ctx);\n"
                if needle in t:
                    t = t.replace(needle, needle + EXTERN_DECL, 1)
                else:
                    t = t.replace(flip_ext, flip_ext + EXTERN_DECL, 1)
            else:
                alt = "#include <stdlib.h>\n"
                if alt in t:
                    t = t.replace(alt, alt + EXTERN_DECL, 1)
                else:
                    h = '#include "ppu_recomp.h"\n'
                    if h not in t:
                        raise SystemExit(f"{path}: no place for extern decl")
                    t = t.replace(h, h + EXTERN_DECL, 1)
            print(f"  extern: added to {path.name}")
        else:
            print(f"  extern: already in {path.name}")
    return t


def patch_file(path: Path, chunk: str) -> str:
    t = path.read_text(encoding="utf-8", errors="replace")
    orig = t
    t = ensure_helpers(t, path, chunk)

    for site_id, (short, fn, fn_chunk, _cap) in enumerate(SITES):
        if fn_chunk != chunk:
            continue
        sig = f"void {fn}(ppu_context* ctx) {{\n"
        if sig not in t:
            print(f"  {fn} ({short}): MISSING — skip")
            continue
        if t.count(sig) != 1:
            raise SystemExit(f"{path}: {fn} signature count={t.count(sig)}")
        idx = t.find(sig)
        region_start = idx + len(sig)
        ahead = t[region_start : region_start + 200]
        probe_line = f"        /* {MARKER} id={site_id} */\n"
        if probe_line in ahead or f"ps3_plp_on_enter({site_id}," in ahead:
            print(f"  {fn} ({short}): ALREADY")
            continue
        t = t[:region_start] + inject_call(site_id) + t[region_start:]
        print(f"  {fn} ({short}): APPLIED id={site_id}")

    if t == orig:
        return t
    try:
        path.write_text(t, encoding="utf-8", newline="\n")
    except TypeError:
        path.write_text(t, encoding="utf-8")
    return t


def main() -> int:
    root = Path(sys.argv[1]) if len(sys.argv) > 1 else (
        Path(__file__).resolve().parent.parent / "recomp_macos_v2"
    )
    if not root.is_dir():
        print(f"lift dir missing: {root}", file=sys.stderr)
        return 2

    files = {
        "000": root / "ppu_recomp_000.cpp",
        "001": root / "ppu_recomp_001.cpp",
        "002": root / "ppu_recomp_002.cpp",
    }
    rc = 0
    for chunk, path in files.items():
        if not path.exists():
            print(f"skip missing {path}")
            rc = 1
            continue
        print(f"== {path.name} ==")
        try:
            before = path.read_text(encoding="utf-8", errors="replace")
            after = patch_file(path, chunk)
            now = path.read_text(encoding="utf-8", errors="replace")
            if MARKER in now or "PRESENTLOOP-PROBE" in now:
                if after == before and MARKER in before:
                    print(f"ALREADY-APPLIED {path}")
                else:
                    print(f"APPLIED/OK {path}")
            else:
                print(f"FAILED {path}: marker missing after patch")
                rc = 1
        except SystemExit as e:
            print(f"FAILED {path}: {e}")
            rc = 1
    return rc


if __name__ == "__main__":
    raise SystemExit(main())
