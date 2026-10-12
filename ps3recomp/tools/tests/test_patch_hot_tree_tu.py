"""Drives tools/patch_hot_tree_tu.py: extract 3 funcs into a new TU, strip sources."""
import importlib.util
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "patch_hot_tree_tu", ROOT / "patch_hot_tree_tu.py"
)
mod = importlib.util.module_from_spec(spec)
assert spec.loader is not None
spec.loader.exec_module(mod)

PREAMBLE = """#include "ppu_recomp.h"
#define DRAIN_TRAMPOLINE(ctx) do { } while(0)
static inline uint64_t ppc_rldicl(uint64_t rs, int sh, int mb) { (void)sh; (void)mb; return rs; }
void func_00059104(ppu_context* PPU_RESTRICT ctx) {
        ctx->gpr[3] = 1;
}
"""
SRC004 = (
    PREAMBLE
    + """
void func_00248F3C(ppu_context* PPU_RESTRICT ctx) {
        ctx->gpr[3] = vm_read32(ctx->gpr[4]);
}
void func_00293010(ppu_context* PPU_RESTRICT ctx) {
        ctx->lr = 1; func_00248F3C(ctx); DRAIN_TRAMPOLINE(ctx);
}
void func_00294000(ppu_context* PPU_RESTRICT ctx) {
        ctx->gpr[3] = 2;
}
"""
)
SRC003 = (
    PREAMBLE
    + """
void func_00156D78(ppu_context* PPU_RESTRICT ctx) {
        ctx->gpr[3] = vm_read32(ctx->gpr[5]);
}
void func_00157000(ppu_context* PPU_RESTRICT ctx) {
        ctx->gpr[3] = 3;
}
"""
)


def main() -> int:
    s003, s004, out_cpp, st = mod.transform(SRC003, SRC004)
    if st != "APPLIED":
        print(f"FAIL expected APPLIED got {st}")
        return 1
    if "void func_00293010(" not in out_cpp:
        print("FAIL missing 00293010 in hot tree")
        return 1
    if "void func_00248F3C(" not in out_cpp:
        print("FAIL missing 00248F3C in hot tree")
        return 1
    if "void func_00156D78(" not in out_cpp:
        print("FAIL missing 00156D78 in hot tree")
        return 1
    if "void func_00294000(" in out_cpp:
        print("FAIL neighbor 00294000 leaked into hot tree")
        return 1
    if "void func_00059104(" in out_cpp.split("void func_00248F3C", 1)[0]:
        # preamble may include first-func from 004; we only copy helpers
        pass
    if "void func_00293010(" in s004:
        print("FAIL 00293010 still in 004")
        return 1
    if "void func_00248F3C(" in s004:
        print("FAIL 00248F3C still in 004")
        return 1
    if "void func_00294000(" not in s004:
        print("FAIL 004 lost neighbor")
        return 1
    if "void func_00156D78(" in s003:
        print("FAIL 00156D78 still in 003")
        return 1
    if "void func_00157000(" not in s003:
        print("FAIL 003 lost neighbor")
        return 1
    if "ppc_rldicl" not in out_cpp:
        print("FAIL hot tree missing rotate helper")
        return 1
    if "DRAIN_TRAMPOLINE" not in out_cpp:
        print("FAIL hot tree missing DRAIN")
        return 1
    _, _, _, st2 = mod.transform(s003, s004)
    if st2 != "NO-EFFECT":
        print(f"FAIL not idempotent on stripped sources {st2}")
        return 1
    print("PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
