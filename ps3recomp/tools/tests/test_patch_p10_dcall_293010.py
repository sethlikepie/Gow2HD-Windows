"""Drives tools/patch_p10_dcall_293010.py; sampled CTR sites only."""
import importlib.util
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "patch_p10_dcall_293010", ROOT / "patch_p10_dcall_293010.py"
)
mod = importlib.util.module_from_spec(spec)
assert spec.loader is not None
spec.loader.exec_module(mod)

FIXTURE = '''void func_00248F3C(ppu_context* ctx) {
        ps3_indirect_call(ctx);
}
void func_00293010(ppu_context* PPU_RESTRICT ctx) {
        ps3_indirect_call(ctx); DRAIN_TRAMPOLINE(ctx);
        ps3_indirect_call(ctx);
        ps3_indirect_call(ctx);
        ps3_indirect_call(ctx);
}
void func_other(ppu_context* ctx) {
        ps3_indirect_call(ctx);
}
'''
EAS = ("00297310", "002BED34", "0029731C", "002BED40")


def main() -> int:
    out, st = mod.transform(FIXTURE)
    if st != "APPLIED":
        print(f"FAIL expected APPLIED got {st}")
        return 1
    f3c = out.split("void func_00293010", 1)[0]
    mid, other = out.split("void func_other", 1)
    p10 = mid.split("void func_00293010", 1)[1]
    if "func_00297310" in f3c.split("void func_00248F3C", 1)[1]:
        print("FAIL 00248F3C rewritten")
        return 1
    for ea in EAS:
        if f"func_{ea}(ctx)" not in p10:
            print("FAIL missing dcall", ea)
            return 1
        if f"0x{ea}u" not in p10:
            print("FAIL missing compare", ea)
            return 1
    if p10.count("else { ps3_indirect_call(ctx); }") != 4:
        print("FAIL missing fallbacks")
        return 1
    if "func_00297310" in other:
        print("FAIL other rewritten")
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
