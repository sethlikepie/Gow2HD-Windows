"""Drives tools/patch_hotread_293010.py on a fixture (not the lift)."""
import importlib.util
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "patch_hotread_293010", ROOT / "patch_hotread_293010.py"
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
void func_00293010(ppu_context* ctx) {
        uint64_t _cs_23 = vm_read64(ctx->gpr[1] + 0x78);
        ctx->gpr[27] = vm_read32(ctx->gpr[2] + -0x190C);
}
void func_00248F3C(ppu_context* ctx) {
        ctx->gpr[0] = vm_read32(ctx->gpr[29] + 0x0);
        ctx->gpr[1] = vm_read64(ctx->gpr[1] + 0);
}
"""
)


def main() -> int:
    out, st = mod.transform(FIXTURE)
    if st != "APPLIED":
        print(f"FAIL expected APPLIED got {st}")
        return 1
    head, tail = out.split("void func_00248F3C", 1)
    if "vm_read64_hot(" not in head or "vm_read32_hot(" not in head:
        print("FAIL 00293010 missing hot reads")
        return 1
    if "vm_read32(" in head.replace("vm_read32_hot(", "") or "vm_read64(" in head.replace("vm_read64_hot(", ""):
        print("FAIL leftover fat accessors in 00293010")
        return 1
    if "vm_read32_hot(" in tail or "vm_read64_hot(" in tail:
        print("FAIL 00248F3C was rewritten")
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
