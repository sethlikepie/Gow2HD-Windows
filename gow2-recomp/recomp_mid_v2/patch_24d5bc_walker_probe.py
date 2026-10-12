#!/usr/bin/env python3
"""Sonda read-only do walker de buckets em func_0024D5BC (a parede de 2026-08-01).

O que se sabe, e como
---------------------
Depois do fix do F2B-STREAM-PUMP (patch_fios_f2b_pump_ring_bounds.py) o objecto
de tipo do WAD sobrevive e o walk passa a executar codigo que antes nao corria.
O boot passou a abortar aqui, identico em 6/6 corridas do smoke_chain_gate e
tambem com o disjuntor subido para 2 000 000:

    [ppu] FATAL: stuck calling 0x00514E80 (2000 times) -- aborting run
    [ICALL-BAD] ctr=0x00514E80 lr=0x0024E2D4 r3=0x00000000 r11=0x00000000

Lido do lift, `func_0024D5BC` percorre 13 buckets (`r31` de 0 a 0x30 de 4 em 4)
e, dentro de cada um, a lista ligada que comeca em `*(r28 + r31 + 0xB8)`:

    loc_0024D6E0:                       // por no'
        r0 = *(uint16*)(r29 - 20 + 2)   // TIPO do no'
        tipo == 0x0F -> func_0024D92C
        tipo  < 0x0F -> loc_0024D6C8
        tipo == 0x15 -> func_0024D938
        tipo != 0x19 -> loc_0024D6D0    // avanca sem chamar
        // tipo == 0x19:
        r9  = r29 - 28                  // objecto
        r11 = *(r9 + 0)                 // vtable
        r10 = *(r11 + 0x10)             // OPD
        ctr = *(r10 + 0);  ps3_indirect_call
        r29 = *(r29 + 0)                // next
        if (r29 != 0) goto loc_0024D6E0

Com `r3 = r9 = r29 - 28 == 0` medido, o `r29` do no' que falha vale **0x1C** --
o proprio deslocamento do objecto escrito onde devia estar um ponteiro. E o
laco nunca sai porque o `next` desse no' se aponta a si mesmo (nao ha outra
forma de 2 000 000 iteracoes com o mesmo `ctr`).

**Isto e inferencia a partir de um unico dump de registos.** Esta sonda mede-o.

O que mede
----------
D1, na cabeca do no' (logo apos ler o tipo): bucket (`r31`), base (`r28`),
no' (`r29`), tipo, e o `next` que la esta (`*(r29+0)`). Da a lista inteira e
mostra o momento exacto em que o `next` deixa de ser um ponteiro plausivel.

D2, imediatamente antes da chamada virtual do tipo 0x19: objecto (`r29-28`),
vtable (`*(obj+0)`) e o OPD (`*(vt+0x10)`), para se ver se a vtable e' valida
(gama .data do guest, ~0x0051xxxx) ou lixo.

Gate: `PS3_TRACE_D5BC` (qualquer valor nao-vazio e != "0"). OFF por default --
zero linhas `[D5BC]` no baseline. Cap por `PS3_TRACE_D5BC_CAP` (default 300).

As agulhas aparecem mais do que uma vez por chunk (o lifter duplica fragmentos
da mesma funcao por varios chunks). Aplica-se a TODAS as ocorrencias: e' o
mesmo caminho guest, a sonda e' read-only, e assim regista seja qual for o
fragmento que corre.

Uso:  patch_24d5bc_walker_probe.py [DIR_DE_LIFT]   (default: ../recomp_macos_v2)
rc: 0 aplicado/ja aplicado; 2 se nenhuma agulha casou.
"""
import os
import sys
import glob

MARKER = "D5BC-WALKER-PROBE"

