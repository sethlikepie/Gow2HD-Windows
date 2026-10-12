#!/usr/bin/env python3
"""R11 — post-thr guest PC / enter sample (PS3_TRACE_POSTTHR_PC=1).

Context
-------
R10: menu arm B6150/B94EC tot=0; thr 25C838 oneshot; 36A598 never.
R11 offline OPD: B94EC has NO OPD (epilogue fragment only); B6150 OPD
0x51D7A0 exists but zero static refs; B61F4 no OPD.

This probe samples which residual functions still run AFTER thr_auto_load
body returns (func_00147038 epilogue sets g_ps3_postthr=1).

Gate (default OFF) — M1 disc contract:
  PS3_TRACE_POSTTHR_PC=1  → ON only if first char is '1'
  unset / empty / '0' / other → OFF

Telemetry
---------
  [POSTTHR] thr_end flag set (once)
  [POSTTHR] enter fn=... tot=N post=M r3=...
  [POSTTHR] SUMMARY at atexit / SIGTERM

Sites: thr residual + TYPE15 spin + menu arm zeros + intro hot (for contrast).

Idempotent. Does not alter guest CF when OFF.
"""
from __future__ import annotations

import sys
from pathlib import Path

MARKER = "POSTTHR-PROBE"
LOG_TAG = "[POSTTHR] enter"

# (short, symbol, chunk, cap)
SITES = [
    # thr window
    ("147038", "func_00147038", "000", 4),   # thr_auto_load body; sets flag on exit
    ("25C838", "func_0025C838", "000", 4),
    ("2B2E74", "func_002B2E74", "001", 4),
    ("B71B8",  "func_000B71B8",  "000", 4),
    ("36A598", "func_0036A598",  "001", 4),
    ("2B2DD0", "func_002B2DD0",  "001", 4),
    ("B951C",  "func_000B951C",  "000", 4),
    # TYPE15 / residual spin
    ("CC9D0",  "func_000CC9D0",  "000", 8),
    ("CCBF0",  "func_000CCBF0",  "003", 4),
    ("CBB2C",  "func_000CBB2C",  "000", 4),
    ("CB56C",  "func_000CB56C",  "000", 4),
    # menu arm (expect 0; correlate with SCHEDARM)
    ("B6150",  "func_000B6150",  "000", 4),
    ("B94EC",  "func_000B94EC",  "000", 4),
    ("B61F4",  "func_000B61F4",  "003", 4),
    ("BB424",  "func_000BB424",  "000", 4),
    # intro hot (should be pre-only)
    ("CDBA4",  "func_000CDBA4",  "000", 4),
    ("2C0508", "func_002C0508",  "001", 4),
]

