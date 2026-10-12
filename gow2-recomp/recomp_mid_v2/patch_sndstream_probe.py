#!/usr/bin/env python3
"""Probe: which named sounds / disc streams does GoW2 ask for? (PS3_TRACE_SNDSTREAM=1)

Question (2026-09-23): the menu and pause ambient music never plays. The menu
WAD (r_shella) names SND_M_HD1001Q / SND_M_HD1002Q, i.e. the disc streams
/wad/_Sound/dvd_streams/hd1001q.vpk and hd1002q.vpk, and a psarc read trace
shows those members are never read, while gameplay streams (a_rhdin3.vpk,
kr15.vag) are. Is the music never requested, or requested and the open fails?

Sites (entry, before the prologue touches anything):
  func_000CD7B4  registers a named sound; r4 = name (copied 0x18 bytes to the
                 slot). The menu code at 0x32670 calls it with SND_M_HD1001Q.
  func_002D3DBC  opens a disc stream; r3 = flags, r4 = stream name, it
                 sprintf's "/wad/_Sound/dvd_streams/%s.%s" (TOC -0xD68).

  func_000CD498 / func_002A459C / func_002A466C: install state 9 / the 6->1
                 and 8->0 transitions of the TYPE15 state machine (f4 at +0x4).
  func_000CC9D0  that machine's tick: logs only when an object's (+0x4, +0x54)
                 changes, so the state path of the music object shows up.

Line: [SNDSTREAM] <site> name='<r4 string>' r3=0x.. lr=0x..  (cap 400 lines)
Gate: PS3_TRACE_SNDSTREAM set and not "0"; absent -> no-op.
Idempotent (MARKER). Does not change guest control flow.

Usage: patch_sndstream_probe.py <lift_dir>
"""
from __future__ import annotations

import sys
from pathlib import Path

MARKER = "SNDSTREAM-PROBE"
SITES = [("CD7B4", "func_000CD7B4"), ("2D3DBC", "func_002D3DBC"),
         ("CD498", "func_000CD498"), ("2A459C", "func_002A459C"),
         ("2A466C", "func_002A466C"), ("CC9D0", "func_000CC9D0"),
         # the music channel's +0xCE (signed byte, items linked at +0x70):
         # CC9D0 aborts a state-5 request when it is <= 0. Who fills it?
         ("2A43DC", "func_002A43DC"), ("2A5930", "func_002A5930"),
         ("2A55DC", "func_002A55DC"), ("2A68EC", "func_002A68EC"),
         # 2A4354 allocates the link node from the channel arena (*(ch+0xD4),
         # func_00263554) and only then bumps +0xCE. 2A381C runs right after
         # the allocation with r3 = the node (0 = allocation failed); 2A42B0
         # only when it succeeded.
         ("2A381C", "func_002A381C"), ("2A42B0", "func_002A42B0"),
         # channel init: 2A7454(mgr, desc) allocates the channel from
         # *(mgr+0x48) and calls 2A5024(ch, desc, pool=*(mgr+0x4C)), which
         # stores the pool at ch+0xD4 -- found 0 on the music channel.
         ("2A7454", "func_002A7454"), ("2A5024", "func_002A5024"),
         # CB56C builds a desc (2A49BC) and asks TABLE[desc.u16@+2]->vslot(0x14)
         # for the handle it stores at obj+8; TABLE = *(TOC-0x5EA0).
         ("2A8E64", "func_002A8E64"), ("2A87D0", "func_002A87D0"),
         ("2A88BC", "func_002A88BC"), ("CB56C", "func_000CB56C"),
         # the generic create picks registry[*(h+0x24) + *(h+0x44)*12][idx],
         # idx = *(scope[*(s8)(h+0xC8)] + 0x20) (func_0039E090); log it for type 21.
         ("39E794", "func_0039E794")]

