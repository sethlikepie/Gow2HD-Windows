"""Drives tools/patch_flatten_293010.py; neighbors stay unmarked."""
import importlib.util
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "patch_flatten_293010", ROOT / "patch_flatten_293010.py"
)
mod = importlib.util.module_from_spec(spec)
assert spec.loader is not None
spec.loader.exec_module(mod)

FIXTURE = """
void func_00248F3C(ppu_context* PPU_RESTRICT ctx) {
        ctx->gpr[3] = 0;
}
void func_00293010(ppu_context* PPU_RESTRICT ctx) {
        ctx->gpr[3] = 1;
}
void func_00292164(ppu_context* PPU_RESTRICT ctx) {
        ctx->gpr[3] = 2;
}
"""


def main() -> int:
    out, st = mod.transform(FIXTURE)
    if st != "APPLIED":
        print(f"FAIL expected APPLIED got {st}")
        return 1
    if out.count("__attribute__((flatten))") != 1:
        print("FAIL flatten count", out.count("__attribute__((flatten))"))
        return 1
    if "void __attribute__((flatten)) func_00293010(" not in out:
        print("FAIL 00293010 missing flatten")
        return 1
    if "__attribute__((flatten)) func_00248F3C" in out:
        print("FAIL 00248F3C marked")
        return 1
    if "__attribute__((flatten)) func_00292164" in out:
        print("FAIL 00292164 marked")
        return 1
    if "void func_00293010(" in out:
        print("FAIL leftover unmarked 00293010")
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
