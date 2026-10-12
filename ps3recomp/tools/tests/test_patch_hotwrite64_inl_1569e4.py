"""Drives tools/patch_hotwrite64_inl_1569e4.py on a fixture (not the lift)."""
import importlib.util
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "patch_hotwrite64_inl_1569e4", ROOT / "patch_hotwrite64_inl_1569e4.py"
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
        vm_write64(ctx->gpr[1] + 0x80, ctx->gpr[21]);
        vm_write64(ctx->gpr[1] + 0x28, ctx->gpr[2]);
}
void func_other(ppu_context* ctx) {
        vm_write64(ctx->gpr[1] + 0x10, ctx->gpr[3]);
}
"""
)


def main() -> int:
    out, st = mod.transform(FIXTURE)
    if st != "APPLIED":
        print(f"FAIL expected APPLIED got {st}")
        return 1
    head, tail = out.split("void func_other", 1)
    fn = head.split("void func_001569E4", 1)[1]
    if fn.count("vm_write64_hot(") != 2:
        print("FAIL 001569E4 hot count", fn.count("vm_write64_hot("))
        return 1
    if "vm_write64(" in fn.replace("vm_write64_hot(", ""):
        print("FAIL leftover vm_write64 in 001569E4")
        return 1
    if "vm_write64_hot(" in tail:
        print("FAIL other function rewritten")
        return 1
    if "vm_write64(" not in tail:
        print("FAIL other function lost vm_write64")
        return 1
    if "PPU_VM_HOT_NO_MACRO" not in out or '#include "ppu_vm_hot.h"' not in out:
        print("FAIL missing inline include")
        return 1
    if 'void vm_write64_hot(uint64_t addr, uint64_t val);' in out:
        print("FAIL must not emit out-of-line decl")
        return 1
    out2, st2 = mod.transform(out)
    if st2 != "ALREADY":
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
