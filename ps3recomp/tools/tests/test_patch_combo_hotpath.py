"""Compose hr + w64 + mono on 000.cpp fixture and dcall on 004.cpp fixture."""
import importlib.util
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def load(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / f"{name}.py")
    mod = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(mod)
    return mod


DRAIN = (
    "#define DRAIN_TRAMPOLINE(ctx) do { \\\n"
    "    while (g_trampoline_fn) { \\\n"
    "        void(*_tf)(void*) = g_trampoline_fn; \\\n"
    "        g_trampoline_fn = 0; \\\n"
    "        _tf((void*)(ctx)); \\\n"
    "    } \\\n"
    "} while(0)\n"
)
FIXTURE_000 = (
    DRAIN
    + """
extern "C" void ps3_indirect_call(ppu_context* ctx);
void func_001569E4(ppu_context* ctx) {
        vm_write64(ctx->gpr[1] + 0x80, ctx->gpr[21]);
        ctx->gpr[11] = vm_read32(ctx->gpr[26] + 0x0);
        ps3_indirect_call(ctx); DRAIN_TRAMPOLINE(ctx);
        vm_write64(ctx->gpr[1] + 0x28, ctx->gpr[2]);
}
void func_other(ppu_context* ctx) {
        vm_write64(ctx->gpr[1] + 0x10, ctx->gpr[3]);
        ctx->gpr[3] = vm_read32(ctx->gpr[2] + 0);
        ps3_indirect_call(ctx);
}
"""
)
FIXTURE_004 = """
void func_00248F3C(ppu_context* ctx) {
        func_00156EDC(ctx); DRAIN_TRAMPOLINE(ctx);
        func_00156EDC(ctx);
}
void func_00156EDC(ppu_context* ctx) {
        g_trampoline_fn = (void(*)(void*))func_001569E4; return;
}
"""


def main() -> int:
    hr = load("patch_hotread_1569e4")
    w64 = load("patch_hotwrite64_1569e4")
    mono = load("patch_icall_mono_1569e4")
    dcall = load("patch_dcall_156edc_004")

    text, st = hr.transform(FIXTURE_000)
    if st != "APPLIED":
        print("FAIL hr", st)
        return 1
    text, st = w64.transform(text)
    if st != "APPLIED":
        print("FAIL w64", st)
        return 1
    text, st = mono.transform(text)
    if st != "APPLIED":
        print("FAIL mono", st)
        return 1

    head, tail = text.split("void func_other", 1)
    fn = head.split("void func_001569E4", 1)[1]
    if fn.count("vm_read32_hot(") != 1:
        print("FAIL read32_hot", fn.count("vm_read32_hot("))
        return 1
    if "vm_read32(" in fn.replace("vm_read32_hot(", ""):
        print("FAIL leftover read32")
        return 1
    if fn.count("vm_write64_hot(") != 2:
        print("FAIL write64_hot", fn.count("vm_write64_hot("))
        return 1
    if "vm_write64(" in fn.replace("vm_write64_hot(", ""):
        print("FAIL leftover write64")
        return 1
    if fn.count("ps3_indirect_call_mono(ctx)") != 1:
        print("FAIL mono count")
        return 1
    if "ps3_indirect_call(ctx)" in fn:
        print("FAIL leftover icall in 001569E4")
        return 1
    if "vm_read32_hot(" in tail or "vm_write64_hot(" in tail or "ps3_indirect_call_mono" in tail:
        print("FAIL other function rewritten")
        return 1
    if "PPU_VM_HOT_NO_MACRO" not in text:
        print("FAIL missing hot include")
        return 1
    if "void vm_write64_hot(uint64_t addr, uint64_t val);" not in text:
        print("FAIL missing write64 decl")
        return 1

    for mod, label in ((hr, "hr"), (w64, "w64"), (mono, "mono")):
        t2, st2 = mod.transform(text)
        if st2 != "ALREADY":
            print(f"FAIL {label} not idempotent {st2}")
            return 1

    out4, st4 = dcall.transform(FIXTURE_004)
    if st4 != "APPLIED":
        print("FAIL dcall", st4)
        return 1
    sites, tramp = out4.split("void func_00156EDC", 1)
    if sites.count("func_001569E4(ctx)") != 2:
        print("FAIL dcall site count", sites.count("func_001569E4(ctx)"))
        return 1
    if "func_00156EDC(ctx)" in sites:
        print("FAIL leftover 56EDC at call sites")
        return 1
    if "func_001569E4" not in tramp:
        print("FAIL trampoline body lost target")
        return 1
    out5, st5 = dcall.transform(out4)
    if st5 != "ALREADY":
        print("FAIL dcall not idempotent", st5)
        return 1
    print("PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
