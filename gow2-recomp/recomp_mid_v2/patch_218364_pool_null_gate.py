#!/usr/bin/env python3
"""GATE DE DIAGNOSTICO (nao e' um fix): devolver bloco NULO quando o "pool" nao
e' um ponteiro, para ver se a parede do B71 e' a ultima antes do loop principal.

Leia isto antes de usar
-----------------------
Isto **NAO corrige nada** e nao pode ser apresentado como correccao. E' um salto
por cima de uma parede, ligado por env var e OFF por default, cujo unico
proposito e' responder a uma pergunta que nenhuma leitura estatica responde:
**se esta parede cair, o boot entra no loop principal, ou bate logo noutra?**

Qualquer resultado obtido com ele fica marcado como **obtido com gate**, nunca
como progresso natural (CLAUDE.md regras 4 e 5). E ha' precedente do mesmo dia
para nao confiar no contrario: em Julho concluiu-se que o gate
`PS3_LIST254_EMPTY_IF_NULL` "nao desbloqueia" -- e a conclusao apoiava-se numa
metrica que nunca podia disparar. Um gate mal medido mente nos dois sentidos.

O que se sabe, medido a 2026-08-01
----------------------------------
Cadeia completa, cada seta medida:

    main() -> func_002B2E74 (7a de 9) -> B71 (9a de 11)
      -> #41 func_0010F5E8   (lookup no registry, idx=(tipo<<2)&0x3FFFC)
      -> func_0039D51C -> func_0039E40C(this=0x400C61C8)
      -> vt[0x60] = func_00254788   <- NAO RETORNA
    func_002B2E04 (8a chamada do main) nunca e' alcancada
      -> func_00242C94, o loop principal, NUNCA CORRE

Dentro do ciclo terminal, `func_002182A4` faz:

    r29  = r3 + 0x118
    base = *(r29 + 0x14)            // 0x4007FCE8 no objecto MAU
    pool = base[idx]                // idx=0 -> le' 0x00000005
    func_00263554(pool)             // pop da free-list sobre lixo
    ...                             // e o bloco devolvido vai, SEM null-check,
    func_002210BC(..., r7=bloco)    // para aqui -> 16 escritas UNCOMMITTED

E o 5 nao e' corrupcao: e' a CONTAGEM de elementos de um cabecalho de bloco
escrito por `func_00263178` (store#12 = `*(inicio) = (fim-inicio)/4`),
verificado contra o EBOOT -- o lift esta fiel, nao ha' offset perdido.

O defeito e' de TIPO: o objecto `0x400C6B50` nunca passou por `func_0022851C`,
o produtor que instala um array de pools em `+0x14`. Medido: esse produtor
corre 107 vezes e **sempre com o mesmo objecto** (`0x406387E0`, o sao).

O que este gate faz
-------------------
Quando o "pool" nao e' um ponteiro plausivel, salta o pop e devolve bloco NULO
(`r3 = 0`) em vez de percorrer lixo. Nao inventa memoria nem estampa valores:
so' recusa usar um numero como ponteiro.

Barulhento de proposito: uma linha por salto, para nunca ser silencioso.

Gate: `PS3_POOL_NULL_IF_BAD=1`.

Uso:  patch_218364_pool_null_gate.py [DIR_DE_LIFT]
rc: 0 aplicado/ja aplicado; 2 se a agulha nao casou.
"""
import os
import sys
import glob

MARKER = "POOL-NULL-GATE"

# Ancorado SO' na chamada: entre a leitura de base[idx] e o pop pode estar a
# sonda PS3_TRACE_POOLIDX, e uma agulha de duas linhas deixaria de casar
# consoante a ordem de aplicacao dos patches.
NEEDLE = (
    "        ctx->lr = 0x00218364; func_00263554(ctx); DRAIN_TRAMPOLINE(ctx);\n"
)

REPL = (
    "        /* " + MARKER + " (DIAGNOSTICO, OFF por default, NUNCA um fix):\n"
    "         * o \"pool\" lido de base[idx] e' as vezes a CONTAGEM de um\n"
    "         * cabecalho de bloco (5), nao um ponteiro -- porque o objecto\n"
    "         * nunca passou pelo produtor func_0022851C. Com o gate ligado\n"
    "         * recusamos usar o numero como ponteiro e devolvemos bloco nulo,\n"
    "         * so' para ver o que ha' a jusante. */\n"
    "        { static int _g=-1; if(_g<0){ extern char* getenv(const char*);\n"
    "            const char* _e=getenv(\"PS3_POOL_NULL_IF_BAD\");\n"
    "            _g=(_e&&*_e&&*_e!='0')?1:0; }\n"
    "          uint32_t _p=(uint32_t)ctx->gpr[3];\n"
    "          if(_g && (_p<0x10000u || _p>=0x4F000000u)){\n"
    "            static int _n=0; if(_n++<32){\n"
    "              fprintf(stderr,\"[POOL-GATE] pool=0x%08X nao e' ponteiro -> bloco NULO \"\n"
    "                \"(GATE, nao e' comportamento natural)\\n\", _p); fflush(stderr); }\n"
    "            ctx->gpr[3] = 0;\n"
    "          } else {\n"
    "            ctx->lr = 0x00218364; func_00263554(ctx); DRAIN_TRAMPOLINE(ctx);\n"
    "          } }\n"
)


def patch_text(t):
    if MARKER in t:
        return t, "ALREADY"
    if t.count(NEEDLE) != 1:
        return t, "MISSING"
    return t.replace(NEEDLE, REPL, 1), "APPLIED"


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    lift = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, "..", "recomp_macos_v2")
    lift = os.path.abspath(lift)
    chunks = sorted(glob.glob(os.path.join(lift, "ppu_recomp_*.cpp")))
    if not chunks:
        print("ERRO: nenhum ppu_recomp_*.cpp em %s" % lift, file=sys.stderr)
        return 2
    applied = already = 0
    for path in chunks:
        with open(path, "r", errors="replace") as fh:
            src = fh.read()
        out, state = patch_text(src)
        if state == "APPLIED":
            with open(path, "w") as fh:
                fh.write(out)
            applied += 1
            print("APPLIED  %s" % os.path.basename(path))
        elif state == "ALREADY":
            already += 1
            print("ALREADY  %s" % os.path.basename(path))
    if applied == 0 and already == 0:
        print("MISSING  agulha do pop com pool suspeito nao encontrada", file=sys.stderr)
        return 2
    print("patch_218364_pool_null_gate: applied=%d already=%d" % (applied, already))
    return 0


if __name__ == "__main__":
    sys.exit(main())
