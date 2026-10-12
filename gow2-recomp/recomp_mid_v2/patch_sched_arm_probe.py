#!/usr/bin/env python3
"""R10 — enter probes: who ARMS menu/frame present schedule (pre vs post R_Perm).

Context
-------
R9 (MENUPRESENT): menu path B61F4→BB424→BB4E0→194FFC never enters (tot=0);
2B21*/2B25AC never; thr chain 25C838→2B2E74→B71 oneshot; 2C0508 pre-only.
H1 confirmed: schedule parents never re-armed post thr.

R10 climbs one level (static RE + enter counters):

  Menu arm:
    B94D4  (dead fall → B94EC; live tail → B7888)
    B94EC  → B6150  (gate: *(TOC-0x62B0)+0x1CC==0 → B61F4 → BB424 JT)
    BB414  (dead fall → BB424; live → BB37C)
  Thr chain:
    10354 → 25C838 → 36A598 (boot only; 004 frags 10488/10498 same)
  Frame:
    2B25A4 → 2B24E0 (live) / dead fall → 2B25AC → 2B21C4
  Movie/CE03C arm:
    CDFDC / CE000 / CE02C → CE03C (idx==2 path)

Gate (default OFF) — M1 disc contract:
  PS3_TRACE_SCHEDARM=1  → ON only if first char is '1'
  unset / empty / '0' / anything else → OFF (no-op baseline)

Also accepts PS3_TRACE_MENUPRESENT=1 as alias ON (so one smoke can enable both
if only MENUPRESENT is set in the recipe — but prefer explicit SCHEDARM).

Telemetry
---------
Shared counters (defined in 000, used by 001/002/003 via extern):
  g_ps3_sa_tot[id]  — all enters
  g_ps3_sa_post[id] — enters while g_ps3_rperma_full != 0

Log:
  [SCHEDARM] enter fn=... tot=N post=M r3=0x.. r4=0x..
  SUMMARY at atexit / SIGTERM / every 10000 grand.

Idempotent: MARKER present → ALREADY. Inject right after each
`void func_... {` signature line. Does not alter guest CF when OFF.
Skip missing functions (warn, do not fail whole file).
"""
from __future__ import annotations

import sys
from pathlib import Path

MARKER = "SCHEDARM-PROBE"
LOG_TAG = "[SCHEDARM] enter"

# (short name, function symbol, cpp chunk key, log cap)
# IDs must match g_ps3_sa_fn_name[] order in HELPER_BLOCK.
SITES = [
    # targets (confirm R9 zeros)
    ("B61F4",  "func_000B61F4",  "003", 4),
    ("BB424",  "func_000BB424",  "000", 4),
    ("BB4E0",  "func_000BB4E0",  "000", 4),
    ("25C838", "func_0025C838",  "000", 4),
    ("2B25AC", "func_002B25AC",  "001", 4),
    ("CE03C",  "func_000CE03C",  "001", 4),
    # menu arm parents
    ("B6150",  "func_000B6150",  "000", 8),
    ("B94EC",  "func_000B94EC",  "000", 4),
    ("B94D4",  "func_000B94D4",  "000", 4),
    ("B7888",  "func_000B7888",  "000", 8),
    ("BB414",  "func_000BB414",  "000", 4),
    ("BB37C",  "func_000BB37C",  "000", 8),
    # thr before/after
    ("10354",  "func_00010354",  "000", 4),
    ("36A598", "func_0036A598",  "001", 4),
    # frame arm
    ("2B25A4", "func_002B25A4",  "001", 4),
    ("2B24E0", "func_002B24E0",  "002", 8),
    # CE03C arm
    ("CDFDC",  "func_000CDFDC",  "000", 4),
    ("CE000",  "func_000CE000",  "001", 4),
    ("CE02C",  "func_000CE02C",  "000", 4),
]

