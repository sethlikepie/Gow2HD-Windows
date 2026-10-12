#!/usr/bin/env python3
"""Marca CADA chamada dentro do B71 (`func_000B71B8`) com um ponto de passagem
numerado -- para localizar, numa so' reconstrucao, a chamada que nao retorna.

Porque assim e nao mais bisseccoes
----------------------------------
Medido a 2026-08-01: o `main()` chama nove funcoes em linha recta e a oitava e'
o loop principal do jogo. A setima (`func_002B2E74`) nao retorna porque a nona
chamada DELA -- `func_000B71B8`, o B71 -- nao retorna. Logo **o jogo nunca entra
no loop principal** e o despacho de estado `*(r30+0x460C)` nunca corre.

Com `PS3_LIST254_EMPTY_IF_NULL=1` o `FATAL` do breaker desaparece (o processo
deixa de morrer) mas o B71 continua sem retornar: passa a PENDURAR num ciclo de
stream FIOS. Ha' um segundo bloqueador la' dentro.

O B71 e' grande de mais para bisseccao manual: cada ronda custa ~4 min de
rebuild. Este patch instrumenta todas as chamadas de uma vez. **O ultimo numero
impresso e' a chamada que nao voltou** -- e o nome do callee vem na linha.

O que imprime
-------------
Antes de cada chamada, uma linha com o indice, o nome do callee e o `r3`:

    [B71] #07 -> func_000CE0A0 r3=0x...
    [B71] #08 -> func_00040090 r3=0x...
    (silencio a partir daqui = a #08 nao voltou)

Cobre chamadas directas (`func_XXXX(ctx)`) e o despacho indirecto
(`ps3_indirect_call`). Read-only: nao toca em registos nem em memoria guest.

Gate: `PS3_TRACE_B71` (vazio ou "0" = OFF, default). `PS3_TRACE_B71_CAP`
(default 400, `-1` = ilimitado) -- sem cap fixo, pela mesma razao que as outras
sondas desta sessao.

Como o B71 pode estar dividido em fragmentos pelo lifter, o patch instrumenta
`func_000B71B8` **e** todos os fragmentos `func_000B7xxx` que existam, para o
rasto nao se perder num trampolim. Cada fragmento tem a sua propria numeracao,
prefixada pelo nome.

Uso:  patch_b71_callsite_trace.py [DIR_DE_LIFT]
rc: 0 aplicado/ja aplicado; 2 se o B71 nao foi encontrado.
"""
import os
import re
import sys
import glob

MARKER = "B71-CALLSITE-TRACE"

# Funcoes cujas chamadas sao marcadas. Cresce a medida que a investigacao
# desce: cada nome novo aqui e' uma funcao que se mediu NAO RETORNAR, e cujo
# interior e' preciso abrir.
#   func_000B7xxx  -- o B71 e os seus fragmentos (a parede, medida)
#   func_0010F5E8  -- a chamada #41 do B71, que nao retorna (medida). Faz
#                     lookup no registry de tipos, idx=(tipo<<2)&0x3FFFC, e
#                     despacha vt[0x20] -- territorio da Parede D.
#   func_0039D51C  -- o alvo desse despacho (this=0x400C5048), que tambem
#                     nao retorna (medido). Familia das fabricas 0x0039Dxxx.
#   func_0039E40C  -- o alvo seguinte (this=0x400C61C8, o objecto de Julho).
#                     Pendura no PRIMEIRO despacho dela, vt[0x60].
#   func_00254xxx / func_0024Exxx / func_0024D5BC -- a familia do walker de
#                     registos do WAD, instrumentada em bloco para nao gastar
#                     uma reconstrucao (~5 min) por degrau. Alargado de
#                     002545xx para 00254xxx: o ponto terminal medido e'
#                     func_00254788 (o vt[0x60] de 0x400C61C8) e a agulha
#                     estreita deixava-o de fora.
#   func_002B2DD0 / func_002B2660 -- com os tres gates de diagnostico o boot
#                     ENTRA no loop principal e despacha o primeiro estado,
#                     cujo handler e' func_002B2DD0. Ele tem tres chamadas e a
#                     PRIMEIRA (func_002B2660) nao retorna: e' a quarta parede,
#                     e a primeira DENTRO do loop.
#   func_0041Fxxx / 00423xxx / 00424xxx / 002B5xxx / 002B24E0 -- o caminho
#                     que a STACK DE UM CRASH REAL revelou (2026-08-02):
#                       func_00424588 -> func_00423AB4 -> func_002B24E0
#                       -> func_002B5ED8 -> func_002B5BF8 -> func_002B5874
#                       -> cellPadSetActDirect  (SIGSEGV, ja' corrigido)
#                     Um stack trace real vale mais do que uma ronda de sondas.
HEAD_RE = re.compile(
    r'^void (func_000B7[0-9A-F]{3}|func_0010F5E8|func_0039D51C|func_0039E40C'
    r'|func_00254[0-9A-F]{3}|func_0024E[0-9A-F]{3}|func_0024D5BC'
    r'|func_002B2660|func_002B2DD0|func_004244C0'
    r'|func_0041F[0-9A-F]{3}|func_00423[0-9A-F]{3}|func_00424[0-9A-F]{3}'
    r'|func_002B5[0-9A-F]{3}|func_002B24E0)'
    r'\(ppu_context\* ctx\) \{$', re.M)