HELPER_BLOCK = r'''
/* POSTTHR-PROBE: R11 sample guest enters after thr_auto_load end (PS3_TRACE_POSTTHR_PC=1) */
#include <signal.h>
#include <time.h>
extern "C" volatile int g_ps3_rperma_full;
/* Set once when thr body (147038) returns — post-thr window. */
extern "C" volatile int g_ps3_postthr = 0;
enum { PS3_PT_FN_N = 17 };
static const char* const g_ps3_pt_fn_name[PS3_PT_FN_N] = {
    "147038","25C838","2B2E74","B71B8","36A598","2B2DD0","B951C",
    "CC9D0","CCBF0","CBB2C","CB56C",
    "B6150","B94EC","B61F4","BB424",
    "CDBA4","2C0508"
};
static const int g_ps3_pt_log_cap[PS3_PT_FN_N] = {
    4,4,4,4,4,4,4, 8,4,4,4, 4,4,4,4, 4,4
};
unsigned long long g_ps3_pt_tot[PS3_PT_FN_N];
unsigned long long g_ps3_pt_post[PS3_PT_FN_N];
unsigned long long g_ps3_pt_grand;
static int g_ps3_pt_atexit_reg = 0;
static int g_ps3_pt_gate = -1;
static unsigned long long g_ps3_pt_thr_ns = 0;
static unsigned long long g_ps3_pt_sample_until_ns = 0;

static unsigned long long ps3_pt_now_ns(void) {
#if defined(CLOCK_MONOTONIC)
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0)
        return (unsigned long long)ts.tv_sec * 1000000000ull
             + (unsigned long long)ts.tv_nsec;
#endif
    return 0;
}

static int ps3_pt_gate_on(void) {
    /* POSTTHR-R12-GATE: also accept PS3_TRACE_POSTTHR=1 */
    if (g_ps3_pt_gate < 0) {
        extern char* getenv(const char*);
        const char* a = getenv("PS3_TRACE_POSTTHR");
        const char* b = getenv("PS3_TRACE_POSTTHR_PC");
        g_ps3_pt_gate = ((a && *a == '1') || (b && *b == '1')) ? 1 : 0;
    }
    return g_ps3_pt_gate;
}

static void ps3_pt_dump_summary(void) {
    if (!ps3_pt_gate_on()) return;
    fprintf(stderr,
            "[POSTTHR] SUMMARY grand=%llu postthr=%d rperma_full=%d thr_ns=%llu\n",
            (unsigned long long)g_ps3_pt_grand,
            (int)g_ps3_postthr,
            (int)g_ps3_rperma_full,
            (unsigned long long)g_ps3_pt_thr_ns);
    for (int i = 0; i < PS3_PT_FN_N; i++) {
        unsigned long long t = g_ps3_pt_tot[i];
        unsigned long long p = g_ps3_pt_post[i];
        fprintf(stderr,
                "[POSTTHR] SUMMARY fn=%s tot=%llu post=%llu pre=%llu\n",
                g_ps3_pt_fn_name[i],
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
typedef void (*ps3_pt_sigh_t)(int);
static ps3_pt_sigh_t g_ps3_pt_prev_sigterm = 0;

static void ps3_pt_on_sigterm(int sig) {
    ps3_pt_dump_summary();
    if (g_ps3_pt_prev_sigterm
        && g_ps3_pt_prev_sigterm != SIG_DFL
        && g_ps3_pt_prev_sigterm != SIG_IGN) {
        g_ps3_pt_prev_sigterm(sig);
    }
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((constructor))
#endif
static void ps3_pt_ctor(void) {
    if (!ps3_pt_gate_on()) return;
    fprintf(stderr, "[POSTTHR] probe armed (post=g_ps3_postthr after 147038 return)\n");
    fflush(stderr);
    if (!g_ps3_pt_atexit_reg) {
        g_ps3_pt_atexit_reg = 1;
        atexit(ps3_pt_dump_summary);
#ifndef _WIN32
        g_ps3_pt_prev_sigterm = signal(SIGTERM, ps3_pt_on_sigterm);
#endif
    }
}

void ps3_pt_mark_thr_end(void) {
    if (!ps3_pt_gate_on()) return;
    if (g_ps3_postthr) return;
    g_ps3_postthr = 1;
    g_ps3_pt_thr_ns = ps3_pt_now_ns();
    /* Sample ~2s wall after thr for dense enter logs; counters keep forever. */
    g_ps3_pt_sample_until_ns = g_ps3_pt_thr_ns
        ? (g_ps3_pt_thr_ns + 2000000000ull) : 0;
    fprintf(stderr, "[POSTTHR] thr_end flag set (sample_window=2s)\n");
    fflush(stderr);
}

void ps3_pt_on_enter(int id, ppu_context* ctx) {
    if (!ps3_pt_gate_on()) return;
    if (id < 0 || id >= PS3_PT_FN_N || !ctx) return;
    if (!g_ps3_pt_atexit_reg) {
        g_ps3_pt_atexit_reg = 1;
        atexit(ps3_pt_dump_summary);
#ifndef _WIN32
        g_ps3_pt_prev_sigterm = signal(SIGTERM, ps3_pt_on_sigterm);
#endif
    }
    unsigned long long t = ++g_ps3_pt_tot[id];
    unsigned long long p = g_ps3_pt_post[id];
    if (g_ps3_postthr) p = ++g_ps3_pt_post[id];
    unsigned long long g = ++g_ps3_pt_grand;
    int cap = g_ps3_pt_log_cap[id];
    int in_window = 0;
    if (g_ps3_postthr && g_ps3_pt_sample_until_ns) {
        unsigned long long now = ps3_pt_now_ns();
        if (now && now <= g_ps3_pt_sample_until_ns) in_window = 1;
    }
    int do_log = (t <= (unsigned long long)cap)
              || (in_window && (t % 500ull) == 0)
              || ((g % 20000ull) == 0);
    if (do_log) {
        fprintf(stderr,
                "[POSTTHR] enter fn=%s tot=%llu post=%llu r3=0x%08X lr=0x%08X\n",
                g_ps3_pt_fn_name[id],
                (unsigned long long)t,
                (unsigned long long)p,
                (unsigned)(uint32_t)ctx->gpr[3],
                (unsigned)(uint32_t)ctx->lr);
        fflush(stderr);
    }
    if ((g % 20000ull) == 0) ps3_pt_dump_summary();
}
/* POSTTHR-PROBE end helpers */
'''

EXTERN_DECL = (
    "/* POSTTHR-PROBE extern (defs in ppu_recomp_000.cpp) */\n"
    "void ps3_pt_on_enter(int id, ppu_context* ctx);\n"
    "void ps3_pt_mark_thr_end(void);\n"
    "extern volatile int g_ps3_postthr;\n"
)

THR_END_MARK = "/* POSTTHR-PROBE thr_end */"


def inject_call(site_id: int) -> str:
    return (
        f"        /* {MARKER} id={site_id} */\n"
        f"        {{ ps3_pt_on_enter({site_id}, ctx); }}\n"
    )


