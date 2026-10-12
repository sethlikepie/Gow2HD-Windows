"""Drives tools/patch_hotread_2b8204.py; other functions stay fat."""
import importlib.util
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "patch_hotread_2b8204", ROOT / "patch_hotread_2b8204.py"
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
void func_00291D50(ppu_context* ctx) {
        ctx->gpr[26] = vm_read32(ctx->gpr[2] + -0x1950);
}
void func_002B8204(ppu_context* PPU_RESTRICT ctx) {
        ctx->gpr[9] = vm_read32(ctx->gpr[3] + 0x0);
        ctx->gpr[8] = vm_read32(ctx->gpr[9] + 0x4);
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
    d50 = out.split("void func_002B8204", 1)[0]
    mid, other = out.split("void func_other", 1)
    b82 = mid.split("void func_002B8204", 1)[1]
    if "vm_read32_hot(" in d50.split("void func_00291D50", 1)[1]:
        print("FAIL 00291D50 rewritten")
        return 1
    if b82.count("vm_read32_hot(") != 2:
        print("FAIL 002B8204 hot count", b82.count("vm_read32_hot("))
        return 1
    if "vm_read32(" in b82.replace("vm_read32_hot(", ""):
        print("FAIL leftover read32 in 002B8204")
        return 1
    if "vm_read32_hot(" in other:
        print("FAIL other rewritten")
        return 1
    if "PPU_VM_HOT_NO_MACRO" not in out:
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
