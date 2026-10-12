#!/usr/bin/env python3
"""Sonda do pop em `func_00212ED0` -- o pool que entra e o valor que sai.

Porque existe (E28, 2026-08-05)
-------------------------------
A sonda herdada `FLHEAD` imprimiu:

    [FLHEAD] ok pool=0x00000000 pool+4=0x00000004 head=0x2F725F70 lr=0x00212F4C

e essa linha **nao fecha**. Com `pool=0`, `head = vm_read32(4)`, e `vm_read32`
ou devolve 0 sem traduzir (endereco nao commitado) ou traduz. A vigia de
traducao (`PS3_WATCH_LOWPTR=0x1000`), com instrumento verificado, da' ZERO
traducoes abaixo de 0x1000 na mesma corrida. Logo `head` seria 0.

A causa e' que existem **16 marcadores FLHEAD** no lift: os rotulos dos campos
sao promessas sobre UM sitio, e eu li o codigo de um e atribui-o as linhas de
todos. Ver docs/re_sessions/2026-08-05-E28-a-linha-que-nao-fecha.md.

Esta sonda evita isso por construcao: e' instalada num **unico sitio**, o da
chamada ao pop dentro de `func_00212ED0`, e captura os dois lados da mesma
chamada -- sem depender de nenhum marcador herdado.

O que mede
----------
Uma linha por pop, no sitio:

    pool  = r3 ANTES da chamada   (o argumento; vem de *(r31 + 0xD0) com r30=0)
    r31   = o gestor
    r9    = o deslocamento calculado (esperado 0xD0)
    ret   = r3 DEPOIS da chamada  (o objecto "alocado")

Com os dois lados na mesma linha decide-se se o valor mau entra pelo argumento
(pool errado) ou e' produzido dentro do pop.

Gate: `PS3_TRACE_POP212` (vazio ou "0" = OFF, default). Cap por
`PS3_TRACE_POP212_CAP` (default 400).

Uso:  patch_212ed0_pop_probe.py [DIR_DE_LIFT]
rc: 0 aplicado ou ja' aplicado; 2 se a agulha nao casou (SEM-EFEITO).
"""
import os
import sys
import glob

MARKER = "POP212-PROBE-V2"

# 4 linhas, verificado unico no lift a 2026-08-05. A linha do +0xD0 e' o que a
# distingue das outras chamadas a func_00263554.
NEEDLE = (
    "        ctx->gpr[9] = ctx->gpr[9] + (int64_t)(0xD0);\n"
    "        ctx->gpr[9] = (int64_t)(int32_t)ctx->gpr[9];\n"
    "        ctx->gpr[3] = vm_read32((ctx->gpr[31] + ctx->gpr[9]));\n"
    "        ctx->lr = 0x00212F4C; func_00263554(ctx); DRAIN_TRAMPOLINE(ctx);\n"
)

REPL = (
    "        ctx->gpr[9] = ctx->gpr[9] + (int64_t)(0xD0);\n"
    "        ctx->gpr[9] = (int64_t)(int32_t)ctx->gpr[9];\n"
    "        ctx->gpr[3] = vm_read32((ctx->gpr[31] + ctx->gpr[9]));\n"
    "        /* " + MARKER + ": os dois lados da MESMA chamada ao pop. Sitio\n"
    "         * unico, sonda propria -- ver E28 para porque nao se usa a FLHEAD. */\n"
    "        uint32_t _p212_pool = (uint32_t)ctx->gpr[3];\n"
    "        uint32_t _p212_mgr  = (uint32_t)ctx->gpr[31];\n"
    "        uint32_t _p212_off  = (uint32_t)ctx->gpr[9];\n"
    "        ctx->lr = 0x00212F4C; func_00263554(ctx); DRAIN_TRAMPOLINE(ctx);\n"
    "        { static int _on=-1; if(_on<0){ extern char* getenv(const char*);\n"
    "            const char* _e=getenv(\"PS3_TRACE_POP212\"); _on=(_e&&*_e&&*_e!='0')?1:0; }\n"
    "          static int _cap=-1; if(_cap<0){ extern char* getenv(const char*);\n"
    "            const char* _c=getenv(\"PS3_TRACE_POP212_CAP\"); _cap=(_c&&*_c)?atoi(_c):400; }\n"
    "          if(_on){ static int _n=0; if(_cap<0 || _n++<_cap){\n"
    "            uint32_t _r=(uint32_t)ctx->gpr[3];\n"
    "            char _a[5]; for(int _b=0;_b<4;_b++){ unsigned _c2=(_r>>(8*(3-_b)))&0xFFu;\n"
    "              _a[_b]=(_c2>=32&&_c2<127)?(char)_c2:'.'; } _a[4]='\\0';\n"
    "            fprintf(stderr,\"[POP212] #%d mgr=0x%08X off=0x%X pool=0x%08X -> ret=0x%08X \\\"%s\\\"%s\\n\",\n"
    "              _n, _p212_mgr, _p212_off, _p212_pool, _r, _a,\n"
    "              (_r==0x2F725F70u)?\" <ENVENENADO>\":\"\");\n"
    "            if(_p212_pool==0u){ fprintf(stderr,\"[POP212] ..pool NULO: vm_read32(4)=0x%08X vm_read32(0)=0x%08X\\n\",\n"
    "                vm_read32(4u), vm_read32(0u)); }\n"
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
            ja += 1
            print("JA-APLICADO  %s" % os.path.basename(f))
        elif estado == "ok":
            with open(f, "w", newline="\n", encoding="utf-8") as fh:
                fh.write(novo)
            aplicados += 1
            print("CONVERTIDO   %s" % os.path.basename(f))

    if aplicados == 0 and ja == 0:
        print("SEM-EFEITO: agulha do pop de func_00212ED0 nao casou em %s" % lift)
        return 2

    print("patch_212ed0_pop_probe: aplicados=%d ja=%d" % (aplicados, ja))
    return 0


if __name__ == "__main__":
    sys.exit(main())