HELPER = r'''
/* SNDSTREAM-PROBE helper (PS3_TRACE_SNDSTREAM=1) */
static void ps3_sndstream_probe(const char* site, ppu_context* ctx) {
    static int gate = -1;
    static int lines = 0;
    if (gate < 0) {
        const char* e = getenv("PS3_TRACE_SNDSTREAM");
        gate = (e && *e && *e != '0') ? 1 : 0;
    }
    if (!gate || lines >= 400) return;
    if (site[0] == 'C' && site[1] == 'C') {       /* CC9D0 tick: only on change */
        static uint32_t objs[32], st[32];
        static int n = 0;
        const uint32_t o = (uint32_t)ctx->gpr[3];
        const uint32_t v = vm_read32(o + 0x4) << 8 | ((uint32_t)vm_read8((uint64_t)(o + 0x54)) & 0xFFu);
        int k = 0;
        for (; k < n && objs[k] != o; k++) {}
        if (k == n) { if (n == 32) return; objs[n] = o; st[n] = ~v; n++; }
        static unsigned ticks[32];
        const int changed = (st[k] != v);
        /* while a request waits in state 5, sample the cross-fade inputs */
        if (!changed && !((v >> 8) == 5 && (++ticks[k] % 3000u) == 1u)) return;
        st[k] = v;
        lines++;
        uint32_t fb[5]; float f[5];
        const uint32_t offs[5] = { 0x14, 0x20, 0x2C, 0x38, 0x0 };
        for (int q = 0; q < 4; q++) { fb[q] = vm_read32(o + offs[q]); memcpy(&f[q], &fb[q], 4); }
        const uint32_t ch = vm_read32(o + 0x8);
        const int ce = ch ? (int)(signed char)vm_read8((uint64_t)(ch + 0xCE)) : -999;
        const uint32_t dtp = vm_read32((uint32_t)ctx->gpr[2] - 0x5E84);
        fb[4] = dtp ? vm_read32(dtp) : 0; memcpy(&f[4], &fb[4], 4);
        fprintf(stderr, "[SNDSTREAM] CC9D0 obj=0x%08X f4=%u f54=%u w0=%u v14=%g v20=%g v2c=%g v38=%g "
                "ch=0x%08X ch.ce=%d dt=%g\n", o, v >> 8, v & 0xFFu, vm_read32(o),
                f[0], f[1], f[2], f[3], ch, ce, f[4]);
        fflush(stderr);
        return;
    }
    if (site[0] == '2' && site[1] == 'A' && site[2] == '4' && site[3] == '3') {   /* 2A43DC */
        const uint32_t ch = (uint32_t)ctx->gpr[30];
        const uint32_t pool = vm_read32(ch + 0xD4);
        lines++;
        fprintf(stderr, "[SNDSTREAM] 2A43DC ch=0x%08X ce=%d pool=0x%08X free=0x%08X chunks=0x%08X "
                "parent=0x%08X elem=%u align=%u grow=%u lr=0x%llX\n", ch,
                (int)(signed char)vm_read8((uint64_t)(ch + 0xCE)), pool,
                pool ? vm_read32(pool + 0x4) : 0, pool ? vm_read32(pool + 0x8) : 0,
                pool ? vm_read32(pool + 0xC) : 0, pool ? vm_read32(pool + 0x10) : 0,
                pool ? (unsigned)vm_read16(pool + 0x14) : 0, pool ? (unsigned)vm_read16(pool + 0x16) : 0,
                (unsigned long long)ctx->lr);
        fflush(stderr);
        return;
    }
    if (site[0] == '2' && site[1] == 'A' && site[2] == '7') {   /* 2A7454 */
        const uint32_t m = (uint32_t)ctx->gpr[3];
        lines++;
        fprintf(stderr, "[SNDSTREAM] 2A7454 mgr=0x%08X chpool=0x%08X nodepool=0x%08X desc=0x%08X lr=0x%llX\n",
                m, vm_read32(m + 0x48), vm_read32(m + 0x4C), (uint32_t)ctx->gpr[4],
                (unsigned long long)ctx->lr);
        fflush(stderr);
        return;
    }
    if (site[0] == '2' && site[1] == 'A' && site[2] == '5' && site[3] == '0') {   /* 2A5024 */
        lines++;
        fprintf(stderr, "[SNDSTREAM] 2A5024 ch=0x%08X desc=0x%08X pool=0x%08X lr=0x%llX\n",
                (uint32_t)ctx->gpr[3], (uint32_t)ctx->gpr[4], (uint32_t)ctx->gpr[5],
                (unsigned long long)ctx->lr);
        fflush(stderr);
        return;
    }
    if (site[0] == '3' && site[1] == '9') {   /* 39E794, type 21 only */
        const uint32_t tbl = vm_read32((uint32_t)ctx->gpr[2] - 0x5EA0);
        const uint32_t h = (uint32_t)ctx->gpr[3];
        if (!tbl || h != vm_read32(tbl + 21u * 4u)) return;
        const uint32_t dsc = (uint32_t)ctx->gpr[4];
        const int depth = (int)(signed char)vm_read8((uint64_t)(h + 0xC8));
        const uint32_t row = vm_read32(h + 0x44);
        const uint32_t rows = vm_read32(h + 0x24);
        const uint32_t rowbase = rows ? vm_read32(rows + row * 12u) : 0;
        lines++;
        fprintf(stderr, "[SNDSTREAM] 39E794 h21=0x%08X desc=0x%08X w0=0x%08X depth=%d row=%u rows=0x%08X lr=0x%llX\n",
                h, dsc, dsc ? vm_read32(dsc) : 0, depth, row, rows, (unsigned long long)ctx->lr);
        for (int k = 0; k <= depth + 1 && k < 8; k++) {
            const uint32_t sc = vm_read32(h + 0x48 + 4u * (uint32_t)k);
            const uint32_t idx = sc ? vm_read32(sc + 0x20) : 0xFFFFFFFFu;
            const uint32_t o = (rowbase && idx < 4096u) ? vm_read32(rowbase + idx * 4u) : 0;
            fprintf(stderr, "[SNDSTREAM]    scope[%d]=0x%08X idx=%u reg[row][idx]=0x%08X vt=0x%08X\n",
                    k, sc, idx, o, o ? vm_read32(o - 4) : 0);
        }
        fflush(stderr);
        return;
    }
    if (site[0] == 'C' && site[1] == 'B') {   /* CB56C: dump the type table once */
        static int dumped = 0;
        if (dumped++) return;
        lines++;
        const uint32_t tbl = vm_read32((uint32_t)ctx->gpr[2] - 0x5EA0);
        fprintf(stderr, "[SNDSTREAM] typetable=0x%08X\n", tbl);
        for (int t = 0; t < 40 && tbl; t++) {
            const uint32_t h = vm_read32(tbl + 4u * (uint32_t)t);
            if (!h) continue;
            const uint32_t vt = vm_read32(h);
            const uint32_t f14 = vt ? vm_read32(vt + 0x14) : 0, f18 = vt ? vm_read32(vt + 0x18) : 0;
            fprintf(stderr, "[SNDSTREAM]   type %2d h=0x%08X vt=0x%08X create=0x%08X attach=0x%08X\n", t, h, vt,
                    f14 ? vm_read32(f14) : 0, f18 ? vm_read32(f18) : 0);
        }
        fflush(stderr);
        return;
    }
    if (site[0] == '2' && site[1] == 'A' && site[2] == '8') {   /* factories */
        const uint32_t dsc = (uint32_t)ctx->gpr[4];
        lines++;
        fprintf(stderr, "[SNDSTREAM] %s this=0x%08X desc=0x%08X desc.type=%u desc.w0=0x%08X lr=0x%llX\n", site,
                (uint32_t)ctx->gpr[3], dsc, dsc ? (unsigned)vm_read16(dsc + 2) : 0,
                dsc ? vm_read32(dsc) : 0, (unsigned long long)ctx->lr);
        fflush(stderr);
        return;
    }
    lines++;
    char nm[48];
    const uint32_t ea = (uint32_t)ctx->gpr[4];
    int i = 0;
    for (; ea && i < (int)sizeof nm - 1; i++) {
        unsigned c = (unsigned)vm_read8((uint64_t)(ea + (uint32_t)i)) & 0xFFu;
        if (!c) break;
        nm[i] = (c >= 0x20 && c < 0x7F) ? (char)c : '?';
    }
    nm[i] = 0;
    fprintf(stderr, "[SNDSTREAM] %s name='%s' r3=0x%llX r4=0x%08X r30=0x%08X lr=0x%llX\n", site, nm,
            (unsigned long long)ctx->gpr[3], ea, (uint32_t)ctx->gpr[30], (unsigned long long)ctx->lr);
    fflush(stderr);
}
'''