# ---- upgrade-in-place: encadear o SIGTERM num bloco JA baked (codigo antigo) --
OLD_SIGTERM_HANDLER = (
    "static void ps3_pt_on_sigterm(int sig) {\n"
    "    (void)sig;\n"
    "    ps3_pt_dump_summary();\n"
    "}"
)
NEW_SIGTERM_HANDLER = (
    "typedef void (*ps3_pt_sigh_t)(int);\n"
    "static ps3_pt_sigh_t g_ps3_pt_prev_sigterm = 0;\n"
    "\n"
    "static void ps3_pt_on_sigterm(int sig) {\n"
    "    ps3_pt_dump_summary();\n"
    "    if (g_ps3_pt_prev_sigterm\n"
    "        && g_ps3_pt_prev_sigterm != SIG_DFL\n"
    "        && g_ps3_pt_prev_sigterm != SIG_IGN) {\n"
    "        g_ps3_pt_prev_sigterm(sig);\n"
    "    }\n"
    "}"
)
OLD_SIGNAL_CALL = "        signal(SIGTERM, ps3_pt_on_sigterm);\n"
NEW_SIGNAL_CALL = "        g_ps3_pt_prev_sigterm = signal(SIGTERM, ps3_pt_on_sigterm);\n"


def upgrade_sigterm_chain(t: str, path: Path) -> str:
    if "g_ps3_pt_prev_sigterm" in t:
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
        if "/* POSTTHR-PROBE: R11 sample" in t:
            return upgrade_sigterm_chain(t, path)
        if "/* POSTTHR-PROBE: R11 sample" not in t:
            # Prefer after SCHEDARM helpers
            sa = "/* SCHEDARM-PROBE end helpers */\n"
            mp = "/* MENUPRESENT-PROBE end helpers */\n"
            plp = "/* PRESENTLOOP-PROBE end helpers */\n"
            if sa in t:
                t = t.replace(sa, sa + HELPER_BLOCK, 1)
            elif mp in t:
                t = t.replace(mp, mp + HELPER_BLOCK, 1)
            elif plp in t:
                t = t.replace(plp, plp + HELPER_BLOCK, 1)
            else:
                alt = "#include <stdlib.h>\n"
                if alt not in t:
                    raise SystemExit(f"{path}: no place for helper block")
                t = t.replace(alt, alt + HELPER_BLOCK, 1)
            print(f"  helpers: added to {path.name}")
        else:
            print(f"  helpers: already in {path.name}")
    else:
        if "/* POSTTHR-PROBE extern" not in t:
            sa_ext = (
                "/* SCHEDARM-PROBE extern (defs in ppu_recomp_000.cpp) */\n"
                "void ps3_sa_on_enter(int id, ppu_context* ctx);\n"
            )
            if sa_ext in t:
                t = t.replace(sa_ext, sa_ext + EXTERN_DECL, 1)
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


def inject_thr_end_on_147038(t: str) -> str:
    """Before each return in thr body, mark post-thr (once-safe in helper)."""
    sig = "void func_00147038(ppu_context* ctx) {\n"
    if sig not in t:
        print("  147038 thr_end: MISSING body")
        return t
    if THR_END_MARK in t:
        print("  147038 thr_end: ALREADY")
        return t
    idx = t.find(sig)
    # Find function end: next "\nvoid func_" after body start
    rest = t[idx + len(sig) :]
    end_rel = rest.find("\nvoid func_")
    if end_rel < 0:
        raise SystemExit("147038: cannot find function end")
    body = rest[:end_rel]
    # Inject before final `return;` of the function (last bare return)
    # Prefer the epilogue return near end of body.
    last_ret = body.rfind("\n        return;\n")
    if last_ret < 0:
        raise SystemExit("147038: no return found")
    inject = (
        f"\n        {THR_END_MARK}\n"
        f"        {{ ps3_pt_mark_thr_end(); }}\n"
        f"        return;\n"
    )
    new_body = body[:last_ret] + inject + body[last_ret + len("\n        return;\n") :]
    t = t[: idx + len(sig)] + new_body + rest[end_rel:]
    print("  147038 thr_end: APPLIED mark before return")
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
        ahead = t[region_start : region_start + 320]
        probe_line = f"        /* {MARKER} id={site_id} */\n"
        if probe_line in ahead or f"ps3_pt_on_enter({site_id}," in ahead:
            print(f"  {fn} ({short}): ALREADY")
            continue
        t = t[:region_start] + inject_call(site_id) + t[region_start:]
        print(f"  {fn} ({short}): APPLIED id={site_id}")

    if chunk == "000":
        t = inject_thr_end_on_147038(t)

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
            patch_file(path, chunk)
            now = path.read_text(encoding="utf-8", errors="replace")
            if MARKER in now or "POSTTHR-PROBE" in now:
                if MARKER in before and before == now:
                    print(f"ALREADY-APPLIED {path}")
                else:
                    print(f"APPLIED/OK {path}")
            else:
                print(f"FAILED {path}: marker missing")
                rc = 1
        except SystemExit as e:
            print(f"FAILED {path}: {e}")
            rc = 1
    return rc


if __name__ == "__main__":
    raise SystemExit(main())
