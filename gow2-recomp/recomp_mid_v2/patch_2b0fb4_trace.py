#!/usr/bin/env python3
"""Deep-trace func_002B0FB4 (type-1 size!=0 path) for SHGX expand + OPD fix.

Duas coisas neste patch:
  (a) DIAGNOSTICO (gated PS3_TRACE_TYMAP, OFF por default, read-only):
      probes [WADLD-T1SZ] / [WADLD-VT28] / [WADLD-VT28R] / [WADLD-VT48].
  (b) CORRECCAO real: os dois slots de vtable usados aqui (vt+0x28 e vt+0x48)
      sao OPDs, nao codigo. Passam de ps3_indirect_call (que trata o OPD como
      EA de codigo) para ps3_call_opd(ctx, opd_ea), que faz o deref do
      descriptor. ps3_call_opd esta' definido em runtime/ppu/ppu_loader.cpp.

CORRECCAO 2026-07-25 -- porque o script tinha deixado de aplicar
---------------------------------------------------------------
O patch era um unico replace literal do corpo inteiro do inicio de
func_002B0FB4. Quatro razoes para falhar, nenhuma delas "o comportamento
desapareceu" (a funcao existe e e' gerada pelo lifter):

  ORFAO       a agulha `old` incluia uma probe [WADLD-T1SZ] de 1a geracao
              que NENHUM script instala (edicao manual de uma sessao antiga).
              O `new` do proprio patch REESCREVE essa probe por uma mais rica,
              ou seja o estado final desejado nao depende dela. Passa-se a
              INSERIR a probe nova a partir do lift limpo, ancorada em codigo
              gerado pelo lifter. Nada e' enfraquecido: o patch continua a ter
              de encontrar as ancoras reais e falha se nao as encontrar.

  chunk-fixo  abria "ppu_recomp_003.cpp" pelo nome (o lifter passou de 31 para
              7 chunks). Passa a aceitar um DIRECTORIO (resolve_lift_paths) e
              a procurar o chunk que DEFINE func_002B0FB4.

  shape-TOCFIX  o restauro do TOC depois da chamada indirecta passou de
              "ctx->gpr[2] = vm_read64(ctx->gpr[1] + 0x28);" para
              "ctx->gpr[2] = 0x00541178ULL; /*TOCFIX ld r2,N(r1)*/". A linha
              encontrada e' capturada e RE-EMITIDA tal e qual (nao regredimos
              o TOCFIX novo do lifter).

  shape-LR / shape-outro  as chamadas ganharam prefixo "ctx->lr = 0x...;" e um
              "/* nop */;" a seguir, e o cast do rlwinm mudou de (uint32_t)
              para (uint64_t). O patch deixa de tocar nessas linhas: as edicoes
              sao insercoes ancoradas em leituras de memoria estaveis.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

from lift_paths import resolve_lift_paths

DEFAULT_LIFT = str(Path(__file__).resolve().parent.parent / "recomp_macos_v2")

FN = "func_002B0FB4"
DECL_OLD = 'extern "C" void ps3_indirect_call(ppu_context* ctx);'
DECL_NEW = DECL_OLD + '\nextern "C" void ps3_call_opd(ppu_context* ctx, uint32_t opd_ea);'

# Ancoras: leituras de memoria emitidas pelo lifter, unicas dentro da regiao
# (verificado: 1 ocorrencia de cada, 2 blocos OPD no total).
A_T1SZ = "        ctx->gpr[11] = vm_read32((ctx->gpr[28] + ctx->gpr[0]));\n"
A_VT28 = "        ctx->gpr[10] = vm_read32(ctx->gpr[9] + 0x28);\n"
A_VT48 = "        ctx->gpr[10] = vm_read32(ctx->gpr[9] + 0x48);\n"

P_T1SZ = """        { static int on=-1; if(on<0){extern char* getenv(const char*); on=getenv("PS3_TRACE_TYMAP")?1:0;}
          if(on){ static int n=0; if(n++<64){
            uint32_t buf=(uint32_t)ctx->gpr[30], np=(uint32_t)ctx->gpr[26];
            uint32_t idx=(uint32_t)ctx->gpr[0], tab=(uint32_t)ctx->gpr[28], obj=(uint32_t)ctx->gpr[11];
            uint16_t u0=vm_read16(buf+0x0), u2=vm_read16(buf+0x2);
            uint32_t w0=vm_read32(buf+0x0), w1=vm_read32(buf+0x4), w2=vm_read32(buf+0x8);
            char nm[24]; int i; for(i=0;i<20&&np;i++){ uint8_t c=vm_read8(np+i); if(!c){nm[i]=0; break;} nm[i]=(c>=32&&c<127)?(char)c:'.'; }
            nm[20]=0;
            fprintf(stderr,"[WADLD-T1SZ] #%d name='%s' u0=%u u2=%u w0=0x%08X w1=0x%08X w2=0x%08X idx=0x%X tab=0x%08X obj=0x%08X\\n",
              n, nm, (unsigned)u0, (unsigned)u2, w0, w1, w2, idx, tab, obj); fflush(stderr);} } }
"""

P_VT28 = """        { static int on=-1; if(on<0){extern char* getenv(const char*); on=getenv("PS3_TRACE_TYMAP")?1:0;}
          if(on){ static int n=0; if(n++<64)
            fprintf(stderr,"[WADLD-VT28] #%d obj=0x%08X vt=0x%08X opd=0x%08X code=0x%08X\\n",
              n,(uint32_t)ctx->gpr[11],(uint32_t)ctx->gpr[9],(uint32_t)ctx->gpr[10],
              ctx->gpr[10]?vm_read32(ctx->gpr[10]+0x0):0); fflush(stderr);} }
