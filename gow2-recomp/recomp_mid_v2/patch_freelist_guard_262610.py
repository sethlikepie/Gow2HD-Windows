#!/usr/bin/env python3
"""FREELIST-TAG-GUARD for func_00262610 (alloc free-list walk).

263178 already aborts when [node+4] holds a boundary tag (bit31). The alloc
walk at 00262610 did not — post-WADLD-BODY (SBP_general) it spins forever
(hang sample: 41D5C→BACE8→262610) either on tag next (0x91/0x95/0x80) or on
insane need=0x687DD790 (~1.7GiB) from a corrupt WAD member header after the
256 KiB F2B stream window.

Guards (always-on safety, same class as 263178 / ALLOC-NULL-GUARD):
  1) entry: arena==0 OR free-head bit31 OR head near-null OR need>64MiB → r3=0
  2) walk: next bit31 OR near-null → 2627EC (OOM)
  3) walk: iteration cap 200000 → 2627EC (cycle/desync)

Idempotent. Apply to recomp_* /ppu_recomp_000.cpp (argv or cwd).
"""
from __future__ import annotations
import sys
from pathlib import Path

def main() -> int:
    roots = []
    if len(sys.argv) > 1:
        roots.append(Path(sys.argv[1]))
    roots += [
        Path(__file__).resolve().parent,
        Path("/Users/andrebrumcortezferreira/Documents/PESSOAL/gow2-recomp/recomp_macos_v2"),
    ]
    path = None
    for r in roots:
        c = r / "ppu_recomp_000.cpp"
        if c.is_file():
            path = c
            break
    if path is None:
        print("FAIL: ppu_recomp_000.cpp not found", file=sys.stderr)
        return 1
    s = path.read_text(encoding="utf-8", errors="replace")
    changed = False

    if "FREELIST-TAG-GUARD] 262610 entry" not in s:
        old = """void func_00262610(ppu_context* ctx) {
        ctx->gpr[4] = (int64_t)(int32_t)(ctx->gpr[4] + 3);
        vm_write64(ctx->gpr[1] + -0x20, ctx->gpr[28]);
        ctx->gpr[4] = (uint32_t)ppc_rlwinm((uint32_t)ctx->gpr[4], 30, 2, 31);
        vm_write64(ctx->gpr[1] + -0x18, ctx->gpr[29]);
        ctx->gpr[4] = (int64_t)(int32_t)(ctx->gpr[4] + 2);
        vm_write64(ctx->gpr[1] + -0x10, ctx->gpr[30]);
        { uint64_t a = (uint32_t)ctx->gpr[4]; uint64_t b = (uint64_t)0x3; uint32_t cr_val = (a < b) ? 8 : (a > b) ? 4 : 2; ctx->cr = (ctx->cr & ~(0xFu << 0)) | (cr_val << 0); }
        vm_write64(ctx->gpr[1] + -0x8, ctx->gpr[31]);
        if (((ctx->cr >> 0) & 4)) goto loc_00262638;
        ctx->gpr[4] = (int64_t)(int32_t)(4);
loc_00262638:
        ctx->gpr[8] = ctx->gpr[3] | ctx->gpr[3];
        ctx->gpr[3] = vm_read32(ctx->gpr[3] + 0x4);"""
        new = """void func_00262610(ppu_context* ctx) {
        /* FREELIST-TAG-GUARD (alloc walk): same class as 263178. */
        { uint32_t arena=(uint32_t)ctx->gpr[3];
          uint32_t need=(uint32_t)ctx->gpr[4];
          uint32_t head = arena ? vm_read32(arena+0x4) : 0u;
          if (arena==0u || (head & 0x80000000u) || (head && head < 0x10000u) || need > 0x4000000u) {
            static int _n=0; if(_n++<32){
              fprintf(stderr,"[FREELIST-TAG-GUARD] 262610 entry arena=0x%08X head=0x%08X need=0x%X → abort r3=0\\n",
                arena, head, need); fflush(stderr); }
            ctx->gpr[3] = 0;
            return;
          } }
        ctx->gpr[4] = (int64_t)(int32_t)(ctx->gpr[4] + 3);
        vm_write64(ctx->gpr[1] + -0x20, ctx->gpr[28]);
        ctx->gpr[4] = (uint32_t)ppc_rlwinm((uint32_t)ctx->gpr[4], 30, 2, 31);
        vm_write64(ctx->gpr[1] + -0x18, ctx->gpr[29]);
        ctx->gpr[4] = (int64_t)(int32_t)(ctx->gpr[4] + 2);
        vm_write64(ctx->gpr[1] + -0x10, ctx->gpr[30]);
        { uint64_t a = (uint32_t)ctx->gpr[4]; uint64_t b = (uint64_t)0x3; uint32_t cr_val = (a < b) ? 8 : (a > b) ? 4 : 2; ctx->cr = (ctx->cr & ~(0xFu << 0)) | (cr_val << 0); }
        vm_write64(ctx->gpr[1] + -0x8, ctx->gpr[31]);
        if (((ctx->cr >> 0) & 4)) goto loc_00262638;
        ctx->gpr[4] = (int64_t)(int32_t)(4);
loc_00262638:
        ctx->gpr[8] = ctx->gpr[3] | ctx->gpr[3];
        ctx->gpr[3] = vm_read32(ctx->gpr[3] + 0x4);"""
        if old not in s:
            print("FAIL: entry needle missing", file=sys.stderr)
            return 2
        s = s.replace(old, new, 1)
        changed = True
        print("patched 262610 entry guard")
    else:
        print("entry already")

    if "FREELIST-TAG-GUARD] 262610 walk" not in s and "_fl_walk_iters" not in s:
        old = """        ctx->gpr[7] = (uint32_t)ppc_rlwinm((uint32_t)ctx->gpr[4], 2, 0, 29);
        ctx->gpr[12] = (int64_t)(int32_t)(0);
loc_00262650:
        ctx->gpr[10] = ppc_rldicl(ctx->gpr[3], 0, 32);"""
        new = """        ctx->gpr[7] = (uint32_t)ppc_rlwinm((uint32_t)ctx->gpr[4], 2, 0, 29);
        ctx->gpr[12] = (int64_t)(int32_t)(0);
        static int _fl_walk_iters = 0; _fl_walk_iters = 0;
loc_00262650:
        if (++_fl_walk_iters > 200000) {
          static int _n=0; if(_n++<16){
            fprintf(stderr,"[FREELIST-TAG-GUARD] 262610 walk iter>%d node=0x%08X → abort r3=0\\n",
              200000, (uint32_t)ctx->gpr[3]); fflush(stderr); }
          { g_trampoline_fn = (void(*)(void*))func_002627EC; return; }
        }
        ctx->gpr[10] = ppc_rldicl(ctx->gpr[3], 0, 32);"""
        i = s.find("void func_00262610")
        j = s.find(old, i if i >= 0 else 0)
        if j < 0:
            print("FAIL: iter needle missing", file=sys.stderr)
            return 3
        s = s[:j] + new + s[j + len(old) :]
        changed = True
        print("patched 262610 iter cap")
    else:
        print("iter already")

    if "FREELIST-TAG-GUARD] 262610 walk next" not in s:
        # optional mid-walk next tag check — only if not already
        pass

    # walk next guard
    marker = "FREELIST-TAG-GUARD] 262610 walk next"
    if marker not in s and "262610 walk next=0x" not in s:
        old = """loc_0026269C:
        ctx->gpr[3] = vm_read32(ctx->gpr[10] + 0x4);
        { int64_t a = (int32_t)ctx->gpr[8]; int64_t b = (int32_t)ctx->gpr[3]; uint32_t cr_val = (a < b) ? 8 : (a > b) ? 4 : 2; ctx->cr = (ctx->cr & ~(0xFu << 0)) | (cr_val << 0); }
        if ((!((ctx->cr >> 0) & 2))) goto loc_00262650;"""
        new = """loc_0026269C:
        ctx->gpr[3] = vm_read32(ctx->gpr[10] + 0x4);
        { uint32_t nx=(uint32_t)ctx->gpr[3];
          if ((nx & 0x80000000u) || (nx && nx < 0x10000u)) {
            static int _n=0; if(_n++<32){
              fprintf(stderr,"[FREELIST-TAG-GUARD] 262610 walk next=0x%08X node=0x%08X → abort r3=0\\n",
                nx, (uint32_t)ctx->gpr[10]); fflush(stderr); }
            { g_trampoline_fn = (void(*)(void*))func_002627EC; return; }
          } }
        { int64_t a = (int32_t)ctx->gpr[8]; int64_t b = (int32_t)ctx->gpr[3]; uint32_t cr_val = (a < b) ? 8 : (a > b) ? 4 : 2; ctx->cr = (ctx->cr & ~(0xFu << 0)) | (cr_val << 0); }
        if ((!((ctx->cr >> 0) & 2))) goto loc_00262650;"""
        i = s.find("void func_00262610")
        j = s.find(old, i if i >= 0 else 0)
        if j < 0:
            print("WARN: walk-next needle missing (may already be patched)")
        else:
            s = s[:j] + new + s[j + len(old) :]
            changed = True
            print("patched 262610 walk-next guard")
    else:
        print("walk-next already")

    if changed:
        path.write_text(s, encoding="utf-8")
    print(f"OK {path}")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
