"""Drives tools/patch_hotwrite64_195d58.py; must not rewrite func_001569E4."""
import importlib.util
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "patch_hotwrite64_195d58", ROOT / "patch_hotwrite64_195d58.py"
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
}
void func_00195D58(ppu_context* ctx) {
        vm_write64(ctx->gpr[1] + -0x80, ctx->gpr[1]);
        vm_write64(ctx->gpr[31] + 0x8, ctx->gpr[3]);
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
    e4 = out.split("void func_00195D58", 1)[0]
    mid, tail = out.split("void func_other", 1)
    s58 = mid.split("void func_00195D58", 1)[1]
    if "vm_write64_hot(" in e4.split("void func_001569E4", 1)[1]:
        print("FAIL 001569E4 was rewritten")
        return 1
    if s58.count("vm_write64_hot(") != 2:
        print("FAIL 00195D58 hot count", s58.count("vm_write64_hot("))
        return 1
    if "vm_write64(" in s58.replace("vm_write64_hot(", ""):
        print("FAIL leftover write64 in 00195D58")
        return 1
    if "vm_write64_hot(" in tail:
        print("FAIL other function rewritten")
        return 1
    if "void vm_write64_hot(uint64_t addr, uint64_t val);" not in out:
        print("FAIL missing decl")
        return 1
    out2, st2 = mod.transform(out)
    if st2 != "ALREADY":
        print(f"FAIL not idempotent st2={st2}")
        return 1
    print("PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
