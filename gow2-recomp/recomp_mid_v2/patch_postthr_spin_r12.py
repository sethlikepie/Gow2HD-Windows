#!/usr/bin/env python3
"""R12 — post-thr spin disc for 2B2DD0 / B951C / CC9D0 (PS3_TRACE_POSTTHR=1).

Extends R11 POSTTHR enter counters with:
  - gate alias: PS3_TRACE_POSTTHR=1 OR PS3_TRACE_POSTTHR_PC=1 (first char '1')
  - [POSTTHR-DISC] samples of known TYPE15 objs f4/f54 while inside the triangle
  - [POSTTHR-ICALL] capped log of B951C vtable ctr (present/B61* check)

Does NOT alter guest CF when OFF. Idempotent.
Hard caps: ≤20 DISC lines total; ≤8 ICALL lines total.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

MARKER_DISC = "POSTTHR-R12-DISC"
MARKER_ICALL = "POSTTHR-R12-ICALL"
MARKER_GATE = "POSTTHR-R12-GATE"

# Known TYPE15 component EAs from M0–M2 / R11 spin (this of CC9D0)
TY15_A = 0x4066D798
TY15_B = 0x4066D804

GATE_OLD = r'''static int ps3_pt_gate_on(void) {
    if (g_ps3_pt_gate < 0) {
        extern char* getenv(const char*);
        const char* e = getenv("PS3_TRACE_POSTTHR_PC");
        g_ps3_pt_gate = (e && *e == '1') ? 1 : 0;
    }
    return g_ps3_pt_gate;
}'''

GATE_NEW = r'''static int ps3_pt_gate_on(void) {
    /* POSTTHR-R12-GATE: also accept PS3_TRACE_POSTTHR=1 */
    if (g_ps3_pt_gate < 0) {
        extern char* getenv(const char*);
        const char* a = getenv("PS3_TRACE_POSTTHR");
        const char* b = getenv("PS3_TRACE_POSTTHR_PC");
        g_ps3_pt_gate = ((a && *a == '1') || (b && *b == '1')) ? 1 : 0;
    }
    return g_ps3_pt_gate;
}'''

DISC_HELPERS = r'''
/* POSTTHR-R12-DISC: sample TYPE15 f4/f54 from parent tick (PS3_TRACE_POSTTHR=1) */
static int g_ps3_pt_disc_n = 0;
static int g_ps3_pt_icall_n = 0;
enum { PS3_PT_DISC_CAP = 20, PS3_PT_ICALL_CAP = 8 };
static const uint32_t g_ps3_pt_ty15[2] = { 0x4066D798u, 0x4066D804u };

void ps3_pt_disc_sample(const char* where, ppu_context* ctx) {
    if (!ps3_pt_gate_on() || !ctx) return;
    /* Prefer post-thr; also sample after R_Perm full so we still get lines
     * if thr_end races or the window is short. Cap hard at 20. */
    if (!g_ps3_postthr && !g_ps3_rperma_full) return;
    if (g_ps3_pt_disc_n >= PS3_PT_DISC_CAP) return;
    /* Throttle: at most one pair every 256 enters after the first 4 pairs. */
    static unsigned long long s_calls = 0;
    s_calls++;
    if (g_ps3_pt_disc_n >= 8 && (s_calls & 0xFFull) != 0) return;
    /* Sample both known TYPE15 components (shared budget ≤20). */
    for (int i = 0; i < 2 && g_ps3_pt_disc_n < PS3_PT_DISC_CAP; i++) {
        uint32_t th = g_ps3_pt_ty15[i];
        uint32_t f4 = 0, prod = 0, child_head = 0;
        uint8_t f54 = 0;
        if (th >= 0x10000u && th < 0x10000000u) {
            f4 = vm_read32((uint64_t)th + 0x4u);
            f54 = (uint8_t)vm_read8((uint64_t)th + 0x54u);
            prod = vm_read32((uint64_t)th + 0x8u);
            if (prod >= 0x10000u && prod < 0x10000000u)
                child_head = vm_read32((uint64_t)prod + 0x70u);
        }
        fprintf(stderr,
                "[POSTTHR-DISC] where=%s r3=0x%08X this=0x%08X f4=0x%08X f54=0x%02X prod=0x%08X child_head=0x%08X post=%d rperm=%d n=%d\n",
                where ? where : "?",
                (unsigned)(uint32_t)ctx->gpr[3],
                (unsigned)th,
                (unsigned)f4,
                (unsigned)f54,
                (unsigned)prod,
                (unsigned)child_head,
                (int)g_ps3_postthr,
                (int)g_ps3_rperma_full,
                g_ps3_pt_disc_n + 1);
        fflush(stderr);
        g_ps3_pt_disc_n++;
    }
}

void ps3_pt_icall_log(const char* slot, ppu_context* ctx) {
    if (!ps3_pt_gate_on() || !ctx) return;
    if (!g_ps3_postthr) return;
    if (g_ps3_pt_icall_n >= PS3_PT_ICALL_CAP) return;
    uint32_t ctr = (uint32_t)ctx->ctr;
    fprintf(stderr,
            "[POSTTHR-ICALL] slot=%s ctr=0x%08X r3=0x%08X lr=0x%08X n=%d\n",
            slot ? slot : "?",
            (unsigned)ctr,
            (unsigned)(uint32_t)ctx->gpr[3],
            (unsigned)(uint32_t)ctx->lr,
            g_ps3_pt_icall_n + 1);
    fflush(stderr);
    g_ps3_pt_icall_n++;
}
/* POSTTHR-R12-DISC end */
'''

EXTERN_R12 = (
    "/* POSTTHR-R12-DISC extern */\n"
    "void ps3_pt_disc_sample(const char* where, ppu_context* ctx);\n"
    "void ps3_pt_icall_log(const char* slot, ppu_context* ctx);\n"
)

DISC_SITES = [
    # (fn, where_tag, chunk)
    ("func_002B2DD0", "2B2DD0", "001"),
    ("func_000B951C", "B951C", "000"),
    ("func_000CC9D0", "CC9D0", "000"),
]


def ensure_gate_alias(t: str) -> str:
    if MARKER_GATE in t:
        print("  gate alias: ALREADY")
        return t
    if GATE_OLD not in t:
        # already new form?
        if "PS3_TRACE_POSTTHR" in t and "ps3_pt_gate_on" in t:
            print("  gate alias: present (other form)")
            return t
        print("  gate alias: WARN old block not found")
        return t
    t = t.replace(GATE_OLD, GATE_NEW, 1)
    print("  gate alias: APPLIED (POSTTHR | POSTTHR_PC)")
    return t


def ensure_disc_helpers(t: str, chunk: str) -> str:
    if chunk == "000":
        if MARKER_DISC in t and "ps3_pt_disc_sample" in t:
            print("  disc helpers: ALREADY")
            return t
        end = "/* POSTTHR-PROBE end helpers */\n"
        if end not in t:
            raise SystemExit("000: POSTTHR end helpers marker missing — run patch_postthr_pc_probe.py first")
        t = t.replace(end, end + DISC_HELPERS, 1)
        print("  disc helpers: APPLIED")
    else:
        if "/* POSTTHR-R12-DISC extern */" in t:
            print("  disc extern: ALREADY")
            return t
        # after POSTTHR extern
        ext = (
            "/* POSTTHR-PROBE extern (defs in ppu_recomp_000.cpp) */\n"
            "void ps3_pt_on_enter(int id, ppu_context* ctx);\n"
            "void ps3_pt_mark_thr_end(void);\n"
            "extern volatile int g_ps3_postthr;\n"
        )
        if ext in t:
            t = t.replace(ext, ext + EXTERN_R12, 1)
        else:
            alt = '#include "ppu_recomp.h"\n'
            if alt not in t:
                raise SystemExit(f"{chunk}: no place for R12 extern")
            t = t.replace(alt, alt + EXTERN_R12, 1)
        print("  disc extern: APPLIED")
    return t


def inject_disc_at_fn(t: str, fn: str, where: str) -> str:
    sig = f"void {fn}(ppu_context* ctx) {{\n"
    if sig not in t:
        print(f"  {fn} disc: MISSING")
        return t
    if t.count(sig) != 1:
        raise SystemExit(f"{fn}: signature count={t.count(sig)}")
    idx = t.find(sig) + len(sig)
    ahead = t[idx : idx + 500]
    tag = f"/* {MARKER_DISC} {where} */"
    if tag in ahead or f'ps3_pt_disc_sample("{where}"' in ahead:
        print(f"  {fn} disc: ALREADY")
        return t
    inject = (
        f"        {tag}\n"
        f'        {{ ps3_pt_disc_sample("{where}", ctx); }}\n'
    )
    # Prefer after existing POSTTHR-PROBE enter if present
    m = re.search(r"/\* POSTTHR-PROBE id=\d+ \*/\n\s*\{\s*ps3_pt_on_enter\(\d+, ctx\);\s*\}\n", ahead)
    if m:
        at = idx + m.end()
        t = t[:at] + inject + t[at:]
    else:
        t = t[:idx] + inject + t[idx:]
    print(f"  {fn} disc: APPLIED")
    return t


def inject_b951c_icall_logs(t: str) -> str:
    """Log ctr immediately before each ps3_indirect_call inside B951C (cap in helper)."""
    sig = "void func_000B951C(ppu_context* ctx) {\n"
    if sig not in t:
        print("  B951C icall: MISSING body")
        return t
    idx = t.find(sig)
    rest = t[idx + len(sig) :]
    end_rel = rest.find("\nvoid func_")
    if end_rel < 0:
        raise SystemExit("B951C: no end")
    body = rest[:end_rel]
    if MARKER_ICALL in body:
        print("  B951C icall: ALREADY")
        return t
    # Replace each `ps3_indirect_call(ctx);` in body with log+call
    slots = ["vt+0x40", "vt+0x44"]
    n = 0

    def repl(m: re.Match) -> str:
        nonlocal n
        slot = slots[n] if n < len(slots) else f"icall{n}"
        n += 1
        return (
            f"\n        /* {MARKER_ICALL} {slot} */\n"
            f'        {{ ps3_pt_icall_log("{slot}", ctx); }}\n'
            f"        ps3_indirect_call(ctx); DRAIN_TRAMPOLINE(ctx);"
        )

    new_body, count = re.subn(
        r"\n\s*ps3_indirect_call\(ctx\);\s*DRAIN_TRAMPOLINE\(ctx\);",
        repl,
        body,
        count=2,
    )
    if count != 2:
        print(f"  B951C icall: WARN expected 2 indirect calls, got {count}")
    t = t[: idx + len(sig)] + new_body + rest[end_rel:]
    print(f"  B951C icall: APPLIED x{count}")
    return t


def patch_file(path: Path, chunk: str) -> None:
    t = path.read_text(encoding="utf-8", errors="replace")
    orig = t
    if chunk == "000":
        t = ensure_gate_alias(t)
    t = ensure_disc_helpers(t, chunk)
    for fn, where, ch in DISC_SITES:
        if ch != chunk:
            continue
        t = inject_disc_at_fn(t, fn, where)
    if chunk == "000":
        t = inject_b951c_icall_logs(t)
    if t == orig:
        print(f"  no text change {path.name}")
        return
    try:
        path.write_text(t, encoding="utf-8", newline="\n")
    except TypeError:
        path.write_text(t, encoding="utf-8")
    print(f"  wrote {path.name}")


def main() -> int:
    root = Path(sys.argv[1]) if len(sys.argv) > 1 else (
        Path(__file__).resolve().parent.parent / "recomp_macos_v2"
    )
    if not root.is_dir():
        print(f"lift dir missing: {root}", file=sys.stderr)
        return 2
    for chunk, name in (("000", "ppu_recomp_000.cpp"), ("001", "ppu_recomp_001.cpp")):
        path = root / name
        if not path.exists():
            print(f"skip missing {path}")
            continue
        print(f"== {path.name} ==")
        try:
            patch_file(path, chunk)
        except SystemExit as e:
            print(f"FAILED: {e}")
            return 1
    # Also keep POSTTHR_PC script gate in sync for re-lift
    pc = Path(__file__).resolve().parent / "patch_postthr_pc_probe.py"
    if pc.exists():
        src = pc.read_text(encoding="utf-8")
        if MARKER_GATE not in src and GATE_OLD in src:
            src = src.replace(GATE_OLD, GATE_NEW, 1)
            # also document alias in header
            if "PS3_TRACE_POSTTHR=1" not in src:
                src = src.replace(
                    "  PS3_TRACE_POSTTHR_PC=1  → ON only if first char is '1'",
                    "  PS3_TRACE_POSTTHR=1 or PS3_TRACE_POSTTHR_PC=1  → ON if first char is '1'",
                    1,
                )
            pc.write_text(src, encoding="utf-8")
            print(f"  synced gate alias into {pc.name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
