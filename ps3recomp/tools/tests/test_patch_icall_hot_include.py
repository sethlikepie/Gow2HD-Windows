"""Drives tools/patch_icall_hot_include.py on a fixture (not the lift)."""
import importlib.util
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "patch_icall_hot_include", ROOT / "patch_icall_hot_include.py"
)
mod = importlib.util.module_from_spec(spec)
assert spec.loader is not None
spec.loader.exec_module(mod)

DRAIN = (
    "#define DRAIN_TRAMPOLINE(ctx) do { \\\n"
    "    while (g_trampoline_fn) { \\\n"
    "        void(*_tf)(void*) = g_trampoline_fn; \\\n"
    "        g_trampoline_fn = 0; \\\n"
    "        _tf((void*)(ctx)); \\\n"
    "    } \\\n"
    "} while(0)\n"
)
FIXTURE = "extern \"C\" void ps3_indirect_call(ppu_context* ctx);\n" + DRAIN + "void func_001569E4(ppu_context* ctx) {}\n"
EXPECT = (
    "extern \"C\" void ps3_indirect_call(ppu_context* ctx);\n"
    + DRAIN
    + '\n#include "ppu_icall_hot.h"\n'
    + "void func_001569E4(ppu_context* ctx) {}\n"
)


def main() -> int:
    out, st = mod.transform(FIXTURE)
    if st != "APPLIED" or out != EXPECT:
        print(f"FAIL applied st={st}")
        print(out[:200])
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
