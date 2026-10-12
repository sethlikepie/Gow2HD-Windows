#!/usr/bin/env python3
"""Sonda da init preguicosa do gestor em `func_00212ED0`.

Porque existe (E30, 2026-08-05)
-------------------------------
Medido: `*(mgr + 0xD0) == 0` na unica alocacao envenenada -- o pool de indice 0
nao existe quando `func_0040FD64` o pede. As 225 alocacoes sas usam o indice 10
(`off=0xF8`); a ma usa o 0 (`off=0xD0`).

E o pool DEVERIA existir: `func_0022851C` cria-o com elem=0x34, count lazy 0,
grow 8, e faz `*(manager + 0xD0) = resultado`
(`ppu_recomp_000.cpp:507641-507649`). O slot e' atribuido.

Antes do pop, `func_00212ED0` consulta um flag:

    r0 = *(mgr + 0x12C)
    se r0 == 0  ->  func_0022831C(mgr)      // init preguicosa
    loc_00212F38: pool = *(mgr + 0xD0 + idx*4)

(Nota: o relatorio delegado dizia `!= 0`; e' ao contrario -- a chamada acontece
quando o flag e' ZERO. Verificado no lift.)

O que mede, e o que cada resultado significa
--------------------------------------------
Imprime, no ponto do flag: o gestor, o flag, e o slot `+0xD0` lido ali mesmo.

  flag != 0 e pool0 == 0   -> alguem marcou "inicializado" sem criar o pool
  flag == 0 e pool0 == 0   -> a init vai correr; se o pop continuar a ver 0,
                              a init corre e nao cria o indice 0
  pool0 != 0 aqui          -> o gestor deste ponto nao e' o do pop; o alvo muda

Os tres sao accionaveis, e e' por isso que a sonda vale a corrida.

Agulha: a sequencia curta do flag aparece **4 vezes** no lift, e mesmo com o
`ps3_indirect_call` e o TOCFIX a' frente ainda sao **3**. A agulha usada ancora
na etiqueta `loc_00212F38` e no `lr = 0x00212F34`, que so' existem nesta
funcao -- verificado unico a 2026-08-05.

Gate: `PS3_TRACE_LAZYINIT` (vazio ou "0" = OFF, default). Cap por
`PS3_TRACE_LAZYINIT_CAP` (default 400).

Uso:  patch_212ed0_lazyinit_probe.py [DIR_DE_LIFT]
rc: 0 aplicado ou ja' aplicado; 2 se a agulha nao casou (SEM-EFEITO).
"""
import os
import sys
import glob

MARKER = "LAZYINIT-212ED0-PROBE"

NEEDLE = (
    "        ctx->gpr[0] = vm_read32(ctx->gpr[31] + 0x12C);\n"
    "        { int64_t a = (int32_t)ctx->gpr[0]; int64_t b = (int64_t)0; uint32_t cr_val = (a < b) ? 8 : (a > b) ? 4 : 2; ctx->cr = (ctx->cr & ~(0xFu << 0)) | (cr_val << 0); }\n"
    "        if ((!((ctx->cr >> 0) & 2))) goto loc_00212F38;\n"
    "        ctx->lr = 0x00212F34; func_0022831C(ctx); DRAIN_TRAMPOLINE(ctx);\n"
)

REPL = (
    "        ctx->gpr[0] = vm_read32(ctx->gpr[31] + 0x12C);\n"
    "        /* " + MARKER + ": gestor, flag de init e o slot +0xD0 lido aqui.\n"
    "         * Ver docs/re_sessions/2026-08-05-E30-sintese-e-o-que-resolver.md */\n"
    "        { static int _on=-1; if(_on<0){ extern char* getenv(const char*);\n"
    "            const char* _e=getenv(\"PS3_TRACE_LAZYINIT\"); _on=(_e&&*_e&&*_e!='0')?1:0; }\n"
    "          static int _cap=-1; if(_cap<0){ extern char* getenv(const char*);\n"
    "            const char* _c=getenv(\"PS3_TRACE_LAZYINIT_CAP\"); _cap=(_c&&*_c)?atoi(_c):400; }\n"
    "          if(_on){ static int _n=0; if(_cap<0 || _n++<_cap){\n"
    "            uint32_t _m=(uint32_t)ctx->gpr[31];\n"
    "            uint32_t _f=(uint32_t)ctx->gpr[0];\n"
    "            uint32_t _p0=vm_read32(_m + 0xD0u);\n"
    "            fprintf(stderr,\"[LAZYINIT] #%d mgr=0x%08X flag=0x%08X pool0=0x%08X init_vai_correr=%d%s\\n\",\n"
    "              _n, _m, _f, _p0, (_f==0u)?1:0,\n"
    "              (_p0==0u)?\" <POOL0-NULO>\":\"\");\n"
    "            fflush(stderr); } } }\n"
    "        { int64_t a = (int32_t)ctx->gpr[0]; int64_t b = (int64_t)0; uint32_t cr_val = (a < b) ? 8 : (a > b) ? 4 : 2; ctx->cr = (ctx->cr & ~(0xFu << 0)) | (cr_val << 0); }\n"
    "        if ((!((ctx->cr >> 0) & 2))) goto loc_00212F38;\n"
    "        ctx->lr = 0x00212F34; func_0022831C(ctx); DRAIN_TRAMPOLINE(ctx);\n"
    "        { static int _on2=-1; if(_on2<0){ extern char* getenv(const char*);\n"
    "            const char* _e=getenv(\"PS3_TRACE_LAZYINIT\"); _on2=(_e&&*_e&&*_e!='0')?1:0; }\n"
    "          if(_on2){ fprintf(stderr,\"[LAZYINIT] ..init CORREU: mgr=0x%08X flag=0x%08X pool0=0x%08X\\n\",\n"
    "              (uint32_t)ctx->gpr[31], vm_read32((uint32_t)ctx->gpr[31] + 0x12Cu),\n"
    "              vm_read32((uint32_t)ctx->gpr[31] + 0xD0u)); fflush(stderr); } }\n"
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
            ja += 1
            print("JA-APLICADO  %s" % os.path.basename(f))
        elif estado == "ok":
            with open(f, "w", newline="\n", encoding="utf-8") as fh:
                fh.write(novo)
            aplicados += 1
            print("CONVERTIDO   %s" % os.path.basename(f))

    if aplicados == 0 and ja == 0:
        print("SEM-EFEITO: agulha da init preguicosa nao casou em %s" % lift)
        return 2

    print("patch_212ed0_lazyinit_probe: aplicados=%d ja=%d" % (aplicados, ja))
    return 0


if __name__ == "__main__":
    sys.exit(main())
