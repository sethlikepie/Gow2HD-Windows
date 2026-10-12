#!/usr/bin/env python3
"""E128 -- verificador de callee-saved (r1, r14..r31) em volta de CADA call (directa e indirecta) dentro de func_0009EC0C.
Imprime [CSAVE] callee=... lr=... rN: antes->depois so' quando ha' diferenca. Gate PS3_TRACE_REG. Diagnostico, nao e' fix."""
import sys,re
from pathlib import Path
ROOT = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "recomp_macos_v2"
FUNCS = sys.argv[2:] or ["func_0009EC0C"]
GATE = r'''static int _cs_on=-1; if(_cs_on<0){ extern char* getenv(const char*); const char* _e=getenv("PS3_TRACE_REG"); _cs_on=(_e&&*_e&&*_e!='0')?1:0; }'''
def wrap(line, callee, lr):
    pre = ("        { "+GATE+" uint64_t _b[32]; uint32_t _ctr0=(uint32_t)ctx->ctr; if(_cs_on){ for(int _i=1;_i<32;_i++) _b[_i]=ctx->gpr[_i]; }\n")
    post = ("\n          if(_cs_on){ static int _k=0; for(int _i=14;_i<32;_i++){ if(_b[_i]!=ctx->gpr[_i] && _k<40){ _k++; "
            "fprintf(stderr,\"[CSAVE] callee=%s tgt=0x%08X r%d: 0x%08X -> 0x%08X\\n\",\"" + callee + "\",(uint32_t)" + lr + ",_i,(uint32_t)_b[_i],(uint32_t)ctx->gpr[_i]); } } "
            "if(_b[1]!=ctx->gpr[1] && _k<40){ _k++; fprintf(stderr,\"[CSAVE] callee=%s tgt=0x%08X r1: 0x%08X -> 0x%08X\\n\",\"" + callee + "\",(uint32_t)" + lr + ",(uint32_t)_b[1],(uint32_t)ctx->gpr[1]); } } }")
    return pre + line + post
def main():
    n=0
    for fn in FUNCS:
      hdr="void "+fn+"(ppu_context* ctx) {"
      f=next((x for x in sorted(ROOT.glob("ppu_recomp_00*.cpp")) if hdr in x.read_text(errors='replace')),None)
      if f is None: print("E128: nao achei",fn); continue
      s=f.read_text(errors='replace'); lines=s.split("\n"); L=next(i for i,l in enumerate(lines) if l.startswith(hdr))
      E=next(i for i in range(L+1,len(lines)) if lines[i].startswith("}"))
      if any("[CSAVE]" in lines[i] for i in range(L,E)): print("E128:",fn,"ALREADY"); continue
      for i in range(L,E):
        l=lines[i]
        m=re.match(r'\s*ctx->lr = (0x[0-9A-F]+); (func_[0-9A-F]+)\(ctx\); DRAIN_TRAMPOLINE\(ctx\);\s*$', l)
        if m: lines[i]=wrap(l, m.group(2), m.group(1)); n+=1; continue
        if re.match(r'\s*ps3_indirect_call\(ctx\); DRAIN_TRAMPOLINE\(ctx\);\s*$', l):
            lines[i]=wrap(l, "indirect", "_ctr0"); n+=1
      f.write_text("\n".join(lines)); print("E128:",fn,"instrumentado")
    print(f"E128: {n} calls embrulhados"); return 0 if n else 2
if __name__=="__main__": raise SystemExit(main())
