"""Drives tools/patch_elide_cs_1658a0.py; keep backchain/lr/TOC, drop r14–r31."""
import importlib.util
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "patch_elide_cs_1658a0", ROOT / "patch_elide_cs_1658a0.py"
)
mod = importlib.util.module_from_spec(spec)
assert spec.loader is not None
spec.loader.exec_module(mod)

FIXTURE = """void func_001569E4(ppu_context* ctx) {
        vm_write64(ctx->gpr[1] + 0x80, ctx->gpr[21]);
}
void func_001658A0(ppu_context* ctx) {
        vm_write64(ctx->gpr[1] + -0x110, ctx->gpr[1]);
        vm_write64(ctx->gpr[1] + 0x120, ctx->gpr[0]);
        vm_write64(ctx->gpr[1] + 0x80, ctx->gpr[14]);
        vm_write64(ctx->gpr[1] + 0x108, ctx->gpr[31]);
        vm_write64(ctx->gpr[1] + 0x28, ctx->gpr[2]);
        ctx->gpr[14] = _cs_14;
}
void func_other(ppu_context* ctx) {
        vm_write64(ctx->gpr[1] + 0x80, ctx->gpr[14]);
}
"""


def main() -> int:
    out, st = mod.transform(FIXTURE)
    if st != "APPLIED":
        print(f"FAIL expected APPLIED got {st}")
        return 1
    a0 = out.split("void func_001658A0", 1)[1].split("void func_other", 1)[0]
    e4 = out.split("void func_001569E4", 1)[1].split("void func_001658A0", 1)[0]
    other = out.split("void func_other", 1)[1]
    if "vm_write64(ctx->gpr[1] + 0x80, ctx->gpr[21])" not in e4:
        print("FAIL 001569E4 spill removed")
        return 1
    if "vm_write64(ctx->gpr[1] + 0x80, ctx->gpr[14])" not in other:
        print("FAIL other spill removed")
        return 1
    if "vm_write64(ctx->gpr[1] + -0x110, ctx->gpr[1])" not in a0:
        print("FAIL lost backchain")
        return 1
    if "vm_write64(ctx->gpr[1] + 0x120, ctx->gpr[0])" not in a0:
        print("FAIL lost lr")
        return 1
    if "vm_write64(ctx->gpr[1] + 0x28, ctx->gpr[2])" not in a0:
        print("FAIL lost TOC")
        return 1
    if "vm_write64(ctx->gpr[1] + 0x80, ctx->gpr[14])" in a0:
        print("FAIL r14 spill kept")
        return 1
    if "vm_write64(ctx->gpr[1] + 0x108, ctx->gpr[31])" in a0:
        print("FAIL r31 spill kept")
        return 1
    if "ELIDE-CS-1658A0" not in a0:
        print("FAIL missing marker")
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
