#!/usr/bin/env python3
"""
Fix OPD calls in func_0032E200 — candidate path that vcalls method +0x8.

If this-object vtable is 0x5130B8, method +0x8 is OPD 0x522E70 -> func_00171244
(the only stream dispatcher that can reach ICGLdr via type-map lookup).

--- shapes do restauro do TOC -------------------------------------------------
O lifter mudou o restauro de r2 depois de uma chamada. Os DOIS shapes existem
em disco e ambos tem de casar:

  ANTIGO (recomp_macos_v2.pre_v4):
      ctx->gpr[2] = vm_read64(ctx->gpr[1] + 0x28);
  NOVO   (recomp_macos_v2, lifter >= 23/07):
      ctx->gpr[2] = 0x00541178ULL; /*TOCFIX ld r2,N(r1)*/

A linha casada e' re-emitida VERBATIM: nao convertemos um shape no outro, so'
trocamos o ps3_indirect_call por ps3_call_opd. Assim o output contra o lift
antigo fica byte-identico ao que este patch sempre produziu.

--- codigos de saida ----------------------------------------------------------
  0  CONVERTIDO   -> converteu >= 1 sitio nesta corrida
  0  JA-APLICADO  -> 0 conversoes mas o corpo ja tem ps3_call_opd (idempotencia)
  3  SEM-EFEITO   -> 0 conversoes e 0 ja aplicadas: as agulhas deixaram de casar
                     (era este o caso que dizia "fixed 0" + "OK" + rc=0 e passava
                      no gate como ALREADY-APPLIED)
"""
from pathlib import Path
import re
import sys

EXPECTED_SITES = 7      # 7 vcalls (+0x4 / +0x8 / +0xC) no corpo de func_0032E200
RC_NO_EFFECT = 3        # patch correu e nao produziu efeito nenhum -> ruidoso

ROOT = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent
p = ROOT / "ppu_recomp_003.cpp"
s = p.read_text(encoding="utf-8", errors="replace")

if 'void ps3_call_opd(ppu_context* ctx, uint32_t opd_ea);' not in s[:30000]:
    old = 'extern "C" void ps3_indirect_call(ppu_context* ctx);'
    if old not in s[:30000]:
        raise SystemExit("no ps3_indirect_call decl in 003")
    s = s.replace(old, old + '\nextern "C" void ps3_call_opd(ppu_context* ctx, uint32_t opd_ea);', 1)
    print("003: declared ps3_call_opd")

# --- delimitacao EXPLICITA da regiao: [inicio de func_0032E200, inicio da seguinte)
i = s.find("void func_0032E200")
if i < 0:
    raise SystemExit("32E200 missing")
j = s.find("void func_", i + 20)
if j < 0:
    raise SystemExit("32E200: nao encontrei a funcao seguinte; regiao indelimitavel")
region = s[i:j]

# Restauro do TOC apos a chamada: aceita os dois shapes do lifter.
# Sem re.S em lado nenhum; [^\n]*? trava a alternativa TOCFIX na propria linha.
TOC_RESTORE = (
    r"(?P<toc>        ctx->gpr\[2\] = "
    r"(?:vm_read64\(ctx->gpr\[1\] \+ 0x28\);"
    r"|0x[0-9A-Fa-f]+ULL; /\*TOCFIX[^\n]*?\*/)"
    r")"
)


def make_pat(ctr_first: bool) -> re.Pattern:
    """Sequencia do vcall PPC: lwz opd; lwz ctr,0(opd); std r2,0x28(r1); mtctr;
    lwz r2,4(opd); bctrl; <restauro do TOC>. A ordem mtctr/lwz r2 varia."""
    ctr = r"        ctx->ctr = \(uint32_t\)ctx->gpr\[0\];\n"
    r2 = r"        ctx->gpr\[2\] = vm_read32\(ctx->gpr\[(?P=reg)\] \+ 0x4\);\n"
    parts = [
        r"(?P<pre>        ctx->gpr\[(?P<reg>\d+)\] = vm_read32\(ctx->gpr\[\d+\] \+ 0x[0-9A-Fa-f]+\);\n)",
        r"        ctx->gpr\[0\] = vm_read32\(ctx->gpr\[(?P=reg)\] \+ 0x0\);\n",
        r"        vm_write64\(ctx->gpr\[1\] \+ 0x28, ctx->gpr\[2\]\);\n",
    ]
    parts += [ctr, r2] if ctr_first else [r2, ctr]
    parts.append(r"        ps3_indirect_call\(ctx\); DRAIN_TRAMPOLINE\(ctx\);\n")
    parts.append(TOC_RESTORE)
    return re.compile("".join(parts))


