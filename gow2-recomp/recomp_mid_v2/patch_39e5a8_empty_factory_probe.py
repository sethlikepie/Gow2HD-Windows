#!/usr/bin/env python3
"""Sonda do cursor da fabrica ANTES do ramo -- ve' o caso vazio, que a sonda
`PRODUCT` nao consegue ver.

Porque existe
-------------
`func_0039E5A8` e' o `vt[0x48]` das fabricas: devolve o produto no topo de uma
pilha cujo cursor vive em `this+0xC8` (= `this+0x48+0x80`). Lido no lift:

    ctx->gpr[10] = 0;                                    // retorno default
    ctx->gpr[0]  = (int8_t)vm_read8(ctx->gpr[3] + 0x80); // cursor COM SINAL
    ...compara com 0...
    if (((ctx->cr >> 0) & 8)) goto loc_0039E5D0;         // < 0 -> salta a leitura
    ctx->gpr[10] = vm_read32(base + cursor*4);
  loc_0039E5D0:
    ctx->gpr[3] = ctx->gpr[10];                          // devolve 0 se vazio

Devolver NULL com a pilha vazia e' o CONTRATO, nao uma falha (E44). Quem nao
guarda e' o consumidor `func_0024E3D0`, que faz `vm_read32(r3 + 0x20)` na
instrucao a seguir a' chamada, sem check.

A divida que esta sonda paga
----------------------------
A sonda `PRODUCT-PROBE` que ja' existe neste sitio foi escrita DEPOIS do
`goto loc_0039E5D0`, logo vive dentro do ramo nao-negativo: **o caso que
interessa salta-a por construcao.** O E44 concluiu «3 produtos nulos em 3
chamadas» por AUSENCIA de linhas `[PRODUCT]`, com controlo positivo (as mesmas
fabricas aparecem mais cedo na mesma corrida, tecto nao esgotado, sintoma
`UNCOMMITTED` presente) -- mas por ausencia, nao por observacao directa.

Esta sonda fica ANTES do ramo e imprime SEMPRE. Serve para converter essa
inferencia em medicao, ou para a refutar.

O que mede
----------
  fab     o `this` da fabrica (r3 antes do +0x48)
  cursor  o valor com sinal em `this+0xC8`
  estado  VAZIO se negativo (vai devolver 0), ok caso contrario
  lr      pista do chamador -- pode estar velho em vcalls, tratar como pista

Predicao a bater (E44): as chamadas vindas de `func_0024E3D0` para as fabricas
`0x400C6210`, `0x403008E8` e `0x40300E80` devem sair `VAZIO`. Se sairem `ok`, o
E44 cai e a ausencia media tinha outra causa.

Gate: `PS3_TRACE_EMPTYFAB` (vazio ou "0" = OFF, default). Cap por
`PS3_TRACE_EMPTYFAB_CAP` (default 400, `-1` = ilimitado). Read-only, no-op
no baseline.

Uso:  patch_39e5a8_empty_factory_probe.py [DIR_DE_LIFT]
rc: 0 aplicado/ja aplicado; 2 se a agulha nao casou.
"""
import os
import sys
import glob

MARKER = "EMPTYFAB-PROBE-V4"
MARKER_V1 = "EMPTYFAB-PROBE:"

# `loc_0039E5D0` e' unico no lift inteiro (label = endereco guest), logo a
# agulha nao pode casar com func_0039DA78/func_0039DAB0, que tem a mesma forma
# de leitura do cursor.
NEEDLE = (
    "        ctx->gpr[9] = (int64_t)(int32_t)ctx->gpr[9];\n"
    "        if (((ctx->cr >> 0) & 8)) goto loc_0039E5D0;\n"
)

