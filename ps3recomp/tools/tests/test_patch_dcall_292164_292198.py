"""Drives tools/patch_dcall_292164_292198.py; other trampolines stay."""
import importlib.util
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "patch_dcall_292164_292198", ROOT / "patch_dcall_292164_292198.py"
)
mod = importlib.util.module_from_spec(spec)
assert spec.loader is not None
spec.loader.exec_module(mod)

FIXTURE = '''void func_00292164(ppu_context* PPU_RESTRICT ctx) {
        vm_write32(ctx->gpr[1] + 0xB8, ctx->gpr[12]);
        { g_trampoline_fn = (void(*)(void*))func_00292198; return; }
}
void func_00292198(ppu_context* PPU_RESTRICT ctx) {
        if (((ctx->cr >> 12) & 2)) { g_trampoline_fn = (void(*)(void*))func_002923A4; return; }
}
void func_other(ppu_context* ctx) {
        { g_trampoline_fn = (void(*)(void*))func_00292198; return; }
}
'''


def main() -> int:
    out, st = mod.transform(FIXTURE)
    if st != "APPLIED":
        print(f"FAIL expected APPLIED got {st}")
        return 1
    mid, other = out.split("void func_other", 1)
    p64 = mid.split("void func_00292164", 1)[1].split("void func_00292198", 1)[0]
    p98 = mid.split("void func_00292198", 1)[1]
    if "func_00292198(ctx); return;" not in p64:
        print("FAIL 00292164 not direct")
        return 1
    if "g_trampoline_fn = (void(*)(void*))func_00292198" in p64:
        print("FAIL leftover trampoline in 00292164")
        return 1
    if "g_trampoline_fn = (void(*)(void*))func_002923A4" not in p98:
        print("FAIL 00292198 trampoline rewritten")
        return 1
    if "g_trampoline_fn = (void(*)(void*))func_00292198" not in other:
        print("FAIL other rewritten")
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
