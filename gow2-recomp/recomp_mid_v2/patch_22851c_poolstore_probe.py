#!/usr/bin/env python3
"""Sonda do store do pool 0 em `func_0022851C` -- correu, e com que gestor?

Porque existe (E31, 2026-08-05)
-------------------------------
Medido: o gestor `0x400C6B50` chega ao pop com `*(mgr+0x12C)` preenchido
(`0x4007FCE8`, a tabela de pools) e `*(mgr+0xD0)` **nulo**. A init preguicosa e'
saltada porque o guard testa `+0x12C != 0` -- e' um ponteiro usado como
booleano de "ja inicializado", e mente quando a tabela existe e os slots nao.

`func_0022851C` e' quem devia preencher o slot:

    ppu_recomp_000.cpp:507687   vm_write32(ctx->gpr[31] + 0xD0, ctx->gpr[3]);

Detalhe que motiva medir `r31` e nao outra coisa: **`r31` nao e' atribuido
dentro desta funcao antes dos stores** (so' nas linhas 507779+, ja' depois). E'
herdado do chamador. E o prologo le' os callee-save da STACK
(`vm_read64(r1+0x90/0x98)`), em vez de os salvar do registo como
`func_002182A4` faz -- o padrao de um FRAGMENTO, uma entrada a meio de uma
funcao maior.

Se este fragmento for alcancado por um caminho que nao poe `r31` no gestor
certo, os stores dos pools caem no gestor errado.

O que mede, e o que cada resultado significa
--------------------------------------------
Uma linha por execucao do store: o `r31` usado e o valor escrito.

  aparece r31=0x400C6B50   -> a init CORREU para este gestor; o store pegou ou
                              foi desfeito depois -> suspeita no lift/ordem
  nunca aparece            -> a init NUNCA correu com este gestor -> a pergunta
                              passa a ser quem devia entrar no fragmento com
                              r31=0x400C6B50

Gate: `PS3_TRACE_POOLSTORE` (vazio ou "0" = OFF, default). Cap por
`PS3_TRACE_POOLSTORE_CAP` (default 400).

Agulha ancorada no `lr = 0x002285A8`, unico desta funcao -- verificado.

Uso:  patch_22851c_poolstore_probe.py [DIR_DE_LIFT]
rc: 0 aplicado ou ja' aplicado; 2 se a agulha nao casou (SEM-EFEITO).
"""
import os
import sys
import glob

MARKER = "POOLSTORE-22851C-PROBE"

NEEDLE = (
    "        ctx->lr = 0x002285A8; func_002BB1B0(ctx); DRAIN_TRAMPOLINE(ctx);\n"
    "        /* nop */;\n"
    "        ctx->gpr[4] = (int64_t)(int32_t)(3);\n"
    "        vm_write32(ctx->gpr[31] + 0xD0, ctx->gpr[3]);\n"
)

REPL = NEEDLE + (
    "        /* " + MARKER + ": quem e' o gestor no store do pool 0.\n"
    "         * r31 e' HERDADO -- nao e' atribuido nesta funcao antes daqui.\n"
    "         * Ver docs/re_sessions/2026-08-05-E31-gestor-meio-inicializado.md */\n"
    "        { static int _on=-1; if(_on<0){ extern char* getenv(const char*);\n"
    "            const char* _e=getenv(\"PS3_TRACE_POOLSTORE\"); _on=(_e&&*_e&&*_e!='0')?1:0; }\n"
    "          static int _cap=-1; if(_cap<0){ extern char* getenv(const char*);\n"
    "            const char* _c=getenv(\"PS3_TRACE_POOLSTORE_CAP\"); _cap=(_c&&*_c)?atoi(_c):400; }\n"
    "          if(_on){ static int _n=0; if(_cap<0 || _n++<_cap){\n"
    "            uint32_t _m=(uint32_t)ctx->gpr[31];\n"
    "            fprintf(stderr,\"[POOLSTORE] #%d mgr(r31)=0x%08X pool0=0x%08X flag12C=0x%08X%s\\n\",\n"
    "              _n, _m, (uint32_t)ctx->gpr[3], vm_read32(_m + 0x12Cu),\n"
    "              (_m==0x400C6B50u)?\" <O-GESTOR-MAU>\":\"\");\n"
    "            fflush(stderr); } } }\n"
)


def patch_text(t):
    if MARKER in t:
        return t, "ja"
    if t.count(NEEDLE) != 1:
        return t, "nao-casou"
    return t.replace(NEEDLE, REPL, 1), "ok"


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    lift = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        os.path.dirname(here), "recomp_macos_v2")
    alvos = sorted(glob.glob(os.path.join(lift, "ppu_recomp_*.cpp")))
    if not alvos:
        print("SEM-EFEITO: nenhum ppu_recomp_*.cpp em %s" % lift)
        return 2
    aplicados = ja = 0
    for f in alvos:
        with open(f, encoding="utf-8", errors="replace") as fh:
            src = fh.read()
        novo, estado = patch_text(src)
        if estado == "ja":
            ja += 1; print("JA-APLICADO  %s" % os.path.basename(f))
        elif estado == "ok":
            with open(f, "w", newline="\n", encoding="utf-8") as fh:
                fh.write(novo)
            aplicados += 1; print("CONVERTIDO   %s" % os.path.basename(f))
    if aplicados == 0 and ja == 0:
        print("SEM-EFEITO: agulha do store do pool 0 nao casou em %s" % lift)
        return 2
    print("patch_22851c_poolstore_probe: aplicados=%d ja=%d" % (aplicados, ja))
    return 0


if __name__ == "__main__":
    sys.exit(main())
