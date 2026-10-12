"""Drives tools/patch_d78_dcall_156d78.py; sampled CTR sites only."""
import importlib.util
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "patch_d78_dcall_156d78", ROOT / "patch_d78_dcall_156d78.py"
)
mod = importlib.util.module_from_spec(spec)
assert spec.loader is not None
spec.loader.exec_module(mod)

FIXTURE = '''void func_00293010(ppu_context* ctx) {
        ps3_indirect_call(ctx);
}
void func_00156D78(ppu_context* PPU_RESTRICT ctx) {
        ps3_indirect_call(ctx); DRAIN_TRAMPOLINE(ctx);
        ps3_indirect_call(ctx);
        ps3_indirect_call(ctx);
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
    p10 = out.split("void func_00156D78", 1)[0]
    mid, other = out.split("void func_other", 1)
    d78 = mid.split("void func_00156D78", 1)[1]
    if "func_001658A0" in p10.split("void func_00293010", 1)[1]:
        print("FAIL 00293010 rewritten")
        return 1
    if "func_0017FB3C(ctx)" not in d78 or "func_0017FA80(ctx)" not in d78:
        print("FAIL missing majority dcalls")
        return 1
    if d78.count("func_0017FA80(ctx)") != 2:
        print("FAIL site1/3 should both dcall 0017FA80", d78.count("func_0017FA80(ctx)"))
        return 1
    if "func_001658A0(ctx)" not in d78:
        print("FAIL missing 001658A0")
        return 1
    if d78.count("else { ps3_indirect_call(ctx); }") != 4:
        print("FAIL missing fallbacks")
        return 1
    if "func_001658A0" in other:
        print("FAIL other rewritten")
        return 1
    out2, st2 = mod.transform(out)
    if st2 != "ALREADY":
        print(f"FAIL not idempotent {st2}")
        return 1
    print("PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
