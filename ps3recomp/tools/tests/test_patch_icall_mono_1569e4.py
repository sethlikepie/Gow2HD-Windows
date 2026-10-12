"""Drives tools/patch_icall_mono_1569e4.py on a fixture (not the lift)."""
import importlib.util
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "patch_icall_mono_1569e4", ROOT / "patch_icall_mono_1569e4.py"
)
mod = importlib.util.module_from_spec(spec)
assert spec.loader is not None
spec.loader.exec_module(mod)

FIXTURE = '''extern "C" void ps3_indirect_call(ppu_context* ctx);
void func_001569E4(ppu_context* ctx) {
        ctx->ctr = (uint32_t)ctx->gpr[0];
        ps3_indirect_call(ctx); DRAIN_TRAMPOLINE(ctx);
}
void func_other(ppu_context* ctx) {
        ps3_indirect_call(ctx); DRAIN_TRAMPOLINE(ctx);
}
'''


def main() -> int:
    out, st = mod.transform(FIXTURE)
    if st != "APPLIED":
        print(f"FAIL expected APPLIED got {st}")
        return 1
    if "ps3_indirect_call_mono(ctx)" not in out.split("func_other")[0]:
        print("FAIL 001569E4 not rewritten")
        return 1
    if "void func_other" in out and out.split("void func_other")[1].count("ps3_indirect_call_mono") != 0:
        print("FAIL other function was rewritten")
        return 1
    if 'extern "C" void ps3_indirect_call_mono(ppu_context* ctx);' not in out:
        print("FAIL missing mono decl")
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
