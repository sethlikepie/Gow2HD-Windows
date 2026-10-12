"""Drives the shipped trampoline-to-direct rewriter (tools/patch_direct_trampoline.py)."""
import importlib.util
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "patch_direct_trampoline", ROOT / "patch_direct_trampoline.py"
)
mod = importlib.util.module_from_spec(spec)
assert spec.loader is not None
spec.loader.exec_module(mod)


FIXTURE = """void func_00156EDC(ppu_context* ctx) {
        { g_trampoline_fn = (void(*)(void*))func_001569E4; return; }
}
void func_other(ppu_context* ctx) {
        ctx->gpr[3] = 1;
        { g_trampoline_fn = (void(*)(void*))func_001569E4; return; }
}
"""

EXPECT_HEAD = """void func_00156EDC(ppu_context* ctx) {
        func_001569E4(ctx);
}
"""


def main() -> int:
    out, n = mod.transform(FIXTURE)
    if n != 1:
        print(f"FAIL expected 1 replacement, got {n}")
        return 1
    if not out.startswith(EXPECT_HEAD):
        print("FAIL rewritten head mismatch:")
        print(out[:200])
        return 1
    if "ctx->gpr[3] = 1;" not in out:
        print("FAIL non-trivial trampoline was rewritten")
        return 1
    out2, n2 = mod.transform(out)
    if n2 != 0 or out2 != out:
        print(f"FAIL not idempotent n2={n2}")
        return 1
    print("PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
