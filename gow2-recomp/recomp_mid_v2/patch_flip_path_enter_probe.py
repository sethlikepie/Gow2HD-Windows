#!/usr/bin/env python3
"""R7 — enter probes for real SetFlip path (pre vs post R_Perm).

Context
-------
R6 identified the real intro flip issuer as:
  NID 0x21397818 _cellGcmSetFlipCommand
  via func_002EFD60 / frag func_002EFDA4
  parents: func_0014FE18 ← … ← func_002B2E74 (B71 mainloop)

Zero SetFlip after R_Perm. This probe answers which of these still ENTER
after R_PermA full (g_ps3_rperma_full), vs die earlier in the chain.

Targets (recomp_macos_v2):
  000.cpp: func_0014FE18, func_00156680, func_000B71B8
  001.cpp: func_002B2E74, func_002EFD60
  002.cpp: func_002EFDA4

Gate (default OFF) — M1 disc contract:
  PS3_TRACE_FLIPPATH=1  → ON only if first char is '1'
  unset / empty / '0' / 'false' / anything else → OFF (no-op baseline)

Telemetry
---------
Shared counters (defined in 000, used by 001/002 via extern):
  g_ps3_flipp_tot[id]  — all enters
  g_ps3_flipp_post[id] — enters while g_ps3_rperma_full != 0
post_rperm uses existing movie_hle flag — no new runtime mark.

Log format (capped):
  [FLIPPATH] enter fn=2EFD60 tot=N post=M r3=0x.. r4=0x..
  SUMMARY at atexit / SIGTERM / every 10000 grand.

Idempotent: MARKER present → ALREADY. Inject right after each
`void func_... {` signature line. Does not alter guest CF when OFF.
"""
from __future__ import annotations

import sys
from pathlib import Path

MARKER = "FLIPPATH-PROBE"
LOG_TAG = "[FLIPPATH] enter"

# (short name, function symbol, cpp chunk key, log cap)
# IDs must match g_ps3_flipp_fn_name[] order in HELPER_BLOCK.
SITES = [
    ("14FE18", "func_0014FE18", "000", 8),
    ("156680", "func_00156680", "000", 4),
    ("B71B8", "func_000B71B8", "000", 4),
    ("2B2E74", "func_002B2E74", "001", 8),
    ("2EFD60", "func_002EFD60", "001", 8),
    ("2EFDA4", "func_002EFDA4", "002", 8),
]

HELPER_BLOCK = r'''
/* FLIPPATH-PROBE: R7 enter counters pre vs post R_Perm (PS3_TRACE_FLIPPATH=1) */
#include <signal.h>
extern "C" volatile int g_ps3_rperma_full;
enum { PS3_FLIPP_FN_N = 6 };
static const char* const g_ps3_flipp_fn_name[PS3_FLIPP_FN_N] = {
    "14FE18","156680","B71B8","2B2E74","2EFD60","2EFDA4"
};
static const int g_ps3_flipp_log_cap[PS3_FLIPP_FN_N] = {
    8,4,4,8,8,8
};
unsigned long long g_ps3_flipp_tot[PS3_FLIPP_FN_N];
unsigned long long g_ps3_flipp_post[PS3_FLIPP_FN_N];
unsigned long long g_ps3_flipp_grand;
static int g_ps3_flipp_atexit_reg = 0;
static int g_ps3_flipp_gate = -1;

static int ps3_flipp_gate_on(void) {
    if (g_ps3_flipp_gate < 0) {
        extern char* getenv(const char*);
        const char* e = getenv("PS3_TRACE_FLIPPATH");
        /* M1 disc contract: ON only if first char is '1'. */
        g_ps3_flipp_gate = (e && *e == '1') ? 1 : 0;
    }
    return g_ps3_flipp_gate;
}

static void ps3_flipp_dump_summary(void) {
    if (!ps3_flipp_gate_on()) return;
    fprintf(stderr, "[FLIPPATH] SUMMARY grand=%llu rperma_full=%d\n",
            (unsigned long long)g_ps3_flipp_grand, (int)g_ps3_rperma_full);
    for (int i = 0; i < PS3_FLIPP_FN_N; i++) {
        unsigned long long t = g_ps3_flipp_tot[i];
        unsigned long long p = g_ps3_flipp_post[i];
        /* Always print every site so tot=0 is visible. */
        fprintf(stderr,
                "[FLIPPATH] SUMMARY fn=%s tot=%llu post=%llu pre=%llu\n",
                g_ps3_flipp_fn_name[i],
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
typedef void (*ps3_flipp_sigh_t)(int);
static ps3_flipp_sigh_t g_ps3_flipp_prev_sigterm = 0;

static void ps3_flipp_on_sigterm(int sig) {
    ps3_flipp_dump_summary();
    if (g_ps3_flipp_prev_sigterm
        && g_ps3_flipp_prev_sigterm != SIG_DFL
        && g_ps3_flipp_prev_sigterm != SIG_IGN) {
        g_ps3_flipp_prev_sigterm(sig);
    }
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((constructor))
#endif
static void ps3_flipp_ctor(void) {
    if (!ps3_flipp_gate_on()) return;
    fprintf(stderr, "[FLIPPATH] probe armed (post=g_ps3_rperma_full)\n");
    fflush(stderr);
    if (!g_ps3_flipp_atexit_reg) {
        g_ps3_flipp_atexit_reg = 1;
        atexit(ps3_flipp_dump_summary);
#ifndef _WIN32
        g_ps3_flipp_prev_sigterm = signal(SIGTERM, ps3_flipp_on_sigterm);
#endif
    }
}

void ps3_flipp_on_enter(int id, ppu_context* ctx) {
    if (!ps3_flipp_gate_on()) return;
    if (id < 0 || id >= PS3_FLIPP_FN_N || !ctx) return;
    if (!g_ps3_flipp_atexit_reg) {
        g_ps3_flipp_atexit_reg = 1;
        atexit(ps3_flipp_dump_summary);
#ifndef _WIN32
        g_ps3_flipp_prev_sigterm = signal(SIGTERM, ps3_flipp_on_sigterm);
#endif
    }
    unsigned long long t = ++g_ps3_flipp_tot[id];
    unsigned long long p = g_ps3_flipp_post[id];
    if (g_ps3_rperma_full) p = ++g_ps3_flipp_post[id];
    unsigned long long g = ++g_ps3_flipp_grand;
    int cap = g_ps3_flipp_log_cap[id];
    int do_log = (t <= (unsigned long long)cap) || ((g % 10000ull) == 0);
    if (do_log) {
        fprintf(stderr,
                "[FLIPPATH] enter fn=%s tot=%llu post=%llu r3=0x%08X r4=0x%08X\n",
                g_ps3_flipp_fn_name[id],
                (unsigned long long)t,
                (unsigned long long)p,
                (unsigned)(uint32_t)ctx->gpr[3],
                (unsigned)(uint32_t)ctx->gpr[4]);
        fflush(stderr);
    }
    if ((g % 10000ull) == 0) ps3_flipp_dump_summary();
}
/* FLIPPATH-PROBE end helpers */
'''

