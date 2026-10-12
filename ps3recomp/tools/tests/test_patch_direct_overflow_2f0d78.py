"""Drives tools/patch_direct_overflow_2f0d78.py; other icalls stay fat."""
import importlib.util
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "patch_direct_overflow_2f0d78", ROOT / "patch_direct_overflow_2f0d78.py"
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
void func_002F0D78(ppu_context* ctx) {
        if ((!((ctx->cr >> 0) & 4))) goto loc_002F0DC0;
        ctx->gpr[9] = vm_read32(ctx->gpr[10] + 0xC);
        ctx->gpr[0] = vm_read32(ctx->gpr[9] + 0x0);
        vm_write64(ctx->gpr[1] + 0x28, ctx->gpr[2]);
        ctx->ctr = (uint32_t)ctx->gpr[0];
        ctx->gpr[2] = vm_read32(ctx->gpr[9] + 0x4);
        ps3_indirect_call(ctx); DRAIN_TRAMPOLINE(ctx);
        ctx->gpr[2] = 0x00541178ULL; /*TOCFIX ld r2,N(r1)*/
loc_002F0DC0:
        return;
}
void func_other(ppu_context* ctx) {
        ps3_indirect_call(ctx); DRAIN_TRAMPOLINE(ctx);
}
"""
)


def main() -> int:
    out, st = mod.transform(FIXTURE)
    if st != "APPLIED":
        print(f"FAIL expected APPLIED got {st}")
        return 1
    head, tail = out.split("void func_other", 1)
    fn = head.split("void func_002F0D78", 1)[1]
    if "ps3_indirect_call(ctx)" in fn:
        print("FAIL leftover icall in 002F0D78")
        return 1
    if "ps3_gcm_ctx_overflow((uint32_t)ctx->gpr[10])" not in fn:
        print("FAIL missing direct HLE")
        return 1
    if "ps3_indirect_call(ctx)" not in tail:
        print("FAIL other icall removed")
        return 1
    if "extern \"C\" int32_t ps3_gcm_ctx_overflow(uint32_t ctx_ea);" not in out:
        print("FAIL missing decl")
        return 1
    # space check kept
    if "goto loc_002F0DC0" not in fn:
        print("FAIL lost space check")
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
