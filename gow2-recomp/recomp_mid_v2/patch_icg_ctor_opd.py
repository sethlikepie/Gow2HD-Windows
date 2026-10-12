#!/usr/bin/env python3
"""
Fix remaining OPD vcall sites in ICG component ctor/init that still use
ps3_indirect_call (code-from-OPD manual sequence). These run once at boot
(ICG-CTOR/ICG-INIT fire) but may return 0 / skip registration if OPD resolve
or TOC is wrong — blocking later type-map dispatch (171244 → ICGLdr).

Also fix residual indirect in 329490 (ICG-PATH-A) which is on the only
static call chain that reaches 32E200 vt+0x8 → OPD 0x522E70 → 171244.

--- 2026-07-26: DOIS SHAPES DE RESTAURO DO TOC -------------------------------
O lifter mudou como emite o `ld r2,0x28(r1)` que fecha um call site:

  antigo (lift de 20 jul, recomp_macos_v2.pre_v4):
      ctx->gpr[2] = vm_read64(ctx->gpr[1] + 0x28);
  novo (lift regenerado, em producao):
      ctx->gpr[2] = 0x00541178ULL; /*TOCFIX ld r2,N(r1)*/

As agulhas terminavam TODAS na forma antiga, logo deixaram de casar no lift
novo: 6 sitios ICG passaram de ps3_call_opd a ficar em ps3_indirect_call, sem
o patch se queixar ("fixed 0" + "OK" + rc=0 -> o apply_all_patches classificava
ALREADY-APPLIED e o gate ficava verde).

Agora TOC_RESTORE aceita as duas formas e o replacement REEMITE VERBATIM a
linha que estava la (grupo nomeado 'toc'). Preservar em vez de converter e
deliberado: contra o lift antigo a saida fica byte-identica a de antes (nao
regride nada), e contra o novo mexemos so' no que e' o nosso mandato — a
conversao ps3_indirect_call -> ps3_call_opd. Se a constante TOCFIX estiver
errada, isso e' um bug do lifter a tratar no lifter, nao aqui.
"""
from pathlib import Path
import re
import sys

ROOT = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent

# Restauro do TOC apos a chamada — as duas formas que o lifter ja emitiu.
# Grupo NOMEADO e' o ultimo de cada regex, por isso os grupos 1/2/3 posicionais
# das agulhas abaixo mantem a numeracao. Sem re.S: `[^\n]*` nao passa da linha.
TOC_RESTORE = (
    r"(?P<toc>ctx->gpr\[2\] = vm_read64\(ctx->gpr\[1\] \+ 0x28\);"
    r"|ctx->gpr\[2\] = 0x[0-9A-Fa-f]+ULL; /\*TOCFIX[^\n]*)"
)

# Generic OPD block: gpr[OPD] holds OPD EA; code loaded to gpr[0]/ctr; toc to r2
OPD_BLOCK = re.compile(
    r"(        ctx->gpr\[(\d+)\] = vm_read32\(ctx->gpr\[\d+\] \+ 0x[0-9A-Fa-f]+\);\n)"
    r"        ctx->gpr\[(\d+)\] = vm_read32\(ctx->gpr\[\2\] \+ 0x0\);\n"
    r"        vm_write64\(ctx->gpr\[1\] \+ 0x28, ctx->gpr\[2\]\);\n"
    r"        ctx->ctr = \(uint32_t\)ctx->gpr\[\3\];\n"
    r"        ctx->gpr\[2\] = vm_read32\(ctx->gpr\[\2\] \+ 0x4\);\n"
    r"        ps3_indirect_call\(ctx\); DRAIN_TRAMPOLINE\(ctx\);\n"
    r"        " + TOC_RESTORE
)

OPD_BLOCK2 = re.compile(
    r"(        ctx->gpr\[(\d+)\] = vm_read32\(ctx->gpr\[\d+\] \+ 0x[0-9A-Fa-f]+\);\n)"
    r"        ctx->gpr\[(\d+)\] = vm_read32\(ctx->gpr\[\2\] \+ 0x0\);\n"
    r"        vm_write64\(ctx->gpr\[1\] \+ 0x28, ctx->gpr\[2\]\);\n"
    r"        ctx->gpr\[2\] = vm_read32\(ctx->gpr\[\2\] \+ 0x4\);\n"
    r"        ctx->ctr = \(uint32_t\)ctx->gpr\[\3\];\n"
    r"        ps3_indirect_call\(ctx\); DRAIN_TRAMPOLINE\(ctx\);\n"
    r"        " + TOC_RESTORE
)

