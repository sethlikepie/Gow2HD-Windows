"""Drives tools/patch_hotread64_286d44.py; neighbors/read32 stay fat."""
import importlib.util
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "patch_hotread64_286d44", ROOT / "patch_hotread64_286d44.py"
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
void func_00283AFC(ppu_context* ctx) {
        ctx->gpr[3] = vm_read64(ctx->gpr[8] + 0x0);
        ctx->gpr[4] = vm_read32(ctx->gpr[9]);
}
void func_00286D44(ppu_context* PPU_RESTRICT ctx) {
        { uint64_t tmp = vm_read64(ctx->gpr[5] + 0x0); memcpy(&ctx->fpr[13], &tmp, 8); }
        ctx->gpr[3] = vm_read32(ctx->gpr[4] + 0x0);
        { uint64_t tmp = vm_read64(ctx->gpr[5] + 0x8); memcpy(&ctx->fpr[0], &tmp, 8); }
}
void func_00286ED4(ppu_context* ctx) {
        ctx->gpr[3] = vm_read64(ctx->gpr[1] + 0x10);
}
"""
)


def main() -> int:
    out, st = mod.transform(FIXTURE)
    if st != "APPLIED":
        print(f"FAIL expected APPLIED got {st}")
        return 1
    pre, rest = out.split("void func_00286D44", 1)
    mid, other = rest.split("void func_00286ED4", 1)
    if "vm_read64_hot(" in pre.split("void func_00283AFC", 1)[1]:
        print("FAIL 00283AFC rewritten")
        return 1
    if mid.count("vm_read64_hot(") != 2:
        print("FAIL 00286D44 hot count", mid.count("vm_read64_hot("))
        return 1
    if "vm_read64(" in mid.replace("vm_read64_hot(", ""):
        print("FAIL leftover read64 in 00286D44")
        return 1
    if "vm_read32(" not in mid or "vm_read32_hot(" in mid:
        print("FAIL read32 rewritten")
        return 1
    if "vm_read64_hot(" in other:
        print("FAIL 00286ED4 rewritten")
        return 1
    if "PPU_VM_HOT_NO_MACRO" not in out or '#include "ppu_vm_hot.h"' not in out:
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
