#!/usr/bin/env python3
"""
Fix stream-reader OPD in func_001856A8 (used by SHADERSRC + ICGLdr).

This function refills/consumes a guest stream via vtable method at [vt+0x8].
It used ps3_indirect_call (treats OPD as raw code) — same broken class as
GroupEnd/factory. Symptom: SHADERSRC always reads N=0 (first u32 of stream).
"""
from pathlib import Path
import sys

ROOT = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent
p = ROOT / "ppu_recomp_000.cpp"
s = p.read_text(encoding="utf-8", errors="replace")

if 'void ps3_call_opd(ppu_context* ctx, uint32_t opd_ea);' not in s[:40000]:
    old = 'extern "C" void ps3_indirect_call(ppu_context* ctx);'
    if old not in s[:40000]:
        raise SystemExit("no ps3_indirect_call decl in 000")
    s = s.replace(old, old + '\nextern "C" void ps3_call_opd(ppu_context* ctx, uint32_t opd_ea);', 1)
    print("000: declared ps3_call_opd")

i = s.find("void func_001856A8")
if i < 0:
    raise SystemExit("1856A8 missing")
j = s.find("void func_", i + 10)
region = s[i:j]

# NOTA (2026-07-25): a agulha terminava no restauro do TOC
#   ctx->gpr[2] = vm_read64(ctx->gpr[1] + 0x28);
# que o lifter actual emite como
#   ctx->gpr[2] = 0x00541178ULL; /*TOCFIX ld r2,N(r1)*/
# (resolucao estatica do TOC). Essa linha nao ancorava nada nem era
# alterada pelo patch -- saiu da agulha, e o patch passa a funcionar
# com as duas formas de lift.
old = """        ctx->gpr[9] = vm_read32(ctx->gpr[11] + 0x8);
        ctx->gpr[0] = vm_read32(ctx->gpr[9] + 0x0);
        vm_write64(ctx->gpr[1] + 0x28, ctx->gpr[2]);
        ctx->gpr[2] = vm_read32(ctx->gpr[9] + 0x4);
        ctx->ctr = (uint32_t)ctx->gpr[0];
        ps3_indirect_call(ctx); DRAIN_TRAMPOLINE(ctx);"""

new = """        ctx->gpr[9] = vm_read32(ctx->gpr[11] + 0x8);
        { static int on=-1; if(on<0){extern char* getenv(const char*); on=getenv("PS3_TRACE_SHADERSRC")||getenv("PS3_TRACE_TYMAP")?1:0;}
          if(on){ static int n=0; if(n++<24)
            fprintf(stderr,"[STREAM-OPD] #%d opd=0x%08X code=0x%08X\\n",
              n,(uint32_t)ctx->gpr[9], ctx->gpr[9]?vm_read32(ctx->gpr[9]+0x0):0); fflush(stderr);} }
        vm_write64(ctx->gpr[1] + 0x28, ctx->gpr[2]);
        ps3_call_opd(ctx, (uint32_t)ctx->gpr[9]); DRAIN_TRAMPOLINE(ctx);"""

if "STREAM-OPD" in region:
    print("1856A8 already patched")
elif old not in region:
    # try alternate spacing from actual file
    old2 = """        ctx->gpr[9] = vm_read32(ctx->gpr[11] + 0x8);
        ctx->gpr[0] = vm_read32(ctx->gpr[9] + 0x0);
        vm_write64(ctx->gpr[1] + 0x28, ctx->gpr[2]);
        ctx->ctr = (uint32_t)ctx->gpr[0];
        ctx->gpr[2] = vm_read32(ctx->gpr[9] + 0x4);
        ps3_indirect_call(ctx); DRAIN_TRAMPOLINE(ctx);"""
    new2 = """        ctx->gpr[9] = vm_read32(ctx->gpr[11] + 0x8);
        { static int on=-1; if(on<0){extern char* getenv(const char*); on=(getenv("PS3_TRACE_SHADERSRC")||getenv("PS3_TRACE_TYMAP"))?1:0;}
          if(on){ static int n=0; if(n++<24)
            fprintf(stderr,"[STREAM-OPD] #%d opd=0x%08X code=0x%08X\\n",
              n,(uint32_t)ctx->gpr[9], ctx->gpr[9]?vm_read32(ctx->gpr[9]+0x0):0); fflush(stderr);} }
        vm_write64(ctx->gpr[1] + 0x28, ctx->gpr[2]);
        ps3_call_opd(ctx, (uint32_t)ctx->gpr[9]); DRAIN_TRAMPOLINE(ctx);"""
    if old2 not in region:
        # show nearby for debug
        k = region.find("ps3_indirect_call")
        print("pattern miss; context:")
        print(repr(region[max(0,k-300):k+80]))
        raise SystemExit("1856A8 OPD pattern missing")
    region = region.replace(old2, new2, 1)
    print("patched 1856A8 (alt order)")
else:
    region = region.replace(old, new, 1)
    print("patched 1856A8")

s = s[:i] + region + s[j:]
p.write_text(s, encoding="utf-8", newline="\n")
print("OK patch_1856a8_stream_opd")
