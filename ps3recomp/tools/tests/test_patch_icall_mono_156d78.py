"""Drives tools/patch_icall_mono_156d78.py; 001569E4 must stay fat."""
import importlib.util
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "patch_icall_mono_156d78", ROOT / "patch_icall_mono_156d78.py"
)
mod = importlib.util.module_from_spec(spec)
assert spec.loader is not None
spec.loader.exec_module(mod)

FIXTURE = '''extern "C" void ps3_indirect_call(ppu_context* ctx);
void func_001569E4(ppu_context* ctx) {
        ps3_indirect_call(ctx); DRAIN_TRAMPOLINE(ctx);
}
void func_00156D78(ppu_context* ctx) {
        ps3_indirect_call(ctx); DRAIN_TRAMPOLINE(ctx);
        ps3_indirect_call(ctx); DRAIN_TRAMPOLINE(ctx);
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
    e4, rest = out.split("void func_00156D78", 1)
    d78, other = rest.split("void func_other", 1)
    if "ps3_indirect_call_mono" in e4.split("void func_001569E4", 1)[1]:
        print("FAIL 001569E4 rewritten")
        return 1
    if d78.count("ps3_indirect_call_mono(ctx)") != 4:
        print("FAIL 00156D78 mono count", d78.count("ps3_indirect_call_mono(ctx)"))
        return 1
    if "ps3_indirect_call(ctx)" in d78:
        print("FAIL leftover icall in 00156D78")
        return 1
    if "ps3_indirect_call_mono" in other:
        print("FAIL other rewritten")
        return 1
    if 'extern "C" void ps3_indirect_call_mono(ppu_context* ctx);' not in out:
        print("FAIL missing decl")
        return 1
    out2, st2 = mod.transform(out)
    if st2 != "ALREADY":
        print(f"FAIL not idempotent {st2}")
        return 1
    print("PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
