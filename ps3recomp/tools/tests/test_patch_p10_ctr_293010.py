"""Drives tools/patch_p10_ctr_293010.py; numbered notes, others stay clean."""
import importlib.util
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "patch_p10_ctr_293010", ROOT / "patch_p10_ctr_293010.py"
)
mod = importlib.util.module_from_spec(spec)
assert spec.loader is not None
spec.loader.exec_module(mod)

FIXTURE = '''extern "C" void ps3_indirect_call(ppu_context* ctx);
void func_00248F3C(ppu_context* ctx) {
        ctx->ctr = (uint32_t)ctx->gpr[0];
        ps3_indirect_call(ctx);
}
void func_00293010(ppu_context* PPU_RESTRICT ctx) {
        ctx->ctr = (uint32_t)ctx->gpr[0];
        ps3_indirect_call(ctx); DRAIN_TRAMPOLINE(ctx);
        ctx->ctr = (uint32_t)ctx->gpr[0];
        ps3_indirect_call(ctx);
        ctx->ctr = (uint32_t)ctx->gpr[0];
        ps3_indirect_call(ctx);
        ctx->ctr = (uint32_t)ctx->gpr[0];
        ps3_indirect_call(ctx);
}
void func_other(ppu_context* ctx) {
        ps3_indirect_call(ctx);
}
'''


def main() -> int:
    out, st = mod.transform(FIXTURE)
    if st != "APPLIED":
        print(f"FAIL expected APPLIED got {st}")
        return 1
    f3c = out.split("void func_00293010", 1)[0]
    mid, other = out.split("void func_other", 1)
    p10 = mid.split("void func_00293010", 1)[1]
    if "ps3_p10_ctr_note" in f3c.split("void func_00248F3C", 1)[1]:
        print("FAIL 00248F3C rewritten")
        return 1
    for n in range(4):
        if f"ps3_p10_ctr_note({n}, (uint32_t)ctx->ctr)" not in p10:
            print("FAIL missing note", n)
            return 1
    if p10.count("ps3_indirect_call(ctx)") != 4:
        print("FAIL icalls removed")
        return 1
    if "ps3_p10_ctr_note" in other:
        print("FAIL other rewritten")
        return 1
    if 'extern "C" void ps3_p10_ctr_note(unsigned site, uint32_t ctr);' not in out:
        print("FAIL missing decl")
        return 1
    out2, st2 = mod.transform(out)
    if st2 != "ALREADY":
        print(f"FAIL not idempotent {st2}")
        return 1
    out3, st3 = mod.transform("void foo() {}\n")
    if st3 != "NO-EFFECT":
        print(f"FAIL expected NO-EFFECT got {st3}")
        return 1
    print("PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
