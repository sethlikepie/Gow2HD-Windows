#!/usr/bin/env python3
"""Sonda do POP da free-list (`func_00263554`) — nomeia o pool cuja cabeça é texto.

A cadeia até aqui (tudo medido a 2026-08-01)
-------------------------------------------
Com o gate de diagnóstico `PS3_LIST254_EMPTY_IF_NULL=1` provou-se, 3/3, que
saltar a parede do `func_002545D4` **não desbloqueia** (o `FATAL` some, o
`thr_auto_load` continua a 0). O log apontou o que está a montante — e com o
`ra` do `[vm] UNCOMMITTED` simbolizado (ps3recomp `8ead924`) ficou nomeado:

    [vm] UNCOMMITTED read32  access 0x726D432E ra=func_00263554+0x1CC
    [vm] UNCOMMITTED write32 access 0x726D4332 ra=func_00220284+0xF78
    ...  16 escritas de func_00220284, endereços a incrementar de 8 em 8

`0x726D432E` é ASCII `rmC.` — texto usado como ponteiro.

E `func_00263554`, lido do lift, é o **pop** de uma free-list:

    r31 = r3                    // o pool
    r3  = *(r31 + 4)            // head
    if (r3 == 0) -> vazia
    r0  = *(r3 + 0)             // next   <- o UNCOMMITTED cai AQUI
    *(r31 + 4) = r0             // head = next
    return r3                   // entrega o bloco

Como o `read32` falhado é `*(r3+0)`, o `r3` — ou seja **a própria cabeça da
free-list** — vale `0x726D432E`. O pool entrega uma string como bloco, e o
`func_00220284` escreve 16 words através dela.

O que esta sonda mede
---------------------
No pop, e **só quando a cabeça não é um ponteiro plausível**: o endereço do
pool (`r3` à entrada) e o valor da cabeça. Isso dá o `pool+4` concreto para se
lhe apontar um `PS3_WATCH_STORE` na corrida seguinte e nomear quem lá pôs o
texto — o mesmo dois-passos que resolveu o stomp do `F2B-STREAM-PUMP`.

`PS3_TRACE_FLHEAD=all` regista todos os pops (com cap), para se ver a
distribuição de pools e apanhar o instante em que um deles vira texto.

Gate: `PS3_TRACE_FLHEAD` (vazio ou "0" = OFF, default). Cap por
`PS3_TRACE_FLHEAD_CAP` (default 60).

A agulha repete-se entre chunks (fragmentos duplicados); aplica-se a todas —
read-only.

Uso:  patch_263554_freelist_head_probe.py [DIR_DE_LIFT]
rc: 0 aplicado/já aplicado; 2 se nenhuma agulha casou.
"""
import os
import sys
import glob

MARKER = "FLHEAD-PROBE"

NEEDLE = (
    "        ctx->gpr[31] = ctx->gpr[3] | ctx->gpr[3];\n"
    "        ctx->gpr[3] = vm_read32(ctx->gpr[3] + 0x4);\n"
    "        { int64_t a = (int32_t)ctx->gpr[3]; int64_t b = (int64_t)0; uint32_t cr_val = (a < b) ? 8 : (a > b) ? 4 : 2; ctx->cr = (ctx->cr & ~(0xFu << 0)) | (cr_val << 0); }\n"
)

REPL = NEEDLE + (
    "        /* " + MARKER + ": cabeca da free-list no pop -- pool e valor */\n"
    "        { static int _on=-1; static int _all=0;\n"
    "          if(_on<0){ extern char* getenv(const char*);\n"
    "            const char* _e=getenv(\"PS3_TRACE_FLHEAD\");\n"
    "            _on=(_e&&*_e&&*_e!='0')?1:0; _all=(_e&&*_e=='a')?1:0; }\n"
    "          static int _cap=-1; if(_cap<0){ extern char* getenv(const char*);\n"
    "            const char* _c=getenv(\"PS3_TRACE_FLHEAD_CAP\"); _cap=(_c&&*_c)?atoi(_c):60; }\n"
    "          if(_on){ uint32_t _h=(uint32_t)ctx->gpr[3];\n"
    "            int _mau = (_h!=0u) && (_h<0x10000u || _h>=0x50000000u);\n"
    "            if(_all || _mau){ static int _n=0; if(_cap<0 || _n++<_cap){\n"
    "              fprintf(stderr,\"[FLHEAD] %s pool=0x%08X pool+4=0x%08X head=0x%08X lr=0x%08X\\n\",\n"
    "                _mau?\"TEXTO\":\"ok   \", (uint32_t)ctx->gpr[31],\n"
    "                (uint32_t)(ctx->gpr[31]+4u), _h, (uint32_t)ctx->lr);\n"
    "              /* O ctx->lr do guest fica CONGELADO no ultimo `bl`: num\n"
    "               * despacho indirecto aponta para o chamador errado. Medido\n"
    "               * 2026-08-01: o lr dizia 0x00218364 e a sonda nesse sitio\n"
    "               * (unico no lift) nunca viu o pool mau. A cadeia HOST nao\n"
    "               * mente -- e' a mesma tecnica do ICALL-BAD simbolizado. */\n"
    "              { struct { const char* fn;\n"
    "                  void* fb; const char* sn; void* sa; } _di;\n"
    "                void* _ra[3] = { __builtin_return_address(0),\n"
    "                                 __builtin_return_address(1),\n"
    "                                 __builtin_return_address(2) };\n"
    "                for(int _i=0;_i<3;_i++){\n"
    "                  if(_ra[_i] && dladdr(_ra[_i], &_di) && _di.sn)\n"
    "                    fprintf(stderr,\"          host#%d %s+0x%lX\\n\", _i, _di.sn,\n"
    "                      (unsigned long)((char*)_ra[_i]-(char*)_di.sa));\n"
    "                } }\n"
    "              fflush(stderr); } } } }\n"
)


def patch_text(t):
    if MARKER in t:
        return t, "ALREADY", 0
    n = t.count(NEEDLE)
    if not n:
        return t, "MISSING", 0
    # `extern "C"` so' e' valido em ambito de namespace, nunca dentro de uma
    # funcao -- daqui a declaracao no topo do ficheiro, uma vez por chunk.
    DECL = 'extern "C" int dladdr(const void*, void*);\n'
    out = t.replace(NEEDLE, REPL)
    if DECL not in out:
        out = DECL + out
    return out, "APPLIED", n


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
        print("MISSING  agulha do pop da free-list nao encontrada", file=sys.stderr)
        return 2
    print("patch_263554_freelist_head_probe: applied=%d already=%d sites=%d"
          % (applied, already, sites))
    return 0


if __name__ == "__main__":
    sys.exit(main())
