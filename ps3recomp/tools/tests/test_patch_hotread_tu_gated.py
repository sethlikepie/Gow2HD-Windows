"""Drives tools/patch_hotread_tu_gated.py; inserts ppu_vm_hot.h after ppu_recomp.h."""
import importlib.util
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "patch_hotread_tu_gated", ROOT / "patch_hotread_tu_gated.py"
)
mod = importlib.util.module_from_spec(spec)
assert spec.loader is not None
spec.loader.exec_module(mod)

FIXTURE = """#include "ppu_recomp.h"
void func_001803C8(ppu_context* ctx) {
        ctx->gpr[3] = vm_read32(ctx->gpr[4] + 0x0);
        vm_write32(ctx->gpr[1] + 0xB0, ctx->gpr[0]);
}
"""


def main() -> int:
    out, st = mod.transform(FIXTURE)
    if st != "APPLIED":
        print(f"FAIL expected APPLIED got {st}")
        return 1
    if '#include "ppu_vm_hot.h"' not in out:
        print("FAIL missing include")
        return 1
    if out.count('#include "ppu_vm_hot.h"') != 1:
        print("FAIL include count", out.count('#include "ppu_vm_hot.h"'))
        return 1
    if 'vm_read32(ctx->gpr[4] + 0x0)' not in out:
        print("FAIL body read rewritten as text")
        return 1
    if "vm_write32(" not in out:
        print("FAIL write32 lost")
        return 1
    idx_inc = out.find('#include "ppu_vm_hot.h"')
    idx_fn = out.find("void func_001803C8")
    if not (0 <= idx_inc < idx_fn):
        print("FAIL include must precede functions")
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