pat = make_pat(ctr_first=True)
pat2 = make_pat(ctr_first=False)

n_already = len(re.findall(r"ps3_call_opd\(ctx,", region))
n_indirect_before = region.count("ps3_indirect_call")

n = 0


def repl(m):
    global n
    n += 1
    reg = m.group("reg")
    probe = ""
    if n <= 16:
        probe = (
            f"        {{ static int on=-1; if(on<0){{extern char* getenv(const char*); "
            f"on=(getenv(\"PS3_TRACE_TYMAP\")||getenv(\"PS3_TRACE_LDRSH\"))?1:0;}}\n"
            f"          if(on){{ static int k=0; if(k++<48)\n"
            f"            fprintf(stderr,\"[CMP-VCALL] #%d opd=0x%08X code=0x%08X\\n\",\n"
            f"              k,(uint32_t)ctx->gpr[{reg}], "
            f"ctx->gpr[{reg}]?vm_read32(ctx->gpr[{reg}]+0x0):0); fflush(stderr);}} }}\n"
        )
    return (
        m.group("pre")
        + probe
        + f"        vm_write64(ctx->gpr[1] + 0x28, ctx->gpr[2]);\n"
        + f"        ps3_call_opd(ctx, (uint32_t)ctx->gpr[{reg}]); DRAIN_TRAMPOLINE(ctx);\n"
        + m.group("toc")   # verbatim: vm_read64(...) OU 0x...ULL; /*TOCFIX ...*/
    )


region, c1 = pat.subn(repl, region)
region, c2 = pat2.subn(repl, region)
n_indirect_after = region.count("ps3_indirect_call")
n_opd_after = len(re.findall(r"ps3_call_opd\(ctx,", region))

print(f"32E200: fixed {n} OPD sites (pat1={c1} pat2={c2}), "
      f"ja_aplicados={n_already}, call_opd_total={n_opd_after}, "
      f"remaining indirect={n_indirect_after} (antes={n_indirect_before})")

if "CMP-ENTER" not in region:
    needle = "void func_0032E200(ppu_context* ctx) {\n"
    probe = (
        "void func_0032E200(ppu_context* ctx) {\n"
        "        { static int on=-1; if(on<0){extern char* getenv(const char*); "
        "on=(getenv(\"PS3_TRACE_TYMAP\")||getenv(\"PS3_TRACE_LDRSH\"))?1:0;}\n"
        "          if(on){ static int n=0; if(n++<16)\n"
        "            fprintf(stderr,\"[CMP-ENTER] #%d r3=0x%08X r4=0x%08X\\n\",\n"
        "              n,(uint32_t)ctx->gpr[3],(uint32_t)ctx->gpr[4]); fflush(stderr);} }\n"
    )
    if needle in region:
        region = region.replace(needle, probe, 1)
        print("added CMP-ENTER probe")

s = s[:i] + region + s[j:]
with open(p, "w", encoding="utf-8", newline="\n") as fh:   # 'newline' em open: ok em py3.6+
    fh.write(s)

# --- veredicto: um patch que corre e nao converte nada NAO pode sair 0 ---------
if n > 0:
    if n_opd_after != EXPECTED_SITES:
        print(f"WARN patch_32e200_opd: esperava {EXPECTED_SITES} sitios convertidos, "
              f"tenho {n_opd_after} (indirect restantes={n_indirect_after})")
    print("OK patch_32e200_opd (CONVERTIDO)")
    raise SystemExit(0)

if n_already > 0:
    print(f"OK patch_32e200_opd (JA-APLICADO: {n_already} sitios ja em ps3_call_opd)")
    raise SystemExit(0)

# 0 conversoes e 0 ja aplicadas -> as agulhas deixaram de casar o shape do lift.
print("FALHA patch_32e200_opd (SEM-EFEITO): 0 conversoes e 0 sitios ja convertidos.")
print("  As agulhas nao casam o shape actual do lift. Sitio nao convertido (contexto):")
mm = re.search(r"ps3_indirect_call", region)
if mm:
    lo = region.rfind("\n", 0, max(0, mm.start() - 400)) + 1
    hi = region.find("\n", mm.end() + 200)
    for ln in region[lo:hi if hi > 0 else None].split("\n"):
        print("    | " + ln)
else:
    print("    | (nenhum ps3_indirect_call na regiao — corpo de 32E200 irreconhecivel)")
raise SystemExit(RC_NO_EFFECT)
