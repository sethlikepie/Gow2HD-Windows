#!/usr/bin/env python3
"""E129 -- fallthrough perdido no fim de fragmento (classe do fix 2550C8, variante "cauda condicional").

Bug do lift: um fragmento cuja ULTIMA instrucao e' um salto condicional (tipicamente o back-edge de um
loop: `if (...) goto loc_X;`) termina SEM trampolim para o sucessor sequencial. Quando o salto nao e'
tomado, a funcao C devolve com g_trampoline_fn=NULL, a cadeia DRAIN_TRAMPOLINE acaba e o controlo volta
ao CALLER do guest -- saltando o resto da funcao guest (em func_0024D144: o epilogo func_0024D1A0 que faz
`addi r1,r1,0x80`). Medido a 2026-09-02 (E128): r1 fica -0x80 por chamada a FUN_0024d0f4, -0x100 em
FUN_0009e9f4, e o epilogo de FUN_0009fe48 restaura r25..r31 da frame errada -> `*(entidade+4)=entidade`
-> lookup de material com chave 0x00512000 -> produto NULL -> FREELIST-TAG-GUARD -> spin em func_002DAF04.

Fix (fiel ao PPC): acrescentar `{ g_trampoline_fn = func_SUCESSOR; return; }` no fim de cada fragmento
cuja ultima instrucao e' condicional e cujo sucessor sequencial pertence a MESMA funcao (Ghidra).
Idempotente. Uso: patch_e129_fragment_fallthrough.py [recomp_dir] [ghidra decompiled.json]
"""
import sys,re,glob,json,bisect
from pathlib import Path
ROOT = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "recomp_macos_v2"
GH = Path(sys.argv[2]) if len(sys.argv) > 2 else Path(__file__).resolve().parent.parent / "ghidra_out" / "decompiled.json"
MARK = "/* E129-FALLTHROUGH */"
COND = re.compile(r'^\s*if \(.*\) goto loc_[0-9A-F]+;\s*$')
def main():
    fs=json.load(open(GH)); names=sorted(int(f['name'].split('_')[-1],16) for f in fs if re.match(r'\.opd\.FUN_[0-9a-f]{8}$',f['name']))
    files={p:open(p,errors='replace').read() for p in sorted(glob.glob(str(ROOT/"ppu_recomp_00[0-5].cpp")))}
    frag={}
    for p,s in files.items():
        for m in re.finditer(r'^void func_([0-9A-F]{8})\(ppu_context\* ctx\) \{\n(.*?)^\}\n',s,re.M|re.S): frag[int(m.group(1),16)]=(p,m.start(),m.end(),m.group(2))
    addrs=sorted(frag); n=0; already=0; edits={}
    for a in addrs:
        p,st,en,body=frag[a]
        if MARK in body: already+=1; continue
        lines=[l for l in body.split('\n') if l.strip()]
        while lines and ('_cs_on' in lines[-1] or 'fprintf(stderr' in lines[-1] or lines[-1].strip() in ('}','} }','} } }')): lines.pop()
        if not lines or not COND.match(lines[-1]): continue
        i=bisect.bisect_right(addrs,a); succ=addrs[i] if i<len(addrs) else None
        if succ is None: continue
        if names[bisect.bisect_right(names,a)-1]!=names[bisect.bisect_right(names,succ)-1]: continue
        edits.setdefault(p,[]).append((a,succ,st,en)); n+=1
    for p,lst in edits.items():
        s=files[p]
        for a,succ,st,en in sorted(lst,key=lambda x:-x[2]):
            fn=s[st:en]
            fixed=fn[:-2]+"        %s { g_trampoline_fn = (void(*)(void*))func_%08X; return; }\n}\n"%(MARK,succ)
            decl="void func_%08X(ppu_context* ctx);\n"%succ
            s=s[:st]+decl+fixed+s[en:]
        Path(p).write_text(s)
    print("E129: %d fragmentos corrigidos (%d ja' corrigidos)"%(n,already)); return 0
if __name__=="__main__": raise SystemExit(main())