HELPER_BLOCK = r'''
/* SCHEDARM-PROBE: R10 who arms menu/frame present schedule pre/post R_Perm (PS3_TRACE_SCHEDARM=1) */
#include <signal.h>
extern "C" volatile int g_ps3_rperma_full;
enum { PS3_SA_FN_N = 19 };
static const char* const g_ps3_sa_fn_name[PS3_SA_FN_N] = {
    "B61F4","BB424","BB4E0","25C838","2B25AC","CE03C",
    "B6150","B94EC","B94D4","B7888","BB414","BB37C",
    "10354","36A598","2B25A4","2B24E0","CDFDC","CE000","CE02C"
};
static const int g_ps3_sa_log_cap[PS3_SA_FN_N] = {
    4,4,4,4,4,4, 8,4,4,8,4,8, 4,4,4,8,4,4,4
};
unsigned long long g_ps3_sa_tot[PS3_SA_FN_N];
unsigned long long g_ps3_sa_post[PS3_SA_FN_N];
unsigned long long g_ps3_sa_grand;
static int g_ps3_sa_atexit_reg = 0;
static int g_ps3_sa_gate = -1;

static int ps3_sa_gate_on(void) {
    if (g_ps3_sa_gate < 0) {
        extern char* getenv(const char*);
        const char* e = getenv("PS3_TRACE_SCHEDARM");
        /* M1 disc contract: ON only if first char is '1'. */
        if (e && *e == '1') {
            g_ps3_sa_gate = 1;
        } else {
            /* alias: allow MENUPRESENT=1 to also arm SCHEDARM (shared smoke) */
            const char* m = getenv("PS3_TRACE_MENUPRESENT");
            g_ps3_sa_gate = (m && *m == '1') ? 1 : 0;
        }
    }
    return g_ps3_sa_gate;
}

static void ps3_sa_dump_summary(void) {
    if (!ps3_sa_gate_on()) return;
    fprintf(stderr, "[SCHEDARM] SUMMARY grand=%llu rperma_full=%d\n",
            (unsigned long long)g_ps3_sa_grand, (int)g_ps3_rperma_full);
    for (int i = 0; i < PS3_SA_FN_N; i++) {
        unsigned long long t = g_ps3_sa_tot[i];
        unsigned long long p = g_ps3_sa_post[i];
        fprintf(stderr,
                "[SCHEDARM] SUMMARY fn=%s tot=%llu post=%llu pre=%llu\n",
                g_ps3_sa_fn_name[i],
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
typedef void (*ps3_sa_sigh_t)(int);
static ps3_sa_sigh_t g_ps3_sa_prev_sigterm = 0;

static void ps3_sa_on_sigterm(int sig) {
    ps3_sa_dump_summary();
    if (g_ps3_sa_prev_sigterm
        && g_ps3_sa_prev_sigterm != SIG_DFL
        && g_ps3_sa_prev_sigterm != SIG_IGN) {
        g_ps3_sa_prev_sigterm(sig);
    }
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((constructor))
#endif
static void ps3_sa_ctor(void) {
    if (!ps3_sa_gate_on()) return;
    fprintf(stderr, "[SCHEDARM] probe armed (post=g_ps3_rperma_full)\n");
    fflush(stderr);
    if (!g_ps3_sa_atexit_reg) {
        g_ps3_sa_atexit_reg = 1;
        atexit(ps3_sa_dump_summary);
#ifndef _WIN32
        g_ps3_sa_prev_sigterm = signal(SIGTERM, ps3_sa_on_sigterm);
#endif
    }
}

void ps3_sa_on_enter(int id, ppu_context* ctx) {
    if (!ps3_sa_gate_on()) return;
    if (id < 0 || id >= PS3_SA_FN_N || !ctx) return;
    if (!g_ps3_sa_atexit_reg) {
        g_ps3_sa_atexit_reg = 1;
        atexit(ps3_sa_dump_summary);
#ifndef _WIN32
        g_ps3_sa_prev_sigterm = signal(SIGTERM, ps3_sa_on_sigterm);
#endif
    }
    unsigned long long t = ++g_ps3_sa_tot[id];
    unsigned long long p = g_ps3_sa_post[id];
    if (g_ps3_rperma_full) p = ++g_ps3_sa_post[id];
    unsigned long long g = ++g_ps3_sa_grand;
    int cap = g_ps3_sa_log_cap[id];
    int do_log = (t <= (unsigned long long)cap) || ((g % 10000ull) == 0);
    if (do_log) {
        fprintf(stderr,
                "[SCHEDARM] enter fn=%s tot=%llu post=%llu r3=0x%08X r4=0x%08X\n",
                g_ps3_sa_fn_name[id],
                (unsigned long long)t,
                (unsigned long long)p,
                (unsigned)(uint32_t)ctx->gpr[3],
                (unsigned)(uint32_t)ctx->gpr[4]);
        fflush(stderr);
    }
    if ((g % 10000ull) == 0) ps3_sa_dump_summary();
}
/* SCHEDARM-PROBE end helpers */
'''

EXTERN_DECL = (
    "/* SCHEDARM-PROBE extern (defs in ppu_recomp_000.cpp) */\n"
    "void ps3_sa_on_enter(int id, ppu_context* ctx);\n"
)


def inject_call(site_id: int) -> str:
    return (
        f"        /* {MARKER} id={site_id} */\n"
        f"        {{ ps3_sa_on_enter({site_id}, ctx); }}\n"
    )


