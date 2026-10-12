"""Drives tools/patch_hotread_248f3c.py on a fixture (not the lift)."""
import importlib.util
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "patch_hotread_248f3c", ROOT / "patch_hotread_248f3c.py"
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
void func_00248F3C(ppu_context* ctx) {
        ctx->gpr[0] = vm_read32(ctx->gpr[29] + 0x0);
        ctx->gpr[3] = vm_read32(ctx->gpr[25] + 0x0);
}
void func_00293010(ppu_context* ctx) {
        ctx->gpr[27] = vm_read32(ctx->gpr[2] + -0x190C);
}
"""
)


def main() -> int:
    out, st = mod.transform(FIXTURE)
    if st != "APPLIED":
        print(f"FAIL expected APPLIED got {st}")
        return 1
    head, tail = out.split("void func_00293010", 1)
    if head.count("vm_read32_hot(") != 2:
        print("FAIL 00248F3C hot count", head.count("vm_read32_hot("))
        return 1
    if "vm_read32(" in head:
        print("FAIL leftover vm_read32 in 00248F3C")
        return 1
    if "vm_read32_hot(" in tail:
        print("FAIL 00293010 was rewritten")
        return 1
    if "vm_read32(" not in tail:
        print("FAIL 00293010 lost vm_read32")
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
