#!/usr/bin/env python3
"""Sonda do despacho de estado do LOOP PRINCIPAL (`func_00242C94`).

Porque este e' o sitio certo
----------------------------
Medido a 2026-08-01: o loop principal corre e desenha (2619 `SetFlipCommand`
numa corrida) e **nunca retorna** -- zero linhas depois da chamada
`bl 0x00242C94` em 0x002B2E14. Tudo o que ha' a jusante (incluindo a criacao da
thread AUTO_LOAD, que o gate media como um elo da cadeia) e' codigo de
pos-loop: so' corre em REQUEST_EXITGAME. Ver a nota
`2026-08-01-o-elo-AUTO_LOAD-nao-pertence-a-cadeia.md`.

Logo o caminho para o menu esta DENTRO do loop, e o loop e' este:

    r30 = *(TOC-0x2544)                  // o objecto da aplicacao
    r31 = *(TOC-0x2454)

  corpo:
    r9  = *(r30 + 0x460C)                // <- OPD do handler do estado corrente
    ctr = *(r9 + 0);  r2 = *(r9 + 4)
    ps3_indirect_call                    // <- O DESPACHO DE ESTADO
    func_00262C64(*(r29))
    func_001E54E0(*(r30 + 0x4610), f1)
    func_00262C8C()
  cabeca:
    func_00194D3C(*(r31))                // cellSysutilCheckCallback + cellPadGetInfo2
    if (*(uint8*)(*(r31)+4) && *(uint8*)(*(r31)+5)) -> sai do jogo
    goto corpo

`*(r30+0x460C)` **nao e' uma vtable**: e' o ponteiro OPD do handler por frame do
estado corrente. Mudar esse ponteiro e' como o jogo avanca de estado
(boot -> intro -> menu). Se ele nunca muda, o jogo esta preso num estado.

O que esta sonda mede
---------------------
So' as **transicoes** -- imprime quando o OPD muda, nunca a 60 fps. Cada linha
traz o numero do frame em que a mudanca aconteceu, o OPD antigo e o novo, e o
endereco de codigo resolvido de cada um. Uma corrida da' a sequencia de estados
inteira em meia duzia de linhas.

Com `PS3_TRACE_STATE=all` imprime tambem um batimento a cada N frames
(`PS3_TRACE_STATE_EVERY`, default 600) para se distinguir "preso num estado" de
"o loop parou".

Gate: `PS3_TRACE_STATE` (vazio ou "0" = OFF, default). Read-only: nao toca em
nenhum registo nem em nenhuma memoria guest.

Uso:  patch_242c94_state_dispatch_probe.py [DIR_DE_LIFT]
rc: 0 aplicado/ja aplicado; 2 se a agulha nao casou.
"""
import os
import sys
import glob

MARKER = "STATE-DISPATCH-PROBE"

NEEDLE = (
    "loc_00242CBC:\n"
    "        ctx->gpr[9] = vm_read32(ctx->gpr[30] + 0x460C);\n"
    "        ctx->gpr[0] = vm_read32(ctx->gpr[9] + 0x0);\n"
)

REPL = (
    "loc_00242CBC:\n"
    "        ctx->gpr[9] = vm_read32(ctx->gpr[30] + 0x460C);\n"
    "        ctx->gpr[0] = vm_read32(ctx->gpr[9] + 0x0);\n"
    "        /* " + MARKER + ": as TRANSICOES de estado do loop principal */\n"
    "        { static int _on=-1; static int _all=0;\n"
    "          if(_on<0){ extern char* getenv(const char*);\n"
    "            const char* _e=getenv(\"PS3_TRACE_STATE\");\n"
    "            _on=(_e&&*_e&&*_e!='0')?1:0; _all=(_e&&*_e=='a')?1:0; }\n"
    "          static int _every=-1; if(_every<0){ extern char* getenv(const char*);\n"
    "            const char* _c=getenv(\"PS3_TRACE_STATE_EVERY\"); _every=(_c&&*_c)?atoi(_c):600; }\n"
    "          if(_on){ static uint32_t _prev=0xFFFFFFFFu; static long _frame=0;\n"
    "            uint32_t _opd=(uint32_t)ctx->gpr[9], _code=(uint32_t)ctx->gpr[0];\n"
    "            _frame++;\n"
    "            if(_opd!=_prev){\n"
    "              fprintf(stderr,\"[STATE] frame=%ld  0x%08X -> 0x%08X  (code 0x%08X) app=0x%08X\\n\",\n"
    "                _frame,_prev,_opd,_code,(uint32_t)ctx->gpr[30]);\n"
    "              fflush(stderr); _prev=_opd;\n"
    "            } else if(_all && _every>0 && (_frame%_every)==0){\n"
    "              fprintf(stderr,\"[STATE] frame=%ld  preso em 0x%08X (code 0x%08X)\\n\",\n"
    "                _frame,_opd,_code);\n"
    "              fflush(stderr);\n"
    "            } } }\n"
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
        print("MISSING  agulha do despacho de estado nao encontrada", file=sys.stderr)
        return 2
    print("patch_242c94_state_dispatch_probe: applied=%d already=%d" % (applied, already))
    return 0


if __name__ == "__main__":
    sys.exit(main())
