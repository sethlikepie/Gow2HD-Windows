#!/usr/bin/env python3
"""ghidra_lookup.py -- de um endereco guest para o codigo ORIGINAL decompilado.

O ciclo de debugging deste projecto e' "vi este EA no log, o que e' que ele
faz?". Ate aqui a resposta vinha de grep no lift (1,3 GB de C anonimo, 56 mil
funcoes chamadas func_XXXXXXXX). Com a analise do Ghidra exportada, a resposta
e' o C decompilado do jogo original.

Consome o que o tools/ghidra_analyze.py escreve:
    functions.json  [{addr,size,name,thunk,calling}]
    symbols.json    [{addr,name,type,source,namespace}]
    strings.json    [{func,addr,strings:[{addr,val}]}]
    decompiled.json [{addr,name,c}]        (so' com --decompile)

Uso:
    ghidra_lookup.py <ghidra_out> 0x002B2DD0            funcao que contem o EA
    ghidra_lookup.py <ghidra_out> 0x002B2DD0 --callers  quem chama
    ghidra_lookup.py <ghidra_out> --grep "f4"           procura no C decompilado
    ghidra_lookup.py <ghidra_out> --writes 0xF4         quem escreve o offset 0xF4
"""
import argparse
import json
import os
import re
import sys


def load(d, name, required=False):
    p = os.path.join(d, name)
    if not os.path.isfile(p):
        if required:
            sys.exit(f"ERRO: falta {p}. Corre primeiro tools/ghidra_analyze.py "
                     f"(com --decompile para ter decompiled.json).")
        return None
    with open(p) as f:
        return json.load(f)


def as_int(v):
    if isinstance(v, int):
        return v
    return int(str(v), 0)


def containing(funcs, ea):
    """(funcao, exacta) para ea.

    exacta=True  -> ea cai mesmo em [addr, addr+size)
    exacta=False -> nenhuma funcao o contem; devolve a anterior mais proxima
                    apenas como PISTA. Nao a apresentar como resposta: um EA
                    fora de tudo dava antes "3733098783 bytes dentro de ...".
    """
    near = None
    for f in funcs:
        a = as_int(f["addr"])
        n = as_int(f.get("size") or 0)
        if a <= ea < a + n:
            return f, True
        if a <= ea and (near is None or a > as_int(near["addr"])):
            near = f
    return near, False


def print_func(f, decomp_by_addr, show_c=True):
    a = as_int(f["addr"])
    print(f"=== 0x{a:08X}  {f.get('name') or '(sem nome)'}"
          f"  size={as_int(f.get('size') or 0)}"
          f"{'  [thunk]' if f.get('thunk') else ''}")
    if show_c:
        d = decomp_by_addr.get(a)
        if d and d.get("c"):
            print(d["c"])
        else:
            print("(sem C decompilado -- corre ghidra_analyze.py --decompile)")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("ghidra_dir", help="dir com functions.json/decompiled.json")
    ap.add_argument("ea", nargs="?", help="endereco guest (0x...)")
    ap.add_argument("--callers", action="store_true",
                    help="listar funcoes cujo C decompilado menciona este EA/nome")
    ap.add_argument("--grep", metavar="PADRAO",
                    help="regex sobre o C decompilado de todas as funcoes")
    ap.add_argument("--writes", metavar="OFFSET",
                    help="procurar escritas a um offset de struct (ex.: 0xF4)")
    ap.add_argument("--no-c", action="store_true", help="so' o cabecalho, sem o C")
    ap.add_argument("--max", type=int, default=20, help="maximo de resultados")
    args = ap.parse_args()

    funcs = load(args.ghidra_dir, "functions.json", required=True)
    decomp = load(args.ghidra_dir, "decompiled.json") or []
    decomp_by_addr = {as_int(d["addr"]): d for d in decomp}
    print(f"[ghidra] {len(funcs)} funcoes, {len(decomp)} com C decompilado\n")

    if args.grep or args.writes:
        if args.writes:
            off = as_int(args.writes)
            # Formas que o decompilador do Ghidra emite para uma escrita a um
            # campo: *(t *)(p + 0xF4) = ..., p->field_0xf4 = ..., [0xf4] = ...
            pat = re.compile(
                rf"(\+\s*0x{off:x}\b[^=;\n]*=[^=]|field_0x{off:x}\s*=[^=]|"
                rf"\[\s*0x{off:x}\s*\]\s*=[^=])", re.I)
            what = f"escritas ao offset 0x{off:X}"
        else:
            pat = re.compile(args.grep, re.I)
            what = f"padrao {args.grep!r}"
        hits = 0
        for d in decomp:
            c = d.get("c") or ""
            for m in pat.finditer(c):
                line = c[:m.start()].count("\n") + 1
                ctx = c.splitlines()[line - 1].strip() if line - 1 < len(c.splitlines()) else ""
                print(f"0x{as_int(d['addr']):08X} {d.get('name',''):40} :{line}  {ctx[:110]}")
                hits += 1
                break
            if hits >= args.max:
                print(f"... (limite {args.max}; use --max)")
                break
        if not hits:
            print(f"nenhum resultado para {what}")
        return 0

    if not args.ea:
        ap.error("indica um endereco, ou usa --grep/--writes")
    ea = as_int(args.ea)

    f, exact = containing(funcs, ea)
    if f is None:
        print(f"0x{ea:08X} nao cai em nenhuma funcao conhecida do Ghidra")
        return 1
    a = as_int(f["addr"])
    if not exact:
        # Nao inventar: dizer que nao sabe e oferecer a anterior como pista.
        print(f"0x{ea:08X} NAO cai em nenhuma funcao conhecida do Ghidra.")
        print(f"  funcao anterior mais proxima: 0x{a:08X} "
              f"{f.get('name') or ''} (+{as_int(f.get('size') or 0)} bytes de tamanho)")
        print("  provavel: dados, regiao nao analisada, ou fronteira em falta.")
        return 1
    if ea != a:
        print(f"(0x{ea:08X} esta {ea - a} bytes dentro de 0x{a:08X})\n")
    print_func(f, decomp_by_addr, show_c=not args.no_c)

    if args.callers:
        name = f.get("name") or ""
        needle_addr = f"{a:08x}"
        print(f"\n--- quem menciona 0x{a:08X} / {name} ---")
        n = 0
        for d in decomp:
            if as_int(d["addr"]) == a:
                continue
            c = (d.get("c") or "")
            if (name and name in c) or needle_addr in c.lower():
                print(f"0x{as_int(d['addr']):08X} {d.get('name','')}")
                n += 1
                if n >= args.max:
                    print(f"... (limite {args.max})")
                    break
        if n == 0:
            print("(nenhum -- pode ser chamada indirecta / por tabela)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
