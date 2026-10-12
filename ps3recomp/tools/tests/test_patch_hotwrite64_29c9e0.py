"""Drives tools/patch_hotwrite64_29c9e0.py; neighbors stay fat."""
import importlib.util
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "patch_hotwrite64_29c9e0", ROOT / "patch_hotwrite64_29c9e0.py"
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
void func_00291D50(ppu_context* PPU_RESTRICT ctx) {
        vm_write64(ctx->gpr[1] + 0x10, ctx->gpr[3]);
}
void func_0029C9E0(ppu_context* PPU_RESTRICT ctx) {
        vm_write64(ctx->gpr[1] + -0x90, ctx->gpr[1]);
        vm_write64(ctx->gpr[1] + 0xA0, ctx->gpr[0]);
        ctx->gpr[0] = vm_read32(ctx->gpr[11] + 0x360);
        vm_write32(ctx->gpr[10] + 0xC, ctx->gpr[9]);
}
void func_0029C140(ppu_context* PPU_RESTRICT ctx) {
        vm_write64(ctx->gpr[1] + 0x28, ctx->gpr[2]);
}
"""
)


def main() -> int:
    out, st = mod.transform(FIXTURE)
    if st != "APPLIED":
        print(f"FAIL expected APPLIED got {st}")
        return 1
    head, rest = out.split("void func_0029C9E0", 1)
    mid, tail = rest.split("void func_0029C140", 1)
    d50 = head.split("void func_00291D50", 1)[1]
    if "vm_write64_hot(" in d50:
        print("FAIL 00291D50 rewritten")
        return 1
    if mid.count("vm_write64_hot(") != 2:
        print("FAIL 0029C9E0 hot count", mid.count("vm_write64_hot("))
        return 1
    if "vm_write64(" in mid.replace("vm_write64_hot(", ""):
        print("FAIL leftover write64 in 0029C9E0")
        return 1
    if "vm_read32(" not in mid or "vm_write32(" not in mid:
        print("FAIL 0029C9E0 lost read32/write32")
        return 1
    if "vm_write64_hot(" in tail:
        print("FAIL 0029C140 rewritten")
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