EXTERN_DECL = (
    "/* FLIPPATH-PROBE extern (defs in ppu_recomp_000.cpp) */\n"
    "void ps3_flipp_on_enter(int id, ppu_context* ctx);\n"
)


def inject_call(site_id: int) -> str:
    return (
        f"        /* {MARKER} id={site_id} */\n"
        f"        {{ ps3_flipp_on_enter({site_id}, ctx); }}\n"
    )


# ---- upgrade-in-place: encadear o SIGTERM num bloco JA baked (codigo antigo) --
OLD_SIGTERM_HANDLER = (
    "static void ps3_flipp_on_sigterm(int sig) {\n"
    "    (void)sig;\n"
    "    ps3_flipp_dump_summary();\n"
    "}"
)
NEW_SIGTERM_HANDLER = (
    "typedef void (*ps3_flipp_sigh_t)(int);\n"
    "static ps3_flipp_sigh_t g_ps3_flipp_prev_sigterm = 0;\n"
    "\n"
    "static void ps3_flipp_on_sigterm(int sig) {\n"
    "    ps3_flipp_dump_summary();\n"
    "    if (g_ps3_flipp_prev_sigterm\n"
    "        && g_ps3_flipp_prev_sigterm != SIG_DFL\n"
    "        && g_ps3_flipp_prev_sigterm != SIG_IGN) {\n"
    "        g_ps3_flipp_prev_sigterm(sig);\n"
    "    }\n"
    "}"
)
OLD_SIGNAL_CALL = "        signal(SIGTERM, ps3_flipp_on_sigterm);\n"
NEW_SIGNAL_CALL = "        g_ps3_flipp_prev_sigterm = signal(SIGTERM, ps3_flipp_on_sigterm);\n"


def upgrade_sigterm_chain(t: str, path: Path) -> str:
    if "g_ps3_flipp_prev_sigterm" in t:
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
        if "/* FLIPPATH-PROBE: R7 enter counters" in t:
            return upgrade_sigterm_chain(t, path)
        if "/* FLIPPATH-PROBE: R7 enter counters" not in t:
            anchor = "static int g_ps3_type15_disc_n = 0;\n"
            if anchor in t:
                t = t.replace(anchor, anchor + HELPER_BLOCK, 1)
            else:
                # Prefer after 17ACC helpers if present
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
        if "/* FLIPPATH-PROBE extern" not in t:
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
            raise SystemExit(f"{path}: missing signature for {fn}")
        if t.count(sig) != 1:
            raise SystemExit(f"{path}: {fn} signature count={t.count(sig)}")
        idx = t.find(sig)
        region_start = idx + len(sig)
        ahead = t[region_start : region_start + 160]
        probe_line = f"        /* {MARKER} id={site_id} */\n"
        if probe_line in ahead or f"ps3_flipp_on_enter({site_id}," in ahead:
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
            if MARKER in now or "FLIPPATH-PROBE" in now:
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