# 329490 residual: OPD in gpr[11], code loaded to gpr[0]
OPD_BLOCK3 = re.compile(
    r"        ctx->gpr\[0\] = vm_read32\(ctx->gpr\[11\] \+ 0x0\);\n"
    r"        ctx->gpr\[4\] = ctx->gpr\[3\] \| ctx->gpr\[3\];\n"
    r"        vm_write64\(ctx->gpr\[1\] \+ 0x28, ctx->gpr\[2\]\);\n"
    r"        ctx->ctr = \(uint32_t\)ctx->gpr\[0\];\n"
    r"        ctx->gpr\[2\] = vm_read32\(ctx->gpr\[11\] \+ 0x4\);\n"
    r"        ps3_indirect_call\(ctx\); DRAIN_TRAMPOLINE\(ctx\);\n"
    r"        " + TOC_RESTORE
)

TAG_A = "ICG-PATH-A-OPD"


def fix_region(region: str, tag: str) -> tuple[str, int]:
    n = 0

    def repl(m):
        nonlocal n
        n += 1
        opd_reg = m.group(2)
        probe = (
            f"        {{ static int on=-1; if(on<0){{extern char* getenv(const char*); "
            f"on=(getenv(\"PS3_TRACE_TYMAP\")||getenv(\"PS3_TRACE_LDRSH\"))?1:0;}}\n"
            f"          if(on){{ static int k=0; if(k++<48)\n"
            f"            fprintf(stderr,\"[{tag}] #%d opd=0x%08X code=0x%08X\\n\",\n"
            f"              k,(uint32_t)ctx->gpr[{opd_reg}], "
            f"ctx->gpr[{opd_reg}]?vm_read32(ctx->gpr[{opd_reg}]+0x0):0); fflush(stderr);}} }}\n"
        )
        return (
            m.group(1)
            + probe
            + "        vm_write64(ctx->gpr[1] + 0x28, ctx->gpr[2]);\n"
            + f"        ps3_call_opd(ctx, (uint32_t)ctx->gpr[{opd_reg}]); DRAIN_TRAMPOLINE(ctx);\n"
            # reemite VERBATIM o restauro do TOC que estava no lift (dinamico ou TOCFIX)
            + "        " + m.group("toc")
        )

    region, c1 = OPD_BLOCK.subn(repl, region)
    region, c2 = OPD_BLOCK2.subn(repl, region)
    return region, n


def ensure_decl(s: str) -> str:
    if 'void ps3_call_opd(ppu_context* ctx, uint32_t opd_ea);' in s[:40000]:
        return s
    old = 'extern "C" void ps3_indirect_call(ppu_context* ctx);'
    if old not in s[:40000]:
        raise SystemExit("no ps3_indirect_call decl")
    return s.replace(old, old + '\nextern "C" void ps3_call_opd(ppu_context* ctx, uint32_t opd_ea);', 1)


