"""Drives tools/patch_hotwrite64_293010.py; neighbors stay fat."""
import importlib.util
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "patch_hotwrite64_293010", ROOT / "patch_hotwrite64_293010.py"
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
void func_00248F3C(ppu_context* PPU_RESTRICT ctx) {
        vm_write64(ctx->gpr[1] + 0x28, ctx->gpr[2]);
        vm_write32(ctx->gpr[8] + 0x0, ctx->gpr[0]);
}
void func_00293010(ppu_context* PPU_RESTRICT ctx) {
        vm_write64(ctx->gpr[1] + 0x28, ctx->gpr[2]);
        vm_write64(ctx->gpr[1] + 0x28, ctx->gpr[2]);
        vm_write32(ctx->gpr[8] + 0x0, ctx->gpr[0]);
        ctx->gpr[9] = vm_read32(ctx->gpr[3] + 0x0);
}
void func_00292164(ppu_context* PPU_RESTRICT ctx) {
        vm_write64(ctx->gpr[1] + -0xB0, ctx->gpr[1]);
}
"""
)


def main() -> int:
    out, st = mod.transform(FIXTURE)
    if st != "APPLIED":
        print(f"FAIL expected APPLIED got {st}")
        return 1
    head, rest = out.split("void func_00293010", 1)
    mid, tail = rest.split("void func_00292164", 1)
    f3c = head.split("void func_00248F3C", 1)[1]
    if "vm_write64_hot(" in f3c:
        print("FAIL 00248F3C rewritten")
        return 1
    if mid.count("vm_write64_hot(") != 2:
        print("FAIL 00293010 hot count", mid.count("vm_write64_hot("))
        return 1
    if "vm_write64(" in mid.replace("vm_write64_hot(", ""):
        print("FAIL leftover write64 in 00293010")
        return 1
    if "vm_write32(" not in mid or "vm_read32(" not in mid:
        print("FAIL 00293010 lost write32/read32")
        return 1
    if "vm_write64_hot(" in tail:
        print("FAIL 00292164 rewritten")
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
