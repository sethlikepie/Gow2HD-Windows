#!/usr/bin/env python3
"""
Fix all OPD indirect calls in func_0014B1F0 (asset/component batch dispatcher).

Evidence:
- Loads TOC-0x3D9C -> OPD 0x5227F0 -> func_00162150 (SHADERSRC type loader)
- 12x ps3_indirect_call on vtable OPDs -- same broken class as factory/GroupEnd
- SHADERSRC already runs somehow, but nested OPDs in this dispatcher may skip
  the path that would reach type-map / ICGLdr (0xF85F9B1E via 171244).

--- shapes do restauro do TOC -------------------------------------------------
O lifter mudou o restauro de r2 depois de uma chamada. Os DOIS shapes existem
em disco e ambos tem de casar (D-3.7, Fase 3):

  ANTIGO (recomp_macos_v2.pre_v4):
      ctx->gpr[2] = vm_read64(ctx->gpr[1] + 0x28);
  NOVO   (recomp_macos_v2, lifter >= 23/07, confirmado ser o unico formato
          presente hoje nos 12 sitios reais de func_0014B1F0):
      ctx->gpr[2] = 0x00541178ULL; /*TOCFIX ld r2,N(r1)*/

A linha casada e' re-emitida VERBATIM: nao convertemos um shape no outro, so'
trocamos o ps3_indirect_call por ps3_call_opd. Assim o output contra o lift
antigo fica byte-identico ao que este patch sempre produziu.

--- codigos de saida ----------------------------------------------------------
  0  CONVERTIDO   -> converteu >= 1 sitio nesta corrida
  0  JA-APLICADO  -> 0 conversoes mas o corpo ja tem ps3_call_opd (idempotencia)
  3  SEM-EFEITO   -> 0 conversoes e 0 ja aplicadas: as agulhas deixaram de casar
                     (era este o caso que dizia "replaced pat1=0 pat2=0" + "OK"
                      e rc=0 e passava no gate como ALREADY-APPLIED -- o bug
                      medido em 03-CONTEXT.md para este script especifico)
"""
from pathlib import Path
import re
import sys

EXPECTED_SITES = 12     # 12 vcalls no corpo de func_0014B1F0 (medido nesta sessao)
RC_NO_EFFECT = 3         # patch correu e nao produziu efeito nenhum -> ruidoso

ROOT = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent
p = ROOT / "ppu_recomp_000.cpp"
s = p.read_text(encoding="utf-8", errors="replace")


def ensure_decl(s: str) -> str:
    if 'void ps3_call_opd(ppu_context* ctx, uint32_t opd_ea);' in s[:80000]:
        return s
    old = 'extern "C" void ps3_indirect_call(ppu_context* ctx);'
    if old not in s[:80000]:
        raise SystemExit("no ps3_indirect_call decl in 000")
    return s.replace(old, old + '\nextern "C" void ps3_call_opd(ppu_context* ctx, uint32_t opd_ea);', 1)


s = ensure_decl(s)

i = s.find("void func_0014B1F0")
if i < 0:
    raise SystemExit("func_0014B1F0 missing")
j = s.find("void func_0014BEAC", i)  # next known func
if j < 0:
    j = s.find("void func_", i + 20)
region = s[i:j]

# Restauro do TOC apos a chamada: aceita os dois shapes do lifter.
# Sem re.S em lado nenhum; [^\n]*? trava a alternativa TOCFIX na propria linha.
TOC_RESTORE = (
    r"(?P<toc>        ctx->gpr\[2\] = "
    r"(?:vm_read64\(ctx->gpr\[1\] \+ 0x28\);"
    r"|0x[0-9A-Fa-f]+ULL; /\*TOCFIX[^\n]*?\*/)"
    r")"
)

