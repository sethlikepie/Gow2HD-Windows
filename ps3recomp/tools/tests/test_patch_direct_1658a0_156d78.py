"""Drives tools/patch_direct_1658a0_156d78.py; other icalls stay fat."""
import importlib.util
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "patch_direct_1658a0_156d78", ROOT / "patch_direct_1658a0_156d78.py"
)
mod = importlib.util.module_from_spec(spec)
assert spec.loader is not None
spec.loader.exec_module(mod)

FIXTURE = """void func_001569E4(ppu_context* ctx) {
        ps3_indirect_call(ctx); DRAIN_TRAMPOLINE(ctx);
}
void func_00156D78(ppu_context* ctx) {
        ps3_indirect_call(ctx); DRAIN_TRAMPOLINE(ctx);
        ps3_indirect_call(ctx); DRAIN_TRAMPOLINE(ctx);
        ps3_indirect_call(ctx); DRAIN_TRAMPOLINE(ctx);
        ps3_indirect_call(ctx); DRAIN_TRAMPOLINE(ctx);
}
void func_other(ppu_context* ctx) {
        ps3_indirect_call(ctx); DRAIN_TRAMPOLINE(ctx);
}
"""


def main() -> int:
    out, st = mod.transform(FIXTURE)
    if st != "APPLIED":
        print(f"FAIL expected APPLIED got {st}")
        return 1
    e4 = out.split("void func_00156D78", 1)[0]
    mid, other = out.split("void func_other", 1)
    d78 = mid.split("void func_00156D78", 1)[1]
    if "0x001658A0u" in e4.split("void func_001569E4", 1)[1]:
        print("FAIL 001569E4 specialized")
        return 1
    if d78.count("func_001658A0(ctx)") != 4:
        print("FAIL 00156D78 direct count", d78.count("func_001658A0(ctx)"))
        return 1
    if d78.count("0x001658A0u") != 4:
        print("FAIL missing CTR compare")
        return 1
    # fat icall still present on the else path
    if d78.count("ps3_indirect_call(ctx)") != 4:
        print("FAIL else-path icall count", d78.count("ps3_indirect_call(ctx)"))
        return 1
    if "0x001658A0u" in other:
        print("FAIL other specialized")
        return 1
    if "ps3_indirect_call(ctx)" not in other:
        print("FAIL other icall lost")
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
