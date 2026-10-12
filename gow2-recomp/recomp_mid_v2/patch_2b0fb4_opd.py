#!/usr/bin/env python3
from pathlib import Path
import sys
ROOT = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent
p = ROOT / "ppu_recomp_003.cpp"
s = p.read_text(encoding="utf-8", errors="replace")
if 'void ps3_call_opd(ppu_context* ctx, uint32_t opd_ea);' not in s:
    old = 'extern "C" void ps3_indirect_call(ppu_context* ctx);'
    if old not in s:
        raise SystemExit("no decl")
    s = s.replace(old, old + '\nextern "C" void ps3_call_opd(ppu_context* ctx, uint32_t opd_ea);', 1)
    print("declared")
i = s.find("void func_002B0FB4")
if i < 0:
    raise SystemExit("no 2B0FB4")
j = s.find("void func_", i + 20)
# find end of 2B0FB4 - it may span to next function far away; use next void func after 300 lines
k = i
for _ in range(5):
    k = s.find("void func_", k + 10)
    if k > i and k < i + 50000:
        j = k
        break
region = s[i:j]
pat = """        ctx->gpr[0] = vm_read32(ctx->gpr[10] + 0x0);
        vm_write64(ctx->gpr[1] + 0x28, ctx->gpr[2]);
        ctx->ctr = (uint32_t)ctx->gpr[0];
        ctx->gpr[2] = vm_read32(ctx->gpr[10] + 0x4);
        ps3_indirect_call(ctx); DRAIN_TRAMPOLINE(ctx);
        ctx->gpr[2] = vm_read64(ctx->gpr[1] + 0x28);"""
rep = """        vm_write64(ctx->gpr[1] + 0x28, ctx->gpr[2]);
        ps3_call_opd(ctx, (uint32_t)ctx->gpr[10]); DRAIN_TRAMPOLINE(ctx);
        ctx->gpr[2] = vm_read64(ctx->gpr[1] + 0x28);"""
n = region.count(pat)
print("sites", n)
region2 = region.replace(pat, rep)
s = s[:i] + region2 + s[j:]
p.write_text(s, encoding="utf-8", newline="\n")
print("OK")