# Bloco da v1, sem `tid`. Fica aqui so' para poder ser SUBSTITUIDO: o lift nao
# e' versionado, logo uma arvore que ja' levou a v1 tem de poder subir para a
# v2 sem passar por um re-lift.
BLOCK_V1 = (
    "        /* EMPTYFAB-PROBE: cursor da fabrica ANTES do ramo -- ve' o vazio */\n"
    "        { static int _on=-1; if(_on<0){ extern char* getenv(const char*);\n"
    "            const char* _e=getenv(\"PS3_TRACE_EMPTYFAB\");\n"
    "            _on=(_e&&*_e&&*_e!='0')?1:0; }\n"
    "          static int _cap=-2; if(_cap==-2){ extern char* getenv(const char*);\n"
    "            const char* _c=getenv(\"PS3_TRACE_EMPTYFAB_CAP\");\n"
    "            _cap=(_c&&*_c)?atoi(_c):400; }\n"
    "          if(_on){ static int _n=0; if(_cap<0 || _n++<_cap){\n"
    "            int _cur=(int)(int32_t)ctx->gpr[0];\n"
    "            fprintf(stderr,\"[EMPTYFAB] fab=0x%08X cursor=%d %s lr=0x%08X\\n\",\n"
    "              (uint32_t)ctx->gpr[3]-0x48u, _cur,\n"
    "              (_cur<0)?\"VAZIO->devolve-0\":\"ok\",\n"
    "              (uint32_t)ctx->lr);\n"
    "            fflush(stderr); } } }\n"
)

# Bloco da v2: tem `tid` mas identifica o chamador por `ctx->lr`, que o E50
# provou ser FALSO numa vcall (o lifter so' escreve lr em chamadas directas).
# Fica aqui so' para ser substituido.
BLOCK_V2 = (
    "        /* EMPTYFAB-PROBE-V2: cursor da fabrica ANTES do ramo -- ve' o vazio.\n"
    "         * O tid separa corrida entre fios de ordem de programa (E46). */\n"
    "        { static int _on=-1; if(_on<0){ extern char* getenv(const char*);\n"
    "            const char* _e=getenv(\"PS3_TRACE_EMPTYFAB\");\n"
    "            _on=(_e&&*_e&&*_e!='0')?1:0; }\n"
    "          static int _cap=-2; if(_cap==-2){ extern char* getenv(const char*);\n"
    "            const char* _c=getenv(\"PS3_TRACE_EMPTYFAB_CAP\");\n"
    "            _cap=(_c&&*_c)?atoi(_c):400; }\n"
    "          if(_on){ static int _n=0; if(_cap<0 || _n++<_cap){\n"
    "            unsigned long ps3_dbg_tid(void);\n"
    "            int _cur=(int)(int32_t)ctx->gpr[0];\n"
    "            fprintf(stderr,\"[EMPTYFAB] tid=%lu fab=0x%08X cursor=%d %s lr=0x%08X\\n\",\n"
    "              ps3_dbg_tid(), (uint32_t)ctx->gpr[3]-0x48u, _cur,\n"
    "              (_cur<0)?\"VAZIO->devolve-0\":\"ok\",\n"
    "              (uint32_t)ctx->lr);\n"
    "            fflush(stderr); } } }\n"
)

BLOCK_V3 = (
    "        /* EMPTYFAB-PROBE-V3: cursor da fabrica ANTES do ramo -- ve' o vazio.\n"
    "         * ra0/ra1 sao os enderecos de retorno do HOST resolvidos por dladdr:\n"
    "         * o ctx->lr NAO serve para identificar o chamador numa vcall, porque\n"
    "         * o lifter so' o escreve em chamadas directas (E50). Fica impresso\n"
    "         * na mesma, ao lado, para se ver a divergencia. */\n"
    "        { static int _on=-1; if(_on<0){ extern char* getenv(const char*);\n"
    "            const char* _e=getenv(\"PS3_TRACE_EMPTYFAB\");\n"
    "            _on=(_e&&*_e&&*_e!='0')?1:0; }\n"
    "          static int _cap=-2; if(_cap==-2){ extern char* getenv(const char*);\n"
    "            const char* _c=getenv(\"PS3_TRACE_EMPTYFAB_CAP\");\n"
    "            _cap=(_c&&*_c)?atoi(_c):400; }\n"
    "          if(_on){ static int _n=0; if(_cap<0 || _n++<_cap){\n"
    # SEM `extern \"C\"`: especificacao de ligacao e' ilegal em escopo de bloco.
    # ps3_dbg_tid/ps3_dbg_sym sao C++ puro no ppu_loader.cpp, logo o mangled bate.
    "            unsigned long ps3_dbg_tid(void);\n"
    "            const char* ps3_dbg_sym(void*);\n"
    "            int _cur=(int)(int32_t)ctx->gpr[0];\n"
    "            void* _r1=__builtin_return_address(1);\n"
    "            void* _r2=__builtin_return_address(2);\n"
    "            fprintf(stderr,\"[EMPTYFAB] tid=%lu fab=0x%08X cursor=%d %s \"\n"
    "              \"ra1=%s ra2=%s lr=0x%08X\\n\",\n"
    "              ps3_dbg_tid(), (uint32_t)ctx->gpr[3]-0x48u, _cur,\n"
    "              (_cur<0)?\"VAZIO->devolve-0\":\"ok\",\n"
    "              ps3_dbg_sym(_r1), ps3_dbg_sym(_r2),\n"
    "              (uint32_t)ctx->lr);\n"
    "            fflush(stderr); } } }\n"
)


