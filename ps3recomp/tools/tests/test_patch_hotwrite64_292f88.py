"""Drives tools/patch_hotwrite64_292f88.py; 00292E3C and next neighbor stay fat."""
import importlib.util
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "patch_hotwrite64_292f88", ROOT / "patch_hotwrite64_292f88.py"
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
void func_00292E3C(ppu_context* PPU_RESTRICT ctx) {
        vm_write64(ctx->gpr[1] + -0x90, ctx->gpr[1]);
}
void func_00292F88(ppu_context* PPU_RESTRICT ctx) {
        vm_write64(ctx->gpr[1] + -0xC0, ctx->gpr[1]);
        vm_write64(ctx->gpr[1] + 0x28, ctx->gpr[2]);
        ctx->gpr[26] = vm_read32(ctx->gpr[2] + -0x1914);
}
void func_002932F0(ppu_context* PPU_RESTRICT ctx) {
        vm_write64(ctx->gpr[1] + -0x40, ctx->gpr[1]);
}
"""
)


def main() -> int:
    out, st = mod.transform(FIXTURE)
    if st != "APPLIED":
        print(f"FAIL expected APPLIED got {st}")
        return 1
    pre, rest = out.split("void func_00292F88", 1)
    mid, other = rest.split("void func_002932F0", 1)
    e3c = pre.split("void func_00292E3C", 1)[1]
    if "vm_write64_hot(" in e3c:
        print("FAIL 00292E3C rewritten")
        return 1
    if mid.count("vm_write64_hot(") != 2:
        print("FAIL 00292F88 hot count", mid.count("vm_write64_hot("))
        return 1
    if "vm_write64(" in mid.replace("vm_write64_hot(", ""):
        print("FAIL leftover write64 in 00292F88")
        return 1
    if "vm_read32(" not in mid:
        print("FAIL 00292F88 lost read32")
        return 1
    if "vm_write64_hot(" in other:
        print("FAIL 002932F0 rewritten")
        return 1
    if "vm_write64(" not in other:
        print("FAIL 002932F0 lost write64")
        return 1
    if "void vm_write64_hot(uint64_t addr, uint64_t val);" not in out:
        print("FAIL missing decl")
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