"""

P_VT48 = """        { static int on=-1; if(on<0){extern char* getenv(const char*); on=getenv("PS3_TRACE_TYMAP")?1:0;}
          if(on){ static int n=0; if(n++<32)
            fprintf(stderr,"[WADLD-VT48] #%d opd=0x%08X code=0x%08X arg=0x%08X\\n",
              n,(uint32_t)ctx->gpr[10], ctx->gpr[10]?vm_read32(ctx->gpr[10]+0x0):0, (uint32_t)ctx->gpr[31]); fflush(stderr);} }
"""

P_VT28R = """        { static int on=-1; if(on<0){extern char* getenv(const char*); on=getenv("PS3_TRACE_TYMAP")?1:0;}
          if(on){ static int n=0; if(n++<64)
            fprintf(stderr,"[WADLD-VT28R] #%d r3=0x%08X\\n", n, (uint32_t)ctx->gpr[3]); fflush(stderr);} }
"""

# Bloco "chamada indirecta por OPD em r10": deref do descriptor a mao seguido de
# ps3_indirect_call. Tolera as duas formas de restauro do TOC (grupo 1).
OPD_BLOCK = re.compile(
    r"        ctx->gpr\[0\] = vm_read32\(ctx->gpr\[10\] \+ 0x0\);\n"
    r"        vm_write64\(ctx->gpr\[1\] \+ 0x28, ctx->gpr\[2\]\);\n"
    r"        ctx->ctr = \(uint32_t\)ctx->gpr\[0\];\n"
    r"        ctx->gpr\[2\] = vm_read32\(ctx->gpr\[10\] \+ 0x4\);\n"
    r"        ps3_indirect_call\(ctx\); DRAIN_TRAMPOLINE\(ctx\);\n"
    r"(        ctx->gpr\[2\] = (?:vm_read64\(ctx->gpr\[1\] \+ 0x28\);"
    r"|0x[0-9A-Fa-f]+ULL; /\*TOCFIX[^\n]*)\n)"
)

OPD_CALL = """        vm_write64(ctx->gpr[1] + 0x28, ctx->gpr[2]);
        ps3_call_opd(ctx, (uint32_t)ctx->gpr[10]); DRAIN_TRAMPOLINE(ctx);
"""


# NOTA: replacement por CALLABLE. Uma string-template faria o re interpretar o
# "\n" dos literais C (fprintf) como newline real e partia o codigo gerado.
def _opd_repl(m: "re.Match[str]") -> str:
    return OPD_CALL + m.group(1)


def insert_after(region: str, anchor: str, block: str, what: str) -> str:
    i = region.find(anchor)
    if i < 0:
        raise SystemExit(f"ancora de {what} nao encontrada em {FN}")
    j = i + len(anchor)
    return region[:j] + block + region[j:]


def main() -> int:
    paths = [p for p in resolve_lift_paths(sys.argv[1:], DEFAULT_LIFT) if p.is_file()]
    if not paths:
        print("FAILED: nenhum chunk de lift encontrado")
        return 1

    fn_re = re.compile(r"^void " + FN + r"\(ppu_context\* ctx\) \{", re.M)
    target = None
    for p in paths:
        t = p.read_text(encoding="utf-8", errors="replace")
        if fn_re.search(t):
            target = (p, t)
            break
    if target is None:
        print(f"FAILED: {FN} nao definido em nenhum chunk")
        return 1
    p, s = target

    if "void ps3_call_opd(ppu_context* ctx, uint32_t opd_ea);" not in s:
        if DECL_OLD not in s:
            print(f"FAILED: ps3_indirect_call decl missing in {p.name}")
            return 1
        s = s.replace(DECL_OLD, DECL_NEW, 1)
        print(f"{p.name}: declared ps3_call_opd")

    m = fn_re.search(s)
    m2 = re.compile(r"^void func_[0-9A-Fa-f]+\(ppu_context\* ctx\) \{", re.M).search(s, m.end())
    a, b = m.start(), (m2.start() if m2 else len(s))
    region = s[a:b]

    if "WADLD-VT28]" in region and "WADLD-VT48]" in region:
        print(f"{p.name}: ALREADY (probes WADLD-* e ps3_call_opd presentes em {FN})")
        # a declaracao pode ter sido acrescentada acima; grava se mudou
        if s != target[1]:
            p.write_text(s, encoding="utf-8", newline="\n")
        return 0

    region = insert_after(region, A_T1SZ, P_T1SZ, "WADLD-T1SZ")
    region = insert_after(region, A_VT28, P_VT28, "WADLD-VT28")
    region = insert_after(region, A_VT48, P_VT48, "WADLD-VT48")

    region, n = OPD_BLOCK.subn(_opd_repl, region, count=2)
    if n != 2:
        raise SystemExit(
            f"esperava 2 blocos OPD (vt+0x28 e vt+0x48) em {FN}, encontrei {n}"
        )
    print(f"{p.name}: {FN} OPD vt+0x28 e vt+0x48 -> ps3_call_opd ({n} sites)")

    # [WADLD-VT28R]: valor de retorno do 1o OPD, logo apos o restauro do TOC.
    k = region.find("ps3_call_opd(ctx, (uint32_t)ctx->gpr[10]); DRAIN_TRAMPOLINE(ctx);\n")
    k = region.find("\n", region.find("ctx->gpr[2] = ", k)) + 1
    region = region[:k] + P_VT28R + region[k:]

    s = s[:a] + region + s[b:]
    p.write_text(s, encoding="utf-8", newline="\n")
    print(f"{p.name}: APPLIED probes WADLD-T1SZ / VT28 / VT28R / VT48")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
