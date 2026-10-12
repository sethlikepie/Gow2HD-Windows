#!/usr/bin/env python3
"""Sonda da entrada NULA na tabela de produtos (`func_0039E6B4`) — o despacho
virtual sobre um objecto zero.

O que se mede, e porque este sitio
----------------------------------
Corrida natural (sem gate nenhum), `PS3_TRACE_OPD_BAD=1 ..._CAP=-1`:

    13x  ps3_call_opd(opd = 0x00000000)  de func_0039E6B4+0x760
     1x  ps3_call_opd(opd = 0x00000000)  de func_002B0AA8+0x3B8

`func_0039E6B4` faz duas chamadas virtuais. A segunda, lida do lift:

    r0  = *(self + 0x44)                 // contador
    r9  = *(self + 0x24) + r0*12         // ranhura
    r0  = *(r9 + 0)                      // tabela
    r11 = 0
    r3  = *(r0 + idx*4)                  // ENTRADA   <- idx vem da 1a chamada
    if (r3 != 0) r11 = r3 - 4
    r3  = r11
    r9  = *(r11 + 0)                     // vtable do produto
    r10 = *(r9 + 0x28)                   // opd
    ps3_call_opd(r10)

Quando a entrada e' 0, o `r11` fica em 0 e o codigo le' a vtable do endereco
guest 0 e despacha `*(0x28)`. Dai o `opd=0x00000000`. **O defeito nao esta no
despacho: esta na tabela, que devolve uma entrada vazia.**

Porque a sonda `[WADLD-FACT]` que ja' la' estava nao servia
-----------------------------------------------------------
Tem `n++<16` **hard-coded**. As 16 primeiras passagens sao todas saudaveis
(`obj` valido, `opd` valido, `code` valido) e as 13 mas' acontecem depois do
cap. A sonda mostrava so' a parte boa da amostra.

E' a **terceira vez** hoje que um cap fixo esconde exactamente a amostra que
interessa (antes: `PS3_WATCH_STORE` a 300 e `PS3_TRACE_ICALL_TO` a 64, ambos
ja' passados a env var). Esta sonda nasce sem cap fixo: `PS3_TRACE_E6B4_CAP`,
com `-1` = sem limite.

O que imprime
-------------
So' quando a entrada e' implausivel (0, ou fora da janela guest) -- ou tudo,
com `PS3_TRACE_E6B4=all`:

    self, vt(self), count=*(self+0x44), base=*(self+0x24),
    slot, table, idx, entry

Com isto sabe-se, sem inferencia, se a tabela e' pequena de mais (idx fora de
alcance) ou se e' do tamanho certo mas tem buracos (registo nunca populado) --
que sao dois defeitos diferentes com fixes diferentes.

Gate: `PS3_TRACE_E6B4` (vazio ou "0" = OFF, default). Read-only: nao muda
nenhum registo nem nenhuma memoria.

Uso:  patch_39e6b4_null_entry_probe.py [DIR_DE_LIFT]
rc: 0 aplicado/ja aplicado; 2 se nenhuma agulha casou.
"""
import os
import sys
import glob

MARKER = "E6B4-NULL-ENTRY-PROBE"

NEEDLE = (
    "        ctx->gpr[9] = ctx->gpr[9] + ctx->gpr[11];\n"
    "        ctx->gpr[11] = (int64_t)(int32_t)(0);\n"
    "        ctx->gpr[9] = ppc_rldicl(ctx->gpr[9], 0, 32);\n"
    "        ctx->gpr[0] = vm_read32(ctx->gpr[9] + 0x0);\n"
    "        ctx->gpr[3] = ctx->gpr[3] + ctx->gpr[0];\n"
)

REPL = (
    "        ctx->gpr[9] = ctx->gpr[9] + ctx->gpr[11];\n"
    "        ctx->gpr[11] = (int64_t)(int32_t)(0);\n"
    "        ctx->gpr[9] = ppc_rldicl(ctx->gpr[9], 0, 32);\n"
    "        ctx->gpr[0] = vm_read32(ctx->gpr[9] + 0x0);\n"
    "        /* " + MARKER + ": a tabela de produtos devolveu uma entrada vazia?\n"
    "         * read-only; sem cap fixo (aprendido a custa de tres sondas cegas). */\n"
    "        { static int _on=-1; static int _all=0;\n"
    "          if(_on<0){ extern char* getenv(const char*);\n"
    "            const char* _e=getenv(\"PS3_TRACE_E6B4\");\n"
    "            _on=(_e&&*_e&&*_e!='0')?1:0; _all=(_e&&*_e=='a')?1:0; }\n"
    "          static int _cap=-2; if(_cap==-2){ extern char* getenv(const char*);\n"
    "            const char* _c=getenv(\"PS3_TRACE_E6B4_CAP\"); _cap=(_c&&*_c)?atoi(_c):200; }\n"
    "          if(_on){ uint32_t _self=(uint32_t)ctx->gpr[29];\n"
    "            uint32_t _slot=(uint32_t)ctx->gpr[9];\n"
    "            uint32_t _tab =(uint32_t)ctx->gpr[0];\n"
    "            uint32_t _idx4=(uint32_t)ctx->gpr[3];\n"
    "            uint32_t _ea  =_tab+_idx4;\n"
    "            uint32_t _ent =(_ea>=0x10000u&&_ea<0x4F000000u)?vm_read32(_ea):0xFFFFFFFFu;\n"
    "            int _mau=(_ent==0u)||(_ent!=0xFFFFFFFFu&&(_ent<0x10000u||_ent>=0x4F000000u));\n"
    "            if(_all||_mau){ static int _n=0; if(_cap<0||_n++<_cap){\n"
    "              uint32_t _vt=(_self>=0x10000u&&_self<0x4F000000u)?vm_read32(_self):0u;\n"
    "              uint32_t _cnt=(_self>=0x10000u&&_self<0x4F000000u)?vm_read32(_self+0x44u):0u;\n"
    "              uint32_t _base=(_self>=0x10000u&&_self<0x4F000000u)?vm_read32(_self+0x24u):0u;\n"
    "              fprintf(stderr,\"[E6B4] %s self=0x%08X vt=0x%08X cnt=%u base=0x%08X \"\n"
    "                \"slot=0x%08X tab=0x%08X idx=%u ent=0x%08X\\n\",\n"
    "                _mau?\"VAZIA\":\"ok   \", _self,_vt,_cnt,_base,_slot,_tab,_idx4>>2,_ent);\n"
    "              fflush(stderr); } } } }\n"
    "        ctx->gpr[3] = ctx->gpr[3] + ctx->gpr[0];\n"
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
        print("MISSING  agulha da tabela de produtos nao encontrada", file=sys.stderr)
        return 2
    print("patch_39e6b4_null_entry_probe: applied=%d already=%d sites=%d"
          % (applied, already, sites))
    return 0


if __name__ == "__main__":
    sys.exit(main())
