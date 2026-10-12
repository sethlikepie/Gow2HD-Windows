"""Drives tools/patch_hotwrite64_292164.py; must not rewrite neighbors."""
import importlib.util
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "patch_hotwrite64_292164", ROOT / "patch_hotwrite64_292164.py"
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
void func_00292164(ppu_context* ctx) {
        vm_write64(ctx->gpr[1] + -0xB0, ctx->gpr[1]);
        vm_write64(ctx->gpr[1] + 0xA8, ctx->gpr[31]);
}
void func_00292198(ppu_context* ctx) {
        vm_write64(ctx->gpr[1] + 0x10, ctx->gpr[3]);
}
"""
)


def main() -> int:
    out, st = mod.transform(FIXTURE)
    if st != "APPLIED":
        print(f"FAIL expected APPLIED got {st}")
        return 1
    head, tail = out.split("void func_00292198", 1)
    fn = head.split("void func_00292164", 1)[1]
    if fn.count("vm_write64_hot(") != 2:
        print("FAIL 00292164 hot count", fn.count("vm_write64_hot("))
        return 1
    if "vm_write64(" in fn.replace("vm_write64_hot(", ""):
        print("FAIL leftover write64")
        return 1
    if "vm_write64_hot(" in tail:
        print("FAIL 00292198 rewritten")
        return 1
    if "vm_write64(" not in tail:
        print("FAIL 00292198 lost write64")
        return 1
    out2, st2 = mod.transform(out)
    if st2 != "ALREADY":
        print(f"FAIL not idempotent {st2}")
        return 1
    print("PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