# uma chamada directa a outra funcao liftada, ou o despacho indirecto
CALL_RE = re.compile(
    r'^(        )(?:ctx->lr = 0x[0-9A-F]+; )?'
    r'(func_[0-9A-F]{8}|ps3_indirect_call|ps3_indirect_tail)\(ctx\);',
    re.M)


ENTRY_PROBE_FOR = ("func_00254788", "func_0041F700")  # lacos terminais: precisam do r4


def entry_probe(fn):
    """Sonda de ENTRADA: imprime this(r3) e arg(r4). O rasto de call-sites nao
    cobre funcoes sem chamadas -- e `func_00254788` nao tem nenhuma, e' um laco
    puro. Sem isto o `arg` do laco terminal nunca foi medido (media-se o r21 do
    walker, que e' OUTRO sitio de chamada)."""
    return (
        "        /* " + MARKER + "-ENTRY */\n"
        "        { static int _on=-1; if(_on<0){ extern char* getenv(const char*);\n"
        "            const char* _e=getenv(\"PS3_TRACE_B71\"); _on=(_e&&*_e&&*_e!='0')?1:0; }\n"
        "          static int _cap=-2; if(_cap==-2){ extern char* getenv(const char*);\n"
        "            const char* _c=getenv(\"PS3_TRACE_B71_ENTRY_CAP\");\n"
        "            _cap=(_c&&*_c)?atoi(_c):40; }\n"
        "          if(_on){ static int _n=0; if(_cap<0 || _n++<_cap){\n"
        "            uint32_t _a=(uint32_t)ctx->gpr[4];\n"
        "            int _ok=(_a>=0x10000u && _a<0x4F000000u);\n"
        "            uint32_t _sent=_a-4u+0x80u;\n"
        "            fprintf(stderr,\"[B71-ENTRY] " + fn + " this=0x%08X arg=0x%08X \"\n"
        "              \"argw0=0x%08X tag=%d sent=0x%08X head=0x%08X\\n\",\n"
        "              (uint32_t)ctx->gpr[3], _a,\n"
        "              _ok?vm_read32(_a):0u, _ok?(int)vm_read16(_a+2u):-1,\n"
        "              _sent, _ok?vm_read32(_sent):0u);\n"
        "            fflush(stderr); } } }\n"
    )


def probe(idx, callee, owner):
    return (
        "        /* " + MARKER + " */\n"
        "        { static int _on=-1; if(_on<0){ extern char* getenv(const char*);\n"
        "            const char* _e=getenv(\"PS3_TRACE_B71\"); _on=(_e&&*_e&&*_e!='0')?1:0; }\n"
        "          static int _cap=-2; if(_cap==-2){ extern char* getenv(const char*);\n"
        "            const char* _c=getenv(\"PS3_TRACE_B71_CAP\"); _cap=(_c&&*_c)?atoi(_c):400; }\n"
        "          if(_on){ static int _n=0; if(_cap<0 || _n++<_cap){\n"
        "            fprintf(stderr,\"[B71] " + owner + " #%03d -> " + callee + " r3=0x%08X ctr=0x%08X\\n\",\n"
        "              " + str(idx) + ", (uint32_t)ctx->gpr[3], (uint32_t)ctx->ctr);\n"
        "            fflush(stderr); } } }\n"
    )


def body_span(t, start):
    """Devolve (ini, fim) do corpo da funcao que comeca em start (linha do
    cabecalho), delimitado pelo primeiro '}' na coluna 0."""
    ini = t.index("\n", start) + 1
    fim = t.index("\n}\n", ini) + 1
    return ini, fim


def patch_text(t):
    if MARKER in t:
        return t, "ALREADY", 0
    heads = list(HEAD_RE.finditer(t))
    if not heads:
        return t, "MISSING", 0
    out = []
    prev = 0
    total = 0
    for h in heads:
        owner = h.group(1)
        ini, fim = body_span(t, h.start())
        out.append(t[prev:ini])
        if owner in ENTRY_PROBE_FOR:
            out.append(entry_probe(owner))
        body = t[ini:fim]
        pieces = []
        last = 0
        idx = 0
        for m in CALL_RE.finditer(body):
            idx += 1
            total += 1
            pieces.append(body[last:m.start()])
            pieces.append(probe(idx, m.group(2), owner))
            last = m.start()
        pieces.append(body[last:])
        out.append("".join(pieces))
        prev = fim
    out.append(t[prev:])
    if not total:
        return t, "MISSING", 0
    return "".join(out), "APPLIED", total


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
            print("APPLIED  %-20s chamadas=%d" % (os.path.basename(path), n))
        elif state == "ALREADY":
            already += 1
            print("ALREADY  %s" % os.path.basename(path))
    if applied == 0 and already == 0:
        print("MISSING  func_000B7xxx nao encontrada", file=sys.stderr)
        return 2
    print("patch_b71_callsite_trace: applied=%d already=%d chamadas=%d"
          % (applied, already, sites))
    return 0


if __name__ == "__main__":
    sys.exit(main())
