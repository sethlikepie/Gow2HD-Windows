#!/usr/bin/env python3
"""Sonda das VITIMAS: quem leva o indice de classe a zero em `func_0024E3D0`.

Porque existe (E58)
-------------------
O lookup por nome ACERTA. `FUN_0024f24c("goGrapplePtActive")` devolve um objecto
de heap plausivel (`0x4077AC10` / `0x4077AC50` em duas corridas), o B71 recebe-o
e segue para o passo #42. A cadeia nao e' "o lookup falha" -- e':

  a travessia completa-se com sucesso APARENTE e estraga outras coisas pelo
  caminho.

O sitio do estrago, no lift:

    ctx->gpr[9] = ppc_rldicl(ctx->gpr[25], 0, 32);   // o objecto VITIMA
    ctx->gpr[3] = ppc_rldicl(ctx->gpr[3], 0, 32);    // o PRODUTO (0 quando vazio)
    ctx->gpr[0] = vm_read32(ctx->gpr[3] + 0x20);     // *(NULL+0x20) -> le' 0
    vm_write16(ctx->gpr[9] + 0x6, ctx->gpr[0]);      // escreve 0 no indice

Com produto NULL isto poe o indice de classe da vitima a zero, e e' esse zero
que mais tarde da' o pool 0, o "rm_." como endereco e as 16 escritas em memoria
nao commitada de `func_00220284`.

O que esta sonda responde
-------------------------
Quais os objectos estragados, e se algum deles e' o `0x40780850` que o E41 mediu
partido. Isso liga a vitima medida em cima a' vitima medida em baixo, que ate'
agora sao os dois extremos de nove niveis sem uma ponte directa.

Imprime SEMPRE (nao so' quando o produto e' nulo), para se ver a proporcao entre
escritas sas e escritas com produto nulo no mesmo sitio -- uma sonda que so'
regista o caso mau nao consegue dizer se ele e' raro ou e' a regra, e ja' me
custou uma leitura errada nesta cadeia (E45).

  vitima   o objecto que leva a escrita (r25)
  produto  o retorno do vt[0x48]; 0 = fabrica vazia
  valor    o que fica no indice de classe
  ra1/ra2  chamador real de host (o ctx->lr NAO serve numa vcall -- E50)

Gate: `PS3_TRACE_VICTIM` (vazio ou "0" = OFF, default). Cap por
`PS3_TRACE_VICTIM_CAP` (default 400, `-1` = ilimitado). Read-only.

Uso:  patch_24e3d0_victim_probe.py [DIR_DE_LIFT]
rc: 0 aplicado/ja aplicado; 2 se a agulha nao casou.
"""
import os
import sys
import glob

MARKER = "VICTIM-PROBE"

# Medido antes de escrever: esta sequencia de duas linhas aparece UMA vez no
# lift inteiro (ppu_recomp_000.cpp).
NEEDLE = (
    "        ctx->gpr[0] = vm_read32(ctx->gpr[3] + 0x20);\n"
    "        vm_write16(ctx->gpr[9] + 0x6, ctx->gpr[0]);\n"
)

BLOCK = (
    "        /* " + MARKER + ": quem leva o indice de classe a zero */\n"
    "        { static int _on=-1; if(_on<0){ extern char* getenv(const char*);\n"
    "            const char* _e=getenv(\"PS3_TRACE_VICTIM\");\n"
    "            _on=(_e&&*_e&&*_e!='0')?1:0; }\n"
    "          static int _cap=-2; if(_cap==-2){ extern char* getenv(const char*);\n"
    "            const char* _c=getenv(\"PS3_TRACE_VICTIM_CAP\");\n"
    "            _cap=(_c&&*_c)?atoi(_c):400; }\n"
    "          if(_on){ static int _n=0; if(_cap<0 || _n++<_cap){\n"
    "            unsigned long ps3_dbg_tid(void);\n"
    "            const char* ps3_dbg_sym(void*);\n"
    "            uint32_t _prod=(uint32_t)ctx->gpr[3];\n"
    "            void* _r1=__builtin_return_address(1);\n"
    "            void* _r2=__builtin_return_address(2);\n"
    "            fprintf(stderr,\"[VICTIM] tid=%lu vitima=0x%08X produto=0x%08X \"\n"
    "              \"valor=0x%04X %s ra1=%s ra2=%s\\n\", ps3_dbg_tid(),\n"
    "              (uint32_t)ctx->gpr[9], _prod, (unsigned)(ctx->gpr[0] & 0xFFFFu),\n"
    "              _prod ? \"ok\" : \"PRODUTO-NULO\",\n"
    "              ps3_dbg_sym(_r1), ps3_dbg_sym(_r2));\n"
    "            fflush(stderr); } } }\n"
)

REPL = NEEDLE + BLOCK


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
        print("MISSING  agulha da escrita do indice de classe nao encontrada",
              file=sys.stderr)
        return 2
    print("patch_24e3d0_victim_probe: applied=%d already=%d sites=%d"
          % (applied, already, sites))
    return 0


if __name__ == "__main__":
    sys.exit(main())
