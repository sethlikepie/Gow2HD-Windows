"""Drives tools/patch_hotread_1658a0.py; must not rewrite func_001569E4."""
import importlib.util
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "patch_hotread_1658a0", ROOT / "patch_hotread_1658a0.py"
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
FIXTURE = (
    DRAIN
    + """
void func_001569E4(ppu_context* ctx) {
        ctx->gpr[11] = vm_read32(ctx->gpr[26] + 0x0);
}
void func_001658A0(ppu_context* ctx) {
        ctx->gpr[11] = vm_read32(ctx->gpr[2] + -0x3B10);
        ctx->gpr[9] = vm_read32(ctx->gpr[11] + 0x0);
}
void func_other(ppu_context* ctx) {
        ctx->gpr[3] = vm_read32(ctx->gpr[2] + 0);
}
"""
)


def main() -> int:
    out, st = mod.transform(FIXTURE)
    if st != "APPLIED":
        print(f"FAIL expected APPLIED got {st}")
        return 1
    e4 = out.split("void func_001658A0", 1)[0]
    mid, tail = out.split("void func_other", 1)
    a0 = mid.split("void func_001658A0", 1)[1]
    if "vm_read32_hot(" in e4.split("void func_001569E4", 1)[1]:
        print("FAIL 001569E4 rewritten")
        return 1
    if a0.count("vm_read32_hot(") != 2:
        print("FAIL 001658A0 hot count", a0.count("vm_read32_hot("))
        return 1
    if "vm_read32(" in a0.replace("vm_read32_hot(", ""):
        print("FAIL leftover read32 in 001658A0")
        return 1
    if "vm_read32_hot(" in tail:
        print("FAIL other rewritten")
        return 1
    if "PPU_VM_HOT_NO_MACRO" not in out or '#include "ppu_vm_hot.h"' not in out:
        print("FAIL missing include")
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
