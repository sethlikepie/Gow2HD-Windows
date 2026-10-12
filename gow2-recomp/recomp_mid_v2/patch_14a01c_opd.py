#!/usr/bin/env python3
"""
Fix OPD sites in constructors that install vtable with 171244 at +0x8.

--- shapes do restauro do TOC (D-4.7, 2026-07-26) -----------------------------
O lifter mudou o restauro de r2 depois de uma chamada. Os DOIS shapes existem
em disco e ambos tem de casar:

  ANTIGO (recomp_macos_v2.pre_v4):
      ctx->gpr[2] = vm_read64(ctx->gpr[1] + 0x28);
  NOVO   (recomp_macos_v2/v3, lifter >= 23/07):
      ctx->gpr[2] = 0x00541178ULL; /*TOCFIX ld r2,N(r1)*/

A linha casada e' re-emitida VERBATIM: nao convertemos um shape no outro, so'
trocamos o ps3_indirect_call por ps3_call_opd. Assim o output contra o lift
antigo fica byte-identico ao que este patch sempre produziu.

Era o UNICO dos 87 patch_*.py sem nenhum caminho de saida diferente de zero
(so' `p.write_text(...); print("OK total", total)`, sem SystemExit): corria,
nao convertia nada contra o lift novo (agulha so' casava a forma antiga), e
saia rc=0 na mesma -- o apply_all_patches.sh classificava isso como
ALREADY-APPLIED. Mesmo defeito e mesma causa raiz ja corrigidos em seis
scripts irmaos (patch_32e200_opd.py e outros cinco).

--- codigos de saida ----------------------------------------------------------
  0  CONVERTIDO   -> converteu >= 1 sitio nesta corrida
  0  JA-APLICADO  -> 0 conversoes mas o corpo ja tem ps3_call_opd (idempotencia)
  3  SEM-EFEITO   -> 0 conversoes e 0 ja aplicadas: as agulhas deixaram de casar
                     (era este o caso que dizia "OK total 0" e rc=0 e passava
                      no gate como ALREADY-APPLIED)
"""
from pathlib import Path
import re
import sys

EXPECTED_SITES = 4      # 2 sitios (gpr[10]/gpr[11]) x 2 funcoes -- medido nesta sessao
RC_NO_EFFECT = 3         # patch correu e nao produziu efeito nenhum -> ruidoso

ROOT = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent
p = ROOT / "ppu_recomp_000.cpp"
s = p.read_text(encoding="utf-8", errors="replace")

# Restauro do TOC apos a chamada: aceita os dois shapes do lifter. Sem re.S
# em lado nenhum; [^\n]*? trava a alternativa TOCFIX na propria linha.
pat = re.compile(
    r"(        ctx->gpr\[(\d+)\] = vm_read32\(ctx->gpr\[\d+\] \+ 0x[0-9A-Fa-f]+\);\n)"
    r"        ctx->gpr\[0\] = vm_read32\(ctx->gpr\[\2\] \+ 0x0\);\n"
    r"        vm_write64\(ctx->gpr\[1\] \+ 0x28, ctx->gpr\[2\]\);\n"
    r"        ctx->ctr = \(uint32_t\)ctx->gpr\[0\];\n"
    r"        ctx->gpr\[2\] = vm_read32\(ctx->gpr\[\2\] \+ 0x4\);\n"
    r"        ps3_indirect_call\(ctx\); DRAIN_TRAMPOLINE\(ctx\);\n"
    r"(?P<toc>        ctx->gpr\[2\] = "
    r"(?:vm_read64\(ctx->gpr\[1\] \+ 0x28\);"
    r"|0x[0-9A-Fa-f]+ULL; /\*TOCFIX[^\n]*?\*/)"
    r")"
)

total = 0
total_already = 0
for name in ["func_0014A01C", "func_0014AD94"]:
    i = s.find(f"void {name}")
    if i < 0:
        print(name, "missing")
        continue
    j = s.find("void func_", i + 20)
    region = s[i:j]
    # Sitios ja convertidos antes desta corrida (medido ANTES da substituicao).
    n_already = len(re.findall(r"ps3_call_opd\(ctx,", region))
    state = {"n": 0}

    def repl(m):
        state["n"] += 1
        reg = m.group(2)
        return (
            m.group(1)
            + "        vm_write64(ctx->gpr[1] + 0x28, ctx->gpr[2]);\n"
            + f"        ps3_call_opd(ctx, (uint32_t)ctx->gpr[{reg}]); DRAIN_TRAMPOLINE(ctx);\n"
            + m.group("toc")   # verbatim: vm_read64(...) OU 0x...ULL; /*TOCFIX ...*/
        )

    region2, c = pat.subn(repl, region)
    print(f"{name}: fixed {state['n']} already={n_already} "
          f"remain_indirect={region2.count('ps3_indirect_call')}")
    total += state["n"]
    total_already += n_already
    s = s[:i] + region2 + s[j:]

p.write_text(s, encoding="utf-8", newline="\n")

# --- veredicto: um patch que corre e nao converte nada NAO pode sair 0 --------
if total > 0:
    if total != EXPECTED_SITES:
        print(f"WARN patch_14a01c_opd: esperava {EXPECTED_SITES} sitios convertidos, "
              f"tenho {total}")
    print("OK patch_14a01c_opd (CONVERTIDO)")
    raise SystemExit(0)

if total_already > 0:
    print(f"OK patch_14a01c_opd (JA-APLICADO: {total_already} sitios ja em ps3_call_opd)")
    raise SystemExit(0)

print("FALHA patch_14a01c_opd (SEM-EFEITO): 0 conversoes e 0 sitios ja convertidos.")
print("  As agulhas nao casam o shape actual do lift.")
raise SystemExit(RC_NO_EFFECT)
