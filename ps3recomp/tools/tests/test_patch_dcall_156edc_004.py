"""Drives tools/patch_dcall_156edc_004.py on a fixture (not the lift)."""
import importlib.util
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "patch_dcall_156edc_004", ROOT / "patch_dcall_156edc_004.py"
)
mod = importlib.util.module_from_spec(spec)
assert spec.loader is not None
spec.loader.exec_module(mod)

FIXTURE = """void func_00248F3C(ppu_context* ctx) {
        ctx->lr = 0x00248F8C; func_00156EDC(ctx); DRAIN_TRAMPOLINE(ctx);
        ctx->lr = 0x00248C48; func_00156EDC(ctx); DRAIN_TRAMPOLINE(ctx);
}
void func_other(ppu_context* ctx) {
        func_00156EE0(ctx);
}
"""


def main() -> int:
    out, st = mod.transform(FIXTURE)
    if st != "APPLIED":
        print(f"FAIL expected APPLIED got {st}")
        return 1
    if out.count("func_001569E4(ctx)") != 2:
        print("FAIL expected 2 direct calls", out.count("func_001569E4(ctx)"))
        return 1
    if "func_00156EDC(ctx)" in out:
        print("FAIL leftover 00156EDC calls")
        return 1
    if "func_00156EE0(ctx)" not in out:
        print("FAIL unrelated call rewritten")
        return 1
    out2, st2 = mod.transform(out)
    if st2 != "ALREADY" or out2 != out:
        print(f"FAIL not idempotent st2={st2}")
        return 1
    out3, st3 = mod.transform("void foo() {}\n")
    if st3 != "NO-EFFECT":
        print(f"FAIL expected NO-EFFECT got {st3}")
        return 1
    print("PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
