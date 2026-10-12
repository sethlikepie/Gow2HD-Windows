#!/usr/bin/env python3
"""E118 -- marca QUAL dos saltos para o abort 0x42578 (estado=0) e' tomado: instrumenta cada `goto loc_00042578`
e cada trampolim para func_00042578 no lift com um id (a linha), imprimindo os registos-chave (r9,r4,r5,r26,r27).
Gate PS3_TRACE_REG."""
import sys,re
from pathlib import Path
ROOT = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "recomp_macos_v2"
def main():
    f=ROOT/"ppu_recomp_000.cpp"; s=f.read_text(errors='replace')
    if "E118-ABORTSITE" in s: print("E118: ALREADY"); return 0
    out=[]; n=0
    for line in s.split("\n"):
        m=re.search(r'goto loc_00042578;|g_trampoline_fn = \(void\(\*\)\(void\*\)\)func_00042578;', line)
        if m and "E118" not in line:
            n+=1
            probe=('{ static int _on=-1; if(_on<0){ extern char* getenv(const char*); const char* _e=getenv("PS3_TRACE_REG"); _on=(_e&&*_e&&*_e!=\'0\')?1:0; } '
                   'if(_on){ static int _k=0; if(_k++<4) fprintf(stderr,"[ABORTSITE] #SITEID r9=0x%08X r4=0x%08X r5=0x%08X r26=0x%08X r27=0x%08X r31=0x%08X\\n",'
                   '(uint32_t)ctx->gpr[9],(uint32_t)ctx->gpr[4],(uint32_t)ctx->gpr[5],(uint32_t)ctx->gpr[26],(uint32_t)ctx->gpr[27],(uint32_t)ctx->gpr[31]); fflush(stderr);} } /* E118-ABORTSITE */ ').replace("SITEID",str(n))
            # inserir a sonda ANTES do salto, dentro do mesmo bloco condicional: substituir "goto ..." por "{probe goto ...}"
            line=line.replace(m.group(0), "{ "+probe+m.group(0)+" }",1)
        out.append(line)
    f.write_text("\n".join(out)); print(f"E118: {n} sitios instrumentados"); return 0 if n else 2
if __name__=="__main__": raise SystemExit(main())