def patch_funcs(path: Path, names: list[str], tag: str) -> tuple[int, int, list[str]]:
    """Devolve (conversoes_agora, sitios_ja_convertidos, residuos)."""
    s = path.read_text(encoding="utf-8", errors="replace")
    s = ensure_decl(s)
    total = 0
    total_already = 0
    residual = []
    for name in names:
        i = s.find(f"void {name}")
        if i < 0:
            print(f"  {name}: MISSING")
            continue
        j = s.find("\nvoid func_", i + 20)
        if j < 0:
            j = len(s)
        region = s[i:j]
        before = region.count("ps3_indirect_call")
        # Sitios que ESTE patch ja converteu numa corrida anterior: as probes com
        # a nossa tag sao marcador exclusivo deste script.
        already = region.count(f"[{tag}]")
        if name == "func_00329490":
            already += region.count(f"[{TAG_A}]")
        region2, n = fix_region(region, tag)
        # 329490 residual pattern
        if name == "func_00329490" and OPD_BLOCK3.search(region2):
            # NB: replacement tem de ser FUNCAO (como o `repl` acima), nao string:
            # re.sub interpreta escapes no template, e o "\\n" do fprintf virava
            # newline REAL dentro do literal C -> "error: expected expression".
            region2 = OPD_BLOCK3.sub(
                lambda _m: (
                "        ctx->gpr[4] = ctx->gpr[3] | ctx->gpr[3];\n"
                "        { static int on=-1; if(on<0){extern char* getenv(const char*); "
                "on=(getenv(\"PS3_TRACE_TYMAP\")||getenv(\"PS3_TRACE_LDRSH\"))?1:0;}\n"
                "          if(on){ static int k=0; if(k++<24)\n"
                f"            fprintf(stderr,\"[{TAG_A}] #%d opd=0x%08X code=0x%08X\\n\",\n"
                "              k,(uint32_t)ctx->gpr[11], "
                "ctx->gpr[11]?vm_read32(ctx->gpr[11]+0x0):0); fflush(stderr);} }\n"
                "        vm_write64(ctx->gpr[1] + 0x28, ctx->gpr[2]);\n"
                "        ps3_call_opd(ctx, (uint32_t)ctx->gpr[11]); DRAIN_TRAMPOLINE(ctx);\n"
                # reemite VERBATIM o restauro do TOC que estava no lift
                "        " + _m.group("toc")
                ),
                region2,
                count=1,
            )
            n += 1
        # Auto-verificacao: depois de converter, NENHUMA agulha pode voltar a casar.
        # Se casar, o replacement nao produziu o efeito esperado -> defeito, nao no-op.
        leftover = 0
        for rx in (OPD_BLOCK, OPD_BLOCK2):
            leftover += len(rx.findall(region2))
        if name == "func_00329490" and OPD_BLOCK3.search(region2):
            leftover += 1
        if leftover:
            residual.append(f"{name}({leftover} agulhas por converter apos a corrida)")
        after = region2.count("ps3_indirect_call")
        state = "CONVERTIDO" if n else ("JA-APLICADO" if already else "SEM-EFEITO")
        print(f"  {name}: {state} fixed~{n} already={already} "
              f"indirect {before}->{after} call_opd={region2.count('ps3_call_opd')}")
        total += n
        total_already += already
        s = s[:i] + region2 + s[j:]
    # newline="\n" e' obrigatorio (evita CRLF no lado Windows do port) e exige
    # Python >= 3.10; o apply_all_patches.sh escolhe um interprete compativel.
    path.write_text(s, encoding="utf-8", newline="\n")
    print(f"OK {path.name} total_fixes~{total} total_already~{total_already}")
    return total, total_already, residual


def main():
    p0 = ROOT / "ppu_recomp_000.cpp"
    p1 = ROOT / "ppu_recomp_001.cpp"
    print("=== 000 ctor/init ===")
    # D-4.7 (Fase 4, 2026-07-26): func_0014A01C/func_0014AD94 saem desta lista.
    # patch_14a01c_opd.py (corrigido nesta sessao, corre PRIMEIRO alfabeticamente
    # no glob patch_*.py) volta a ser o dono unico dessas 2 funcoes -- antes desta
    # correcao, as duas reivindicavam as mesmas 4 conversoes OPD, e assim que
    # patch_14a01c_opd.py voltasse a converter, este script imprimiria SEM-EFEITO
    # enganoso para elas (a probe [ICG-VCALL] deixaria de aparecer, convertida
    # por outro script). Posse unica restaurada: aqui fica so' func_00151248.
    n0, a0, r0 = patch_funcs(p0, ["func_00151248"], "ICG-VCALL")
    print("=== 001 path ===")
    n1, a1, r1 = patch_funcs(p1, ["func_00329490", "func_0032854C", "func_00328B18"], "ICG-PATH-OPD")

    conv = n0 + n1
    already = a0 + a1
    residual = r0 + r1

    # --- contrato de ruido ----------------------------------------------------
    # Um patch que corre e nao produz efeito NENHUM tem de ser barulhento. Os dois
    # casos legitimos de 0 conversoes sao distinguidos pelo marcador exclusivo
    # deste script (as probes [ICG-*]) ja presente no lift:
    #   conv>0                -> APLICADO AGORA        rc=0
    #   conv==0 e already>0   -> JA APLICADO (no-op)   rc=0
    #   conv==0 e already==0  -> AGULHAS NAO CASARAM   rc=1
    if residual:
        print("ERRO: agulhas continuam a casar depois da substituicao: "
              + ", ".join(residual), file=sys.stderr)
        raise SystemExit(1)
    if conv == 0 and already == 0:
        print("ERRO: 0 conversoes e 0 sitios ja convertidos — as agulhas nao casaram "
              "com este lift (shape mudou?). Nao e' um no-op legitimo.", file=sys.stderr)
        raise SystemExit(1)
    if conv == 0:
        print(f"JA-APLICADO: 0 conversoes novas, {already} sitios ja convertidos (no-op)")
    else:
        print(f"APLICADO: {conv} conversoes novas, {already} ja convertidos")


if __name__ == "__main__":
    main()
