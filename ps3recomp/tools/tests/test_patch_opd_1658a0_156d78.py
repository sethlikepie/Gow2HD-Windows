"""Drives tools/patch_opd_1658a0_156d78.py; OPD compare, other icalls stay fat."""
import importlib.util
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "patch_opd_1658a0_156d78", ROOT / "patch_opd_1658a0_156d78.py"
)
mod = importlib.util.module_from_spec(spec)
assert spec.loader is not None
spec.loader.exec_module(mod)

BLOCK11 = (
    "        ctx->gpr[0] = vm_read32(ctx->gpr[11] + 0x0);\n"
    "        vm_write64(ctx->gpr[1] + 0x28, ctx->gpr[2]);\n"
    "        ctx->ctr = (uint32_t)ctx->gpr[0];\n"
    "        ctx->gpr[2] = vm_read32(ctx->gpr[11] + 0x4);\n"
    "        ps3_indirect_call(ctx); DRAIN_TRAMPOLINE(ctx);\n"
    "        ctx->gpr[2] = 0x00541178ULL; /*TOCFIX ld r2,N(r1)*/\n"
)
BLOCK9 = BLOCK11.replace("gpr[11]", "gpr[9]")
FIXTURE = (
    "void func_001569E4(ppu_context* ctx) {\n"
    + BLOCK11
    + "}\nvoid func_00156D78(ppu_context* ctx) {\n"
    + BLOCK11
    + BLOCK9
    + BLOCK11
    + BLOCK9
    + "}\nvoid func_other(ppu_context* ctx) {\n"
    + BLOCK9
    + "}\n"
)


def main() -> int:
    out, st = mod.transform(FIXTURE)
    if st != "APPLIED":
        print(f"FAIL expected APPLIED got {st}")
        return 1
    e4 = out.split("void func_00156D78", 1)[0]
    mid, other = out.split("void func_other", 1)
    d78 = mid.split("void func_00156D78", 1)[1]
    if "0x00522988u" in e4.split("void func_001569E4", 1)[1]:
        print("FAIL 001569E4 rewritten")
        return 1
    if d78.count("0x00522988u") != 4:
        print("FAIL OPD compare count", d78.count("0x00522988u"))
        return 1
    if d78.count("func_001658A0(ctx)") != 4:
        print("FAIL direct call count", d78.count("func_001658A0(ctx)"))
        return 1
    if "0x00522988u" in other:
        print("FAIL other rewritten")
        return 1
    if "ps3_indirect_call(ctx)" not in other:
        print("FAIL other icall lost")
        return 1
    # fat path still present inside 00156D78 for misses
    if d78.count("ps3_indirect_call(ctx)") != 4:
        print("FAIL miss-path icall", d78.count("ps3_indirect_call(ctx)"))
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
