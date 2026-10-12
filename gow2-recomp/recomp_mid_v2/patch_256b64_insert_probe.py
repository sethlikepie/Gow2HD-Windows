#!/usr/bin/env python3
"""Sonda da INSERCAO na lista de filhos -- quantos nos entram em cada lista.

Cadeia medida (2026-08-02, parede 4 da Fase 11)
----------------------------------------------
O walk `func_0041F700` explode exponencialmente (12^7 ~ 35M) porque cada tipo tem
os outros doze como filhos e a poda so' salta filhos do MESMO tipo, sem conjunto
de visitados. A pilha de produtos e' um ACUMULADOR (push sem pop -- verificado
desmontando a funcao inteira: o unico store do cursor e' o push em 0x0041F73C, e
o epilogo nao tem `stb`; 5a suspeita de bug do lifter, 5a refutada).

E as listas de filhos sao POR OBJECTO, nao globais (medido: tres sentinelas, tres
blocos de nos distintos, 12 cada).

`PS3_WATCH_STORE` na sentinela e nos nos deu os tres construtores:

    [0x40007D0C]=0x40007D18  ra0=func_0026372C  <- alocador encadeia a free-list
    [0x42F853B0]=0x42F853B0  ra0=func_00253BAC  <- AUTO-LIGACAO (lista vazia)
    [0x42F853B0]=0x40007D0C  ra0=func_00256B64  <- INSERCAO (push-front)
    [0x40007D0C]=0x42F853B0  ra0=func_00256B64

E as funcoes reais, por xref de `bl` no EBOOT:

    func_00256B64 -> 0x00256254, chamada de 0x00256E38
    func_00253BAC -> ela propria, chamada de 0x0041FA60
    func_0041FA30 -> 0x0041B7AC, chamada de 0x0024E874 (o WALKER DO WAD) e outros

Ou seja **as listas de filhos sao construidas durante o walk do WAD**.

O que esta sonda mede
---------------------
Em cada insercao: a lista (sentinela) e o no' inserido. Agrupando por sentinela
sabe-se **quantos nos entram em cada lista** e se todas recebem os mesmos.

Duas leituras, e so' a medicao as separa:

  - **cada lista recebe 12 nos distintos** -> a estrutura e' a que o jogo quer, e
    o defeito esta no walk que a percorre sem conjunto de visitados
  - **todas as listas recebem os mesmos nos** -> a insercao nao esta a discriminar
    por tipo, e o defeito esta aqui

Gate: `PS3_TRACE_INSERT` (vazio ou "0" = OFF, default). Cap por
`PS3_TRACE_INSERT_CAP` (default 200, `-1` = ilimitado). Read-only.

Uso:  patch_256b64_insert_probe.py [DIR_DE_LIFT]
rc: 0 aplicado/ja aplicado; 2 se a agulha nao casou.
"""
import os
import sys
import glob

MARKER = "INSERT-PROBE"

NEEDLE = "void func_00256B64(ppu_context* ctx) {\n"

REPL = NEEDLE + (
    "        /* " + MARKER + ": que no' entra em que lista */\n"
    "        { static int _on=-1; if(_on<0){ extern char* getenv(const char*);\n"
    "            const char* _e=getenv(\"PS3_TRACE_INSERT\"); _on=(_e&&*_e&&*_e!='0')?1:0; }\n"
    "          static int _cap=-2; if(_cap==-2){ extern char* getenv(const char*);\n"
    "            const char* _c=getenv(\"PS3_TRACE_INSERT_CAP\"); _cap=(_c&&*_c)?atoi(_c):200; }\n"
    "          if(_on){ static int _n=0; if(_cap<0 || _n++<_cap){\n"
    "            fprintf(stderr,\"[INSERT] #%d r3=0x%08X r4=0x%08X r5=0x%08X\\n\",\n"
    "              _n,(uint32_t)ctx->gpr[3],(uint32_t)ctx->gpr[4],(uint32_t)ctx->gpr[5]);\n"
    "            fflush(stderr); } } }\n"
)


def patch_text(t):
    if MARKER in t:
        return t, "ALREADY", 0
    n = t.count(NEEDLE)
    if not n:
        return t, "MISSING", 0
    return t.replace(NEEDLE, REPL), "APPLIED", n


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    lift = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, "..", "recomp_macos_v2")
    lift = os.path.abspath(lift)
    chunks = sorted(glob.glob(os.path.join(lift, "ppu_recomp_*.cpp")))
    if not chunks:
        print("ERRO: nenhum ppu_recomp_*.cpp em %s" % lift, file=sys.stderr)
        return 2
    applied = already = sites = 0
    for path in chunks:
        with open(path, "r", errors="replace") as fh:
            src = fh.read()
        out, state, n = patch_text(src)
        if state == "APPLIED":
            with open(path, "w") as fh:
                fh.write(out)
            applied += 1
            sites += n
            print("APPLIED  %-20s sites=%d" % (os.path.basename(path), n))
        elif state == "ALREADY":
            already += 1
            print("ALREADY  %s" % os.path.basename(path))
    if applied == 0 and already == 0:
        print("MISSING  func_00256B64 nao encontrada", file=sys.stderr)
        return 2
    print("patch_256b64_insert_probe: applied=%d already=%d sites=%d"
          % (applied, already, sites))
    return 0


if __name__ == "__main__":
    sys.exit(main())