def patch_file(path: Path, sites: list[tuple[str, str]]) -> int:
    src = path.read_text()
    done = 0
    for short, sym in sites:
        head = f"void {sym}(ppu_context* PPU_RESTRICT ctx) {{\n"
        i = src.find(head)
        if i < 0:
            continue
        probe = f"        /* {MARKER} */ {{ ps3_sndstream_probe(\"{short}\", ctx); }}\n"
        j = i + len(head)
        if src[j:j + len(probe)] == probe:
            continue
        src = src[:j] + probe + src[j:]
        done += 1
    if done or "SNDSTREAM-PROBE helper" in src:
        a = src.find("\n/* SNDSTREAM-PROBE helper")
        if a >= 0:                       # refresh an older helper in place
            b = src.index("\n}\n", a) + 3
            src = src[:a] + src[b:]
        k = src.find("\nvoid func_")
        new_src = src[:k] + HELPER + src[k:]
        if new_src != path.read_text():
            path.write_text(new_src)
            done = done or 1
    return done


def main() -> int:
    lift = Path(sys.argv[1] if len(sys.argv) > 1 else ".")
    total = 0
    for f in sorted(lift.glob("ppu_recomp_*.cpp")):
        n = patch_file(f, SITES)
        if n:
            print(f"{f.name}: {n} site(s)")
        total += n
    print("PATCHED" if total else "ALREADY/none")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