# ---- upgrade-in-place: encadear o SIGTERM num bloco JA baked (codigo antigo) --
OLD_SIGTERM_HANDLER = (
    "static void ps3_sa_on_sigterm(int sig) {\n"
    "    (void)sig;\n"
    "    ps3_sa_dump_summary();\n"
    "}"
)
NEW_SIGTERM_HANDLER = (
    "typedef void (*ps3_sa_sigh_t)(int);\n"
    "static ps3_sa_sigh_t g_ps3_sa_prev_sigterm = 0;\n"
    "\n"
    "static void ps3_sa_on_sigterm(int sig) {\n"
    "    ps3_sa_dump_summary();\n"
    "    if (g_ps3_sa_prev_sigterm\n"
    "        && g_ps3_sa_prev_sigterm != SIG_DFL\n"
    "        && g_ps3_sa_prev_sigterm != SIG_IGN) {\n"
    "        g_ps3_sa_prev_sigterm(sig);\n"
    "    }\n"
    "}"
)
OLD_SIGNAL_CALL = "        signal(SIGTERM, ps3_sa_on_sigterm);\n"
NEW_SIGNAL_CALL = "        g_ps3_sa_prev_sigterm = signal(SIGTERM, ps3_sa_on_sigterm);\n"


def upgrade_sigterm_chain(t: str, path: Path) -> str:
    if "g_ps3_sa_prev_sigterm" in t:
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
        if "/* SCHEDARM-PROBE: R10 who arms" in t:
            return upgrade_sigterm_chain(t, path)
        if "/* SCHEDARM-PROBE: R10 who arms" not in t:
            mp = "/* MENUPRESENT-PROBE end helpers */\n"
            if mp in t:
                t = t.replace(mp, mp + HELPER_BLOCK, 1)
            else:
                plp = "/* PRESENTLOOP-PROBE end helpers */\n"
                if plp in t:
                    t = t.replace(plp, plp + HELPER_BLOCK, 1)
                else:
                    flip = "/* FLIPPATH-PROBE end helpers */\n"
                    if flip in t:
                        t = t.replace(flip, flip + HELPER_BLOCK, 1)
                    else:
                        alt = "#include <stdlib.h>\n"
                        if alt not in t:
                            raise SystemExit(f"{path}: no place for helper block")
                        t = t.replace(alt, alt + HELPER_BLOCK, 1)
            print(f"  helpers: added to {path.name}")
        else:
            print(f"  helpers: already in {path.name}")
    else:
        if "/* SCHEDARM-PROBE extern" not in t:
            mp_ext = "/* MENUPRESENT-PROBE extern (defs in ppu_recomp_000.cpp) */\n"
            mp_needle = mp_ext + "void ps3_mp_on_enter(int id, ppu_context* ctx);\n"
            if mp_needle in t:
                t = t.replace(mp_needle, mp_needle + EXTERN_DECL, 1)
            elif mp_ext in t:
                t = t.replace(mp_ext, mp_ext + EXTERN_DECL, 1)
            else:
                plp_ext = "/* PRESENTLOOP-PROBE extern (defs in ppu_recomp_000.cpp) */\n"
                plp_needle = plp_ext + "void ps3_plp_on_enter(int id, ppu_context* ctx);\n"
                if plp_needle in t:
                    t = t.replace(plp_needle, plp_needle + EXTERN_DECL, 1)
                elif plp_ext in t:
                    t = t.replace(plp_ext, plp_ext + EXTERN_DECL, 1)
                else:
                    flip_ext = "/* FLIPPATH-PROBE extern (defs in ppu_recomp_000.cpp) */\n"
                    if flip_ext in t:
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
        ahead = t[region_start : region_start + 280]
        probe_line = f"        /* {MARKER} id={site_id} */\n"
        if probe_line in ahead or f"ps3_sa_on_enter({site_id}," in ahead:
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
        "003": root / "ppu_recomp_003.cpp",
    }
    rc = 0
    for chunk, path in files.items():
        need = any(c == chunk for _, _, c, _ in SITES) or chunk == "000"
        if not need:
            continue
        if not path.exists():
            print(f"skip missing {path}")
            rc = 1
            continue
        print(f"== {path.name} ==")
        try:
            before = path.read_text(encoding="utf-8", errors="replace")
            after = patch_file(path, chunk)
            now = path.read_text(encoding="utf-8", errors="replace")
            if MARKER in now or "SCHEDARM-PROBE" in now:
                if after == before and MARKER in before:
                    print(f"ALREADY-APPLIED {path}")
                else:
                    print(f"APPLIED/OK {path}")
            else:
                has_site = any(c == chunk for _, _, c, _ in SITES)
                if has_site:
                    print(f"FAILED {path}: marker missing after patch")
                    rc = 1
                else:
                    print(f"OK-no-sites {path}")
        except SystemExit as e:
            print(f"FAILED {path}: {e}")
            rc = 1
    return rc


if __name__ == "__main__":
    raise SystemExit(main())