# Generic OPD block: load opd into gpr[N], code into gpr[0], set ctr/toc, indirect
pat = re.compile(
    r"(        ctx->gpr\[(\d+)\] = vm_read32\(ctx->gpr\[\d+\] \+ 0x[0-9A-Fa-f]+\);\n)"
    r"        ctx->gpr\[0\] = vm_read32\(ctx->gpr\[\2\] \+ 0x0\);\n"
    r"        vm_write64\(ctx->gpr\[1\] \+ 0x28, ctx->gpr\[2\]\);\n"
    r"        ctx->ctr = \(uint32_t\)ctx->gpr\[0\];\n"
    r"        ctx->gpr\[2\] = vm_read32\(ctx->gpr\[\2\] \+ 0x4\);\n"
    r"        ps3_indirect_call\(ctx\); DRAIN_TRAMPOLINE\(ctx\);\n"
    + TOC_RESTORE
)

# Alternate order: ctr after toc
pat2 = re.compile(
    r"(        ctx->gpr\[(\d+)\] = vm_read32\(ctx->gpr\[\d+\] \+ 0x[0-9A-Fa-f]+\);\n)"
    r"        ctx->gpr\[0\] = vm_read32\(ctx->gpr\[\2\] \+ 0x0\);\n"
    r"        vm_write64\(ctx->gpr\[1\] \+ 0x28, ctx->gpr\[2\]\);\n"
    r"        ctx->gpr\[2\] = vm_read32\(ctx->gpr\[\2\] \+ 0x4\);\n"
    r"        ctx->ctr = \(uint32_t\)ctx->gpr\[0\];\n"
    r"        ps3_indirect_call\(ctx\); DRAIN_TRAMPOLINE\(ctx\);\n"
    + TOC_RESTORE
)

n_already = len(re.findall(r"ps3_call_opd\(ctx,", region))

n = 0

def repl(m):
    global n
    n += 1
    reg = m.group(2)
    return (
        m.group(1)
        + f"        vm_write64(ctx->gpr[1] + 0x28, ctx->gpr[2]);\n"
        + f"        ps3_call_opd(ctx, (uint32_t)ctx->gpr[{reg}]); DRAIN_TRAMPOLINE(ctx);\n"
        + m.group("toc")   # verbatim: vm_read64(...) OU 0x...ULL; /*TOCFIX ...*/
    )

region, c1 = pat.subn(repl, region)
region, c2 = pat2.subn(repl, region)
print(f"replaced pat1={c1} pat2={c2} total_opd_sites={n}")
print(f"remaining indirect in region: {region.count('ps3_indirect_call')}")

# Add entry probe once
if "WADLD-BATCH" not in region:
    needle = "void func_0014B1F0(ppu_context* ctx) {\n"
    probe = (
        "void func_0014B1F0(ppu_context* ctx) {\n"
        "        { static int on=-1; if(on<0){extern char* getenv(const char*); "
        "on=(getenv(\"PS3_TRACE_TYMAP\")||getenv(\"PS3_TRACE_LDRSH\"))?1:0;}\n"
        "          if(on){ static int n=0; if(n++<8)\n"
        "            fprintf(stderr,\"[WADLD-BATCH] #%d enter r3=0x%08X r4=0x%08X\\n\",\n"
        "              n,(uint32_t)ctx->gpr[3],(uint32_t)ctx->gpr[4]); fflush(stderr);} }\n"
    )
    if needle in region:
        region = region.replace(needle, probe, 1)
        print("added BATCH entry probe")

s = s[:i] + region + s[j:]
p.write_text(s, encoding="utf-8", newline="\n")

# --- veredicto: um patch que corre e nao converte nada NAO pode sair 0 --------
if n > 0:
    if n != EXPECTED_SITES:
        print(f"WARN patch_14b1f0_opd: esperava {EXPECTED_SITES} sitios convertidos, "
              f"tenho {n}")
    print("OK patch_14b1f0_opd (CONVERTIDO)")
    raise SystemExit(0)

if n_already > 0:
    print(f"OK patch_14b1f0_opd (JA-APLICADO: {n_already} sitios ja em ps3_call_opd)")
    raise SystemExit(0)

print("FALHA patch_14b1f0_opd (SEM-EFEITO): 0 conversoes e 0 sitios ja convertidos.")
print("  As agulhas nao casam o shape actual do lift.")
raise SystemExit(RC_NO_EFFECT)
