"""Drives tools/patch_hotread32_23272c.py; neighbors/read64 stay fat."""
import importlib.util
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "patch_hotread32_23272c", ROOT / "patch_hotread32_23272c.py"
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
void func_0023266C(ppu_context* ctx) {
        ctx->gpr[3] = vm_read32(ctx->gpr[9]);
        ctx->gpr[4] = vm_read64(ctx->gpr[1] + 0x90);
}
void func_0023272C(ppu_context* PPU_RESTRICT ctx) {
        ctx->gpr[3] = vm_read32(ctx->gpr[4] + 0x0);
        ctx->gpr[4] = vm_read64(ctx->gpr[1] + 0x90);
        ctx->gpr[5] = vm_read32(ctx->gpr[11] + 0x4);
}
void func_00232904(ppu_context* ctx) {
        ctx->gpr[3] = vm_read32(ctx->gpr[1] + 0x10);
}
"""
)


def main() -> int:
    out, st = mod.transform(FIXTURE)
    if st != "APPLIED":
        print(f"FAIL expected APPLIED got {st}")
        return 1
    pre, rest = out.split("void func_0023272C", 1)
    mid, other = rest.split("void func_00232904", 1)
    if "vm_read32_hot(" in pre.split("void func_0023266C", 1)[1]:
        print("FAIL 0023266C rewritten")
        return 1
    if mid.count("vm_read32_hot(") != 2:
        print("FAIL 0023272C hot count", mid.count("vm_read32_hot("))
        return 1
    if "vm_read32(" in mid.replace("vm_read32_hot(", ""):
        print("FAIL leftover read32 in 0023272C")
        return 1
    if "vm_read64(" not in mid or "vm_read64_hot(" in mid:
        print("FAIL read64 rewritten")
        return 1
    if "vm_read32_hot(" in other:
        print("FAIL 00232904 rewritten")
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
