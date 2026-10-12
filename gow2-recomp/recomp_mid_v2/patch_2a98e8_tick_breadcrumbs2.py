#!/usr/bin/env python3
"""Migalhas de entrada/saida nas chamadas do tick de audio `func_002A98E8`.

Porque existe
-------------
Cadeia medida a 2026-08-08, do loop principal ate' aqui:

    [STATE] frame=1 ... code 0x002B2DD0
    [B71] func_002B2DD0 #001 -> func_002B2660          (#002 e #003 nunca)
    [B71] func_002B2660 #001 -> ps3_indirect_call ctr=0x004244C0
    [LISTWALK] #1..#15  travessia dos gestores (NAO e' infinita)
    [B71] func_00423AB4 #001 -> ps3_indirect_call r3=0x401002F0 ctr=0x002A98E8
    ... e nada mais do tick.

`func_002A98E8` e' o **tick de audio**: um `do{}while(true)` sobre 15 canais
(`uVar17 == 0xe` termina) com fades de volume, e chamadas com o id `0x534D5044`
= **"SMPD"**. O ciclo TERMINA por construcao, logo o bloqueio esta' numa das
chamadas — e nenhum dos fragmentos tem instrumentacao.

Estas migalhas imprimem ENTRADA e SAIDA de cada candidato. A ultima entrada sem
saida correspondente nomeia o culpado, e custa uma corrida.

Candidatos (lidos do decompilado de `FUN_002a98e8`):
  func_0043FF30  despachante "SMPD" -- chamado 3x no epilogo; principal suspeito
  func_0043F010  volume por canal, ate' 15x dentro do ciclo
  func_002A6A94  func_0032988C  func_00448548  func_00429748
  func_0042973C  func_002A60E8  func_002A9220

Gate: `PS3_TRACE_TICK` (OFF por default). Sem cap: sao poucas chamadas e a
ausencia de uma saida e' precisamente o que se procura -- um cap aqui esconderia
a resposta, que foi o erro dos ledgers E63/E64.

Uso:  patch_2a98e8_tick_breadcrumbs.py [DIR_DE_LIFT]
rc: 0 aplicado/ja aplicado; 2 se nenhum alvo casou.
"""
import os
import re
import sys
import glob

MARKER = "TICK-BREADCRUMB2"

ALVOS = [
    "func_0048B1C8", "func_00457DBC", "func_00457E44", "func_00448548",
    "func_0043FF30", "func_0043F010", "func_002A6A94", "func_0032988C",
    "func_00448548", "func_00429748", "func_0042973C", "func_002A60E8",
    "func_002A9220",
]


def entrada(fn):
    return (
        '        /* ' + MARKER + ' */\n'
        '        { static int _on=-1; if(_on<0){ extern char* getenv(const char*);\n'
        '            const char* _e=getenv("PS3_TRACE_TICK");\n'
        '            _on=(_e&&*_e&&*_e!=\'0\')?1:0; }\n'
        '          if(_on){ fprintf(stderr,"[TICK] entra ' + fn + ' r3=0x%08X\\n",\n'
        '            (uint32_t)ctx->gpr[3]); fflush(stderr); } }\n'
    )


def patch_text(src):
    if MARKER in src:
        return src, "ALREADY", 0
    n = 0
    for fn in ALVOS:
        pat = "void " + fn + "(ppu_context* ctx) {\n"
        if pat in src:
            src = src.replace(pat, pat + entrada(fn), 1)
            n += 1
    if not n:
        return src, "MISSING", 0
    return src, "APPLIED", n


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
            print("APPLIED  %-20s alvos=%d" % (os.path.basename(path), n))
        elif state == "ALREADY":
            already += 1
    if not applied and not already:
        print("MISSING  nenhum dos %d alvos encontrado" % len(ALVOS), file=sys.stderr)
        return 2
    if already and not applied:
        print("ALREADY  (%d chunk(s))" % already)
        return 0
    print("patch_2a98e8_tick_breadcrumbs: applied=%d alvos=%d de %d"
          % (applied, sites, len(ALVOS)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