_GATE = (
    "        { static int _on=-1; if(_on<0){extern char* getenv(const char*);\n"
    "            const char* _e=getenv(\"PS3_TRACE_D5BC\"); _on=(_e&&*_e&&*_e!='0')?1:0;}\n"
    "          static int _cap=-1; if(_cap<0){extern char* getenv(const char*);\n"
    "            const char* _c=getenv(\"PS3_TRACE_D5BC_CAP\"); _cap=(_c&&*_c)?atoi(_c):300;}\n"
    "          if(_on){ static int _n=0; if(_cap<0 || _n++<_cap){\n"
)

NEEDLE_D1 = (
    "        ctx->gpr[0] = ctx->gpr[29] + (int64_t)(-20);\n"
    "        ctx->gpr[3] = ppc_rldicl(ctx->gpr[0], 0, 32);\n"
    "        ctx->gpr[0] = vm_read16(ctx->gpr[3] + 0x2);\n"
)

REPL_D1 = NEEDLE_D1 + (
    "        /* " + MARKER + "#D1: cabeca do no' -- bucket, no', tipo e o next que la esta */\n"
    + _GATE +
    "            uint32_t _no=(uint32_t)ctx->gpr[29];\n"
    "            uint32_t _nx=(_no>=0x10000u && _no<0x4F000000u)?vm_read32(_no+0x0u):0xFFFFFFFFu;\n"
    "            fprintf(stderr,\"[D5BC] D1 bucket=0x%X base=0x%08X no=0x%08X tipo=0x%X next=0x%08X\\n\",\n"
    "              (unsigned)(uint32_t)ctx->gpr[31], (uint32_t)ctx->gpr[28], _no,\n"
    "              (unsigned)(uint32_t)ctx->gpr[0], _nx);\n"
    "            fflush(stderr); } } }\n"
)

NEEDLE_D2 = (
    "        ctx->gpr[9] = ctx->gpr[29] + (int64_t)(-28);\n"
    "        ctx->gpr[9] = ppc_rldicl(ctx->gpr[9], 0, 32);\n"
    "        ctx->gpr[3] = ctx->gpr[9] | ctx->gpr[9];\n"
    "        ctx->gpr[11] = vm_read32(ctx->gpr[9] + 0x0);\n"
    "        ctx->gpr[10] = vm_read32(ctx->gpr[11] + 0x10);\n"
)

REPL_D2 = NEEDLE_D2 + (
    "        /* " + MARKER + "#D2: tipo 0x19 -- objecto, vtable e OPD antes da chamada */\n"
    + _GATE +
    "            fprintf(stderr,\"[D5BC] D2 no=0x%08X obj=0x%08X vt=0x%08X opd=0x%08X code=0x%08X\\n\",\n"
    "              (uint32_t)ctx->gpr[29], (uint32_t)ctx->gpr[9], (uint32_t)ctx->gpr[11],\n"
    "              (uint32_t)ctx->gpr[10],\n"
    "              (((uint32_t)ctx->gpr[10]>=0x10000u && (uint32_t)ctx->gpr[10]<0x4F000000u)\n"
    "                 ? vm_read32((uint32_t)ctx->gpr[10]+0x0u) : 0xFFFFFFFFu));\n"
    "            fflush(stderr); } } }\n"
)


def patch_text(t):
    if MARKER in t:
        return t, "ALREADY", 0
    n = 0
    if NEEDLE_D1 in t:
        n += t.count(NEEDLE_D1)
        t = t.replace(NEEDLE_D1, REPL_D1)
    if NEEDLE_D2 in t:
        n += t.count(NEEDLE_D2)
        t = t.replace(NEEDLE_D2, REPL_D2)
    return (t, "APPLIED", n) if n else (t, "MISSING", 0)


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
        print("MISSING  agulhas do walker 0024D5BC nao encontradas", file=sys.stderr)
        return 2
    print("patch_24d5bc_walker_probe: applied=%d already=%d sites=%d"
          % (applied, already, sites))
    return 0


if __name__ == "__main__":
    sys.exit(main())