BLOCK = (
    "        /* " + MARKER + ": cursor da fabrica ANTES do ramo -- ve' o vazio.\n"
    "         * ra0/ra1 sao os enderecos de retorno do HOST resolvidos por dladdr:\n"
    "         * o ctx->lr NAO serve para identificar o chamador numa vcall, porque\n"
    "         * o lifter so' o escreve em chamadas directas (E50). Fica impresso\n"
    "         * na mesma, ao lado, para se ver a divergencia. */\n"
    "        { static int _on=-1; if(_on<0){ extern char* getenv(const char*);\n"
    "            const char* _e=getenv(\"PS3_TRACE_EMPTYFAB\");\n"
    "            _on=(_e&&*_e&&*_e!='0')?1:0; }\n"
    "          static int _cap=-2; if(_cap==-2){ extern char* getenv(const char*);\n"
    "            const char* _c=getenv(\"PS3_TRACE_EMPTYFAB_CAP\");\n"
    "            _cap=(_c&&*_c)?atoi(_c):400; }\n"
    "          if(_on){ static int _n=0; if(_cap<0 || _n++<_cap){\n"
    # SEM `extern \"C\"`: especificacao de ligacao e' ilegal em escopo de bloco.
    # ps3_dbg_tid/ps3_dbg_sym sao C++ puro no ppu_loader.cpp, logo o mangled bate.
    "            unsigned long ps3_dbg_tid(void);\n"
    "            const char* ps3_dbg_sym(void*);\n"
    "            int _cur=(int)(int32_t)ctx->gpr[0];\n"
    "            void* _r1=__builtin_return_address(1);\n"
    "            void* _r2=__builtin_return_address(2);\n"
    "            void* _r3=__builtin_return_address(3);\n"
    "            void* _r4=__builtin_return_address(4);\n"
    "            fprintf(stderr,\"[EMPTYFAB] tid=%lu fab=0x%08X cursor=%d %s \"\n"
    "              \"ra1=%s ra2=%s lr=0x%08X\\n\",\n"
    "              ps3_dbg_tid(), (uint32_t)ctx->gpr[3]-0x48u, _cur,\n"
    "              (_cur<0)?\"VAZIO->devolve-0\":\"ok\",\n"
    "              ps3_dbg_sym(_r1), ps3_dbg_sym(_r2),\n"
    "              (uint32_t)ctx->lr);\n"
    "            if(_cur<0) fprintf(stderr,\"[EMPTYFAB+] ra3=%s ra4=%s\\n\",\n"
    "              ps3_dbg_sym(_r3), ps3_dbg_sym(_r4));\n"
    "            fflush(stderr); } } }\n"
)

REPL = (
    "        ctx->gpr[9] = (int64_t)(int32_t)ctx->gpr[9];\n"
    + BLOCK +
    "        if (((ctx->cr >> 0) & 8)) goto loc_0039E5D0;\n"
)


def patch_text(t):
    if MARKER in t:
        return t, "ALREADY", 0
    for older in (BLOCK_V3, BLOCK_V2, BLOCK_V1):     # upgrade sem re-lift
        if older in t:
            return t.replace(older, BLOCK), "APPLIED", t.count(older)
    if MARKER_V1 in t or "EMPTYFAB-PROBE-V2" in t or "EMPTYFAB-PROBE-V3" in t:
        # versao antiga presente mas com outro corpo -- nao adivinhar, parar.
        return t, "MISSING", 0
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
        print("MISSING  agulha do ramo de loc_0039E5D0 nao encontrada", file=sys.stderr)
        return 2
    print("patch_39e5a8_empty_factory_probe: applied=%d already=%d sites=%d"
          % (applied, already, sites))
    return 0


if __name__ == "__main__":
    sys.exit(main())
