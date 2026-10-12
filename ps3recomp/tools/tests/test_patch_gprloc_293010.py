"""Drives tools/patch_gprloc_293010.py; neighbors stay on ctx->gpr."""
import importlib.util
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "patch_gprloc_293010", ROOT / "patch_gprloc_293010.py"
)
mod = importlib.util.module_from_spec(spec)
assert spec.loader is not None
spec.loader.exec_module(mod)

FIXTURE = """
void func_00248F3C(ppu_context* PPU_RESTRICT ctx) {
        ctx->gpr[3] = ctx->gpr[4];
        func_00292164(ctx); DRAIN_TRAMPOLINE(ctx);
}
void func_00293010(ppu_context* PPU_RESTRICT ctx) {
        ctx->gpr[3] = ctx->gpr[29] | ctx->gpr[29];
        ctx->lr = 0x002930EC; func_00292164(ctx); DRAIN_TRAMPOLINE(ctx);
        ctx->gpr[0] = ctx->gpr[3];
        return;
}
void func_00292164(ppu_context* PPU_RESTRICT ctx) {
        ctx->gpr[5] = 2;
}
"""


def main() -> int:
    out, st = mod.transform(FIXTURE)
    if st != "APPLIED":
        print(f"FAIL expected APPLIED got {st}")
        return 1
    start = out.find("void func_00293010(")
    end = out.find("void func_00292164(", start)
    body = out[start:end]
    neigh = out[:start] + out[end:]
    if "uint64_t r3 = ctx->gpr[3];" not in body:
        print("FAIL missing r3 load")
        return 1
    if "uint64_t r29 = ctx->gpr[29];" not in body:
        print("FAIL missing r29 load")
        return 1
    if "r3 = r29 | r29;" not in body:
        print("FAIL body not using locals", body)
        return 1
    if "ctx->gpr[3] = r3;" not in body:
        print("FAIL missing spill")
        return 1
    if "r3 = ctx->gpr[3];" not in body.split("uint64_t r3 = ctx->gpr[3];", 1)[-1]:
        print("FAIL missing fill after call")
        return 1
    if body.count("func_00292164(ctx); DRAIN_TRAMPOLINE(ctx);") != 1:
        print("FAIL call count")
        return 1
    if "r3 = r29 | r29;" in neigh:
        print("FAIL neighbor rewritten")
        return 1
    if "ctx->gpr[3] = ctx->gpr[4];" not in neigh:
        print("FAIL neighbor gpr lost")
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
